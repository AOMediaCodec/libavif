// Copyright 2022 Yuan Tong. All rights reserved.
// SPDX-License-Identifier: BSD-2-Clause

#include "avif/avif.h"
#include "aviftest_helpers.h"
#include "gtest/gtest.h"

namespace libavif {
namespace {

TEST(ChangeSettingTest, AOM) {
  if (avifCodecName(AVIF_CODEC_CHOICE_AOM, AVIF_CODEC_FLAG_CAN_ENCODE) ==
      nullptr) {
    GTEST_SKIP() << "AOM encoder unavailable, skip test.";
  }

  const uint32_t image_size = 512;
  testutil::AvifImagePtr image =
      testutil::CreateImage(image_size, image_size, 8, AVIF_PIXEL_FORMAT_YUV444,
                            AVIF_PLANES_YUV, AVIF_RANGE_FULL);
  ASSERT_NE(image, nullptr);
  testutil::FillImageGradient(image.get());

  // Encode
  testutil::AvifEncoderPtr encoder(avifEncoderCreate(), avifEncoderDestroy);
  ASSERT_NE(encoder, nullptr);
  encoder->codecChoice = AVIF_CODEC_CHOICE_AOM;
  encoder->speed = AVIF_SPEED_FASTEST;
  encoder->minQuantizer = 63;
  encoder->maxQuantizer = 63;
  encoder->timescale = 1;
  // Force cbr mode, to ensure quality settings is fully complied.
  avifEncoderSetCodecSpecificOption(encoder.get(), "end-usage", "cbr");

  ASSERT_EQ(avifEncoderAddImage(encoder.get(), image.get(), 1,
                                AVIF_ADD_IMAGE_FLAG_FORCE_KEYFRAME),
            AVIF_RESULT_OK);

  encoder->minQuantizer = 0;
  encoder->maxQuantizer = 0;
  ASSERT_EQ(avifEncoderAddImage(encoder.get(), image.get(), 1,
                                AVIF_ADD_IMAGE_FLAG_UPDATE_SETTINGS |
                                    AVIF_ADD_IMAGE_FLAG_FORCE_KEYFRAME),
            AVIF_RESULT_OK);

  testutil::AvifRwData encodedAvif;
  ASSERT_EQ(avifEncoderFinish(encoder.get(), &encodedAvif), AVIF_RESULT_OK);

  // Decode
  testutil::AvifDecoderPtr decoder(avifDecoderCreate(), avifDecoderDestroy);
  ASSERT_NE(decoder, nullptr);

  // The second frame is set to have far better quality,
  // and should be much bigger, so small amount of data at beginning
  // should be enough to decode the first frame.
  auto io = testutil::AvifIOCreateLimitedReader(
      avifIOCreateMemoryReader(encodedAvif.data, encodedAvif.size),
      encodedAvif.size / 10);
  ASSERT_NE(io, nullptr);
  avifDecoderSetIO(decoder.get(), io);
  ASSERT_EQ(avifDecoderParse(decoder.get()), AVIF_RESULT_OK);
  ASSERT_EQ(avifDecoderNextImage(decoder.get()), AVIF_RESULT_OK);
  ASSERT_EQ(avifDecoderNextImage(decoder.get()), AVIF_RESULT_WAITING_ON_IO);
  ((testutil::AvifIOLimitedReader*)io)->clamp =
      testutil::AvifIOLimitedReader::NoClamp;
  ASSERT_EQ(avifDecoderNextImage(decoder.get()), AVIF_RESULT_OK);
  ASSERT_EQ(avifDecoderNextImage(decoder.get()),
            AVIF_RESULT_NO_IMAGES_REMAINING);
}

TEST(ChangeSettingTest, RAV1E) {
  if (avifCodecName(AVIF_CODEC_CHOICE_RAV1E, AVIF_CODEC_FLAG_CAN_ENCODE) ==
      nullptr) {
    GTEST_SKIP() << "rav1e encoder unavailable, skip test.";
  }

  const uint32_t image_size = 512;
  testutil::AvifImagePtr image =
      testutil::CreateImage(image_size, image_size, 8, AVIF_PIXEL_FORMAT_YUV444,
                            AVIF_PLANES_YUV, AVIF_RANGE_FULL);
  ASSERT_NE(image, nullptr);
  testutil::FillImageGradient(image.get());

  // Encode
  testutil::AvifEncoderPtr encoder(avifEncoderCreate(), avifEncoderDestroy);
  ASSERT_NE(encoder, nullptr);
  encoder->codecChoice = AVIF_CODEC_CHOICE_RAV1E;
  encoder->speed = AVIF_SPEED_FASTEST;
  encoder->minQuantizer = 63;
  encoder->maxQuantizer = 63;
  encoder->timescale = 1;

  ASSERT_EQ(avifEncoderAddImage(encoder.get(), image.get(), 1,
                                AVIF_ADD_IMAGE_FLAG_FORCE_KEYFRAME),
            AVIF_RESULT_OK);

  encoder->minQuantizer = 0;
  encoder->maxQuantizer = 0;

  // rav1e does not support updating settings.
  ASSERT_EQ(avifEncoderAddImage(encoder.get(), image.get(), 1,
                                AVIF_ADD_IMAGE_FLAG_UPDATE_SETTINGS |
                                    AVIF_ADD_IMAGE_FLAG_FORCE_KEYFRAME),
            AVIF_RESULT_NOT_IMPLEMENTED);
}

TEST(ChangeSettingTest, SVT) {
  if (avifCodecName(AVIF_CODEC_CHOICE_SVT, AVIF_CODEC_FLAG_CAN_ENCODE) ==
      nullptr) {
    GTEST_SKIP() << "SVT encoder unavailable, skip test.";
  }

  const uint32_t image_size = 512;
  testutil::AvifImagePtr image =
      testutil::CreateImage(image_size, image_size, 8, AVIF_PIXEL_FORMAT_YUV420,
                            AVIF_PLANES_YUV, AVIF_RANGE_FULL);
  ASSERT_NE(image, nullptr);
  testutil::FillImageGradient(image.get());

  // Encode
  testutil::AvifEncoderPtr encoder(avifEncoderCreate(), avifEncoderDestroy);
  ASSERT_NE(encoder, nullptr);
  encoder->codecChoice = AVIF_CODEC_CHOICE_SVT;
  encoder->speed = AVIF_SPEED_FASTEST;
  encoder->minQuantizer = 63;
  encoder->maxQuantizer = 63;
  encoder->timescale = 1;

  ASSERT_EQ(avifEncoderAddImage(encoder.get(), image.get(), 1,
                                AVIF_ADD_IMAGE_FLAG_FORCE_KEYFRAME),
            AVIF_RESULT_OK);

  encoder->minQuantizer = 0;
  encoder->maxQuantizer = 0;

  // SVT does not support updating settings.
  ASSERT_EQ(avifEncoderAddImage(encoder.get(), image.get(), 1,
                                AVIF_ADD_IMAGE_FLAG_UPDATE_SETTINGS |
                                    AVIF_ADD_IMAGE_FLAG_FORCE_KEYFRAME),
            AVIF_RESULT_NOT_IMPLEMENTED);
}

}  // namespace
}  // namespace libavif
