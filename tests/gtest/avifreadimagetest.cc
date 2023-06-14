// Copyright 2022 Google LLC
// SPDX-License-Identifier: BSD-2-Clause

#include <array>
#include <tuple>

#include "avif/avif.h"
#include "aviftest_helpers.h"
#include "gtest/gtest.h"

namespace libavif {
namespace {

// Used to pass the data folder path to the GoogleTest suites.
const char* data_path = nullptr;

//------------------------------------------------------------------------------

bool AreSamplesEqualForAllReadSettings(const char* file_name1,
                                       const char* file_name2) {
  constexpr bool kIgnoreMetadata = true;
  for (avifPixelFormat requested_format :
       {AVIF_PIXEL_FORMAT_YUV444, AVIF_PIXEL_FORMAT_YUV422,
        AVIF_PIXEL_FORMAT_YUV420, AVIF_PIXEL_FORMAT_YUV400}) {
    for (int requested_depth : {8, 10, 12, 16}) {
      for (avifChromaDownsampling chroma_downsampling :
           {AVIF_CHROMA_DOWNSAMPLING_AUTOMATIC,
            AVIF_CHROMA_DOWNSAMPLING_FASTEST,
            AVIF_CHROMA_DOWNSAMPLING_BEST_QUALITY,
            AVIF_CHROMA_DOWNSAMPLING_AVERAGE}) {
        const testutil::AvifImagePtr image1 = testutil::ReadImage(
            data_path, file_name1, requested_format, requested_depth,
            chroma_downsampling, kIgnoreMetadata, kIgnoreMetadata,
            kIgnoreMetadata);
        const testutil::AvifImagePtr image2 = testutil::ReadImage(
            data_path, file_name2, requested_format, requested_depth,
            chroma_downsampling, kIgnoreMetadata, kIgnoreMetadata,
            kIgnoreMetadata);
        if (!image1 || !image2 || !testutil::AreImagesEqual(*image1, *image2)) {
          return false;
        }
      }
    }
  }
  return true;
}

TEST(JpegTest, ReadAllSubsamplingsAndAllBitDepths) {
  EXPECT_TRUE(AreSamplesEqualForAllReadSettings(
      "paris_exif_xmp_icc.jpg", "paris_exif_orientation_5.jpg"));
}

TEST(PngTest, ReadAllSubsamplingsAndAllBitDepths) {
  EXPECT_TRUE(AreSamplesEqualForAllReadSettings(
      "paris_icc_exif_xmp.png", "paris_icc_exif_xmp_at_end.png"));
}

// Verify we can read a PNG file with PNG_COLOR_TYPE_PALETTE and a tRNS chunk.
TEST(PngTest, PaletteColorTypeWithTrnsChunk) {
  const testutil::AvifImagePtr image = testutil::ReadImage(
      data_path, "draw_points.png", AVIF_PIXEL_FORMAT_YUV444, 8);
  ASSERT_NE(image, nullptr);
  EXPECT_EQ(image->width, 33u);
  EXPECT_EQ(image->height, 11u);
  EXPECT_NE(image->alphaPlane, nullptr);
}

// Verify we can read a PNG file with PNG_COLOR_TYPE_RGB and a tRNS chunk
// after a PLTE chunk.
TEST(PngTest, RgbColorTypeWithTrnsAfterPlte) {
  const testutil::AvifImagePtr image = testutil::ReadImage(
      data_path, "circle-trns-after-plte.png", AVIF_PIXEL_FORMAT_YUV444, 8);
  ASSERT_NE(image, nullptr);
  EXPECT_EQ(image->width, 100u);
  EXPECT_EQ(image->height, 60u);
  EXPECT_NE(image->alphaPlane, nullptr);
}

// Verify we can read a PNG file with PNG_COLOR_TYPE_RGB and a tRNS chunk
// before a PLTE chunk. libpng considers the tRNS chunk as invalid and ignores
// it, so the decoded image should have no alpha.
TEST(PngTest, RgbColorTypeWithTrnsBeforePlte) {
  const testutil::AvifImagePtr image = testutil::ReadImage(
      data_path, "circle-trns-before-plte.png", AVIF_PIXEL_FORMAT_YUV444, 8);
  ASSERT_NE(image, nullptr);
  EXPECT_EQ(image->width, 100u);
  EXPECT_EQ(image->height, 60u);
  EXPECT_EQ(image->alphaPlane, nullptr);
}

//------------------------------------------------------------------------------

}  // namespace
}  // namespace libavif

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  if (argc != 2) {
    std::cerr << "There must be exactly one argument containing the path to "
                 "the test data folder"
              << std::endl;
    return 1;
  }
  libavif::data_path = argv[1];
  return RUN_ALL_TESTS();
}
