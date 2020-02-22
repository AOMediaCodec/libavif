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

avifBool avifPrepareReformatState(avifImage * image, avifReformatState * state)
{
    if (image->yuvFormat == AVIF_PIXEL_FORMAT_NONE) {
        return AVIF_FALSE;
    }
    avifGetPixelFormatInfo(image->yuvFormat, &state->formatInfo);
    avifCalcYUVCoefficients(image, &state->kr, &state->kg, &state->kb);
    state->usesU16 = avifImageUsesU16(image);
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

avifResult avifImageRGBToYUV(avifImage * image)
{
    if (!image->rgbPlanes[AVIF_CHAN_R] || !image->rgbPlanes[AVIF_CHAN_G] || !image->rgbPlanes[AVIF_CHAN_B]) {
        return AVIF_RESULT_REFORMAT_FAILED;
    }

    avifReformatState state;
    if (!avifPrepareReformatState(image, &state)) {
        return AVIF_RESULT_REFORMAT_FAILED;
    }

    avifImageAllocatePlanes(image, AVIF_PLANES_YUV);

    const float kr = state.kr;
    const float kg = state.kg;
    const float kb = state.kb;

    struct YUVBlock yuvBlock[2][2];
    float rgbPixel[3];
    float maxChannel = (float)((1 << image->depth) - 1);
    uint8_t ** rgbPlanes = image->rgbPlanes;
    uint32_t * rgbRowBytes = image->rgbRowBytes;
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
                    if (state.usesU16) {
                        rgbPixel[0] = *((uint16_t *)(&rgbPlanes[AVIF_CHAN_R][(i * 2) + (j * rgbRowBytes[AVIF_CHAN_R])])) / maxChannel;
                        rgbPixel[1] = *((uint16_t *)(&rgbPlanes[AVIF_CHAN_G][(i * 2) + (j * rgbRowBytes[AVIF_CHAN_G])])) / maxChannel;
                        rgbPixel[2] = *((uint16_t *)(&rgbPlanes[AVIF_CHAN_B][(i * 2) + (j * rgbRowBytes[AVIF_CHAN_B])])) / maxChannel;
                    } else {
                        rgbPixel[0] = rgbPlanes[AVIF_CHAN_R][i + (j * rgbRowBytes[AVIF_CHAN_R])] / maxChannel;
                        rgbPixel[1] = rgbPlanes[AVIF_CHAN_G][i + (j * rgbRowBytes[AVIF_CHAN_G])] / maxChannel;
                        rgbPixel[2] = rgbPlanes[AVIF_CHAN_B][i + (j * rgbRowBytes[AVIF_CHAN_B])] / maxChannel;
                    }

                    // RGB -> YUV conversion
                    float Y = (kr * rgbPixel[0]) + (kg * rgbPixel[1]) + (kb * rgbPixel[2]);
                    yuvBlock[bI][bJ].y = Y;
                    yuvBlock[bI][bJ].u = (rgbPixel[2] - Y) / (2 * (1 - kb));
                    yuvBlock[bI][bJ].v = (rgbPixel[0] - Y) / (2 * (1 - kr));

                    if (state.usesU16) {
                        uint16_t * pY = (uint16_t *)&yuvPlanes[AVIF_CHAN_Y][(i * 2) + (j * yuvRowBytes[AVIF_CHAN_Y])];
                        *pY = (uint16_t)yuvToUNorm(AVIF_CHAN_Y, image->yuvRange, image->depth, maxChannel, yuvBlock[bI][bJ].y);
                        if (!state.formatInfo.chromaShiftX && !state.formatInfo.chromaShiftY) {
                            // YUV444, full chroma
                            uint16_t * pU = (uint16_t *)&yuvPlanes[AVIF_CHAN_U][(i * 2) + (j * yuvRowBytes[AVIF_CHAN_U])];
                            *pU = (uint16_t)yuvToUNorm(AVIF_CHAN_U, image->yuvRange, image->depth, maxChannel, yuvBlock[bI][bJ].u);
                            uint16_t * pV = (uint16_t *)&yuvPlanes[AVIF_CHAN_V][(i * 2) + (j * yuvRowBytes[AVIF_CHAN_V])];
                            *pV = (uint16_t)yuvToUNorm(AVIF_CHAN_V, image->yuvRange, image->depth, maxChannel, yuvBlock[bI][bJ].v);
                        }
                    } else {
                        yuvPlanes[AVIF_CHAN_Y][i + (j * yuvRowBytes[AVIF_CHAN_Y])] =
                            (uint8_t)yuvToUNorm(AVIF_CHAN_Y, image->yuvRange, image->depth, maxChannel, yuvBlock[bI][bJ].y);
                        if (!state.formatInfo.chromaShiftX && !state.formatInfo.chromaShiftY) {
                            // YUV444, full chroma
                            yuvPlanes[AVIF_CHAN_U][i + (j * yuvRowBytes[AVIF_CHAN_U])] =
                                (uint8_t)yuvToUNorm(AVIF_CHAN_U, image->yuvRange, image->depth, maxChannel, yuvBlock[bI][bJ].u);
                            yuvPlanes[AVIF_CHAN_V][i + (j * yuvRowBytes[AVIF_CHAN_V])] =
                                (uint8_t)yuvToUNorm(AVIF_CHAN_V, image->yuvRange, image->depth, maxChannel, yuvBlock[bI][bJ].v);
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
                if (state.usesU16) {
                    uint16_t * pU = (uint16_t *)&yuvPlanes[AVIF_CHAN_U][(uvI * 2) + (uvJ * yuvRowBytes[AVIF_CHAN_U])];
                    *pU = (uint16_t)yuvToUNorm(AVIF_CHAN_U, image->yuvRange, image->depth, maxChannel, avgU);
                    uint16_t * pV = (uint16_t *)&yuvPlanes[AVIF_CHAN_V][(uvI * 2) + (uvJ * yuvRowBytes[AVIF_CHAN_V])];
                    *pV = (uint16_t)yuvToUNorm(AVIF_CHAN_V, image->yuvRange, image->depth, maxChannel, avgV);
                } else {
                    yuvPlanes[AVIF_CHAN_U][uvI + (uvJ * yuvRowBytes[AVIF_CHAN_U])] =
                        (uint8_t)yuvToUNorm(AVIF_CHAN_U, image->yuvRange, image->depth, maxChannel, avgU);
                    yuvPlanes[AVIF_CHAN_V][uvI + (uvJ * yuvRowBytes[AVIF_CHAN_V])] =
                        (uint8_t)yuvToUNorm(AVIF_CHAN_V, image->yuvRange, image->depth, maxChannel, avgV);
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
                    if (state.usesU16) {
                        uint16_t * pU = (uint16_t *)&yuvPlanes[AVIF_CHAN_U][(uvI * 2) + (uvJ * yuvRowBytes[AVIF_CHAN_U])];
                        *pU = (uint16_t)yuvToUNorm(AVIF_CHAN_U, image->yuvRange, image->depth, maxChannel, avgU);
                        uint16_t * pV = (uint16_t *)&yuvPlanes[AVIF_CHAN_V][(uvI * 2) + (uvJ * yuvRowBytes[AVIF_CHAN_V])];
                        *pV = (uint16_t)yuvToUNorm(AVIF_CHAN_V, image->yuvRange, image->depth, maxChannel, avgV);
                    } else {
                        yuvPlanes[AVIF_CHAN_U][uvI + (uvJ * yuvRowBytes[AVIF_CHAN_U])] =
                            (uint8_t)yuvToUNorm(AVIF_CHAN_U, image->yuvRange, image->depth, maxChannel, avgU);
                        yuvPlanes[AVIF_CHAN_V][uvI + (uvJ * yuvRowBytes[AVIF_CHAN_V])] =
                            (uint8_t)yuvToUNorm(AVIF_CHAN_V, image->yuvRange, image->depth, maxChannel, avgV);
                    }
                }
            }
        }
    }

    return AVIF_RESULT_OK;
}

