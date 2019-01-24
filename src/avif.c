// Copyright 2019 Joe Drago. All rights reserved.
// SPDX-License-Identifier: BSD-2-Clause

#include "avif/avif.h"

#include <string.h>

static void avifImageClearPlanes(avifImage * image)
{
    for (int plane = 0; plane < AVIF_MAX_PLANES; ++plane) {
        if (image->planes[plane]) {
            avifFree(image->planes[plane]);
            image->planes[plane] = NULL;
        }
        image->strides[plane] = 0;
    }
}

// This function assumes nothing in this struct needs to be freed; use avifImageClear() externally
static void avifImageSetDefaults(avifImage * image)
{
    memset(image, 0, sizeof(avifImage));
}

avifImage * avifImageCreate()
{
    avifImage * image = (avifImage *)avifAlloc(sizeof(avifImage));
    avifImageSetDefaults(image);
    return image;
}

void avifImageDestroy(avifImage * image)
{
    avifImageClear(image);
    avifFree(image);
}

void avifImageClear(avifImage * image)
{
    avifImageClearPlanes(image);
    avifImageSetDefaults(image);
}

void avifImageCreatePixels(avifImage * image, avifPixelFormat pixelFormat, int width, int height, int depth)
{
    avifImageClearPlanes(image);

    switch (pixelFormat) {
        case AVIF_PIXEL_FORMAT_NONE:
            image->width = 0;
            image->height = 0;
            image->depth = 0;
            break;

        case AVIF_PIXEL_FORMAT_RGBA:
        case AVIF_PIXEL_FORMAT_YUV444:
            image->width = width;
            image->height = height;
            image->depth = depth;
            image->strides[0] = width;
            image->strides[1] = width;
            image->strides[2] = width;
            image->strides[3] = width;
            break;

        case AVIF_PIXEL_FORMAT_YUV420:
            image->width = width;
            image->height = height;
            image->depth = depth;
            image->strides[0] = width;
            image->strides[1] = width >> 1;
            image->strides[2] = width >> 1;
            image->strides[3] = width;
            break;
    }
    image->pixelFormat = pixelFormat;

    for (int plane = 0; plane < AVIF_MAX_PLANES; ++plane) {
        if (image->strides[plane]) {
            image->planes[plane] = avifAlloc(sizeof(uint16_t) * image->strides[plane] * image->height);
        }
    }
}
