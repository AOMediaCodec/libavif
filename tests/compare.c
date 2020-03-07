// Copyright 2020 Joe Drago. All rights reserved.
// SPDX-License-Identifier: BSD-2-Clause

#include "compare.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

avifBool compareYUVA(ImageComparison * ic, const avifImage * image1, const avifImage * image2)
{
    memset(ic, 0, sizeof(ImageComparison));
    if (!image1 || !image2) {
        return AVIF_FALSE;
    }
    if ((image1->width != image2->width) || (image1->height != image2->height) || (image1->depth != image2->depth) ||
        (image1->yuvFormat != image2->yuvFormat) || (image1->yuvRange != image2->yuvRange)) {
        return AVIF_FALSE;
    }

    avifPixelFormatInfo formatInfo;
    avifGetPixelFormatInfo(image1->yuvFormat, &formatInfo);
    unsigned int uvW = image1->width >> formatInfo.chromaShiftX;
    if (uvW < 1) {
        uvW = 1;
    }
    unsigned int uvH = image1->height >> formatInfo.chromaShiftY;
    if (uvH < 1) {
        uvH = 1;
    }

    if (image1->depth == 8) {
        // uint8_t path
        uint8_t maxChannel = 255;

        for (unsigned int j = 0; j < image1->height; ++j) {
            for (unsigned int i = 0; i < image1->width; ++i) {
                uint8_t * Y1 = &image1->yuvPlanes[AVIF_CHAN_Y][i + (j * image1->yuvRowBytes[AVIF_CHAN_Y])];
                uint8_t * Y2 = &image2->yuvPlanes[AVIF_CHAN_Y][i + (j * image2->yuvRowBytes[AVIF_CHAN_Y])];

                int diffY = abs(*Y1 - *Y2);
                if (ic->maxDiffY < diffY) {
                    ic->maxDiffY = diffY;
                }
                ic->avgDiffY += (float)diffY;

                uint8_t A1 = maxChannel;
                if (image1->alphaPlane) {
                    A1 = image1->alphaPlane[i + (j * image1->alphaRowBytes)];
                }
                uint8_t A2 = maxChannel;
                if (image2->alphaPlane) {
                    A2 = image2->alphaPlane[i + (j * image1->alphaRowBytes)];
                }

                int aDiff = abs(A1 - A2);
                if (ic->maxDiffA < aDiff) {
                    ic->maxDiffA = aDiff;
                }
                ic->avgDiffA += (float)aDiff;
            }
        }

        for (unsigned int j = 0; j < uvH; ++j) {
            for (unsigned int i = 0; i < uvW; ++i) {
                uint8_t * U1 = &image1->yuvPlanes[AVIF_CHAN_U][i + (j * image1->yuvRowBytes[AVIF_CHAN_U])];
                uint8_t * U2 = &image2->yuvPlanes[AVIF_CHAN_U][i + (j * image2->yuvRowBytes[AVIF_CHAN_U])];
                int diffU = abs(*U1 - *U2);
                if (ic->maxDiffU < diffU) {
                    ic->maxDiffU = diffU;
                }
                ic->avgDiffU += (float)diffU;

                uint8_t * V1 = &image1->yuvPlanes[AVIF_CHAN_V][i + (j * image1->yuvRowBytes[AVIF_CHAN_V])];
                uint8_t * V2 = &image2->yuvPlanes[AVIF_CHAN_V][i + (j * image2->yuvRowBytes[AVIF_CHAN_V])];
                int diffV = abs(*V1 - *V2);
                if (ic->maxDiffV < diffV) {
                    ic->maxDiffV = diffV;
                }
                ic->avgDiffV += (float)diffV;
            }
        }
    } else {
        // uint16_t path

        uint16_t maxChannel = (uint16_t)((1 << image1->depth) - 1);
        for (unsigned int j = 0; j < image1->height; ++j) {
            for (unsigned int i = 0; i < image1->width; ++i) {
                uint16_t * Y1 =
                    (uint16_t *)&image1->yuvPlanes[AVIF_CHAN_Y][(i * sizeof(uint16_t)) + (j * image1->yuvRowBytes[AVIF_CHAN_Y])];
                uint16_t * Y2 =
                    (uint16_t *)&image2->yuvPlanes[AVIF_CHAN_Y][(i * sizeof(uint16_t)) + (j * image2->yuvRowBytes[AVIF_CHAN_Y])];

                int diffY = abs(*Y1 - *Y2);
                if (ic->maxDiffY < diffY) {
                    ic->maxDiffY = diffY;
                }
                ic->avgDiffY += (float)diffY;

                uint16_t A1 = maxChannel;
                if (image1->alphaPlane) {
                    A1 = *((uint16_t *)&image1->alphaPlane[(i * sizeof(uint16_t)) + (j * image1->alphaRowBytes)]);
                }
                uint16_t A2 = maxChannel;
                if (image2->alphaPlane) {
                    A2 = *((uint16_t *)&image2->alphaPlane[(i * sizeof(uint16_t)) + (j * image2->alphaRowBytes)]);
                }

                int aDiff = abs(A1 - A2);
                if (ic->maxDiffA < aDiff) {
                    ic->maxDiffA = aDiff;
                }
                ic->avgDiffA += (float)aDiff;
            }
        }

        for (unsigned int j = 0; j < uvH; ++j) {
            for (unsigned int i = 0; i < uvW; ++i) {
                uint16_t * U1 =
                    (uint16_t *)&image1->yuvPlanes[AVIF_CHAN_U][(i * sizeof(uint16_t)) + (j * image1->yuvRowBytes[AVIF_CHAN_U])];
                uint16_t * U2 =
                    (uint16_t *)&image2->yuvPlanes[AVIF_CHAN_U][(i * sizeof(uint16_t)) + (j * image2->yuvRowBytes[AVIF_CHAN_U])];
                int diffU = abs(*U1 - *U2);
                if (ic->maxDiffU < diffU) {
                    ic->maxDiffU = diffU;
                }
                ic->avgDiffU += (float)diffU;

                uint16_t * V1 =
                    (uint16_t *)&image1->yuvPlanes[AVIF_CHAN_V][(i * sizeof(uint16_t)) + (j * image1->yuvRowBytes[AVIF_CHAN_V])];
                uint16_t * V2 =
                    (uint16_t *)&image2->yuvPlanes[AVIF_CHAN_V][(i * sizeof(uint16_t)) + (j * image2->yuvRowBytes[AVIF_CHAN_V])];
                int diffV = abs(*V1 - *V2);
                if (ic->maxDiffV < diffV) {
                    ic->maxDiffV = diffV;
                }
                ic->avgDiffV += (float)diffV;
            }
        }
    }

    float totalPixels = (float)(image1->width * image1->height);
    ic->avgDiffY /= totalPixels;
    ic->avgDiffU /= totalPixels;
    ic->avgDiffV /= totalPixels;
    ic->avgDiffA /= totalPixels;

    ic->maxDiff = ic->maxDiffY;
    if (ic->maxDiff < ic->maxDiffU) {
        ic->maxDiff = ic->maxDiffU;
    }
    if (ic->maxDiff < ic->maxDiffV) {
        ic->maxDiff = ic->maxDiffV;
    }
    if (ic->maxDiff < ic->maxDiffA) {
        ic->maxDiff = ic->maxDiffA;
    }

    ic->avgDiff = (ic->avgDiffY + ic->avgDiffU + ic->avgDiffV + ic->avgDiffA) / 4.0f;
    return AVIF_TRUE;
}
