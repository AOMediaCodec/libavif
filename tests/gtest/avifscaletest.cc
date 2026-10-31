// Copyright 2023 Google LLC
// SPDX-License-Identifier: BSD-2-Clause

#include <cstdint>
#include <limits>

#include "avif/avif.h"
#include "avif/internal.h"
#include "aviftest_helpers.h"
#include "gtest/gtest.h"

using testing::Combine;
using testing::Values;

namespace avif {
namespace {

// Used to pass the data folder path to the GoogleTest suites.
const char* data_path = nullptr;

constexpr bool kIgnoreMetadata = true;

//------------------------------------------------------------------------------

class ScaleTest
    : public testing::TestWithParam<
          std::tuple</*bit_depth=*/int, /*yuv_format=*/avifPixelFormat,
                     /*create_alpha=*/bool>> {};

TEST_P(ScaleTest, Roundtrip) {
  const int bit_depth = std::get<0>(GetParam());
  const avifPixelFormat yuv_format = std::get<1>(GetParam());
  const bool create_alpha = std::get<2>(GetParam());

  const ImagePtr image =
      testutil::ReadImage(data_path, "paris_exif_xmp_icc.jpg", yuv_format,
                          bit_depth, AVIF_CHROMA_DOWNSAMPLING_BEST_QUALITY,
                          kIgnoreMetadata, kIgnoreMetadata, kIgnoreMetadata);
  ASSERT_NE(image, nullptr);
  if (create_alpha && !image->alphaPlane) {
    // Simulate alpha plane with a view on luma.
    image->alphaPlane = image->yuvPlanes[AVIF_CHAN_Y];
    image->alphaRowBytes = image->yuvRowBytes[AVIF_CHAN_Y];
    image->imageOwnsAlphaPlane = false;
  }

  ImagePtr scaled_image(avifImageCreateEmpty());
  ASSERT_NE(scaled_image, nullptr);
  ASSERT_EQ(avifImageCopy(scaled_image.get(), image.get(), AVIF_PLANES_ALL),
            AVIF_RESULT_OK);

  const uint32_t scaled_width = static_cast<uint32_t>(image->width * 0.9);
  const uint32_t scaled_height = static_cast<uint32_t>(image->height * 2.14);
  avifDiagnostics diag;
  ASSERT_EQ(
      avifImageScale(scaled_image.get(), scaled_width, scaled_height, &diag),
      AVIF_RESULT_OK)
      << diag.error;
  EXPECT_EQ(scaled_image->width, scaled_width);
  EXPECT_EQ(scaled_image->height, scaled_height);

  ASSERT_EQ(
      avifImageScale(scaled_image.get(), image->width, image->height, &diag),
      AVIF_RESULT_OK)
      << diag.error;
  EXPECT_EQ(scaled_image->width, image->width);
  EXPECT_EQ(scaled_image->height, image->height);

  const double psnr = testutil::GetPsnr(*image, *scaled_image);
  EXPECT_GT(psnr, 30.0);
  EXPECT_LT(psnr, 45.0);
}

INSTANTIATE_TEST_SUITE_P(
    Some, ScaleTest,
    Combine(/*bit_depth=*/Values(8, 10, 12),
            Values(AVIF_PIXEL_FORMAT_YUV444, AVIF_PIXEL_FORMAT_YUV422,
                   AVIF_PIXEL_FORMAT_YUV420, AVIF_PIXEL_FORMAT_YUV400),
            /*create_alpha=*/Values(true, false)));

void ExpectScaleStateUnchanged(const avifImage& image,
                               const avifImage& before) {
  EXPECT_EQ(image.width, before.width);
  EXPECT_EQ(image.height, before.height);
  EXPECT_EQ(image.depth, before.depth);
  EXPECT_EQ(image.yuvFormat, before.yuvFormat);
  EXPECT_EQ(image.imageOwnsYUVPlanes, before.imageOwnsYUVPlanes);
  EXPECT_EQ(image.imageOwnsAlphaPlane, before.imageOwnsAlphaPlane);
  for (int i = 0; i < AVIF_PLANE_COUNT_YUV; ++i) {
    EXPECT_EQ(image.yuvPlanes[i], before.yuvPlanes[i]);
    EXPECT_EQ(image.yuvRowBytes[i], before.yuvRowBytes[i]);
  }
  EXPECT_EQ(image.alphaPlane, before.alphaPlane);
  EXPECT_EQ(image.alphaRowBytes, before.alphaRowBytes);
}

TEST(ScaleTest, RejectsDestinationOutsideIntArithmeticRange) {
  const uint32_t int_max = std::numeric_limits<int>::max();
  for (const uint32_t dimension :
       {int_max / 3 + 1, int_max / 2 + 1, int_max, int_max + 1,
        std::numeric_limits<uint32_t>::max()}) {
    for (const int depth : {8, 10, 12}) {
      for (const avifPlanesFlags planes :
           {AVIF_PLANES_YUV, AVIF_PLANES_A, AVIF_PLANES_ALL}) {
        for (const bool scale_width : {false, true}) {
          SCOPED_TRACE(testing::Message()
                       << dimension << " depth=" << depth
                       << " planes=" << planes << " width=" << scale_width);
          const ImagePtr image = testutil::CreateImage(
              1, 1, depth, AVIF_PIXEL_FORMAT_YUV444, planes);
          ASSERT_NE(image, nullptr);
          const avifImage before = *image;
          avifDiagnostics diag = {};
          EXPECT_EQ(avifImageScaleWithLimit(
                        image.get(), scale_width ? dimension : 1,
                        scale_width ? 1 : dimension,
                        /*imageSizeLimit=*/std::numeric_limits<uint32_t>::max(),
                        /*imageDimensionLimit=*/0, &diag),
                    AVIF_RESULT_NOT_IMPLEMENTED);
          ExpectScaleStateUnchanged(*image, before);
        }
      }
    }
  }
}

TEST(ScaleTest, RejectsSourceStrideOutsideIntRange) {
  const uint32_t int_max = std::numeric_limits<int>::max();
  for (const uint32_t row_bytes :
       {int_max + 1, std::numeric_limits<uint32_t>::max()}) {
    for (const avifPlanesFlags planes :
         {AVIF_PLANES_YUV, AVIF_PLANES_A, AVIF_PLANES_ALL}) {
      for (int channel = AVIF_CHAN_Y; channel <= AVIF_CHAN_A; ++channel) {
        if (!(planes &
              (channel == AVIF_CHAN_A ? AVIF_PLANES_A : AVIF_PLANES_YUV))) {
          continue;
        }
        SCOPED_TRACE(testing::Message() << row_bytes << " planes=" << planes
                                        << " channel=" << channel);
        // One sample per plane is sufficient: rejection precedes plane access.
        uint8_t samples[4] = {};
        const ImagePtr image(
            avifImageCreate(1, 1, 8, AVIF_PIXEL_FORMAT_YUV444));
        ASSERT_NE(image, nullptr);
        if (planes & AVIF_PLANES_YUV) {
          for (int i = 0; i < AVIF_PLANE_COUNT_YUV; ++i) {
            image->yuvPlanes[i] = &samples[i];
            image->yuvRowBytes[i] = (i == channel) ? row_bytes : 1;
          }
        }
        if (planes & AVIF_PLANES_A) {
          image->alphaPlane = &samples[AVIF_CHAN_A];
          image->alphaRowBytes = (channel == AVIF_CHAN_A) ? row_bytes : 1;
        }
        const avifImage before = *image;
        avifDiagnostics diag = {};
        EXPECT_EQ(avifImageScale(image.get(), 2, 2, &diag),
                  AVIF_RESULT_INVALID_ARGUMENT);
        ExpectScaleStateUnchanged(*image, before);
      }
    }
  }
}

TEST(ScaleTest, AcceptsRepresentableSourceStrides) {
  // A single-row view can have a valid INT_MAX sample stride at every depth.
  for (const int depth : {8, 10, 12}) {
    SCOPED_TRACE(depth);
    uint8_t samples8[4] = {16, 32, 64, 128};
    uint16_t samples16[4] = {64, 128, 256, 512};
    const uint32_t row_bytes =
        static_cast<uint32_t>(std::numeric_limits<int>::max()) *
        (depth > 8 ? 2u : 1u);
    const ImagePtr image(
        avifImageCreate(1, 1, depth, AVIF_PIXEL_FORMAT_YUV444));
    ASSERT_NE(image, nullptr);
    for (int channel = AVIF_CHAN_Y; channel <= AVIF_CHAN_A; ++channel) {
      uint8_t* plane = depth > 8
                           ? reinterpret_cast<uint8_t*>(&samples16[channel])
                           : &samples8[channel];
      if (channel == AVIF_CHAN_A) {
        image->alphaPlane = plane;
        image->alphaRowBytes = row_bytes;
      } else {
        image->yuvPlanes[channel] = plane;
        image->yuvRowBytes[channel] = row_bytes;
      }
    }
    avifDiagnostics diag = {};
    ASSERT_EQ(avifImageScale(image.get(), 2, 2, &diag), AVIF_RESULT_OK)
        << diag.error;
    EXPECT_EQ(image->width, 2u);
    EXPECT_EQ(image->height, 2u);
    for (int channel = AVIF_CHAN_Y; channel <= AVIF_CHAN_A; ++channel) {
      const uint8_t* plane = avifImagePlane(image.get(), channel);
      ASSERT_NE(plane, nullptr);
      for (uint32_t y = 0; y < 2; ++y) {
        const uint8_t* row =
            plane + y * avifImagePlaneRowBytes(image.get(), channel);
        for (uint32_t x = 0; x < 2; ++x) {
          EXPECT_EQ(
              depth > 8 ? reinterpret_cast<const uint16_t*>(row)[x] : row[x],
              depth > 8 ? samples16[channel] : samples8[channel]);
        }
      }
    }
  }
}

TEST(ScaleTest, AcceptsUnusedPlaneStrides) {
  for (const avifPlanesFlags planes : {AVIF_PLANES_YUV, AVIF_PLANES_A}) {
    const ImagePtr image =
        testutil::CreateImage(1, 1, 8, AVIF_PIXEL_FORMAT_YUV400, planes);
    ASSERT_NE(image, nullptr);
    for (int i = 0; i < AVIF_PLANE_COUNT_YUV; ++i) {
      if (!image->yuvPlanes[i]) {
        image->yuvRowBytes[i] = std::numeric_limits<uint32_t>::max();
      }
    }
    if (!image->alphaPlane) {
      image->alphaRowBytes = std::numeric_limits<uint32_t>::max();
    }
    avifDiagnostics diag = {};
    EXPECT_EQ(avifImageScale(image.get(), 2, 2, &diag), AVIF_RESULT_OK);
    EXPECT_EQ(image->width, 2u);
    EXPECT_EQ(image->height, 2u);
  }
}

TEST(ScaleTest, MetadataOnlyDimensionsAndUnusedStrides) {
  const uint32_t dimension = std::numeric_limits<uint32_t>::max();
  const ImagePtr image(avifImageCreate(1, 1, 8, AVIF_PIXEL_FORMAT_YUV400));
  ASSERT_NE(image, nullptr);
  image->yuvRowBytes[AVIF_CHAN_U] = dimension;
  image->alphaRowBytes = dimension;
  avifDiagnostics diag = {};
  EXPECT_EQ(
      avifImageScaleWithLimit(image.get(), dimension, 1, dimension, 0, &diag),
      AVIF_RESULT_OK);
  EXPECT_EQ(image->width, dimension);
  EXPECT_EQ(image->height, 1u);
  // An unchanged image never enters libyuv, even with large dimensions.
  EXPECT_EQ(
      avifImageScaleWithLimit(image.get(), dimension, 1, dimension, 0, &diag),
      AVIF_RESULT_OK);
}

TEST(ScaleTest, LargerThanDefaultLimits) {
  const ImagePtr image = testutil::ReadImage(
      data_path, "paris_exif_xmp_icc.jpg", AVIF_PIXEL_FORMAT_YUV420, 8,
      AVIF_CHROMA_DOWNSAMPLING_BEST_QUALITY, kIgnoreMetadata, kIgnoreMetadata,
      kIgnoreMetadata);
  ASSERT_NE(image, nullptr);
  avifDiagnostics diag;
  // dstWidth*dstHeight is larger than AVIF_DEFAULT_IMAGE_SIZE_LIMIT.
  EXPECT_NE(avifImageScale(image.get(), 20000, 20000, &diag), AVIF_RESULT_OK);
  // dstWidth is larger than AVIF_DEFAULT_IMAGE_DIMENSION_LIMIT.
  EXPECT_NE(avifImageScale(image.get(), 40000, 2, &diag), AVIF_RESULT_OK);
  // dstHeight is larger than AVIF_DEFAULT_IMAGE_DIMENSION_LIMIT.
  EXPECT_NE(avifImageScale(image.get(), 2, 40000, &diag), AVIF_RESULT_OK);
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
