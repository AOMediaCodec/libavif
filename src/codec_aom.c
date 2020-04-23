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
};

static void aomCodecDestroyInternal(avifCodec * codec)
{
    if (codec->internal->decoderInitialized) {
        aom_codec_destroy(&codec->internal->decoder);
    }
    avifFree(codec->internal);
}

static avifBool aomCodecOpen(struct avifCodec * codec, uint32_t firstSampleIndex)
{
    aom_codec_iface_t * decoder_interface = aom_codec_av1_dx();
    if (aom_codec_dec_init(&codec->internal->decoder, decoder_interface, NULL, 0)) {
        return AVIF_FALSE;
    }
    codec->internal->decoderInitialized = AVIF_TRUE;

    if (aom_codec_control(&codec->internal->decoder, AV1D_SET_OUTPUT_ALL_LAYERS, 1)) {
        return AVIF_FALSE;
    }

    codec->internal->inputSampleIndex = firstSampleIndex;
    codec->internal->iter = NULL;
    return AVIF_TRUE;
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
            avifSample * sample = &codec->decodeInput->samples.sample[codec->internal->inputSampleIndex];
            ++codec->internal->inputSampleIndex;
            codec->internal->iter = NULL;
            if (aom_codec_decode(&codec->internal->decoder, sample->data.data, sample->data.size, NULL)) {
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

        if (image->profileFormat == AVIF_PROFILE_FORMAT_NONE) {
            // If the AVIF container doesn't provide a color profile, allow the AV1 OBU to provide one as a fallback
            avifNclxColorProfile nclx;
            nclx.colourPrimaries = (avifNclxColourPrimaries)codec->internal->image->cp;
            nclx.transferCharacteristics = (avifNclxTransferCharacteristics)codec->internal->image->tc;
            nclx.matrixCoefficients = (avifNclxMatrixCoefficients)codec->internal->image->mc;
            nclx.range = image->yuvRange;
            avifImageSetProfileNCLX(image, &nclx);
        }

        avifPixelFormatInfo formatInfo;
        avifGetPixelFormatInfo(yuvFormat, &formatInfo);

        // Steal the pointers from the decoder's image directly
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

        if (image->width && image->height) {
            if ((image->width != codec->internal->image->d_w) || (image->height != codec->internal->image->d_h) ||
                (image->depth != codec->internal->image->bit_depth)) {
                // Alpha plane doesn't match previous alpha plane decode, bail out
                return AVIF_FALSE;
            }
        }
        image->width = codec->internal->image->d_w;
        image->height = codec->internal->image->d_h;
        image->depth = codec->internal->image->bit_depth;

        avifImageFreePlanes(image, AVIF_PLANES_A);
        image->alphaPlane = codec->internal->image->planes[0];
        image->alphaRowBytes = codec->internal->image->stride[0];
        image->alphaRange = (codec->internal->image->range == AOM_CR_STUDIO_RANGE) ? AVIF_RANGE_LIMITED : AVIF_RANGE_FULL;
        image->decoderOwnsAlphaPlane = AVIF_TRUE;
    }

    return AVIF_TRUE;
}

static aom_img_fmt_t avifImageCalcAOMFmt(avifImage * image, avifBool alpha, int * yShift)
{
    *yShift = 0;

    aom_img_fmt_t fmt;
    if (alpha) {
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
            case AVIF_PIXEL_FORMAT_NONE:
            default:
                return AOM_IMG_FMT_NONE;
        }
    }

    if (image->depth > 8) {
        fmt |= AOM_IMG_FMT_HIGHBITDEPTH;
    }

    return fmt;
}

