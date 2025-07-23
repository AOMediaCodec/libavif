// Copyright 2025 Google LLC
// SPDX-License-Identifier: BSD-2-Clause

#include <vector>

#include "avif/avif.h"
#include "aviftest_helpers.h"
#include "gtest/gtest.h"

namespace avif {
namespace {

// Used to pass the data folder path to the GoogleTest suites.
const char* data_path = nullptr;

//------------------------------------------------------------------------------

TEST(AomTuneMetricTest, GenerateDifferentBitstreams) {
  if (avifCodecName(AVIF_CODEC_CHOICE_AOM, AVIF_CODEC_FLAG_CAN_ENCODE) ==
          nullptr ||
      !testutil::Av1DecoderAvailable()) {
    GTEST_SKIP() << "Codec unavailable, skip test.";
  }

  const ImagePtr image =
      testutil::ReadImage(data_path, "paris_exif_xmp_icc.jpg");
  ASSERT_NE(image, nullptr);
  // Speed up the test.
  image->width = 64;
  image->height = 64;

  std::vector<std::vector<uint8_t>> encoded_bitstreams;
  for (const char* tune : {"psnr", "ssim", "iq"}) {
    EncoderPtr encoder(avifEncoderCreate());
    ASSERT_NE(encoder, nullptr);
    encoder->codecChoice = AVIF_CODEC_CHOICE_AOM;
    ASSERT_EQ(avifEncoderSetCodecSpecificOption(encoder.get(), "tune", tune),
              AVIF_RESULT_OK);
    testutil::AvifRwData encoded;
    avifResult result = avifEncoderWrite(encoder.get(), image.get(), &encoded);
    if (std::string(tune) == "iq" &&
        result == AVIF_RESULT_INVALID_CODEC_SPECIFIC_OPTION) {
      // The aom version that libavif was built with likely does not support
      // AOM_TUNE_IQ yet.
      continue;
    }
    ASSERT_EQ(result, AVIF_RESULT_OK);
    for (const std::vector<uint8_t>& encoded_with_another_tune :
         encoded_bitstreams) {
      const bool is_same = encoded.size == encoded_with_another_tune.size() &&
                           std::equal(encoded.data, encoded.data + encoded.size,
                                      encoded_with_another_tune.data());
      ASSERT_FALSE(is_same);
    }

    DecoderPtr decoder(avifDecoderCreate());
    ASSERT_NE(decoder, nullptr);
    ImagePtr decoded(avifImageCreateEmpty());
    ASSERT_NE(decoded, nullptr);
    ASSERT_EQ(avifDecoderReadMemory(decoder.get(), decoded.get(), encoded.data,
                                    encoded.size),
              AVIF_RESULT_OK);
    ASSERT_GT(testutil::GetPsnr(*image, *decoded), 32.0);

    encoded_bitstreams.emplace_back(encoded.data, encoded.data + encoded.size);
  }
}

TEST(AomSharpnessTest, GenerateDifferentBitstreams) {
  if (avifCodecName(AVIF_CODEC_CHOICE_AOM, AVIF_CODEC_FLAG_CAN_ENCODE) ==
          nullptr ||
      !testutil::Av1DecoderAvailable()) {
    GTEST_SKIP() << "Codec unavailable, skip test.";
  }

  const ImagePtr image =
      testutil::ReadImage(data_path, "paris_exif_xmp_icc.jpg");
  ASSERT_NE(image, nullptr);
  // Speed up the test.
  image->width = 64;
  image->height = 64;

  std::vector<std::vector<uint8_t>> encoded_bitstreams;
  for (const char* sharpness : {"0", "2"}) {
    EncoderPtr encoder(avifEncoderCreate());
    ASSERT_NE(encoder, nullptr);
    encoder->codecChoice = AVIF_CODEC_CHOICE_AOM;
    ASSERT_EQ(avifEncoderSetCodecSpecificOption(encoder.get(), "sharpness",
                                                sharpness),
              AVIF_RESULT_OK);
    testutil::AvifRwData encoded;
    ASSERT_EQ(avifEncoderWrite(encoder.get(), image.get(), &encoded),
              AVIF_RESULT_OK);
    for (const std::vector<uint8_t>& encoded_with_another_tune :
         encoded_bitstreams) {
      const bool is_same = encoded.size == encoded_with_another_tune.size() &&
                           std::equal(encoded.data, encoded.data + encoded.size,
                                      encoded_with_another_tune.data());
      ASSERT_FALSE(is_same);
    }

    DecoderPtr decoder(avifDecoderCreate());
    ASSERT_NE(decoder, nullptr);
    ImagePtr decoded(avifImageCreateEmpty());
    ASSERT_NE(decoded, nullptr);
    ASSERT_EQ(avifDecoderReadMemory(decoder.get(), decoded.get(), encoded.data,
                                    encoded.size),
              AVIF_RESULT_OK);
    ASSERT_GT(testutil::GetPsnr(*image, *decoded), 32.0);

    encoded_bitstreams.emplace_back(encoded.data, encoded.data + encoded.size);
  }
}

//------------------------------------------------------------------------------

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
