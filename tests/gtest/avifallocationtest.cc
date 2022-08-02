// Copyright 2022 Google LLC. All rights reserved.
// SPDX-License-Identifier: BSD-2-Clause

#include <limits>
#include <vector>

#include "avif/avif.h"
#include "aviftest_helpers.h"
#include "gtest/gtest.h"

using ::testing::Combine;
using ::testing::Values;
using ::testing::ValuesIn;

namespace libavif {
namespace {

void TestAllocation(uint32_t width, uint32_t height, uint32_t depth,
                    avifResult expected_result) {
  // The format of the image and which planes are allocated should not matter.
  // Test all combinations.
  for (avifPixelFormat format :
       {AVIF_PIXEL_FORMAT_NONE, AVIF_PIXEL_FORMAT_YUV444,
        AVIF_PIXEL_FORMAT_YUV422, AVIF_PIXEL_FORMAT_YUV420,
        AVIF_PIXEL_FORMAT_YUV400}) {
    for (avifPlanesFlag planes :
         {AVIF_PLANES_YUV, AVIF_PLANES_A, AVIF_PLANES_ALL}) {
      testutil::AvifImagePtr image(avifImageCreateEmpty(), avifImageDestroy);
      ASSERT_NE(image, nullptr);
      image->width = width;
      image->height = height;
      image->depth = depth;
      image->yuvFormat = format;
      EXPECT_EQ(avifImageAllocatePlanes(image.get(), planes), expected_result);

      // Make sure the actual plane pointers are consistent with the settings.
      if (expected_result == AVIF_RESULT_OK &&
          format != AVIF_PIXEL_FORMAT_NONE && (planes & AVIF_PLANES_YUV)) {
        EXPECT_NE(image->yuvPlanes[AVIF_CHAN_Y], nullptr);
      } else {
        EXPECT_EQ(image->yuvPlanes[AVIF_CHAN_Y], nullptr);
      }
      if (expected_result == AVIF_RESULT_OK &&
          format != AVIF_PIXEL_FORMAT_NONE &&
          format != AVIF_PIXEL_FORMAT_YUV400 && (planes & AVIF_PLANES_YUV)) {
        EXPECT_NE(image->yuvPlanes[AVIF_CHAN_U], nullptr);
        EXPECT_NE(image->yuvPlanes[AVIF_CHAN_V], nullptr);
      } else {
        EXPECT_EQ(image->yuvPlanes[AVIF_CHAN_U], nullptr);
        EXPECT_EQ(image->yuvPlanes[AVIF_CHAN_V], nullptr);
      }
      if (expected_result == AVIF_RESULT_OK && (planes & AVIF_PLANES_A)) {
        EXPECT_NE(image->alphaPlane, nullptr);
      } else {
        EXPECT_EQ(image->alphaPlane, nullptr);
      }
    }
  }
}

TEST(AllocationTest, MinimumValidDimensions) {
  TestAllocation(1, 1, 8, AVIF_RESULT_OK);
}

TEST(AllocationTest, MaximumValidDimensions) {
  // On 32-bit builds, malloc() will fail with fairly low sizes.
  // Adapt the tests to take that into account.
  constexpr bool kIsPlatform64b = sizeof(void*) > 4;
  constexpr uint32_t kMaxAllocatableDimension =
      kIsPlatform64b ? std::numeric_limits<typeof(avifImage::width)>::max()
                     : 134217728;  // Up to 1 GB total for YUVA

  // 8 bits
  TestAllocation(kMaxAllocatableDimension, 1, 8, AVIF_RESULT_OK);
  TestAllocation(1, kMaxAllocatableDimension, 8, AVIF_RESULT_OK);
  // 12 bits (impacts the width because avifImage stride is stored as uint32_t)
  TestAllocation(kMaxAllocatableDimension / 2, 1, 12, AVIF_RESULT_OK);
  TestAllocation(1, kMaxAllocatableDimension, 12, AVIF_RESULT_OK);
  // Some high number of bytes that malloc() accepts to allocate.
  TestAllocation(1024 * 16, 1024 * 8, 12, AVIF_RESULT_OK);  // Up to 1 GB total
}

TEST(AllocationTest, MinimumInvalidDimensions) {
  TestAllocation(std::numeric_limits<typeof(avifImage::width)>::max(), 1, 12,
                 AVIF_RESULT_INVALID_ARGUMENT);
}

TEST(AllocationTest, MaximumInvalidDimensions) {
  TestAllocation(std::numeric_limits<typeof(avifImage::width)>::max(),
                 std::numeric_limits<typeof(avifImage::height)>::max(), 12,
                 AVIF_RESULT_INVALID_ARGUMENT);
}

// This is valid in theory but malloc() should refuse to allocate so much and
// avifAlloc() aborts on malloc() failure.
TEST(DISABLED_AllocationTest, OutOfMemory) {
  TestAllocation(std::numeric_limits<typeof(avifImage::width)>::max() / 2,
                 std::numeric_limits<typeof(avifImage::height)>::max(), 12,
                 AVIF_RESULT_OUT_OF_MEMORY);
}

void TestEncoding(uint32_t width, uint32_t height, uint32_t depth,
                  avifResult expected_result) {
  testutil::AvifImagePtr image(avifImageCreateEmpty(), avifImageDestroy);
  ASSERT_NE(image, nullptr);
  image->width = width;
  image->height = height;
  image->depth = depth;
  image->yuvFormat = AVIF_PIXEL_FORMAT_YUV444;

  // This is a fairly high number of bytes that can safely be allocated in this
  // test. The goal is to have something to give to libavif but libavif should
  // return an error before attempting to read all of it, so it does not matter
  // if there are fewer bytes than the provided image dimensions.
  static constexpr uint64_t kMaxAlloc = 1073741824;
  uint32_t row_bytes;
  size_t num_allocated_bytes;
  if ((uint64_t)image->width * image->height >
      kMaxAlloc / (avifImageUsesU16(image.get()) ? 2 : 1)) {
    row_bytes = 1024;  // Does not matter much.
    num_allocated_bytes = kMaxAlloc;
  } else {
    row_bytes = image->width * (avifImageUsesU16(image.get()) ? 2 : 1);
    num_allocated_bytes = row_bytes * image->height;
  }

  // Initialize pixels as 16b values to make sure values are valid for 10
  // and 12-bit depths. The array will be cast to uint8_t for 8-bit depth.
  std::vector<uint16_t> pixels(num_allocated_bytes / sizeof(uint16_t), 400);
  uint8_t* bytes = reinterpret_cast<uint8_t*>(pixels.data());
  // Avoid avifImageAllocatePlanes() to exercise the checks at encoding.
  image->imageOwnsYUVPlanes = AVIF_FALSE;
  image->imageOwnsAlphaPlane = AVIF_FALSE;
  image->yuvRowBytes[AVIF_CHAN_Y] = row_bytes;
  image->yuvPlanes[AVIF_CHAN_Y] = bytes;
  image->yuvRowBytes[AVIF_CHAN_U] = row_bytes;
  image->yuvPlanes[AVIF_CHAN_U] = bytes;
  image->yuvRowBytes[AVIF_CHAN_V] = row_bytes;
  image->yuvPlanes[AVIF_CHAN_V] = bytes;
  image->alphaRowBytes = row_bytes;
  image->alphaPlane = bytes;

  // Try to encode.
  testutil::AvifEncoderPtr encoder(avifEncoderCreate(), avifEncoderDestroy);
  ASSERT_NE(encoder, nullptr);
  encoder->speed = AVIF_SPEED_FASTEST;
  testutil::AvifRwData encoded_avif;
  ASSERT_EQ(avifEncoderWrite(encoder.get(), image.get(), &encoded_avif),
            expected_result);
}

TEST(EncodingTest, MinimumValidDimensions) {
  TestAllocation(1, 1, 8, AVIF_RESULT_OK);
}

TEST(EncodingTest, MaximumValidDimensions) {
  // 65536 is the maximum AV1 frame dimension allowed by the AV1 specification.
  // See the section 5.5.1. General sequence header OBU syntax.
  // Old versions of libaom are capped to 65535 (http://crbug.com/aomedia/3304).
  TestEncoding(65535, 1, 12, AVIF_RESULT_OK);
  TestEncoding(1, 65535, 12, AVIF_RESULT_OK);
  // TestEncoding(65536, 65536, 12, AVIF_RESULT_OK);  // Too slow.
}

TEST(EncodingTest, MinimumInvalidDimensions) {
  TestEncoding(0, 1, 8, AVIF_RESULT_NO_CONTENT);
  TestEncoding(1, 0, 8, AVIF_RESULT_NO_CONTENT);
  TestEncoding(1, 1, 0, AVIF_RESULT_UNSUPPORTED_DEPTH);
  TestEncoding(65536 + 1, 1, 8, AVIF_RESULT_ENCODE_COLOR_FAILED);
  TestEncoding(1, 65536 + 1, 8, AVIF_RESULT_ENCODE_COLOR_FAILED);
  TestEncoding(65536 + 1, 65536 + 1, 8, AVIF_RESULT_ENCODE_COLOR_FAILED);
}

TEST(EncodingTest, MaximumInvalidDimensions) {
  TestEncoding(std::numeric_limits<typeof(avifImage::width)>::max(), 1, 8,
               AVIF_RESULT_ENCODE_COLOR_FAILED);
  TestEncoding(1, std::numeric_limits<typeof(avifImage::height)>::max(), 8,
               AVIF_RESULT_ENCODE_COLOR_FAILED);
  TestEncoding(std::numeric_limits<typeof(avifImage::width)>::max(),
               std::numeric_limits<typeof(avifImage::height)>::max(), 12,
               AVIF_RESULT_ENCODE_COLOR_FAILED);
  TestEncoding(1, 1, std::numeric_limits<typeof(avifImage::depth)>::max(),
               AVIF_RESULT_UNSUPPORTED_DEPTH);
}

}  // namespace
}  // namespace libavif
