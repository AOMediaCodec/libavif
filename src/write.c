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

static avifBool avifImageIsOpaque(avifImage * image);
static void writeConfigBox(avifStream * s, avifCodecConfigurationBox * cfg);

avifEncoder * avifEncoderCreate(void)
{
    avifEncoder * encoder = (avifEncoder *)avifAlloc(sizeof(avifEncoder));
    memset(encoder, 0, sizeof(avifEncoder));
    encoder->maxThreads = 1;
    encoder->quality = AVIF_BEST_QUALITY;
    return encoder;
}

void avifEncoderDestroy(avifEncoder * encoder)
{
    avifFree(encoder);
}

avifResult avifEncoderWrite(avifEncoder * encoder, avifImage * image, avifRawData * output)
{
    if ((image->depth != 8) && (image->depth != 10) && (image->depth != 12)) {
        return AVIF_RESULT_UNSUPPORTED_DEPTH;
    }

    avifResult result = AVIF_RESULT_UNKNOWN_ERROR;
    avifRawData colorOBU = AVIF_RAW_DATA_EMPTY;
    avifRawData alphaOBU = AVIF_RAW_DATA_EMPTY;
    avifCodec * codec = NULL;

#ifdef AVIF_CODEC_AOM
    codec = avifCodecCreateAOM();
#else
    // Just bail out early, we're not surviving this function without an encoder compiled in
    return AVIF_RESULT_NO_CODEC_AVAILABLE;
#endif

    avifStream s;
    avifStreamStart(&s, output);

    // -----------------------------------------------------------------------
    // Reformat pixels, if need be

    if (!image->width || !image->height || !image->depth) {
        result = AVIF_RESULT_NO_CONTENT;
        goto writeCleanup;
    }

    if ((image->yuvFormat == AVIF_PIXEL_FORMAT_NONE) || !image->yuvPlanes[AVIF_CHAN_Y] || !image->yuvPlanes[AVIF_CHAN_U] ||
        !image->yuvPlanes[AVIF_CHAN_V]) {
        if (!image->rgbPlanes[AVIF_CHAN_R] || !image->rgbPlanes[AVIF_CHAN_G] || !image->rgbPlanes[AVIF_CHAN_B]) {
            result = AVIF_RESULT_NO_CONTENT;
            goto writeCleanup;
        }

        avifImageFreePlanes(image, AVIF_PLANES_YUV);
        if (image->yuvFormat == AVIF_PIXEL_FORMAT_NONE) {
            result = AVIF_RESULT_NO_YUV_FORMAT_SELECTED;
            goto writeCleanup;
        }
        avifImageRGBToYUV(image);
    }

    // -----------------------------------------------------------------------
    // Encode AV1 OBUs

    avifRawData * alphaOBUPtr = &alphaOBU;
    if (avifImageIsOpaque(image)) {
        alphaOBUPtr = NULL;
    }

    avifResult encodeResult = codec->encodeImage(codec, image, encoder, &colorOBU, alphaOBUPtr);
    if (encodeResult != AVIF_RESULT_OK) {
        result = encodeResult;
        goto writeCleanup;
    }
    avifBool hasAlpha = (alphaOBU.size > 0) ? AVIF_TRUE : AVIF_FALSE;

    // -----------------------------------------------------------------------
    // Write ftyp

    avifBoxMarker ftyp = avifStreamWriteBox(&s, "ftyp", -1, 0);
    avifStreamWriteChars(&s, "avif", 4);                           // unsigned int(32) major_brand;
    avifStreamWriteU32(&s, 0);                                     // unsigned int(32) minor_version;
    avifStreamWriteChars(&s, "avif", 4);                           // unsigned int(32) compatible_brands[];
    avifStreamWriteChars(&s, "mif1", 4);                           // ... compatible_brands[]
    avifStreamWriteChars(&s, "miaf", 4);                           // ... compatible_brands[]
    if ((image->depth == 8) || (image->depth == 10)) {             //
        if (image->yuvFormat == AVIF_PIXEL_FORMAT_YUV420) {        //
            avifStreamWriteChars(&s, "MA1B", 4);                   // ... compatible_brands[]
        } else if (image->yuvFormat == AVIF_PIXEL_FORMAT_YUV444) { //
            avifStreamWriteChars(&s, "MA1A", 4);                   // ... compatible_brands[]
        }
    }
    avifStreamFinishBox(&s, ftyp);

    // -----------------------------------------------------------------------
    // Start meta

    avifBoxMarker meta = avifStreamWriteBox(&s, "meta", 0, 0);

    // -----------------------------------------------------------------------
    // Write hdlr

    avifBoxMarker hdlr = avifStreamWriteBox(&s, "hdlr", 0, 0);
    avifStreamWriteU32(&s, 0);              // unsigned int(32) pre_defined = 0;
    avifStreamWriteChars(&s, "pict", 4);    // unsigned int(32) handler_type;
    avifStreamWriteZeros(&s, 12);           // const unsigned int(32)[3] reserved = 0;
    avifStreamWriteChars(&s, "libavif", 8); // string name; (writing null terminator)
    avifStreamFinishBox(&s, hdlr);

    // -----------------------------------------------------------------------
    // Write pitm

    avifStreamWriteBox(&s, "pitm", 0, sizeof(uint16_t));
    avifStreamWriteU16(&s, 1); //  unsigned int(16) item_ID;

    // -----------------------------------------------------------------------
    // Write iloc

    // Remember where we want to store the offsets to the mdat OBU offsets to adjust them later.
    size_t colorOBUOffsetOffset = 0;
    size_t alphaOBUOffsetOffset = 0;

    avifBoxMarker iloc = avifStreamWriteBox(&s, "iloc", 0, 0);

    // iloc header
    uint8_t offsetSizeAndLengthSize = (4 << 4) + (4 << 0); // unsigned int(4) offset_size;
                                                           // unsigned int(4) length_size;
    avifStreamWrite(&s, &offsetSizeAndLengthSize, 1);      //
    avifStreamWriteZeros(&s, 1);                           // unsigned int(4) base_offset_size;
                                                           // unsigned int(4) reserved;
    avifStreamWriteU16(&s, hasAlpha ? 2 : 1);              // unsigned int(16) item_count;

    // Item ID #1 (Color OBU)
    avifStreamWriteU16(&s, 1);                       // unsigned int(16) item_ID;
    avifStreamWriteU16(&s, 0);                       // unsigned int(16) data_reference_index;
    avifStreamWriteU16(&s, 1);                       // unsigned int(16) extent_count;
    colorOBUOffsetOffset = avifStreamOffset(&s);     //
    avifStreamWriteU32(&s, 0 /* set later */);       // unsigned int(offset_size*8) extent_offset;
    avifStreamWriteU32(&s, (uint32_t)colorOBU.size); // unsigned int(length_size*8) extent_length;

    if (hasAlpha) {
        avifStreamWriteU16(&s, 2);                       // unsigned int(16) item_ID;
        avifStreamWriteU16(&s, 0);                       // unsigned int(16) data_reference_index;
        avifStreamWriteU16(&s, 1);                       // unsigned int(16) extent_count;
        alphaOBUOffsetOffset = avifStreamOffset(&s);     //
        avifStreamWriteU32(&s, 0 /* set later */);       // unsigned int(offset_size*8) extent_offset;
        avifStreamWriteU32(&s, (uint32_t)alphaOBU.size); // unsigned int(length_size*8) extent_length;
    }

    avifStreamFinishBox(&s, iloc);

    // -----------------------------------------------------------------------
    // Write iinf

    avifBoxMarker iinf = avifStreamWriteBox(&s, "iinf", 0, 0);
    avifStreamWriteU16(&s, hasAlpha ? 2 : 1); //  unsigned int(16) entry_count;

    avifBoxMarker infe0 = avifStreamWriteBox(&s, "infe", 2, 0);
    avifStreamWriteU16(&s, 1);            // unsigned int(16) item_ID;
    avifStreamWriteU16(&s, 0);            // unsigned int(16) item_protection_index;
    avifStreamWriteChars(&s, "av01", 4);  // unsigned int(32) item_type;
    avifStreamWriteChars(&s, "Color", 6); // string item_name; (writing null terminator)
    avifStreamFinishBox(&s, infe0);
    if (hasAlpha) {
        avifBoxMarker infe1 = avifStreamWriteBox(&s, "infe", 2, 0);
        avifStreamWriteU16(&s, 2);            // unsigned int(16) item_ID;
        avifStreamWriteU16(&s, 0);            // unsigned int(16) item_protection_index;
        avifStreamWriteChars(&s, "av01", 4);  // unsigned int(32) item_type;
        avifStreamWriteChars(&s, "Alpha", 6); // string item_name; (writing null terminator)
        avifStreamFinishBox(&s, infe1);
    }
    avifStreamFinishBox(&s, iinf);

    // -----------------------------------------------------------------------
    // Write iref (auxl) for alpha, if any

    if (hasAlpha) {
        avifBoxMarker iref = avifStreamWriteBox(&s, "iref", 0, 0);
        avifBoxMarker auxl = avifStreamWriteBox(&s, "auxl", -1, 0);
        avifStreamWriteU16(&s, 2); // unsigned int(16) from_item_ID;
        avifStreamWriteU16(&s, 1); // unsigned int(16) reference_count;
        avifStreamWriteU16(&s, 1); // unsigned int(16) to_item_ID;
        avifStreamFinishBox(&s, auxl);
        avifStreamFinishBox(&s, iref);
    }

    // -----------------------------------------------------------------------
    // Write iprp->ipco->ispe

    avifBoxMarker iprp = avifStreamWriteBox(&s, "iprp", -1, 0);
    {
        uint8_t ipcoIndex = 0;
        struct ipmaArray ipmaColor;
        memset(&ipmaColor, 0, sizeof(ipmaColor));
        struct ipmaArray ipmaAlpha;
        memset(&ipmaAlpha, 0, sizeof(ipmaAlpha));

        avifBoxMarker ipco = avifStreamWriteBox(&s, "ipco", -1, 0);
        {
            avifBoxMarker ispe = avifStreamWriteBox(&s, "ispe", 0, 0);
            avifStreamWriteU32(&s, image->width);  // unsigned int(32) image_width;
            avifStreamWriteU32(&s, image->height); // unsigned int(32) image_height;
            avifStreamFinishBox(&s, ispe);
            ++ipcoIndex;
            ipmaPush(&ipmaColor, ipcoIndex); // ipma is 1-indexed, doing this afterwards is correct
            ipmaPush(&ipmaAlpha, ipcoIndex); // Alpha shares the ispe prop

            if (image->profileFormat == AVIF_PROFILE_FORMAT_NCLX) {
                avifBoxMarker colr = avifStreamWriteBox(&s, "colr", -1, 0);
                avifStreamWriteChars(&s, "nclx", 4);                         // unsigned int(32) colour_type;
                avifStreamWriteU16(&s, image->nclx.colourPrimaries);         // unsigned int(16) colour_primaries;
                avifStreamWriteU16(&s, image->nclx.transferCharacteristics); // unsigned int(16) transfer_characteristics;
                avifStreamWriteU16(&s, image->nclx.matrixCoefficients);      // unsigned int(16) matrix_coefficients;
                avifStreamWriteU8(&s, image->nclx.fullRangeFlag & 0x80);     // unsigned int(1) full_range_flag;
                                                                             // unsigned int(7) reserved = 0;
                avifStreamFinishBox(&s, colr);
                ++ipcoIndex;
                ipmaPush(&ipmaColor, ipcoIndex);
            } else if ((image->profileFormat == AVIF_PROFILE_FORMAT_ICC) && image->icc.data && (image->icc.size > 0)) {
                avifBoxMarker colr = avifStreamWriteBox(&s, "colr", -1, 0);
                avifStreamWriteChars(&s, "prof", 4); // unsigned int(32) colour_type;
                avifStreamWrite(&s, image->icc.data, image->icc.size);
                avifStreamFinishBox(&s, colr);
                ++ipcoIndex;
                ipmaPush(&ipmaColor, ipcoIndex);
            }

            avifBoxMarker pixiC = avifStreamWriteBox(&s, "pixi", 0, 0);
            avifStreamWriteU8(&s, 3);            // unsigned int (8) num_channels;
            avifStreamWriteU8(&s, image->depth); // unsigned int (8) bits_per_channel;
            avifStreamWriteU8(&s, image->depth); // unsigned int (8) bits_per_channel;
            avifStreamWriteU8(&s, image->depth); // unsigned int (8) bits_per_channel;
            avifStreamFinishBox(&s, pixiC);
            ++ipcoIndex;
            ipmaPush(&ipmaColor, ipcoIndex);

            avifCodecConfigurationBox colorConfig;
            codec->getConfigurationBox(codec, AVIF_CODEC_PLANES_COLOR, &colorConfig);
            writeConfigBox(&s, &colorConfig);
            ++ipcoIndex;
            ipmaPush(&ipmaColor, ipcoIndex);

            if (hasAlpha) {
                avifBoxMarker pixiA = avifStreamWriteBox(&s, "pixi", 0, 0);
                avifStreamWriteU8(&s, 1);            // unsigned int (8) num_channels;
                avifStreamWriteU8(&s, image->depth); // unsigned int (8) bits_per_channel;
                avifStreamFinishBox(&s, pixiA);
                ++ipcoIndex;
                ipmaPush(&ipmaAlpha, ipcoIndex);

                avifCodecConfigurationBox alphaConfig;
                codec->getConfigurationBox(codec, AVIF_CODEC_PLANES_ALPHA, &alphaConfig);
                writeConfigBox(&s, &alphaConfig);
                ++ipcoIndex;
                ipmaPush(&ipmaAlpha, ipcoIndex);

                avifBoxMarker auxC = avifStreamWriteBox(&s, "auxC", 0, 0);
                avifStreamWriteChars(&s, alphaURN, alphaURNSize); //  string aux_type;
                avifStreamFinishBox(&s, auxC);
                ++ipcoIndex;
                ipmaPush(&ipmaAlpha, ipcoIndex);
            }
        }
        avifStreamFinishBox(&s, ipco);

        avifBoxMarker ipma = avifStreamWriteBox(&s, "ipma", 0, 0);
        {
            int ipmaCount = hasAlpha ? 2 : 1;
            avifStreamWriteU32(&s, ipmaCount); // unsigned int(32) entry_count;

            avifStreamWriteU16(&s, 1);                            // unsigned int(16) item_ID;
            avifStreamWriteU8(&s, ipmaColor.count);               // unsigned int(8) association_count;
            for (int i = 0; i < ipmaColor.count; ++i) {           //
                avifStreamWriteU8(&s, ipmaColor.associations[i]); // bit(1) essential; unsigned int(7) property_index;
            }

            if (hasAlpha) {
                avifStreamWriteU16(&s, 2);                            // unsigned int(16) item_ID;
                avifStreamWriteU8(&s, ipmaAlpha.count);               // unsigned int(8) association_count;
                for (int i = 0; i < ipmaAlpha.count; ++i) {           //
                    avifStreamWriteU8(&s, ipmaAlpha.associations[i]); // bit(1) essential; unsigned int(7) property_index;
                }
            }
        }
        avifStreamFinishBox(&s, ipma);
    }
    avifStreamFinishBox(&s, iprp);

    // -----------------------------------------------------------------------
    // Finish meta box

    avifStreamFinishBox(&s, meta);

    // -----------------------------------------------------------------------
    // Write mdat

    avifBoxMarker mdat = avifStreamWriteBox(&s, "mdat", -1, 0);
    uint32_t colorOBUOffset = (uint32_t)s.offset;
    avifStreamWrite(&s, colorOBU.data, colorOBU.size);
    uint32_t alphaOBUOffset = (uint32_t)s.offset;
    avifStreamWrite(&s, alphaOBU.data, alphaOBU.size);
    avifStreamFinishBox(&s, mdat);

    // -----------------------------------------------------------------------
    // Finish up stream

    // Set offsets needed in meta box based on where we eventually wrote mdat
    size_t prevOffset = avifStreamOffset(&s);
    if (colorOBUOffsetOffset != 0) {
        avifStreamSetOffset(&s, colorOBUOffsetOffset);
        avifStreamWriteU32(&s, colorOBUOffset);
    }
    if (alphaOBUOffsetOffset != 0) {
        avifStreamSetOffset(&s, alphaOBUOffsetOffset);
        avifStreamWriteU32(&s, alphaOBUOffset);
    }
    avifStreamSetOffset(&s, prevOffset);

    // Close write stream
    avifStreamFinishWrite(&s);

    // -----------------------------------------------------------------------
    // IO stats

    encoder->ioStats.colorOBUSize = colorOBU.size;
    encoder->ioStats.alphaOBUSize = alphaOBU.size;

    // -----------------------------------------------------------------------
    // Set result and cleanup

    result = AVIF_RESULT_OK;

writeCleanup:
    if (codec) {
        avifCodecDestroy(codec);
    }
    avifRawDataFree(&colorOBU);
    avifRawDataFree(&alphaOBU);
    return result;
}

