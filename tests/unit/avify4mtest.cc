// Copyright 2022 Google LLC. All rights reserved.
// SPDX-License-Identifier: BSD-2-Clause

#include "avif/avif.h"

#include "y4m.h"

#include <sstream>
#include <tuple>

#include "aviftest_helpers.h"
#include "gtest/gtest.h"

using testing::Combine;
using testing::Values;

namespace libavif
{
namespace
{

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

    testutil::avifImagePtr image =
        testutil::createImage(width, height, bitDepth, yuvFormat, createAlpha ? AVIF_PLANES_ALL : AVIF_PLANES_YUV, yuvRange);
    ASSERT_NE(image, nullptr);
    const uint32_t yuva[] = { (yuvRange == AVIF_RANGE_LIMITED) ? (235u << (bitDepth - 8)) : ((1u << bitDepth) - 1),
                              (yuvRange == AVIF_RANGE_LIMITED) ? (240u << (bitDepth - 8)) : ((1u << bitDepth) - 1),
                              (yuvRange == AVIF_RANGE_LIMITED) ? (240u << (bitDepth - 8)) : ((1u << bitDepth) - 1),
                              (1u << bitDepth) - 1 };
    testutil::fillImagePlain(image.get(), yuva);
    ASSERT_TRUE(y4mWrite(filePath.str().c_str(), image.get()));

    testutil::avifImagePtr decoded(avifImageCreateEmpty(), avifImageDestroy);
    ASSERT_NE(decoded, nullptr);
    ASSERT_TRUE(y4mRead(filePath.str().c_str(), decoded.get(), /*sourceTiming=*/nullptr, /*iter=*/nullptr));

    EXPECT_TRUE(testutil::areImagesEqual(*image, *decoded));
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
} // namespace libavif
