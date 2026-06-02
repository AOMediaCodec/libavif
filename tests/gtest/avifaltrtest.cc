// Copyright 2025 Google LLC
// SPDX-License-Identifier: BSD-2-Clause

#include <algorithm>
#include <cstring>
#include <iostream>
#include <string>

#include "avif/avif.h"
#include "avif/avif_cxx.h"
#include "avifincrtest_helpers.h"
#include "aviftest_helpers.h"
#include "gtest/gtest.h"

namespace avif {
namespace {

// Used to pass the data folder path to the GoogleTest suites.
const char* data_path = nullptr;

//------------------------------------------------------------------------------

// Verifies that avifDecoderParse() takes the 'sato' image item from the same
// 'atlr' group as the primary item into account.
TEST(AltrTest, SampleTransformDepthEqualToInput) {
  const ImagePtr image =
      testutil::ReadImage(data_path, "weld_16bit.png", AVIF_PIXEL_FORMAT_YUV444,
                          /*requested_depth=*/16);
  ASSERT_NE(image, nullptr);

  EncoderPtr encoder(avifEncoderCreate());
  ASSERT_NE(encoder, nullptr);
  encoder->quality = 0;  // Generate a tiny file as pixel values do not matter.
  encoder->speed = AVIF_SPEED_FASTEST;
  encoder->sampleTransformRecipe =
      AVIF_SAMPLE_TRANSFORM_BIT_DEPTH_EXTENSION_12B_8B_OVERLAP_4B;
  testutil::AvifRwData encoded;
  ASSERT_EQ(avifEncoderWrite(encoder.get(), image.get(), &encoded),
            AVIF_RESULT_OK);

  DecoderPtr decoder(avifDecoderCreate());
  ASSERT_NE(decoder, nullptr);
  decoder->imageContentToDecode |= AVIF_IMAGE_CONTENT_SAMPLE_TRANSFORMS;
  ASSERT_EQ(avifDecoderSetIOMemory(decoder.get(), encoded.data, encoded.size),
            AVIF_RESULT_OK);
  ASSERT_EQ(avifDecoderParse(decoder.get()), AVIF_RESULT_OK);
  EXPECT_EQ(decoder->image->depth, image->depth);
  ASSERT_EQ(avifDecoderNextImage(decoder.get()), AVIF_RESULT_OK);
  EXPECT_EQ(decoder->image->depth, image->depth);

  // Uncomment the following to generate the image used as input below.
  // std::ofstream("weld_sato_12B_8B_q0.avif", std::ios::binary)
  //     .write(reinterpret_cast<char*>(encoded.data), encoded.size);
}

// Verifies that avifDecoderNextImage() returns the same sample bit depth as
// avifDecoderParse().
TEST(AltrTest, SampleTransformDepthParseNextEqual) {
  const testutil::AvifRwData encoded =
      testutil::ReadFile(std::string(data_path) + "weld_sato_12B_8B_q0.avif");

  DecoderPtr decoder(avifDecoderCreate());
  ASSERT_NE(decoder, nullptr);
  decoder->imageContentToDecode |= AVIF_IMAGE_CONTENT_SAMPLE_TRANSFORMS;
  ASSERT_EQ(avifDecoderSetIOMemory(decoder.get(), encoded.data, encoded.size),
            AVIF_RESULT_OK);

  ASSERT_EQ(avifDecoderParse(decoder.get()), AVIF_RESULT_OK);
  const uint32_t depth = decoder->image->depth;
  EXPECT_EQ(depth, 16);

  ASSERT_EQ(avifDecoderNextImage(decoder.get()), AVIF_RESULT_OK);
  EXPECT_EQ(decoder->image->depth, depth);
}

// Verifies the fix for https://github.com/AOMediaCodec/libavif/issues/2979.
TEST(AltrTest, ZeroImageContentToDecode) {
  const std::string file_path =
      std::string(data_path) + "weld_sato_12B_8B_q0.avif";

  DecoderPtr decoder(avifDecoderCreate());
  ASSERT_NE(decoder, nullptr);
  decoder->imageContentToDecode = AVIF_IMAGE_CONTENT_NONE;
  ImagePtr image(avifImageCreateEmpty());
  ASSERT_NE(image, nullptr);
  ASSERT_EQ(avifDecoderReadFile(decoder.get(), image.get(), file_path.c_str()),
            AVIF_RESULT_NO_CONTENT);
}

TEST(AltrTest, OnlySampleTransformContentToDecode) {
  const std::string file_path =
      std::string(data_path) + "weld_sato_12B_8B_q0.avif";

  DecoderPtr decoder(avifDecoderCreate());
  ASSERT_NE(decoder, nullptr);
  // Has no effect without AVIF_IMAGE_CONTENT_COLOR_AND_ALPHA.
  decoder->imageContentToDecode = AVIF_IMAGE_CONTENT_SAMPLE_TRANSFORMS;
  ImagePtr image(avifImageCreateEmpty());
  ASSERT_NE(image, nullptr);
  ASSERT_EQ(avifDecoderReadFile(decoder.get(), image.get(), file_path.c_str()),
            AVIF_RESULT_NO_CONTENT);
}

// A 'sato' derived image item that references zero input image items must be
// rejected at parse time. Otherwise it would reach an AVIF_ASSERT_OR_RETURN in
// avifDecoderApplySampleTransformForPlanesImpl() and surface as
// AVIF_RESULT_INTERNAL_ERROR.
TEST(AltrTest, SampleTransformZeroInputItemsRejected) {
  const std::string file_path =
      std::string(data_path) + "weld_sato_12B_8B_q0.avif";
  testutil::AvifRwData encoded = testutil::ReadFile(file_path);
  ASSERT_GE(encoded.size, size_t{0x7f1 + 17});

  // Sanity check: the fixture is the version this test was authored against.
  ASSERT_EQ(std::memcmp(encoded.data + 0x111, "dimg", 4), 0);
  ASSERT_EQ(encoded.data[0x7f1], 0x02);
  ASSERT_EQ(encoded.data[0x7f2], 0x07);

  // Demote the iref's inner reference type from 'dimg' to a type libavif does
  // not interpret, so no item gets dimgForID == sato.id (i.e. the 'sato' item
  // ends up with zero input image items).
  std::memcpy(encoded.data + 0x111, "xxxx", 4);
  // Rewrite the 17-byte 'sato' payload to a constants-only expression that
  // passes avifSampleTransformExpressionIsValid(_, /*numInputImageItems=*/0):
  //   token_count = 11, [CONSTANT(16), NEGATION x 10] -> stack stays at 1.
  encoded.data[0x7f2] = 0x0b;
  encoded.data[0x7f3] = 0x00;
  encoded.data[0x7f4] = 0x00;
  encoded.data[0x7f5] = 0x00;
  encoded.data[0x7f6] = 0x00;
  encoded.data[0x7f7] = 0x10;
  for (int i = 0; i < 10; ++i) {
    encoded.data[0x7f8 + i] = 0x40;
  }

  DecoderPtr decoder(avifDecoderCreate());
  ASSERT_NE(decoder, nullptr);
  decoder->imageContentToDecode |= AVIF_IMAGE_CONTENT_SAMPLE_TRANSFORMS;
  ImagePtr image(avifImageCreateEmpty());
  ASSERT_NE(image, nullptr);
  const avifResult result = avifDecoderReadMemory(decoder.get(), image.get(),
                                                  encoded.data, encoded.size);
  EXPECT_EQ(result, AVIF_RESULT_BMFF_PARSE_FAILED);
  EXPECT_NE(result, AVIF_RESULT_INTERNAL_ERROR);
}

//------------------------------------------------------------------------------

}  // namespace
}  // namespace avif

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  if (argc < 2) {
    std::cerr
        << "The path to the test data folder must be provided as an argument"
        << std::endl;
    return 1;
  }
  avif::data_path = argv[1];
  return RUN_ALL_TESTS();
}
