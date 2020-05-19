// Copyright 2019 Joe Drago. All rights reserved.
// SPDX-License-Identifier: BSD-2-Clause

#include "avif/internal.h"

#include <string.h>

#define MAX_ASSOCIATIONS 16
struct ipmaArray
{
    uint8_t associations[MAX_ASSOCIATIONS];
    avifBool essential[MAX_ASSOCIATIONS];
    uint8_t count;
};
static void ipmaPush(struct ipmaArray * ipma, uint8_t assoc, avifBool essential)
{
    ipma->associations[ipma->count] = assoc;
    ipma->essential[ipma->count] = essential;
    ++ipma->count;
}

static const char alphaURN[] = URN_ALPHA0;
static const size_t alphaURNSize = sizeof(alphaURN);

static const char xmpContentType[] = CONTENT_TYPE_XMP;
static const size_t xmpContentTypeSize = sizeof(xmpContentType);

static avifBool avifImageIsOpaque(const avifImage * image);
static void fillConfigBox(avifCodec * codec, const avifImage * image, avifBool alpha);
static void writeConfigBox(avifRWStream * s, avifCodecConfigurationBox * cfg);

// ---------------------------------------------------------------------------
// avifEncoderItem

// one "item" worth for encoder
typedef struct avifEncoderItem
{
    uint16_t id;
    uint8_t type[4];
    const avifImage * image; // avifImage* to use when encoding or populating ipma for this item (unowned)
    avifCodec * codec;       // only present on type==av01
    avifRWData content;      // OBU data on av01, metadata payload for Exif/XMP
    avifBool alpha;

    const char * infeName;
    size_t infeNameSize;
    const char * infeContentType;
    size_t infeContentTypeSize;
    size_t infeOffsetOffset; // Stream offset where infe offset was written, so it can be properly set after mdat is written

    uint16_t irefToID; // if non-zero, make an iref from this id -> irefToID
    const char * irefType;

    struct ipmaArray ipma;
} avifEncoderItem;
AVIF_ARRAY_DECLARE(avifEncoderItemArray, avifEncoderItem, item);

// ---------------------------------------------------------------------------
// avifEncoderData

typedef struct avifEncoderData
{
    avifEncoderItemArray items;
    uint16_t lastItemID;
    uint16_t primaryItemID;
} avifEncoderData;

static avifEncoderData * avifEncoderDataCreate()
{
    avifEncoderData * data = (avifEncoderData *)avifAlloc(sizeof(avifEncoderData));
    memset(data, 0, sizeof(avifEncoderData));
    avifArrayCreate(&data->items, sizeof(avifEncoderItem), 8);
    return data;
}

static avifEncoderItem * avifEncoderDataCreateItem(avifEncoderData * data, const char * type, const char * infeName, size_t infeNameSize)
{
    avifEncoderItem * item = (avifEncoderItem *)avifArrayPushPtr(&data->items);
    ++data->lastItemID;
    item->id = data->lastItemID;
    memcpy(item->type, type, sizeof(item->type));
    item->infeName = infeName;
    item->infeNameSize = infeNameSize;
    return item;
}

static void avifEncoderDataDestroy(avifEncoderData * data)
{
    for (uint32_t i = 0; i < data->items.count; ++i) {
        avifEncoderItem * item = &data->items.item[i];
        if (item->codec) {
            avifCodecDestroy(item->codec);
        }
        avifRWDataFree(&item->content);
    }
    avifArrayDestroy(&data->items);
    avifFree(data);
}

// ---------------------------------------------------------------------------

avifEncoder * avifEncoderCreate(void)
{
    avifEncoder * encoder = (avifEncoder *)avifAlloc(sizeof(avifEncoder));
    memset(encoder, 0, sizeof(avifEncoder));
    encoder->maxThreads = 1;
    encoder->minQuantizer = AVIF_QUANTIZER_LOSSLESS;
    encoder->maxQuantizer = AVIF_QUANTIZER_LOSSLESS;
    encoder->minQuantizerAlpha = AVIF_QUANTIZER_LOSSLESS;
    encoder->maxQuantizerAlpha = AVIF_QUANTIZER_LOSSLESS;
    encoder->tileRowsLog2 = 0;
    encoder->tileColsLog2 = 0;
    encoder->speed = AVIF_SPEED_DEFAULT;
    encoder->data = avifEncoderDataCreate();
    return encoder;
}

