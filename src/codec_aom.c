// Copyright 2019 Joe Drago. All rights reserved.
// SPDX-License-Identifier: BSD-2-Clause

#include "avif/internal.h"

#include "aom/aom_decoder.h"
#include "aom/aomdx.h"
#include "aom/aom_encoder.h"
#include "aom/aomcx.h"

#include <string.h>

struct avifCodecInternal
{
    avifBool decoderInitialized[AVIF_CODEC_PLANES_COUNT];
    aom_codec_ctx_t decoders[AVIF_CODEC_PLANES_COUNT];

    aom_image_t * images[AVIF_CODEC_PLANES_COUNT];
    avifRawData encodedOBUs[AVIF_CODEC_PLANES_COUNT];
    avifCodecConfigurationBox configs[AVIF_CODEC_PLANES_COUNT];
};

avifCodec * avifCodecCreate()
{
    avifCodec * codec = (avifCodec *)avifAlloc(sizeof(avifCodec));
    codec->internal = (struct avifCodecInternal *)avifAlloc(sizeof(struct avifCodecInternal));
    memset(codec->internal, 0, sizeof(struct avifCodecInternal));
    return codec;
}

void avifCodecDestroy(avifCodec * codec)
{
    for (int plane = 0; plane < AVIF_CODEC_PLANES_COUNT; ++plane) {
        if (codec->internal->decoderInitialized[plane]) {
            aom_codec_destroy(&codec->internal->decoders[plane]);
        }
        avifRawDataFree(&codec->internal->encodedOBUs[plane]);
    }
    avifFree(codec->internal);
    avifFree(codec);
}

avifBool avifCodecDecode(avifCodec * codec, avifCodecPlanes planes, avifRawData * obu)
{
    aom_codec_stream_info_t si;
    aom_codec_iface_t * decoder_interface = aom_codec_av1_dx();
    if (aom_codec_dec_init(&codec->internal->decoders[planes], decoder_interface, NULL, 0)) {
        return AVIF_FALSE;
    }
    codec->internal->decoderInitialized[planes] = AVIF_TRUE;

    if (aom_codec_control(&codec->internal->decoders[planes], AV1D_SET_OUTPUT_ALL_LAYERS, 1)) {
        return AVIF_FALSE;
    }

    si.is_annexb = 0;
    if (aom_codec_peek_stream_info(decoder_interface, obu->data, obu->size, &si)) {
        return AVIF_FALSE;
    }

    if (aom_codec_decode(&codec->internal->decoders[planes], obu->data, obu->size, NULL)) {
        return AVIF_FALSE;
    }

    aom_codec_iter_t iter = NULL;
    codec->internal->images[planes] = aom_codec_get_frame(&codec->internal->decoders[planes], &iter); // It doesn't appear that I own this / need to free this
    return (codec->internal->images[planes]) ? AVIF_TRUE : AVIF_FALSE;
}

avifCodecImageSize avifCodecGetImageSize(avifCodec * codec, avifCodecPlanes planes)
{
    avifCodecImageSize size;
    if (codec->internal->images[planes]) {
        size.width = codec->internal->images[planes]->d_w;
        size.height = codec->internal->images[planes]->d_h;
    } else {
        size.width = 0;
        size.height = 0;
    }
    return size;
}

avifBool avifCodecAlphaLimitedRange(avifCodec * codec)
{
    aom_image_t * aomAlphaImage = codec->internal->images[AVIF_CODEC_PLANES_ALPHA];
    if (aomAlphaImage && (aomAlphaImage->range == AOM_CR_STUDIO_RANGE)) {
        return AVIF_TRUE;
    }
    return AVIF_FALSE;
}

