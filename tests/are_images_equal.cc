// Copyright 2022 Google LLC. All rights reserved.
// SPDX-License-Identifier: BSD-2-Clause
// Compares two files and returns whether they are the same once decoded.

#include <iostream>

#include "avifutil.h"
#include "gtest/aviftest_helpers.h"

using libavif::testutil::avifImagePtr;

int main(int argc, char **argv) {
  if (argc != 4) {
    std::cerr
        << "Wrong argument: ./are_image_equal file1 file2 ignore_alpha_flag"
        << std::endl;
    return 1;
  }
  avifImagePtr decoded0(avifImageCreateEmpty(), avifImageDestroy);
  uint32_t depth0, depth1;
  constexpr int kRequestedDepth = 8;
  constexpr avifPixelFormat requestedFormat = AVIF_PIXEL_FORMAT_NONE;
  avifReadImage(argv[1], requestedFormat, kRequestedDepth, decoded0.get(), &depth0);
  avifImagePtr decoded1(avifImageCreateEmpty(), avifImageDestroy);
  avifReadImage(argv[2], requestedFormat, kRequestedDepth, decoded1.get(), &depth1);

  if (depth0 == depth1 && libavif::testutil::AreImagesEqual(
                              *decoded0, *decoded1, std::stoi(argv[3]))) {
    std::cout << "Images " << argv[1] << " and " << argv[2] << " are identical."
              << std::endl;
    return 0;
  }
  return 1;
}
