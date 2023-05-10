// Copyright 2022 Google LLC
// SPDX-License-Identifier: BSD-2-Clause

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

void AreSamplesEqualForAllReadSettings(const char* file_name1,
                                       const char* file_name2,
                                       bool ignore_metadata, double min_psnr) {
  for (avifPixelFormat requested_format :
       {AVIF_PIXEL_FORMAT_YUV444, AVIF_PIXEL_FORMAT_YUV422,
        AVIF_PIXEL_FORMAT_YUV420, AVIF_PIXEL_FORMAT_YUV400}) {
    SCOPED_TRACE("requested_format: " + std::to_string(requested_format));
    for (int requested_depth : {8, 10, 12, 16}) {
      SCOPED_TRACE("requested_depth: " + std::to_string(requested_depth));
      for (avifChromaDownsampling chroma_downsampling :
           {AVIF_CHROMA_DOWNSAMPLING_AUTOMATIC,
            AVIF_CHROMA_DOWNSAMPLING_FASTEST,
            AVIF_CHROMA_DOWNSAMPLING_BEST_QUALITY,
            AVIF_CHROMA_DOWNSAMPLING_AVERAGE}) {
        SCOPED_TRACE("chroma_downsampling: " +
                     std::to_string(chroma_downsampling));
        const testutil::AvifImagePtr image1 = testutil::ReadImage(
            data_path, file_name1, requested_format, requested_depth,
            chroma_downsampling, ignore_metadata, ignore_metadata,
            ignore_metadata);
        ASSERT_TRUE(image1);
        const testutil::AvifImagePtr image2 = testutil::ReadImage(
            data_path, file_name2, requested_format, requested_depth,
            chroma_downsampling, ignore_metadata, ignore_metadata,
            ignore_metadata);
        ASSERT_TRUE(image2);
        const double psnr = testutil::GetPsnr(*image1, *image2,
                                              /*ignore_alpha=*/false);
        const std::string dir = testing::TempDir();
        const std::string dst1 = dir + "/" + file_name1;
        const std::string dst2 = dir + "/" + file_name2;
        if (psnr < min_psnr) {
          testutil::WriteImage(image1.get(), dst1.c_str());
          testutil::WriteImage(image2.get(), dst2.c_str());
        }
        ASSERT_GE(psnr, min_psnr) << "Actual images saved to " << dst1
                                  << " and " << dst2 << " psnr: " << psnr;
      }
    }
  }
}

TEST(JpegTest, ReadAllSubsamplingsAndAllBitDepths) {
  AreSamplesEqualForAllReadSettings("paris_exif_xmp_icc.jpg",
                                    "paris_exif_orientation_5.jpg",
                                    /*ignoreMetadata=*/true, /*min_psnr=*/99.);
}

TEST(PngTest, ReadAllSubsamplingsAndAllBitDepths) {
  AreSamplesEqualForAllReadSettings("paris_icc_exif_xmp.png",
                                    "paris_icc_exif_xmp_at_end.png",
                                    /*ignoreMetadata=*/true, /*min_psnr=*/99.);
  AreSamplesEqualForAllReadSettings("gray_gama_chrm.png",
                                    "gray_gama_applied.png",
                                    /*ignoreMetadata=*/false, /*min_psnr=*/99.);
  AreSamplesEqualForAllReadSettings("gray16_gama_chrm.png",
                                    "gray16_gama_applied.png",
                                    /*ignoreMetadata=*/false, /*min_psnr=*/65.);
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