avifResult avifCodecGetDecodedImage(avifCodec * codec, avifImage * image)
{
    aom_image_t * aomColorImage = codec->internal->images[AVIF_CODEC_PLANES_COLOR];
    aom_image_t * aomAlphaImage = codec->internal->images[AVIF_CODEC_PLANES_ALPHA];
    avifBool hasAlpha = aomAlphaImage ? AVIF_TRUE : AVIF_FALSE;

    avifPixelFormat yuvFormat = AVIF_PIXEL_FORMAT_NONE;
    switch (aomColorImage->fmt) {
        case AOM_IMG_FMT_I420:
        case AOM_IMG_FMT_AOMI420:
        case AOM_IMG_FMT_I42016:
            yuvFormat = AVIF_PIXEL_FORMAT_YUV420;
            break;
        case AOM_IMG_FMT_I422:
        case AOM_IMG_FMT_I42216:
            yuvFormat = AVIF_PIXEL_FORMAT_YUV422;
            break;
        case AOM_IMG_FMT_I444:
        case AOM_IMG_FMT_I44416:
            yuvFormat = AVIF_PIXEL_FORMAT_YUV444;
            break;
        case AOM_IMG_FMT_YV12:
        case AOM_IMG_FMT_AOMYV12:
        case AOM_IMG_FMT_YV1216:
            yuvFormat = AVIF_PIXEL_FORMAT_YV12;
            break;
        case AOM_IMG_FMT_NONE:
        default:
            break;
    }

    image->width = aomColorImage->d_w;
    image->height = aomColorImage->d_h;
    image->depth = aomColorImage->bit_depth;
    image->yuvFormat = yuvFormat;
    image->yuvRange = (aomColorImage->range == AOM_CR_STUDIO_RANGE) ? AVIF_RANGE_LIMITED : AVIF_RANGE_FULL;

    avifPixelFormatInfo formatInfo;
    avifGetPixelFormatInfo(yuvFormat, &formatInfo);

    int uvHeight = image->height >> formatInfo.chromaShiftY;
    avifImageAllocatePlanes(image, AVIF_PLANES_YUV);
    for (int yuvPlane = 0; yuvPlane < 3; ++yuvPlane) {
        int aomPlaneIndex = yuvPlane;
        int planeHeight = image->height;
        if (yuvPlane == AVIF_CHAN_U) {
            aomPlaneIndex = formatInfo.aomIndexU;
            planeHeight = uvHeight;
        } else if (yuvPlane == AVIF_CHAN_V) {
            aomPlaneIndex = formatInfo.aomIndexV;
            planeHeight = uvHeight;
        }

        for (int j = 0; j < planeHeight; ++j) {
            uint8_t * srcRow = &aomColorImage->planes[aomPlaneIndex][j * aomColorImage->stride[aomPlaneIndex]];
            uint8_t * dstRow = &image->yuvPlanes[yuvPlane][j * image->yuvRowBytes[yuvPlane]];
            memcpy(dstRow, srcRow, image->yuvRowBytes[yuvPlane]);
        }
    }

    if (hasAlpha) {
        avifImageAllocatePlanes(image, AVIF_PLANES_A);
        for (int j = 0; j < image->height; ++j) {
            uint8_t * srcAlphaRow = &aomAlphaImage->planes[0][j * aomAlphaImage->stride[0]];
            uint8_t * dstAlphaRow = &image->alphaPlane[j * image->alphaRowBytes];
            memcpy(dstAlphaRow, srcAlphaRow, image->alphaRowBytes);
        }
    }
    return AVIF_RESULT_OK;
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

static avifBool encodeOBU(avifImage * image, avifBool alphaOnly, avifEncoder * encoder, avifRawData * outputOBU, avifCodecConfigurationBox * outputConfig)
{
    avifBool success = AVIF_FALSE;
    aom_codec_iface_t * encoder_interface = aom_codec_av1_cx();
    aom_codec_ctx_t aomEncoder;

    int quality = encoder->quality;
    if (alphaOnly) {
        quality = AVIF_BEST_QUALITY;
    }

    memset(outputConfig, 0, sizeof(avifCodecConfigurationBox));

    int yShift = 0;
    aom_img_fmt_t aomFormat = avifImageCalcAOMFmt(image, alphaOnly, &yShift);
    if (aomFormat == AOM_IMG_FMT_NONE) {
        return AVIF_FALSE;
    }

    avifPixelFormatInfo formatInfo;
    avifGetPixelFormatInfo(image->yuvFormat, &formatInfo);

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
                case AVIF_PIXEL_FORMAT_NONE:
                default:
                    break;
            }
        }
    }

    cfg.g_bit_depth = image->depth;
    cfg.g_input_bit_depth = image->depth;
    cfg.g_w = image->width;
    cfg.g_h = image->height;
    if (encoder->maxThreads > 1) {
        cfg.g_threads = encoder->maxThreads;
    }

    // TODO: Choose correct value from Annex A.3 table: https://aomediacodec.github.io/av1-spec/av1-spec.pdf
    uint8_t seqLevelIdx0 = 31;
    if ((image->width <= 8192) && (image->height <= 4352) && ((image->width * image->height) <= 8912896)) {
        // Image is 5.1 compatible
        seqLevelIdx0 = 13; // 5.1
    }

    outputConfig->seqProfile = cfg.g_profile;
    outputConfig->seqLevelIdx0 = seqLevelIdx0;
    outputConfig->seqTier0 = 0;
    outputConfig->highBitdepth = (image->depth > 8) ? 1 : 0;
    outputConfig->twelveBit = (image->depth == 12) ? 1 : 0;
    outputConfig->monochrome = alphaOnly ? 1 : 0;
    outputConfig->chromaSubsamplingX = formatInfo.chromaShiftX;
    outputConfig->chromaSubsamplingY = formatInfo.chromaShiftY;

    // TODO: choose the correct one from below:
    //   * 0 - CSP_UNKNOWN   Unknown (in this case the source video transfer function must be signaled outside the AV1 bitstream)
    //   * 1 - CSP_VERTICAL  Horizontally co-located with (0, 0) luma sample, vertical position in the middle between two luma samples
    //   * 2 - CSP_COLOCATED co-located with (0, 0) luma sample
    //   * 3 - CSP_RESERVED
    outputConfig->chromaSamplePosition = 0;

    avifBool lossless = (quality == AVIF_BEST_QUALITY) ? AVIF_TRUE : AVIF_FALSE;
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
    aom_codec_enc_init(&aomEncoder, encoder_interface, &cfg, encoderFlags);

    if (lossless) {
        aom_codec_control(&aomEncoder, AV1E_SET_LOSSLESS, 1);
    }
    if (encoder->maxThreads > 1) {
        aom_codec_control(&aomEncoder, AV1E_SET_ROW_MT, 1);
    }

    int uvHeight = image->height >> yShift;
    aom_image_t * aomImage = aom_img_alloc(NULL, aomFormat, image->width, image->height, 16);

    if (alphaOnly) {
        aomImage->range = AOM_CR_FULL_RANGE; // Alpha is always full range
        aom_codec_control(&aomEncoder, AV1E_SET_COLOR_RANGE, aomImage->range);
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
        aom_codec_control(&aomEncoder, AV1E_SET_COLOR_RANGE, aomImage->range);
        for (int yuvPlane = 0; yuvPlane < 3; ++yuvPlane) {
            int aomPlaneIndex = yuvPlane;
            int planeHeight = image->height;
            if (yuvPlane == AVIF_CHAN_U) {
                aomPlaneIndex = formatInfo.aomIndexU;
                planeHeight = uvHeight;
            } else if (yuvPlane == AVIF_CHAN_V) {
                aomPlaneIndex = formatInfo.aomIndexV;
                planeHeight = uvHeight;
            }

            for (int j = 0; j < planeHeight; ++j) {
                uint8_t * srcRow = &image->yuvPlanes[yuvPlane][j * image->yuvRowBytes[yuvPlane]];
                uint8_t * dstRow = &aomImage->planes[aomPlaneIndex][j * aomImage->stride[aomPlaneIndex]];
                memcpy(dstRow, srcRow, image->yuvRowBytes[yuvPlane]);
            }
        }
    }

    aom_codec_encode(&aomEncoder, aomImage, 0, 1, 0);
    aom_codec_encode(&aomEncoder, NULL, 0, 1, 0); // flush

    aom_codec_iter_t iter = NULL;
    for (;;) {
        const aom_codec_cx_pkt_t * pkt = aom_codec_get_cx_data(&aomEncoder, &iter);
        if (pkt == NULL)
            break;
        if (pkt->kind == AOM_CODEC_CX_FRAME_PKT) {
            avifRawDataSet(outputOBU, pkt->data.frame.buf, pkt->data.frame.sz);
            success = AVIF_TRUE;
            break;
        }
    }

    aom_img_free(aomImage);
    aom_codec_destroy(&aomEncoder);
    return success;
}

avifResult avifCodecEncodeImage(avifCodec * codec, avifImage * image, avifEncoder * encoder, avifRawData * colorOBU, avifRawData * alphaOBU)
{
    if (colorOBU) {
        if (!encodeOBU(image, AVIF_FALSE, encoder, colorOBU, &codec->internal->configs[AVIF_CODEC_PLANES_COLOR])) {
            return AVIF_RESULT_ENCODE_COLOR_FAILED;
        }
    }
    if (alphaOBU) {
        if (!encodeOBU(image, AVIF_TRUE, encoder, alphaOBU, &codec->internal->configs[AVIF_CODEC_PLANES_ALPHA])) {
            return AVIF_RESULT_ENCODE_COLOR_FAILED;
        }
    }
    return AVIF_RESULT_OK;
}

void avifCodecGetConfigurationBox(avifCodec * codec, avifCodecPlanes planes, avifCodecConfigurationBox * outConfig)
{
    memcpy(outConfig, &codec->internal->configs[planes], sizeof(avifCodecConfigurationBox));
}
