// Copyright 2022 Google LLC. All rights reserved.
// SPDX-License-Identifier: BSD-2-Clause

#include "avif/avif.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

//------------------------------------------------------------------------------

// Creates an image where the pixel values are defined but do not matter.
// Returns false in case of memory failure.
static avifBool createImage(int width, int height, int depth, avifPixelFormat yuvFormat, avifImage ** image)
{
    *image = avifImageCreate(width, height, depth, yuvFormat);
    if (*image == NULL) {
        printf("ERROR: avifImageCreate() failed\n");
        return AVIF_FALSE;
    }
    avifImageAllocatePlanes(*image, AVIF_PLANES_YUV);
    if (width * height == 0) {
        return AVIF_TRUE;
    }

    avifPixelFormatInfo formatInfo;
    avifGetPixelFormatInfo((*image)->yuvFormat, &formatInfo);
    uint32_t uvWidth = ((*image)->width + formatInfo.chromaShiftX) >> formatInfo.chromaShiftX;
    uint32_t uvHeight = ((*image)->height + formatInfo.chromaShiftY) >> formatInfo.chromaShiftY;

    const int planeCount = formatInfo.monochrome ? 1 : AVIF_PLANE_COUNT_YUV;
    for (int plane = 0; plane < planeCount; ++plane) {
        const uint32_t widthByteCount = ((plane == AVIF_CHAN_Y) ? (*image)->width : uvWidth) * (((*image)->depth > 8) ? 2 : 1);
        const uint32_t planeHeight = (plane == AVIF_CHAN_Y) ? (*image)->height : uvHeight;
        uint8_t * row = (*image)->yuvPlanes[plane];
        for (uint32_t y = 0; y < planeHeight; ++y) {
            memset(row, (int)(y % 256), widthByteCount); // Fill with a repeating gradient.
            row += (*image)->yuvRowBytes[plane];
        }
    }
    return AVIF_TRUE;
}

// Generates then encodes a grid image. Returns false in case of failure.
static avifBool encodeGrid(int columns, int rows, int cellWidth, int cellHeight, int depth, avifPixelFormat yuvFormat, avifRWData * output)
{
    avifBool success = AVIF_FALSE;
    avifEncoder * encoder = NULL;
    avifImage ** cellImages = avifAlloc(sizeof(avifImage *) * columns * rows);
    memset(cellImages, 0, sizeof(avifImage *) * columns * rows);
    for (int iCell = 0; iCell < columns * rows; ++iCell) {
        if (!createImage(cellWidth, cellHeight, depth, yuvFormat, &cellImages[iCell])) {
            goto cleanup;
        }
    }

    encoder = avifEncoderCreate();
    if (encoder == NULL) {
        printf("ERROR: avifEncoderCreate() failed\n");
        goto cleanup;
    }
    encoder->speed = AVIF_SPEED_FASTEST;
    if (avifEncoderAddImageGrid(encoder, columns, rows, (const avifImage * const *)cellImages, AVIF_ADD_IMAGE_FLAG_SINGLE) !=
        AVIF_RESULT_OK) {
        printf("ERROR: avifEncoderAddImageGrid() failed\n");
        goto cleanup;
    }
    if (avifEncoderFinish(encoder, output) != AVIF_RESULT_OK) {
        printf("ERROR: avifEncoderFinish() failed\n");
        goto cleanup;
    }

    success = AVIF_TRUE;
cleanup:
    if (encoder != NULL) {
        avifEncoderDestroy(encoder);
    }
    if (cellImages != NULL) {
        for (int i = 0; i < columns * rows; ++i) {
            if (cellImages[i] != NULL) {
                avifImageDestroy(cellImages[i]);
            }
        }
        avifFree(cellImages);
    }
    return success;
}

//------------------------------------------------------------------------------

// Decodes the data. Returns false in case of failure.
static avifBool decode(const avifRWData * encodedAvif)
{
    avifBool success = AVIF_FALSE;
    avifImage * const image = avifImageCreateEmpty();
    avifDecoder * const decoder = avifDecoderCreate();
    if (image == NULL || decoder == NULL) {
        printf("ERROR: memory allocation failed\n");
        goto cleanup;
    }
    if (avifDecoderReadMemory(decoder, image, encodedAvif->data, encodedAvif->size) != AVIF_RESULT_OK) {
        printf("ERROR: avifDecoderReadMemory() failed\n");
        goto cleanup;
    }
    success = AVIF_TRUE;
cleanup:
    if (image != NULL) {
        avifImageDestroy(image);
    }
    if (decoder != NULL) {
        avifDecoderDestroy(decoder);
    }
    return success;
}

//------------------------------------------------------------------------------

