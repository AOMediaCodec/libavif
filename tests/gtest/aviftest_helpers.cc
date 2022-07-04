// Copyright 2022 Google LLC. All rights reserved.
// SPDX-License-Identifier: BSD-2-Clause

#include "aviftest_helpers.h"

#include <algorithm>
#include <cassert>

#include "avif/avif.h"

namespace libavif {
namespace testutil {
namespace {

constexpr int AVIF_YUV_CHANS[] = { AVIF_CHAN_Y, AVIF_CHAN_U, AVIF_CHAN_V, avifChannel::A };

}  // namespace

uint32_t avifChannelOffset(avifRGBFormat format, avifChannel channel)
{
    if (channel == avifChannel::A) {
        assert(avifRGBFormatHasAlpha(format));
        return ((format == AVIF_RGB_FORMAT_ARGB) || (format == AVIF_RGB_FORMAT_ABGR)) ? 0 : 3;
    }
    switch (format) {
        case AVIF_RGB_FORMAT_RGB:
        case AVIF_RGB_FORMAT_RGBA:
            return channel;
        case AVIF_RGB_FORMAT_ARGB:
            return 1 + channel;
        case AVIF_RGB_FORMAT_BGR:
        case AVIF_RGB_FORMAT_BGRA:
            return 2 - channel;
        default:
            assert(format == AVIF_RGB_FORMAT_ABGR);
            return 3 - channel;
    }
}

//------------------------------------------------------------------------------

avifRGBImageCleaner::avifRGBImageCleaner(const avifImage * yuv, int rgbDepth, avifRGBFormat rgbFormat)
{
    avifRGBImageSetDefaults(this, yuv);
    depth = rgbDepth;
    format = rgbFormat;
    avifRGBImageAllocatePixels(this);
}

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

    for (int c : AVIF_YUV_CHANS) {
        uint8_t * row = (c == avifChannel::A) ? image->alphaPlane : image->yuvPlanes[c];
        if (!row) {
            continue;
        }
        const uint32_t rowBytes = (c == avifChannel::A) ? image->alphaRowBytes : image->yuvRowBytes[c];
        const uint32_t planeWidth =
            (c == AVIF_CHAN_Y || c == avifChannel::A) ? image->width : ((image->width + info.chromaShiftX) >> info.chromaShiftX);
        const uint32_t planeHeight =
            (c == AVIF_CHAN_Y || c == avifChannel::A) ? image->height : ((image->height + info.chromaShiftY) >> info.chromaShiftY);
        for (uint32_t y = 0; y < planeHeight; ++y) {
            if (avifImageUsesU16(image)) {
                std::fill(reinterpret_cast<uint16_t *>(row),
                          reinterpret_cast<uint16_t *>(row) + planeWidth,
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

    for (int c : AVIF_YUV_CHANS) {
        uint8_t * row = (c == avifChannel::A) ? image->alphaPlane : image->yuvPlanes[c];
        if (!row) {
            continue;
        }
        const uint32_t rowBytes = (c == avifChannel::A) ? image->alphaRowBytes : image->yuvRowBytes[c];
        const uint32_t planeWidth =
            (c == AVIF_CHAN_Y || c == avifChannel::A) ? image->width : ((image->width + info.chromaShiftX) >> info.chromaShiftX);
        const uint32_t planeHeight =
            (c == AVIF_CHAN_Y || c == avifChannel::A) ? image->height : ((image->height + info.chromaShiftY) >> info.chromaShiftY);
        for (uint32_t y = 0; y < planeHeight; ++y) {
            for (uint32_t x = 0; x < planeWidth; ++x) {
                const uint32_t value = (x + y) * ((1u << image->depth) - 1u) / std::max(1u, planeWidth + planeHeight - 2);
                if (avifImageUsesU16(image)) {
                    reinterpret_cast<uint16_t *>(row)[x] = static_cast<uint16_t>(value);
                } else {
                    row[x] = static_cast<uint8_t>(value);
                }
            }
            row += rowBytes;
        }
    }
}

namespace
{
template <typename PixelType>
void fillImageChannel(avifRGBImage * image, avifChannel channel, uint32_t value)
{
    const uint32_t channelCount = avifRGBFormatChannelCount(image->format);
    const uint32_t channelOffset = avifChannelOffset(image->format, channel);
    for (uint32_t y = 0; y < image->height; ++y) {
        PixelType * pixel = reinterpret_cast<PixelType *>(image->pixels + image->rowBytes * y);
        for (uint32_t x = 0; x < image->width; ++x) {
            pixel[channelOffset] = static_cast<PixelType>(value);
            pixel += channelCount;
        }
    }
}
} // namespace

void fillImageChannel(avifRGBImage * image, avifChannel channel, uint32_t value)
{
    (image->depth <= 8) ? fillImageChannel<uint8_t>(image, channel, value) : fillImageChannel<uint16_t>(image, channel, value);
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

    for (int c : AVIF_YUV_CHANS) {
        uint8_t * row1 = (c == avifChannel::A) ? image1.alphaPlane : image1.yuvPlanes[c];
        uint8_t * row2 = (c == avifChannel::A) ? image2.alphaPlane : image2.yuvPlanes[c];
        if (!row1 != !row2) {
            return false;
        }
        if (!row1) {
            continue;
        }
        const uint32_t rowBytes1 = (c == avifChannel::A) ? image1.alphaRowBytes : image1.yuvRowBytes[c];
        const uint32_t rowBytes2 = (c == avifChannel::A) ? image2.alphaRowBytes : image2.yuvRowBytes[c];
        const uint32_t planeWidth =
            (c == AVIF_CHAN_Y || c == avifChannel::A) ? image1.width : ((image1.width + info.chromaShiftX) >> info.chromaShiftX);
        const uint32_t planeHeight =
            (c == AVIF_CHAN_Y || c == avifChannel::A) ? image1.height : ((image1.height + info.chromaShiftY) >> info.chromaShiftY);
        for (uint32_t y = 0; y < planeHeight; ++y) {
            if (avifImageUsesU16(&image1)) {
                if (!std::equal(reinterpret_cast<uint16_t *>(row1),
                                reinterpret_cast<uint16_t *>(row1) + planeWidth,
                                reinterpret_cast<uint16_t *>(row2))) {
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
