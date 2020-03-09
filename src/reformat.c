// Copyright 2019 Joe Drago. All rights reserved.
// SPDX-License-Identifier: BSD-2-Clause

#include "avif/internal.h"

#include <string.h>

struct YUVBlock
{
    float y;
    float u;
    float v;
};

avifBool avifPrepareReformatState(avifImage * image, avifRGBImage * rgb, avifReformatState * state)
{
    if ((image->depth != 8) && (image->depth != 10) && (image->depth != 12)) {
        return AVIF_FALSE;
    }
    if ((rgb->depth != 8) && (rgb->depth != 10) && (rgb->depth != 12) && (rgb->depth != 16)) {
        return AVIF_FALSE;
    }

    if (image->yuvFormat == AVIF_PIXEL_FORMAT_NONE) {
        return AVIF_FALSE;
    }
    avifGetPixelFormatInfo(image->yuvFormat, &state->formatInfo);
    avifCalcYUVCoefficients(image, &state->kr, &state->kg, &state->kb);

    state->yuvChannelBytes = (image->depth > 8) ? 2 : 1;
    state->rgbChannelBytes = (rgb->depth > 8) ? 2 : 1;
    state->rgbChannelCount = avifRGBFormatChannelCount(rgb->format);
    state->rgbPixelBytes = state->rgbChannelBytes * state->rgbChannelCount;

    switch (rgb->format) {
        case AVIF_RGB_FORMAT_RGB:
            state->rgbOffsetBytesR = state->rgbChannelBytes * 0;
            state->rgbOffsetBytesG = state->rgbChannelBytes * 1;
            state->rgbOffsetBytesB = state->rgbChannelBytes * 2;
            state->rgbOffsetBytesA = 0;
            break;
        case AVIF_RGB_FORMAT_RGBA:
            state->rgbOffsetBytesR = state->rgbChannelBytes * 0;
            state->rgbOffsetBytesG = state->rgbChannelBytes * 1;
            state->rgbOffsetBytesB = state->rgbChannelBytes * 2;
            state->rgbOffsetBytesA = state->rgbChannelBytes * 3;
            break;
        case AVIF_RGB_FORMAT_ARGB:
            state->rgbOffsetBytesA = state->rgbChannelBytes * 0;
            state->rgbOffsetBytesR = state->rgbChannelBytes * 1;
            state->rgbOffsetBytesG = state->rgbChannelBytes * 2;
            state->rgbOffsetBytesB = state->rgbChannelBytes * 3;
            break;
        case AVIF_RGB_FORMAT_BGR:
            state->rgbOffsetBytesB = state->rgbChannelBytes * 0;
            state->rgbOffsetBytesG = state->rgbChannelBytes * 1;
            state->rgbOffsetBytesR = state->rgbChannelBytes * 2;
            state->rgbOffsetBytesA = 0;
            break;
        case AVIF_RGB_FORMAT_BGRA:
            state->rgbOffsetBytesB = state->rgbChannelBytes * 0;
            state->rgbOffsetBytesG = state->rgbChannelBytes * 1;
            state->rgbOffsetBytesR = state->rgbChannelBytes * 2;
            state->rgbOffsetBytesA = state->rgbChannelBytes * 3;
            break;
        case AVIF_RGB_FORMAT_ABGR:
            state->rgbOffsetBytesA = state->rgbChannelBytes * 0;
            state->rgbOffsetBytesB = state->rgbChannelBytes * 1;
            state->rgbOffsetBytesG = state->rgbChannelBytes * 2;
            state->rgbOffsetBytesR = state->rgbChannelBytes * 3;
            break;

        default:
            return AVIF_FALSE;
    }
    return AVIF_TRUE;
}

static int yuvToUNorm(int chan, avifRange range, int depth, float maxChannel, float v)
{
    if (chan != AVIF_CHAN_Y) {
        v += 0.5f;
    }
    v = AVIF_CLAMP(v, 0.0f, 1.0f);
    int unorm = (int)avifRoundf(v * maxChannel);
    if (range == AVIF_RANGE_LIMITED) {
        if (chan == AVIF_CHAN_Y) {
            unorm = avifFullToLimitedY(depth, unorm);
        } else {
            unorm = avifFullToLimitedUV(depth, unorm);
        }
    }
    return unorm;
}

