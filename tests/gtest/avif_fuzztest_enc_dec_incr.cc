// Copyright 2022 Google LLC
// SPDX-License-Identifier: BSD-2-Clause
// Encode a fuzzed image split into a grid and decode it incrementally.
// Compare the output with a non-incremental decode.

#include <cassert>
#include <cstdint>
#include <vector>

#include "avif/internal.h"
#include "avif_fuzztest_helpers.h"
#include "avifincrtest_helpers.h"
#include "aviftest_helpers.h"
#include "fuzztest/fuzztest.h"
#include "gtest/gtest.h"

using ::fuzztest::Arbitrary;
using ::fuzztest::InRange;

namespace libavif {
namespace testutil {
namespace {

::testing::Environment* const stack_limit_env =
    ::testing::AddGlobalTestEnvironment(
        new FuzztestStackLimitEnvironment("524288"));  // 512 * 1024

// Splits the input image into grid_cols*grid_rows views to be encoded as a
// grid. Returns an empty vector if the input image cannot be split that way.
std::vector<AvifImagePtr> ImageToGrid(const AvifImagePtr& image,
                                      uint32_t grid_cols, uint32_t grid_rows) {
  if (image->width < grid_cols || image->height < grid_rows) return {};

  // Round up, to make sure all samples are used by exactly one cell.
  uint32_t cell_width = (image->width + grid_cols - 1) / grid_cols;
  uint32_t cell_height = (image->height + grid_rows - 1) / grid_rows;

  if ((grid_cols - 1) * cell_width >= image->width) {
    // Some cells are completely outside the image. Fallback to a grid entirely
    // contained within the image boundaries. Some samples will be discarded but
    // at least the test can go on.
    cell_width = image->width / grid_cols;
  }
  if ((grid_rows - 1) * cell_height >= image->height) {
    cell_height = image->height / grid_rows;
  }
  std::vector<AvifImagePtr> cells;
  for (uint32_t row = 0; row < grid_rows; ++row) {
    for (uint32_t col = 0; col < grid_cols; ++col) {
      avifCropRect rect{col * cell_width, row * cell_height, cell_width,
                        cell_height};
      assert(rect.x < image->width);
      assert(rect.y < image->height);
      // The right-most and bottom-most cells may be smaller than others.
      // The encoder will pad them.
      if (rect.x + rect.width > image->width) {
        rect.width = image->width - rect.x;
      }
      if (rect.y + rect.height > image->height) {
        rect.height = image->height - rect.y;
      }
      cells.emplace_back(avifImageCreateEmpty(), avifImageDestroy);
      if (avifImageSetViewRect(cells.back().get(), image.get(), &rect) !=
          AVIF_RESULT_OK) {
        return {};
      }
    }
  }
  return cells;
}

// Converts a unique_ptr array to a raw pointer array as needed by libavif API.
std::vector<const avifImage*> UniquePtrToRawPtr(
    const std::vector<AvifImagePtr>& unique_ptrs) {
  std::vector<const avifImage*> rawPtrs;
  rawPtrs.reserve(unique_ptrs.size());
  for (const AvifImagePtr& unique_ptr : unique_ptrs) {
    rawPtrs.emplace_back(unique_ptr.get());
  }
  return rawPtrs;
}

// Encodes an image into an AVIF grid then decodes it.
void EncodeDecodeGridValid(AvifImagePtr image, AvifEncoderPtr encoder,
                           AvifDecoderPtr decoder, uint32_t grid_cols,
                           uint32_t grid_rows, bool is_encoded_data_persistent,
                           bool give_size_hint_to_decoder) {
  ASSERT_NE(image.get(), nullptr);
  ASSERT_NE(encoder.get(), nullptr);

  std::vector<AvifImagePtr> cells = ImageToGrid(image, grid_cols, grid_rows);
  if (cells.empty()) return;
  const uint32_t cell_width = cells.front().get()->width;
  const uint32_t cell_height = cells.front().get()->height;
  const uint32_t encoded_width = std::min(image->width, grid_cols * cell_width);
  const uint32_t encoded_height =
      std::min(image->height, grid_rows * cell_height);

  AvifRwData encoded_data;
  const avifResult encoder_result = avifEncoderAddImageGrid(
      encoder.get(), grid_cols, grid_rows, UniquePtrToRawPtr(cells).data(),
      AVIF_ADD_IMAGE_FLAG_SINGLE);
  if (((grid_cols > 1 || grid_rows > 1) &&
       !avifAreGridDimensionsValid(image->yuvFormat, encoded_width,
                                   encoded_height, cell_width, cell_height,
                                   nullptr))) {
    ASSERT_TRUE(encoder_result == AVIF_RESULT_INVALID_IMAGE_GRID)
        << avifResultToString(encoder_result);
    return;
  }
  ASSERT_EQ(encoder_result, AVIF_RESULT_OK)
      << avifResultToString(encoder_result);

  const avifResult finish_result =
      avifEncoderFinish(encoder.get(), &encoded_data);
  ASSERT_EQ(finish_result, AVIF_RESULT_OK) << avifResultToString(finish_result);

  DecodeNonIncrementallyAndIncrementally(encoded_data, decoder.get(),
                                         is_encoded_data_persistent,
                                         give_size_hint_to_decoder,
                                         /*useNthImageApi=*/true, cell_height);
}

FUZZ_TEST(EncodeDecodeAvifTest, EncodeDecodeGridValid)
    .WithDomains(ArbitraryAvifImage(), ArbitraryAvifEncoder(),
                 ArbitraryAvifDecoder({AVIF_CODEC_CHOICE_AUTO}),
                 InRange<uint32_t>(1, 32), InRange<uint32_t>(1, 32),
                 Arbitrary<bool>(), Arbitrary<bool>());

}  // namespace
}  // namespace testutil
}  // namespace libavif
