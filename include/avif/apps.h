// Copyright 2026 Yuan Tong. All rights reserved.
// SPDX-License-Identifier: BSD-2-Clause

#ifndef AVIF_APPS_H
#define AVIF_APPS_H

#include "avif/avif.h" // IWYU pragma: export

#ifdef __cplusplus
extern "C" {
#endif

// A shared library intended to be loaded by avifenc/avifdec with --custom-codec must export both
// functions below. Only works with build using shared libavif.
//
// Setup is called once after the library is loaded and before the codec name is resolved. The
// custom codec should initialize its private states and call avifRegisterCustomCodec().
// Shutdown is called once before the library is unloaded, if Setup succeeded. The custom codec can
// use that as an opportunity to clean up their own state. Codecs stay registered after Shutdown,
// but the apps do not use them anymore at that point.
#define AVIF_APPS_CUSTOM_CODEC_SETUP_SYMBOL "avifAppsCustomCodecSetup"
#define AVIF_APPS_CUSTOM_CODEC_SHUTDOWN_SYMBOL "avifAppsCustomCodecShutdown"

// These functions always need to be exported.
#define AVIF_APPS_CUSTOM_CODEC_API AVIF_HELPER_EXPORT

typedef avifResult (*avifAppsCustomCodecSetupFunc)(avifDiagnostics * diag);
typedef avifResult (*avifAppsCustomCodecShutdownFunc)(avifDiagnostics * diag);

AVIF_APPS_CUSTOM_CODEC_API avifResult avifAppsCustomCodecSetup(avifDiagnostics * diag);
AVIF_APPS_CUSTOM_CODEC_API avifResult avifAppsCustomCodecShutdown(avifDiagnostics * diag);

#ifdef __cplusplus
} // extern "C"
#endif

#endif // AVIF_APPS_H
