// Copyright 2022 Google LLC
// SPDX-License-Identifier: BSD-2-Clause

#ifndef LIBAVIF_TESTS_AVIFTEST_HELPERS_H_
#define LIBAVIF_TESTS_AVIFTEST_HELPERS_H_

#include <memory>

#include "avif/avif.h"

namespace libavif {
namespace testutil {

enum avifChannel
{
    R = 0,
    G = 1,
    B = 2,
    A = 3
};

// Maps from avifChannel to sample position in avifRGBFormat.
uint32_t avifChannelOffset(avifRGBFormat format, avifChannel channel);

using avifImagePtr = std::unique_ptr<avifImage, decltype(&avifImageDestroy)>;
using avifEncoderPtr =
    std::unique_ptr<avifEncoder, decltype(&avifEncoderDestroy)>;
using avifDecoderPtr =
    std::unique_ptr<avifDecoder, decltype(&avifDecoderDestroy)>;

class avifRWDataCleaner : public avifRWData {
 public:
  avifRWDataCleaner() : avifRWData({}) {}
  ~avifRWDataCleaner() { avifRWDataFree(this); }
};

class avifRGBImageCleaner : public avifRGBImage
{
public:
    avifRGBImageCleaner(const avifImage * yuv, int rgbDepth, avifRGBFormat rgbFormat);
    ~avifRGBImageCleaner() { avifRGBImageFreePixels(this); }
};

// Creates an image. Returns null in case of memory failure.
avifImagePtr createImage(int width, int height, int depth,
                         avifPixelFormat yuvFormat, avifPlanesFlags planes,
                         avifRange yuvRange = AVIF_RANGE_FULL);

// Set all pixels of each plane of an image.
void fillImagePlain(avifImage * image, const uint32_t yuva[4]);
void fillImageGradient(avifImage * image);
void fillImageChannel(avifRGBImage * image, avifChannel channel, uint32_t value);

// Returns true if both images have the same features and pixel values.
bool areImagesEqual(const avifImage& image1, const avifImage& image2);

}  // namespace testutil
}  // namespace libavif

#endif  // LIBAVIF_TESTS_AVIFTEST_HELPERS_H_
