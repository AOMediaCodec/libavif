// Copyright 2023 Google LLC
// SPDX-License-Identifier: BSD-2-Clause

#include <array>
#include <cstring>
#include <tuple>
#include <vector>

#include "avif/internal.h"
#include "aviftest_helpers.h"
#include "gtest/gtest.h"

using ::testing::Combine;
using ::testing::Values;

namespace avif {
namespace {

class SetGetRGBATest
    : public testing::TestWithParam<std::tuple<
          /*rgb_depth=*/int, avifRGBFormat, /*is_float=*/bool>> {};

TEST_P(SetGetRGBATest, SetGetTest) {
  const int rgb_depth = std::get<0>(GetParam());
  const avifRGBFormat rgb_format = std::get<1>(GetParam());
  const bool is_float = std::get<2>(GetParam());

  // Unused yuv image, simply needed to initialize the rgb image.
  ImagePtr yuv(avifImageCreate(/*width=*/13, /*height=*/17, 8,
                               AVIF_PIXEL_FORMAT_YUV444));

  testutil::AvifRgbImage rgb(yuv.get(), rgb_depth, rgb_format);
  rgb.isFloat = is_float;

  avifRGBColorSpaceInfo color_space;
  ASSERT_TRUE(avifGetRGBColorSpaceInfo(&rgb, &color_space));

  float epsilon = 1.0f / color_space.maxChannelF;
  if (rgb_format == AVIF_RGB_FORMAT_RGB_565) {
    // Only 5 bits of information per channel except G which has 6.
    epsilon = 1.0f / (1 << 5);
  } else if (rgb.isFloat) {
    epsilon = 0.0005f;  // Half precision floats are not that precise.
  }

  std::array<float, 4> pixel_read;
  for (uint32_t j = 0; j < rgb.height; ++j) {
    for (uint32_t i = 0; i < rgb.width; ++i) {
      // Generate some arbitrary pixel values.
      const std::array<float, 4> pixel_to_write = {
          0.0f + static_cast<float>(i) / rgb.width,
          0.5f + static_cast<float>(j) / (rgb.height * 2),
          1.0f - static_cast<float>(i + j) / ((rgb.width + rgb.height) * 2),
          1.0f - static_cast<float>(i) / rgb.width};

      avifSetRGBAPixel(&rgb, i, j, &color_space, pixel_to_write.data());
      avifGetRGBAPixel(&rgb, i, j, &color_space, pixel_read.data());
      EXPECT_NEAR(pixel_read[0], pixel_to_write[0], epsilon);
      EXPECT_NEAR(pixel_read[1], pixel_to_write[1], epsilon);
      EXPECT_NEAR(pixel_read[2], pixel_to_write[2], epsilon);
      if (avifRGBFormatHasAlpha(rgb_format)) {
        EXPECT_NEAR(pixel_read[3], pixel_to_write[3], epsilon);
      } else {
        EXPECT_EQ(pixel_read[3], 1.0f);
      }
    }
  }

  // Check that 0 maps to 0 and 1.0f maps to 1.0f.
  const std::array<float, 4> pixel_zero = {0.0f, 0.0f, 0.0f, 1.0f};
  avifSetRGBAPixel(&rgb, 0, 0, &color_space, pixel_zero.data());
  avifGetRGBAPixel(&rgb, 0, 0, &color_space, pixel_read.data());
  EXPECT_EQ(pixel_read[0], pixel_zero[0]);
  EXPECT_EQ(pixel_read[1], pixel_zero[1]);
  EXPECT_EQ(pixel_read[2], pixel_zero[2]);
  EXPECT_EQ(pixel_read[3], pixel_zero[3]);

  const std::array<float, 4> pixel_one = {1.0f, 1.0f, 1.0f, 1.0f};
  avifSetRGBAPixel(&rgb, 0, 0, &color_space, pixel_one.data());
  avifGetRGBAPixel(&rgb, 0, 0, &color_space, pixel_read.data());
  EXPECT_EQ(pixel_read[0], pixel_one[0]);
  EXPECT_EQ(pixel_read[1], pixel_one[1]);
  EXPECT_EQ(pixel_read[2], pixel_one[2]);
  EXPECT_EQ(pixel_read[3], pixel_one[3]);
}

TEST_P(SetGetRGBATest, GradientTest) {
  const int rgb_depth = std::get<0>(GetParam());
  const avifRGBFormat rgb_format = std::get<1>(GetParam());
  const bool is_float = std::get<2>(GetParam());

  // Only used for convenience to generate RGB values.
  ImagePtr yuv =
      testutil::CreateImage(/*width=*/13, /*height=*/17, /*depth=*/8,
                            AVIF_PIXEL_FORMAT_YUV444, AVIF_PLANES_ALL);
  testutil::FillImageGradient(yuv.get());

  testutil::AvifRgbImage input_rgb(yuv.get(), rgb_depth, rgb_format);
  testutil::AvifRgbImage output_rgb(yuv.get(), rgb_depth, rgb_format);
  input_rgb.isFloat = is_float;
  output_rgb.isFloat = is_float;
  ASSERT_EQ(avifImageYUVToRGB(yuv.get(), &input_rgb), AVIF_RESULT_OK);

  avifRGBColorSpaceInfo color_space;
  ASSERT_TRUE(avifGetRGBColorSpaceInfo(&input_rgb, &color_space));

  for (uint32_t j = 0; j < input_rgb.height; ++j) {
    for (uint32_t i = 0; i < input_rgb.width; ++i) {
      std::array<float, 4> pixel;
      avifGetRGBAPixel(&input_rgb, i, j, &color_space, pixel.data());
      avifSetRGBAPixel(&output_rgb, i, j, &color_space, pixel.data());
    }
  }
  EXPECT_TRUE(testutil::AreImagesEqual(input_rgb, output_rgb));
}

TEST(YuvToRgbTest, FloatOutputWithUnalignedOddPaddedStride) {
  constexpr uint32_t kWidth = 1;
  constexpr uint32_t kHeight = 4;
  ImagePtr yuv = testutil::CreateImage(
      kWidth, kHeight, /*depth=*/16, AVIF_PIXEL_FORMAT_YUV400, AVIF_PLANES_YUV);
  ASSERT_NE(yuv, nullptr);

  const uint16_t samples[kHeight] = {0x1000, 0x4000, 0x8000, 0xf000};
  for (uint32_t y = 0; y < kHeight; ++y) {
    uint16_t* row = reinterpret_cast<uint16_t*>(
        yuv->yuvPlanes[AVIF_CHAN_Y] +
        static_cast<size_t>(y) * yuv->yuvRowBytes[AVIF_CHAN_Y]);
    row[0] = samples[y];
  }

  testutil::AvifRgbImage tight(yuv.get(), /*depth=*/16, AVIF_RGB_FORMAT_RGBA);
  tight.isFloat = true;
  ASSERT_EQ(avifImageYUVToRGB(yuv.get(), &tight), AVIF_RESULT_OK);

  avifRGBImage padded;
  avifRGBImageSetDefaults(&padded, yuv.get());
  padded.depth = 16;
  padded.format = AVIF_RGB_FORMAT_RGBA;
  padded.isFloat = true;
  padded.rowBytes = tight.rowBytes + 1;
  std::vector<uint8_t> pixels(
      static_cast<size_t>(padded.rowBytes) * kHeight + 1, 0xa5);
  padded.pixels = pixels.data() + 1;

  ASSERT_EQ(avifImageYUVToRGB(yuv.get(), &padded), AVIF_RESULT_OK);
  for (uint32_t y = 0; y < kHeight; ++y) {
    EXPECT_EQ(
        std::memcmp(padded.pixels + static_cast<size_t>(y) * padded.rowBytes,
                    tight.pixels + static_cast<size_t>(y) * tight.rowBytes,
                    tight.rowBytes),
        0)
        << "row " << y;
    EXPECT_EQ(padded.pixels[static_cast<size_t>(y + 1) * padded.rowBytes - 1],
              0xa5)
        << "padding byte for row " << y;
  }
}

TEST(RgbToYuvTest, InputWithUnalignedOddPaddedStride) {
  constexpr uint32_t kWidth = 1;
  constexpr uint32_t kHeight = 4;
  ImagePtr tight_yuv(
      avifImageCreate(kWidth, kHeight, /*depth=*/16, AVIF_PIXEL_FORMAT_YUV444));
  ImagePtr padded_yuv(
      avifImageCreate(kWidth, kHeight, /*depth=*/16, AVIF_PIXEL_FORMAT_YUV444));
  ASSERT_NE(tight_yuv, nullptr);
  ASSERT_NE(padded_yuv, nullptr);

  testutil::AvifRgbImage tight(tight_yuv.get(), /*depth=*/16,
                               AVIF_RGB_FORMAT_RGBA);
  tight.avoidLibYUV = true;
  avifRGBColorSpaceInfo color_space;
  ASSERT_TRUE(avifGetRGBColorSpaceInfo(&tight, &color_space));
  for (uint32_t y = 0; y < kHeight; ++y) {
    const std::array<float, 4> pixel = {
        static_cast<float>(y + 1) / 8.0f, static_cast<float>(y + 2) / 8.0f,
        static_cast<float>(y + 3) / 8.0f, static_cast<float>(y + 4) / 8.0f};
    avifSetRGBAPixel(&tight, 0, y, &color_space, pixel.data());
  }

  avifRGBImage padded;
  avifRGBImageSetDefaults(&padded, padded_yuv.get());
  padded.depth = 16;
  padded.format = AVIF_RGB_FORMAT_RGBA;
  padded.avoidLibYUV = true;
  padded.rowBytes = tight.rowBytes + 1;
  std::vector<uint8_t> pixels(
      static_cast<size_t>(padded.rowBytes) * kHeight + 1, 0xa5);
  padded.pixels = pixels.data() + 1;
  for (uint32_t y = 0; y < kHeight; ++y) {
    std::memcpy(padded.pixels + static_cast<size_t>(y) * padded.rowBytes,
                tight.pixels + static_cast<size_t>(y) * tight.rowBytes,
                tight.rowBytes);
  }

  ASSERT_EQ(avifImageRGBToYUV(tight_yuv.get(), &tight), AVIF_RESULT_OK);
  ASSERT_EQ(avifImageRGBToYUV(padded_yuv.get(), &padded), AVIF_RESULT_OK);
  EXPECT_TRUE(testutil::AreImagesEqual(*tight_yuv, *padded_yuv));
  for (uint32_t y = 0; y < kHeight; ++y) {
    EXPECT_EQ(padded.pixels[static_cast<size_t>(y + 1) * padded.rowBytes - 1],
              0xa5)
        << "padding byte for row " << y;
  }
}

INSTANTIATE_TEST_SUITE_P(
    NonFloatNonRgb565, SetGetRGBATest,
    Combine(/*rgb_depth=*/Values(8, 10, 12, 16),
            Values(AVIF_RGB_FORMAT_RGB, AVIF_RGB_FORMAT_RGBA,
                   AVIF_RGB_FORMAT_ARGB, AVIF_RGB_FORMAT_BGR,
                   AVIF_RGB_FORMAT_BGRA, AVIF_RGB_FORMAT_ABGR),
            /*is_float=*/Values(false)));

INSTANTIATE_TEST_SUITE_P(Rgb565, SetGetRGBATest,
                         Combine(/*rgb_depth=*/Values(8),
                                 Values(AVIF_RGB_FORMAT_RGB_565),
                                 /*is_float=*/Values(false)));

INSTANTIATE_TEST_SUITE_P(
    Float, SetGetRGBATest,
    Combine(/*rgb_depth=*/Values(16),
            Values(AVIF_RGB_FORMAT_RGB, AVIF_RGB_FORMAT_RGBA,
                   AVIF_RGB_FORMAT_ARGB, AVIF_RGB_FORMAT_BGR,
                   AVIF_RGB_FORMAT_BGRA, AVIF_RGB_FORMAT_ABGR),
            /*is_float=*/Values(true)));

}  // namespace
}  // namespace avif
