// Copyright 2022 Google LLC
// SPDX-License-Identifier: BSD-2-Clause

#include <string.h>

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

TEST(AlphaMultiplyTest, ValidIntegerStorageRemainsSupported) {
  for (uint32_t depth : {8u, 10u, 12u, 16u}) {
    for (avifRGBFormat format :
         {AVIF_RGB_FORMAT_RGBA, AVIF_RGB_FORMAT_BGRA, AVIF_RGB_FORMAT_ARGB,
          AVIF_RGB_FORMAT_ABGR, AVIF_RGB_FORMAT_GRAYA, AVIF_RGB_FORMAT_AGRAY}) {
      for (avifBool avoid_libyuv : {AVIF_FALSE, AVIF_TRUE}) {
        SCOPED_TRACE(depth);
        SCOPED_TRACE(format);
        SCOPED_TRACE(avoid_libyuv);
        const uint16_t maximum = static_cast<uint16_t>((1u << depth) - 1u);
        uint16_t pixels[] = {maximum, maximum, maximum, maximum};
        if (depth == 8) {
          memset(pixels, 255, sizeof(pixels));
        }
        uint16_t expected[4];
        memcpy(expected, pixels, sizeof(pixels));
        avifRGBImage rgb = {};
        rgb.width = 1;
        rgb.height = 1;
        rgb.depth = depth;
        rgb.format = format;
        rgb.avoidLibYUV = avoid_libyuv;
        rgb.isFloat = AVIF_FALSE;
        rgb.pixels = reinterpret_cast<uint8_t*>(pixels);
        rgb.rowBytes = sizeof(pixels);
        EXPECT_EQ(avifRGBImagePremultiplyAlpha(&rgb), AVIF_RESULT_OK);
        EXPECT_EQ(memcmp(pixels, expected, sizeof(pixels)), 0);
        EXPECT_EQ(avifRGBImageUnpremultiplyAlpha(&rgb), AVIF_RESULT_OK);
        EXPECT_EQ(memcmp(pixels, expected, sizeof(pixels)), 0);
      }
    }
  }
}

TEST(AlphaMultiplyTest, FloatOutputPreservesIntegerAlphaConversion) {
  for (bool premultiplied_input : {false, true}) {
    for (avifBool avoid_libyuv : {AVIF_FALSE, AVIF_TRUE}) {
      for (int max_threads : {1, 2}) {
        for (uint32_t alpha : {0u, 128u, 255u}) {
          SCOPED_TRACE(premultiplied_input);
          SCOPED_TRACE(avoid_libyuv);
          SCOPED_TRACE(max_threads);
          SCOPED_TRACE(alpha);
          ImagePtr image = testutil::CreateImage(
              4, 8, 8, AVIF_PIXEL_FORMAT_YUV444, AVIF_PLANES_ALL);
          ASSERT_NE(image, nullptr);
          const uint32_t yuva[] = {128, 128, 128, alpha};
          testutil::FillImagePlain(image.get(), yuva);
          image->alphaPremultiplied = premultiplied_input;

          testutil::AvifRgbImage rgb(image.get(), 16, AVIF_RGB_FORMAT_RGBA);
          rgb.avoidLibYUV = avoid_libyuv;
          rgb.isFloat = AVIF_TRUE;
          rgb.alphaPremultiplied = !premultiplied_input;
          rgb.maxThreads = max_threads;
          ASSERT_EQ(avifImageYUVToRGB(image.get(), &rgb), AVIF_RESULT_OK);
          EXPECT_TRUE(rgb.isFloat);
          EXPECT_EQ(rgb.alphaPremultiplied, !premultiplied_input);
          ASSERT_NE(rgb.pixels, nullptr);
          for (uint32_t y = 0; y < rgb.height; ++y) {
            const uint16_t* row = reinterpret_cast<const uint16_t*>(
                rgb.pixels + static_cast<size_t>(y) * rgb.rowBytes);
            for (uint32_t x = 0; x < rgb.width; ++x) {
              if (alpha == 0) {
                EXPECT_EQ(row[x * 4], 0);
                EXPECT_EQ(row[x * 4 + 1], 0);
                EXPECT_EQ(row[x * 4 + 2], 0);
                EXPECT_EQ(row[x * 4 + 3], 0);
              } else {
                EXPECT_GT(row[x * 4], 0);
                EXPECT_LE(row[x * 4], 0x3c00);
                EXPECT_GT(row[x * 4 + 3], 0);
                EXPECT_LE(row[x * 4 + 3], 0x3c00);
              }
            }
          }
        }
      }
    }
  }
}

