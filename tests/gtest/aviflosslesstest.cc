// Copyright 2023 Google LLC
// SPDX-License-Identifier: BSD-2-Clause

#include <tuple>

#include "avif/avif.h"
#include "aviftest_helpers.h"
#include "avifutil.h"
#include "gtest/gtest.h"

namespace avif {
namespace {

// Used to pass the data folder path to the GoogleTest suites.
const char* data_path = nullptr;

// Tests that AVIF_MATRIX_COEFFICIENTS_YCGCO_RO does not work because the input
// depth is not odd.
TEST(LosslessTest, YCGCO_RO) {
  const std::string file_path =
      std::string(data_path) + "paris_icc_exif_xmp.png";
  ImagePtr image(avifImageCreateEmpty());
  ASSERT_NE(image, nullptr);
  image->matrixCoefficients = AVIF_MATRIX_COEFFICIENTS_YCGCO_RO;
  const avifAppFileFormat file_format = avifReadImage(
      file_path.c_str(), /*requestedFormat=*/AVIF_PIXEL_FORMAT_NONE,
      /*requestedDepth=*/0,
      /*chromaDownsampling=*/AVIF_CHROMA_DOWNSAMPLING_AUTOMATIC,
      /*ignoreColorProfile=*/false, /*ignoreExif=*/false, /*ignoreXMP=*/false,
      /*allowChangingCicp=*/true, /*ignoreGainMap=*/true,
      AVIF_DEFAULT_IMAGE_SIZE_LIMIT, image.get(), /*outDepth=*/nullptr,
      /*sourceTiming=*/nullptr, /*frameIter=*/nullptr);
  ASSERT_EQ(file_format, AVIF_APP_FILE_FORMAT_UNKNOWN);
}

////////////////////////////////////////////////////////////////////////////////

// Reads an image with a simpler API. ASSERT_NO_FATAL_FAILURE should be used
// when calling it.
// If image is set to nullptr, it is because AVIF_MATRIX_COEFFICIENTS_IDENTITY
// is requested with AVIF_PIXEL_FORMAT_YUV420.
void ReadImageSimple(const std::string& file_path, avifPixelFormat pixel_format,
                     avifMatrixCoefficients matrix_coefficients,
                     avifBool ignore_icc, ImagePtr& image) {
  image.reset(avifImageCreateEmpty());
  ASSERT_NE(image, nullptr);
  image->matrixCoefficients = matrix_coefficients;
  const avifAppFileFormat file_format = avifReadImage(
      file_path.c_str(), /*requestedFormat=*/pixel_format, /*requestedDepth=*/0,
      /*chromaDownsampling=*/AVIF_CHROMA_DOWNSAMPLING_AUTOMATIC,
      /*ignoreColorProfile=*/ignore_icc, /*ignoreExif=*/false,
      /*ignoreXMP=*/false, /*allowChangingCicp=*/true, /*ignoreGainMap=*/true,
      AVIF_DEFAULT_IMAGE_SIZE_LIMIT, image.get(), /*outDepth=*/nullptr,
      /*sourceTiming=*/nullptr, /*frameIter=*/nullptr);
  if (matrix_coefficients == AVIF_MATRIX_COEFFICIENTS_IDENTITY &&
      image->yuvFormat == AVIF_PIXEL_FORMAT_YUV420) {
    // 420 cannot be converted from RGB to YUV with
    // AVIF_MATRIX_COEFFICIENTS_IDENTITY due to a decision taken in
    // avifGetYUVColorSpaceInfo.
    ASSERT_EQ(file_format, AVIF_APP_FILE_FORMAT_UNKNOWN);
    image.reset();
    return;
  }
  ASSERT_NE(file_format, AVIF_APP_FILE_FORMAT_UNKNOWN);
}

// Checks whether an image is grayscale. ASSERT_NO_FATAL_FAILURE should be used
// when calling it.
void GetIsGray(const std::string& path, bool& is_gray) {
  ImagePtr image(avifImageCreateEmpty());
  ASSERT_NE(image, nullptr);
  image->matrixCoefficients = AVIF_MATRIX_COEFFICIENTS_UNSPECIFIED;
  ASSERT_NE(
      AVIF_APP_FILE_FORMAT_UNKNOWN,
      avifReadImage(path.c_str(), AVIF_PIXEL_FORMAT_NONE, /*requestedDepth=*/0,
                    /*chromaDownsampling=*/AVIF_CHROMA_DOWNSAMPLING_AUTOMATIC,
                    /*ignoreColorProfile=*/true, /*ignoreExif=*/true,
                    /*ignoreXMP=*/true,
                    /*allowChangingCicp=*/true, /*ignoreGainMap=*/true,
                    AVIF_DEFAULT_IMAGE_SIZE_LIMIT, image.get(),
                    /*outDepth=*/nullptr, /*sourceTiming=*/nullptr,
                    /*frameIter=*/nullptr));
  is_gray = image->yuvFormat == AVIF_PIXEL_FORMAT_YUV400;
}

// The test parameters are: the file path, the matrix coefficients and the pixel
// format.
class EncodeDecodeMemory
    : public testing::TestWithParam<std::tuple<std::string, int, int>> {};

// Tests encode/decode round trips in memory.
TEST_P(EncodeDecodeMemory, RoundTrip) {
  const std::string& file_path =
      std::string(data_path) + std::get<0>(GetParam());
  const avifMatrixCoefficients matrix_coefficients =
      static_cast<avifMatrixCoefficients>(std::get<1>(GetParam()));
  const avifPixelFormat pixel_format =
      static_cast<avifPixelFormat>(std::get<2>(GetParam()));

  // Check if the input image is grayscale.
  bool gt_is_gray = false;
  ASSERT_NO_FATAL_FAILURE(GetIsGray(file_path, gt_is_gray));

  // Ignore ICC when going from RGB to gray or gray to RGB.
  avifBool ignore_icc;
  if (gt_is_gray && pixel_format != AVIF_PIXEL_FORMAT_YUV400 &&
      pixel_format != AVIF_PIXEL_FORMAT_NONE) {
    ignore_icc = AVIF_TRUE;
  } else if (!gt_is_gray && pixel_format == AVIF_PIXEL_FORMAT_YUV400) {
    ignore_icc = AVIF_TRUE;
  } else {
    ignore_icc = AVIF_FALSE;
  }

  // Read a ground truth image but do not care about the matrix coefficients: we
  // just want data.
  avifMatrixCoefficients gt_matrix_coefficients;
  if (gt_is_gray) {
    // gray to gray or RGB does not require MC.
    gt_matrix_coefficients = AVIF_MATRIX_COEFFICIENTS_UNSPECIFIED;
  } else if (pixel_format != AVIF_PIXEL_FORMAT_YUV400) {
    // RGB to RGB is done with identity to be lossless.
    gt_matrix_coefficients = AVIF_MATRIX_COEFFICIENTS_IDENTITY;
  } else {
    // RGB to gray depends on the MC so use the input one.
    gt_matrix_coefficients = matrix_coefficients;
  }
  ImagePtr image;
  ASSERT_NO_FATAL_FAILURE(ReadImageSimple(
      file_path, pixel_format, gt_matrix_coefficients, ignore_icc, image));
  // ReadImageSimple does not set image and does not trigger an assert for the
  // unsupported case of AVIF_MATRIX_COEFFICIENTS_IDENTITY + 420 only. Hence
  // stop the test here.
  if (image == nullptr) return;

  // Encode.
  EncoderPtr encoder(avifEncoderCreate());
  ASSERT_NE(encoder, nullptr);
  encoder->speed = AVIF_SPEED_FASTEST;
  encoder->quality = AVIF_QUALITY_LOSSLESS;
  testutil::AvifRwData encoded;
  image->matrixCoefficients = matrix_coefficients;
  avifResult result = avifEncoderWrite(encoder.get(), image.get(), &encoded);

  if (image->matrixCoefficients == AVIF_MATRIX_COEFFICIENTS_IDENTITY &&
      image->yuvFormat != AVIF_PIXEL_FORMAT_YUV444) {
    // The AV1 spec does not allow identity with subsampling.
    ASSERT_EQ(result, AVIF_RESULT_INVALID_ARGUMENT);
    return;
  }
  ASSERT_EQ(result, AVIF_RESULT_OK) << avifResultToString(result);

  // Decode to memory.
  ImagePtr decoded(avifImageCreateEmpty());
  ASSERT_NE(decoded, nullptr);
  DecoderPtr decoder(avifDecoderCreate());
  ASSERT_NE(decoder, nullptr);
  result = avifDecoderReadMemory(decoder.get(), decoded.get(), encoded.data,
                                 encoded.size);
  ASSERT_EQ(result, AVIF_RESULT_OK) << avifResultToString(result);

  // What we read should be what we encoded
  ASSERT_TRUE(testutil::AreImagesEqual(*image, *decoded));
}

INSTANTIATE_TEST_SUITE_P(
    EncodeDecodeMemoryInstantiation, EncodeDecodeMemory,
    testing::Combine(
        testing::Values("paris_icc_exif_xmp.png", "paris_exif_xmp_icc.jpg",
                        "kodim03_grayscale_gamma1.6.png"),
        testing::Values(static_cast<int>(AVIF_MATRIX_COEFFICIENTS_IDENTITY),
                        static_cast<int>(AVIF_MATRIX_COEFFICIENTS_YCGCO),
                        static_cast<int>(AVIF_MATRIX_COEFFICIENTS_YCGCO_RE)),
        testing::Values(static_cast<int>(AVIF_PIXEL_FORMAT_NONE),
                        static_cast<int>(AVIF_PIXEL_FORMAT_YUV444),
                        static_cast<int>(AVIF_PIXEL_FORMAT_YUV420),
                        static_cast<int>(AVIF_PIXEL_FORMAT_YUV400))));

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
