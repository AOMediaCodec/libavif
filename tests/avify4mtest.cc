// Copyright 2022 Google LLC. All rights reserved.
// SPDX-License-Identifier: BSD-2-Clause

#include "avif/avif.h"

#include "y4m.h"

#include <cassert>
#include <cinttypes>
#include <cstdio>
#include <cstring>
#include <sstream>
#include <tuple>

#include "gtest/gtest.h"

using testing::Combine;
using testing::Values;

namespace
{

//------------------------------------------------------------------------------

// Returns true if image1 and image2 are identical.
bool compareYUVA(const avifImage * image1, const avifImage * image2)
{
    if (image1->width != image2->width || image1->height != image2->height || image1->depth != image2->depth ||
        image1->yuvFormat != image2->yuvFormat || image1->yuvRange != image2->yuvRange) {
        printf("ERROR: input mismatch\n");
        return false;
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
                return false;
            }
            row1 += image1->yuvRowBytes[plane];
            row2 += image2->yuvRowBytes[plane];
        }
    }

    if (image1->alphaPlane || image2->alphaPlane) {
        if (!image1->alphaPlane || !image2->alphaPlane || image1->alphaPremultiplied != image2->alphaPremultiplied) {
            printf("ERROR: input mismatch\n");
            return false;
        }
        const uint32_t widthByteCount = image1->width * ((image1->depth > 8) ? sizeof(uint16_t) : sizeof(uint8_t));
        const uint8_t * row1 = image1->alphaPlane;
        const uint8_t * row2 = image2->alphaPlane;
        for (uint32_t y = 0; y < image1->height; ++y) {
            if (memcmp(row1, row2, widthByteCount) != 0) {
                printf("ERROR: different px at row %" PRIu32 ", alpha\n", y);
                return false;
            }
            row1 += image1->alphaRowBytes;
            row2 += image2->alphaRowBytes;
        }
    }
    return true;
}

//------------------------------------------------------------------------------

// Fills each plane of the image with the maximum allowed value.
void fillPlanes(avifImage * image)
{
    const uint16_t yuvValue = (image->yuvRange == AVIF_RANGE_LIMITED) ? (235 << (image->depth - 8)) : ((1 << image->depth) - 1);
    avifPixelFormatInfo formatInfo;
    avifGetPixelFormatInfo(image->yuvFormat, &formatInfo);
    const int planeCount = formatInfo.monochrome ? 1 : AVIF_PLANE_COUNT_YUV;
    for (int plane = 0; plane < planeCount; ++plane) {
        if (image->yuvPlanes[plane]) {
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
    if (image->alphaPlane) {
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
bool encodeDecodeY4m(uint32_t width,
                     uint32_t height,
                     uint32_t depth,
                     avifPixelFormat yuvFormat,
                     avifRange yuvRange,
                     bool createAlpha,
                     const char filePath[])
{
    bool success = false;
    avifImage * image = avifImageCreateEmpty();
    avifImage * decoded = avifImageCreateEmpty();
    if (!image || !decoded) {
        printf("ERROR: avifImageCreate() failed\n");
        goto cleanup;
    }
    image->width = width;
    image->height = height;
    image->depth = depth;
    image->yuvFormat = yuvFormat;
    image->yuvRange = yuvRange;
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

    success = true;
cleanup:
    if (image) {
        avifImageDestroy(image);
    }
    if (decoded) {
        avifImageDestroy(decoded);
    }
    return success;
}

//------------------------------------------------------------------------------

class Y4mTest
    : public testing::TestWithParam<std::tuple</*width=*/int, /*height=*/int, /*bitDepth=*/int, /*yuvFormat=*/avifPixelFormat, /*yuvRange=*/avifRange, /*createAlpha=*/bool>>
{
};

TEST_P(Y4mTest, EncodeDecode)
{
    const int width = std::get<0>(GetParam());
    const int height = std::get<1>(GetParam());
    const int bitDepth = std::get<2>(GetParam());
    const avifPixelFormat yuvFormat = std::get<3>(GetParam());
    const avifRange yuvRange = std::get<4>(GetParam());
    const bool createAlpha = std::get<5>(GetParam());
    std::ostringstream filePath;
    filePath << testing::TempDir() << "avify4mtest_" << width << "_" << height << "_" << bitDepth << "_" << yuvFormat << "_"
             << yuvRange << "_" << createAlpha;
    EXPECT_TRUE(encodeDecodeY4m(width, height, bitDepth, yuvFormat, yuvRange, createAlpha, filePath.str().c_str()));
}

INSTANTIATE_TEST_SUITE_P(OpaqueCombinations,
                         Y4mTest,
                         Combine(/*width=*/Values(1, 2, 3),
                                 /*height=*/Values(1, 2, 3),
                                 /*depths=*/Values(8, 10, 12),
                                 Values(AVIF_PIXEL_FORMAT_YUV444, AVIF_PIXEL_FORMAT_YUV422, AVIF_PIXEL_FORMAT_YUV420, AVIF_PIXEL_FORMAT_YUV400),
                                 Values(AVIF_RANGE_LIMITED, AVIF_RANGE_FULL),
                                 /*createAlpha=*/Values(false)));

// Writing alpha is currently only supported in 8bpc YUV444.
INSTANTIATE_TEST_SUITE_P(AlphaCombinations,
                         Y4mTest,
                         Combine(/*width=*/Values(1, 2, 3),
                                 /*height=*/Values(1, 2, 3),
                                 /*depths=*/Values(8),
                                 Values(AVIF_PIXEL_FORMAT_YUV444),
                                 Values(AVIF_RANGE_LIMITED, AVIF_RANGE_FULL),
                                 /*createAlpha=*/Values(true)));

} // namespace
