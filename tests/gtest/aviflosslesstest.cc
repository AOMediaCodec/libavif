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

// Reads an image with a simpler API. ASSERT_NO_FATAL_FAILURE should be used
// when calling it.
void ReadImageSimple(const std::string& file_name, avifPixelFormat pixel_format,
                     avifMatrixCoefficients matrix_coefficients,
                     avifBool ignore_icc, ImagePtr& image) {
  const std::string file_path = std::string(data_path) + file_name;
  image.reset(avifImageCreateEmpty());
  ASSERT_NE(image, nullptr);
  image->matrixCoefficients = matrix_coefficients;
  const avifAppFileFormat file_format = avifReadImage(
      file_path.c_str(),
      /*requestedFormat=*/pixel_format,
      /*requestedDepth=*/0,
      /*chromaDownsampling=*/AVIF_CHROMA_DOWNSAMPLING_AUTOMATIC,
      /*ignoreColorProfile=*/ignore_icc, /*ignoreExif=*/false,
      /*ignoreXMP=*/false, /*allowChangingCicp=*/true,
      /*ignoreGainMap=*/true, AVIF_DEFAULT_IMAGE_SIZE_LIMIT, image.get(),
      /*outDepth=*/nullptr, /*sourceTiming=*/nullptr,
      /*frameIter=*/nullptr);
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

// The test parameters are: the file path, the matrix coefficients and the pixel
// format.
class LosslessTest
    : public testing::TestWithParam<std::tuple<std::string, int, int>> {};

// Tests encode/decode round trips.
TEST_P(LosslessTest, EncodeDecode) {
  const std::string& file_name = std::get<0>(GetParam());
  const avifMatrixCoefficients matrix_coefficients =
      static_cast<avifMatrixCoefficients>(std::get<1>(GetParam()));
  const avifPixelFormat pixel_format =
      static_cast<avifPixelFormat>(std::get<2>(GetParam()));
  // Ignore ICC when going from RGB to gray.
  const avifBool ignore_icc =
      pixel_format == AVIF_PIXEL_FORMAT_YUV400 ? AVIF_TRUE : AVIF_FALSE;

  // Read a ground truth image but ask for certain matrix coefficients.
  ImagePtr image;
  ASSERT_NO_FATAL_FAILURE(ReadImageSimple(
      file_name, pixel_format, matrix_coefficients, ignore_icc, image));
  if (image == nullptr) return;

  // Encode.
  EncoderPtr encoder(avifEncoderCreate());
  ASSERT_NE(encoder, nullptr);
  encoder->speed = AVIF_SPEED_FASTEST;
  encoder->quality = AVIF_QUALITY_LOSSLESS;
  testutil::AvifRwData encoded;
  avifResult result = avifEncoderWrite(encoder.get(), image.get(), &encoded);

  if (matrix_coefficients == AVIF_MATRIX_COEFFICIENTS_IDENTITY &&
      image->yuvFormat != AVIF_PIXEL_FORMAT_YUV444) {
    // The AV1 spec does not allow identity with subsampling.
    ASSERT_EQ(result, AVIF_RESULT_INVALID_ARGUMENT);
    return;
  }
  ASSERT_EQ(result, AVIF_RESULT_OK) << avifResultToString(result);

  // Decode to RAM.
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

// Reads an image losslessly, using identiy MC.
ImagePtr ReadImageLossless(const std::string& path,
                           avifPixelFormat requested_format,
                           avifBool ignore_icc) {
  ImagePtr image(avifImageCreateEmpty());
  if (!image) return nullptr;
  image->matrixCoefficients = AVIF_MATRIX_COEFFICIENTS_IDENTITY;
  if (avifReadImage(path.c_str(), requested_format, /*requested_depth=*/0,
                    /*chroma_downsampling=*/AVIF_CHROMA_DOWNSAMPLING_AUTOMATIC,
                    ignore_icc, /*ignore_exif=*/false, /*ignore_xmp=*/false,
                    /*allow_changing_cicp=*/false, /*ignore_gain_map=*/false,
                    AVIF_DEFAULT_IMAGE_SIZE_LIMIT, image.get(),
                    /*outDepth=*/nullptr, /*sourceTiming=*/nullptr,
                    /*frameIter=*/nullptr) == AVIF_APP_FILE_FORMAT_UNKNOWN) {
    return nullptr;
  }
  return image;
}

// Tests encode/decode round trips under different matrix coefficients.
TEST_P(LosslessTest, EncodeDecodeMatrixCoefficients) {
  const std::string& file_name = std::get<0>(GetParam());
  const avifMatrixCoefficients matrix_coefficients =
      static_cast<avifMatrixCoefficients>(std::get<1>(GetParam()));
  const avifPixelFormat pixel_format =
      static_cast<avifPixelFormat>(std::get<2>(GetParam()));
  // Ignore ICC when going from RGB to gray.
  const avifBool ignore_icc =
      pixel_format == AVIF_PIXEL_FORMAT_YUV400 ? AVIF_TRUE : AVIF_FALSE;

  // Read a ground truth image but ask for certain matrix coefficients.
  ImagePtr image;
  ASSERT_NO_FATAL_FAILURE(ReadImageSimple(
      file_name, pixel_format, matrix_coefficients, ignore_icc, image));
  if (image == nullptr) return;

  // Convert to a temporary PNG and read back losslessly.
  const std::string tmp_path =
      testing::TempDir() + "decoded_EncodeDecodeMatrixCoefficients.png";
  ASSERT_TRUE(testutil::WriteImage(image.get(), tmp_path.c_str()));
  const ImagePtr decoded_lossless =
      ReadImageLossless(tmp_path, pixel_format, ignore_icc);
  if (image->yuvFormat == AVIF_PIXEL_FORMAT_YUV420) {
    // 420 cannot be converted from RGB to YUV with
    // AVIF_MATRIX_COEFFICIENTS_IDENTITY due to a decision taken in
    // avifGetYUVColorSpaceInfo.
    ASSERT_EQ(decoded_lossless, nullptr);
    return;
  }
  ASSERT_NE(decoded_lossless, nullptr);

  // Verify that the ground truth and decoded images are the same.
  const ImagePtr ground_truth_lossless = ReadImageLossless(
      std::string(data_path) + file_name, pixel_format, ignore_icc);
  ASSERT_NE(ground_truth_lossless, nullptr);
  const bool are_images_equal =
      testutil::AreImagesEqual(*ground_truth_lossless, *decoded_lossless);

  if (matrix_coefficients == AVIF_MATRIX_COEFFICIENTS_YCGCO) {
    // AVIF_MATRIX_COEFFICIENTS_YCGCO is not a lossless transform.
    ASSERT_FALSE(are_images_equal);
  } else if (matrix_coefficients == AVIF_MATRIX_COEFFICIENTS_YCGCO_RE &&
             pixel_format == AVIF_PIXEL_FORMAT_YUV400) {
    // For gray images, information is lost in YCGCO_RE.
    ASSERT_FALSE(are_images_equal);
  } else {
    ASSERT_TRUE(are_images_equal);
  }
}

INSTANTIATE_TEST_SUITE_P(
    LosslessTestInstantiation, LosslessTest,
    testing::Combine(
        testing::Values("paris_icc_exif_xmp.png", "paris_exif_xmp_icc.jpg"),
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
