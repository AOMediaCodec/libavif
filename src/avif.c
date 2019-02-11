// Copyright 2019 Joe Drago. All rights reserved.
// SPDX-License-Identifier: BSD-2-Clause

#include "avif/avif.h"

#include <string.h>

void avifGetPixelFormatInfo(avifPixelFormat format, avifPixelFormatInfo * info)
{
    memset(info, 0, sizeof(avifPixelFormatInfo));
    info->aomIndexU = 1;
    info->aomIndexV = 2;

    switch (format) {
        case AVIF_PIXEL_FORMAT_YUV444:
            info->chromaShiftX = 0;
            info->chromaShiftY = 0;
            break;

        case AVIF_PIXEL_FORMAT_YUV422:
            info->chromaShiftX = 1;
            info->chromaShiftY = 0;
            break;

        case AVIF_PIXEL_FORMAT_YUV420:
            info->chromaShiftX = 1;
            info->chromaShiftY = 1;
            break;

        case AVIF_PIXEL_FORMAT_YV12:
            info->chromaShiftX = 1;
            info->chromaShiftY = 1;
            info->aomIndexU = 2;
            info->aomIndexV = 1;
            break;
    }
}

// This function assumes nothing in this struct needs to be freed; use avifImageClear() externally
static void avifImageSetDefaults(avifImage * image)
{
    memset(image, 0, sizeof(avifImage));
    image->yuvRange = AVIF_RANGE_FULL;
}

avifImage * avifImageCreate(int width, int height, int depth, avifPixelFormat yuvFormat)
{
    avifImage * image = (avifImage *)avifAlloc(sizeof(avifImage));
    avifImageSetDefaults(image);
    image->width = width;
    image->height = height;
    image->depth = depth;
    image->yuvFormat = yuvFormat;
    return image;
}

avifImage * avifImageCreateEmpty(void)
{
    return avifImageCreate(0, 0, 0, AVIF_PIXEL_FORMAT_NONE);
}

void avifImageDestroy(avifImage * image)
{
    avifImageFreePlanes(image, AVIF_PLANES_ALL);
    avifRawDataFree(&image->icc);
    avifFree(image);
}

void avifImageSetProfileICC(avifImage * image, uint8_t * icc, size_t iccSize)
{
    if (iccSize) {
        image->profileFormat = AVIF_PROFILE_FORMAT_ICC;
        avifRawDataSet(&image->icc, icc, iccSize);
    } else {
        image->profileFormat = AVIF_PROFILE_FORMAT_NONE;
        avifRawDataFree(&image->icc);
    }
}

