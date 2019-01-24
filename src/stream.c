// Copyright 2019 Joe Drago. All rights reserved.
// SPDX-License-Identifier: BSD-2-Clause

#include "avif/internal.h"

#include <string.h>

void avifStreamStart(avifStream * stream, avifRawData * raw)
{
    stream->raw = raw;
    stream->offset = 0;
}

// ---------------------------------------------------------------------------
// Read

avifBool avifStreamHasBytesLeft(avifStream * stream, size_t byteCount)
{
    return (stream->offset + byteCount) <= stream->raw->size;
}

avifBool avifStreamSkip(avifStream * stream, size_t byteCount)
{
    if (!avifStreamHasBytesLeft(stream, byteCount)) {
        return AVIF_FALSE;
    }
    stream->offset += byteCount;
    return AVIF_TRUE;
}

avifBool avifStreamRead(avifStream * stream, uint8_t * data, size_t size)
{
    if (!avifStreamHasBytesLeft(stream, size)) {
        return AVIF_FALSE;
    }

    memcpy(data, stream->raw->data + stream->offset, size);
    stream->offset += size;
    return AVIF_TRUE;
}

avifBool avifStreamReadUX8(avifStream * stream, uint64_t * v, uint64_t factor)
{
    if (factor == 0) {
        // Don't read anything, just set to 0
        *v = 0;
    } else if (factor == 1) {
        uint8_t tmp;
        CHECK(avifStreamRead(stream, &tmp, 1));
        *v = tmp;
    } else if (factor == 2) {
        uint16_t tmp;
        CHECK(avifStreamReadU16(stream, &tmp));
        *v = tmp;
    } else if (factor == 4) {
        uint32_t tmp;
        CHECK(avifStreamReadU32(stream, &tmp));
        *v = tmp;
    } else if (factor == 8) {
        uint64_t tmp;
        CHECK(avifStreamReadU64(stream, &tmp));
        *v = tmp;
    } else {
        // Unsupported factor
        return AVIF_FALSE;
    }
    return AVIF_TRUE;
}

avifBool avifStreamReadU16(avifStream * stream, uint16_t * v)
{
    CHECK(avifStreamRead(stream, (uint8_t *)v, sizeof(uint16_t)));
    *v = avifNTOHS(*v);
    return AVIF_TRUE;
}

avifBool avifStreamReadU32(avifStream * stream, uint32_t * v)
{
    CHECK(avifStreamRead(stream, (uint8_t *)v, sizeof(uint32_t)));
    *v = avifNTOHL(*v);
    return AVIF_TRUE;
}

avifBool avifStreamReadU64(avifStream * stream, uint64_t * v)
{
    CHECK(avifStreamRead(stream, (uint8_t *)v, sizeof(uint64_t)));
    *v = avifNTOH64(*v);
    return AVIF_TRUE;
}

avifBool avifStreamReadBoxHeader(avifStream * stream, uint8_t outputType[4], size_t * outputContentSize)
{
    size_t startOffset = stream->offset;

    uint32_t smallSize;
    CHECK(avifStreamReadU32(stream, &smallSize));
    CHECK(avifStreamRead(stream, outputType, 4));

    uint64_t size = smallSize;
    if (size == 1) {
        CHECK(avifStreamReadU64(stream, &size));
    }

    if (!memcmp(outputType, "uuid", 4)) {
        CHECK(avifStreamSkip(stream, 16));
    }

    *outputContentSize = (size_t)(size - (stream->offset - startOffset));
    return AVIF_TRUE;
}

avifBool avifStreamReadVersionAndFlags(avifStream * stream, uint8_t * version)
{
    uint8_t versionAndFlags[4];
    CHECK(avifStreamRead(stream, versionAndFlags, 4));
    if (version) {
        *version = versionAndFlags[0];
    }
    return AVIF_TRUE;
}

avifBool avifStreamReadAndEnforceVersion(avifStream * stream, uint8_t enforcedVersion)
{
    uint8_t version;
    CHECK(avifStreamReadVersionAndFlags(stream, &version));
    return (version == enforcedVersion) ? AVIF_TRUE : AVIF_FALSE;
}

// ---------------------------------------------------------------------------
// Write

#define AVIF_STREAM_BUFFER_INCREMENT (1024 * 1024)
static void makeRoom(avifStream * stream, size_t size)
{
    size_t neededSize = stream->offset + size;
    size_t newSize = stream->raw->size;
    while (newSize < neededSize) {
        newSize += AVIF_STREAM_BUFFER_INCREMENT;
    }
    if (stream->raw->size != newSize) {
        avifRawDataRealloc(stream->raw, newSize);
    }
}

void avifStreamFinishWrite(avifStream * stream)
{
    if (stream->raw->size != stream->offset) {
        if (stream->offset) {
            stream->raw->size = stream->offset;
        } else {
            avifRawDataFree(stream->raw);
        }
    }
}

void avifStreamWrite(avifStream * stream, const uint8_t * data, size_t size)
{
    if (!size) {
        return;
    }

    makeRoom(stream, size);
    memcpy(stream->raw->data + stream->offset, data, size);
    stream->offset += size;
}

avifBoxMarker avifStreamWriteBox(avifStream * stream, const char * type, int version, size_t contentSize)
{
    avifBoxMarker marker = stream->offset;
    size_t headerSize = sizeof(uint32_t) + 4 /* size of type */;
    if (version != -1) {
        headerSize += 4;
    }

    makeRoom(stream, headerSize);
    memset(stream->raw->data + stream->offset, 0, headerSize);
    if (version != -1) {
        stream->raw->data[stream->offset + 8] = version;
    }
    uint32_t noSize = avifNTOHL((uint32_t)(headerSize + contentSize));
    memcpy(stream->raw->data + stream->offset, &noSize, sizeof(uint32_t));
    memcpy(stream->raw->data + stream->offset + 4, type, 4);
    stream->offset += headerSize;

    return marker;
}

void avifStreamFinishBox(avifStream * stream, avifBoxMarker marker)
{
    uint32_t noSize = avifNTOHL((uint32_t)(stream->offset - marker));
    memcpy(stream->raw->data + marker, &noSize, sizeof(uint32_t));
}

void avifStreamWriteU16(avifStream * stream, uint16_t v)
{
    size_t size = sizeof(uint16_t);
    v = avifHTONS(v);
    makeRoom(stream, size);
    memcpy(stream->raw->data + stream->offset, &v, size);
    stream->offset += size;
}

void avifStreamWriteU32(avifStream * stream, uint32_t v)
{
    size_t size = sizeof(uint32_t);
    v = avifHTONL(v);
    makeRoom(stream, size);
    memcpy(stream->raw->data + stream->offset, &v, size);
    stream->offset += size;
}

void avifStreamWriteZeros(avifStream * stream, size_t byteCount)
{
    makeRoom(stream, byteCount);
    uint8_t * p = stream->raw->data + stream->offset;
    uint8_t * end = p + byteCount;
    while (p != end) {
        *p = 0;
        ++p;
    }
    stream->offset += byteCount;
}
