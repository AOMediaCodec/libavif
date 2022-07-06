// Copyright 2022 Google LLC. All rights reserved.
// SPDX-License-Identifier: BSD-2-Clause

#include <array>
#include <tuple>

#include "avif/avif.h"
#include "aviftest_helpers.h"
#include "gtest/gtest.h"

using ::testing::Bool;
using ::testing::Combine;

namespace libavif {
namespace {

//------------------------------------------------------------------------------

// ICC color profiles are not checked by libavif so the content does not matter.
// This is a truncated widespread ICC color profile.
const std::array<uint8_t, 24> kSampleIcc = {
    0x00, 0x00, 0x02, 0x0c, 0x6c, 0x63, 0x6d, 0x73, 0x02, 0x10, 0x00, 0x00,
    0x6d, 0x6e, 0x74, 0x72, 0x52, 0x47, 0x42, 0x20, 0x58, 0x59, 0x5a, 0x20};

// Exif bytes are partially checked by libavif. This is a truncated widespread
// Exif metadata chunk.
const std::array<uint8_t, 24> kSampleExif = {
    0xff, 0x1,  0x45, 0x78, 0x69, 0x76, 0x32, 0xff, 0xe1, 0x12, 0x5a, 0x45,
    0x78, 0x69, 0x66, 0x0,  0x0,  0x49, 0x49, 0x2a, 0x0,  0x8,  0x0,  0x0};

// XMP bytes are not checked by libavif so the content does not matter.
// This is a truncated widespread XMP metadata chunk.
const std::array<uint8_t, 24> kSampleXmp = {
    0x3c, 0x3f, 0x78, 0x70, 0x61, 0x63, 0x6b, 0x65, 0x74, 0x20, 0x62, 0x65,
    0x67, 0x69, 0x6e, 0x3d, 0x22, 0xef, 0xbb, 0xbf, 0x22, 0x20, 0x69, 0x64};

//------------------------------------------------------------------------------

class MetadataTest
    : public testing::TestWithParam<
          std::tuple</*use_icc=*/bool, /*use_exif=*/bool, /*use_xmp=*/bool>> {};

// Encodes, decodes then verifies that the output metadata matches the input
// metadata defined by the parameters.
TEST_P(MetadataTest, EncodeDecode) {
  const bool use_icc = std::get<0>(GetParam());
  const bool use_exif = std::get<1>(GetParam());
  const bool use_xmp = std::get<2>(GetParam());

  testutil::AvifImagePtr image =
      testutil::CreateImage(/*width=*/12, /*height=*/34, /*depth=*/10,
                            AVIF_PIXEL_FORMAT_YUV444, AVIF_PLANES_ALL);
  ASSERT_NE(image, nullptr);
  testutil::FillImageGradient(image.get());  // The pixels do not matter.
  if (use_icc) {
    avifImageSetProfileICC(image.get(), kSampleIcc.data(), kSampleIcc.size());
  }
  if (use_exif) {
    avifImageSetMetadataExif(image.get(), kSampleExif.data(),
                             kSampleExif.size());
  }
  if (use_xmp) {
    avifImageSetMetadataXMP(image.get(), kSampleXmp.data(), kSampleXmp.size());
  }

  // Encode.
  testutil::AvifEncoderPtr encoder(avifEncoderCreate(), avifEncoderDestroy);
  ASSERT_NE(encoder, nullptr);
  encoder->speed = AVIF_SPEED_FASTEST;
  testutil::AvifRwData encoded_avif;
  ASSERT_EQ(avifEncoderWrite(encoder.get(), image.get(), &encoded_avif),
            AVIF_RESULT_OK);

  // Decode.
  testutil::AvifImagePtr decoded(avifImageCreateEmpty(), avifImageDestroy);
  ASSERT_NE(decoded, nullptr);
  testutil::AvifDecoderPtr decoder(avifDecoderCreate(), avifDecoderDestroy);
  ASSERT_NE(decoder, nullptr);
  ASSERT_EQ(avifDecoderReadMemory(decoder.get(), decoded.get(),
                                  encoded_avif.data, encoded_avif.size),
            AVIF_RESULT_OK);

  // Compare input and output metadata.
  if (use_icc) {
    ASSERT_EQ(decoded->icc.size, kSampleIcc.size());
    EXPECT_TRUE(
        std::equal(kSampleIcc.begin(), kSampleIcc.end(), decoded->icc.data));
  } else {
    EXPECT_EQ(decoded->icc.size, 0u);
  }
  if (use_exif) {
    ASSERT_EQ(decoded->exif.size, kSampleExif.size());
    EXPECT_TRUE(
        std::equal(kSampleExif.begin(), kSampleExif.end(), decoded->exif.data));
  } else {
    EXPECT_EQ(decoded->exif.size, 0u);
  }
  if (use_xmp) {
    ASSERT_EQ(decoded->xmp.size, kSampleXmp.size());
    EXPECT_TRUE(
        std::equal(kSampleXmp.begin(), kSampleXmp.end(), decoded->xmp.data));
  } else {
    EXPECT_EQ(decoded->xmp.size, 0u);
  }
}

INSTANTIATE_TEST_SUITE_P(All, MetadataTest,
                         Combine(/*use_icc=*/Bool(), /*use_exif=*/Bool(),
                                 /*use_xmp=*/Bool()));

}  // namespace
}  // namespace libavif
