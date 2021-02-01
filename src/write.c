// Copyright 2019 Joe Drago. All rights reserved.
// SPDX-License-Identifier: BSD-2-Clause

#include "avif/internal.h"

#include <assert.h>
#include <string.h>
#include <time.h>

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

// Used to store offsets in meta boxes which need to point at mdat offsets that
// aren't known yet. When an item's mdat payload is written, all registered fixups
// will have this now-known offset "fixed up".
typedef struct avifOffsetFixup
{
    size_t offset;
} avifOffsetFixup;
AVIF_ARRAY_DECLARE(avifOffsetFixupArray, avifOffsetFixup, fixup);

static const char alphaURN[] = URN_ALPHA0;
static const size_t alphaURNSize = sizeof(alphaURN);

static const char xmpContentType[] = CONTENT_TYPE_XMP;
static const size_t xmpContentTypeSize = sizeof(xmpContentType);

static avifBool avifImageIsOpaque(const avifImage * image);
static void writeConfigBox(avifRWStream * s, avifCodecConfigurationBox * cfg);

// ---------------------------------------------------------------------------
// avifCodecEncodeOutput

avifCodecEncodeOutput * avifCodecEncodeOutputCreate(void)
{
    avifCodecEncodeOutput * encodeOutput = (avifCodecEncodeOutput *)avifAlloc(sizeof(avifCodecEncodeOutput));
    memset(encodeOutput, 0, sizeof(avifCodecEncodeOutput));
    avifArrayCreate(&encodeOutput->samples, sizeof(avifEncodeSample), 1);
    return encodeOutput;
}

void avifCodecEncodeOutputAddSample(avifCodecEncodeOutput * encodeOutput, const uint8_t * data, size_t len, avifBool sync)
{
    avifEncodeSample * sample = (avifEncodeSample *)avifArrayPushPtr(&encodeOutput->samples);
    avifRWDataSet(&sample->data, data, len);
    sample->sync = sync;
}

void avifCodecEncodeOutputDestroy(avifCodecEncodeOutput * encodeOutput)
{
    for (uint32_t sampleIndex = 0; sampleIndex < encodeOutput->samples.count; ++sampleIndex) {
        avifRWDataFree(&encodeOutput->samples.sample[sampleIndex].data);
    }
    avifArrayDestroy(&encodeOutput->samples);
    avifFree(encodeOutput);
}

// ---------------------------------------------------------------------------
// avifEncoderItem

// one "item" worth for encoder
typedef struct avifEncoderItem
{
    uint16_t id;
    uint8_t type[4];
    avifCodec * codec;                    // only present on type==av01
    avifCodecEncodeOutput * encodeOutput; // AV1 sample data
    avifRWData metadataPayload;           // Exif/XMP data
    avifCodecConfigurationBox av1C;       // Harvested in avifEncoderFinish(), if encodeOutput has samples
    uint32_t cellIndex;                   // Which row-major cell index corresponds to this item. ignored on non-av01 types
    avifBool alpha;

    const char * infeName;
    size_t infeNameSize;
    const char * infeContentType;
    size_t infeContentTypeSize;
    avifOffsetFixupArray mdatFixups;

    uint16_t irefToID; // if non-zero, make an iref from this id -> irefToID
    const char * irefType;

    uint32_t gridCols; // if non-zero (legal range [1-256]), this is a grid item
    uint32_t gridRows; // if non-zero (legal range [1-256]), this is a grid item

    uint16_t dimgFromID; // if non-zero, make an iref from dimgFromID -> this id

    struct ipmaArray ipma;
} avifEncoderItem;
AVIF_ARRAY_DECLARE(avifEncoderItemArray, avifEncoderItem, item);

// ---------------------------------------------------------------------------
// avifEncoderFrame

typedef struct avifEncoderFrame
{
    uint64_t durationInTimescales;
} avifEncoderFrame;
AVIF_ARRAY_DECLARE(avifEncoderFrameArray, avifEncoderFrame, frame);

// ---------------------------------------------------------------------------
// avifEncoderData

typedef struct avifEncoderData
{
    avifEncoderItemArray items;
    avifEncoderFrameArray frames;
    avifImage * imageMetadata;
    uint16_t lastItemID;
    uint16_t primaryItemID;
    avifBool singleImage; // if true, the AVIF_ADD_IMAGE_FLAG_SINGLE flag was set on the first call to avifEncoderAddImage()
    avifBool alphaPresent;
} avifEncoderData;

static avifEncoderData * avifEncoderDataCreate()
{
    avifEncoderData * data = (avifEncoderData *)avifAlloc(sizeof(avifEncoderData));
    memset(data, 0, sizeof(avifEncoderData));
    data->imageMetadata = avifImageCreateEmpty();
    avifArrayCreate(&data->items, sizeof(avifEncoderItem), 8);
    avifArrayCreate(&data->frames, sizeof(avifEncoderFrame), 1);
    return data;
}

static avifEncoderItem * avifEncoderDataCreateItem(avifEncoderData * data, const char * type, const char * infeName, size_t infeNameSize, uint32_t cellIndex)
{
    avifEncoderItem * item = (avifEncoderItem *)avifArrayPushPtr(&data->items);
    ++data->lastItemID;
    item->id = data->lastItemID;
    memcpy(item->type, type, sizeof(item->type));
    item->infeName = infeName;
    item->infeNameSize = infeNameSize;
    item->encodeOutput = avifCodecEncodeOutputCreate();
    item->cellIndex = cellIndex;
    avifArrayCreate(&item->mdatFixups, sizeof(avifOffsetFixup), 4);
    return item;
}

static avifEncoderItem * avifEncoderDataFindItemByID(avifEncoderData * data, uint16_t id)
{
    for (uint32_t itemIndex = 0; itemIndex < data->items.count; ++itemIndex) {
        avifEncoderItem * item = &data->items.item[itemIndex];
        if (item->id == id) {
            return item;
        }
    }
    return NULL;
}

static void avifEncoderDataDestroy(avifEncoderData * data)
{
    for (uint32_t i = 0; i < data->items.count; ++i) {
        avifEncoderItem * item = &data->items.item[i];
        if (item->codec) {
            avifCodecDestroy(item->codec);
        }
        avifCodecEncodeOutputDestroy(item->encodeOutput);
        avifRWDataFree(&item->metadataPayload);
        avifArrayDestroy(&item->mdatFixups);
    }
    avifImageDestroy(data->imageMetadata);
    avifArrayDestroy(&data->items);
    avifArrayDestroy(&data->frames);
    avifFree(data);
}

static void avifEncoderItemAddMdatFixup(avifEncoderItem * item, const avifRWStream * s)
{
    avifOffsetFixup * fixup = (avifOffsetFixup *)avifArrayPushPtr(&item->mdatFixups);
    fixup->offset = avifRWStreamOffset(s);
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
    encoder->keyframeInterval = 0;
    encoder->timescale = 1;
    encoder->data = avifEncoderDataCreate();
    encoder->csOptions = avifCodecSpecificOptionsCreate();
    return encoder;
}

void avifEncoderDestroy(avifEncoder * encoder)
{
    avifCodecSpecificOptionsDestroy(encoder->csOptions);
    avifEncoderDataDestroy(encoder->data);
    avifFree(encoder);
}

void avifEncoderSetCodecSpecificOption(avifEncoder * encoder, const char * key, const char * value)
{
    avifCodecSpecificOptionsSet(encoder->csOptions, key, value);
}

