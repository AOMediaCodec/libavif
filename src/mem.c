// Copyright 2019 Joe Drago. All rights reserved.
// SPDX-License-Identifier: BSD-2-Clause

#include "avif/internal.h"

#include <assert.h>
#include <stdlib.h>

void * avifAlloc(size_t size)
{
    assert(size != 0); // Implementation-defined. See https://en.cppreference.com/w/cpp/memory/c/malloc
    return malloc(size);
}

void * avifCalloc(size_t count, size_t size)
{
    return calloc(count, size);
}

void avifFree(void * p)
{
    free(p);
}
