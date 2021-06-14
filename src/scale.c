// Copyright 2021 Joe Drago. All rights reserved.
// SPDX-License-Identifier: BSD-2-Clause

#include "avif/internal.h"

#if !defined(AVIF_LIBYUV_ENABLED)

avifBool avifImageScale(avifImage * image, uint32_t dstWidth, uint32_t dstHeight, avifDiagnostics * diag)
{
    (void)image;
    (void)dstWidth;
    (void)dstHeight;
    avifDiagnosticsPrintf(diag, "avifImageScale() called, but is unimplemented without libyuv!");
    return AVIF_FALSE;
}

#else

#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wstrict-prototypes" // "this function declaration is not a prototype"
#endif
#include <libyuv.h>
#if defined(__clang__)
#pragma clang diagnostic pop
#endif

// This should be configurable and/or smarter
#define AVIF_LIBYUV_FILTER_MODE kFilterBox

avifBool avifImageScale(avifImage * image, uint32_t dstWidth, uint32_t dstHeight, avifDiagnostics * diag)
{
    if ((image->width == dstWidth) && (image->height == dstHeight)) {
        // Nothing to do
        return AVIF_TRUE;
    }

    if ((dstWidth == 0) || (dstHeight == 0) || (dstWidth > (AVIF_MAX_IMAGE_SIZE / dstHeight))) {
        avifDiagnosticsPrintf(diag, "avifImageScale requested invalid dst dimensions [%ux%u]", dstWidth, dstHeight);
        return AVIF_FALSE;
    }

    uint8_t * srcYUVPlanes[AVIF_PLANE_COUNT_YUV];
    uint32_t srcYUVRowBytes[AVIF_PLANE_COUNT_YUV];
    for (int i = 0; i < AVIF_PLANE_COUNT_YUV; ++i) {
        srcYUVPlanes[i] = image->yuvPlanes[i];
        image->yuvPlanes[i] = NULL;
        srcYUVRowBytes[i] = image->yuvRowBytes[i];
        image->yuvRowBytes[i] = 0;
    }
    const avifBool srcImageOwnsYUVPlanes = image->imageOwnsYUVPlanes;
    image->imageOwnsYUVPlanes = AVIF_FALSE;

    uint8_t * srcAlphaPlane = image->alphaPlane;
    image->alphaPlane = NULL;
    uint32_t srcAlphaRowBytes = image->alphaRowBytes;
    image->alphaRowBytes = 0;
    const avifBool srcImageOwnsAlphaPlane = image->imageOwnsAlphaPlane;
    image->imageOwnsAlphaPlane = AVIF_FALSE;

    const uint32_t srcWidth = image->width;
    image->width = dstWidth;
    const uint32_t srcHeight = image->height;
    image->height = dstHeight;

    if (srcYUVPlanes[0]) {
        avifImageAllocatePlanes(image, AVIF_PLANES_YUV);

        avifPixelFormatInfo formatInfo;
        avifGetPixelFormatInfo(image->yuvFormat, &formatInfo);
        const uint32_t srcUVWidth = (srcWidth + formatInfo.chromaShiftX) >> formatInfo.chromaShiftX;
        const uint32_t srcUVHeight = (srcHeight + formatInfo.chromaShiftY) >> formatInfo.chromaShiftY;
        const uint32_t dstUVWidth = (dstWidth + formatInfo.chromaShiftX) >> formatInfo.chromaShiftX;
        const uint32_t dstUVHeight = (dstHeight + formatInfo.chromaShiftY) >> formatInfo.chromaShiftY;

        for (int i = 0; i < AVIF_PLANE_COUNT_YUV; ++i) {
            if (!srcYUVPlanes[i]) {
                continue;
            }

            const uint32_t srcW = (i == AVIF_CHAN_Y) ? srcWidth : srcUVWidth;
            const uint32_t srcH = (i == AVIF_CHAN_Y) ? srcHeight : srcUVHeight;
            const uint32_t dstW = (i == AVIF_CHAN_Y) ? dstWidth : dstUVWidth;
            const uint32_t dstH = (i == AVIF_CHAN_Y) ? dstHeight : dstUVHeight;
            if (image->depth > 8) {
                uint16_t * srcPlane = (uint16_t *)srcYUVPlanes[i];
                uint16_t * dstPlane = (uint16_t *)image->yuvPlanes[i];
                ScalePlane_12(srcPlane, srcYUVRowBytes[i], srcW, srcH, dstPlane, image->yuvRowBytes[i], dstW, dstH, AVIF_LIBYUV_FILTER_MODE);
            } else {
                uint8_t * srcPlane = srcYUVPlanes[i];
                uint8_t * dstPlane = image->yuvPlanes[i];
                ScalePlane(srcPlane, srcYUVRowBytes[i], srcW, srcH, dstPlane, image->yuvRowBytes[i], dstW, dstH, AVIF_LIBYUV_FILTER_MODE);
            }

            if (srcImageOwnsYUVPlanes) {
                avifFree(srcYUVPlanes[i]);
            }
        }
    }

    if (srcAlphaPlane) {
        avifImageAllocatePlanes(image, AVIF_PLANES_A);

        if (image->depth > 8) {
            uint16_t * srcPlane = (uint16_t *)srcAlphaPlane;
            uint16_t * dstPlane = (uint16_t *)image->alphaPlane;
            ScalePlane_12(srcPlane, srcAlphaRowBytes, srcWidth, srcHeight, dstPlane, image->alphaRowBytes, dstWidth, dstHeight, AVIF_LIBYUV_FILTER_MODE);
        } else {
            uint8_t * srcPlane = srcAlphaPlane;
            uint8_t * dstPlane = image->alphaPlane;
            ScalePlane(srcPlane, srcAlphaRowBytes, srcWidth, srcHeight, dstPlane, image->alphaRowBytes, dstWidth, dstHeight, AVIF_LIBYUV_FILTER_MODE);
        }

        if (srcImageOwnsAlphaPlane) {
            avifFree(srcAlphaPlane);
        }
    }

    return AVIF_TRUE;
}

#endif
