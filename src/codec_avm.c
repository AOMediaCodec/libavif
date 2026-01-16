// Copyright 2023 Google LLC
// SPDX-License-Identifier: BSD-2-Clause

#include "avif/internal.h"

#include "avm/avm_decoder.h"
#include "avm/avm_encoder.h"
#include "avm/avmcx.h"
#include "avm/avmdx.h"

#include <assert.h>
#include <limits.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

struct avifCodecInternal
{
    avifBool decoderInitialized;
    avm_codec_ctx_t decoder;
    avm_codec_iter_t iter;
    avm_image_t * image;

    avifBool encoderInitialized;
    avm_codec_ctx_t encoder;
    struct avm_codec_enc_cfg cfg;
    avifPixelFormatInfo formatInfo;
    avm_img_fmt_t avmFormat;
    avifBool monochromeEnabled;
    // Whether 'tuning' (of the specified distortion metric) was set with an
    // avifEncoderSetCodecSpecificOption(encoder, "tune", value) call.
    avifBool tuningSet;
    uint32_t currentLayer;
};

static void avmCodecDestroyInternal(avifCodec * codec)
{
    if (codec->internal->decoderInitialized) {
        avm_codec_destroy(&codec->internal->decoder);
    }

    if (codec->internal->encoderInitialized) {
        avm_codec_destroy(&codec->internal->encoder);
    }

    avifFree(codec->internal);
}

static avifResult avifCheckCodecVersionAVM()
{
    // The minimum supported version of avm is the anchor 4.0.0.
    // avm_codec.h says: avm_codec_version() == (major<<16 | minor<<8 | patch)
    AVIF_CHECKERR((avm_codec_version() >> 16) >= 4, AVIF_RESULT_NO_CODEC_AVAILABLE);
    return AVIF_RESULT_OK;
}

