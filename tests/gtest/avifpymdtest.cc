// Copyright 2026 the libavif contributors
// SPDX-License-Identifier: BSD-2-Clause

#include <cstddef>
#include <iostream>
#include <string>

#include "avif/avif.h"
#include "avif/avif_cxx.h"
#include "aviftest_helpers.h"
#include "gtest/gtest.h"

namespace avif {
namespace {

// Used to pass the data folder path to the GoogleTest suites.
const char* data_path = nullptr;

//------------------------------------------------------------------------------

// Parses the given bytes. Strictness is disabled so that the files below can
// only be rejected for the reason under test.
avifResult Parse(const testutil::AvifRwData& encoded) {
  DecoderPtr decoder(avifDecoderCreate());
  if (decoder == nullptr) return AVIF_RESULT_UNKNOWN_ERROR;
  decoder->strictFlags = AVIF_STRICT_DISABLED;
  const avifResult result =
      avifDecoderSetIOMemory(decoder.get(), encoded.data, encoded.size);
  if (result != AVIF_RESULT_OK) return result;
  return avifDecoderParse(decoder.get());
}

// An EntityToGroupBox may carry grouping type specific data after the
// entity_id array. The 'pymd' group of ISO/IEC 23008-12 does, the 'prsl' group
// of ISO/IEC 14496-12 does, and libheif writes the former. Every child of
// 'grpl' must therefore be parsed within its own declared box size, so that
// the trailing data is not read as the beginning of the next
// EntityToGroupBox.
TEST(EntityToGroupTest, PyramidGroupWithGroupingTypeSpecificData) {
  const testutil::AvifRwData encoded =
      testutil::ReadFile(std::string(data_path) + "pyramid_pymd.avif");
  ASSERT_NE(encoded.size, size_t{0});
  EXPECT_EQ(Parse(encoded), AVIF_RESULT_OK);
}

// The mirror direction: a SingleItemTypeReferenceBox whose reference_count
// asks for more to_item_ID values than its declared box size holds must not be
// satisfied from the bytes that follow the box.
TEST(ItemReferenceTest, ReferenceCountBeyondChildBoxSizeIsRejected) {
  const testutil::AvifRwData encoded = testutil::ReadFile(
      std::string(data_path) + "iref_count_past_box_end.avif");
  ASSERT_NE(encoded.size, size_t{0});
  EXPECT_EQ(Parse(encoded), AVIF_RESULT_BMFF_PARSE_FAILED);
}

//------------------------------------------------------------------------------

}  // namespace
}  // namespace avif

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  if (argc < 2) {
    std::cerr
        << "The path to the test data folder must be provided as an argument"
        << std::endl;
    return 1;
  }
  avif::data_path = argv[1];
  return RUN_ALL_TESTS();
}