static void avifEncoderWriteColorProperties(avifRWStream * s, const avifImage * imageMetadata, struct ipmaArray * ipma, uint8_t * itemPropertyIndex)
{
    if (imageMetadata->icc.size > 0) {
        avifBoxMarker colr = avifRWStreamWriteBox(s, "colr", AVIF_BOX_SIZE_TBD);
        avifRWStreamWriteChars(s, "prof", 4); // unsigned int(32) colour_type;
        avifRWStreamWrite(s, imageMetadata->icc.data, imageMetadata->icc.size);
        avifRWStreamFinishBox(s, colr);
        if (ipma && itemPropertyIndex) {
            ipmaPush(ipma, ++(*itemPropertyIndex), AVIF_FALSE);
        }
    }

    // HEIF 6.5.5.1, from Amendment 3 allows multiple colr boxes: "at most one for a given value of colour type"
    // Therefore, *always* writing an nclx box, even if an a prof box was already written above.
    avifBoxMarker colr = avifRWStreamWriteBox(s, "colr", AVIF_BOX_SIZE_TBD);
    avifRWStreamWriteChars(s, "nclx", 4);                                            // unsigned int(32) colour_type;
    avifRWStreamWriteU16(s, imageMetadata->colorPrimaries);                          // unsigned int(16) colour_primaries;
    avifRWStreamWriteU16(s, imageMetadata->transferCharacteristics);                 // unsigned int(16) transfer_characteristics;
    avifRWStreamWriteU16(s, imageMetadata->matrixCoefficients);                      // unsigned int(16) matrix_coefficients;
    avifRWStreamWriteU8(s, (imageMetadata->yuvRange == AVIF_RANGE_FULL) ? 0x80 : 0); // unsigned int(1) full_range_flag;
                                                                                     // unsigned int(7) reserved = 0;
    avifRWStreamFinishBox(s, colr);
    if (ipma && itemPropertyIndex) {
        ipmaPush(ipma, ++(*itemPropertyIndex), AVIF_FALSE);
    }

    // Write (Optional) Transformations
    if (imageMetadata->transformFlags & AVIF_TRANSFORM_PASP) {
        avifBoxMarker pasp = avifRWStreamWriteBox(s, "pasp", AVIF_BOX_SIZE_TBD);
        avifRWStreamWriteU32(s, imageMetadata->pasp.hSpacing); // unsigned int(32) hSpacing;
        avifRWStreamWriteU32(s, imageMetadata->pasp.vSpacing); // unsigned int(32) vSpacing;
        avifRWStreamFinishBox(s, pasp);
        if (ipma && itemPropertyIndex) {
            ipmaPush(ipma, ++(*itemPropertyIndex), AVIF_FALSE);
        }
    }
    if (imageMetadata->transformFlags & AVIF_TRANSFORM_CLAP) {
        avifBoxMarker clap = avifRWStreamWriteBox(s, "clap", AVIF_BOX_SIZE_TBD);
        avifRWStreamWriteU32(s, imageMetadata->clap.widthN);    // unsigned int(32) cleanApertureWidthN;
        avifRWStreamWriteU32(s, imageMetadata->clap.widthD);    // unsigned int(32) cleanApertureWidthD;
        avifRWStreamWriteU32(s, imageMetadata->clap.heightN);   // unsigned int(32) cleanApertureHeightN;
        avifRWStreamWriteU32(s, imageMetadata->clap.heightD);   // unsigned int(32) cleanApertureHeightD;
        avifRWStreamWriteU32(s, imageMetadata->clap.horizOffN); // unsigned int(32) horizOffN;
        avifRWStreamWriteU32(s, imageMetadata->clap.horizOffD); // unsigned int(32) horizOffD;
        avifRWStreamWriteU32(s, imageMetadata->clap.vertOffN);  // unsigned int(32) vertOffN;
        avifRWStreamWriteU32(s, imageMetadata->clap.vertOffD);  // unsigned int(32) vertOffD;
        avifRWStreamFinishBox(s, clap);
        if (ipma && itemPropertyIndex) {
            ipmaPush(ipma, ++(*itemPropertyIndex), AVIF_TRUE);
        }
    }
    if (imageMetadata->transformFlags & AVIF_TRANSFORM_IROT) {
        avifBoxMarker irot = avifRWStreamWriteBox(s, "irot", AVIF_BOX_SIZE_TBD);
        uint8_t angle = imageMetadata->irot.angle & 0x3;
        avifRWStreamWrite(s, &angle, 1); // unsigned int (6) reserved = 0; unsigned int (2) angle;
        avifRWStreamFinishBox(s, irot);
        if (ipma && itemPropertyIndex) {
            ipmaPush(ipma, ++(*itemPropertyIndex), AVIF_TRUE);
        }
    }
    if (imageMetadata->transformFlags & AVIF_TRANSFORM_IMIR) {
        avifBoxMarker imir = avifRWStreamWriteBox(s, "imir", AVIF_BOX_SIZE_TBD);
        uint8_t axis = imageMetadata->imir.axis & 0x1;
        avifRWStreamWrite(s, &axis, 1); // unsigned int (7) reserved = 0; unsigned int (1) axis;
        avifRWStreamFinishBox(s, imir);
        if (ipma && itemPropertyIndex) {
            ipmaPush(ipma, ++(*itemPropertyIndex), AVIF_TRUE);
        }
    }
}

// Write unassociated metadata items (EXIF, XMP) to a small meta box inside of a trak box.
// These items are implicitly associated with the track they are contained within.
static void avifEncoderWriteTrackMetaBox(avifEncoder * encoder, avifRWStream * s)
{
    // Count how many non-av01 items (such as EXIF/XMP) are being written
    uint32_t metadataItemCount = 0;
    for (uint32_t itemIndex = 0; itemIndex < encoder->data->items.count; ++itemIndex) {
        avifEncoderItem * item = &encoder->data->items.item[itemIndex];
        if (memcmp(item->type, "av01", 4) != 0) {
            ++metadataItemCount;
        }
    }
    if (metadataItemCount == 0) {
        // Don't even bother writing the trak meta box
        return;
    }

    avifBoxMarker meta = avifRWStreamWriteFullBox(s, "meta", AVIF_BOX_SIZE_TBD, 0, 0);

    avifBoxMarker hdlr = avifRWStreamWriteFullBox(s, "hdlr", AVIF_BOX_SIZE_TBD, 0, 0);
    avifRWStreamWriteU32(s, 0);              // unsigned int(32) pre_defined = 0;
    avifRWStreamWriteChars(s, "pict", 4);    // unsigned int(32) handler_type;
    avifRWStreamWriteZeros(s, 12);           // const unsigned int(32)[3] reserved = 0;
    avifRWStreamWriteChars(s, "libavif", 8); // string name; (writing null terminator)
    avifRWStreamFinishBox(s, hdlr);

    avifBoxMarker iloc = avifRWStreamWriteFullBox(s, "iloc", AVIF_BOX_SIZE_TBD, 0, 0);
    uint8_t offsetSizeAndLengthSize = (4 << 4) + (4 << 0); // unsigned int(4) offset_size;
                                                           // unsigned int(4) length_size;
    avifRWStreamWrite(s, &offsetSizeAndLengthSize, 1);     //
    avifRWStreamWriteZeros(s, 1);                          // unsigned int(4) base_offset_size;
                                                           // unsigned int(4) reserved;
    avifRWStreamWriteU16(s, (uint16_t)metadataItemCount);  // unsigned int(16) item_count;
    for (uint32_t trakItemIndex = 0; trakItemIndex < encoder->data->items.count; ++trakItemIndex) {
        avifEncoderItem * item = &encoder->data->items.item[trakItemIndex];
        if (memcmp(item->type, "av01", 4) == 0) {
            // Skip over all non-metadata items
            continue;
        }

        avifRWStreamWriteU16(s, item->id);                             // unsigned int(16) item_ID;
        avifRWStreamWriteU16(s, 0);                                    // unsigned int(16) data_reference_index;
        avifRWStreamWriteU16(s, 1);                                    // unsigned int(16) extent_count;
        avifEncoderItemAddMdatFixup(item, s);                          //
        avifRWStreamWriteU32(s, 0 /* set later */);                    // unsigned int(offset_size*8) extent_offset;
        avifRWStreamWriteU32(s, (uint32_t)item->metadataPayload.size); // unsigned int(length_size*8) extent_length;
    }
    avifRWStreamFinishBox(s, iloc);

    avifBoxMarker iinf = avifRWStreamWriteFullBox(s, "iinf", AVIF_BOX_SIZE_TBD, 0, 0);
    avifRWStreamWriteU16(s, (uint16_t)metadataItemCount); //  unsigned int(16) entry_count;
    for (uint32_t trakItemIndex = 0; trakItemIndex < encoder->data->items.count; ++trakItemIndex) {
        avifEncoderItem * item = &encoder->data->items.item[trakItemIndex];
        if (memcmp(item->type, "av01", 4) == 0) {
            continue;
        }

        avifBoxMarker infe = avifRWStreamWriteFullBox(s, "infe", AVIF_BOX_SIZE_TBD, 2, 0);
        avifRWStreamWriteU16(s, item->id);                             // unsigned int(16) item_ID;
        avifRWStreamWriteU16(s, 0);                                    // unsigned int(16) item_protection_index;
        avifRWStreamWrite(s, item->type, 4);                           // unsigned int(32) item_type;
        avifRWStreamWriteChars(s, item->infeName, item->infeNameSize); // string item_name; (writing null terminator)
        if (item->infeContentType && item->infeContentTypeSize) {      // string content_type; (writing null terminator)
            avifRWStreamWriteChars(s, item->infeContentType, item->infeContentTypeSize);
        }
        avifRWStreamFinishBox(s, infe);
    }
    avifRWStreamFinishBox(s, iinf);

    avifRWStreamFinishBox(s, meta);
}

