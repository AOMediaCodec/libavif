// Copyright 2023 Google LLC
// SPDX-License-Identifier: BSD-2-Clause

#include <iostream>
#include <string>

#include "avif/avif.h"
#include "aviftest_helpers.h"
#include "gtest/gtest.h"

namespace avif {
namespace {

// Used to pass the data folder path to the GoogleTest suites.
const char* data_path = nullptr;

TEST(AvifDecodeTest, ColorGridAlphaNoGrid) {
  if (!testutil::Av1DecoderAvailable()) {
    GTEST_SKIP() << "AV1 Codec unavailable, skip test.";
  }
  // Test case from https://github.com/AOMediaCodec/libavif/issues/1203.
  const char* file_name = "color_grid_alpha_nogrid.avif";
  DecoderPtr decoder(avifDecoderCreate());
  ASSERT_NE(decoder, nullptr);
  ASSERT_EQ(avifDecoderSetIOFile(decoder.get(),
                                 (std::string(data_path) + file_name).c_str()),
            AVIF_RESULT_OK);
  ASSERT_EQ(avifDecoderParse(decoder.get()), AVIF_RESULT_OK);
  EXPECT_EQ(decoder->alphaPresent, AVIF_TRUE);
  EXPECT_EQ(decoder->imageSequenceTrackPresent, AVIF_FALSE);
  EXPECT_EQ(avifDecoderNextImage(decoder.get()), AVIF_RESULT_OK);
  EXPECT_NE(decoder->image->alphaPlane, nullptr);
  EXPECT_GT(decoder->image->alphaRowBytes, 0u);
}

TEST(AvifDecodeTest, ImageContentToDecodeNone) {
  if (!testutil::Av1DecoderAvailable()) {
    GTEST_SKIP() << "AV1 Codec unavailable, skip test.";
  }
  for (const std::string file_name :
       {"paris_icc_exif_xmp.avif", "draw_points_idat.avif",
        "sofa_grid1x5_420.avif", "color_grid_alpha_nogrid.avif",
        "seine_sdr_gainmap_srgb.avif", "draw_points_idat_progressive.avif"}) {
    SCOPED_TRACE(file_name);
    DecoderPtr decoder(avifDecoderCreate());
    ASSERT_NE(decoder, nullptr);
    // Do not decode anything.
    decoder->imageContentToDecode = AVIF_IMAGE_CONTENT_NONE;
    ASSERT_EQ(avifDecoderSetIOFile(
                  decoder.get(), (std::string(data_path) + file_name).c_str()),
              AVIF_RESULT_OK);
    ASSERT_EQ(avifDecoderParse(decoder.get()), AVIF_RESULT_OK)
        << decoder->diag.error;
    EXPECT_EQ(decoder->imageSequenceTrackPresent, AVIF_FALSE);
    EXPECT_EQ(avifDecoderNextImage(decoder.get()), AVIF_RESULT_NO_CONTENT);
  }
}

TEST(AvifDecodeTest, ParseEmptyData) {
  DecoderPtr decoder(avifDecoderCreate());
  ASSERT_NE(decoder, nullptr);
  ASSERT_EQ(avifDecoderSetIOMemory(decoder.get(), nullptr, 0), AVIF_RESULT_OK);
  // No ftyp box was seen.
  ASSERT_EQ(avifDecoderParse(decoder.get()), AVIF_RESULT_INVALID_FTYP);
}

TEST(AvifDecodeTest, Idat) {
  if (!testutil::Av1DecoderAvailable()) {
    GTEST_SKIP() << "AV1 Codec unavailable, skip test.";
  }

  const ImagePtr original = testutil::ReadImage(data_path, "draw_points.png");

  for (const std::string file_name :
       {"draw_points_idat.avif", "draw_points_idat_metasize0.avif",
        "draw_points_idat_progressive.avif",
        "draw_points_idat_progressive_metasize0.avif"}) {
    SCOPED_TRACE(file_name);
    DecoderPtr decoder(avifDecoderCreate());
    ASSERT_NE(decoder, nullptr);
    ASSERT_EQ(avifDecoderSetIOFile(
                  decoder.get(), (std::string(data_path) + file_name).c_str()),
              AVIF_RESULT_OK);
    ASSERT_EQ(avifDecoderParse(decoder.get()), AVIF_RESULT_OK);
    EXPECT_EQ(decoder->alphaPresent, AVIF_TRUE);
    EXPECT_EQ(decoder->imageSequenceTrackPresent, AVIF_FALSE);
    ASSERT_EQ(avifDecoderNextImage(decoder.get()), AVIF_RESULT_OK);
    EXPECT_NE(decoder->image->alphaPlane, nullptr);
    EXPECT_GT(decoder->image->alphaRowBytes, 0u);

    EXPECT_EQ(testutil::GetPsnr(*original, *decoder->image), 99.0);
  }
}

// From https://crbug.com/334281983.
TEST(AvifDecodeTest, PeekCompatibleFileTypeBad1) {
  constexpr uint8_t kData[] = {0x00, 0x00, 0x00, 0x1c, 0x66,
                               0x74, 0x79, 0x70, 0x84, 0xca};
  avifROData input = {kData, sizeof(kData)};
  EXPECT_FALSE(avifPeekCompatibleFileType(&input));
}

// From https://crbug.com/334682511.
TEST(AvifDecodeTest, PeekCompatibleFileTypeBad2) {
  constexpr uint8_t kData[] = {0x00, 0x00, 0x00, 0x20, 0x66, 0x74, 0x79,
                               0x70, 0x61, 0x73, 0x31, 0x6d, 0x00, 0x00,
                               0x08, 0x00, 0xd7, 0x89, 0xdb, 0x7f};
  avifROData input = {kData, sizeof(kData)};
  EXPECT_FALSE(avifPeekCompatibleFileType(&input));
}

}  // namespace
}  // namespace avif

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  if (argc != 2) {
    std::cerr << "There must be exactly one argument containing the path to "
                 "the test data folder"
              << std::endl;
    return 1;
  }
  avif::data_path = argv[1];
  return RUN_ALL_TESTS();
}
