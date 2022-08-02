// Copyright 2022 Google LLC
// SPDX-License-Identifier: BSD-2-Clause

#ifndef LIBAVIF_TESTS_AVIFTEST_HELPERS_H_
#define LIBAVIF_TESTS_AVIFTEST_HELPERS_H_

#include <memory>

#include "avif/avif.h"

namespace libavif {
namespace testutil {

//------------------------------------------------------------------------------
// Memory management

using AvifImagePtr = std::unique_ptr<avifImage, decltype(&avifImageDestroy)>;
using AvifEncoderPtr =
    std::unique_ptr<avifEncoder, decltype(&avifEncoderDestroy)>;
using AvifDecoderPtr =
    std::unique_ptr<avifDecoder, decltype(&avifDecoderDestroy)>;

class AvifRwData : public avifRWData {
 public:
  AvifRwData() : avifRWData{nullptr, 0} {}
  ~AvifRwData() { avifRWDataFree(this); }
};

class AvifRgbImage : public avifRGBImage {
 public:
  AvifRgbImage(const avifImage* yuv, int rgbDepth, avifRGBFormat rgbFormat);
  ~AvifRgbImage() { avifRGBImageFreePixels(this); }
};

//------------------------------------------------------------------------------
// Samples and images

// Contains the sample position of each channel for a given avifRGBFormat.
// The alpha sample position is set to 0 for layouts having no alpha channel.
struct RgbChannelOffsets {
  uint8_t r, g, b, a;
};
RgbChannelOffsets GetRgbChannelOffsets(avifRGBFormat format);

// Creates an image. Returns null in case of memory failure.
AvifImagePtr CreateImage(int width, int height, int depth,
                         avifPixelFormat yuv_format, avifPlanesFlags planes,
                         avifRange yuv_range = AVIF_RANGE_FULL);

// Set all pixels of each plane of an image.
void FillImagePlain(avifImage* image, const uint32_t yuva[4]);
void FillImageGradient(avifImage* image);
void FillImageChannel(avifRGBImage* image, uint32_t channel_offset,
                      uint32_t value);

// Returns true if both images have the same features and pixel values.
// If ignore_alpha is true, the alpha channel is not taken into account in the
// comparison.
bool AreImagesEqual(const avifImage& image1, const avifImage& image2,
                    bool ignore_alpha = false);

//------------------------------------------------------------------------------

}  // namespace testutil
}  // namespace libavif

#endif  // LIBAVIF_TESTS_AVIFTEST_HELPERS_H_