void avifImageAllocatePlanes(avifImage * image, uint32_t planes)
{
    int channelSize = avifImageUsesU16(image) ? 2 : 1;
    int fullRowBytes = channelSize * image->width;
    int fullSize = fullRowBytes * image->height;
    if (planes & AVIF_PLANES_RGB) {
        if (!image->rgbPlanes[AVIF_CHAN_R]) {
            image->rgbRowBytes[AVIF_CHAN_R] = fullRowBytes;
            image->rgbPlanes[AVIF_CHAN_R] = avifAlloc(fullSize);
            memset(image->rgbPlanes[AVIF_CHAN_R], 0, fullSize);
        }
        if (!image->rgbPlanes[AVIF_CHAN_G]) {
            image->rgbRowBytes[AVIF_CHAN_G] = fullRowBytes;
            image->rgbPlanes[AVIF_CHAN_G] = avifAlloc(fullSize);
            memset(image->rgbPlanes[AVIF_CHAN_G], 0, fullSize);
        }
        if (!image->rgbPlanes[AVIF_CHAN_B]) {
            image->rgbRowBytes[AVIF_CHAN_B] = fullRowBytes;
            image->rgbPlanes[AVIF_CHAN_B] = avifAlloc(fullSize);
            memset(image->rgbPlanes[AVIF_CHAN_B], 0, fullSize);
        }
    }
    if ((planes & AVIF_PLANES_YUV) && (image->yuvFormat != AVIF_PIXEL_FORMAT_NONE)) {
        avifPixelFormatInfo info;
        avifGetPixelFormatInfo(image->yuvFormat, &info);

        int shiftedW = (image->width >> info.chromaShiftX);
        int shiftedH = (image->height >> info.chromaShiftY);
        shiftedW = shiftedW ? shiftedW : 1;
        shiftedH = shiftedH ? shiftedH : 1;

        int uvRowBytes = channelSize * shiftedW;
        int uvSize = uvRowBytes * shiftedH;

        if (!image->yuvPlanes[AVIF_CHAN_Y]) {
            image->yuvRowBytes[AVIF_CHAN_Y] = fullRowBytes;
            image->yuvPlanes[AVIF_CHAN_Y] = avifAlloc(fullSize);
            memset(image->yuvPlanes[AVIF_CHAN_Y], 0, fullSize);
        }
        if (!image->yuvPlanes[AVIF_CHAN_U]) {
            image->yuvRowBytes[AVIF_CHAN_U] = uvRowBytes;
            image->yuvPlanes[AVIF_CHAN_U] = avifAlloc(uvSize);
            memset(image->yuvPlanes[AVIF_CHAN_U], 0, uvSize);
        }
        if (!image->yuvPlanes[AVIF_CHAN_V]) {
            image->yuvRowBytes[AVIF_CHAN_V] = uvRowBytes;
            image->yuvPlanes[AVIF_CHAN_V] = avifAlloc(uvSize);
            memset(image->yuvPlanes[AVIF_CHAN_V], 0, uvSize);
        }
    }
    if (planes & AVIF_PLANES_A) {
        if (!image->alphaPlane) {
            image->alphaRowBytes = fullRowBytes;
            image->alphaPlane = avifAlloc(fullRowBytes * image->height);
            memset(image->alphaPlane, 0, fullRowBytes * image->height);
        }
    }
}

void avifImageFreePlanes(avifImage * image, uint32_t planes)
{
    if (planes & AVIF_PLANES_RGB) {
        avifFree(image->rgbPlanes[AVIF_CHAN_R]);
        image->rgbPlanes[AVIF_CHAN_R] = NULL;
        image->rgbRowBytes[AVIF_CHAN_R] = 0;
        avifFree(image->rgbPlanes[AVIF_CHAN_G]);
        image->rgbPlanes[AVIF_CHAN_G] = NULL;
        image->rgbRowBytes[AVIF_CHAN_G] = 0;
        avifFree(image->rgbPlanes[AVIF_CHAN_G]);
        image->rgbPlanes[AVIF_CHAN_G] = NULL;
        image->rgbRowBytes[AVIF_CHAN_G] = 0;
    }
    if ((planes & AVIF_PLANES_YUV) && (image->yuvFormat != AVIF_PIXEL_FORMAT_NONE)) {
        avifFree(image->yuvPlanes[AVIF_CHAN_Y]);
        image->yuvPlanes[AVIF_CHAN_Y] = NULL;
        image->yuvRowBytes[AVIF_CHAN_Y] = 0;
        avifFree(image->yuvPlanes[AVIF_CHAN_U]);
        image->yuvPlanes[AVIF_CHAN_U] = NULL;
        image->yuvRowBytes[AVIF_CHAN_U] = 0;
        avifFree(image->yuvPlanes[AVIF_CHAN_V]);
        image->yuvPlanes[AVIF_CHAN_V] = NULL;
        image->yuvRowBytes[AVIF_CHAN_V] = 0;
    }
    if (planes & AVIF_PLANES_A) {
        avifFree(image->alphaPlane);
        image->alphaPlane = NULL;
        image->alphaRowBytes = 0;
    }
}

avifBool avifImageUsesU16(avifImage * image)
{
    return (image->depth > 8) ? AVIF_TRUE : AVIF_FALSE;
}
