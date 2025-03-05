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

// Read image losslessly, using identiy MC.
ImagePtr ReadImageLossless(const std::string& path,
                           avifPixelFormat requested_format,
                           avifBool ignore_icc) {
  ImagePtr image(avifImageCreateEmpty());
  image->matrixCoefficients = AVIF_MATRIX_COEFFICIENTS_IDENTITY;
  if (!image ||
      avifReadImage(path.c_str(), requested_format, /*requested_depth=*/0,
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

class LosslessTest
    : public testing::TestWithParam<std::tuple<std::string, int, int>> {};

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
  const std::string file_path = std::string(data_path) + file_name;
  ImagePtr image(avifImageCreateEmpty());
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
  if (matrix_coefficients == AVIF_MATRIX_COEFFICIENTS_YCGCO_RO) {
    // AVIF_MATRIX_COEFFICIENTS_YCGCO_RO does not work because the input
    // depth is not odd.
    ASSERT_EQ(file_format, AVIF_APP_FILE_FORMAT_UNKNOWN);
    return;
  }
  ASSERT_NE(file_format, AVIF_APP_FILE_FORMAT_UNKNOWN);

  // Encode.
  EncoderPtr encoder(avifEncoderCreate());
  ASSERT_NE(encoder, nullptr);
  encoder->speed = AVIF_SPEED_FASTEST;
  encoder->quality = AVIF_QUALITY_LOSSLESS;
  encoder->qualityAlpha = AVIF_QUALITY_LOSSLESS;
  testutil::AvifRwData encoded;
  avifResult result = avifEncoderWrite(encoder.get(), image.get(), &encoded);

  if (matrix_coefficients == AVIF_MATRIX_COEFFICIENTS_IDENTITY &&
      pixel_format != AVIF_PIXEL_FORMAT_NONE &&
      pixel_format != AVIF_PIXEL_FORMAT_YUV444) {
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

  // Convert to a temporary PNG and read back, to have default matrix
  // coefficients.
  const std::string tmp_path = testing::TempDir() + "decoded_default.png";
  ASSERT_TRUE(testutil::WriteImage(decoded.get(), tmp_path.c_str()));
  const ImagePtr decoded_lossless =
      ReadImageLossless(tmp_path, pixel_format, ignore_icc);

  // Verify that the ground truth and decoded images are the same.
  const ImagePtr ground_truth_lossless =
      ReadImageLossless(file_path, pixel_format, ignore_icc);
  const bool are_images_equal =
      testutil::AreImagesEqual(*ground_truth_lossless, *decoded_lossless);

  if (matrix_coefficients == AVIF_MATRIX_COEFFICIENTS_YCGCO) {
    // AVIF_MATRIX_COEFFICIENTS_YCGCO is not a lossless transform.
    ASSERT_FALSE(are_images_equal);
  } else if (pixel_format == AVIF_PIXEL_FORMAT_YUV400) {
    // For gray images, information is lost.
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
                        static_cast<int>(AVIF_MATRIX_COEFFICIENTS_YCGCO_RE),
                        static_cast<int>(AVIF_MATRIX_COEFFICIENTS_YCGCO_RO)),
        testing::Values(static_cast<int>(AVIF_PIXEL_FORMAT_NONE),
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
