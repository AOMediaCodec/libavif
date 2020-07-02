// Copyright 2019 Joe Drago. All rights reserved.
// SPDX-License-Identifier: BSD-2-Clause

#ifndef LIBAVIF_APPS_SHARED_AVIFJPEG_H
#define LIBAVIF_APPS_SHARED_AVIFJPEG_H

#include "avif/avif.h"

avifBool avifJPEGRead(avifImage * avif, const char * inputFilename, avifPixelFormat requestedFormat, uint32_t requestedDepth);
avifBool avifJPEGWrite(avifImage * avif, const char * outputFilename, int jpegQuality, avifChromaUpsampling chromaUpsampling);

#endif // ifndef LIBAVIF_APPS_SHARED_AVIFJPEG_H
