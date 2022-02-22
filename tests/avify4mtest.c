// Copyright 2022 Google LLC. All rights reserved.
// SPDX-License-Identifier: BSD-2-Clause

#include "avif/avif.h"

#include "y4m.h"

#include <assert.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

//------------------------------------------------------------------------------

// Returns true if image1 and image2 are identical.
static avifBool compareYUVA(const avifImage * image1, const avifImage * image2)
{
    if (image1->width != image2->width || image1->height != image2->height || image1->depth != image2->depth ||
        image1->yuvFormat != image2->yuvFormat || image1->yuvRange != image2->yuvRange) {
        printf("ERROR: input mismatch\n");
        return AVIF_FALSE;
    }
    assert(image1->width * image1->height > 0);

    avifPixelFormatInfo formatInfo;
    avifGetPixelFormatInfo(image1->yuvFormat, &formatInfo);
    const uint32_t uvWidth = (image1->width + formatInfo.chromaShiftX) >> formatInfo.chromaShiftX;
    const uint32_t uvHeight = (image1->height + formatInfo.chromaShiftY) >> formatInfo.chromaShiftY;

    const int planeCount = formatInfo.monochrome ? 1 : AVIF_PLANE_COUNT_YUV;
    for (int plane = 0; plane < planeCount; ++plane) {
        const uint32_t widthByteCount =
            ((plane == AVIF_CHAN_Y) ? image1->width : uvWidth) * ((image1->depth > 8) ? sizeof(uint16_t) : sizeof(uint8_t));
        const uint32_t height = (plane == AVIF_CHAN_Y) ? image1->height : uvHeight;
        const uint8_t * row1 = image1->yuvPlanes[plane];
        const uint8_t * row2 = image2->yuvPlanes[plane];
        for (uint32_t y = 0; y < height; ++y) {
            if (memcmp(row1, row2, widthByteCount) != 0) {
                printf("ERROR: different px at row %" PRIu32 ", channel %" PRIu32 "\n", y, plane);
                return AVIF_FALSE;
            }
            row1 += image1->yuvRowBytes[plane];
            row2 += image2->yuvRowBytes[plane];
        }
    }

    if (image1->alphaPlane != NULL || image2->alphaPlane != NULL) {
        if (image1->alphaPlane == NULL || image2->alphaPlane == NULL || image1->alphaRange != image2->alphaRange ||
            image1->alphaPremultiplied != image2->alphaPremultiplied) {
            printf("ERROR: input mismatch\n");
            return AVIF_FALSE;
        }
        const uint32_t widthByteCount = image1->width * ((image1->depth > 8) ? sizeof(uint16_t) : sizeof(uint8_t));
        const uint8_t * row1 = image1->alphaPlane;
        const uint8_t * row2 = image2->alphaPlane;
        for (uint32_t y = 0; y < image1->height; ++y) {
            if (memcmp(row1, row2, widthByteCount) != 0) {
                printf("ERROR: different px at row %" PRIu32 ", alpha\n", y);
                return AVIF_FALSE;
            }
            row1 += image1->alphaRowBytes;
            row2 += image2->alphaRowBytes;
        }
    }
    return AVIF_TRUE;
}

//------------------------------------------------------------------------------

// Fills each plane of the image with the maximum allowed value.
static void fillPlanes(avifImage * image)
{
    const uint16_t yuvValue = (image->yuvRange == AVIF_RANGE_LIMITED) ? (235 << (image->depth - 8)) : ((1 << image->depth) - 1);
    avifPixelFormatInfo formatInfo;
    avifGetPixelFormatInfo(image->yuvFormat, &formatInfo);
    const int planeCount = formatInfo.monochrome ? 1 : AVIF_PLANE_COUNT_YUV;
    for (int plane = 0; plane < planeCount; ++plane) {
        if (image->yuvPlanes[plane] != NULL) {
            const uint32_t planeWidth =
                (plane == AVIF_CHAN_Y) ? image->width : ((image->width + formatInfo.chromaShiftX) >> formatInfo.chromaShiftX);
            const uint32_t planeHeight =
                (plane == AVIF_CHAN_Y) ? image->height : ((image->height + formatInfo.chromaShiftY) >> formatInfo.chromaShiftY);
            for (uint32_t y = 0; y < planeHeight; ++y) {
                uint8_t * const row = image->yuvPlanes[plane] + y * image->yuvRowBytes[plane];
                if (image->depth == 8) {
                    memset(row, yuvValue, planeWidth);
                } else {
                    for (uint32_t x = 0; x < planeWidth; ++x) {
                        ((uint16_t *)row)[x] = yuvValue;
                    }
                }
            }
        }
    }
    if (image->alphaPlane != NULL) {
        assert(image->alphaRange == AVIF_RANGE_FULL);
        const uint16_t alphaValue = (1 << image->depth) - 1;
        for (uint32_t y = 0; y < image->height; ++y) {
            uint8_t * const row = image->alphaPlane + y * image->alphaRowBytes;
            if (image->depth == 8) {
                memset(row, alphaValue, image->width);
            } else {
                for (uint32_t x = 0; x < image->width; ++x) {
                    ((uint16_t *)row)[x] = alphaValue;
                }
            }
        }
    }
}

