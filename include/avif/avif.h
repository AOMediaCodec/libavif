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

#define AVIF_PLANE_COUNT_RGB 3
#define AVIF_PLANE_COUNT_YUV 3

enum avifPlanesFlags
{
    AVIF_PLANES_RGB = (1 << 0),
    AVIF_PLANES_YUV = (1 << 1),
    AVIF_PLANES_A   = (1 << 2),

    AVIF_PLANES_ALL = 0xff
};

enum avifChannelIndex
{
    // rgbPlanes
    AVIF_CHAN_R = 0,
    AVIF_CHAN_G = 1,
    AVIF_CHAN_B = 2,

    // yuvPlanes - These are always correct, even if UV is flipped when encoded (YV12)
    AVIF_CHAN_Y = 0,
    AVIF_CHAN_U = 1,
    AVIF_CHAN_V = 2
};

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
    AVIF_RESULT_INVALID_FTYP,
    AVIF_RESULT_NO_CONTENT,
    AVIF_RESULT_NO_YUV_FORMAT_SELECTED,
    AVIF_RESULT_REFORMAT_FAILED,
    AVIF_RESULT_UNSUPPORTED_DEPTH,
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

    AVIF_PIXEL_FORMAT_YUV444,
    AVIF_PIXEL_FORMAT_YUV422,
    AVIF_PIXEL_FORMAT_YUV420,
    AVIF_PIXEL_FORMAT_YV12
} avifPixelFormat;

typedef struct avifPixelFormatInfo
{
    int chromaShiftX;
    int chromaShiftY;
    int aomIndexU; // maps U plane to AOM-side plane index
    int aomIndexV; // maps V plane to AOM-side plane index
} avifPixelFormatInfo;

void avifGetPixelFormatInfo(avifPixelFormat format, avifPixelFormatInfo * info);

// ---------------------------------------------------------------------------
// avifRange

typedef enum avifRange
{
    AVIF_RANGE_LIMITED = 0,
    AVIF_RANGE_FULL,
} avifRange;

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
    int width;
    int height;
    int depth; // all planes (RGB/YUV/A) must share this depth; if depth>8, all planes are uint16_t internally

    uint8_t * rgbPlanes[AVIF_PLANE_COUNT_RGB];
    uint32_t rgbRowBytes[AVIF_PLANE_COUNT_RGB];

    avifPixelFormat yuvFormat;
    avifRange yuvRange;
    uint8_t * yuvPlanes[AVIF_PLANE_COUNT_YUV];
    uint32_t yuvRowBytes[AVIF_PLANE_COUNT_YUV];

    uint8_t * alphaPlane;
    uint32_t alphaRowBytes;

    // Profile information
    avifProfileFormat profileFormat;
    avifRawData icc;
} avifImage;

avifImage * avifImageCreate(int width, int height, int depth, avifPixelFormat yuvFormat);
avifImage * avifImageCreateEmpty(void); // helper for making an image to decode into
void avifImageDestroy(avifImage * image);

void avifImageSetProfileICC(avifImage * image, uint8_t * icc, size_t iccSize);

void avifImageAllocatePlanes(avifImage * image, uint32_t planes); // Ignores any pre-existing planes
void avifImageFreePlanes(avifImage * image, uint32_t planes);     // Ignores already-freed planes
avifResult avifImageRead(avifImage * image, avifRawData * input);
avifResult avifImageWrite(avifImage * image, avifRawData * output, int quality); // if OK, output must be freed with avifRawDataFree()

// Used by avifImageRead/avifImageWrite
avifResult avifImageRGBToYUV(avifImage * image);
avifResult avifImageYUVToRGB(avifImage * image);

// Helpers
avifBool avifImageUsesU16(avifImage * image);

#endif // ifndef AVIF_AVIF_H