avifResult avifImageRGBToYUV(avifImage * image, avifRGBImage * rgb)
{
    if (!rgb->pixels) {
        return AVIF_RESULT_REFORMAT_FAILED;
    }

    avifReformatState state;
    if (!avifPrepareReformatState(image, rgb, &state)) {
        return AVIF_RESULT_REFORMAT_FAILED;
    }

    avifImageAllocatePlanes(image, AVIF_PLANES_YUV);
    if (avifRGBFormatHasAlpha(rgb->format)) {
        avifImageAllocatePlanes(image, AVIF_PLANES_A);
    }

    const float kr = state.kr;
    const float kg = state.kg;
    const float kb = state.kb;

    struct YUVBlock yuvBlock[2][2];
    float rgbPixel[3];
    float yuvMaxChannel = (float)((1 << image->depth) - 1);
    float rgbMaxChannel = (float)((1 << rgb->depth) - 1);
    uint8_t ** yuvPlanes = image->yuvPlanes;
    uint32_t * yuvRowBytes = image->yuvRowBytes;
    for (uint32_t outerJ = 0; outerJ < image->height; outerJ += 2) {
        for (uint32_t outerI = 0; outerI < image->width; outerI += 2) {
            int blockW = 2, blockH = 2;
            if ((outerI + 1) >= image->width) {
                blockW = 1;
            }
            if ((outerJ + 1) >= image->height) {
                blockH = 1;
            }

            // Convert an entire 2x2 block to YUV, and populate any fully sampled channels as we go
            for (int bJ = 0; bJ < blockH; ++bJ) {
                for (int bI = 0; bI < blockW; ++bI) {
                    int i = outerI + bI;
                    int j = outerJ + bJ;

                    // Unpack RGB into normalized float
                    if (state.rgbChannelBytes > 1) {
                        rgbPixel[0] =
                            *((uint16_t *)(&rgb->pixels[state.rgbOffsetBytesR + (i * state.rgbPixelBytes) + (j * rgb->rowBytes)])) /
                            rgbMaxChannel;
                        rgbPixel[1] =
                            *((uint16_t *)(&rgb->pixels[state.rgbOffsetBytesG + (i * state.rgbPixelBytes) + (j * rgb->rowBytes)])) /
                            rgbMaxChannel;
                        rgbPixel[2] =
                            *((uint16_t *)(&rgb->pixels[state.rgbOffsetBytesB + (i * state.rgbPixelBytes) + (j * rgb->rowBytes)])) /
                            rgbMaxChannel;
                    } else {
                        rgbPixel[0] = rgb->pixels[state.rgbOffsetBytesR + (i * state.rgbPixelBytes) + (j * rgb->rowBytes)] / rgbMaxChannel;
                        rgbPixel[1] = rgb->pixels[state.rgbOffsetBytesG + (i * state.rgbPixelBytes) + (j * rgb->rowBytes)] / rgbMaxChannel;
                        rgbPixel[2] = rgb->pixels[state.rgbOffsetBytesB + (i * state.rgbPixelBytes) + (j * rgb->rowBytes)] / rgbMaxChannel;
                    }

                    // RGB -> YUV conversion
                    float Y = (kr * rgbPixel[0]) + (kg * rgbPixel[1]) + (kb * rgbPixel[2]);
                    yuvBlock[bI][bJ].y = Y;
                    yuvBlock[bI][bJ].u = (rgbPixel[2] - Y) / (2 * (1 - kb));
                    yuvBlock[bI][bJ].v = (rgbPixel[0] - Y) / (2 * (1 - kr));

                    if (state.yuvChannelBytes > 1) {
                        uint16_t * pY = (uint16_t *)&yuvPlanes[AVIF_CHAN_Y][(i * 2) + (j * yuvRowBytes[AVIF_CHAN_Y])];
                        *pY = (uint16_t)yuvToUNorm(AVIF_CHAN_Y, image->yuvRange, image->depth, yuvMaxChannel, yuvBlock[bI][bJ].y);
                        if (!state.formatInfo.chromaShiftX && !state.formatInfo.chromaShiftY) {
                            // YUV444, full chroma
                            uint16_t * pU = (uint16_t *)&yuvPlanes[AVIF_CHAN_U][(i * 2) + (j * yuvRowBytes[AVIF_CHAN_U])];
                            *pU = (uint16_t)yuvToUNorm(AVIF_CHAN_U, image->yuvRange, image->depth, yuvMaxChannel, yuvBlock[bI][bJ].u);
                            uint16_t * pV = (uint16_t *)&yuvPlanes[AVIF_CHAN_V][(i * 2) + (j * yuvRowBytes[AVIF_CHAN_V])];
                            *pV = (uint16_t)yuvToUNorm(AVIF_CHAN_V, image->yuvRange, image->depth, yuvMaxChannel, yuvBlock[bI][bJ].v);
                        }
                    } else {
                        yuvPlanes[AVIF_CHAN_Y][i + (j * yuvRowBytes[AVIF_CHAN_Y])] =
                            (uint8_t)yuvToUNorm(AVIF_CHAN_Y, image->yuvRange, image->depth, yuvMaxChannel, yuvBlock[bI][bJ].y);
                        if (!state.formatInfo.chromaShiftX && !state.formatInfo.chromaShiftY) {
                            // YUV444, full chroma
                            yuvPlanes[AVIF_CHAN_U][i + (j * yuvRowBytes[AVIF_CHAN_U])] =
                                (uint8_t)yuvToUNorm(AVIF_CHAN_U, image->yuvRange, image->depth, yuvMaxChannel, yuvBlock[bI][bJ].u);
                            yuvPlanes[AVIF_CHAN_V][i + (j * yuvRowBytes[AVIF_CHAN_V])] =
                                (uint8_t)yuvToUNorm(AVIF_CHAN_V, image->yuvRange, image->depth, yuvMaxChannel, yuvBlock[bI][bJ].v);
                        }
                    }
                }
            }

            // Populate any subsampled channels with averages from the 2x2 block
            if (state.formatInfo.chromaShiftX && state.formatInfo.chromaShiftY) {
                // YUV420, average 4 samples (2x2)

                float sumU = 0.0f;
                float sumV = 0.0f;
                for (int bJ = 0; bJ < blockH; ++bJ) {
                    for (int bI = 0; bI < blockW; ++bI) {
                        sumU += yuvBlock[bI][bJ].u;
                        sumV += yuvBlock[bI][bJ].v;
                    }
                }
                float totalSamples = (float)(blockW * blockH);
                float avgU = sumU / totalSamples;
                float avgV = sumV / totalSamples;

                int uvI = outerI >> state.formatInfo.chromaShiftX;
                int uvJ = outerJ >> state.formatInfo.chromaShiftY;
                if (state.yuvChannelBytes > 1) {
                    uint16_t * pU = (uint16_t *)&yuvPlanes[AVIF_CHAN_U][(uvI * 2) + (uvJ * yuvRowBytes[AVIF_CHAN_U])];
                    *pU = (uint16_t)yuvToUNorm(AVIF_CHAN_U, image->yuvRange, image->depth, yuvMaxChannel, avgU);
                    uint16_t * pV = (uint16_t *)&yuvPlanes[AVIF_CHAN_V][(uvI * 2) + (uvJ * yuvRowBytes[AVIF_CHAN_V])];
                    *pV = (uint16_t)yuvToUNorm(AVIF_CHAN_V, image->yuvRange, image->depth, yuvMaxChannel, avgV);
                } else {
                    yuvPlanes[AVIF_CHAN_U][uvI + (uvJ * yuvRowBytes[AVIF_CHAN_U])] =
                        (uint8_t)yuvToUNorm(AVIF_CHAN_U, image->yuvRange, image->depth, yuvMaxChannel, avgU);
                    yuvPlanes[AVIF_CHAN_V][uvI + (uvJ * yuvRowBytes[AVIF_CHAN_V])] =
                        (uint8_t)yuvToUNorm(AVIF_CHAN_V, image->yuvRange, image->depth, yuvMaxChannel, avgV);
                }
            } else if (state.formatInfo.chromaShiftX && !state.formatInfo.chromaShiftY) {
                // YUV422, average 2 samples (1x2), twice

                for (int bJ = 0; bJ < blockH; ++bJ) {
                    float sumU = 0.0f;
                    float sumV = 0.0f;
                    for (int bI = 0; bI < blockW; ++bI) {
                        sumU += yuvBlock[bI][bJ].u;
                        sumV += yuvBlock[bI][bJ].v;
                    }
                    float totalSamples = (float)blockW;
                    float avgU = sumU / totalSamples;
                    float avgV = sumV / totalSamples;

                    int uvI = outerI >> state.formatInfo.chromaShiftX;
                    int uvJ = outerJ + bJ;
                    if (state.yuvChannelBytes > 1) {
                        uint16_t * pU = (uint16_t *)&yuvPlanes[AVIF_CHAN_U][(uvI * 2) + (uvJ * yuvRowBytes[AVIF_CHAN_U])];
                        *pU = (uint16_t)yuvToUNorm(AVIF_CHAN_U, image->yuvRange, image->depth, yuvMaxChannel, avgU);
                        uint16_t * pV = (uint16_t *)&yuvPlanes[AVIF_CHAN_V][(uvI * 2) + (uvJ * yuvRowBytes[AVIF_CHAN_V])];
                        *pV = (uint16_t)yuvToUNorm(AVIF_CHAN_V, image->yuvRange, image->depth, yuvMaxChannel, avgV);
                    } else {
                        yuvPlanes[AVIF_CHAN_U][uvI + (uvJ * yuvRowBytes[AVIF_CHAN_U])] =
                            (uint8_t)yuvToUNorm(AVIF_CHAN_U, image->yuvRange, image->depth, yuvMaxChannel, avgU);
                        yuvPlanes[AVIF_CHAN_V][uvI + (uvJ * yuvRowBytes[AVIF_CHAN_V])] =
                            (uint8_t)yuvToUNorm(AVIF_CHAN_V, image->yuvRange, image->depth, yuvMaxChannel, avgV);
                    }
                }
            }
        }
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
        params.dstPixelBytes = state.yuvChannelBytes;

        if (avifRGBFormatHasAlpha(rgb->format)) {
            params.srcDepth = rgb->depth;
            params.srcRange = AVIF_RANGE_FULL;
            params.srcPlane = rgb->pixels;
            params.srcRowBytes = rgb->rowBytes;
            params.srcOffsetBytes = state.rgbOffsetBytesA;
            params.srcPixelBytes = state.rgbPixelBytes;

            avifReformatAlpha(&params);
        } else {
            avifFillAlpha(&params);
        }
    }
    return AVIF_RESULT_OK;
}