static avifBool avmCodecGetNextImage(struct avifCodec * codec,
                                     const avifDecodeSample * sample,
                                     avifBool alpha,
                                     avifBool * isLimitedRangeAlpha,
                                     avifImage * image)
{
    assert(sample);

    if (!codec->internal->decoderInitialized) {
        AVIF_CHECKRES(avifCheckCodecVersionAVM());

        avm_codec_dec_cfg_t cfg;
        memset(&cfg, 0, sizeof(avm_codec_dec_cfg_t));
        cfg.threads = codec->maxThreads;

        avm_codec_iface_t * decoder_interface = avm_codec_av2_dx();
        if (avm_codec_dec_init(&codec->internal->decoder, decoder_interface, &cfg, 0)) {
            return AVIF_FALSE;
        }
        codec->internal->decoderInitialized = AVIF_TRUE;

        if (avm_codec_control(&codec->internal->decoder, AV2D_SET_OUTPUT_ALL_LAYERS, codec->allLayers)) {
            return AVIF_FALSE;
        }
        if (avm_codec_control(&codec->internal->decoder, AV2D_SET_OPERATING_POINT, codec->operatingPoint)) {
            return AVIF_FALSE;
        }

        codec->internal->iter = NULL;
    }

    avm_image_t * nextFrame = NULL;
    uint8_t spatialID = AVIF_SPATIAL_ID_UNSET;
    for (;;) {
        nextFrame = avm_codec_get_frame(&codec->internal->decoder, &codec->internal->iter);
        if (nextFrame) {
            if (spatialID != AVIF_SPATIAL_ID_UNSET) {
                if (spatialID == nextFrame->mlayer_id) {
                    // Found the correct spatial_id.
                    break;
                }
            } else {
                // Got an image!
                break;
            }
        } else if (sample) {
            codec->internal->iter = NULL;
            if (avm_codec_decode(&codec->internal->decoder, sample->data.data, sample->data.size, NULL)) {
                return AVIF_FALSE;
            }
            spatialID = sample->spatialID;
            sample = NULL;
        } else {
            break;
        }
    }

    if (nextFrame) {
        codec->internal->image = nextFrame;
    } else {
        if (alpha && codec->internal->image) {
            // Special case: reuse last alpha frame
        } else {
            return AVIF_FALSE;
        }
    }

    avifBool isColor = !alpha;
    if (isColor) {
        // Color (YUV) planes - set image to correct size / format, fill color

        avifPixelFormat yuvFormat = AVIF_PIXEL_FORMAT_NONE;
        switch (codec->internal->image->fmt) {
            case AVM_IMG_FMT_I420:
            case AVM_IMG_FMT_AVMI420:
            case AVM_IMG_FMT_I42016:
                yuvFormat = AVIF_PIXEL_FORMAT_YUV420;
                break;
            case AVM_IMG_FMT_I422:
            case AVM_IMG_FMT_I42216:
                yuvFormat = AVIF_PIXEL_FORMAT_YUV422;
                break;
            case AVM_IMG_FMT_I444:
            case AVM_IMG_FMT_I44416:
                yuvFormat = AVIF_PIXEL_FORMAT_YUV444;
                break;
            case AVM_IMG_FMT_NONE:
            case AVM_IMG_FMT_YV12:
            case AVM_IMG_FMT_AVMYV12:
            case AVM_IMG_FMT_YV1216:
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
        image->yuvRange = (codec->internal->image->range == AVM_CR_STUDIO_RANGE) ? AVIF_RANGE_LIMITED : AVIF_RANGE_FULL;
        if (codec->internal->image->csp == AVM_CSP_LEFT) {
            // CSP_LEFT: Horizontal offset 0, vertical offset 0.5
            image->yuvChromaSamplePosition = AVIF_CHROMA_SAMPLE_POSITION_VERTICAL;
        } else if (codec->internal->image->csp == AVM_CSP_CENTER) {
            // CSP_CENTER: Horizontal offset 0.5, vertical offset 0.5
            image->yuvChromaSamplePosition = AVIF_CHROMA_SAMPLE_POSITION_UNKNOWN;
        } else if (codec->internal->image->csp == AVM_CSP_TOPLEFT) {
            // CSP_TOPLEFT: Horizontal offset 0, vertical offset 0
            image->yuvChromaSamplePosition = AVIF_CHROMA_SAMPLE_POSITION_COLOCATED;
        } else {
            image->yuvChromaSamplePosition = AVIF_CHROMA_SAMPLE_POSITION_UNKNOWN;
        }

        image->colorPrimaries = (avifColorPrimaries)codec->internal->image->cp;
        image->transferCharacteristics = (avifTransferCharacteristics)codec->internal->image->tc;
        image->matrixCoefficients = (avifMatrixCoefficients)codec->internal->image->mc;

        avifImageFreePlanes(image, AVIF_PLANES_YUV);
        int yuvPlaneCount = (yuvFormat == AVIF_PIXEL_FORMAT_YUV400) ? 1 : 3;

        // avifImage assumes that a depth of 8 bits means an 8-bit buffer.
        // avm_image does not. The buffer depth depends on fmt|AVM_IMG_FMT_HIGHBITDEPTH, even for 8-bit values.
        if (!avifImageUsesU16(image) && (codec->internal->image->fmt & AVM_IMG_FMT_HIGHBITDEPTH)) {
            AVIF_CHECK(avifImageAllocatePlanes(image, AVIF_PLANES_YUV) == AVIF_RESULT_OK);
            for (int yuvPlane = 0; yuvPlane < yuvPlaneCount; ++yuvPlane) {
                const uint32_t planeWidth = avifImagePlaneWidth(image, yuvPlane);
                const uint32_t planeHeight = avifImagePlaneHeight(image, yuvPlane);
                const uint8_t * srcRow = codec->internal->image->planes[yuvPlane];
                uint8_t * dstRow = avifImagePlane(image, yuvPlane);
                const uint32_t dstRowBytes = avifImagePlaneRowBytes(image, yuvPlane);
                for (uint32_t y = 0; y < planeHeight; ++y) {
                    const uint16_t * srcRow16 = (const uint16_t *)srcRow;
                    for (uint32_t x = 0; x < planeWidth; ++x) {
                        dstRow[x] = (uint8_t)srcRow16[x];
                    }
                    srcRow += codec->internal->image->stride[yuvPlane];
                    dstRow += dstRowBytes;
                }
            }
        } else {
            // Steal the pointers from the decoder's image directly
            for (int yuvPlane = 0; yuvPlane < yuvPlaneCount; ++yuvPlane) {
                image->yuvPlanes[yuvPlane] = codec->internal->image->planes[yuvPlane];
                image->yuvRowBytes[yuvPlane] = codec->internal->image->stride[yuvPlane];
            }
            image->imageOwnsYUVPlanes = AVIF_FALSE;
        }
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

        if (!avifImageUsesU16(image) && (codec->internal->image->fmt & AVM_IMG_FMT_HIGHBITDEPTH)) {
            AVIF_CHECK(avifImageAllocatePlanes(image, AVIF_PLANES_A) == AVIF_RESULT_OK);
            const uint8_t * srcRow = codec->internal->image->planes[0];
            uint8_t * dstRow = image->alphaPlane;
            for (uint32_t y = 0; y < image->height; ++y) {
                const uint16_t * srcRow16 = (const uint16_t *)srcRow;
                for (uint32_t x = 0; x < image->width; ++x) {
                    dstRow[x] = (uint8_t)srcRow16[x];
                }
                srcRow += codec->internal->image->stride[0];
                dstRow += image->alphaRowBytes;
            }
        } else {
            image->alphaPlane = codec->internal->image->planes[0];
            image->alphaRowBytes = codec->internal->image->stride[0];
            image->imageOwnsAlphaPlane = AVIF_FALSE;
        }
        *isLimitedRangeAlpha = (codec->internal->image->range == AVM_CR_STUDIO_RANGE);
    }

    return AVIF_TRUE;
}

static avm_img_fmt_t avifImageCalcAVMFmt(const avifImage * image, avifBool alpha)
{
    avm_img_fmt_t fmt;
    if (alpha) {
        // We're going monochrome, who cares about chroma quality
        fmt = AVM_IMG_FMT_I420;
    } else {
        switch (image->yuvFormat) {
            case AVIF_PIXEL_FORMAT_YUV444:
                fmt = AVM_IMG_FMT_I444;
                break;
            case AVIF_PIXEL_FORMAT_YUV422:
                fmt = AVM_IMG_FMT_I422;
                break;
            case AVIF_PIXEL_FORMAT_YUV420:
            case AVIF_PIXEL_FORMAT_YUV400:
                fmt = AVM_IMG_FMT_I420;
                break;
            case AVIF_PIXEL_FORMAT_NONE:
            case AVIF_PIXEL_FORMAT_COUNT:
            default:
                return AVM_IMG_FMT_NONE;
        }
    }

    if (image->depth > 8) {
        fmt |= AVM_IMG_FMT_HIGHBITDEPTH;
    }

    return fmt;
}

struct avmOptionEnumList
{
    const char * name;
    int val;
};

static avifBool avmOptionParseEnum(const char * str, const struct avmOptionEnumList * enums, int * val)
{
    const struct avmOptionEnumList * listptr;
    long int rawval;
    char * endptr;

    // First see if the value can be parsed as a raw value.
    rawval = strtol(str, &endptr, 10);
    if (str[0] != '\0' && endptr[0] == '\0') {
        // Got a raw value, make sure it's valid.
        for (listptr = enums; listptr->name; listptr++)
            if (listptr->val == rawval) {
                *val = (int)rawval;
                return AVIF_TRUE;
            }
    }

    // Next see if it can be parsed as a string.
    for (listptr = enums; listptr->name; listptr++) {
        if (!strcmp(str, listptr->name)) {
            *val = listptr->val;
            return AVIF_TRUE;
        }
    }

    return AVIF_FALSE;
}

static const struct avmOptionEnumList endUsageEnum[] = { //
    { "vbr", AVM_VBR },                                  // Variable Bit Rate (VBR) mode
    { "cbr", AVM_CBR },                                  // Constant Bit Rate (CBR) mode
    { "cq", AVM_CQ },                                    // Constrained Quality (CQ) mode
    { "q", AVM_Q },                                      // Constant Quality (Q) mode
    { NULL, 0 }
};

// Returns true if <key> equals <name> or <prefix><name>, where <prefix> is "color:" or "alpha:"
// or the abbreviated form "c:" or "a:".
static avifBool avifKeyEqualsName(const char * key, const char * name, avifBool alpha)
{
    const char * prefix = alpha ? "alpha:" : "color:";
    size_t prefixLen = 6;
    const char * shortPrefix = alpha ? "a:" : "c:";
    size_t shortPrefixLen = 2;
    return !strcmp(key, name) || (!strncmp(key, prefix, prefixLen) && !strcmp(key + prefixLen, name)) ||
           (!strncmp(key, shortPrefix, shortPrefixLen) && !strcmp(key + shortPrefixLen, name));
}

static avifBool avifProcessAVMOptionsPreInit(avifCodec * codec, avifBool alpha, struct avm_codec_enc_cfg * cfg)
{
    for (uint32_t i = 0; i < codec->csOptions->count; ++i) {
        avifCodecSpecificOption * entry = &codec->csOptions->entries[i];
        int val;
        if (avifKeyEqualsName(entry->key, "end-usage", alpha)) { // Rate control mode
            if (!avmOptionParseEnum(entry->value, endUsageEnum, &val)) {
                avifDiagnosticsPrintf(codec->diag, "Invalid value for end-usage: %s", entry->value);
                return AVIF_FALSE;
            }
            cfg->rc_end_usage = val;
        }
    }
    return AVIF_TRUE;
}

static avifBool avifProcessAVMOptionsPostInit(avifCodec * codec, avifBool alpha)
{
    for (uint32_t i = 0; i < codec->csOptions->count; ++i) {
        avifCodecSpecificOption * entry = &codec->csOptions->entries[i];
        // Skip options for the other kind of plane.
        const char * otherPrefix = alpha ? "color:" : "alpha:";
        size_t otherPrefixLen = 6;
        const char * otherShortPrefix = alpha ? "c:" : "a:";
        size_t otherShortPrefixLen = 2;
        if (!strncmp(entry->key, otherPrefix, otherPrefixLen) || !strncmp(entry->key, otherShortPrefix, otherShortPrefixLen)) {
            continue;
        }

        // Skip options processed by avifProcessAVMOptionsPreInit.
        if (avifKeyEqualsName(entry->key, "end-usage", alpha)) {
            continue;
        }

        const char * prefix = alpha ? "alpha:" : "color:";
        size_t prefixLen = 6;
        const char * shortPrefix = alpha ? "a:" : "c:";
        size_t shortPrefixLen = 2;
        const char * key = entry->key;
        if (!strncmp(key, prefix, prefixLen)) {
            key += prefixLen;
        } else if (!strncmp(key, shortPrefix, shortPrefixLen)) {
            key += shortPrefixLen;
        }
        if (avm_codec_set_option(&codec->internal->encoder, key, entry->value) != AVM_CODEC_OK) {
            avifDiagnosticsPrintf(codec->diag,
                                  "avm_codec_set_option(\"%s\", \"%s\") failed: %s: %s",
                                  key,
                                  entry->value,
                                  avm_codec_error(&codec->internal->encoder),
                                  avm_codec_error_detail(&codec->internal->encoder));
            return AVIF_FALSE;
        }
        if (!strcmp(key, "tune")) {
            codec->internal->tuningSet = AVIF_TRUE;
        }
    }
    return AVIF_TRUE;
}

struct avmScalingModeMapList
{
    avifFraction avifMode;
    AVM_SCALING_MODE avmMode;
};

static const struct avmScalingModeMapList scalingModeMap[] = {
    { { 1, 1 }, AVME_NORMAL },    { { 1, 2 }, AVME_ONETWO },    { { 1, 4 }, AVME_ONEFOUR },  { { 1, 8 }, AVME_ONEEIGHT },
    { { 3, 4 }, AVME_THREEFOUR }, { { 3, 5 }, AVME_THREEFIVE }, { { 4, 5 }, AVME_FOURFIVE },
};

static const int scalingModeMapSize = sizeof(scalingModeMap) / sizeof(scalingModeMap[0]);

static avifBool avifFindAVMScalingMode(const avifFraction * avifMode, AVM_SCALING_MODE * avmMode)
{
    avifFraction simplifiedFraction = *avifMode;
    avifFractionSimplify(&simplifiedFraction);
    for (int i = 0; i < scalingModeMapSize; ++i) {
        if (scalingModeMap[i].avifMode.n == simplifiedFraction.n && scalingModeMap[i].avifMode.d == simplifiedFraction.d) {
            *avmMode = scalingModeMap[i].avmMode;
            return AVIF_TRUE;
        }
    }

    return AVIF_FALSE;
}

// Scales from aom's [0:63] to avm's [M:255], where M=0/-48/-96 for 8/10/12 bit.
// See --min-qp help in
// https://gitlab.com/AOMediaCodec/avm/-/blob/main/apps/avmenc.c
static int avmScaleQuantizer(int quantizer, uint32_t depth)
{
    if (depth == 10) {
        return AVIF_CLAMP((quantizer * (255 + 48) + 31) / 63 - 48, -48, 255);
    }
    if (depth == 12) {
        return AVIF_CLAMP((quantizer * (255 + 96) + 31) / 63 - 96, -96, 255);
    }
    assert(depth == 8);
    return AVIF_CLAMP((quantizer * 255 + 31) / 63, 0, 255);
}

// Converts quality to avm's quantizer in the range of [M:255], where M=0/-48/-96 for 8/10/12 bit.
// See --min-qp help in
// https://gitlab.com/AOMediaCodec/avm/-/blob/main/apps/avmenc.c
static int avmQualityToQuantizer(int quality, uint32_t depth)
{
    if (depth == 10) {
        return 255 - (quality * (255 + 48) + 50) / 100;
    }
    if (depth == 12) {
        return 255 - (quality * (255 + 96) + 50) / 100;
    }
    assert(depth == 8);
    return 255 - (quality * 255 + 50) / 100;
}

static avifBool avmCodecEncodeFinish(avifCodec * codec, avifCodecEncodeOutput * output);

static avifResult avmCodecEncodeImage(avifCodec * codec,
                                      avifEncoder * encoder,
                                      const avifImage * image,
                                      avifBool alpha,
                                      int tileRowsLog2,
                                      int tileColsLog2,
                                      int quality,
                                      avifEncoderChanges encoderChanges,
                                      avifBool disableLaggedOutput,
                                      avifAddImageFlags addImageFlags,
                                      avifCodecEncodeOutput * output)
{
    struct avm_codec_enc_cfg * cfg = &codec->internal->cfg;
    avifBool quantizerUpdated = AVIF_FALSE;
    const int quantizer = avmQualityToQuantizer(quality, image->depth);

    // For encoder->scalingMode.horizontal and encoder->scalingMode.vertical to take effect in AV2
    // encoder, config should be applied for each frame, so we don't care about changes on these
    // two fields.
    encoderChanges &= ~AVIF_ENCODER_CHANGE_SCALING_MODE;

    if (!codec->internal->encoderInitialized) {
        AVIF_CHECKRES(avifCheckCodecVersionAVM());

        int avmCpuUsed = -1;
        if (encoder->speed != AVIF_SPEED_DEFAULT) {
            avmCpuUsed = AVIF_CLAMP(encoder->speed, 0, 9);
        }

        codec->internal->avmFormat = avifImageCalcAVMFmt(image, alpha);
        if (codec->internal->avmFormat == AVM_IMG_FMT_NONE) {
            return AVIF_RESULT_UNKNOWN_ERROR;
        }

        avifGetPixelFormatInfo(image->yuvFormat, &codec->internal->formatInfo);

        avm_codec_iface_t * encoderInterface = avm_codec_av2_cx();
        avm_codec_err_t err = avm_codec_enc_config_default(encoderInterface, cfg, AVM_USAGE_GOOD_QUALITY);
        if (err != AVM_CODEC_OK) {
            avifDiagnosticsPrintf(codec->diag, "avm_codec_enc_config_default() failed: %s", avm_codec_err_to_string(err));
            return AVIF_RESULT_UNKNOWN_ERROR;
        }

        // avm's default is AVM_VBR. Change the default to AVM_Q since we don't need to hit a certain target bit rate.
        // It's easier to control the worst quality in Q mode.
        cfg->rc_end_usage = AVM_Q;

        // Profile 0.  8-bit and 10-bit 4:2:0 and 4:0:0 only.
        // Profile 1.  8-bit and 10-bit 4:4:4
        // Profile 2.  8-bit and 10-bit 4:2:2
        //            12-bit 4:0:0, 4:2:0, 4:2:2 and 4:4:4
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
                    case AVIF_PIXEL_FORMAT_YUV400:
                        seqProfile = 0;
                        break;
                    case AVIF_PIXEL_FORMAT_NONE:
                    case AVIF_PIXEL_FORMAT_COUNT:
                    default:
                        break;
                }
            }
        }

        cfg->g_profile = seqProfile;
        cfg->g_bit_depth = image->depth;
        cfg->g_input_bit_depth = image->depth;
        cfg->g_w = image->width;
        cfg->g_h = image->height;
        if (addImageFlags & AVIF_ADD_IMAGE_FLAG_SINGLE) {
            // Set the maximum number of frames to encode to 1. This instructs
            // libavm to set still_picture and reduced_still_picture_header to
            // 1 in AV2 sequence headers.
            cfg->g_limit = 1;

            // Use the default settings of the new AVM_USAGE_ALL_INTRA (added in
            // https://crbug.com/aomedia/2959).
            //
            // Set g_lag_in_frames to 0 to reduce the number of frame buffers
            // (from 20 to 2) in libavm's lookahead structure. This reduces
            // memory consumption when encoding a single image.
            cfg->g_lag_in_frames = 0;
            // Disable automatic placement of key frames by the encoder.
            cfg->kf_mode = AVM_KF_DISABLED;
            // Tell libavm that all frames will be key frames.
            cfg->kf_max_dist = 0;
        }
        if (encoder->extraLayerCount > 0) {
            cfg->g_limit = encoder->extraLayerCount + 1;
            // For layered image, disable lagged encoding to always get output
            // frame for each input frame.
            cfg->g_lag_in_frames = 0;
        }
        if (disableLaggedOutput) {
            cfg->g_lag_in_frames = 0;
        }
        if (encoder->maxThreads > 1) {
            // libavm fails if cfg->g_threads is greater than 64 threads. See MAX_NUM_THREADS in
            // avm/avm_util/avm_thread.h.
            cfg->g_threads = AVIF_MIN(encoder->maxThreads, 64);
        }

        codec->internal->monochromeEnabled = AVIF_FALSE;
        if (alpha || (image->yuvFormat == AVIF_PIXEL_FORMAT_YUV400)) {
            codec->internal->monochromeEnabled = AVIF_TRUE;
            cfg->monochrome = 1;
        }

        if (!avifProcessAVMOptionsPreInit(codec, alpha, cfg)) {
            return AVIF_RESULT_INVALID_CODEC_SPECIFIC_OPTION;
        }

        int minQuantizer;
        int maxQuantizer;
        if (alpha) {
            minQuantizer = encoder->minQuantizerAlpha;
            maxQuantizer = encoder->maxQuantizerAlpha;
        } else {
            minQuantizer = encoder->minQuantizer;
            maxQuantizer = encoder->maxQuantizer;
        }
        minQuantizer = avmScaleQuantizer(minQuantizer, image->depth);
        maxQuantizer = avmScaleQuantizer(maxQuantizer, image->depth);
        if ((cfg->rc_end_usage == AVM_VBR) || (cfg->rc_end_usage == AVM_CBR)) {
            // cq-level is ignored in these two end-usage modes, so adjust minQuantizer and
            // maxQuantizer to the target quantizer.
            if (quantizer == AVIF_QUANTIZER_LOSSLESS) {
                minQuantizer = AVIF_QUANTIZER_LOSSLESS;
                maxQuantizer = AVIF_QUANTIZER_LOSSLESS;
            } else {
                minQuantizer = AVIF_MAX(quantizer - 4, minQuantizer);
                maxQuantizer = AVIF_MIN(quantizer + 4, maxQuantizer);
            }
        }
        cfg->rc_min_quantizer = minQuantizer;
        cfg->rc_max_quantizer = maxQuantizer;
        quantizerUpdated = AVIF_TRUE;

        if (avm_codec_enc_init(&codec->internal->encoder, encoderInterface, cfg, /*flags=*/0) != AVM_CODEC_OK) {
            avifDiagnosticsPrintf(codec->diag,
                                  "avm_codec_enc_init() failed: %s: %s",
                                  avm_codec_error(&codec->internal->encoder),
                                  avm_codec_error_detail(&codec->internal->encoder));
            return AVIF_RESULT_UNKNOWN_ERROR;
        }
        codec->internal->encoderInitialized = AVIF_TRUE;

        if ((cfg->rc_end_usage == AVM_CQ) || (cfg->rc_end_usage == AVM_Q)) {
            avm_codec_control(&codec->internal->encoder, AVME_SET_QP, quantizer);
        }
        avifBool lossless = (quantizer == AVIF_QUANTIZER_LOSSLESS);
        if (lossless) {
            avm_codec_control(&codec->internal->encoder, AV2E_SET_LOSSLESS, 1);
        }
        if (encoder->maxThreads > 1) {
            avm_codec_control(&codec->internal->encoder, AV2E_SET_ROW_MT, 1);
        }
        if (tileRowsLog2 != 0) {
            avm_codec_control(&codec->internal->encoder, AV2E_SET_TILE_ROWS, tileRowsLog2);
        }
        if (tileColsLog2 != 0) {
            avm_codec_control(&codec->internal->encoder, AV2E_SET_TILE_COLUMNS, tileColsLog2);
        }
        if (encoder->extraLayerCount > 0) {
            int layerCount = encoder->extraLayerCount + 1;
            if (avm_codec_control(&codec->internal->encoder, AVME_SET_NUMBER_MLAYERS, layerCount) != AVM_CODEC_OK) {
                return AVIF_RESULT_UNKNOWN_ERROR;
            }
        }
        if (avmCpuUsed != -1) {
            if (avm_codec_control(&codec->internal->encoder, AVME_SET_CPUUSED, avmCpuUsed) != AVM_CODEC_OK) {
                return AVIF_RESULT_UNKNOWN_ERROR;
            }
        }

        // Set color_config() in the sequence header OBU.
        if (alpha) {
            // AV1-AVIF specification, Section 4 "Auxiliary Image Items and Sequences":
            //   The color_range field in the Sequence Header OBU shall be set to 1.
            avm_codec_control(&codec->internal->encoder, AV2E_SET_COLOR_RANGE, AVM_CR_FULL_RANGE);

            // Keep the default AVM_CSP_UNKNOWN value.

            // CICP (CP/TC/MC) does not apply to the alpha auxiliary image.
            // Keep default Unspecified (2) colour primaries, transfer characteristics,
            // and matrix coefficients.
        } else {
            // libavm's defaults are AVM_CSP_UNKNOWN and 0 (studio/limited range).
            // Call avm_codec_control() only if the values are not the defaults.

            // AV1-AVIF specification, Section 2.2.1. "AV1 Item Configuration Property":
            //   The values of the fields in the AV1CodecConfigurationBox shall match those
            //   of the Sequence Header OBU in the AV1 Image Item Data.
            if (image->yuvChromaSamplePosition != AVIF_CHROMA_SAMPLE_POSITION_UNKNOWN) {
                avm_codec_control(&codec->internal->encoder, AV2E_SET_CHROMA_SAMPLE_POSITION, (int)image->yuvChromaSamplePosition);
            }

            // AV1-ISOBMFF specification, Section 2.3.4:
            //   The value of full_range_flag in the 'colr' box SHALL match the color_range
            //   flag in the Sequence Header OBU.
            if (image->yuvRange != AVIF_RANGE_LIMITED) {
                avm_codec_control(&codec->internal->encoder, AV2E_SET_COLOR_RANGE, (int)image->yuvRange);
            }

            // Section 2.3.4 of AV1-ISOBMFF says 'colr' with 'nclx' should be present and shall match CICP
            // values in the Sequence Header OBU, unless the latter has 2/2/2 (Unspecified).
            // So set CICP values to 2/2/2 (Unspecified) in the Sequence Header OBU for simplicity.
            // libavm's defaults are AVM_CICP_CP_UNSPECIFIED, AVM_CICP_TC_UNSPECIFIED, and
            // AVM_CICP_MC_UNSPECIFIED. No need to call avm_codec_control().
            // avm_image_t::cp, avm_image_t::tc and avm_image_t::mc are ignored by avm_codec_encode().
        }

        if (!avifProcessAVMOptionsPostInit(codec, alpha)) {
            return AVIF_RESULT_INVALID_CODEC_SPECIFIC_OPTION;
        }
        // Disabling these two gives 1.19% PSNR YUV loss in All-Intra config, but encode will be ~4X faster.
        if (avm_codec_set_option(&codec->internal->encoder, "enable-ext-partitions", "0") != AVM_CODEC_OK ||
            avm_codec_set_option(&codec->internal->encoder, "enable-uneven-4way-partitions", "0") != AVM_CODEC_OK) {
            return AVIF_RESULT_UNKNOWN_ERROR;
        }
        if (!codec->internal->tuningSet) {
            if (avm_codec_control(&codec->internal->encoder, AVME_SET_TUNING, AVM_TUNE_SSIM) != AVM_CODEC_OK) {
                return AVIF_RESULT_UNKNOWN_ERROR;
            }
        }
    } else {
        avifBool dimensionsChanged = AVIF_FALSE;
        if ((cfg->g_w != image->width) || (cfg->g_h != image->height)) {
            // We are not ready for dimension change for now.
            return AVIF_RESULT_NOT_IMPLEMENTED;
        }
        if (alpha) {
            if (encoderChanges & (AVIF_ENCODER_CHANGE_MIN_QUANTIZER_ALPHA | AVIF_ENCODER_CHANGE_MAX_QUANTIZER_ALPHA)) {
                cfg->rc_min_quantizer = avmScaleQuantizer(encoder->minQuantizerAlpha, image->depth);
                cfg->rc_max_quantizer = avmScaleQuantizer(encoder->maxQuantizerAlpha, image->depth);
                quantizerUpdated = AVIF_TRUE;
            }
        } else {
            if (encoderChanges & (AVIF_ENCODER_CHANGE_MIN_QUANTIZER | AVIF_ENCODER_CHANGE_MAX_QUANTIZER)) {
                cfg->rc_min_quantizer = avmScaleQuantizer(encoder->minQuantizer, image->depth);
                cfg->rc_max_quantizer = avmScaleQuantizer(encoder->maxQuantizer, image->depth);
                quantizerUpdated = AVIF_TRUE;
            }
        }
        const int qualityChangedBit = alpha ? AVIF_ENCODER_CHANGE_QUALITY_ALPHA : AVIF_ENCODER_CHANGE_QUALITY;
        if (encoderChanges & qualityChangedBit) {
            if ((cfg->rc_end_usage == AVM_VBR) || (cfg->rc_end_usage == AVM_CBR)) {
                // cq-level is ignored in these two end-usage modes, so adjust minQuantizer and
                // maxQuantizer to the target quantizer.
                if (quantizer == AVIF_QUANTIZER_LOSSLESS) {
                    cfg->rc_min_quantizer = AVIF_QUANTIZER_LOSSLESS;
                    cfg->rc_max_quantizer = AVIF_QUANTIZER_LOSSLESS;
                } else {
                    int minQuantizer;
                    int maxQuantizer;
                    if (alpha) {
                        minQuantizer = encoder->minQuantizerAlpha;
                        maxQuantizer = encoder->maxQuantizerAlpha;
                    } else {
                        minQuantizer = encoder->minQuantizer;
                        maxQuantizer = encoder->maxQuantizer;
                    }
                    minQuantizer = avmScaleQuantizer(minQuantizer, image->depth);
                    maxQuantizer = avmScaleQuantizer(maxQuantizer, image->depth);
                    cfg->rc_min_quantizer = AVIF_MAX(quantizer - 4, minQuantizer);
                    cfg->rc_max_quantizer = AVIF_MIN(quantizer + 4, maxQuantizer);
                }
                quantizerUpdated = AVIF_TRUE;
            }
        }
        if (quantizerUpdated || dimensionsChanged) {
            avm_codec_err_t err = avm_codec_enc_config_set(&codec->internal->encoder, cfg);
            if (err != AVM_CODEC_OK) {
                avifDiagnosticsPrintf(codec->diag,
                                      "avm_codec_enc_config_set() failed: %s: %s",
                                      avm_codec_error(&codec->internal->encoder),
                                      avm_codec_error_detail(&codec->internal->encoder));
                return AVIF_RESULT_UNKNOWN_ERROR;
            }
        }
        if (encoderChanges & AVIF_ENCODER_CHANGE_TILE_ROWS_LOG2) {
            avm_codec_control(&codec->internal->encoder, AV2E_SET_TILE_ROWS, tileRowsLog2);
        }
        if (encoderChanges & AVIF_ENCODER_CHANGE_TILE_COLS_LOG2) {
            avm_codec_control(&codec->internal->encoder, AV2E_SET_TILE_COLUMNS, tileColsLog2);
        }
        if (encoderChanges & qualityChangedBit) {
            if ((cfg->rc_end_usage == AVM_CQ) || (cfg->rc_end_usage == AVM_Q)) {
                avm_codec_control(&codec->internal->encoder, AVME_SET_QP, quantizer);
            }
            avifBool lossless = (quantizer == AVIF_QUANTIZER_LOSSLESS);
            avm_codec_control(&codec->internal->encoder, AV2E_SET_LOSSLESS, lossless);
        }
        if (encoderChanges & AVIF_ENCODER_CHANGE_CODEC_SPECIFIC) {
            if (!avifProcessAVMOptionsPostInit(codec, alpha)) {
                return AVIF_RESULT_INVALID_CODEC_SPECIFIC_OPTION;
            }
        }
    }

    if (codec->internal->currentLayer > encoder->extraLayerCount) {
        avifDiagnosticsPrintf(codec->diag,
                              "Too many layers sent. Expected %u layers, but got %u layers.",
                              encoder->extraLayerCount + 1,
                              codec->internal->currentLayer + 1);
        return AVIF_RESULT_INVALID_ARGUMENT;
    }
    if (encoder->extraLayerCount > 0) {
        avm_codec_control(&codec->internal->encoder, AVME_SET_MLAYER_ID, codec->internal->currentLayer);
    }

    avm_scaling_mode_t avmScalingMode;
    if (!avifFindAVMScalingMode(&encoder->scalingMode.horizontal, &avmScalingMode.h_scaling_mode)) {
        return AVIF_RESULT_NOT_IMPLEMENTED;
    }
    if (!avifFindAVMScalingMode(&encoder->scalingMode.vertical, &avmScalingMode.v_scaling_mode)) {
        return AVIF_RESULT_NOT_IMPLEMENTED;
    }
    if ((avmScalingMode.h_scaling_mode != AVME_NORMAL) || (avmScalingMode.v_scaling_mode != AVME_NORMAL)) {
        // AVME_SET_SCALEMODE only applies to next frame (layer), so we have to set it every time.
        avm_codec_control(&codec->internal->encoder, AVME_SET_SCALEMODE, &avmScalingMode);
    }

    avm_image_t avmImage;
    // We prefer to simply set the avmImage.planes[] pointers to the plane buffers in 'image'. When
    // doing this, we set avmImage.w equal to avmImage.d_w and avmImage.h equal to avmImage.d_h and
    // do not "align" avmImage.w and avmImage.h. Unfortunately this exposes a libaom bug in libavm
    // (https://crbug.com/aomedia/3113) if chroma is subsampled and image->width or image->height is
    // equal to 1. To work around this libavm bug, we allocate the avmImage.planes[] buffers and
    // copy the image YUV data if image->width or image->height is equal to 1. This bug has been
    // fixed in libaom v3.1.3 but not in libavm.
    //
    // Note: The exact condition for the bug is
    //   ((image->width == 1) && (chroma is subsampled horizontally)) ||
    //   ((image->height == 1) && (chroma is subsampled vertically))
    // Since an image width or height of 1 is uncommon in practice, we test an inexact but simpler
    // condition.
    avifBool avmImageAllocated = (image->width == 1) || (image->height == 1);
    if (avmImageAllocated) {
        avm_img_alloc(&avmImage, codec->internal->avmFormat, image->width, image->height, 16);
    } else {
        memset(&avmImage, 0, sizeof(avmImage));
        avmImage.fmt = codec->internal->avmFormat;
        avmImage.bit_depth = (image->depth > 8) ? 16 : 8;
        avmImage.w = image->width;
        avmImage.h = image->height;
        avmImage.d_w = image->width;
        avmImage.d_h = image->height;
        // Get sample size for this format.
        unsigned int bps;
        if (codec->internal->avmFormat == AVM_IMG_FMT_I420) {
            bps = 12;
        } else if (codec->internal->avmFormat == AVM_IMG_FMT_I422) {
            bps = 16;
        } else if (codec->internal->avmFormat == AVM_IMG_FMT_I444) {
            bps = 24;
        } else if (codec->internal->avmFormat == AVM_IMG_FMT_I42016) {
            bps = 24;
        } else if (codec->internal->avmFormat == AVM_IMG_FMT_I42216) {
            bps = 32;
        } else if (codec->internal->avmFormat == AVM_IMG_FMT_I44416) {
            bps = 48;
        } else {
            bps = 16;
        }
        avmImage.bps = bps;
        // See avifImageCalcAVMFmt(). libavm doesn't have AVM_IMG_FMT_I400, so we use AVM_IMG_FMT_I420 as a substitute for monochrome.
        avmImage.x_chroma_shift = (alpha || codec->internal->formatInfo.monochrome) ? 1 : codec->internal->formatInfo.chromaShiftX;
        avmImage.y_chroma_shift = (alpha || codec->internal->formatInfo.monochrome) ? 1 : codec->internal->formatInfo.chromaShiftY;
    }

    avifBool monochromeRequested = AVIF_FALSE;

    if (alpha) {
        // AV1-AVIF specification, Section 4 "Auxiliary Image Items and Sequences":
        //   The color_range field in the Sequence Header OBU shall be set to 1.
        avmImage.range = AVM_CR_FULL_RANGE;

        // AV1-AVIF specification, Section 4 "Auxiliary Image Items and Sequences":
        //   The mono_chrome field in the Sequence Header OBU shall be set to 1.
        // Some encoders do not support 4:0:0 and encode alpha as 4:2:0 so it is not always respected.
        monochromeRequested = AVIF_TRUE;
        if (avmImageAllocated) {
            const uint32_t bytesPerRow = ((image->depth > 8) ? 2 : 1) * image->width;
            for (uint32_t j = 0; j < image->height; ++j) {
                const uint8_t * srcAlphaRow = &image->alphaPlane[j * image->alphaRowBytes];
                uint8_t * dstAlphaRow = &avmImage.planes[0][j * avmImage.stride[0]];
                memcpy(dstAlphaRow, srcAlphaRow, bytesPerRow);
            }
        } else {
            avmImage.planes[0] = image->alphaPlane;
            avmImage.stride[0] = image->alphaRowBytes;
        }

        // Ignore UV planes when monochrome. Keep the default AVM_CSP_UNKNOWN value.
    } else {
        int yuvPlaneCount = 3;
        if (image->yuvFormat == AVIF_PIXEL_FORMAT_YUV400) {
            yuvPlaneCount = 1; // Ignore UV planes when monochrome
            monochromeRequested = AVIF_TRUE;
        }
        if (avmImageAllocated) {
            uint32_t bytesPerPixel = (image->depth > 8) ? 2 : 1;
            for (int yuvPlane = 0; yuvPlane < yuvPlaneCount; ++yuvPlane) {
                uint32_t planeWidth = avifImagePlaneWidth(image, yuvPlane);
                uint32_t planeHeight = avifImagePlaneHeight(image, yuvPlane);
                uint32_t bytesPerRow = bytesPerPixel * planeWidth;

                for (uint32_t j = 0; j < planeHeight; ++j) {
                    const uint8_t * srcRow = &image->yuvPlanes[yuvPlane][j * image->yuvRowBytes[yuvPlane]];
                    uint8_t * dstRow = &avmImage.planes[yuvPlane][j * avmImage.stride[yuvPlane]];
                    memcpy(dstRow, srcRow, bytesPerRow);
                }
            }
        } else {
            for (int yuvPlane = 0; yuvPlane < yuvPlaneCount; ++yuvPlane) {
                avmImage.planes[yuvPlane] = image->yuvPlanes[yuvPlane];
                avmImage.stride[yuvPlane] = image->yuvRowBytes[yuvPlane];
            }
        }

        // AV1-AVIF specification, Section 2.2.1. "AV1 Item Configuration Property":
        //   The values of the fields in the AV1CodecConfigurationBox shall match those
        //   of the Sequence Header OBU in the AV1 Image Item Data.
        if (image->yuvChromaSamplePosition == AVIF_CHROMA_SAMPLE_POSITION_VERTICAL) {
            // CSP_LEFT: Horizontal offset 0, vertical offset 0.5
            avmImage.csp = AVM_CSP_LEFT;
        } else if (image->yuvChromaSamplePosition == AVIF_CHROMA_SAMPLE_POSITION_COLOCATED) {
            // CSP_TOPLEFT: Horizontal offset 0, vertical offset 0
            avmImage.csp = AVM_CSP_TOPLEFT;
        } else if (image->yuvChromaSamplePosition == AVIF_CHROMA_SAMPLE_POSITION_RESERVED) {
            // CSP_CENTER: Horizontal offset 0.5, vertical offset 0.5
            avmImage.csp = AVM_CSP_CENTER;
        } else { // AVIF_CHROMA_SAMPLE_POSITION_UNKNOWN or invalid values
            avmImage.csp = AVM_CSP_UNSPECIFIED;
        }

        // AV1-ISOBMFF specification, Section 2.3.4:
        //   The value of full_range_flag in the 'colr' box SHALL match the color_range
        //   flag in the Sequence Header OBU.
        avmImage.range = (avm_color_range_t)image->yuvRange;
    }

    unsigned char * monoUVPlane = NULL;
    if (monochromeRequested) {
        if (codec->internal->monochromeEnabled) {
            avmImage.monochrome = 1;
        } else {
            // The user requested monochrome (via alpha or YUV400) but libavm does not support
            // monochrome. Manually set UV planes to 0.5.

            // avmImage is always 420 when we're monochrome
            uint32_t monoUVWidth = (image->width + 1) >> 1;
            uint32_t monoUVHeight = (image->height + 1) >> 1;

            // Allocate the U plane if necessary.
            if (!avmImageAllocated) {
                uint32_t channelSize = avifImageUsesU16(image) ? 2 : 1;
                uint32_t monoUVRowBytes = channelSize * monoUVWidth;
                size_t monoUVSize = (size_t)monoUVHeight * monoUVRowBytes;

                monoUVPlane = avifAlloc(monoUVSize);
                AVIF_CHECKERR(monoUVPlane != NULL, AVIF_RESULT_OUT_OF_MEMORY); // No need for avm_img_free() because !avmImageAllocated
                avmImage.planes[1] = monoUVPlane;
                avmImage.stride[1] = monoUVRowBytes;
            }
            // Set the U plane to 0.5.
            if (image->depth > 8) {
                const uint16_t half = 1 << (image->depth - 1);
                for (uint32_t j = 0; j < monoUVHeight; ++j) {
                    uint16_t * dstRow = (uint16_t *)&avmImage.planes[1][(size_t)j * avmImage.stride[1]];
                    for (uint32_t i = 0; i < monoUVWidth; ++i) {
                        dstRow[i] = half;
                    }
                }
            } else {
                const uint8_t half = 128;
                size_t planeSize = (size_t)monoUVHeight * avmImage.stride[1];
                memset(avmImage.planes[1], half, planeSize);
            }
            // Make the V plane the same as the U plane.
            avmImage.planes[2] = avmImage.planes[1];
            avmImage.stride[2] = avmImage.stride[1];
        }
    }

    avm_enc_frame_flags_t encodeFlags = 0;
    if (addImageFlags & AVIF_ADD_IMAGE_FLAG_FORCE_KEYFRAME) {
        encodeFlags |= AVM_EFLAG_FORCE_KF;
    }
    if (codec->internal->currentLayer > 0) {
        encodeFlags |= AVM_EFLAG_NO_REF_GF | AVM_EFLAG_NO_REF_ARF | AVM_EFLAG_NO_REF_BWD | AVM_EFLAG_NO_REF_ARF2 | AVM_EFLAG_NO_UPD_ALL;
    }
    avm_codec_err_t encodeErr = avm_codec_encode(&codec->internal->encoder, &avmImage, 0, 1, encodeFlags);
    avifFree(monoUVPlane);
    if (avmImageAllocated) {
        avm_img_free(&avmImage);
    }
    if (encodeErr != AVM_CODEC_OK) {
        avifDiagnosticsPrintf(codec->diag,
                              "avm_codec_encode() failed: %s: %s",
                              avm_codec_error(&codec->internal->encoder),
                              avm_codec_error_detail(&codec->internal->encoder));
        return AVIF_RESULT_UNKNOWN_ERROR;
    }

    avm_codec_iter_t iter = NULL;
    for (;;) {
        const avm_codec_cx_pkt_t * pkt = avm_codec_get_cx_data(&codec->internal->encoder, &iter);
        if (pkt == NULL) {
            break;
        }
        if (pkt->kind == AVM_CODEC_CX_FRAME_PKT) {
            AVIF_CHECKRES(
                avifCodecEncodeOutputAddSample(output, pkt->data.frame.buf, pkt->data.frame.sz, (pkt->data.frame.flags & AVM_FRAME_IS_KEY)));
        }
    }

    if ((addImageFlags & AVIF_ADD_IMAGE_FLAG_SINGLE) ||
        ((encoder->extraLayerCount > 0) && (encoder->extraLayerCount == codec->internal->currentLayer))) {
        // Flush and clean up encoder resources early to save on overhead when encoding alpha or grid images,
        // as encoding is finished now. For layered image, encoding finishes when the last layer is encoded.

        if (!avmCodecEncodeFinish(codec, output)) {
            return AVIF_RESULT_UNKNOWN_ERROR;
        }
        avm_codec_destroy(&codec->internal->encoder);
        codec->internal->encoderInitialized = AVIF_FALSE;
    }
    if (encoder->extraLayerCount > 0) {
        ++codec->internal->currentLayer;
    }
    return AVIF_RESULT_OK;
}

