// Copyright 2022 Google LLC
// SPDX-License-Identifier: BSD-2-Clause

#ifndef LIBAVIF_TESTS_AVIFINCRTEST_HELPERS_H_
#define LIBAVIF_TESTS_AVIFINCRTEST_HELPERS_H_

#include "avif/avif.h"

#ifdef __cplusplus
extern "C" {
#endif

// Encodes a portion of the image to be decoded incrementally.
avifBool encodeRectAsIncremental(const avifImage * image,
                                 uint32_t width,
                                 uint32_t height,
                                 avifBool createAlphaIfNone,
                                 avifBool flatCells,
                                 avifRWData * output,
                                 uint32_t * cellWidth,
                                 uint32_t * cellHeight);

// Decodes the data into an image.
avifBool decodeNonIncrementally(const avifRWData * encodedAvif, avifImage * image);

// Decodes incrementally the encodedAvif and compares the pixels with the given reference.
// If isPersistent is true, the input encodedAvif is considered as accessible during the whole decoding.
// If giveSizeHint is true, the whole encodedAvif size is given as a hint to the decoder.
// useNthImageApi describes whether the NthImage or NextImage decoder API will be used.
// The cellHeight of all planes of the encodedAvif is given to estimate the incremental granularity.
avifBool decodeIncrementally(const avifRWData * encodedAvif,
                             avifBool isPersistent,
                             avifBool giveSizeHint,
                             avifBool useNthImageApi,
                             const avifImage * reference,
                             uint32_t cellHeight);

// Calls decodeIncrementally() with the output of decodeNonIncrementally() as reference.
avifBool decodeNonIncrementallyAndIncrementally(const avifRWData * encodedAvif,
                                                avifBool isPersistent,
                                                avifBool giveSizeHint,
                                                avifBool useNthImageApi,
                                                uint32_t cellHeight);

#ifdef __cplusplus
} // extern "C"
#endif

#endif // LIBAVIF_TESTS_AVIFINCRTEST_HELPERS_H_
