// Copyright 2023 Yuan Tong. All rights reserved.
// SPDX-License-Identifier: BSD-2-Clause

#ifndef LIBAVIF_APPS_SHARED_ICCMAKER_H
#define LIBAVIF_APPS_SHARED_ICCMAKER_H

#include "avif/avif.h"

#ifdef __cplusplus
extern "C" {
#endif

avifBool avifImageGenerateRGBICCFromGammaPrimaries(avifImage * image, float gamma, const float primaries[8]);
avifBool avifImageGenerateGrayICCFromGammaPrimaries(avifImage * image, float gamma, const float white[2]);

#ifdef __cplusplus
} // extern "C"
#endif

#endif //LIBAVIF_APPS_SHARED_ICCMAKER_H
