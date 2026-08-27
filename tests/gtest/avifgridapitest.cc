// Copyright 2022 Google LLC
// SPDX-License-Identifier: BSD-2-Clause

#include <vector>

#include "avif/avif.h"
#include "aviftest_helpers.h"
#include "gtest/gtest.h"

namespace avif {
namespace {

// One AVIF cell in an AVIF grid.
struct Cell {
  int width, height;  // In pixels.
};

avifResult EncodeDecodeGrid(const std::vector<std::vector<Cell>>& cell_rows,
                            avifPixelFormat yuv_format) {
  // Construct a grid.
  std::vector<ImagePtr> cell_images;
  cell_images.reserve(cell_rows.size() * cell_rows.front().size());
  for (const std::vector<Cell>& cell_row : cell_rows) {
    assert(cell_row.size() == cell_rows.front().size());
    for (const Cell& cell : cell_row) {
      cell_images.emplace_back(testutil::CreateImage(
          cell.width, cell.height, /*depth=*/8, yuv_format, AVIF_PLANES_ALL));
      if (!cell_images.back()) {
        return AVIF_RESULT_INVALID_ARGUMENT;
      }
      testutil::FillImageGradient(cell_images.back().get());
    }
  }

  // Encode the grid image (losslessly for easy pixel-by-pixel comparison).
  EncoderPtr encoder(avifEncoderCreate());
  if (!encoder) {
    return AVIF_RESULT_OUT_OF_MEMORY;
  }
  encoder->speed = AVIF_SPEED_FASTEST;
  encoder->quality = AVIF_QUALITY_LOSSLESS;
  encoder->qualityAlpha = AVIF_QUALITY_LOSSLESS;
  // cell_image_ptrs exists only to match the libavif API.
  std::vector<avifImage*> cell_image_ptrs(cell_images.size());
  for (size_t i = 0; i < cell_images.size(); ++i) {
    cell_image_ptrs[i] = cell_images[i].get();
  }
  avifResult result = avifEncoderAddImageGrid(
      encoder.get(), static_cast<uint32_t>(cell_rows.front().size()),
      static_cast<uint32_t>(cell_rows.size()), cell_image_ptrs.data(),
      AVIF_ADD_IMAGE_FLAG_SINGLE);
  if (result != AVIF_RESULT_OK) {
    return result;
  }

  testutil::AvifRwData encoded_avif;
  result = avifEncoderFinish(encoder.get(), &encoded_avif);
  if (result != AVIF_RESULT_OK) {
    return result;
  }

  // Decode the grid image.
  ImagePtr image(avifImageCreateEmpty());
  DecoderPtr decoder(avifDecoderCreate());
  if (!image || !decoder) {
    return AVIF_RESULT_OUT_OF_MEMORY;
  }
  result = avifDecoderReadMemory(decoder.get(), image.get(), encoded_avif.data,
                                 encoded_avif.size);
  if (result != AVIF_RESULT_OK) {
    return result;
  }

  // Reconstruct the input image by merging all cells into a single avifImage.
  ImagePtr grid = testutil::CreateImage(
      static_cast<int>(image->width), static_cast<int>(image->height),
      /*depth=*/8, yuv_format, AVIF_PLANES_ALL);
  const int num_rows = (int)cell_rows.size();
  const int num_cols = (int)cell_rows[0].size();
  AVIF_CHECKRES(
      testutil::MergeGrid(num_cols, num_rows, cell_images, grid.get()));

  if ((grid->width != image->width) || (grid->height != image->height) ||
      !testutil::AreImagesEqual(*image, *grid)) {
    return AVIF_RESULT_UNKNOWN_ERROR;
  }

  return AVIF_RESULT_OK;
}

TEST(GridApiTest, SingleCell) {
  for (avifPixelFormat pixel_format :
       {AVIF_PIXEL_FORMAT_YUV444, AVIF_PIXEL_FORMAT_YUV422,
        AVIF_PIXEL_FORMAT_YUV420, AVIF_PIXEL_FORMAT_YUV400}) {
    // Rules on grids do not apply to a single cell.
    EXPECT_EQ(EncodeDecodeGrid({{{1, 1}}}, pixel_format), AVIF_RESULT_OK);
    EXPECT_EQ(EncodeDecodeGrid({{{1, 64}}}, pixel_format), AVIF_RESULT_OK);
    EXPECT_EQ(EncodeDecodeGrid({{{64, 1}}}, pixel_format), AVIF_RESULT_OK);
    EXPECT_EQ(EncodeDecodeGrid({{{64, 64}}}, pixel_format), AVIF_RESULT_OK);
    EXPECT_EQ(EncodeDecodeGrid({{{127, 127}}}, pixel_format), AVIF_RESULT_OK);
  }
}

TEST(GridApiTest, CellsOfSameDimensions) {
  for (avifPixelFormat pixel_format :
       {AVIF_PIXEL_FORMAT_YUV444, AVIF_PIXEL_FORMAT_YUV422,
        AVIF_PIXEL_FORMAT_YUV420, AVIF_PIXEL_FORMAT_YUV400}) {
    // ISO/IEC 23000-22:2019, Section 7.3.11.4.2:
    //   - the tile_width shall be greater than or equal to 64, and should be a
    //     multiple of 64
    //   - the tile_height shall be greater than or equal to 64, and should be a
    //     multiple of 64
    EXPECT_EQ(EncodeDecodeGrid({{{64, 64}, {64, 64}, {64, 64}}}, pixel_format),
              AVIF_RESULT_OK);
    EXPECT_EQ(EncodeDecodeGrid({{{100, 110}},  //
                                {{100, 110}},  //
                                {{100, 110}}},
                               pixel_format),
              AVIF_RESULT_OK);
    EXPECT_EQ(EncodeDecodeGrid({{{64, 64}, {64, 64}, {64, 64}},
                                {{64, 64}, {64, 64}, {64, 64}},
                                {{64, 64}, {64, 64}, {64, 64}}},
                               pixel_format),
              AVIF_RESULT_OK);

    EXPECT_EQ(EncodeDecodeGrid({{{2, 64}, {2, 64}}}, pixel_format),
              AVIF_RESULT_INVALID_IMAGE_GRID);
    EXPECT_EQ(EncodeDecodeGrid({{{64, 62}, {64, 62}}}, pixel_format),
              AVIF_RESULT_INVALID_IMAGE_GRID);
    EXPECT_EQ(EncodeDecodeGrid({{{64, 2}},  //
                                {{64, 2}}},
                               pixel_format),
              AVIF_RESULT_INVALID_IMAGE_GRID);
    EXPECT_EQ(EncodeDecodeGrid({{{2, 64}},  //
                                {{2, 64}}},
                               pixel_format),
              AVIF_RESULT_INVALID_IMAGE_GRID);
  }

  // ISO/IEC 23000-22:2019, Section 7.3.11.4.2:
  //   - when the images are in the 4:2:2 chroma sampling format the horizontal
  //     tile offsets and widths, and the output width, shall be even numbers;
  EXPECT_EQ(EncodeDecodeGrid({{{64, 65}, {64, 65}}}, AVIF_PIXEL_FORMAT_YUV422),
            AVIF_RESULT_OK);
  EXPECT_EQ(EncodeDecodeGrid({{{65, 64}, {65, 64}}}, AVIF_PIXEL_FORMAT_YUV422),
            AVIF_RESULT_INVALID_IMAGE_GRID);
  //   - when the images are in the 4:2:0 chroma sampling format both the
  //     horizontal and vertical tile offsets and widths, and the output width
  //     and height, shall be even numbers.
  EXPECT_EQ(EncodeDecodeGrid({{{64, 65}, {64, 65}}}, AVIF_PIXEL_FORMAT_YUV420),
            AVIF_RESULT_INVALID_IMAGE_GRID);
  EXPECT_EQ(EncodeDecodeGrid({{{65, 64}, {65, 64}}}, AVIF_PIXEL_FORMAT_YUV420),
            AVIF_RESULT_INVALID_IMAGE_GRID);
}

TEST(GridApiTest, CellsOfDifferentDimensions) {
  for (avifPixelFormat pixel_format :
       {AVIF_PIXEL_FORMAT_YUV444, AVIF_PIXEL_FORMAT_YUV422,
        AVIF_PIXEL_FORMAT_YUV420, AVIF_PIXEL_FORMAT_YUV400}) {
    // Right-most cells are narrower.
    EXPECT_EQ(
        EncodeDecodeGrid({{{100, 100}, {100, 100}, {66, 100}}}, pixel_format),
        AVIF_RESULT_OK);
    // Bottom-most cells are shorter.
    EXPECT_EQ(EncodeDecodeGrid({{{100, 100}, {100, 100}},
                                {{100, 100}, {100, 100}},
                                {{100, 66}, {100, 66}}},
                               pixel_format),
              AVIF_RESULT_OK);
    // Right-most cells are narrower and bottom-most cells are shorter.
    EXPECT_EQ(EncodeDecodeGrid({{{100, 100}, {100, 100}, {66, 100}},
                                {{100, 100}, {100, 100}, {66, 100}},
                                {{100, 66}, {100, 66}, {66, 66}}},
                               pixel_format),
              AVIF_RESULT_OK);

    // Right-most cells are wider.
    EXPECT_EQ(EncodeDecodeGrid({{{100, 100}, {100, 100}, {222, 100}},
                                {{100, 100}, {100, 100}, {222, 100}},
                                {{100, 100}, {100, 100}, {222, 100}}},
                               pixel_format),
              AVIF_RESULT_INVALID_IMAGE_GRID);
    // Bottom-most cells are taller.
    EXPECT_EQ(EncodeDecodeGrid({{{100, 100}, {100, 100}, {100, 100}},
                                {{100, 100}, {100, 100}, {100, 100}},
                                {{100, 222}, {100, 222}, {100, 222}}},
                               pixel_format),
              AVIF_RESULT_INVALID_IMAGE_GRID);
    // One cell dimension is off.
    EXPECT_EQ(EncodeDecodeGrid({{{100, 100}, {100, 100}, {100, 100}},
                                {{100, 100}, {66 /* here */, 100}, {100, 100}},
                                {{100, 100}, {100, 100}, {100, 100}}},
                               pixel_format),
              AVIF_RESULT_INVALID_IMAGE_GRID);
    EXPECT_EQ(EncodeDecodeGrid({{{100, 100}, {100, 100}, {66, 100}},
                                {{100, 100}, {100, 100}, {66, 100}},
                                {{100, 66}, {100, 66}, {66, 100 /* here */}}},
                               pixel_format),
              AVIF_RESULT_INVALID_IMAGE_GRID);
  }

  // ISO/IEC 23000-22:2019, Section 7.3.11.4.2:
  //   - when the images are in the 4:2:2 chroma sampling format the horizontal
  //     tile offsets and widths, and the output width, shall be even numbers;
  EXPECT_EQ(EncodeDecodeGrid({{{66, 66}},  //
                              {{66, 65}}},
                             AVIF_PIXEL_FORMAT_YUV422),
            AVIF_RESULT_OK);
  EXPECT_EQ(EncodeDecodeGrid({{{66, 66}, {65, 66}}}, AVIF_PIXEL_FORMAT_YUV422),
            AVIF_RESULT_INVALID_IMAGE_GRID);
  //   - when the images are in the 4:2:0 chroma sampling format both the
  //     horizontal and vertical tile offsets and widths, and the output width
  //     and height, shall be even numbers.
  EXPECT_EQ(EncodeDecodeGrid({{{66, 66}},  //
                              {{66, 65}}},
                             AVIF_PIXEL_FORMAT_YUV420),
            AVIF_RESULT_INVALID_IMAGE_GRID);
  EXPECT_EQ(EncodeDecodeGrid({{{66, 66}, {65, 66}}}, AVIF_PIXEL_FORMAT_YUV420),
            AVIF_RESULT_INVALID_IMAGE_GRID);
}

//------------------------------------------------------------------------------

TEST(GridApiTest, SameMatrixCoefficients) {
  ImagePtr cell_0 = testutil::CreateImage(
      64, 64, /*depth=*/8, AVIF_PIXEL_FORMAT_YUV444, AVIF_PLANES_ALL);
  ImagePtr cell_1 = testutil::CreateImage(
      1, 64, /*depth=*/8, AVIF_PIXEL_FORMAT_YUV444, AVIF_PLANES_ALL);
  ASSERT_NE(cell_0, nullptr);
  ASSERT_NE(cell_1, nullptr);

  // The pixels do not matter but avoid use-of-uninitialized-value errors.
  testutil::FillImageGradient(cell_0.get());
  testutil::FillImageGradient(cell_1.get());

  // All input cells have the same non-default properties.
  cell_0->matrixCoefficients = AVIF_MATRIX_COEFFICIENTS_BT601;
  cell_1->matrixCoefficients = AVIF_MATRIX_COEFFICIENTS_BT601;

  EncoderPtr encoder(avifEncoderCreate());
  ASSERT_NE(encoder, nullptr);
  encoder->speed = AVIF_SPEED_FASTEST;
  const avifImage* cell_image_ptrs[2] = {cell_0.get(), cell_1.get()};
  ASSERT_EQ(
      avifEncoderAddImageGrid(encoder.get(), /*gridCols=*/2, /*gridRows=*/1,
                              cell_image_ptrs, AVIF_ADD_IMAGE_FLAG_SINGLE),
      AVIF_RESULT_OK);
  testutil::AvifRwData encoded_avif;
  ASSERT_EQ(avifEncoderFinish(encoder.get(), &encoded_avif), AVIF_RESULT_OK);
  ASSERT_NE(testutil::Decode(encoded_avif.data, encoded_avif.size), nullptr);
}

TEST(GridApiTest, DifferentMatrixCoefficients) {
  ImagePtr cell_0 = testutil::CreateImage(
      64, 64, /*depth=*/8, AVIF_PIXEL_FORMAT_YUV444, AVIF_PLANES_ALL);
  ImagePtr cell_1 = testutil::CreateImage(
      1, 64, /*depth=*/8, AVIF_PIXEL_FORMAT_YUV444, AVIF_PLANES_ALL);
  ASSERT_NE(cell_0, nullptr);
  ASSERT_NE(cell_1, nullptr);

  // The pixels do not matter but avoid use-of-uninitialized-value errors.
  testutil::FillImageGradient(cell_0.get());
  testutil::FillImageGradient(cell_1.get());

  // Some input cells have different properties.
  cell_0->matrixCoefficients = AVIF_MATRIX_COEFFICIENTS_BT601;
  cell_1->matrixCoefficients = AVIF_MATRIX_COEFFICIENTS_UNSPECIFIED;

  EncoderPtr encoder(avifEncoderCreate());
  ASSERT_NE(encoder, nullptr);
  encoder->speed = AVIF_SPEED_FASTEST;
  // Encoding should fail.
  const avifImage* cell_image_ptrs[2] = {cell_0.get(), cell_1.get()};
  ASSERT_EQ(
      avifEncoderAddImageGrid(encoder.get(), /*gridCols=*/2, /*gridRows=*/1,
                              cell_image_ptrs, AVIF_ADD_IMAGE_FLAG_SINGLE),
      AVIF_RESULT_INVALID_IMAGE_GRID);
}

TEST(GridApiTest, CellsTooManyForDimgReferenceCount) {
  // Section 8.11.12.1 of ISO/IEC 14496-12 requires all the references from one
  // item to be collected into a single item type reference box, whose
  // reference_count field is unsigned int(16) whatever the 'iref' version
  // (Section 8.11.12.2). ISO/IEC 23008-12 (HEIF) Section 6.6.1 additionally
  // forbids more than one 'dimg' box with the same from_item_ID. A grid item
  // therefore cannot have more than 65535 cells, so a 256x256-cell grid is
  // rejected upfront instead of silently generating a broken file.
  ImagePtr cell = testutil::CreateImage(
      /*width=*/64, /*height=*/64, /*depth=*/8, AVIF_PIXEL_FORMAT_YUV400,
      AVIF_PLANES_YUV);
  ASSERT_NE(cell, nullptr);
  testutil::FillImageGradient(cell.get());
  const std::vector<avifImage*> cell_image_ptrs(256 * 256, cell.get());

  EncoderPtr encoder(avifEncoderCreate());
  ASSERT_NE(encoder, nullptr);
  encoder->speed = AVIF_SPEED_FASTEST;
  ASSERT_EQ(avifEncoderAddImageGrid(encoder.get(), /*gridCols=*/256,
                                    /*gridRows=*/256, cell_image_ptrs.data(),
                                    AVIF_ADD_IMAGE_FLAG_SINGLE),
            AVIF_RESULT_INVALID_IMAGE_GRID);
}

TEST(GridApiTest, ColorAlphaGridExceeding16BitItemIDs) {
  // A color grid and an alpha grid of 128x256 cells each contain 32768 cells,
  // so encoding them requires 65538 distinct item IDs (2 grid items + 2 x 32768
  // cell items), which does not fit in the 16-bit item ID space. Such a file is
  // conformant though, so the encoder relies on 32-bit item IDs, switching to
  // the 'infe' version 3, 'iloc' version 2, 'iref' version 1 and 'ipma' version
  // 1 boxes, rather than overflowing the item IDs. The cells are 64x64 because
  // it is the smallest size allowed by [MIAF].
  ImagePtr cell = testutil::CreateImage(
      /*width=*/64, /*height=*/64, /*depth=*/8, AVIF_PIXEL_FORMAT_YUV420,
      AVIF_PLANES_ALL /* alpha channel needed */);
  ASSERT_NE(cell, nullptr);
  // The pixels do not matter but avoid use-of-uninitialized-value errors.
  testutil::FillImageGradient(cell.get());
  const std::vector<avifImage*> cell_image_ptrs(128 * 256, cell.get());

  EncoderPtr encoder(avifEncoderCreate());
  ASSERT_NE(encoder, nullptr);
  // Worst quality to keep the encoding of the 65536 cells fast.
  encoder->speed = AVIF_SPEED_FASTEST;
  encoder->minQuantizer = 63;
  encoder->maxQuantizer = 63;
  encoder->minQuantizerAlpha = 63;
  encoder->maxQuantizerAlpha = 63;
  ASSERT_EQ(avifEncoderAddImageGrid(encoder.get(), /*gridCols=*/128,
                                    /*gridRows=*/256, cell_image_ptrs.data(),
                                    AVIF_ADD_IMAGE_FLAG_SINGLE),
            AVIF_RESULT_OK);
  testutil::AvifRwData encoded_avif;
  ASSERT_EQ(avifEncoderFinish(encoder.get(), &encoded_avif), AVIF_RESULT_OK);

  // Decoding all 65536 cells would take too long for a test, so only parse the
  // file, which still walks the whole 'iinf', 'iloc', 'iref' and 'ipma' boxes
  // and verifies that all 65538 item IDs are valid and unique.
  DecoderPtr decoder(avifDecoderCreate());
  ASSERT_NE(decoder, nullptr);
  ASSERT_EQ(avifDecoderSetIOMemory(decoder.get(), encoded_avif.data,
                                   encoded_avif.size),
            AVIF_RESULT_OK);
  ASSERT_EQ(avifDecoderParse(decoder.get()), AVIF_RESULT_OK);
  EXPECT_EQ(decoder->image->width, 64 * 128);
  EXPECT_EQ(decoder->image->height, 64 * 256);
  EXPECT_EQ(decoder->alphaPresent, AVIF_TRUE);
}

}  // namespace
}  // namespace avif
