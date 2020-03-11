// Copyright 2019 Joe Drago. All rights reserved.
// SPDX-License-Identifier: BSD-2-Clause

#include "avif/internal.h"

#include <string.h>

#define MAX_ASSOCIATIONS 16
struct ipmaArray
{
    uint8_t associations[MAX_ASSOCIATIONS];
    uint8_t count;
};
static void ipmaPush(struct ipmaArray * ipma, uint8_t assoc)
{
    ipma->associations[ipma->count] = assoc;
    ++ipma->count;
}

static const char alphaURN[] = URN_ALPHA0;
static const size_t alphaURNSize = sizeof(alphaURN);

static const char xmpContentType[] = CONTENT_TYPE_XMP;
static const size_t xmpContentTypeSize = sizeof(xmpContentType);

static avifBool avifImageIsOpaque(avifImage * image);
static void fillConfigBox(avifCodec * codec, avifImage * image, avifBool alpha);
static void writeConfigBox(avifRWStream * s, avifCodecConfigurationBox * cfg);

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
    return encoder;
}

void avifEncoderDestroy(avifEncoder * encoder)
{
    avifFree(encoder);
}

avifResult avifEncoderWrite(avifEncoder * encoder, avifImage * image, avifRWData * output)
{
    if ((image->depth != 8) && (image->depth != 10) && (image->depth != 12)) {
        return AVIF_RESULT_UNSUPPORTED_DEPTH;
    }

    avifResult result = AVIF_RESULT_UNKNOWN_ERROR;
    avifRWData colorOBU = AVIF_DATA_EMPTY;
    avifRWData alphaOBU = AVIF_DATA_EMPTY;
    avifCodec * codec[AVIF_CODEC_PLANES_COUNT];

    codec[AVIF_CODEC_PLANES_COLOR] = avifCodecCreate(encoder->codecChoice, AVIF_CODEC_FLAG_CAN_ENCODE);
    if (!codec[AVIF_CODEC_PLANES_COLOR]) {
        // Just bail out early, we're not surviving this function without an encoder compiled in
        return AVIF_RESULT_NO_CODEC_AVAILABLE;
    }

    avifBool imageIsOpaque = avifImageIsOpaque(image);
    if (imageIsOpaque) {
        codec[AVIF_CODEC_PLANES_ALPHA] = NULL;
    } else {
        codec[AVIF_CODEC_PLANES_ALPHA] = avifCodecCreate(encoder->codecChoice, AVIF_CODEC_FLAG_CAN_ENCODE);
        if (!codec[AVIF_CODEC_PLANES_ALPHA]) {
            return AVIF_RESULT_NO_CODEC_AVAILABLE;
        }
    }

    // -----------------------------------------------------------------------
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

    // -----------------------------------------------------------------------
    // Pre-fill config boxes based on image (codec can query/update later)

    fillConfigBox(codec[AVIF_CODEC_PLANES_COLOR], image, AVIF_FALSE);
    if (codec[AVIF_CODEC_PLANES_ALPHA]) {
        fillConfigBox(codec[AVIF_CODEC_PLANES_ALPHA], image, AVIF_TRUE);
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

    if (!codec[AVIF_CODEC_PLANES_COLOR]->encodeImage(codec[AVIF_CODEC_PLANES_COLOR], image, encoder, &colorOBU, AVIF_FALSE)) {
        result = AVIF_RESULT_ENCODE_COLOR_FAILED;
        goto writeCleanup;
    }

    if (!imageIsOpaque) {
        if (!codec[AVIF_CODEC_PLANES_ALPHA]->encodeImage(codec[AVIF_CODEC_PLANES_ALPHA], image, encoder, &alphaOBU, AVIF_TRUE)) {
            result = AVIF_RESULT_ENCODE_ALPHA_FAILED;
            goto writeCleanup;
        }
    }

    // TODO: consider collapsing all items into local structs for iteration / code sharing
    avifBool hasAlpha = (alphaOBU.size > 0);
    avifBool hasExif = (image->exif.size > 0);
    avifBool hasXMP = (image->xmp.size > 0);

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

    avifRWStreamWriteBox(&s, "pitm", 0, sizeof(uint16_t));
    avifRWStreamWriteU16(&s, 1); //  unsigned int(16) item_ID;

    // -----------------------------------------------------------------------
    // Calculate item IDs and counts

    uint16_t itemCount = 1;
    uint16_t colorItemID = 1;
    uint16_t nextItemID = 2;
    uint16_t alphaItemID = 0;
    uint16_t exifItemID = 0;
    uint16_t xmpItemID = 0;
    if (hasAlpha) {
        ++itemCount;
        alphaItemID = nextItemID;
        ++nextItemID;
    }
    if (hasExif) {
        ++itemCount;
        exifItemID = nextItemID;
        ++nextItemID;
    }
    if (hasXMP) {
        ++itemCount;
        xmpItemID = nextItemID;
        ++nextItemID;
    }

    // -----------------------------------------------------------------------
    // Write iloc

    // Remember where we want to store the offsets to the mdat OBU offsets to adjust them later.
    // These are named as such because they are remembering the stream offset where we will write an offset later.
    size_t colorOBUOffsetOffset = 0;
    size_t alphaOBUOffsetOffset = 0;
    size_t exifOffsetOffset = 0;
    size_t xmpOffsetOffset = 0;

    avifBoxMarker iloc = avifRWStreamWriteBox(&s, "iloc", 0, 0);

    // iloc header
    uint8_t offsetSizeAndLengthSize = (4 << 4) + (4 << 0); // unsigned int(4) offset_size;
                                                           // unsigned int(4) length_size;
    avifRWStreamWrite(&s, &offsetSizeAndLengthSize, 1);    //
    avifRWStreamWriteZeros(&s, 1);                         // unsigned int(4) base_offset_size;
                                                           // unsigned int(4) reserved;
    avifRWStreamWriteU16(&s, itemCount);                   // unsigned int(16) item_count;

    // Item ID #1 (Color OBU)
    avifRWStreamWriteU16(&s, colorItemID);             // unsigned int(16) item_ID;
    avifRWStreamWriteU16(&s, 0);                       // unsigned int(16) data_reference_index;
    avifRWStreamWriteU16(&s, 1);                       // unsigned int(16) extent_count;
    colorOBUOffsetOffset = avifRWStreamOffset(&s);     //
    avifRWStreamWriteU32(&s, 0 /* set later */);       // unsigned int(offset_size*8) extent_offset;
    avifRWStreamWriteU32(&s, (uint32_t)colorOBU.size); // unsigned int(length_size*8) extent_length;

    if (hasAlpha) {
        avifRWStreamWriteU16(&s, alphaItemID);             // unsigned int(16) item_ID;
        avifRWStreamWriteU16(&s, 0);                       // unsigned int(16) data_reference_index;
        avifRWStreamWriteU16(&s, 1);                       // unsigned int(16) extent_count;
        alphaOBUOffsetOffset = avifRWStreamOffset(&s);     //
        avifRWStreamWriteU32(&s, 0 /* set later */);       // unsigned int(offset_size*8) extent_offset;
        avifRWStreamWriteU32(&s, (uint32_t)alphaOBU.size); // unsigned int(length_size*8) extent_length;
    }

    if (hasExif) {
        // From ISO/IEC 23008-12:2017
        // :: aligned(8) class ExifDataBlock() {
        // ::     unsigned int(32) exif_tiff_header_offset;
        // ::     unsigned int(8) exif_payload[];
        // :: }
        uint32_t exifDataBlockSize = (uint32_t)(sizeof(uint32_t) + image->exif.size);

        avifRWStreamWriteU16(&s, exifItemID);        // unsigned int(16) item_ID;
        avifRWStreamWriteU16(&s, 0);                 // unsigned int(16) data_reference_index;
        avifRWStreamWriteU16(&s, 1);                 // unsigned int(16) extent_count;
        exifOffsetOffset = avifRWStreamOffset(&s);   //
        avifRWStreamWriteU32(&s, 0 /* set later */); // unsigned int(offset_size*8) extent_offset;
        avifRWStreamWriteU32(&s, exifDataBlockSize); // unsigned int(length_size*8) extent_length;
    }

    if (hasXMP) {
        avifRWStreamWriteU16(&s, xmpItemID);                 // unsigned int(16) item_ID;
        avifRWStreamWriteU16(&s, 0);                         // unsigned int(16) data_reference_index;
        avifRWStreamWriteU16(&s, 1);                         // unsigned int(16) extent_count;
        xmpOffsetOffset = avifRWStreamOffset(&s);            //
        avifRWStreamWriteU32(&s, 0 /* set later */);         // unsigned int(offset_size*8) extent_offset;
        avifRWStreamWriteU32(&s, (uint32_t)image->xmp.size); // unsigned int(length_size*8) extent_length;
    }

    avifRWStreamFinishBox(&s, iloc);

    // -----------------------------------------------------------------------
    // Write iinf

    avifBoxMarker iinf = avifRWStreamWriteBox(&s, "iinf", 0, 0);
    avifRWStreamWriteU16(&s, itemCount); //  unsigned int(16) entry_count;

    avifBoxMarker infeImage = avifRWStreamWriteBox(&s, "infe", 2, 0);
    avifRWStreamWriteU16(&s, colorItemID);  // unsigned int(16) item_ID;
    avifRWStreamWriteU16(&s, 0);            // unsigned int(16) item_protection_index;
    avifRWStreamWriteChars(&s, "av01", 4);  // unsigned int(32) item_type;
    avifRWStreamWriteChars(&s, "Color", 6); // string item_name; (writing null terminator)
    avifRWStreamFinishBox(&s, infeImage);
    if (hasAlpha) {
        avifBoxMarker infeAlpha = avifRWStreamWriteBox(&s, "infe", 2, 0);
        avifRWStreamWriteU16(&s, alphaItemID);  // unsigned int(16) item_ID;
        avifRWStreamWriteU16(&s, 0);            // unsigned int(16) item_protection_index;
        avifRWStreamWriteChars(&s, "av01", 4);  // unsigned int(32) item_type;
        avifRWStreamWriteChars(&s, "Alpha", 6); // string item_name; (writing null terminator)
        avifRWStreamFinishBox(&s, infeAlpha);
    }
    if (hasExif) {
        avifBoxMarker infeExif = avifRWStreamWriteBox(&s, "infe", 2, 0);
        avifRWStreamWriteU16(&s, exifItemID);  // unsigned int(16) item_ID;
        avifRWStreamWriteU16(&s, 0);           // unsigned int(16) item_protection_index;
        avifRWStreamWriteChars(&s, "Exif", 4); // unsigned int(32) item_type;
        avifRWStreamWriteChars(&s, "Exif", 5); // string item_name; (writing null terminator)
        avifRWStreamFinishBox(&s, infeExif);
    }
    if (hasXMP) {
        avifBoxMarker infeXMP = avifRWStreamWriteBox(&s, "infe", 2, 0);
        avifRWStreamWriteU16(&s, xmpItemID);                            // unsigned int(16) item_ID;
        avifRWStreamWriteU16(&s, 0);                                    // unsigned int(16) item_protection_index;
        avifRWStreamWriteChars(&s, "mime", 4);                          // unsigned int(32) item_type;
        avifRWStreamWriteChars(&s, "XMP", 4);                           // string item_name; (writing null terminator)
        avifRWStreamWriteChars(&s, xmpContentType, xmpContentTypeSize); // string content_type; (writing null terminator)
        avifRWStreamFinishBox(&s, infeXMP);
    }
    avifRWStreamFinishBox(&s, iinf);

    // -----------------------------------------------------------------------
    // Write iref (auxl) for alpha, if any

    if (hasAlpha) {
        avifBoxMarker iref = avifRWStreamWriteBox(&s, "iref", 0, 0);
        avifBoxMarker auxl = avifRWStreamWriteBox(&s, "auxl", -1, 0);
        avifRWStreamWriteU16(&s, alphaItemID); // unsigned int(16) from_item_ID;
        avifRWStreamWriteU16(&s, 1);           // unsigned int(16) reference_count;
        avifRWStreamWriteU16(&s, colorItemID); // unsigned int(16) to_item_ID;
        avifRWStreamFinishBox(&s, auxl);
        avifRWStreamFinishBox(&s, iref);
    }

    // -----------------------------------------------------------------------
    // Write iref (cdsc) for Exif, if any

    if (hasExif) {
        avifBoxMarker iref = avifRWStreamWriteBox(&s, "iref", 0, 0);
        avifBoxMarker cdsc = avifRWStreamWriteBox(&s, "cdsc", -1, 0);
        avifRWStreamWriteU16(&s, exifItemID);  // unsigned int(16) from_item_ID;
        avifRWStreamWriteU16(&s, 1);           // unsigned int(16) reference_count;
        avifRWStreamWriteU16(&s, colorItemID); // unsigned int(16) to_item_ID;
        avifRWStreamFinishBox(&s, cdsc);
        avifRWStreamFinishBox(&s, iref);
    }

    // -----------------------------------------------------------------------
    // Write iref (cdsc) for XMP, if any

    if (hasXMP) {
        avifBoxMarker iref = avifRWStreamWriteBox(&s, "iref", 0, 0);
        avifBoxMarker cdsc = avifRWStreamWriteBox(&s, "cdsc", -1, 0);
        avifRWStreamWriteU16(&s, xmpItemID);   // unsigned int(16) from_item_ID;
        avifRWStreamWriteU16(&s, 1);           // unsigned int(16) reference_count;
        avifRWStreamWriteU16(&s, colorItemID); // unsigned int(16) to_item_ID;
        avifRWStreamFinishBox(&s, cdsc);
        avifRWStreamFinishBox(&s, iref);
    }

    // -----------------------------------------------------------------------
    // Write iprp->ipco->ispe

    avifBoxMarker iprp = avifRWStreamWriteBox(&s, "iprp", -1, 0);
    {
        uint8_t ipcoIndex = 0;
        struct ipmaArray ipmaColor;
        memset(&ipmaColor, 0, sizeof(ipmaColor));
        struct ipmaArray ipmaAlpha;
        memset(&ipmaAlpha, 0, sizeof(ipmaAlpha));

        avifBoxMarker ipco = avifRWStreamWriteBox(&s, "ipco", -1, 0);
        {
            avifBoxMarker ispe = avifRWStreamWriteBox(&s, "ispe", 0, 0);
            avifRWStreamWriteU32(&s, image->width);  // unsigned int(32) image_width;
            avifRWStreamWriteU32(&s, image->height); // unsigned int(32) image_height;
            avifRWStreamFinishBox(&s, ispe);
            ++ipcoIndex;
            ipmaPush(&ipmaColor, ipcoIndex); // ipma is 1-indexed, doing this afterwards is correct
            ipmaPush(&ipmaAlpha, ipcoIndex); // Alpha shares the ispe prop

            if (image->profileFormat == AVIF_PROFILE_FORMAT_NCLX) {
                avifBoxMarker colr = avifRWStreamWriteBox(&s, "colr", -1, 0);
                avifRWStreamWriteChars(&s, "nclx", 4);                         // unsigned int(32) colour_type;
                avifRWStreamWriteU16(&s, image->nclx.colourPrimaries);         // unsigned int(16) colour_primaries;
                avifRWStreamWriteU16(&s, image->nclx.transferCharacteristics); // unsigned int(16) transfer_characteristics;
                avifRWStreamWriteU16(&s, image->nclx.matrixCoefficients);      // unsigned int(16) matrix_coefficients;
                avifRWStreamWriteU8(&s, image->nclx.fullRangeFlag & 0x80);     // unsigned int(1) full_range_flag;
                                                                               // unsigned int(7) reserved = 0;
                avifRWStreamFinishBox(&s, colr);
                ++ipcoIndex;
                ipmaPush(&ipmaColor, ipcoIndex);
            } else if ((image->profileFormat == AVIF_PROFILE_FORMAT_ICC) && image->icc.data && (image->icc.size > 0)) {
                avifBoxMarker colr = avifRWStreamWriteBox(&s, "colr", -1, 0);
                avifRWStreamWriteChars(&s, "prof", 4); // unsigned int(32) colour_type;
                avifRWStreamWrite(&s, image->icc.data, image->icc.size);
                avifRWStreamFinishBox(&s, colr);
                ++ipcoIndex;
                ipmaPush(&ipmaColor, ipcoIndex);
            }

            avifBoxMarker pixiC = avifRWStreamWriteBox(&s, "pixi", 0, 0);
            avifRWStreamWriteU8(&s, 3);                     // unsigned int (8) num_channels;
            avifRWStreamWriteU8(&s, (uint8_t)image->depth); // unsigned int (8) bits_per_channel;
            avifRWStreamWriteU8(&s, (uint8_t)image->depth); // unsigned int (8) bits_per_channel;
            avifRWStreamWriteU8(&s, (uint8_t)image->depth); // unsigned int (8) bits_per_channel;
            avifRWStreamFinishBox(&s, pixiC);
            ++ipcoIndex;
            ipmaPush(&ipmaColor, ipcoIndex);

            writeConfigBox(&s, &codec[AVIF_CODEC_PLANES_COLOR]->configBox);
            ++ipcoIndex;
            ipmaPush(&ipmaColor, ipcoIndex);

            // Write (Optional) Transformations
            if (image->transformFlags & AVIF_TRANSFORM_PASP) {
                avifBoxMarker pasp = avifRWStreamWriteBox(&s, "pasp", -1, 0);
                avifRWStreamWriteU32(&s, image->pasp.hSpacing); // unsigned int(32) hSpacing;
                avifRWStreamWriteU32(&s, image->pasp.vSpacing); // unsigned int(32) vSpacing;
                avifRWStreamFinishBox(&s, pasp);
                ++ipcoIndex;
                ipmaPush(&ipmaColor, ipcoIndex);
            }
            if (image->transformFlags & AVIF_TRANSFORM_CLAP) {
                avifBoxMarker clap = avifRWStreamWriteBox(&s, "clap", -1, 0);
                avifRWStreamWriteU32(&s, image->clap.widthN);    // unsigned int(32) cleanApertureWidthN;
                avifRWStreamWriteU32(&s, image->clap.widthD);    // unsigned int(32) cleanApertureWidthD;
                avifRWStreamWriteU32(&s, image->clap.heightN);   // unsigned int(32) cleanApertureHeightN;
                avifRWStreamWriteU32(&s, image->clap.heightD);   // unsigned int(32) cleanApertureHeightD;
                avifRWStreamWriteU32(&s, image->clap.horizOffN); // unsigned int(32) horizOffN;
                avifRWStreamWriteU32(&s, image->clap.horizOffD); // unsigned int(32) horizOffD;
                avifRWStreamWriteU32(&s, image->clap.vertOffN);  // unsigned int(32) vertOffN;
                avifRWStreamWriteU32(&s, image->clap.vertOffD);  // unsigned int(32) vertOffD;
                avifRWStreamFinishBox(&s, clap);
                ++ipcoIndex;
                ipmaPush(&ipmaColor, ipcoIndex);
            }
            if (image->transformFlags & AVIF_TRANSFORM_IROT) {
                avifBoxMarker irot = avifRWStreamWriteBox(&s, "irot", -1, 0);
                uint8_t angle = image->irot.angle & 0x3;
                avifRWStreamWrite(&s, &angle, 1); // unsigned int (6) reserved = 0; unsigned int (2) angle;
                avifRWStreamFinishBox(&s, irot);
                ++ipcoIndex;
                ipmaPush(&ipmaColor, ipcoIndex);
            }
            if (image->transformFlags & AVIF_TRANSFORM_IMIR) {
                avifBoxMarker imir = avifRWStreamWriteBox(&s, "imir", -1, 0);
                uint8_t axis = image->imir.axis & 0x1;
                avifRWStreamWrite(&s, &axis, 1); // unsigned int (7) reserved = 0; unsigned int (1) axis;
                avifRWStreamFinishBox(&s, imir);
                ++ipcoIndex;
                ipmaPush(&ipmaColor, ipcoIndex);
            }

            if (hasAlpha) {
                avifBoxMarker pixiA = avifRWStreamWriteBox(&s, "pixi", 0, 0);
                avifRWStreamWriteU8(&s, 1);                     // unsigned int (8) num_channels;
                avifRWStreamWriteU8(&s, (uint8_t)image->depth); // unsigned int (8) bits_per_channel;
                avifRWStreamFinishBox(&s, pixiA);
                ++ipcoIndex;
                ipmaPush(&ipmaAlpha, ipcoIndex);

                writeConfigBox(&s, &codec[AVIF_CODEC_PLANES_ALPHA]->configBox);
                ++ipcoIndex;
                ipmaPush(&ipmaAlpha, ipcoIndex);

                avifBoxMarker auxC = avifRWStreamWriteBox(&s, "auxC", 0, 0);
                avifRWStreamWriteChars(&s, alphaURN, alphaURNSize); //  string aux_type;
                avifRWStreamFinishBox(&s, auxC);
                ++ipcoIndex;
                ipmaPush(&ipmaAlpha, ipcoIndex);
            }
        }
        avifRWStreamFinishBox(&s, ipco);

        avifBoxMarker ipma = avifRWStreamWriteBox(&s, "ipma", 0, 0);
        {
            int ipmaCount = hasAlpha ? 2 : 1;
            avifRWStreamWriteU32(&s, ipmaCount); // unsigned int(32) entry_count;

            avifRWStreamWriteU16(&s, 1);                            // unsigned int(16) item_ID;
            avifRWStreamWriteU8(&s, ipmaColor.count);               // unsigned int(8) association_count;
            for (int i = 0; i < ipmaColor.count; ++i) {             //
                avifRWStreamWriteU8(&s, ipmaColor.associations[i]); // bit(1) essential; unsigned int(7) property_index;
            }

            if (hasAlpha) {
                avifRWStreamWriteU16(&s, 2);                            // unsigned int(16) item_ID;
                avifRWStreamWriteU8(&s, ipmaAlpha.count);               // unsigned int(8) association_count;
                for (int i = 0; i < ipmaAlpha.count; ++i) {             //
                    avifRWStreamWriteU8(&s, ipmaAlpha.associations[i]); // bit(1) essential; unsigned int(7) property_index;
                }
            }
        }
        avifRWStreamFinishBox(&s, ipma);
    }
    avifRWStreamFinishBox(&s, iprp);

    // -----------------------------------------------------------------------
    // Finish meta box

    avifRWStreamFinishBox(&s, meta);

    // -----------------------------------------------------------------------
    // Write mdat

    avifBoxMarker mdat = avifRWStreamWriteBox(&s, "mdat", -1, 0);
    uint32_t colorOBUOffset = (uint32_t)s.offset;
    avifRWStreamWrite(&s, colorOBU.data, colorOBU.size);
    uint32_t alphaOBUOffset = (uint32_t)s.offset;
    avifRWStreamWrite(&s, alphaOBU.data, alphaOBU.size);
    uint32_t exifOffset = (uint32_t)s.offset;
    if (image->exif.size > 0) {
        avifRWStreamWriteU32(&s, (uint32_t)exifTiffHeaderOffset); // unsigned int(32) exif_tiff_header_offset; (Annex A.2.1)
        avifRWStreamWrite(&s, image->exif.data, image->exif.size);
    }
    uint32_t xmpOffset = (uint32_t)s.offset;
    avifRWStreamWrite(&s, image->xmp.data, image->xmp.size);
    avifRWStreamFinishBox(&s, mdat);

    // -----------------------------------------------------------------------
    // Finish up stream

    // Set offsets needed in meta box based on where we eventually wrote mdat
    size_t prevOffset = avifRWStreamOffset(&s);
    if (colorOBUOffsetOffset != 0) {
        avifRWStreamSetOffset(&s, colorOBUOffsetOffset);
        avifRWStreamWriteU32(&s, colorOBUOffset);
    }
    if (alphaOBUOffsetOffset != 0) {
        avifRWStreamSetOffset(&s, alphaOBUOffsetOffset);
        avifRWStreamWriteU32(&s, alphaOBUOffset);
    }
    if (exifOffsetOffset != 0) {
        avifRWStreamSetOffset(&s, exifOffsetOffset);
        avifRWStreamWriteU32(&s, exifOffset);
    }
    if (xmpOffsetOffset != 0) {
        avifRWStreamSetOffset(&s, xmpOffsetOffset);
        avifRWStreamWriteU32(&s, xmpOffset);
    }
    avifRWStreamSetOffset(&s, prevOffset);

    // Close write stream
    avifRWStreamFinishWrite(&s);

    // -----------------------------------------------------------------------
    // IO stats

    encoder->ioStats.colorOBUSize = colorOBU.size;
    encoder->ioStats.alphaOBUSize = alphaOBU.size;

    // -----------------------------------------------------------------------
    // Set result and cleanup

    result = AVIF_RESULT_OK;

writeCleanup:
    if (codec[AVIF_CODEC_PLANES_COLOR]) {
        avifCodecDestroy(codec[AVIF_CODEC_PLANES_COLOR]);
    }
    if (codec[AVIF_CODEC_PLANES_ALPHA]) {
        avifCodecDestroy(codec[AVIF_CODEC_PLANES_ALPHA]);
    }
    avifRWDataFree(&colorOBU);
    avifRWDataFree(&alphaOBU);
    return result;
}

static avifBool avifImageIsOpaque(avifImage * image)
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

static void fillConfigBox(avifCodec * codec, avifImage * image, avifBool alpha)
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
