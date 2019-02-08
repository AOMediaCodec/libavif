// Copyright 2019 Joe Drago. All rights reserved.
// SPDX-License-Identifier: BSD-2-Clause

#include "avif/internal.h"

#include <string.h>

#include "aom/aom_encoder.h"
#include "aom/aomcx.h"

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

static avifBool encodeOBU(avifImage * image, avifBool alphaOnly, avifRawData * outputOBU, int quality);
static avifBool avifImageIsOpaque(avifImage * image);

avifResult avifImageWrite(avifImage * image, avifRawData * output, int quality)
{
    if ((image->depth != 8) && (image->depth != 10) && (image->depth != 12)) {
        return AVIF_RESULT_UNSUPPORTED_DEPTH;
    }

    avifResult result = AVIF_RESULT_UNKNOWN_ERROR;
    avifRawData colorOBU = AVIF_RAW_DATA_EMPTY;
    avifRawData alphaOBU = AVIF_RAW_DATA_EMPTY;

    avifStream s;
    avifStreamStart(&s, output);

    // -----------------------------------------------------------------------
    // Reformat pixels, if need be

    if (!image->width || !image->height || !image->depth) {
        result = AVIF_RESULT_NO_CONTENT;
        goto writeCleanup;
    }

    if ((image->yuvFormat == AVIF_PIXEL_FORMAT_NONE) || !image->yuvPlanes[AVIF_CHAN_Y] || !image->yuvPlanes[AVIF_CHAN_U] || !image->yuvPlanes[AVIF_CHAN_V]) {
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

    if (!encodeOBU(image, AVIF_FALSE, &colorOBU, quality)) {
        result = AVIF_RESULT_ENCODE_COLOR_FAILED;
        goto writeCleanup;
    }

    // Skip alpha creation on opaque images
    avifBool hasAlpha = AVIF_FALSE;
    if (!avifImageIsOpaque(image)) {
        if (!encodeOBU(image, AVIF_TRUE, &alphaOBU, quality)) {
            result = AVIF_RESULT_ENCODE_ALPHA_FAILED;
            goto writeCleanup;
        }
        hasAlpha = AVIF_TRUE;
    }

    // -----------------------------------------------------------------------
    // Write ftyp

    avifBoxMarker ftyp = avifStreamWriteBox(&s, "ftyp", -1, 0);
    avifStreamWrite(&s, "mif1", 4); // unsigned int(32) major_brand;
    avifStreamWriteU32(&s, 0);      // unsigned int(32) minor_version;
    avifStreamWrite(&s, "mif1", 4); // unsigned int(32) compatible_brands[];
    avifStreamWrite(&s, "avif", 4); // ... compatible_brands[]
    avifStreamWrite(&s, "miaf", 4); // ... compatible_brands[]
    avifStreamFinishBox(&s, ftyp);

    // -----------------------------------------------------------------------
    // Write mdat

    avifBoxMarker mdat = avifStreamWriteBox(&s, "mdat", -1, 0);
    uint32_t colorOBUOffset = (uint32_t)s.offset;
    avifStreamWrite(&s, colorOBU.data, colorOBU.size);
    uint32_t alphaOBUOffset = (uint32_t)s.offset;
    avifStreamWrite(&s, alphaOBU.data, alphaOBU.size);
    avifStreamFinishBox(&s, mdat);

    // -----------------------------------------------------------------------
    // Start meta

    avifBoxMarker meta = avifStreamWriteBox(&s, "meta", 0, 0);

    // -----------------------------------------------------------------------
    // Write hdlr

    avifBoxMarker hdlr = avifStreamWriteBox(&s, "hdlr", 0, 0);
    avifStreamWriteU32(&s, 0);         // unsigned int(32) pre_defined = 0;
    avifStreamWrite(&s, "pict", 4);    // unsigned int(32) handler_type;
    avifStreamWriteZeros(&s, 12);      // const unsigned int(32)[3] reserved = 0;
    avifStreamWrite(&s, "libavif", 8); // string name; (writing null terminator)
    avifStreamFinishBox(&s, hdlr);

    // -----------------------------------------------------------------------
    // Write pitm

    avifStreamWriteBox(&s, "pitm", 0, sizeof(uint16_t));
    avifStreamWriteU16(&s, 1); //  unsigned int(16) item_ID;

    // -----------------------------------------------------------------------
    // Write iloc

    avifBoxMarker iloc = avifStreamWriteBox(&s, "iloc", 0, 0);

    // iloc header
    uint8_t offsetSizeAndLengthSize = (4 << 4) + (4 << 0); // unsigned int(4) offset_size; unsigned int(4) length_size;
    avifStreamWrite(&s, &offsetSizeAndLengthSize, 1);      //
    avifStreamWriteZeros(&s, 1);                           // unsigned int(4) base_offset_size; unsigned int(4) reserved;
    avifStreamWriteU16(&s, hasAlpha ? 2 : 1);              // unsigned int(16) item_count;

    // Item ID #1 (Color OBU)
    avifStreamWriteU16(&s, 1);                       // unsigned int(16) item_ID;
    avifStreamWriteU16(&s, 0);                       // unsigned int(16) data_reference_index;
    avifStreamWriteU16(&s, 1);                       // unsigned int(16) extent_count;
    avifStreamWriteU32(&s, colorOBUOffset);          // unsigned int(offset_size*8) extent_offset;
    avifStreamWriteU32(&s, (uint32_t)colorOBU.size); // unsigned int(length_size*8) extent_length;

    if (hasAlpha) {
        avifStreamWriteU16(&s, 2);                       // unsigned int(16) item_ID;
        avifStreamWriteU16(&s, 0);                       // unsigned int(16) data_reference_index;
        avifStreamWriteU16(&s, 1);                       // unsigned int(16) extent_count;
        avifStreamWriteU32(&s, alphaOBUOffset);          // unsigned int(offset_size*8) extent_offset;
        avifStreamWriteU32(&s, (uint32_t)alphaOBU.size); // unsigned int(length_size*8) extent_length;
    }

    avifStreamFinishBox(&s, iloc);

    // -----------------------------------------------------------------------
    // Write iinf

    avifBoxMarker iinf = avifStreamWriteBox(&s, "iinf", 0, 0);
    avifStreamWriteU16(&s, hasAlpha ? 2 : 1); //  unsigned int(16) entry_count;

    avifBoxMarker infe0 = avifStreamWriteBox(&s, "infe", 2, 0);
    avifStreamWriteU16(&s, 1);       // unsigned int(16) item_ID;
    avifStreamWriteU16(&s, 0);       // unsigned int(16) item_protection_index;
    avifStreamWrite(&s, "av01", 4);  // unsigned int(32) item_type;
    avifStreamWrite(&s, "Color", 6); // string item_name; (writing null terminator)
    avifStreamFinishBox(&s, infe0);
    if (hasAlpha) {
        avifBoxMarker infe1 = avifStreamWriteBox(&s, "infe", 2, 0);
        avifStreamWriteU16(&s, 2);       // unsigned int(16) item_ID;
        avifStreamWriteU16(&s, 0);       // unsigned int(16) item_protection_index;
        avifStreamWrite(&s, "av01", 4);  // unsigned int(32) item_type;
        avifStreamWrite(&s, "Alpha", 6); // string item_name; (writing null terminator)
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

            if ((image->profileFormat == AVIF_PROFILE_FORMAT_ICC) && image->icc.data && (image->icc.data > 0)) {
                avifBoxMarker colr = avifStreamWriteBox(&s, "colr", -1, 0);
                avifStreamWrite(&s, "prof", 4); // unsigned int(32) colour_type;
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

            if (hasAlpha) {
                avifBoxMarker pixiA = avifStreamWriteBox(&s, "pixi", 0, 0);
                avifStreamWriteU8(&s, 1);            // unsigned int (8) num_channels;
                avifStreamWriteU8(&s, image->depth); // unsigned int (8) bits_per_channel;
                avifStreamFinishBox(&s, pixiA);
                ++ipcoIndex;
                ipmaPush(&ipmaAlpha, ipcoIndex);

                avifBoxMarker auxC = avifStreamWriteBox(&s, "auxC", 0, 0);
                avifStreamWrite(&s, alphaURN, alphaURNSize); //  string aux_type;
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

            avifStreamWriteU16(&s, 1);              // unsigned int(16) item_ID;
            avifStreamWriteU8(&s, ipmaColor.count); // unsigned int(8) association_count;
            for (int i = 0; i < ipmaColor.count; ++i) {
                avifStreamWriteU8(&s, ipmaColor.associations[i]); // bit(1) essential; unsigned int(7) property_index;
            }

            if (hasAlpha) {
                avifStreamWriteU16(&s, 2);              // unsigned int(16) item_ID;
                avifStreamWriteU8(&s, ipmaAlpha.count); // unsigned int(8) association_count;
                for (int i = 0; i < ipmaAlpha.count; ++i) {
                    avifStreamWriteU8(&s, ipmaAlpha.associations[i]); // bit(1) essential; unsigned int(7) property_index;
                }
            }
        }
        avifStreamFinishBox(&s, ipma);
    }
    avifStreamFinishBox(&s, iprp);

    // -----------------------------------------------------------------------
    // Finish up stream

    avifStreamFinishBox(&s, meta);
    avifStreamFinishWrite(&s);
    result = AVIF_RESULT_OK;

    // -----------------------------------------------------------------------
    // Cleanup

writeCleanup:
    avifRawDataFree(&colorOBU);
    avifRawDataFree(&alphaOBU);
    return result;
}

static aom_img_fmt_t avifImageCalcAOMFmt(avifImage * image, avifBool alphaOnly, int * yShift)
{
    *yShift = 0;

    aom_img_fmt_t fmt;
    if (alphaOnly) {
        // We're going monochrome, who cares about chroma quality
        fmt = AOM_IMG_FMT_I420;
        *yShift = 1;
    } else {
        switch (image->yuvFormat) {
            case AVIF_PIXEL_FORMAT_YUV444:
                fmt = AOM_IMG_FMT_I444;
                break;
            case AVIF_PIXEL_FORMAT_YUV422:
                fmt = AOM_IMG_FMT_I422;
                break;
            case AVIF_PIXEL_FORMAT_YUV420:
                fmt = AOM_IMG_FMT_I420;
                *yShift = 1;
                break;
            case AVIF_PIXEL_FORMAT_YV12:
                fmt = AOM_IMG_FMT_YV12;
                *yShift = 1;
                break;
            default:
                return AOM_IMG_FMT_NONE;
        }
    }

    if (image->depth > 8) {
        fmt |= AOM_IMG_FMT_HIGHBITDEPTH;
    }

    return fmt;
}

static avifBool encodeOBU(avifImage * image, avifBool alphaOnly, avifRawData * outputOBU, int quality)
{
    avifBool success = AVIF_FALSE;
    aom_codec_iface_t * encoder_interface = aom_codec_av1_cx();
    aom_codec_ctx_t encoder;

    int yShift = 0;
    aom_img_fmt_t aomFormat = avifImageCalcAOMFmt(image, alphaOnly, &yShift);
    if (aomFormat == AOM_IMG_FMT_NONE) {
        return AVIF_FALSE;
    }

    struct aom_codec_enc_cfg cfg;
    aom_codec_enc_config_default(encoder_interface, &cfg, 0);

    // Profile 0.  8-bit and 10-bit 4:2:0 and 4:0:0 only.
    // Profile 1.  8-bit and 10-bit 4:4:4
    // Profile 2.  8-bit and 10-bit 4:2:2
    //            12-bit  4:0:0, 4:2:2 and 4:4:4
    if (image->depth == 12) {
        // Only profile 2 can handle 12 bit
        cfg.g_profile = 2;
    } else {
        // 8-bit or 10-bit

        if (alphaOnly) {
            // Assuming aomImage->monochrome makes it 4:0:0
            cfg.g_profile = 0;
        } else {
            switch (image->yuvFormat) {
                case AVIF_PIXEL_FORMAT_YUV444: cfg.g_profile = 1; break;
                case AVIF_PIXEL_FORMAT_YUV422: cfg.g_profile = 2; break;
                case AVIF_PIXEL_FORMAT_YUV420: cfg.g_profile = 0; break;
                case AVIF_PIXEL_FORMAT_YV12:   cfg.g_profile = 0; break;
            }
        }
    }

    cfg.g_bit_depth = image->depth;
    cfg.g_input_bit_depth = image->depth;
    cfg.g_w = image->width;
    cfg.g_h = image->height;
    // cfg.g_threads = ...;

    avifBool lossless = (quality == 0) || (quality == 100) || alphaOnly; // alpha is always lossless
    cfg.rc_min_quantizer = 0;
    if (lossless) {
        cfg.rc_max_quantizer = 0;
    } else {
        cfg.rc_max_quantizer = quality;
    }

    uint32_t encoderFlags = 0;
    if (image->depth > 8) {
        encoderFlags |= AOM_CODEC_USE_HIGHBITDEPTH;
    }
    aom_codec_enc_init(&encoder, encoder_interface, &cfg, encoderFlags);
    if (lossless) {
        aom_codec_control(&encoder, AV1E_SET_LOSSLESS, 1);
    }

    int uvHeight = image->height >> yShift;
    aom_image_t * aomImage = aom_img_alloc(NULL, aomFormat, image->width, image->height, 16);

    if (alphaOnly) {
        aomImage->range = AOM_CR_FULL_RANGE; // Alpha is always full range
        aom_codec_control(&encoder, AV1E_SET_COLOR_RANGE, aomImage->range);
        aomImage->monochrome = 1;
        for (int j = 0; j < image->height; ++j) {
            uint8_t * srcAlphaRow = &image->alphaPlane[j * image->alphaRowBytes];
            uint8_t * dstAlphaRow = &aomImage->planes[0][j * aomImage->stride[0]];
            memcpy(dstAlphaRow, srcAlphaRow, image->alphaRowBytes);
        }

        for (int j = 0; j < uvHeight; ++j) {
            // Zero out U and V
            memset(&aomImage->planes[1][j * aomImage->stride[1]], 0, aomImage->stride[1]);
            memset(&aomImage->planes[2][j * aomImage->stride[2]], 0, aomImage->stride[2]);
        }
    } else {
        aomImage->range = (image->yuvRange == AVIF_RANGE_FULL) ? AOM_CR_FULL_RANGE : AOM_CR_STUDIO_RANGE;
        aom_codec_control(&encoder, AV1E_SET_COLOR_RANGE, aomImage->range);
        for (int j = 0; j < image->height; ++j) {
            for (int yuvPlane = 0; yuvPlane < 3; ++yuvPlane) {
                if ((yuvPlane > 0) && (j >= uvHeight)) {
                    // Bail out if we're on a half-height UV plane
                    break;
                }

                uint8_t * srcRow = &image->yuvPlanes[yuvPlane][j * image->yuvRowBytes[yuvPlane]];
                uint8_t * dstRow = &aomImage->planes[yuvPlane][j * aomImage->stride[yuvPlane]];
                memcpy(dstRow, srcRow, image->yuvRowBytes[yuvPlane]);
            }
        }
    }

    aom_codec_encode(&encoder, aomImage, 0, 1, 0);
    aom_codec_encode(&encoder, NULL, 0, 1, 0); // flush

    aom_codec_iter_t iter = NULL;
    for (;;) {
        const aom_codec_cx_pkt_t * pkt = aom_codec_get_cx_data(&encoder, &iter);
        if (pkt == NULL)
            break;
        if (pkt->kind == AOM_CODEC_CX_FRAME_PKT) {
            avifRawDataSet(outputOBU, pkt->data.frame.buf, pkt->data.frame.sz);
            success = AVIF_TRUE;
            break;
        }
    }

    aom_img_free(aomImage);
    aom_codec_destroy(&encoder);
    return success;
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
