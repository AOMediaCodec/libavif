// Copyright 2022 Google LLC
// SPDX-License-Identifier: BSD-2-Clause

#ifndef LIBAVIF_TESTS_AVIFINCRTEST_HELPERS_H_
#define LIBAVIF_TESTS_AVIFINCRTEST_HELPERS_H_

#include "avif/avif.h"

namespace libavif
{
namespace testutil
{

// Encodes a portion of the image to be decoded incrementally.
void encodeRectAsIncremental(const avifImage & image,
                             uint32_t width,
                             uint32_t height,
                             bool createAlphaIfNone,
                             bool flatCells,
                             avifRWData * output,
                             uint32_t * cellWidth,
                             uint32_t * cellHeight);

// Decodes incrementally the encodedAvif and compares the pixels with the given reference.
// If isPersistent is true, the input encodedAvif is considered as accessible during the whole decoding.
// If giveSizeHint is true, the whole encodedAvif size is given as a hint to the decoder.
// useNthImageApi describes whether the NthImage or NextImage decoder API will be used.
// The cellHeight of all planes of the encodedAvif is given to estimate the incremental granularity.
void decodeIncrementally(const avifRWData & encodedAvif,
                         bool isPersistent,
                         bool giveSizeHint,
                         bool useNthImageApi,
                         const avifImage & reference,
                         uint32_t cellHeight);

// Calls decodeIncrementally() with the reference being a regular decoding of encodedAvif.
void decodeNonIncrementallyAndIncrementally(const avifRWData & encodedAvif,
                                            bool isPersistent,
                                            bool giveSizeHint,
                                            bool useNthImageApi,
                                            uint32_t cellHeight);

} // namespace testutil
} // namespace libavif

#endif // LIBAVIF_TESTS_AVIFINCRTEST_HELPERS_H_
