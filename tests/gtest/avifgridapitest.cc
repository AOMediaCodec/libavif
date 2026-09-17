// Copyright 2022 Google LLC
// SPDX-License-Identifier: BSD-2-Clause

#include <cstring>
#include <vector>

#include "avif/avif.h"
#include "avif/internal.h"
#include "aviftest_helpers.h"
#include "gtest/gtest.h"

namespace avif {
namespace {

// Returns the content of the first box of the given type among the sibling
// boxes contained in container_content, or an empty avifROData if not found.
avifROData FindBox(avifROData container_content, const char type[4]) {
  avifROStream stream;
  avifROStreamStart(&stream, &container_content,
                    /*diag=*/nullptr, /*diagContext=*/nullptr);
  avifBoxHeader header;
  while (avifROStreamReadBoxHeader(&stream, &header)) {
    if (!memcmp(header.type, type, 4)) {
      return {avifROStreamCurrent(&stream), header.size};
    }
    if (!avifROStreamSkip(&stream, header.size)) {
      break;
    }
  }
  return {nullptr, 0};
}

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

TEST(GridApiTest, ColorAlphaGridExceeding16BitItemIDs) {
  // A color grid and an alpha grid of 128x256 cells each contain 32768 cells,
  // so encoding them requires 65538 distinct item IDs (2 grid items + 2 x 32768
  // cell items), which does not fit in the 16-bit item ID space (item ID 0 is
  // also invalid, see ISO/IEC 14496-12 Section 8.11.1.1). The encoder writes
  // the 32-bit item ID variants of the boxes with item ID fields
  // ('pitm' version 1, 'iloc' version 2, 'iinf' version 1, 'infe' version 3,
  // 'iref' version 1 and 'ipma' version 1) so that such files are valid.
  // The cells are 64x64 because it is the smallest size allowed by MIAF.
  // Note: Most of the time spent in this test is the encoding of the 65536
  // cells. The generated file is not parsed with avifDecoderParse() because
  // that would cost minutes with the current quadratic lookups for this many
  // items; walking the boxes below is enough to check what was written.
  ImagePtr cell = testutil::CreateImage(
      /*width=*/64, /*height=*/64, /*depth=*/8, AVIF_PIXEL_FORMAT_YUV400,
      AVIF_PLANES_ALL /* the alpha channel is needed */);
  ASSERT_NE(cell, nullptr);
  // The pixels do not matter but avoid use-of-uninitialized-value errors.
  testutil::FillImageGradient(cell.get());
  const std::vector<avifImage*> cell_image_ptrs(128 * 256, cell.get());

  EncoderPtr encoder(avifEncoderCreate());
  ASSERT_NE(encoder, nullptr);
  // Worst quality to keep the encoding of the 65536 cells as fast as possible.
  encoder->speed = AVIF_SPEED_FASTEST;
  encoder->quality = AVIF_QUALITY_WORST;
  ASSERT_EQ(avifEncoderAddImageGrid(encoder.get(), /*gridCols=*/128,
                                    /*gridRows=*/256, cell_image_ptrs.data(),
                                    AVIF_ADD_IMAGE_FLAG_SINGLE),
            AVIF_RESULT_OK);
  testutil::AvifRwData encoded_avif;
  ASSERT_EQ(avifEncoderFinish(encoder.get(), &encoded_avif), AVIF_RESULT_OK);

  // The color grid item, the color cell items, the alpha grid item, the alpha
  // cell items and possibly some metadata items are given sequential item IDs,
  // starting with the primary item (the color grid item).
  constexpr uint32_t kExpectedItemCount = 65538;

  const avifROData file = {encoded_avif.data, encoded_avif.size};
  avifROData meta = FindBox(file, "meta");
  ASSERT_NE(meta.data, nullptr);
  // The 'meta' box is a FullBox: its version and flags come before its child
  // boxes.
  const avifROData meta_children = {meta.data + 4, meta.size - 4};

  // 'pitm' version 1 and a 32-bit primary item ID.
  avifROData pitm = FindBox(meta_children, "pitm");
  ASSERT_NE(pitm.data, nullptr);
  {
    avifROStream stream;
    avifROStreamStart(&stream, &pitm, /*diag=*/nullptr,
                      /*diagContext=*/nullptr);
    uint8_t version;
    uint32_t flags;
    ASSERT_TRUE(avifROStreamReadVersionAndFlags(&stream, &version, &flags));
    EXPECT_EQ(version, 1);
    uint32_t item_id;
    ASSERT_TRUE(avifROStreamReadU32(&stream, &item_id));
    EXPECT_EQ(item_id, 1);
  }

  // 'iloc' version 2 and a 32-bit item_count.
  avifROData iloc = FindBox(meta_children, "iloc");
  ASSERT_NE(iloc.data, nullptr);
  {
    avifROStream stream;
    avifROStreamStart(&stream, &iloc, /*diag=*/nullptr,
                      /*diagContext=*/nullptr);
    uint8_t version;
    ASSERT_TRUE(avifROStreamReadVersionAndFlags(&stream, &version, nullptr));
    EXPECT_EQ(version, 2);
    ASSERT_TRUE(avifROStreamSkip(
        &stream,
        2));  // offset_size, length_size, base_offset_size and index_size
    uint32_t item_count;
    ASSERT_TRUE(avifROStreamReadU32(&stream, &item_count));
    EXPECT_EQ(item_count, kExpectedItemCount);
  }

  // 'iinf' version 1, a 32-bit entry_count, and 'infe' entries with version 3.
  avifROData iinf = FindBox(meta_children, "iinf");
  ASSERT_NE(iinf.data, nullptr);
  {
    avifROStream stream;
    avifROStreamStart(&stream, &iinf, /*diag=*/nullptr,
                      /*diagContext=*/nullptr);
    uint8_t version;
    ASSERT_TRUE(avifROStreamReadVersionAndFlags(&stream, &version, nullptr));
    EXPECT_EQ(version, 1);
    uint32_t entry_count;
    ASSERT_TRUE(avifROStreamReadU32(&stream, &entry_count));
    EXPECT_EQ(entry_count, kExpectedItemCount);
    // The 'infe' child boxes follow the version, flags and entry_count fields.
    avifROData iinf_children = {iinf.data + 8, iinf.size - 8};
    avifROData infe = FindBox(iinf_children, "infe");
    ASSERT_NE(infe.data, nullptr);
    avifROStream infe_stream;
    avifROStreamStart(&infe_stream, &infe, /*diag=*/nullptr,
                      /*diagContext=*/nullptr);
    ASSERT_TRUE(
        avifROStreamReadVersionAndFlags(&infe_stream, &version, nullptr));
    EXPECT_EQ(version, 3);
    uint32_t item_id;
    ASSERT_TRUE(avifROStreamReadU32(&infe_stream, &item_id));
    EXPECT_EQ(item_id, 1);
  }

  // 'iref' version 1 and 32-bit item_ID fields in the 'dimg' references.
  avifROData iref = FindBox(meta_children, "iref");
  ASSERT_NE(iref.data, nullptr);
  {
    avifROStream stream;
    avifROStreamStart(&stream, &iref, /*diag=*/nullptr,
                      /*diagContext=*/nullptr);
    uint8_t version;
    ASSERT_TRUE(avifROStreamReadVersionAndFlags(&stream, &version, nullptr));
    EXPECT_EQ(version, 1);
    // The SingleItemTypeReferenceBox child boxes follow the version and flags.
    avifROData iref_children = {iref.data + 4, iref.size - 4};
    avifROData dimg = FindBox(iref_children, "dimg");
    ASSERT_NE(dimg.data, nullptr);
    avifROStream dimg_stream;
    avifROStreamStart(&dimg_stream, &dimg, /*diag=*/nullptr,
                      /*diagContext=*/nullptr);
    uint32_t from_item_id;
    ASSERT_TRUE(avifROStreamReadU32(&dimg_stream, &from_item_id));
    EXPECT_EQ(from_item_id, 1);
    uint16_t reference_count;
    ASSERT_TRUE(avifROStreamReadU16(&dimg_stream, &reference_count));
    EXPECT_EQ(reference_count, 32768);
  }

  // 'ipma' version 1 and a 32-bit item_ID field per entry.
  const avifROData iprp = FindBox(meta_children, "iprp");
  ASSERT_NE(iprp.data, nullptr);
  avifROData ipma = FindBox(iprp, "ipma");
  ASSERT_NE(ipma.data, nullptr);
  {
    avifROStream stream;
    avifROStreamStart(&stream, &ipma, /*diag=*/nullptr,
                      /*diagContext=*/nullptr);
    uint8_t version;
    uint32_t flags;
    ASSERT_TRUE(avifROStreamReadVersionAndFlags(&stream, &version, &flags));
    EXPECT_EQ(version, 1);
    EXPECT_EQ(flags, 0);  // The property_index fields are 7-bit wide.
    uint32_t entry_count;
    ASSERT_TRUE(avifROStreamReadU32(&stream, &entry_count));
    EXPECT_EQ(entry_count, kExpectedItemCount);
    uint32_t item_id;
    ASSERT_TRUE(avifROStreamReadU32(&stream, &item_id));
    EXPECT_EQ(item_id, 1);
  }
}

TEST(GridApiTest, CellCountExceeding16BitReferenceCount) {
  // A 256x256 grid contains 65536 cells, which does not fit in the 16-bit
  // reference_count field of a 'dimg' item reference (ISO/IEC 14496-12
  // Section 8.11.12), so avifEncoderAddImageGrid() refuses it. Note that the
  // maximum is a single 256x256 grid; any other combination of grid
  // dimensions is at most 256x255 = 65280 cells.
  ImagePtr cell = testutil::CreateImage(
      /*width=*/64, /*height=*/64, /*depth=*/8, AVIF_PIXEL_FORMAT_YUV400,
      AVIF_PLANES_ALL);
  ASSERT_NE(cell, nullptr);
  const std::vector<avifImage*> cell_image_ptrs(256 * 256, cell.get());

  EncoderPtr encoder(avifEncoderCreate());
  ASSERT_NE(encoder, nullptr);
  ASSERT_EQ(avifEncoderAddImageGrid(encoder.get(), /*gridCols=*/256,
                                    /*gridRows=*/256, cell_image_ptrs.data(),
                                    AVIF_ADD_IMAGE_FLAG_SINGLE),
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

}  // namespace
}  // namespace avif
