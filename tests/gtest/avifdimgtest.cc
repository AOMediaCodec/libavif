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

TEST(DimgTest, IrefRepetition) {
  testutil::AvifRwData avif = testutil::ReadFile(
      std::string(data_path) + "sofa_grid1x5_420_dimg_repeat.avif");
  ASSERT_NE(avif.size, 0u);
  ImagePtr reference(avifImageCreateEmpty());
  ASSERT_NE(reference, nullptr);
  DecoderPtr decoder(avifDecoderCreate());
  ASSERT_NE(decoder, nullptr);
  ASSERT_EQ(avifDecoderReadMemory(decoder.get(), reference.get(), avif.data,
                                  avif.size),
            AVIF_RESULT_INVALID_IMAGE_GRID);
}

TEST(DimgTest, ItemShared) {
  testutil::AvifRwData avif =
      testutil::ReadFile(std::string(data_path) +
                         "color_grid_alpha_grid_tile_shared_in_dimg.avif");
  ASSERT_NE(avif.size, 0u);
  ImagePtr reference(avifImageCreateEmpty());
  ASSERT_NE(reference, nullptr);
  DecoderPtr decoder(avifDecoderCreate());
  ASSERT_NE(decoder, nullptr);
  ASSERT_EQ(avifDecoderReadMemory(decoder.get(), reference.get(), avif.data,
                                  avif.size),
            AVIF_RESULT_NOT_IMPLEMENTED);
}

//------------------------------------------------------------------------------

TEST(DimgTest, ItemOutOfOrder) {
  testutil::AvifRwData avif =
      testutil::ReadFile(std::string(data_path) + "sofa_grid1x5_420.avif");
  ASSERT_NE(avif.size, 0u);
  ImagePtr decoded(avifImageCreateEmpty());
  ASSERT_NE(decoded, nullptr);
  DecoderPtr decoder(avifDecoderCreate());
  ASSERT_NE(decoder, nullptr);
  ASSERT_EQ(
      avifDecoderReadMemory(decoder.get(), decoded.get(), avif.data, avif.size),
      AVIF_RESULT_OK);

  testutil::AvifRwData avif_reversed_dimg_order = testutil::ReadFile(
      std::string(data_path) + "sofa_grid1x5_420_reversed_dimg_order.avif");
  ImagePtr decoded_reversed_dimg_order(avifImageCreateEmpty());
  ASSERT_NE(decoded_reversed_dimg_order, nullptr);
  ASSERT_EQ(avifDecoderReadMemory(decoder.get(), decoded.get(),
                                  avif_reversed_dimg_order.data,
                                  avif_reversed_dimg_order.size),
            AVIF_RESULT_OK);
  EXPECT_FALSE(
      testutil::AreImagesEqual(*decoded, *decoded_reversed_dimg_order));

  // Verify that it works incrementally.
  // enable_fine_incremental_check is false because the tiles are out-of-order.
  ASSERT_EQ(testutil::DecodeIncrementally(
                avif_reversed_dimg_order, decoder.get(), /*is_persistent=*/true,
                /*give_size_hint=*/true,
                /*use_nth_image_api=*/false, *decoded_reversed_dimg_order,
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
