// Copyright 2022 Google LLC
// SPDX-License-Identifier: BSD-2-Clause

#include "unicode.h"

#if defined(_WIN32) && defined(_UNICODE)

#include <fcntl.h>
#include <io.h>
#include <wchar.h>
#include <windows.h>
#include <shellapi.h>

W_CHAR * * AVIF_CommandLineToArgvW(const W_CHAR* lpCmdLine, int * pNumArgs)
{
    return CommandLineToArgvW(lpCmdLine, pNumArgs);
}

W_CHAR* AVIF_GetCommandLineW() {
  return GetCommandLineW();
}

int AVIF_fileno(FILE * stream)
{
    return fileno(stream);
}

FILE * AVIF_wfopen(const W_CHAR * filename, const W_CHAR * mode)
{
    return _wfopen(filename, mode);
}

int AVIF_setmode(int fd, int mode)
{
    return _setmode(fd, mode);
}

int AVIF_setmode_u8(int fd)
{
    return _setmode(fd, _O_U8TEXT);
}

void AVIF_LocalFree(W_CHAR * * const wargv)
{
    LocalFree(wargv);
}

#endif