void avifEncoderDestroy(avifEncoder * encoder)
{
    avifEncoderDataDestroy(encoder->data);
    avifFree(encoder);
}

avifResult avifEncoderWrite(avifEncoder * encoder, const avifImage * image, avifRWData * output)
{
    if ((image->depth != 8) && (image->depth != 10) && (image->depth != 12)) {
        return AVIF_RESULT_UNSUPPORTED_DEPTH;
    }

    avifResult result = AVIF_RESULT_UNKNOWN_ERROR;

    avifEncoderItem * colorItem = avifEncoderDataCreateItem(encoder->data, "av01", "Color", 6);
    colorItem->image = image;
    colorItem->codec = avifCodecCreate(encoder->codecChoice, AVIF_CODEC_FLAG_CAN_ENCODE);
    if (!colorItem->codec) {
        // Just bail out early, we're not surviving this function without an encoder compiled in
        return AVIF_RESULT_NO_CODEC_AVAILABLE;
    }
    encoder->data->primaryItemID = colorItem->id;

    avifBool imageIsOpaque = avifImageIsOpaque(image);
    if (!imageIsOpaque) {
        avifEncoderItem * alphaItem = avifEncoderDataCreateItem(encoder->data, "av01", "Alpha", 6);
        alphaItem->image = image;
        alphaItem->codec = avifCodecCreate(encoder->codecChoice, AVIF_CODEC_FLAG_CAN_ENCODE);
        if (!alphaItem->codec) {
            return AVIF_RESULT_NO_CODEC_AVAILABLE;
        }
        alphaItem->alpha = AVIF_TRUE;
        alphaItem->irefToID = encoder->data->primaryItemID;
        alphaItem->irefType = "auxl";
    }

    // -----------------------------------------------------------------------
    // Create metadata items (Exif, XMP)

    if (image->exif.size > 0) {
        // Validate Exif payload (if any) and find TIFF header offset
        uint32_t exifTiffHeaderOffset = 0;
        if (image->exif.size > 0) {
            if (image->exif.size < 4) {
                // Can't even fit the TIFF header, something is wrong
                return AVIF_RESULT_INVALID_EXIF_PAYLOAD;
            }

            const uint8_t tiffHeaderBE[4] = { 'M', 'M', 0, 42 };
            const uint8_t tiffHeaderLE[4] = { 'I', 'I', 42, 0 };
            for (; exifTiffHeaderOffset < (image->exif.size - 4); ++exifTiffHeaderOffset) {
                if (!memcmp(&image->exif.data[exifTiffHeaderOffset], tiffHeaderBE, sizeof(tiffHeaderBE))) {
                    break;
                }
                if (!memcmp(&image->exif.data[exifTiffHeaderOffset], tiffHeaderLE, sizeof(tiffHeaderLE))) {
                    break;
                }
            }

            if (exifTiffHeaderOffset >= image->exif.size - 4) {
                // Couldn't find the TIFF header
                return AVIF_RESULT_INVALID_EXIF_PAYLOAD;
            }
        }

        avifEncoderItem * exifItem = avifEncoderDataCreateItem(encoder->data, "Exif", "Exif", 5);
        exifItem->irefToID = encoder->data->primaryItemID;
        exifItem->irefType = "cdsc";

        avifRWDataRealloc(&exifItem->content, sizeof(uint32_t) + image->exif.size);
        exifTiffHeaderOffset = avifHTONL(exifTiffHeaderOffset);
        memcpy(exifItem->content.data, &exifTiffHeaderOffset, sizeof(uint32_t));
        memcpy(exifItem->content.data + sizeof(uint32_t), image->exif.data, image->exif.size);
    }

    if (image->xmp.size > 0) {
        avifEncoderItem * xmpItem = avifEncoderDataCreateItem(encoder->data, "mime", "XMP", 4);
        xmpItem->irefToID = encoder->data->primaryItemID;
        xmpItem->irefType = "cdsc";

        xmpItem->infeContentType = xmpContentType;
        xmpItem->infeContentTypeSize = xmpContentTypeSize;
        avifRWDataSet(&xmpItem->content, image->xmp.data, image->xmp.size);
    }

    // -----------------------------------------------------------------------
    // Pre-fill config boxes based on image (codec can query/update later)

    for (uint32_t itemIndex = 0; itemIndex < encoder->data->items.count; ++itemIndex) {
        avifEncoderItem * item = &encoder->data->items.item[itemIndex];
        if (item->codec && item->image) {
            fillConfigBox(item->codec, item->image, item->alpha);
        }
    }

    // -----------------------------------------------------------------------
    // Begin write stream

    avifRWStream s;
    avifRWStreamStart(&s, output);

    // -----------------------------------------------------------------------
    // Validate image

    if (!image->width || !image->height || !image->yuvPlanes[AVIF_CHAN_Y]) {
        result = AVIF_RESULT_NO_CONTENT;
        goto writeCleanup;
    }

    if (image->yuvFormat == AVIF_PIXEL_FORMAT_NONE) {
        result = AVIF_RESULT_NO_YUV_FORMAT_SELECTED;
        goto writeCleanup;
    }

    // -----------------------------------------------------------------------
    // Encode AV1 OBUs

    for (uint32_t itemIndex = 0; itemIndex < encoder->data->items.count; ++itemIndex) {
        avifEncoderItem * item = &encoder->data->items.item[itemIndex];
        if (item->codec && item->image) {
            if (!item->codec->encodeImage(item->codec, item->image, encoder, &item->content, item->alpha)) {
                result = item->alpha ? AVIF_RESULT_ENCODE_ALPHA_FAILED : AVIF_RESULT_ENCODE_COLOR_FAILED;
                goto writeCleanup;
            }

            // TODO: rethink this if/when image grid encoding support is added
            if (item->alpha) {
                encoder->ioStats.alphaOBUSize = item->content.size;
            } else {
                encoder->ioStats.colorOBUSize = item->content.size;
            }
        }
    }

    // -----------------------------------------------------------------------
    // Write ftyp

    avifBoxMarker ftyp = avifRWStreamWriteBox(&s, "ftyp", -1, 0);
    avifRWStreamWriteChars(&s, "avif", 4);                         // unsigned int(32) major_brand;
    avifRWStreamWriteU32(&s, 0);                                   // unsigned int(32) minor_version;
    avifRWStreamWriteChars(&s, "avif", 4);                         // unsigned int(32) compatible_brands[];
    avifRWStreamWriteChars(&s, "mif1", 4);                         // ... compatible_brands[]
    avifRWStreamWriteChars(&s, "miaf", 4);                         // ... compatible_brands[]
    if ((image->depth == 8) || (image->depth == 10)) {             //
        if (image->yuvFormat == AVIF_PIXEL_FORMAT_YUV420) {        //
            avifRWStreamWriteChars(&s, "MA1B", 4);                 // ... compatible_brands[]
        } else if (image->yuvFormat == AVIF_PIXEL_FORMAT_YUV444) { //
            avifRWStreamWriteChars(&s, "MA1A", 4);                 // ... compatible_brands[]
        }
    }
    avifRWStreamFinishBox(&s, ftyp);

    // -----------------------------------------------------------------------
    // Start meta

    avifBoxMarker meta = avifRWStreamWriteBox(&s, "meta", 0, 0);

    // -----------------------------------------------------------------------
    // Write hdlr

    avifBoxMarker hdlr = avifRWStreamWriteBox(&s, "hdlr", 0, 0);
    avifRWStreamWriteU32(&s, 0);              // unsigned int(32) pre_defined = 0;
    avifRWStreamWriteChars(&s, "pict", 4);    // unsigned int(32) handler_type;
    avifRWStreamWriteZeros(&s, 12);           // const unsigned int(32)[3] reserved = 0;
    avifRWStreamWriteChars(&s, "libavif", 8); // string name; (writing null terminator)
    avifRWStreamFinishBox(&s, hdlr);

    // -----------------------------------------------------------------------
    // Write pitm

    if (encoder->data->primaryItemID != 0) {
        avifRWStreamWriteBox(&s, "pitm", 0, sizeof(uint16_t));
        avifRWStreamWriteU16(&s, encoder->data->primaryItemID); //  unsigned int(16) item_ID;
    }

    // -----------------------------------------------------------------------
    // Write iloc

    avifBoxMarker iloc = avifRWStreamWriteBox(&s, "iloc", 0, 0);

    uint8_t offsetSizeAndLengthSize = (4 << 4) + (4 << 0);          // unsigned int(4) offset_size;
                                                                    // unsigned int(4) length_size;
    avifRWStreamWrite(&s, &offsetSizeAndLengthSize, 1);             //
    avifRWStreamWriteZeros(&s, 1);                                  // unsigned int(4) base_offset_size;
                                                                    // unsigned int(4) reserved;
    avifRWStreamWriteU16(&s, (uint16_t)encoder->data->items.count); // unsigned int(16) item_count;

    for (uint32_t itemIndex = 0; itemIndex < encoder->data->items.count; ++itemIndex) {
        avifEncoderItem * item = &encoder->data->items.item[itemIndex];
        avifRWStreamWriteU16(&s, item->id);                     // unsigned int(16) item_ID;
        avifRWStreamWriteU16(&s, 0);                            // unsigned int(16) data_reference_index;
        avifRWStreamWriteU16(&s, 1);                            // unsigned int(16) extent_count;
        item->infeOffsetOffset = avifRWStreamOffset(&s);        //
        avifRWStreamWriteU32(&s, 0 /* set later */);            // unsigned int(offset_size*8) extent_offset;
        avifRWStreamWriteU32(&s, (uint32_t)item->content.size); // unsigned int(length_size*8) extent_length;
    }

    avifRWStreamFinishBox(&s, iloc);

    // -----------------------------------------------------------------------
    // Write iinf

    avifBoxMarker iinf = avifRWStreamWriteBox(&s, "iinf", 0, 0);
    avifRWStreamWriteU16(&s, (uint16_t)encoder->data->items.count); //  unsigned int(16) entry_count;

    for (uint32_t itemIndex = 0; itemIndex < encoder->data->items.count; ++itemIndex) {
        avifEncoderItem * item = &encoder->data->items.item[itemIndex];

        avifBoxMarker infe = avifRWStreamWriteBox(&s, "infe", 2, 0);
        avifRWStreamWriteU16(&s, item->id);                             // unsigned int(16) item_ID;
        avifRWStreamWriteU16(&s, 0);                                    // unsigned int(16) item_protection_index;
        avifRWStreamWrite(&s, item->type, 4);                           // unsigned int(32) item_type;
        avifRWStreamWriteChars(&s, item->infeName, item->infeNameSize); // string item_name; (writing null terminator)
        if (item->infeContentType && item->infeContentTypeSize) {       // string content_type; (writing null terminator)
            avifRWStreamWriteChars(&s, item->infeContentType, item->infeContentTypeSize);
        }
        avifRWStreamFinishBox(&s, infe);
    }

    avifRWStreamFinishBox(&s, iinf);

    // -----------------------------------------------------------------------
    // Write iref boxes

    for (uint32_t itemIndex = 0; itemIndex < encoder->data->items.count; ++itemIndex) {
        avifEncoderItem * item = &encoder->data->items.item[itemIndex];
        if (item->irefToID != 0) {
            avifBoxMarker iref = avifRWStreamWriteBox(&s, "iref", 0, 0);
            avifBoxMarker refType = avifRWStreamWriteBox(&s, item->irefType, -1, 0);
            avifRWStreamWriteU16(&s, item->id);       // unsigned int(16) from_item_ID;
            avifRWStreamWriteU16(&s, 1);              // unsigned int(16) reference_count;
            avifRWStreamWriteU16(&s, item->irefToID); // unsigned int(16) to_item_ID;
            avifRWStreamFinishBox(&s, refType);
            avifRWStreamFinishBox(&s, iref);
        }
    }

    // -----------------------------------------------------------------------
    // Write iprp -> ipco/ipma

    avifBoxMarker iprp = avifRWStreamWriteBox(&s, "iprp", -1, 0);

    uint8_t itemPropertyIndex = 0;
    avifBoxMarker ipco = avifRWStreamWriteBox(&s, "ipco", -1, 0);
    for (uint32_t itemIndex = 0; itemIndex < encoder->data->items.count; ++itemIndex) {
        avifEncoderItem * item = &encoder->data->items.item[itemIndex];
        memset(&item->ipma, 0, sizeof(item->ipma));
        if (!item->image || !item->codec) {
            // No ipma to write for this item
            continue;
        }

        // Properties all av01 items need

        avifBoxMarker ispe = avifRWStreamWriteBox(&s, "ispe", 0, 0);
        avifRWStreamWriteU32(&s, item->image->width);  // unsigned int(32) image_width;
        avifRWStreamWriteU32(&s, item->image->height); // unsigned int(32) image_height;
        avifRWStreamFinishBox(&s, ispe);
        ipmaPush(&item->ipma, ++itemPropertyIndex, AVIF_FALSE); // ipma is 1-indexed, doing this afterwards is correct

        uint8_t channelCount = item->alpha ? 1 : 3; // TODO: write the correct value here when adding monochrome support
        avifBoxMarker pixi = avifRWStreamWriteBox(&s, "pixi", 0, 0);
        avifRWStreamWriteU8(&s, channelCount); // unsigned int (8) num_channels;
        for (uint8_t chan = 0; chan < channelCount; ++chan) {
            avifRWStreamWriteU8(&s, (uint8_t)item->image->depth); // unsigned int (8) bits_per_channel;
        }
        avifRWStreamFinishBox(&s, pixi);
        ipmaPush(&item->ipma, ++itemPropertyIndex, AVIF_FALSE);

        writeConfigBox(&s, &item->codec->configBox);
        ipmaPush(&item->ipma, ++itemPropertyIndex, AVIF_TRUE);

        if (item->alpha) {
            // Alpha specific properties

            avifBoxMarker auxC = avifRWStreamWriteBox(&s, "auxC", 0, 0);
            avifRWStreamWriteChars(&s, alphaURN, alphaURNSize); //  string aux_type;
            avifRWStreamFinishBox(&s, auxC);
            ipmaPush(&item->ipma, ++itemPropertyIndex, AVIF_FALSE);
        } else {
            // Color specific properties

            if (item->image->icc.data && (item->image->icc.size > 0)) {
                avifBoxMarker colr = avifRWStreamWriteBox(&s, "colr", -1, 0);
                avifRWStreamWriteChars(&s, "prof", 4); // unsigned int(32) colour_type;
                avifRWStreamWrite(&s, item->image->icc.data, item->image->icc.size);
                avifRWStreamFinishBox(&s, colr);
                ipmaPush(&item->ipma, ++itemPropertyIndex, AVIF_FALSE);
            } else {
                avifBoxMarker colr = avifRWStreamWriteBox(&s, "colr", -1, 0);
                avifRWStreamWriteChars(&s, "nclx", 4);                                    // unsigned int(32) colour_type;
                avifRWStreamWriteU16(&s, (uint16_t)item->image->colorPrimaries);          // unsigned int(16) colour_primaries;
                avifRWStreamWriteU16(&s, (uint16_t)item->image->transferCharacteristics); // unsigned int(16) transfer_characteristics;
                avifRWStreamWriteU16(&s, (uint16_t)item->image->matrixCoefficients);      // unsigned int(16) matrix_coefficients;
                avifRWStreamWriteU8(&s, (item->image->yuvRange == AVIF_RANGE_FULL) ? 0x80 : 0); // unsigned int(1) full_range_flag;
                                                                                                // unsigned int(7) reserved = 0;
                avifRWStreamFinishBox(&s, colr);
                ipmaPush(&item->ipma, ++itemPropertyIndex, AVIF_FALSE);
            }

            // Write (Optional) Transformations
            if (item->image->transformFlags & AVIF_TRANSFORM_PASP) {
                avifBoxMarker pasp = avifRWStreamWriteBox(&s, "pasp", -1, 0);
                avifRWStreamWriteU32(&s, item->image->pasp.hSpacing); // unsigned int(32) hSpacing;
                avifRWStreamWriteU32(&s, item->image->pasp.vSpacing); // unsigned int(32) vSpacing;
                avifRWStreamFinishBox(&s, pasp);
                ipmaPush(&item->ipma, ++itemPropertyIndex, AVIF_FALSE);
            }
            if (item->image->transformFlags & AVIF_TRANSFORM_CLAP) {
                avifBoxMarker clap = avifRWStreamWriteBox(&s, "clap", -1, 0);
                avifRWStreamWriteU32(&s, item->image->clap.widthN);    // unsigned int(32) cleanApertureWidthN;
                avifRWStreamWriteU32(&s, item->image->clap.widthD);    // unsigned int(32) cleanApertureWidthD;
                avifRWStreamWriteU32(&s, item->image->clap.heightN);   // unsigned int(32) cleanApertureHeightN;
                avifRWStreamWriteU32(&s, item->image->clap.heightD);   // unsigned int(32) cleanApertureHeightD;
                avifRWStreamWriteU32(&s, item->image->clap.horizOffN); // unsigned int(32) horizOffN;
                avifRWStreamWriteU32(&s, item->image->clap.horizOffD); // unsigned int(32) horizOffD;
                avifRWStreamWriteU32(&s, item->image->clap.vertOffN);  // unsigned int(32) vertOffN;
                avifRWStreamWriteU32(&s, item->image->clap.vertOffD);  // unsigned int(32) vertOffD;
                avifRWStreamFinishBox(&s, clap);
                ipmaPush(&item->ipma, ++itemPropertyIndex, AVIF_TRUE);
            }
            if (item->image->transformFlags & AVIF_TRANSFORM_IROT) {
                avifBoxMarker irot = avifRWStreamWriteBox(&s, "irot", -1, 0);
                uint8_t angle = item->image->irot.angle & 0x3;
                avifRWStreamWrite(&s, &angle, 1); // unsigned int (6) reserved = 0; unsigned int (2) angle;
                avifRWStreamFinishBox(&s, irot);
                ipmaPush(&item->ipma, ++itemPropertyIndex, AVIF_TRUE);
            }
            if (item->image->transformFlags & AVIF_TRANSFORM_IMIR) {
                avifBoxMarker imir = avifRWStreamWriteBox(&s, "imir", -1, 0);
                uint8_t axis = item->image->imir.axis & 0x1;
                avifRWStreamWrite(&s, &axis, 1); // unsigned int (7) reserved = 0; unsigned int (1) axis;
                avifRWStreamFinishBox(&s, imir);
                ipmaPush(&item->ipma, ++itemPropertyIndex, AVIF_TRUE);
            }
        }
    }
    avifRWStreamFinishBox(&s, ipco);

    avifBoxMarker ipma = avifRWStreamWriteBox(&s, "ipma", 0, 0);
    {
        int ipmaCount = 0;
        for (uint32_t itemIndex = 0; itemIndex < encoder->data->items.count; ++itemIndex) {
            avifEncoderItem * item = &encoder->data->items.item[itemIndex];
            if (item->ipma.count > 0) {
                ++ipmaCount;
            }
        }
        avifRWStreamWriteU32(&s, ipmaCount); // unsigned int(32) entry_count;

        for (uint32_t itemIndex = 0; itemIndex < encoder->data->items.count; ++itemIndex) {
            avifEncoderItem * item = &encoder->data->items.item[itemIndex];
            if (item->ipma.count == 0) {
                continue;
            }

            avifRWStreamWriteU16(&s, item->id);          // unsigned int(16) item_ID;
            avifRWStreamWriteU8(&s, item->ipma.count);   // unsigned int(8) association_count;
            for (int i = 0; i < item->ipma.count; ++i) { //
                uint8_t essentialAndIndex = item->ipma.associations[i];
                if (item->ipma.essential[i]) {
                    essentialAndIndex |= 0x80;
                }
                avifRWStreamWriteU8(&s, essentialAndIndex); // bit(1) essential; unsigned int(7) property_index;
            }
        }
    }
    avifRWStreamFinishBox(&s, ipma);

    avifRWStreamFinishBox(&s, iprp);

    // -----------------------------------------------------------------------
    // Finish meta box

    avifRWStreamFinishBox(&s, meta);

    // -----------------------------------------------------------------------
    // Write mdat

    avifBoxMarker mdat = avifRWStreamWriteBox(&s, "mdat", -1, 0);
    for (uint32_t itemIndex = 0; itemIndex < encoder->data->items.count; ++itemIndex) {
        avifEncoderItem * item = &encoder->data->items.item[itemIndex];
        if (item->content.size == 0) {
            continue;
        }

        uint32_t infeOffset = (uint32_t)s.offset;
        avifRWStreamWrite(&s, item->content.data, item->content.size);

        if (item->infeOffsetOffset != 0) {
            size_t prevOffset = avifRWStreamOffset(&s);
            avifRWStreamSetOffset(&s, item->infeOffsetOffset);
            avifRWStreamWriteU32(&s, infeOffset);
            avifRWStreamSetOffset(&s, prevOffset);
        }
    }
    avifRWStreamFinishBox(&s, mdat);

    // -----------------------------------------------------------------------
    // Finish up stream

    avifRWStreamFinishWrite(&s);

    // -----------------------------------------------------------------------
    // Set result and cleanup

    result = AVIF_RESULT_OK;

writeCleanup:
    return result;
}

