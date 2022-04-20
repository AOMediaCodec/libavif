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
// The cellHeight of all planes of the encodedAvif is given to estimate the incremental granularity.
avifBool decodeIncrementally(const avifRWData * encodedAvif, const avifImage * reference, uint32_t cellHeight, avifBool useNthImageApi);

// Calls decodeIncrementally() with the output of decodeNonIncrementally() as reference.
avifBool decodeNonIncrementallyAndIncrementally(const avifRWData * encodedAvif, uint32_t cellHeight, avifBool useNthImageApi);

#ifdef __cplusplus
} // extern "C"
#endif

#endif // LIBAVIF_TESTS_AVIFINCRTEST_HELPERS_H_
