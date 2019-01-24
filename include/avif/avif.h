// Copyright 2019 Joe Drago. All rights reserved.
// SPDX-License-Identifier: BSD-2-Clause

#ifndef AVIF_AVIF_H
#define AVIF_AVIF_H

#include <stddef.h>
#include <stdint.h>

// ---------------------------------------------------------------------------
// Constants

typedef int avifBool;
#define AVIF_TRUE 1
#define AVIF_FALSE 0

#define AVIF_LOSSLESS 0
#define AVIF_BEST_QUALITY 0
#define AVIF_WORST_QUALITY 63

#define AVIF_MAX_PLANES 4

// ---------------------------------------------------------------------------
// Utils

// Yes, clamp macros are nasty. Do not use them.
#define AVIF_CLAMP(x, low, high)  (((x) > (high)) ? (high) : (((x) < (low)) ? (low) : (x)))

float avifRoundf(float v);

uint16_t avifHTONS(uint16_t s);
uint16_t avifNTOHS(uint16_t s);
uint32_t avifHTONL(uint32_t l);
uint32_t avifNTOHL(uint32_t l);

// ---------------------------------------------------------------------------
// Memory management

void * avifAlloc(size_t size);
void avifFree(void * p);

// ---------------------------------------------------------------------------
// avifResult

typedef enum avifResult
{
    AVIF_RESULT_OK = 0,
    AVIF_RESULT_UNKNOWN_ERROR,
    AVIF_RESULT_REFORMAT_FAILED,
    AVIF_RESULT_ENCODE_COLOR_FAILED,
    AVIF_RESULT_ENCODE_ALPHA_FAILED,
    AVIF_RESULT_BMFF_PARSE_FAILED,
    AVIF_RESULT_NO_AV1_ITEMS_FOUND,
    AVIF_RESULT_DECODE_COLOR_FAILED,
    AVIF_RESULT_DECODE_ALPHA_FAILED,
    AVIF_RESULT_COLOR_ALPHA_SIZE_MISMATCH,
    AVIF_RESULT_ISPE_SIZE_MISMATCH,
    AVIF_UNSUPPORTED_PIXEL_FORMAT
} avifResult;

// ---------------------------------------------------------------------------
// avifRawData: Generic raw memory storage

// Note: you can use this struct directly (without the functions) if you're
// passing data into avif*() functions, but you should use avifRawDataFree()
// if any avif*() function populates one of these.

typedef struct avifRawData
{
    uint8_t * data;
    size_t size;
} avifRawData;

// Initialize avifRawData on the stack with this
#define AVIF_RAW_DATA_EMPTY { NULL, 0 }

void avifRawDataRealloc(avifRawData * raw, size_t newSize);
void avifRawDataSet(avifRawData * raw, const uint8_t * data, size_t len);
void avifRawDataConcat(avifRawData * dst, avifRawData ** srcs, int srcsCount);
void avifRawDataFree(avifRawData * raw);

// ---------------------------------------------------------------------------
// avifPixelFormat

typedef enum avifPixelFormat
{
    // No pixels are present
    AVIF_PIXEL_FORMAT_NONE = 0,

    // R:0, G:1, B:2, A:3
    AVIF_PIXEL_FORMAT_RGBA,

    // Y:0, U:1, V:2, A:3, full size chroma
    AVIF_PIXEL_FORMAT_YUV444,

    // Y:0, U:1, V:2, A:3, half size chroma
    AVIF_PIXEL_FORMAT_YUV420
} avifPixelFormat;

// ---------------------------------------------------------------------------
// avifProfileFormat

typedef enum avifProfileFormat
{
    // No color profile present
    AVIF_PROFILE_FORMAT_NONE = 0,

    // icc represents an ICC profile chunk
    AVIF_PROFILE_FORMAT_ICC
} avifProfileFormat;

// ---------------------------------------------------------------------------
// avifImage

typedef struct avifImage
{
    // Image information
    avifPixelFormat pixelFormat;
    int width;
    int height;
    int depth; // all planes must share this depth
    uint16_t * planes[AVIF_MAX_PLANES];
    uint32_t strides[AVIF_MAX_PLANES];

    // Profile information
    avifProfileFormat profileFormat;
    avifRawData icc;

#if 0
    // Additional data from an encode/decode (useful for verbose logging)
    struct Stats
    {
        uint32_t colorPayloadSize;
        uint32_t alphaPayloadSize;
    } stats;
#endif
} avifImage;

avifImage * avifImageCreate();
void avifImageDestroy(avifImage * image);
void avifImageClear(avifImage * image);
void avifImageCreatePixels(avifImage * image, avifPixelFormat pixelFormat, int width, int height, int depth);
avifResult avifImageRead(avifImage * image, avifRawData * input);
avifResult avifImageWrite(avifImage * image, avifRawData * output, int quality); // if OK, output must be freed with avifRawDataFree()
avifResult avifImageReformatPixels(avifImage * srcImage, avifImage * dstImage, avifPixelFormat dstPixelFormat, int dstDepth);

#endif // ifndef AVIF_AVIF_H
