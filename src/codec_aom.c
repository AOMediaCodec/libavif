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

    avifBool encoderInitialized;
    aom_codec_ctx_t encoder;
    avifPixelFormatInfo formatInfo;
    aom_img_fmt_t aomFormat;
};

static void aomCodecDestroyInternal(avifCodec * codec)
{
    if (codec->internal->decoderInitialized) {
        aom_codec_destroy(&codec->internal->decoder);
    }
    if (codec->internal->encoderInitialized) {
        aom_codec_destroy(&codec->internal->encoder);
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
            avifDecodeSample * sample = &codec->decodeInput->samples.sample[codec->internal->inputSampleIndex];
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
            case AOM_IMG_FMT_NONE:
            case AOM_IMG_FMT_YV12:
            case AOM_IMG_FMT_AOMYV12:
            case AOM_IMG_FMT_YV1216:
            default:
                return AVIF_FALSE;
        }
        if (codec->internal->image->monochrome) {
            yuvFormat = AVIF_PIXEL_FORMAT_YUV400;
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

        image->colorPrimaries = (avifColorPrimaries)codec->internal->image->cp;
        image->transferCharacteristics = (avifTransferCharacteristics)codec->internal->image->tc;
        image->matrixCoefficients = (avifMatrixCoefficients)codec->internal->image->mc;

        avifPixelFormatInfo formatInfo;
        avifGetPixelFormatInfo(yuvFormat, &formatInfo);

        // Steal the pointers from the decoder's image directly
        avifImageFreePlanes(image, AVIF_PLANES_YUV);
        int yuvPlaneCount = (yuvFormat == AVIF_PIXEL_FORMAT_YUV400) ? 1 : 3;
        for (int yuvPlane = 0; yuvPlane < yuvPlaneCount; ++yuvPlane) {
            image->yuvPlanes[yuvPlane] = codec->internal->image->planes[yuvPlane];
            image->yuvRowBytes[yuvPlane] = codec->internal->image->stride[yuvPlane];
        }
        image->imageOwnsYUVPlanes = AVIF_FALSE;
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
        image->imageOwnsAlphaPlane = AVIF_FALSE;
    }

    return AVIF_TRUE;
}

