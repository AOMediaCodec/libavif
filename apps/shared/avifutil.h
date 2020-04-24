// Copyright 2019 Joe Drago. All rights reserved.
// SPDX-License-Identifier: BSD-2-Clause

#ifndef LIBAVIF_APPS_SHARED_AVIFUTIL_H
#define LIBAVIF_APPS_SHARED_AVIFUTIL_H

#include "avif/avif.h"

void avifImageDump(avifImage * avif);
void avifPrintVersions(void);

typedef enum avifAppFileFormat
{
    AVIF_APP_FILE_FORMAT_UNKNOWN = 0,

    AVIF_APP_FILE_FORMAT_AVIF,
    AVIF_APP_FILE_FORMAT_JPEG,
    AVIF_APP_FILE_FORMAT_PNG,
    AVIF_APP_FILE_FORMAT_Y4M
} avifAppFileFormat;

avifAppFileFormat avifGuessFileFormat(const char * filename);

#endif // ifndef LIBAVIF_APPS_SHARED_AVIFUTIL_H
