// Copyright 2019 Joe Drago. All rights reserved.
// SPDX-License-Identifier: BSD-2-Clause

#include "avif/avif.h"
#include "avif/internal.h"

#include <string.h>

avifResult avifRWDataRealloc(avifRWData * raw, size_t newSize)
{
    if (raw->size != newSize) {
        uint8_t * old = raw->data;
        size_t oldSize = raw->size;
        raw->data = avifAlloc(newSize);
        if (!raw->data) {
            // The alternative would be to keep old in raw->data but this avoids
            // the need of calling avifRWDataFree() on error.
            avifFree(old);
            raw->size = 0;
            return AVIF_RESULT_OUT_OF_MEMORY;
        }
        raw->size = newSize;
        if (oldSize) {
            size_t bytesToCopy = (oldSize < raw->size) ? oldSize : raw->size;
            memcpy(raw->data, old, bytesToCopy);
            avifFree(old);
        }
    }
    return AVIF_RESULT_OK;
}

avifResult avifRWDataSet(avifRWData * raw, const uint8_t * data, size_t len)
{
    if (len) {
        AVIF_CHECKRES(avifRWDataRealloc(raw, len));
        memcpy(raw->data, data, len);
    } else {
        avifRWDataFree(raw);
    }
    return AVIF_RESULT_OK;
}

void avifRWDataFree(avifRWData * raw)
{
    avifFree(raw->data);
    raw->data = NULL;
    raw->size = 0;
}
