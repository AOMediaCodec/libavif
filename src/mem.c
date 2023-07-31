// Copyright 2019 Joe Drago. All rights reserved.
// SPDX-License-Identifier: BSD-2-Clause

#include "avif/avif.h"

#include <stdlib.h>

void * avifAlloc(size_t size)
{
    void * out = malloc(size);
    if (out == NULL) {
        // TODO(issue #820): Remove once all calling sites propagate the error as AVIF_RESULT_OUT_OF_MEMORY.
        abort();
    }
    return out;
}

void avifFree(void * p)
{
    free(p);
}
