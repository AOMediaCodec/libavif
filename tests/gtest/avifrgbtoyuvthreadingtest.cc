// Copyright 2022 Google LLC
// SPDX-License-Identifier: BSD-2-Clause

#include <algorithm>
#include <cmath>
#include <memory>
#include <tuple>

#include "aviftest_helpers.h"
#include "gtest/gtest.h"

using ::testing::Bool;
using ::testing::Combine;
using ::testing::Range;
using ::testing::Values;

namespace avif {
namespace {

// Converts YUV pixels to RGB using one thread and multiple threads and checks
// whether the results of both are identical.
class YUVToRGBThreadingTest
    : public testing::TestWithParam<std::tuple<
          /*rgb_depth=*/int, /*yuv_depth=*/int,
          /*width=*/int, /*height=*/int, avifRGBFormat, avifPixelFormat,
          /*threads=*/int, /*avoidLibYUV=*/bool, avifChromaUpsampling,
          /*has_alpha=*/bool>> {};

TEST_P(YUVToRGBThreadingTest, TestIdentical) {
  const int rgb_depth = std::get<0>(GetParam());
  const int yuv_depth = std::get<1>(GetParam());
  const int width = std::get<2>(GetParam());
  const int height = std::get<3>(GetParam());
  const avifRGBFormat rgb_format = std::get<4>(GetParam());
  const avifPixelFormat yuv_format = std::get<5>(GetParam());
  const int maxThreads = std::get<6>(GetParam());
  const bool avoidLibYUV = std::get<7>(GetParam());
  const avifChromaUpsampling chromaUpsampling = std::get<8>(GetParam());
  const bool has_alpha = std::get<9>(GetParam());

  if (rgb_depth > 8 && rgb_format == AVIF_RGB_FORMAT_RGB_565) {
    return;
  }

  ImagePtr yuv(avifImageCreate(width, height, yuv_depth, yuv_format));
  ASSERT_NE(yuv, nullptr);
  yuv->matrixCoefficients = AVIF_MATRIX_COEFFICIENTS_BT601;
  yuv->yuvRange = AVIF_RANGE_FULL;

  // Fill YUVA planes with random values.
  srand(0xAABBCCDD);
  const int yuv_max = (1 << yuv_depth);
  ASSERT_EQ(avifImageAllocatePlanes(
                yuv.get(), has_alpha ? AVIF_PLANES_ALL : AVIF_PLANES_YUV),
            AVIF_RESULT_OK);
  for (int plane = AVIF_CHAN_Y; plane <= AVIF_CHAN_A; ++plane) {
    const uint32_t plane_width = avifImagePlaneWidth(yuv.get(), plane);
    if (plane_width == 0) continue;
    const uint32_t plane_height = avifImagePlaneHeight(yuv.get(), plane);
    const uint32_t rowBytes = avifImagePlaneRowBytes(yuv.get(), plane);
    uint8_t* row = avifImagePlane(yuv.get(), plane);
    for (uint32_t y = 0; y < plane_height; ++y, row += rowBytes) {
      for (uint32_t x = 0; x < plane_width; ++x) {
        if (yuv_depth == 8) {
          row[x] = (uint8_t)(rand() % yuv_max);
        } else {
          ((uint16_t*)row)[x] = (uint16_t)(rand() % yuv_max);
        }
      }
    }
  }

  // Convert to RGB with 1 thread.
  testutil::AvifRgbImage rgb(yuv.get(), rgb_depth, rgb_format);
  rgb.avoidLibYUV = avoidLibYUV;
  rgb.chromaUpsampling = chromaUpsampling;
  ASSERT_EQ(avifImageYUVToRGB(yuv.get(), &rgb), AVIF_RESULT_OK);

  // Convert to RGB with multiple threads.
  testutil::AvifRgbImage rgb_threaded(yuv.get(), rgb_depth, rgb_format);
  rgb_threaded.avoidLibYUV = avoidLibYUV;
  rgb_threaded.chromaUpsampling = chromaUpsampling;
  rgb_threaded.maxThreads = maxThreads;
  ASSERT_EQ(avifImageYUVToRGB(yuv.get(), &rgb_threaded), AVIF_RESULT_OK);

  EXPECT_TRUE(testutil::AreImagesEqual(rgb, rgb_threaded));
}

INSTANTIATE_TEST_SUITE_P(
    YUVToRGBThreadingTestInstance, YUVToRGBThreadingTest,
    Combine(/*rgb_depth=*/Values(8, 16),
            /*yuv_depth=*/Values(8, 10),
            /*width=*/Values(1, 2, 127, 200),
            /*height=*/Values(1, 2, 127, 200),
            Values(AVIF_RGB_FORMAT_RGB, AVIF_RGB_FORMAT_RGBA),
            Range(AVIF_PIXEL_FORMAT_YUV444, AVIF_PIXEL_FORMAT_COUNT),
            // Test an odd and even number for threads. Not adding all possible
            // thread values to keep the number of test instances low.
            /*threads=*/Values(2, 7),
            /*avoidLibYUV=*/Bool(),
            Values(AVIF_CHROMA_UPSAMPLING_FASTEST,
                   AVIF_CHROMA_UPSAMPLING_BILINEAR),
            /*has_alpha=*/Bool()));

// This will generate a large number of test instances and hence it is disabled
// by default. It can be run manually if necessary.
INSTANTIATE_TEST_SUITE_P(
    DISABLED_ExhaustiveYUVToRGBThreadingTestInstance, YUVToRGBThreadingTest,
    Combine(/*rgb_depth=*/Values(8, 10, 12, 16),
            /*yuv_depth=*/Values(8, 10, 12),
            /*width=*/Values(1, 2, 127, 200),
            /*height=*/Values(1, 2, 127, 200),
            Range(AVIF_RGB_FORMAT_RGB, AVIF_RGB_FORMAT_COUNT),
            Range(AVIF_PIXEL_FORMAT_YUV444, AVIF_PIXEL_FORMAT_COUNT),
            /*threads=*/Range(0, 9),
            /*avoidLibYUV=*/Bool(),
            Values(AVIF_CHROMA_UPSAMPLING_AUTOMATIC,
                   AVIF_CHROMA_UPSAMPLING_FASTEST,
                   AVIF_CHROMA_UPSAMPLING_NEAREST,
                   AVIF_CHROMA_UPSAMPLING_BILINEAR),
            /*has_alpha=*/Bool()));

}  // namespace
}  // namespace avif
