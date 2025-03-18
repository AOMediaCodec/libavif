// Copyright 2022 Google LLC
// SPDX-License-Identifier: BSD-2-Clause

#include <string>

#include "avif/avif.h"
#include "avifjpeg.h"
#include "avifpng.h"
#include "aviftest_helpers.h"
#include "avifutil.h"
#include "gtest/gtest.h"
#include "iccmaker.h"

namespace avif {
namespace {

// Used to pass the data folder path to the GoogleTest suites.
const char* data_path = nullptr;

//------------------------------------------------------------------------------
// Generic tests

bool AreSamplesEqualForAllReadSettings(const char* file_name1,
                                       const char* file_name2) {
  constexpr bool kIgnoreMetadata = true;
  for (avifPixelFormat requested_format :
       {AVIF_PIXEL_FORMAT_YUV444, AVIF_PIXEL_FORMAT_YUV422,
        AVIF_PIXEL_FORMAT_YUV420, AVIF_PIXEL_FORMAT_YUV400}) {
    for (int requested_depth : {8, 10, 12, 16}) {
      for (avifChromaDownsampling chroma_downsampling :
           {AVIF_CHROMA_DOWNSAMPLING_AUTOMATIC,
            AVIF_CHROMA_DOWNSAMPLING_FASTEST,
            AVIF_CHROMA_DOWNSAMPLING_BEST_QUALITY,
            AVIF_CHROMA_DOWNSAMPLING_AVERAGE}) {
        const ImagePtr image1 = testutil::ReadImage(
            data_path, file_name1, requested_format, requested_depth,
            chroma_downsampling, kIgnoreMetadata, kIgnoreMetadata,
            kIgnoreMetadata);
        const ImagePtr image2 = testutil::ReadImage(
            data_path, file_name2, requested_format, requested_depth,
            chroma_downsampling, kIgnoreMetadata, kIgnoreMetadata,
            kIgnoreMetadata);
        if (!image1 || !image2 || !testutil::AreImagesEqual(*image1, *image2)) {
          return false;
        }
      }
    }
  }
  return true;
}

TEST(JpegTest, ReadAllSubsamplingsAndAllBitDepths) {
  EXPECT_TRUE(AreSamplesEqualForAllReadSettings(
      "paris_exif_xmp_icc.jpg", "paris_exif_orientation_5.jpg"));
}

TEST(PngTest, ReadAllSubsamplingsAndAllBitDepths) {
  EXPECT_TRUE(AreSamplesEqualForAllReadSettings(
      "paris_icc_exif_xmp.png", "paris_icc_exif_xmp_at_end.png"));
}

//------------------------------------------------------------------------------
// PNG color metadata handling tests

// Verify we can read a PNG file with PNG_COLOR_TYPE_PALETTE and a tRNS chunk.
TEST(PngTest, PaletteColorTypeWithTrnsChunk) {
  const ImagePtr image = testutil::ReadImage(data_path, "draw_points.png",
                                             AVIF_PIXEL_FORMAT_YUV444, 8);
  ASSERT_NE(image, nullptr);
  EXPECT_EQ(image->width, 33u);
  EXPECT_EQ(image->height, 11u);
  EXPECT_NE(image->alphaPlane, nullptr);
}

// Verify we can read a PNG file with PNG_COLOR_TYPE_RGB and a tRNS chunk
// after a PLTE chunk.
TEST(PngTest, RgbColorTypeWithTrnsAfterPlte) {
  const ImagePtr image = testutil::ReadImage(
      data_path, "circle-trns-after-plte.png", AVIF_PIXEL_FORMAT_YUV444, 8);
  ASSERT_NE(image, nullptr);
  EXPECT_EQ(image->width, 100u);
  EXPECT_EQ(image->height, 60u);
  EXPECT_NE(image->alphaPlane, nullptr);
}

// Verify we can read a PNG file with PNG_COLOR_TYPE_RGB and a tRNS chunk
// before a PLTE chunk, with no MSan use-of-uninitialized-value warnings in
// avifImageRGBToYUV(). libpng 1.6.46 or older considers the tRNS chunk as
// invalid and ignores it, so the decoded image has no alpha. The behavior
// changed starting with libpng 1.6.47 (the decoded image has alpha).
// See https://github.com/pnggroup/libpng/blob/libpng16/CHANGES#L6243-L6246.
TEST(PngTest, RgbColorTypeWithTrnsBeforePlte) {
  const ImagePtr image = testutil::ReadImage(
      data_path, "circle-trns-before-plte.png", AVIF_PIXEL_FORMAT_YUV444, 8);
  ASSERT_NE(image, nullptr);
  EXPECT_EQ(image->width, 100u);
  EXPECT_EQ(image->height, 60u);
}

constexpr size_t kColorProfileSize = 376;
constexpr size_t kGrayProfileSize = 275;

// Verify we can read a color PNG file tagged as gamma 2.2 through gAMA chunk,
// and set transfer characteristics correctly.
TEST(PngTest, ColorGamma22) {
  const ImagePtr image = testutil::ReadImage(data_path, "ffffcc-gamma2.2.png");
  ASSERT_NE(image, nullptr);

  // gamma 2.2 should match BT470M
  EXPECT_EQ(image->transferCharacteristics,
            AVIF_TRANSFER_CHARACTERISTICS_BT470M);

  // should not generate ICC profile
  EXPECT_EQ(image->icc.size, 0u);
}

// Verify that color info does not get overwritten if allow_changing_cicp is
// false.
TEST(PngTest, ColorGamma22ForbitChangingCicp) {
  const ImagePtr image = testutil::ReadImage(
      data_path, "ffffcc-gamma2.2.png",
      /*requested_format=*/AVIF_PIXEL_FORMAT_NONE,
      /*requested_depth=*/0, AVIF_CHROMA_DOWNSAMPLING_AUTOMATIC,
      /*ignore_icc=*/false, /*ignore_exif*/ false,
      /*ignore_xmp=*/false, /*allow_changing_cicp=*/false);
  ASSERT_NE(image, nullptr);

  // Color info should still be unspecified even if file gamma is 2.2
  EXPECT_EQ(image->colorPrimaries, AVIF_COLOR_PRIMARIES_UNSPECIFIED);
  EXPECT_EQ(image->transferCharacteristics,
            AVIF_TRANSFER_CHARACTERISTICS_UNSPECIFIED);

  // should not generate ICC profile
  EXPECT_EQ(image->icc.size, 0u);
}

// Verify we can read a color PNG file tagged as gamma 1.6 through gAMA chunk,
// and generate a color profile for it.
TEST(PngTest, ColorGamma16) {
  const ImagePtr image = testutil::ReadImage(data_path, "ffffcc-gamma1.6.png");
  ASSERT_NE(image, nullptr);

  // if ICC profile generated, CP and TC should be set to unspecified
  EXPECT_EQ(image->colorPrimaries, AVIF_COLOR_PRIMARIES_UNSPECIFIED);
  EXPECT_EQ(image->transferCharacteristics,
            AVIF_TRANSFER_CHARACTERISTICS_UNSPECIFIED);

  // should generate a color profile
  EXPECT_EQ(image->icc.size, kColorProfileSize);

  // Generated profile is tested in test_cmd_icc_profile.sh
}

// Verify we can read a gray PNG file tagged as gamma 2.2 through gAMA chunk,
// and set transfer characteristics correctly.
TEST(PngTest, GrayGamma22) {
  const ImagePtr image = testutil::ReadImage(data_path, "ffffff-gamma2.2.png",
                                             AVIF_PIXEL_FORMAT_YUV400);
  ASSERT_NE(image, nullptr);

  // gamma 2.2 should match BT470M
  EXPECT_EQ(image->transferCharacteristics,
            AVIF_TRANSFER_CHARACTERISTICS_BT470M);

  // should not generate ICC profile
  EXPECT_EQ(image->icc.size, 0u);
}

// Verify we can read a gray PNG file tagged as gamma 1.6 through gAMA chunk,
// and generate a gray profile for it.
TEST(PngTest, GrayGamma16) {
  const ImagePtr image = testutil::ReadImage(data_path, "ffffff-gamma1.6.png",
                                             AVIF_PIXEL_FORMAT_YUV400);
  ASSERT_NE(image, nullptr);

  // if ICC profile generated, CP and TC should be set to unspecified
  EXPECT_EQ(image->colorPrimaries, AVIF_COLOR_PRIMARIES_UNSPECIFIED);
  EXPECT_EQ(image->transferCharacteristics,
            AVIF_TRANSFER_CHARACTERISTICS_UNSPECIFIED);

  // should generate a gray profile
  EXPECT_EQ(image->icc.size, kGrayProfileSize);

  // Generated profile is tested in test_cmd_icc_profile.sh
}

// Verify we can read a color PNG file tagged as sRGB through sRGB chunk,
// and set color primaries and transfer characteristics correctly.
TEST(PngTest, SRGBTagged) {
  const ImagePtr image = testutil::ReadImage(data_path, "ffffcc-srgb.png");
  ASSERT_NE(image, nullptr);

  // should set to BT709 primaries and SRGB transfer
  EXPECT_EQ(image->colorPrimaries, AVIF_COLOR_PRIMARIES_BT709);
  EXPECT_EQ(image->transferCharacteristics, AVIF_TRANSFER_CHARACTERISTICS_SRGB);

  // should not generate ICC profile
  EXPECT_EQ(image->icc.size, 0u);
}

// Verify we are not generating profile if asked to ignore it.
TEST(PngTest, IgnoreProfile) {
  const ImagePtr image = testutil::ReadImage(
      data_path, "ffffcc-gamma1.6.png", AVIF_PIXEL_FORMAT_NONE, 0,
      AVIF_CHROMA_DOWNSAMPLING_AUTOMATIC, true);
  ASSERT_NE(image, nullptr);

  // should be left unspecified
  EXPECT_EQ(image->colorPrimaries, AVIF_COLOR_PRIMARIES_UNSPECIFIED);
  EXPECT_EQ(image->transferCharacteristics, AVIF_COLOR_PRIMARIES_UNSPECIFIED);

  // should not generate ICC profile
  EXPECT_EQ(image->icc.size, 0u);
}

// Verify we can read a PNG file tagged as gamma 2.2 through gAMA chunk
// and BT709 primaries through cHRM chunk,
// and set color primaries and transfer characteristics correctly.
TEST(PngTest, BT709Gamma22) {
  const ImagePtr image =
      testutil::ReadImage(data_path, "ArcTriomphe-cHRM-orig.png");
  ASSERT_NE(image, nullptr);

  // primaries should match BT709
  EXPECT_EQ(image->colorPrimaries, AVIF_COLOR_PRIMARIES_BT709);

  // gamma 2.2 should match BT470M
  EXPECT_EQ(image->transferCharacteristics,
            AVIF_TRANSFER_CHARACTERISTICS_BT470M);

  // should not generate ICC profile
  EXPECT_EQ(image->icc.size, 0u);
}

// Verify we can read a PNG file tagged as gamma 2.2 through gAMA chunk
// and BT709 primaries with red and green swapped through cHRM chunk,
// and generate a color profile for it.
TEST(PngTest, BT709SwappedGamma22) {
  const ImagePtr image =
      testutil::ReadImage(data_path, "ArcTriomphe-cHRM-red-green-swap.png");
  ASSERT_NE(image, nullptr);

  // if ICC profile generated, CP and TC should be set to unspecified
  EXPECT_EQ(image->colorPrimaries, AVIF_COLOR_PRIMARIES_UNSPECIFIED);
  EXPECT_EQ(image->transferCharacteristics,
            AVIF_TRANSFER_CHARACTERISTICS_UNSPECIFIED);

  // should generate a color profile
  EXPECT_EQ(image->icc.size, kColorProfileSize);

  // Generated profile is tested in test_cmd_icc_profile.sh
}

//------------------------------------------------------------------------------
// ICC metadata tests

constexpr size_t kChecksumOffset = 0x54;

// Verify we wrote correct hash in generated ICC profile.
TEST(ICCTest, GeneratedICCHash) {
  float primariesCoords[8];
  avifColorPrimariesGetValues(AVIF_COLOR_PRIMARIES_BT709, primariesCoords);

  testutil::AvifRwData icc;
  EXPECT_EQ(avifGenerateRGBICC(&icc, 2.2f, primariesCoords), AVIF_TRUE);
  // Steps to generate this checksum:
  // - memset 16 bytes starting from icc.data + kChecksumOffset to 0
  // - write `icc` to file
  // - run `md5sum` with the written file
  const uint8_t expectedChecksumRGB[] = {
      // 89b06c4cc611c3110c022e06e6a0f81b
      0x89, 0xb0, 0x6c, 0x4c, 0xc6, 0x11, 0xc3, 0x11,
      0x0c, 0x02, 0x2e, 0x06, 0xe6, 0xa0, 0xf8, 0x1b,
  };
  EXPECT_EQ(memcmp(icc.data + kChecksumOffset, expectedChecksumRGB,
                   sizeof(expectedChecksumRGB)),
            0);

  EXPECT_EQ(avifGenerateGrayICC(&icc, 2.2f, primariesCoords), AVIF_TRUE);
  // Steps to generate this checksum:
  // - memset 16 bytes starting from icc.data + kChecksumOffset to 0
  // - write `icc` to file
  // - run `md5sum` with the written file
  const uint8_t expectedChecksumGray[] = {
      // 7610e64f148ebe4d00cafa56cf45aea0
      0x76, 0x10, 0xe6, 0x4f, 0x14, 0x8e, 0xbe, 0x4d,
      0x00, 0xca, 0xfa, 0x56, 0xcf, 0x45, 0xae, 0xa0,
  };
  EXPECT_EQ(memcmp(icc.data + kChecksumOffset, expectedChecksumGray,
                   sizeof(expectedChecksumGray)),
            0);
}

// Simpler function to read an image.
static avifAppFileFormat avifReadImageForRGB2Gray2RGB(const std::string& path,
                                                      avifPixelFormat format,
                                                      bool ignore_icc,
                                                      ImagePtr& image) {
  return avifReadImage(
      path.c_str(), format, /*requestedDepth=*/0,
      /*chromaDownsampling=*/AVIF_CHROMA_DOWNSAMPLING_AUTOMATIC,
      /*ignoreColorProfile=*/ignore_icc, /*ignoreExif=*/false,
      /*ignoreXMP=*/false, /*allowChangingCicp=*/true,
      /*ignoreGainMap=*/true, AVIF_DEFAULT_IMAGE_SIZE_LIMIT, image.get(),
      /*outDepth=*/nullptr, /*sourceTiming=*/nullptr,
      /*frameIter=*/nullptr);
}

// Verify the invalidity of keeping the ICC profile for a gray/color image read
// from a color/gray image.
TEST(ICCTest, RGB2Gray2RGB) {
  constexpr char file_name[] = "paris_icc_exif_xmp.png";
  const std::string file_path = std::string(data_path) + file_name;

  for (auto format : {AVIF_PIXEL_FORMAT_YUV400, AVIF_PIXEL_FORMAT_YUV444}) {
    // Read the ground truth image in the appropriate format.
    ImagePtr image(avifImageCreateEmpty());
    ASSERT_NE(image, nullptr);
    ASSERT_NE(avifReadImageForRGB2Gray2RGB(file_path, format,
                                           /*ignore_icc=*/true, image),
              AVIF_APP_FILE_FORMAT_UNKNOWN);

    // Add an ICC profile.
    float primariesCoords[8];
    avifColorPrimariesGetValues(AVIF_COLOR_PRIMARIES_BT709, primariesCoords);

    testutil::AvifRwData icc;
    if (format == AVIF_PIXEL_FORMAT_YUV400) {
      EXPECT_EQ(avifGenerateGrayICC(&icc, 2.2f, primariesCoords), AVIF_TRUE);
    } else {
      EXPECT_EQ(avifGenerateRGBICC(&icc, 2.2f, primariesCoords), AVIF_TRUE);
    }
    ASSERT_EQ(avifImageSetProfileICC(image.get(), icc.data, icc.size),
              AVIF_RESULT_OK);

    for (const std::string ext : {"jpg", "png"}) {
      // Write the image with the appropriate codec.
      const std::string new_path =
          testing::TempDir() + "tmp_RGB2Gray2RGB." + ext;
      if (ext == "jpg") {
        ASSERT_EQ(
            avifJPEGWrite(new_path.c_str(), image.get(), /*jpegQuality=*/75,
                          AVIF_CHROMA_UPSAMPLING_BEST_QUALITY),
            AVIF_TRUE);
      } else {
        ASSERT_EQ(
            avifPNGWrite(new_path.c_str(), image.get(), /*requestedDepth=*/0,
                         AVIF_CHROMA_UPSAMPLING_BEST_QUALITY,
                         /*compressionLevel=*/0),
            AVIF_TRUE);
      }

      for (bool ignore_icc : {false, true}) {
        for (auto new_format :
             {AVIF_PIXEL_FORMAT_YUV400, AVIF_PIXEL_FORMAT_YUV444}) {
          ImagePtr new_image(avifImageCreateEmpty());
          ASSERT_NE(new_image, nullptr);
          const avifAppFileFormat new_file_format =
              avifReadImageForRGB2Gray2RGB(new_path, new_format, ignore_icc,
                                           new_image);
          if (format == new_format || ignore_icc) {
            ASSERT_NE(new_file_format, AVIF_APP_FILE_FORMAT_UNKNOWN);
          } else {
            // When formats are different, the ICC cannot be kept.
            ASSERT_EQ(new_file_format, AVIF_APP_FILE_FORMAT_UNKNOWN);
          }
        }
      }
    }
  }
}

TEST(GrayTest, Roundtrip) {
  constexpr char file_name[] = "paris_icc_exif_xmp.png";
  const std::string file_path = std::string(data_path) + file_name;

  // Read the ground truth image as gray.
  ImagePtr image(avifImageCreateEmpty());
  ASSERT_NE(image, nullptr);
  ASSERT_NE(avifReadImageForRGB2Gray2RGB(file_path, AVIF_PIXEL_FORMAT_YUV400,
                                         /*ignore_icc=*/true, image),
            AVIF_APP_FILE_FORMAT_UNKNOWN);
  for (const std::string ext : {"jpg", "png"}) {
    // Write the image with the appropriate codec.
    const std::string new_path = testing::TempDir() + std::string(file_name) +
                                 "_tmp_GrayTestRoundtrip." + ext;
    if (ext == "png") {
      ASSERT_EQ(
          avifPNGWrite(new_path.c_str(), image.get(), /*requestedDepth=*/0,
                       AVIF_CHROMA_UPSAMPLING_BEST_QUALITY,
                       /*compressionLevel=*/0),
          AVIF_TRUE);
    } else {
      ASSERT_EQ(avifJPEGWrite(new_path.c_str(), image.get(), /*jpegQuality=*/99,
                              AVIF_CHROMA_UPSAMPLING_BEST_QUALITY),
                AVIF_TRUE);
    }

    // Read the image back without specifying the color space.
    ImagePtr rt_image(avifImageCreateEmpty());
    ASSERT_NE(rt_image, nullptr);
    const avifAppFileFormat new_file_format = avifReadImageForRGB2Gray2RGB(
        new_path, AVIF_PIXEL_FORMAT_NONE, /*ignore_icc=*/true, rt_image);
    ASSERT_NE(new_file_format, AVIF_APP_FILE_FORMAT_UNKNOWN);
    ASSERT_EQ(rt_image->yuvFormat, AVIF_PIXEL_FORMAT_YUV400);
    if (ext == "png") {
      ASSERT_TRUE(testutil::AreImagesEqual(*image, *rt_image));
    } else {
      ASSERT_GT(testutil::GetPsnr(*image, *rt_image, /*ignore_alpha=*/false),
                60);
    }
  }
}

TEST(GrayAlphaTest, Roundtrip) {
  constexpr char file_name[] = "paris_icc_exif_xmp.png";
  const std::string file_path = std::string(data_path) + file_name;

  for (bool use_alpha : {false, true}) {
    // Read the ground truth image as gray.
    ImagePtr image(avifImageCreateEmpty());
    ASSERT_NE(image, nullptr);
    ASSERT_NE(avifReadImageForRGB2Gray2RGB(file_path, AVIF_PIXEL_FORMAT_YUV400,
                                           /*ignore_icc=*/true, image),
              AVIF_APP_FILE_FORMAT_UNKNOWN);
    if (use_alpha) {
      // Have alpha point to the Y plane, just to get some data.
      image->alphaRowBytes = image->yuvRowBytes[AVIF_CHAN_Y];
      const size_t size = image->height * image->alphaRowBytes;
      image->alphaPlane = (uint8_t*)avifAlloc(size);
      ASSERT_NE(image->alphaPlane, nullptr);
      image->imageOwnsAlphaPlane = AVIF_TRUE;
      for (size_t i = 0; i < size; ++i) image->alphaPlane[i] = i % 256;
    }

    // Write the image.
    const std::string new_path =
        testing::TempDir() + "tmp_GrayTestRoundtrip.png";
    ASSERT_EQ(avifPNGWrite(new_path.c_str(), image.get(), /*requestedDepth=*/0,
                           AVIF_CHROMA_UPSAMPLING_BEST_QUALITY,
                           /*compressionLevel=*/0),
              AVIF_TRUE);

    // Read the image back with the default color space.
    ImagePtr rt_image(avifImageCreateEmpty());
    ASSERT_NE(rt_image, nullptr);
    const avifAppFileFormat new_file_format = avifReadImageForRGB2Gray2RGB(
        new_path, AVIF_PIXEL_FORMAT_NONE, /*ignore_icc=*/true, rt_image);
    ASSERT_NE(new_file_format, AVIF_APP_FILE_FORMAT_UNKNOWN);
    ASSERT_EQ(rt_image->yuvFormat, AVIF_PIXEL_FORMAT_YUV400);
    ASSERT_TRUE(testutil::AreImagesEqual(*image, *rt_image));
  }
}

//------------------------------------------------------------------------------
// Memory management tests

TEST(ImageSizeLimitTest, AllFormats) {
  for (const uint32_t image_size_limit :
       {1u, std::numeric_limits<uint32_t>::max()}) {
    for (const char* filename :
         {"paris_exif_xmp_icc.jpg", "paris_icc_exif_xmp.png",
          "cosmos1650_yuv444_10bpc_p3pq.y4m"}) {
      const std::string filepath = std::string(data_path) + filename;
      ImagePtr image(avifImageCreateEmpty());
      ASSERT_NE(image, nullptr);

      const avifAppFileFormat format = avifReadImage(
          filepath.c_str(), AVIF_PIXEL_FORMAT_NONE, /*requestedDepth=*/0,
          AVIF_CHROMA_DOWNSAMPLING_AUTOMATIC, /*ignoreColorProfile=*/true,
          /*ignoreExif=*/true, /*ignoreXMP=*/true, /*allowChangingCicp=*/true,
          /*ignoreGainMap=*/true, image_size_limit, image.get(),
          /*outDepth=*/nullptr, /*sourceTiming=*/nullptr,
          /*frameIter=*/nullptr);
      if (image_size_limit == 1) {
        EXPECT_EQ(format, AVIF_APP_FILE_FORMAT_UNKNOWN);
      } else {
        EXPECT_NE(format, AVIF_APP_FILE_FORMAT_UNKNOWN);
      }
    }
  }
}

//------------------------------------------------------------------------------

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