static avifBool avmCodecEncodeFinish(avifCodec * codec, avifCodecEncodeOutput * output)
{
    if (!codec->internal->encoderInitialized) {
        return AVIF_TRUE;
    }
    for (;;) {
        // flush encoder
        if (avm_codec_encode(&codec->internal->encoder, NULL, 0, 1, 0) != AVM_CODEC_OK) {
            avifDiagnosticsPrintf(codec->diag,
                                  "avm_codec_encode() with img=NULL failed: %s: %s",
                                  avm_codec_error(&codec->internal->encoder),
                                  avm_codec_error_detail(&codec->internal->encoder));
            return AVIF_FALSE;
        }

        avifBool gotPacket = AVIF_FALSE;
        avm_codec_iter_t iter = NULL;
        for (;;) {
            const avm_codec_cx_pkt_t * pkt = avm_codec_get_cx_data(&codec->internal->encoder, &iter);
            if (pkt == NULL) {
                break;
            }
            if (pkt->kind == AVM_CODEC_CX_FRAME_PKT) {
                gotPacket = AVIF_TRUE;
                const avifResult result = avifCodecEncodeOutputAddSample(output,
                                                                         pkt->data.frame.buf,
                                                                         pkt->data.frame.sz,
                                                                         (pkt->data.frame.flags & AVM_FRAME_IS_KEY));
                if (result != AVIF_RESULT_OK) {
                    avifDiagnosticsPrintf(codec->diag, "avifCodecEncodeOutputAddSample() failed: %s", avifResultToString(result));
                    return AVIF_FALSE;
                }
            }
        }

        if (!gotPacket) {
            break;
        }
    }
    return AVIF_TRUE;
}

const char * avifCodecVersionAVM(void)
{
    return avm_codec_version_str();
}

avifCodec * avifCodecCreateAVM(void)
{
    avifCodec * codec = (avifCodec *)avifAlloc(sizeof(avifCodec));
    if (codec == NULL) {
        return NULL;
    }
    memset(codec, 0, sizeof(struct avifCodec));

    codec->getNextImage = avmCodecGetNextImage;

    codec->encodeImage = avmCodecEncodeImage;
    codec->encodeFinish = avmCodecEncodeFinish;

    codec->destroyInternal = avmCodecDestroyInternal;
    codec->internal = (struct avifCodecInternal *)avifAlloc(sizeof(struct avifCodecInternal));
    if (codec->internal == NULL) {
        avifFree(codec);
        return NULL;
    }
    memset(codec->internal, 0, sizeof(struct avifCodecInternal));
    return codec;
}
