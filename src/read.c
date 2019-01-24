// Copyright 2019 Joe Drago. All rights reserved.
// SPDX-License-Identifier: BSD-2-Clause

#include "avif/internal.h"

#include "aom/aom_decoder.h"
#include "aom/aomdx.h"

#include <string.h>

#define MAX_ITEMS 8

typedef struct avifItem
{
    int id;
    uint8_t type[4];
    uint32_t offset;
    uint32_t size;
} avifItem;

typedef struct avifData
{
    avifBool ispe_seen;
    uint32_t ispe_width;
    uint32_t ispe_height;
    avifItem items[MAX_ITEMS];
} avifData;

int findItemID(avifData * data, int itemID)
{
    if (itemID == 0) {
        return -1;
    }

    for (int i = 0; i < MAX_ITEMS; ++i) {
        if (data->items[i].id == itemID) {
            return i;
        }
    }

    for (int i = 0; i < MAX_ITEMS; ++i) {
        if (data->items[i].id == 0) {
            data->items[i].id = itemID;
            return i;
        }
    }

    return -1;
}

static avifBool avifParse(avifData * data, uint8_t * raw, size_t rawLen)
{
    avifStream s;
    avifRawData rawData = { raw, rawLen };
    avifStreamStart(&s, &rawData);

    while (avifStreamHasBytesLeft(&s, 1)) {
        size_t contentSize;
        uint8_t type[4];
        CHECK(avifStreamReadBoxHeader(&s, type, &contentSize));

        if (!memcmp(type, "meta", 4)) {
            // Basic container FullBoxes, skip version and parse contents

            CHECK(avifStreamReadAndEnforceVersion(&s, 0));
            contentSize -= 4;

            CHECK(avifParse(data, raw + s.offset, contentSize));

        } else if (!memcmp(type, "iprp", 4) || !memcmp(type, "ipco", 4)) {
            // Basic container Boxes, just parse contents

            CHECK(avifParse(data, raw + s.offset, contentSize));

        } else if (!memcmp(type, "ispe", 4)) {
            // ImageSpatialExtentsProperty

            CHECK(avifStreamReadAndEnforceVersion(&s, 0));
            CHECK(avifStreamReadU32(&s, &data->ispe_width));
            CHECK(avifStreamReadU32(&s, &data->ispe_height));
            data->ispe_seen = AVIF_TRUE;

        } else if (!memcmp(type, "iloc", 4)) {
            // ItemLocationBox

            CHECK(avifStreamReadAndEnforceVersion(&s, 0)); // TODO: support more

            uint8_t offsetSizeAndLengthSize;
            CHECK(avifStreamRead(&s, &offsetSizeAndLengthSize, 1));
            uint8_t offsetSize = (offsetSizeAndLengthSize >> 4) & 0xf; // unsigned int(4) offset_size;
            uint8_t lengthSize = (offsetSizeAndLengthSize >> 0) & 0xf; // unsigned int(4) length_size;

            uint8_t baseOffsetSizeAndReserved;
            CHECK(avifStreamRead(&s, &baseOffsetSizeAndReserved, 1));
            uint8_t baseOffsetSize = (baseOffsetSizeAndReserved >> 4) & 0xf; // unsigned int(4) base_offset_size;

            uint16_t itemCount;
            CHECK(avifStreamReadU16(&s, &itemCount)); // unsigned int(16) item_count;
            for (int itemIndex = 0; itemIndex < itemCount; ++itemIndex) {
                uint16_t itemID;                                           // unsigned int(16) item_ID;
                CHECK(avifStreamReadU16(&s, &itemID));                     //
                uint16_t dataReferenceIndex;                               // unsigned int(16) data_reference_index;
                CHECK(avifStreamReadU16(&s, &dataReferenceIndex));         //
                uint64_t baseOffset;                                       // unsigned int(base_offset_size*8) base_offset;
                CHECK(avifStreamReadUX8(&s, &baseOffset, baseOffsetSize)); //
                uint16_t extentCount;                                      // unsigned int(16) extent_count;
                CHECK(avifStreamReadU16(&s, &extentCount));                //
                if (extentCount == 1) {
                    uint64_t extentOffset; // unsigned int(offset_size*8) extent_offset;
                    CHECK(avifStreamReadUX8(&s, &extentOffset, offsetSize));
                    uint64_t extentLength; // unsigned int(offset_size*8) extent_length;
                    CHECK(avifStreamReadUX8(&s, &extentLength, lengthSize));

                    int itemIndex = findItemID(data, itemID);
                    if (itemIndex == -1) {
                        return AVIF_FALSE;
                    }
                    data->items[itemIndex].id = itemID;
                    data->items[itemIndex].offset = (uint32_t)(baseOffset + extentOffset);
                    data->items[itemIndex].size = (uint32_t)extentLength;
                } else {
                    // TODO: support more than one extent
                    return AVIF_FALSE;
                }
            }

        } else if (!memcmp(type, "iinf", 4)) {
            // ItemInfoBox

            uint8_t version;
            CHECK(avifStreamReadVersionAndFlags(&s, &version));
            contentSize -= 4;
            if (version == 0) {
                uint16_t entryCount;
                CHECK(avifStreamReadU16(&s, &entryCount)); // unsigned int(16) entry_count;
                contentSize -= sizeof(uint16_t);
            } else if (version == 1) {
                uint32_t entryCount;
                CHECK(avifStreamReadU32(&s, &entryCount)); // unsigned int(16) entry_count;
                contentSize -= sizeof(uint32_t);
            } else {
                return AVIF_FALSE;
            }

            CHECK(avifParse(data, raw + s.offset, contentSize));

        } else if (!memcmp(type, "infe", 4)) {
            // ItemInfoEntry

            size_t startOffset = s.offset;
            CHECK(avifStreamReadAndEnforceVersion(&s, 2)); // TODO: support version > 2? 2+ is required for item_type

            uint16_t itemID;                                    // unsigned int(16) item_ID;
            CHECK(avifStreamReadU16(&s, &itemID));              //
            uint16_t itemProtectionIndex;                       // unsigned int(16) item_protection_index;
            CHECK(avifStreamReadU16(&s, &itemProtectionIndex)); //
            uint8_t itemType[4];                                // unsigned int(32) item_type;
            CHECK(avifStreamRead(&s, itemType, 4));             //

            // Skip the remainder of the box
            CHECK(avifStreamSkip(&s, contentSize - (s.offset - startOffset)));

            int itemIndex = findItemID(data, itemID);
            if (itemIndex == -1) {
                return AVIF_FALSE;
            }
            memcpy(data->items[itemIndex].type, itemType, sizeof(itemType));

        } else {
            // Unsupported box, move on
            CHECK(avifStreamSkip(&s, contentSize));
        }
    }
    return AVIF_TRUE;
}