static avifResult avifImageYUV16ToRGB16Color(avifImage * image, avifRGBImage * rgb, avifReformatState * state)
{
    const float kr = state->kr;
    const float kg = state->kg;
    const float kb = state->kb;
    const uint32_t maxUVI = ((image->width + state->formatInfo.chromaShiftX) >> state->formatInfo.chromaShiftX) - 1;
    const uint32_t maxUVJ = ((image->height + state->formatInfo.chromaShiftY) >> state->formatInfo.chromaShiftY) - 1;

    float yuvMaxChannel = (float)((1 << image->depth) - 1);
    float rgbMaxChannel = (float)((1 << rgb->depth) - 1);
    for (uint32_t j = 0; j < image->height; ++j) {
        const uint32_t uvJ = AVIF_MIN(j >> state->formatInfo.chromaShiftY, maxUVJ);
        uint16_t * ptrY = (uint16_t *)&image->yuvPlanes[AVIF_CHAN_Y][(j * image->yuvRowBytes[AVIF_CHAN_Y])];
        uint16_t * ptrU = (uint16_t *)&image->yuvPlanes[AVIF_CHAN_U][(uvJ * image->yuvRowBytes[AVIF_CHAN_U])];
        uint16_t * ptrV = (uint16_t *)&image->yuvPlanes[AVIF_CHAN_V][(uvJ * image->yuvRowBytes[AVIF_CHAN_V])];
        uint8_t * ptrR = &rgb->pixels[state->rgbOffsetBytesR + (j * rgb->rowBytes)];
        uint8_t * ptrG = &rgb->pixels[state->rgbOffsetBytesG + (j * rgb->rowBytes)];
        uint8_t * ptrB = &rgb->pixels[state->rgbOffsetBytesB + (j * rgb->rowBytes)];

        for (uint32_t i = 0; i < image->width; ++i) {
            // Unpack YUV into unorm
            uint32_t uvI = AVIF_MIN(i >> state->formatInfo.chromaShiftX, maxUVI);
            uint32_t unormY = ptrY[i];
            uint32_t unormU = ptrU[uvI];
            uint32_t unormV = ptrV[uvI];

            // adjust for limited/full color range, if need be
            if (image->yuvRange == AVIF_RANGE_LIMITED) {
                unormY = avifLimitedToFullY(image->depth, unormY);
                unormU = avifLimitedToFullUV(image->depth, unormU);
                unormV = avifLimitedToFullUV(image->depth, unormV);
            }

            // Convert unorm to float
            const float Y = (float)unormY / yuvMaxChannel;
            const float Cb = ((float)unormU / yuvMaxChannel) - 0.5f;
            const float Cr = ((float)unormV / yuvMaxChannel) - 0.5f;

            float R = Y + (2 * (1 - kr)) * Cr;
            float B = Y + (2 * (1 - kb)) * Cb;
            float G = Y - ((2 * ((kr * (1 - kr) * Cr) + (kb * (1 - kb) * Cb))) / kg);
            R = AVIF_CLAMP(R, 0.0f, 1.0f);
            G = AVIF_CLAMP(G, 0.0f, 1.0f);
            B = AVIF_CLAMP(B, 0.0f, 1.0f);

            *((uint16_t *)&ptrR[i * state->rgbPixelBytes]) = (uint16_t)(0.5f + (R * rgbMaxChannel));
            *((uint16_t *)&ptrG[i * state->rgbPixelBytes]) = (uint16_t)(0.5f + (G * rgbMaxChannel));
            *((uint16_t *)&ptrB[i * state->rgbPixelBytes]) = (uint16_t)(0.5f + (B * rgbMaxChannel));
        }
    }
    return AVIF_RESULT_OK;
}

