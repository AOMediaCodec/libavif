// Copyright 2022 Google LLC. All rights reserved.
// SPDX-License-Identifier: BSD-2-Clause

#include <array>
#include <tuple>

#include "avif/avif.h"
#include "aviftest_helpers.h"
#include "avifutil.h"
#include "gtest/gtest.h"

using ::testing::Bool;
using ::testing::Combine;
using ::testing::Values;

namespace libavif {
namespace {

//------------------------------------------------------------------------------

// Used to pass the data folder path to the GoogleTest suites.
const char* data_path = nullptr;

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
// AVIF encode/decode metadata tests

class AvifMetadataTest
    : public testing::TestWithParam<
          std::tuple</*use_icc=*/bool, /*use_exif=*/bool, /*use_xmp=*/bool>> {};

// Encodes, decodes then verifies that the output metadata matches the input
// metadata defined by the parameters.
TEST_P(AvifMetadataTest, EncodeDecode) {
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
  EXPECT_TRUE(testutil::AreByteSequencesEqual(
      decoded->icc.data, decoded->icc.size, kSampleIcc.data(),
      use_icc ? kSampleIcc.size() : 0u));
  EXPECT_TRUE(testutil::AreByteSequencesEqual(
      decoded->exif.data, decoded->exif.size, kSampleExif.data(),
      use_exif ? kSampleExif.size() : 0u));
  EXPECT_TRUE(testutil::AreByteSequencesEqual(
      decoded->xmp.data, decoded->xmp.size, kSampleXmp.data(),
      use_xmp ? kSampleXmp.size() : 0u));
}

INSTANTIATE_TEST_SUITE_P(All, AvifMetadataTest,
                         Combine(/*use_icc=*/Bool(), /*use_exif=*/Bool(),
                                 /*use_xmp=*/Bool()));

//------------------------------------------------------------------------------
// Jpeg and PNG metadata tests

class MetadataTest
    : public testing::TestWithParam<
          std::tuple</*file_name=*/const char*, /*use_icc=*/bool,
                     /*use_exif=*/bool, /*use_xmp=*/bool, /*expect_icc=*/bool,
                     /*expect_exif=*/bool, /*expect_xmp=*/bool>> {};

// zTXt "Raw profile type exif" at the beginning of a PNG file.
TEST_P(MetadataTest, Read) {
  const std::string file_path =
      std::string(data_path) + "/" + std::get<0>(GetParam());
  const bool use_icc = std::get<1>(GetParam());
  const bool use_exif = std::get<2>(GetParam());
  const bool use_xmp = std::get<3>(GetParam());
  const bool expect_icc = std::get<4>(GetParam());
  const bool expect_exif = std::get<5>(GetParam());
  const bool expect_xmp = std::get<6>(GetParam());

  avifImage* image = avifImageCreateEmpty();
  ASSERT_NE(image, nullptr);
  image->matrixCoefficients = AVIF_MATRIX_COEFFICIENTS_IDENTITY;  // lossless
  ASSERT_NE(
      avifReadImage(file_path.c_str(), AVIF_PIXEL_FORMAT_NONE, 0, !use_icc,
                    !use_exif, !use_xmp, image, nullptr, nullptr, nullptr),
      AVIF_APP_FILE_FORMAT_UNKNOWN);
  EXPECT_NE(image->width * image->height, 0u);
  if (expect_icc) {
    EXPECT_NE(image->icc.size, 0u);
    EXPECT_NE(image->icc.data, nullptr);
  } else {
    EXPECT_EQ(image->icc.size, 0u);
    EXPECT_EQ(image->icc.data, nullptr);
  }
  if (expect_exif) {
    EXPECT_NE(image->exif.size, 0u);
    EXPECT_NE(image->exif.data, nullptr);
  } else {
    EXPECT_EQ(image->exif.size, 0u);
    EXPECT_EQ(image->exif.data, nullptr);
  }
  if (expect_xmp) {
    EXPECT_NE(image->xmp.size, 0u);
    EXPECT_NE(image->xmp.data, nullptr);
  } else {
    EXPECT_EQ(image->xmp.size, 0u);
    EXPECT_EQ(image->xmp.data, nullptr);
  }
}

INSTANTIATE_TEST_SUITE_P(
    PngNone, MetadataTest,
    Combine(Values("paris_exif_xmp_icc.png"),  // zTXt iCCP iTXt IDAT
            /*use_icc=*/Values(false), /*use_exif=*/Values(false),
            /*use_xmp=*/Values(false),
            // ignoreICC is not yet implemented.
            /*expected_icc=*/Values(true),
            /*expected_exif=*/Values(false), /*expected_xmp=*/Values(false)));
INSTANTIATE_TEST_SUITE_P(
    PngAll, MetadataTest,
    Combine(Values("paris_exif_xmp_icc.png"), /*use_icc=*/Values(true),
            /*use_exif=*/Values(true), /*use_xmp=*/Values(true),
            /*expected_icc=*/Values(true), /*expected_exif=*/Values(true),
            // XMP extraction is not yet implemented.
            /*expected_xmp=*/Values(false)));

INSTANTIATE_TEST_SUITE_P(
    PngExifAtEnd, MetadataTest,
    Combine(Values("paris_exif_at_end.png"),  // iCCP IDAT eXIf
            /*use_icc=*/Values(true), /*use_exif=*/Values(true),
            /*use_xmp=*/Values(true), /*expected_icc=*/Values(true),
            /*expected_exif=*/Values(true), /*expected_xmp=*/Values(false)));

INSTANTIATE_TEST_SUITE_P(
    Jpeg, MetadataTest,
    Combine(Values("paris_exif_xmp_icc.jpg"), /*use_icc=*/Values(true),
            /*use_exif=*/Values(true), /*use_xmp=*/Values(true),
            /*expected_icc=*/Values(true),
            // Exif and XMP are not yet implemented.
            /*expected_exif=*/Values(false), /*expected_xmp=*/Values(false)));

// Verify all parsers lead exactly to the same metadata bytes.
TEST(MetadataTest, Compare) {
  constexpr const char* kFileNames[] = {"paris_exif_at_end.png",
                                        "paris_exif_xmp_icc.jpg",
                                        "paris_exif_xmp_icc.png"};
  avifImage* images[sizeof(kFileNames) / sizeof(kFileNames[0])];
  avifImage** image_it = images;
  for (const char* file_name : kFileNames) {
    const std::string file_path = std::string(data_path) + "/" + file_name;

    *image_it = avifImageCreateEmpty();
    ASSERT_NE(*image_it, nullptr);
    ASSERT_NE(avifReadImage(file_path.c_str(), AVIF_PIXEL_FORMAT_NONE, 0,
                            /*ignoreICC=*/false, /*ignoreExif=*/false,
                            /*ignoreXMP=*/false, *image_it, nullptr, nullptr,
                            nullptr),
              AVIF_APP_FILE_FORMAT_UNKNOWN);
    ++image_it;
  }

  for (avifImage* image : images) {
    if (image->exif.size != 0) {  // Not implemented for JPEG.
      EXPECT_TRUE(
          testutil::AreByteSequencesEqual(image->exif, images[0]->exif));
    }
    if (image->xmp.size != 0) {  // Not implemented.
      EXPECT_TRUE(testutil::AreByteSequencesEqual(image->xmp, images[0]->xmp));
    }
    EXPECT_TRUE(testutil::AreByteSequencesEqual(image->icc, images[0]->icc));
  }
}

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
