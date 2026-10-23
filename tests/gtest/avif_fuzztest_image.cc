// Copyright 2026 Google LLC
// SPDX-License-Identifier: BSD-2-Clause

#include <array>
#include <cstdint>
#include <cstring>

#include "avif/avif.h"
#include "avif_fuzztest_helpers.h"
#include "aviftest_helpers.h"
#include "fuzztest/fuzztest.h"
#include "gtest/gtest.h"

namespace avif {
namespace testutil {
namespace {

void CopyViewIntoOwner(ImagePtr image, uint16_t x_seed, uint16_t y_seed,
                       uint16_t width_seed, uint16_t height_seed) {
  ASSERT_NE(image, nullptr);

  avifPixelFormatInfo format_info;
  avifGetPixelFormatInfo(image->yuvFormat, &format_info);
  uint32_t x = x_seed % image->width;
  uint32_t y = y_seed % image->height;
  if (!format_info.monochrome) {
    x &= ~((1u << format_info.chromaShiftX) - 1u);
    y &= ~((1u << format_info.chromaShiftY) - 1u);
  }
  const avifCropRect rect = {x, y, 1u + width_seed % (image->width - x),
                             1u + height_seed % (image->height - y)};

  ImagePtr view(avifImageCreateEmpty());
  ASSERT_NE(view, nullptr);
  ASSERT_EQ(avifImageSetViewRect(view.get(), image.get(), &rect),
            AVIF_RESULT_OK);

  const size_t bytes_per_sample = image->depth > 8 ? 2u : 1u;
  std::array<std::array<uint8_t, 2>, 4> expected_first_samples = {};
  std::array<bool, 4> had_plane = {};
  for (int channel = AVIF_CHAN_Y; channel <= AVIF_CHAN_A; ++channel) {
    const uint8_t* plane = avifImagePlane(view.get(), channel);
    if (plane != nullptr) {
      had_plane[channel] = true;
      std::memcpy(expected_first_samples[channel].data(), plane,
                  bytes_per_sample);
    }
  }

  ASSERT_EQ(avifImageCopy(image.get(), view.get(), AVIF_PLANES_ALL),
            AVIF_RESULT_OK);
  EXPECT_EQ(image->width, rect.width);
  EXPECT_EQ(image->height, rect.height);
  EXPECT_TRUE(image->imageOwnsYUVPlanes);
  EXPECT_EQ(image->imageOwnsAlphaPlane, had_plane[AVIF_CHAN_A]);
  for (int channel = AVIF_CHAN_Y; channel <= AVIF_CHAN_A; ++channel) {
    const uint8_t* const copied_plane = avifImagePlane(image.get(), channel);
    ASSERT_EQ(copied_plane != nullptr, had_plane[channel]);
    if (copied_plane != nullptr) {
      EXPECT_EQ(
          std::memcmp(copied_plane, expected_first_samples[channel].data(),
                      bytes_per_sample),
          0);
    }
  }
}

FUZZ_TEST(ImageApiFuzzTest, CopyViewIntoOwner)
    .WithDomains(ArbitraryAvifImage(), fuzztest::Arbitrary<uint16_t>(),
                 fuzztest::Arbitrary<uint16_t>(),
                 fuzztest::Arbitrary<uint16_t>(),
                 fuzztest::Arbitrary<uint16_t>());

}  // namespace
}  // namespace testutil
}  // namespace avif
