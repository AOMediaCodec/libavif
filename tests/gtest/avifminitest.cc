// Copyright 2023 Google LLC
// SPDX-License-Identifier: BSD-2-Clause

#include "avif/avif.h"
#include "aviftest_helpers.h"
#include "gtest/gtest.h"

using ::testing::Combine;
using ::testing::Values;

namespace avif {
namespace {

//------------------------------------------------------------------------------

class AvifMinimizedImageBoxTest
    : public testing::TestWithParam<std::tuple<
          /*width=*/int, /*height=*/int, /*depth=*/int, avifPixelFormat,
          avifPlanesFlags, avifRange, /*create_icc=*/bool, /*create_exif=*/bool,
          /*create_xmp=*/bool, avifTransformFlags, /*create_hdr=*/bool>> {};

TEST_P(AvifMinimizedImageBoxTest, All) {
  const int width = std::get<0>(GetParam());
  const int height = std::get<1>(GetParam());
  const int depth = std::get<2>(GetParam());
  const avifPixelFormat format = std::get<3>(GetParam());
  const avifPlanesFlags planes = std::get<4>(GetParam());
  const avifRange range = std::get<5>(GetParam());
  const bool create_icc = std::get<6>(GetParam());
  const bool create_exif = std::get<7>(GetParam());
  const bool create_xmp = std::get<8>(GetParam());
  const avifTransformFlags create_transform_flags = std::get<9>(GetParam());
  const bool create_hdr = std::get<10>(GetParam());

  ImagePtr image =
      testutil::CreateImage(width, height, depth, format, planes, range);
  ASSERT_NE(image, nullptr);
  testutil::FillImageGradient(image.get());  // The pixels do not matter.
  if (create_icc) {
    ASSERT_EQ(avifImageSetProfileICC(image.get(), testutil::kSampleIcc.data(),
                                     testutil::kSampleIcc.size()),
              AVIF_RESULT_OK);
  }
  if (create_exif) {
    size_t exif_tiff_header_offset;  // Must be 0 for 'mif3' brand.
    ASSERT_EQ(avifGetExifTiffHeaderOffset(testutil::kSampleExif.data(),
                                          testutil::kSampleExif.size(),
                                          &exif_tiff_header_offset),
              AVIF_RESULT_OK);
    ASSERT_EQ(
        avifImageSetMetadataExif(
            image.get(), testutil::kSampleExif.data() + exif_tiff_header_offset,
            testutil::kSampleExif.size() - exif_tiff_header_offset),
        AVIF_RESULT_OK);
  }
  if (create_xmp) {
    ASSERT_EQ(avifImageSetMetadataXMP(image.get(), testutil::kSampleXmp.data(),
                                      testutil::kSampleXmp.size()),
              AVIF_RESULT_OK);
  }
  image->transformFlags = create_transform_flags;
  if (create_transform_flags & AVIF_TRANSFORM_IROT) {
    image->irot.angle = 1;
  }
  if (create_transform_flags & AVIF_TRANSFORM_IMIR) {
    image->imir.axis = 0;
  }
  if (create_hdr) {
    image->gainMap = avifGainMapCreate();
    ASSERT_NE(image->gainMap, nullptr);
    image->gainMap->image =
        testutil::CreateImage(width, height, /*depth=*/8,
                              AVIF_PIXEL_FORMAT_YUV400, AVIF_PLANES_YUV,
                              AVIF_RANGE_FULL)
            .release();
    ASSERT_NE(image->gainMap->image, nullptr);
    testutil::FillImageGradient(image->gainMap->image);
  }

  // Encode.
  testutil::AvifRwData encoded_mini;
  EncoderPtr encoder(avifEncoderCreate());
  ASSERT_NE(encoder, nullptr);
  encoder->speed = AVIF_SPEED_FASTEST;
  encoder->headerFormat = AVIF_HEADER_REDUCED;
  ASSERT_EQ(avifEncoderWrite(encoder.get(), image.get(), &encoded_mini),
            AVIF_RESULT_OK);

  // Decode.
  ImagePtr decoded_mini(avifImageCreateEmpty());
  ASSERT_NE(decoded_mini, nullptr);
  DecoderPtr decoder_mini(avifDecoderCreate());
  ASSERT_NE(decoder_mini, nullptr);
  decoder_mini->imageContentToDecode |= AVIF_IMAGE_CONTENT_GAIN_MAP;
  ASSERT_EQ(avifDecoderReadMemory(decoder_mini.get(), decoded_mini.get(),
                                  encoded_mini.data, encoded_mini.size),
            AVIF_RESULT_OK);

  // Compare.
  testutil::AvifRwData encoded_meta =
      testutil::Encode(image.get(), encoder->speed);
  ASSERT_NE(encoded_meta.data, nullptr);
  // At least 200 bytes should be saved.
  EXPECT_LT(encoded_mini.size, encoded_meta.size - 200);

  ImagePtr decoded_meta(avifImageCreateEmpty());
  ASSERT_NE(decoded_meta, nullptr);
  DecoderPtr decoder_meta(avifDecoderCreate());
  ASSERT_NE(decoder_meta, nullptr);
  decoder_meta->imageContentToDecode |= AVIF_IMAGE_CONTENT_GAIN_MAP;
  ASSERT_EQ(avifDecoderReadMemory(decoder_meta.get(), decoded_meta.get(),
                                  encoded_meta.data, encoded_meta.size),
            AVIF_RESULT_OK);
  EXPECT_EQ(decoder_meta->image->gainMap != nullptr,
            decoder_mini->image->gainMap != nullptr);

  // Only the container changed. The pixels, features and metadata should be
  // identical.
  EXPECT_TRUE(
      testutil::AreImagesEqual(*decoded_meta.get(), *decoded_mini.get()));
  EXPECT_EQ(decoded_meta->gainMap != nullptr, decoded_mini->gainMap != nullptr);
  if (create_hdr) {
    ASSERT_NE(decoded_meta->gainMap, nullptr);
    ASSERT_NE(decoded_mini->gainMap, nullptr);
    ASSERT_NE(decoded_meta->gainMap->image, nullptr);
    ASSERT_NE(decoded_mini->gainMap->image, nullptr);
    EXPECT_TRUE(testutil::AreImagesEqual(*decoded_meta->gainMap->image,
                                         *decoded_mini->gainMap->image));
  }
}

INSTANTIATE_TEST_SUITE_P(OnePixel, AvifMinimizedImageBoxTest,
                         Combine(/*width=*/Values(1), /*height=*/Values(1),
                                 /*depth=*/Values(8),
                                 Values(AVIF_PIXEL_FORMAT_YUV444),
                                 Values(AVIF_PLANES_YUV, AVIF_PLANES_ALL),
                                 Values(AVIF_RANGE_LIMITED, AVIF_RANGE_FULL),
                                 /*create_icc=*/Values(false, true),
                                 /*create_exif=*/Values(false, true),
                                 /*create_xmp=*/Values(false, true),
                                 Values(AVIF_TRANSFORM_NONE),
                                 /*create_hdr=*/Values(false)));

INSTANTIATE_TEST_SUITE_P(
    DepthsSubsamplings, AvifMinimizedImageBoxTest,
    Combine(/*width=*/Values(12), /*height=*/Values(34),
            /*depth=*/Values(8, 10, 12),
            Values(AVIF_PIXEL_FORMAT_YUV444, AVIF_PIXEL_FORMAT_YUV422,
                   AVIF_PIXEL_FORMAT_YUV420, AVIF_PIXEL_FORMAT_YUV400),
            Values(AVIF_PLANES_ALL), Values(AVIF_RANGE_FULL),
            /*create_icc=*/Values(false), /*create_exif=*/Values(false),
            /*create_xmp=*/Values(false), Values(AVIF_TRANSFORM_NONE),
            /*create_hdr=*/Values(false)));

INSTANTIATE_TEST_SUITE_P(
    Dimensions, AvifMinimizedImageBoxTest,
    Combine(/*width=*/Values(127), /*height=*/Values(200), /*depth=*/Values(8),
            Values(AVIF_PIXEL_FORMAT_YUV444), Values(AVIF_PLANES_ALL),
            Values(AVIF_RANGE_FULL), /*create_icc=*/Values(true),
            /*create_exif=*/Values(true), /*create_xmp=*/Values(true),
            Values(AVIF_TRANSFORM_NONE), /*create_hdr=*/Values(false)));

INSTANTIATE_TEST_SUITE_P(
    Orientation, AvifMinimizedImageBoxTest,
    Combine(/*width=*/Values(16), /*height=*/Values(24), /*depth=*/Values(8),
            Values(AVIF_PIXEL_FORMAT_YUV444), Values(AVIF_PLANES_ALL),
            Values(AVIF_RANGE_FULL), /*create_icc=*/Values(true),
            /*create_exif=*/Values(true), /*create_xmp=*/Values(true),
            Values(AVIF_TRANSFORM_NONE, AVIF_TRANSFORM_IROT,
                   AVIF_TRANSFORM_IMIR,
                   AVIF_TRANSFORM_IROT | AVIF_TRANSFORM_IMIR),
            /*create_hdr=*/Values(false)));

INSTANTIATE_TEST_SUITE_P(
    Hdr, AvifMinimizedImageBoxTest,
    Combine(/*width=*/Values(8), /*height=*/Values(10), /*depth=*/Values(10),
            Values(AVIF_PIXEL_FORMAT_YUV420),
            Values(AVIF_PLANES_YUV, AVIF_PLANES_ALL), Values(AVIF_RANGE_FULL),
            /*create_icc=*/Values(false),
            /*create_exif=*/Values(false), /*create_xmp=*/Values(false),
            Values(AVIF_TRANSFORM_NONE), /*create_hdr=*/Values(true)));

//------------------------------------------------------------------------------

}  // namespace
}  // namespace avif