static avifResult avifImageYUVToRGB16Color(avifImage * image, avifReformatState * state)
{
    const float kr = state->kr;
    const float kg = state->kg;
    const float kb = state->kb;
    const uint32_t maxUVI = ((image->width + state->formatInfo.chromaShiftX) >> state->formatInfo.chromaShiftX) - 1;
    const uint32_t maxUVJ = ((image->height + state->formatInfo.chromaShiftY) >> state->formatInfo.chromaShiftY) - 1;

    float maxChannel = (float)((1 << image->depth) - 1);
    uint8_t ** rgbPlanes = image->rgbPlanes;
    uint32_t * rgbRowBytes = image->rgbRowBytes;
    for (uint32_t j = 0; j < image->height; ++j) {
        const uint32_t uvJ = AVIF_MIN(j >> state->formatInfo.chromaShiftY, maxUVJ);
        uint16_t * ptrY = (uint16_t *)&image->yuvPlanes[AVIF_CHAN_Y][(j * image->yuvRowBytes[AVIF_CHAN_Y])];
        uint16_t * ptrU = (uint16_t *)&image->yuvPlanes[AVIF_CHAN_U][(uvJ * image->yuvRowBytes[AVIF_CHAN_U])];
        uint16_t * ptrV = (uint16_t *)&image->yuvPlanes[AVIF_CHAN_V][(uvJ * image->yuvRowBytes[AVIF_CHAN_V])];
        uint16_t * ptrR = (uint16_t *)&rgbPlanes[AVIF_CHAN_R][j * rgbRowBytes[AVIF_CHAN_R]];
        uint16_t * ptrG = (uint16_t *)&rgbPlanes[AVIF_CHAN_G][j * rgbRowBytes[AVIF_CHAN_G]];
        uint16_t * ptrB = (uint16_t *)&rgbPlanes[AVIF_CHAN_B][j * rgbRowBytes[AVIF_CHAN_B]];

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
            const float Y = (float)unormY / maxChannel;
            const float Cb = ((float)unormU / maxChannel) - 0.5f;
            const float Cr = ((float)unormV / maxChannel) - 0.5f;

            float R = Y + (2 * (1 - kr)) * Cr;
            float B = Y + (2 * (1 - kb)) * Cb;
            float G = Y - ((2 * ((kr * (1 - kr) * Cr) + (kb * (1 - kb) * Cb))) / kg);
            R = AVIF_CLAMP(R, 0.0f, 1.0f);
            G = AVIF_CLAMP(G, 0.0f, 1.0f);
            B = AVIF_CLAMP(B, 0.0f, 1.0f);

            ptrR[i] = (uint16_t)(0.5f + (R * maxChannel));
            ptrG[i] = (uint16_t)(0.5f + (G * maxChannel));
            ptrB[i] = (uint16_t)(0.5f + (B * maxChannel));
        }
    }
    return AVIF_RESULT_OK;
}

