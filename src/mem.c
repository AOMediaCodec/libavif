// Copyright 2019 Joe Drago. All rights reserved.
// SPDX-License-Identifier: BSD-2-Clause

#include "avif/avif.h"

#include <assert.h>
#include <stdlib.h>

void * avifAlloc(size_t size)
{
    if (size == 0) {
        avifBreakOnError();
        return NULL;
    }
    return malloc(size);
}

void avifFree(void * p)
{
    free(p);
}
