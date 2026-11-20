// Copyright 2026 libavif contributors. All rights reserved.
// SPDX-License-Identifier: BSD-2-Clause

#include <cstdint>
#include <limits>

#include "avif/internal.h"
#include "aviftest_helpers.h"
#include "gtest/gtest.h"

namespace avif {
namespace {

TEST(PlaneRowBytesTest, AcceptsTightAndPaddedStrides) {
  EXPECT_TRUE(avifArePlaneRowBytesValid(8, 4, 8, 1, 1));
  EXPECT_TRUE(avifArePlaneRowBytesValid(8, 4, 32, 1, 1));
  EXPECT_TRUE(avifArePlaneRowBytesValid(8, 4, 16, 2, 2));
  EXPECT_TRUE(avifArePlaneRowBytesValid(8, 4, 18, 2, 2));
  // 8-bit RGB is 3 bytes per pixel. A stride aligned up to 4 still covers the
  // row.
  EXPECT_TRUE(avifArePlaneRowBytesValid(1, 2, 4, 3, 1));
  // 8-bit RGBA is 4 bytes per pixel but each channel is one byte. An odd
  // padded stride still covers the single pixel on each row.
  EXPECT_TRUE(avifArePlaneRowBytesValid(1, 2, 5, 4, 1));
  EXPECT_TRUE(avifArePlaneRowBytesValid(0, 4, 0, 1, 1));
  EXPECT_TRUE(avifArePlaneRowBytesValid(4, 0, 99, 1, 1));
}

TEST(PlaneRowBytesTest, RejectsShortOddAndOverflowingStrides) {
  EXPECT_FALSE(avifArePlaneRowBytesValid(8, 4, 7, 1, 1));
  EXPECT_FALSE(avifArePlaneRowBytesValid(8, 4, 15, 2, 2));
  EXPECT_FALSE(avifArePlaneRowBytesValid(8, 4, 8, 2, 2));
  // 16-bit RGBA is 8 bytes per pixel. The odd stride covers the pixel but the
  // next row would not be aligned for uint16_t.
  EXPECT_FALSE(avifArePlaneRowBytesValid(1, 2, 9, 8, 2));
  // width * bytesPerPixel must itself fit in uint32_t before it is compared to
  // the stride.
  EXPECT_FALSE(
      avifArePlaneRowBytesValid(std::numeric_limits<uint32_t>::max(), 1,
                                std::numeric_limits<uint32_t>::max(), 2, 2));
  EXPECT_FALSE(avifArePlaneRowBytesValid(1, 1, 1, 0, 1));

  uint32_t stored = 123;
  EXPECT_FALSE(avifStorePlaneRowBytes(&stored, -16, 8, 4, 1));
  EXPECT_EQ(stored, 123u);
  EXPECT_FALSE(avifStorePlaneRowBytes(
      &stored, static_cast<int64_t>(std::numeric_limits<uint32_t>::max()) + 1,
      1, 1, 1));
  EXPECT_EQ(stored, 123u);
  EXPECT_TRUE(avifStorePlaneRowBytes(&stored, 32, 8, 4, 1));
  EXPECT_EQ(stored, 32u);

  // height * rowBytes is done in size_t. On 32-bit size_t this product wraps.
  if (sizeof(size_t) < 8) {
    EXPECT_FALSE(avifArePlaneRowBytesValid(1, 3, 0x80000000u, 1, 1));
  }
}

TEST(PlaneRowBytesTest, CopyRejectsUndersizedSourceStride) {
  constexpr int kWidth = 64;
  constexpr int kHeight = 2;
  ImagePtr src = testutil::CreateImage(
      kWidth, kHeight, 8, AVIF_PIXEL_FORMAT_YUV400, AVIF_PLANES_YUV);
  ImagePtr dst = testutil::CreateImage(
      kWidth, kHeight, 8, AVIF_PIXEL_FORMAT_YUV400, AVIF_PLANES_YUV);
  ASSERT_NE(src, nullptr);
  ASSERT_NE(dst, nullptr);
  testutil::FillImageGradient(src.get());

  // The plane holds kWidth bytes per row. Reporting a shorter stride makes a
  // copy of kWidth bytes per row read past the row and, on the last row, past
  // the allocation.
  src->yuvRowBytes[AVIF_CHAN_Y] = static_cast<uint32_t>(kWidth / 2);
  EXPECT_FALSE(avifImagePlanesHaveValidRowBytes(src.get(), AVIF_PLANES_YUV));
  EXPECT_FALSE(avifImageCopySamples(dst.get(), src.get(), AVIF_PLANES_YUV));
  EXPECT_EQ(avifImageCopy(dst.get(), src.get(), AVIF_PLANES_YUV),
            AVIF_RESULT_INVALID_ARGUMENT);

  src->yuvRowBytes[AVIF_CHAN_Y] = static_cast<uint32_t>(kWidth);
  EXPECT_EQ(avifImageCopy(dst.get(), src.get(), AVIF_PLANES_YUV),
            AVIF_RESULT_OK);
  EXPECT_TRUE(testutil::AreImagesEqual(*src, *dst));
}

TEST(PlaneRowBytesTest, YuvToRgbRejectsUndersizedStrides) {
  constexpr int kWidth = 32;
  constexpr int kHeight = 3;
  ImagePtr src = testutil::CreateImage(
      kWidth, kHeight, 8, AVIF_PIXEL_FORMAT_YUV400, AVIF_PLANES_YUV);
  ASSERT_NE(src, nullptr);
  testutil::FillImageGradient(src.get());

  testutil::AvifRgbImage rgb(src.get(), 8, AVIF_RGB_FORMAT_RGB);
  ASSERT_EQ(avifImageYUVToRGB(src.get(), &rgb), AVIF_RESULT_OK);

  src->yuvRowBytes[AVIF_CHAN_Y] = 1;
  EXPECT_EQ(avifImageYUVToRGB(src.get(), &rgb), AVIF_RESULT_REFORMAT_FAILED);

  src->yuvRowBytes[AVIF_CHAN_Y] = static_cast<uint32_t>(kWidth);
  rgb.rowBytes = 1;
  EXPECT_EQ(avifImageYUVToRGB(src.get(), &rgb), AVIF_RESULT_REFORMAT_FAILED);
}

TEST(PlaneRowBytesTest, RgbToYuvRejectsUndersizedRgbStride) {
  ImagePtr dst = testutil::CreateImage(16, 2, 8, AVIF_PIXEL_FORMAT_YUV444,
                                       AVIF_PLANES_YUV);
  ASSERT_NE(dst, nullptr);
  testutil::AvifRgbImage rgb(dst.get(), 8, AVIF_RGB_FORMAT_RGB);
  // 16 RGB pixels need 48 bytes per row. A stride of 2 cannot hold them.
  rgb.rowBytes = 2;
  EXPECT_EQ(avifImageRGBToYUV(dst.get(), &rgb), AVIF_RESULT_REFORMAT_FAILED);
}

TEST(PlaneRowBytesTest, EightBitRgbaOddStrideConverts) {
  ImagePtr src =
      testutil::CreateImage(1, 2, 8, AVIF_PIXEL_FORMAT_YUV400, AVIF_PLANES_YUV);
  ASSERT_NE(src, nullptr);
  testutil::FillImageGradient(src.get());

  testutil::AvifRgbImage rgb(src.get(), 8, AVIF_RGB_FORMAT_RGBA);
  avifRGBImageFreePixels(&rgb);
  rgb.rowBytes = 5;
  rgb.pixels = static_cast<uint8_t*>(
      avifAlloc(static_cast<size_t>(rgb.height) * rgb.rowBytes));
  ASSERT_NE(rgb.pixels, nullptr);
  EXPECT_EQ(avifImageYUVToRGB(src.get(), &rgb), AVIF_RESULT_OK);
}

TEST(PlaneRowBytesTest, ScaleRejectsUndersizedSourceStride) {
  ImagePtr image = testutil::CreateImage(20, 8, 8, AVIF_PIXEL_FORMAT_YUV420,
                                         AVIF_PLANES_YUV);
  ASSERT_NE(image, nullptr);
  testutil::FillImageGradient(image.get());
  image->yuvRowBytes[AVIF_CHAN_U] = 1;

  avifDiagnostics diag;
  avifDiagnosticsClearError(&diag);
  EXPECT_EQ(avifImageScale(image.get(), 10, 4, &diag),
            AVIF_RESULT_INVALID_ARGUMENT);
}

}  // namespace
}  // namespace avif