static avifResult avifImageYUVToRGB16Mono(avifImage * image, avifReformatState * state)
{
    const float kr = state->kr;
    const float kg = state->kg;
    const float kb = state->kb;

    float maxChannel = (float)((1 << image->depth) - 1);
    uint8_t ** rgbPlanes = image->rgbPlanes;
    uint32_t * rgbRowBytes = image->rgbRowBytes;
    for (uint32_t j = 0; j < image->height; ++j) {
        uint16_t * ptrY = (uint16_t *)&image->yuvPlanes[AVIF_CHAN_Y][(j * image->yuvRowBytes[AVIF_CHAN_Y])];
        uint16_t * ptrR = (uint16_t *)&rgbPlanes[AVIF_CHAN_R][j * rgbRowBytes[AVIF_CHAN_R]];
        uint16_t * ptrG = (uint16_t *)&rgbPlanes[AVIF_CHAN_G][j * rgbRowBytes[AVIF_CHAN_G]];
        uint16_t * ptrB = (uint16_t *)&rgbPlanes[AVIF_CHAN_B][j * rgbRowBytes[AVIF_CHAN_B]];

        for (uint32_t i = 0; i < image->width; ++i) {
            // Unpack YUV into unorm
            uint32_t unormY = ptrY[i];

            // adjust for limited/full color range, if need be
            if (image->yuvRange == AVIF_RANGE_LIMITED) {
                unormY = avifLimitedToFullY(image->depth, unormY);
            }

            // Convert unorm to float
            const float Y = (float)unormY / maxChannel;
            const float Cb = 0.0f;
            const float Cr = 0.0f;

            float R = Y + (2 * (1 - kr)) * Cr;
            float B = Y + (2 * (1 - kb)) * Cb;
            float G = Y - ((2 * ((kr * (1 - kr) * Cr) + (kb * (1 - kb) * Cb))) / kg);
            R = AVIF_CLAMP(R, 0.0f, 1.0f);
            G = AVIF_CLAMP(G, 0.0f, 1.0f);
            B = AVIF_CLAMP(B, 0.0f, 1.0f);

            ptrR[i] = (uint16_t)(0.5f + (R * maxChannel));
            ptrG[i] = (uint16_t)(0.5f + (G * maxChannel));
            ptrB[i] = (uint16_t)(0.5f + (B * maxChannel));
        }
    }
    return AVIF_RESULT_OK;
}

