// Copyright 2022 Google LLC
// SPDX-License-Identifier: BSD-2-Clause

#include "avif/avif.h"
#include "aviftest_helpers.h"
#include "gtest/gtest.h"

namespace avif {
namespace {

TEST(ClliTest, Simple) {
  ImagePtr image =
      testutil::CreateImage(/*width=*/8, /*height=*/8, /*depth=*/8,
                            AVIF_PIXEL_FORMAT_YUV444, AVIF_PLANES_YUV);
  ASSERT_NE(image, nullptr);
  testutil::FillImageGradient(image.get());  // The pixels do not matter.

  for (int max_content_light_level : {0, 1, 65535}) {
    for (int max_pic_average_light_level : {0, 1, 65535}) {
      image->clli.maxCLL = static_cast<uint16_t>(max_content_light_level);
      image->clli.maxPALL = static_cast<uint16_t>(max_pic_average_light_level);

      const testutil::AvifRwData encoded = testutil::Encode(image.get());
      const ImagePtr decoded = testutil::Decode(encoded.data, encoded.size);
      ASSERT_NE(decoded, nullptr);
      ASSERT_EQ(decoded->clli.maxCLL, image->clli.maxCLL);
      ASSERT_EQ(decoded->clli.maxPALL, image->clli.maxPALL);
    }
  }
}

// The clli property of an image sequence is written in the sample entry of its
// track, where there is no property deduplication.
void EncodeDecodeImageSequence(uint16_t max_content_light_level,
                               uint16_t max_pic_average_light_level) {
  ImagePtr image =
      testutil::CreateImage(/*width=*/8, /*height=*/8, /*depth=*/8,
                            AVIF_PIXEL_FORMAT_YUV444, AVIF_PLANES_YUV);
  ASSERT_NE(image, nullptr);
  testutil::FillImageGradient(image.get());  // The pixels do not matter.
  image->clli.maxCLL = max_content_light_level;
  image->clli.maxPALL = max_pic_average_light_level;

  EncoderPtr encoder(avifEncoderCreate());
  ASSERT_NE(encoder, nullptr);
  encoder->speed = AVIF_SPEED_FASTEST;
  for (int i = 0; i < 2; ++i) {
    ASSERT_EQ(avifEncoderAddImage(encoder.get(), image.get(),
                                  /*durationInTimescales=*/1,
                                  AVIF_ADD_IMAGE_FLAG_NONE),
              AVIF_RESULT_OK)
        << encoder->diag.error;
  }
  testutil::AvifRwData encoded;
  ASSERT_EQ(avifEncoderFinish(encoder.get(), &encoded), AVIF_RESULT_OK)
      << encoder->diag.error;

  DecoderPtr decoder(avifDecoderCreate());
  ASSERT_NE(decoder, nullptr);
  ASSERT_EQ(avifDecoderSetIOMemory(decoder.get(), encoded.data, encoded.size),
            AVIF_RESULT_OK);
  ASSERT_EQ(avifDecoderParse(decoder.get()), AVIF_RESULT_OK)
      << decoder->diag.error;
  EXPECT_EQ(decoder->imageSequenceTrackPresent, AVIF_TRUE);
  ASSERT_EQ(decoder->imageCount, 2);
  for (int i = 0; i < 2; ++i) {
    ASSERT_EQ(avifDecoderNextImage(decoder.get()), AVIF_RESULT_OK)
        << decoder->diag.error;
  }
  EXPECT_EQ(decoder->image->clli.maxCLL, image->clli.maxCLL);
  EXPECT_EQ(decoder->image->clli.maxPALL, image->clli.maxPALL);
}

// Control: no clli box is written.
TEST(ClliTest, ImageSequenceWithoutClliControl) {
  if (!testutil::Av1EncoderAvailable() || !testutil::Av1DecoderAvailable()) {
    GTEST_SKIP() << "AV1 codec unavailable, skip test.";
  }
  EncodeDecodeImageSequence(/*max_content_light_level=*/0,
                            /*max_pic_average_light_level=*/0);
}

TEST(ClliTest, ImageSequence) {
  if (!testutil::Av1EncoderAvailable() || !testutil::Av1DecoderAvailable()) {
    GTEST_SKIP() << "AV1 codec unavailable, skip test.";
  }
  EncodeDecodeImageSequence(/*max_content_light_level=*/1000,
                            /*max_pic_average_light_level=*/400);
}

}  // namespace
}  // namespace avif
