// Copyright 2019 Joe Drago. All rights reserved.
// SPDX-License-Identifier: BSD-2-Clause

#include "avif/internal.h"

#include <assert.h>
#include <math.h>
#include <string.h>

float avifRoundf(float v)
{
    return floorf(v + 0.5f);
}

// Thanks, Rob Pike! https://commandcenter.blogspot.nl/2012/04/byte-order-fallacy.html

uint16_t avifHTONS(uint16_t s)
{
    uint16_t result;
    uint8_t * data = (uint8_t *)&result;
    data[0] = (s >> 8) & 0xff;
    data[1] = (s >> 0) & 0xff;
    return result;
}

uint16_t avifNTOHS(uint16_t s)
{
    const uint8_t * data = (const uint8_t *)&s;
    return (uint16_t)((data[1] << 0) | (data[0] << 8));
}

uint16_t avifCTOHS(uint16_t s)
{
    const uint8_t * data = (const uint8_t *)&s;
    return (uint16_t)((data[0] << 0) | (data[1] << 8));
}

uint32_t avifHTONL(uint32_t l)
{
    uint32_t result;
    uint8_t * data = (uint8_t *)&result;
    data[0] = (l >> 24) & 0xff;
    data[1] = (l >> 16) & 0xff;
    data[2] = (l >> 8) & 0xff;
    data[3] = (l >> 0) & 0xff;
    return result;
}

uint32_t avifNTOHL(uint32_t l)
{
    const uint8_t * data = (const uint8_t *)&l;
    return ((uint32_t)data[3] << 0) | ((uint32_t)data[2] << 8) | ((uint32_t)data[1] << 16) | ((uint32_t)data[0] << 24);
}

uint32_t avifCTOHL(uint32_t l)
{
    const uint8_t * data = (const uint8_t *)&l;
    return ((uint32_t)data[0] << 0) | ((uint32_t)data[1] << 8) | ((uint32_t)data[2] << 16) | ((uint32_t)data[3] << 24);
}

uint64_t avifHTON64(uint64_t l)
{
    uint64_t result;
    uint8_t * data = (uint8_t *)&result;
    data[0] = (l >> 56) & 0xff;
    data[1] = (l >> 48) & 0xff;
    data[2] = (l >> 40) & 0xff;
    data[3] = (l >> 32) & 0xff;
    data[4] = (l >> 24) & 0xff;
    data[5] = (l >> 16) & 0xff;
    data[6] = (l >> 8) & 0xff;
    data[7] = (l >> 0) & 0xff;
    return result;
}

uint64_t avifNTOH64(uint64_t l)
{
    const uint8_t * data = (const uint8_t *)&l;
    return ((uint64_t)data[7] << 0) | ((uint64_t)data[6] << 8) | ((uint64_t)data[5] << 16) | ((uint64_t)data[4] << 24) |
           ((uint64_t)data[3] << 32) | ((uint64_t)data[2] << 40) | ((uint64_t)data[1] << 48) | ((uint64_t)data[0] << 56);
}

AVIF_ARRAY_DECLARE(avifArrayInternal, uint8_t, ptr);

// On error, this function must set arr->ptr to NULL and both arr->count and arr->capacity to 0.
avifBool avifArrayCreate(void * arrayStruct, uint32_t elementSize, uint32_t initialCapacity)
{
    avifArrayInternal * arr = (avifArrayInternal *)arrayStruct;
    arr->elementSize = elementSize ? elementSize : 1;
    arr->count = 0;
    arr->capacity = initialCapacity;
    size_t byteCount = (size_t)arr->elementSize * arr->capacity;
    arr->ptr = (uint8_t *)avifAlloc(byteCount);
    if (!arr->ptr) {
        arr->capacity = 0;
        return AVIF_FALSE;
    }
    memset(arr->ptr, 0, byteCount);
    return AVIF_TRUE;
}

