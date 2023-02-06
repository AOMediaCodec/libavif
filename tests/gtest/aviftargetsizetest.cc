// Copyright 2022 Google LLC
// SPDX-License-Identifier: BSD-2-Clause

#include <cstddef>
#include <cstdint>
#include <limits>

#include "avif/avif.h"
#include "aviftest_helpers.h"
#include "gtest/gtest.h"

namespace libavif {
namespace {

// Constants for avifEncoder::targetSize.
constexpr size_t kNoTargetSize = 0;
constexpr size_t kMinTargetSize = 1;
constexpr size_t kMaxTargetSize = std::numeric_limits<size_t>::max();

// Constant for durationInTimescales arg of avifEncoderAddImage().
constexpr uint64_t kNoDuration = 0;

// The content of the input image does not matter for this test.
testutil::AvifImagePtr CreateImage(int width, int height) {
  testutil::AvifImagePtr image = testutil::CreateImage(
      width, height, 8, AVIF_PIXEL_FORMAT_YUV444, AVIF_PLANES_ALL);
  testutil::FillImageGradient(image.get());
  return image;
}

// Shortcut for avifEncoderWrite().
testutil::AvifRwData Write(size_t target_size, int quality, int qualityAlpha) {
  testutil::AvifEncoderPtr encoder(avifEncoderCreate(), avifEncoderDestroy);
  encoder->targetSize = target_size;
  encoder->quality = quality;
  encoder->qualityAlpha = qualityAlpha;
  testutil::AvifRwData data;
  EXPECT_EQ(avifEncoderWrite(encoder.get(), CreateImage(6, 7).get(), &data),
            AVIF_RESULT_OK);
  return data;
}

testutil::AvifRwData Write(size_t target_size,
                           int quality = AVIF_QUALITY_DEFAULT) {
  return Write(target_size, quality, quality);
}

TEST(TargetSizeTest, ExtremeTargetSizes) {
  const size_t default_size = Write(kNoTargetSize, AVIF_QUALITY_DEFAULT).size;
  const size_t worst_size = Write(kNoTargetSize, AVIF_QUALITY_WORST).size;
  const size_t best_size = Write(kNoTargetSize, AVIF_QUALITY_BEST).size;
  EXPECT_NE(default_size, 0u);
  EXPECT_LT(worst_size, default_size);
  EXPECT_GT(best_size, default_size);

  constexpr int kQuality = AVIF_QUALITY_DEFAULT;       // Not set.
  constexpr int kQualityAlpha = AVIF_QUALITY_DEFAULT;  // Not set.
  EXPECT_EQ(Write(kMinTargetSize, kQuality, kQualityAlpha).size, worst_size);
  EXPECT_EQ(Write(kMaxTargetSize, kQuality, kQualityAlpha).size, best_size);
}

TEST(TargetSizeTest, FindDefaultQuality) {
  const size_t default_size = Write(kNoTargetSize, AVIF_QUALITY_DEFAULT).size;

  // Find the quality that generated this default_size.
  testutil::AvifEncoderPtr encoder(avifEncoderCreate(), avifEncoderDestroy);
  encoder->targetSize = default_size;
  testutil::AvifRwData data;
  ASSERT_EQ(avifEncoderWrite(encoder.get(), CreateImage(6, 7).get(), &data),
            AVIF_RESULT_OK);
  // 1% margin of error in case the size-quality ratio is not monotonic.
  // Ugly casts because of MSVC warning C4244.
  EXPECT_NEAR(static_cast<double>(data.size), static_cast<double>(default_size),
              default_size / 100.0);
  EXPECT_GT(encoder->quality, AVIF_QUALITY_WORST);
  EXPECT_LT(encoder->quality, AVIF_QUALITY_BEST);
  EXPECT_EQ(encoder->quality, encoder->qualityAlpha);

  // Check if the quality found by the binary search matches the size generated
  // by the binary search.
  EXPECT_EQ(Write(kNoTargetSize, encoder->quality, encoder->qualityAlpha).size,
            data.size);
}

TEST(TargetSizeTest, OnlySearchColorQuality) {
  constexpr int kQuality = AVIF_QUALITY_DEFAULT;        // Not set.
  constexpr int kQualityAlpha = AVIF_QUALITY_BEST / 2;  // Set.
  EXPECT_LT(Write(kMinTargetSize, kQuality, kQualityAlpha).size,
            Write(kMaxTargetSize, kQuality, kQualityAlpha).size);
}

TEST(TargetSizeTest, OnlySearchAlphaQuality) {
  constexpr int kQuality = AVIF_QUALITY_BEST / 2;      // Set.
  constexpr int kQualityAlpha = AVIF_QUALITY_DEFAULT;  // Not set.
  EXPECT_LT(Write(kMinTargetSize, kQuality, kQualityAlpha).size,
            Write(kMaxTargetSize, kQuality, kQualityAlpha).size);
}

TEST(TargetSizeTest, NoBinarySearch) {
  constexpr int kQuality = AVIF_QUALITY_BEST / 2;       // Set.
  constexpr int kQualityAlpha = AVIF_QUALITY_BEST / 2;  // Set.
  // avifEncoder::targetSize has no impact if quality and qualityAlpha are set.
  EXPECT_EQ(Write(kNoTargetSize, kQuality, kQualityAlpha).size,
            Write(kMinTargetSize, kQuality, kQualityAlpha).size);
  EXPECT_EQ(Write(kMinTargetSize, kQuality, kQualityAlpha).size,
            Write(kMaxTargetSize, kQuality, kQualityAlpha).size);
}

TEST(TargetSizeTest, AddImageAndFinish) {
  testutil::AvifEncoderPtr encoder(avifEncoderCreate(), avifEncoderDestroy);
  encoder->targetSize = kMinTargetSize;
  ASSERT_EQ(avifEncoderAddImage(encoder.get(), CreateImage(6, 7).get(),
                                kNoDuration, AVIF_ADD_IMAGE_FLAG_SINGLE),
            AVIF_RESULT_OK);
  testutil::AvifRwData data;
  ASSERT_EQ(avifEncoderFinish(encoder.get(), &data), AVIF_RESULT_OK);

  // Using avifEncoderAddImage()+avifEncoderFinish() or avifEncoderWrite()
  // should be equivalent.
  EXPECT_EQ(data.size, Write(kMinTargetSize).size);
}

TEST(TargetSizeTest, AddImageGridAndFinish) {
  testutil::AvifEncoderPtr encoder(avifEncoderCreate(), avifEncoderDestroy);
  encoder->targetSize = kMinTargetSize;
  const testutil::AvifImagePtr image = CreateImage(64, 66);  // Grids need >=64.
  const avifImage* const cell_images[] = {image.get(), image.get()};
  ASSERT_EQ(
      avifEncoderAddImageGrid(encoder.get(), /*gridCols=*/2, /*gridRows=*/1,
                              cell_images, AVIF_ADD_IMAGE_FLAG_SINGLE),
      AVIF_RESULT_OK);
  testutil::AvifRwData data;
  // The feature works with grids.
  EXPECT_EQ(avifEncoderFinish(encoder.get(), &data), AVIF_RESULT_OK);
}

TEST(TargetSizeTest, AddImageAndAddImage) {
  testutil::AvifEncoderPtr encoder(avifEncoderCreate(), avifEncoderDestroy);
  encoder->targetSize = kMinTargetSize;
  // The feature does not work with animations nor layers.
  EXPECT_EQ(avifEncoderAddImage(encoder.get(), CreateImage(64, 66).get(),
                                kNoDuration, AVIF_ADD_IMAGE_FLAG_NONE),
            AVIF_RESULT_INVALID_ARGUMENT);
}

}  // namespace
}  // namespace libavif
