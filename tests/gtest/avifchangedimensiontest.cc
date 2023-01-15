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
          /*size_first*/ uint32_t, /*size_second*/ uint32_t, /*speed=*/int,
          /*depth=*/int, /*maxThreads*/ int, /*tiling*/ bool,
          /*end_usage=*/std::string, /*tune=*/std::string, /*denoise=*/bool>> {
};

TEST_P(ChangeDimensionTest, EncodeDecode) {
  if (avifCodecName(AVIF_CODEC_CHOICE_AOM, AVIF_CODEC_FLAG_CAN_ENCODE) ==
      nullptr) {
    GTEST_SKIP() << "Codec unavailable, skip test.";
  }

  const uint32_t size_first = std::get<0>(GetParam());
  const uint32_t size_second = std::get<1>(GetParam());
  const int speed = std::get<2>(GetParam());
  const int depth = std::get<3>(GetParam());
  const int maxThreads = std::get<4>(GetParam());
  const bool tiling = std::get<5>(GetParam());
  const std::string end_usage = std::get<6>(GetParam());
  const std::string tune = std::get<7>(GetParam());
  const bool denoise = std::get<8>(GetParam());

  char versionBuffer[256];
  avifCodecVersions(versionBuffer);
  bool will_fail = (versionBuffer < std::string("v3.6.0")) &&
                   ((size_first < size_second) || (depth > 8));

  uint32_t size_display = std::max(size_first, size_second);

  testutil::AvifImagePtr first = testutil::CreateImage(
      int(size_first), int(size_first), depth, AVIF_PIXEL_FORMAT_YUV420,
      AVIF_PLANES_YUV, AVIF_RANGE_FULL);
  ASSERT_NE(first, nullptr);
  testutil::FillImageGradient(first.get());

  testutil::AvifImagePtr second = testutil::CreateImage(
      int(size_second), int(size_second), depth, AVIF_PIXEL_FORMAT_YUV420,
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
              AVIF_RESULT_OK);

    if (will_fail) {
      ASSERT_EQ(avifEncoderAddImage(encoder.get(), second.get(), 1, 0),
                AVIF_RESULT_INCOMPATIBLE_IMAGE);
      return;
    }

    ASSERT_EQ(avifEncoderAddImage(encoder.get(), second.get(), 1, 0),
              AVIF_RESULT_OK);

    ASSERT_EQ(avifEncoderFinish(encoder.get(), &encodedAvif), AVIF_RESULT_OK);
  }

  // Decode
  {
    testutil::AvifDecoderPtr decoder(avifDecoderCreate(), avifDecoderDestroy);
    ASSERT_NE(decoder, nullptr);

    avifDecoderSetIOMemory(decoder.get(), encodedAvif.data, encodedAvif.size);
    ASSERT_EQ(avifDecoderParse(decoder.get()), AVIF_RESULT_OK);
    ASSERT_EQ(avifDecoderNextImage(decoder.get()), AVIF_RESULT_OK);
    // libavif scales frames automatically.
    ASSERT_EQ(decoder->image->width, size_display);
    ASSERT_EQ(decoder->image->height, size_display);
    ASSERT_EQ(avifDecoderNextImage(decoder.get()), AVIF_RESULT_OK);
    ASSERT_EQ(decoder->image->width, size_display);
    ASSERT_EQ(decoder->image->height, size_display);
  }
}

INSTANTIATE_TEST_SUITE_P(AOMDecreasing, ChangeDimensionTest,
                         Combine(/*size_first*/ Values(128),
                                 /*size_second*/ Values(64),
                                 /*speed=*/Values(6, 10),
                                 /*depth=*/Values(8, 10),
                                 /*maxThreads*/ Values(1),
                                 /*tiling*/ Bool(),
                                 /*end_usage=*/Values("q", "cbr"),
                                 /*tune=*/Values("ssim", "psnr"),
                                 /*denoise=*/Bool()));

INSTANTIATE_TEST_SUITE_P(AOMIncreasing, ChangeDimensionTest,
                         Combine(/*size_first*/ Values(64),
                                 /*size_second*/ Values(128),
                                 /*speed=*/Values(6, 10),
                                 /*depth=*/Values(8, 10),
                                 /*maxThreads*/ Values(1),
                                 /*tiling*/ Bool(),
                                 /*end_usage=*/Values("q", "cbr"),
                                 /*tune=*/Values("ssim", "psnr"),
                                 /*denoise=*/Bool()));

INSTANTIATE_TEST_SUITE_P(AOMIncreasingMultiThread, ChangeDimensionTest,
                         Combine(/*size_first*/ Values(512),
                                 /*size_second*/ Values(768),
                                 /*speed=*/Values(6, 10),
                                 /*depth=*/Values(8, 10),
                                 /*maxThreads*/ Values(8),
                                 /*tiling*/ Values(true),
                                 /*end_usage=*/Values("q"),
                                 /*tune=*/Values("ssim"),
                                 /*denoise=*/Values(true)));

}  // namespace
}  // namespace libavif
