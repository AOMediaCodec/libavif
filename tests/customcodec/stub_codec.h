// Copyright 2026 Yuan Tong. All rights reserved.
// SPDX-License-Identifier: BSD-2-Clause

// A stub custom codec used to test the custom codec registry. It only uses the public libavif API,
// so it can be linked statically into a test or built as a shared library loaded by the apps.
//
// The encoder only accepts the fixed input image described below and always emits the same
// hardcoded AV1 bitstream. The decoder only accepts that bitstream and always outputs the fixed
// image. A successful round trip therefore proves both requests reached the stub codec.

#ifndef LIBAVIF_TESTS_CUSTOMCODEC_STUB_CODEC_H
#define LIBAVIF_TESTS_CUSTOMCODEC_STUB_CODEC_H

#include "avif/codec.h"

#ifdef __cplusplus
extern "C" {
#endif

#define AVIF_STUB_CODEC_NAME "stub"

// The fixed image: 8x8, 8-bit, YUV 4:2:0.
#define AVIF_STUB_CODEC_WIDTH 8
#define AVIF_STUB_CODEC_HEIGHT 8
#define AVIF_STUB_CODEC_DEPTH 8
#define AVIF_STUB_CODEC_FORMAT AVIF_PIXEL_FORMAT_YUV420
// Sample value of the fixed image at (x, y) of the given plane (AVIF_CHAN_Y, AVIF_CHAN_U or AVIF_CHAN_V).
uint8_t avifStubCodecPixel(int plane, uint32_t x, uint32_t y);

typedef struct avifStubCodecStats
{
    uint32_t encodedFrames;
    uint32_t decodedFrames;
} avifStubCodecStats;

// Registers the stub codec, or returns the existing registration if a codec named
// AVIF_STUB_CODEC_NAME is already registered.
avifResult avifStubCodecRegister(avifCodecChoice * choice);
// Number of frames that went through the stub encoder and decoder so far.
avifStubCodecStats avifStubCodecGetStats(void);

#ifdef __cplusplus
} // extern "C"
#endif

#endif // LIBAVIF_TESTS_CUSTOMCODEC_STUB_CODEC_H
