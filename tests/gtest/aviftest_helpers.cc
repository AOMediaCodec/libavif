// Copyright 2022 Google LLC. All rights reserved.
// SPDX-License-Identifier: BSD-2-Clause

#include "aviftest_helpers.h"

#include <algorithm>
#include <cassert>

#include "avif/avif.h"

namespace libavif {
namespace testutil {
namespace {

constexpr int AVIF_CHAN_A = AVIF_CHAN_V + 1;
constexpr int AVIF_CHANS[] = {AVIF_CHAN_Y, AVIF_CHAN_U, AVIF_CHAN_V,
                              AVIF_CHAN_A};

}  // namespace

//------------------------------------------------------------------------------

avifImagePtr createImage(int width, int height, int depth,
                         avifPixelFormat yuvFormat, avifPlanesFlags planes,
                         avifRange yuvRange) {
  avifImagePtr image(avifImageCreate(width, height, depth, yuvFormat),
                     avifImageDestroy);
  if (!image) {
    return {nullptr, nullptr};
  }
  image->yuvRange = yuvRange;
  avifImageAllocatePlanes(image.get(), planes);
  return image;
}

void fillImagePlain(avifImage* image, const uint32_t yuva[4]) {
  avifPixelFormatInfo info;
  avifGetPixelFormatInfo(image->yuvFormat, &info);

  for (int c : AVIF_CHANS) {
    uint8_t* row = (c == AVIF_CHAN_A) ? image->alphaPlane : image->yuvPlanes[c];
    if (!row) {
      continue;
    }
    const uint32_t rowBytes =
        (c == AVIF_CHAN_A) ? image->alphaRowBytes : image->yuvRowBytes[c];
    const uint32_t planeWidth =
        (c == AVIF_CHAN_Y || c == AVIF_CHAN_A)
            ? image->width
            : ((image->width + info.chromaShiftX) >> info.chromaShiftX);
    const uint32_t planeHeight =
        (c == AVIF_CHAN_Y || c == AVIF_CHAN_A)
            ? image->height
            : ((image->height + info.chromaShiftY) >> info.chromaShiftY);
    for (uint32_t y = 0; y < planeHeight; ++y) {
      if (avifImageUsesU16(image)) {
        std::fill(reinterpret_cast<uint16_t*>(row),
                  reinterpret_cast<uint16_t*>(row) + planeWidth,
                  static_cast<uint16_t>(yuva[c]));
      } else {
        std::fill(row, row + planeWidth, static_cast<uint8_t>(yuva[c]));
      }
      row += rowBytes;
    }
  }
}

void fillImageGradient(avifImage* image) {
  avifPixelFormatInfo info;
  avifGetPixelFormatInfo(image->yuvFormat, &info);

  for (int c : AVIF_CHANS) {
    uint8_t* row = (c == AVIF_CHAN_A) ? image->alphaPlane : image->yuvPlanes[c];
    if (!row) {
      continue;
    }
    const uint32_t rowBytes =
        (c == AVIF_CHAN_A) ? image->alphaRowBytes : image->yuvRowBytes[c];
    const uint32_t planeWidth =
        (c == AVIF_CHAN_Y || c == AVIF_CHAN_A)
            ? image->width
            : ((image->width + info.chromaShiftX) >> info.chromaShiftX);
    const uint32_t planeHeight =
        (c == AVIF_CHAN_Y || c == AVIF_CHAN_A)
            ? image->height
            : ((image->height + info.chromaShiftY) >> info.chromaShiftY);
    for (uint32_t y = 0; y < planeHeight; ++y) {
      for (uint32_t x = 0; x < planeWidth; ++x) {
        const uint32_t value = (x + y) * ((1u << image->depth) - 1u) /
                               std::max(1u, planeWidth + planeHeight - 2);
        if (avifImageUsesU16(image)) {
          reinterpret_cast<uint16_t*>(row)[x] = static_cast<uint16_t>(value);
        } else {
          row[x] = static_cast<uint8_t>(value);
        }
      }
      row += rowBytes;
    }
  }
}

//------------------------------------------------------------------------------

// Returns true if image1 and image2 are identical.
bool areImagesEqual(const avifImage& image1, const avifImage& image2) {
  if (image1.width != image2.width || image1.height != image2.height ||
      image1.depth != image2.depth || image1.yuvFormat != image2.yuvFormat ||
      image1.yuvRange != image2.yuvRange) {
    return false;
  }
  assert(image1.width * image1.height > 0);

  avifPixelFormatInfo info;
  avifGetPixelFormatInfo(image1.yuvFormat, &info);

  for (int c : AVIF_CHANS) {
    uint8_t* row1 =
        (c == AVIF_CHAN_A) ? image1.alphaPlane : image1.yuvPlanes[c];
    uint8_t* row2 =
        (c == AVIF_CHAN_A) ? image2.alphaPlane : image2.yuvPlanes[c];
    if (!row1 != !row2) {
      return false;
    }
    if (!row1) {
      continue;
    }
    const uint32_t rowBytes1 =
        (c == AVIF_CHAN_A) ? image1.alphaRowBytes : image1.yuvRowBytes[c];
    const uint32_t rowBytes2 =
        (c == AVIF_CHAN_A) ? image2.alphaRowBytes : image2.yuvRowBytes[c];
    const uint32_t planeWidth =
        (c == AVIF_CHAN_Y || c == AVIF_CHAN_A)
            ? image1.width
            : ((image1.width + info.chromaShiftX) >> info.chromaShiftX);
    const uint32_t planeHeight =
        (c == AVIF_CHAN_Y || c == AVIF_CHAN_A)
            ? image1.height
            : ((image1.height + info.chromaShiftY) >> info.chromaShiftY);
    for (uint32_t y = 0; y < planeHeight; ++y) {
      if (avifImageUsesU16(&image1)) {
        if (!std::equal(reinterpret_cast<uint16_t*>(row1),
                        reinterpret_cast<uint16_t*>(row1) + planeWidth,
                        reinterpret_cast<uint16_t*>(row2))) {
          return false;
        }
      } else {
        if (!std::equal(row1, row1 + planeWidth, row2)) {
          return false;
        }
      }
      row1 += rowBytes1;
      row2 += rowBytes2;
    }
  }
  return true;
}

//------------------------------------------------------------------------------

}  // namespace testutil
}  // namespace libavif