// Generates, encodes then decodes a grid image.
static avifBool encodeDecode(int columns, int rows, int cellWidth, int cellHeight, avifPixelFormat yuvFormat, int expected_success)
{
    avifBool success = AVIF_FALSE;
    avifRWData encodedAvif = { 0 };
    if (encodeGrid(columns, rows, cellWidth, cellHeight, /*depth=*/8, yuvFormat, &encodedAvif) != expected_success) {
        goto cleanup;
    }
    // Only decode if the encoding was expected to succeed.
    // Any successful encoding shall result in a valid decoding.
    if (expected_success && !decode(&encodedAvif)) {
        goto cleanup;
    }
    success = AVIF_TRUE;
cleanup:
    avifRWDataFree(&encodedAvif);
    return success;
}

//------------------------------------------------------------------------------

// For each dimension, for each combination of cell count and size, generates, encodes then decodes a grid image.
static avifBool encodeDecodeSizes(const int columnsCellWidths[][2],
                                  int horizontalCombinationCount,
                                  const int rowsCellHeights[][2],
                                  int verticalCombinationCount,
                                  avifPixelFormat yuvFormat,
                                  int expected_success)
{
    for (int i = 0; i < horizontalCombinationCount; ++i) {
        for (int j = 0; j < verticalCombinationCount; ++j) {
            if (!encodeDecode(/*columns=*/columnsCellWidths[i][0],
                              /*rows=*/rowsCellHeights[j][0],
                              /*cellWidth=*/columnsCellWidths[i][1],
                              /*cellHeight=*/rowsCellHeights[j][1],
                              yuvFormat,
                              expected_success)) {
                return AVIF_FALSE;
            }
        }
    }
    return AVIF_TRUE;
}

int main(void)
{
    // Pairs of cell count and cell size for a single dimension.
    // A cell cannot be smaller than 64px in any dimension if there are several cells.
    // A cell cannot have an odd size in any dimension if there are several cells and chroma subsampling.
    // Image size must be a multiple of cell size.
    const int validCellCountsSizes[][2] = { { 1, 64 }, { 1, 66 }, { 2, 64 }, { 3, 68 } };
    const int validCellCountsSizeCount = sizeof(validCellCountsSizes) / sizeof(validCellCountsSizes[0]);
    const int invalidCellCountsSizes[][2] = { { 0, 0 }, { 0, 1 }, { 1, 0 }, { 2, 1 }, { 2, 2 }, { 2, 3 }, { 2, 63 } };
    const int invalidCellCountsSizeCount = sizeof(invalidCellCountsSizes) / sizeof(invalidCellCountsSizes[0]);

    for (int yuvFormat = AVIF_PIXEL_FORMAT_YUV444; yuvFormat <= AVIF_PIXEL_FORMAT_YUV400; ++yuvFormat) {
        if (!encodeDecodeSizes(validCellCountsSizes,
                               validCellCountsSizeCount,
                               validCellCountsSizes,
                               validCellCountsSizeCount,
                               yuvFormat,
                               /*expected_success=*/AVIF_TRUE)) {
            return EXIT_FAILURE;
        }

        if (!encodeDecodeSizes(validCellCountsSizes,
                               validCellCountsSizeCount,
                               invalidCellCountsSizes,
                               invalidCellCountsSizeCount,
                               yuvFormat,
                               /*expected_success=*/AVIF_FALSE) ||
            !encodeDecodeSizes(invalidCellCountsSizes,
                               invalidCellCountsSizeCount,
                               validCellCountsSizes,
                               validCellCountsSizeCount,
                               yuvFormat,
                               /*expected_success=*/AVIF_FALSE) ||
            !encodeDecodeSizes(invalidCellCountsSizes,
                               invalidCellCountsSizeCount,
                               invalidCellCountsSizes,
                               invalidCellCountsSizeCount,
                               yuvFormat,
                               /*expected_success=*/AVIF_FALSE)) {
            return EXIT_FAILURE;
        }

        // Special case depending on the cell count and the chroma subsampling.
        for (int rows = 1; rows <= 2; ++rows) {
            int expected_success = (rows == 1) || (yuvFormat != AVIF_PIXEL_FORMAT_YUV420);
            if (!encodeDecode(/*columns=*/1, rows, /*cellWidth=*/64, /*cellHeight=*/65, yuvFormat, expected_success)) {
                return EXIT_FAILURE;
            }
        }

        // Special case depending on the cell count and the cell size.
        for (int columns = 1; columns <= 2; ++columns) {
            for (int rows = 1; rows <= 2; ++rows) {
                int expected_success = (columns * rows == 1);
                if (!encodeDecode(columns, rows, /*cellWidth=*/1, /*cellHeight=*/65, yuvFormat, expected_success)) {
                    return EXIT_FAILURE;
                }
            }
        }
    }
    return EXIT_SUCCESS;
}