// Creates an image and encodes then decodes it as a y4m file.
static avifBool encodeDecodeY4m(uint32_t width,
                                uint32_t height,
                                uint32_t depth,
                                avifPixelFormat yuvFormat,
                                avifRange yuvRange,
                                avifBool createAlpha,
                                const char filePath[])
{
    avifBool success = AVIF_FALSE;
    avifImage * image = avifImageCreateEmpty();
    avifImage * decoded = avifImageCreateEmpty();
    if (image == NULL || decoded == NULL) {
        printf("ERROR: avifImageCreate() failed\n");
        goto cleanup;
    }
    image->width = width;
    image->height = height;
    image->depth = depth;
    image->yuvFormat = yuvFormat;
    image->yuvRange = yuvRange;
    image->alphaRange = AVIF_RANGE_FULL; // Unused by y4mRead() and y4mWrite().
    avifImageAllocatePlanes(image, createAlpha ? AVIF_PLANES_ALL : AVIF_PLANES_YUV);
    fillPlanes(image);

    if (!y4mWrite(filePath, image)) {
        printf("ERROR: y4mWrite() failed\n");
        goto cleanup;
    }
    if (!y4mRead(filePath, decoded, /*sourceTiming=*/NULL, /*iter=*/NULL)) {
        printf("ERROR: y4mRead() failed\n");
        goto cleanup;
    }

    if (!compareYUVA(image, decoded)) {
        goto cleanup;
    }

    success = AVIF_TRUE;
cleanup:
    if (image != NULL) {
        avifImageDestroy(image);
    }
    if (decoded != NULL) {
        avifImageDestroy(decoded);
    }
    return success;
}

//------------------------------------------------------------------------------

int main(int argc, char * argv[])
{
    if (argc != 2 || !strlen(argv[1])) {
        fprintf(stderr, "Missing temporary directory path environment variable name argument\n");
        return EXIT_FAILURE;
    }
    const char * testTmpdir = getenv(argv[1]);
    if (testTmpdir == NULL || !strlen(testTmpdir)) {
        fprintf(stderr, "The environment variable %s is missing or is an empty string\n", argv[1]);
        return EXIT_FAILURE;
    }
    char filePath[256];
    const int result = snprintf(filePath, sizeof(filePath), "%s/avify4mtest.y4m", testTmpdir);
    if (result < 0 || result >= (int)sizeof(filePath)) {
        fprintf(stderr, "Could not generate a temporary file path\n");
        return EXIT_FAILURE;
    }

    // Try several configurations.
    const uint32_t depths[] = { 8, 10, 12 };
    const uint32_t widths[] = { 1, 2, 3 };
    const uint32_t heights[] = { 1, 2, 3 };
    for (uint32_t d = 0; d < sizeof(depths) / sizeof(depths[0]); ++d) {
        for (int yuvFormat = AVIF_PIXEL_FORMAT_YUV444; yuvFormat <= AVIF_PIXEL_FORMAT_YUV400; ++yuvFormat) {
            for (avifBool createAlpha = AVIF_FALSE; createAlpha <= AVIF_TRUE; ++createAlpha) {
                if (createAlpha && (depths[d] != 8 || yuvFormat != AVIF_PIXEL_FORMAT_YUV444)) {
                    continue; // writing alpha is currently only supported in 8bpc YUV444
                }

                for (int yuvRange = AVIF_RANGE_LIMITED; yuvRange <= AVIF_RANGE_FULL; ++yuvRange) {
                    for (uint32_t w = 0; w < sizeof(widths) / sizeof(widths[0]); ++w) {
                        for (uint32_t h = 0; h < sizeof(heights) / sizeof(heights[0]); ++h) {
                            if (!encodeDecodeY4m(widths[w], heights[h], depths[d], yuvFormat, yuvRange, createAlpha, filePath)) {
                                return EXIT_FAILURE;
                            }
                        }
                    }
                }
            }
        }
    }

    return EXIT_SUCCESS;
}
