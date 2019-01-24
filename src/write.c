// Copyright 2019 Joe Drago. All rights reserved.
// SPDX-License-Identifier: BSD-2-Clause

#include "avif/internal.h"

#include <string.h>

#include "aom/aom_encoder.h"
#include "aom/aomcx.h"

static avifBool encodeOBU(avifImage * image, avifBool alphaOnly, avifRawData * outputOBU, int quality);
static avifBool avifImageIsOpaque(avifImage * image);

avifResult avifImageWrite(avifImage * image, avifRawData * output, int quality)
{
    avifResult result = AVIF_RESULT_UNKNOWN_ERROR;
    avifRawData colorOBU = AVIF_RAW_DATA_EMPTY;
    avifRawData alphaOBU = AVIF_RAW_DATA_EMPTY;

    avifStream s;
    avifStreamStart(&s, output);

    // -----------------------------------------------------------------------
    // Reformat pixels, if need be

    avifPixelFormat dstPixelFormat = image->pixelFormat;
    if (image->pixelFormat == AVIF_PIXEL_FORMAT_RGBA) {
        // AV1 doesn't support RGB, reformat
        dstPixelFormat = AVIF_PIXEL_FORMAT_YUV444;
    }

#if 0 // TODO: implement choice in depth
    int dstDepth = AVIF_CLAMP(image->depth, 8, 12);
    if ((dstDepth == 9) || (dstDepth == 11)) {
        ++dstDepth;
    }
#else
    int dstDepth = 12;
#endif

    avifImage * pixelImage = image;
    avifImage * reformattedImage = NULL;
    if ((image->pixelFormat != dstPixelFormat) || (image->depth != dstDepth)) {
        reformattedImage = avifImageCreate();
        avifResult reformatResult = avifImageReformatPixels(image, reformattedImage, dstPixelFormat, dstDepth);
        if (reformatResult != AVIF_RESULT_OK) {
            result = reformatResult;
            goto writeCleanup;
        }
        pixelImage = reformattedImage;
    }

    // -----------------------------------------------------------------------
    // Encode AV1 OBUs

    if (!encodeOBU(pixelImage, AVIF_FALSE, &colorOBU, quality)) {
        result = AVIF_RESULT_ENCODE_COLOR_FAILED;
        goto writeCleanup;
    }

    // Skip alpha creation on opaque images
    avifBool hasAlpha = AVIF_FALSE;
    if (!avifImageIsOpaque(pixelImage)) {
        if (!encodeOBU(pixelImage, AVIF_TRUE, &alphaOBU, quality)) {
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
    // Write iprp->ipco->ispe

    avifBoxMarker iprp = avifStreamWriteBox(&s, "iprp", -1, 0);
    avifBoxMarker ipco = avifStreamWriteBox(&s, "ipco", -1, 0);
    avifBoxMarker ispe = avifStreamWriteBox(&s, "ispe", 0, 0);
    avifStreamWriteU32(&s, image->width);  // unsigned int(32) image_width;
    avifStreamWriteU32(&s, image->height); // unsigned int(32) image_height;
    avifStreamFinishBox(&s, ispe);
    avifStreamFinishBox(&s, ipco);
    avifBoxMarker ipma = avifStreamWriteBox(&s, "ipma", 0, 0);
    avifStreamWriteU32(&s, 1); // unsigned int(32) entry_count;
    avifStreamWriteU16(&s, 1); // unsigned int(16) item_ID;
    avifStreamWriteU8(&s, 1);  // unsigned int(8) association_count;
    avifStreamWriteU8(&s, 1);  // bit(1) essential; unsigned int(7) property_index;
    avifStreamFinishBox(&s, ipma);
    avifStreamFinishBox(&s, iprp);

    // -----------------------------------------------------------------------
    // Finish up stream

    avifStreamFinishBox(&s, meta);
    avifStreamFinishWrite(&s);
    result = AVIF_RESULT_OK;

    // -----------------------------------------------------------------------
    // Cleanup

writeCleanup:
    if (reformattedImage) {
        avifImageDestroy(reformattedImage);
    }
    avifRawDataFree(&colorOBU);
    avifRawDataFree(&alphaOBU);
    return result;
}

static avifBool encodeOBU(avifImage * image, avifBool alphaOnly, avifRawData * outputOBU, int quality)
{
    avifBool success = AVIF_FALSE;
    aom_codec_iface_t * encoder_interface = aom_codec_av1_cx();
    aom_codec_ctx_t encoder;

    struct aom_codec_enc_cfg cfg;
    aom_codec_enc_config_default(encoder_interface, &cfg, 0);

    // Profile 2.  8-bit and 10-bit 4:2:2
    //            12-bit  4:0:0, 4:2:2 and 4:4:4
    cfg.g_profile = 2;
    cfg.g_bit_depth = AOM_BITS_12;
    cfg.g_input_bit_depth = 12;
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

    aom_codec_enc_init(&encoder, encoder_interface, &cfg, AOM_CODEC_USE_HIGHBITDEPTH);
    aom_codec_control(&encoder, AV1E_SET_COLOR_RANGE, AOM_CR_FULL_RANGE);
    if (lossless) {
        aom_codec_control(&encoder, AV1E_SET_LOSSLESS, 1);
    }

    aom_image_t * aomImage = aom_img_alloc(NULL, AOM_IMG_FMT_I44416, image->width, image->height, 16);
    aomImage->range = AOM_CR_FULL_RANGE; // always use full range
    if (alphaOnly) {
    }

    if (alphaOnly) {
        aomImage->monochrome = 1;
        for (int j = 0; j < image->height; ++j) {
            for (int i = 0; i < image->width; ++i) {
                for (int plane = 0; plane < 3; ++plane) {
                    uint16_t * planeChannel = (uint16_t *)&aomImage->planes[plane][(j * aomImage->stride[plane]) + (2 * i)];
                    if (plane == 0) {
                        *planeChannel = image->planes[3][i + (j * image->strides[plane])];
                    } else {
                        *planeChannel = 0;
                    }
                }
            }
        }
    } else {
        for (int j = 0; j < image->height; ++j) {
            for (int i = 0; i < image->width; ++i) {
                for (int plane = 0; plane < 3; ++plane) {
                    uint16_t * planeChannel = (uint16_t *)&aomImage->planes[plane][(j * aomImage->stride[plane]) + (2 * i)];
                    *planeChannel = image->planes[plane][i + (j * image->strides[plane])];
                }
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
    int maxChannel = (1 << image->depth) - 1;
    for (int j = 0; j < image->height; ++j) {
        for (int i = 0; i < image->width; ++i) {
            if (image->planes[3][i + (j * image->strides[3])] != maxChannel) {
                return AVIF_FALSE;
            }
        }
    }
    return AVIF_TRUE;
}
