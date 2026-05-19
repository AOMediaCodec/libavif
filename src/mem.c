// Copyright 2019 Joe Drago. All rights reserved.
// SPDX-License-Identifier: BSD-2-Clause

#include "avif/avif.h"

#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

void * avifAlloc(size_t size)
{
    assert(size != 0); // Implementation-defined. See https://en.cppreference.com/w/cpp/memory/c/malloc
    return malloc(size);
}

void * avifCalloc(size_t count, size_t size)
{
    // Match the contract documented in avif.h: reject zero or overflow rather than
    // passing 0 to avifAlloc (which would trip its assert) or allowing the wrap.
    if (count == 0 || size == 0) {
        return NULL;
    }
    if (count > SIZE_MAX / size) {
        return NULL;
    }
    const size_t bytes = count * size;
    void * ptr = avifAlloc(bytes);
    if (ptr != NULL) {
        memset(ptr, 0, bytes);
    }
    return ptr;
}

void avifFree(void * p)
{
    free(p);
}
