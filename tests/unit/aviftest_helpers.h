// Copyright 2022 Google LLC
// SPDX-License-Identifier: BSD-2-Clause

#ifndef LIBAVIF_TESTS_AVIFTEST_HELPERS_H_
#define LIBAVIF_TESTS_AVIFTEST_HELPERS_H_

#include "avif/avif.h"

#include <memory>

namespace libavif
{
namespace testutil
{

using avifImagePtr = std::unique_ptr<avifImage, decltype(&avifImageDestroy)>;
using avifEncoderPtr = std::unique_ptr<avifEncoder, decltype(&avifEncoderDestroy)>;
using avifDecoderPtr = std::unique_ptr<avifDecoder, decltype(&avifDecoderDestroy)>;

class avifRWDataCleaner : public avifRWData
{
public:
    avifRWDataCleaner() : avifRWData({}) {}
    ~avifRWDataCleaner() { avifRWDataFree(this); }
};

// Creates an image. Returns null in case of memory failure.
avifImagePtr createImage(int width, int height, int depth, avifPixelFormat yuvFormat, avifPlanesFlags planes, avifRange yuvRange = AVIF_RANGE_FULL);

// Set all pixels of each plane of an image.
void fillImagePlain(avifImage * image, const uint32_t yuva[4]);
void fillImageGradient(avifImage * image);

// Returns true if both images have the same features and pixel values.
bool areImagesEqual(const avifImage & image1, const avifImage & image2);

} // namespace testutil
} // namespace libavif

#endif // LIBAVIF_TESTS_AVIFTEST_HELPERS_H_