static avifResult avifImageYUV16ToRGB16Mono(avifImage * image, avifRGBImage * rgb, avifReformatState * state)
{
    const float kr = state->kr;
    const float kg = state->kg;
    const float kb = state->kb;

    float yuvMaxChannel = (float)((1 << image->depth) - 1);
    float rgbMaxChannel = (float)((1 << rgb->depth) - 1);
    for (uint32_t j = 0; j < image->height; ++j) {
        uint16_t * ptrY = (uint16_t *)&image->yuvPlanes[AVIF_CHAN_Y][(j * image->yuvRowBytes[AVIF_CHAN_Y])];
        uint8_t * ptrR = &rgb->pixels[state->rgbOffsetBytesR + (j * rgb->rowBytes)];
        uint8_t * ptrG = &rgb->pixels[state->rgbOffsetBytesG + (j * rgb->rowBytes)];
        uint8_t * ptrB = &rgb->pixels[state->rgbOffsetBytesB + (j * rgb->rowBytes)];

        for (uint32_t i = 0; i < image->width; ++i) {
            // Unpack YUV into unorm
            uint32_t unormY = ptrY[i];

            // adjust for limited/full color range, if need be
            if (image->yuvRange == AVIF_RANGE_LIMITED) {
                unormY = avifLimitedToFullY(image->depth, unormY);
            }

            // Convert unorm to float
            const float Y = (float)unormY / yuvMaxChannel;
            const float Cb = 0.0f;
            const float Cr = 0.0f;

            float R = Y + (2 * (1 - kr)) * Cr;
            float B = Y + (2 * (1 - kb)) * Cb;
            float G = Y - ((2 * ((kr * (1 - kr) * Cr) + (kb * (1 - kb) * Cb))) / kg);
            R = AVIF_CLAMP(R, 0.0f, 1.0f);
            G = AVIF_CLAMP(G, 0.0f, 1.0f);
            B = AVIF_CLAMP(B, 0.0f, 1.0f);

            *((uint16_t *)&ptrR[i * state->rgbPixelBytes]) = (uint16_t)(0.5f + (R * rgbMaxChannel));
            *((uint16_t *)&ptrG[i * state->rgbPixelBytes]) = (uint16_t)(0.5f + (G * rgbMaxChannel));
            *((uint16_t *)&ptrB[i * state->rgbPixelBytes]) = (uint16_t)(0.5f + (B * rgbMaxChannel));
        }
    }
    return AVIF_RESULT_OK;
}
static avifResult avifImageYUV16ToRGB8Color(avifImage * image, avifRGBImage * rgb, avifReformatState * state)
{
    const float kr = state->kr;
    const float kg = state->kg;
    const float kb = state->kb;
    const uint32_t maxUVI = ((image->width + state->formatInfo.chromaShiftX) >> state->formatInfo.chromaShiftX) - 1;
    const uint32_t maxUVJ = ((image->height + state->formatInfo.chromaShiftY) >> state->formatInfo.chromaShiftY) - 1;

    float yuvMaxChannel = (float)((1 << image->depth) - 1);
    float rgbMaxChannel = (float)((1 << rgb->depth) - 1);
    for (uint32_t j = 0; j < image->height; ++j) {
        const uint32_t uvJ = AVIF_MIN(j >> state->formatInfo.chromaShiftY, maxUVJ);
        uint16_t * ptrY = (uint16_t *)&image->yuvPlanes[AVIF_CHAN_Y][(j * image->yuvRowBytes[AVIF_CHAN_Y])];
        uint16_t * ptrU = (uint16_t *)&image->yuvPlanes[AVIF_CHAN_U][(uvJ * image->yuvRowBytes[AVIF_CHAN_U])];
        uint16_t * ptrV = (uint16_t *)&image->yuvPlanes[AVIF_CHAN_V][(uvJ * image->yuvRowBytes[AVIF_CHAN_V])];
        uint8_t * ptrR = &rgb->pixels[state->rgbOffsetBytesR + (j * rgb->rowBytes)];
        uint8_t * ptrG = &rgb->pixels[state->rgbOffsetBytesG + (j * rgb->rowBytes)];
        uint8_t * ptrB = &rgb->pixels[state->rgbOffsetBytesB + (j * rgb->rowBytes)];

        for (uint32_t i = 0; i < image->width; ++i) {
            // Unpack YUV into unorm
            uint32_t uvI = AVIF_MIN(i >> state->formatInfo.chromaShiftX, maxUVI);
            uint32_t unormY = ptrY[i];
            uint32_t unormU = ptrU[uvI];
            uint32_t unormV = ptrV[uvI];

            // adjust for limited/full color range, if need be
            if (image->yuvRange == AVIF_RANGE_LIMITED) {
                unormY = avifLimitedToFullY(image->depth, unormY);
                unormU = avifLimitedToFullUV(image->depth, unormU);
                unormV = avifLimitedToFullUV(image->depth, unormV);
            }

            // Convert unorm to float
            const float Y = (float)unormY / yuvMaxChannel;
            const float Cb = ((float)unormU / yuvMaxChannel) - 0.5f;
            const float Cr = ((float)unormV / yuvMaxChannel) - 0.5f;

            float R = Y + (2 * (1 - kr)) * Cr;
            float B = Y + (2 * (1 - kb)) * Cb;
            float G = Y - ((2 * ((kr * (1 - kr) * Cr) + (kb * (1 - kb) * Cb))) / kg);
            R = AVIF_CLAMP(R, 0.0f, 1.0f);
            G = AVIF_CLAMP(G, 0.0f, 1.0f);
            B = AVIF_CLAMP(B, 0.0f, 1.0f);

            ptrR[i * state->rgbPixelBytes] = (uint8_t)(0.5f + (R * rgbMaxChannel));
            ptrG[i * state->rgbPixelBytes] = (uint8_t)(0.5f + (G * rgbMaxChannel));
            ptrB[i * state->rgbPixelBytes] = (uint8_t)(0.5f + (B * rgbMaxChannel));
        }
    }
    return AVIF_RESULT_OK;
}

