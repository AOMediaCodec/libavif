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

}  // namespace

//------------------------------------------------------------------------------

AvifRgbImage::AvifRgbImage(const avifImage* yuv, int rgbDepth,
                           avifRGBFormat rgbFormat) {
  avifRGBImageSetDefaults(this, yuv);
  depth = rgbDepth;
  format = rgbFormat;
  avifRGBImageAllocatePixels(this);
}

//------------------------------------------------------------------------------

RgbChannelOffsets GetRgbChannelOffsets(avifRGBFormat format) {
  switch (format) {
    case AVIF_RGB_FORMAT_RGB:
      return {/*r=*/0, /*g=*/1, /*b=*/2, /*a=*/0};
    case AVIF_RGB_FORMAT_RGBA:
      return {/*r=*/0, /*g=*/1, /*b=*/2, /*a=*/3};
    case AVIF_RGB_FORMAT_ARGB:
      return {/*r=*/1, /*g=*/2, /*b=*/3, /*a=*/0};
    case AVIF_RGB_FORMAT_BGR:
      return {/*r=*/2, /*g=*/1, /*b=*/0, /*a=*/0};
    case AVIF_RGB_FORMAT_BGRA:
      return {/*r=*/2, /*g=*/1, /*b=*/0, /*a=*/3};
    default:
      assert(format == AVIF_RGB_FORMAT_ABGR);
      return {/*r=*/3, /*g=*/2, /*b=*/1, /*a=*/0};
  }
}

//------------------------------------------------------------------------------

AvifImagePtr CreateImage(int width, int height, int depth,
                         avifPixelFormat yuv_format, avifPlanesFlags planes,
                         avifRange yuv_range) {
  AvifImagePtr image(avifImageCreate(width, height, depth, yuv_format),
                     avifImageDestroy);
  if (!image) {
    return {nullptr, nullptr};
  }
  image->yuvRange = yuv_range;
  if (avifImageAllocatePlanes(image.get(), planes) != AVIF_RESULT_OK) {
    return {nullptr, nullptr};
  }
  return image;
}

void FillImagePlain(avifImage* image, const uint32_t yuva[4]) {
  avifPixelFormatInfo info;
  avifGetPixelFormatInfo(image->yuvFormat, &info);

  for (int c = 0; c < 4; c++) {
    uint8_t* row = (c == AVIF_CHAN_A) ? image->alphaPlane : image->yuvPlanes[c];
    if (!row) {
      continue;
    }
    const uint32_t row_bytes =
        (c == AVIF_CHAN_A) ? image->alphaRowBytes : image->yuvRowBytes[c];
    const uint32_t plane_width =
        (c == AVIF_CHAN_Y || c == AVIF_CHAN_A)
            ? image->width
            : ((image->width + info.chromaShiftX) >> info.chromaShiftX);
    const uint32_t plane_height =
        (c == AVIF_CHAN_Y || c == AVIF_CHAN_A)
            ? image->height
            : ((image->height + info.chromaShiftY) >> info.chromaShiftY);
    for (uint32_t y = 0; y < plane_height; ++y) {
      if (avifImageUsesU16(image)) {
        std::fill(reinterpret_cast<uint16_t*>(row),
                  reinterpret_cast<uint16_t*>(row) + plane_width,
                  static_cast<uint16_t>(yuva[c]));
      } else {
        std::fill(row, row + plane_width, static_cast<uint8_t>(yuva[c]));
      }
      row += row_bytes;
    }
  }
}

