// Copyright 2021 Yuan Tong. All rights reserved.
// SPDX-License-Identifier: BSD-2-Clause

#ifndef LIBAVIF_APPS_SHARED_AVIFWIC_H
#define LIBAVIF_APPS_SHARED_AVIFWIC_H

#include "avif/avif.h"

avifBool avifWICRead(const char * inputFilename, avifImage * avif, avifPixelFormat requestedFormat, uint32_t requestedDepth, uint32_t * outDepth);

#endif //LIBAVIF_APPS_SHARED_AVIFWIC_H
