// Copyright 2019 Joe Drago. All rights reserved.
// SPDX-License-Identifier: BSD-2-Clause

#include "avif/internal.h"

// These are for libaom to deal with
#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wduplicate-enum"
#pragma clang diagnostic ignored "-Wextra-semi"
#pragma clang diagnostic ignored "-Wused-but-marked-unused"
#endif

#include "aom/aom_decoder.h"
#include "aom/aom_encoder.h"
#include "aom/aomcx.h"
#include "aom/aomdx.h"

#ifdef __clang__
#pragma clang diagnostic pop

// This fixes complaints with aom_codec_control() and aom_img_fmt that are from libaom
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wused-but-marked-unused"
#pragma clang diagnostic ignored "-Wassign-enum"
#endif

#include <string.h>

struct avifCodecInternal
{
    avifBool decoderInitialized;
    aom_codec_ctx_t decoder;
    aom_codec_iter_t iter;
    uint32_t inputSampleIndex;
    aom_image_t * image;

    avifRawData encodedOBU;
    avifCodecConfigurationBox config;
};

static void aomCodecDestroyInternal(avifCodec * codec)
{
    if (codec->internal->decoderInitialized) {
        aom_codec_destroy(&codec->internal->decoder);
    }
    avifRawDataFree(&codec->internal->encodedOBU);
    avifFree(codec->internal);
}

static avifBool aomCodecDecode(struct avifCodec * codec)
{
    aom_codec_iface_t * decoder_interface = aom_codec_av1_dx();
    if (aom_codec_dec_init(&codec->internal->decoder, decoder_interface, NULL, 0)) {
        return AVIF_FALSE;
    }
    codec->internal->decoderInitialized = AVIF_TRUE;

    if (aom_codec_control(&codec->internal->decoder, AV1D_SET_OUTPUT_ALL_LAYERS, 1)) {
        return AVIF_FALSE;
    }

    codec->internal->inputSampleIndex = 0;
    codec->internal->iter = NULL;
    return AVIF_TRUE;
}

static avifBool aomCodecAlphaLimitedRange(avifCodec * codec)
{
    if (codec->decodeInput->alpha && codec->internal->image && (codec->internal->image->range == AOM_CR_STUDIO_RANGE)) {
        return AVIF_TRUE;
    }
    return AVIF_FALSE;
}

