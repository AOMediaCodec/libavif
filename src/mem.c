// Copyright 2019 Joe Drago. All rights reserved.
// SPDX-License-Identifier: BSD-2-Clause

#include "avif/avif.h"

#include <stdlib.h>

void * avifAlloc(size_t size)
{
    // malloc(0) is implementation-defined (see
    // https://en.cppreference.com/w/cpp/memory/c/malloc), so collapse the
    // zero-size case to a deterministic NULL return. Callers must either treat
    // 0 as an allocation failure or guard against it before calling.
    if (size == 0) {
        return NULL;
    }
    return malloc(size);
}

void avifFree(void * p)
{
    free(p);
}
