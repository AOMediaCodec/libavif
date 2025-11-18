// Copyright 2025 Google LLC
// SPDX-License-Identifier: BSD-2-Clause

#include <algorithm>
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
  ASSERT_EQ(avifDecoderSetIOMemory(decoder.get(), encoded.data, encoded.size),
            AVIF_RESULT_OK);

  ASSERT_EQ(avifDecoderParse(decoder.get()), AVIF_RESULT_OK);
  const uint32_t depth = decoder->image->depth;
  EXPECT_EQ(depth, 16);

  ASSERT_EQ(avifDecoderNextImage(decoder.get()), AVIF_RESULT_OK);
  EXPECT_EQ(decoder->image->depth, depth);
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