static avifBool aomCodecGetNextImage(avifCodec * codec, avifImage * image)
{
    aom_image_t * nextFrame = NULL;
    for (;;) {
        nextFrame = aom_codec_get_frame(&codec->internal->decoder, &codec->internal->iter);
        if (nextFrame) {
            // Got an image!
            break;
        } else if (codec->internal->inputSampleIndex < codec->decodeInput->samples.count) {
            // Feed another sample
            avifRawData * sample = &codec->decodeInput->samples.raw[codec->internal->inputSampleIndex];
            ++codec->internal->inputSampleIndex;
            codec->internal->iter = NULL;
            if (aom_codec_decode(&codec->internal->decoder, sample->data, sample->size, NULL)) {
                return AVIF_FALSE;
            }
        } else {
            // No more samples to feed
            break;
        }
    }

    if (nextFrame) {
        codec->internal->image = nextFrame;
    } else {
        if (codec->decodeInput->alpha && codec->internal->image) {
            // Special case: reuse last alpha frame
        } else {
            return AVIF_FALSE;
        }
    }

    if (!codec->internal->image) {
        return AVIF_FALSE;
    }

    avifBool isColor = !codec->decodeInput->alpha;
    if (isColor) {
        // Color (YUV) planes - set image to correct size / format, fill color

        avifPixelFormat yuvFormat = AVIF_PIXEL_FORMAT_NONE;
        switch (codec->internal->image->fmt) {
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

        if (image->width && image->height) {
            if ((image->width != codec->internal->image->d_w) || (image->height != codec->internal->image->d_h) ||
                (image->depth != codec->internal->image->bit_depth) || (image->yuvFormat != yuvFormat)) {
                // Throw it all out
                avifImageFreePlanes(image, AVIF_PLANES_ALL);
            }
        }

        image->width = codec->internal->image->d_w;
        image->height = codec->internal->image->d_h;
        image->depth = codec->internal->image->bit_depth;
        image->yuvFormat = yuvFormat;
        image->yuvRange = (codec->internal->image->range == AOM_CR_STUDIO_RANGE) ? AVIF_RANGE_LIMITED : AVIF_RANGE_FULL;

        avifPixelFormatInfo formatInfo;
        avifGetPixelFormatInfo(yuvFormat, &formatInfo);

        // Steal the pointers from the image directly
        avifImageFreePlanes(image, AVIF_PLANES_YUV);
        for (int yuvPlane = 0; yuvPlane < 3; ++yuvPlane) {
            int aomPlaneIndex = yuvPlane;
            if (yuvPlane == AVIF_CHAN_U) {
                aomPlaneIndex = formatInfo.aomIndexU;
            } else if (yuvPlane == AVIF_CHAN_V) {
                aomPlaneIndex = formatInfo.aomIndexV;
            }
            image->yuvPlanes[yuvPlane] = codec->internal->image->planes[aomPlaneIndex];
            image->yuvRowBytes[yuvPlane] = codec->internal->image->stride[aomPlaneIndex];
        }
        image->decoderOwnsYUVPlanes = AVIF_TRUE;
    } else {
        // Alpha plane - ensure image is correct size, fill color

        if ((image->width != codec->internal->image->d_w) || (image->height != codec->internal->image->d_h) ||
            (image->depth != codec->internal->image->bit_depth)) {
            return AVIF_FALSE;
        }

        avifImageFreePlanes(image, AVIF_PLANES_A);
        image->alphaPlane = codec->internal->image->planes[0];
        image->alphaRowBytes = codec->internal->image->stride[0];
        image->decoderOwnsAlphaPlane = AVIF_TRUE;
    }

    return AVIF_TRUE;
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
                case AVIF_PIXEL_FORMAT_YUV444:
                    cfg.g_profile = 1;
                    break;
                case AVIF_PIXEL_FORMAT_YUV422:
                    cfg.g_profile = 2;
                    break;
                case AVIF_PIXEL_FORMAT_YUV420:
                    cfg.g_profile = 0;
                    break;
                case AVIF_PIXEL_FORMAT_YV12:
                    cfg.g_profile = 0;
                    break;
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

    outputConfig->seqProfile = (uint8_t)cfg.g_profile;
    outputConfig->seqLevelIdx0 = seqLevelIdx0;
    outputConfig->seqTier0 = 0;
    outputConfig->highBitdepth = (image->depth > 8) ? 1 : 0;
    outputConfig->twelveBit = (image->depth == 12) ? 1 : 0;
    outputConfig->monochrome = alphaOnly ? 1 : 0;
    outputConfig->chromaSubsamplingX = (uint8_t)formatInfo.chromaShiftX;
    outputConfig->chromaSubsamplingY = (uint8_t)formatInfo.chromaShiftY;

    // TODO: choose the correct one from below:
    //   * 0 - CSP_UNKNOWN   Unknown (in this case the source video transfer function must be signaled outside the AV1 bitstream)
    //   * 1 - CSP_VERTICAL  Horizontally co-located with (0, 0) luma sample, vertical position in the middle between two luma samples
    //   * 2 - CSP_COLOCATED co-located with (0, 0) luma sample
    //   * 3 - CSP_RESERVED
    outputConfig->chromaSamplePosition = 0;

    int minQuantizer = encoder->minQuantizer;
    int maxQuantizer = encoder->maxQuantizer;
    if (alphaOnly) {
        minQuantizer = AVIF_QUANTIZER_LOSSLESS;
        maxQuantizer = AVIF_QUANTIZER_LOSSLESS;
    }
    avifBool lossless = ((encoder->minQuantizer == AVIF_QUANTIZER_LOSSLESS) && (encoder->maxQuantizer == AVIF_QUANTIZER_LOSSLESS))
                            ? AVIF_TRUE
                            : AVIF_FALSE;
    cfg.rc_min_quantizer = minQuantizer;
    cfg.rc_min_quantizer = maxQuantizer;

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

    uint32_t uvHeight = image->height >> yShift;
    aom_image_t * aomImage = aom_img_alloc(NULL, aomFormat, image->width, image->height, 16);

    if (alphaOnly) {
        aomImage->range = AOM_CR_FULL_RANGE; // Alpha is always full range
        aom_codec_control(&aomEncoder, AV1E_SET_COLOR_RANGE, aomImage->range);
        aomImage->monochrome = 1;
        for (uint32_t j = 0; j < image->height; ++j) {
            uint8_t * srcAlphaRow = &image->alphaPlane[j * image->alphaRowBytes];
            uint8_t * dstAlphaRow = &aomImage->planes[0][j * aomImage->stride[0]];
            memcpy(dstAlphaRow, srcAlphaRow, image->alphaRowBytes);
        }

        for (uint32_t j = 0; j < uvHeight; ++j) {
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

static avifBool aomCodecEncodeImage(avifCodec * codec, avifImage * image, avifEncoder * encoder, avifRawData * obu, avifBool alpha)
{
    if (!encodeOBU(image, alpha, encoder, obu, &codec->internal->config)) {
        return AVIF_FALSE;
    }
    return AVIF_TRUE;
}

static void aomCodecGetConfigurationBox(avifCodec * codec, avifCodecConfigurationBox * outConfig)
{
    memcpy(outConfig, &codec->internal->config, sizeof(avifCodecConfigurationBox));
}

avifCodec * avifCodecCreateAOM(void)
{
    avifCodec * codec = (avifCodec *)avifAlloc(sizeof(avifCodec));
    memset(codec, 0, sizeof(struct avifCodec));
    codec->decode = aomCodecDecode;
    codec->alphaLimitedRange = aomCodecAlphaLimitedRange;
    codec->getNextImage = aomCodecGetNextImage;
    codec->encodeImage = aomCodecEncodeImage;
    codec->getConfigurationBox = aomCodecGetConfigurationBox;
    codec->destroyInternal = aomCodecDestroyInternal;

    codec->internal = (struct avifCodecInternal *)avifAlloc(sizeof(struct avifCodecInternal));
    memset(codec->internal, 0, sizeof(struct avifCodecInternal));
    return codec;
}

#ifdef __clang__
#pragma clang diagnostic pop
#endif