void FillImageGradient(avifImage* image) {
  avifPixelFormatInfo info;
  avifGetPixelFormatInfo(image->yuvFormat, &info);

  for (int c = 0; c < 4; c++) {
    uint8_t* row = (c == AVIF_CHAN_A) ? image->alphaPlane : image->yuvPlanes[c];
    if (!row) {
      continue;
    }
    const uint32_t row_bytes =
        (c == AVIF_CHAN_A) ? image->alphaRowBytes : image->yuvRowBytes[c];
    const uint32_t plane_width =
        (c == AVIF_CHAN_Y || c == AVIF_CHAN_A)
            ? image->width
            : ((image->width + info.chromaShiftX) >> info.chromaShiftX);
    const uint32_t plane_height =
        (c == AVIF_CHAN_Y || c == AVIF_CHAN_A)
            ? image->height
            : ((image->height + info.chromaShiftY) >> info.chromaShiftY);
    for (uint32_t y = 0; y < plane_height; ++y) {
      for (uint32_t x = 0; x < plane_width; ++x) {
        const uint32_t value = (x + y) * ((1u << image->depth) - 1u) /
                               std::max(1u, plane_width + plane_height - 2);
        if (avifImageUsesU16(image)) {
          reinterpret_cast<uint16_t*>(row)[x] = static_cast<uint16_t>(value);
        } else {
          row[x] = static_cast<uint8_t>(value);
        }
      }
      row += row_bytes;
    }
  }
}

namespace {
template <typename PixelType>
void FillImageChannel(avifRGBImage* image, uint32_t channel_offset,
                      uint32_t value) {
  const uint32_t channel_count = avifRGBFormatChannelCount(image->format);
  assert(channel_offset < channel_count);
  for (uint32_t y = 0; y < image->height; ++y) {
    PixelType* pixel =
        reinterpret_cast<PixelType*>(image->pixels + image->rowBytes * y);
    for (uint32_t x = 0; x < image->width; ++x) {
      pixel[channel_offset] = static_cast<PixelType>(value);
      pixel += channel_count;
    }
  }
}
}  // namespace

void FillImageChannel(avifRGBImage* image, uint32_t channel_offset,
                      uint32_t value) {
  (image->depth <= 8)
      ? FillImageChannel<uint8_t>(image, channel_offset, value)
      : FillImageChannel<uint16_t>(image, channel_offset, value);
}

//------------------------------------------------------------------------------

// Returns true if image1 and image2 are identical.
bool AreImagesEqual(const avifImage& image1, const avifImage& image2,
                    bool ignore_alpha) {
  if (image1.width != image2.width || image1.height != image2.height ||
      image1.depth != image2.depth || image1.yuvFormat != image2.yuvFormat ||
      image1.yuvRange != image2.yuvRange) {
    return false;
  }
  assert(image1.width * image1.height > 0);

  avifPixelFormatInfo info;
  avifGetPixelFormatInfo(image1.yuvFormat, &info);

  for (int c = 0; c < 4; c++) {
    if (ignore_alpha && c == AVIF_CHAN_A) continue;
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
    const uint32_t row_bytes1 =
        (c == AVIF_CHAN_A) ? image1.alphaRowBytes : image1.yuvRowBytes[c];
    const uint32_t row_bytes2 =
        (c == AVIF_CHAN_A) ? image2.alphaRowBytes : image2.yuvRowBytes[c];
    const uint32_t plane_width =
        (c == AVIF_CHAN_Y || c == AVIF_CHAN_A)
            ? image1.width
            : ((image1.width + info.chromaShiftX) >> info.chromaShiftX);
    const uint32_t plane_height =
        (c == AVIF_CHAN_Y || c == AVIF_CHAN_A)
            ? image1.height
            : ((image1.height + info.chromaShiftY) >> info.chromaShiftY);
    for (uint32_t y = 0; y < plane_height; ++y) {
      if (avifImageUsesU16(&image1)) {
        if (!std::equal(reinterpret_cast<uint16_t*>(row1),
                        reinterpret_cast<uint16_t*>(row1) + plane_width,
                        reinterpret_cast<uint16_t*>(row2))) {
          return false;
        }
      } else {
        if (!std::equal(row1, row1 + plane_width, row2)) {
          return false;
        }
      }
      row1 += row_bytes1;
      row2 += row_bytes2;
    }
  }
  return true;
}

//------------------------------------------------------------------------------

}  // namespace testutil
}  // namespace libavif