static avifResult avifImageYUV16ToRGB8Mono(avifImage * image, avifRGBImage * rgb, avifReformatState * state)
{
    const float kr = state->kr;
    const float kg = state->kg;
    const float kb = state->kb;

    float yuvMaxChannel = (float)((1 << image->depth) - 1);
    float rgbMaxChannel = (float)((1 << rgb->depth) - 1);
    for (uint32_t j = 0; j < image->height; ++j) {
        uint16_t * ptrY = (uint16_t *)&image->yuvPlanes[AVIF_CHAN_Y][(j * image->yuvRowBytes[AVIF_CHAN_Y])];
        uint8_t * ptrR = &rgb->pixels[state->rgbOffsetBytesR + (j * rgb->rowBytes)];
        uint8_t * ptrG = &rgb->pixels[state->rgbOffsetBytesG + (j * rgb->rowBytes)];
        uint8_t * ptrB = &rgb->pixels[state->rgbOffsetBytesB + (j * rgb->rowBytes)];

        for (uint32_t i = 0; i < image->width; ++i) {
            // Unpack YUV into unorm
            uint32_t unormY = ptrY[i];

            // adjust for limited/full color range, if need be
            if (image->yuvRange == AVIF_RANGE_LIMITED) {
                unormY = avifLimitedToFullY(image->depth, unormY);
            }

            // Convert unorm to float
            const float Y = (float)unormY / yuvMaxChannel;
            const float Cb = 0.0f;
            const float Cr = 0.0f;

            float R = Y + (2 * (1 - kr)) * Cr;
            float B = Y + (2 * (1 - kb)) * Cb;
            float G = Y - ((2 * ((kr * (1 - kr) * Cr) + (kb * (1 - kb) * Cb))) / kg);
            R = AVIF_CLAMP(R, 0.0f, 1.0f);
            G = AVIF_CLAMP(G, 0.0f, 1.0f);
            B = AVIF_CLAMP(B, 0.0f, 1.0f);

            *((uint16_t *)&ptrR[i * state->rgbPixelBytes]) = (uint16_t)(0.5f + (R * rgbMaxChannel));
            *((uint16_t *)&ptrG[i * state->rgbPixelBytes]) = (uint16_t)(0.5f + (G * rgbMaxChannel));
            *((uint16_t *)&ptrB[i * state->rgbPixelBytes]) = (uint16_t)(0.5f + (B * rgbMaxChannel));
        }
    }
    return AVIF_RESULT_OK;
}

