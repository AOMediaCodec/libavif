// Copyright 2024 Google LLC
// SPDX-License-Identifier: BSD-2-Clause

#include <algorithm>
#include <string>

#include "avif/avif.h"
#include "aviftest_helpers.h"
#include "gtest/gtest.h"

namespace avif {
namespace {

// Used to pass the data folder path to the GoogleTest suites.
const char* data_path = nullptr;

//------------------------------------------------------------------------------

TEST(IlocTest, TwoExtents) {
  if (!testutil::Av1DecoderAvailable()) {
    GTEST_SKIP() << "AV1 Codec unavailable, skip test.";
  }

  const ImagePtr source =
      testutil::ReadImage(data_path, "ArcTriomphe-cHRM-orig.png");
  const ImagePtr decoded =
      testutil::DecodeFile(std::string(data_path) +
                           "arc_triomphe_extent1000_nullbyte_extent1310.avif");
  ASSERT_NE(source, nullptr);
  ASSERT_NE(decoded, nullptr);
  const double psnr = testutil::GetPsnr(*source, *decoded);
  EXPECT_GT(psnr, 30.0);
  EXPECT_LT(psnr, 45.0);
}

TEST(IlocTest, NonZeroDataReferenceIndex) {
  testutil::AvifRwData avif =
      testutil::ReadFile(std::string(data_path) + "white_1x1.avif");
  ASSERT_NE(avif.data, nullptr);

  const uint8_t kIloc[] = {'i', 'l', 'o', 'c'};
  uint8_t* iloc_position =
      std::search(avif.data, avif.data + avif.size, kIloc, kIloc + 4);
  ASSERT_NE(iloc_position, avif.data + avif.size);
  ASSERT_GE(static_cast<size_t>(avif.data + avif.size - iloc_position),
            size_t{16});

  // white_1x1.avif uses iloc version 0 with a single item. The
  // data_reference_index field follows the item_ID field.
  ASSERT_EQ(iloc_position[4], 0);
  ASSERT_EQ(iloc_position[10], 0);
  ASSERT_EQ(iloc_position[11], 1);
  ASSERT_EQ(iloc_position[14], 0);
  ASSERT_EQ(iloc_position[15], 0);
  iloc_position[14] = 0;
  iloc_position[15] = 1;

  DecoderPtr decoder(avifDecoderCreate());
  ASSERT_NE(decoder, nullptr);
  ASSERT_EQ(avifDecoderSetIOMemory(decoder.get(), avif.data, avif.size),
            AVIF_RESULT_OK);
  EXPECT_EQ(avifDecoderParse(decoder.get()), AVIF_RESULT_BMFF_PARSE_FAILED);
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