static avifBool aomCodecEncodeImage(avifCodec * codec, avifImage * image, avifEncoder * encoder, avifRWData * obu, avifBool alpha)
{
    avifBool success = AVIF_FALSE;
    aom_codec_iface_t * encoder_interface = aom_codec_av1_cx();
    aom_codec_ctx_t aomEncoder;

    // Map encoder speed to AOM usage + CpuUsed:
    // Speed  0: GoodQuality CpuUsed 0
    // Speed  1: GoodQuality CpuUsed 1
    // Speed  2: GoodQuality CpuUsed 2
    // Speed  3: GoodQuality CpuUsed 3
    // Speed  4: GoodQuality CpuUsed 4
    // Speed  5: GoodQuality CpuUsed 5
    // Speed  6: GoodQuality CpuUsed 5
    // Speed  7: GoodQuality CpuUsed 5
    // Speed  8: RealTime    CpuUsed 6
    // Speed  9: RealTime    CpuUsed 7
    // Speed 10: RealTime    CpuUsed 8
    unsigned int aomUsage = AOM_USAGE_GOOD_QUALITY;
    int aomCpuUsed = -1;
    if (encoder->speed != AVIF_SPEED_DEFAULT) {
        if (encoder->speed < 8) {
            aomUsage = AOM_USAGE_GOOD_QUALITY;
            aomCpuUsed = AVIF_CLAMP(encoder->speed, 0, 5);
        } else {
            aomUsage = AOM_USAGE_REALTIME;
            aomCpuUsed = AVIF_CLAMP(encoder->speed - 2, 6, 8);
        }
    }

    if (image->depth > 8) {
        // Due to a known issue with libavif v1.0.0-errata1-avif, 10bpc and
        // 12bpc image encodes will call the wrong variant of
        // aom_subtract_block when cpu-used is 7 or 8, and crash. Until we get
        // a new tagged release from libaom with the fix and can verify we're
        // running with that version of libaom, we must avoid using
        // cpu-used=7/8 on any >8bpc image encodes.
        //
        // Context:
        //   * https://github.com/AOMediaCodec/libavif/issues/49
        //   * https://bugs.chromium.org/p/aomedia/issues/detail?id=2587
        //
        // Continued bug tracking here:
        //   * https://github.com/AOMediaCodec/libavif/issues/56

        if (aomCpuUsed > 6) {
            aomCpuUsed = 6;
        }
    }

    int yShift = 0;
    aom_img_fmt_t aomFormat = avifImageCalcAOMFmt(image, alpha, &yShift);
    if (aomFormat == AOM_IMG_FMT_NONE) {
        return AVIF_FALSE;
    }

    avifPixelFormatInfo formatInfo;
    avifGetPixelFormatInfo(image->yuvFormat, &formatInfo);

    struct aom_codec_enc_cfg cfg;
    aom_codec_enc_config_default(encoder_interface, &cfg, aomUsage);

    cfg.g_profile = codec->configBox.seqProfile;
    cfg.g_bit_depth = image->depth;
    cfg.g_input_bit_depth = image->depth;
    cfg.g_w = image->width;
    cfg.g_h = image->height;
    if (encoder->maxThreads > 1) {
        cfg.g_threads = encoder->maxThreads;
    }

    int minQuantizer = AVIF_CLAMP(encoder->minQuantizer, 0, 63);
    int maxQuantizer = AVIF_CLAMP(encoder->maxQuantizer, 0, 63);
    if (alpha) {
        minQuantizer = AVIF_CLAMP(encoder->minQuantizerAlpha, 0, 63);
        maxQuantizer = AVIF_CLAMP(encoder->maxQuantizerAlpha, 0, 63);
    }
    avifBool lossless = ((minQuantizer == AVIF_QUANTIZER_LOSSLESS) && (maxQuantizer == AVIF_QUANTIZER_LOSSLESS));
    cfg.rc_min_quantizer = minQuantizer;
    cfg.rc_max_quantizer = maxQuantizer;

    aom_codec_flags_t encoderFlags = 0;
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
    if (encoder->tileRowsLog2 != 0) {
        int tileRowsLog2 = AVIF_CLAMP(encoder->tileRowsLog2, 0, 6);
        aom_codec_control(&aomEncoder, AV1E_SET_TILE_ROWS, tileRowsLog2);
    }
    if (encoder->tileColsLog2 != 0) {
        int tileColsLog2 = AVIF_CLAMP(encoder->tileColsLog2, 0, 6);
        aom_codec_control(&aomEncoder, AV1E_SET_TILE_COLUMNS, tileColsLog2);
    }
    if (aomCpuUsed != -1) {
        aom_codec_control(&aomEncoder, AOME_SET_CPUUSED, aomCpuUsed);
    }

    uint32_t uvHeight = (image->height + yShift) >> yShift;
    aom_image_t * aomImage = aom_img_alloc(NULL, aomFormat, image->width, image->height, 16);

    if (alpha) {
        aomImage->range = (image->alphaRange == AVIF_RANGE_FULL) ? AOM_CR_FULL_RANGE : AOM_CR_STUDIO_RANGE;
        aom_codec_control(&aomEncoder, AV1E_SET_COLOR_RANGE, aomImage->range);
        aomImage->monochrome = 1;
        for (uint32_t j = 0; j < image->height; ++j) {
            uint8_t * srcAlphaRow = &image->alphaPlane[j * image->alphaRowBytes];
            uint8_t * dstAlphaRow = &aomImage->planes[0][j * aomImage->stride[0]];
            memcpy(dstAlphaRow, srcAlphaRow, image->alphaRowBytes);
        }

        // Zero out U and V
        memset(aomImage->planes[1], 0, aomImage->stride[1] * uvHeight);
        memset(aomImage->planes[2], 0, aomImage->stride[2] * uvHeight);
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

        if (image->profileFormat == AVIF_PROFILE_FORMAT_NCLX) {
            aomImage->cp = (aom_color_primaries_t)image->nclx.colourPrimaries;
            aomImage->tc = (aom_transfer_characteristics_t)image->nclx.transferCharacteristics;
            aomImage->mc = (aom_matrix_coefficients_t)image->nclx.matrixCoefficients;
            aom_codec_control(&aomEncoder, AV1E_SET_COLOR_PRIMARIES, aomImage->cp);
            aom_codec_control(&aomEncoder, AV1E_SET_TRANSFER_CHARACTERISTICS, aomImage->tc);
            aom_codec_control(&aomEncoder, AV1E_SET_MATRIX_COEFFICIENTS, aomImage->mc);
        }
    }

    aom_codec_encode(&aomEncoder, aomImage, 0, 1, 0);

    avifBool flushed = AVIF_FALSE;
    aom_codec_iter_t iter = NULL;
    for (;;) {
        const aom_codec_cx_pkt_t * pkt = aom_codec_get_cx_data(&aomEncoder, &iter);
        if (pkt == NULL) {
            if (flushed)
                break;

            aom_codec_encode(&aomEncoder, NULL, 0, 1, 0); // flush
            flushed = AVIF_TRUE;
            continue;
        }
        if (pkt->kind == AOM_CODEC_CX_FRAME_PKT) {
            avifRWDataSet(obu, pkt->data.frame.buf, pkt->data.frame.sz);
            success = AVIF_TRUE;
            break;
        }
    }

    aom_img_free(aomImage);
    aom_codec_destroy(&aomEncoder);
    return success;
}

const char * avifCodecVersionAOM(void)
{
    return aom_codec_version_str();
}

avifCodec * avifCodecCreateAOM(void)
{
    avifCodec * codec = (avifCodec *)avifAlloc(sizeof(avifCodec));
    memset(codec, 0, sizeof(struct avifCodec));
    codec->open = aomCodecOpen;
    codec->getNextImage = aomCodecGetNextImage;
    codec->encodeImage = aomCodecEncodeImage;
    codec->destroyInternal = aomCodecDestroyInternal;

    codec->internal = (struct avifCodecInternal *)avifAlloc(sizeof(struct avifCodecInternal));
    memset(codec->internal, 0, sizeof(struct avifCodecInternal));
    return codec;
}

#ifdef __clang__
#pragma clang diagnostic pop
#endif
