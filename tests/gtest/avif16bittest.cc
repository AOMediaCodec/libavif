// Copyright 2022 Google LLC
// SPDX-License-Identifier: BSD-2-Clause

#include <cstring>

#include "avif/avif.h"
#include "avif/internal.h"
#include "aviftest_helpers.h"
#include "gtest/gtest.h"

namespace libavif {
namespace {

//------------------------------------------------------------------------------

// Used to pass the data folder path to the GoogleTest suites.
const char* data_path = nullptr;

//------------------------------------------------------------------------------

TEST(BitDepthTest, Avif16bitLossy) {
  const testutil::AvifImagePtr image =
      testutil::ReadImage(data_path, "weld_16bit.png", AVIF_PIXEL_FORMAT_NONE,
                          /*requested_depth=*/16);
  ASSERT_NE(image, nullptr);

  const testutil::AvifRwData encoded =
      testutil::Encode(image.get(), AVIF_SPEED_FASTEST, AVIF_QUALITY_WORST);
  const testutil::AvifImagePtr decoded =
      testutil::Decode(encoded.data, encoded.size);
  ASSERT_NE(decoded, nullptr);

  ASSERT_EQ(image->depth, decoded->depth);
  ASSERT_EQ(image->width, decoded->width);
  ASSERT_EQ(image->height, decoded->height);
  EXPECT_FALSE(testutil::AreImagesEqual(*image, *decoded));
}

//------------------------------------------------------------------------------

TEST(BitDepthTest, Avif16bitLossless) {
  const testutil::AvifImagePtr image =
      testutil::ReadImage(data_path, "weld_16bit.png", AVIF_PIXEL_FORMAT_NONE,
                          /*requested_depth=*/16);
  ASSERT_NE(image, nullptr);

  const testutil::AvifRwData encoded =
      testutil::Encode(image.get(), AVIF_SPEED_FASTEST, AVIF_QUALITY_LOSSLESS);
  const testutil::AvifImagePtr decoded =
      testutil::Decode(encoded.data, encoded.size);
  ASSERT_NE(decoded, nullptr);

  EXPECT_TRUE(testutil::AreImagesEqual(*image, *decoded));
}

//------------------------------------------------------------------------------

TEST(BitDepthTest, Avif16bitRetroCompatible) {
  const testutil::AvifImagePtr image =
      testutil::ReadImage(data_path, "weld_16bit.png", AVIF_PIXEL_FORMAT_NONE,
                          /*requested_depth=*/16);
  ASSERT_NE(image, nullptr);

  testutil::AvifRwData encoded =
      testutil::Encode(image.get(), AVIF_SPEED_FASTEST, AVIF_QUALITY_WORST);

  // Replace all subdepth tags by "zzzzzz" garbage. This simulates an old
  // decoder that does not recognize the subdepth feature.
  const size_t kTagLength = std::strlen(AVIF_URN_SUBDEPTH_8LSB);
  for (size_t i = 0; i + kTagLength <= encoded.size; ++i) {
    if (!std::memcmp(&encoded.data[i], AVIF_URN_SUBDEPTH_8LSB, kTagLength)) {
      std::fill(&encoded.data[i], &encoded.data[i] + kTagLength, 'z');
    }
  }

  const testutil::AvifImagePtr decoded =
      testutil::Decode(encoded.data, encoded.size);
  ASSERT_NE(decoded, nullptr);

  // Only the 8 most significant bits of each sample can be retrieved.
  // They should be encoded losslessly no matter the quantizer settings.
  testutil::AvifImagePtr image_8msb = testutil::CreateImage(
      image->width, image->height, 8, image->yuvFormat,
      image->alphaPlane ? AVIF_PLANES_ALL : AVIF_PLANES_YUV, image->yuvRange);
  ASSERT_NE(image_8msb, nullptr);
  ASSERT_EQ(avifImageCopySamplesExtended(
                image_8msb.get(), AVIF_SUBDEPTH_8_MOST_SIGNIFICANT_BITS,
                image.get(), AVIF_SUBDEPTH_NONE, AVIF_PLANES_ALL),
            AVIF_RESULT_OK);
  EXPECT_TRUE(testutil::AreImagesEqual(*image_8msb, *decoded));
}

//------------------------------------------------------------------------------

// TODO(yguyon): Test 16-bit alpha

//------------------------------------------------------------------------------

}  // namespace
}  // namespace libavif

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  if (argc != 2) {
    std::cerr << "There must be exactly one argument containing the path to "
                 "the test data folder"
              << std::endl;
    return 1;
  }
  libavif::data_path = argv[1];
  return RUN_ALL_TESTS();
}
