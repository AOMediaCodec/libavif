// Copyright 2019 Joe Drago. All rights reserved.
// SPDX-License-Identifier: BSD-2-Clause

#include "avif/internal.h"

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

void * avifAllocArray(size_t count, size_t elemSize)
{
    // Explicitly reject count == 0 or elemSize == 0 so callers cannot
    // accidentally request a zero-size allocation (avifAlloc() returns NULL
    // for those anyway, but rejecting them here makes the intent clearer).
    if (count == 0 || elemSize == 0) {
        return NULL;
    }
    // Reject products that would overflow size_t on 32-bit targets. Without
    // this check, a small `count` and a large `elemSize` (or vice versa) can
    // silently wrap to a tiny value, causing the subsequent writes to go far
    // past the end of the returned allocation.
    if (count > SIZE_MAX / elemSize) {
        return NULL;
    }
    return avifAlloc(count * elemSize);
}

void * avifCalloc(size_t count, size_t size)
{
    return calloc(count, size);
}

void avifFree(void * p)
{
    free(p);
}
