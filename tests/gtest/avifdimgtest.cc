// Copyright 2024 Google LLC
// SPDX-License-Identifier: BSD-2-Clause

#include <algorithm>
#include <iostream>
#include <string>

#include "avif/avif.h"
#include "avifincrtest_helpers.h"
#include "aviftest_helpers.h"
#include "gtest/gtest.h"

namespace avif {
namespace {

// Used to pass the data folder path to the GoogleTest suites.
const char* data_path = nullptr;

//------------------------------------------------------------------------------

TEST(IncrementalTest, Dimg) {
  testutil::AvifRwData avif =
      testutil::ReadFile(std::string(data_path) + "sofa_grid1x5_420.avif");
  ASSERT_NE(avif.size, 0u);
  ImagePtr reference(avifImageCreateEmpty());
  ASSERT_NE(reference, nullptr);
  DecoderPtr decoder(avifDecoderCreate());
  ASSERT_NE(decoder, nullptr);
  ASSERT_EQ(avifDecoderReadMemory(decoder.get(), reference.get(), avif.data,
                                  avif.size),
            AVIF_RESULT_OK);

  // Change the order of the 'dimg' associations.
  const uint8_t* kMeta = reinterpret_cast<const uint8_t*>("dimg");
  uint8_t* dimg_position =
      std::search(avif.data, avif.data + avif.size, kMeta, kMeta + 4);
  ASSERT_NE(dimg_position, avif.data + avif.size);
  uint8_t* to_item_id = dimg_position + /*"dimg"*/ 4 + /*from_item_ID*/ 2 +
                        /*reference_count*/ 2;
  for (uint8_t i = 0; i < 5; ++i) {
    const size_t offset =
        i * sizeof(uint16_t) + /*most significant byte of uint16_t*/ 1;
    EXPECT_EQ(to_item_id[offset], /*first tile item ID*/ 2 + i);
    to_item_id[offset] = /*first tile item ID*/ 2 + 4 - i;
  }

  // Verify that libavif detects the 'dimg' modification.
  ImagePtr reference_dimg_swapped(avifImageCreateEmpty());
  ASSERT_NE(reference_dimg_swapped, nullptr);
  ASSERT_EQ(avifDecoderReadMemory(decoder.get(), reference.get(), avif.data,
                                  avif.size),
            AVIF_RESULT_OK);
  EXPECT_FALSE(testutil::AreImagesEqual(*reference, *reference_dimg_swapped));

  // Verify that it works incrementally.
  // enable_fine_incremental_check is false because the tiles are out-of-order.
  ASSERT_EQ(
      testutil::DecodeIncrementally(
          avif, decoder.get(), /*is_persistent=*/true, /*give_size_hint=*/true,
          /*use_nth_image_api=*/false, *reference_dimg_swapped,
          /*cell_height=*/154, /*enable_fine_incremental_check=*/false),
      AVIF_RESULT_OK);
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
