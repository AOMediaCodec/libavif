// Copyright 2019 Joe Drago. All rights reserved.
// SPDX-License-Identifier: BSD-2-Clause

#ifndef LIBAVIF_APPS_SHARED_Y4M_H
#define LIBAVIF_APPS_SHARED_Y4M_H

#include "avif/avif.h"

avifBool y4mRead(avifImage * avif, const char * inputFilename);
avifBool y4mWrite(avifImage * avif, const char * outputFilename);

#endif // ifndef LIBAVIF_APPS_SHARED_Y4M_H
