// Copyright 2019 Joe Drago. All rights reserved.
// SPDX-License-Identifier: BSD-2-Clause

#ifndef LIBAVIF_APPS_SHARED_AVIFPNG_H
#define LIBAVIF_APPS_SHARED_AVIFPNG_H

#include "avif/avif.h"

// if (requestedDepth == 0), do best-fit
avifBool avifPNGRead(const char * inputFilename, avifImage * avif, avifPixelFormat requestedFormat, uint32_t requestedDepth, uint32_t * outPNGDepth);
avifBool avifPNGWrite(const char * outputFilename, const avifImage * avif, uint32_t requestedDepth, avifChromaUpsampling chromaUpsampling);

#endif // ifndef LIBAVIF_APPS_SHARED_AVIFPNG_H
