// Copyright 2019 Joe Drago. All rights reserved.
// SPDX-License-Identifier: BSD-2-Clause

#ifndef LIBAVIF_APPS_SHARED_Y4M_H
#define LIBAVIF_APPS_SHARED_Y4M_H

#include "avif/avif.h"

// Optionally pass one of these pointers (set to NULL) on a fresh input. If it successfully reads in
// a frame and sees that there is more data to be read, it will allocate an internal structure remembering
// the y4m header and FILE position and return it. Pass in this pointer to continue reading frames.
// The structure will always be freed upon failure or reaching EOF.
struct y4mFrameIterator;

avifBool y4mRead(avifImage * avif, const char * inputFilename, struct y4mFrameIterator ** iter);
avifBool y4mWrite(avifImage * avif, const char * outputFilename);

#endif // ifndef LIBAVIF_APPS_SHARED_Y4M_H
