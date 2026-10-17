// Copyright 2023 Google LLC
// SPDX-License-Identifier: BSD-2-Clause

#include <algorithm>
#include <cstdint>
#include <memory>

#include "avif/avif.h"
#include "avif/internal.h"
#include "aviftest_helpers.h"
#include "gtest/gtest.h"

namespace avif {
namespace {

class CodecTest : public testing::TestWithParam<
                      std::tuple</*encoding_codec=*/avifCodecChoice,
                                 /*decoding_codec=*/avifCodecChoice>> {};

TEST_P(CodecTest, EncodeDecode) {
  const avifCodecChoice encoding_codec = std::get<0>(GetParam());
  const avifCodecChoice decoding_codec = std::get<1>(GetParam());

  if (avifCodecName(encoding_codec, AVIF_CODEC_FLAG_CAN_ENCODE) == nullptr ||
      avifCodecName(decoding_codec, AVIF_CODEC_FLAG_CAN_DECODE) == nullptr) {
    GTEST_SKIP() << "Codec unavailable, skip test.";
  }

  // AVIF_CODEC_CHOICE_SVT requires dimensions to be at least 64 pixels.
  ImagePtr image =
      testutil::CreateImage(/*width=*/64, /*height=*/64, /*depth=*/8,
                            AVIF_PIXEL_FORMAT_YUV420, AVIF_PLANES_ALL);
  ASSERT_NE(image, nullptr);
  testutil::FillImageGradient(image.get());

  EncoderPtr encoder(avifEncoderCreate());
  ASSERT_NE(encoder, nullptr);
  encoder->codecChoice = encoding_codec;
  encoder->quality = encoder->qualityAlpha = 90;  // Small loss.
  testutil::AvifRwData encoded;
  ASSERT_EQ(avifEncoderWrite(encoder.get(), image.get(), &encoded),
            AVIF_RESULT_OK);

  DecoderPtr decoder(avifDecoderCreate());
  ASSERT_NE(decoder, nullptr);
  decoder->codecChoice = decoding_codec;
  ImagePtr decoded(avifImageCreateEmpty());
  ASSERT_NE(decoded, nullptr);
  ASSERT_EQ(avifDecoderReadMemory(decoder.get(), decoded.get(), encoded.data,
                                  encoded.size),
            AVIF_RESULT_OK);

  ASSERT_GT(testutil::GetPsnr(*image, *decoded), 32.0);
}

INSTANTIATE_TEST_SUITE_P(
    All, CodecTest,
    testing::Combine(/*encoding_codec=*/testing::Values(AVIF_CODEC_CHOICE_AOM,
                                                        AVIF_CODEC_CHOICE_RAV1E,
                                                        AVIF_CODEC_CHOICE_SVT),
                     /*decoding_codec=*/testing::Values(
                         AVIF_CODEC_CHOICE_AOM, AVIF_CODEC_CHOICE_DAV1D,
                         AVIF_CODEC_CHOICE_LIBGAV1)));

class CodecGetNextImageTest : public testing::TestWithParam<avifCodecChoice> {};

TEST_P(CodecGetNextImageTest, AlphaSampleWithoutFrameIsRejected) {
  const avifCodecChoice decoding_codec = GetParam();
  if (!testutil::Av1EncoderAvailable() ||
      avifCodecName(decoding_codec, AVIF_CODEC_FLAG_CAN_DECODE) == nullptr) {
    GTEST_SKIP() << "Codec unavailable, skip test.";
  }

  // Encode a still image and take its AV1 payload from the 'mdat' box.
  // AVIF_CODEC_CHOICE_SVT requires dimensions to be at least 64 pixels.
  ImagePtr image =
      testutil::CreateImage(/*width=*/64, /*height=*/64, /*depth=*/8,
                            AVIF_PIXEL_FORMAT_YUV420, AVIF_PLANES_YUV);
  ASSERT_NE(image, nullptr);
  testutil::FillImageGradient(image.get());
  EncoderPtr encoder(avifEncoderCreate());
  ASSERT_NE(encoder, nullptr);
  testutil::AvifRwData encoded;
  ASSERT_EQ(avifEncoderWrite(encoder.get(), image.get(), &encoded),
            AVIF_RESULT_OK);
  const uint8_t* kMdat = reinterpret_cast<const uint8_t*>("mdat");
  const uint8_t* mdat_type =
      std::search(encoded.data, encoded.data + encoded.size, kMdat, kMdat + 4);
  ASSERT_NE(mdat_type, encoded.data + encoded.size);
  ASSERT_GE(mdat_type, encoded.data + 4);
  const uint32_t mdat_size =
      (uint32_t{mdat_type[-4]} << 24) | (uint32_t{mdat_type[-3]} << 16) |
      (uint32_t{mdat_type[-2]} << 8) | uint32_t{mdat_type[-1]};
  ASSERT_GT(mdat_size, 8u);
  ASSERT_LE(mdat_type - 4 + mdat_size, encoded.data + encoded.size);

  avifCodec* codec_ptr = nullptr;
  ASSERT_EQ(
      avifCodecCreate(decoding_codec, AVIF_CODEC_FLAG_CAN_DECODE, &codec_ptr),
      AVIF_RESULT_OK);
  std::unique_ptr<avifCodec, decltype(&avifCodecDestroy)> codec(
      codec_ptr, avifCodecDestroy);
  avifDiagnostics diag;
  avifDiagnosticsClearError(&diag);
  codec->diag = &diag;
  codec->maxThreads = 1;
  codec->imageSizeLimit = AVIF_DEFAULT_IMAGE_SIZE_LIMIT;
  codec->imageDimensionLimit = AVIF_DEFAULT_IMAGE_DIMENSION_LIMIT;

  avifDecodeSample sample = {};
  sample.data.data = mdat_type + 4;
  sample.data.size = mdat_size - 8;
  sample.size = sample.data.size;
  sample.spatialID = AVIF_SPATIAL_ID_UNSET;
  sample.sync = AVIF_TRUE;
  ImagePtr decoded(avifImageCreateEmpty());
  ASSERT_NE(decoded, nullptr);
  avifBool is_limited_range_alpha = AVIF_FALSE;
  ASSERT_TRUE(codec->getNextImage(codec.get(), &sample, /*alpha=*/AVIF_TRUE,
                                  &is_limited_range_alpha, decoded.get()));
  ASSERT_NE(decoded->alphaPlane, nullptr);

  // A temporal unit made of a temporal delimiter OBU only outputs no frame.
  const uint8_t kTemporalDelimiter[] = {0x12, 0x00};
  sample.data.data = kTemporalDelimiter;
  sample.data.size = sizeof(kTemporalDelimiter);
  sample.size = sample.data.size;
  EXPECT_FALSE(codec->getNextImage(codec.get(), &sample, /*alpha=*/AVIF_TRUE,
                                   &is_limited_range_alpha, decoded.get()));
}

INSTANTIATE_TEST_SUITE_P(All, CodecGetNextImageTest,
                         testing::Values(AVIF_CODEC_CHOICE_AOM,
                                         AVIF_CODEC_CHOICE_DAV1D,
                                         AVIF_CODEC_CHOICE_LIBGAV1));

}  // namespace
}  // namespace avif
