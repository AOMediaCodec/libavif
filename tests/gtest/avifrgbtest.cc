// Copyright 2023 Google LLC
// SPDX-License-Identifier: BSD-2-Clause

#include <array>
#include <cmath>
#include <cstring>
#include <limits>
#include <tuple>

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

// -----------------------------------------------------------------------------
// IEEE 754 binary16 (half precision) tests.
//
// Regression tests for the avifF16ToFloat()/avifFloatToF16() conversions used
// by avifGetRGBAPixel()/avifSetRGBAPixel() on avifRGBImage::isFloat images.
// The previous implementation misinterpreted the sign bit as part of the
// exponent: -1.0h (0xBC00) decoded as +4294967296.0f, +Inf (0x7C00) as the
// finite 65536.0f, NaN (0x7E00) as 98304.0f and -0.0h (0x8000) as +131072.0f.

// Reference binary16 decoder, straight from the IEEE 754 format definition.
float ReferenceF16ToFloat(uint16_t bits) {
  const float sign = (bits & 0x8000u) ? -1.0f : 1.0f;
  const uint32_t exponent = (bits >> 10) & 0x1Fu;
  const uint32_t mantissa = bits & 0x3FFu;
  if (exponent == 0x1Fu) {
    return (mantissa == 0) ? sign * std::numeric_limits<float>::infinity()
                           : std::numeric_limits<float>::quiet_NaN();
  }
  if (exponent == 0) {
    // Subnormal: mantissa * 2^-24.
    return sign * std::ldexp(static_cast<float>(mantissa), -24);
  }
  return sign * std::ldexp(1.0f + static_cast<float>(mantissa) / 1024.0f,
                           static_cast<int>(exponent) - 15);
}

class F16Test : public testing::Test {
 protected:
  void SetUp() override {
    // Unused yuv image, simply needed to initialize the rgb image.
    yuv_ = testutil::CreateImage(/*width=*/1, /*height=*/1, /*depth=*/10,
                                 AVIF_PIXEL_FORMAT_YUV444, AVIF_PLANES_ALL);
    rgb_ = std::make_unique<testutil::AvifRgbImage>(yuv_.get(), /*depth=*/16,
                                                    AVIF_RGB_FORMAT_RGBA);
    rgb_->isFloat = true;
    ASSERT_TRUE(avifGetRGBColorSpaceInfo(&*rgb_, &color_space_));
  }

  uint16_t RawChannel(int channel) const {
    uint16_t value;
    std::memcpy(&value, rgb_->pixels + (size_t)channel * sizeof(uint16_t),
                sizeof(value));
    return value;
  }

  void SetRawChannel(int channel, uint16_t bits) {
    std::memcpy(rgb_->pixels + (size_t)channel * sizeof(uint16_t), &bits,
                sizeof(bits));
  }

  ImagePtr yuv_;
  std::unique_ptr<testutil::AvifRgbImage> rgb_;
  avifRGBColorSpaceInfo color_space_ = {};
};

TEST_F(F16Test, GetPixelDecodesAllHalfPrecisionPatterns) {
  // Exhaustive sweep: each of the 65536 binary16 bit patterns must decode to
  // its exact IEEE 754 value.
  for (uint32_t bits = 0; bits <= 0xFFFF; ++bits) {
    const uint16_t pattern = static_cast<uint16_t>(bits);
    SetRawChannel(0, pattern);
    std::array<float, 4> rgba;
    avifGetRGBAPixel(&*rgb_, 0, 0, &color_space_, rgba.data());
    const float reference = ReferenceF16ToFloat(pattern);
    if (std::isnan(reference)) {
      EXPECT_TRUE(std::isnan(rgba[0])) << std::hex << pattern;
    } else {
      // Bit exact comparison so that -0.0h and tiny subnormals are covered.
      EXPECT_EQ(std::memcmp(&rgba[0], &reference, sizeof(float)), 0)
          << std::hex << pattern << " expected " << reference << ", got "
          << rgba[0];
    }
  }
}

TEST_F(F16Test, PreviouslyMisdecodedValues) {
  const struct {
    uint16_t bits;
    float expected;
  } cases[] = {
      {0xBC00, -1.0f},       // Was decoded as +4294967296.0f.
      {0xA400, -0.015625f},  // Was decoded as +67108864.0f.
      {0x7C00, std::numeric_limits<float>::infinity()},   // Was 65536.0f.
      {0xFC00, -std::numeric_limits<float>::infinity()},  // Was +2.8e14f.
      {0x7E00, std::numeric_limits<float>::quiet_NaN()},  // Was 98304.0f.
      {0x8000, -0.0f},           // Was decoded as +131072.0f.
      {0x0001, 5.9604645e-08f},  // Smallest subnormal (already worked).
  };
  for (const auto& value : cases) {
    SetRawChannel(0, value.bits);
    std::array<float, 4> rgba;
    avifGetRGBAPixel(&*rgb_, 0, 0, &color_space_, rgba.data());
    if (std::isnan(value.expected)) {
      EXPECT_TRUE(std::isnan(rgba[0])) << std::hex << value.bits;
    } else {
      EXPECT_EQ(std::memcmp(&rgba[0], &value.expected, sizeof(float)), 0)
          << std::hex << value.bits << " expected " << value.expected
          << ", got " << rgba[0];
    }
  }
}

TEST_F(F16Test, SetPixelEncodesValuesWithinContractRange) {
  // avifSetRGBAPixel documents channel values in [0, 1] (see its assertions).
  // Every non-negative binary16 pattern whose value is <= 1.0f must encode back
  // to its original bits (round-to-nearest-even, subnormals included).
  for (uint32_t bits = 0; bits <= 0x7FFF; ++bits) {
    const uint16_t pattern = static_cast<uint16_t>(bits);
    const float value = ReferenceF16ToFloat(pattern);
    if (!(value >= 0.0f) || value > 1.0f) {
      continue;  // Skip negatives and out-of-contract values.
    }
    const std::array<float, 4> rgba = {value, value, value, value};
    avifSetRGBAPixel(&*rgb_, 0, 0, &color_space_, rgba.data());
    EXPECT_EQ(RawChannel(0), pattern)
        << std::hex << pattern << " (" << value << ")";
  }
}

TEST_F(F16Test, SetPixelHandlesSpecialValues) {
  const struct {
    float value;
    uint16_t expected;
    const char* description;
  } cases[] = {
      {0.0f, 0x0000, "zero"},
      {1.0f, 0x3C00, "one"},
      {0.5f, 0x3800, "half"},
      {0.25f, 0x3400, "quarter"},
      {5.9604645e-08f, 0x0001, "smallest subnormal"},
      {2.9802322e-08f, 0x0000, "half of it: tie, rounds to even (zero)"},
      {4.4703484e-08f, 0x0001, "three quarters of it: rounds to 0x0001"},
      {0.999755859f, 0x3C00, "halfway between 0x3BFF and 0x3C00: even"},
      {0.999267578f, 0x3BFE, "halfway between 0x3BFE and 0x3BFF: even"},
  };
  for (const auto& value : cases) {
    const std::array<float, 4> rgba = {value.value, value.value, value.value,
                                       value.value};
    avifSetRGBAPixel(&*rgb_, 0, 0, &color_space_, rgba.data());
    EXPECT_EQ(RawChannel(0), value.expected)
        << value.description << " (" << value.value << ") -> " << std::hex
        << RawChannel(0);
  }
}

}  // namespace
}  // namespace avif
