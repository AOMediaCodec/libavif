// Copyright 2024 Google LLC
// SPDX-License-Identifier: BSD-2-Clause

#include "avif/avif.h"
#include "aviftest_helpers.h"
#include "gtest/gtest.h"

#include <algorithm>
#include <cstdint>
#include <vector>

namespace avif {
namespace {

// Used to pass the data folder path to the GoogleTest suites.
const char* data_path = nullptr;

//------------------------------------------------------------------------------

uint32_t ReadU32(const uint8_t* data) {
  return (static_cast<uint32_t>(data[0]) << 24) |
         (static_cast<uint32_t>(data[1]) << 16) |
         (static_cast<uint32_t>(data[2]) << 8) | static_cast<uint32_t>(data[3]);
}

bool SetFirstIlocDataReferenceIndex(std::vector<uint8_t>* bytes,
                                    uint16_t data_reference_index) {
  constexpr uint8_t kIlocType[] = {'i', 'l', 'o', 'c'};
  auto iloc =
      std::search(bytes->begin(), bytes->end(), std::begin(kIlocType),
                  std::end(kIlocType));
  if (iloc == bytes->end() || iloc - bytes->begin() < 4) return false;
  const size_t box_start = static_cast<size_t>(iloc - bytes->begin()) - 4;
  const uint32_t box_size = ReadU32(bytes->data() + box_start);
  if (box_size < 20 || box_start > bytes->size() - box_size) return false;

  // This helper mutates the version 0 iloc box written by libavif. The first
  // entry's data_reference_index follows the full box header, iloc size fields,
  // item_count and item_ID.
  const size_t version_offset = box_start + 8;
  if ((*bytes)[version_offset] != 0) return false;
  const size_t data_reference_index_offset = box_start + 18;
  if (data_reference_index_offset + 1 >= box_start + box_size) return false;
  (*bytes)[data_reference_index_offset] =
      static_cast<uint8_t>(data_reference_index >> 8);
  (*bytes)[data_reference_index_offset + 1] =
      static_cast<uint8_t>(data_reference_index);
  return true;
}

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

TEST(IlocTest, NonZeroDataReferenceIndexIsRejected) {
  testutil::AvifRwData encoded =
      testutil::ReadFile(std::string(data_path) + "white_1x1.avif");
  ASSERT_NE(encoded.data, nullptr);
  std::vector<uint8_t> bytes(encoded.data, encoded.data + encoded.size);
  ASSERT_TRUE(SetFirstIlocDataReferenceIndex(&bytes, 1));

  DecoderPtr decoder(avifDecoderCreate());
  ASSERT_NE(decoder, nullptr);
  ASSERT_EQ(avifDecoderSetIOMemory(decoder.get(), bytes.data(), bytes.size()),
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