static avifBool avifImageIsOpaque(const avifImage * image)
{
    if (!image->alphaPlane) {
        return AVIF_TRUE;
    }

    int maxChannel = (1 << image->depth) - 1;
    if (avifImageUsesU16(image)) {
        for (uint32_t j = 0; j < image->height; ++j) {
            for (uint32_t i = 0; i < image->width; ++i) {
                uint16_t * p = (uint16_t *)&image->alphaPlane[(i * 2) + (j * image->alphaRowBytes)];
                if (*p != maxChannel) {
                    return AVIF_FALSE;
                }
            }
        }
    } else {
        for (uint32_t j = 0; j < image->height; ++j) {
            for (uint32_t i = 0; i < image->width; ++i) {
                if (image->alphaPlane[i + (j * image->alphaRowBytes)] != maxChannel) {
                    return AVIF_FALSE;
                }
            }
        }
    }
    return AVIF_TRUE;
}

static void fillConfigBox(avifCodec * codec, const avifImage * image, avifBool alpha)
{
    avifPixelFormatInfo formatInfo;
    avifGetPixelFormatInfo(image->yuvFormat, &formatInfo);

    // Profile 0.  8-bit and 10-bit 4:2:0 and 4:0:0 only.
    // Profile 1.  8-bit and 10-bit 4:4:4
    // Profile 2.  8-bit and 10-bit 4:2:2
    //            12-bit  4:0:0, 4:2:2 and 4:4:4
    uint8_t seqProfile = 0;
    if (image->depth == 12) {
        // Only seqProfile 2 can handle 12 bit
        seqProfile = 2;
    } else {
        // 8-bit or 10-bit

        if (alpha) {
            seqProfile = 0;
        } else {
            switch (image->yuvFormat) {
                case AVIF_PIXEL_FORMAT_YUV444:
                    seqProfile = 1;
                    break;
                case AVIF_PIXEL_FORMAT_YUV422:
                    seqProfile = 2;
                    break;
                case AVIF_PIXEL_FORMAT_YUV420:
                    seqProfile = 0;
                    break;
                case AVIF_PIXEL_FORMAT_YV12:
                    seqProfile = 0;
                    break;
                case AVIF_PIXEL_FORMAT_NONE:
                default:
                    break;
            }
        }
    }

    // TODO: Choose correct value from Annex A.3 table: https://aomediacodec.github.io/av1-spec/av1-spec.pdf
    uint8_t seqLevelIdx0 = 31;
    if ((image->width <= 8192) && (image->height <= 4352) && ((image->width * image->height) <= 8912896)) {
        // Image is 5.1 compatible
        seqLevelIdx0 = 13; // 5.1
    }

    memset(&codec->configBox, 0, sizeof(avifCodecConfigurationBox));
    codec->configBox.seqProfile = seqProfile;
    codec->configBox.seqLevelIdx0 = seqLevelIdx0;
    codec->configBox.seqTier0 = 0;
    codec->configBox.highBitdepth = (image->depth > 8) ? 1 : 0;
    codec->configBox.twelveBit = (image->depth == 12) ? 1 : 0;
    codec->configBox.monochrome = alpha ? 1 : 0;
    codec->configBox.chromaSubsamplingX = (uint8_t)formatInfo.chromaShiftX;
    codec->configBox.chromaSubsamplingY = (uint8_t)formatInfo.chromaShiftY;

    // TODO: choose the correct one from below:
    //   * 0 - CSP_UNKNOWN   Unknown (in this case the source video transfer function must be signaled outside the AV1 bitstream)
    //   * 1 - CSP_VERTICAL  Horizontally co-located with (0, 0) luma sample, vertical position in the middle between two luma samples
    //   * 2 - CSP_COLOCATED co-located with (0, 0) luma sample
    //   * 3 - CSP_RESERVED
    codec->configBox.chromaSamplePosition = 0;
}