static avifResult avifImageYUV8ToRGB16Color(avifImage * image, avifRGBImage * rgb, avifReformatState * state)
{
    const float kr = state->kr;
    const float kg = state->kg;
    const float kb = state->kb;
    const uint32_t maxUVI = ((image->width + state->formatInfo.chromaShiftX) >> state->formatInfo.chromaShiftX) - 1;
    const uint32_t maxUVJ = ((image->height + state->formatInfo.chromaShiftY) >> state->formatInfo.chromaShiftY) - 1;

    float yuvMaxChannel = (float)((1 << image->depth) - 1);
    float rgbMaxChannel = (float)((1 << rgb->depth) - 1);
    for (uint32_t j = 0; j < image->height; ++j) {
        const uint32_t uvJ = AVIF_MIN(j >> state->formatInfo.chromaShiftY, maxUVJ);
        uint8_t * ptrY = &image->yuvPlanes[AVIF_CHAN_Y][(j * image->yuvRowBytes[AVIF_CHAN_Y])];
        uint8_t * ptrU = &image->yuvPlanes[AVIF_CHAN_U][(uvJ * image->yuvRowBytes[AVIF_CHAN_U])];
        uint8_t * ptrV = &image->yuvPlanes[AVIF_CHAN_V][(uvJ * image->yuvRowBytes[AVIF_CHAN_V])];
        uint8_t * ptrR = &rgb->pixels[state->rgbOffsetBytesR + (j * rgb->rowBytes)];
        uint8_t * ptrG = &rgb->pixels[state->rgbOffsetBytesG + (j * rgb->rowBytes)];
        uint8_t * ptrB = &rgb->pixels[state->rgbOffsetBytesB + (j * rgb->rowBytes)];

        for (uint32_t i = 0; i < image->width; ++i) {
            // Unpack YUV into unorm
            uint32_t uvI = AVIF_MIN(i >> state->formatInfo.chromaShiftX, maxUVI);
            uint32_t unormY = ptrY[i];
            uint32_t unormU = ptrU[uvI];
            uint32_t unormV = ptrV[uvI];

            // adjust for limited/full color range, if need be
            if (image->yuvRange == AVIF_RANGE_LIMITED) {
                unormY = avifLimitedToFullY(image->depth, unormY);
                unormU = avifLimitedToFullUV(image->depth, unormU);
                unormV = avifLimitedToFullUV(image->depth, unormV);
            }

            // Convert unorm to float
            const float Y = (float)unormY / yuvMaxChannel;
            const float Cb = ((float)unormU / yuvMaxChannel) - 0.5f;
            const float Cr = ((float)unormV / yuvMaxChannel) - 0.5f;

            float R = Y + (2 * (1 - kr)) * Cr;
            float B = Y + (2 * (1 - kb)) * Cb;
            float G = Y - ((2 * ((kr * (1 - kr) * Cr) + (kb * (1 - kb) * Cb))) / kg);
            R = AVIF_CLAMP(R, 0.0f, 1.0f);
            G = AVIF_CLAMP(G, 0.0f, 1.0f);
            B = AVIF_CLAMP(B, 0.0f, 1.0f);

            *((uint16_t *)&ptrR[i * state->rgbPixelBytes]) = (uint16_t)(0.5f + (R * rgbMaxChannel));
            *((uint16_t *)&ptrG[i * state->rgbPixelBytes]) = (uint16_t)(0.5f + (G * rgbMaxChannel));
            *((uint16_t *)&ptrB[i * state->rgbPixelBytes]) = (uint16_t)(0.5f + (B * rgbMaxChannel));
        }
    }
    return AVIF_RESULT_OK;
}

static avifResult avifImageYUV8ToRGB16Mono(avifImage * image, avifRGBImage * rgb, avifReformatState * state)
{
    const float kr = state->kr;
    const float kg = state->kg;
    const float kb = state->kb;

    float yuvMaxChannel = (float)((1 << image->depth) - 1);
    float rgbMaxChannel = (float)((1 << rgb->depth) - 1);
    for (uint32_t j = 0; j < image->height; ++j) {
        uint8_t * ptrY = &image->yuvPlanes[AVIF_CHAN_Y][(j * image->yuvRowBytes[AVIF_CHAN_Y])];
        uint8_t * ptrR = &rgb->pixels[state->rgbOffsetBytesR + (j * rgb->rowBytes)];
        uint8_t * ptrG = &rgb->pixels[state->rgbOffsetBytesG + (j * rgb->rowBytes)];
        uint8_t * ptrB = &rgb->pixels[state->rgbOffsetBytesB + (j * rgb->rowBytes)];

        for (uint32_t i = 0; i < image->width; ++i) {
            // Unpack YUV into unorm
            uint32_t unormY = ptrY[i];

            // adjust for limited/full color range, if need be
            if (image->yuvRange == AVIF_RANGE_LIMITED) {
                unormY = avifLimitedToFullY(image->depth, unormY);
            }

            // Convert unorm to float
            const float Y = (float)unormY / yuvMaxChannel;
            const float Cb = 0.0f;
            const float Cr = 0.0f;

            float R = Y + (2 * (1 - kr)) * Cr;
            float B = Y + (2 * (1 - kb)) * Cb;
            float G = Y - ((2 * ((kr * (1 - kr) * Cr) + (kb * (1 - kb) * Cb))) / kg);
            R = AVIF_CLAMP(R, 0.0f, 1.0f);
            G = AVIF_CLAMP(G, 0.0f, 1.0f);
            B = AVIF_CLAMP(B, 0.0f, 1.0f);

            ptrR[i * state->rgbPixelBytes] = (uint8_t)(0.5f + (R * rgbMaxChannel));
            ptrG[i * state->rgbPixelBytes] = (uint8_t)(0.5f + (G * rgbMaxChannel));
            ptrB[i * state->rgbPixelBytes] = (uint8_t)(0.5f + (B * rgbMaxChannel));
        }
    }
    return AVIF_RESULT_OK;
}

