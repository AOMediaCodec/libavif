// Copyright 2023 Google LLC
// SPDX-License-Identifier: BSD-2-Clause

#include "avif/avif.h"
#include "gtest/gtest.h"

namespace libavif {
namespace {

struct AVIFClapPropertyParam {
  uint32_t width;
  uint32_t height;
  avifPixelFormat yuv_format;
  avifCleanApertureBox clap;

  bool expected_result;
  avifCropRect expected_crop_rect;  // Ignored if expected_result is false.
};

AVIFClapPropertyParam kAVIFClapPropertyTestParams[] = {
    //////////////////////////////////////////////////
    // Negative tests:

    // Zero or negative denominators.
    {120,
     160,
     AVIF_PIXEL_FORMAT_YUV420,
     {96, 0, 132, 1, 0, 1, 0, 1},
     false,
     {0, 0, 0, 0}},
    {120,
     160,
     AVIF_PIXEL_FORMAT_YUV420,
     {96, static_cast<uint32_t>(-1), 132, 1, 0, 1, 0, 1},
     false,
     {0, 0, 0, 0}},
    {120,
     160,
     AVIF_PIXEL_FORMAT_YUV420,
     {96, 1, 132, 0, 0, 1, 0, 1},
     false,
     {0, 0, 0, 0}},
    {120,
     160,
     AVIF_PIXEL_FORMAT_YUV420,
     {96, 1, 132, static_cast<uint32_t>(-1), 0, 1, 0, 1},
     false,
     {0, 0, 0, 0}},
    {120,
     160,
     AVIF_PIXEL_FORMAT_YUV420,
     {96, 1, 132, 1, 0, 0, 0, 1},
     false,
     {0, 0, 0, 0}},
    {120,
     160,
     AVIF_PIXEL_FORMAT_YUV420,
     {96, 1, 132, 1, 0, static_cast<uint32_t>(-1), 0, 1},
     false,
     {0, 0, 0, 0}},
    {120,
     160,
     AVIF_PIXEL_FORMAT_YUV420,
     {96, 1, 132, 1, 0, 1, 0, 0},
     false,
     {0, 0, 0, 0}},
    {120,
     160,
     AVIF_PIXEL_FORMAT_YUV420,
     {96, 1, 132, 1, 0, 1, 0, static_cast<uint32_t>(-1)},
     false,
     {0, 0, 0, 0}},
    // Zero or negative clean aperture width or height.
    {120,
     160,
     AVIF_PIXEL_FORMAT_YUV420,
     {static_cast<uint32_t>(-96), 1, 132, 1, 0, 1, 0, 1},
     false,
     {0, 0, 0, 0}},
    {120,
     160,
     AVIF_PIXEL_FORMAT_YUV420,
     {0, 1, 132, 1, 0, 1, 0, 1},
     false,
     {0, 0, 0, 0}},
    {120,
     160,
     AVIF_PIXEL_FORMAT_YUV420,
     {96, 1, static_cast<uint32_t>(-132), 1, 0, 1, 0, 1},
     false,
     {0, 0, 0, 0}},
    {120,
     160,
     AVIF_PIXEL_FORMAT_YUV420,
     {96, 1, 0, 1, 0, 1, 0, 1},
     false,
     {0, 0, 0, 0}},
    // Clean aperture width or height is not an integer.
    {120,
     160,
     AVIF_PIXEL_FORMAT_YUV420,
     {96, 5, 132, 1, 0, 1, 0, 1},
     false,
     {0, 0, 0, 0}},
    {120,
     160,
     AVIF_PIXEL_FORMAT_YUV420,
     {96, 1, 132, 5, 0, 1, 0, 1},
     false,
     {0, 0, 0, 0}},
    // pcX = 103 + (722 - 1)/2 = 463.5
    // pcY = -308 + (1024 - 1)/2 = 203.5
    // leftmost = 463.5 - (385 - 1)/2 = 271.5 (not an integer)
    // topmost = 203.5 - (330 - 1)/2 = 39
    {722,
     1024,
     AVIF_PIXEL_FORMAT_YUV420,
     {385, 1, 330, 1, 103, 1, static_cast<uint32_t>(-308), 1},
     false,
     {0, 0, 0, 0}},
    // pcX = -308 + (1024 - 1)/2 = 203.5
    // pcY = 103 + (722 - 1)/2 = 463.5
    // leftmost = 203.5 - (330 - 1)/2 = 39
    // topmost = 463.5 - (385 - 1)/2 = 271.5 (not an integer)
    {1024,
     722,
     AVIF_PIXEL_FORMAT_YUV420,
     {330, 1, 385, 1, static_cast<uint32_t>(-308), 1, 103, 1},
     false,
     {0, 0, 0, 0}},

    //////////////////////////////////////////////////
    // Positive tests:

    // pcX = 0 + (120 - 1)/2 = 59.5
    // pcY = 0 + (160 - 1)/2 = 79.5
    // leftmost = 59.5 - (96 - 1)/2 = 12
    // topmost = 79.5 - (132 - 1)/2 = 14
    {120,
     160,
     AVIF_PIXEL_FORMAT_YUV420,
     {96, 1, 132, 1, 0, 1, 0, 1},
     true,
     {12, 14, 96, 132}},
    // pcX = -30 + (120 - 1)/2 = 29.5
    // pcY = -40 + (160 - 1)/2 = 39.5
    // leftmost = 29.5 - (60 - 1)/2 = 0
    // topmost = 39.5 - (80 - 1)/2 = 0
    {120,
     160,
     AVIF_PIXEL_FORMAT_YUV420,
     {60, 1, 80, 1, static_cast<uint32_t>(-30), 1, static_cast<uint32_t>(-40),
      1},
     true,
     {0, 0, 60, 80}},
};

using AVIFClapPropertyTest = ::testing::TestWithParam<AVIFClapPropertyParam>;

INSTANTIATE_TEST_SUITE_P(Parameterized, AVIFClapPropertyTest,
                         ::testing::ValuesIn(kAVIFClapPropertyTestParams));

// Test the avifCropRectConvertCleanApertureBox() function.
TEST_P(AVIFClapPropertyTest, ValidateClapProperty) {
  const AVIFClapPropertyParam& param = GetParam();
  avifCropRect crop_rect;
  avifDiagnostics diag;
  EXPECT_EQ(avifCropRectConvertCleanApertureBox(&crop_rect, &param.clap,
                                                param.width, param.height,
                                                param.yuv_format, &diag),
            param.expected_result);
  if (param.expected_result) {
    EXPECT_EQ(crop_rect.x, param.expected_crop_rect.x);
    EXPECT_EQ(crop_rect.y, param.expected_crop_rect.y);
    EXPECT_EQ(crop_rect.width, param.expected_crop_rect.width);
    EXPECT_EQ(crop_rect.height, param.expected_crop_rect.height);
  }
}

}  // namespace
}  // namespace libavif