static avifResult avifImageYUVToRGB8Color(avifImage * image, avifReformatState * state)
{
    const float kr = state->kr;
    const float kg = state->kg;
    const float kb = state->kb;
    const uint32_t maxUVI = ((image->width + state->formatInfo.chromaShiftX) >> state->formatInfo.chromaShiftX) - 1;
    const uint32_t maxUVJ = ((image->height + state->formatInfo.chromaShiftY) >> state->formatInfo.chromaShiftY) - 1;

    float maxChannel = (float)((1 << image->depth) - 1);
    uint8_t ** rgbPlanes = image->rgbPlanes;
    uint32_t * rgbRowBytes = image->rgbRowBytes;
    for (uint32_t j = 0; j < image->height; ++j) {
        const uint32_t uvJ = AVIF_MIN(j >> state->formatInfo.chromaShiftY, maxUVJ);
        uint8_t * ptrY = &image->yuvPlanes[AVIF_CHAN_Y][(j * image->yuvRowBytes[AVIF_CHAN_Y])];
        uint8_t * ptrU = &image->yuvPlanes[AVIF_CHAN_U][(uvJ * image->yuvRowBytes[AVIF_CHAN_U])];
        uint8_t * ptrV = &image->yuvPlanes[AVIF_CHAN_V][(uvJ * image->yuvRowBytes[AVIF_CHAN_V])];
        uint8_t * ptrR = &rgbPlanes[AVIF_CHAN_R][j * rgbRowBytes[AVIF_CHAN_R]];
        uint8_t * ptrG = &rgbPlanes[AVIF_CHAN_G][j * rgbRowBytes[AVIF_CHAN_G]];
        uint8_t * ptrB = &rgbPlanes[AVIF_CHAN_B][j * rgbRowBytes[AVIF_CHAN_B]];

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
            const float Y = (float)unormY / maxChannel;
            const float Cb = ((float)unormU / maxChannel) - 0.5f;
            const float Cr = ((float)unormV / maxChannel) - 0.5f;

            float R = Y + (2 * (1 - kr)) * Cr;
            float B = Y + (2 * (1 - kb)) * Cb;
            float G = Y - ((2 * ((kr * (1 - kr) * Cr) + (kb * (1 - kb) * Cb))) / kg);
            R = AVIF_CLAMP(R, 0.0f, 1.0f);
            G = AVIF_CLAMP(G, 0.0f, 1.0f);
            B = AVIF_CLAMP(B, 0.0f, 1.0f);

            ptrR[i] = (uint8_t)(0.5f + (R * maxChannel));
            ptrG[i] = (uint8_t)(0.5f + (G * maxChannel));
            ptrB[i] = (uint8_t)(0.5f + (B * maxChannel));
        }
    }
    return AVIF_RESULT_OK;
}

static avifResult avifImageYUVToRGB8Mono(avifImage * image, avifReformatState * state)
{
    const float kr = state->kr;
    const float kg = state->kg;
    const float kb = state->kb;

    float maxChannel = (float)((1 << image->depth) - 1);
    uint8_t ** rgbPlanes = image->rgbPlanes;
    uint32_t * rgbRowBytes = image->rgbRowBytes;
    for (uint32_t j = 0; j < image->height; ++j) {
        uint8_t * ptrY = &image->yuvPlanes[AVIF_CHAN_Y][(j * image->yuvRowBytes[AVIF_CHAN_Y])];
        uint8_t * ptrR = &rgbPlanes[AVIF_CHAN_R][j * rgbRowBytes[AVIF_CHAN_R]];
        uint8_t * ptrG = &rgbPlanes[AVIF_CHAN_G][j * rgbRowBytes[AVIF_CHAN_G]];
        uint8_t * ptrB = &rgbPlanes[AVIF_CHAN_B][j * rgbRowBytes[AVIF_CHAN_B]];

        for (uint32_t i = 0; i < image->width; ++i) {
            // Unpack YUV into unorm
            uint32_t unormY = ptrY[i];

            // adjust for limited/full color range, if need be
            if (image->yuvRange == AVIF_RANGE_LIMITED) {
                unormY = avifLimitedToFullY(image->depth, unormY);
            }

            // Convert unorm to float
            const float Y = (float)unormY / maxChannel;
            const float Cb = 0.0f;
            const float Cr = 0.0f;

            float R = Y + (2 * (1 - kr)) * Cr;
            float B = Y + (2 * (1 - kb)) * Cb;
            float G = Y - ((2 * ((kr * (1 - kr) * Cr) + (kb * (1 - kb) * Cb))) / kg);
            R = AVIF_CLAMP(R, 0.0f, 1.0f);
            G = AVIF_CLAMP(G, 0.0f, 1.0f);
            B = AVIF_CLAMP(B, 0.0f, 1.0f);

            ptrR[i] = (uint8_t)(0.5f + (R * maxChannel));
            ptrG[i] = (uint8_t)(0.5f + (G * maxChannel));
            ptrB[i] = (uint8_t)(0.5f + (B * maxChannel));
        }
    }
    return AVIF_RESULT_OK;
}

