// Copyright 2022 Google LLC
// SPDX-License-Identifier: BSD-2-Clause

#include <string.h>

#include <vector>

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

TEST(AlphaMultiplyTest, HighDepthUnalignedOddStride) {
  constexpr uint32_t kWidth = 1;
  constexpr uint32_t kHeight = 3;
  for (const avifRGBFormat format :
       {AVIF_RGB_FORMAT_RGBA, AVIF_RGB_FORMAT_ARGB, AVIF_RGB_FORMAT_BGRA,
        AVIF_RGB_FORMAT_ABGR, AVIF_RGB_FORMAT_GRAYA, AVIF_RGB_FORMAT_AGRAY}) {
    SCOPED_TRACE(format);
    avifRGBImage reference;
    memset(&reference, 0, sizeof(reference));
    reference.width = kWidth;
    reference.height = kHeight;
    reference.depth = 10;
    reference.format = format;
    ASSERT_EQ(avifRGBImageAllocatePixels(&reference), AVIF_RESULT_OK);

    const uint32_t channel_count = avifRGBFormatChannelCount(format);
    const uint32_t alpha_channel =
        (format == AVIF_RGB_FORMAT_ARGB || format == AVIF_RGB_FORMAT_ABGR ||
         format == AVIF_RGB_FORMAT_AGRAY)
            ? 0
            : channel_count - 1;
    const uint16_t alpha[kHeight] = {0, 512, 1023};
    for (uint32_t y = 0; y < kHeight; ++y) {
      for (uint32_t channel = 0; channel < channel_count; ++channel) {
        const uint16_t sample =
            (channel == alpha_channel)
                ? alpha[y]
                : static_cast<uint16_t>(128 * (channel + y + 1));
        memcpy(reference.pixels + static_cast<size_t>(y) * reference.rowBytes +
                   channel * sizeof(sample),
               &sample, sizeof(sample));
      }
    }

    avifRGBImage padded = reference;
    padded.rowBytes = reference.rowBytes + 1;
    std::vector<uint8_t> pixels(
        static_cast<size_t>(padded.rowBytes) * kHeight + 1, 0xa5);
    padded.pixels = pixels.data() + 1;
    for (uint32_t y = 0; y < kHeight; ++y) {
      memcpy(padded.pixels + static_cast<size_t>(y) * padded.rowBytes,
             reference.pixels + static_cast<size_t>(y) * reference.rowBytes,
             reference.rowBytes);
    }

    ASSERT_EQ(avifRGBImagePremultiplyAlpha(&reference), AVIF_RESULT_OK);
    ASSERT_EQ(avifRGBImagePremultiplyAlpha(&padded), AVIF_RESULT_OK);
    for (uint32_t y = 0; y < kHeight; ++y) {
      EXPECT_EQ(
          memcmp(padded.pixels + static_cast<size_t>(y) * padded.rowBytes,
                 reference.pixels + static_cast<size_t>(y) * reference.rowBytes,
                 reference.rowBytes),
          0);
      EXPECT_EQ(padded.pixels[static_cast<size_t>(y + 1) * padded.rowBytes - 1],
                0xa5);
    }

    ASSERT_EQ(avifRGBImageUnpremultiplyAlpha(&reference), AVIF_RESULT_OK);
    ASSERT_EQ(avifRGBImageUnpremultiplyAlpha(&padded), AVIF_RESULT_OK);
    for (uint32_t y = 0; y < kHeight; ++y) {
      EXPECT_EQ(
          memcmp(padded.pixels + static_cast<size_t>(y) * padded.rowBytes,
                 reference.pixels + static_cast<size_t>(y) * reference.rowBytes,
                 reference.rowBytes),
          0);
      EXPECT_EQ(padded.pixels[static_cast<size_t>(y + 1) * padded.rowBytes - 1],
                0xa5);
    }
    avifRGBImageFreePixels(&reference);
  }
}

//------------------------------------------------------------------------------

}  // namespace
}  // namespace avif