static avifBool avifImageIsOpaque(avifImage * image)
{
    if (!image->alphaPlane) {
        return AVIF_TRUE;
    }

    int maxChannel = (1 << image->depth) - 1;
    if (avifImageUsesU16(image)) {
        for (int j = 0; j < image->height; ++j) {
            for (int i = 0; i < image->width; ++i) {
                uint16_t * p = (uint16_t *)&image->alphaPlane[(i * 2) + (j * image->alphaRowBytes)];
                if (*p != maxChannel) {
                    return AVIF_FALSE;
                }
            }
        }
    } else {
        for (int j = 0; j < image->height; ++j) {
            for (int i = 0; i < image->width; ++i) {
                if (image->alphaPlane[i + (j * image->alphaRowBytes)] != maxChannel) {
                    return AVIF_FALSE;
                }
            }
        }
    }
    return AVIF_TRUE;
}

static void writeConfigBox(avifStream * s, avifCodecConfigurationBox * cfg)
{
    avifBoxMarker av1C = avifStreamWriteBox(s, "av1C", -1, 0);

    // unsigned int (1) marker = 1;
    // unsigned int (7) version = 1;
    avifStreamWriteU8(s, 0x80 | 0x1);

    // unsigned int (3) seq_profile;
    // unsigned int (5) seq_level_idx_0;
    avifStreamWriteU8(s, ((cfg->seqProfile & 0x7) << 5) | (cfg->seqLevelIdx0 & 0x1f));

    uint8_t bits = 0;
    bits |= (cfg->seqTier0 & 0x1) << 7;           // unsigned int (1) seq_tier_0;
    bits |= (cfg->highBitdepth & 0x1) << 6;       // unsigned int (1) high_bitdepth;
    bits |= (cfg->twelveBit & 0x1) << 5;          // unsigned int (1) twelve_bit;
    bits |= (cfg->monochrome & 0x1) << 4;         // unsigned int (1) monochrome;
    bits |= (cfg->chromaSubsamplingX & 0x1) << 3; // unsigned int (1) chroma_subsampling_x;
    bits |= (cfg->chromaSubsamplingY & 0x1) << 2; // unsigned int (1) chroma_subsampling_y;
    bits |= (cfg->chromaSamplePosition & 0x3);    // unsigned int (2) chroma_sample_position;
    avifStreamWriteU8(s, bits);

    // unsigned int (3) reserved = 0;
    // unsigned int (1) initial_presentation_delay_present;
    // if (initial_presentation_delay_present) {
    //   unsigned int (4) initial_presentation_delay_minus_one;
    // } else {
    //   unsigned int (4) reserved = 0;
    // }
    avifStreamWriteU8(s, 0);

    avifStreamFinishBox(s, av1C);
}
