// Copyright 2023 Google LLC
// SPDX-License-Identifier: BSD-2-Clause

#include <fstream>

#include "avif/avif.h"
#include "avif/internal.h"
#include "aviftest_helpers.h"
#include "gtest/gtest.h"

namespace libavif {
namespace {

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

TEST(GainMapTest, EncodeDecodeBaseImageSdr) {
  testutil::AvifImagePtr image =
      testutil::CreateImage(/*width=*/12, /*height=*/34, /*depth=*/10,
                            AVIF_PIXEL_FORMAT_YUV420, AVIF_PLANES_ALL);
  ASSERT_NE(image, nullptr);
  image->transferCharacteristics = AVIF_TRANSFER_CHARACTERISTICS_SRGB;
  testutil::FillImageGradient(image.get());

  testutil::AvifImagePtr gain_map =
      testutil::CreateImage(/*width=*/6, /*height=*/17, /*depth=*/8,
                            AVIF_PIXEL_FORMAT_YUV420, AVIF_PLANES_YUV);
  ASSERT_NE(gain_map, nullptr);
  testutil::FillImageGradient(gain_map.get());
  gain_map->matrixCoefficients = AVIF_MATRIX_COEFFICIENTS_FCC;
  // Even though this is attached to the gain map, it represents the clli
  // information of the tone mapped image.
  gain_map->clli.maxCLL = 10;
  gain_map->clli.maxPALL = 5;

  image->gainMap.image = gain_map.release();  // 'image' now owns the gain map.
  image->gainMap.metadata =
      GetTestGainMapMetadata(/*base_rendition_is_hdr=*/false);

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
  decoder->ignoreGainMap = AVIF_FALSE;
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
  EXPECT_EQ(decoded->gainMap.image->matrixCoefficients,
            image->gainMap.image->matrixCoefficients);
  EXPECT_EQ(decoded->gainMap.image->clli.maxCLL,
            image->gainMap.image->clli.maxCLL);
  EXPECT_EQ(decoded->gainMap.image->clli.maxPALL,
            image->gainMap.image->clli.maxPALL);
  CheckGainMapMetadataMatches(decoded->gainMap.metadata,
                              image->gainMap.metadata);

  // Uncomment the following to save the encoded image as an AVIF file.
  //  std::ofstream("/tmp/avifgainmaptest_basesdr.avif", std::ios::binary)
  //      .write(reinterpret_cast<char*>(encoded.data), encoded.size);
}

TEST(GainMapTest, EncodeDecodeBaseImageHdr) {
  testutil::AvifImagePtr image =
      testutil::CreateImage(/*width=*/12, /*height=*/34, /*depth=*/10,
                            AVIF_PIXEL_FORMAT_YUV420, AVIF_PLANES_ALL);
  ASSERT_NE(image, nullptr);
  image->transferCharacteristics =
      AVIF_TRANSFER_CHARACTERISTICS_SMPTE2084;  // PQ
  image->clli.maxCLL = 10;
  image->clli.maxPALL = 5;
  testutil::FillImageGradient(image.get());

  testutil::AvifImagePtr gain_map =
      testutil::CreateImage(/*width=*/6, /*height=*/17, /*depth=*/8,
                            AVIF_PIXEL_FORMAT_YUV420, AVIF_PLANES_YUV);
  ASSERT_NE(gain_map, nullptr);
  testutil::FillImageGradient(gain_map.get());

  image->gainMap.image = gain_map.release();  // 'image' now owns the gain map.
  image->gainMap.metadata =
      GetTestGainMapMetadata(/*base_rendition_is_hdr=*/true);

  testutil::AvifEncoderPtr encoder(avifEncoderCreate(), avifEncoderDestroy);
  ASSERT_NE(encoder, nullptr);
  testutil::AvifRwData encoded;
  avifResult result = avifEncoderWrite(encoder.get(), image.get(), &encoded);
  ASSERT_EQ(result, AVIF_RESULT_OK)
      << avifResultToString(result) << " " << encoder->diag.error;

  testutil::AvifImagePtr decoded(avifImageCreateEmpty(), avifImageDestroy);
  ASSERT_NE(decoded, nullptr);
  testutil::AvifDecoderPtr decoder(avifDecoderCreate(), avifDecoderDestroy);
  decoder->ignoreGainMap = AVIF_FALSE;
  ASSERT_NE(decoder, nullptr);
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
  decoder->ignoreGainMap = AVIF_FALSE;
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

TEST(GainMapTest, IgnoreGainMapOrColor) {
  // Create image with gain map.
  testutil::AvifImagePtr image =
      testutil::CreateImage(/*width=*/12, /*height=*/34, /*depth=*/10,
                            AVIF_PIXEL_FORMAT_YUV420, AVIF_PLANES_ALL);
  ASSERT_NE(image, nullptr);
  image->transferCharacteristics = AVIF_TRANSFER_CHARACTERISTICS_SRGB;
  testutil::FillImageGradient(image.get());
  testutil::AvifImagePtr gain_map =
      testutil::CreateImage(/*width=*/6, /*height=*/17, /*depth=*/8,
                            AVIF_PIXEL_FORMAT_YUV420, AVIF_PLANES_YUV);
  ASSERT_NE(gain_map, nullptr);
  testutil::FillImageGradient(gain_map.get());
  image->gainMap.image = gain_map.release();  // 'image' now owns the gain map.
  image->gainMap.metadata =
      GetTestGainMapMetadata(/*base_rendition_is_hdr=*/false);
  testutil::AvifEncoderPtr encoder(avifEncoderCreate(), avifEncoderDestroy);
  ASSERT_NE(encoder, nullptr);
  testutil::AvifRwData encoded;
  avifResult result = avifEncoderWrite(encoder.get(), image.get(), &encoded);
  ASSERT_EQ(result, AVIF_RESULT_OK)
      << avifResultToString(result) << " " << encoder->diag.error;

  // Decode image, with ignoreGainMap true by default.
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
  // ... but not decoded because ignoreGainMap is true by default.
  EXPECT_EQ(decoded->gainMap.image, nullptr);
  // Check that the gain map metadata was not populated either.
  CheckGainMapMetadataMatches(decoded->gainMap.metadata, avifGainMapMetadata());

  // Decode again, just the gain map this time.
  decoder->ignoreGainMap = AVIF_FALSE;
  decoder->ignoreColorAndAlpha = AVIF_TRUE;
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

  // Decode again, disabling both the gain map and the main image...
  decoder->ignoreGainMap = AVIF_TRUE;
  decoder->ignoreColorAndAlpha = AVIF_TRUE;
  result = avifDecoderReadMemory(decoder.get(), decoded.get(), encoded.data,
                                 encoded.size);
  // ... which should give an error.
  ASSERT_EQ(result, AVIF_RESULT_NO_CONTENT);
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
  decoder->ignoreGainMap = AVIF_FALSE;
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

}  // namespace
}  // namespace libavif