static void writeConfigBox(avifRWStream * s, avifCodecConfigurationBox * cfg)
{
    avifBoxMarker av1C = avifRWStreamWriteBox(s, "av1C", -1, 0);

    // unsigned int (1) marker = 1;
    // unsigned int (7) version = 1;
    avifRWStreamWriteU8(s, 0x80 | 0x1);

    // unsigned int (3) seq_profile;
    // unsigned int (5) seq_level_idx_0;
    avifRWStreamWriteU8(s, (uint8_t)((cfg->seqProfile & 0x7) << 5) | (uint8_t)(cfg->seqLevelIdx0 & 0x1f));

    uint8_t bits = 0;
    bits |= (cfg->seqTier0 & 0x1) << 7;           // unsigned int (1) seq_tier_0;
    bits |= (cfg->highBitdepth & 0x1) << 6;       // unsigned int (1) high_bitdepth;
    bits |= (cfg->twelveBit & 0x1) << 5;          // unsigned int (1) twelve_bit;
    bits |= (cfg->monochrome & 0x1) << 4;         // unsigned int (1) monochrome;
    bits |= (cfg->chromaSubsamplingX & 0x1) << 3; // unsigned int (1) chroma_subsampling_x;
    bits |= (cfg->chromaSubsamplingY & 0x1) << 2; // unsigned int (1) chroma_subsampling_y;
    bits |= (cfg->chromaSamplePosition & 0x3);    // unsigned int (2) chroma_sample_position;
    avifRWStreamWriteU8(s, bits);

    // unsigned int (3) reserved = 0;
    // unsigned int (1) initial_presentation_delay_present;
    // if (initial_presentation_delay_present) {
    //   unsigned int (4) initial_presentation_delay_minus_one;
    // } else {
    //   unsigned int (4) reserved = 0;
    // }
    avifRWStreamWriteU8(s, 0);

    avifRWStreamFinishBox(s, av1C);
}
