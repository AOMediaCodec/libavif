// Copyright 2022 Google LLC. All rights reserved.
// SPDX-License-Identifier: BSD-2-Clause

#include "avif/avif.h"
#include "aviftest_helpers.h"
#include "gtest/gtest.h"

namespace libavif {
namespace {

TEST(DeprecatedApiTest, avifImageRGBToYUV) {
  testutil::AvifImagePtr yuv =
      testutil::CreateImage(123, 456, 10, AVIF_PIXEL_FORMAT_YUV422,
                            AVIF_PLANES_ALL, AVIF_RANGE_LIMITED);
  ASSERT_NE(yuv, nullptr);
  testutil::FillImageGradient(yuv.get());

  testutil::AvifRgbImage rgb(yuv.get(), yuv->depth, AVIF_RGB_FORMAT_RGBA);
  rgb.chromaUpsampling = AVIF_CHROMA_UPSAMPLING_FASTEST;
  ASSERT_EQ(avifImageYUVToRGB(yuv.get(), &rgb), AVIF_RESULT_OK);
  rgb.chromaDownsampling = AVIF_CHROMA_DOWNSAMPLING_BEST_QUALITY;
  ASSERT_EQ(avifImageRGBToYUV(yuv.get(), &rgb), AVIF_RESULT_OK);
}

}  // namespace
}  // namespace libavif