static aom_image_t * decodeOBU(aom_codec_ctx_t * decoder, avifRawData * inputOBU)
{
    aom_codec_stream_info_t si;
    aom_codec_iface_t * decoder_interface = aom_codec_av1_dx();
    if (aom_codec_dec_init(decoder, decoder_interface, NULL, 0)) {
        return NULL;
    }

    if (aom_codec_control(decoder, AV1D_SET_OUTPUT_ALL_LAYERS, 1)) {
        return NULL;
    }

    si.is_annexb = 0;
    if (aom_codec_peek_stream_info(decoder_interface, inputOBU->data, inputOBU->size, &si)) {
        return NULL;
    }

    if (aom_codec_decode(decoder, inputOBU->data, inputOBU->size, NULL)) {
        return NULL;
    }

    aom_codec_iter_t iter = NULL;
    return aom_codec_get_frame(decoder, &iter); // It doesn't appear that I own this / need to free this
}

avifResult avifImageRead(avifImage * image, avifRawData * input)
{
    // -----------------------------------------------------------------------
    // Parse BMFF boxes

    avifData data;
    memset(&data, 0, sizeof(data));
    if (!avifParse(&data, input->data, input->size)) {
        return AVIF_RESULT_BMFF_PARSE_FAILED;
    }

    // -----------------------------------------------------------------------

    avifRawData colorOBU = AVIF_RAW_DATA_EMPTY;
    avifRawData alphaOBU = AVIF_RAW_DATA_EMPTY;

    for (int itemIndex = 0; itemIndex < MAX_ITEMS; ++itemIndex) {
        avifItem * item = &data.items[itemIndex];
        if (!item->id || !item->size) {
            break;
        }
        if (item->offset > input->size) {
            break;
        }
        if ((item->offset + item->size) > input->size) {
            break;
        }

        if (memcmp(item->type, "av01", 4)) {
            // probably exif or some other data
            continue;
        }

        if (colorOBU.size == 0) {
            colorOBU.data = input->data + item->offset;
            colorOBU.size = item->size;
        } else {
            alphaOBU.data = input->data + item->offset;
            alphaOBU.size = item->size;
            break;
        }
    }

    if (colorOBU.size == 0) {
        return AVIF_RESULT_NO_AV1_ITEMS_FOUND;
    }
    avifBool hasAlpha = (alphaOBU.size > 0) ? AVIF_TRUE : AVIF_FALSE;

    aom_codec_ctx_t colorDecoder;
    aom_image_t * aomColorImage = decodeOBU(&colorDecoder, &colorOBU);
    if (!aomColorImage) {
        aom_codec_destroy(&colorDecoder);
        return AVIF_RESULT_DECODE_COLOR_FAILED;
    }

    aom_codec_ctx_t alphaDecoder;
    aom_image_t * aomAlphaImage = NULL;
    if (hasAlpha) {
        aomAlphaImage = decodeOBU(&alphaDecoder, &alphaOBU);
        if (!aomAlphaImage) {
            aom_codec_destroy(&colorDecoder);
            aom_codec_destroy(&alphaDecoder);
            return AVIF_RESULT_DECODE_ALPHA_FAILED;
        }
        if ((aomColorImage->d_w != aomAlphaImage->d_w) || (aomColorImage->d_h != aomAlphaImage->d_h)) {
            aom_codec_destroy(&colorDecoder);
            aom_codec_destroy(&alphaDecoder);
            return AVIF_RESULT_COLOR_ALPHA_SIZE_MISMATCH;
        }
    }

    if (data.ispe_seen && ((data.ispe_width != aomColorImage->d_w) || (data.ispe_height != aomColorImage->d_h))) {
        aom_codec_destroy(&colorDecoder);
        if (hasAlpha) {
            aom_codec_destroy(&alphaDecoder);
        }
        return AVIF_RESULT_ISPE_SIZE_MISMATCH;
    }

    avifPixelFormat pixelFormat;
    int xShift = 0;
    int yShift = 0;
    switch (aomColorImage->fmt) {
        case AOM_IMG_FMT_I42016:
            pixelFormat = AVIF_PIXEL_FORMAT_YUV420;
            xShift = 1;
            yShift = 1;
            break;
        case AOM_IMG_FMT_I44416:
            pixelFormat = AVIF_PIXEL_FORMAT_YUV444;
            break;
        default:
            aom_codec_destroy(&colorDecoder);
            if (hasAlpha) {
                aom_codec_destroy(&alphaDecoder);
            }
            return AVIF_UNSUPPORTED_PIXEL_FORMAT;
    }

    avifImageCreatePixels(image, pixelFormat, aomColorImage->d_w, aomColorImage->d_h, aomColorImage->bit_depth);

    uint16_t maxChannel = (1 << image->depth) - 1;
    for (int j = 0; j < image->height; ++j) {
        for (int i = 0; i < image->width; ++i) {
            uint16_t * planeChannel;
            int x = i >> xShift;
            int y = j >> yShift;

            planeChannel = (uint16_t *)&aomColorImage->planes[0][(j * aomColorImage->stride[0]) + (2 * i)];
            image->planes[0][i + (j * image->strides[0])] = *planeChannel;
            planeChannel = (uint16_t *)&aomColorImage->planes[1][(y * aomColorImage->stride[1]) + (2 * x)];
            image->planes[1][x + (y * image->strides[1])] = *planeChannel;
            planeChannel = (uint16_t *)&aomColorImage->planes[2][(y * aomColorImage->stride[2]) + (2 * x)];
            image->planes[2][x + (y * image->strides[2])] = *planeChannel;

            if (hasAlpha) {
                uint16_t * planeChannel = (uint16_t *)&aomAlphaImage->planes[0][(j * aomColorImage->stride[0]) + (2 * i)];
                image->planes[3][i + (j * image->strides[3])] = *planeChannel;
            } else {
                image->planes[3][i + (j * image->strides[3])] = maxChannel;
            }
        }
    }

    aom_codec_destroy(&colorDecoder);
    if (hasAlpha) {
        aom_codec_destroy(&alphaDecoder);
    }
    return AVIF_RESULT_OK;
}
