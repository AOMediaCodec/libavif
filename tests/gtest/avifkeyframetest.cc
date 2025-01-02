// Copyright 2024 Google LLC
// SPDX-License-Identifier: BSD-2-Clause

#include <array>
#include <string>

#include "avif/avif.h"
#include "aviftest_helpers.h"
#include "gtest/gtest.h"

namespace avif {
namespace {

// Used to pass the data folder path to the GoogleTest suites.
const char* data_path = nullptr;

TEST(KeyframeTest, Decode) {
  if (!testutil::Av1DecoderAvailable()) {
    GTEST_SKIP() << "AV1 Codec unavailable, skip test.";
  }

  std::array<ImagePtr, 5> images;
  for (ImagePtr& image : images) {
    // Use 12-bit 4:2:2 studio range for extra coverage.
    image = testutil::CreateImage(64, 64, 12, AVIF_PIXEL_FORMAT_YUV422,
                                  AVIF_PLANES_ALL, AVIF_RANGE_LIMITED);
    ASSERT_NE(image, nullptr);
  }

  // Alpha is always full range.
  constexpr uint32_t kColor[4] = {3760, 3840, 3840, 4095};
  testutil::FillImagePlain(images[0].get(), kColor);
  constexpr uint32_t kSomeColor[4] = {3760, 256, 256, 4095};
  testutil::FillImagePlain(images[1].get(), kSomeColor);
  constexpr uint32_t kTranslucentColor[4] = {256, 256, 256, 2002};
  testutil::FillImagePlain(images[2].get(), kTranslucentColor);
  testutil::FillImageGradient(images[3].get());
  testutil::FillImageGradient(images[4].get());

  // The file read below was generated with the following:

  // EncoderPtr e(avifEncoderCreate()); e->timescale = 1;
  // avifEncoderAddImage(e.get(), images[1].get(), 1, AVIF_ADD_IMAGE_FLAG_NONE);
  // avifEncoderAddImage(e.get(), images[1].get(), 1, AVIF_ADD_IMAGE_FLAG_NONE);
  // avifEncoderAddImage(e.get(), images[1].get(), 1,
  //                     AVIF_ADD_IMAGE_FLAG_FORCE_KEYFRAME);
  // avifEncoderAddImage(e.get(), images[1].get(), 1, AVIF_ADD_IMAGE_FLAG_NONE);
  // avifEncoderAddImage(e.get(), images[1].get(), 1, AVIF_ADD_IMAGE_FLAG_NONE);
  // testutil::AvifRwData encoded; avifEncoderFinish(e.get(), &encoded);

  // Reading a file makes sure the encoder does not pick different keyframes in
  // the future.

  DecoderPtr decoder(avifDecoderCreate());
  ASSERT_NE(decoder, nullptr);
  const std::string file_name = "colors-animated-12bpc-keyframes-0-2-3.avif";
  ASSERT_EQ(
      avifDecoderSetIOFile(decoder.get(), (data_path + file_name).c_str()),
      AVIF_RESULT_OK);
  ASSERT_EQ(avifDecoderParse(decoder.get()), AVIF_RESULT_OK);

  // The first frame is always a keyframe.
  EXPECT_TRUE(avifDecoderIsKeyframe(decoder.get(), 0));
  EXPECT_EQ(avifDecoderNearestKeyframe(decoder.get(), 0), 0);

  // The encoder may choose to use a keyframe here, even without FORCE_KEYFRAME.
  // It seems not to.
  EXPECT_FALSE(avifDecoderIsKeyframe(decoder.get(), 1));
  EXPECT_EQ(avifDecoderNearestKeyframe(decoder.get(), 1), 0);

  EXPECT_TRUE(avifDecoderIsKeyframe(decoder.get(), 2));
  EXPECT_EQ(avifDecoderNearestKeyframe(decoder.get(), 2), 2);

  // The encoder seems to prefer a keyframe here
  // (gradient too different from plain color).
  EXPECT_TRUE(avifDecoderIsKeyframe(decoder.get(), 3));
  EXPECT_EQ(avifDecoderNearestKeyframe(decoder.get(), 3), 3);

  // This is the same frame as the previous one. It should not be a keyframe.
  EXPECT_FALSE(avifDecoderIsKeyframe(decoder.get(), 4));
  EXPECT_EQ(avifDecoderNearestKeyframe(decoder.get(), 4), 3);

  // Check it decodes properly.
  for (const ImagePtr& image : images) {
    ASSERT_EQ(avifDecoderNextImage(decoder.get()), AVIF_RESULT_OK);
    EXPECT_GT(testutil::GetPsnr(*image, *decoder->image), 20.0);
  }
}

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
