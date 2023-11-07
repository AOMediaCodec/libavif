// Copyright 2023 Google LLC
// SPDX-License-Identifier: BSD-2-Clause

// This is a header only library.

#ifndef AVIF_AVIF_CXX_H
#define AVIF_AVIF_CXX_H

#if !(defined(__cplusplus) || defined(c_plusplus))
#error "This a C++ only header. Use avif/avif.h for C."
#endif

#include <memory>

#include "avif/avif.h"

namespace libavif
{

// Struct to call the destroy functions in a unique_ptr.
struct UniquePtrDeleter
{
    void operator()(avifEncoder * encoder) const { avifEncoderDestroy(encoder); }
    void operator()(avifDecoder * decoder) const { avifDecoderDestroy(decoder); }
    void operator()(avifImage * image) const { avifImageDestroy(image); }
};

// Use these unique_ptr to ensure the structs are automatically destroyed.
typedef std::unique_ptr<avifEncoder, UniquePtrDeleter> EncoderPtr;
typedef std::unique_ptr<avifDecoder, UniquePtrDeleter> DecoderPtr;
typedef std::unique_ptr<avifImage, UniquePtrDeleter> ImagePtr;

} // namespace libavif

#endif // AVIF_AVIF_CXX_H
