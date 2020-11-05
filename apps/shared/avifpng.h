// Copyright 2019 Joe Drago. All rights reserved.
// SPDX-License-Identifier: BSD-2-Clause

#ifndef LIBAVIF_APPS_SHARED_AVIFPNG_H
#define LIBAVIF_APPS_SHARED_AVIFPNG_H

#include "avif/avif.h"

// if (requestedDepth == 0), do best-fit
avifBool avifPNGRead(avifImage * avif, const char * inputFilename, avifPixelFormat requestedFormat, uint32_t requestedDepth, uint32_t * outPNGDepth);
avifBool avifPNGWrite(avifImage * avif, const char * outputFilename, uint32_t requestedDepth, avifChromaUpsampling chromaUpsampling);

#endif // ifndef LIBAVIF_APPS_SHARED_AVIFPNG_H
