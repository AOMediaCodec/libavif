// Copyright 2022 Google LLC
// SPDX-License-Identifier: BSD-2-Clause

#include <string.h>

#include <array>
#include <cstdint>

#include "avif/avif.h"
#include "aviftest_helpers.h"
#include "gtest/gtest.h"

namespace avif {
namespace {

//------------------------------------------------------------------------------

TEST(AlphaMultiplyTest, OpaqueIsNoOp) {
  for (bool premultiplied_input : {false, true}) {
    if (!premultiplied_input) {
      // avifPrepareReformatState() will result in different YUV(A)-to-RGB
      // conversion algorithm choices (built-in or libyuv) depending on the
      // presence of an alpha channel, no matter if it is opaque or not, because
      // it considers that unpremultiplied YUVA samples should be premultiplied
      // before discarding alpha and converting to RGB. Skip the test.
      continue;
    }

    // YUVA.
    ImagePtr opaque_alpha = testutil::CreateImage(
        1024, 1024, /*depth=*/8, AVIF_PIXEL_FORMAT_YUV444, AVIF_PLANES_ALL);
    const uint32_t yuva[] = {255, 255, 255, 255};
    testutil::FillImagePlain(opaque_alpha.get(), yuva);
    opaque_alpha->alphaPremultiplied = premultiplied_input;

    // View on YUV (no alpha), to make sure the color values are identical.
    ImagePtr no_alpha(avifImageCreateEmpty());
    ASSERT_NE(no_alpha, nullptr);
    const avifCropRect rect = {0, 0, opaque_alpha->width, opaque_alpha->height};
    ASSERT_EQ(avifImageSetViewRect(no_alpha.get(), opaque_alpha.get(), &rect),
              AVIF_RESULT_OK);
    avifImageFreePlanes(no_alpha.get(), AVIF_PLANES_A);

    // Decorrelate YUV and Alpha.
    testutil::FillImageGradient(no_alpha.get());

    // Convert to RGB and discard any Alpha.
    testutil::AvifRgbImage opaque_rgb(opaque_alpha.get(), opaque_alpha->depth,
                                      AVIF_RGB_FORMAT_RGB);
    ASSERT_EQ(avifImageYUVToRGB(opaque_alpha.get(), &opaque_rgb),
              AVIF_RESULT_OK);
    testutil::AvifRgbImage no_alpha_rgb(no_alpha.get(), no_alpha->depth,
                                        AVIF_RGB_FORMAT_RGB);
    ASSERT_EQ(avifImageYUVToRGB(no_alpha.get(), &no_alpha_rgb), AVIF_RESULT_OK);

    // YUV samples with opaque alpha and with missing alpha should be converted
    // to the same RGB values.
    EXPECT_TRUE(testutil::AreByteSequencesEqual(
        opaque_rgb.pixels, opaque_rgb.height * opaque_rgb.rowBytes,
        no_alpha_rgb.pixels, no_alpha_rgb.height * no_alpha_rgb.rowBytes));

    // TODO(yguyon): Also compare avifImageYUVToRGB() with ignoreAlpha.
  }
}

TEST(AlphaMultiplyTest, SupportedDepths) {
  for (uint32_t depth : {8u, 10u, 12u, 16u}) {
    for (avifRGBFormat format :
         {AVIF_RGB_FORMAT_RGBA, AVIF_RGB_FORMAT_BGRA, AVIF_RGB_FORMAT_ARGB,
          AVIF_RGB_FORMAT_ABGR, AVIF_RGB_FORMAT_GRAYA, AVIF_RGB_FORMAT_AGRAY}) {
      for (bool premultiply : {false, true}) {
        SCOPED_TRACE(depth);
        SCOPED_TRACE(format);
        SCOPED_TRACE(premultiply);
        const uint32_t channels = avifRGBFormatChannelCount(format);
        const uint32_t alpha_channel =
            (format == AVIF_RGB_FORMAT_ARGB || format == AVIF_RGB_FORMAT_ABGR ||
             format == AVIF_RGB_FORMAT_AGRAY)
                ? 0
                : channels - 1;
        const uint16_t max = (uint32_t{1} << depth) - 1;
        const std::array<uint16_t, 3> alphas = {0, uint16_t(max / 2), max};
        // Real, aligned storage accommodates all three pixels at every depth.
        std::array<uint16_t, 12> storage = {};
        avifRGBImage rgb = {};
        rgb.width = alphas.size();
        rgb.height = 1;
        rgb.depth = depth;
        rgb.format = format;
        rgb.pixels = reinterpret_cast<uint8_t*>(storage.data());
        rgb.rowBytes = rgb.width * channels * (depth == 8 ? 1 : 2);
        for (uint32_t pixel = 0; pixel < rgb.width; ++pixel) {
          for (uint32_t channel = 0; channel < channels; ++channel) {
            const uint16_t value =
                channel == alpha_channel || !premultiply ? alphas[pixel] : max;
            const size_t index = pixel * channels + channel;
            if (depth == 8) {
              rgb.pixels[index] = static_cast<uint8_t>(value);
            } else {
              storage[index] = value;
            }
          }
        }
        ASSERT_EQ(premultiply ? avifRGBImagePremultiplyAlpha(&rgb)
                              : avifRGBImageUnpremultiplyAlpha(&rgb),
                  AVIF_RESULT_OK);
        for (uint32_t pixel = 0; pixel < rgb.width; ++pixel) {
          for (uint32_t channel = 0; channel < channels; ++channel) {
            const uint16_t expected = channel == alpha_channel || premultiply
                                          ? alphas[pixel]
                                          : (alphas[pixel] == 0 ? 0 : max);
            const size_t index = pixel * channels + channel;
            const uint16_t actual =
                depth == 8 ? rgb.pixels[index] : storage[index];
            EXPECT_EQ(actual, expected);
          }
        }
      }
    }
  }
}

TEST(AlphaMultiplyTest, RejectsUnsupportedDepths) {
  for (uint32_t depth : {0u, 7u, 9u, 11u, 15u, 17u}) {
    for (avifRGBFormat format :
         {AVIF_RGB_FORMAT_RGBA, AVIF_RGB_FORMAT_ARGB, AVIF_RGB_FORMAT_GRAYA}) {
      for (bool premultiply : {false, true}) {
        SCOPED_TRACE(depth);
        SCOPED_TRACE(format);
        SCOPED_TRACE(premultiply);
        const uint32_t channels = avifRGBFormatChannelCount(format);
        std::array<uint16_t, 12> storage = {};
        storage.fill(0x0102u);
        const auto expected = storage;
        avifRGBImage rgb = {};
        rgb.width = 3;
        rgb.height = 1;
        rgb.depth = depth;
        rgb.format = format;
        rgb.pixels = reinterpret_cast<uint8_t*>(storage.data());
        rgb.rowBytes = rgb.width * channels * 2;
        EXPECT_EQ(premultiply ? avifRGBImagePremultiplyAlpha(&rgb)
                              : avifRGBImageUnpremultiplyAlpha(&rgb),
                  AVIF_RESULT_INVALID_ARGUMENT);
        EXPECT_EQ(storage, expected);
      }
    }
  }
}

TEST(AlphaMultiplyTest, GrayAImagePremultiplyAlpha) {
  avifRGBImage rgb;
  memset(&rgb, 0, sizeof(rgb));
  rgb.width = 6;
  rgb.height = 1;
  rgb.depth = 10;
  rgb.format = AVIF_RGB_FORMAT_GRAYA;  // 2 channels with alpha
  ASSERT_EQ(avifRGBImageAllocatePixels(&rgb), AVIF_RESULT_OK);
  memset(rgb.pixels, 1, (size_t)rgb.rowBytes * rgb.height);
  EXPECT_EQ(avifRGBImagePremultiplyAlpha(&rgb), AVIF_RESULT_OK);
  avifRGBImageFreePixels(&rgb);
}

TEST(AlphaMultiplyTest, AGrayImagePremultiplyAlpha) {
  avifRGBImage rgb;
  memset(&rgb, 0, sizeof(rgb));
  rgb.width = 6;
  rgb.height = 1;
  rgb.depth = 8;
  rgb.format = AVIF_RGB_FORMAT_AGRAY;  // 2 channels with alpha
  ASSERT_EQ(avifRGBImageAllocatePixels(&rgb), AVIF_RESULT_OK);
  memset(rgb.pixels, 1, (size_t)rgb.rowBytes * rgb.height);
  EXPECT_EQ(avifRGBImagePremultiplyAlpha(&rgb), AVIF_RESULT_OK);
  avifRGBImageFreePixels(&rgb);
}

//------------------------------------------------------------------------------

}  // namespace
}  // namespace avif
