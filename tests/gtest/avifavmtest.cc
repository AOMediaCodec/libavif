// Copyright 2023 Google LLC
// SPDX-License-Identifier: BSD-2-Clause

#include "avif/avif.h"
#include "aviftest_helpers.h"
#include "gtest/gtest.h"

namespace libavif {
namespace {

TEST(AvmTest, EncodeDecode) {
  testutil::AvifImagePtr image =
      testutil::CreateImage(/*width=*/12, /*height=*/34, /*depth=*/8,
                            AVIF_PIXEL_FORMAT_YUV420, AVIF_PLANES_ALL);
  ASSERT_NE(image, nullptr);
  testutil::FillImageGradient(image.get());

  testutil::AvifEncoderPtr encoder(avifEncoderCreate(), avifEncoderDestroy);
  ASSERT_NE(encoder, nullptr);
  encoder->codecChoice = AVIF_CODEC_CHOICE_AVM;
  testutil::AvifRwData encoded;
  ASSERT_EQ(avifEncoderWrite(encoder.get(), image.get(), &encoded),
            AVIF_RESULT_OK);

  testutil::AvifImagePtr decoded(avifImageCreateEmpty(), avifImageDestroy);
  ASSERT_NE(decoded, nullptr);
  testutil::AvifDecoderPtr decoder(avifDecoderCreate(), avifDecoderDestroy);
  ASSERT_NE(decoder, nullptr);
  // No need to set AVIF_CODEC_CHOICE_AVM. The decoder should recognize AV2.
  ASSERT_EQ(avifDecoderReadMemory(decoder.get(), decoded.get(), encoded.data,
                                  encoded.size),
            AVIF_RESULT_OK);

  // Verify that the input and decoded images are close.
  EXPECT_GT(testutil::GetPsnr(*image, *decoded), 40.0);

  // Forcing an AV1 decoding codec should fail.
  for (avifCodecChoice av1_codec :
       {AVIF_CODEC_CHOICE_AOM, AVIF_CODEC_CHOICE_DAV1D,
        AVIF_CODEC_CHOICE_LIBGAV1}) {
    decoder->codecChoice = av1_codec;
    // AVIF_RESULT_NO_CODEC_AVAILABLE is expected because av1_codec is not
    // enabled or because we are trying to decode an AV2 file with an AV1 codec.
    ASSERT_EQ(avifDecoderReadMemory(decoder.get(), decoded.get(), encoded.data,
                                    encoded.size),
              AVIF_RESULT_NO_CODEC_AVAILABLE);
  }
}

TEST(AvmTest, Av1StillWorksWhenAvmIsEnabled) {
  const char* encoding_codec =
      avifCodecName(AVIF_CODEC_CHOICE_AUTO, AVIF_CODEC_FLAG_CAN_ENCODE);
  const char* decoding_codec =
      avifCodecName(AVIF_CODEC_CHOICE_AUTO, AVIF_CODEC_FLAG_CAN_DECODE);
  if (encoding_codec == nullptr || std::string(encoding_codec) == "avm" ||
      decoding_codec == nullptr || std::string(decoding_codec) == "avm") {
    GTEST_SKIP() << "AV1 codec unavailable, skip test.";
  }
  // avm is the only AV2 codec, so the default codec will be an AV1 one.

  testutil::AvifImagePtr image =
      testutil::CreateImage(/*width=*/64, /*height=*/64, /*depth=*/8,
                            AVIF_PIXEL_FORMAT_YUV420, AVIF_PLANES_ALL);
  ASSERT_NE(image, nullptr);
  testutil::FillImageGradient(image.get());

  testutil::AvifEncoderPtr encoder(avifEncoderCreate(), avifEncoderDestroy);
  ASSERT_NE(encoder, nullptr);
  testutil::AvifRwData encoded;
  ASSERT_EQ(avifEncoderWrite(encoder.get(), image.get(), &encoded),
            AVIF_RESULT_OK);

  testutil::AvifImagePtr decoded(avifImageCreateEmpty(), avifImageDestroy);
  ASSERT_NE(decoded, nullptr);
  testutil::AvifDecoderPtr decoder(avifDecoderCreate(), avifDecoderDestroy);
  ASSERT_NE(decoder, nullptr);
  ASSERT_EQ(avifDecoderReadMemory(decoder.get(), decoded.get(), encoded.data,
                                  encoded.size),
            AVIF_RESULT_OK);

  // Verify that the input and decoded images are close.
  EXPECT_GT(testutil::GetPsnr(*image, *decoded), 40.0);

  // Forcing an AV2 decoding codec should fail.
  decoder->codecChoice = AVIF_CODEC_CHOICE_AVM;
  ASSERT_EQ(avifDecoderReadMemory(decoder.get(), decoded.get(), encoded.data,
                                  encoded.size),
            AVIF_RESULT_NO_CODEC_AVAILABLE);
}

}  // namespace
}  // namespace libavif