static aom_img_fmt_t avifImageCalcAOMFmt(const avifImage * image, avifBool alpha)
{
    aom_img_fmt_t fmt;
    if (alpha) {
        // We're going monochrome, who cares about chroma quality
        fmt = AOM_IMG_FMT_I420;
    } else {
        switch (image->yuvFormat) {
            case AVIF_PIXEL_FORMAT_YUV444:
                fmt = AOM_IMG_FMT_I444;
                break;
            case AVIF_PIXEL_FORMAT_YUV422:
                fmt = AOM_IMG_FMT_I422;
                break;
            case AVIF_PIXEL_FORMAT_YUV420:
            case AVIF_PIXEL_FORMAT_YUV400:
                fmt = AOM_IMG_FMT_I420;
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

static avifBool aomCodecEncodeImage(avifCodec * codec,
                                    avifEncoder * encoder,
                                    const avifImage * image,
                                    avifBool alpha,
                                    uint32_t addImageFlags,
                                    avifCodecEncodeOutput * output)
{
    if (!codec->internal->encoderInitialized) {
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

        int aomMajorVersion = aom_codec_version_major();
        if ((aomMajorVersion < 2) && (image->depth > 8)) {
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

        codec->internal->aomFormat = avifImageCalcAOMFmt(image, alpha);
        if (codec->internal->aomFormat == AOM_IMG_FMT_NONE) {
            return AVIF_FALSE;
        }

        avifGetPixelFormatInfo(image->yuvFormat, &codec->internal->formatInfo);

        aom_codec_iface_t * encoder_interface = aom_codec_av1_cx();
        struct aom_codec_enc_cfg cfg;
        aom_codec_enc_config_default(encoder_interface, &cfg, aomUsage);
        codec->internal->encoderInitialized = AVIF_TRUE;

        cfg.g_profile = codec->configBox.seqProfile;
        cfg.g_bit_depth = image->depth;
        cfg.g_input_bit_depth = image->depth;
        cfg.g_w = image->width;
        cfg.g_h = image->height;
        if (encoder->maxThreads > 1) {
            cfg.g_threads = encoder->maxThreads;
        }
        cfg.kf_mode = AOM_KF_DISABLED;

        int minQuantizer = AVIF_CLAMP(encoder->minQuantizer, 0, 63);
        int maxQuantizer = AVIF_CLAMP(encoder->maxQuantizer, 0, 63);
        if (alpha) {
            minQuantizer = AVIF_CLAMP(encoder->minQuantizerAlpha, 0, 63);
            maxQuantizer = AVIF_CLAMP(encoder->maxQuantizerAlpha, 0, 63);
        }
        avifBool lossless = ((minQuantizer == AVIF_QUANTIZER_LOSSLESS) && (maxQuantizer == AVIF_QUANTIZER_LOSSLESS));
        cfg.rc_min_quantizer = minQuantizer;
        cfg.rc_max_quantizer = maxQuantizer;

        if (alpha || (image->yuvFormat == AVIF_PIXEL_FORMAT_YUV400)) {
            cfg.monochrome = 1;
        }

        aom_codec_flags_t encoderFlags = 0;
        if (image->depth > 8) {
            encoderFlags |= AOM_CODEC_USE_HIGHBITDEPTH;
        }
        aom_codec_enc_init(&codec->internal->encoder, encoder_interface, &cfg, encoderFlags);

        if (lossless) {
            aom_codec_control(&codec->internal->encoder, AV1E_SET_LOSSLESS, 1);
        }
        if (encoder->maxThreads > 1) {
            aom_codec_control(&codec->internal->encoder, AV1E_SET_ROW_MT, 1);
        }
        if (encoder->tileRowsLog2 != 0) {
            int tileRowsLog2 = AVIF_CLAMP(encoder->tileRowsLog2, 0, 6);
            aom_codec_control(&codec->internal->encoder, AV1E_SET_TILE_ROWS, tileRowsLog2);
        }
        if (encoder->tileColsLog2 != 0) {
            int tileColsLog2 = AVIF_CLAMP(encoder->tileColsLog2, 0, 6);
            aom_codec_control(&codec->internal->encoder, AV1E_SET_TILE_COLUMNS, tileColsLog2);
        }
        if (aomCpuUsed != -1) {
            aom_codec_control(&codec->internal->encoder, AOME_SET_CPUUSED, aomCpuUsed);
        }
    }

    int yShift = codec->internal->formatInfo.chromaShiftY;
    uint32_t uvHeight = (image->height + yShift) >> yShift;
    aom_image_t * aomImage = aom_img_alloc(NULL, codec->internal->aomFormat, image->width, image->height, 16);

    if (alpha) {
        aomImage->range = (image->alphaRange == AVIF_RANGE_FULL) ? AOM_CR_FULL_RANGE : AOM_CR_STUDIO_RANGE;
        aom_codec_control(&codec->internal->encoder, AV1E_SET_COLOR_RANGE, aomImage->range);
        aomImage->monochrome = 1;
        for (uint32_t j = 0; j < image->height; ++j) {
            uint8_t * srcAlphaRow = &image->alphaPlane[j * image->alphaRowBytes];
            uint8_t * dstAlphaRow = &aomImage->planes[0][j * aomImage->stride[0]];
            memcpy(dstAlphaRow, srcAlphaRow, image->alphaRowBytes);
        }

        // Ignore UV planes when monochrome
    } else {
        aomImage->range = (image->yuvRange == AVIF_RANGE_FULL) ? AOM_CR_FULL_RANGE : AOM_CR_STUDIO_RANGE;
        aom_codec_control(&codec->internal->encoder, AV1E_SET_COLOR_RANGE, aomImage->range);
        int yuvPlaneCount = 3;
        if (image->yuvFormat == AVIF_PIXEL_FORMAT_YUV400) {
            yuvPlaneCount = 1; // Ignore UV planes when monochrome
            aomImage->monochrome = 1;
        }
        for (int yuvPlane = 0; yuvPlane < yuvPlaneCount; ++yuvPlane) {
            uint32_t planeHeight = (yuvPlane == AVIF_CHAN_Y) ? image->height : uvHeight;

            for (uint32_t j = 0; j < planeHeight; ++j) {
                uint8_t * srcRow = &image->yuvPlanes[yuvPlane][j * image->yuvRowBytes[yuvPlane]];
                uint8_t * dstRow = &aomImage->planes[yuvPlane][j * aomImage->stride[yuvPlane]];
                memcpy(dstRow, srcRow, image->yuvRowBytes[yuvPlane]);
            }
        }

        aomImage->cp = (aom_color_primaries_t)image->colorPrimaries;
        aomImage->tc = (aom_transfer_characteristics_t)image->transferCharacteristics;
        aomImage->mc = (aom_matrix_coefficients_t)image->matrixCoefficients;
        aom_codec_control(&codec->internal->encoder, AV1E_SET_COLOR_PRIMARIES, aomImage->cp);
        aom_codec_control(&codec->internal->encoder, AV1E_SET_TRANSFER_CHARACTERISTICS, aomImage->tc);
        aom_codec_control(&codec->internal->encoder, AV1E_SET_MATRIX_COEFFICIENTS, aomImage->mc);
    }

    aom_enc_frame_flags_t encodeFlags = 0;
    if (addImageFlags & AVIF_ADD_IMAGE_FLAG_FORCE_KEYFRAME) {
        encodeFlags |= AOM_EFLAG_FORCE_KF;
    }
    aom_codec_encode(&codec->internal->encoder, aomImage, 0, 1, encodeFlags);

    aom_codec_iter_t iter = NULL;
    for (;;) {
        const aom_codec_cx_pkt_t * pkt = aom_codec_get_cx_data(&codec->internal->encoder, &iter);
        if (pkt == NULL) {
            break;
        }
        if (pkt->kind == AOM_CODEC_CX_FRAME_PKT) {
            avifCodecEncodeOutputAddSample(output, pkt->data.frame.buf, pkt->data.frame.sz, (pkt->data.frame.flags & AOM_FRAME_IS_KEY));
        }
    }

    aom_img_free(aomImage);
    return AVIF_TRUE;
}

static avifBool aomCodecEncodeFinish(avifCodec * codec, avifCodecEncodeOutput * output)
{
    for (;;) {
        // flush encoder
        aom_codec_encode(&codec->internal->encoder, NULL, 0, 1, 0);

        avifBool gotPacket = AVIF_FALSE;
        aom_codec_iter_t iter = NULL;
        for (;;) {
            const aom_codec_cx_pkt_t * pkt = aom_codec_get_cx_data(&codec->internal->encoder, &iter);
            if (pkt == NULL) {
                break;
            }
            if (pkt->kind == AOM_CODEC_CX_FRAME_PKT) {
                gotPacket = AVIF_TRUE;
                avifCodecEncodeOutputAddSample(
                    output, pkt->data.frame.buf, pkt->data.frame.sz, (pkt->data.frame.flags & AOM_FRAME_IS_KEY));
            }
        }

        if (!gotPacket) {
            break;
        }
    }
    return AVIF_TRUE;
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
    codec->encodeFinish = aomCodecEncodeFinish;
    codec->destroyInternal = aomCodecDestroyInternal;

    codec->internal = (struct avifCodecInternal *)avifAlloc(sizeof(struct avifCodecInternal));
    memset(codec->internal, 0, sizeof(struct avifCodecInternal));
    return codec;
}

#ifdef __clang__
#pragma clang diagnostic pop
#endif
