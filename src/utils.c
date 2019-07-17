// Copyright 2019 Joe Drago. All rights reserved.
// SPDX-License-Identifier: BSD-2-Clause

#include "avif/internal.h"

#include <math.h>
#include <string.h>

float avifRoundf(float v)
{
    return floorf(v + 0.5f);
}

// Thanks, Rob Pike! https://commandcenter.blogspot.nl/2012/04/byte-order-fallacy.html

uint16_t avifHTONS(uint16_t s)
{
    uint8_t data[2];
    data[0] = (s >> 8) & 0xff;
    data[1] = (s >> 0) & 0xff;
    uint16_t result;
    memcpy(&result, data, sizeof(uint16_t));
    return result;
}

uint16_t avifNTOHS(uint16_t s)
{
    uint8_t data[2];
    memcpy(&data, &s, sizeof(data));

    return (uint16_t)((data[1] << 0) | (data[0] << 8));
}

uint32_t avifHTONL(uint32_t l)
{
    uint8_t data[4];
    data[0] = (l >> 24) & 0xff;
    data[1] = (l >> 16) & 0xff;
    data[2] = (l >> 8) & 0xff;
    data[3] = (l >> 0) & 0xff;
    uint32_t result;
    memcpy(&result, data, sizeof(uint32_t));
    return result;
}

uint32_t avifNTOHL(uint32_t l)
{
    uint8_t data[4];
    memcpy(&data, &l, sizeof(data));

    return ((uint32_t)data[3] << 0) | ((uint32_t)data[2] << 8) | ((uint32_t)data[1] << 16) | ((uint32_t)data[0] << 24);
}

uint64_t avifHTON64(uint64_t l)
{
    uint8_t data[8];
    data[0] = (l >> 56) & 0xff;
    data[1] = (l >> 48) & 0xff;
    data[2] = (l >> 40) & 0xff;
    data[3] = (l >> 32) & 0xff;
    data[4] = (l >> 24) & 0xff;
    data[5] = (l >> 16) & 0xff;
    data[6] = (l >> 8) & 0xff;
    data[7] = (l >> 0) & 0xff;
    uint64_t result;
    memcpy(&result, data, sizeof(uint64_t));
    return result;
}

uint64_t avifNTOH64(uint64_t l)
{
    uint8_t data[8];
    memcpy(&data, &l, sizeof(data));

    return ((uint64_t)data[7] << 0) | ((uint64_t)data[6] << 8) | ((uint64_t)data[5] << 16) | ((uint64_t)data[4] << 24) |
           ((uint64_t)data[3] << 32) | ((uint64_t)data[2] << 40) | ((uint64_t)data[1] << 48) | ((uint64_t)data[0] << 56);
}

AVIF_ARRAY_DECLARE(avifArrayInternal, uint8_t, ptr);

void avifArrayCreate(void * arrayStruct, uint32_t elementSize, uint32_t initialCapacity)
{
    avifArrayInternal * arr = (avifArrayInternal *)arrayStruct;
    arr->elementSize = elementSize ? elementSize : 1;
    arr->count = 0;
    arr->capacity = initialCapacity;
    arr->ptr = (uint8_t *)avifAlloc(arr->elementSize * arr->capacity);
    memset(arr->ptr, 0, arr->elementSize * arr->capacity);
}

uint32_t avifArrayPushIndex(void * arrayStruct)
{
    avifArrayInternal * arr = (avifArrayInternal *)arrayStruct;
    if (arr->count == arr->capacity) {
        uint8_t * oldPtr = arr->ptr;
        uint32_t oldByteCount = arr->elementSize * arr->capacity;
        arr->ptr = (uint8_t *)avifAlloc(oldByteCount * 2);
        memset(arr->ptr + oldByteCount, 0, oldByteCount);
        memcpy(arr->ptr, oldPtr, oldByteCount);
        arr->capacity *= 2;
        avifFree(oldPtr);
    }
    ++arr->count;
    return arr->count - 1;
}

void * avifArrayPushPtr(void * arrayStruct)
{
    uint32_t index = avifArrayPushIndex(arrayStruct);
    avifArrayInternal * arr = (avifArrayInternal *)arrayStruct;
    return &arr->ptr[index * arr->elementSize];
}

void avifArrayPush(void * arrayStruct, void * element)
{
    avifArrayInternal * arr = (avifArrayInternal *)arrayStruct;
    void * newElement = avifArrayPushPtr(arr);
    memcpy(newElement, element, arr->elementSize);
}

void avifArrayDestroy(void * arrayStruct)
{
    avifArrayInternal * arr = (avifArrayInternal *)arrayStruct;
    if (arr->ptr) {
        avifFree(arr->ptr);
        arr->ptr = NULL;
    }
    memset(arr, 0, sizeof(avifArrayInternal));
}