uint32_t avifArrayPushIndex(void * arrayStruct)
{
    avifArrayInternal * arr = (avifArrayInternal *)arrayStruct;
    if (arr->count == arr->capacity) {
        uint8_t * oldPtr = arr->ptr;
        size_t oldByteCount = (size_t)arr->elementSize * arr->capacity;
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
    return &arr->ptr[index * (size_t)arr->elementSize];
}

void avifArrayPush(void * arrayStruct, void * element)
{
    avifArrayInternal * arr = (avifArrayInternal *)arrayStruct;
    void * newElement = avifArrayPushPtr(arr);
    memcpy(newElement, element, arr->elementSize);
}

void avifArrayPop(void * arrayStruct)
{
    avifArrayInternal * arr = (avifArrayInternal *)arrayStruct;
    assert(arr->count > 0);
    --arr->count;
    memset(&arr->ptr[arr->count * (size_t)arr->elementSize], 0, arr->elementSize);
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

// |a| and |b| hold int32_t values. The int64_t type is used so that we can negate INT32_MIN without
// overflowing int32_t.
static int64_t calcGCD(int64_t a, int64_t b)
{
    if (a < 0) {
        a *= -1;
    }
    if (b < 0) {
        b *= -1;
    }
    while (b != 0) {
        int64_t r = a % b;
        a = b;
        b = r;
    }
    return a;
}

void avifFractionSimplify(avifFraction * f)
{
    int64_t gcd = calcGCD(f->n, f->d);
    if (gcd > 1) {
        f->n = (int32_t)(f->n / gcd);
        f->d = (int32_t)(f->d / gcd);
    }
}

static avifBool overflowsInt32(int64_t x)
{
    return (x < INT32_MIN) || (x > INT32_MAX);
}

avifBool avifFractionCD(avifFraction * a, avifFraction * b)
{
    avifFractionSimplify(a);
    avifFractionSimplify(b);
    if (a->d != b->d) {
        const int64_t ad = a->d;
        const int64_t bd = b->d;
        const int64_t anNew = a->n * bd;
        const int64_t adNew = a->d * bd;
        const int64_t bnNew = b->n * ad;
        const int64_t bdNew = b->d * ad;
        if (overflowsInt32(anNew) || overflowsInt32(adNew) || overflowsInt32(bnNew) || overflowsInt32(bdNew)) {
            return AVIF_FALSE;
        }
        a->n = (int32_t)anNew;
        a->d = (int32_t)adNew;
        b->n = (int32_t)bnNew;
        b->d = (int32_t)bdNew;
    }
    return AVIF_TRUE;
}

avifBool avifFractionAdd(avifFraction a, avifFraction b, avifFraction * result)
{
    if (!avifFractionCD(&a, &b)) {
        return AVIF_FALSE;
    }

    const int64_t resultN = (int64_t)a.n + b.n;
    if (overflowsInt32(resultN)) {
        return AVIF_FALSE;
    }
    result->n = (int32_t)resultN;
    result->d = a.d;

    avifFractionSimplify(result);
    return AVIF_TRUE;
}

avifBool avifFractionSub(avifFraction a, avifFraction b, avifFraction * result)
{
    if (!avifFractionCD(&a, &b)) {
        return AVIF_FALSE;
    }

    const int64_t resultN = (int64_t)a.n - b.n;
    if (overflowsInt32(resultN)) {
        return AVIF_FALSE;
    }
    result->n = (int32_t)resultN;
    result->d = a.d;

    avifFractionSimplify(result);
    return AVIF_TRUE;
}

avifBool avifToUnsignedFraction(double v, uint32_t * numerator, uint32_t * denominator)
{
    if (v < 0 || v > UINT32_MAX) {
        return AVIF_FALSE;
    }

    if (round(v) == v) {
        *numerator = (uint32_t)v;
        *denominator = 1u;
        return AVIF_TRUE;
    }

    if (v < 1.0) {
        // Maximize precision by having the denominator as large as possible.
        *denominator = UINT32_MAX;
        *numerator = (uint32_t)round(v * (*denominator));
        return AVIF_TRUE;
    }

    // v >= 1.0f
    // Maximize precision by having the numerator as large as possible.
    *numerator = UINT32_MAX;
    assert(v != 0.0);
    *denominator = (uint32_t)round(*numerator / v);
    assert(*denominator != 0.0);

    if (fabs((double)*numerator / *denominator - v) > fabs(round(v) - v)) {
        // If we get a lower error by just rounding v, then go with that instead.
        // This happens for large values.
        *numerator = (uint32_t)round(v);
        *denominator = 1u;
    }

    return AVIF_TRUE;
}
