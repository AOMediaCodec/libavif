// Copyright 2022 Google LLC. All rights reserved.
// SPDX-License-Identifier: BSD-2-Clause

#include "avif/avif.h"

#include <algorithm>
#include <cmath>
#include <memory>
#include <tuple>

#include "aviftest_helpers.h"
#include "gtest/gtest.h"

using ::libavif::testutil::avifChannel;
using ::testing::Combine;
using ::testing::Values;
using ::testing::ValuesIn;

namespace libavif
{
namespace
{

//------------------------------------------------------------------------------

// Offsets the pixel values of the channel in image by modifier[] (row-ordered).
template <typename PixelType>
void modifyImageChannel(avifRGBImage * image, avifChannel channel, const int32_t modifier[])
{
    assert(channel != avifChannel::A || avifRGBFormatHasAlpha(image->format));
    const uint32_t channelCount = avifRGBFormatChannelCount(image->format);
    const uint32_t channelOffset = avifChannelOffset(image->format, channel);
    for (uint32_t y = 0, i = 0; y < image->height; ++y) {
        PixelType * pixel = reinterpret_cast<PixelType *>(image->pixels + image->rowBytes * y);
        for (uint32_t x = 0; x < image->width; ++x, ++i) {
            pixel[channelOffset] += modifier[i];
            pixel += channelCount;
        }
    }
}

void modifyImageChannel(avifRGBImage * image, avifChannel channel, const int32_t modifier[])
{
    (image->depth <= 8) ? modifyImageChannel<uint8_t>(image, channel, modifier) : modifyImageChannel<uint16_t>(image, channel, modifier);
}

// Fills the image channel with the given value, and offsets the individual pixel values of that channel with the modifier, if not null.
void setImageChannel(avifRGBImage * image, avifChannel channel, uint32_t value, const int32_t modifier[])
{
    testutil::fillImageChannel(image, channel, value);
    if (modifier) {
        modifyImageChannel(image, channel, modifier);
    }
}

// Accumulates stats about the differences between the images a and b.
template <typename PixelType>
void getDiffSumAndSqDiffSum(const avifRGBImage & a, const avifRGBImage & b, int64_t * diffSum, int64_t * absDiffSum, int64_t * sqDiffSum, int64_t * maxAbsDiff)
{
    const uint32_t channelCount = avifRGBFormatChannelCount(a.format);
    for (uint32_t y = 0; y < a.height; ++y) {
        const PixelType * rowA = reinterpret_cast<PixelType *>(a.pixels + a.rowBytes * y);
        const PixelType * rowB = reinterpret_cast<PixelType *>(b.pixels + b.rowBytes * y);
        for (uint32_t x = 0; x < a.width * channelCount; ++x) {
            const int64_t diff = static_cast<int64_t>(rowB[x]) - static_cast<int64_t>(rowA[x]);
            *diffSum += diff;
            *absDiffSum += std::abs(diff);
            *sqDiffSum += diff * diff;
            *maxAbsDiff = std::max(*maxAbsDiff, std::abs(diff));
        }
    }
}

void getDiffSumAndSqDiffSum(const avifRGBImage & a, const avifRGBImage & b, int64_t * diffSum, int64_t * absDiffSum, int64_t * sqDiffSum, int64_t * maxAbsDiff)
{
    (a.depth <= 8) ? getDiffSumAndSqDiffSum<uint8_t>(a, b, diffSum, absDiffSum, sqDiffSum, maxAbsDiff)
                   : getDiffSumAndSqDiffSum<uint16_t>(a, b, diffSum, absDiffSum, sqDiffSum, maxAbsDiff);
}

// Returns the Peak Signal-to-Noise Ratio from accumulated stats.
double getPsnr(double sqDiffSum, double numDiffs, double maxAbsDiff)
{
    if (sqDiffSum == 0.) {
        return 99.; // Lossless.
    }
    const double distortion = sqDiffSum / (numDiffs * maxAbsDiff * maxAbsDiff);
    return (distortion > 0.) ? std::min(-10 * std::log10(distortion), 98.9) : 98.9; // Not lossless.
}

//------------------------------------------------------------------------------

class YUVToRGBTest
    : public testing::TestWithParam<
          std::tuple</*rgbDepth=*/int, /*yuvDepth=*/int, avifRGBFormat, avifPixelFormat, avifRange, avifMatrixCoefficients, /*addNoise=*/bool, /*rgbStep=*/uint32_t, /*maxAbsAverageDiff=*/double, /*minPsnr=*/double>>
{
};

// Converts from RGB to YUV and back to RGB for all RGB combinations, separated by a color step for reasonable timing.
// If addNoise is true, also applies some noise to the input samples to exercise chroma subsampling.
TEST_P(YUVToRGBTest, Convert)
{
    const int rgbDepth = std::get<0>(GetParam());
    const int yuvDepth = std::get<1>(GetParam());
    const avifRGBFormat rgbFormat = std::get<2>(GetParam());
    const avifPixelFormat yuvFormat = std::get<3>(GetParam());
    const avifRange yuvRange = std::get<4>(GetParam());
    const avifMatrixCoefficients matrixCoefficients = std::get<5>(GetParam());
    // Whether to add noise to the input RGB samples. Should only impact subsampled chroma (4:2:2 and 4:2:0).
    const bool addNoise = std::get<6>(GetParam());
    // Testing each RGB combination would be more accurate but results are similar with faster settings.
    const uint32_t rgbStep = std::get<7>(GetParam());
    // Thresholds to pass.
    const double maxAbsAverageDiff = std::get<8>(GetParam());
    const double minPsnr = std::get<9>(GetParam());
    // Deduced constants.
    const bool isMonochrome = (yuvFormat == AVIF_PIXEL_FORMAT_YUV400); // If true, only test greyish input.
    const uint32_t rgbMax = (1 << rgbDepth) - 1;

    // The YUV upsampling treats the first and last rows and columns differently than the remaining pairs of rows and columns.
    // An image of 16 pixels is used to test all these possibilities.
    static constexpr int width = 4;
    static constexpr int height = 4;
    std::unique_ptr<avifImage, decltype(&avifImageDestroy)> yuv(avifImageCreate(width, height, yuvDepth, yuvFormat), avifImageDestroy);
    yuv->matrixCoefficients = matrixCoefficients;
    yuv->yuvRange = yuvRange;
    testutil::avifRGBImageCleaner srcRgb(yuv.get(), rgbDepth, rgbFormat);
    testutil::avifRGBImageCleaner dstRgb(yuv.get(), rgbDepth, rgbFormat);

    // Alpha values are not tested here. Keep it opaque.
    if (avifRGBFormatHasAlpha(srcRgb.format)) {
        testutil::fillImageChannel(&srcRgb, avifChannel::A, rgbMax);
    }

    // To exercise the chroma subsampling loss, the input samples must differ in each of the RGB channels.
    // Chroma subsampling expects the input RGB channels to be correlated to minimize the quality loss.
    static constexpr int32_t redNoise[] = { 7,  14, 11, 5,  // Random permutation of 16 values.
                                            4,  6,  8,  15, //
                                            2,  9,  13, 3,  //
                                            12, 1,  10, 0 };
    static constexpr int32_t greenNoise[] = { 3,  2,  12, 15, // Random permutation of 16 values
                                              14, 10, 7,  13, // that is somewhat close to redNoise.
                                              5,  1,  9,  0,  //
                                              8,  4,  11, 6 };
    static constexpr int32_t blueNoise[] = { 0,  8,  14, 9,  // Random permutation of 16 values
                                             13, 12, 2,  7,  // that is somewhat close to greenNoise.
                                             3,  1,  11, 10, //
                                             6,  15, 5,  4 };
    static constexpr int32_t * plainColor = nullptr;

    // Estimate the loss from converting RGB values to YUV and back.
    int64_t diffSum = 0, absDiffSum = 0, sqDiffSum = 0, maxAbsDiff = 0, numDiffs = 0;
    const uint32_t maxValue = rgbMax - (addNoise ? 15 : 0);
    for (uint32_t r = 0; r < maxValue + rgbStep; r += rgbStep) {
        r = std::min(r, maxValue); // Test the maximum sample value even if it is not a multiple of rgbStep.
        setImageChannel(&srcRgb, avifChannel::R, r, addNoise ? redNoise : plainColor);

        if (isMonochrome) {
            // Test only greyish input when converting to a single channel.
            setImageChannel(&srcRgb, avifChannel::G, r, addNoise ? greenNoise : plainColor);
            setImageChannel(&srcRgb, avifChannel::B, r, addNoise ? blueNoise : plainColor);

            ASSERT_EQ(avifImageRGBToYUV(yuv.get(), &srcRgb), AVIF_RESULT_OK);
            ASSERT_EQ(avifImageYUVToRGB(yuv.get(), &dstRgb), AVIF_RESULT_OK);
            getDiffSumAndSqDiffSum(srcRgb, dstRgb, &diffSum, &absDiffSum, &sqDiffSum, &maxAbsDiff);
            numDiffs += srcRgb.width * srcRgb.height * 3; // Alpha is lossless.
        } else {
            for (uint32_t g = 0; g < maxValue + rgbStep; g += rgbStep) {
                g = std::min(g, maxValue);
                setImageChannel(&srcRgb, avifChannel::G, g, addNoise ? greenNoise : plainColor);
                for (uint32_t b = 0; b < maxValue + rgbStep; b += rgbStep) {
                    b = std::min(b, maxValue);
                    setImageChannel(&srcRgb, avifChannel::B, b, addNoise ? blueNoise : plainColor);

                    ASSERT_EQ(avifImageRGBToYUV(yuv.get(), &srcRgb), AVIF_RESULT_OK);
                    ASSERT_EQ(avifImageYUVToRGB(yuv.get(), &dstRgb), AVIF_RESULT_OK);
                    getDiffSumAndSqDiffSum(srcRgb, dstRgb, &diffSum, &absDiffSum, &sqDiffSum, &maxAbsDiff);
                    numDiffs += srcRgb.width * srcRgb.height * 3; // Alpha is lossless.
                }
            }
        }
    }

    // Stats and thresholds.
    // Note: The thresholds defined in this test are calibrated for libyuv fast paths. See reformat_libyuv.c.
    //       Slower non-libyuv conversions in libavif have a higher precision (using floating point operations for example).
    const double averageDiff = static_cast<double>(diffSum) / static_cast<double>(numDiffs);
    const double averageAbsDiff = static_cast<double>(absDiffSum) / static_cast<double>(numDiffs);
    const double psnr = getPsnr(sqDiffSum, numDiffs, rgbMax);
    EXPECT_LE(std::abs(averageDiff), maxAbsAverageDiff);
    EXPECT_GE(psnr, minPsnr);

    // Print stats for convenience and easier threshold tuning.
    static constexpr const char * avifRGBFormatToString[] = { "RGB", "RGBA", "ARGB", "BGR", "BGRA", "ABGR" };
    std::cout << " RGB " << rgbDepth << " bits, YUV " << yuvDepth << " bits, " << avifRGBFormatToString[rgbFormat] << ", "
              << avifPixelFormatToString(yuvFormat) << ", " << (yuvRange ? "full" : "lmtd") << ", MC " << matrixCoefficients
              << ", " << (addNoise ? "noisy" : "plain") << ", avg " << averageDiff << ", abs avg " << averageAbsDiff << ", max "
              << maxAbsDiff << ", PSNR " << psnr << "dB" << std::endl;
}

constexpr avifRGBFormat allRgbFormats[] = { AVIF_RGB_FORMAT_RGB, AVIF_RGB_FORMAT_RGBA, AVIF_RGB_FORMAT_ARGB,
                                            AVIF_RGB_FORMAT_BGR, AVIF_RGB_FORMAT_BGRA, AVIF_RGB_FORMAT_ABGR };

// This is the default avifenc setup when encoding from 8b PNG files to AVIF.
INSTANTIATE_TEST_SUITE_P(DefaultFormat,
                         YUVToRGBTest,
                         Combine(/*rgbDepth=*/Values(8),
                                 /*yuvDepth=*/Values(8),
                                 Values(AVIF_RGB_FORMAT_RGBA),
                                 Values(AVIF_PIXEL_FORMAT_YUV420),
                                 Values(AVIF_RANGE_FULL),
                                 Values(AVIF_MATRIX_COEFFICIENTS_BT601),
                                 /*addNoise=*/Values(true),
                                 /*rgbStep=*/Values(3),
                                 /*maxAbsAverageDiff=*/Values(0.1), // The color drift is almost centered.
                                 /*minPsnr=*/Values(36.)            // Subsampling distortion is acceptable.
                                 ));

// Keeping RGB samples in full range and same or higher bit depth should not bring any loss in the roundtrip.
INSTANTIATE_TEST_SUITE_P(Identity8b,
                         YUVToRGBTest,
                         Combine(/*rgbDepth=*/Values(8),
                                 /*yuvDepth=*/Values(8, 10, 12),
                                 ValuesIn(allRgbFormats),
                                 Values(AVIF_PIXEL_FORMAT_YUV444),
                                 Values(AVIF_RANGE_FULL),
                                 Values(AVIF_MATRIX_COEFFICIENTS_IDENTITY),
                                 /*addNoise=*/Values(true),
                                 /*rgbStep=*/Values(31),
                                 /*maxAbsAverageDiff=*/Values(0.),
                                 /*minPsnr=*/Values(99.)));
INSTANTIATE_TEST_SUITE_P(Identity10b,
                         YUVToRGBTest,
                         Combine(/*rgbDepth=*/Values(10),
                                 /*yuvDepth=*/Values(10, 12),
                                 ValuesIn(allRgbFormats),
                                 Values(AVIF_PIXEL_FORMAT_YUV444),
                                 Values(AVIF_RANGE_FULL),
                                 Values(AVIF_MATRIX_COEFFICIENTS_IDENTITY),
                                 /*addNoise=*/Values(true),
                                 /*rgbStep=*/Values(101),
                                 /*maxAbsAverageDiff=*/Values(0.),
                                 /*minPsnr=*/Values(99.)));
INSTANTIATE_TEST_SUITE_P(Identity12b,
                         YUVToRGBTest,
                         Combine(/*rgbDepth=*/Values(12),
                                 /*yuvDepth=*/Values(12),
                                 ValuesIn(allRgbFormats),
                                 Values(AVIF_PIXEL_FORMAT_YUV444),
                                 Values(AVIF_RANGE_FULL),
                                 Values(AVIF_MATRIX_COEFFICIENTS_IDENTITY),
                                 /*addNoise=*/Values(true),
                                 /*rgbStep=*/Values(401),
                                 /*maxAbsAverageDiff=*/Values(0.),
                                 /*minPsnr=*/Values(99.)));

// 4:4:4 and chroma subsampling have similar distortions on plain color inputs.
INSTANTIATE_TEST_SUITE_P(PlainAnySubsampling8b,
                         YUVToRGBTest,
                         Combine(/*rgbDepth=*/Values(8),
                                 /*yuvDepth=*/Values(8),
                                 ValuesIn(allRgbFormats),
                                 Values(AVIF_PIXEL_FORMAT_YUV444, AVIF_PIXEL_FORMAT_YUV422, AVIF_PIXEL_FORMAT_YUV420),
                                 Values(AVIF_RANGE_FULL),
                                 Values(AVIF_MATRIX_COEFFICIENTS_BT601),
                                 /*addNoise=*/Values(false),
                                 /*rgbStep=*/Values(17),
                                 /*maxAbsAverageDiff=*/Values(0.02), // The color drift is centered.
                                 /*minPsnr=*/Values(52.)             // RGB>YUV>RGB distortion is barely noticeable.
                                 ));

// Converting grey RGB samples to full-range monochrome of same or greater bit depth should be lossless.
INSTANTIATE_TEST_SUITE_P(MonochromeLossless8b,
                         YUVToRGBTest,
                         Combine(/*rgbDepth=*/Values(8),
                                 /*yuvDepth=*/Values(8, 10, 12),
                                 ValuesIn(allRgbFormats),
                                 Values(AVIF_PIXEL_FORMAT_YUV400),
                                 Values(AVIF_RANGE_FULL),
                                 Values(AVIF_MATRIX_COEFFICIENTS_BT601),
                                 /*addNoise=*/Values(false),
                                 /*rgbStep=*/Values(1),
                                 /*maxAbsAverageDiff=*/Values(0.),
                                 /*minPsnr=*/Values(99.)));
INSTANTIATE_TEST_SUITE_P(MonochromeLossless10b,
                         YUVToRGBTest,
                         Combine(/*rgbDepth=*/Values(10),
                                 /*yuvDepth=*/Values(10, 12),
                                 ValuesIn(allRgbFormats),
                                 Values(AVIF_PIXEL_FORMAT_YUV400),
                                 Values(AVIF_RANGE_FULL),
                                 Values(AVIF_MATRIX_COEFFICIENTS_BT601),
                                 /*addNoise=*/Values(false),
                                 /*rgbStep=*/Values(1),
                                 /*maxAbsAverageDiff=*/Values(0.),
                                 /*minPsnr=*/Values(99.)));
INSTANTIATE_TEST_SUITE_P(MonochromeLossless12b,
                         YUVToRGBTest,
                         Combine(/*rgbDepth=*/Values(12),
                                 /*yuvDepth=*/Values(12),
                                 ValuesIn(allRgbFormats),
                                 Values(AVIF_PIXEL_FORMAT_YUV400),
                                 Values(AVIF_RANGE_FULL),
                                 Values(AVIF_MATRIX_COEFFICIENTS_BT601),
                                 /*addNoise=*/Values(false),
                                 /*rgbStep=*/Values(1),
                                 /*maxAbsAverageDiff=*/Values(0.),
                                 /*minPsnr=*/Values(99.)));

// Can be used to print the drift of all RGB to YUV conversion possibilities. Also used for coverage.
INSTANTIATE_TEST_SUITE_P(All8b,
                         YUVToRGBTest,
                         Combine(/*rgbDepth=*/Values(8),
                                 /*yuvDepth=*/Values(8, 10, 12),
                                 ValuesIn(allRgbFormats),
                                 Values(AVIF_PIXEL_FORMAT_YUV444, AVIF_PIXEL_FORMAT_YUV422, AVIF_PIXEL_FORMAT_YUV420),
                                 Values(AVIF_RANGE_LIMITED, AVIF_RANGE_FULL),
                                 Values(AVIF_MATRIX_COEFFICIENTS_BT601),
                                 /*addNoise=*/Values(false, true),
                                 /*rgbStep=*/Values(31),           // High or it would be too slow.
                                 /*maxAbsAverageDiff=*/Values(1.), // Not very accurate because high rgbStep.
                                 /*minPsnr=*/Values(36.)));
INSTANTIATE_TEST_SUITE_P(All10b,
                         YUVToRGBTest,
                         Combine(/*rgbDepth=*/Values(10),
                                 /*yuvDepth=*/Values(8, 10, 12),
                                 ValuesIn(allRgbFormats),
                                 Values(AVIF_PIXEL_FORMAT_YUV444, AVIF_PIXEL_FORMAT_YUV422, AVIF_PIXEL_FORMAT_YUV420),
                                 Values(AVIF_RANGE_LIMITED, AVIF_RANGE_FULL),
                                 Values(AVIF_MATRIX_COEFFICIENTS_BT601),
                                 /*addNoise=*/Values(false, true),
                                 /*rgbStep=*/Values(101),            // High or it would be too slow.
                                 /*maxAbsAverageDiff=*/Values(0.03), // Not very accurate because high rgbStep.
                                 /*minPsnr=*/Values(47.)));
INSTANTIATE_TEST_SUITE_P(All12b,
                         YUVToRGBTest,
                         Combine(/*rgbDepth=*/Values(12),
                                 /*yuvDepth=*/Values(8, 10, 12),
                                 ValuesIn(allRgbFormats),
                                 Values(AVIF_PIXEL_FORMAT_YUV444, AVIF_PIXEL_FORMAT_YUV422, AVIF_PIXEL_FORMAT_YUV420),
                                 Values(AVIF_RANGE_LIMITED, AVIF_RANGE_FULL),
                                 Values(AVIF_MATRIX_COEFFICIENTS_BT601),
                                 /*addNoise=*/Values(false, true),
                                 /*rgbStep=*/Values(401), // High or it would be too slow.
                                 /*maxAbsAverageDiff=*/Values(0.04),
                                 /*minPsnr=*/Values(52.)));

// TODO: Test other matrix coefficients than identity and bt.601.

} // namespace
} // namespace libavif
