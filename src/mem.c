// Copyright 2019 Joe Drago. All rights reserved.
// SPDX-License-Identifier: BSD-2-Clause

#include "avif/avif.h"

#include <assert.h>
#include <stdlib.h>

// Enable this to catch use-after-free issues when debugging on Windows.
// #define DEBUG_USE_AFTER_FREE_ON_WINDOWS

#if !defined(DEBUG_USE_AFTER_FREE_ON_WINDOWS)

// The standard implementation, using malloc() and free().

void * avifAlloc(size_t size)
{
    assert(size != 0); // Implementation-defined. See https://en.cppreference.com/w/cpp/memory/c/malloc
    return malloc(size);
}

void avifFree(void * p)
{
    free(p);
}

#else

// This implementation rounds up all memory allocations to the nearest 4k (page size), allocates
// using VirtualAlloc(), and then records the allocation in a linked list. When avifFree is called,
// instead of freeing the memory, it simply revokes all read and write access to that region
// forever. If any use-after-free issues are discovered (via fuzzing or the like), enabling this
// should immediately catch it in a debugger right when it happens.

#include <stdio.h>
#include <windows.h>

typedef struct Allocation
{
    void * ptr;
    size_t originalSize;
    size_t size;
    struct Allocation * next;
} Allocation;

static Allocation * allocations = NULL;

void * avifAlloc(size_t size)
{
    size_t originalSize = size;
    size = (size + 4095) & ~(4095);

    void * out = VirtualAlloc(NULL, size, MEM_COMMIT, PAGE_READWRITE);
    if (out == NULL) {
        abort();
    }

    Allocation * a = (Allocation *)malloc(sizeof(Allocation));
    a->ptr = out;
    a->originalSize = originalSize;
    a->size = size;
    a->next = allocations;
    allocations = a;

    // printf("Alloc %p %zu (%zu)\n", a->ptr, a->originalSize, a->size);
    return out;
}

void avifFree(void * p)
{
    if (!p) {
        return;
    }

    Allocation * a = allocations;
    for (; a != NULL; a = a->next) {
        if (a->ptr == p) {
            break;
        }
    }

    if (!a) {
        abort();
    }

    DWORD old;
    if (!VirtualProtect(a->ptr, a->size, PAGE_NOACCESS, &old)) {
        abort();
    }

    // printf("Free  %p %zu (%zu)\n", a->ptr, a->originalSize, a->size);
}

#endif