TEST(AlphaMultiplyTest, RejectsFloatStorageWithoutWriting) {
  for (avifRGBFormat format :
       {AVIF_RGB_FORMAT_RGBA, AVIF_RGB_FORMAT_BGRA, AVIF_RGB_FORMAT_ARGB,
        AVIF_RGB_FORMAT_ABGR, AVIF_RGB_FORMAT_GRAYA, AVIF_RGB_FORMAT_AGRAY}) {
    for (uint32_t depth : {8u, 10u, 12u, 16u}) {
      for (avifBool avoid_libyuv : {AVIF_FALSE, AVIF_TRUE}) {
        SCOPED_TRACE(format);
        SCOPED_TRACE(depth);
        SCOPED_TRACE(avoid_libyuv);
        const avifResult expected_result = (depth == 16u)
                                               ? AVIF_RESULT_NOT_IMPLEMENTED
                                               : AVIF_RESULT_INVALID_ARGUMENT;
        uint16_t pixels[] = {0x3800, 0x3c00, 0x3400, 0x3800};
        const uint16_t expected[] = {0x3800, 0x3c00, 0x3400, 0x3800};
        avifRGBImage rgb = {};
        rgb.width = 1;
        rgb.height = 1;
        rgb.depth = depth;
        rgb.format = format;
        rgb.avoidLibYUV = avoid_libyuv;
        rgb.isFloat = AVIF_TRUE;
        rgb.pixels = reinterpret_cast<uint8_t*>(pixels);
        rgb.rowBytes = sizeof(pixels);
        EXPECT_EQ(avifRGBImagePremultiplyAlpha(&rgb), expected_result);
        EXPECT_EQ(memcmp(pixels, expected, sizeof(pixels)), 0);
        EXPECT_EQ(avifRGBImageUnpremultiplyAlpha(&rgb), expected_result);
        EXPECT_EQ(memcmp(pixels, expected, sizeof(pixels)), 0);
      }
    }
  }
}

TEST(AlphaMultiplyTest, RejectsFloatStorageBeforeToneMap) {
  ImagePtr base_yuv =
      testutil::CreateImage(2, 2, 8, AVIF_PIXEL_FORMAT_YUV444, AVIF_PLANES_ALL);
  ASSERT_NE(base_yuv, nullptr);
  const uint32_t yuva[] = {255, 128, 128, 7};
  testutil::FillImagePlain(base_yuv.get(), yuva);

  ImagePtr gain_map_img =
      testutil::CreateImage(2, 2, 8, AVIF_PIXEL_FORMAT_YUV400, AVIF_PLANES_YUV);
  ASSERT_NE(gain_map_img, nullptr);
  const uint32_t gm_y[] = {128};
  testutil::FillImagePlain(gain_map_img.get(), gm_y);

  avifGainMap* gain_map = avifGainMapCreate();
  ASSERT_NE(gain_map, nullptr);
  gain_map->image = gain_map_img.get();
  gain_map->baseHdrHeadroom = {0, 1};
  gain_map->alternateHdrHeadroom = {0, 1};

  for (avifRGBFormat format :
       {AVIF_RGB_FORMAT_ARGB, AVIF_RGB_FORMAT_BGRA, AVIF_RGB_FORMAT_GRAYA}) {
    SCOPED_TRACE(format);
    testutil::AvifRgbImage rgb(base_yuv.get(), 16, format);
    rgb.isFloat = AVIF_TRUE;
    ASSERT_EQ(avifImageYUVToRGB(base_yuv.get(), &rgb), AVIF_RESULT_OK);

    const size_t byte_count = static_cast<size_t>(rgb.rowBytes) * rgb.height;
    std::vector<uint8_t> expected(rgb.pixels, rgb.pixels + byte_count);

    EXPECT_EQ(avifRGBImagePremultiplyAlpha(&rgb), AVIF_RESULT_NOT_IMPLEMENTED);
    EXPECT_EQ(memcmp(rgb.pixels, expected.data(), byte_count), 0);
    EXPECT_EQ(avifRGBImageUnpremultiplyAlpha(&rgb),
              AVIF_RESULT_NOT_IMPLEMENTED);
    EXPECT_EQ(memcmp(rgb.pixels, expected.data(), byte_count), 0);

    for (uint32_t out_depth : {16u, 8u}) {
      SCOPED_TRACE(out_depth);
      avifRGBImage tone_mapped;
      avifRGBImageSetDefaults(&tone_mapped, base_yuv.get());
      tone_mapped.format = AVIF_RGB_FORMAT_RGBA;
      tone_mapped.depth = out_depth;
      tone_mapped.isFloat = AVIF_FALSE;
      avifDiagnostics diag;
      memset(&diag, 0, sizeof(diag));
      EXPECT_EQ(avifRGBImageApplyGainMap(
                    &rgb, base_yuv->colorPrimaries,
                    base_yuv->transferCharacteristics, gain_map,
                    /*hdrHeadroom=*/0.0f, base_yuv->colorPrimaries,
                    base_yuv->transferCharacteristics, &tone_mapped,
                    /*clli=*/nullptr, &diag),
                AVIF_RESULT_OK);
      if (tone_mapped.pixels != nullptr) {
        if (out_depth == 16u) {
          const uint16_t* px =
              reinterpret_cast<const uint16_t*>(tone_mapped.pixels);
          EXPECT_EQ(px[0], 65535u);
          EXPECT_EQ(px[1], 65535u);
          EXPECT_EQ(px[2], 65535u);
        } else {
          EXPECT_EQ(tone_mapped.pixels[0], 255u);
          EXPECT_EQ(tone_mapped.pixels[1], 255u);
          EXPECT_EQ(tone_mapped.pixels[2], 255u);
        }
      }
      avifRGBImageFreePixels(&tone_mapped);
    }
  }

  gain_map->image = nullptr;
  avifGainMapDestroy(gain_map);
}

//------------------------------------------------------------------------------

}  // namespace
}  // namespace avif
