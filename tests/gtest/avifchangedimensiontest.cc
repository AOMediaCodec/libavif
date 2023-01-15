// Copyright 2022 Yuan Tong. All rights reserved.
// SPDX-License-Identifier: BSD-2-Clause

#include <map>
#include <string>

#include "avif/avif.h"
#include "aviftest_helpers.h"
#include "gtest/gtest.h"

using testing::Bool;
using testing::Combine;
using testing::Values;

namespace libavif {
namespace {

class ChangeDimensionTest
    : public testing::TestWithParam<std::tuple<
          /*speed=*/int, /*depth=*/int, /*maxThreads*/ int, /*tiling*/ bool,
          /*end_usage=*/std::string, /*tune=*/std::string, /*denoise=*/bool>> {
};

TEST_P(ChangeDimensionTest, EncodeDecode) {
  if (avifCodecName(AVIF_CODEC_CHOICE_AOM, AVIF_CODEC_FLAG_CAN_ENCODE) ==
      nullptr) {
    GTEST_SKIP() << "Codec unavailable, skip test.";
  }

  const int speed = std::get<0>(GetParam());
  const int depth = std::get<1>(GetParam());
  const int maxThreads = std::get<2>(GetParam());
  const bool tiling = std::get<3>(GetParam());
  const std::string end_usage = std::get<4>(GetParam());
  const std::string tune = std::get<5>(GetParam());
  const bool denoise = std::get<6>(GetParam());

  uint32_t size_small = 64;
  uint32_t size_display = 128;
  if (maxThreads > 1) {
    size_small = 512;
    size_display = 768;
  }

  testutil::AvifImagePtr first = testutil::CreateImage(
      int(size_small), int(size_small), depth, AVIF_PIXEL_FORMAT_YUV420,
      AVIF_PLANES_YUV, AVIF_RANGE_FULL);
  ASSERT_NE(first, nullptr);
  testutil::FillImageGradient(first.get());

  testutil::AvifImagePtr second = testutil::CreateImage(
      int(size_display), int(size_display), depth, AVIF_PIXEL_FORMAT_YUV420,
      AVIF_PLANES_YUV, AVIF_RANGE_FULL);
  ASSERT_NE(second, nullptr);
  testutil::FillImageGradient(second.get());

  testutil::AvifRwData encodedAvif;

  // Encode
  {
    testutil::AvifEncoderPtr encoder(avifEncoderCreate(), avifEncoderDestroy);
    ASSERT_NE(encoder, nullptr);
    encoder->codecChoice = AVIF_CODEC_CHOICE_AOM;
    encoder->speed = speed;
    encoder->maxThreads = maxThreads;
    encoder->timescale = 1;
    encoder->minQuantizer = 20;
    encoder->maxQuantizer = 40;
    encoder->tileRowsLog2 = (tiling ? 1 : 0);
    encoder->width = size_display;
    encoder->height = size_display;

    avifEncoderSetCodecSpecificOption(encoder.get(), "end-usage",
                                      end_usage.c_str());
    if (end_usage == "q") {
      avifEncoderSetCodecSpecificOption(encoder.get(), "cq-level", "30");
    }
    avifEncoderSetCodecSpecificOption(encoder.get(), "tune", tune.c_str());
    if (denoise) {
      avifEncoderSetCodecSpecificOption(encoder.get(), "denoise-noise-level",
                                        "25");
    }

    ASSERT_EQ(avifEncoderAddImage(encoder.get(), first.get(), 1, 0),
              AVIF_RESULT_OK)
        << encoder->diag.error;

    ASSERT_EQ(avifEncoderAddImage(encoder.get(), second.get(), 1, 0),
              AVIF_RESULT_OK)
        << encoder->diag.error;

    ASSERT_EQ(avifEncoderFinish(encoder.get(), &encodedAvif), AVIF_RESULT_OK)
        << encoder->diag.error;
  }

  // Decode
  {
    testutil::AvifDecoderPtr decoder(avifDecoderCreate(), avifDecoderDestroy);
    ASSERT_NE(decoder, nullptr);

    avifDecoderSetIOMemory(decoder.get(), encodedAvif.data, encodedAvif.size);
    ASSERT_EQ(avifDecoderParse(decoder.get()), AVIF_RESULT_OK)
        << decoder->diag.error;
    ASSERT_EQ(avifDecoderNextImage(decoder.get()), AVIF_RESULT_OK)
        << decoder->diag.error;
    // libavif scales frames automatically.
    ASSERT_EQ(decoder->image->width, size_display);
    ASSERT_EQ(decoder->image->height, size_display);
    ASSERT_EQ(avifDecoderNextImage(decoder.get()), AVIF_RESULT_OK)
        << decoder->diag.error;
    ASSERT_EQ(decoder->image->width, size_display);
    ASSERT_EQ(decoder->image->height, size_display);
  }
}

INSTANTIATE_TEST_SUITE_P(
    AOM, ChangeDimensionTest,
    Combine(/*speed=*/Values(6, 10),  // Test both GOOD_QUALITY and REALTIME
            /*depth=*/Values(8, 10),
            /*maxThreads*/ Values(1),
            /*tiling*/ Bool(),
            /*end_usage=*/Values("q", "cbr"),
            /*tune=*/Values("ssim", "psnr"),
            /*denoise=*/Bool()));

INSTANTIATE_TEST_SUITE_P(
    AOMMultiThread, ChangeDimensionTest,
    Combine(/*speed=*/Values(6, 10),  // Test both GOOD_QUALITY and REALTIME
            /*depth=*/Values(8, 10),
            /*maxThreads*/ Values(8),
            /*tiling*/ Values(true),
            /*end_usage=*/Values("q"),
            /*tune=*/Values("ssim"),
            /*denoise=*/Values(true)));

}  // namespace
}  // namespace libavif
