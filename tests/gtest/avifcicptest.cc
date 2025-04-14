// Copyright 2025 Google LLC
// SPDX-License-Identifier: BSD-2-Clause

#include <tuple>

#include "avif/avif.h"
#include "aviftest_helpers.h"
#include "gtest/gtest.h"

namespace avif {
namespace {

class CicpTest
    : public testing::TestWithParam<std::tuple<
          avifCodecChoice, avifColorPrimaries, avifTransferCharacteristics,
          avifMatrixCoefficients, avifPixelFormat, avifPlanesFlag, avifRange>> {
};

TEST_P(CicpTest, EncodeDecode) {
  const avifCodecChoice codec_choice = std::get<0>(GetParam());
  if (avifCodecName(codec_choice, AVIF_CODEC_FLAG_CAN_ENCODE) == nullptr) {
    GTEST_SKIP() << "Codec unavailable, skip test.";
  }
  const avifColorPrimaries cp = std::get<1>(GetParam());
  const avifTransferCharacteristics tc = std::get<2>(GetParam());
  const avifMatrixCoefficients mc = std::get<3>(GetParam());
  const avifPixelFormat subsampling = std::get<4>(GetParam());
  const avifPlanesFlag planes = std::get<5>(GetParam());
  const avifRange range = std::get<6>(GetParam());

  ImagePtr image =
      testutil::CreateImage(32, 32, /*depth=*/8, subsampling, planes, range);
  ASSERT_NE(image, nullptr);
  testutil::FillImageGradient(image.get());
  image->colorPrimaries = cp;
  image->transferCharacteristics = tc;
  image->matrixCoefficients = mc;

  EncoderPtr encoder(avifEncoderCreate());
  ASSERT_NE(encoder, nullptr);
  encoder->codecChoice = codec_choice;
  encoder->speed = AVIF_SPEED_FASTEST;
  testutil::AvifRwData encoded;
  ASSERT_EQ(avifEncoderWrite(encoder.get(), image.get(), &encoded),
            AVIF_RESULT_OK);

  if (testutil::Av1DecoderAvailable()) {
    ImagePtr decoded(avifImageCreateEmpty());
    ASSERT_NE(decoded, nullptr);
    DecoderPtr decoder(avifDecoderCreate());
    ASSERT_NE(decoder, nullptr);
    ASSERT_EQ(avifDecoderReadMemory(decoder.get(), decoded.get(), encoded.data,
                                    encoded.size),
              AVIF_RESULT_OK);
    EXPECT_EQ(decoded->colorPrimaries, cp);
    EXPECT_EQ(decoded->transferCharacteristics, tc);
    EXPECT_EQ(decoded->matrixCoefficients, mc);
    EXPECT_EQ(decoded->yuvRange, range);
  }
}

INSTANTIATE_TEST_SUITE_P(
    Reserved0Identity, CicpTest,
    // Identity MC require 4:4:4 and AVIF_CODEC_CHOICE_SVT only supports 4:2:0.
    testing::Combine(
        testing::Values(AVIF_CODEC_CHOICE_AOM, AVIF_CODEC_CHOICE_RAV1E),
        testing::Values(static_cast<avifColorPrimaries>(
            AVIF_COLOR_PRIMARIES_UNKNOWN)),  // Reserved CICP value.
        testing::Values(static_cast<avifTransferCharacteristics>(
            AVIF_TRANSFER_CHARACTERISTICS_UNKNOWN)),  // Reserved CICP value.
        testing::Values(static_cast<avifMatrixCoefficients>(
            AVIF_MATRIX_COEFFICIENTS_IDENTITY)),
        testing::Values(AVIF_PIXEL_FORMAT_YUV444),
        testing::Values(AVIF_PLANES_YUV, AVIF_PLANES_ALL),
        testing::Values(AVIF_RANGE_LIMITED, AVIF_RANGE_FULL)));

INSTANTIATE_TEST_SUITE_P(
    Unspecified, CicpTest,
    testing::Combine(testing::Values(AVIF_CODEC_CHOICE_AOM,
                                     AVIF_CODEC_CHOICE_RAV1E,
                                     AVIF_CODEC_CHOICE_SVT),
                     testing::Values(static_cast<avifColorPrimaries>(
                         AVIF_COLOR_PRIMARIES_UNSPECIFIED)),
                     testing::Values(static_cast<avifTransferCharacteristics>(
                         AVIF_TRANSFER_CHARACTERISTICS_UNSPECIFIED)),
                     testing::Values(static_cast<avifMatrixCoefficients>(
                         AVIF_MATRIX_COEFFICIENTS_UNSPECIFIED)),
                     testing::Values(AVIF_PIXEL_FORMAT_YUV420),
                     testing::Values(AVIF_PLANES_YUV, AVIF_PLANES_ALL),
                     testing::Values(AVIF_RANGE_LIMITED, AVIF_RANGE_FULL)));

INSTANTIATE_TEST_SUITE_P(
    SrgbBt601, CicpTest,
    testing::Combine(testing::Values(AVIF_CODEC_CHOICE_AOM,
                                     AVIF_CODEC_CHOICE_RAV1E,
                                     AVIF_CODEC_CHOICE_SVT),
                     testing::Values(static_cast<avifColorPrimaries>(
                         AVIF_COLOR_PRIMARIES_SRGB)),
                     testing::Values(static_cast<avifTransferCharacteristics>(
                         AVIF_TRANSFER_CHARACTERISTICS_SRGB)),
                     testing::Values(static_cast<avifMatrixCoefficients>(
                         AVIF_MATRIX_COEFFICIENTS_BT601)),
                     testing::Values(AVIF_PIXEL_FORMAT_YUV420),
                     testing::Values(AVIF_PLANES_YUV, AVIF_PLANES_ALL),
                     testing::Values(AVIF_RANGE_LIMITED, AVIF_RANGE_FULL)));

}  // namespace
}  // namespace avif
