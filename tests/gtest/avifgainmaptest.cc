// Copyright 2023 Google LLC
// SPDX-License-Identifier: BSD-2-Clause

#include <cmath>
#include <fstream>

#include "avif/avif.h"
#include "avif/internal.h"
#include "avifincrtest_helpers.h"
#include "aviftest_helpers.h"
#include "gtest/gtest.h"

namespace libavif {
namespace {

using ::testing::Values;

// Used to pass the data folder path to the GoogleTest suites.
const char* data_path = nullptr;

void CheckGainMapMetadataMatches(const avifGainMapMetadata& lhs,
                                 const avifGainMapMetadata& rhs) {
  EXPECT_EQ(lhs.baseRenditionIsHDR, rhs.baseRenditionIsHDR);
  EXPECT_EQ(lhs.hdrCapacityMinN, rhs.hdrCapacityMinN);
  EXPECT_EQ(lhs.hdrCapacityMinD, rhs.hdrCapacityMinD);
  EXPECT_EQ(lhs.hdrCapacityMaxN, rhs.hdrCapacityMaxN);
  EXPECT_EQ(lhs.hdrCapacityMaxD, rhs.hdrCapacityMaxD);
  for (int c = 0; c < 3; ++c) {
    SCOPED_TRACE(c);
    EXPECT_EQ(lhs.offsetSdrN[c], rhs.offsetSdrN[c]);
    EXPECT_EQ(lhs.offsetSdrD[c], rhs.offsetSdrD[c]);
    EXPECT_EQ(lhs.offsetHdrN[c], rhs.offsetHdrN[c]);
    EXPECT_EQ(lhs.offsetHdrD[c], rhs.offsetHdrD[c]);
    EXPECT_EQ(lhs.gainMapGammaN[c], rhs.gainMapGammaN[c]);
    EXPECT_EQ(lhs.gainMapGammaD[c], rhs.gainMapGammaD[c]);
    EXPECT_EQ(lhs.gainMapMinN[c], rhs.gainMapMinN[c]);
    EXPECT_EQ(lhs.gainMapMinD[c], rhs.gainMapMinD[c]);
    EXPECT_EQ(lhs.gainMapMaxN[c], rhs.gainMapMaxN[c]);
    EXPECT_EQ(lhs.gainMapMaxD[c], rhs.gainMapMaxD[c]);
  }
}

avifGainMapMetadata GetTestGainMapMetadata(bool base_rendition_is_hdr) {
  avifGainMapMetadata metadata;
  metadata.baseRenditionIsHDR = base_rendition_is_hdr;
  metadata.hdrCapacityMinN = 1;
  metadata.hdrCapacityMinD = 1;
  metadata.hdrCapacityMaxN = 16;
  metadata.hdrCapacityMaxD = 2;
  for (int c = 0; c < 3; ++c) {
    metadata.offsetSdrN[c] = 10 * c;
    metadata.offsetSdrD[c] = 1000;
    metadata.offsetHdrN[c] = 20 * c;
    metadata.offsetHdrD[c] = 1000;
    metadata.gainMapGammaN[c] = 1;
    metadata.gainMapGammaD[c] = c + 1;
    metadata.gainMapMinN[c] = 1;
    metadata.gainMapMinD[c] = c + 1;
    metadata.gainMapMaxN[c] = 10 + c + 1;
    metadata.gainMapMaxD[c] = c + 1;
  }
  return metadata;
}

testutil::AvifImagePtr CreateTestImageWithGainMap(bool base_rendition_is_hdr) {
  testutil::AvifImagePtr image =
      testutil::CreateImage(/*width=*/12, /*height=*/34, /*depth=*/10,
                            AVIF_PIXEL_FORMAT_YUV420, AVIF_PLANES_ALL);
  if (image == nullptr) {
    return {nullptr, nullptr};
  }
  image->transferCharacteristics =
      (avifTransferCharacteristics)(base_rendition_is_hdr
                                        ? AVIF_TRANSFER_CHARACTERISTICS_SMPTE2084
                                        : AVIF_TRANSFER_CHARACTERISTICS_SRGB);
  testutil::FillImageGradient(image.get());
  testutil::AvifImagePtr gain_map =
      testutil::CreateImage(/*width=*/6, /*height=*/17, /*depth=*/8,
                            AVIF_PIXEL_FORMAT_YUV420, AVIF_PLANES_YUV);
  if (gain_map == nullptr) {
    return {nullptr, nullptr};
  }
  testutil::FillImageGradient(gain_map.get());
  image->gainMap.image = gain_map.release();  // 'image' now owns the gain map.
  image->gainMap.metadata = GetTestGainMapMetadata(base_rendition_is_hdr);

  if (base_rendition_is_hdr) {
    image->clli.maxCLL = 10;
    image->clli.maxPALL = 5;
  } else {
    // Even though this is attached to the gain map, it represents the clli
    // information of the tone mapped image.
    image->gainMap.image->clli.maxCLL = 10;
    image->gainMap.image->clli.maxPALL = 5;
  }

  return image;
}

TEST(GainMapTest, EncodeDecodeBaseImageSdr) {
  testutil::AvifImagePtr image =
      CreateTestImageWithGainMap(/*base_rendition_is_hdr=*/false);

  testutil::AvifEncoderPtr encoder(avifEncoderCreate(), avifEncoderDestroy);
  ASSERT_NE(encoder, nullptr);
  testutil::AvifRwData encoded;
  avifResult result = avifEncoderWrite(encoder.get(), image.get(), &encoded);
  ASSERT_EQ(result, AVIF_RESULT_OK)
      << avifResultToString(result) << " " << encoder->diag.error;

  testutil::AvifDecoderPtr decoder(avifDecoderCreate(), avifDecoderDestroy);
  ASSERT_NE(decoder, nullptr);
  decoder->enableDecodingGainMap = AVIF_TRUE;
  decoder->enableParsingGainMapMetadata = AVIF_TRUE;

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
  EXPECT_TRUE(decoder->gainMapPresent);
  ASSERT_NE(decoded->gainMap.image, nullptr);
  EXPECT_EQ(decoded->gainMap.image->matrixCoefficients,
            image->gainMap.image->matrixCoefficients);
  EXPECT_EQ(decoded->gainMap.image->clli.maxCLL,
            image->gainMap.image->clli.maxCLL);
  EXPECT_EQ(decoded->gainMap.image->clli.maxPALL,
            image->gainMap.image->clli.maxPALL);
  EXPECT_EQ(decoded->gainMap.image->width, image->gainMap.image->width);
  EXPECT_EQ(decoded->gainMap.image->height, image->gainMap.image->height);
  EXPECT_EQ(decoded->gainMap.image->depth, image->gainMap.image->depth);
  CheckGainMapMetadataMatches(decoded->gainMap.metadata,
                              image->gainMap.metadata);

  // Decode the image.
  result = avifDecoderNextImage(decoder.get());
  ASSERT_EQ(result, AVIF_RESULT_OK)
      << avifResultToString(result) << " " << decoder->diag.error;

  // Verify that the input and decoded images are close.
  EXPECT_GT(testutil::GetPsnr(*image, *decoded), 40.0);
  EXPECT_GT(testutil::GetPsnr(*image->gainMap.image, *decoded->gainMap.image),
            40.0);

  // Uncomment the following to save the encoded image as an AVIF file.
  //  std::ofstream("/tmp/avifgainmaptest_basesdr.avif", std::ios::binary)
  //      .write(reinterpret_cast<char*>(encoded.data), encoded.size);
}

TEST(GainMapTest, EncodeDecodeBaseImageHdr) {
  testutil::AvifImagePtr image =
      CreateTestImageWithGainMap(/*base_rendition_is_hdr=*/true);

  testutil::AvifEncoderPtr encoder(avifEncoderCreate(), avifEncoderDestroy);
  ASSERT_NE(encoder, nullptr);
  testutil::AvifRwData encoded;
  avifResult result = avifEncoderWrite(encoder.get(), image.get(), &encoded);
  ASSERT_EQ(result, AVIF_RESULT_OK)
      << avifResultToString(result) << " " << encoder->diag.error;

  testutil::AvifImagePtr decoded(avifImageCreateEmpty(), avifImageDestroy);
  ASSERT_NE(decoded, nullptr);
  testutil::AvifDecoderPtr decoder(avifDecoderCreate(), avifDecoderDestroy);
  ASSERT_NE(decoder, nullptr);
  decoder->enableDecodingGainMap = AVIF_TRUE;
  decoder->enableParsingGainMapMetadata = AVIF_TRUE;
  result = avifDecoderReadMemory(decoder.get(), decoded.get(), encoded.data,
                                 encoded.size);
  ASSERT_EQ(result, AVIF_RESULT_OK)
      << avifResultToString(result) << " " << decoder->diag.error;

  // Verify that the input and decoded images are close.
  EXPECT_GT(testutil::GetPsnr(*image, *decoded), 40.0);
  // Verify that the gain map is present and matches the input.
  EXPECT_TRUE(decoder->gainMapPresent);
  ASSERT_NE(decoded->gainMap.image, nullptr);
  EXPECT_GT(testutil::GetPsnr(*image->gainMap.image, *decoded->gainMap.image),
            40.0);
  EXPECT_EQ(decoded->clli.maxCLL, image->clli.maxCLL);
  EXPECT_EQ(decoded->clli.maxPALL, image->clli.maxPALL);
  CheckGainMapMetadataMatches(decoded->gainMap.metadata,
                              image->gainMap.metadata);

  // Uncomment the following to save the encoded image as an AVIF file.
  //  std::ofstream("/tmp/avifgainmaptest_basehdr.avif", std::ios::binary)
  //      .write(reinterpret_cast<char*>(encoded.data), encoded.size);
}

TEST(GainMapTest, EncodeDecodeGrid) {
  std::vector<testutil::AvifImagePtr> cells;
  std::vector<const avifImage*> cell_ptrs;
  std::vector<const avifImage*> gain_map_ptrs;
  constexpr int kGridCols = 2;
  constexpr int kGridRows = 2;
  constexpr int kCellWidth = 128;
  constexpr int kCellHeight = 200;

  avifGainMapMetadata gain_map_metadata =
      GetTestGainMapMetadata(/*base_rendition_is_hdr=*/true);

  for (int i = 0; i < kGridCols * kGridRows; ++i) {
    testutil::AvifImagePtr image =
        testutil::CreateImage(kCellWidth, kCellHeight, /*depth=*/10,
                              AVIF_PIXEL_FORMAT_YUV444, AVIF_PLANES_ALL);
    ASSERT_NE(image, nullptr);
    image->transferCharacteristics =
        AVIF_TRANSFER_CHARACTERISTICS_SMPTE2084;  // PQ
    testutil::FillImageGradient(image.get());
    testutil::AvifImagePtr gain_map =
        testutil::CreateImage(kCellWidth / 2, kCellHeight / 2, /*depth=*/8,
                              AVIF_PIXEL_FORMAT_YUV420, AVIF_PLANES_YUV);
    ASSERT_NE(gain_map, nullptr);
    testutil::FillImageGradient(gain_map.get());
    // 'image' now owns the gain map.
    image->gainMap.image = gain_map.release();
    // all cells must have the same metadata
    image->gainMap.metadata = gain_map_metadata;

    cell_ptrs.push_back(image.get());
    gain_map_ptrs.push_back(image->gainMap.image);
    cells.push_back(std::move(image));
  }

  testutil::AvifEncoderPtr encoder(avifEncoderCreate(), avifEncoderDestroy);
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

  testutil::AvifImagePtr decoded(avifImageCreateEmpty(), avifImageDestroy);
  ASSERT_NE(decoded, nullptr);
  testutil::AvifDecoderPtr decoder(avifDecoderCreate(), avifDecoderDestroy);
  ASSERT_NE(decoder, nullptr);
  decoder->enableDecodingGainMap = AVIF_TRUE;
  decoder->enableParsingGainMapMetadata = AVIF_TRUE;
  result = avifDecoderReadMemory(decoder.get(), decoded.get(), encoded.data,
                                 encoded.size);
  ASSERT_EQ(result, AVIF_RESULT_OK)
      << avifResultToString(result) << " " << decoder->diag.error;

  testutil::AvifImagePtr merged = testutil::CreateImage(
      static_cast<int>(decoded->width), static_cast<int>(decoded->height),
      decoded->depth, decoded->yuvFormat, AVIF_PLANES_ALL);
  ASSERT_EQ(testutil::MergeGrid(kGridCols, kGridRows, cell_ptrs, merged.get()),
            AVIF_RESULT_OK);

  testutil::AvifImagePtr merged_gain_map =
      testutil::CreateImage(static_cast<int>(decoded->gainMap.image->width),
                            static_cast<int>(decoded->gainMap.image->height),
                            decoded->gainMap.image->depth,
                            decoded->gainMap.image->yuvFormat, AVIF_PLANES_YUV);
  ASSERT_EQ(testutil::MergeGrid(kGridCols, kGridRows, gain_map_ptrs,
                                merged_gain_map.get()),
            AVIF_RESULT_OK);

  // Verify that the input and decoded images are close.
  ASSERT_GT(testutil::GetPsnr(*merged, *decoded), 40.0);
  // Verify that the gain map is present and matches the input.
  EXPECT_TRUE(decoder->gainMapPresent);
  ASSERT_NE(decoded->gainMap.image, nullptr);
  ASSERT_GT(testutil::GetPsnr(*merged_gain_map, *decoded->gainMap.image), 40.0);
  CheckGainMapMetadataMatches(decoded->gainMap.metadata, gain_map_metadata);

  // Check that non-incremental and incremental decodings of a grid AVIF produce
  // the same pixels.
  testutil::DecodeNonIncrementallyAndIncrementally(
      encoded, decoder.get(),
      /*is_persistent=*/true, /*give_size_hint=*/true,
      /*use_nth_image_api=*/false, kCellHeight,
      /*enable_fine_incremental_check=*/true);

  // Uncomment the following to save the encoded image as an AVIF file.
  //  std::ofstream("/tmp/avifgainmaptest_grid.avif", std::ios::binary)
  //      .write(reinterpret_cast<char*>(encoded.data), encoded.size);
}

TEST(GainMapTest, InvalidGrid) {
  std::vector<testutil::AvifImagePtr> cells;
  std::vector<const avifImage*> cell_ptrs;
  constexpr int kGridCols = 2;
  constexpr int kGridRows = 2;

  avifGainMapMetadata gain_map_metadata =
      GetTestGainMapMetadata(/*base_rendition_is_hdr=*/true);

  for (int i = 0; i < kGridCols * kGridRows; ++i) {
    testutil::AvifImagePtr image =
        testutil::CreateImage(/*width=*/64, /*height=*/100, /*depth=*/10,
                              AVIF_PIXEL_FORMAT_YUV444, AVIF_PLANES_ALL);
    ASSERT_NE(image, nullptr);
    image->transferCharacteristics =
        AVIF_TRANSFER_CHARACTERISTICS_SMPTE2084;  // PQ
    testutil::FillImageGradient(image.get());
    testutil::AvifImagePtr gain_map =
        testutil::CreateImage(/*width=*/64, /*height=*/100, /*depth=*/8,
                              AVIF_PIXEL_FORMAT_YUV420, AVIF_PLANES_YUV);
    ASSERT_NE(gain_map, nullptr);
    testutil::FillImageGradient(gain_map.get());
    // 'image' now owns the gain map.
    image->gainMap.image = gain_map.release();
    // all cells must have the same metadata
    image->gainMap.metadata = gain_map_metadata;

    cell_ptrs.push_back(image.get());
    cells.push_back(std::move(image));
  }

  testutil::AvifEncoderPtr encoder(avifEncoderCreate(), avifEncoderDestroy);
  ASSERT_NE(encoder, nullptr);
  testutil::AvifRwData encoded;

  avifResult result;

  // Invalid: one cell has the wrong size.
  cells[1]->gainMap.image->height = 90;
  result =
      avifEncoderAddImageGrid(encoder.get(), kGridCols, kGridRows,
                              cell_ptrs.data(), AVIF_ADD_IMAGE_FLAG_SINGLE);
  EXPECT_EQ(result, AVIF_RESULT_INVALID_IMAGE_GRID)
      << avifResultToString(result) << " " << encoder->diag.error;
  cells[1]->gainMap.image->height = cells[0]->gainMap.image->height;  // Revert.

  // Invalid: one cell has a different depth.
  cells[1]->gainMap.image->depth = 12;
  result =
      avifEncoderAddImageGrid(encoder.get(), kGridCols, kGridRows,
                              cell_ptrs.data(), AVIF_ADD_IMAGE_FLAG_SINGLE);
  EXPECT_EQ(result, AVIF_RESULT_INVALID_IMAGE_GRID)
      << avifResultToString(result) << " " << encoder->diag.error;
  cells[1]->gainMap.image->depth = cells[0]->gainMap.image->depth;  // Revert.

  // Invalid: one cell has different gain map metadata.
  cells[1]->gainMap.metadata.gainMapGammaN[0] = 42;
  result =
      avifEncoderAddImageGrid(encoder.get(), kGridCols, kGridRows,
                              cell_ptrs.data(), AVIF_ADD_IMAGE_FLAG_SINGLE);
  EXPECT_EQ(result, AVIF_RESULT_INVALID_IMAGE_GRID)
      << avifResultToString(result) << " " << encoder->diag.error;
  cells[1]->gainMap.metadata.gainMapGammaN[0] =
      cells[0]->gainMap.metadata.gainMapGammaN[0];  // Revert.
}

TEST(GainMapTest, SequenceNotSupported) {
  testutil::AvifImagePtr image =
      testutil::CreateImage(/*width=*/64, /*height=*/100, /*depth=*/10,
                            AVIF_PIXEL_FORMAT_YUV444, AVIF_PLANES_ALL);
  ASSERT_NE(image, nullptr);
  image->transferCharacteristics =
      AVIF_TRANSFER_CHARACTERISTICS_SMPTE2084;  // PQ
  testutil::FillImageGradient(image.get());
  testutil::AvifImagePtr gain_map =
      testutil::CreateImage(/*width=*/64, /*height=*/100, /*depth=*/8,
                            AVIF_PIXEL_FORMAT_YUV420, AVIF_PLANES_YUV);
  ASSERT_NE(gain_map, nullptr);
  testutil::FillImageGradient(gain_map.get());
  // 'image' now owns the gain map.
  image->gainMap.image = gain_map.release();

  testutil::AvifEncoderPtr encoder(avifEncoderCreate(), avifEncoderDestroy);
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

TEST(GainMapTest, IgnoreGainMap) {
  testutil::AvifImagePtr image =
      CreateTestImageWithGainMap(/*base_rendition_is_hdr=*/false);
  ASSERT_NE(image, nullptr);

  testutil::AvifEncoderPtr encoder(avifEncoderCreate(), avifEncoderDestroy);
  ASSERT_NE(encoder, nullptr);
  testutil::AvifRwData encoded;
  avifResult result = avifEncoderWrite(encoder.get(), image.get(), &encoded);
  ASSERT_EQ(result, AVIF_RESULT_OK)
      << avifResultToString(result) << " " << encoder->diag.error;

  // Decode image, with enableDecodingGainMap false by default.
  testutil::AvifImagePtr decoded(avifImageCreateEmpty(), avifImageDestroy);
  ASSERT_NE(decoded, nullptr);
  testutil::AvifDecoderPtr decoder(avifDecoderCreate(), avifDecoderDestroy);
  ASSERT_NE(decoder, nullptr);
  result = avifDecoderReadMemory(decoder.get(), decoded.get(), encoded.data,
                                 encoded.size);
  ASSERT_EQ(result, AVIF_RESULT_OK)
      << avifResultToString(result) << " " << decoder->diag.error;

  // Verify that the input and decoded images are close.
  EXPECT_GT(testutil::GetPsnr(*image, *decoded), 40.0);
  // Verify that the gain map was detected...
  EXPECT_TRUE(decoder->gainMapPresent);
  // ... but not decoded because enableDecodingGainMap is false by default.
  EXPECT_EQ(decoded->gainMap.image, nullptr);
  // Check that the gain map metadata was not populated either.
  CheckGainMapMetadataMatches(decoded->gainMap.metadata, avifGainMapMetadata());
}

TEST(GainMapTest, IgnoreGainMapButReadMetadata) {
  testutil::AvifImagePtr image =
      CreateTestImageWithGainMap(/*base_rendition_is_hdr=*/false);
  ASSERT_NE(image, nullptr);

  testutil::AvifEncoderPtr encoder(avifEncoderCreate(), avifEncoderDestroy);
  ASSERT_NE(encoder, nullptr);
  testutil::AvifRwData encoded;
  avifResult result = avifEncoderWrite(encoder.get(), image.get(), &encoded);
  ASSERT_EQ(result, AVIF_RESULT_OK)
      << avifResultToString(result) << " " << encoder->diag.error;

  // Decode image, with enableDecodingGainMap false by default.
  testutil::AvifImagePtr decoded(avifImageCreateEmpty(), avifImageDestroy);
  ASSERT_NE(decoded, nullptr);
  testutil::AvifDecoderPtr decoder(avifDecoderCreate(), avifDecoderDestroy);
  ASSERT_NE(decoder, nullptr);
  decoder->enableParsingGainMapMetadata = AVIF_TRUE;  // Read gain map metadata.
  result = avifDecoderReadMemory(decoder.get(), decoded.get(), encoded.data,
                                 encoded.size);
  ASSERT_EQ(result, AVIF_RESULT_OK)
      << avifResultToString(result) << " " << decoder->diag.error;

  // Verify that the input and decoded images are close.
  EXPECT_GT(testutil::GetPsnr(*image, *decoded), 40.0);
  // Verify that the gain map was detected...
  EXPECT_TRUE(decoder->gainMapPresent);
  // ... but not decoded because enableDecodingGainMap is false by default.
  EXPECT_EQ(decoded->gainMap.image, nullptr);
  // Check that the gain map metadata WAS populated.
  CheckGainMapMetadataMatches(decoded->gainMap.metadata,
                              image->gainMap.metadata);
}

TEST(GainMapTest, IgnoreColorAndAlpha) {
  testutil::AvifImagePtr image =
      CreateTestImageWithGainMap(/*base_rendition_is_hdr=*/false);
  ASSERT_NE(image, nullptr);

  testutil::AvifEncoderPtr encoder(avifEncoderCreate(), avifEncoderDestroy);
  ASSERT_NE(encoder, nullptr);
  testutil::AvifRwData encoded;
  avifResult result = avifEncoderWrite(encoder.get(), image.get(), &encoded);
  ASSERT_EQ(result, AVIF_RESULT_OK)
      << avifResultToString(result) << " " << encoder->diag.error;

  testutil::AvifImagePtr decoded(avifImageCreateEmpty(), avifImageDestroy);
  ASSERT_NE(decoded, nullptr);
  testutil::AvifDecoderPtr decoder(avifDecoderCreate(), avifDecoderDestroy);
  ASSERT_NE(decoder, nullptr);
  // Decode just the gain map.
  decoder->ignoreColorAndAlpha = AVIF_TRUE;
  decoder->enableDecodingGainMap = AVIF_TRUE;
  decoder->enableParsingGainMapMetadata = AVIF_TRUE;
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
  EXPECT_TRUE(decoder->gainMapPresent);
  ASSERT_NE(decoded->gainMap.image, nullptr);
  EXPECT_GT(testutil::GetPsnr(*image->gainMap.image, *decoded->gainMap.image),
            40.0);
  CheckGainMapMetadataMatches(decoded->gainMap.metadata,
                              image->gainMap.metadata);
}

TEST(GainMapTest, IgnoreAll) {
  testutil::AvifImagePtr image =
      CreateTestImageWithGainMap(/*base_rendition_is_hdr=*/false);
  ASSERT_NE(image, nullptr);

  testutil::AvifEncoderPtr encoder(avifEncoderCreate(), avifEncoderDestroy);
  ASSERT_NE(encoder, nullptr);
  testutil::AvifRwData encoded;
  avifResult result = avifEncoderWrite(encoder.get(), image.get(), &encoded);
  ASSERT_EQ(result, AVIF_RESULT_OK)
      << avifResultToString(result) << " " << encoder->diag.error;

  testutil::AvifDecoderPtr decoder(avifDecoderCreate(), avifDecoderDestroy);
  ASSERT_NE(decoder, nullptr);
  // Ignore both the main image and the gain map.
  decoder->ignoreColorAndAlpha = AVIF_TRUE;
  decoder->enableDecodingGainMap = AVIF_FALSE;
  // But do read the gain map metadata.
  decoder->enableParsingGainMapMetadata = AVIF_TRUE;

  // Parsing just the header should work.
  ASSERT_EQ(avifDecoderSetIOMemory(decoder.get(), encoded.data, encoded.size),
            AVIF_RESULT_OK);
  ASSERT_EQ(avifDecoderParse(decoder.get()), AVIF_RESULT_OK);

  EXPECT_TRUE(decoder->gainMapPresent);
  CheckGainMapMetadataMatches(decoder->image->gainMap.metadata,
                              image->gainMap.metadata);
  ASSERT_EQ(decoder->image->gainMap.image, nullptr);

  // But trying to access the next image should give an error because both
  // ignoreColorAndAlpha and enableDecodingGainMap are set.
  ASSERT_EQ(avifDecoderNextImage(decoder.get()), AVIF_RESULT_NO_CONTENT);
}

TEST(GainMapTest, NoGainMap) {
  // Create a simple image without a gain map.
  testutil::AvifImagePtr image =
      testutil::CreateImage(/*width=*/12, /*height=*/34, /*depth=*/10,
                            AVIF_PIXEL_FORMAT_YUV420, AVIF_PLANES_ALL);
  ASSERT_NE(image, nullptr);
  image->transferCharacteristics = AVIF_TRANSFER_CHARACTERISTICS_SRGB;
  testutil::FillImageGradient(image.get());
  testutil::AvifEncoderPtr encoder(avifEncoderCreate(), avifEncoderDestroy);
  ASSERT_NE(encoder, nullptr);
  testutil::AvifRwData encoded;
  avifResult result = avifEncoderWrite(encoder.get(), image.get(), &encoded);
  ASSERT_EQ(result, AVIF_RESULT_OK)
      << avifResultToString(result) << " " << encoder->diag.error;

  testutil::AvifImagePtr decoded(avifImageCreateEmpty(), avifImageDestroy);
  ASSERT_NE(decoded, nullptr);
  testutil::AvifDecoderPtr decoder(avifDecoderCreate(), avifDecoderDestroy);
  ASSERT_NE(decoder, nullptr);
  // Enable gain map decoding.
  decoder->enableDecodingGainMap = AVIF_TRUE;
  decoder->enableParsingGainMapMetadata = AVIF_TRUE;
  result = avifDecoderReadMemory(decoder.get(), decoded.get(), encoded.data,
                                 encoded.size);
  ASSERT_EQ(result, AVIF_RESULT_OK)
      << avifResultToString(result) << " " << decoder->diag.error;

  // Verify that the input and decoded images are close.
  EXPECT_GT(testutil::GetPsnr(*image, *decoded), 40.0);
  // Verify that no gain map was found.
  EXPECT_FALSE(decoder->gainMapPresent);
  EXPECT_EQ(decoded->gainMap.image, nullptr);
  CheckGainMapMetadataMatches(decoded->gainMap.metadata, avifGainMapMetadata());
}

TEST(GainMapTest, DecodeGainMapGrid) {
  const std::string path =
      std::string(data_path) + "color_grid_gainmap_different_grid.avif";
  testutil::AvifDecoderPtr decoder(avifDecoderCreate(), avifDecoderDestroy);
  ASSERT_NE(decoder, nullptr);
  decoder->enableDecodingGainMap = true;
  decoder->enableParsingGainMapMetadata = true;

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
  EXPECT_TRUE(decoder->gainMapPresent);
  // Color+alpha: 4x3 grid of 128x200 tiles.
  EXPECT_EQ(decoded->width, 128u * 4u);
  EXPECT_EQ(decoded->height, 200u * 3u);
  EXPECT_EQ(decoded->depth, 10u);
  ASSERT_NE(decoded->gainMap.image, nullptr);
  // Gain map: 2x2 grid of 64x80 tiles.
  EXPECT_EQ(decoded->gainMap.image->width, 64u * 2u);
  EXPECT_EQ(decoded->gainMap.image->height, 80u * 2u);
  EXPECT_EQ(decoded->gainMap.image->depth, 8u);
  EXPECT_EQ(decoded->gainMap.metadata.hdrCapacityMaxN, 16u);
  EXPECT_EQ(decoded->gainMap.metadata.hdrCapacityMaxD, 2u);

  // Decode the image.
  result = avifDecoderNextImage(decoder.get());
  ASSERT_EQ(result, AVIF_RESULT_OK)
      << avifResultToString(result) << " " << decoder->diag.error;
}

TEST(GainMapTest, DecodeColorGridGainMapNoGrid) {
  const std::string path =
      std::string(data_path) + "color_grid_alpha_grid_gainmap_nogrid.avif";
  testutil::AvifImagePtr decoded(avifImageCreateEmpty(), avifImageDestroy);
  ASSERT_NE(decoded, nullptr);
  testutil::AvifDecoderPtr decoder(avifDecoderCreate(), avifDecoderDestroy);
  ASSERT_NE(decoder, nullptr);
  decoder->enableDecodingGainMap = true;
  decoder->enableParsingGainMapMetadata = true;
  ASSERT_EQ(avifDecoderReadFile(decoder.get(), decoded.get(), path.c_str()),
            AVIF_RESULT_OK);

  // Color+alpha: 4x3 grid of 128x200 tiles.
  EXPECT_EQ(decoded->width, 128u * 4u);
  EXPECT_EQ(decoded->height, 200u * 3u);
  ASSERT_NE(decoded->gainMap.image, nullptr);
  // Gain map: single image of size 64x80.
  EXPECT_EQ(decoded->gainMap.image->width, 64u);
  EXPECT_EQ(decoded->gainMap.image->height, 80u);
  EXPECT_EQ(decoded->gainMap.metadata.hdrCapacityMaxN, 16u);
  EXPECT_EQ(decoded->gainMap.metadata.hdrCapacityMaxD, 2u);
}

TEST(GainMapTest, DecodeColorNoGridGainMapGrid) {
  const std::string path =
      std::string(data_path) + "color_nogrid_alpha_nogrid_gainmap_grid.avif";
  testutil::AvifImagePtr decoded(avifImageCreateEmpty(), avifImageDestroy);
  ASSERT_NE(decoded, nullptr);
  testutil::AvifDecoderPtr decoder(avifDecoderCreate(), avifDecoderDestroy);
  ASSERT_NE(decoder, nullptr);
  decoder->enableDecodingGainMap = true;
  decoder->enableParsingGainMapMetadata = true;
  ASSERT_EQ(avifDecoderReadFile(decoder.get(), decoded.get(), path.c_str()),
            AVIF_RESULT_OK);

  // Color+alpha: single image of size 128x200 .
  EXPECT_EQ(decoded->width, 128u);
  EXPECT_EQ(decoded->height, 200u);
  ASSERT_NE(decoded->gainMap.image, nullptr);
  // Gain map: 2x2 grid of 64x80 tiles.
  EXPECT_EQ(decoded->gainMap.image->width, 64u * 2u);
  EXPECT_EQ(decoded->gainMap.image->height, 80u * 2u);
  EXPECT_EQ(decoded->gainMap.metadata.hdrCapacityMaxN, 16u);
  EXPECT_EQ(decoded->gainMap.metadata.hdrCapacityMaxD, 2u);
}

#define EXPECT_FRACTION_NEAR(numerator, denominator, expected)     \
  EXPECT_NEAR(std::abs((double)numerator / denominator), expected, \
              expected * 0.001);

TEST(GainMapTest, ConvertMetadata) {
  avifGainMapMetadataDouble metadata_double = {};
  metadata_double.gainMapMin[0] = 1.0;
  metadata_double.gainMapMin[1] = 1.1;
  metadata_double.gainMapMin[2] = 1.2;
  metadata_double.gainMapMax[0] = 10.0;
  metadata_double.gainMapMax[1] = 10.1;
  metadata_double.gainMapMax[2] = 10.2;
  metadata_double.gainMapGamma[0] = 1.0;
  metadata_double.gainMapGamma[1] = 1.0;
  metadata_double.gainMapGamma[2] = 1.2;
  metadata_double.offsetSdr[0] = 1.0 / 32.0;
  metadata_double.offsetSdr[1] = 1.0 / 64.0;
  metadata_double.offsetSdr[2] = 1.0 / 128.0;
  metadata_double.offsetHdr[0] = 0.004564;
  metadata_double.offsetHdr[1] = 0.0;
  metadata_double.hdrCapacityMin = 1.0;
  metadata_double.hdrCapacityMax = 10.0;
  metadata_double.baseRenditionIsHDR = AVIF_TRUE;

  // Convert to avifGainMapMetadata.
  avifGainMapMetadata metadata = {};
  ASSERT_TRUE(
      avifGainMapMetadataDoubleToFractions(&metadata, &metadata_double));

  for (int i = 0; i < 3; ++i) {
    EXPECT_FRACTION_NEAR(metadata.gainMapMinN[i], metadata.gainMapMinD[i],
                         metadata_double.gainMapMin[i]);
    EXPECT_FRACTION_NEAR(metadata.gainMapMaxN[i], metadata.gainMapMaxD[i],
                         metadata_double.gainMapMax[i]);
    EXPECT_FRACTION_NEAR(metadata.gainMapGammaN[i], metadata.gainMapGammaD[i],
                         metadata_double.gainMapGamma[i]);
    EXPECT_FRACTION_NEAR(metadata.offsetSdrN[i], metadata.offsetSdrD[i],
                         metadata_double.offsetSdr[i]);
    EXPECT_FRACTION_NEAR(metadata.offsetHdrN[i], metadata.offsetHdrD[i],
                         metadata_double.offsetHdr[i]);
  }
  EXPECT_FRACTION_NEAR(metadata.hdrCapacityMinN, metadata.hdrCapacityMinD,
                       metadata_double.hdrCapacityMin);
  EXPECT_FRACTION_NEAR(metadata.hdrCapacityMaxN, metadata.hdrCapacityMaxD,
                       metadata_double.hdrCapacityMax);
  EXPECT_EQ(metadata.baseRenditionIsHDR, metadata_double.baseRenditionIsHDR);

  // Convert back to avifGainMapMetadataDouble.
  avifGainMapMetadataDouble metadata_double2 = {};
  ASSERT_TRUE(
      avifGainMapMetadataFractionsToDouble(&metadata_double2, &metadata));

  constexpr double kEpsilon = 0.000001;
  for (int i = 0; i < 3; ++i) {
    EXPECT_NEAR(metadata_double2.gainMapMin[i], metadata_double.gainMapMin[i],
                kEpsilon);
    EXPECT_NEAR(metadata_double2.gainMapMax[i], metadata_double.gainMapMax[i],
                kEpsilon);
    EXPECT_NEAR(metadata_double2.gainMapGamma[i],
                metadata_double.gainMapGamma[i], kEpsilon);
    EXPECT_NEAR(metadata_double2.offsetSdr[i], metadata_double.offsetSdr[i],
                kEpsilon);
    EXPECT_NEAR(metadata_double2.offsetHdr[i], metadata_double.offsetHdr[i],
                kEpsilon);
  }
  EXPECT_NEAR(metadata_double2.hdrCapacityMin, metadata_double.hdrCapacityMin,
              kEpsilon);
  EXPECT_NEAR(metadata_double2.hdrCapacityMax, metadata_double.hdrCapacityMax,
              kEpsilon);
  EXPECT_EQ(metadata_double2.baseRenditionIsHDR,
            metadata_double.baseRenditionIsHDR);
}

TEST(GainMapTest, ConvertMetadataToFractionInvalid) {
  avifGainMapMetadataDouble metadata_double = {};
  metadata_double.gainMapGamma[0] = -42;  // A negative value is invalid!
  avifGainMapMetadata metadata = {};
  ASSERT_FALSE(
      avifGainMapMetadataDoubleToFractions(&metadata, &metadata_double));
}

TEST(GainMapTest, ConvertMetadataToDoubleInvalid) {
  avifGainMapMetadata metadata = {};  // Denominators are zero.
  avifGainMapMetadataDouble metadata_double = {};
  ASSERT_FALSE(
      avifGainMapMetadataFractionsToDouble(&metadata_double, &metadata));
}

class ToneMapTest
    : public testing::TestWithParam<std::tuple<
          /*source=*/std::string, /*hdr_capacity=*/float,
          /*out_depth=*/int,
          /*out_transfer_characteristics=*/avifTransferCharacteristics,
          /*reference=*/std::string, /*min_psnr=*/float>> {};

TEST_P(ToneMapTest, ToneMapImage) {
  const std::string source = std::get<0>(GetParam());
  const float hdr_capacity = std::get<1>(GetParam());
  // out_depth and out_transfer_characteristics should match the reference image
  // when ther eis one, so that GetPsnr works.
  const int out_depth = std::get<2>(GetParam());
  const avifTransferCharacteristics out_transfer_characteristics =
      std::get<3>(GetParam());
  const std::string reference = std::get<4>(GetParam());
  const float min_psnr = std::get<5>(GetParam());

  testutil::AvifImagePtr reference_image = {nullptr, nullptr};
  if (!source.empty()) {
    reference_image = testutil::DecodFile(std::string(data_path) + reference);
  }

  // Load the source image (that should contain a gain map).
  const std::string path = std::string(data_path) + source;
  testutil::AvifImagePtr image(avifImageCreateEmpty(), avifImageDestroy);
  ASSERT_NE(image, nullptr);
  testutil::AvifDecoderPtr decoder(avifDecoderCreate(), avifDecoderDestroy);
  ASSERT_NE(decoder, nullptr);
  decoder->enableDecodingGainMap = true;
  decoder->enableParsingGainMapMetadata = true;
  avifResult result =
      avifDecoderReadFile(decoder.get(), image.get(), path.c_str());
  ASSERT_EQ(result, AVIF_RESULT_OK)
      << avifResultToString(result) << " " << decoder->diag.error;

  ASSERT_NE(image->gainMap.image, nullptr);

  testutil::AvifRgbImage tone_mapped_rgb(image.get(), out_depth,
                                         AVIF_RGB_FORMAT_RGB);
  testutil::AvifImagePtr tone_mapped(
      avifImageCreate(tone_mapped_rgb.width, tone_mapped_rgb.height,
                      tone_mapped_rgb.depth, AVIF_PIXEL_FORMAT_YUV444),
      avifImageDestroy);
  tone_mapped->transferCharacteristics = out_transfer_characteristics;
  tone_mapped->colorPrimaries = image->colorPrimaries;

  avifDiagnostics diag;
  result = avifImageApplyGainMap(image.get(), &image->gainMap, hdr_capacity,
                                 tone_mapped->transferCharacteristics,
                                 &tone_mapped_rgb, &tone_mapped->clli, &diag);
  ASSERT_EQ(result, AVIF_RESULT_OK)
      << avifResultToString(result) << " " << decoder->diag.error;
  ASSERT_EQ(avifImageRGBToYUV(tone_mapped.get(), &tone_mapped_rgb),
            AVIF_RESULT_OK);
  if (reference_image != nullptr) {
    EXPECT_GT(testutil::GetPsnr(*reference_image, *tone_mapped), min_psnr);
  }

  // Uncomment the following to save the encoded image as an AVIF file.
  //   testutil::AvifEncoderPtr encoder(avifEncoderCreate(),
  //     avifEncoderDestroy);
  //   ASSERT_NE(encoder, nullptr);
  //   encoder->speed = 9;
  //   encoder->quality = 90;
  //   encoder->qualityGainMap = 90;
  //   testutil::AvifRwData encoded;
  //   ASSERT_EQ(avifEncoderWrite(encoder.get(), tone_mapped.get(), &encoded),
  //             AVIF_RESULT_OK);
  //   std::ofstream(
  //       "/tmp/tone_mapped_" + std::to_string(hdr_capacity) + "_" + source,
  //       std::ios::binary)
  //       .write(reinterpret_cast<char*>(encoded.data), encoded.size);
}

INSTANTIATE_TEST_SUITE_P(
    All, ToneMapTest,
    Values(
        // ------ SDR BASE IMAGE ------

        // hdr_capacity=1, the image should stay SDR (base image untouched).
        // A small loss is expected due to YUV/RGB conversion.
        std::make_tuple(
            /*source=*/"seine_sdr_gainmap_srgb.avif", /*hdr_capacity=*/1.0f,
            /*out_depth=*/8,
            /*out_transfer_characteristics=*/AVIF_TRANSFER_CHARACTERISTICS_SRGB,
            /*reference=*/"seine_sdr_gainmap_srgb.avif", /*min_psnr=*/60.0f),

        // hdr_capacity=3, the gain map should be fully applied.
        std::make_tuple(
            /*source=*/"seine_sdr_gainmap_srgb.avif", /*hdr_capacity=*/3.0f,
            /*out_depth=*/10,
            /*out_transfer_characteristics=*/
            AVIF_TRANSFER_CHARACTERISTICS_SMPTE2084,
            /*reference=*/"seine_hdr_srgb.avif", /*min_psnr=*/40.0f),

        // hdr_capacity=3, the gain map should be fully applied.
        // Version with a gain map that is larger than the base image (needs
        // rescaling).
        std::make_tuple(
            /*source=*/"seine_sdr_gainmap_big_srgb.avif", /*hdr_capacity=*/3.0f,
            /*out_depth=*/10,
            /*out_transfer_characteristics=*/
            AVIF_TRANSFER_CHARACTERISTICS_SMPTE2084,
            /*reference=*/"seine_hdr_srgb.avif", /*min_psnr=*/40.0f),

        // hdr_capacity=1.5 No reference image.
        std::make_tuple(
            /*source=*/"seine_sdr_gainmap_srgb.avif", /*hdr_capacity=*/1.5f,
            /*out_depth=*/10,
            /*out_transfer_characteristics=*/
            AVIF_TRANSFER_CHARACTERISTICS_SMPTE2084,
            /*reference=*/"", /*min_psnr=*/0.0f),

        // ------ HDR BASE IMAGE ------

        // hdr_capacity=1, the gain map should be fully applied.
        std::make_tuple(
            /*source=*/"seine_hdr_gainmap_srgb.avif", /*hdr_capacity=*/1.0f,
            /*out_depth=*/8,
            /*out_transfer_characteristics=*/AVIF_TRANSFER_CHARACTERISTICS_SRGB,
            /*reference=*/"seine_sdr_gainmap_srgb.avif", /*min_psnr=*/38.0f),

        // hdr_capacity=1, the gain map should be fully applied.
        // Version with a gain map that is smaller than the base image (needs
        // rescaling). The PSNR is a bit lower than above due to quality loss on
        // the gain map.
        std::make_tuple(
            /*source=*/"seine_hdr_gainmap_small_srgb.avif",
            /*hdr_capacity=*/1.0f,
            /*out_depth=*/8,
            /*out_transfer_characteristics=*/AVIF_TRANSFER_CHARACTERISTICS_SRGB,
            /*reference=*/"seine_sdr_gainmap_srgb.avif", /*min_psnr=*/36.0f),

        // hdr_capacity=3, the image should stay HDR (base image untouched).
        // A small loss is expected due to YUV/RGB conversion.
        std::make_tuple(
            /*source=*/"seine_hdr_gainmap_srgb.avif", /*hdr_capacity=*/3.0f,
            /*out_depth=*/10,
            /*out_transfer_characteristics=*/
            AVIF_TRANSFER_CHARACTERISTICS_SMPTE2084,
            /*reference=*/"seine_hdr_gainmap_srgb.avif", /*min_psnr=*/60.0f),

        // hdr_capacity=1.5 No reference image.
        std::make_tuple(
            /*source=*/"seine_hdr_gainmap_srgb.avif", /*hdr_capacity=*/1.5f,
            /*out_depth=*/10,
            /*out_transfer_characteristics=*/
            AVIF_TRANSFER_CHARACTERISTICS_SMPTE2084,
            /*reference=*/"", /*min_psnr=*/0.0f)));

}  // namespace
}  // namespace libavif

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  if (argc != 2) {
    std::cerr << "There must be exactly one argument containing the path to "
                 "the test data folder"
              << std::endl;
    return 1;
  }
  libavif::data_path = argv[1];
  return RUN_ALL_TESTS();
}
