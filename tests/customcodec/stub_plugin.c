// Copyright 2026 Yuan Tong. All rights reserved.
// SPDX-License-Identifier: BSD-2-Clause

// Exposes the stub codec to avifenc/avifdec through --custom-codec.

#include <stdio.h>

#include "avif/apps.h"
#include "stub_codec.h"

avifResult avifAppsCustomCodecSetup(avifDiagnostics * diag)
{
    (void)diag;
    avifCodecChoice choice;
    return avifStubCodecRegister(&choice);
}

avifResult avifAppsCustomCodecShutdown(avifDiagnostics * diag)
{
    (void)diag;
    const avifStubCodecStats stats = avifStubCodecGetStats();
    // Checked by test_cmd_custom_codec.sh.
    printf("Custom codec stub: encoded %u frame(s), decoded %u frame(s)\n", stats.encodedFrames, stats.decodedFrames);
    return AVIF_RESULT_OK;
}