static avifResult avifImageYUV8ToRGB8Color(avifImage * image, avifRGBImage * rgb, avifReformatState * state)
{
    const float kr = state->kr;
    const float kg = state->kg;
    const float kb = state->kb;
    const uint32_t maxUVI = ((image->width + state->formatInfo.chromaShiftX) >> state->formatInfo.chromaShiftX) - 1;
    const uint32_t maxUVJ = ((image->height + state->formatInfo.chromaShiftY) >> state->formatInfo.chromaShiftY) - 1;

    float yuvMaxChannel = (float)((1 << image->depth) - 1);
    float rgbMaxChannel = (float)((1 << rgb->depth) - 1);
    for (uint32_t j = 0; j < image->height; ++j) {
        const uint32_t uvJ = AVIF_MIN(j >> state->formatInfo.chromaShiftY, maxUVJ);
        uint8_t * ptrY = &image->yuvPlanes[AVIF_CHAN_Y][(j * image->yuvRowBytes[AVIF_CHAN_Y])];
        uint8_t * ptrU = &image->yuvPlanes[AVIF_CHAN_U][(uvJ * image->yuvRowBytes[AVIF_CHAN_U])];
        uint8_t * ptrV = &image->yuvPlanes[AVIF_CHAN_V][(uvJ * image->yuvRowBytes[AVIF_CHAN_V])];
        uint8_t * ptrR = &rgb->pixels[state->rgbOffsetBytesR + (j * rgb->rowBytes)];
        uint8_t * ptrG = &rgb->pixels[state->rgbOffsetBytesG + (j * rgb->rowBytes)];
        uint8_t * ptrB = &rgb->pixels[state->rgbOffsetBytesB + (j * rgb->rowBytes)];

        for (uint32_t i = 0; i < image->width; ++i) {
            // Unpack YUV into unorm
            uint32_t uvI = AVIF_MIN(i >> state->formatInfo.chromaShiftX, maxUVI);
            uint32_t unormY = ptrY[i];
            uint32_t unormU = ptrU[uvI];
            uint32_t unormV = ptrV[uvI];

            // adjust for limited/full color range, if need be
            if (image->yuvRange == AVIF_RANGE_LIMITED) {
                unormY = avifLimitedToFullY(image->depth, unormY);
                unormU = avifLimitedToFullUV(image->depth, unormU);
                unormV = avifLimitedToFullUV(image->depth, unormV);
            }

            // Convert unorm to float
            const float Y = (float)unormY / yuvMaxChannel;
            const float Cb = ((float)unormU / yuvMaxChannel) - 0.5f;
            const float Cr = ((float)unormV / yuvMaxChannel) - 0.5f;

            float R = Y + (2 * (1 - kr)) * Cr;
            float B = Y + (2 * (1 - kb)) * Cb;
            float G = Y - ((2 * ((kr * (1 - kr) * Cr) + (kb * (1 - kb) * Cb))) / kg);
            R = AVIF_CLAMP(R, 0.0f, 1.0f);
            G = AVIF_CLAMP(G, 0.0f, 1.0f);
            B = AVIF_CLAMP(B, 0.0f, 1.0f);

            ptrR[i * state->rgbPixelBytes] = (uint8_t)(0.5f + (R * rgbMaxChannel));
            ptrG[i * state->rgbPixelBytes] = (uint8_t)(0.5f + (G * rgbMaxChannel));
            ptrB[i * state->rgbPixelBytes] = (uint8_t)(0.5f + (B * rgbMaxChannel));
        }
    }
    return AVIF_RESULT_OK;
}

static avifResult avifImageYUV8ToRGB8Mono(avifImage * image, avifRGBImage * rgb, avifReformatState * state)
{
    const float kr = state->kr;
    const float kg = state->kg;
    const float kb = state->kb;

    float yuvMaxChannel = (float)((1 << image->depth) - 1);
    float rgbMaxChannel = (float)((1 << rgb->depth) - 1);
    for (uint32_t j = 0; j < image->height; ++j) {
        uint8_t * ptrY = &image->yuvPlanes[AVIF_CHAN_Y][(j * image->yuvRowBytes[AVIF_CHAN_Y])];
        uint8_t * ptrR = &rgb->pixels[state->rgbOffsetBytesR + (j * rgb->rowBytes)];
        uint8_t * ptrG = &rgb->pixels[state->rgbOffsetBytesG + (j * rgb->rowBytes)];
        uint8_t * ptrB = &rgb->pixels[state->rgbOffsetBytesB + (j * rgb->rowBytes)];

        for (uint32_t i = 0; i < image->width; ++i) {
            // Unpack YUV into unorm
            uint32_t unormY = ptrY[i];

            // adjust for limited/full color range, if need be
            if (image->yuvRange == AVIF_RANGE_LIMITED) {
                unormY = avifLimitedToFullY(image->depth, unormY);
            }

            // Convert unorm to float
            const float Y = (float)unormY / yuvMaxChannel;
            const float Cb = 0.0f;
            const float Cr = 0.0f;

            float R = Y + (2 * (1 - kr)) * Cr;
            float B = Y + (2 * (1 - kb)) * Cb;
            float G = Y - ((2 * ((kr * (1 - kr) * Cr) + (kb * (1 - kb) * Cb))) / kg);
            R = AVIF_CLAMP(R, 0.0f, 1.0f);
            G = AVIF_CLAMP(G, 0.0f, 1.0f);
            B = AVIF_CLAMP(B, 0.0f, 1.0f);

            ptrR[i * state->rgbPixelBytes] = (uint8_t)(0.5f + (R * rgbMaxChannel));
            ptrG[i * state->rgbPixelBytes] = (uint8_t)(0.5f + (G * rgbMaxChannel));
            ptrB[i * state->rgbPixelBytes] = (uint8_t)(0.5f + (B * rgbMaxChannel));
        }
    }
    return AVIF_RESULT_OK;
}

