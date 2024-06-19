// Copyright 2024 Google LLC
// SPDX-License-Identifier: BSD-2-Clause

#include <cstring>

#include "avif/avif.h"
#include "avif/avif_cxx.h"
#include "avif/internal.h"
#include "aviftest_helpers.h"
#include "gtest/gtest.h"

namespace avif {
namespace {

//------------------------------------------------------------------------------

// Used to pass the data folder path to the GoogleTest suites.
const char* data_path = nullptr;

class SampleTransformTest
    : public testing::TestWithParam<
          std::tuple<avifSampleTransformRecipe, avifPixelFormat,
                     /*create_alpha=*/bool, /*quality=*/int>> {};

//------------------------------------------------------------------------------

TEST_P(SampleTransformTest, Avif16bit) {
  const avifSampleTransformRecipe recipe = std::get<0>(GetParam());
  const avifPixelFormat yuv_format = std::get<1>(GetParam());
  const bool create_alpha = std::get<2>(GetParam());
  const int quality = std::get<3>(GetParam());

  const ImagePtr image = testutil::ReadImage(
      data_path, "weld_16bit.png", yuv_format, /*requested_depth=*/16);
  ASSERT_NE(image, nullptr);
  if (create_alpha && !image->alphaPlane) {
    // Simulate alpha plane with a view on luma.
    image->alphaPlane = image->yuvPlanes[AVIF_CHAN_Y];
    image->alphaRowBytes = image->yuvRowBytes[AVIF_CHAN_Y];
    image->imageOwnsAlphaPlane = false;
  }

  EncoderPtr encoder(avifEncoderCreate());
  ASSERT_NE(encoder, nullptr);
  encoder->speed = AVIF_SPEED_FASTEST;
  encoder->quality = quality;
  encoder->qualityAlpha = quality;
  encoder->sampleTransformRecipe = recipe;
  testutil::AvifRwData encoded;
  ASSERT_EQ(avifEncoderWrite(encoder.get(), image.get(), &encoded),
            AVIF_RESULT_OK);
  const ImagePtr decoded = testutil::Decode(encoded.data, encoded.size);
  ASSERT_NE(decoded, nullptr);

  ASSERT_EQ(image->depth, decoded->depth);
  ASSERT_EQ(image->width, decoded->width);
  ASSERT_EQ(image->height, decoded->height);

  EXPECT_GE(testutil::GetPsnr(*image, *decoded),
            (quality == AVIF_QUALITY_LOSSLESS) ? 99.0 : 15.0);

  // Replace all 'sato' box types by "zzzz" garbage. This simulates an old
  // decoder that does not recognize the Sample Transform feature.
  for (size_t i = 0; i + 4 <= encoded.size; ++i) {
    if (!std::memcmp(&encoded.data[i], "sato", 4)) {
      std::memcpy(&encoded.data[i], "zzzz", 4);
    }
  }
  const ImagePtr decoded_no_sato = testutil::Decode(encoded.data, encoded.size);
  ASSERT_NE(decoded_no_sato, nullptr);
  // Only the most significant bits of each sample can be retrieved.
  // They should be encoded losslessly no matter the quantizer settings.
  ImagePtr image_no_sato = testutil::CreateImage(
      static_cast<int>(image->width), static_cast<int>(image->height),
      static_cast<int>(decoded_no_sato->depth), image->yuvFormat,
      image->alphaPlane ? AVIF_PLANES_ALL : AVIF_PLANES_YUV, image->yuvRange);
  ASSERT_NE(image_no_sato, nullptr);

  const uint32_t shift =
      recipe == AVIF_SAMPLE_TRANSFORM_BIT_DEPTH_EXTENSION_8B_8B ? 8 : 4;
  const avifImage* inputImage = image.get();
  // Postfix notation.
  const avifSampleTransformToken tokens[] = {
      {AVIF_SAMPLE_TRANSFORM_INPUT_IMAGE_ITEM_INDEX, 0,
       /*inputImageItemIndex=*/1},
      {AVIF_SAMPLE_TRANSFORM_CONSTANT, 1 << shift, 0},
      {AVIF_SAMPLE_TRANSFORM_DIVIDE, 0, 0}};
  ASSERT_EQ(avifImageApplyOperations(
                image_no_sato.get(), AVIF_SAMPLE_TRANSFORM_BIT_DEPTH_32,
                /*numTokens=*/3, tokens, /*numInputImageItems=*/1, &inputImage,
                AVIF_PLANES_ALL),
            AVIF_RESULT_OK);
  EXPECT_TRUE(testutil::AreImagesEqual(*image_no_sato, *decoded_no_sato));
}

//------------------------------------------------------------------------------

INSTANTIATE_TEST_SUITE_P(
    Formats, SampleTransformTest,
    testing::Combine(
        testing::Values(AVIF_SAMPLE_TRANSFORM_BIT_DEPTH_EXTENSION_8B_8B),
        testing::Values(AVIF_PIXEL_FORMAT_YUV444, AVIF_PIXEL_FORMAT_YUV420,
                        AVIF_PIXEL_FORMAT_YUV400),
        /*create_alpha=*/testing::Values(false),
        /*quality=*/
        testing::Values(AVIF_QUALITY_DEFAULT)));

INSTANTIATE_TEST_SUITE_P(
    BitDepthExtensions, SampleTransformTest,
    testing::Combine(
        testing::Values(AVIF_SAMPLE_TRANSFORM_BIT_DEPTH_EXTENSION_8B_8B,
                        AVIF_SAMPLE_TRANSFORM_BIT_DEPTH_EXTENSION_12B_4B),
        testing::Values(AVIF_PIXEL_FORMAT_YUV444),
        /*create_alpha=*/testing::Values(false),
        /*quality=*/
        testing::Values(AVIF_QUALITY_LOSSLESS)));

INSTANTIATE_TEST_SUITE_P(
    Alpha, SampleTransformTest,
    testing::Combine(
        testing::Values(AVIF_SAMPLE_TRANSFORM_BIT_DEPTH_EXTENSION_8B_8B),
        testing::Values(AVIF_PIXEL_FORMAT_YUV444),
        /*create_alpha=*/testing::Values(true),
        /*quality=*/
        testing::Values(AVIF_QUALITY_LOSSLESS)));

// TODO(yguyon): Test grids with bit depth extensions.

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
