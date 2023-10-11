// Copyright 2022 Google LLC
// SPDX-License-Identifier: BSD-2-Clause
// Compares two files and returns whether they are the same once decoded.

#include <iostream>
#include <string>

#include "aviftest_helpers.h"
#include "avifutil.h"
#include "unicode.h"

using libavif::testutil::AvifImagePtr;

int main(int argc, char** argv) {
  if (argc != 4 && argc != 5) {
    std::cerr << "Wrong argument: " << argv[0]
              << " file1 file2 ignore_alpha_flag [psnr_threshold]" << std::endl;
    return 2;
  }
  INIT_WARGV(argc, argv);
  AvifImagePtr decoded[2] = {
      AvifImagePtr(avifImageCreateEmpty(), avifImageDestroy),
      AvifImagePtr(avifImageCreateEmpty(), avifImageDestroy)};
  if (!decoded[0] || !decoded[1]) {
    std::cerr << "Cannot create AVIF images." << std::endl;
    return 2;
  }
  uint32_t depth[2];
  // Request the bit depth closest to the bit depth of the input file.
  constexpr int kRequestedDepth = 0;
  constexpr avifPixelFormat requestedFormat = AVIF_PIXEL_FORMAT_NONE;
  const W_CHAR* files[2];
  files[0] = GET_WARGV(argv, 1);
  files[1] = GET_WARGV(argv, 2);
  for (int i : {0, 1}) {
    // Make sure no color conversion happens.
    decoded[i]->matrixCoefficients = AVIF_MATRIX_COEFFICIENTS_IDENTITY;
    if (avifReadImage((const char*)files[i], requestedFormat, kRequestedDepth,
                      AVIF_CHROMA_DOWNSAMPLING_AUTOMATIC,
                      /*ignoreColorProfile==*/AVIF_FALSE,
                      /*ignoreExif=*/AVIF_FALSE,
                      /*ignoreXMP=*/AVIF_FALSE, /*allowChangingCicp=*/AVIF_TRUE,
                      // TODO(maryla): also compare gain maps.
                      /*ignoreGainMap=*/AVIF_TRUE, decoded[i].get(), &depth[i],
                      nullptr, nullptr) == AVIF_APP_FILE_FORMAT_UNKNOWN) {
      WFPRINTF(stderr, "Image %s cannot be read.\n", files[i]);
      return 2;
    }
  }

  if (depth[0] != depth[1]) {
    WFPRINTF(stderr, "Images %s and %s have different depths.\n", files[0],
             files[1]);
    return 1;
  }

  bool ignore_alpha = std::stoi(argv[3]) != 0;

  if (argc == 4) {
    if (!libavif::testutil::AreImagesEqual(*decoded[0], *decoded[1],
                                           ignore_alpha)) {
      WFPRINTF(stderr, "Images %s and %s are different.\n", files[0], files[1]);
      return 1;
    }
    WFPRINTF(stdout, "Images %s and %s are identical.\n", files[0], files[1]);
  } else {
    auto psnr =
        libavif::testutil::GetPsnr(*decoded[0], *decoded[1], ignore_alpha);
    if (psnr < std::stod(argv[4])) {
      WFPRINTF(stderr, "PSNR: %f, images %s and %s are not similar.\n", psnr,
               files[0], files[1]);
      return 1;
    }
    WFPRINTF(stderr, "PSNR: %f, images %s and %s are similar.\n", psnr,
             files[0], files[1]);
  }
  FREE_WARGV();

  return 0;
}
