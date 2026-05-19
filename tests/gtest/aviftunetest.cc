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

constexpr uint64_t kDuration = 1;

void EncodeAnimation(const char* key, const char* value_before_first_frame,
                     const char* value_after_first_frame,
                     const char* value_before_second_frame,
                     std::vector<uint8_t>& encoded_bitstream) {
  // Generate an animation with two different frames.
  const ImagePtr first_frame =
      testutil::ReadImage(data_path, "paris_exif_xmp_icc.jpg");
  ASSERT_NE(first_frame, nullptr);
  // Speed up the test.
  first_frame->width = 64;
  first_frame->height = 64;
  const ImagePtr second_frame(avifImageCreateEmpty());
  ASSERT_NE(second_frame, nullptr);
  ASSERT_EQ(
      avifImageCopy(second_frame.get(), first_frame.get(), AVIF_PLANES_ALL),
      AVIF_RESULT_OK);
  testutil::FillImageGradient(first_frame.get());

  EncoderPtr encoder(avifEncoderCreate());
  ASSERT_NE(encoder, nullptr);
  encoder->codecChoice = AVIF_CODEC_CHOICE_AOM;
  encoder->creationTime = encoder->modificationTime = 1;  // Deterministic.
  const avifAddImageFlag flag = AVIF_ADD_IMAGE_FLAG_FORCE_KEYFRAME;

  // First frame.
  ASSERT_EQ(avifEncoderSetCodecSpecificOption(encoder.get(), key,
                                              value_before_first_frame),
            AVIF_RESULT_OK);
  ASSERT_EQ(
      avifEncoderAddImage(encoder.get(), first_frame.get(), kDuration, flag),
      AVIF_RESULT_OK);
  ASSERT_EQ(avifEncoderSetCodecSpecificOption(encoder.get(), key,
                                              value_after_first_frame),
            AVIF_RESULT_OK);

  // Second frame.
  ASSERT_EQ(avifEncoderSetCodecSpecificOption(encoder.get(), key,
                                              value_before_second_frame),
            AVIF_RESULT_OK);
  ASSERT_EQ(
      avifEncoderAddImage(encoder.get(), second_frame.get(), kDuration, flag),
      AVIF_RESULT_OK);

  testutil::AvifRwData encoded;
  ASSERT_EQ(avifEncoderFinish(encoder.get(), &encoded), AVIF_RESULT_OK);

  // Make sure it decodes fine, even if unrelated to the current test.
  DecoderPtr decoder(avifDecoderCreate());
  ASSERT_NE(decoder, nullptr);
  ASSERT_EQ(avifDecoderSetIOMemory(decoder.get(), encoded.data, encoded.size),
            AVIF_RESULT_OK);
  ASSERT_EQ(avifDecoderParse(decoder.get()), AVIF_RESULT_OK);
  ASSERT_EQ(avifDecoderNextImage(decoder.get()), AVIF_RESULT_OK);
  ASSERT_GT(testutil::GetPsnr(*first_frame, *decoder->image), 32.0);
  ASSERT_EQ(avifDecoderNextImage(decoder.get()), AVIF_RESULT_OK);
  ASSERT_GT(testutil::GetPsnr(*second_frame, *decoder->image), 32.0);

  encoded_bitstream = std::vector(encoded.data, encoded.data + encoded.size);
}

TEST(AomTuneMetricTest, TuneOptionHasSameBehaviorAsOtherCodecSpecificOptions) {
  if (avifCodecName(AVIF_CODEC_CHOICE_AOM, AVIF_CODEC_FLAG_CAN_ENCODE) ==
      nullptr) {
    GTEST_SKIP() << "Codec unavailable, skip test.";
  }
  std::vector<uint8_t> a, b;

  EncodeAnimation("tune", nullptr, nullptr, nullptr, a);
  EncodeAnimation("tune", nullptr, nullptr, nullptr, b);
  // Make sure the comparison works as intended for identical input.
  EXPECT_EQ(a, b);

  EncodeAnimation("tune", "psnr", nullptr, nullptr, a);
  EncodeAnimation("tune", nullptr, nullptr, nullptr, b);
  // AOM_TUNE_PSNR is not the default.
  EXPECT_NE(a, b);

  EncodeAnimation("tune", nullptr, nullptr, nullptr, a);
  EncodeAnimation("tune", nullptr, nullptr, "psnr", b);
  // The second frame differs.
  EXPECT_NE(a, b);

  EncodeAnimation("tune", nullptr, "ssim", "psnr", a);
  EncodeAnimation("tune", nullptr, nullptr, "psnr", b);
  // The option is overwritten successfully.
  EXPECT_EQ(a, b);

  EncodeAnimation("tune", nullptr, nullptr, nullptr, a);
  EncodeAnimation("tune", nullptr, "psnr", nullptr, b);
  // The pending key is successfully deleted.
  EXPECT_EQ(a, b);

  EncodeAnimation("tune", "psnr", "psnr", "psnr", a);
  EncodeAnimation("tune", "psnr", nullptr, nullptr, b);
  // avifEncoderSetCodecSpecificOption(NULL) only deletes the *pending* key.
  EXPECT_EQ(a, b);
}

TEST(AomTuneMetricTest, TuneIqOnlySupportsAllIntra) {
  if (avifCodecName(AVIF_CODEC_CHOICE_AOM, AVIF_CODEC_FLAG_CAN_ENCODE) ==
      nullptr) {
    GTEST_SKIP() << "Codec unavailable, skip test.";
  }

  const ImagePtr image =
      testutil::ReadImage(data_path, "paris_exif_xmp_icc.jpg");
  ASSERT_NE(image, nullptr);

  EncoderPtr encoder(avifEncoderCreate());
  ASSERT_NE(encoder, nullptr);
  encoder->codecChoice = AVIF_CODEC_CHOICE_AOM;
  ASSERT_EQ(avifEncoderSetCodecSpecificOption(encoder.get(), "tune", "iq"),
            AVIF_RESULT_OK);
  ASSERT_EQ(avifEncoderAddImage(encoder.get(), image.get(), kDuration,
                                AVIF_ADD_IMAGE_FLAG_NONE),
            AVIF_RESULT_INVALID_CODEC_SPECIFIC_OPTION);
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
