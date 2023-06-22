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

const size_t color_profile_size = 376;
const size_t gray_profile_size = 275;

// Verify we can read a color PNG file tagged as gamma 2.2 through gAMA chunk,
// and set transfer characteristics correctly.
TEST(PngTest, ColorGamma22) {
  const auto image = testutil::ReadImage(data_path, "ffffcc-gamma2.2.png");
  ASSERT_NE(image, nullptr);

  // gamma 2.2 should match BT470M
  EXPECT_EQ(image->transferCharacteristics,
            AVIF_TRANSFER_CHARACTERISTICS_BT470M);

  // should not generate ICC profile
  EXPECT_EQ(image->icc.size, 0u);
}

// Verify we can read a color PNG file tagged as gamma 1.6 through gAMA chunk,
// and generate a color profile for it.
TEST(PngTest, ColorGamma16) {
  const auto image = testutil::ReadImage(data_path, "ffffcc-gamma1.6.png");
  ASSERT_NE(image, nullptr);

  // if ICC profile generated, CP and TC should be set to unspecified
  EXPECT_EQ(image->colorPrimaries, AVIF_COLOR_PRIMARIES_UNSPECIFIED);
  EXPECT_EQ(image->transferCharacteristics,
            AVIF_TRANSFER_CHARACTERISTICS_UNSPECIFIED);

  // should generate a color profile
  EXPECT_EQ(image->icc.size, color_profile_size);

  // TODO: more verification on the generated profile
}

// Verify we can read a gray PNG file tagged as gamma 2.2 through gAMA chunk,
// and set transfer characteristics correctly.
TEST(PngTest, GrayGamma22) {
  const auto image = testutil::ReadImage(data_path, "ffffff-gamma2.2.png");
  ASSERT_NE(image, nullptr);

  // gamma 2.2 should match BT470M
  EXPECT_EQ(image->transferCharacteristics,
            AVIF_TRANSFER_CHARACTERISTICS_BT470M);

  // should not generate ICC profile
  EXPECT_EQ(image->icc.size, 0u);
}

// Verify we can read a gray PNG file tagged as gamma 1.6 through gAMA chunk,
// and generate a gray profile for it.
TEST(PngTest, GrayGamma16) {
  const auto image = testutil::ReadImage(data_path, "ffffff-gamma1.6.png");
  ASSERT_NE(image, nullptr);

  // if ICC profile generated, CP and TC should be set to unspecified
  EXPECT_EQ(image->colorPrimaries, AVIF_COLOR_PRIMARIES_UNSPECIFIED);
  EXPECT_EQ(image->transferCharacteristics,
            AVIF_TRANSFER_CHARACTERISTICS_UNSPECIFIED);

  // should generate a gray profile
  EXPECT_EQ(image->icc.size, gray_profile_size);

  // TODO: more verification on the generated profile
}

// Verify we can read a color PNG file tagged as sRGB through sRGB chunk,
// and set color primaries and transfer characteristics correctly.
TEST(PngTest, SRGBTagged) {
  const auto image = testutil::ReadImage(data_path, "ffffcc-srgb.png");
  ASSERT_NE(image, nullptr);

  // should set to BT709 primaries and SRGB transfer
  EXPECT_EQ(image->colorPrimaries, AVIF_COLOR_PRIMARIES_BT709);
  EXPECT_EQ(image->transferCharacteristics, AVIF_TRANSFER_CHARACTERISTICS_SRGB);

  // should not generate ICC profile
  EXPECT_EQ(image->icc.size, 0u);
}

// Verify we are not generating profile if asked to ignore it.
TEST(PngTest, IgnoreProfile) {
  const auto image = testutil::ReadImage(
      data_path, "ffffcc-gamma1.6.png", AVIF_PIXEL_FORMAT_NONE, 0,
      AVIF_CHROMA_DOWNSAMPLING_AUTOMATIC, true);
  ASSERT_NE(image, nullptr);

  // should be left unspecified
  EXPECT_EQ(image->colorPrimaries, AVIF_COLOR_PRIMARIES_UNSPECIFIED);
  EXPECT_EQ(image->transferCharacteristics, AVIF_COLOR_PRIMARIES_UNSPECIFIED);

  // should not generate ICC profile
  EXPECT_EQ(image->icc.size, 0u);
}

// Verify we can read a PNG file tagged as gamma 2.2 through gAMA chunk
// and BT709 primaries through cHRM chunk,
// and set color primaries and transfer characteristics correctly.
TEST(PngTest, BT709Gamma22) {
  const auto image =
      testutil::ReadImage(data_path, "ArcTriomphe-cHRM-orig.png");
  ASSERT_NE(image, nullptr);

  // primaries should match BT709
  EXPECT_EQ(image->colorPrimaries, AVIF_COLOR_PRIMARIES_BT709);

  // gamma 2.2 should match BT470M
  EXPECT_EQ(image->transferCharacteristics,
            AVIF_TRANSFER_CHARACTERISTICS_BT470M);

  // should not generate ICC profile
  EXPECT_EQ(image->icc.size, 0u);
}

// Verify we can read a PNG file tagged as gamma 2.2 through gAMA chunk
// and BT709 primaries with red and green swapped through cHRM chunk,
// and generate a color profile for it.
TEST(PngTest, BT709SwappedGamma22) {
  const auto image =
      testutil::ReadImage(data_path, "ArcTriomphe-cHRM-red-green-swap.png");
  ASSERT_NE(image, nullptr);

  // if ICC profile generated, CP and TC should be set to unspecified
  EXPECT_EQ(image->colorPrimaries, AVIF_COLOR_PRIMARIES_UNSPECIFIED);
  EXPECT_EQ(image->transferCharacteristics,
            AVIF_TRANSFER_CHARACTERISTICS_UNSPECIFIED);

  // should generate a color profile
  EXPECT_EQ(image->icc.size, color_profile_size);

  // TODO: more verification on the generated profile
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
