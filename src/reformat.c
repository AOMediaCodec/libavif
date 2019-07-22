// Copyright 2019 Joe Drago. All rights reserved.
// SPDX-License-Identifier: BSD-2-Clause

#include "avif/internal.h"

#include <string.h>

typedef struct avifReformatState
{
    // YUV coefficients
    float kr;
    float kg;
    float kb;

    avifPixelFormatInfo formatInfo;
    avifBool usesU16;
} avifReformatState;

struct YUVBlock
{
    float y;
    float u;
    float v;
};

static avifBool avifPrepareReformatState(avifImage * image, avifReformatState * state)
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
    for (int outerJ = 0; outerJ < image->height; outerJ += 2) {
        for (int outerI = 0; outerI < image->width; outerI += 2) {
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

avifResult avifImageYUVToRGB(avifImage * image)
{
    if (!image->yuvPlanes[AVIF_CHAN_Y] || !image->yuvPlanes[AVIF_CHAN_U] || !image->yuvPlanes[AVIF_CHAN_V]) {
        return AVIF_RESULT_REFORMAT_FAILED;
    }

    avifReformatState state;
    if (!avifPrepareReformatState(image, &state)) {
        return AVIF_RESULT_REFORMAT_FAILED;
    }

    avifImageAllocatePlanes(image, AVIF_PLANES_RGB);

    const float kr = state.kr;
    const float kg = state.kg;
    const float kb = state.kb;

    int yuvUNorm[3];
    float yuvPixel[3];
    float rgbPixel[3];
    float maxChannel = (float)((1 << image->depth) - 1);
    uint8_t ** rgbPlanes = image->rgbPlanes;
    uint32_t * rgbRowBytes = image->rgbRowBytes;
    uint8_t ** yuvPlanes = image->yuvPlanes;
    uint32_t * yuvRowBytes = image->yuvRowBytes;
    for (int j = 0; j < image->height; ++j) {
        for (int i = 0; i < image->width; ++i) {
            // Unpack YUV into unorm
            int uvI = i >> state.formatInfo.chromaShiftX;
            int uvJ = j >> state.formatInfo.chromaShiftY;
            if (state.usesU16) {
                yuvUNorm[0] = *((uint16_t *)&image->yuvPlanes[AVIF_CHAN_Y][(i * 2) + (j * image->yuvRowBytes[AVIF_CHAN_Y])]);
                if (image->yuvRowBytes[AVIF_CHAN_U]) {
                    yuvUNorm[1] = *((uint16_t *)&image->yuvPlanes[AVIF_CHAN_U][(uvI * 2) + (uvJ * image->yuvRowBytes[AVIF_CHAN_U])]);
                } else {
                    yuvUNorm[1] = 0;
                }
                if (image->yuvRowBytes[AVIF_CHAN_V]) {
                    yuvUNorm[2] = *((uint16_t *)&image->yuvPlanes[AVIF_CHAN_V][(uvI * 2) + (uvJ * image->yuvRowBytes[AVIF_CHAN_V])]);
                } else {
                    yuvUNorm[2] = 0;
                }
            } else {
                yuvUNorm[0] = image->yuvPlanes[AVIF_CHAN_Y][i + (j * image->yuvRowBytes[AVIF_CHAN_Y])];
                if (image->yuvRowBytes[AVIF_CHAN_U]) {
                    yuvUNorm[1] = image->yuvPlanes[AVIF_CHAN_U][uvI + (uvJ * image->yuvRowBytes[AVIF_CHAN_U])];
                } else {
                    yuvUNorm[1] = 0;
                }
                if (image->yuvRowBytes[AVIF_CHAN_V]) {
                    yuvUNorm[2] = image->yuvPlanes[AVIF_CHAN_V][uvI + (uvJ * image->yuvRowBytes[AVIF_CHAN_V])];
                } else {
                    yuvUNorm[2] = 0;
                }
            }

            // adjust for limited/full color range, if need be
            if (image->yuvRange == AVIF_RANGE_LIMITED) {
                yuvUNorm[0] = avifLimitedToFullY(image->depth, yuvUNorm[0]);
                yuvUNorm[1] = avifLimitedToFullUV(image->depth, yuvUNorm[1]);
                yuvUNorm[2] = avifLimitedToFullUV(image->depth, yuvUNorm[2]);
            }

            // Convert unorm to float
            yuvPixel[0] = yuvUNorm[0] / maxChannel;
            yuvPixel[1] = yuvUNorm[1] / maxChannel;
            yuvPixel[2] = yuvUNorm[2] / maxChannel;
            yuvPixel[1] -= 0.5f;
            yuvPixel[2] -= 0.5f;

            float Y = yuvPixel[0];
            float Cb = yuvPixel[1];
            float Cr = yuvPixel[2];

            float R = Y + (2 * (1 - kr)) * Cr;
            float B = Y + (2 * (1 - kb)) * Cb;
            float G = Y - ((2 * ((kr * (1 - kr) * Cr) + (kb * (1 - kb) * Cb))) / kg);

            rgbPixel[0] = AVIF_CLAMP(R, 0.0f, 1.0f);
            rgbPixel[1] = AVIF_CLAMP(G, 0.0f, 1.0f);
            rgbPixel[2] = AVIF_CLAMP(B, 0.0f, 1.0f);

            if (state.usesU16) {
                uint16_t * pR = (uint16_t *)&rgbPlanes[AVIF_CHAN_R][(i * 2) + (j * rgbRowBytes[AVIF_CHAN_R])];
                *pR = (uint16_t)avifRoundf(rgbPixel[0] * maxChannel);
                uint16_t * pG = (uint16_t *)&rgbPlanes[AVIF_CHAN_G][(i * 2) + (j * rgbRowBytes[AVIF_CHAN_G])];
                *pG = (uint16_t)avifRoundf(rgbPixel[1] * maxChannel);
                uint16_t * pB = (uint16_t *)&rgbPlanes[AVIF_CHAN_B][(i * 2) + (j * rgbRowBytes[AVIF_CHAN_B])];
                *pB = (uint16_t)avifRoundf(rgbPixel[2] * maxChannel);
            } else {
                image->rgbPlanes[AVIF_CHAN_R][i + (j * image->rgbRowBytes[AVIF_CHAN_R])] = (uint8_t)avifRoundf(rgbPixel[0] * maxChannel);
                image->rgbPlanes[AVIF_CHAN_G][i + (j * image->rgbRowBytes[AVIF_CHAN_G])] = (uint8_t)avifRoundf(rgbPixel[1] * maxChannel);
                image->rgbPlanes[AVIF_CHAN_B][i + (j * image->rgbRowBytes[AVIF_CHAN_B])] = (uint8_t)avifRoundf(rgbPixel[2] * maxChannel);
            }
        }
    }
    return AVIF_RESULT_OK;
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
