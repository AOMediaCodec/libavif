// Copyright 2020 Joe Drago. All rights reserved.
// SPDX-License-Identifier: BSD-2-Clause

#ifndef COMPARE_H
#define COMPARE_H

#include "avif/avif.h"

typedef struct ImageComparison
{
    int maxDiff;
    int maxDiffY;
    int maxDiffU;
    int maxDiffV;
    int maxDiffA;

    float avgDiff;
    float avgDiffY;
    float avgDiffU;
    float avgDiffV;
    float avgDiffA;
} ImageComparison;

// Returns AVIF_FALSE if they're not even worth comparing (mismatched sizes / pixel formats / etc)
avifBool compareYUVA(ImageComparison * ic, const avifImage * image1, const avifImage * image2);

#endif