avifResult avifImageYUVToRGB(avifImage * image, avifRGBImage * rgb)
{
    if (!image->yuvPlanes[AVIF_CHAN_Y]) {
        return AVIF_RESULT_REFORMAT_FAILED;
    }

    avifReformatState state;
    if (!avifPrepareReformatState(image, rgb, &state)) {
        return AVIF_RESULT_REFORMAT_FAILED;
    }

    if (avifRGBFormatHasAlpha(rgb->format)) {
        avifAlphaParams params;

        params.width = rgb->width;
        params.height = rgb->height;
        params.dstDepth = rgb->depth;
        params.dstRange = AVIF_RANGE_FULL;
        params.dstPlane = rgb->pixels;
        params.dstRowBytes = rgb->rowBytes;
        params.dstOffsetBytes = state.rgbOffsetBytesA;
        params.dstPixelBytes = state.rgbPixelBytes;

        if (image->alphaPlane && image->alphaRowBytes) {
            params.srcDepth = image->depth;
            params.srcRange = image->alphaRange;
            params.srcPlane = image->alphaPlane;
            params.srcRowBytes = image->alphaRowBytes;
            params.srcOffsetBytes = 0;
            params.srcPixelBytes = state.yuvChannelBytes;

            avifReformatAlpha(&params);
        } else {
            avifFillAlpha(&params);
        }
    }

    if (image->depth > 8) {
        // yuv:u16

        if (rgb->depth > 8) {
            // yuv:u16, rgb:u16

            if (image->yuvRowBytes[AVIF_CHAN_U] && image->yuvRowBytes[AVIF_CHAN_V]) {
                return avifImageYUV16ToRGB16Color(image, rgb, &state);
            }
            return avifImageYUV16ToRGB16Mono(image, rgb, &state);
        } else {
            // yuv:u16, rgb:u8

            if (image->yuvRowBytes[AVIF_CHAN_U] && image->yuvRowBytes[AVIF_CHAN_V]) {
                return avifImageYUV16ToRGB8Color(image, rgb, &state);
            }
            return avifImageYUV16ToRGB8Mono(image, rgb, &state);
        }
    } else {
        // yuv:u8

        if (rgb->depth > 8) {
            // yuv:u8, rgb:u16

            if (image->yuvRowBytes[AVIF_CHAN_U] && image->yuvRowBytes[AVIF_CHAN_V]) {
                return avifImageYUV8ToRGB16Color(image, rgb, &state);
            }
            return avifImageYUV8ToRGB16Mono(image, rgb, &state);
        } else {
            // yuv:u8, rgb:u8

            if (image->yuvRowBytes[AVIF_CHAN_U] && image->yuvRowBytes[AVIF_CHAN_V]) {
                return avifImageYUV8ToRGB8Color(image, rgb, &state);
            }
            return avifImageYUV8ToRGB8Mono(image, rgb, &state);
        }
    }
}

// Limited -> Full
// Plan: subtract limited offset, then multiply by ratio of FULLSIZE/LIMITEDSIZE (rounding), then clamp.
// RATIO = (FULLY - 0) / (MAXLIMITEDY - MINLIMITEDY)
// -----------------------------------------
// ( ( (v - MINLIMITEDY)                    | subtract limited offset
//     * FULLY                              | multiply numerator of ratio
//   ) + ((MAXLIMITEDY - MINLIMITEDY) / 2)  | add 0.5 (half of denominator) to round
// ) / (MAXLIMITEDY - MINLIMITEDY)          | divide by denominator of ratio
// AVIF_CLAMP(v, 0, FULLY)                  | clamp to full range
// -----------------------------------------
#define LIMITED_TO_FULL(MINLIMITEDY, MAXLIMITEDY, FULLY)                                                 \
    v = (((v - MINLIMITEDY) * FULLY) + ((MAXLIMITEDY - MINLIMITEDY) / 2)) / (MAXLIMITEDY - MINLIMITEDY); \
    v = AVIF_CLAMP(v, 0, FULLY)

// Full -> Limited
// Plan: multiply by ratio of LIMITEDSIZE/FULLSIZE (rounding), then add limited offset, then clamp.
// RATIO = (MAXLIMITEDY - MINLIMITEDY) / (FULLY - 0)
// -----------------------------------------
// ( ( (v * (MAXLIMITEDY - MINLIMITEDY))    | multiply numerator of ratio
//     + (FULLY / 2)                        | add 0.5 (half of denominator) to round
//   ) / FULLY                              | divide by denominator of ratio
// ) + MINLIMITEDY                          | add limited offset
//  AVIF_CLAMP(v, MINLIMITEDY, MAXLIMITEDY) | clamp to limited range
// -----------------------------------------
#define FULL_TO_LIMITED(MINLIMITEDY, MAXLIMITEDY, FULLY)                           \
    v = (((v * (MAXLIMITEDY - MINLIMITEDY)) + (FULLY / 2)) / FULLY) + MINLIMITEDY; \
    v = AVIF_CLAMP(v, MINLIMITEDY, MAXLIMITEDY)

int avifLimitedToFullY(int depth, int v)
{
    switch (depth) {
        case 8:
            LIMITED_TO_FULL(16, 235, 255);
            break;
        case 10:
            LIMITED_TO_FULL(64, 940, 1023);
            break;
        case 12:
            LIMITED_TO_FULL(256, 3760, 4095);
            break;
        case 16:
            LIMITED_TO_FULL(1024, 60160, 65535);
            break;
    }
    return v;
}

int avifLimitedToFullUV(int depth, int v)
{
    switch (depth) {
        case 8:
            LIMITED_TO_FULL(16, 240, 255);
            break;
        case 10:
            LIMITED_TO_FULL(64, 960, 1023);
            break;
        case 12:
            LIMITED_TO_FULL(256, 3840, 4095);
            break;
        case 16:
            LIMITED_TO_FULL(1024, 61440, 65535);
            break;
    }
    return v;
}

int avifFullToLimitedY(int depth, int v)
{
    switch (depth) {
        case 8:
            FULL_TO_LIMITED(16, 235, 255);
            break;
        case 10:
            FULL_TO_LIMITED(64, 940, 1023);
            break;
        case 12:
            FULL_TO_LIMITED(256, 3760, 4095);
            break;
        case 16:
            FULL_TO_LIMITED(1024, 60160, 65535);
            break;
    }
    return v;
}

int avifFullToLimitedUV(int depth, int v)
{
    switch (depth) {
        case 8:
            FULL_TO_LIMITED(16, 240, 255);
            break;
        case 10:
            FULL_TO_LIMITED(64, 960, 1023);
            break;
        case 12:
            FULL_TO_LIMITED(256, 3840, 4095);
            break;
        case 16:
            FULL_TO_LIMITED(1024, 61440, 65535);
            break;
    }
    return v;
}
