// Copyright 2022 Google LLC. All rights reserved.
// SPDX-License-Identifier: BSD-2-Clause

#include <algorithm>
#include <cmath>
#include <memory>
#include <tuple>

#include "avif/avif.h"
#include "aviftest_helpers.h"
#include "gtest/gtest.h"

using ::testing::Combine;
using ::testing::Values;
using ::testing::ValuesIn;

namespace libavif {
namespace {

//------------------------------------------------------------------------------

// Modifies the pixel values of a channel in image by modifier[] (row-ordered).
template <typename PixelType>
void ModifyImageChannel(avifRGBImage* image, uint32_t channel_offset,
                        const int32_t modifier[]) {
  const uint32_t channel_count = avifRGBFormatChannelCount(image->format);
  assert(channel_offset < channel_count);
  for (uint32_t y = 0, i = 0; y < image->height; ++y) {
    PixelType* pixel =
        reinterpret_cast<PixelType*>(image->pixels + image->rowBytes * y);
    for (uint32_t x = 0; x < image->width; ++x, ++i) {
      pixel[channel_offset] += modifier[i];
      pixel += channel_count;
    }
  }
}

void ModifyImageChannel(avifRGBImage* image, uint32_t channel_offset,
                        const int32_t modifier[]) {
  (image->depth <= 8)
      ? ModifyImageChannel<uint8_t>(image, channel_offset, modifier)
      : ModifyImageChannel<uint16_t>(image, channel_offset, modifier);
}

// Fills the image channel with the given value, and modifies the individual
// pixel values of that channel with the modifier, if not null.
void SetImageChannel(avifRGBImage* image, uint32_t channel_offset,
                     uint32_t value, const int32_t modifier[]) {
  testutil::FillImageChannel(image, channel_offset, value);
  if (modifier) {
    ModifyImageChannel(image, channel_offset, modifier);
  }
}

// Accumulates stats about the differences between the images a and b.
template <typename PixelType>
void GetDiffSumAndSqDiffSum(const avifRGBImage& a, const avifRGBImage& b,
                            int64_t* diff_sum, int64_t* abs_diff_sum,
                            int64_t* sq_diff_sum, int64_t* max_abs_diff) {
  const uint32_t channel_count = avifRGBFormatChannelCount(a.format);
  for (uint32_t y = 0; y < a.height; ++y) {
    const PixelType* row_a =
        reinterpret_cast<PixelType*>(a.pixels + a.rowBytes * y);
    const PixelType* row_b =
        reinterpret_cast<PixelType*>(b.pixels + b.rowBytes * y);
    for (uint32_t x = 0; x < a.width * channel_count; ++x) {
      const int64_t diff =
          static_cast<int64_t>(row_b[x]) - static_cast<int64_t>(row_a[x]);
      *diff_sum += diff;
      *abs_diff_sum += std::abs(diff);
      *sq_diff_sum += diff * diff;
      *max_abs_diff = std::max(*max_abs_diff, std::abs(diff));
    }
  }
}

void GetDiffSumAndSqDiffSum(const avifRGBImage& a, const avifRGBImage& b,
                            int64_t* diff_sum, int64_t* abs_diff_sum,
                            int64_t* sq_diff_sum, int64_t* max_abs_diff) {
  (a.depth <= 8) ? GetDiffSumAndSqDiffSum<uint8_t>(a, b, diff_sum, abs_diff_sum,
                                                   sq_diff_sum, max_abs_diff)
                 : GetDiffSumAndSqDiffSum<uint16_t>(
                       a, b, diff_sum, abs_diff_sum, sq_diff_sum, max_abs_diff);
}

// Returns the Peak Signal-to-Noise Ratio from accumulated stats.
double GetPsnr(double sq_diff_sum, double num_diffs, double max_abs_diff) {
  if (sq_diff_sum == 0.) {
    return 99.;  // Lossless.
  }
  const double distortion =
      sq_diff_sum / (num_diffs * max_abs_diff * max_abs_diff);
  return (distortion > 0.) ? std::min(-10 * std::log10(distortion), 98.9)
                           : 98.9;  // Not lossless.
}

//------------------------------------------------------------------------------

class RGBToYUVTest
    : public testing::TestWithParam<
          std::tuple</*rgb_depth=*/int, /*yuv_depth=*/int, avifRGBFormat,
                     avifPixelFormat, avifRange, avifMatrixCoefficients,
                     /*add_noise=*/bool, /*rgb_step=*/uint32_t,
                     /*max_abs_average_diff=*/double, /*min_psnr=*/double>> {};

// Converts from RGB to YUV and back to RGB for all RGB combinations, separated
// by a color step for reasonable timing. If add_noise is true, also applies
// some noise to the input samples to exercise chroma subsampling.
TEST_P(RGBToYUVTest, Convert) {
  const int rgb_depth = std::get<0>(GetParam());
  const int yuv_depth = std::get<1>(GetParam());
  const avifRGBFormat rgb_format = std::get<2>(GetParam());
  const avifPixelFormat yuv_format = std::get<3>(GetParam());
  const avifRange yuv_range = std::get<4>(GetParam());
  const avifMatrixCoefficients matrix_coefficients = std::get<5>(GetParam());
  // Whether to add noise to the input RGB samples. Should only impact
  // subsampled chroma (4:2:2 and 4:2:0).
  const bool add_noise = std::get<6>(GetParam());
  // Testing each RGB combination would be more accurate but results are similar
  // with faster settings.
  const uint32_t rgb_step = std::get<7>(GetParam());
  // Thresholds to pass.
  const double max_abs_average_diff = std::get<8>(GetParam());
  const double min_psnr = std::get<9>(GetParam());
  // Deduced constants.
  const bool is_monochrome =
      (yuv_format == AVIF_PIXEL_FORMAT_YUV400);  // If so, only test grey input.
  const uint32_t rgb_max = (1 << rgb_depth) - 1;

  // The YUV upsampling treats the first and last rows and columns differently
  // than the remaining pairs of rows and columns. An image of 16 pixels is used
  // to test all these possibilities.
  static constexpr int width = 4;
  static constexpr int height = 4;
  std::unique_ptr<avifImage, decltype(&avifImageDestroy)> yuv(
      avifImageCreate(width, height, yuv_depth, yuv_format), avifImageDestroy);
  yuv->matrixCoefficients = matrix_coefficients;
  yuv->yuvRange = yuv_range;
  testutil::AvifRgbImage src_rgb(yuv.get(), rgb_depth, rgb_format);
  testutil::AvifRgbImage dst_rgb(yuv.get(), rgb_depth, rgb_format);
  const testutil::RgbChannelOffsets offsets =
      testutil::GetRgbChannelOffsets(rgb_format);

  // Alpha values are not tested here. Keep it opaque.
  if (avifRGBFormatHasAlpha(src_rgb.format)) {
    testutil::FillImageChannel(&src_rgb, offsets.a, rgb_max);
  }

  // To exercise the chroma subsampling loss, the input samples must differ in
  // each of the RGB channels. Chroma subsampling expects the input RGB channels
  // to be correlated to minimize the quality loss.
  static constexpr int32_t kRedNoise[] = {
      7,  14, 11, 5,   // Random permutation of 16 values.
      4,  6,  8,  15,  //
      2,  9,  13, 3,   //
      12, 1,  10, 0};
  static constexpr int32_t kGreenNoise[] = {
      3,  2,  12, 15,  // Random permutation of 16 values
      14, 10, 7,  13,  // that is somewhat close to kRedNoise.
      5,  1,  9,  0,   //
      8,  4,  11, 6};
  static constexpr int32_t kBlueNoise[] = {
      0,  8,  14, 9,   // Random permutation of 16 values
      13, 12, 2,  7,   // that is somewhat close to kGreenNoise.
      3,  1,  11, 10,  //
      6,  15, 5,  4};
  static constexpr int32_t* kPlainColor = nullptr;

  // Estimate the loss from converting RGB values to YUV and back.
  int64_t diff_sum = 0, abs_diff_sum = 0, sq_diff_sum = 0, max_abs_diff = 0;
  int64_t num_diffs = 0;
  const uint32_t max_value = rgb_max - (add_noise ? 15 : 0);
  for (uint32_t r = 0; r < max_value + rgb_step; r += rgb_step) {
    r = std::min(r, max_value);  // Test the maximum sample value even if it is
                                 // not a multiple of rgb_step.
    SetImageChannel(&src_rgb, offsets.r, r,
                    add_noise ? kRedNoise : kPlainColor);

    if (is_monochrome) {
      // Test only greyish input when converting to a single channel.
      SetImageChannel(&src_rgb, offsets.g, r,
                      add_noise ? kGreenNoise : kPlainColor);
      SetImageChannel(&src_rgb, offsets.b, r,
                      add_noise ? kBlueNoise : kPlainColor);

      // Change these to BEST_QUALITY to force built-in over libyuv conversion.
      src_rgb.chromaDownsampling = AVIF_CHROMA_DOWNSAMPLING_AUTOMATIC;
      dst_rgb.chromaUpsampling = AVIF_CHROMA_UPSAMPLING_AUTOMATIC;

      ASSERT_EQ(avifImageRGBToYUV(yuv.get(), &src_rgb), AVIF_RESULT_OK);
      ASSERT_EQ(avifImageYUVToRGB(yuv.get(), &dst_rgb), AVIF_RESULT_OK);
      GetDiffSumAndSqDiffSum(src_rgb, dst_rgb, &diff_sum, &abs_diff_sum,
                             &sq_diff_sum, &max_abs_diff);
      num_diffs += src_rgb.width * src_rgb.height * 3;  // Alpha is lossless.
    } else {
      for (uint32_t g = 0; g < max_value + rgb_step; g += rgb_step) {
        g = std::min(g, max_value);
        SetImageChannel(&src_rgb, offsets.g, g,
                        add_noise ? kGreenNoise : kPlainColor);
        for (uint32_t b = 0; b < max_value + rgb_step; b += rgb_step) {
          b = std::min(b, max_value);
          SetImageChannel(&src_rgb, offsets.b, b,
                          add_noise ? kBlueNoise : kPlainColor);

          ASSERT_EQ(avifImageRGBToYUV(yuv.get(), &src_rgb), AVIF_RESULT_OK);
          ASSERT_EQ(avifImageYUVToRGB(yuv.get(), &dst_rgb), AVIF_RESULT_OK);
          GetDiffSumAndSqDiffSum(src_rgb, dst_rgb, &diff_sum, &abs_diff_sum,
                                 &sq_diff_sum, &max_abs_diff);
          num_diffs +=
              src_rgb.width * src_rgb.height * 3;  // Alpha is lossless.
        }
      }
    }
  }

  // Stats and thresholds.
  // Note: The thresholds defined in this test are calibrated for libyuv fast
  //       paths. See reformat_libyuv.c. Slower non-libyuv conversions in
  //       libavif have a higher precision (using floating point operations).
  const double average_diff =
      static_cast<double>(diff_sum) / static_cast<double>(num_diffs);
  const double average_abs_diff =
      static_cast<double>(abs_diff_sum) / static_cast<double>(num_diffs);
  const double psnr = GetPsnr(sq_diff_sum, num_diffs, rgb_max);
  EXPECT_LE(std::abs(average_diff), max_abs_average_diff);
  EXPECT_GE(psnr, min_psnr);

  // Print stats for convenience and easier threshold tuning.
  static constexpr const char* kAvifRgbFormatToString[] = {
      "RGB", "RGBA", "ARGB", "BGR", "BGRA", "ABGR"};
  std::cout << " RGB " << rgb_depth << " bits, YUV " << yuv_depth << " bits, "
            << kAvifRgbFormatToString[rgb_format] << ", "
            << avifPixelFormatToString(yuv_format) << ", "
            << (yuv_range ? "full" : "lmtd") << ", MC " << matrix_coefficients
            << ", " << (add_noise ? "noisy" : "plain") << ", avg "
            << average_diff << ", abs avg " << average_abs_diff << ", max "
            << max_abs_diff << ", PSNR " << psnr << "dB" << std::endl;
}

constexpr avifRGBFormat kAllRgbFormats[] = {
    AVIF_RGB_FORMAT_RGB, AVIF_RGB_FORMAT_RGBA, AVIF_RGB_FORMAT_ARGB,
    AVIF_RGB_FORMAT_BGR, AVIF_RGB_FORMAT_BGRA, AVIF_RGB_FORMAT_ABGR};

// This is the default avifenc setup when encoding from 8b PNG files to AVIF.
INSTANTIATE_TEST_SUITE_P(
    DefaultFormat, RGBToYUVTest,
    Combine(/*rgb_depth=*/Values(8),
            /*yuv_depth=*/Values(8), Values(AVIF_RGB_FORMAT_RGBA),
            Values(AVIF_PIXEL_FORMAT_YUV420), Values(AVIF_RANGE_FULL),
            Values(AVIF_MATRIX_COEFFICIENTS_BT601),
            /*add_noise=*/Values(true),
            /*rgb_step=*/Values(3),
            /*max_abs_average_diff=*/Values(0.1),  // The color drift is almost
                                                   // centered.
            /*min_psnr=*/Values(36.)  // Subsampling distortion is acceptable.
            ));

// Keeping RGB samples in full range and same or higher bit depth should not
// bring any loss in the roundtrip.
INSTANTIATE_TEST_SUITE_P(Identity8b, RGBToYUVTest,
                         Combine(/*rgb_depth=*/Values(8),
                                 /*yuv_depth=*/Values(8, 10, 12),
                                 ValuesIn(kAllRgbFormats),
                                 Values(AVIF_PIXEL_FORMAT_YUV444),
                                 Values(AVIF_RANGE_FULL),
                                 Values(AVIF_MATRIX_COEFFICIENTS_IDENTITY),
                                 /*add_noise=*/Values(true),
                                 /*rgb_step=*/Values(31),
                                 /*max_abs_average_diff=*/Values(0.),
                                 /*min_psnr=*/Values(99.)));
INSTANTIATE_TEST_SUITE_P(Identity10b, RGBToYUVTest,
                         Combine(/*rgb_depth=*/Values(10),
                                 /*yuv_depth=*/Values(10, 12),
                                 ValuesIn(kAllRgbFormats),
                                 Values(AVIF_PIXEL_FORMAT_YUV444),
                                 Values(AVIF_RANGE_FULL),
                                 Values(AVIF_MATRIX_COEFFICIENTS_IDENTITY),
                                 /*add_noise=*/Values(true),
                                 /*rgb_step=*/Values(101),
                                 /*max_abs_average_diff=*/Values(0.),
                                 /*min_psnr=*/Values(99.)));
INSTANTIATE_TEST_SUITE_P(Identity12b, RGBToYUVTest,
                         Combine(/*rgb_depth=*/Values(12),
                                 /*yuv_depth=*/Values(12),
                                 ValuesIn(kAllRgbFormats),
                                 Values(AVIF_PIXEL_FORMAT_YUV444),
                                 Values(AVIF_RANGE_FULL),
                                 Values(AVIF_MATRIX_COEFFICIENTS_IDENTITY),
                                 /*add_noise=*/Values(true),
                                 /*rgb_step=*/Values(401),
                                 /*max_abs_average_diff=*/Values(0.),
                                 /*min_psnr=*/Values(99.)));

// 4:4:4 and chroma subsampling have similar distortions on plain color inputs.
INSTANTIATE_TEST_SUITE_P(
    PlainAnySubsampling8b, RGBToYUVTest,
    Combine(
        /*rgb_depth=*/Values(8),
        /*yuv_depth=*/Values(8), ValuesIn(kAllRgbFormats),
        Values(AVIF_PIXEL_FORMAT_YUV444, AVIF_PIXEL_FORMAT_YUV422,
               AVIF_PIXEL_FORMAT_YUV420),
        Values(AVIF_RANGE_FULL), Values(AVIF_MATRIX_COEFFICIENTS_BT601),
        /*add_noise=*/Values(false),
        /*rgb_step=*/Values(17),
        /*max_abs_average_diff=*/Values(0.02),  // The color drift is centered.
        /*min_psnr=*/Values(49.)  // RGB>YUV>RGB distortion is barely
                                  // noticeable.
        ));

// Converting grey RGB samples to full-range monochrome of same or greater bit
// depth should be lossless.
INSTANTIATE_TEST_SUITE_P(MonochromeLossless8b, RGBToYUVTest,
                         Combine(/*rgb_depth=*/Values(8),
                                 /*yuv_depth=*/Values(8, 10, 12),
                                 ValuesIn(kAllRgbFormats),
                                 Values(AVIF_PIXEL_FORMAT_YUV400),
                                 Values(AVIF_RANGE_FULL),
                                 Values(AVIF_MATRIX_COEFFICIENTS_BT601),
                                 /*add_noise=*/Values(false),
                                 /*rgb_step=*/Values(1),
                                 /*max_abs_average_diff=*/Values(0.),
                                 /*min_psnr=*/Values(99.)));
INSTANTIATE_TEST_SUITE_P(MonochromeLossless10b, RGBToYUVTest,
                         Combine(/*rgb_depth=*/Values(10),
                                 /*yuv_depth=*/Values(10, 12),
                                 ValuesIn(kAllRgbFormats),
                                 Values(AVIF_PIXEL_FORMAT_YUV400),
                                 Values(AVIF_RANGE_FULL),
                                 Values(AVIF_MATRIX_COEFFICIENTS_BT601),
                                 /*add_noise=*/Values(false),
                                 /*rgb_step=*/Values(1),
                                 /*max_abs_average_diff=*/Values(0.),
                                 /*min_psnr=*/Values(99.)));
INSTANTIATE_TEST_SUITE_P(MonochromeLossless12b, RGBToYUVTest,
                         Combine(/*rgb_depth=*/Values(12),
                                 /*yuv_depth=*/Values(12),
                                 ValuesIn(kAllRgbFormats),
                                 Values(AVIF_PIXEL_FORMAT_YUV400),
                                 Values(AVIF_RANGE_FULL),
                                 Values(AVIF_MATRIX_COEFFICIENTS_BT601),
                                 /*add_noise=*/Values(false),
                                 /*rgb_step=*/Values(1),
                                 /*max_abs_average_diff=*/Values(0.),
                                 /*min_psnr=*/Values(99.)));

// Can be used to print the drift of all RGB to YUV conversion possibilities.
// Also used for coverage.
INSTANTIATE_TEST_SUITE_P(
    All8b, RGBToYUVTest,
    Combine(/*rgb_depth=*/Values(8),
            /*yuv_depth=*/Values(8, 10, 12), ValuesIn(kAllRgbFormats),
            Values(AVIF_PIXEL_FORMAT_YUV444, AVIF_PIXEL_FORMAT_YUV422,
                   AVIF_PIXEL_FORMAT_YUV420),
            Values(AVIF_RANGE_LIMITED, AVIF_RANGE_FULL),
            Values(AVIF_MATRIX_COEFFICIENTS_BT601),
            /*add_noise=*/Values(false, true),
            /*rgb_step=*/Values(61),  // High or it would be too slow.
            /*max_abs_average_diff=*/Values(1.),  // Not very accurate because
                                                  // of high rgb_step.
            /*min_psnr=*/Values(36.)));
INSTANTIATE_TEST_SUITE_P(
    All10b, RGBToYUVTest,
    Combine(/*rgb_depth=*/Values(10),
            /*yuv_depth=*/Values(8, 10, 12), ValuesIn(kAllRgbFormats),
            Values(AVIF_PIXEL_FORMAT_YUV444, AVIF_PIXEL_FORMAT_YUV422,
                   AVIF_PIXEL_FORMAT_YUV420),
            Values(AVIF_RANGE_LIMITED, AVIF_RANGE_FULL),
            Values(AVIF_MATRIX_COEFFICIENTS_BT601),
            /*add_noise=*/Values(false, true),
            /*rgb_step=*/Values(211),  // High or it would be too slow.
            /*max_abs_average_diff=*/Values(0.2),  // Not very accurate because
                                                   // of high rgb_step.
            /*min_psnr=*/Values(47.)));
INSTANTIATE_TEST_SUITE_P(
    All12b, RGBToYUVTest,
    Combine(/*rgb_depth=*/Values(12),
            /*yuv_depth=*/Values(8, 10, 12), ValuesIn(kAllRgbFormats),
            Values(AVIF_PIXEL_FORMAT_YUV444, AVIF_PIXEL_FORMAT_YUV422,
                   AVIF_PIXEL_FORMAT_YUV420),
            Values(AVIF_RANGE_LIMITED, AVIF_RANGE_FULL),
            Values(AVIF_MATRIX_COEFFICIENTS_BT601),
            /*add_noise=*/Values(false, true),
            /*rgb_step=*/Values(809),  // High or it would be too slow.
            /*max_abs_average_diff=*/Values(0.3),  // Not very accurate because
                                                   // of high rgb_step.
            /*min_psnr=*/Values(52.)));

// TODO: Test other matrix coefficients than identity and bt.601.

// This was used to estimate the quality loss of libyuv for RGB-to-YUV.
// Disabled because it takes a few minutes.
INSTANTIATE_TEST_SUITE_P(
    DISABLED_All8bTo8b, RGBToYUVTest,
    Combine(/*rgb_depth=*/Values(8),
            /*yuv_depth=*/Values(8), ValuesIn(kAllRgbFormats),
            Values(AVIF_PIXEL_FORMAT_YUV444, AVIF_PIXEL_FORMAT_YUV422,
                   AVIF_PIXEL_FORMAT_YUV420, AVIF_PIXEL_FORMAT_YUV400),
            Values(AVIF_RANGE_FULL, AVIF_RANGE_LIMITED),
            Values(AVIF_MATRIX_COEFFICIENTS_BT601),
            /*add_noise=*/Values(false, true),
            /*rgb_step=*/Values(3),  // way faster and 99% similar to rgb_step=1
            /*max_abs_average_diff=*/Values(10.),
            /*min_psnr=*/Values(10.)));

}  // namespace
}  // namespace libavif
