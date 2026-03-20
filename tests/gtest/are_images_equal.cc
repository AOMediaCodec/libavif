// Copyright 2022 Google LLC
// SPDX-License-Identifier: BSD-2-Clause
// Compares two files and returns whether they are the same once decoded.

#include <cstdint>
#include <cstring>
#include <iostream>
#include <limits>
#include <string>

#include "aviftest_helpers.h"
#include "avifutil.h"

using avif::ImagePtr;
using avif::RGBImageCleanup;

namespace {

ImagePtr ToneMapToAlternate(const ImagePtr& image) {
  avif::testutil::AvifRgbImage toneMappedRgb(image.get(), image->depth,
                                             AVIF_RGB_FORMAT_RGB);
  RGBImageCleanup rgbCleanup(&toneMappedRgb);

  avif::ImagePtr toneMapped(
      avifImageCreate(toneMappedRgb.width, toneMappedRgb.height,
                      toneMappedRgb.depth, AVIF_PIXEL_FORMAT_YUV444));
  toneMapped->colorPrimaries = image->gainMap->altColorPrimaries;
  toneMapped->transferCharacteristics =
      image->gainMap->altTransferCharacteristics;
  toneMapped->matrixCoefficients = image->gainMap->altMatrixCoefficients;
  toneMapped->yuvRange = image->yuvRange;

  float altHeadroom = 0.0f;
  if (image->gainMap->alternateHdrHeadroom.d != 0) {
    altHeadroom = (float)image->gainMap->alternateHdrHeadroom.n /
                  image->gainMap->alternateHdrHeadroom.d;
  }

  avifDiagnostics diag;
  avifResult res = avifImageApplyGainMap(
      image.get(), image->gainMap, altHeadroom, toneMapped->colorPrimaries,
      toneMapped->transferCharacteristics, &toneMappedRgb, &toneMapped->clli,
      &diag);
  if (res != AVIF_RESULT_OK) {
    std::cerr << "Failed to apply gain map: " << diag.error << std::endl;
    return nullptr;
  }

  res = avifImageRGBToYUV(toneMapped.get(), &toneMappedRgb);
  if (res != AVIF_RESULT_OK) {
    return nullptr;
  }
  return toneMapped;
}

}  // namespace

