// Copyright 2022 Google LLC
// SPDX-License-Identifier: BSD-2-Clause

#include <math.h>

#include <array>
#include <tuple>

#include "avif/avif.h"
#include "aviftest_helpers.h"
#include "gtest/gtest.h"

namespace libavif {
namespace {

// Used to pass the data folder path to the GoogleTest suites.
const char* data_path = nullptr;

//------------------------------------------------------------------------------

TEST(JpegTest, ReadJpegWithGainMap) {
  for (const char* filename : {"paris_exif_xmp_gainmap_bigendian.jpg",
                               "paris_exif_xmp_gainmap_littleendian.jpg"}) {
    SCOPED_TRACE(filename);

    const testutil::AvifImagePtr image =
        testutil::ReadImage(data_path, filename, AVIF_PIXEL_FORMAT_YUV444, 8,
                            AVIF_CHROMA_DOWNSAMPLING_AUTOMATIC,
                            /*ignore_icc=*/false, /*ignore_exif=*/false,
                            /*ignore_xmp=*/true, /*allow_changing_cicp=*/true,
                            /*ignore_gain_map=*/false);
    ASSERT_NE(image, nullptr);
    ASSERT_NE(image->gainMap.image, nullptr);
    EXPECT_EQ(image->gainMap.image->width, 512u);
    EXPECT_EQ(image->gainMap.image->height, 384u);
    EXPECT_EQ(image->xmp.size, 0u);

    EXPECT_FALSE(image->gainMap.metadata.baseRenditionIsHDR);
    const double kEpsilon = 1e-8;
    EXPECT_NEAR((double)image->gainMap.metadata.hdrCapacityMinN /
                    image->gainMap.metadata.hdrCapacityMinD,
                1, kEpsilon);
    EXPECT_NEAR((double)image->gainMap.metadata.hdrCapacityMaxN /
                    image->gainMap.metadata.hdrCapacityMaxD,
                exp2(3.5), kEpsilon);
    EXPECT_NEAR((double)image->gainMap.metadata.gainMapMinN[0] /
                    image->gainMap.metadata.gainMapMinD[0],
                exp2(0), kEpsilon);
    EXPECT_NEAR((double)image->gainMap.metadata.gainMapMinN[1] /
                    image->gainMap.metadata.gainMapMinD[1],
                exp2(0), kEpsilon);
    EXPECT_NEAR((double)image->gainMap.metadata.gainMapMinN[2] /
                    image->gainMap.metadata.gainMapMinD[2],
                exp2(0), kEpsilon);
    EXPECT_NEAR((double)image->gainMap.metadata.gainMapMaxN[0] /
                    image->gainMap.metadata.gainMapMaxD[0],
                exp2(3.5), kEpsilon);
    EXPECT_NEAR((double)image->gainMap.metadata.gainMapMaxN[1] /
                    image->gainMap.metadata.gainMapMaxD[1],
                exp2(3.6), kEpsilon);
    EXPECT_NEAR((double)image->gainMap.metadata.gainMapMaxN[2] /
                    image->gainMap.metadata.gainMapMaxD[2],
                exp2(3.7), kEpsilon);
    EXPECT_NEAR((double)image->gainMap.metadata.gainMapGammaN[0] /
                    image->gainMap.metadata.gainMapGammaD[0],
                1.0, kEpsilon);
    EXPECT_NEAR((double)image->gainMap.metadata.gainMapGammaN[1] /
                    image->gainMap.metadata.gainMapGammaD[1],
                1.0, kEpsilon);
    EXPECT_NEAR((double)image->gainMap.metadata.gainMapGammaN[2] /
                    image->gainMap.metadata.gainMapGammaD[2],
                1.0, kEpsilon);
  }
}

TEST(JpegTest, IgnoreGainMap) {
  const testutil::AvifImagePtr image = testutil::ReadImage(
      data_path, "paris_exif_xmp_gainmap_littleendian.jpg",
      AVIF_PIXEL_FORMAT_YUV444, 8, AVIF_CHROMA_DOWNSAMPLING_AUTOMATIC,
      /*ignore_icc=*/false, /*ignore_exif=*/false,
      /*ignore_xmp=*/false, /*allow_changing_cicp=*/true,
      /*ignore_gain_map=*/true);
  ASSERT_NE(image, nullptr);
  EXPECT_EQ(image->gainMap.image, nullptr);
  EXPECT_GT(image->xmp.size, 0u);
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
