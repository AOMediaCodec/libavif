// Copyright 2024 Google LLC
// SPDX-License-Identifier: BSD-2-Clause

#include <algorithm>
#include <cstring>
#include <iostream>
#include <string>

#include "avif/avif.h"
#include "aviftest_helpers.h"
#include "gtest/gtest.h"

namespace avif {
namespace {

// Used to pass the data folder path to the GoogleTest suites.
const char* data_path = nullptr;

//------------------------------------------------------------------------------

TEST(AvifDecodeTest, SingleWhitePixel) {
  const char* file_name = "white_1x1.avif";
  DecoderPtr decoder(avifDecoderCreate());
  ASSERT_NE(decoder, nullptr);
  ASSERT_EQ(avifDecoderSetIOFile(decoder.get(),
                                 (std::string(data_path) + file_name).c_str()),
            AVIF_RESULT_OK);
  ASSERT_EQ(avifDecoderParse(decoder.get()), AVIF_RESULT_OK);
  if (testutil::Av1DecoderAvailable()) {
    EXPECT_EQ(avifDecoderNextImage(decoder.get()), AVIF_RESULT_OK);
  }
}

TEST(AvifDecodeTest, MdatSize0) {
  testutil::AvifRwData avif =
      testutil::ReadFile(std::string(data_path) + "white_1x1.avif");
  // Edit the file to simulate an 'mdat' box with size 0 (meaning ending at EOF)
  const uint8_t* kMdat = reinterpret_cast<const uint8_t*>("mdat");
  uint8_t* mdat_position =
      std::search(avif.data, avif.data + avif.size, kMdat, kMdat + 4);
  ASSERT_NE(mdat_position, avif.data + avif.size);
  mdat_position[-1] = '\0';

  DecoderPtr decoder(avifDecoderCreate());
  ASSERT_NE(decoder, nullptr);
  ASSERT_EQ(avifDecoderSetIOMemory(decoder.get(), avif.data, avif.size),
            AVIF_RESULT_OK);
  ASSERT_EQ(avifDecoderParse(decoder.get()), AVIF_RESULT_OK);
  if (testutil::Av1DecoderAvailable()) {
    EXPECT_EQ(avifDecoderNextImage(decoder.get()), AVIF_RESULT_OK);
  }
}

TEST(AvifDecodeTest, MetaSize0) {
  testutil::AvifRwData avif =
      testutil::ReadFile(std::string(data_path) + "white_1x1.avif");
  // Edit the file to simulate a 'meta' box with size 0 (invalid).
  const uint8_t* kMeta = reinterpret_cast<const uint8_t*>("meta");
  uint8_t* meta_position =
      std::search(avif.data, avif.data + avif.size, kMeta, kMeta + 4);
  ASSERT_NE(meta_position, avif.data + avif.size);
  meta_position[-1] = '\0';

  DecoderPtr decoder(avifDecoderCreate());
  ASSERT_NE(decoder, nullptr);
  ASSERT_EQ(avifDecoderSetIOMemory(decoder.get(), avif.data, avif.size),
            AVIF_RESULT_OK);

  // This should fail because the meta box contains the mdat box.
  // However, the section 8.11.3.1 of ISO/IEC 14496-12 does not explicitly
  // require the coded image item extents to be read from the MediaDataBox if
  // the construction_method is 0.
  // Maybe another section or specification enforces that.
  ASSERT_EQ(avifDecoderParse(decoder.get()), AVIF_RESULT_OK);
  if (testutil::Av1DecoderAvailable()) {
    EXPECT_EQ(avifDecoderNextImage(decoder.get()), AVIF_RESULT_OK);
  }
}

TEST(AvifDecodeTest, FtypSize0) {
  testutil::AvifRwData avif =
      testutil::ReadFile(std::string(data_path) + "white_1x1.avif");
  // Edit the file to simulate a 'ftyp' box with size 0 (invalid).
  avif.data[3] = '\0';

  DecoderPtr decoder(avifDecoderCreate());
  ASSERT_NE(decoder, nullptr);
  ASSERT_EQ(avifDecoderSetIOMemory(decoder.get(), avif.data, avif.size),
            AVIF_RESULT_OK);
  ASSERT_EQ(avifDecoderParse(decoder.get()), AVIF_RESULT_BMFF_PARSE_FAILED);
}

TEST(AvifDecodeTest, UnknownTopLevelBoxSize0) {
  testutil::AvifRwData avif =
      testutil::ReadFile(std::string(data_path) + "white_1x1.avif");
  // Edit the file to insert an unknown top level box with size 0 after ftyp
  // (invalid).
  testutil::AvifRwData avif_edited;
  ASSERT_EQ(avifRWDataRealloc(&avif_edited, avif.size + 8), AVIF_RESULT_OK);
  // Copy the ftyp box.
  std::memcpy(avif_edited.data, avif.data, 32);
  // Set 8 bytes to 0 (box type and size all 0s).
  std::memset(avif_edited.data + 32, 0, 8);
  // Copy the other boxes.
  std::memcpy(avif_edited.data + 40, avif.data + 32, avif.size - 32);

  DecoderPtr decoder(avifDecoderCreate());
  ASSERT_NE(decoder, nullptr);
  ASSERT_EQ(
      avifDecoderSetIOMemory(decoder.get(), avif_edited.data, avif_edited.size),
      AVIF_RESULT_OK);
  ASSERT_EQ(avifDecoderParse(decoder.get()), AVIF_RESULT_BMFF_PARSE_FAILED);
}

//------------------------------------------------------------------------------

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