static void avifWriteGridPayload(avifRWData * data, uint32_t gridCols, uint32_t gridRows, const avifImage * firstCell)
{
    // ISO/IEC 23008-12 6.6.2.3.2
    // aligned(8) class ImageGrid {
    //     unsigned int(8) version = 0;
    //     unsigned int(8) flags;
    //     FieldLength = ((flags & 1) + 1) * 16;
    //     unsigned int(8) rows_minus_one;
    //     unsigned int(8) columns_minus_one;
    //     unsigned int(FieldLength) output_width;
    //     unsigned int(FieldLength) output_height;
    // }

    uint32_t gridWidth = firstCell->width * gridCols;
    uint32_t gridHeight = firstCell->height * gridRows;
    uint8_t gridFlags = ((gridWidth > 65535) || (gridHeight > 65535)) ? 1 : 0;

    avifRWStream s;
    avifRWStreamStart(&s, data);
    avifRWStreamWriteU8(&s, 0);                       // unsigned int(8) version = 0;
    avifRWStreamWriteU8(&s, gridFlags);               // unsigned int(8) flags;
    avifRWStreamWriteU8(&s, (uint8_t)(gridRows - 1)); // unsigned int(8) rows_minus_one;
    avifRWStreamWriteU8(&s, (uint8_t)(gridCols - 1)); // unsigned int(8) columns_minus_one;
    if (gridFlags & 1) {
        avifRWStreamWriteU32(&s, gridWidth);  // unsigned int(FieldLength) output_width;
        avifRWStreamWriteU32(&s, gridHeight); // unsigned int(FieldLength) output_height;
    } else {
        uint16_t tmpWidth = (uint16_t)gridWidth;
        uint16_t tmpHeight = (uint16_t)gridHeight;
        avifRWStreamWriteU16(&s, tmpWidth);  // unsigned int(FieldLength) output_width;
        avifRWStreamWriteU16(&s, tmpHeight); // unsigned int(FieldLength) output_height;
    }
    avifRWStreamFinishWrite(&s);
}

