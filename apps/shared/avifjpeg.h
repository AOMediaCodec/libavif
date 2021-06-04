// Copyright 2019 Joe Drago. All rights reserved.
// SPDX-License-Identifier: BSD-2-Clause

#ifndef LIBAVIF_APPS_SHARED_AVIFJPEG_H
#define LIBAVIF_APPS_SHARED_AVIFJPEG_H

#include "avif/avif.h"

avifBool avifJPEGRead(const char * inputFilename, avifImage * avif, avifPixelFormat requestedFormat, uint32_t requestedDepth);
avifBool avifJPEGWrite(const char * outputFilename, const avifImage * avif, int jpegQuality, avifChromaUpsampling chromaUpsampling);

#endif // ifndef LIBAVIF_APPS_SHARED_AVIFJPEG_H
