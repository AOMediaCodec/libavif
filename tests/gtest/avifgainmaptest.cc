// Copyright 2023 Google LLC
// SPDX-License-Identifier: BSD-2-Clause

#include <cmath>
#include <fstream>
#include <iostream>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include "avif/avif_cxx.h"
#include "avif/internal.h"
#include "avifincrtest_helpers.h"
#include "aviftest_helpers.h"
#include "gtest/gtest.h"

namespace avif {
namespace {

using ::testing::Values;

// Used to pass the data folder path to the GoogleTest suites.
const char* data_path = nullptr;

void CheckGainMapMetadataMatches(const avifGainMap& lhs,
                                 const avifGainMap& rhs) {
  EXPECT_EQ(lhs.baseHdrHeadroom.n, rhs.baseHdrHeadroom.n);
  EXPECT_EQ(lhs.baseHdrHeadroom.d, rhs.baseHdrHeadroom.d);
  EXPECT_EQ(lhs.alternateHdrHeadroom.n, rhs.alternateHdrHeadroom.n);
  EXPECT_EQ(lhs.alternateHdrHeadroom.d, rhs.alternateHdrHeadroom.d);
  for (int c = 0; c < 3; ++c) {
    SCOPED_TRACE(c);
    EXPECT_EQ(lhs.baseOffset[c].n, rhs.baseOffset[c].n);
    EXPECT_EQ(lhs.baseOffset[c].d, rhs.baseOffset[c].d);
    EXPECT_EQ(lhs.alternateOffset[c].n, rhs.alternateOffset[c].n);
    EXPECT_EQ(lhs.alternateOffset[c].d, rhs.alternateOffset[c].d);
    EXPECT_EQ(lhs.gainMapGamma[c].n, rhs.gainMapGamma[c].n);
    EXPECT_EQ(lhs.gainMapGamma[c].d, rhs.gainMapGamma[c].d);
    EXPECT_EQ(lhs.gainMapMin[c].n, rhs.gainMapMin[c].n);
    EXPECT_EQ(lhs.gainMapMin[c].d, rhs.gainMapMin[c].d);
    EXPECT_EQ(lhs.gainMapMax[c].n, rhs.gainMapMax[c].n);
    EXPECT_EQ(lhs.gainMapMax[c].d, rhs.gainMapMax[c].d);
  }
}

void FillTestGainMapMetadata(bool base_rendition_is_hdr, avifGainMap* gainMap) {
  gainMap->useBaseColorSpace = true;
  gainMap->baseHdrHeadroom = {0, 1};
  gainMap->alternateHdrHeadroom = {6, 2};
  if (base_rendition_is_hdr) {
    std::swap(gainMap->baseHdrHeadroom, gainMap->alternateHdrHeadroom);
  }
  for (int c = 0; c < 3; ++c) {
    gainMap->baseOffset[c] = {10 * c, 1000};
    gainMap->alternateOffset[c] = {20 * c, 1000};
    gainMap->gainMapGamma[c] = {1, static_cast<uint32_t>(c + 1)};
    gainMap->gainMapMin[c] = {-1, static_cast<uint32_t>(c + 1)};
    gainMap->gainMapMax[c] = {10 + c + 1, static_cast<uint32_t>(c + 1)};
  }
}

ImagePtr CreateTestImageWithGainMap(bool base_rendition_is_hdr) {
  ImagePtr image =
      testutil::CreateImage(/*width=*/12, /*height=*/34, /*depth=*/10,
                            AVIF_PIXEL_FORMAT_YUV420, AVIF_PLANES_ALL);
  if (image == nullptr) {
    return nullptr;
  }
  image->transferCharacteristics =
      (avifTransferCharacteristics)(base_rendition_is_hdr
                                        ? AVIF_TRANSFER_CHARACTERISTICS_PQ
                                        : AVIF_TRANSFER_CHARACTERISTICS_SRGB);
  testutil::FillImageGradient(image.get());
  ImagePtr gain_map =
      testutil::CreateImage(/*width=*/6, /*height=*/17, /*depth=*/8,
                            AVIF_PIXEL_FORMAT_YUV420, AVIF_PLANES_YUV);
  if (gain_map == nullptr) {
    return nullptr;
  }
  testutil::FillImageGradient(gain_map.get());
  image->gainMap = avifGainMapCreate();
  if (image->gainMap == nullptr) {
    return nullptr;
  }
  image->gainMap->image = gain_map.release();  // 'image' now owns the gain map.
  FillTestGainMapMetadata(base_rendition_is_hdr, image->gainMap);

  if (base_rendition_is_hdr) {
    image->clli.maxCLL = 10;
    image->clli.maxPALL = 5;
    image->gainMap->altDepth = 8;
    image->gainMap->altPlaneCount = 3;
    image->gainMap->altColorPrimaries = AVIF_COLOR_PRIMARIES_BT601;
    image->gainMap->altTransferCharacteristics =
        AVIF_TRANSFER_CHARACTERISTICS_SRGB;
    image->gainMap->altMatrixCoefficients = AVIF_MATRIX_COEFFICIENTS_SMPTE2085;
  } else {
    image->gainMap->altCLLI.maxCLL = 10;
    image->gainMap->altCLLI.maxPALL = 5;
    image->gainMap->altDepth = 10;
    image->gainMap->altPlaneCount = 3;
    image->gainMap->altColorPrimaries = AVIF_COLOR_PRIMARIES_BT2020;
    image->gainMap->altTransferCharacteristics =
        AVIF_TRANSFER_CHARACTERISTICS_PQ;
    image->gainMap->altMatrixCoefficients = AVIF_MATRIX_COEFFICIENTS_SMPTE2085;
  }

  return image;
}

TEST(GainMapTest, EncodeDecodeBaseImageSdr) {
  ImagePtr image = CreateTestImageWithGainMap(/*base_rendition_is_hdr=*/false);
  ASSERT_NE(image, nullptr);

  EncoderPtr encoder(avifEncoderCreate());
  ASSERT_NE(encoder, nullptr);
  testutil::AvifRwData encoded;
  avifResult result = avifEncoderWrite(encoder.get(), image.get(), &encoded);
  ASSERT_EQ(result, AVIF_RESULT_OK)
      << avifResultToString(result) << " " << encoder->diag.error;

  DecoderPtr decoder(avifDecoderCreate());
  ASSERT_NE(decoder, nullptr);
  decoder->imageContentToDecode |= AVIF_IMAGE_CONTENT_GAIN_MAP;

  result = avifDecoderSetIOMemory(decoder.get(), encoded.data, encoded.size);
  ASSERT_EQ(result, AVIF_RESULT_OK)
      << avifResultToString(result) << " " << decoder->diag.error;

  // Just parse the image first.
  result = avifDecoderParse(decoder.get());
  ASSERT_EQ(result, AVIF_RESULT_OK)
      << avifResultToString(result) << " " << decoder->diag.error;
  avifImage* decoded = decoder->image;
  ASSERT_NE(decoded, nullptr);

  // Verify that the gain map is present and matches the input.
  ASSERT_NE(decoded->gainMap, nullptr);
  ASSERT_NE(decoded->gainMap->image, nullptr);
  EXPECT_EQ(decoded->gainMap->image->matrixCoefficients,
            image->gainMap->image->matrixCoefficients);
  EXPECT_EQ(decoded->gainMap->altCLLI.maxCLL, image->gainMap->altCLLI.maxCLL);
  EXPECT_EQ(decoded->gainMap->altCLLI.maxPALL, image->gainMap->altCLLI.maxPALL);
  EXPECT_EQ(decoded->gainMap->altDepth, 10u);
  EXPECT_EQ(decoded->gainMap->altPlaneCount, 3u);
  EXPECT_EQ(decoded->gainMap->altColorPrimaries, AVIF_COLOR_PRIMARIES_BT2020);
  EXPECT_EQ(decoded->gainMap->altTransferCharacteristics,
            AVIF_TRANSFER_CHARACTERISTICS_PQ);
  EXPECT_EQ(decoded->gainMap->altMatrixCoefficients,
            AVIF_MATRIX_COEFFICIENTS_SMPTE2085);
  EXPECT_EQ(decoded->gainMap->image->width, image->gainMap->image->width);
  EXPECT_EQ(decoded->gainMap->image->height, image->gainMap->image->height);
  EXPECT_EQ(decoded->gainMap->image->depth, image->gainMap->image->depth);
  CheckGainMapMetadataMatches(*decoded->gainMap, *image->gainMap);

  // Decode the image.
  result = avifDecoderNextImage(decoder.get());
  ASSERT_EQ(result, AVIF_RESULT_OK)
      << avifResultToString(result) << " " << decoder->diag.error;

  // Verify that the input and decoded images are close.
  EXPECT_GT(testutil::GetPsnr(*image, *decoded), 40.0);
  EXPECT_GT(testutil::GetPsnr(*image->gainMap->image, *decoded->gainMap->image),
            40.0);

  // Uncomment the following to save the encoded image as an AVIF file.
  //  std::ofstream("/tmp/avifgainmaptest_basesdr.avif", std::ios::binary)
  //      .write(reinterpret_cast<char*>(encoded.data), encoded.size);
}

TEST(GainMapTest, EncodeDecodeBaseImageHdr) {
  ImagePtr image = CreateTestImageWithGainMap(/*base_rendition_is_hdr=*/true);
  ASSERT_NE(image, nullptr);

  EncoderPtr encoder(avifEncoderCreate());
  ASSERT_NE(encoder, nullptr);
  testutil::AvifRwData encoded;
  avifResult result = avifEncoderWrite(encoder.get(), image.get(), &encoded);
  ASSERT_EQ(result, AVIF_RESULT_OK)
      << avifResultToString(result) << " " << encoder->diag.error;

  ImagePtr decoded(avifImageCreateEmpty());
  ASSERT_NE(decoded, nullptr);
  DecoderPtr decoder(avifDecoderCreate());
  ASSERT_NE(decoder, nullptr);
  decoder->imageContentToDecode |= AVIF_IMAGE_CONTENT_GAIN_MAP;
  result = avifDecoderReadMemory(decoder.get(), decoded.get(), encoded.data,
                                 encoded.size);
  ASSERT_EQ(result, AVIF_RESULT_OK)
      << avifResultToString(result) << " " << decoder->diag.error;

  // Verify that the input and decoded images are close.
  EXPECT_GT(testutil::GetPsnr(*image, *decoded), 40.0);
  // Verify that the gain map is present and matches the input.
  ASSERT_NE(decoded->gainMap, nullptr);
  ASSERT_NE(decoded->gainMap->image, nullptr);
  EXPECT_GT(testutil::GetPsnr(*image->gainMap->image, *decoded->gainMap->image),
            40.0);
  EXPECT_EQ(decoded->clli.maxCLL, image->clli.maxCLL);
  EXPECT_EQ(decoded->clli.maxPALL, image->clli.maxPALL);
  EXPECT_EQ(decoded->gainMap->altCLLI.maxCLL, 0u);
  EXPECT_EQ(decoded->gainMap->altCLLI.maxPALL, 0u);
  EXPECT_EQ(decoded->gainMap->altDepth, 8u);
  EXPECT_EQ(decoded->gainMap->altPlaneCount, 3u);
  EXPECT_EQ(decoded->gainMap->altColorPrimaries, AVIF_COLOR_PRIMARIES_BT601);
  EXPECT_EQ(decoded->gainMap->altTransferCharacteristics,
            AVIF_TRANSFER_CHARACTERISTICS_SRGB);
  EXPECT_EQ(decoded->gainMap->altMatrixCoefficients,
            AVIF_MATRIX_COEFFICIENTS_SMPTE2085);
  EXPECT_EQ(decoded->gainMap->image->width, image->gainMap->image->width);
  EXPECT_EQ(decoded->gainMap->image->height, image->gainMap->image->height);
  EXPECT_EQ(decoded->gainMap->image->depth, image->gainMap->image->depth);
  CheckGainMapMetadataMatches(*decoded->gainMap, *image->gainMap);

  // Uncomment the following to save the encoded image as an AVIF file.
  //  std::ofstream("/tmp/avifgainmaptest_basehdr.avif", std::ios::binary)
  //      .write(reinterpret_cast<char*>(encoded.data), encoded.size);
}

TEST(GainMapTest, EncodeDecodeOrientedNotEqual) {
  ImagePtr image = CreateTestImageWithGainMap(/*base_rendition_is_hdr=*/false);
  ASSERT_NE(image, nullptr);
  image->gainMap->image->transformFlags = AVIF_TRANSFORM_IMIR;
  // The gain map should have no transformative property. Expect a failure.
  EncoderPtr encoder(avifEncoderCreate());
  ASSERT_NE(encoder, nullptr);
  testutil::AvifRwData encoded;
  ASSERT_EQ(avifEncoderWrite(encoder.get(), image.get(), &encoded),
            AVIF_RESULT_ENCODE_GAIN_MAP_FAILED);
}

TEST(GainMapTest, EncodeDecodeOriented) {
  ImagePtr image = CreateTestImageWithGainMap(/*base_rendition_is_hdr=*/false);
  ASSERT_NE(image, nullptr);
  image->transformFlags = AVIF_TRANSFORM_IROT | AVIF_TRANSFORM_IMIR;
  image->irot.angle = 1;
  image->imir.axis = 0;

  EncoderPtr encoder(avifEncoderCreate());
  ASSERT_NE(encoder, nullptr);
  testutil::AvifRwData encoded;
  ASSERT_EQ(avifEncoderWrite(encoder.get(), image.get(), &encoded),
            AVIF_RESULT_OK);

  DecoderPtr decoder(avifDecoderCreate());
  ASSERT_NE(decoder, nullptr);
  decoder->imageContentToDecode |= AVIF_IMAGE_CONTENT_GAIN_MAP;
  ASSERT_EQ(avifDecoderSetIOMemory(decoder.get(), encoded.data, encoded.size),
            AVIF_RESULT_OK);
  ASSERT_EQ(avifDecoderParse(decoder.get()), AVIF_RESULT_OK);

  // Verify that the transformative properties were kept.
  EXPECT_EQ(decoder->image->transformFlags, image->transformFlags);
  EXPECT_EQ(decoder->image->irot.angle, image->irot.angle);
  EXPECT_EQ(decoder->image->imir.axis, image->imir.axis);
  EXPECT_EQ(decoder->image->gainMap->image->transformFlags,
            AVIF_TRANSFORM_NONE);
}

TEST(GainMapTest, EncodeDecodeMetadataSameDenominator) {
  ImagePtr image = CreateTestImageWithGainMap(/*base_rendition_is_hdr=*/true);
  ASSERT_NE(image, nullptr);

  const uint32_t kDenominator = 1000;
  image->gainMap->baseHdrHeadroom.d = kDenominator;
  image->gainMap->alternateHdrHeadroom.d = kDenominator;
  for (int c = 0; c < 3; ++c) {
    image->gainMap->baseOffset[c].d = kDenominator;
    image->gainMap->alternateOffset[c].d = kDenominator;
    image->gainMap->gainMapGamma[c].d = kDenominator;
    image->gainMap->gainMapMin[c].d = kDenominator;
    image->gainMap->gainMapMax[c].d = kDenominator;
  }

  EncoderPtr encoder(avifEncoderCreate());
  ASSERT_NE(encoder, nullptr);
  testutil::AvifRwData encoded;
  avifResult result = avifEncoderWrite(encoder.get(), image.get(), &encoded);
  ASSERT_EQ(result, AVIF_RESULT_OK)
      << avifResultToString(result) << " " << encoder->diag.error;

  ImagePtr decoded(avifImageCreateEmpty());
  ASSERT_NE(decoded, nullptr);
  DecoderPtr decoder(avifDecoderCreate());
  ASSERT_NE(decoder, nullptr);
  result = avifDecoderReadMemory(decoder.get(), decoded.get(), encoded.data,
                                 encoded.size);
  ASSERT_EQ(result, AVIF_RESULT_OK)
      << avifResultToString(result) << " " << decoder->diag.error;

  // Verify that the gain map metadata matches the input.
  CheckGainMapMetadataMatches(*decoded->gainMap, *image->gainMap);
}

TEST(GainMapTest, EncodeDecodeMetadataAllChannelsIdentical) {
  ImagePtr image = CreateTestImageWithGainMap(/*base_rendition_is_hdr=*/true);
  ASSERT_NE(image, nullptr);

  for (int c = 0; c < 3; ++c) {
    image->gainMap->baseOffset[c] = {1, 2};
    image->gainMap->alternateOffset[c] = {3, 4};
    image->gainMap->gainMapGamma[c] = {5, 6};
    image->gainMap->gainMapMin[c] = {7, 8};
    image->gainMap->gainMapMax[c] = {9, 10};
  }

  EncoderPtr encoder(avifEncoderCreate());
  ASSERT_NE(encoder, nullptr);
  testutil::AvifRwData encoded;
  avifResult result = avifEncoderWrite(encoder.get(), image.get(), &encoded);
  ASSERT_EQ(result, AVIF_RESULT_OK)
      << avifResultToString(result) << " " << encoder->diag.error;

  ImagePtr decoded(avifImageCreateEmpty());
  ASSERT_NE(decoded, nullptr);
  DecoderPtr decoder(avifDecoderCreate());
  ASSERT_NE(decoder, nullptr);
  result = avifDecoderReadMemory(decoder.get(), decoded.get(), encoded.data,
                                 encoded.size);
  ASSERT_EQ(result, AVIF_RESULT_OK)
      << avifResultToString(result) << " " << decoder->diag.error;

  // Verify that the gain map metadata matches the input.
  CheckGainMapMetadataMatches(*decoded->gainMap, *image->gainMap);
}

TEST(GainMapTest, EncodeDecodeGrid) {
  std::vector<ImagePtr> cells;
  std::vector<const avifImage*> cell_ptrs;
  std::vector<const avifImage*> gain_map_ptrs;
  constexpr int kGridCols = 2;
  constexpr int kGridRows = 2;
  constexpr int kCellWidth = 128;
  constexpr int kCellHeight = 200;

  for (int i = 0; i < kGridCols * kGridRows; ++i) {
    const int gradient_offset = i * 10;
    ImagePtr image =
        testutil::CreateImage(kCellWidth, kCellHeight, /*depth=*/10,
                              AVIF_PIXEL_FORMAT_YUV444, AVIF_PLANES_ALL);
    ASSERT_NE(image, nullptr);
    image->transferCharacteristics = AVIF_TRANSFER_CHARACTERISTICS_PQ;
    testutil::FillImageGradient(image.get(), gradient_offset);
    ImagePtr gain_map =
        testutil::CreateImage(kCellWidth / 2, kCellHeight / 2, /*depth=*/8,
                              AVIF_PIXEL_FORMAT_YUV420, AVIF_PLANES_YUV);
    ASSERT_NE(gain_map, nullptr);
    testutil::FillImageGradient(gain_map.get(), gradient_offset);
    // 'image' now owns the gain map.
    image->gainMap = avifGainMapCreate();
    ASSERT_NE(image->gainMap, nullptr);
    image->gainMap->image = gain_map.release();
    // all cells must have the same metadata
    FillTestGainMapMetadata(/*base_rendition_is_hdr=*/true, image->gainMap);

    cell_ptrs.push_back(image.get());
    gain_map_ptrs.push_back(image->gainMap->image);
    cells.push_back(std::move(image));
  }

  EncoderPtr encoder(avifEncoderCreate());
  ASSERT_NE(encoder, nullptr);
  testutil::AvifRwData encoded;
  avifResult result =
      avifEncoderAddImageGrid(encoder.get(), kGridCols, kGridRows,
                              cell_ptrs.data(), AVIF_ADD_IMAGE_FLAG_SINGLE);
  ASSERT_EQ(result, AVIF_RESULT_OK)
      << avifResultToString(result) << " " << encoder->diag.error;
  result = avifEncoderFinish(encoder.get(), &encoded);
  ASSERT_EQ(result, AVIF_RESULT_OK)
      << avifResultToString(result) << " " << encoder->diag.error;

  ImagePtr decoded(avifImageCreateEmpty());
  ASSERT_NE(decoded, nullptr);
  DecoderPtr decoder(avifDecoderCreate());
  ASSERT_NE(decoder, nullptr);
  decoder->imageContentToDecode |= AVIF_IMAGE_CONTENT_GAIN_MAP;
  result = avifDecoderReadMemory(decoder.get(), decoded.get(), encoded.data,
                                 encoded.size);
  ASSERT_EQ(result, AVIF_RESULT_OK)
      << avifResultToString(result) << " " << decoder->diag.error;

  ImagePtr merged = testutil::CreateImage(
      static_cast<int>(decoded->width), static_cast<int>(decoded->height),
      decoded->depth, decoded->yuvFormat, AVIF_PLANES_ALL);
  ASSERT_EQ(testutil::MergeGrid(kGridCols, kGridRows, cell_ptrs, merged.get()),
            AVIF_RESULT_OK);

  ImagePtr merged_gain_map = testutil::CreateImage(
      static_cast<int>(decoded->gainMap->image->width),
      static_cast<int>(decoded->gainMap->image->height),
      decoded->gainMap->image->depth, decoded->gainMap->image->yuvFormat,
      AVIF_PLANES_YUV);
  ASSERT_EQ(testutil::MergeGrid(kGridCols, kGridRows, gain_map_ptrs,
                                merged_gain_map.get()),
            AVIF_RESULT_OK);

  // Verify that the input and decoded images are close.
  ASSERT_GT(testutil::GetPsnr(*merged, *decoded), 40.0);
  // Verify that the gain map is present and matches the input.
  ASSERT_NE(decoded->gainMap, nullptr);
  ASSERT_NE(decoded->gainMap->image, nullptr);
  ASSERT_GT(testutil::GetPsnr(*merged_gain_map, *decoded->gainMap->image),
            40.0);
  CheckGainMapMetadataMatches(*decoded->gainMap, *cell_ptrs[0]->gainMap);

  // Check that non-incremental and incremental decodings of a grid AVIF produce
  // the same pixels.
  ASSERT_EQ(testutil::DecodeNonIncrementallyAndIncrementally(
                encoded, decoder.get(),
                /*is_persistent=*/true, /*give_size_hint=*/true,
                /*use_nth_image_api=*/false, kCellHeight,
                /*enable_fine_incremental_check=*/true),
            AVIF_RESULT_OK)
      << decoder->diag.error;

  // Uncomment the following to save the encoded image as an AVIF file.
  //  std::ofstream("/tmp/avifgainmaptest_grid.avif", std::ios::binary)
  //      .write(reinterpret_cast<char*>(encoded.data), encoded.size);
}

TEST(GainMapTest, InvalidGrid) {
  std::vector<ImagePtr> cells;
  std::vector<const avifImage*> cell_ptrs;
  constexpr int kGridCols = 2;
  constexpr int kGridRows = 2;

  for (int i = 0; i < kGridCols * kGridRows; ++i) {
    ImagePtr image =
        testutil::CreateImage(/*width=*/64, /*height=*/100, /*depth=*/10,
                              AVIF_PIXEL_FORMAT_YUV444, AVIF_PLANES_ALL);
    ASSERT_NE(image, nullptr);
    image->transferCharacteristics = AVIF_TRANSFER_CHARACTERISTICS_PQ;
    testutil::FillImageGradient(image.get());
    ImagePtr gain_map =
        testutil::CreateImage(/*width=*/64, /*height=*/100, /*depth=*/8,
                              AVIF_PIXEL_FORMAT_YUV420, AVIF_PLANES_YUV);
    ASSERT_NE(gain_map, nullptr);
    testutil::FillImageGradient(gain_map.get());
    // 'image' now owns the gain map.
    image->gainMap = avifGainMapCreate();
    ASSERT_NE(image->gainMap, nullptr);
    image->gainMap->image = gain_map.release();
    // all cells must have the same metadata
    FillTestGainMapMetadata(/*base_rendition_is_hdr=*/true, image->gainMap);

    cell_ptrs.push_back(image.get());
    cells.push_back(std::move(image));
  }

  EncoderPtr encoder(avifEncoderCreate());
  ASSERT_NE(encoder, nullptr);
  testutil::AvifRwData encoded;

  avifResult result;

  // Invalid: one cell has the wrong size.
  cells[1]->gainMap->image->height = 90;
  result =
      avifEncoderAddImageGrid(encoder.get(), kGridCols, kGridRows,
                              cell_ptrs.data(), AVIF_ADD_IMAGE_FLAG_SINGLE);
  EXPECT_EQ(result, AVIF_RESULT_INVALID_IMAGE_GRID)
      << avifResultToString(result) << " " << encoder->diag.error;
  cells[1]->gainMap->image->height =
      cells[0]->gainMap->image->height;  // Revert.

  // Invalid: one cell has a different depth.
  cells[1]->gainMap->image->depth = 12;
  result =
      avifEncoderAddImageGrid(encoder.get(), kGridCols, kGridRows,
                              cell_ptrs.data(), AVIF_ADD_IMAGE_FLAG_SINGLE);
  EXPECT_EQ(result, AVIF_RESULT_INVALID_IMAGE_GRID)
      << avifResultToString(result) << " " << encoder->diag.error;
  cells[1]->gainMap->image->depth = cells[0]->gainMap->image->depth;  // Revert.

  // Invalid: one cell has different gain map metadata
  cells[1]->gainMap->gainMapGamma[0].n = 42;
  result =
      avifEncoderAddImageGrid(encoder.get(), kGridCols, kGridRows,
                              cell_ptrs.data(), AVIF_ADD_IMAGE_FLAG_SINGLE);
  EXPECT_EQ(result, AVIF_RESULT_INVALID_IMAGE_GRID)
      << avifResultToString(result) << " " << encoder->diag.error;
  cells[1]->gainMap->gainMapGamma[0].n =
      cells[0]->gainMap->gainMapGamma[0].n;  // Revert.
}

TEST(GainMapTest, SequenceNotSupported) {
  ImagePtr image =
      testutil::CreateImage(/*width=*/64, /*height=*/100, /*depth=*/10,
                            AVIF_PIXEL_FORMAT_YUV444, AVIF_PLANES_ALL);
  ASSERT_NE(image, nullptr);
  image->transferCharacteristics = AVIF_TRANSFER_CHARACTERISTICS_PQ;
  testutil::FillImageGradient(image.get());
  ImagePtr gain_map =
      testutil::CreateImage(/*width=*/64, /*height=*/100, /*depth=*/8,
                            AVIF_PIXEL_FORMAT_YUV420, AVIF_PLANES_YUV);
  ASSERT_NE(gain_map, nullptr);
  testutil::FillImageGradient(gain_map.get());
  // 'image' now owns the gain map.
  image->gainMap = avifGainMapCreate();
  ASSERT_NE(image->gainMap, nullptr);
  image->gainMap->image = gain_map.release();
  EncoderPtr encoder(avifEncoderCreate());
  ASSERT_NE(encoder, nullptr);
  testutil::AvifRwData encoded;
  // Add a first frame.
  avifResult result =
      avifEncoderAddImage(encoder.get(), image.get(),
                          /*durationInTimescales=*/2, AVIF_ADD_IMAGE_FLAG_NONE);
  ASSERT_EQ(result, AVIF_RESULT_OK)
      << avifResultToString(result) << " " << encoder->diag.error;
  // Add a second frame.
  result =
      avifEncoderAddImage(encoder.get(), image.get(),
                          /*durationInTimescales=*/2, AVIF_ADD_IMAGE_FLAG_NONE);
  // Image sequences with gain maps are not supported.
  ASSERT_EQ(result, AVIF_RESULT_NOT_IMPLEMENTED)
      << avifResultToString(result) << " " << encoder->diag.error;
}

TEST(GainMapTest, IgnoreGainMapButReadMetadata) {
  ImagePtr image = CreateTestImageWithGainMap(/*base_rendition_is_hdr=*/false);
  ASSERT_NE(image, nullptr);

  EncoderPtr encoder(avifEncoderCreate());
  ASSERT_NE(encoder, nullptr);
  testutil::AvifRwData encoded;
  avifResult result = avifEncoderWrite(encoder.get(), image.get(), &encoded);
  ASSERT_EQ(result, AVIF_RESULT_OK)
      << avifResultToString(result) << " " << encoder->diag.error;

  // Decode image, with the gain map not decoded by default.
  ImagePtr decoded(avifImageCreateEmpty());
  ASSERT_NE(decoded, nullptr);
  DecoderPtr decoder(avifDecoderCreate());
  ASSERT_NE(decoder, nullptr);
  result = avifDecoderReadMemory(decoder.get(), decoded.get(), encoded.data,
                                 encoded.size);
  ASSERT_EQ(result, AVIF_RESULT_OK)
      << avifResultToString(result) << " " << decoder->diag.error;

  // Verify that the input and decoded images are close.
  EXPECT_GT(testutil::GetPsnr(*image, *decoded), 40.0);
  // Verify that the gain map was detected...
  ASSERT_NE(decoded->gainMap, nullptr);
  // ... but not decoded.
  EXPECT_EQ(decoded->gainMap->image, nullptr);
  // Check that the gain map metadata WAS populated.
  CheckGainMapMetadataMatches(*decoded->gainMap, *image->gainMap);
  EXPECT_EQ(decoded->gainMap->altDepth, image->gainMap->altDepth);
  EXPECT_EQ(decoded->gainMap->altPlaneCount, image->gainMap->altPlaneCount);
  EXPECT_EQ(decoded->gainMap->altColorPrimaries,
            image->gainMap->altColorPrimaries);
  EXPECT_EQ(decoded->gainMap->altTransferCharacteristics,
            image->gainMap->altTransferCharacteristics);
  EXPECT_EQ(decoded->gainMap->altMatrixCoefficients,
            image->gainMap->altMatrixCoefficients);
}

TEST(GainMapTest, IgnoreColorAndAlpha) {
  ImagePtr image = CreateTestImageWithGainMap(/*base_rendition_is_hdr=*/false);
  ASSERT_NE(image, nullptr);

  EncoderPtr encoder(avifEncoderCreate());
  ASSERT_NE(encoder, nullptr);
  testutil::AvifRwData encoded;
  avifResult result = avifEncoderWrite(encoder.get(), image.get(), &encoded);
  ASSERT_EQ(result, AVIF_RESULT_OK)
      << avifResultToString(result) << " " << encoder->diag.error;

  ImagePtr decoded(avifImageCreateEmpty());
  ASSERT_NE(decoded, nullptr);
  DecoderPtr decoder(avifDecoderCreate());
  ASSERT_NE(decoder, nullptr);
  // Decode just the gain map.
  decoder->imageContentToDecode = AVIF_IMAGE_CONTENT_GAIN_MAP;
  result = avifDecoderReadMemory(decoder.get(), decoded.get(), encoded.data,
                                 encoded.size);
  ASSERT_EQ(result, AVIF_RESULT_OK)
      << avifResultToString(result) << " " << decoder->diag.error;

  // Main image metadata is available.
  EXPECT_EQ(decoder->image->width, 12u);
  EXPECT_EQ(decoder->image->height, 34u);
  // But pixels are not.
  EXPECT_EQ(decoder->image->yuvRowBytes[0], 0u);
  EXPECT_EQ(decoder->image->yuvRowBytes[1], 0u);
  EXPECT_EQ(decoder->image->yuvRowBytes[2], 0u);
  EXPECT_EQ(decoder->image->alphaRowBytes, 0u);
  // The gain map was decoded.
  ASSERT_NE(decoded->gainMap, nullptr);
  ASSERT_NE(decoded->gainMap->image, nullptr);
  EXPECT_GT(testutil::GetPsnr(*image->gainMap->image, *decoded->gainMap->image),
            40.0);
  CheckGainMapMetadataMatches(*decoded->gainMap, *image->gainMap);
}

TEST(GainMapTest, IgnoreAll) {
  ImagePtr image = CreateTestImageWithGainMap(/*base_rendition_is_hdr=*/false);
  ASSERT_NE(image, nullptr);

  EncoderPtr encoder(avifEncoderCreate());
  ASSERT_NE(encoder, nullptr);
  testutil::AvifRwData encoded;
  avifResult result = avifEncoderWrite(encoder.get(), image.get(), &encoded);
  ASSERT_EQ(result, AVIF_RESULT_OK)
      << avifResultToString(result) << " " << encoder->diag.error;

  DecoderPtr decoder(avifDecoderCreate());
  ASSERT_NE(decoder, nullptr);
  // Ignore both the main image and the gain map.
  decoder->imageContentToDecode = AVIF_IMAGE_CONTENT_NONE;
  // But do read the gain map metadata

  // Parsing just the header should work.
  ASSERT_EQ(avifDecoderSetIOMemory(decoder.get(), encoded.data, encoded.size),
            AVIF_RESULT_OK);
  ASSERT_EQ(avifDecoderParse(decoder.get()), AVIF_RESULT_OK);

  EXPECT_TRUE(decoder->image->gainMap != nullptr);
  CheckGainMapMetadataMatches(*decoder->image->gainMap, *image->gainMap);
  ASSERT_EQ(decoder->image->gainMap->image, nullptr);

  // But trying to access the next image should give an error.
  ASSERT_EQ(avifDecoderNextImage(decoder.get()), AVIF_RESULT_NO_CONTENT);
}

TEST(GainMapTest, NoGainMap) {
  // Create a simple image without a gain map.
  ImagePtr image =
      testutil::CreateImage(/*width=*/12, /*height=*/34, /*depth=*/10,
                            AVIF_PIXEL_FORMAT_YUV420, AVIF_PLANES_ALL);
  ASSERT_NE(image, nullptr);
  image->transferCharacteristics = AVIF_TRANSFER_CHARACTERISTICS_SRGB;
  testutil::FillImageGradient(image.get());
  EncoderPtr encoder(avifEncoderCreate());
  ASSERT_NE(encoder, nullptr);
  testutil::AvifRwData encoded;
  avifResult result = avifEncoderWrite(encoder.get(), image.get(), &encoded);
  ASSERT_EQ(result, AVIF_RESULT_OK)
      << avifResultToString(result) << " " << encoder->diag.error;

  ImagePtr decoded(avifImageCreateEmpty());
  ASSERT_NE(decoded, nullptr);
  DecoderPtr decoder(avifDecoderCreate());
  ASSERT_NE(decoder, nullptr);
  // Enable gain map decoding.
  decoder->imageContentToDecode |= AVIF_IMAGE_CONTENT_GAIN_MAP;
  result = avifDecoderReadMemory(decoder.get(), decoded.get(), encoded.data,
                                 encoded.size);
  ASSERT_EQ(result, AVIF_RESULT_OK)
      << avifResultToString(result) << " " << decoder->diag.error;

  // Verify that the input and decoded images are close.
  EXPECT_GT(testutil::GetPsnr(*image, *decoded), 40.0);
  // Verify that no gain map was found.
  EXPECT_EQ(decoded->gainMap, nullptr);
}

TEST(GainMapTest, DecodeGainMapGrid) {
  const std::string path =
      std::string(data_path) + "color_grid_gainmap_different_grid.avif";
  DecoderPtr decoder(avifDecoderCreate());
  ASSERT_NE(decoder, nullptr);
  decoder->imageContentToDecode |= AVIF_IMAGE_CONTENT_GAIN_MAP;

  avifResult result = avifDecoderSetIOFile(decoder.get(), path.c_str());
  ASSERT_EQ(result, AVIF_RESULT_OK)
      << avifResultToString(result) << " " << decoder->diag.error;

  // Just parse the image first.
  result = avifDecoderParse(decoder.get());
  ASSERT_EQ(result, AVIF_RESULT_OK)
      << avifResultToString(result) << " " << decoder->diag.error;
  avifImage* decoded = decoder->image;
  ASSERT_NE(decoded, nullptr);

  // Verify that the gain map is present and matches the input.
  ASSERT_TRUE(decoded->gainMap != nullptr);
  // Color+alpha: 4x3 grid of 128x200 tiles.
  EXPECT_EQ(decoded->width, 128u * 4u);
  EXPECT_EQ(decoded->height, 200u * 3u);
  EXPECT_EQ(decoded->depth, 10u);
  ASSERT_NE(decoded->gainMap, nullptr);
  ASSERT_NE(decoded->gainMap->image, nullptr);
  // Gain map: 2x2 grid of 64x80 tiles.
  EXPECT_EQ(decoded->gainMap->image->width, 64u * 2u);
  EXPECT_EQ(decoded->gainMap->image->height, 80u * 2u);
  EXPECT_EQ(decoded->gainMap->image->depth, 8u);
  EXPECT_EQ(decoded->gainMap->baseHdrHeadroom.n, 6u);
  EXPECT_EQ(decoded->gainMap->baseHdrHeadroom.d, 2u);

  // Decode the image.
  result = avifDecoderNextImage(decoder.get());
  ASSERT_EQ(result, AVIF_RESULT_OK)
      << avifResultToString(result) << " " << decoder->diag.error;
}

TEST(GainMapTest, DecodeColorGridGainMapNoGrid) {
  const std::string path =
      std::string(data_path) + "color_grid_alpha_grid_gainmap_nogrid.avif";
  ImagePtr decoded(avifImageCreateEmpty());
  ASSERT_NE(decoded, nullptr);
  DecoderPtr decoder(avifDecoderCreate());
  ASSERT_NE(decoder, nullptr);
  decoder->imageContentToDecode |= AVIF_IMAGE_CONTENT_GAIN_MAP;
  ASSERT_EQ(avifDecoderReadFile(decoder.get(), decoded.get(), path.c_str()),
            AVIF_RESULT_OK);

  // Color+alpha: 4x3 grid of 128x200 tiles.
  EXPECT_EQ(decoded->width, 128u * 4u);
  EXPECT_EQ(decoded->height, 200u * 3u);
  ASSERT_NE(decoded->gainMap, nullptr);
  ASSERT_NE(decoded->gainMap->image, nullptr);
  // Gain map: single image of size 64x80.
  EXPECT_EQ(decoded->gainMap->image->width, 64u);
  EXPECT_EQ(decoded->gainMap->image->height, 80u);
  EXPECT_EQ(decoded->gainMap->baseHdrHeadroom.n, 6u);
  EXPECT_EQ(decoded->gainMap->baseHdrHeadroom.d, 2u);
}

TEST(GainMapTest, DecodeColorNoGridGainMapGrid) {
  const std::string path =
      std::string(data_path) + "color_nogrid_alpha_nogrid_gainmap_grid.avif";
  ImagePtr decoded(avifImageCreateEmpty());
  ASSERT_NE(decoded, nullptr);
  DecoderPtr decoder(avifDecoderCreate());
  ASSERT_NE(decoder, nullptr);
  decoder->imageContentToDecode |= AVIF_IMAGE_CONTENT_GAIN_MAP;
  ASSERT_EQ(avifDecoderReadFile(decoder.get(), decoded.get(), path.c_str()),
            AVIF_RESULT_OK);

  // Color+alpha: single image of size 128x200 .
  EXPECT_EQ(decoded->width, 128u);
  EXPECT_EQ(decoded->height, 200u);
  ASSERT_NE(decoded->gainMap, nullptr);
  ASSERT_NE(decoded->gainMap->image, nullptr);
  // Gain map: 2x2 grid of 64x80 tiles.
  EXPECT_EQ(decoded->gainMap->image->width, 64u * 2u);
  EXPECT_EQ(decoded->gainMap->image->height, 80u * 2u);
  EXPECT_EQ(decoded->gainMap->baseHdrHeadroom.n, 6u);
  EXPECT_EQ(decoded->gainMap->baseHdrHeadroom.d, 2u);
}

TEST(GainMapTest, DecodeUnsupportedVersion) {
  // The two test files should produce the same results:
  // One has an unsupported 'version' field, the other an unsupported
  // 'minimum_version' field, but the behavior of these two fields is the same.
  for (const std::string image : {"unsupported_gainmap_version.avif",
                                  "unsupported_gainmap_minimum_version.avif"}) {
    SCOPED_TRACE(image);
    const std::string path = std::string(data_path) + image;
    ImagePtr decoded(avifImageCreateEmpty());
    ASSERT_NE(decoded, nullptr);
    DecoderPtr decoder(avifDecoderCreate());
    ASSERT_NE(decoder, nullptr);

    // Decode with and without gain map decoding.

    ASSERT_EQ(avifDecoderReadFile(decoder.get(), decoded.get(), path.c_str()),
              AVIF_RESULT_OK);
    // Gain map marked as not present because the metadata is not supported.
    ASSERT_EQ(decoded->gainMap, nullptr);

    ASSERT_EQ(avifDecoderReset(decoder.get()), AVIF_RESULT_OK);
    decoder->imageContentToDecode |= AVIF_IMAGE_CONTENT_GAIN_MAP;
    ASSERT_EQ(avifDecoderReadFile(decoder.get(), decoded.get(), path.c_str()),
              AVIF_RESULT_OK);
    // Gain map marked as not present because the metadata is not supported.
    ASSERT_EQ(decoded->gainMap, nullptr);
  }
}

TEST(GainMapTest, ExtraBytesAfterGainMapMetadataUnsupportedWriterVersion) {
  const std::string path =
      std::string(data_path) +
      "unsupported_gainmap_writer_version_with_extra_bytes.avif";
  ImagePtr decoded(avifImageCreateEmpty());
  ASSERT_NE(decoded, nullptr);
  DecoderPtr decoder(avifDecoderCreate());
  ASSERT_NE(decoder, nullptr);

  ASSERT_EQ(avifDecoderReadFile(decoder.get(), decoded.get(), path.c_str()),
            AVIF_RESULT_OK);
  // Decodes successfully: there are extra bytes at the end of the gain map
  // metadata but that's expected as the writer_version field is higher
  // that supported.
  ASSERT_NE(decoded->gainMap, nullptr);
}

TEST(GainMapTest, ExtraBytesAfterGainMapMetadataSupporterWriterVersion) {
  const std::string path =
      std::string(data_path) +
      "supported_gainmap_writer_version_with_extra_bytes.avif";
  ImagePtr decoded(avifImageCreateEmpty());
  ASSERT_NE(decoded, nullptr);
  DecoderPtr decoder(avifDecoderCreate());
  ASSERT_NE(decoder, nullptr);

  // Fails to decode: there are extra bytes at the end of the gain map metadata
  // that shouldn't be there.
  ASSERT_EQ(avifDecoderReadFile(decoder.get(), decoded.get(), path.c_str()),
            AVIF_RESULT_INVALID_TONE_MAPPED_IMAGE);
}

TEST(GainMapTest, DecodeInvalidFtyp) {
  const std::string path =
      std::string(data_path) + "seine_sdr_gainmap_notmapbrand.avif";
  ImagePtr decoded(avifImageCreateEmpty());
  ASSERT_NE(decoded, nullptr);
  DecoderPtr decoder(avifDecoderCreate());
  ASSERT_NE(decoder, nullptr);
  decoder->imageContentToDecode |= AVIF_IMAGE_CONTENT_GAIN_MAP;

  ASSERT_EQ(avifDecoderReadFile(decoder.get(), decoded.get(), path.c_str()),
            AVIF_RESULT_OK);
  // The gain map is ignored because the 'tmap' brand is not present.
  ASSERT_EQ(decoded->gainMap, nullptr);
}

TEST(GainMapTest, DecodeInvalidGamma) {
  const std::string path =
      std::string(data_path) + "seine_sdr_gainmap_gammazero.avif";
  ImagePtr decoded(avifImageCreateEmpty());
  ASSERT_NE(decoded, nullptr);
  DecoderPtr decoder(avifDecoderCreate());
  ASSERT_NE(decoder, nullptr);
  decoder->imageContentToDecode |= AVIF_IMAGE_CONTENT_GAIN_MAP;
  // Fails to decode: invalid because the gain map gamma is zero.
  ASSERT_EQ(avifDecoderReadFile(decoder.get(), decoded.get(), path.c_str()),
            AVIF_RESULT_INVALID_TONE_MAPPED_IMAGE);
}

#define EXPECT_FRACTION_NEAR(numerator, denominator, expected)     \
  EXPECT_NEAR(std::abs((double)numerator / denominator), expected, \
              expected * 0.001);

static void SwapBaseAndAlternate(const avifImage& new_alternate,
                                 avifGainMap& gain_map) {
  gain_map.useBaseColorSpace = !gain_map.useBaseColorSpace;
  std::swap(gain_map.baseHdrHeadroom.n, gain_map.alternateHdrHeadroom.n);
  std::swap(gain_map.baseHdrHeadroom.d, gain_map.alternateHdrHeadroom.d);
  for (int c = 0; c < 3; ++c) {
    std::swap(gain_map.baseOffset[c].n, gain_map.alternateOffset[c].n);
    std::swap(gain_map.baseOffset[c].d, gain_map.alternateOffset[c].d);
  }
  gain_map.altColorPrimaries = new_alternate.colorPrimaries;
  gain_map.altTransferCharacteristics = new_alternate.transferCharacteristics;
  gain_map.altMatrixCoefficients = new_alternate.matrixCoefficients;
  gain_map.altYUVRange = new_alternate.yuvRange;
  gain_map.altDepth = new_alternate.depth;
  gain_map.altPlaneCount =
      (new_alternate.yuvFormat == AVIF_PIXEL_FORMAT_YUV400) ? 1 : 3;
  gain_map.altCLLI = new_alternate.clli;
}

// Test to generate some test images used by other tests and fuzzers.
// Allows regenerating the images if the gain map format changes.
TEST(GainMapTest, CreateTestImages) {
  // Set to true to update test images.
  constexpr bool kUpdateTestImages = false;

  // Generate seine_sdr_gainmap_big_srgb.jpg
  {
    const std::string path =
        std::string(data_path) + "seine_sdr_gainmap_srgb.avif";
    DecoderPtr decoder(avifDecoderCreate());
    ASSERT_NE(decoder, nullptr);
    decoder->imageContentToDecode |= AVIF_IMAGE_CONTENT_GAIN_MAP;

    ImagePtr image(avifImageCreateEmpty());
    ASSERT_NE(image, nullptr);
    avifResult result =
        avifDecoderReadFile(decoder.get(), image.get(), path.c_str());
    ASSERT_EQ(result, AVIF_RESULT_OK)
        << avifResultToString(result) << " " << decoder->diag.error;
    ASSERT_NE(image->gainMap, nullptr);
    ASSERT_NE(image->gainMap->image, nullptr);

    avifDiagnostics diag;
    result =
        avifImageScale(image->gainMap->image, image->gainMap->image->width * 2,
                       image->gainMap->image->height * 2, &diag);
    ASSERT_EQ(result, AVIF_RESULT_OK)
        << avifResultToString(result) << " " << decoder->diag.error;

    const testutil::AvifRwData encoded =
        testutil::Encode(image.get(), /*speed=*/9, /*quality=*/90);
    ASSERT_GT(encoded.size, 0u);
    if (kUpdateTestImages) {
      std::ofstream(std::string(data_path) + "seine_sdr_gainmap_big_srgb.avif",
                    std::ios::binary)
          .write(reinterpret_cast<char*>(encoded.data), encoded.size);
    }
  }

  // Generate seine_hdr_gainmap_srgb.avif and seine_hdr_gainmap_small_srgb.avif
  {
    ImagePtr hdr_image =
        testutil::DecodeFile(std::string(data_path) + "seine_hdr_srgb.avif");
    ASSERT_NE(hdr_image, nullptr);

    const std::string sdr_path =
        std::string(data_path) + "seine_sdr_gainmap_srgb.avif";
    DecoderPtr decoder(avifDecoderCreate());
    ASSERT_NE(decoder, nullptr);
    decoder->imageContentToDecode |= AVIF_IMAGE_CONTENT_GAIN_MAP;
    ImagePtr sdr_with_gainmap(avifImageCreateEmpty());
    ASSERT_NE(sdr_with_gainmap, nullptr);
    avifResult result = avifDecoderReadFile(
        decoder.get(), sdr_with_gainmap.get(), sdr_path.c_str());
    ASSERT_EQ(result, AVIF_RESULT_OK)
        << avifResultToString(result) << " " << decoder->diag.error;
    ASSERT_NE(sdr_with_gainmap->gainMap->image, nullptr);

    // Move the gain map from the sdr image to the hdr image.
    hdr_image->gainMap = sdr_with_gainmap->gainMap;
    sdr_with_gainmap->gainMap = nullptr;
    SwapBaseAndAlternate(*sdr_with_gainmap, *hdr_image->gainMap);
    hdr_image->gainMap->altColorPrimaries = sdr_with_gainmap->colorPrimaries;
    hdr_image->gainMap->altTransferCharacteristics =
        sdr_with_gainmap->transferCharacteristics;
    hdr_image->gainMap->altMatrixCoefficients =
        sdr_with_gainmap->matrixCoefficients;
    hdr_image->gainMap->altDepth = sdr_with_gainmap->depth;
    hdr_image->gainMap->altPlaneCount = 3;

    const testutil::AvifRwData encoded =
        testutil::Encode(hdr_image.get(), /*speed=*/9, /*quality=*/90);
    ASSERT_GT(encoded.size, 0u);
    if (kUpdateTestImages) {
      std::ofstream(std::string(data_path) + "seine_hdr_gainmap_srgb.avif",
                    std::ios::binary)
          .write(reinterpret_cast<char*>(encoded.data), encoded.size);
    }

    avifDiagnostics diag;
    result = avifImageScale(hdr_image->gainMap->image,
                            hdr_image->gainMap->image->width / 2,
                            hdr_image->gainMap->image->height / 2, &diag);
    ASSERT_EQ(result, AVIF_RESULT_OK)
        << avifResultToString(result) << " " << decoder->diag.error;

    const testutil::AvifRwData encoded_small_gainmap =
        testutil::Encode(hdr_image.get(), /*speed=*/9, /*quality=*/90);
    ASSERT_GT(encoded.size, 0u);
    if (kUpdateTestImages) {
      std::ofstream(
          std::string(data_path) + "seine_hdr_gainmap_small_srgb.avif",
          std::ios::binary)
          .write(reinterpret_cast<char*>(encoded_small_gainmap.data),
                 encoded_small_gainmap.size);
    }
  }
}

class ToneMapTest
    : public testing::TestWithParam<std::tuple<
          /*source=*/std::string, /*hdr_headroom=*/float,
          /*out_depth=*/int,
          /*out_transfer=*/avifTransferCharacteristics,
          /*out_rgb_format=*/avifRGBFormat,
          /*reference=*/std::string, /*min_psnr=*/float, /*max_psnr=*/float>> {
};

void ToneMapImageAndCompareToReference(
    const avifImage* base_image, const avifGainMap& gain_map,
    float hdr_headroom, int out_depth,
    avifTransferCharacteristics out_transfer_characteristics,
    avifRGBFormat out_rgb_format, const avifImage* reference_image,
    float min_psnr, float max_psnr, float* psnr_out = nullptr) {
  SCOPED_TRACE("hdr_headroom: " + std::to_string(hdr_headroom));

  testutil::AvifRgbImage tone_mapped_rgb(base_image, out_depth, out_rgb_format);
  ImagePtr tone_mapped(
      avifImageCreate(tone_mapped_rgb.width, tone_mapped_rgb.height,
                      tone_mapped_rgb.depth, AVIF_PIXEL_FORMAT_YUV444));
  tone_mapped->transferCharacteristics = out_transfer_characteristics;
  tone_mapped->colorPrimaries = reference_image
                                    ? reference_image->colorPrimaries
                                    : base_image->colorPrimaries;
  tone_mapped->matrixCoefficients = reference_image
                                        ? reference_image->matrixCoefficients
                                        : base_image->matrixCoefficients;

  avifDiagnostics diag;
  avifResult result = avifImageApplyGainMap(
      base_image, &gain_map, hdr_headroom, tone_mapped->colorPrimaries,
      tone_mapped->transferCharacteristics, &tone_mapped_rgb,
      &tone_mapped->clli, &diag);
  ASSERT_EQ(result, AVIF_RESULT_OK)
      << avifResultToString(result) << " " << diag.error;
  ASSERT_EQ(avifImageRGBToYUV(tone_mapped.get(), &tone_mapped_rgb),
            AVIF_RESULT_OK);
  if (reference_image != nullptr) {
    EXPECT_EQ(out_depth, (int)reference_image->depth);
    const double psnr = testutil::GetPsnr(*reference_image, *tone_mapped);
    std::cout << "PSNR (tone mapped vs reference): " << psnr << "\n";
    EXPECT_GE(psnr, min_psnr);
    EXPECT_LE(psnr, max_psnr);
    if (psnr_out != nullptr) {
      *psnr_out = (float)psnr;
    }
  }

  // Uncomment the following to save the encoded image as an AVIF file.
  // const testutil::AvifRwData encoded =
  //     testutil::Encode(tone_mapped.get(), /*speed=*/9, /*quality=*/90);
  // ASSERT_GT(encoded.size, 0u);
  // std::ofstream("/tmp/tone_mapped_" + std::to_string(hdr_headroom) +
  // ".avif", std::ios::binary)
  //     .write(reinterpret_cast<char*>(encoded.data), encoded.size);
}

TEST_P(ToneMapTest, ToneMapImage) {
  const std::string source = std::get<0>(GetParam());
  const float hdr_headroom = std::get<1>(GetParam());
  // out_depth and out_transfer_characteristics should match the reference image
  // when ther eis one, so that GetPsnr works.
  const int out_depth = std::get<2>(GetParam());
  const avifTransferCharacteristics out_transfer_characteristics =
      std::get<3>(GetParam());
  const avifRGBFormat out_rgb_format = std::get<4>(GetParam());
  const std::string reference = std::get<5>(GetParam());
  const float min_psnr = std::get<6>(GetParam());
  const float max_psnr = std::get<7>(GetParam());

  ImagePtr reference_image = nullptr;
  if (!source.empty()) {
    reference_image = testutil::DecodeFile(std::string(data_path) + reference);
  }

  // Load the source image (that should contain a gain map).
  const std::string path = std::string(data_path) + source;
  ImagePtr image(avifImageCreateEmpty());
  ASSERT_NE(image, nullptr);
  DecoderPtr decoder(avifDecoderCreate());
  ASSERT_NE(decoder, nullptr);
  decoder->imageContentToDecode |= AVIF_IMAGE_CONTENT_GAIN_MAP;
  avifResult result =
      avifDecoderReadFile(decoder.get(), image.get(), path.c_str());
  ASSERT_EQ(result, AVIF_RESULT_OK)
      << avifResultToString(result) << " " << decoder->diag.error;
  ASSERT_NE(image->gainMap, nullptr);
  ASSERT_NE(image->gainMap->image, nullptr);

  ToneMapImageAndCompareToReference(image.get(), *image->gainMap, hdr_headroom,
                                    out_depth, out_transfer_characteristics,
                                    out_rgb_format, reference_image.get(),
                                    min_psnr, max_psnr);
}

INSTANTIATE_TEST_SUITE_P(
    All, ToneMapTest,
    Values(
        // ------ SDR BASE IMAGE ------

        // hdr_headroom=0, the image should stay SDR (base image untouched).
        // A small loss is expected due to YUV/RGB conversion.
        std::make_tuple(
            /*source=*/"seine_sdr_gainmap_srgb.avif", /*hdr_headroom=*/0.0f,
            /*out_depth=*/8,
            /*out_transfer=*/AVIF_TRANSFER_CHARACTERISTICS_SRGB,
            /*out_rgb_format=*/AVIF_RGB_FORMAT_RGB,
            /*reference=*/"seine_sdr_gainmap_srgb.avif", /*min_psnr=*/60.0f,
            /*max_psnr=*/80.0f),

        // Same as above, outputting to RGBA.
        std::make_tuple(
            /*source=*/"seine_sdr_gainmap_srgb.avif", /*hdr_headroom=*/0.0f,
            /*out_depth=*/8,
            /*out_transfer=*/AVIF_TRANSFER_CHARACTERISTICS_SRGB,
            /*out_rgb_format=*/AVIF_RGB_FORMAT_RGBA,
            /*reference=*/"seine_sdr_gainmap_srgb.avif", /*min_psnr=*/60.0f,
            /*max_psnr=*/80.0f),

        // Same as above, outputting to a different transfer characteristic.
        // As a result we expect a low PSNR (since the PSNR function is not
        // aware of the transfer curve difference).
        std::make_tuple(
            /*source=*/"seine_sdr_gainmap_srgb.avif", /*hdr_headroom=*/0.0f,
            /*out_depth=*/8,
            /*out_transfer=*/AVIF_TRANSFER_CHARACTERISTICS_LOG100,
            /*out_rgb_format=*/AVIF_RGB_FORMAT_RGBA,
            /*reference=*/"seine_sdr_gainmap_srgb.avif", /*min_psnr=*/20.0f,
            /*max_psnr=*/30.0f),

        // hdr_headroom=3, the gain map should be fully applied.
        std::make_tuple(
            /*source=*/"seine_sdr_gainmap_srgb.avif", /*hdr_headroom=*/3.0f,
            /*out_depth=*/10,
            /*out_transfer=*/AVIF_TRANSFER_CHARACTERISTICS_PQ,
            /*out_rgb_format=*/AVIF_RGB_FORMAT_RGB,
            /*reference=*/"seine_hdr_srgb.avif", /*min_psnr=*/40.0f,
            /*max_psnr=*/60.0f),

        // hdr_headroom=3, the gain map should be fully applied.
        // Version with a gain map that is larger than the base image (needs
        // rescaling).
        std::make_tuple(
            /*source=*/"seine_sdr_gainmap_big_srgb.avif", /*hdr_headroom=*/3.0f,
            /*out_depth=*/10,
            /*out_transfer=*/AVIF_TRANSFER_CHARACTERISTICS_PQ,
            /*out_rgb_format=*/AVIF_RGB_FORMAT_RGB,
            /*reference=*/"seine_hdr_srgb.avif", /*min_psnr=*/40.0f,
            /*max_psnr=*/60.0f),

        // hdr_headroom=0.5 No reference image.
        std::make_tuple(
            /*source=*/"seine_sdr_gainmap_srgb.avif", /*hdr_headroom=*/1.5f,
            /*out_depth=*/10,
            /*out_transfer=*/AVIF_TRANSFER_CHARACTERISTICS_PQ,
            /*out_rgb_format=*/AVIF_RGB_FORMAT_RGB,
            /*reference=*/"", /*min_psnr=*/0.0f,
            /*max_psnr=*/0.0f),

        // ------ HDR BASE IMAGE ------

        // hdr_headroom=0, the gain map should be fully applied.
        std::make_tuple(
            /*source=*/"seine_hdr_gainmap_srgb.avif", /*hdr_headroom=*/0.0f,
            /*out_depth=*/8,
            /*out_transfer=*/AVIF_TRANSFER_CHARACTERISTICS_SRGB,
            /*out_rgb_format=*/AVIF_RGB_FORMAT_RGB,
            /*reference=*/"seine_sdr_gainmap_srgb.avif", /*min_psnr=*/38.0f,
            /*max_psnr=*/60.0f),

        // hdr_headroom=0, the gain map should be fully applied.
        // Version with a gain map that is smaller than the base image (needs
        // rescaling). The PSNR is a bit lower than above due to quality loss on
        // the gain map.
        std::make_tuple(
            /*source=*/"seine_hdr_gainmap_small_srgb.avif",
            /*hdr_headroom=*/0.0f,
            /*out_depth=*/8,
            /*out_transfer=*/AVIF_TRANSFER_CHARACTERISTICS_SRGB,
            AVIF_RGB_FORMAT_RGB,
            /*reference=*/"seine_sdr_gainmap_srgb.avif", /*min_psnr=*/36.0f,
            /*max_psnr=*/60.0f),

        // hdr_headroom=3, the image should stay HDR (base image untouched).
        // A small loss is expected due to YUV/RGB conversion.
        std::make_tuple(
            /*source=*/"seine_hdr_gainmap_srgb.avif", /*hdr_headroom=*/3.0f,
            /*out_depth=*/10,
            /*out_transfer=*/AVIF_TRANSFER_CHARACTERISTICS_PQ,
            /*out_rgb_format=*/AVIF_RGB_FORMAT_RGB,
            /*reference=*/"seine_hdr_gainmap_srgb.avif", /*min_psnr=*/60.0f,
            /*max_psnr=*/80.0f),

        // hdr_headroom=0.5 No reference image.
        std::make_tuple(
            /*source=*/"seine_hdr_gainmap_srgb.avif", /*hdr_headroom=*/1.5f,
            /*out_depth=*/10,
            /*out_transfer=*/AVIF_TRANSFER_CHARACTERISTICS_PQ,
            /*out_rgb_format=*/AVIF_RGB_FORMAT_RGB,
            /*reference=*/"", /*min_psnr=*/0.0f, /*max_psnr=*/0.0f)));

TEST(ToneMapTest, ToneMapImageSameHeadroom) {
  const std::string path =
      std::string(data_path) + "seine_sdr_gainmap_srgb.avif";
  ImagePtr image(avifImageCreateEmpty());
  ASSERT_NE(image, nullptr);
  DecoderPtr decoder(avifDecoderCreate());
  ASSERT_NE(decoder, nullptr);
  decoder->imageContentToDecode |= AVIF_IMAGE_CONTENT_GAIN_MAP;
  avifResult result =
      avifDecoderReadFile(decoder.get(), image.get(), path.c_str());
  ASSERT_EQ(result, AVIF_RESULT_OK)
      << avifResultToString(result) << " " << decoder->diag.error;

  ASSERT_NE(image->gainMap, nullptr);
  ASSERT_NE(image->gainMap->image, nullptr);

  // Force the alternate and base HDR headroom to the same value.
  image->gainMap->baseHdrHeadroom = image->gainMap->alternateHdrHeadroom;
  const float headroom =
      static_cast<float>(static_cast<float>(image->gainMap->baseHdrHeadroom.n) /
                         image->gainMap->baseHdrHeadroom.d);

  // Check that when the two headrooms are the same, the gain map is not applied
  // whatever the target headroom is.
  for (const float tonemap_to : {headroom, headroom - 0.5f, headroom + 0.5f}) {
    ToneMapImageAndCompareToReference(
        image.get(), *image->gainMap, /*hdr_headroom=*/tonemap_to,
        /*out_depth=*/image->depth,
        /*out_transfer_characteristics=*/image->transferCharacteristics,
        AVIF_RGB_FORMAT_RGB, /*reference_image=*/image.get(),
        /*min_psnr=*/60, /*max_psnr=*/100);
  }
}

class CreateGainMapTest
    : public testing::TestWithParam<std::tuple<
          /*image1_name=*/std::string, /*image2_name=*/std::string,
          /*downscaling=*/int, /*gain_map_depth=*/int,
          /*gain_map_format=*/avifPixelFormat,
          /*min_psnr=*/float, /*max_psnr=*/float>> {};

// Creates a gain map to go from image1 to image2, and tone maps to check we get
// the correct result. Then does the same thing going from image2 to image1.
TEST_P(CreateGainMapTest, Create) {
  const std::string image1_name = std::get<0>(GetParam());
  const std::string image2_name = std::get<1>(GetParam());
  const int downscaling = std::get<2>(GetParam());
  const int gain_map_depth = std::get<3>(GetParam());
  const avifPixelFormat gain_map_format = std::get<4>(GetParam());
  const float min_psnr = std::get<5>(GetParam());
  const float max_psnr = std::get<6>(GetParam());

  ImagePtr image1 = testutil::DecodeFile(std::string(data_path) + image1_name);
  ASSERT_NE(image1, nullptr);
  ImagePtr image2 = testutil::DecodeFile(std::string(data_path) + image2_name);
  ASSERT_NE(image2, nullptr);

  const uint32_t gain_map_width = std::max<uint32_t>(
      (uint32_t)std::round((float)image1->width / downscaling), 1u);
  const uint32_t gain_map_height = std::max<uint32_t>(
      (uint32_t)std::round((float)image1->height / downscaling), 1u);
  std::unique_ptr<avifGainMap, decltype(&avifGainMapDestroy)> gain_map(
      avifGainMapCreate(), avifGainMapDestroy);
  gain_map->image = avifImageCreate(gain_map_width, gain_map_height,
                                    gain_map_depth, gain_map_format);

  avifDiagnostics diag;
  gain_map->useBaseColorSpace = true;
  avifResult result = avifImageComputeGainMap(image1.get(), image2.get(),
                                              gain_map.get(), &diag);
  ASSERT_EQ(result, AVIF_RESULT_OK)
      << avifResultToString(result) << " " << diag.error;

  EXPECT_EQ(gain_map->image->width, gain_map_width);
  EXPECT_EQ(gain_map->image->height, gain_map_height);

  const float image1_headroom =
      (float)gain_map->baseHdrHeadroom.n / gain_map->baseHdrHeadroom.d;
  const float image2_headroom = (float)gain_map->alternateHdrHeadroom.n /
                                gain_map->alternateHdrHeadroom.d;

  // Tone map from image1 to image2 by applying the gainmap forward.
  float psnr_image1_to_image2_forward;
  ToneMapImageAndCompareToReference(
      image1.get(), *gain_map, image2_headroom, image2->depth,
      image2->transferCharacteristics, AVIF_RGB_FORMAT_RGB, image2.get(),
      min_psnr, max_psnr, &psnr_image1_to_image2_forward);

  // Tone map from image2 to image1 by applying the gainmap backward.
  SwapBaseAndAlternate(*image1, *gain_map);
  float psnr_image2_to_image1_backward;
  ToneMapImageAndCompareToReference(
      image2.get(), *gain_map, image1_headroom, image1->depth,
      image1->transferCharacteristics, AVIF_RGB_FORMAT_RGB, image1.get(),
      min_psnr, max_psnr, &psnr_image2_to_image1_backward);

  // Uncomment the following to save the gain map as a PNG file.
  // ASSERT_TRUE(testutil::WriteImage(gain_map->image,
  //                                  "/tmp/gain_map_image1_to_image2.png"));

  // Compute the gain map in the other direction (from image2 to image1).
  gain_map->useBaseColorSpace = false;
  result = avifImageComputeGainMap(image2.get(), image1.get(), gain_map.get(),
                                   &diag);
  ASSERT_EQ(result, AVIF_RESULT_OK)
      << avifResultToString(result) << " " << diag.error;

  const float image2_headroom2 =
      (float)gain_map->baseHdrHeadroom.n / gain_map->baseHdrHeadroom.d;
  EXPECT_NEAR(image2_headroom2, image2_headroom, 0.001);

  // Tone map from image2 to image1 by applying the new gainmap forward.
  float psnr_image2_to_image1_forward;
  ToneMapImageAndCompareToReference(
      image2.get(), *gain_map, image1_headroom, image1->depth,
      image1->transferCharacteristics, AVIF_RGB_FORMAT_RGB, image1.get(),
      min_psnr, max_psnr, &psnr_image2_to_image1_forward);

  // Tone map from image1 to image2 by applying the new gainmap backward.
  SwapBaseAndAlternate(*image2, *gain_map);
  float psnr_image1_to_image2_backward;
  ToneMapImageAndCompareToReference(
      image1.get(), *gain_map, image2_headroom, image2->depth,
      image2->transferCharacteristics, AVIF_RGB_FORMAT_RGB, image2.get(),
      min_psnr, max_psnr, &psnr_image1_to_image2_backward);

  // Uncomment the following to save the gain map as a PNG file.
  // ASSERT_TRUE(testutil::WriteImage(gain_map->image,
  //                                  "/tmp/gain_map_image2_to_image1.png"));

  // Results should be about the same whether the gain map was computed from sdr
  // to hdr or the other way around.
  EXPECT_NEAR(psnr_image1_to_image2_backward, psnr_image1_to_image2_forward,
              min_psnr * 0.1f);
  EXPECT_NEAR(psnr_image2_to_image1_forward, psnr_image2_to_image1_backward,
              min_psnr * 0.1f);
}

INSTANTIATE_TEST_SUITE_P(
    All, CreateGainMapTest,
    Values(
        // Full scale gain map, 3 channels, 10 bit gain map.
        std::make_tuple(/*image1_name=*/"seine_sdr_gainmap_srgb.avif",
                        /*image2_name=*/"seine_hdr_gainmap_srgb.avif",
                        /*downscaling=*/1, /*gain_map_depth=*/10,
                        /*gain_map_format=*/AVIF_PIXEL_FORMAT_YUV444,
                        /*min_psnr=*/55.0f, /*max_psnr=*/80.0f),
        // 8 bit gain map, expect a slightly lower PSNR.
        std::make_tuple(/*image1_name=*/"seine_sdr_gainmap_srgb.avif",
                        /*image2_name=*/"seine_hdr_gainmap_srgb.avif",
                        /*downscaling=*/1, /*gain_map_depth=*/8,
                        /*gain_map_format=*/AVIF_PIXEL_FORMAT_YUV444,
                        /*min_psnr=*/50.0f, /*max_psnr=*/70.0f),
        // 420 gain map, expect a lower PSNR.
        std::make_tuple(/*image1_name=*/"seine_sdr_gainmap_srgb.avif",
                        /*image2_name=*/"seine_hdr_gainmap_srgb.avif",
                        /*downscaling=*/1, /*gain_map_depth=*/8,
                        /*gain_map_format=*/AVIF_PIXEL_FORMAT_YUV420,
                        /*min_psnr=*/40.0f, /*max_psnr=*/60.0f),
        // Downscaled gain map, expect a lower PSNR.
        std::make_tuple(/*image1_name=*/"seine_sdr_gainmap_srgb.avif",
                        /*image2_name=*/"seine_hdr_gainmap_srgb.avif",
                        /*downscaling=*/2, /*gain_map_depth=*/8,
                        /*gain_map_format=*/AVIF_PIXEL_FORMAT_YUV444,
                        /*min_psnr=*/35.0f, /*max_psnr=*/45.0f),
        // Even more downscaled gain map, expect a lower PSNR.
        std::make_tuple(/*image1_name=*/"seine_sdr_gainmap_srgb.avif",
                        /*image2_name=*/"seine_hdr_gainmap_srgb.avif",
                        /*downscaling=*/3, /*gain_map_depth=*/8,
                        /*gain_map_format=*/AVIF_PIXEL_FORMAT_YUV444,
                        /*min_psnr=*/35.0f, /*max_psnr=*/45.0f),
        // Extreme downscaling, just for fun.
        std::make_tuple(/*image1_name=*/"seine_sdr_gainmap_srgb.avif",
                        /*image2_name=*/"seine_hdr_gainmap_srgb.avif",
                        /*downscaling=*/255, /*gain_map_depth=*/8,
                        /*gain_map_format=*/AVIF_PIXEL_FORMAT_YUV444,
                        /*min_psnr=*/20.0f, /*max_psnr=*/35.0f),
        // Grayscale gain map.
        std::make_tuple(/*image1_name=*/"seine_sdr_gainmap_srgb.avif",
                        /*image2_name=*/"seine_hdr_gainmap_srgb.avif",
                        /*downscaling=*/1, /*gain_map_depth=*/8,
                        /*gain_map_format=*/AVIF_PIXEL_FORMAT_YUV400,
                        /*min_psnr=*/40.0f, /*max_psnr=*/60.0f),
        // Downscaled AND grayscale.
        std::make_tuple(/*image1_name=*/"seine_sdr_gainmap_srgb.avif",
                        /*image2_name=*/"seine_hdr_gainmap_srgb.avif",
                        /*downscaling=*/2, /*gain_map_depth=*/8,
                        /*gain_map_format=*/AVIF_PIXEL_FORMAT_YUV400,
                        /*min_psnr=*/35.0f, /*max_psnr=*/45.0f),

        // Color space conversions.
        std::make_tuple(/*image1_name=*/"colors_sdr_srgb.avif",
                        /*image2_name=*/"colors_hdr_rec2020.avif",
                        /*downscaling=*/1, /*gain_map_depth=*/10,
                        /*gain_map_format=*/AVIF_PIXEL_FORMAT_YUV444,
                        /*min_psnr=*/55.0f, /*max_psnr=*/100.0f),
        // The PSNR is very high because there are essentially the same image,
        // simply expresed in different colorspaces.
        std::make_tuple(/*image1_name=*/"colors_hdr_rec2020.avif",
                        /*image2_name=*/"colors_hdr_p3.avif",
                        /*downscaling=*/1, /*gain_map_depth=*/8,
                        /*gain_map_format=*/AVIF_PIXEL_FORMAT_YUV444,
                        /*min_psnr=*/90.0f, /*max_psnr=*/100.0f),
        // Color space conversions with wider color gamut.
        std::make_tuple(/*image1_name=*/"colors_sdr_srgb.avif",
                        /*image2_name=*/"colors_wcg_hdr_rec2020.avif",
                        /*downscaling=*/1, /*gain_map_depth=*/10,
                        /*gain_map_format=*/AVIF_PIXEL_FORMAT_YUV444,
                        /*min_psnr=*/55.0f, /*max_psnr=*/80.0f)));

TEST(FindMinMaxWithoutOutliers, AllSame) {
  constexpr int kNumValues = 10000;

  for (float v : {0.0f, 42.f, -12.f, 1.52f}) {
    std::vector<float> values(kNumValues, v);

    float min, max;
    ASSERT_EQ(
        avifFindMinMaxWithoutOutliers(values.data(), kNumValues, &min, &max),
        AVIF_RESULT_OK);
    EXPECT_EQ(min, v);
    EXPECT_EQ(max, v);
  }
}

TEST(FindMinMaxWithoutOutliers, Test) {
  constexpr int kNumValues = 10000;

  for (const float value_shift : {0.0f, -20.0f, 20.0f}) {
    SCOPED_TRACE("value_shift: " + std::to_string(value_shift));
    std::vector<float> values(kNumValues, value_shift + 2.0f);
    int k = 0;
    for (int i = 0; i < 5; ++i, ++k) {
      values[k] = value_shift + 1.99f;
    }
    for (int i = 0; i < 5; ++i, ++k) {
      values[k] = value_shift + 1.98f;
    }
    for (int i = 0; i < 1; ++i, ++k) {
      values[k] = value_shift + 1.97f;
    }
    for (int i = 0; i < 2; ++i, ++k) {
      values[k] = value_shift + 1.93f;  // Outliers.
    }
    for (int i = 0; i < 3; ++i, ++k) {
      values[k] = value_shift + 10.2f;  // Outliers.
    }

    float min, max;
    ASSERT_EQ(
        avifFindMinMaxWithoutOutliers(values.data(), kNumValues, &min, &max),
        AVIF_RESULT_OK);
    const float kEpsilon = 0.001f;
    EXPECT_NEAR(min, value_shift + 1.97f, kEpsilon);
    const float bucketSize = 0.01f;  // Size of one bucket.
    EXPECT_NEAR(max, value_shift + 2.0f + bucketSize, kEpsilon);
  }
}

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
