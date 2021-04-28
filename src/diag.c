// Copyright 2021 Joe Drago. All rights reserved.
// SPDX-License-Identifier: BSD-2-Clause

#include "avif/internal.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

void avifDiagnosticsClearError(avifDiagnostics * diag)
{
    memset(diag->error, 0, AVIF_DIAGNOSTICS_ERROR_BUFFER_SIZE);
}

static void avifDiagnosticsPrintv(avifDiagnostics * diag, const char * format, va_list args)
{
    if (*diag->error) {
        // There is already a detailed error set.
        return;
    }

    vsnprintf(diag->error, AVIF_DIAGNOSTICS_ERROR_BUFFER_SIZE, format, args);
    diag->error[AVIF_DIAGNOSTICS_ERROR_BUFFER_SIZE - 1] = '\0';
}

void avifDiagnosticsPrintf(avifDiagnostics * diag, const char * format, ...)
{
    if (!diag) {
        // It is possible this is NULL (e.g. calls to avifFileTypeIsCompatible())
        return;
    }

    va_list args;
    va_start(args, format);
    avifDiagnosticsPrintv(diag, format, args);
    va_end(args);
}