int main(int argc, char** argv) {
  if (argc != 4 && argc != 5 && argc != 6) {
    std::cerr << "Wrong argument: " << argv[0]
              << " file1 file2 ignore_alpha_flag [psnr_threshold] "
                 "[ignore_gain_map_flag]"
              << std::endl;
    return 2;
  }
  const bool ignore_gain_map = argc > 5 && std::stoi(argv[5]) != 0;
  const double psnr_threshold = argc > 4 ? std::stod(argv[4]) : 0;

  ImagePtr decoded[2] = {ImagePtr(avifImageCreateEmpty()),
                         ImagePtr(avifImageCreateEmpty())};
  if (!decoded[0] || !decoded[1]) {
    std::cerr << "Cannot create AVIF images." << std::endl;
    return 2;
  }
  uint32_t depth[2];
  // Request the bit depth closest to the bit depth of the input file.
  constexpr int kRequestedDepth = 0;
  constexpr avifPixelFormat requestedFormat = AVIF_PIXEL_FORMAT_NONE;
  for (int i : {0, 1}) {
    // Make sure no color conversion happens.
    decoded[i]->matrixCoefficients = AVIF_MATRIX_COEFFICIENTS_IDENTITY;
    if (avifReadImage(
            argv[i + 1], AVIF_APP_FILE_FORMAT_UNKNOWN /* guess format */,
            requestedFormat, kRequestedDepth,
            AVIF_CHROMA_DOWNSAMPLING_AUTOMATIC,
            /*ignoreColorProfile==*/AVIF_FALSE, /*ignoreExif=*/AVIF_FALSE,
            /*ignoreXMP=*/AVIF_FALSE, ignore_gain_map,
            /*imageSizeLimit=*/std::numeric_limits<uint32_t>::max(),
            decoded[i].get(), &depth[i], nullptr,
            nullptr) == AVIF_APP_FILE_FORMAT_UNKNOWN) {
      std::cerr << "Image " << argv[i + 1] << " cannot be read." << std::endl;
      return 2;
    }
  }

  if (depth[0] != depth[1]) {
    std::cerr << "Images " << argv[1] << " and " << argv[2]
              << " have different depths." << std::endl;
    return 1;
  }

  if (!ignore_gain_map &&
      ((decoded[0]->gainMap != nullptr) != (decoded[1]->gainMap != nullptr))) {
    std::cerr << "Images " << argv[1] << " and " << argv[2]
              << " have different gainmap presence" << std::endl;
    return 1;
  }

  bool ignore_alpha = std::stoi(argv[3]) != 0;

  if (psnr_threshold <= 0) {  // Check for strict equality.
    if (!avif::testutil::AreImagesEqual(*decoded[0], *decoded[1],
                                        ignore_alpha)) {
      auto psnr =
          avif::testutil::GetPsnr(*decoded[0], *decoded[1], ignore_alpha);
      std::cerr << "Images " << argv[1] << " and " << argv[2]
                << " are different (PSNR: " << psnr << ")." << std::endl;
      return 1;
    }
    std::cout << "Images " << argv[1] << " and " << argv[2] << " are identical."
              << std::endl;
  } else {
    auto psnr = avif::testutil::GetPsnr(*decoded[0], *decoded[1], ignore_alpha);
    if (psnr < psnr_threshold) {
      std::cerr << "PSNR: " << psnr << ", images " << argv[1] << " and "
                << argv[2] << " are not similar enough (threshold: " << argv[4]
                << ")." << std::endl;
      return 1;
    }
    std::cout << "PSNR: " << psnr << ", images " << argv[1] << " and "
              << argv[2] << " are similar." << std::endl;

    // If there is a gain map, tone map both images to their alternate headroom
    // and check the PSNR of the tone mapped images.
    if (decoded[0]->gainMap && decoded[1]->gainMap &&
        decoded[0]->gainMap->image && decoded[1]->gainMap->image) {
      // We don't support ICC profiles when tonemapping, so clear them after
      // checking for equality.
      if (decoded[0]->icc.size != decoded[1]->icc.size ||
          (decoded[0]->icc.size > 0 &&
           std::memcmp(decoded[0]->icc.data, decoded[1]->icc.data,
                       decoded[0]->icc.size) != 0)) {
        std::cerr << "Gainmap PSNR: Images " << argv[1] << " and " << argv[2]
                  << " have different ICC profiles." << std::endl;
        return 1;
      }
      if (decoded[0]->gainMap->altICC.size !=
              decoded[1]->gainMap->altICC.size ||
          (decoded[0]->gainMap->altICC.size > 0 &&
           std::memcmp(decoded[0]->gainMap->altICC.data,
                       decoded[1]->gainMap->altICC.data,
                       decoded[0]->gainMap->altICC.size) != 0)) {
        std::cerr << "Gainmap PSNR: Images " << argv[1] << " and " << argv[2]
                  << " have different ICC profiles." << std::endl;
        return 1;
      }
      avifRWDataFree(&decoded[0]->icc);
      avifRWDataFree(&decoded[1]->icc);
      avifRWDataFree(&decoded[0]->gainMap->altICC);
      avifRWDataFree(&decoded[1]->gainMap->altICC);

      avif::ImagePtr toneMapped0 = ToneMapToAlternate(decoded[0]);
      avif::ImagePtr toneMapped1 = ToneMapToAlternate(decoded[1]);
      if (!toneMapped0 || !toneMapped1) {
        std::cerr << "Failed to tone map images." << std::endl;
        return 1;
      }
      psnr = avif::testutil::GetPsnr(*toneMapped0, *toneMapped1);
      if (psnr < psnr_threshold) {
        std::cerr << "Tone-mapped PSNR: " << psnr << ", images " << argv[1]
                  << " and " << argv[2]
                  << " are not similar enough (threshold: " << argv[4] << ")."
                  << std::endl;
        return 1;
      }
      std::cout << "Tone-mapped PSNR: " << psnr << ", images " << argv[1]
                << " and " << argv[2] << " are similar." << std::endl;
    }
  }

  return 0;
}
