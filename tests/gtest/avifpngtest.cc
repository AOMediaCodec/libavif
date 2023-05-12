// Copyright 2022 Google LLC
// SPDX-License-Identifier: BSD-2-Clause

#include <limits>
#include <numeric>
#include <string>
#include <vector>

#include "avifpng.h"
#include "aviftest_helpers.h"
#include "gtest/gtest.h"

namespace libavif {
namespace {

//------------------------------------------------------------------------------

TEST(avifConvertGammaToSrgb, SrgbGamma) {
  testutil::AvifImagePtr yuv = testutil::CreateImage(
      3, 1, /*depth=*/8, AVIF_PIXEL_FORMAT_YUV444, AVIF_PLANES_ALL);
  testutil::AvifRgbImage rgb(yuv.get(), yuv->depth, AVIF_RGB_FORMAT_RGBA);
  rgb.pixels[0] = 0;   // R
  rgb.pixels[1] = 2;   // G
  rgb.pixels[2] = 10;  // B
  rgb.pixels[3] = 42;  // A

  rgb.pixels[4] = 50;   // R
  rgb.pixels[5] = 100;  // G
  rgb.pixels[6] = 150;  // B
  rgb.pixels[7] = 42;   // A

  rgb.pixels[8] = 200;   // R
  rgb.pixels[9] = 250;   // G
  rgb.pixels[10] = 255;  // B
  rgb.pixels[11] = 42;   // A

  const double gamma = 2.2;  // sRGB approximation.
  avifConvertGammaToSrgb(&rgb, gamma);

  // Note how small values are affected more than larger ones.
  EXPECT_EQ(rgb.pixels[0], 0);
  EXPECT_EQ(rgb.pixels[1], 0);
  EXPECT_EQ(rgb.pixels[2], 3);
  EXPECT_EQ(rgb.pixels[3], 42);  // A unchanged

  EXPECT_EQ(rgb.pixels[4], 46);
  EXPECT_EQ(rgb.pixels[5], 100);
  EXPECT_EQ(rgb.pixels[6], 151);
  EXPECT_EQ(rgb.pixels[7], 42);  // A unchanged

  EXPECT_EQ(rgb.pixels[8], 201);
  EXPECT_EQ(rgb.pixels[9], 250);
  EXPECT_EQ(rgb.pixels[10], 255);
  EXPECT_EQ(rgb.pixels[11], 42);  // A unchanged
}

TEST(avifConvertGammaToSrgb, 10b) {
  testutil::AvifImagePtr yuv = testutil::CreateImage(
      3, 1, /*depth=*/10, AVIF_PIXEL_FORMAT_YUV444, AVIF_PLANES_ALL);
  testutil::AvifRgbImage rgb(yuv.get(), yuv->depth, AVIF_RGB_FORMAT_RGBA);
  uint16_t* rgb16 = (uint16_t*)rgb.pixels;
  rgb16[0] = 0;    // R
  rgb16[1] = 2;    // G
  rgb16[2] = 30;   // B
  rgb16[3] = 500;  // A

  rgb16[4] = 302;  // R
  rgb16[5] = 350;  // G
  rgb16[6] = 400;  // B
  rgb16[7] = 500;  // A

  rgb16[8] = 600;    // R
  rgb16[9] = 789;    // G
  rgb16[10] = 1023;  // B
  rgb16[11] = 500;   // A

  const double gamma = 2.2;  // sRGB approximation.
  avifConvertGammaToSrgb(&rgb, gamma);

  // Note how small values are affected more than larger ones.
  EXPECT_EQ(rgb16[0], 0);
  EXPECT_EQ(rgb16[1], 0);
  EXPECT_EQ(rgb16[2], 6);
  EXPECT_EQ(rgb16[3], 500);  // A unchanged

  EXPECT_EQ(rgb16[4], 296);
  EXPECT_EQ(rgb16[5], 348);
  EXPECT_EQ(rgb16[6], 400);
  EXPECT_EQ(rgb16[7], 500);  // A unchanged

  EXPECT_EQ(rgb16[8], 606);
  EXPECT_EQ(rgb16[9], 794);
  EXPECT_EQ(rgb16[10], 1023);
  EXPECT_EQ(rgb16[11], 500);  // A unchanged
}

TEST(avifConvertGammaToSrgb, VeryLargeGamma) {
  testutil::AvifImagePtr yuv = testutil::CreateImage(
      3, 1, /*depth=*/8, AVIF_PIXEL_FORMAT_YUV444, AVIF_PLANES_ALL);
  testutil::AvifRgbImage rgb(yuv.get(), yuv->depth, AVIF_RGB_FORMAT_RGBA);
  rgb.pixels[0] = 0;   // R
  rgb.pixels[1] = 2;   // G
  rgb.pixels[2] = 10;  // B
  rgb.pixels[3] = 42;  // A

  rgb.pixels[4] = 50;   // R
  rgb.pixels[5] = 100;  // G
  rgb.pixels[6] = 150;  // B
  rgb.pixels[7] = 42;   // A

  rgb.pixels[8] = 200;   // R
  rgb.pixels[9] = 250;   // G
  rgb.pixels[10] = 255;  // B
  rgb.pixels[11] = 42;   // A

  // Largest gamma that can be represented in PNG.
  const double gamma = 1. / 0.00001;
  avifConvertGammaToSrgb(&rgb, gamma);

  // All RGB samples get mapped to 0 except for 255.
  EXPECT_EQ(rgb.pixels[0], 0);
  EXPECT_EQ(rgb.pixels[1], 0);
  EXPECT_EQ(rgb.pixels[2], 0);
  EXPECT_EQ(rgb.pixels[3], 42);  // A unchanged

  EXPECT_EQ(rgb.pixels[4], 0);
  EXPECT_EQ(rgb.pixels[5], 0);
  EXPECT_EQ(rgb.pixels[6], 0);
  EXPECT_EQ(rgb.pixels[7], 42);  // A unchanged

  EXPECT_EQ(rgb.pixels[8], 0);
  EXPECT_EQ(rgb.pixels[9], 0);
  EXPECT_EQ(rgb.pixels[10], 255);
  EXPECT_EQ(rgb.pixels[11], 42);  // A unchanged
}

TEST(avifConvertGammaToSrgb, SmallGamma) {
  testutil::AvifImagePtr yuv = testutil::CreateImage(
      3, 1, /*depth=*/8, AVIF_PIXEL_FORMAT_YUV444, AVIF_PLANES_ALL);
  testutil::AvifRgbImage rgb(yuv.get(), yuv->depth, AVIF_RGB_FORMAT_RGBA);
  rgb.pixels[0] = 0;   // R
  rgb.pixels[1] = 2;   // G
  rgb.pixels[2] = 10;  // B
  rgb.pixels[3] = 42;  // A

  rgb.pixels[4] = 50;   // R
  rgb.pixels[5] = 100;  // G
  rgb.pixels[6] = 150;  // B
  rgb.pixels[7] = 42;   // A

  rgb.pixels[8] = 200;   // R
  rgb.pixels[9] = 250;   // G
  rgb.pixels[10] = 255;  // B
  rgb.pixels[11] = 42;   // A

  // Smallest gamma that can be represented in PNG.
  const double gamma = 1.0 / (std::numeric_limits<uint32_t>::max() / 100000.0);
  avifConvertGammaToSrgb(&rgb, gamma);

  // All RGB samples get mapped to 255 except for 0.
  EXPECT_EQ(rgb.pixels[0], 0);
  EXPECT_EQ(rgb.pixels[1], 255);
  EXPECT_EQ(rgb.pixels[2], 255);
  EXPECT_EQ(rgb.pixels[3], 42);  // A unchanged

  EXPECT_EQ(rgb.pixels[4], 255);
  EXPECT_EQ(rgb.pixels[5], 255);
  EXPECT_EQ(rgb.pixels[6], 255);
  EXPECT_EQ(rgb.pixels[7], 42);  // A unchanged

  EXPECT_EQ(rgb.pixels[8], 255);
  EXPECT_EQ(rgb.pixels[9], 255);
  EXPECT_EQ(rgb.pixels[10], 255);
  EXPECT_EQ(rgb.pixels[11], 42);  // A unchanged
}

//------------------------------------------------------------------------------

}  // namespace
}  // namespace libavif
