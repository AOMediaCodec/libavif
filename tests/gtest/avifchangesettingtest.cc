// Copyright 2022 Yuan Tong. All rights reserved.
// SPDX-License-Identifier: BSD-2-Clause

#include <map>
#include <string>

#include "avif/avif.h"
#include "aviftest_helpers.h"
#include "gtest/gtest.h"

namespace avif {
namespace {

// Used to pass the data folder path to the GoogleTest suites.
const char* data_path = nullptr;

//------------------------------------------------------------------------------

void TestEncodeDecode(avifCodecChoice codec,
                      const std::map<std::string, std::string>& init_cs_options,
                      bool can_encode, bool use_cq) {
  if (avifCodecName(codec, AVIF_CODEC_FLAG_CAN_ENCODE) == nullptr) {
    GTEST_SKIP() << "Codec unavailable, skip test.";
  }

  constexpr uint32_t kImageSize = 512;
  ImagePtr image = testutil::CreateImage(kImageSize, kImageSize, /*depth=*/8,
                                         AVIF_PIXEL_FORMAT_YUV420,
                                         AVIF_PLANES_YUV, AVIF_RANGE_FULL);
  ASSERT_NE(image, nullptr);
  testutil::FillImageGradient(image.get());

  // Encode
  EncoderPtr encoder(avifEncoderCreate());
  ASSERT_NE(encoder, nullptr);
  encoder->codecChoice = codec;
  encoder->speed = AVIF_SPEED_FASTEST;
  encoder->timescale = 1;

  for (const auto& option : init_cs_options) {
    ASSERT_EQ(avifEncoderSetCodecSpecificOption(
                  encoder.get(), option.first.c_str(), option.second.c_str()),
              AVIF_RESULT_OK);
  }

  if (use_cq) {
    encoder->minQuantizer = 0;
    encoder->maxQuantizer = 63;
    ASSERT_EQ(
        avifEncoderSetCodecSpecificOption(encoder.get(), "end-usage", "q"),
        AVIF_RESULT_OK);
    ASSERT_EQ(
        avifEncoderSetCodecSpecificOption(encoder.get(), "cq-level", "63"),
        AVIF_RESULT_OK);
  } else {
    encoder->minQuantizer = 63;
    encoder->maxQuantizer = 63;
  }

  ASSERT_EQ(avifEncoderAddImage(encoder.get(), image.get(), 1,
                                AVIF_ADD_IMAGE_FLAG_FORCE_KEYFRAME),
            AVIF_RESULT_OK);

  if (use_cq) {
    ASSERT_EQ(avifEncoderSetCodecSpecificOption(encoder.get(), "cq-level", "0"),
              AVIF_RESULT_OK);
  } else {
    encoder->minQuantizer = 0;
    encoder->maxQuantizer = 0;
  }

  if (!can_encode) {
    ASSERT_EQ(avifEncoderAddImage(encoder.get(), image.get(), 1,
                                  AVIF_ADD_IMAGE_FLAG_FORCE_KEYFRAME),
              AVIF_RESULT_NOT_IMPLEMENTED);

    return;
  }

  ASSERT_EQ(avifEncoderAddImage(encoder.get(), image.get(), 1,
                                AVIF_ADD_IMAGE_FLAG_FORCE_KEYFRAME),
            AVIF_RESULT_OK);

  testutil::AvifRwData encodedAvif;
  ASSERT_EQ(avifEncoderFinish(encoder.get(), &encodedAvif), AVIF_RESULT_OK);

  // Decode
  DecoderPtr decoder(avifDecoderCreate());
  ASSERT_NE(decoder, nullptr);

  // The second frame is set to have far better quality,
  // and should be much bigger, so small amount of data at beginning
  // should be enough to decode the first frame.
  avifIO* io = testutil::AvifIOCreateLimitedReader(
      avifIOCreateMemoryReader(encodedAvif.data, encodedAvif.size),
      encodedAvif.size / 10);
  ASSERT_NE(io, nullptr);
  avifDecoderSetIO(decoder.get(), io);
  ASSERT_EQ(avifDecoderParse(decoder.get()), AVIF_RESULT_OK);
  ASSERT_EQ(avifDecoderNextImage(decoder.get()), AVIF_RESULT_OK);
  ASSERT_EQ(avifDecoderNextImage(decoder.get()), AVIF_RESULT_WAITING_ON_IO);
  reinterpret_cast<testutil::AvifIOLimitedReader*>(io)->clamp =
      testutil::AvifIOLimitedReader::kNoClamp;
  ASSERT_EQ(avifDecoderNextImage(decoder.get()), AVIF_RESULT_OK);
  ASSERT_EQ(avifDecoderNextImage(decoder.get()),
            AVIF_RESULT_NO_IMAGES_REMAINING);
}

TEST(ChangeSettingTest, AOM) {
  // Test if changes to AV1 encode settings are detected.
  TestEncodeDecode(AVIF_CODEC_CHOICE_AOM, {{"end-usage", "cbr"}}, true, false);

  // Test if changes to codec specific options are detected.
  TestEncodeDecode(AVIF_CODEC_CHOICE_AOM, {}, true, true);
}

TEST(ChangeSettingTest, RAV1E) {
  TestEncodeDecode(AVIF_CODEC_CHOICE_RAV1E, {}, false, false);
}

TEST(ChangeSettingTest, SVT) {
  TestEncodeDecode(AVIF_CODEC_CHOICE_SVT, {}, false, false);
}

TEST(ChangeSettingTest, UnchangeableSetting) {
  if (avifCodecName(AVIF_CODEC_CHOICE_AOM, AVIF_CODEC_FLAG_CAN_ENCODE) ==
      nullptr) {
    GTEST_SKIP() << "Codec unavailable, skip test.";
  }

  constexpr uint32_t kImageSize = 512;
  ImagePtr image = testutil::CreateImage(kImageSize, kImageSize, /*depth=*/8,
                                         AVIF_PIXEL_FORMAT_YUV420,
                                         AVIF_PLANES_YUV, AVIF_RANGE_FULL);
  ASSERT_NE(image, nullptr);
  testutil::FillImageGradient(image.get());

  // Encode
  EncoderPtr encoder(avifEncoderCreate());
  ASSERT_NE(encoder, nullptr);
  encoder->codecChoice = AVIF_CODEC_CHOICE_AOM;
  encoder->speed = AVIF_SPEED_FASTEST;
  encoder->timescale = 1;
  ASSERT_EQ(encoder->repetitionCount, AVIF_REPETITION_COUNT_INFINITE);
  encoder->minQuantizer = 63;
  encoder->maxQuantizer = 63;

  ASSERT_EQ(avifEncoderAddImage(encoder.get(), image.get(), 1,
                                AVIF_ADD_IMAGE_FLAG_FORCE_KEYFRAME),
            AVIF_RESULT_OK);

  encoder->timescale = 2;
  ASSERT_EQ(avifEncoderAddImage(encoder.get(), image.get(), 1,
                                AVIF_ADD_IMAGE_FLAG_FORCE_KEYFRAME),
            AVIF_RESULT_CANNOT_CHANGE_SETTING);

  encoder->timescale = 1;
  ASSERT_EQ(avifEncoderAddImage(encoder.get(), image.get(), 1,
                                AVIF_ADD_IMAGE_FLAG_FORCE_KEYFRAME),
            AVIF_RESULT_OK);

  encoder->repetitionCount = 0;
  ASSERT_EQ(avifEncoderAddImage(encoder.get(), image.get(), 1,
                                AVIF_ADD_IMAGE_FLAG_FORCE_KEYFRAME),
            AVIF_RESULT_CANNOT_CHANGE_SETTING);
}

TEST(ChangeSettingTest, UnchangeableImageColorRange) {
  if (avifCodecName(AVIF_CODEC_CHOICE_AOM, AVIF_CODEC_FLAG_CAN_ENCODE) ==
      nullptr) {
    GTEST_SKIP() << "Codec unavailable, skip test.";
  }

  constexpr uint32_t kImageSize = 512;
  ImagePtr image = testutil::CreateImage(kImageSize, kImageSize, /*depth=*/8,
                                         AVIF_PIXEL_FORMAT_YUV420,
                                         AVIF_PLANES_YUV, AVIF_RANGE_FULL);
  ASSERT_NE(image, nullptr);
  const uint32_t yuva[] = {128, 128, 128, 255};
  testutil::FillImagePlain(image.get(), yuva);

  // Encode
  EncoderPtr encoder(avifEncoderCreate());
  ASSERT_NE(encoder, nullptr);
  encoder->codecChoice = AVIF_CODEC_CHOICE_AOM;
  encoder->speed = AVIF_SPEED_FASTEST;
  encoder->timescale = 1;
  ASSERT_EQ(encoder->repetitionCount, AVIF_REPETITION_COUNT_INFINITE);
  encoder->quality = AVIF_QUALITY_WORST;

  ASSERT_EQ(avifEncoderAddImage(encoder.get(), image.get(), 1,
                                AVIF_ADD_IMAGE_FLAG_NONE),
            AVIF_RESULT_OK);

  ASSERT_EQ(avifEncoderAddImage(encoder.get(), image.get(), 1,
                                AVIF_ADD_IMAGE_FLAG_NONE),
            AVIF_RESULT_OK);

  image->yuvRange = AVIF_RANGE_LIMITED;
  ASSERT_EQ(avifEncoderAddImage(encoder.get(), image.get(), 1,
                                AVIF_ADD_IMAGE_FLAG_NONE),
            AVIF_RESULT_INCOMPATIBLE_IMAGE);
}

TEST(ChangeSettingTest, UnchangeableImageChromaSamplePosition) {
  if (avifCodecName(AVIF_CODEC_CHOICE_AOM, AVIF_CODEC_FLAG_CAN_ENCODE) ==
      nullptr) {
    GTEST_SKIP() << "Codec unavailable, skip test.";
  }

  constexpr uint32_t kImageSize = 512;
  ImagePtr image = testutil::CreateImage(kImageSize, kImageSize, /*depth=*/8,
                                         AVIF_PIXEL_FORMAT_YUV420,
                                         AVIF_PLANES_YUV, AVIF_RANGE_FULL);
  ASSERT_NE(image, nullptr);
  const uint32_t yuva[] = {128, 128, 128, 255};
  testutil::FillImagePlain(image.get(), yuva);

  // Encode
  EncoderPtr encoder(avifEncoderCreate());
  ASSERT_NE(encoder, nullptr);
  encoder->codecChoice = AVIF_CODEC_CHOICE_AOM;
  encoder->speed = AVIF_SPEED_FASTEST;
  encoder->timescale = 1;
  ASSERT_EQ(encoder->repetitionCount, AVIF_REPETITION_COUNT_INFINITE);
  encoder->quality = AVIF_QUALITY_WORST;

  ASSERT_EQ(avifEncoderAddImage(encoder.get(), image.get(), 1,
                                AVIF_ADD_IMAGE_FLAG_NONE),
            AVIF_RESULT_OK);

  ASSERT_EQ(avifEncoderAddImage(encoder.get(), image.get(), 1,
                                AVIF_ADD_IMAGE_FLAG_NONE),
            AVIF_RESULT_OK);

  ASSERT_EQ(image->yuvChromaSamplePosition,
            AVIF_CHROMA_SAMPLE_POSITION_UNKNOWN);
  image->yuvChromaSamplePosition = AVIF_CHROMA_SAMPLE_POSITION_VERTICAL;
  ASSERT_EQ(avifEncoderAddImage(encoder.get(), image.get(), 1,
                                AVIF_ADD_IMAGE_FLAG_NONE),
            AVIF_RESULT_INCOMPATIBLE_IMAGE);
}

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
  constexpr uint64_t kDuration = 1;
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

TEST(ChangeSettingTest, SetCodecSpecificOptionWithNull) {
  if (avifCodecName(AVIF_CODEC_CHOICE_AOM, AVIF_CODEC_FLAG_CAN_ENCODE) ==
      nullptr) {
    GTEST_SKIP() << "Codec unavailable, skip test.";
  }
  std::vector<uint8_t> a, b;

  EncodeAnimation("sharpness", nullptr, nullptr, nullptr, a);
  EncodeAnimation("sharpness", nullptr, nullptr, nullptr, b);
  // Make sure the comparison works as intended for identical input.
  EXPECT_EQ(a, b);

  EncodeAnimation("sharpness", "7", nullptr, nullptr, a);
  EncodeAnimation("sharpness", nullptr, nullptr, nullptr, b);
  // 7 is not the default.
  EXPECT_NE(a, b);

  EncodeAnimation("sharpness", nullptr, nullptr, nullptr, a);
  EncodeAnimation("sharpness", nullptr, nullptr, "7", b);
  // The second frame differs.
  EXPECT_NE(a, b);

  EncodeAnimation("sharpness", "7", nullptr, "0", a);
  EncodeAnimation("sharpness", "7", nullptr, "5", b);
  // The second frame differs.
  EXPECT_NE(a, b);

  EncodeAnimation("sharpness", nullptr, "6", "7", a);
  EncodeAnimation("sharpness", nullptr, nullptr, "7", b);
  // The option is overwritten successfully.
  EXPECT_EQ(a, b);

  EncodeAnimation("sharpness", nullptr, nullptr, nullptr, a);
  EncodeAnimation("sharpness", nullptr, "7", nullptr, b);
  // The pending key is successfully deleted.
  EXPECT_EQ(a, b);

  EncodeAnimation("sharpness", "7", "7", "7", a);
  EncodeAnimation("sharpness", "7", nullptr, nullptr, b);
  // avifEncoderSetCodecSpecificOption(NULL) only deletes the *pending* key.
  EXPECT_EQ(a, b);
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
