// Copyright 2025 Google LLC
// SPDX-License-Identifier: BSD-2-Clause

#include <iostream>
#include <string>

#include "avif/avif.h"
#include "avif/avif_cxx.h"
#include "aviftest_helpers.h"
#include "gtest/gtest.h"

namespace avif {
namespace {

// Used to pass the data folder path to the GoogleTest suites.
const char* data_path = nullptr;

//------------------------------------------------------------------------------

#if defined(AVIF_ENABLE_EXPERIMENTAL_EXTENDED_PIXI)
TEST(AvifPixiTest, SameOutput) {
  ImagePtr image =
      testutil::CreateImage(4, 4, 8, AVIF_PIXEL_FORMAT_YUV420, AVIF_PLANES_YUV);
  ASSERT_NE(image, nullptr);
  image->yuvChromaSamplePosition = AVIF_CHROMA_SAMPLE_POSITION_VERTICAL;
  testutil::FillImageGradient(image.get());  // The pixels do not matter.

  // Encode.

  testutil::AvifRwData encoded_regular_pixi;
  EncoderPtr encoder_regular_pixi(avifEncoderCreate());
  ASSERT_NE(encoder_regular_pixi, nullptr);
  encoder_regular_pixi->speed = AVIF_SPEED_FASTEST;
  encoder_regular_pixi->headerFormat = AVIF_HEADER_DEFAULT;
  ASSERT_EQ(avifEncoderWrite(encoder_regular_pixi.get(), image.get(),
                             &encoded_regular_pixi),
            AVIF_RESULT_OK);

  testutil::AvifRwData encoded_extended_pixi;
  EncoderPtr encoder_extended_pixi(avifEncoderCreate());
  ASSERT_NE(encoder_extended_pixi, nullptr);
  encoder_extended_pixi->speed = AVIF_SPEED_FASTEST;
  encoder_extended_pixi->headerFormat = AVIF_HEADER_EXTENDED_PIXI;
  ASSERT_EQ(avifEncoderWrite(encoder_extended_pixi.get(), image.get(),
                             &encoded_extended_pixi),
            AVIF_RESULT_OK);
  EXPECT_LT(encoded_regular_pixi.size, encoded_extended_pixi.size);

  // Decode.

  ImagePtr decoded_regular_pixi(avifImageCreateEmpty());
  ASSERT_NE(decoded_regular_pixi, nullptr);
  DecoderPtr decoder_regular_pixi(avifDecoderCreate());
  ASSERT_NE(decoder_regular_pixi, nullptr);
  decoder_regular_pixi->imageContentToDecode |= AVIF_IMAGE_CONTENT_GAIN_MAP;
  ASSERT_EQ(avifDecoderReadMemory(
                decoder_regular_pixi.get(), decoded_regular_pixi.get(),
                encoded_regular_pixi.data, encoded_regular_pixi.size),
            AVIF_RESULT_OK);

  ImagePtr decoded_extended_pixi(avifImageCreateEmpty());
  ASSERT_NE(decoded_extended_pixi, nullptr);
  DecoderPtr decoder_extended_pixi(avifDecoderCreate());
  ASSERT_NE(decoder_extended_pixi, nullptr);
  decoder_extended_pixi->imageContentToDecode |= AVIF_IMAGE_CONTENT_GAIN_MAP;
  ASSERT_EQ(avifDecoderReadMemory(
                decoder_extended_pixi.get(), decoded_extended_pixi.get(),
                encoded_extended_pixi.data, encoded_extended_pixi.size),
            AVIF_RESULT_OK);

  EXPECT_TRUE(
      testutil::AreImagesEqual(*decoded_regular_pixi, *decoded_extended_pixi));
}
#endif  // AVIF_ENABLE_EXPERIMENTAL_EXTENDED_PIXI

TEST(AvifPixiTest, ExtendedPixiWorksEvenWithoutCMakeFlagOn) {
  const testutil::AvifRwData avif =
      testutil::ReadFile(std::string(data_path) + "extended_pixi.avif");
  ASSERT_NE(avif.size, 0u);
  ImagePtr image(avifImageCreateEmpty());
  ASSERT_NE(image, nullptr);
  DecoderPtr decoder(avifDecoderCreate());
  ASSERT_NE(decoder, nullptr);
  ASSERT_EQ(
      avifDecoderReadMemory(decoder.get(), image.get(), avif.data, avif.size),
      AVIF_RESULT_OK);
  EXPECT_EQ(image->yuvFormat, AVIF_PIXEL_FORMAT_YUV420);
  EXPECT_EQ(image->yuvChromaSamplePosition,
            AVIF_CHROMA_SAMPLE_POSITION_VERTICAL);
}

TEST(AvifHeaderFormatTest, ABI) {
  // avifEncoder::headerFormat was of type avifHeaderFormat in libavif
  // version 1.1.1:
  //   https://github.com/AOMediaCodec/libavif/blob/v1.1.1/include/avif/avif.h#L1498
  // It was later changed to avifHeaderFormatFlags to be able to combine
  // multiple avifHeaderFormat features. Check that it was not an ABI
  // incompatible change.
  EXPECT_EQ(sizeof(avifEncoder::headerFormat), sizeof(avifHeaderFormat));

#if defined(AVIF_ENABLE_EXPERIMENTAL_EXTENDED_PIXI)
  // Check that the field can be assigned with a combination of flags without
  // compile errors:
  EncoderPtr encoder(avifEncoderCreate());
  ASSERT_NE(encoder, nullptr);
  encoder->headerFormat = AVIF_HEADER_DEFAULT | AVIF_HEADER_EXTENDED_PIXI;
#endif  // AVIF_ENABLE_EXPERIMENTAL_EXTENDED_PIXI
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
