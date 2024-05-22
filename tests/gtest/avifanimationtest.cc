// Copyright 2023 Google LLC
// SPDX-License-Identifier: BSD-2-Clause

#include <algorithm>

#include "avif/avif.h"
#include "aviftest_helpers.h"
#include "gtest/gtest.h"

namespace avif {
namespace {

// Used to pass the data folder path to the GoogleTest suites.
const char* data_path = nullptr;

TEST(AvifDecodeTest, AnimatedImage) {
  if (!testutil::Av1DecoderAvailable()) {
    GTEST_SKIP() << "AV1 Codec unavailable, skip test.";
  }
  const char* file_name = "colors-animated-8bpc.avif";
  DecoderPtr decoder(avifDecoderCreate());
  ASSERT_NE(decoder, nullptr);
  ASSERT_EQ(avifDecoderSetIOFile(decoder.get(),
                                 (std::string(data_path) + file_name).c_str()),
            AVIF_RESULT_OK);
  ASSERT_EQ(avifDecoderParse(decoder.get()), AVIF_RESULT_OK);
  EXPECT_EQ(decoder->alphaPresent, AVIF_FALSE);
  EXPECT_EQ(decoder->imageSequenceTrackPresent, AVIF_TRUE);
  EXPECT_EQ(decoder->imageCount, 5);
  EXPECT_EQ(decoder->repetitionCount, 0);
  for (int i = 0; i < 5; ++i) {
    EXPECT_EQ(avifDecoderIsKeyframe(decoder.get(), i), i == 0);
    EXPECT_EQ(avifDecoderNearestKeyframe(decoder.get(), i), 0);
  }
  for (int i = 0; i < 5; ++i) {
    EXPECT_EQ(avifDecoderNextImage(decoder.get()), AVIF_RESULT_OK);
  }
}

TEST(AvifDecodeTest, AnimatedImageWithSourceSetToPrimaryItem) {
  if (!testutil::Av1DecoderAvailable()) {
    GTEST_SKIP() << "AV1 Codec unavailable, skip test.";
  }
  const char* file_name = "colors-animated-8bpc.avif";
  DecoderPtr decoder(avifDecoderCreate());
  ASSERT_NE(decoder, nullptr);
  ASSERT_EQ(avifDecoderSetIOFile(decoder.get(),
                                 (std::string(data_path) + file_name).c_str()),
            AVIF_RESULT_OK);
  ASSERT_EQ(
      avifDecoderSetSource(decoder.get(), AVIF_DECODER_SOURCE_PRIMARY_ITEM),
      AVIF_RESULT_OK);
  ASSERT_EQ(avifDecoderParse(decoder.get()), AVIF_RESULT_OK);
  EXPECT_EQ(decoder->alphaPresent, AVIF_FALSE);
  EXPECT_EQ(decoder->imageSequenceTrackPresent, AVIF_TRUE);
  // imageCount is expected to be 1 because we are using primary item as the
  // preferred source.
  EXPECT_EQ(decoder->imageCount, 1);
  EXPECT_EQ(decoder->repetitionCount, 0);
  // Get the first (and only) image.
  EXPECT_EQ(avifDecoderNextImage(decoder.get()), AVIF_RESULT_OK);
  // Subsequent calls should not return AVIF_RESULT_OK since there is only one
  // image in the preferred source.
  EXPECT_NE(avifDecoderNextImage(decoder.get()), AVIF_RESULT_OK);
}

TEST(AvifDecodeTest, AnimatedImageWithAlphaAndMetadata) {
  const char* file_name = "colors-animated-8bpc-alpha-exif-xmp.avif";
  DecoderPtr decoder(avifDecoderCreate());
  ASSERT_NE(decoder, nullptr);
  ASSERT_EQ(avifDecoderSetIOFile(decoder.get(),
                                 (std::string(data_path) + file_name).c_str()),
            AVIF_RESULT_OK);
  ASSERT_EQ(avifDecoderParse(decoder.get()), AVIF_RESULT_OK);
  EXPECT_EQ(decoder->alphaPresent, AVIF_TRUE);
  EXPECT_EQ(decoder->imageSequenceTrackPresent, AVIF_TRUE);
  EXPECT_EQ(decoder->imageCount, 5);
  EXPECT_EQ(decoder->repetitionCount, AVIF_REPETITION_COUNT_INFINITE);
  EXPECT_EQ(decoder->image->exif.size, 1126);
  EXPECT_EQ(decoder->image->xmp.size, 3898);
}

TEST(AvifDecodeTest, AnimatedImageWithoutTracksShouldFail) {
  testutil::AvifRwData avif =
      testutil::ReadFile(std::string(data_path) + "colors-animated-8bpc.avif");
  // Edit the file to replace 'trak' box with a 'free' box. This way the file
  // will not contain any 'trak' boxes.
  const uint8_t* kTrak = reinterpret_cast<const uint8_t*>("trak");
  uint8_t* trak_position =
      std::search(avif.data, avif.data + avif.size, kTrak, kTrak + 4);
  ASSERT_NE(trak_position, avif.data + avif.size);
  trak_position[0] = static_cast<uint8_t>('f');
  trak_position[1] = static_cast<uint8_t>('r');
  trak_position[2] = static_cast<uint8_t>('e');
  trak_position[3] = static_cast<uint8_t>('e');

  for (auto source :
       {AVIF_DECODER_SOURCE_PRIMARY_ITEM, AVIF_DECODER_SOURCE_TRACKS}) {
    DecoderPtr decoder(avifDecoderCreate());
    ASSERT_NE(decoder, nullptr);
    ASSERT_EQ(avifDecoderSetIOMemory(decoder.get(), avif.data, avif.size),
              AVIF_RESULT_OK);
    ASSERT_EQ(avifDecoderSetSource(decoder.get(), source), AVIF_RESULT_OK);
    ASSERT_EQ(avifDecoderParse(decoder.get()), AVIF_RESULT_BMFF_PARSE_FAILED);
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