static avifResult avifEncoderAddImageInternal(avifEncoder * encoder,
                                              uint32_t gridCols,
                                              uint32_t gridRows,
                                              const avifImage * const * cellImages,
                                              uint64_t durationInTimescales,
                                              uint32_t addImageFlags)
{
    // -----------------------------------------------------------------------
    // Verify encoding is possible

    if (!avifCodecName(encoder->codecChoice, AVIF_CODEC_FLAG_CAN_ENCODE)) {
        return AVIF_RESULT_NO_CODEC_AVAILABLE;
    }

    // -----------------------------------------------------------------------
    // Validate images

    const uint32_t cellCount = gridCols * gridRows;
    if (cellCount == 0) {
        return AVIF_RESULT_INVALID_ARGUMENT;
    }

    const avifImage * firstCell = cellImages[0];
    if ((firstCell->depth != 8) && (firstCell->depth != 10) && (firstCell->depth != 12)) {
        return AVIF_RESULT_UNSUPPORTED_DEPTH;
    }

    if (!firstCell->width || !firstCell->height) {
        return AVIF_RESULT_NO_CONTENT;
    }

    if ((cellCount > 1) && ((firstCell->width < 64) || (firstCell->height < 64))) {
        return AVIF_RESULT_INVALID_IMAGE_GRID;
    }

    for (uint32_t cellIndex = 0; cellIndex < cellCount; ++cellIndex) {
        const avifImage * cellImage = cellImages[cellIndex];
        if ((cellImage->depth != firstCell->depth) || (cellImage->width != firstCell->width) ||
            (cellImage->height != firstCell->height) || (!!cellImage->alphaPlane != !!firstCell->alphaPlane) ||
            (cellImage->alphaPremultiplied != firstCell->alphaPremultiplied)) {
            return AVIF_RESULT_INVALID_IMAGE_GRID;
        }

        if (!cellImage->yuvPlanes[AVIF_CHAN_Y]) {
            return AVIF_RESULT_NO_CONTENT;
        }

        if (cellImage->yuvFormat == AVIF_PIXEL_FORMAT_NONE) {
            return AVIF_RESULT_NO_YUV_FORMAT_SELECTED;
        }
    }

    // -----------------------------------------------------------------------
    // Validate flags

    if (encoder->data->singleImage) {
        // The previous call to avifEncoderAddImage() set AVIF_ADD_IMAGE_FLAG_SINGLE.
        // avifEncoderAddImage() cannot be called again for this encode.
        return AVIF_RESULT_ENCODE_COLOR_FAILED;
    }

    if (addImageFlags & AVIF_ADD_IMAGE_FLAG_SINGLE) {
        encoder->data->singleImage = AVIF_TRUE;

        if (encoder->data->items.count > 0) {
            // AVIF_ADD_IMAGE_FLAG_SINGLE may only be set on the first and only image.
            return AVIF_RESULT_INVALID_ARGUMENT;
        }
    }

    // -----------------------------------------------------------------------

    if (durationInTimescales == 0) {
        durationInTimescales = 1;
    }

    if (encoder->data->items.count == 0) {
        // Make a copy of the first image's metadata (sans pixels) for future writing/validation
        avifImageCopy(encoder->data->imageMetadata, firstCell, 0);

        // Prepare all AV1 items

        uint16_t gridColorID = 0;
        if (cellCount > 1) {
            avifEncoderItem * gridColorItem = avifEncoderDataCreateItem(encoder->data, "grid", "Color", 6, 0);
            avifWriteGridPayload(&gridColorItem->metadataPayload, gridCols, gridRows, firstCell);
            gridColorItem->gridCols = gridCols;
            gridColorItem->gridRows = gridRows;

            gridColorID = gridColorItem->id;
            encoder->data->primaryItemID = gridColorID;
        }

        for (uint32_t cellIndex = 0; cellIndex < cellCount; ++cellIndex) {
            avifEncoderItem * item = avifEncoderDataCreateItem(encoder->data, "av01", "Color", 6, cellIndex);
            item->codec = avifCodecCreate(encoder->codecChoice, AVIF_CODEC_FLAG_CAN_ENCODE);
            if (!item->codec) {
                // Just bail out early, we're not surviving this function without an encoder compiled in
                return AVIF_RESULT_NO_CODEC_AVAILABLE;
            }
            item->codec->csOptions = encoder->csOptions;

            if (cellCount > 1) {
                item->dimgFromID = gridColorID;
            } else {
                encoder->data->primaryItemID = item->id;
            }
        }

        encoder->data->alphaPresent = (firstCell->alphaPlane != NULL);
        if (encoder->data->alphaPresent && (addImageFlags & AVIF_ADD_IMAGE_FLAG_SINGLE)) {
            // If encoding a single image in which the alpha plane exists but is entirely opaque,
            // simply skip writing an alpha AV1 payload entirely, as it'll be interpreted as opaque
            // and is less bytes.
            //
            // However, if encoding an image sequence, the first frame's alpha plane being entirely
            // opaque could be a false positive for removing the alpha AV1 payload, as it might simply
            // be a fade out later in the sequence. This is why avifImageIsOpaque() is only called
            // when encoding a single image.

            encoder->data->alphaPresent = AVIF_FALSE;
            for (uint32_t cellIndex = 0; cellIndex < cellCount; ++cellIndex) {
                const avifImage * cellImage = cellImages[cellIndex];
                if (!avifImageIsOpaque(cellImage)) {
                    encoder->data->alphaPresent = AVIF_TRUE;
                    break;
                }
            }
        }

        if (encoder->data->alphaPresent) {
            uint16_t gridAlphaID = 0;
            if (cellCount > 1) {
                avifEncoderItem * gridAlphaItem = avifEncoderDataCreateItem(encoder->data, "grid", "Alpha", 6, 0);
                avifWriteGridPayload(&gridAlphaItem->metadataPayload, gridCols, gridRows, firstCell);
                gridAlphaItem->alpha = AVIF_TRUE;
                gridAlphaItem->irefToID = encoder->data->primaryItemID;
                gridAlphaItem->irefType = "auxl";
                gridAlphaItem->gridCols = gridCols;
                gridAlphaItem->gridRows = gridRows;
                gridAlphaID = gridAlphaItem->id;

                if (encoder->data->imageMetadata->alphaPremultiplied) {
                    avifEncoderItem * primaryItem = avifEncoderDataFindItemByID(encoder->data, encoder->data->primaryItemID);
                    assert(primaryItem);
                    primaryItem->irefType = "prem";
                    primaryItem->irefToID = gridAlphaID;
                }
            }

            for (uint32_t cellIndex = 0; cellIndex < cellCount; ++cellIndex) {
                avifEncoderItem * item = avifEncoderDataCreateItem(encoder->data, "av01", "Alpha", 6, cellIndex);
                item->codec = avifCodecCreate(encoder->codecChoice, AVIF_CODEC_FLAG_CAN_ENCODE);
                if (!item->codec) {
                    return AVIF_RESULT_NO_CODEC_AVAILABLE;
                }
                item->codec->csOptions = encoder->csOptions;
                item->alpha = AVIF_TRUE;

                if (cellCount > 1) {
                    item->dimgFromID = gridAlphaID;
                } else {
                    item->irefToID = encoder->data->primaryItemID;
                    item->irefType = "auxl";

                    if (encoder->data->imageMetadata->alphaPremultiplied) {
                        avifEncoderItem * primaryItem = avifEncoderDataFindItemByID(encoder->data, encoder->data->primaryItemID);
                        assert(primaryItem);
                        primaryItem->irefType = "prem";
                        primaryItem->irefToID = item->id;
                    }
                }
            }
        }

        // -----------------------------------------------------------------------
        // Create metadata items (Exif, XMP)

        if (firstCell->exif.size > 0) {
            // Validate Exif payload (if any) and find TIFF header offset
            uint32_t exifTiffHeaderOffset = 0;
            if (firstCell->exif.size < 4) {
                // Can't even fit the TIFF header, something is wrong
                return AVIF_RESULT_INVALID_EXIF_PAYLOAD;
            }

            const uint8_t tiffHeaderBE[4] = { 'M', 'M', 0, 42 };
            const uint8_t tiffHeaderLE[4] = { 'I', 'I', 42, 0 };
            for (; exifTiffHeaderOffset < (firstCell->exif.size - 4); ++exifTiffHeaderOffset) {
                if (!memcmp(&firstCell->exif.data[exifTiffHeaderOffset], tiffHeaderBE, sizeof(tiffHeaderBE))) {
                    break;
                }
                if (!memcmp(&firstCell->exif.data[exifTiffHeaderOffset], tiffHeaderLE, sizeof(tiffHeaderLE))) {
                    break;
                }
            }

            if (exifTiffHeaderOffset >= firstCell->exif.size - 4) {
                // Couldn't find the TIFF header
                return AVIF_RESULT_INVALID_EXIF_PAYLOAD;
            }

            avifEncoderItem * exifItem = avifEncoderDataCreateItem(encoder->data, "Exif", "Exif", 5, 0);
            exifItem->irefToID = encoder->data->primaryItemID;
            exifItem->irefType = "cdsc";

            avifRWDataRealloc(&exifItem->metadataPayload, sizeof(uint32_t) + firstCell->exif.size);
            exifTiffHeaderOffset = avifHTONL(exifTiffHeaderOffset);
            memcpy(exifItem->metadataPayload.data, &exifTiffHeaderOffset, sizeof(uint32_t));
            memcpy(exifItem->metadataPayload.data + sizeof(uint32_t), firstCell->exif.data, firstCell->exif.size);
        }

        if (firstCell->xmp.size > 0) {
            avifEncoderItem * xmpItem = avifEncoderDataCreateItem(encoder->data, "mime", "XMP", 4, 0);
            xmpItem->irefToID = encoder->data->primaryItemID;
            xmpItem->irefType = "cdsc";

            xmpItem->infeContentType = xmpContentType;
            xmpItem->infeContentTypeSize = xmpContentTypeSize;
            avifRWDataSet(&xmpItem->metadataPayload, firstCell->xmp.data, firstCell->xmp.size);
        }
    } else {
        // Another frame in an image sequence

        if (encoder->data->alphaPresent && !firstCell->alphaPlane) {
            // If the first image in the sequence had an alpha plane (even if fully opaque), all
            // subsequence images must have alpha as well.
            return AVIF_RESULT_ENCODE_ALPHA_FAILED;
        }
    }

    // -----------------------------------------------------------------------
    // Encode AV1 OBUs

    if (encoder->keyframeInterval && ((encoder->data->frames.count % encoder->keyframeInterval) == 0)) {
        addImageFlags |= AVIF_ADD_IMAGE_FLAG_FORCE_KEYFRAME;
    }

    for (uint32_t itemIndex = 0; itemIndex < encoder->data->items.count; ++itemIndex) {
        avifEncoderItem * item = &encoder->data->items.item[itemIndex];
        if (item->codec) {
            const avifImage * cellImage = cellImages[item->cellIndex];
            avifResult encodeResult =
                item->codec->encodeImage(item->codec, encoder, cellImage, item->alpha, addImageFlags, item->encodeOutput);
            if (encodeResult == AVIF_RESULT_UNKNOWN_ERROR) {
                encodeResult = item->alpha ? AVIF_RESULT_ENCODE_ALPHA_FAILED : AVIF_RESULT_ENCODE_COLOR_FAILED;
            }
            if (encodeResult != AVIF_RESULT_OK) {
                return encodeResult;
            }
        }
    }

    avifEncoderFrame * frame = (avifEncoderFrame *)avifArrayPushPtr(&encoder->data->frames);
    frame->durationInTimescales = durationInTimescales;
    return AVIF_RESULT_OK;
}

avifResult avifEncoderAddImage(avifEncoder * encoder, const avifImage * image, uint64_t durationInTimescales, uint32_t addImageFlags)
{
    return avifEncoderAddImageInternal(encoder, 1, 1, &image, durationInTimescales, addImageFlags);
}

avifResult avifEncoderAddImageGrid(avifEncoder * encoder, uint32_t gridCols, uint32_t gridRows, const avifImage * const * cellImages, uint32_t addImageFlags)
{
    if ((gridCols == 0) || (gridCols > 256) || (gridRows == 0) || (gridRows > 256)) {
        return AVIF_RESULT_INVALID_IMAGE_GRID;
    }
    return avifEncoderAddImageInternal(encoder, gridCols, gridRows, cellImages, 1, addImageFlags | AVIF_ADD_IMAGE_FLAG_SINGLE); // only single image grids are supported
}

static size_t avifEncoderFindExistingChunk(avifRWStream * s, size_t mdatStartOffset, const uint8_t * data, size_t size)
{
    const size_t mdatCurrentOffset = avifRWStreamOffset(s);
    const size_t mdatSearchSize = mdatCurrentOffset - mdatStartOffset;
    if (mdatSearchSize < size) {
        return 0;
    }
    const size_t mdatEndSearchOffset = mdatCurrentOffset - size;
    for (size_t searchOffset = mdatStartOffset; searchOffset <= mdatEndSearchOffset; ++searchOffset) {
        if (!memcmp(data, &s->raw->data[searchOffset], size)) {
            return searchOffset;
        }
    }
    return 0;
}

