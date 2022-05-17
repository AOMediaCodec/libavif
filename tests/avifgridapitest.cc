// Copyright 2022 Google LLC. All rights reserved.
// SPDX-License-Identifier: BSD-2-Clause

#include "avif/avif.h"

#include <cassert>
#include <cinttypes>
#include <cstdio>
#include <cstring>
#include <tuple>

#include "gtest/gtest.h"

using testing::Combine;
using testing::Values;
using testing::ValuesIn;

namespace
{

//------------------------------------------------------------------------------

// Fills a plane with a repeating gradient.
void fillPlane(int width, int height, int depth, uint8_t * row, uint32_t rowBytes)
{
    assert(depth == 8 || depth == 10 || depth == 12); // Values allowed by AV1.
    const int maxValuePlusOne = 1 << depth;
    for (int y = 0; y < height; ++y) {
        if (depth == 8) {
            memset(row, y % maxValuePlusOne, width);
        } else {
            for (int x = 0; x < width; ++x) {
                ((uint16_t *)row)[x] = (uint16_t)(y % maxValuePlusOne);
            }
        }
        row += rowBytes;
    }
}

// Creates an image where the pixel values are defined but do not matter.
// Returns false in case of memory failure.
bool createImage(int width, int height, int depth, avifPixelFormat yuvFormat, bool createAlpha, avifImage ** image)
{
    *image = avifImageCreate(width, height, depth, yuvFormat);
    if (!*image) {
        printf("ERROR: avifImageCreate() failed\n");
        return false;
    }
    avifImageAllocatePlanes(*image, createAlpha ? AVIF_PLANES_ALL : AVIF_PLANES_YUV);
    if (width * height == 0) {
        return true;
    }

    avifPixelFormatInfo formatInfo;
    avifGetPixelFormatInfo((*image)->yuvFormat, &formatInfo);
    uint32_t uvWidth = ((*image)->width + formatInfo.chromaShiftX) >> formatInfo.chromaShiftX;
    uint32_t uvHeight = ((*image)->height + formatInfo.chromaShiftY) >> formatInfo.chromaShiftY;

    const int planeCount = formatInfo.monochrome ? 1 : AVIF_PLANE_COUNT_YUV;
    for (int plane = 0; plane < planeCount; ++plane) {
        fillPlane((plane == AVIF_CHAN_Y) ? (*image)->width : uvWidth,
                  (plane == AVIF_CHAN_Y) ? (*image)->height : uvHeight,
                  (*image)->depth,
                  (*image)->yuvPlanes[plane],
                  (*image)->yuvRowBytes[plane]);
    }

    if (createAlpha) {
        fillPlane((*image)->width, (*image)->height, (*image)->depth, (*image)->alphaPlane, (*image)->alphaRowBytes);
    }
    return true;
}

// Generates then encodes a grid image. Returns false in case of failure.
bool encodeGrid(int columns, int rows, int cellWidth, int cellHeight, int depth, avifPixelFormat yuvFormat, bool createAlpha, avifRWData * output)
{
    bool success = false;
    avifEncoder * encoder = NULL;
    avifImage ** cellImages = (avifImage **)avifAlloc(sizeof(avifImage *) * columns * rows);
    memset(cellImages, 0, sizeof(avifImage *) * columns * rows);
    for (int iCell = 0; iCell < columns * rows; ++iCell) {
        if (!createImage(cellWidth, cellHeight, depth, yuvFormat, createAlpha, &cellImages[iCell])) {
            goto cleanup;
        }
    }

    encoder = avifEncoderCreate();
    if (!encoder) {
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

    success = true;
cleanup:
    if (encoder) {
        avifEncoderDestroy(encoder);
    }
    if (cellImages) {
        for (int i = 0; i < columns * rows; ++i) {
            if (cellImages[i]) {
                avifImageDestroy(cellImages[i]);
            }
        }
        avifFree(cellImages);
    }
    return success;
}

//------------------------------------------------------------------------------

// Decodes the data. Returns false in case of failure.
bool decode(const avifRWData * encodedAvif)
{
    bool success = false;
    avifImage * const image = avifImageCreateEmpty();
    avifDecoder * const decoder = avifDecoderCreate();
    if (!image || !decoder) {
        printf("ERROR: memory allocation failed\n");
        goto cleanup;
    }
    if (avifDecoderReadMemory(decoder, image, encodedAvif->data, encodedAvif->size) != AVIF_RESULT_OK) {
        printf("ERROR: avifDecoderReadMemory() failed\n");
        goto cleanup;
    }
    success = true;
cleanup:
    if (image) {
        avifImageDestroy(image);
    }
    if (decoder) {
        avifDecoderDestroy(decoder);
    }
    return success;
}

//------------------------------------------------------------------------------

// Generates, encodes then decodes a grid image.
bool encodeDecode(int columns, int rows, int cellWidth, int cellHeight, int depth, avifPixelFormat yuvFormat, bool createAlpha, bool expectedSuccess)
{
    bool success = false;
    avifRWData encodedAvif = { nullptr, 0 };
    if (encodeGrid(columns, rows, cellWidth, cellHeight, depth, yuvFormat, createAlpha, &encodedAvif) != expectedSuccess) {
        goto cleanup;
    }
    // Only decode if the encoding was expected to succeed.
    // Any successful encoding shall result in a valid decoding.
    if (expectedSuccess && !decode(&encodedAvif)) {
        goto cleanup;
    }
    success = true;
cleanup:
    avifRWDataFree(&encodedAvif);
    return success;
}

//------------------------------------------------------------------------------

// Pair of cell count and cell size for a single dimension.
struct Cell
{
    int count, size;
};

class GridApiTest
    : public testing::TestWithParam<std::tuple</*horizontal=*/Cell, /*vertical=*/Cell, /*bitDepth=*/int, /*yuvFormat=*/avifPixelFormat, /*createAlpha=*/bool, /*expectedSuccess=*/bool>>
{
};

TEST_P(GridApiTest, EncodeDecode)
{
    const Cell horizontal = std::get<0>(GetParam());
    const Cell vertical = std::get<1>(GetParam());
    const int bitDepth = std::get<2>(GetParam());
    const avifPixelFormat yuvFormat = std::get<3>(GetParam());
    const bool createAlpha = std::get<4>(GetParam());
    const bool expectedSuccess = std::get<5>(GetParam());

    EXPECT_TRUE(encodeDecode(/*columns=*/horizontal.count,
                             /*rows=*/vertical.count,
                             /*cellWidth=*/horizontal.size,
                             /*cellHeight=*/vertical.size,
                             bitDepth,
                             yuvFormat,
                             createAlpha,
                             expectedSuccess));
}

// A cell cannot be smaller than 64px in any dimension if there are several cells.
// A cell cannot have an odd size in any dimension if there are several cells and chroma subsampling.
// Image size must be a multiple of cell size.
constexpr Cell kValidCells[] = { { 1, 64 }, { 1, 66 }, { 2, 64 }, { 3, 68 } };
constexpr Cell kInvalidCells[] = { { 0, 0 }, { 0, 1 }, { 1, 0 }, { 2, 1 }, { 2, 2 }, { 2, 3 }, { 2, 63 } };
constexpr int kBitDepths[] = { 8, 10, 12 };
constexpr avifPixelFormat kPixelFormats[] = { AVIF_PIXEL_FORMAT_YUV444, AVIF_PIXEL_FORMAT_YUV422, AVIF_PIXEL_FORMAT_YUV420, AVIF_PIXEL_FORMAT_YUV400 };

INSTANTIATE_TEST_SUITE_P(Valid,
                         GridApiTest,
                         Combine(/*horizontal=*/ValuesIn(kValidCells),
                                 /*vertical=*/ValuesIn(kValidCells),
                                 ValuesIn(kBitDepths),
                                 ValuesIn(kPixelFormats),
                                 /*createAlpha=*/Values(false, true),
                                 /*expectedSuccess=*/Values(true)));

INSTANTIATE_TEST_SUITE_P(InvalidVertically,
                         GridApiTest,
                         Combine(/*horizontal=*/ValuesIn(kValidCells),
                                 /*vertical=*/ValuesIn(kInvalidCells),
                                 ValuesIn(kBitDepths),
                                 ValuesIn(kPixelFormats),
                                 /*createAlpha=*/Values(false, true),
                                 /*expectedSuccess=*/Values(false)));
INSTANTIATE_TEST_SUITE_P(InvalidHorizontally,
                         GridApiTest,
                         Combine(/*horizontal=*/ValuesIn(kInvalidCells),
                                 /*vertical=*/ValuesIn(kValidCells),
                                 ValuesIn(kBitDepths),
                                 ValuesIn(kPixelFormats),
                                 /*createAlpha=*/Values(false, true),
                                 /*expectedSuccess=*/Values(false)));
INSTANTIATE_TEST_SUITE_P(InvalidBoth,
                         GridApiTest,
                         Combine(/*horizontal=*/ValuesIn(kInvalidCells),
                                 /*vertical=*/ValuesIn(kInvalidCells),
                                 ValuesIn(kBitDepths),
                                 ValuesIn(kPixelFormats),
                                 /*createAlpha=*/Values(false, true),
                                 /*expectedSuccess=*/Values(false)));

// Special case depending on the cell count and the chroma subsampling.
INSTANTIATE_TEST_SUITE_P(ValidOddHeight,
                         GridApiTest,
                         Combine(/*horizontal=*/Values(Cell { 1, 64 }),
                                 /*vertical=*/Values(Cell { 1, 65 }, Cell { 2, 65 }),
                                 ValuesIn(kBitDepths),
                                 Values(AVIF_PIXEL_FORMAT_YUV444, AVIF_PIXEL_FORMAT_YUV422, AVIF_PIXEL_FORMAT_YUV400),
                                 /*createAlpha=*/Values(false, true),
                                 /*expectedSuccess=*/Values(true)));
INSTANTIATE_TEST_SUITE_P(InvalidOddHeight,
                         GridApiTest,
                         Combine(/*horizontal=*/Values(Cell { 1, 64 }),
                                 /*vertical=*/Values(Cell { 2, 65 }),
                                 ValuesIn(kBitDepths),
                                 Values(AVIF_PIXEL_FORMAT_YUV420),
                                 /*createAlpha=*/Values(false, true),
                                 /*expectedSuccess=*/Values(false)));

// Special case depending on the cell count and the cell size.
INSTANTIATE_TEST_SUITE_P(ValidOddDimensions,
                         GridApiTest,
                         Combine(/*horizontal=*/Values(Cell { 1, 1 }),
                                 /*vertical=*/Values(Cell { 1, 65 }),
                                 ValuesIn(kBitDepths),
                                 ValuesIn(kPixelFormats),
                                 /*createAlpha=*/Values(false, true),
                                 /*expectedSuccess=*/Values(true)));
INSTANTIATE_TEST_SUITE_P(InvalidOddDimensions,
                         GridApiTest,
                         Combine(/*horizontal=*/Values(Cell { 2, 1 }),
                                 /*vertical=*/Values(Cell { 1, 65 }, Cell { 2, 65 }),
                                 ValuesIn(kBitDepths),
                                 ValuesIn(kPixelFormats),
                                 /*createAlpha=*/Values(false, true),
                                 /*expectedSuccess=*/Values(false)));

} // namespace
