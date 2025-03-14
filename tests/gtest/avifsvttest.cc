// Copyright 2025 Google LLC
// SPDX-License-Identifier: BSD-2-Clause

#include <cstdlib>
#include <tuple>
#include <vector>

#include "avif/avif.h"
#include "avif/internal.h"
#include "aviftest_helpers.h"
#include "gtest/gtest.h"

namespace avif {
namespace {

bool SvtAv1SupportsLossless() {
  const char* version = avifCodecVersionSvt();  // "vX.Y.Z" expected.
  if (version == nullptr || version[0] != 'v') return false;
  const int major_version = std::atoi(version + 1);
  // EbSvtAv1EncConfiguration::lossless was introduced in SVT-AV1 version 3.0.0.
  return major_version >= 3;
}

class SvtAv1Test : public testing::TestWithParam<std::tuple</*quality=*/int>> {
};

TEST_P(SvtAv1Test, EncodeDecodeStillImage) {
  const int quality = std::get<0>(GetParam());

  ASSERT_NE(avifCodecName(AVIF_CODEC_CHOICE_SVT, AVIF_CODEC_FLAG_CAN_ENCODE),
            nullptr);
  if (!testutil::Av1DecoderAvailable()) {
    GTEST_SKIP() << "Decoder unavailable, skip test.";
  }

  // AVIF_CODEC_CHOICE_SVT requires dimensions to be at least 64 pixels.
  ImagePtr image =
      testutil::CreateImage(/*width=*/64, /*height=*/64, /*depth=*/8,
                            AVIF_PIXEL_FORMAT_YUV420, AVIF_PLANES_YUV);
  ASSERT_NE(image, nullptr);
  testutil::FillImageGradient(image.get());

  EncoderPtr encoder(avifEncoderCreate());
  ASSERT_NE(encoder, nullptr);
  encoder->codecChoice = AVIF_CODEC_CHOICE_SVT;
  encoder->quality = quality;
  encoder->qualityAlpha = quality;
  testutil::AvifRwData encoded;
  ASSERT_EQ(avifEncoderWrite(encoder.get(), image.get(), &encoded),
            AVIF_RESULT_OK);

  DecoderPtr decoder(avifDecoderCreate());
  ASSERT_NE(decoder, nullptr);
  ImagePtr decoded(avifImageCreateEmpty());
  ASSERT_NE(decoded, nullptr);
  ASSERT_EQ(avifDecoderReadMemory(decoder.get(), decoded.get(), encoded.data,
                                  encoded.size),
            AVIF_RESULT_OK);

  if (quality == AVIF_QUALITY_LOSSLESS && SvtAv1SupportsLossless()) {
    EXPECT_TRUE(testutil::AreImagesEqual(*image, *decoded));
  } else {
    EXPECT_GT(testutil::GetPsnr(*image, *decoded), 20.0);
  }
}

TEST_P(SvtAv1Test, EncodeDecodeSequence) {
  const int quality = std::get<0>(GetParam());

  ASSERT_NE(avifCodecName(AVIF_CODEC_CHOICE_SVT, AVIF_CODEC_FLAG_CAN_ENCODE),
            nullptr);
  if (!testutil::Av1DecoderAvailable()) {
    GTEST_SKIP() << "Decoder unavailable, skip test.";
  }

  std::vector<ImagePtr> sequence;
  sequence.reserve(3);
  for (uint32_t i = 0; i < sequence.capacity(); ++i) {
    // AVIF_CODEC_CHOICE_SVT requires dimensions to be at least 64 pixels.
    sequence.push_back(
        testutil::CreateImage(/*width=*/64, /*height=*/64, /*depth=*/8,
                              AVIF_PIXEL_FORMAT_YUV420, AVIF_PLANES_ALL));
    ASSERT_NE(sequence.back(), nullptr);
    // Generate different frames.
    testutil::FillImageGradient(sequence.back().get(),
                                /*offset=*/static_cast<int>(i) * 100);
  }

  EncoderPtr encoder(avifEncoderCreate());
  ASSERT_NE(encoder, nullptr);
  encoder->codecChoice = AVIF_CODEC_CHOICE_SVT;
  encoder->quality = quality;
  encoder->qualityAlpha = quality;
  for (const ImagePtr& image : sequence) {
    ASSERT_EQ(avifEncoderAddImage(encoder.get(), image.get(),
                                  /*durationInTimescales=*/1,
                                  AVIF_ADD_IMAGE_FLAG_NONE),
              AVIF_RESULT_OK);
  }
  testutil::AvifRwData encoded;
  ASSERT_EQ(avifEncoderFinish(encoder.get(), &encoded), AVIF_RESULT_OK);

  DecoderPtr decoder(avifDecoderCreate());
  ASSERT_NE(decoder, nullptr);
  ASSERT_EQ(avifDecoderSetIOMemory(decoder.get(), encoded.data, encoded.size),
            AVIF_RESULT_OK);
  ASSERT_EQ(avifDecoderParse(decoder.get()), AVIF_RESULT_OK);
  for (const ImagePtr& image : sequence) {
    ASSERT_EQ(avifDecoderNextImage(decoder.get()), AVIF_RESULT_OK);
    if (quality == AVIF_QUALITY_LOSSLESS && SvtAv1SupportsLossless()) {
      EXPECT_TRUE(testutil::AreImagesEqual(*image, *decoder->image));
    } else {
      EXPECT_GT(testutil::GetPsnr(*image, *decoder->image), 20.0);
    }
  }
}

INSTANTIATE_TEST_SUITE_P(All, SvtAv1Test,
                         testing::Combine(testing::Values(
                             AVIF_QUALITY_DEFAULT, AVIF_QUALITY_WORST,
                             AVIF_QUALITY_BEST - 1, AVIF_QUALITY_LOSSLESS)));

}  // namespace
}  // namespace avif
