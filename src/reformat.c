// Copyright 2019 Joe Drago. All rights reserved.
// SPDX-License-Identifier: BSD-2-Clause

#include "avif/avif.h"

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

static avifBool avifPrepareReformatState(avifImage * image, avifReformatState * state)
{
    if (image->yuvFormat == AVIF_PIXEL_FORMAT_NONE) {
        return AVIF_FALSE;
    }
    avifGetPixelFormatInfo(image->yuvFormat, &state->formatInfo);

    // TODO: calculate coefficients
    state->kr = 0.2126f;
    state->kb = 0.0722f;
    state->kg = 1.0f - state->kr - state->kb;

    state->usesU16 = avifImageUsesU16(image);
    return AVIF_TRUE;
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

    float yuvPixel[3];
    float rgbPixel[3];
    float maxChannel = (float)((1 << image->depth) - 1);
    for (int j = 0; j < image->height; ++j) {
        for (int i = 0; i < image->width; ++i) {
            // Unpack RGB into normalized float
            if (state.usesU16) {
                rgbPixel[0] = *((uint16_t *)(&image->rgbPlanes[AVIF_CHAN_R][(i * 2) + (j * image->rgbRowBytes[AVIF_CHAN_R])])) / maxChannel;
                rgbPixel[1] = *((uint16_t *)(&image->rgbPlanes[AVIF_CHAN_G][(i * 2) + (j * image->rgbRowBytes[AVIF_CHAN_G])])) / maxChannel;
                rgbPixel[2] = *((uint16_t *)(&image->rgbPlanes[AVIF_CHAN_B][(i * 2) + (j * image->rgbRowBytes[AVIF_CHAN_B])])) / maxChannel;
            } else {
                rgbPixel[0] = image->rgbPlanes[AVIF_CHAN_R][i + (j * image->rgbRowBytes[AVIF_CHAN_R])] / maxChannel;
                rgbPixel[1] = image->rgbPlanes[AVIF_CHAN_G][i + (j * image->rgbRowBytes[AVIF_CHAN_G])] / maxChannel;
                rgbPixel[2] = image->rgbPlanes[AVIF_CHAN_B][i + (j * image->rgbRowBytes[AVIF_CHAN_B])] / maxChannel;
            }

            // RGB -> YUV conversion
            float Y = (kr * rgbPixel[0]) + (kg * rgbPixel[1]) + (kb * rgbPixel[2]);
            yuvPixel[0] = Y;
            yuvPixel[1] = (rgbPixel[2] - Y) / (2 * (1 - kb));
            yuvPixel[2] = (rgbPixel[0] - Y) / (2 * (1 - kr));

            // Stuff YUV into unorm16 color layer
            yuvPixel[0] = AVIF_CLAMP(yuvPixel[0], 0.0f, 1.0f);
            yuvPixel[1] += 0.5f;
            yuvPixel[1] = AVIF_CLAMP(yuvPixel[1], 0.0f, 1.0f);
            yuvPixel[2] += 0.5f;
            yuvPixel[2] = AVIF_CLAMP(yuvPixel[2], 0.0f, 1.0f);

            int uvI = i >> state.formatInfo.chromaShiftX;
            int uvJ = j >> state.formatInfo.chromaShiftY;
            if (state.usesU16) {
                uint16_t * pY = (uint16_t *)&image->yuvPlanes[AVIF_CHAN_Y][(i * 2) + (j * image->yuvRowBytes[AVIF_CHAN_Y])];
                *pY = (uint16_t)avifRoundf(yuvPixel[0] * maxChannel);
                uint16_t * pU = (uint16_t *)&image->yuvPlanes[AVIF_CHAN_U][(uvI * 2) + (uvJ * image->yuvRowBytes[AVIF_CHAN_U])];
                *pU = (uint16_t)avifRoundf(yuvPixel[1] * maxChannel);
                uint16_t * pV = (uint16_t *)&image->yuvPlanes[AVIF_CHAN_V][(uvI * 2) + (uvJ * image->yuvRowBytes[AVIF_CHAN_V])];
                *pV = (uint16_t)avifRoundf(yuvPixel[2] * maxChannel);
            } else {
                image->yuvPlanes[AVIF_CHAN_Y][i + (j * image->yuvRowBytes[AVIF_CHAN_Y])] = (uint8_t)avifRoundf(yuvPixel[0] * maxChannel);
                image->yuvPlanes[AVIF_CHAN_U][uvI + (uvJ * image->yuvRowBytes[AVIF_CHAN_U])] = (uint8_t)avifRoundf(yuvPixel[1] * maxChannel);
                image->yuvPlanes[AVIF_CHAN_V][uvI + (uvJ * image->yuvRowBytes[AVIF_CHAN_V])] = (uint8_t)avifRoundf(yuvPixel[2] * maxChannel);
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

    float yuvPixel[3];
    float rgbPixel[3];
    float maxChannel = (float)((1 << image->depth) - 1);
    for (int j = 0; j < image->height; ++j) {
        for (int i = 0; i < image->width; ++i) {
            // Unpack YUV into normalized float
            int uvI = i >> state.formatInfo.chromaShiftX;
            int uvJ = j >> state.formatInfo.chromaShiftY;
            if (state.usesU16) {
                yuvPixel[0] = *((uint16_t *)&image->yuvPlanes[AVIF_CHAN_Y][(i * 2) + (j * image->yuvRowBytes[AVIF_CHAN_Y])]) / maxChannel;
                yuvPixel[1] = *((uint16_t *)&image->yuvPlanes[AVIF_CHAN_U][(uvI * 2) + (uvJ * image->yuvRowBytes[AVIF_CHAN_U])]) / maxChannel;
                yuvPixel[2] = *((uint16_t *)&image->yuvPlanes[AVIF_CHAN_V][(uvI * 2) + (uvJ * image->yuvRowBytes[AVIF_CHAN_V])]) / maxChannel;
            } else {
                yuvPixel[0] = image->yuvPlanes[AVIF_CHAN_Y][i + (j * image->yuvRowBytes[AVIF_CHAN_Y])] / maxChannel;
                yuvPixel[1] = image->yuvPlanes[AVIF_CHAN_U][uvI + (uvJ * image->yuvRowBytes[AVIF_CHAN_U])] / maxChannel;
                yuvPixel[2] = image->yuvPlanes[AVIF_CHAN_V][uvI + (uvJ * image->yuvRowBytes[AVIF_CHAN_V])] / maxChannel;
            }
            yuvPixel[1] -= 0.5f;
            yuvPixel[2] -= 0.5f;

            float Y  = yuvPixel[0];
            float Cb = yuvPixel[1];
            float Cr = yuvPixel[2];

            float R = Y + (2 * (1 - kr)) * Cr;
            float B = Y + (2 * (1 - kb)) * Cb;
            float G = Y - (
                (2 * ((kr * (1 - kr) * Cr) + (kb * (1 - kb) * Cb)))
                /
                kg);

            rgbPixel[0] = AVIF_CLAMP(R, 0.0f, 1.0f);
            rgbPixel[1] = AVIF_CLAMP(G, 0.0f, 1.0f);
            rgbPixel[2] = AVIF_CLAMP(B, 0.0f, 1.0f);

            if (state.usesU16) {
                uint16_t * pR = (uint16_t *)&image->rgbPlanes[AVIF_CHAN_R][(i * 2) + (j * image->rgbRowBytes[AVIF_CHAN_R])];
                *pR = (uint16_t)avifRoundf(rgbPixel[0] * maxChannel);
                uint16_t * pG = (uint16_t *)&image->rgbPlanes[AVIF_CHAN_G][(i * 2) + (j * image->rgbRowBytes[AVIF_CHAN_G])];
                *pG = (uint16_t)avifRoundf(rgbPixel[1] * maxChannel);
                uint16_t * pB = (uint16_t *)&image->rgbPlanes[AVIF_CHAN_B][(i * 2) + (j * image->rgbRowBytes[AVIF_CHAN_B])];
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
