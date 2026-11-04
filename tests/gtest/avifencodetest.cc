// Copyright 2023 Google LLC
// SPDX-License-Identifier: BSD-2-Clause

#include <cstdint>
#include <iostream>
#include <limits>
#include <vector>

#include "avif/avif.h"
#include "aviftest_helpers.h"
#include "gtest/gtest.h"

namespace avif {
namespace {

// Used to pass the data folder path to the GoogleTest suites.
const char* data_path = nullptr;

//------------------------------------------------------------------------------

TEST(BasicTest, Encode) {
  ImagePtr image = testutil::ReadImage(data_path, "paris_exif_xmp_icc.jpg");
  ASSERT_NE(image, nullptr);

  EncoderPtr encoder(avifEncoderCreate());
  ASSERT_NE(encoder, nullptr);
  testutil::AvifRwData encoded;
  ASSERT_EQ(avifEncoderWrite(encoder.get(), image.get(), &encoded),
            AVIF_RESULT_OK);

  // Uncomment to save the generated file to disk.
  // std::ofstream(testing::TempDir() + "avifencodetest.avif", std::ios::binary)
  //     .write(reinterpret_cast<char*>(encoded.data), encoded.size);
}

//------------------------------------------------------------------------------

// Encodes an image sequence with one frame per duration.
avifResult EncodeSequence(uint64_t timescale,
                          const std::vector<uint64_t>& durations,
                          testutil::AvifRwData& encoded) {
  ImagePtr image = testutil::CreateImage(64, 64, 8, AVIF_PIXEL_FORMAT_YUV420,
                                         AVIF_PLANES_YUV, AVIF_RANGE_FULL);
  if (image == nullptr) return AVIF_RESULT_OUT_OF_MEMORY;
  testutil::FillImageGradient(image.get());

  EncoderPtr encoder(avifEncoderCreate());
  if (encoder == nullptr) return AVIF_RESULT_OUT_OF_MEMORY;
  encoder->speed = AVIF_SPEED_FASTEST;
  encoder->timescale = timescale;
  for (uint64_t duration : durations) {
    const avifResult result = avifEncoderAddImage(
        encoder.get(), image.get(), duration, AVIF_ADD_IMAGE_FLAG_NONE);
    if (result != AVIF_RESULT_OK) return result;
  }
  return avifEncoderFinish(encoder.get(), &encoded);
}

TEST(EncodeTest, SequenceTimingAtUint32Limits) {
  if (!testutil::Av1EncoderAvailable() || !testutil::Av1DecoderAvailable()) {
    GTEST_SKIP() << "AV1 Codec unavailable, skip test.";
  }
  constexpr uint64_t kMax = std::numeric_limits<uint32_t>::max();
  testutil::AvifRwData encoded;
  ASSERT_EQ(EncodeSequence(/*timescale=*/kMax, {kMax, 1}, encoded),
            AVIF_RESULT_OK);

  DecoderPtr decoder(avifDecoderCreate());
  ASSERT_NE(decoder, nullptr);
  ASSERT_EQ(avifDecoderSetIOMemory(decoder.get(), encoded.data, encoded.size),
            AVIF_RESULT_OK);
  ASSERT_EQ(avifDecoderParse(decoder.get()), AVIF_RESULT_OK);
  ASSERT_EQ(decoder->imageCount, 2);
  EXPECT_EQ(decoder->timescale, kMax);
  EXPECT_EQ(decoder->durationInTimescales, kMax + 1);
  avifImageTiming timing;
  ASSERT_EQ(avifDecoderNthImageTiming(decoder.get(), 0, &timing),
            AVIF_RESULT_OK);
  EXPECT_EQ(timing.ptsInTimescales, 0u);
  EXPECT_EQ(timing.durationInTimescales, kMax);
  ASSERT_EQ(avifDecoderNthImageTiming(decoder.get(), 1, &timing),
            AVIF_RESULT_OK);
  EXPECT_EQ(timing.ptsInTimescales, kMax);
  EXPECT_EQ(timing.durationInTimescales, 1u);
}

// The timescale and the frame durations of an image sequence are stored in
// 32-bit fields. They must not be silently truncated.
TEST(EncodeTest, SequenceTimescaleTooLarge) {
  if (!testutil::Av1EncoderAvailable()) {
    GTEST_SKIP() << "AV1 Codec unavailable, skip test.";
  }
  constexpr uint64_t kTooLarge = uint64_t{1} << 32;
  testutil::AvifRwData encoded;
  EXPECT_EQ(EncodeSequence(/*timescale=*/kTooLarge, {1, 1}, encoded),
            AVIF_RESULT_INVALID_ARGUMENT);
}

TEST(EncodeTest, SequenceFrameDurationTooLarge) {
  if (!testutil::Av1EncoderAvailable()) {
    GTEST_SKIP() << "AV1 Codec unavailable, skip test.";
  }
  constexpr uint64_t kTooLarge = uint64_t{1} << 32;
  testutil::AvifRwData encoded;
  EXPECT_EQ(EncodeSequence(/*timescale=*/1, {1, kTooLarge}, encoded),
            AVIF_RESULT_INVALID_ARGUMENT);
}

TEST(EncodeTest, SequenceTotalDurationOverflow) {
  if (!testutil::Av1EncoderAvailable()) {
    GTEST_SKIP() << "AV1 Codec unavailable, skip test.";
  }
  // The sum of these durations wraps around to 0 in 64 bits.
  constexpr uint64_t kHalf = uint64_t{1} << 63;
  testutil::AvifRwData encoded;
  EXPECT_EQ(EncodeSequence(/*timescale=*/1, {kHalf, kHalf}, encoded),
            AVIF_RESULT_INVALID_ARGUMENT);
}

TEST(EncodeTest, StillImageDurationIsIgnored) {
  if (!testutil::Av1EncoderAvailable()) {
    GTEST_SKIP() << "AV1 Codec unavailable, skip test.";
  }
  // The duration and the timescale are ignored for a still image.
  constexpr uint64_t kTooLarge = uint64_t{1} << 32;
  testutil::AvifRwData encoded;
  EXPECT_EQ(EncodeSequence(/*timescale=*/kTooLarge, {kTooLarge}, encoded),
            AVIF_RESULT_OK);
}

//------------------------------------------------------------------------------

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
