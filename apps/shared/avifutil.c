// Copyright 2019 Joe Drago. All rights reserved.
// SPDX-License-Identifier: BSD-2-Clause

#include "avifutil.h"

#include <stdio.h>

void avifImageDump(avifImage * avif)
{
    printf(" * Resolution   : %dx%d\n", avif->width, avif->height);
    printf(" * Bit Depth    : %d\n", avif->depth);
    printf(" * Format       : %s\n", avifPixelFormatToString(avif->yuvFormat));
    switch (avif->profileFormat) {
        case AVIF_PROFILE_FORMAT_NONE:
            printf(" * Color Profile: None\n");
            break;
        case AVIF_PROFILE_FORMAT_ICC:
            printf(" * Color Profile: ICC (%zu bytes)\n", avif->icc.size);
            break;
        case AVIF_PROFILE_FORMAT_NCLX:
            printf(" * Color Profile: nclx - P:%d / T:%d / M:%d / R:%s\n",
            avif->nclx.colourPrimaries, avif->nclx.transferCharacteristics, avif->nclx.matrixCoefficients,
            avif->nclx.fullRangeFlag ? "full" : "limited");
            break;
    }
    printf("\n");
}
