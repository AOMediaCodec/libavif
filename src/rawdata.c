// Copyright 2019 Joe Drago. All rights reserved.
// SPDX-License-Identifier: BSD-2-Clause

#include "avif/avif.h"

#include <string.h>

void avifRawDataRealloc(avifRawData * raw, size_t newSize)
{
    if (raw->size != newSize) {
        uint8_t * old = raw->data;
        size_t oldSize = raw->size;
        raw->data = avifAlloc(newSize);
        raw->size = newSize;
        if (oldSize) {
            size_t bytesToCopy = (oldSize < raw->size) ? oldSize : raw->size;
            memcpy(raw->data, old, bytesToCopy);
            avifFree(old);
        }
    }
}

void avifRawDataSet(avifRawData * raw, const uint8_t * data, size_t len)
{
    if (len) {
        avifRawDataRealloc(raw, len);
        memcpy(raw->data, data, len);
    } else {
        avifFree(raw);
    }
}

void avifRawDataFree(avifRawData * raw)
{
    avifFree(raw->data);
    raw->data = NULL;
    raw->size = 0;
}

void avifRawDataConcat(avifRawData * dst, avifRawData ** srcs, int srcsCount)
{
    size_t totalSize = 0;
    for (int i = 0; i < srcsCount; ++i) {
        totalSize += srcs[i]->size;
    }

    avifRawDataRealloc(dst, totalSize);

    uint8_t * p = dst->data;
    for (int i = 0; i < srcsCount; ++i) {
        if (srcs[i]->size) {
            memcpy(p, srcs[i]->data, srcs[i]->size);
            p += srcs[i]->size;
        }
    }
}
