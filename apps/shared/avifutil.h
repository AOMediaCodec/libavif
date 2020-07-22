// Copyright 2019 Joe Drago. All rights reserved.
// SPDX-License-Identifier: BSD-2-Clause

#ifndef LIBAVIF_APPS_SHARED_AVIFUTIL_H
#define LIBAVIF_APPS_SHARED_AVIFUTIL_H

#include "avif/avif.h"

/*
 * With Visual Studio before 2013 and mingw-w64 before 2020 the 29th april,
 * (and __USE_MINGW_ANSI_STDIO not set to 1), %z precision specifier is not
 * supported. Hence %I must be used. %I is on the other hand always supported.
 */
#ifdef _WIN32
# define AVIF_FMT_ZU "%Iu"
#else
# define AVIF_FMT_ZU "%zu"
#endif

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
