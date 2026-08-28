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

// One item is the alpha auxiliary of both tiles of a color grid. The synthetic
// alpha grid then holds that item in both cells, which the ordered dimg input
// list can express. Every cell carries the same alpha, so the two halves of the
// alpha plane match each other while they differ in the file this one is
// derived from.
TEST(DimgTest, AlphaItemSharedBetweenTiles) {
  testutil::AvifRwData avif = testutil::ReadFile(
      std::string(data_path) + "color_grid_alpha_item_shared_in_auxl.avif");
  ASSERT_NE(avif.size, 0u);
  ImagePtr decoded(avifImageCreateEmpty());
  ASSERT_NE(decoded, nullptr);
  DecoderPtr decoder(avifDecoderCreate());
  ASSERT_NE(decoder, nullptr);
  ASSERT_EQ(
      avifDecoderReadMemory(decoder.get(), decoded.get(), avif.data, avif.size),
      AVIF_RESULT_OK);
  ASSERT_NE(decoded->alphaPlane, nullptr);

  // The grid is two cells tall, so a row in the second cell repeats a row of
  // the first one. The cell height is the tile height, not half the output
  // height.
  testutil::AvifRwData reference = testutil::ReadFile(
      std::string(data_path) + "color_grid_alpha_nogrid.avif");
  ASSERT_NE(reference.size, 0u);
  ImagePtr referenceDecoded(avifImageCreateEmpty());
  ASSERT_NE(referenceDecoded, nullptr);
  DecoderPtr referenceDecoder(avifDecoderCreate());
  ASSERT_NE(referenceDecoder, nullptr);
  ASSERT_EQ(
      avifDecoderReadMemory(referenceDecoder.get(), referenceDecoded.get(),
                            reference.data, reference.size),
      AVIF_RESULT_OK);
  ASSERT_NE(referenceDecoded->alphaPlane, nullptr);
  ASSERT_EQ(decoded->width, referenceDecoded->width);
  ASSERT_EQ(decoded->height, referenceDecoded->height);

  // The first cell is unchanged, and the second one now repeats it instead of
  // carrying the alpha of the item that no longer claims that tile.
  const uint32_t width = decoded->width;
  EXPECT_EQ(memcmp(decoded->alphaPlane, referenceDecoded->alphaPlane, width),
            0);
  const uint8_t* lastRow =
      decoded->alphaPlane +
      (size_t)decoded->alphaRowBytes * (decoded->height - 1);
  const uint8_t* referenceLastRow =
      referenceDecoded->alphaPlane +
      (size_t)referenceDecoded->alphaRowBytes * (referenceDecoded->height - 1);
  EXPECT_NE(memcmp(lastRow, referenceLastRow, width), 0);
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
