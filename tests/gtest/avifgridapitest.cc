// Copyright 2022 Google LLC. All rights reserved.
// SPDX-License-Identifier: BSD-2-Clause

#include <tuple>
#include <vector>

#include "avif/avif.h"
#include "aviftest_helpers.h"
#include "gtest/gtest.h"

using testing::Combine;
using testing::Values;
using testing::ValuesIn;

namespace libavif {
namespace {

// Pair of cell count and cell size for a single dimension.
struct Cell {
  int count, size;
};

class GridApiTest
    : public testing::TestWithParam<
          std::tuple</*horizontal=*/Cell, /*vertical=*/Cell, /*bitDepth=*/int,
                     /*yuvFormat=*/avifPixelFormat, /*createAlpha=*/bool,
                     /*expectedSuccess=*/bool>> {};

TEST_P(GridApiTest, EncodeDecode) {
  const Cell horizontal = std::get<0>(GetParam());
  const Cell vertical = std::get<1>(GetParam());
  const int bitDepth = std::get<2>(GetParam());
  const avifPixelFormat yuvFormat = std::get<3>(GetParam());
  const bool createAlpha = std::get<4>(GetParam());
  const bool expectedSuccess = std::get<5>(GetParam());

  // Construct a grid.
  std::vector<testutil::avifImagePtr> cellImages;
  cellImages.reserve(horizontal.count * vertical.count);
  for (int i = 0; i < horizontal.count * vertical.count; ++i) {
    cellImages.emplace_back(testutil::createImage(
        horizontal.size, vertical.size, bitDepth, yuvFormat,
        createAlpha ? AVIF_PLANES_ALL : AVIF_PLANES_YUV));
    ASSERT_NE(cellImages.back(), nullptr);
    testutil::fillImageGradient(cellImages.back().get());
  }

  // Encode the grid image.
  testutil::avifEncoderPtr encoder(avifEncoderCreate(), avifEncoderDestroy);
  ASSERT_NE(encoder, nullptr);
  encoder->speed = AVIF_SPEED_FASTEST;
  // Just here to match libavif API.
  std::vector<avifImage *> cellImagePtrs(cellImages.size());
  for (size_t i = 0; i < cellImages.size(); ++i) {
    cellImagePtrs[i] = cellImages[i].get();
  }
  const avifResult result =
      avifEncoderAddImageGrid(encoder.get(), horizontal.count, vertical.count,
                              cellImagePtrs.data(), AVIF_ADD_IMAGE_FLAG_SINGLE);

  if (expectedSuccess) {
    ASSERT_EQ(result, AVIF_RESULT_OK);
    testutil::avifRWDataCleaner encodedAvif;
    ASSERT_EQ(avifEncoderFinish(encoder.get(), &encodedAvif), AVIF_RESULT_OK);

    // Decode the grid image.
    testutil::avifImagePtr image(avifImageCreateEmpty(), avifImageDestroy);
    ASSERT_NE(image, nullptr);
    testutil::avifDecoderPtr decoder(avifDecoderCreate(), avifDecoderDestroy);
    ASSERT_NE(decoder, nullptr);
    ASSERT_EQ(avifDecoderReadMemory(decoder.get(), image.get(),
                                    encodedAvif.data, encodedAvif.size),
              AVIF_RESULT_OK);
  } else {
    ASSERT_TRUE(result == AVIF_RESULT_INVALID_IMAGE_GRID ||
                result == AVIF_RESULT_NO_CONTENT);
  }
}

// A cell cannot be smaller than 64px in any dimension if there are several
// cells. A cell cannot have an odd size in any dimension if there are several
// cells and chroma subsampling. Image size must be a multiple of cell size.
constexpr Cell kValidCells[] = {{1, 64}, {1, 66}, {2, 64}, {3, 68}};
constexpr Cell kInvalidCells[] = {{0, 0}, {0, 1}, {1, 0}, {2, 1},
                                  {2, 2}, {2, 3}, {2, 63}};
constexpr int kBitDepths[] = {8, 10, 12};
constexpr avifPixelFormat kPixelFormats[] = {
    AVIF_PIXEL_FORMAT_YUV444, AVIF_PIXEL_FORMAT_YUV422,
    AVIF_PIXEL_FORMAT_YUV420, AVIF_PIXEL_FORMAT_YUV400};

INSTANTIATE_TEST_SUITE_P(Valid, GridApiTest,
                         Combine(/*horizontal=*/ValuesIn(kValidCells),
                                 /*vertical=*/ValuesIn(kValidCells),
                                 ValuesIn(kBitDepths), ValuesIn(kPixelFormats),
                                 /*createAlpha=*/Values(false, true),
                                 /*expectedSuccess=*/Values(true)));

INSTANTIATE_TEST_SUITE_P(InvalidVertically, GridApiTest,
                         Combine(/*horizontal=*/ValuesIn(kValidCells),
                                 /*vertical=*/ValuesIn(kInvalidCells),
                                 ValuesIn(kBitDepths), ValuesIn(kPixelFormats),
                                 /*createAlpha=*/Values(false, true),
                                 /*expectedSuccess=*/Values(false)));
INSTANTIATE_TEST_SUITE_P(InvalidHorizontally, GridApiTest,
                         Combine(/*horizontal=*/ValuesIn(kInvalidCells),
                                 /*vertical=*/ValuesIn(kValidCells),
                                 ValuesIn(kBitDepths), ValuesIn(kPixelFormats),
                                 /*createAlpha=*/Values(false, true),
                                 /*expectedSuccess=*/Values(false)));
INSTANTIATE_TEST_SUITE_P(InvalidBoth, GridApiTest,
                         Combine(/*horizontal=*/ValuesIn(kInvalidCells),
                                 /*vertical=*/ValuesIn(kInvalidCells),
                                 ValuesIn(kBitDepths), ValuesIn(kPixelFormats),
                                 /*createAlpha=*/Values(false, true),
                                 /*expectedSuccess=*/Values(false)));

// Special case depending on the cell count and the chroma subsampling.
INSTANTIATE_TEST_SUITE_P(ValidOddHeight, GridApiTest,
                         Combine(/*horizontal=*/Values(Cell{1, 64}),
                                 /*vertical=*/Values(Cell{1, 65}, Cell{2, 65}),
                                 ValuesIn(kBitDepths),
                                 Values(AVIF_PIXEL_FORMAT_YUV444,
                                        AVIF_PIXEL_FORMAT_YUV422,
                                        AVIF_PIXEL_FORMAT_YUV400),
                                 /*createAlpha=*/Values(false, true),
                                 /*expectedSuccess=*/Values(true)));
INSTANTIATE_TEST_SUITE_P(InvalidOddHeight, GridApiTest,
                         Combine(/*horizontal=*/Values(Cell{1, 64}),
                                 /*vertical=*/Values(Cell{2, 65}),
                                 ValuesIn(kBitDepths),
                                 Values(AVIF_PIXEL_FORMAT_YUV420),
                                 /*createAlpha=*/Values(false, true),
                                 /*expectedSuccess=*/Values(false)));

// Special case depending on the cell count and the cell size.
INSTANTIATE_TEST_SUITE_P(ValidOddDimensions, GridApiTest,
                         Combine(/*horizontal=*/Values(Cell{1, 1}),
                                 /*vertical=*/Values(Cell{1, 65}),
                                 ValuesIn(kBitDepths), ValuesIn(kPixelFormats),
                                 /*createAlpha=*/Values(false, true),
                                 /*expectedSuccess=*/Values(true)));
INSTANTIATE_TEST_SUITE_P(InvalidOddDimensions, GridApiTest,
                         Combine(/*horizontal=*/Values(Cell{2, 1}),
                                 /*vertical=*/Values(Cell{1, 65}, Cell{2, 65}),
                                 ValuesIn(kBitDepths), ValuesIn(kPixelFormats),
                                 /*createAlpha=*/Values(false, true),
                                 /*expectedSuccess=*/Values(false)));

}  // namespace
}  // namespace libavif
