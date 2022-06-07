// Copyright 2019 Joe Drago. All rights reserved.
// SPDX-License-Identifier: BSD-2-Clause

#ifndef LIBAVIF_APPS_SHARED_AVIFJPEG_H
#define LIBAVIF_APPS_SHARED_AVIFJPEG_H

#include "avif/avif.h"

#ifdef __cplusplus
extern "C" {
#endif

avifBool avifJPEGRead(const char * inputFilename, avifImage * avif, avifPixelFormat requestedFormat, uint32_t requestedDepth);
avifBool avifJPEGWrite(const char * outputFilename, const avifImage * avif, int jpegQuality, avifChromaUpsampling chromaUpsampling);

#ifdef __cplusplus
} // extern "C"
#endif

#endif // ifndef LIBAVIF_APPS_SHARED_AVIFJPEG_H