avifResult avifEncoderFinish(avifEncoder * encoder, avifRWData * output)
{
    if (encoder->data->items.count == 0) {
        return AVIF_RESULT_NO_CONTENT;
    }

    // -----------------------------------------------------------------------
    // Finish up AV1 encoding

    for (uint32_t itemIndex = 0; itemIndex < encoder->data->items.count; ++itemIndex) {
        avifEncoderItem * item = &encoder->data->items.item[itemIndex];
        if (item->codec) {
            if (!item->codec->encodeFinish(item->codec, item->encodeOutput)) {
                return item->alpha ? AVIF_RESULT_ENCODE_ALPHA_FAILED : AVIF_RESULT_ENCODE_COLOR_FAILED;
            }

            if (item->encodeOutput->samples.count != encoder->data->frames.count) {
                return item->alpha ? AVIF_RESULT_ENCODE_ALPHA_FAILED : AVIF_RESULT_ENCODE_COLOR_FAILED;
            }
        }
    }

    // -----------------------------------------------------------------------
    // Harvest av1C properties from AV1 sequence headers

    for (uint32_t itemIndex = 0; itemIndex < encoder->data->items.count; ++itemIndex) {
        avifEncoderItem * item = &encoder->data->items.item[itemIndex];
        if (item->encodeOutput->samples.count > 0) {
            const avifEncodeSample * firstSample = &item->encodeOutput->samples.sample[0];
            avifSequenceHeader sequenceHeader;
            if (avifSequenceHeaderParse(&sequenceHeader, (const avifROData *)&firstSample->data)) {
                memcpy(&item->av1C, &sequenceHeader.av1C, sizeof(avifCodecConfigurationBox));
            } else {
                // This must be an invalid AV1 payload
                return item->alpha ? AVIF_RESULT_ENCODE_ALPHA_FAILED : AVIF_RESULT_ENCODE_COLOR_FAILED;
            }
        }
    }

    // -----------------------------------------------------------------------
    // Begin write stream

    const avifImage * imageMetadata = encoder->data->imageMetadata;
    // The epoch for creation_time and modification_time is midnight, Jan. 1,
    // 1904, in UTC time. Add the number of seconds between that epoch and the
    // Unix epoch.
    uint64_t now = (uint64_t)time(NULL) + 2082844800;

    avifRWStream s;
    avifRWStreamStart(&s, output);

    // -----------------------------------------------------------------------
    // Write ftyp

    const char * majorBrand = "avif";
    if (encoder->data->frames.count > 1) {
        majorBrand = "avis";
    }

    avifBoxMarker ftyp = avifRWStreamWriteBox(&s, "ftyp", AVIF_BOX_SIZE_TBD);
    avifRWStreamWriteChars(&s, majorBrand, 4);                             // unsigned int(32) major_brand;
    avifRWStreamWriteU32(&s, 0);                                           // unsigned int(32) minor_version;
    avifRWStreamWriteChars(&s, "avif", 4);                                 // unsigned int(32) compatible_brands[];
    if (encoder->data->frames.count > 1) {                                 //
        avifRWStreamWriteChars(&s, "avis", 4);                             // ... compatible_brands[]
        avifRWStreamWriteChars(&s, "msf1", 4);                             // ... compatible_brands[]
    }                                                                      //
    avifRWStreamWriteChars(&s, "mif1", 4);                                 // ... compatible_brands[]
    avifRWStreamWriteChars(&s, "miaf", 4);                                 // ... compatible_brands[]
    if ((imageMetadata->depth == 8) || (imageMetadata->depth == 10)) {     //
        if (imageMetadata->yuvFormat == AVIF_PIXEL_FORMAT_YUV420) {        //
            avifRWStreamWriteChars(&s, "MA1B", 4);                         // ... compatible_brands[]
        } else if (imageMetadata->yuvFormat == AVIF_PIXEL_FORMAT_YUV444) { //
            avifRWStreamWriteChars(&s, "MA1A", 4);                         // ... compatible_brands[]
        }
    }
    avifRWStreamFinishBox(&s, ftyp);

    // -----------------------------------------------------------------------
    // Start meta

    avifBoxMarker meta = avifRWStreamWriteFullBox(&s, "meta", AVIF_BOX_SIZE_TBD, 0, 0);

    // -----------------------------------------------------------------------
    // Write hdlr

    avifBoxMarker hdlr = avifRWStreamWriteFullBox(&s, "hdlr", AVIF_BOX_SIZE_TBD, 0, 0);
    avifRWStreamWriteU32(&s, 0);              // unsigned int(32) pre_defined = 0;
    avifRWStreamWriteChars(&s, "pict", 4);    // unsigned int(32) handler_type;
    avifRWStreamWriteZeros(&s, 12);           // const unsigned int(32)[3] reserved = 0;
    avifRWStreamWriteChars(&s, "libavif", 8); // string name; (writing null terminator)
    avifRWStreamFinishBox(&s, hdlr);

    // -----------------------------------------------------------------------
    // Write pitm

    if (encoder->data->primaryItemID != 0) {
        avifRWStreamWriteFullBox(&s, "pitm", sizeof(uint16_t), 0, 0);
        avifRWStreamWriteU16(&s, encoder->data->primaryItemID); //  unsigned int(16) item_ID;
    }

    // -----------------------------------------------------------------------
    // Write iloc

    avifBoxMarker iloc = avifRWStreamWriteFullBox(&s, "iloc", AVIF_BOX_SIZE_TBD, 0, 0);

    uint8_t offsetSizeAndLengthSize = (4 << 4) + (4 << 0);          // unsigned int(4) offset_size;
                                                                    // unsigned int(4) length_size;
    avifRWStreamWrite(&s, &offsetSizeAndLengthSize, 1);             //
    avifRWStreamWriteZeros(&s, 1);                                  // unsigned int(4) base_offset_size;
                                                                    // unsigned int(4) reserved;
    avifRWStreamWriteU16(&s, (uint16_t)encoder->data->items.count); // unsigned int(16) item_count;

    for (uint32_t itemIndex = 0; itemIndex < encoder->data->items.count; ++itemIndex) {
        avifEncoderItem * item = &encoder->data->items.item[itemIndex];

        uint32_t contentSize = (uint32_t)item->metadataPayload.size;
        if (item->encodeOutput->samples.count > 0) {
            // This is choosing sample 0's size as there are two cases here:
            // * This is a single image, in which case this is correct
            // * This is an image sequence, but this file should still be a valid single-image avif,
            //   so there must still be a primary item pointing at a sync sample. Since the first
            //   frame of the image sequence is guaranteed to be a sync sample, it is chosen here.
            //
            // TODO: Offer the ability for a user to specify which frame in the sequence should
            //       become the primary item's image, and force that frame to be a keyframe.
            contentSize = (uint32_t)item->encodeOutput->samples.sample[0].data.size;
        }

        avifRWStreamWriteU16(&s, item->id);              // unsigned int(16) item_ID;
        avifRWStreamWriteU16(&s, 0);                     // unsigned int(16) data_reference_index;
        avifRWStreamWriteU16(&s, 1);                     // unsigned int(16) extent_count;
        avifEncoderItemAddMdatFixup(item, &s);           //
        avifRWStreamWriteU32(&s, 0 /* set later */);     // unsigned int(offset_size*8) extent_offset;
        avifRWStreamWriteU32(&s, (uint32_t)contentSize); // unsigned int(length_size*8) extent_length;
    }

    avifRWStreamFinishBox(&s, iloc);

    // -----------------------------------------------------------------------
    // Write iinf

    avifBoxMarker iinf = avifRWStreamWriteFullBox(&s, "iinf", AVIF_BOX_SIZE_TBD, 0, 0);
    avifRWStreamWriteU16(&s, (uint16_t)encoder->data->items.count); //  unsigned int(16) entry_count;

    for (uint32_t itemIndex = 0; itemIndex < encoder->data->items.count; ++itemIndex) {
        avifEncoderItem * item = &encoder->data->items.item[itemIndex];

        avifBoxMarker infe = avifRWStreamWriteFullBox(&s, "infe", AVIF_BOX_SIZE_TBD, 2, 0);
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

    avifBoxMarker iref = 0;
    for (uint32_t itemIndex = 0; itemIndex < encoder->data->items.count; ++itemIndex) {
        avifEncoderItem * item = &encoder->data->items.item[itemIndex];

        // Count how many other items refer to this item with dimgFromID
        uint16_t dimgCount = 0;
        for (uint32_t dimgIndex = 0; dimgIndex < encoder->data->items.count; ++dimgIndex) {
            avifEncoderItem * dimgItem = &encoder->data->items.item[dimgIndex];
            if (dimgItem->dimgFromID == item->id) {
                ++dimgCount;
            }
        }

        if (dimgCount > 0) {
            if (!iref) {
                iref = avifRWStreamWriteFullBox(&s, "iref", AVIF_BOX_SIZE_TBD, 0, 0);
            }
            avifBoxMarker refType = avifRWStreamWriteBox(&s, "dimg", AVIF_BOX_SIZE_TBD);
            avifRWStreamWriteU16(&s, item->id);  // unsigned int(16) from_item_ID;
            avifRWStreamWriteU16(&s, dimgCount); // unsigned int(16) reference_count;
            for (uint32_t dimgIndex = 0; dimgIndex < encoder->data->items.count; ++dimgIndex) {
                avifEncoderItem * dimgItem = &encoder->data->items.item[dimgIndex];
                if (dimgItem->dimgFromID == item->id) {
                    avifRWStreamWriteU16(&s, dimgItem->id); // unsigned int(16) to_item_ID;
                }
            }
            avifRWStreamFinishBox(&s, refType);
        }

        if (item->irefToID != 0) {
            if (!iref) {
                iref = avifRWStreamWriteFullBox(&s, "iref", AVIF_BOX_SIZE_TBD, 0, 0);
            }
            avifBoxMarker refType = avifRWStreamWriteBox(&s, item->irefType, AVIF_BOX_SIZE_TBD);
            avifRWStreamWriteU16(&s, item->id);       // unsigned int(16) from_item_ID;
            avifRWStreamWriteU16(&s, 1);              // unsigned int(16) reference_count;
            avifRWStreamWriteU16(&s, item->irefToID); // unsigned int(16) to_item_ID;
            avifRWStreamFinishBox(&s, refType);
        }
    }
    if (iref) {
        avifRWStreamFinishBox(&s, iref);
    }

    // -----------------------------------------------------------------------
    // Write iprp -> ipco/ipma

    avifBoxMarker iprp = avifRWStreamWriteBox(&s, "iprp", AVIF_BOX_SIZE_TBD);

    uint8_t itemPropertyIndex = 0;
    avifBoxMarker ipco = avifRWStreamWriteBox(&s, "ipco", AVIF_BOX_SIZE_TBD);
    for (uint32_t itemIndex = 0; itemIndex < encoder->data->items.count; ++itemIndex) {
        avifEncoderItem * item = &encoder->data->items.item[itemIndex];
        const avifBool isGrid = (item->gridCols > 0);
        memset(&item->ipma, 0, sizeof(item->ipma));
        if (!item->codec && !isGrid) {
            // No ipma to write for this item
            continue;
        }

        if (item->dimgFromID) {
            // All image cells from a grid should share the exact same properties, so see if we've
            // already written properties out for another cell in this grid, and if so, just steal
            // their ipma and move on. This is a sneaky way to provide iprp deduplication.

            avifBool foundPreviousCell = AVIF_FALSE;
            for (uint32_t dedupIndex = 0; dedupIndex < itemIndex; ++dedupIndex) {
                avifEncoderItem * dedupItem = &encoder->data->items.item[dedupIndex];
                if (item->dimgFromID == dedupItem->dimgFromID) {
                    // We've already written dedup's items out. Steal their ipma indices and move on!
                    memcpy(&item->ipma, &dedupItem->ipma, sizeof(struct ipmaArray));
                    foundPreviousCell = AVIF_TRUE;
                    break;
                }
            }
            if (foundPreviousCell) {
                continue;
            }
        }

        uint32_t imageWidth = imageMetadata->width;
        uint32_t imageHeight = imageMetadata->height;
        if (isGrid) {
            imageWidth = imageMetadata->width * item->gridCols;
            imageHeight = imageMetadata->height * item->gridRows;
        }

        // Properties all av01 items need

        avifBoxMarker ispe = avifRWStreamWriteFullBox(&s, "ispe", AVIF_BOX_SIZE_TBD, 0, 0);
        avifRWStreamWriteU32(&s, imageWidth);  // unsigned int(32) image_width;
        avifRWStreamWriteU32(&s, imageHeight); // unsigned int(32) image_height;
        avifRWStreamFinishBox(&s, ispe);
        ipmaPush(&item->ipma, ++itemPropertyIndex, AVIF_FALSE); // ipma is 1-indexed, doing this afterwards is correct

        uint8_t channelCount = (item->alpha || (imageMetadata->yuvFormat == AVIF_PIXEL_FORMAT_YUV400)) ? 1 : 3;
        avifBoxMarker pixi = avifRWStreamWriteFullBox(&s, "pixi", AVIF_BOX_SIZE_TBD, 0, 0);
        avifRWStreamWriteU8(&s, channelCount); // unsigned int (8) num_channels;
        for (uint8_t chan = 0; chan < channelCount; ++chan) {
            avifRWStreamWriteU8(&s, (uint8_t)imageMetadata->depth); // unsigned int (8) bits_per_channel;
        }
        avifRWStreamFinishBox(&s, pixi);
        ipmaPush(&item->ipma, ++itemPropertyIndex, AVIF_FALSE);

        if (item->codec) {
            writeConfigBox(&s, &item->av1C);
            ipmaPush(&item->ipma, ++itemPropertyIndex, AVIF_TRUE);
        }

        if (item->alpha) {
            // Alpha specific properties

            avifBoxMarker auxC = avifRWStreamWriteFullBox(&s, "auxC", AVIF_BOX_SIZE_TBD, 0, 0);
            avifRWStreamWriteChars(&s, alphaURN, alphaURNSize); //  string aux_type;
            avifRWStreamFinishBox(&s, auxC);
            ipmaPush(&item->ipma, ++itemPropertyIndex, AVIF_FALSE);
        } else {
            // Color specific properties

            avifEncoderWriteColorProperties(&s, imageMetadata, &item->ipma, &itemPropertyIndex);
        }
    }
    avifRWStreamFinishBox(&s, ipco);

    avifBoxMarker ipma = avifRWStreamWriteFullBox(&s, "ipma", AVIF_BOX_SIZE_TBD, 0, 0);
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
    // Write tracks (if an image sequence)

    if (encoder->data->frames.count > 1) {
        static const uint32_t unityMatrix[9] = { 0x00010000, 0, 0, 0, 0x00010000, 0, 0, 0, 0x40000000 };

        uint64_t durationInTimescales = 0;
        for (uint32_t frameIndex = 0; frameIndex < encoder->data->frames.count; ++frameIndex) {
            const avifEncoderFrame * frame = &encoder->data->frames.frame[frameIndex];
            durationInTimescales += frame->durationInTimescales;
        }

        // -------------------------------------------------------------------
        // Start moov

        avifBoxMarker moov = avifRWStreamWriteBox(&s, "moov", AVIF_BOX_SIZE_TBD);

        avifBoxMarker mvhd = avifRWStreamWriteFullBox(&s, "mvhd", AVIF_BOX_SIZE_TBD, 1, 0);
        avifRWStreamWriteU64(&s, now);                          // unsigned int(64) creation_time;
        avifRWStreamWriteU64(&s, now);                          // unsigned int(64) modification_time;
        avifRWStreamWriteU32(&s, (uint32_t)encoder->timescale); // unsigned int(32) timescale;
        avifRWStreamWriteU64(&s, durationInTimescales);         // unsigned int(64) duration;
        avifRWStreamWriteU32(&s, 0x00010000);                   // template int(32) rate = 0x00010000; // typically 1.0
        avifRWStreamWriteU16(&s, 0x0100);                       // template int(16) volume = 0x0100; // typically, full volume
        avifRWStreamWriteU16(&s, 0);                            // const bit(16) reserved = 0;
        avifRWStreamWriteZeros(&s, 8);                          // const unsigned int(32)[2] reserved = 0;
        avifRWStreamWrite(&s, unityMatrix, sizeof(unityMatrix));
        avifRWStreamWriteZeros(&s, 24);                       // bit(32)[6] pre_defined = 0;
        avifRWStreamWriteU32(&s, encoder->data->items.count); // unsigned int(32) next_track_ID;
        avifRWStreamFinishBox(&s, mvhd);

        // -------------------------------------------------------------------
        // Write tracks

        for (uint32_t itemIndex = 0; itemIndex < encoder->data->items.count; ++itemIndex) {
            avifEncoderItem * item = &encoder->data->items.item[itemIndex];
            if (item->encodeOutput->samples.count == 0) {
                continue;
            }

            uint32_t syncSamplesCount = 0;
            for (uint32_t sampleIndex = 0; sampleIndex < item->encodeOutput->samples.count; ++sampleIndex) {
                avifEncodeSample * sample = &item->encodeOutput->samples.sample[sampleIndex];
                if (sample->sync) {
                    ++syncSamplesCount;
                }
            }

            avifBoxMarker trak = avifRWStreamWriteBox(&s, "trak", AVIF_BOX_SIZE_TBD);

            avifBoxMarker tkhd = avifRWStreamWriteFullBox(&s, "tkhd", AVIF_BOX_SIZE_TBD, 1, 1);
            avifRWStreamWriteU64(&s, now);                    // unsigned int(64) creation_time;
            avifRWStreamWriteU64(&s, now);                    // unsigned int(64) modification_time;
            avifRWStreamWriteU32(&s, itemIndex + 1);          // unsigned int(32) track_ID;
            avifRWStreamWriteU32(&s, 0);                      // const unsigned int(32) reserved = 0;
            avifRWStreamWriteU64(&s, durationInTimescales);   // unsigned int(64) duration;
            avifRWStreamWriteZeros(&s, sizeof(uint32_t) * 2); // const unsigned int(32)[2] reserved = 0;
            avifRWStreamWriteU16(&s, 0);                      // template int(16) layer = 0;
            avifRWStreamWriteU16(&s, 0);                      // template int(16) alternate_group = 0;
            avifRWStreamWriteU16(&s, 0);                      // template int(16) volume = {if track_is_audio 0x0100 else 0};
            avifRWStreamWriteU16(&s, 0);                      // const unsigned int(16) reserved = 0;
            avifRWStreamWrite(&s, unityMatrix, sizeof(unityMatrix)); // template int(32)[9] matrix= // { 0x00010000,0,0,0,0x00010000,0,0,0,0x40000000 };
            avifRWStreamWriteU32(&s, imageMetadata->width << 16);  // unsigned int(32) width;
            avifRWStreamWriteU32(&s, imageMetadata->height << 16); // unsigned int(32) height;
            avifRWStreamFinishBox(&s, tkhd);

            if (item->irefToID != 0) {
                avifBoxMarker tref = avifRWStreamWriteBox(&s, "tref", AVIF_BOX_SIZE_TBD);
                avifBoxMarker refType = avifRWStreamWriteBox(&s, item->irefType, AVIF_BOX_SIZE_TBD);
                avifRWStreamWriteU32(&s, (uint32_t)item->irefToID);
                avifRWStreamFinishBox(&s, refType);
                avifRWStreamFinishBox(&s, tref);
            }

            if (!item->alpha) {
                avifEncoderWriteTrackMetaBox(encoder, &s);
            }

            avifBoxMarker mdia = avifRWStreamWriteBox(&s, "mdia", AVIF_BOX_SIZE_TBD);

            avifBoxMarker mdhd = avifRWStreamWriteFullBox(&s, "mdhd", AVIF_BOX_SIZE_TBD, 1, 0);
            avifRWStreamWriteU64(&s, now);                          // unsigned int(64) creation_time;
            avifRWStreamWriteU64(&s, now);                          // unsigned int(64) modification_time;
            avifRWStreamWriteU32(&s, (uint32_t)encoder->timescale); // unsigned int(32) timescale;
            avifRWStreamWriteU64(&s, durationInTimescales);         // unsigned int(64) duration;
            avifRWStreamWriteU16(&s, 21956);                        // bit(1) pad = 0; unsigned int(5)[3] language; ("und")
            avifRWStreamWriteU16(&s, 0);                            // unsigned int(16) pre_defined = 0;
            avifRWStreamFinishBox(&s, mdhd);

            avifBoxMarker hdlrTrak = avifRWStreamWriteFullBox(&s, "hdlr", AVIF_BOX_SIZE_TBD, 0, 0);
            avifRWStreamWriteU32(&s, 0);              // unsigned int(32) pre_defined = 0;
            avifRWStreamWriteChars(&s, "pict", 4);    // unsigned int(32) handler_type;
            avifRWStreamWriteZeros(&s, 12);           // const unsigned int(32)[3] reserved = 0;
            avifRWStreamWriteChars(&s, "libavif", 8); // string name; (writing null terminator)
            avifRWStreamFinishBox(&s, hdlrTrak);

            avifBoxMarker minf = avifRWStreamWriteBox(&s, "minf", AVIF_BOX_SIZE_TBD);

            avifBoxMarker vmhd = avifRWStreamWriteFullBox(&s, "vmhd", AVIF_BOX_SIZE_TBD, 0, 1);
            avifRWStreamWriteU16(&s, 0);   // template unsigned int(16) graphicsmode = 0; (copy over the existing image)
            avifRWStreamWriteZeros(&s, 6); // template unsigned int(16)[3] opcolor = {0, 0, 0};
            avifRWStreamFinishBox(&s, vmhd);

            avifBoxMarker dinf = avifRWStreamWriteBox(&s, "dinf", AVIF_BOX_SIZE_TBD);
            avifBoxMarker dref = avifRWStreamWriteFullBox(&s, "dref", AVIF_BOX_SIZE_TBD, 0, 0);
            avifRWStreamWriteU32(&s, 1);                   // unsigned int(32) entry_count;
            avifRWStreamWriteFullBox(&s, "url ", 0, 0, 1); // flags:1 means data is in this file
            avifRWStreamFinishBox(&s, dref);
            avifRWStreamFinishBox(&s, dinf);

            avifBoxMarker stbl = avifRWStreamWriteBox(&s, "stbl", AVIF_BOX_SIZE_TBD);

            avifBoxMarker stco = avifRWStreamWriteFullBox(&s, "stco", AVIF_BOX_SIZE_TBD, 0, 0);
            avifRWStreamWriteU32(&s, 1);           // unsigned int(32) entry_count;
            avifEncoderItemAddMdatFixup(item, &s); //
            avifRWStreamWriteU32(&s, 1);           // unsigned int(32) chunk_offset; (set later)
            avifRWStreamFinishBox(&s, stco);

            avifBoxMarker stsc = avifRWStreamWriteFullBox(&s, "stsc", AVIF_BOX_SIZE_TBD, 0, 0);
            avifRWStreamWriteU32(&s, 1);                                 // unsigned int(32) entry_count;
            avifRWStreamWriteU32(&s, 1);                                 // unsigned int(32) first_chunk;
            avifRWStreamWriteU32(&s, item->encodeOutput->samples.count); // unsigned int(32) samples_per_chunk;
            avifRWStreamWriteU32(&s, 1);                                 // unsigned int(32) sample_description_index;
            avifRWStreamFinishBox(&s, stsc);

            avifBoxMarker stsz = avifRWStreamWriteFullBox(&s, "stsz", AVIF_BOX_SIZE_TBD, 0, 0);
            avifRWStreamWriteU32(&s, 0);                                 // unsigned int(32) sample_size;
            avifRWStreamWriteU32(&s, item->encodeOutput->samples.count); // unsigned int(32) sample_count;
            for (uint32_t sampleIndex = 0; sampleIndex < item->encodeOutput->samples.count; ++sampleIndex) {
                avifEncodeSample * sample = &item->encodeOutput->samples.sample[sampleIndex];
                avifRWStreamWriteU32(&s, (uint32_t)sample->data.size); // unsigned int(32) entry_size;
            }
            avifRWStreamFinishBox(&s, stsz);

            avifBoxMarker stss = avifRWStreamWriteFullBox(&s, "stss", AVIF_BOX_SIZE_TBD, 0, 0);
            avifRWStreamWriteU32(&s, syncSamplesCount); // unsigned int(32) entry_count;
            for (uint32_t sampleIndex = 0; sampleIndex < item->encodeOutput->samples.count; ++sampleIndex) {
                avifEncodeSample * sample = &item->encodeOutput->samples.sample[sampleIndex];
                if (sample->sync) {
                    avifRWStreamWriteU32(&s, sampleIndex + 1); // unsigned int(32) sample_number;
                }
            }
            avifRWStreamFinishBox(&s, stss);

            avifBoxMarker stts = avifRWStreamWriteFullBox(&s, "stts", AVIF_BOX_SIZE_TBD, 0, 0);
            size_t sttsEntryCountOffset = avifRWStreamOffset(&s);
            uint32_t sttsEntryCount = 0;
            avifRWStreamWriteU32(&s, 0); // unsigned int(32) entry_count;
            for (uint32_t sampleCount = 0, frameIndex = 0; frameIndex < encoder->data->frames.count; ++frameIndex) {
                avifEncoderFrame * frame = &encoder->data->frames.frame[frameIndex];
                ++sampleCount;
                if (frameIndex < (encoder->data->frames.count - 1)) {
                    avifEncoderFrame * nextFrame = &encoder->data->frames.frame[frameIndex + 1];
                    if (frame->durationInTimescales == nextFrame->durationInTimescales) {
                        continue;
                    }
                }
                avifRWStreamWriteU32(&s, sampleCount);                           // unsigned int(32) sample_count;
                avifRWStreamWriteU32(&s, (uint32_t)frame->durationInTimescales); // unsigned int(32) sample_delta;
                sampleCount = 0;
                ++sttsEntryCount;
            }
            size_t prevOffset = avifRWStreamOffset(&s);
            avifRWStreamSetOffset(&s, sttsEntryCountOffset);
            avifRWStreamWriteU32(&s, sttsEntryCount);
            avifRWStreamSetOffset(&s, prevOffset);
            avifRWStreamFinishBox(&s, stts);

            avifBoxMarker stsd = avifRWStreamWriteFullBox(&s, "stsd", AVIF_BOX_SIZE_TBD, 0, 0);
            avifRWStreamWriteU32(&s, 1); // unsigned int(32) entry_count;
            avifBoxMarker av01 = avifRWStreamWriteBox(&s, "av01", AVIF_BOX_SIZE_TBD);
            avifRWStreamWriteZeros(&s, 6);                             // const unsigned int(8)[6] reserved = 0;
            avifRWStreamWriteU16(&s, 1);                               // unsigned int(16) data_reference_index;
            avifRWStreamWriteU16(&s, 0);                               // unsigned int(16) pre_defined = 0;
            avifRWStreamWriteU16(&s, 0);                               // const unsigned int(16) reserved = 0;
            avifRWStreamWriteZeros(&s, sizeof(uint32_t) * 3);          // unsigned int(32)[3] pre_defined = 0;
            avifRWStreamWriteU16(&s, (uint16_t)imageMetadata->width);  // unsigned int(16) width;
            avifRWStreamWriteU16(&s, (uint16_t)imageMetadata->height); // unsigned int(16) height;
            avifRWStreamWriteU32(&s, 0x00480000);                      // template unsigned int(32) horizresolution
            avifRWStreamWriteU32(&s, 0x00480000);                      // template unsigned int(32) vertresolution
            avifRWStreamWriteU32(&s, 0);                               // const unsigned int(32) reserved = 0;
            avifRWStreamWriteU16(&s, 1);                               // template unsigned int(16) frame_count = 1;
            avifRWStreamWriteChars(&s, "\012AOM Coding", 11);          // string[32] compressorname;
            avifRWStreamWriteZeros(&s, 32 - 11);                       //
            avifRWStreamWriteU16(&s, 0x0018);                          // template unsigned int(16) depth = 0x0018;
            avifRWStreamWriteU16(&s, (uint16_t)0xffff);                // int(16) pre_defined = -1;
            writeConfigBox(&s, &item->av1C);
            if (!item->alpha) {
                avifEncoderWriteColorProperties(&s, imageMetadata, NULL, NULL);
            }
            avifRWStreamFinishBox(&s, av01);
            avifRWStreamFinishBox(&s, stsd);

            avifRWStreamFinishBox(&s, stbl);

            avifRWStreamFinishBox(&s, minf);
            avifRWStreamFinishBox(&s, mdia);
            avifRWStreamFinishBox(&s, trak);
        }

        // -------------------------------------------------------------------
        // Finish moov box

        avifRWStreamFinishBox(&s, moov);
    }

    // -----------------------------------------------------------------------
    // Write mdat

    encoder->ioStats.colorOBUSize = 0;
    encoder->ioStats.alphaOBUSize = 0;

    avifBoxMarker mdat = avifRWStreamWriteBox(&s, "mdat", AVIF_BOX_SIZE_TBD);
    const size_t mdatStartOffset = avifRWStreamOffset(&s);
    for (uint32_t itemPasses = 0; itemPasses < 3; ++itemPasses) {
        // Use multiple passes to pack in the following order:
        //   * Pass 0: metadata (Exif/XMP)
        //   * Pass 1: alpha (AV1)
        //   * Pass 2: all other item data (AV1 color)
        //
        // See here for the discussion on alpha coming before color:
        // https://github.com/AOMediaCodec/libavif/issues/287
        //
        // Exif and XMP are packed first as they're required to be fully available
        // by avifDecoderParse() before it returns AVIF_RESULT_OK, unless ignoreXMP
        // and ignoreExif are enabled.
        //
        const avifBool metadataPass = (itemPasses == 0);
        const avifBool alphaPass = (itemPasses == 1);

        for (uint32_t itemIndex = 0; itemIndex < encoder->data->items.count; ++itemIndex) {
            avifEncoderItem * item = &encoder->data->items.item[itemIndex];
            if ((item->metadataPayload.size == 0) && (item->encodeOutput->samples.count == 0)) {
                // this item has nothing for the mdat box
                continue;
            }
            if (metadataPass != (item->metadataPayload.size > 0)) {
                // only process metadata payloads when metadataPass is true
                continue;
            }
            if (alphaPass != item->alpha) {
                // only process alpha payloads when alphaPass is true
                continue;
            }

            size_t chunkOffset = 0;

            // Deduplication - See if an identical chunk to this has already been written
            if (item->encodeOutput->samples.count > 0) {
                avifEncodeSample * sample = &item->encodeOutput->samples.sample[0];
                chunkOffset = avifEncoderFindExistingChunk(&s, mdatStartOffset, sample->data.data, sample->data.size);
            } else {
                chunkOffset = avifEncoderFindExistingChunk(&s, mdatStartOffset, item->metadataPayload.data, item->metadataPayload.size);
            }

            if (!chunkOffset) {
                // We've never seen this chunk before; write it out
                chunkOffset = avifRWStreamOffset(&s);
                if (item->encodeOutput->samples.count > 0) {
                    for (uint32_t sampleIndex = 0; sampleIndex < item->encodeOutput->samples.count; ++sampleIndex) {
                        avifEncodeSample * sample = &item->encodeOutput->samples.sample[sampleIndex];
                        avifRWStreamWrite(&s, sample->data.data, sample->data.size);

                        if (item->alpha) {
                            encoder->ioStats.alphaOBUSize += sample->data.size;
                        } else {
                            encoder->ioStats.colorOBUSize += sample->data.size;
                        }
                    }
                } else {
                    avifRWStreamWrite(&s, item->metadataPayload.data, item->metadataPayload.size);
                }
            }

            for (uint32_t fixupIndex = 0; fixupIndex < item->mdatFixups.count; ++fixupIndex) {
                avifOffsetFixup * fixup = &item->mdatFixups.fixup[fixupIndex];
                size_t prevOffset = avifRWStreamOffset(&s);
                avifRWStreamSetOffset(&s, fixup->offset);
                avifRWStreamWriteU32(&s, (uint32_t)chunkOffset);
                avifRWStreamSetOffset(&s, prevOffset);
            }
        }
    }
    avifRWStreamFinishBox(&s, mdat);

    // -----------------------------------------------------------------------
    // Finish up stream

    avifRWStreamFinishWrite(&s);

    return AVIF_RESULT_OK;
}

avifResult avifEncoderWrite(avifEncoder * encoder, const avifImage * image, avifRWData * output)
{
    avifResult addImageResult = avifEncoderAddImage(encoder, image, 1, AVIF_ADD_IMAGE_FLAG_SINGLE);
    if (addImageResult != AVIF_RESULT_OK) {
        return addImageResult;
    }
    return avifEncoderFinish(encoder, output);
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

static void writeConfigBox(avifRWStream * s, avifCodecConfigurationBox * cfg)
{
    avifBoxMarker av1C = avifRWStreamWriteBox(s, "av1C", AVIF_BOX_SIZE_TBD);

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
