// Copyright 2022 Google LLC
// SPDX-License-Identifier: BSD-2-Clause

#include <climits>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iostream>
#include <limits>
#include <numeric>
#include <string>
#include <vector>

#include "avif/avif.h"
#include "avif/avif_cxx.h"
#include "avifpng.h"
#include "aviftest_helpers.h"
#include "avifutil.h"
#include "gtest/gtest.h"
#include "iccmaker.h"
#include "y4m.h"

namespace avif {
namespace {

// Used to pass the data folder path to the GoogleTest suites.
const char* data_path = nullptr;

//------------------------------------------------------------------------------

// Reads samples with an expected bit depth.
ImagePtr ReadImageLosslessBitDepth(const std::string& path,
                                   uint32_t bit_depth) {
  ImagePtr image(avifImageCreateEmpty());
  if (!image) return nullptr;
  // Lossless.
  image->yuvRange = AVIF_RANGE_FULL;
  image->matrixCoefficients = AVIF_MATRIX_COEFFICIENTS_IDENTITY;

  uint32_t output_depth;
  if (avifReadImage(path.c_str(),
                    AVIF_APP_FILE_FORMAT_UNKNOWN /* guess format */,
                    AVIF_PIXEL_FORMAT_YUV444, bit_depth,
                    AVIF_CHROMA_DOWNSAMPLING_AUTOMATIC,
                    /*ignoreColorProfile=*/true,
                    /*ignoreExif=*/true, /*ignoreXMP=*/true,
                    /*ignoreAlpha=*/false,
                    /*ignoreGainMap=*/true, AVIF_DEFAULT_IMAGE_SIZE_LIMIT,
                    image.get(), &output_depth,
                    /*sourceTiming=*/nullptr,
                    /*frameIter=*/nullptr) == AVIF_APP_FILE_FORMAT_UNKNOWN) {
    return nullptr;
  }
  EXPECT_EQ(output_depth, bit_depth);
  EXPECT_EQ(image->depth, bit_depth);
  return image;
}

// Returns the ratio of sample values present in the channel over all possible
// values for the sample bit depth.
double GetSampleValueSpread(const avifImage& image, avifChannelIndex channel) {
  std::vector<uint32_t> is_value_used(size_t{1} << image.depth, 0);
  const uint8_t* row = image.yuvPlanes[channel];
  for (uint32_t y = 0; y < avifImagePlaneHeight(&image, channel); ++y) {
    for (uint32_t x = 0; x < avifImagePlaneWidth(&image, channel); ++x) {
      const uint32_t value = avifImageUsesU16(&image)
                                 ? reinterpret_cast<const uint16_t*>(row)[x]
                                 : row[x];
      is_value_used[value] = 1;
    }
    row += avifImagePlaneRowBytes(&image, channel);
  }
  const double num_used_values =
      std::accumulate(is_value_used.begin(), is_value_used.end(), 0u);
  return num_used_values / is_value_used.size();
}

//------------------------------------------------------------------------------

TEST(BitDepthTest, Png16b) {
  // Read 16-bit image.
  ImagePtr original =
      ReadImageLosslessBitDepth(std::string(data_path) + "weld_16bit.png", 16);
  ASSERT_NE(original, nullptr);
  // More than 75% of possible sample values are present in each channel.
  // This is a "true" 16-bit image, not 8-bit samples shifted left by 8 bits.
  EXPECT_GT(GetSampleValueSpread(*original, AVIF_CHAN_Y), 0.75);
  EXPECT_GT(GetSampleValueSpread(*original, AVIF_CHAN_U), 0.75);
  EXPECT_GT(GetSampleValueSpread(*original, AVIF_CHAN_V), 0.75);

  // Write and read it as a 16-bit PNG.
  const std::string file_path =
      testing::TempDir() + "avifpng16bittest_weld_16bit.png";
  ASSERT_TRUE(avifPNGWrite(file_path.c_str(), original.get(), original->depth,
                           AVIF_CHROMA_UPSAMPLING_AUTOMATIC,
                           /*compressionLevel=*/0));
  ImagePtr image = ReadImageLosslessBitDepth(file_path, original->depth);
  ASSERT_NE(image, nullptr);

  // They should match exactly.
  EXPECT_TRUE(testutil::AreImagesEqual(*original, *image));
}

//------------------------------------------------------------------------------

bool WriteBinaryFile(const std::string& path, const void* data, size_t size) {
  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  if (!out.is_open()) return false;
  out.write(reinterpret_cast<const char*>(data),
            static_cast<std::streamsize>(size));
  return out.good();
}

TEST(PngWriteValidationTest, CopyYPlaneValidatesBufferAndRowBytes) {
  ImagePtr control_gray =
      testutil::CreateImage(/*width=*/8, /*height=*/2, /*depth=*/8,
                            AVIF_PIXEL_FORMAT_YUV400, AVIF_PLANES_YUV);
  ASSERT_NE(control_gray, nullptr);
  const uint32_t kGrayFill[4] = {128, 0, 0, 255};
  testutil::FillImagePlain(control_gray.get(), kGrayFill);
  const std::string valid_gray_path =
      testing::TempDir() + "avifpng16bittest_valid_gray8.png";
  ASSERT_TRUE(avifPNGWrite(valid_gray_path.c_str(), control_gray.get(),
                           /*requestedDepth=*/8,
                           AVIF_CHROMA_UPSAMPLING_AUTOMATIC,
                           /*compressionLevel=*/0));

  // Under-strided 8-bit YUV400 image (yuvRowBytes < width) must be rejected on
  // the copyYPlane fast path instead of reading width bytes per row past the
  // end of yuvPlanes[AVIF_CHAN_Y].
  std::vector<uint8_t> short_y_buffer(4, 0x42);
  ImagePtr short_stride_gray(avifImageCreateEmpty());
  ASSERT_NE(short_stride_gray, nullptr);
  short_stride_gray->width = 8;
  short_stride_gray->height = 2;
  short_stride_gray->depth = 8;
  short_stride_gray->yuvFormat = AVIF_PIXEL_FORMAT_YUV400;
  short_stride_gray->yuvPlanes[AVIF_CHAN_Y] = short_y_buffer.data();
  short_stride_gray->yuvRowBytes[AVIF_CHAN_Y] = 2;
  const std::string short_stride_path =
      testing::TempDir() + "avifpng16bittest_short_stride_gray8.png";
  const avifBool short_stride_ok =
      avifPNGWrite(short_stride_path.c_str(), short_stride_gray.get(),
                   /*requestedDepth=*/8, AVIF_CHROMA_UPSAMPLING_AUTOMATIC,
                   /*compressionLevel=*/0);
  short_stride_gray->yuvPlanes[AVIF_CHAN_Y] = nullptr;
  short_stride_gray->yuvRowBytes[AVIF_CHAN_Y] = 0;
  EXPECT_FALSE(short_stride_ok);
}

TEST(PngWriteValidationTest, OversizedIccRejectedOnWrite) {
  ImagePtr image =
      testutil::CreateImage(/*width=*/2, /*height=*/2, /*depth=*/8,
                            AVIF_PIXEL_FORMAT_YUV444, AVIF_PLANES_YUV);
  ASSERT_NE(image, nullptr);
  const uint32_t kYuvFill[4] = {100, 128, 128, 255};
  testutil::FillImagePlain(image.get(), kYuvFill);

  float primaries[8] = {0};
  avifColorPrimariesGetValues(AVIF_COLOR_PRIMARIES_BT709, primaries);
  testutil::AvifRwData valid_icc;
  ASSERT_TRUE(avifGenerateRGBICC(&valid_icc, 2.2f, primaries));
  ASSERT_EQ(valid_icc.size, 376u);

  const std::string control_out_path =
      testing::TempDir() + "avifpng16bittest_valid_icc_out.png";
  image->icc.data = valid_icc.data;
  image->icc.size = valid_icc.size;
  EXPECT_TRUE(avifPNGWrite(control_out_path.c_str(), image.get(),
                           /*requestedDepth=*/8,
                           AVIF_CHROMA_UPSAMPLING_AUTOMATIC,
                           /*compressionLevel=*/0));
  image->icc.data = nullptr;
  image->icc.size = 0;

  if (sizeof(size_t) > sizeof(uint32_t)) {
    // Point icc.data to the valid 376-byte ICC buffer while setting icc.size to
    // 2^32 + 376 so an unguarded (png_uint_32) cast wraps to 376 without
    // reading out of bounds.
    image->icc.data = valid_icc.data;
    image->icc.size = (static_cast<size_t>(UINT32_MAX) + 1u) + valid_icc.size;
    const std::string oversized_out_path =
        testing::TempDir() + "avifpng16bittest_oversized_icc_out.png";
    const avifBool write_ok =
        avifPNGWrite(oversized_out_path.c_str(), image.get(),
                     /*requestedDepth=*/8, AVIF_CHROMA_UPSAMPLING_AUTOMATIC,
                     /*compressionLevel=*/0);
    image->icc.data = nullptr;
    image->icc.size = 0;
    EXPECT_FALSE(write_ok);
  }
}

TEST(IccMakerTest, RejectsNonFiniteInputs) {
  float bt709[8] = {0};
  avifColorPrimariesGetValues(AVIF_COLOR_PRIMARIES_BT709, bt709);

  testutil::AvifRwData rgb_control;
  ASSERT_TRUE(avifGenerateRGBICC(&rgb_control, 2.2f, bt709));
  EXPECT_EQ(rgb_control.size, 376u);

  testutil::AvifRwData gray_control;
  ASSERT_TRUE(avifGenerateGrayICC(&gray_control, 2.2f, &bt709[6]));
  EXPECT_EQ(gray_control.size, 275u);

  const float nan_val = std::numeric_limits<float>::quiet_NaN();
  const float inf_val = std::numeric_limits<float>::infinity();

  testutil::AvifRwData rejected;
  EXPECT_FALSE(avifGenerateRGBICC(&rejected, nan_val, bt709));
  EXPECT_FALSE(avifGenerateRGBICC(&rejected, inf_val, bt709));
  EXPECT_FALSE(avifGenerateRGBICC(&rejected, -inf_val, bt709));
  EXPECT_FALSE(avifGenerateGrayICC(&rejected, nan_val, &bt709[6]));
  EXPECT_FALSE(avifGenerateGrayICC(&rejected, inf_val, &bt709[6]));
  EXPECT_FALSE(avifGenerateGrayICC(&rejected, -inf_val, &bt709[6]));
  EXPECT_EQ(rejected.size, 0u);

  for (int i = 0; i < 8; ++i) {
    float bad_primaries[8];
    std::memcpy(bad_primaries, bt709, sizeof(bt709));
    bad_primaries[i] = nan_val;
    EXPECT_FALSE(avifGenerateRGBICC(&rejected, 2.2f, bad_primaries))
        << "NaN primaries[" << i << "]";
    bad_primaries[i] = inf_val;
    EXPECT_FALSE(avifGenerateRGBICC(&rejected, 2.2f, bad_primaries))
        << "Inf primaries[" << i << "]";
  }

  const float nan_white_x[2] = {nan_val, bt709[7]};
  const float nan_white_y[2] = {bt709[6], nan_val};
  const float inf_white_x[2] = {inf_val, bt709[7]};
  const float zero_white_y[2] = {bt709[6], 0.0f};
  EXPECT_FALSE(avifGenerateGrayICC(&rejected, 2.2f, nan_white_x));
  EXPECT_FALSE(avifGenerateGrayICC(&rejected, 2.2f, nan_white_y));
  EXPECT_FALSE(avifGenerateGrayICC(&rejected, 2.2f, inf_white_x));
  EXPECT_FALSE(avifGenerateGrayICC(&rejected, 2.2f, zero_white_y));
  EXPECT_EQ(rejected.size, 0u);
}

TEST(Y4mHeaderTest, RejectsOverflowAndTrailingGarbage) {
  const std::string kSamples(6, '\x80');
  const std::string valid_y4m =
      "YUV4MPEG2 W2 H2 F25:1 Ip A0:0 C420\nFRAME\n" + kSamples;
  const std::string valid_path =
      testing::TempDir() + "avifpng16bittest_valid.y4m";
  ASSERT_TRUE(WriteBinaryFile(valid_path, valid_y4m.data(), valid_y4m.size()));

  ImagePtr valid_image(avifImageCreateEmpty());
  ASSERT_NE(valid_image, nullptr);
  avifAppSourceTiming timing = {0, 0};
  ASSERT_TRUE(y4mRead(valid_path.c_str(), /*ignoreAlpha=*/AVIF_FALSE,
                      AVIF_DEFAULT_IMAGE_SIZE_LIMIT, valid_image.get(), &timing,
                      /*iter=*/nullptr));
  EXPECT_EQ(valid_image->width, 2u);
  EXPECT_EQ(valid_image->height, 2u);
  EXPECT_EQ(timing.timescale, 25u);
  EXPECT_EQ(timing.duration, 1u);

  const std::vector<std::string> malformed_inputs = {
      "YUV4MPEG2 W2 H2 F4294967321:1 Ip C420\nFRAME\n" + kSamples,
      "YUV4MPEG2 W2 H2 F25:4294967297 Ip C420\nFRAME\n" + kSamples,
      "YUV4MPEG2 W2 H2 F25fps:1 Ip C420\nFRAME\n" + kSamples,
      "YUV4MPEG2 W2 H2 F25:1:2 Ip C420\nFRAME\n" + kSamples,
      "YUV4MPEG2 W2abc H2 F25:1 Ip C420\nFRAME\n" + kSamples,
      "YUV4MPEG2 W2 H2xyz F25:1 Ip C420\nFRAME\n" + kSamples,
      "YUV4MPEG2 W2 H2 F25:1 Ip C420\nFRAMEBUFFER\n" + kSamples,
      std::string("YUV4MPEG2 W2 H2 F25:1\0bad C420\nFRAME\n", 37) + kSamples,
  };
  for (size_t idx = 0; idx < malformed_inputs.size(); ++idx) {
    const std::string bad_path = testing::TempDir() + "avifpng16bittest_bad_" +
                                 std::to_string(idx) + ".y4m";
    ASSERT_TRUE(WriteBinaryFile(bad_path, malformed_inputs[idx].data(),
                                malformed_inputs[idx].size()));
    ImagePtr bad_image(avifImageCreateEmpty());
    ASSERT_NE(bad_image, nullptr);
    avifAppSourceTiming bad_timing = {0, 0};
    const avifBool read_ok =
        y4mRead(bad_path.c_str(), /*ignoreAlpha=*/AVIF_FALSE,
                AVIF_DEFAULT_IMAGE_SIZE_LIMIT, bad_image.get(), &bad_timing,
                /*iter=*/nullptr);
    EXPECT_FALSE(read_ok) << "index=" << idx;
  }

  uint8_t dummy_sample[2] = {0, 0};
  ImagePtr invalid_dim_image(avifImageCreateEmpty());
  ASSERT_NE(invalid_dim_image, nullptr);
  invalid_dim_image->width = 0;
  invalid_dim_image->height = 1;
  invalid_dim_image->depth = 8;
  invalid_dim_image->yuvFormat = AVIF_PIXEL_FORMAT_YUV400;
  invalid_dim_image->yuvPlanes[AVIF_CHAN_Y] = dummy_sample;
  invalid_dim_image->yuvRowBytes[AVIF_CHAN_Y] = sizeof(dummy_sample);
  const std::string zero_out_path =
      testing::TempDir() + "avifpng16bittest_zero_width_out.y4m";
  EXPECT_FALSE(y4mWrite(zero_out_path.c_str(), invalid_dim_image.get()));

  // With depth = 10 and width = INT_MAX + 1 (0x80000000), an unguarded signed
  // %d header format writes W-2147483648 and planeWidth << 1 overflows uint32_t
  // to 0 bytes.
  invalid_dim_image->depth = 10;
  invalid_dim_image->width = static_cast<uint32_t>(INT_MAX) + 1u;
  const std::string oversized_out_path =
      testing::TempDir() + "avifpng16bittest_oversized_width_out.y4m";
  const avifBool oversized_write_ok =
      y4mWrite(oversized_out_path.c_str(), invalid_dim_image.get());
  invalid_dim_image->yuvPlanes[AVIF_CHAN_Y] = nullptr;
  invalid_dim_image->yuvRowBytes[AVIF_CHAN_Y] = 0;
  EXPECT_FALSE(oversized_write_ok);
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
