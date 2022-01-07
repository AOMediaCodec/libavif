// Copyright 2019 Joe Drago. All rights reserved.
// SPDX-License-Identifier: BSD-2-Clause

#ifndef SRC_DECODEINPUT_H_
#define SRC_DECODEINPUT_H_

#include "avif/internal.h"
#include "decoderitem.h"
#include "sample.h"

#include <assert.h>
#include <inttypes.h>
#include <limits.h>
#include <string.h>

// ---------------------------------------------------------------------------
// avifCodecDecodeInput

avifBool avifCodecDecodeInputFillFromSampleTable(avifCodecDecodeInput * decodeInput,
                                                 avifSampleTable * sampleTable,
                                                 const uint32_t imageCountLimit,
                                                 const uint64_t sizeHint,
                                                 avifDiagnostics * diag);

avifBool avifCodecDecodeInputFillFromDecoderItem(avifCodecDecodeInput * decodeInput,
                                                 avifDecoderItem * item,
                                                 avifBool allowProgressive,
                                                 const uint32_t imageCountLimit,
                                                 const uint64_t sizeHint,
                                                 avifDiagnostics * diag);

#endif  // SRC_DECODEINPUT_H_
