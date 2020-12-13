/*
 * Copyright (c) 2010, Google Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *   * Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *
 *   * Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in
 *     the documentation and/or other materials provided with the
 *     distribution.
 *
 *   * Neither the name of Google nor the names of its contributors may
 *     be used to endorse or promote products derived from this software
 *     without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

// Sharp YUV conversion algorithm originally from libwebp's picture_csp_enc.c,
// but completely rewritten and extended to handle different colorspace
// and bit depth.
//
// Any other code in here is under this license:
//
// Copyright 2020 Yuan Tong. All rights reserved.
// SPDX-License-Identifier: BSD-2-Clause

#include <float.h>
#include <math.h>
#include <memory.h>

#include "avif/internal.h"

static const double avifSharpYUVStopThreshold = 3.0 / 255.0;
static const uint32_t avifSharpYUVIterLimit = 4;

typedef struct avifSharpYUVReformatExtraState
{
    // MC coefficient to calculate Y
    float kr;
    float kg;
    float kb;

    // TC function to convert between gamma and linear
    float (*toLinear)(float);
    float (*fromLinear)(float);

    // LUT for gamma->linear
    uint32_t lutSize;
    float lutMax;
    float * toLinearLUT;

    // base weight assigned to each opaque pixel. Prevents divide-by-zero.
    float baseWeight;

    uint32_t floatSize;

    uint32_t wW;
    uint32_t hW;
    uint32_t rowFloatsW;

    uint32_t wDRGB;
    uint32_t hDRGB;
    uint32_t rowFloatsDRGB;
    uint32_t pixelFloatsDRGB;

    // We use a White-DiffRGB intermediate color representation.
    // This representation has clear relation with RGB value,
    // but also separates luma and chroma for processing.

    // result*: store the value that will be converted into YUV. Produced using NCL.
    float * resultW;
    float * resultDRGB;

    // target*: store the reference value. Produced using CL.
    float * targetW;
    float * targetDRGB;

    // measure*: store the measure value. result* -(NCL)-> RGB -(CL)-> measure*
    // used to compare with target* to show the difference to target.
    float * measureW;
    float * measureDRGB;

    // temp buffer to store RGB value
    float * tmpRGB;
    uint32_t rowFloatsRGB;
    uint32_t pixelFloatsRGB;

    // weights of each pixel in chroma down sampling process
    float * Weight;
    uint32_t rowFloatsWeight;

    double wDiffThreshold;
    double wDiffPrevious;

    avifBool shouldAlphaWeighted;
    // temp buffer to store alpha value
    float * tmpA;
    uint32_t rowFloatsA;

    uint32_t yW;
    uint32_t yH;
    uint32_t uvW;
    uint32_t uvH;

} avifSharpYUVReformatExtraState;

static avifResult avifPrepareSharpYUVReformatExtraState(const avifImage * image,
                                                        const avifRGBImage * rgb,
                                                        const avifReformatState * state,
                                                        avifSharpYUVReformatExtraState * exState)
{
    exState->kr = state->kr;
    exState->kg = state->kg;
    exState->kb = state->kb;

    float (*converter[2])(float);
    avifTransferCharacteristicsGetConverter(image->transferCharacteristics, converter);
    exState->toLinear = converter[0];
    exState->fromLinear = converter[1];

    exState->wW = (rgb->width + 1) & (~1);
    exState->wDRGB = exState->wW >> 1;
    uint32_t tmpHeight;
    if (image->yuvFormat == AVIF_PIXEL_FORMAT_YUV420) {
        exState->hW = (rgb->height + 1) & (~1);
        exState->hDRGB = exState->hW >> 1;
        tmpHeight = 2;
    } else if (image->yuvFormat == AVIF_PIXEL_FORMAT_YUV422) {
        exState->hW = rgb->height;
        exState->hDRGB = exState->hW;
        tmpHeight = 1;
    } else {
        return AVIF_RESULT_INVALID_ARGUMENT;
    }

    // 6 extra precision
    exState->lutSize = 1 << (image->depth + 6);
    exState->toLinearLUT = avifAlloc(exState->lutSize * sizeof(float));
    exState->lutMax = (float)(exState->lutSize - 1);

    for (uint32_t i = 0; i < exState->lutSize; ++i) {
        exState->toLinearLUT[i] = converter[0](i / exState->lutMax);
    }

    exState->wDiffThreshold = avifSharpYUVStopThreshold * exState->wW * exState->hW;
    exState->wDiffPrevious = DBL_MAX;
    exState->baseWeight = 1.f / (float)(1 << (image->depth));

    avifAlphaParams params;
    params.width = rgb->width;
    params.height = rgb->height;
    params.srcDepth = rgb->depth;
    params.srcRange = AVIF_RANGE_FULL;
    params.srcPlane = rgb->pixels;
    params.srcRowBytes = rgb->rowBytes;
    params.srcOffsetBytes = state->rgbOffsetBytesA;
    params.srcPixelBytes = state->rgbPixelBytes;

    exState->shouldAlphaWeighted = image->alphaPlane && image->alphaRowBytes && avifRGBFormatHasAlpha(rgb->format) &&
                                   !rgb->ignoreAlpha && !avifCheckAlphaOpaque(&params);

    exState->floatSize = sizeof(float);

    exState->rowFloatsW = exState->wW;
    exState->resultW = avifAlloc(exState->hW * exState->rowFloatsW * exState->floatSize);
    exState->targetW = avifAlloc(exState->hW * exState->rowFloatsW * exState->floatSize);
    exState->measureW = avifAlloc(tmpHeight * exState->rowFloatsW * exState->floatSize);

    exState->pixelFloatsDRGB = 3;
    exState->rowFloatsDRGB = exState->wDRGB * exState->pixelFloatsDRGB;
    exState->resultDRGB = avifAlloc(exState->hDRGB * exState->rowFloatsDRGB * exState->floatSize);
    exState->targetDRGB = avifAlloc(exState->hDRGB * exState->rowFloatsDRGB * exState->floatSize);
    exState->measureDRGB = avifAlloc(tmpHeight * exState->rowFloatsDRGB * exState->floatSize);

    exState->pixelFloatsRGB = 3;
    exState->rowFloatsRGB = exState->wW * exState->pixelFloatsRGB;
    exState->tmpRGB = avifAlloc(3 * exState->rowFloatsRGB * exState->floatSize);

    exState->rowFloatsWeight = exState->wW;
    exState->Weight = avifAlloc(exState->hW * exState->rowFloatsWeight * exState->floatSize);

    if (exState->shouldAlphaWeighted) {
        exState->rowFloatsA = exState->rowFloatsW;
        exState->tmpA = avifAlloc(1 * exState->rowFloatsA * exState->floatSize);
    }

    exState->yW = image->width;
    exState->yH = image->height;
    exState->uvW = (image->width + state->formatInfo.chromaShiftX) >> state->formatInfo.chromaShiftX;
    exState->uvH = (image->height + state->formatInfo.chromaShiftY) >> state->formatInfo.chromaShiftY;

    return AVIF_RESULT_OK;
}

static void avifReleaseSharpYUVReformatExtraState(avifSharpYUVReformatExtraState * exState)
{
    avifFree(exState->toLinearLUT);

    avifFree(exState->resultW);
    avifFree(exState->targetW);
    avifFree(exState->measureW);

    avifFree(exState->resultDRGB);
    avifFree(exState->targetDRGB);
    avifFree(exState->measureDRGB);

    avifFree(exState->tmpRGB);

    avifFree(exState->Weight);

    if (exState->shouldAlphaWeighted) {
        avifFree(exState->tmpA);
    }
}

static float avifSharpYUVToLinearLookup(const float gamma, const avifSharpYUVReformatExtraState * exState)
{
    int norm = (int)avifRoundf(gamma * exState->lutMax);
    if (norm < 0 || norm >= (int)exState->lutSize) {
        // fallback
        return exState->toLinear(gamma);
    }

    return exState->toLinearLUT[norm];
}

// Import RGB as normalized float point value.
static void avifSharpYUVImportRGBRow(const uint8_t * src,
                                     float * dst,
                                     uint32_t pic_w,
                                     const avifReformatState * state,
                                     const avifSharpYUVReformatExtraState * exState)
{
    uint32_t w = (pic_w + 1) & (~1);
    float maxF = state->rgbMaxChannelF;

    if (state->rgbDepth > 8) {
        for (uint32_t i = 0; i < pic_w; ++i) {
            dst[exState->pixelFloatsRGB * i + 0] = *((const uint16_t *)(&src[state->rgbOffsetBytesR + (i * state->rgbPixelBytes)])) / maxF;
            dst[exState->pixelFloatsRGB * i + 1] = *((const uint16_t *)(&src[state->rgbOffsetBytesG + (i * state->rgbPixelBytes)])) / maxF;
            dst[exState->pixelFloatsRGB * i + 2] = *((const uint16_t *)(&src[state->rgbOffsetBytesB + (i * state->rgbPixelBytes)])) / maxF;
        }
    } else {
        for (uint32_t i = 0; i < pic_w; ++i) {
            dst[exState->pixelFloatsRGB * i + 0] = src[state->rgbOffsetBytesR + (i * state->rgbPixelBytes)] / maxF;
            dst[exState->pixelFloatsRGB * i + 1] = src[state->rgbOffsetBytesG + (i * state->rgbPixelBytes)] / maxF;
            dst[exState->pixelFloatsRGB * i + 2] = src[state->rgbOffsetBytesB + (i * state->rgbPixelBytes)] / maxF;
        }
    }

    if (pic_w & 1) {
        memcpy(&dst[exState->pixelFloatsRGB * (w - 1)], &dst[exState->pixelFloatsRGB * (w - 2)], exState->floatSize * 3);
    }
}

// Import Alpha as normalized float point value.
static void avifSharpYUVImportAlphaRow(const uint8_t * src, float * dst, uint32_t pic_w, const avifReformatState * state)
{
    uint32_t w = (pic_w + 1) & (~1);
    float maxF = state->rgbMaxChannelF;

    if (state->rgbDepth > 8) {
        for (uint32_t i = 0; i < pic_w; ++i) {
            dst[i] = *((const uint16_t *)(&src[state->rgbOffsetBytesA + (i * state->rgbPixelBytes)])) / maxF;
        }
    } else {
        for (uint32_t i = 0; i < pic_w; ++i) {
            dst[i] = src[state->rgbOffsetBytesA + (i * state->rgbPixelBytes)] / maxF;
        }
    }

    if (pic_w & 1) {
        dst[w - 1] = dst[w - 2];
    }
}

// Compute luminance from RGB value, use traditional NCL method.
static void avifSharpYUVRGB2LumaNCL(const float * src, float * dst, const avifSharpYUVReformatExtraState * exState)
{
    float kr = exState->kr;
    float kg = exState->kg;
    float kb = exState->kb;

    uint32_t w = exState->wW;
    for (uint32_t i = 0; i < w; ++i) {
        dst[i] = kr * src[exState->pixelFloatsRGB * i + 0] + kg * src[exState->pixelFloatsRGB * i + 1] +
                 kb * src[exState->pixelFloatsRGB * i + 2];
    }
}

// convert gamma-corrected RGB back to linear.
static void avifSharpYUVRGBGamma2Linear(const float * src, float * dst, const avifSharpYUVReformatExtraState * exState)
{
    uint32_t w = exState->wW;
    for (uint32_t i = 0; i < w; ++i) {
        dst[exState->pixelFloatsRGB * i + 0] = avifSharpYUVToLinearLookup(src[exState->pixelFloatsRGB * i + 0], exState);
        dst[exState->pixelFloatsRGB * i + 1] = avifSharpYUVToLinearLookup(src[exState->pixelFloatsRGB * i + 1], exState);
        dst[exState->pixelFloatsRGB * i + 2] = avifSharpYUVToLinearLookup(src[exState->pixelFloatsRGB * i + 2], exState);
    }
}

// Compute luminance from RGB value, use CL method.
// This function expect linear RGB value stored in src, but produce gamma-corrected
// luminance to dst.
static void avifSharpYUVRGB2LumaCL(const float * src, float * dst, const avifSharpYUVReformatExtraState * exState)
{
    float kr = exState->kr;
    float kg = exState->kg;
    float kb = exState->kb;

    float (*fl)(float) = exState->fromLinear;

    uint32_t w = exState->wW;
    for (uint32_t i = 0; i < w; ++i) {
        dst[i] = fl(kr * src[exState->pixelFloatsRGB * i + 0] + kg * src[exState->pixelFloatsRGB * i + 1] +
                    kb * src[exState->pixelFloatsRGB * i + 2]);
    }
}

// Weight each pixels differently according to color difference with neighbor pixel.
// Gives edges more weight to preserve its appearance.
// Coefficients apply to the difference between a pixel and its neighbor:
// 0.707 1.000 0.707
// 1.000 ----- 1.000
// 0.707 1.000 0.707
// For boundary, we duplicate nearby pixel, so it goes:
// (0.707) | 1.707 0.707
// (1.000) | ----- 1.000
// (0.707) | 1.707 0.707
static void avifSharpYUVDRGBWeightFilter(const float * srcWPrev,
                                         const float * srcWCurr,
                                         const float * srcWNext,
                                         const float * srcRGBPrev,
                                         const float * srcRGBCurr,
                                         const float * srcRGBNext,
                                         float * dst,
                                         const avifSharpYUVReformatExtraState * exState)
{
    const float sqrt2_2 = 0.70710678118654752f;
    uint32_t w = exState->wW - 1;
    uint32_t pf = exState->pixelFloatsRGB;

    {
        const float dR = fabsf(srcRGBCurr[0] - srcRGBCurr[pf + 0] - srcWCurr[0] + srcWCurr[1]) +
                         (fabsf(srcRGBCurr[0] - srcRGBPrev[0] - srcWCurr[0] + srcWPrev[0]) +
                          +fabsf(srcRGBCurr[0] - srcRGBNext[0] - srcWCurr[0] + srcWNext[0])) *
                             (1.f + sqrt2_2) +
                         (fabsf(srcRGBCurr[0] - srcRGBPrev[pf + 0] - srcWCurr[0] + srcWPrev[1]) +
                          fabsf(srcRGBCurr[0] - srcRGBNext[pf + 0] - srcWCurr[0] + srcWNext[1])) *
                             sqrt2_2;

        const float dG = fabsf(srcRGBCurr[1] - srcRGBCurr[pf + 1] - srcWCurr[0] + srcWCurr[1]) +
                         (fabsf(srcRGBCurr[1] - srcRGBPrev[1] - srcWCurr[0] + srcWPrev[0]) +
                          +fabsf(srcRGBCurr[1] - srcRGBNext[1] - srcWCurr[0] + srcWNext[0])) *
                             (1.f + sqrt2_2) +
                         (fabsf(srcRGBCurr[1] - srcRGBPrev[pf + 1] - srcWCurr[0] + srcWPrev[1]) +
                          fabsf(srcRGBCurr[1] - srcRGBNext[pf + 1] - srcWCurr[0] + srcWNext[1])) *
                             sqrt2_2;

        const float dB = fabsf(srcRGBCurr[2] - srcRGBCurr[pf + 2] - srcWCurr[0] + srcWCurr[1]) +
                         (fabsf(srcRGBCurr[2] - srcRGBPrev[2] - srcWCurr[0] + srcWPrev[0]) +
                          +fabsf(srcRGBCurr[2] - srcRGBNext[2] - srcWCurr[0] + srcWNext[0])) *
                             (1.f + sqrt2_2) +
                         (fabsf(srcRGBCurr[2] - srcRGBPrev[pf + 2] - srcWCurr[0] + srcWPrev[1]) +
                          fabsf(srcRGBCurr[2] - srcRGBNext[pf + 2] - srcWCurr[0] + srcWNext[1])) *
                             sqrt2_2;

        dst[0] = exState->baseWeight + dR + dG + dB;
    }

    for (uint32_t i = 1; i < w; ++i) {
        const float dR = (fabsf(srcRGBCurr[pf * i + 0] - srcRGBPrev[pf * i + 0] - srcWCurr[i] + srcWPrev[i]) +
                          fabsf(srcRGBCurr[pf * i + 0] - srcRGBCurr[pf * (i - 1) + 0] - srcWCurr[i] + srcWCurr[i - 1]) +
                          fabsf(srcRGBCurr[pf * i + 0] - srcRGBCurr[pf * (i + 1) + 0] - srcWCurr[i] + srcWCurr[i + 1]) +
                          fabsf(srcRGBCurr[pf * i + 0] - srcRGBNext[pf * i + 0] - srcWCurr[i] + srcWNext[i])) +
                         (fabsf(srcRGBCurr[pf * i + 0] - srcRGBPrev[pf * (i - 1) + 0] - srcWCurr[i] + srcWPrev[i - 1]) +
                          fabsf(srcRGBCurr[pf * i + 0] - srcRGBPrev[pf * (i + 1) + 0] - srcWCurr[i] + srcWPrev[i + 1]) +
                          fabsf(srcRGBCurr[pf * i + 0] - srcRGBNext[pf * (i - 1) + 0] - srcWCurr[i] + srcWNext[i - 1]) +
                          fabsf(srcRGBCurr[pf * i + 0] - srcRGBNext[pf * (i + 1) + 0] - srcWCurr[i] + srcWNext[i + 1])) *
                             sqrt2_2;
        const float dG = (fabsf(srcRGBCurr[pf * i + 1] - srcRGBPrev[pf * i + 1] - srcWCurr[i] + srcWPrev[i]) +
                          fabsf(srcRGBCurr[pf * i + 1] - srcRGBCurr[pf * (i - 1) + 1] - srcWCurr[i] + srcWCurr[i - 1]) +
                          fabsf(srcRGBCurr[pf * i + 1] - srcRGBCurr[pf * (i + 1) + 1] - srcWCurr[i] + srcWCurr[i + 1]) +
                          fabsf(srcRGBCurr[pf * i + 1] - srcRGBNext[pf * i + 1] - srcWCurr[i] + srcWNext[i])) +
                         (fabsf(srcRGBCurr[pf * i + 1] - srcRGBPrev[pf * (i - 1) + 1] - srcWCurr[i] + srcWPrev[i - 1]) +
                          fabsf(srcRGBCurr[pf * i + 1] - srcRGBPrev[pf * (i + 1) + 1] - srcWCurr[i] + srcWPrev[i + 1]) +
                          fabsf(srcRGBCurr[pf * i + 1] - srcRGBNext[pf * (i - 1) + 1] - srcWCurr[i] + srcWNext[i - 1]) +
                          fabsf(srcRGBCurr[pf * i + 1] - srcRGBNext[pf * (i + 1) + 1] - srcWCurr[i] + srcWNext[i + 1])) *
                             sqrt2_2;
        const float dB = (fabsf(srcRGBCurr[pf * i + 2] - srcRGBPrev[pf * i + 2] - srcWCurr[i] + srcWPrev[i]) +
                          fabsf(srcRGBCurr[pf * i + 2] - srcRGBCurr[pf * (i - 1) + 2] - srcWCurr[i] + srcWCurr[i - 1]) +
                          fabsf(srcRGBCurr[pf * i + 2] - srcRGBCurr[pf * (i + 1) + 2] - srcWCurr[i] + srcWCurr[i + 1]) +
                          fabsf(srcRGBCurr[pf * i + 2] - srcRGBNext[pf * i + 2] - srcWCurr[i] + srcWNext[i])) +
                         (fabsf(srcRGBCurr[pf * i + 2] - srcRGBPrev[pf * (i - 1) + 2] - srcWCurr[i] + srcWPrev[i - 1]) +
                          fabsf(srcRGBCurr[pf * i + 2] - srcRGBPrev[pf * (i + 1) + 2] - srcWCurr[i] + srcWPrev[i + 1]) +
                          fabsf(srcRGBCurr[pf * i + 2] - srcRGBNext[pf * (i - 1) + 2] - srcWCurr[i] + srcWNext[i - 1]) +
                          fabsf(srcRGBCurr[pf * i + 2] - srcRGBNext[pf * (i + 1) + 2] - srcWCurr[i] + srcWNext[i + 1])) *
                             sqrt2_2;

        dst[i] = exState->baseWeight + dR + dG + dB;
    }

    {
        const float dR = fabsf(srcRGBCurr[pf * w + 0] - srcRGBCurr[pf * (w - 1) + 0] - srcWCurr[w] + srcWCurr[w - 1]) +
                         (fabsf(srcRGBCurr[pf * w + 0] - srcRGBPrev[pf * w + 0] - srcWCurr[w] + srcWPrev[w]) +
                          +fabsf(srcRGBCurr[pf * w + 0] - srcRGBNext[pf * w + 0] - srcWCurr[w] + srcWNext[w])) *
                             (1.f + sqrt2_2) +
                         (fabsf(srcRGBCurr[pf * w + 0] - srcRGBPrev[pf * (w - 1) + 0] - srcWCurr[w] + srcWPrev[w - 1]) +
                          fabsf(srcRGBCurr[pf * w + 0] - srcRGBNext[pf * (w - 1) + 0] - srcWCurr[w] + srcWNext[w - 1])) *
                             sqrt2_2;

        const float dG = fabsf(srcRGBCurr[pf * w + 1] - srcRGBCurr[pf * (w - 1) + 1] - srcWCurr[w] + srcWCurr[w - 1]) +
                         (fabsf(srcRGBCurr[pf * w + 1] - srcRGBPrev[pf * w + 1] - srcWCurr[w] + srcWPrev[w]) +
                          +fabsf(srcRGBCurr[pf * w + 1] - srcRGBNext[pf * w + 1] - srcWCurr[w] + srcWNext[w])) *
                             (1.f + sqrt2_2) +
                         (fabsf(srcRGBCurr[pf * w + 1] - srcRGBPrev[pf * (w - 1) + 1] - srcWCurr[w] + srcWPrev[w - 1]) +
                          fabsf(srcRGBCurr[pf * w + 1] - srcRGBNext[pf * (w - 1) + 1] - srcWCurr[w] + srcWNext[w - 1])) *
                             sqrt2_2;

        const float dB = fabsf(srcRGBCurr[pf * w + 2] - srcRGBCurr[pf * (w - 1) + 2] - srcWCurr[w] + srcWCurr[w - 1]) +
                         (fabsf(srcRGBCurr[pf * w + 2] - srcRGBPrev[pf * w + 2] - srcWCurr[w] + srcWPrev[w]) +
                          +fabsf(srcRGBCurr[pf * w + 2] - srcRGBNext[pf * w + 2] - srcWCurr[w] + srcWNext[w])) *
                             (1.f + sqrt2_2) +
                         (fabsf(srcRGBCurr[pf * w + 2] - srcRGBPrev[pf * (w - 1) + 2] - srcWCurr[w] + srcWPrev[w - 1]) +
                          fabsf(srcRGBCurr[pf * w + 2] - srcRGBNext[pf * (w - 1) + 2] - srcWCurr[w] + srcWNext[w - 1])) *
                             sqrt2_2;

        dst[w] = exState->baseWeight + dR + dG + dB;
    }
}

static void avifSharpYUVElementwiseMultiply(const float * src1, const float * src2, float * dst, uint32_t w)
{
    for (uint32_t i = 0; i < w; ++i) {
        dst[i] = src1[i] * src2[i];
    }
}

// Standard deviation of 4 value, normalized
static inline float avifSharpYUVStdDev4(const float a, const float b, const float c, const float d)
{
    const float sum = a + b + c + d;
    const float variance = ((a * a + b * b + c * c + d * d) - sum * sum / 4.f);
    return sqrtf(AVIF_CLAMP(variance, 0.f, 1.f));
}

// Converts two rows of RGB into one down sampled row of chroma.
static void avifSharpYUVRGB2DRGB420Weighted(const float * src1,
                                            const float * src2,
                                            const float * w1,
                                            const float * w2,
                                            float * dst,
                                            const avifSharpYUVReformatExtraState * exState)
{
    float kr = exState->kr;
    float kg = exState->kg;
    float kb = exState->kb;

    float p11;
    float p12;
    float p21;
    float p22;

    float (*fl)(float) = exState->fromLinear;

    uint32_t wDRGB = exState->wDRGB;
    for (uint32_t i = 0; i < wDRGB; ++i) {
        const float wSum = w1[2 * i] + w1[2 * i + 1] + w2[2 * i] + w2[2 * i + 1];
        // weight given by pixel difference has a base, so this can only happen if the pixels are all transparent
        // so we can safely make it anything we want
        if (wSum < (1.f / 65536.f)) {
            dst[exState->pixelFloatsDRGB * i + 0] = 0;
            dst[exState->pixelFloatsDRGB * i + 1] = 0;
            dst[exState->pixelFloatsDRGB * i + 2] = 0;
            continue;
        }

        // if pixels are different (big stddev), then we try to preserve the pixel that has bigger weight, as it's
        // visually more important (be an edge, or have higher opacity);
        // if pixels are near (small stddev), then we try to preserve the similarity between the pixels, as it's
        // probably a surface and we don't want to add artificial textures.
        p11 = src1[exState->pixelFloatsRGB * 2 * i + 0];
        p12 = src1[exState->pixelFloatsRGB * (2 * i + 1) + 0];
        p21 = src2[exState->pixelFloatsRGB * 2 * i + 0];
        p22 = src2[exState->pixelFloatsRGB * (2 * i + 1) + 0];

        const float vr = avifSharpYUVStdDev4(p11, p12, p21, p22);
        const float r = fl((w1[2 * i] * p11 + w1[2 * i + 1] * p12 + w2[2 * i] * p21 + w2[2 * i + 1] * p22) / wSum * vr +
                           (p11 + p12 + p21 + p22) / 4.f * (1.f - vr));

        p11 = src1[exState->pixelFloatsRGB * 2 * i + 1];
        p12 = src1[exState->pixelFloatsRGB * (2 * i + 1) + 1];
        p21 = src2[exState->pixelFloatsRGB * 2 * i + 1];
        p22 = src2[exState->pixelFloatsRGB * (2 * i + 1) + 1];

        const float vg = avifSharpYUVStdDev4(p11, p12, p21, p22);
        const float g = fl((w1[2 * i] * p11 + w1[2 * i + 1] * p12 + w2[2 * i] * p21 + w2[2 * i + 1] * p22) / wSum * vg +
                           (p11 + p12 + p21 + p22) / 4.f * (1.f - vg));

        p11 = src1[exState->pixelFloatsRGB * 2 * i + 2];
        p12 = src1[exState->pixelFloatsRGB * (2 * i + 1) + 2];
        p21 = src2[exState->pixelFloatsRGB * 2 * i + 2];
        p22 = src2[exState->pixelFloatsRGB * (2 * i + 1) + 2];

        const float vb = avifSharpYUVStdDev4(p11, p12, p21, p22);
        const float b = fl((w1[2 * i] * p11 + w1[2 * i + 1] * p12 + w2[2 * i] * p21 + w2[2 * i + 1] * p22) / wSum * vb +
                           (p11 + p12 + p21 + p22) / 4.f * (1.f - vb));

        const float w = kr * r + kg * g + kb * b;

        dst[exState->pixelFloatsDRGB * i + 0] = r - w;
        dst[exState->pixelFloatsDRGB * i + 1] = g - w;
        dst[exState->pixelFloatsDRGB * i + 2] = b - w;
    }
}

// 2 value version
static inline float avifSharpYUVStdDev2(const float a, const float b)
{
    const float sum = a + b;
    const float variance = 2 * (a * a + b * b) - sum * sum;
    return sqrtf(AVIF_CLAMP(variance, 0.f, 1.f));
}

// YUV422 version. One row in and one row out.
static void avifSharpYUVRGB2DRGB422Weighted(const float * src, const float * w, float * dst, const avifSharpYUVReformatExtraState * exState)
{
    float kr = exState->kr;
    float kg = exState->kg;
    float kb = exState->kb;

    float p1;
    float p2;

    float (*fl)(float) = exState->fromLinear;

    uint32_t wDRGB = exState->wDRGB;
    for (uint32_t i = 0; i < wDRGB; ++i) {
        const float aSum = w[2 * i] + w[2 * i + 1];
        if (aSum < (1.f / 65536.f)) {
            dst[exState->pixelFloatsDRGB * i + 0] = 0;
            dst[exState->pixelFloatsDRGB * i + 1] = 0;
            dst[exState->pixelFloatsDRGB * i + 2] = 0;
            continue;
        }

        p1 = src[exState->pixelFloatsRGB * 2 * i + 0];
        p2 = src[exState->pixelFloatsRGB * (2 * i + 1) + 0];

        const float vr = avifSharpYUVStdDev2(p1, p2);
        const float r = fl((w[2 * i] * p1 + w[2 * i + 1] * p2) / aSum * vr + (p1 + p2) / 2.f * (1.f - vr));

        p1 = src[exState->pixelFloatsRGB * 2 * i + 1];
        p2 = src[exState->pixelFloatsRGB * (2 * i + 1) + 1];

        const float vg = avifSharpYUVStdDev2(p1, p2);
        const float g = fl((w[2 * i] * p1 + w[2 * i + 1] * p2) / aSum * vg + (p1 + p2) / 2.f * (1.f - vg));

        p1 = src[exState->pixelFloatsRGB * 2 * i + 2];
        p2 = src[exState->pixelFloatsRGB * (2 * i + 1) + 2];

        const float vb = avifSharpYUVStdDev2(p1, p2);
        const float b = fl((w[2 * i] * p1 + w[2 * i + 1] * p2) / aSum * vb + (p1 + p2) / 2.f * (1.f - vb));

        const float white = kr * r + kg * g + kb * b;

        dst[exState->pixelFloatsDRGB * i + 0] = r - white;
        dst[exState->pixelFloatsDRGB * i + 1] = g - white;
        dst[exState->pixelFloatsDRGB * i + 2] = b - white;
    }
}

// Produce one row of RGB using one row of Luma and two nearby row of chroma.
// This is exactly what YUV->RGB with bilinear filter do.
static void avifSharpYUVBilinearFilterRow420(const float * srcW,
                                             const float * srcDRGBNear,
                                             const float * srcDRGBFar,
                                             float * dst,
                                             uint32_t wWork,
                                             const avifSharpYUVReformatExtraState * exState)
{
    for (uint32_t i = 0; i < wWork; ++i) {
        uint32_t pD1 = exState->pixelFloatsRGB * 2 * i;
        uint32_t pD2 = pD1 + exState->pixelFloatsRGB;
        uint32_t pS1 = exState->pixelFloatsDRGB * i;
        uint32_t pS2 = pS1 + exState->pixelFloatsDRGB;
        dst[pD1 + 0] = srcW[2 * i + 0] + (9.f / 16.f * srcDRGBNear[pS1 + 0] + 3.f / 16.f * srcDRGBNear[pS2 + 0] +
                                          3.f / 16.f * srcDRGBFar[pS1 + 0] + 1.f / 16.f * srcDRGBFar[pS2 + 0]);
        dst[pD1 + 1] = srcW[2 * i + 0] + (9.f / 16.f * srcDRGBNear[pS1 + 1] + 3.f / 16.f * srcDRGBNear[pS2 + 1] +
                                          3.f / 16.f * srcDRGBFar[pS1 + 1] + 1.f / 16.f * srcDRGBFar[pS2 + 1]);
        dst[pD1 + 2] = srcW[2 * i + 0] + (9.f / 16.f * srcDRGBNear[pS1 + 2] + 3.f / 16.f * srcDRGBNear[pS2 + 2] +
                                          3.f / 16.f * srcDRGBFar[pS1 + 2] + 1.f / 16.f * srcDRGBFar[pS2 + 2]);
        dst[pD2 + 0] = srcW[2 * i + 1] + (3.f / 16.f * srcDRGBNear[pS1 + 0] + 9.f / 16.f * srcDRGBNear[pS2 + 0] +
                                          1.f / 16.f * srcDRGBFar[pS1 + 0] + 3.f / 16.f * srcDRGBFar[pS2 + 0]);
        dst[pD2 + 1] = srcW[2 * i + 1] + (3.f / 16.f * srcDRGBNear[pS1 + 1] + 9.f / 16.f * srcDRGBNear[pS2 + 1] +
                                          1.f / 16.f * srcDRGBFar[pS1 + 1] + 3.f / 16.f * srcDRGBFar[pS2 + 1]);
        dst[pD2 + 2] = srcW[2 * i + 1] + (3.f / 16.f * srcDRGBNear[pS1 + 2] + 9.f / 16.f * srcDRGBNear[pS2 + 2] +
                                          1.f / 16.f * srcDRGBFar[pS1 + 2] + 3.f / 16.f * srcDRGBFar[pS2 + 2]);
    }
}

// YUV422 version. we have a dedicate chroma row.
static void avifSharpYUVBilinearFilterRow422(const float * srcW,
                                             const float * srcDRGB,
                                             float * dst,
                                             uint32_t wWork,
                                             const avifSharpYUVReformatExtraState * exState)
{
    for (uint32_t i = 0; i < wWork; ++i) {
        uint32_t pD1 = exState->pixelFloatsRGB * 2 * i;
        uint32_t pD2 = pD1 + exState->pixelFloatsRGB;
        uint32_t pS1 = exState->pixelFloatsDRGB * i;
        uint32_t pS2 = pS1 + exState->pixelFloatsDRGB;
        dst[pD1 + 0] = srcW[2 * i + 0] + (3.f / 4.f * srcDRGB[pS1 + 0] + 1.f / 4.f * srcDRGB[pS2 + 0]);
        dst[pD1 + 1] = srcW[2 * i + 0] + (3.f / 4.f * srcDRGB[pS1 + 1] + 1.f / 4.f * srcDRGB[pS2 + 1]);
        dst[pD1 + 2] = srcW[2 * i + 0] + (3.f / 4.f * srcDRGB[pS1 + 2] + 1.f / 4.f * srcDRGB[pS2 + 2]);
        dst[pD2 + 0] = srcW[2 * i + 1] + (1.f / 4.f * srcDRGB[pS1 + 0] + 3.f / 4.f * srcDRGB[pS2 + 0]);
        dst[pD2 + 1] = srcW[2 * i + 1] + (1.f / 4.f * srcDRGB[pS1 + 1] + 3.f / 4.f * srcDRGB[pS2 + 1]);
        dst[pD2 + 2] = srcW[2 * i + 1] + (1.f / 4.f * srcDRGB[pS1 + 2] + 3.f / 4.f * srcDRGB[pS2 + 2]);
    }
}

// convert YUV420 back to RGB, two rows per call.
static void avifSharpYUVWRGB4202RGB(const float * srcW1,
                                    const float * srcW2,
                                    const float * srcDRGBPrev,
                                    const float * srcDRGBCurr,
                                    const float * srcDRGBNext,
                                    float * dst1,
                                    float * dst2,
                                    const avifSharpYUVReformatExtraState * exState)
{
    const uint32_t wFilter = exState->wDRGB - 1;

    dst1[0] = srcW1[0] + (3.f / 4.f * srcDRGBCurr[0] + 1.f / 4.f * srcDRGBPrev[0]);
    dst1[1] = srcW1[0] + (3.f / 4.f * srcDRGBCurr[1] + 1.f / 4.f * srcDRGBPrev[1]);
    dst1[2] = srcW1[0] + (3.f / 4.f * srcDRGBCurr[2] + 1.f / 4.f * srcDRGBPrev[2]);

    dst2[0] = srcW2[0] + (3.f / 4.f * srcDRGBCurr[0] + 1.f / 4.f * srcDRGBNext[0]);
    dst2[1] = srcW2[0] + (3.f / 4.f * srcDRGBCurr[1] + 1.f / 4.f * srcDRGBNext[1]);
    dst2[2] = srcW2[0] + (3.f / 4.f * srcDRGBCurr[2] + 1.f / 4.f * srcDRGBNext[2]);

    avifSharpYUVBilinearFilterRow420(&srcW1[1], srcDRGBCurr, srcDRGBPrev, dst1 + exState->pixelFloatsRGB, wFilter, exState);
    avifSharpYUVBilinearFilterRow420(&srcW2[1], srcDRGBCurr, srcDRGBNext, dst2 + exState->pixelFloatsRGB, wFilter, exState);

    dst1[exState->rowFloatsRGB - exState->pixelFloatsRGB + 0] =
        srcW1[exState->rowFloatsW - 1] + (3.f / 4.f * srcDRGBCurr[exState->rowFloatsDRGB - exState->pixelFloatsDRGB + 0] +
                                          1.f / 4.f * srcDRGBNext[exState->rowFloatsDRGB - exState->pixelFloatsDRGB + 0]);
    dst1[exState->rowFloatsRGB - exState->pixelFloatsRGB + 1] =
        srcW1[exState->rowFloatsW - 1] + (3.f / 4.f * srcDRGBCurr[exState->rowFloatsDRGB - exState->pixelFloatsDRGB + 1] +
                                          1.f / 4.f * srcDRGBNext[exState->rowFloatsDRGB - exState->pixelFloatsDRGB + 1]);
    dst1[exState->rowFloatsRGB - exState->pixelFloatsRGB + 2] =
        srcW1[exState->rowFloatsW - 1] + (3.f / 4.f * srcDRGBCurr[exState->rowFloatsDRGB - exState->pixelFloatsDRGB + 2] +
                                          1.f / 4.f * srcDRGBNext[exState->rowFloatsDRGB - exState->pixelFloatsDRGB + 2]);

    dst2[exState->rowFloatsRGB - exState->pixelFloatsRGB + 0] =
        srcW2[exState->rowFloatsW - 1] + (3.f / 4.f * srcDRGBCurr[exState->rowFloatsDRGB - exState->pixelFloatsDRGB + 0] +
                                          1.f / 4.f * srcDRGBNext[exState->rowFloatsDRGB - exState->pixelFloatsDRGB + 0]);
    dst2[exState->rowFloatsRGB - exState->pixelFloatsRGB + 1] =
        srcW2[exState->rowFloatsW - 1] + (3.f / 4.f * srcDRGBCurr[exState->rowFloatsDRGB - exState->pixelFloatsDRGB + 1] +
                                          1.f / 4.f * srcDRGBNext[exState->rowFloatsDRGB - exState->pixelFloatsDRGB + 1]);
    dst2[exState->rowFloatsRGB - exState->pixelFloatsRGB + 2] =
        srcW2[exState->rowFloatsW - 1] + (3.f / 4.f * srcDRGBCurr[exState->rowFloatsDRGB - exState->pixelFloatsDRGB + 2] +
                                          1.f / 4.f * srcDRGBNext[exState->rowFloatsDRGB - exState->pixelFloatsDRGB + 2]);
}

// YUV422 version. One row per call.
static void avifSharpYUVWRGB4222RGB(const float * srcW, const float * srcDRGB, float * dst, const avifSharpYUVReformatExtraState * exState)
{
    const uint32_t wFilter = (exState->wW - 1) >> 1;

    dst[0] = srcW[0] + srcDRGB[0];
    dst[1] = srcW[0] + srcDRGB[1];
    dst[2] = srcW[0] + srcDRGB[2];

    avifSharpYUVBilinearFilterRow422(&srcW[1], srcDRGB, dst + exState->pixelFloatsRGB, wFilter, exState);

    dst[exState->rowFloatsRGB - exState->pixelFloatsRGB + 0] =
        srcW[exState->rowFloatsW - 1] + srcDRGB[exState->rowFloatsDRGB - exState->pixelFloatsDRGB + 0];
    dst[exState->rowFloatsRGB - exState->pixelFloatsRGB + 1] =
        srcW[exState->rowFloatsW - 1] + srcDRGB[exState->rowFloatsDRGB - exState->pixelFloatsDRGB + 1];
    dst[exState->rowFloatsRGB - exState->pixelFloatsRGB + 2] =
        srcW[exState->rowFloatsW - 1] + srcDRGB[exState->rowFloatsDRGB - exState->pixelFloatsDRGB + 2];
}

// Update according to the value shift of the YUV->RGB->YUV, to approach the best value.
static double avifSharpYUVUpdateW(const float * ref, const float * src, float * dst, const avifSharpYUVReformatExtraState * exState)
{
    double acc = 0;
    for (uint32_t i = 0; i < exState->wW; ++i) {
        const float diff = ref[i] - src[i];
        dst[i] += diff;
        acc += (double)fabsf(diff);
    }

    return acc;
}

// same as above, but on chroma
static void avifSharpYUVUpdateDRGB(const float * ref, const float * src, float * dst, const avifSharpYUVReformatExtraState * exState)
{
    for (uint32_t i = 0; i < exState->wDRGB; ++i) {
        dst[exState->pixelFloatsDRGB * i + 0] += (ref[exState->pixelFloatsDRGB * i + 0] - src[exState->pixelFloatsDRGB * i + 0]);
        dst[exState->pixelFloatsDRGB * i + 1] += (ref[exState->pixelFloatsDRGB * i + 1] - src[exState->pixelFloatsDRGB * i + 1]);
        dst[exState->pixelFloatsDRGB * i + 2] += (ref[exState->pixelFloatsDRGB * i + 2] - src[exState->pixelFloatsDRGB * i + 2]);
    }
}

// Convert our White-DiffRGB intermediate representation into actual Y.
static void avifSharpYUVExportYFromWRGB(const float * srcW,
                                        const float * srcDRGB,
                                        uint8_t * dstY,
                                        const avifReformatState * state,
                                        const avifSharpYUVReformatExtraState * exState)
{
    for (uint32_t i = 0; i < exState->yW; ++i) {
        const uint32_t pS = exState->pixelFloatsDRGB * (i >> 1);

        const float w = srcW[i];
        const float r = srcDRGB[pS + 0] + w;
        const float g = srcDRGB[pS + 1] + w;
        const float b = srcDRGB[pS + 2] + w;
        const float y = state->kr * r + state->kg * g + state->kb * b;

        int yNorm = (int)avifRoundf(y * state->yuvMaxChannelF);
        yNorm = AVIF_CLAMP(yNorm, 0, state->yuvMaxChannel);

        if (state->yuvRange == AVIF_RANGE_LIMITED) {
            yNorm = avifFullToLimitedY(state->yuvDepth, yNorm);
        }

        if (state->yuvDepth > 8) {
            *(uint16_t *)&dstY[i * 2] = (uint16_t)yNorm;
        } else {
            dstY[i] = (uint8_t)yNorm;
        }
    }
}

// Convert our DiffRGB intermediate representation into actual UV.
// Note that we don't need White.
static void avifSharpYUVExportUVFromDRGB(const float * srcDRGB,
                                         uint8_t * dstU,
                                         uint8_t * dstV,
                                         const avifReformatState * state,
                                         const avifSharpYUVReformatExtraState * exState)
{
    const float krU = -state->kr / (2 * (1 - state->kb));
    const float kgU = -state->kg / (2 * (1 - state->kb));
    const float kbU = 1.f / 2.f;

    const float krV = 1.f / 2.f;
    const float kgV = -state->kg / (2 * (1 - state->kr));
    const float kbV = -state->kb / (2 * (1 - state->kr));

    for (uint32_t i = 0; i < exState->uvW; ++i) {
        const float r = srcDRGB[exState->pixelFloatsDRGB * i + 0];
        const float g = srcDRGB[exState->pixelFloatsDRGB * i + 1];
        const float b = srcDRGB[exState->pixelFloatsDRGB * i + 2];
        const float u = krU * r + kgU * g + kbU * b;
        const float v = krV * r + kgV * g + kbV * b;

        int uNorm = (int)avifRoundf(u * state->yuvMaxChannelF) + state->uvBias;
        int vNorm = (int)avifRoundf(v * state->yuvMaxChannelF) + state->uvBias;
        uNorm = AVIF_CLAMP(uNorm, 0, state->yuvMaxChannel);
        vNorm = AVIF_CLAMP(vNorm, 0, state->yuvMaxChannel);

        if (state->yuvRange == AVIF_RANGE_LIMITED) {
            uNorm = avifFullToLimitedUV(state->yuvDepth, uNorm);
            vNorm = avifFullToLimitedUV(state->yuvDepth, vNorm);
        }

        if (state->yuvDepth > 8) {
            *(uint16_t *)&dstU[i * 2] = (uint16_t)uNorm;
            *(uint16_t *)&dstV[i * 2] = (uint16_t)vNorm;
        } else {
            dstU[i] = (uint8_t)uNorm;
            dstV[i] = (uint8_t)vNorm;
        }
    }
}

avifResult avifImageRGBtoYUVSharp(avifImage * image, const avifRGBImage * rgb, avifReformatState * state)
{
    // sharp yuv doesn't have improvement on these cases
    if (image->yuvFormat == AVIF_PIXEL_FORMAT_YUV444 || image->yuvFormat == AVIF_PIXEL_FORMAT_YUV400 ||
        image->matrixCoefficients == AVIF_MATRIX_COEFFICIENTS_IDENTITY ||
        image->matrixCoefficients == AVIF_MATRIX_COEFFICIENTS_YCGCO || image->width < 4 || image->height < 4) {
        return AVIF_RESULT_INVALID_ARGUMENT;
    }

    avifSharpYUVReformatExtraState exState;
    if (avifPrepareSharpYUVReformatExtraState(image, rgb, state, &exState) != AVIF_RESULT_OK) {
        return AVIF_RESULT_REFORMAT_FAILED;
    }
    if (image->alphaPlane && image->alphaRowBytes) {
        avifAlphaParams params;

        params.width = image->width;
        params.height = image->height;
        params.dstDepth = image->depth;
        params.dstRange = image->alphaRange;
        params.dstPlane = image->alphaPlane;
        params.dstRowBytes = image->alphaRowBytes;
        params.dstOffsetBytes = 0;
        params.dstPixelBytes = state->yuvChannelBytes;

        if (exState.shouldAlphaWeighted) {
            params.srcDepth = rgb->depth;
            params.srcRange = AVIF_RANGE_FULL;
            params.srcPlane = rgb->pixels;
            params.srcRowBytes = rgb->rowBytes;
            params.srcOffsetBytes = state->rgbOffsetBytesA;
            params.srcPixelBytes = state->rgbPixelBytes;

            avifReformatAlpha(&params);
        } else {
            avifFillAlpha(&params);
        }
    }

    float * line1 = exState.tmpRGB;
    float * line2 = exState.tmpRGB + exState.rowFloatsRGB;
    float * line3 = exState.tmpRGB + 2 * exState.rowFloatsRGB;

    float * pResultW = exState.resultW;
    float * pResultDRGB = exState.resultDRGB;

    float * pTargetW = exState.targetW;
    float * pTargetDRGB = exState.targetDRGB;

    float * pWeight = exState.Weight;

    uint8_t * pSource = rgb->pixels;
    uint32_t sourceWidth = rgb->width;

    float * srcRGBPrev = line1;
    float * srcRGBCurr = line1;
    float * srcRGBNext = line2;

    float * srcWPrev = pTargetW;
    float * srcWCurr = pTargetW;
    float * srcWNext;

    avifSharpYUVImportRGBRow(pSource, srcRGBCurr, sourceWidth, state, &exState);
    avifSharpYUVRGB2LumaNCL(srcRGBCurr, pResultW, &exState);
    avifSharpYUVRGBGamma2Linear(srcRGBCurr, srcRGBCurr, &exState);
    avifSharpYUVRGB2LumaCL(srcRGBCurr, pTargetW, &exState);

    for (uint32_t h = 0; h < rgb->height; ++h) {
        if (rgb->height - 1 != h) {
            pSource += rgb->rowBytes;
            pResultW += exState.rowFloatsW;
            pTargetW += exState.rowFloatsW;

            avifSharpYUVImportRGBRow(pSource, srcRGBNext, sourceWidth, state, &exState);
            avifSharpYUVRGB2LumaNCL(srcRGBNext, pResultW, &exState);
            avifSharpYUVRGBGamma2Linear(srcRGBNext, srcRGBNext, &exState);
            avifSharpYUVRGB2LumaCL(srcRGBNext, pTargetW, &exState);

            srcWNext = pTargetW;
        } else {
            srcRGBNext = srcRGBCurr;
            srcWNext = srcWCurr;
        }

        avifSharpYUVDRGBWeightFilter(srcWPrev, srcWCurr, srcWNext, srcRGBPrev, srcRGBCurr, srcRGBNext, pWeight, &exState);

        if (exState.shouldAlphaWeighted) {
            avifSharpYUVImportAlphaRow(pSource - rgb->rowBytes, exState.tmpA, sourceWidth, state);
            avifSharpYUVElementwiseMultiply(pWeight, exState.tmpA, pWeight, exState.rowFloatsWeight);
        }

        if (image->yuvFormat == AVIF_PIXEL_FORMAT_YUV422) {
            avifSharpYUVRGB2DRGB422Weighted(srcRGBCurr, pWeight, pTargetDRGB, &exState);
            memcpy(pResultDRGB, pTargetDRGB, exState.rowFloatsDRGB * exState.floatSize);

            pResultDRGB += exState.rowFloatsDRGB;
            pTargetDRGB += exState.rowFloatsDRGB;
        } else if (image->yuvFormat == AVIF_PIXEL_FORMAT_YUV420 && ((h & 1) == 1)) {
            avifSharpYUVRGB2DRGB420Weighted(srcRGBPrev, srcRGBCurr, pWeight - exState.rowFloatsWeight, pWeight, pTargetDRGB, &exState);
            memcpy(pResultDRGB, pTargetDRGB, exState.rowFloatsDRGB * exState.floatSize);

            pResultDRGB += exState.rowFloatsDRGB;
            pTargetDRGB += exState.rowFloatsDRGB;
        }

        if (h == 0) {
            srcRGBPrev = line3;
        }

        float * tmp = srcRGBNext;
        srcRGBNext = srcRGBPrev;
        srcRGBPrev = srcRGBCurr;
        srcRGBCurr = tmp;

        srcWPrev = srcWCurr;
        srcWCurr = srcWNext;

        pWeight += exState.rowFloatsWeight;
    }

    if (image->yuvFormat == AVIF_PIXEL_FORMAT_YUV420 && ((rgb->height & 1) == 1)) {
        // padding and do YUV420 is the same as do YUV422 on first line
        avifSharpYUVRGB2DRGB422Weighted(srcRGBCurr, pWeight - exState.rowFloatsWeight, pTargetDRGB, &exState);
        memcpy(pResultDRGB, pTargetDRGB, exState.rowFloatsDRGB * exState.floatSize);
    }

    for (uint32_t iter = 0; iter < avifSharpYUVIterLimit; ++iter) {
        double diffAcc = 0.;

        float * DRGBPrev = exState.resultDRGB;
        float * DRGBCurr = exState.resultDRGB;
        float * DRGBNext;

        pResultW = exState.resultW;
        pResultDRGB = exState.resultDRGB;

        pTargetW = exState.targetW;
        pTargetDRGB = exState.targetDRGB;

        pWeight = exState.Weight;

        if (image->yuvFormat == AVIF_PIXEL_FORMAT_YUV420) {
            for (uint32_t h = 0; h < exState.hW; h += 2) {
                DRGBNext = DRGBCurr + ((h < exState.hW - 2) ? exState.rowFloatsDRGB : 0);
                avifSharpYUVWRGB4202RGB(pResultW, pResultW + exState.rowFloatsW, DRGBPrev, DRGBCurr, DRGBNext, line1, line2, &exState);

                DRGBPrev = DRGBCurr;
                DRGBCurr = DRGBNext;

                avifSharpYUVRGBGamma2Linear(line1, line1, &exState);
                avifSharpYUVRGBGamma2Linear(line2, line2, &exState);

                avifSharpYUVRGB2LumaCL(line1, exState.measureW, &exState);
                avifSharpYUVRGB2LumaCL(line2, exState.measureW + exState.rowFloatsW, &exState);
                avifSharpYUVRGB2DRGB420Weighted(line1, line2, pWeight, pWeight + exState.rowFloatsWeight, exState.measureDRGB, &exState);

                diffAcc += avifSharpYUVUpdateW(pTargetW, exState.measureW, pResultW, &exState);
                diffAcc += avifSharpYUVUpdateW(
                    pTargetW + exState.rowFloatsW, exState.measureW + exState.rowFloatsW, pResultW + exState.rowFloatsW, &exState);
                avifSharpYUVUpdateDRGB(pTargetDRGB, exState.measureDRGB, pResultDRGB, &exState);

                pResultW += 2 * exState.rowFloatsW;
                pResultDRGB += exState.rowFloatsDRGB;

                pTargetW += 2 * exState.rowFloatsW;
                pTargetDRGB += exState.rowFloatsDRGB;

                pWeight += 2 * exState.rowFloatsWeight;
            }
        } else if (image->yuvFormat == AVIF_PIXEL_FORMAT_YUV422) {
            for (uint32_t h = 0; h < exState.hW; ++h) {
                avifSharpYUVWRGB4222RGB(pResultW, pResultDRGB, line1, &exState);

                avifSharpYUVRGBGamma2Linear(line1, line1, &exState);

                avifSharpYUVRGB2LumaCL(line1, exState.measureW, &exState);
                avifSharpYUVRGB2DRGB422Weighted(line1, pWeight, exState.measureDRGB, &exState);

                diffAcc += avifSharpYUVUpdateW(pTargetW, exState.measureW, pResultW, &exState);
                avifSharpYUVUpdateDRGB(pTargetDRGB, exState.measureDRGB, pResultDRGB, &exState);

                pResultW += exState.rowFloatsW;
                pResultDRGB += exState.rowFloatsDRGB;

                pTargetW += exState.rowFloatsW;
                pTargetDRGB += exState.rowFloatsDRGB;

                pWeight += exState.rowFloatsWeight;
            }
        }

        if (iter == 0) {
            continue;
        }

        if (diffAcc < exState.wDiffThreshold) {
            break;
        }

        if (diffAcc > exState.wDiffPrevious) {
            break;
        }
    }

    for (uint32_t h = 0; h < exState.yH; ++h) {
        const uint32_t uvLine = image->yuvFormat == AVIF_PIXEL_FORMAT_YUV420 ? h >> 1 : h;

        avifSharpYUVExportYFromWRGB(&exState.resultW[exState.rowFloatsW * h],
                                    &exState.resultDRGB[exState.rowFloatsDRGB * uvLine],
                                    &image->yuvPlanes[AVIF_CHAN_Y][image->yuvRowBytes[AVIF_CHAN_Y] * h],
                                    state,
                                    &exState);
    }

    for (uint32_t h = 0; h < exState.uvH; ++h) {
        avifSharpYUVExportUVFromDRGB(&exState.resultDRGB[exState.rowFloatsDRGB * h],
                                     &image->yuvPlanes[AVIF_CHAN_U][image->yuvRowBytes[AVIF_CHAN_U] * h],
                                     &image->yuvPlanes[AVIF_CHAN_V][image->yuvRowBytes[AVIF_CHAN_V] * h],
                                     state,
                                     &exState);
    }

    avifReleaseSharpYUVReformatExtraState(&exState);
    return AVIF_RESULT_OK;
}
