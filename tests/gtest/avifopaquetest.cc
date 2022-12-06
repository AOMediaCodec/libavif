// Copyright 2022 Google LLC
// SPDX-License-Identifier: BSD-2-Clause

#include "avif/avif.h"
#include "avifpng.h"
#include "aviftest_helpers.h"
#include "gtest/gtest.h"

namespace libavif {
namespace {

TEST(OpaqueTest, AlphaAndNoAlpha) {
  for (bool alpha_is_opaque : {false, true}) {
    for (int depth : {8, 10, 12}) {
      testutil::AvifImagePtr alpha = testutil::CreateImage(
          1, 1, depth, AVIF_PIXEL_FORMAT_YUV444, AVIF_PLANES_ALL);
      testutil::AvifImagePtr no_alpha = testutil::CreateImage(
          1, 1, depth, AVIF_PIXEL_FORMAT_YUV444, AVIF_PLANES_YUV);

      const uint32_t max_value = (1u << depth) - 1;
      const uint32_t yuva[] = {max_value, max_value, max_value,
                               alpha_is_opaque ? max_value : (max_value - 1)};
      testutil::FillImagePlain(alpha.get(), yuva);
      testutil::FillImagePlain(no_alpha.get(), yuva);

      EXPECT_EQ(testutil::AreImagesEqual(*alpha, *no_alpha), alpha_is_opaque);
    }
  }
}

TEST(OpaqueTest, Gradient) {
  for (int depth : {8, 10, 12}) {
    // YUVA.
    testutil::AvifImagePtr opaque_alpha = testutil::CreateImage(
        1024, 1024, depth, AVIF_PIXEL_FORMAT_YUV444, AVIF_PLANES_ALL);
    const uint32_t max_value = (1u << depth) - 1;
    const uint32_t yuva[] = {max_value, max_value, max_value, max_value};
    testutil::FillImagePlain(opaque_alpha.get(), yuva);

    // View on YUV, to make sure the color values are identical.
    testutil::AvifImagePtr no_alpha(avifImageCreateEmpty(), avifImageDestroy);
    const avifCropRect rect = {0, 0, opaque_alpha->width, opaque_alpha->height};
    ASSERT_EQ(avifImageSetViewRect(no_alpha.get(), opaque_alpha.get(), &rect),
              AVIF_RESULT_OK);
    avifImageFreePlanes(no_alpha.get(), AVIF_PLANES_A);

    // Decorrelate YUV and A.
    testutil::FillImageGradient(no_alpha.get());

    EXPECT_TRUE(testutil::AreImagesEqual(*opaque_alpha, *no_alpha));
  }
}

}  // namespace
}  // namespace libavif
