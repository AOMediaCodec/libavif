// Copyright 2026 the libavif contributors
// SPDX-License-Identifier: BSD-2-Clause

#include "avif/internal.h"
#include "aviftest_helpers.h"
#include "gtest/gtest.h"

namespace avif {
namespace {

// avifImageCopySamples() copies plane data with nothing but assert() guarding
// its preconditions, so a release build memcpys happily if a caller ever breaks
// one of them. The checks now return AVIF_RESULT_INTERNAL_ERROR instead.
TEST(ImageCopySamplesTest, MismatchedDepthIsRejected) {
  ImagePtr src =
      testutil::CreateImage(/*width=*/2, /*height=*/2, /*depth=*/10,
                            AVIF_PIXEL_FORMAT_YUV420, AVIF_PLANES_YUV);
  ImagePtr dst =
      testutil::CreateImage(/*width=*/2, /*height=*/2, /*depth=*/8,
                            AVIF_PIXEL_FORMAT_YUV420, AVIF_PLANES_YUV);
  ASSERT_NE(src, nullptr);
  ASSERT_NE(dst, nullptr);
  EXPECT_EQ(avifImageCopySamples(dst.get(), src.get(), AVIF_PLANES_YUV),
            AVIF_RESULT_INTERNAL_ERROR);
}

TEST(ImageCopySamplesTest, MismatchedFormatIsRejected) {
  ImagePtr src =
      testutil::CreateImage(/*width=*/2, /*height=*/2, /*depth=*/8,
                            AVIF_PIXEL_FORMAT_YUV444, AVIF_PLANES_YUV);
  ImagePtr dst =
      testutil::CreateImage(/*width=*/2, /*height=*/2, /*depth=*/8,
                            AVIF_PIXEL_FORMAT_YUV420, AVIF_PLANES_YUV);
  ASSERT_NE(src, nullptr);
  ASSERT_NE(dst, nullptr);
  EXPECT_EQ(avifImageCopySamples(dst.get(), src.get(), AVIF_PLANES_YUV),
            AVIF_RESULT_INTERNAL_ERROR);
}

TEST(ImageCopySamplesTest, MismatchedDimensionsAreRejected) {
  ImagePtr src =
      testutil::CreateImage(/*width=*/4, /*height=*/4, /*depth=*/8,
                            AVIF_PIXEL_FORMAT_YUV420, AVIF_PLANES_YUV);
  ImagePtr dst =
      testutil::CreateImage(/*width=*/2, /*height=*/2, /*depth=*/8,
                            AVIF_PIXEL_FORMAT_YUV420, AVIF_PLANES_YUV);
  ASSERT_NE(src, nullptr);
  ASSERT_NE(dst, nullptr);
  EXPECT_EQ(avifImageCopySamples(dst.get(), src.get(), AVIF_PLANES_YUV),
            AVIF_RESULT_INTERNAL_ERROR);
}

TEST(ImageCopySamplesTest, MissingDestinationPlaneIsRejected) {
  ImagePtr src =
      testutil::CreateImage(/*width=*/2, /*height=*/2, /*depth=*/8,
                            AVIF_PIXEL_FORMAT_YUV420, AVIF_PLANES_YUV);
  ImagePtr dst = testutil::CreateImage(/*width=*/2, /*height=*/2, /*depth=*/8,
                                       AVIF_PIXEL_FORMAT_YUV420, 0u);
  ASSERT_NE(src, nullptr);
  ASSERT_NE(dst, nullptr);
  EXPECT_EQ(avifImageCopySamples(dst.get(), src.get(), AVIF_PLANES_YUV),
            AVIF_RESULT_INTERNAL_ERROR);
}

TEST(ImageCopySamplesTest, MatchingImagesCopyFine) {
  ImagePtr src =
      testutil::CreateImage(/*width=*/3, /*height=*/2, /*depth=*/8,
                            AVIF_PIXEL_FORMAT_YUV420, AVIF_PLANES_ALL);
  ImagePtr dst =
      testutil::CreateImage(/*width=*/3, /*height=*/2, /*depth=*/8,
                            AVIF_PIXEL_FORMAT_YUV420, AVIF_PLANES_ALL);
  ASSERT_NE(src, nullptr);
  ASSERT_NE(dst, nullptr);
  testutil::FillImageGradient(src.get());
  ASSERT_EQ(avifImageCopySamples(dst.get(), src.get(), AVIF_PLANES_ALL),
            AVIF_RESULT_OK);
  EXPECT_EQ(testutil::AreImagesEqual(*src, *dst), true);
}

TEST(ImageCopySamplesTest, MissingPlanesOnBothSidesIsFine) {
  ImagePtr src = testutil::CreateImage(/*width=*/2, /*height=*/2, /*depth=*/8,
                                       AVIF_PIXEL_FORMAT_YUV420, 0u);
  ImagePtr dst = testutil::CreateImage(/*width=*/2, /*height=*/2, /*depth=*/8,
                                       AVIF_PIXEL_FORMAT_YUV420, 0u);
  ASSERT_NE(src, nullptr);
  ASSERT_NE(dst, nullptr);
  EXPECT_EQ(avifImageCopySamples(dst.get(), src.get(), AVIF_PLANES_ALL),
            AVIF_RESULT_OK);
}

}  // namespace
}  // namespace avif
