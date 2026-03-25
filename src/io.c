// Copyright 2020 Joe Drago. All rights reserved.
// SPDX-License-Identifier: BSD-2-Clause

#if !defined(_WIN32)
// Ensure off_t is 64 bits.
#undef _FILE_OFFSET_BITS
#define _FILE_OFFSET_BITS 64
#define _POSIX_C_SOURCE 200112L
#endif

#include "avif/internal.h"

#include <assert.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#if defined(_WIN32)
// Windows uses _fseeki64 / _ftelli64 for large file support
typedef __int64 avif_off_t;

static int avif_fseeko(FILE * stream, avif_off_t offset, int whence)
{
    return _fseeki64(stream, offset, whence);
}

static avif_off_t avif_ftello(FILE * stream)
{
    return _ftelli64(stream);
}
#else

#if defined(__ANDROID__)
#include <android/api-level.h>
#if __ANDROID_API__ >= 24
#define USE_FSEEKO
#else
#define USE_FSEEK_FALLBACK
#endif
#elif defined(_POSIX_VERSION) && _POSIX_VERSION >= 200112L
/* Standard Modern POSIX */
#define USE_FSEEKO
#else
/* Unknown or very old platform */
#define USE_FSEEK_FALLBACK
#endif

#if defined(USE_FSEEKO)
// POSIX large file support
static_assert(sizeof(off_t) == sizeof(int64_t), "");
typedef off_t avif_off_t;

static int avif_fseeko(FILE * stream, avif_off_t offset, int whence)
{
    return fseeko(stream, offset, whence);
}

static avif_off_t avif_ftello(FILE * stream)
{
    return ftello(stream);
}
#else
typedef long avif_off_t;

static int avif_fseeko(FILE * stream, avif_off_t offset, int whence)
{
    return fseek(stream, offset, whence);
}

static avif_off_t avif_ftello(FILE * stream)
{
    return ftell(stream);
}
#endif // defined(USE_FSEEKO)

#endif // defined(_WIN32)

#define AVIF_OFF_MAX (sizeof(avif_off_t) == 8 ? INT64_MAX : INT32_MAX)

void avifIODestroy(avifIO * io)
{
    if (io && io->destroy) {
        io->destroy(io);
    }
}

// --------------------------------------------------------------------------------------
// avifIOMemoryReader

typedef struct avifIOMemoryReader
{
    avifIO io; // this must be the first member for easy casting to avifIO*
    avifROData rodata;
} avifIOMemoryReader;

static avifResult avifIOMemoryReaderRead(struct avifIO * io, uint32_t readFlags, uint64_t offset, size_t size, avifROData * out)
{
    // printf("avifIOMemoryReaderRead offset %" PRIu64 " size %zu\n", offset, size);

    if (readFlags != 0) {
        // Unsupported readFlags
        return AVIF_RESULT_IO_ERROR;
    }

    avifIOMemoryReader * reader = (avifIOMemoryReader *)io;

    // Sanitize/clamp incoming request
    if (offset > reader->rodata.size) {
        // The offset is past the end of the buffer.
        return AVIF_RESULT_IO_ERROR;
    }
    uint64_t availableSize = reader->rodata.size - offset;
    if (size > availableSize) {
        size = (size_t)availableSize;
    }

    // Prevent the offset addition from triggering an undefined behavior
    // sanitizer error if data is NULL (happens even with offset zero).
    out->data = offset ? reader->rodata.data + offset : reader->rodata.data;
    out->size = size;
    return AVIF_RESULT_OK;
}

static void avifIOMemoryReaderDestroy(struct avifIO * io)
{
    avifFree(io);
}

avifIO * avifIOCreateMemoryReader(const uint8_t * data, size_t size)
{
    avifIOMemoryReader * reader = (avifIOMemoryReader *)avifAlloc(sizeof(avifIOMemoryReader));
    if (reader == NULL) {
        return NULL;
    }
    memset(reader, 0, sizeof(avifIOMemoryReader));
    reader->io.destroy = avifIOMemoryReaderDestroy;
    reader->io.read = avifIOMemoryReaderRead;
    reader->io.sizeHint = size;
    reader->io.persistent = AVIF_TRUE;
    reader->rodata.data = data;
    reader->rodata.size = size;
    return (avifIO *)reader;
}

// --------------------------------------------------------------------------------------
// avifIOFileReader

typedef struct avifIOFileReader
{
    avifIO io; // this must be the first member for easy casting to avifIO*
    avifRWData buffer;
    FILE * f;
} avifIOFileReader;

static avifResult avifIOFileReaderRead(struct avifIO * io, uint32_t readFlags, uint64_t offset, size_t size, avifROData * out)
{
    // printf("avifIOFileReaderRead offset %" PRIu64 " size %zu\n", offset, size);

    if (readFlags != 0) {
        // Unsupported readFlags
        return AVIF_RESULT_IO_ERROR;
    }

    avifIOFileReader * reader = (avifIOFileReader *)io;

    // Sanitize/clamp incoming request
    if (offset > reader->io.sizeHint) {
        // The offset is past the EOF.
        return AVIF_RESULT_IO_ERROR;
    }
    uint64_t availableSize = reader->io.sizeHint - offset;
    if (size > availableSize) {
        size = (size_t)availableSize;
    }

    if (size > 0) {
        if (offset > AVIF_OFF_MAX) {
            return AVIF_RESULT_IO_ERROR;
        }
        if (reader->buffer.size < size) {
            AVIF_CHECKRES(avifRWDataRealloc(&reader->buffer, size));
        }
        if (avif_fseeko(reader->f, (avif_off_t)offset, SEEK_SET) != 0) {
            return AVIF_RESULT_IO_ERROR;
        }
        size_t bytesRead = fread(reader->buffer.data, 1, size, reader->f);
        if (size != bytesRead) {
            if (ferror(reader->f)) {
                return AVIF_RESULT_IO_ERROR;
            }
            size = bytesRead;
        }
    }

    out->data = reader->buffer.data;
    out->size = size;
    return AVIF_RESULT_OK;
}

static void avifIOFileReaderDestroy(struct avifIO * io)
{
    avifIOFileReader * reader = (avifIOFileReader *)io;
    fclose(reader->f);
    avifRWDataFree(&reader->buffer);
    avifFree(io);
}

avifIO * avifIOCreateFileReader(const char * filename)
{
    FILE * f = fopen(filename, "rb");
    if (!f) {
        return NULL;
    }

    if (avif_fseeko(f, 0, SEEK_END) != 0) {
        fclose(f);
        return NULL;
    }
    avif_off_t fileSize = avif_ftello(f);
    if (fileSize < 0) {
        fclose(f);
        return NULL;
    }
    if (avif_fseeko(f, 0, SEEK_SET) != 0) {
        fclose(f);
        return NULL;
    }

    avifIOFileReader * reader = (avifIOFileReader *)avifAlloc(sizeof(avifIOFileReader));
    if (!reader) {
        fclose(f);
        return NULL;
    }
    memset(reader, 0, sizeof(avifIOFileReader));
    reader->f = f;
    reader->io.destroy = avifIOFileReaderDestroy;
    reader->io.read = avifIOFileReaderRead;
    reader->io.sizeHint = (uint64_t)fileSize;
    reader->io.persistent = AVIF_FALSE;
    if (avifRWDataRealloc(&reader->buffer, 1024) != AVIF_RESULT_OK) {
        avifFree(reader);
        fclose(f);
        return NULL;
    }
    return (avifIO *)reader;
}