avifResult avifImageYUVToRGB(avifImage * image)
{
    if (!image->yuvPlanes[AVIF_CHAN_Y]) {
        return AVIF_RESULT_REFORMAT_FAILED;
    }

    avifReformatState state;
    if (!avifPrepareReformatState(image, &state)) {
        return AVIF_RESULT_REFORMAT_FAILED;
    }

    avifImageAllocatePlanes(image, AVIF_PLANES_RGB);

    if (state.usesU16) {
        if (image->yuvRowBytes[AVIF_CHAN_U] && image->yuvRowBytes[AVIF_CHAN_V]) {
            return avifImageYUVToRGB16Color(image, &state);
        }
        return avifImageYUVToRGB16Mono(image, &state);
    }

    if (image->yuvRowBytes[AVIF_CHAN_U] && image->yuvRowBytes[AVIF_CHAN_V]) {
        return avifImageYUVToRGB8Color(image, &state);
    }
    return avifImageYUVToRGB8Mono(image, &state);
}

int avifLimitedToFullY(int depth, int v)
{
    switch (depth) {
        case 8:
            v = ((v - 16) * 255) / (235 - 16);
            v = AVIF_CLAMP(v, 0, 255);
            return v;
        case 10:
            v = ((v - 64) * 1023) / (940 - 64);
            v = AVIF_CLAMP(v, 0, 1023);
            return v;
        case 12:
            v = ((v - 256) * 4095) / (3760 - 256);
            v = AVIF_CLAMP(v, 0, 4095);
            return v;
    }
    return v;
}

int avifLimitedToFullUV(int depth, int v)
{
    switch (depth) {
        case 8:
            v = ((v - 16) * 255) / (240 - 16);
            v = AVIF_CLAMP(v, 0, 255);
            return v;
        case 10:
            v = ((v - 64) * 1023) / (960 - 64);
            v = AVIF_CLAMP(v, 0, 1023);
            return v;
        case 12:
            v = ((v - 256) * 4095) / (3840 - 256);
            v = AVIF_CLAMP(v, 0, 4095);
            return v;
    }
    return v;
}

int avifFullToLimitedY(int depth, int v)
{
    switch (depth) {
        case 8:
            v = ((v * (235 - 16)) / 255) + 16;
            v = AVIF_CLAMP(v, 16, 235);
            return v;
        case 10:
            v = ((v * (940 - 64)) / 1023) + 64;
            v = AVIF_CLAMP(v, 64, 940);
            return v;
        case 12:
            v = ((v * (3760 - 256)) / 4095) + 256;
            v = AVIF_CLAMP(v, 256, 3760);
            return v;
    }
    return v;
}

int avifFullToLimitedUV(int depth, int v)
{
    switch (depth) {
        case 8:
            v = ((v * (240 - 16)) / 255) + 16;
            v = AVIF_CLAMP(v, 16, 240);
            return v;
        case 10:
            v = ((v * (960 - 64)) / 1023) + 64;
            v = AVIF_CLAMP(v, 64, 960);
            return v;
        case 12:
            v = ((v * (3840 - 256)) / 4095) + 256;
            v = AVIF_CLAMP(v, 256, 3840);
            return v;
    }
    return v;
}
