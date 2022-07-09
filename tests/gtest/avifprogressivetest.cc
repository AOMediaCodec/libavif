// Copyright 2022 Google LLC. All rights reserved.
// SPDX-License-Identifier: BSD-2-Clause

#include <array>

#include "avif/avif.h"
#include "aviftest_helpers.h"
#include "gtest/gtest.h"

using ::testing::Combine;
using ::testing::Values;
using ::testing::ValuesIn;

namespace libavif {
namespace {

class ProgressiveTest : public testing::Test {};
}  // namespace

TEST(ProgressiveTest, EncodeDecode) {
  if (avifCodecName(AVIF_CODEC_CHOICE_AOM, AVIF_CODEC_FLAG_CAN_ENCODE) ==
      nullptr) {
    GTEST_SKIP_("ProgressiveTest requires AOM encoder.");
  }

  testutil::AvifImagePtr image = testutil::CreateImage(
      512, 512, 8, AVIF_PIXEL_FORMAT_YUV444, AVIF_PLANES_YUV, AVIF_RANGE_FULL);
  ASSERT_NE(image, nullptr);
  testutil::FillImageGradient(image.get());

  // Encode
  testutil::AvifEncoderPtr encoder(avifEncoderCreate(), avifEncoderDestroy);
  ASSERT_NE(encoder, nullptr);
  encoder->codecChoice = AVIF_CODEC_CHOICE_AOM;
  encoder->speed = AVIF_SPEED_FASTEST;
  encoder->extraLayerCount = 1;
  encoder->layers[0] = {50, 50, {1, 4}, {1, 4}};
  encoder->layers[1] = {25, 25, {1, 1}, {1, 1}};
  std::array<avifImage*, 2> layer_image_ptrs = {image.get(), image.get()};
  ASSERT_EQ(
      avifEncoderAddImageProgressive(encoder.get(), layer_image_ptrs.data(),
                                     AVIF_ADD_IMAGE_FLAG_SINGLE),
      AVIF_RESULT_OK);
  testutil::AvifRwData encodedAvif;
  ASSERT_EQ(avifEncoderFinish(encoder.get(), &encodedAvif), AVIF_RESULT_OK);

  // Decode
  ASSERT_NE(image, nullptr);
  testutil::AvifDecoderPtr decoder(avifDecoderCreate(), avifDecoderDestroy);
  ASSERT_NE(decoder, nullptr);
  decoder->allowProgressive = true;
  ASSERT_EQ(
      avifDecoderSetIOMemory(decoder.get(), encodedAvif.data, encodedAvif.size),
      AVIF_RESULT_OK);
  ASSERT_EQ(avifDecoderParse(decoder.get()), AVIF_RESULT_OK);
  ASSERT_EQ(decoder->progressiveState, AVIF_PROGRESSIVE_STATE_ACTIVE);
  ASSERT_EQ(avifDecoderNextImage(decoder.get()), AVIF_RESULT_OK);
  // Check decoder->image
  ASSERT_EQ(avifDecoderNextImage(decoder.get()), AVIF_RESULT_OK);
  // Check decoder->image
}
}  // namespace libavif