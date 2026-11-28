// Copyright 2026 Yuan Tong. All rights reserved.
// SPDX-License-Identifier: BSD-2-Clause

#include "avif/avif.h"
#include "avif/codec.h"
#include "aviftest_helpers.h"
#include "gtest/gtest.h"
#include "stub_codec.h"

namespace avif {
namespace {

// Custom codec registry is process global, so the stub codec is registered once
// for all tests.
class CustomCodecTest : public testing::Test {
 protected:
  static void SetUpTestSuite() {
    ASSERT_EQ(avifStubCodecRegister(&stub_choice_), AVIF_RESULT_OK);
  }

  static avifCodecChoice stub_choice_;
};

avifCodecChoice CustomCodecTest::stub_choice_ = AVIF_CODEC_CHOICE_AUTO;

const char* DummyVersion() { return "0"; }
avifCodec* DummyCreate() { return nullptr; }

TEST_F(CustomCodecTest, Register) {
  // The stub codec got a custom choice and can be looked up by name.
  EXPECT_GE(stub_choice_, AVIF_CODEC_CHOICE_CUSTOM_BASE);
  EXPECT_EQ(avifCodecChoiceFromName(AVIF_STUB_CODEC_NAME), stub_choice_);
  EXPECT_STREQ(avifCodecName(stub_choice_, AVIF_CODEC_FLAG_CAN_ENCODE),
               AVIF_STUB_CODEC_NAME);
  EXPECT_STREQ(avifCodecName(stub_choice_, AVIF_CODEC_FLAG_CAN_DECODE),
               AVIF_STUB_CODEC_NAME);

  avifCodecInformation info = {};
  info.type = AVIF_CODEC_TYPE_AV1;
  info.name = "encodeonly";
  info.version = DummyVersion;
  info.create = DummyCreate;
  info.flags = AVIF_CODEC_FLAG_CAN_ENCODE;

  // Missing fields.
  EXPECT_EQ(avifRegisterCustomCodec(nullptr), AVIF_RESULT_INVALID_ARGUMENT);
  avifCodecInformation invalid = info;
  invalid.name = nullptr;
  EXPECT_EQ(avifRegisterCustomCodec(&invalid), AVIF_RESULT_INVALID_ARGUMENT);
  invalid = info;
  invalid.version = nullptr;
  EXPECT_EQ(avifRegisterCustomCodec(&invalid), AVIF_RESULT_INVALID_ARGUMENT);
  invalid = info;
  invalid.create = nullptr;
  EXPECT_EQ(avifRegisterCustomCodec(&invalid), AVIF_RESULT_INVALID_ARGUMENT);

  // Duplicate names.
  invalid = info;
  invalid.name = AVIF_STUB_CODEC_NAME;
  EXPECT_EQ(avifRegisterCustomCodec(&invalid), AVIF_RESULT_INVALID_ARGUMENT);
  const char* built_in_name = avifCodecName(AVIF_CODEC_CHOICE_AUTO, 0);
  if (built_in_name != nullptr) {
    invalid.name = built_in_name;
    EXPECT_EQ(avifRegisterCustomCodec(&invalid), AVIF_RESULT_INVALID_ARGUMENT);
  }

  // The flags of a custom codec are honored.
  info.choice = AVIF_CODEC_CHOICE_AUTO;
  ASSERT_EQ(avifRegisterCustomCodec(&info), AVIF_RESULT_OK);
  EXPECT_GE(info.choice, AVIF_CODEC_CHOICE_CUSTOM_BASE);
  EXPECT_NE(info.choice, stub_choice_);
  EXPECT_EQ(avifCodecChoiceFromName("encodeonly"), info.choice);
  EXPECT_STREQ(avifCodecName(info.choice, AVIF_CODEC_FLAG_CAN_ENCODE),
               "encodeonly");
  EXPECT_EQ(avifCodecName(info.choice, AVIF_CODEC_FLAG_CAN_DECODE), nullptr);
}

TEST_F(CustomCodecTest, NotSelectedByAuto) {
  for (avifCodecFlags flags :
       {avifCodecFlags{0}, avifCodecFlags{AVIF_CODEC_FLAG_CAN_ENCODE},
        avifCodecFlags{AVIF_CODEC_FLAG_CAN_DECODE}}) {
    const char* name = avifCodecName(AVIF_CODEC_CHOICE_AUTO, flags);
    if (name != nullptr) {
      EXPECT_STRNE(name, AVIF_STUB_CODEC_NAME);
    }
  }
}

TEST_F(CustomCodecTest, EncodeDecode) {
  ImagePtr image = testutil::CreateImage(
      AVIF_STUB_CODEC_WIDTH, AVIF_STUB_CODEC_HEIGHT, AVIF_STUB_CODEC_DEPTH,
      AVIF_STUB_CODEC_FORMAT, AVIF_PLANES_YUV);
  ASSERT_NE(image, nullptr);
  for (int plane = AVIF_CHAN_Y; plane <= AVIF_CHAN_V; ++plane) {
    uint8_t* row = avifImagePlane(image.get(), plane);
    for (uint32_t y = 0; y < avifImagePlaneHeight(image.get(), plane); ++y) {
      for (uint32_t x = 0; x < avifImagePlaneWidth(image.get(), plane); ++x) {
        row[x] = avifStubCodecPixel(plane, x, y);
      }
      row += avifImagePlaneRowBytes(image.get(), plane);
    }
  }
  const avifStubCodecStats stats_before = avifStubCodecGetStats();

  EncoderPtr encoder(avifEncoderCreate());
  ASSERT_NE(encoder, nullptr);
  encoder->codecChoice = stub_choice_;
  testutil::AvifRwData encoded;
  ASSERT_EQ(avifEncoderWrite(encoder.get(), image.get(), &encoded),
            AVIF_RESULT_OK);
  EXPECT_EQ(avifStubCodecGetStats().encodedFrames,
            stats_before.encodedFrames + 1);

  DecoderPtr decoder(avifDecoderCreate());
  ASSERT_NE(decoder, nullptr);
  decoder->codecChoice = stub_choice_;
  ImagePtr decoded(avifImageCreateEmpty());
  ASSERT_NE(decoded, nullptr);
  ASSERT_EQ(avifDecoderReadMemory(decoder.get(), decoded.get(), encoded.data,
                                  encoded.size),
            AVIF_RESULT_OK);
  EXPECT_EQ(avifStubCodecGetStats().decodedFrames,
            stats_before.decodedFrames + 1);
  EXPECT_TRUE(testutil::AreImagesEqual(*image, *decoded));
}

}  // namespace
}  // namespace avif
