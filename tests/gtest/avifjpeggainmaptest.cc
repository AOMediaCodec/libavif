// Copyright 2023 Google LLC
// SPDX-License-Identifier: BSD-2-Clause

#include <math.h>

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
    // Since ignore_xmp is true, there should be no XMP, even if it had to
    // be read to parse the gain map.
    EXPECT_EQ(image->xmp.size, 0u);

    const auto& m = image->gainMap.metadata;
    EXPECT_FALSE(m.baseRenditionIsHDR);
    const double kEpsilon = 1e-8;
    EXPECT_NEAR(static_cast<double>(m.hdrCapacityMinN) / m.hdrCapacityMinD, 1,
                kEpsilon);
    EXPECT_NEAR(static_cast<double>(m.hdrCapacityMaxN) / m.hdrCapacityMaxD,
                exp2(3.5), kEpsilon);
    EXPECT_NEAR(static_cast<double>(m.gainMapMinN[0]) / m.gainMapMinD[0],
                exp2(0), kEpsilon);
    EXPECT_NEAR(static_cast<double>(m.gainMapMinN[1]) / m.gainMapMinD[1],
                exp2(0), kEpsilon);
    EXPECT_NEAR(static_cast<double>(m.gainMapMinN[2]) / m.gainMapMinD[2],
                exp2(0), kEpsilon);
    EXPECT_NEAR(static_cast<double>(m.gainMapMaxN[0]) / m.gainMapMaxD[0],
                exp2(3.5), kEpsilon);
    EXPECT_NEAR(static_cast<double>(m.gainMapMaxN[1]) / m.gainMapMaxD[1],
                exp2(3.6), kEpsilon);
    EXPECT_NEAR(static_cast<double>(m.gainMapMaxN[2]) / m.gainMapMaxD[2],
                exp2(3.7), kEpsilon);
    EXPECT_NEAR(static_cast<double>(m.gainMapGammaN[0]) / m.gainMapGammaD[0],
                1.0, kEpsilon);
    EXPECT_NEAR(static_cast<double>(m.gainMapGammaN[1]) / m.gainMapGammaD[1],
                1.0, kEpsilon);
    EXPECT_NEAR(static_cast<double>(m.gainMapGammaN[2]) / m.gainMapGammaD[2],
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
  // Check there is xmp since ignore_xmp is false (just making sure that
  // ignore_gain_map=true has no impact on this).
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
