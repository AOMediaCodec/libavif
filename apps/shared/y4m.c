// Copyright 2019 Joe Drago. All rights reserved.
// SPDX-License-Identifier: BSD-2-Clause

// This is a barebones y4m reader/writer for basic libavif testing. It is NOT comprehensive!

#include "y4m.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static avifBool y4mColorSpaceParse(const char * formatString, avifPixelFormat * format, int * depth, avifBool * hasAlpha)
{
    *hasAlpha = AVIF_FALSE;

    if (!strcmp(formatString, "C420jpeg")) {
        *format = AVIF_PIXEL_FORMAT_YUV420;
        *depth = 8;
        return AVIF_TRUE;
    }
    if (!strcmp(formatString, "C444p10")) {
        *format = AVIF_PIXEL_FORMAT_YUV444;
        *depth = 10;
        return AVIF_TRUE;
    }
    if (!strcmp(formatString, "C422p10")) {
        *format = AVIF_PIXEL_FORMAT_YUV422;
        *depth = 10;
        return AVIF_TRUE;
    }
    if (!strcmp(formatString, "C420p10")) {
        *format = AVIF_PIXEL_FORMAT_YUV420;
        *depth = 10;
        return AVIF_TRUE;
    }
    if (!strcmp(formatString, "C422p10")) {
        *format = AVIF_PIXEL_FORMAT_YUV422;
        *depth = 10;
        return AVIF_TRUE;
    }
    if (!strcmp(formatString, "C444p12")) {
        *format = AVIF_PIXEL_FORMAT_YUV444;
        *depth = 12;
        return AVIF_TRUE;
    }
    if (!strcmp(formatString, "C422p12")) {
        *format = AVIF_PIXEL_FORMAT_YUV422;
        *depth = 12;
        return AVIF_TRUE;
    }
    if (!strcmp(formatString, "C420p12")) {
        *format = AVIF_PIXEL_FORMAT_YUV420;
        *depth = 12;
        return AVIF_TRUE;
    }
    if (!strcmp(formatString, "C422p12")) {
        *format = AVIF_PIXEL_FORMAT_YUV422;
        *depth = 12;
        return AVIF_TRUE;
    }
    if (!strcmp(formatString, "C444")) {
        *format = AVIF_PIXEL_FORMAT_YUV444;
        *depth = 8;
        return AVIF_TRUE;
    }
    if (!strcmp(formatString, "C444alpha")) {
        *format = AVIF_PIXEL_FORMAT_YUV444;
        *depth = 8;
        *hasAlpha = AVIF_TRUE;
        return AVIF_TRUE;
    }
    if (!strcmp(formatString, "C422")) {
        *format = AVIF_PIXEL_FORMAT_YUV422;
        *depth = 8;
        return AVIF_TRUE;
    }
    if (!strcmp(formatString, "C420")) {
        *format = AVIF_PIXEL_FORMAT_YUV420;
        *depth = 8;
        return AVIF_TRUE;
    }
    return AVIF_FALSE;
}

static avifBool getHeaderString(uint8_t * p, uint8_t * end, char * out, size_t maxChars)
{
    uint8_t * headerEnd = p;
    while ((*headerEnd != ' ') && (*headerEnd != '\n')) {
        if (headerEnd >= end) {
            return AVIF_FALSE;
        }
        ++headerEnd;
    }
    size_t formatLen = headerEnd - p;
    if (formatLen > maxChars) {
        return AVIF_FALSE;
    }

    strncpy(out, (const char *)p, formatLen);
    out[formatLen] = 0;
    return AVIF_TRUE;
}

#define ADVANCE(BYTES)    \
    do {                  \
        p += BYTES;       \
        if (p >= end)     \
            goto cleanup; \
    } while (0)

avifBool y4mRead(avifImage * avif, const char * inputFilename)
{
    FILE * inputFile = fopen(inputFilename, "rb");
    if (!inputFile) {
        fprintf(stderr, "Cannot open file for read: %s\n", inputFilename);
        return AVIF_FALSE;
    }
    fseek(inputFile, 0, SEEK_END);
    size_t inputFileSize = ftell(inputFile);
    fseek(inputFile, 0, SEEK_SET);

    if (inputFileSize < 10) {
        fprintf(stderr, "File too small: %s\n", inputFilename);
        fclose(inputFile);
        return AVIF_FALSE;
    }

    avifRWData raw = AVIF_DATA_EMPTY;
    avifRWDataRealloc(&raw, inputFileSize);
    if (fread(raw.data, 1, inputFileSize, inputFile) != inputFileSize) {
        fprintf(stderr, "Failed to read %zu bytes: %s\n", inputFileSize, inputFilename);
        fclose(inputFile);
        avifRWDataFree(&raw);
        return AVIF_FALSE;
    }

    avifBool result = AVIF_FALSE;

    fclose(inputFile);
    inputFile = NULL;

    uint8_t * end = raw.data + raw.size;
    uint8_t * p = raw.data;

    if (memcmp(p, "YUV4MPEG2 ", 10) != 0) {
        fprintf(stderr, "Not a y4m file: %s\n", inputFilename);
        avifRWDataFree(&raw);
        return AVIF_FALSE;
    }
    ADVANCE(10); // skip past header

    char tmpBuffer[32];

    int width = -1;
    int height = -1;
    int depth = -1;
    avifBool hasAlpha = AVIF_FALSE;
    avifPixelFormat format = AVIF_PIXEL_FORMAT_NONE;
    avifRange range = AVIF_RANGE_LIMITED;
    while (p != end) {
        switch (*p) {
            case 'W': // width
                width = atoi((const char *)p + 1);
                break;
            case 'H': // height
                height = atoi((const char *)p + 1);
                break;
            case 'C': // color space
                if (!getHeaderString(p, end, tmpBuffer, 31)) {
                    fprintf(stderr, "Bad y4m header: %s\n", inputFilename);
                    goto cleanup;
                }
                if (!y4mColorSpaceParse(tmpBuffer, &format, &depth, &hasAlpha)) {
                    fprintf(stderr, "Unsupported y4m pixel format: %s\n", inputFilename);
                    goto cleanup;
                }
                break;
            case 'X':
                if (!getHeaderString(p, end, tmpBuffer, 31)) {
                    fprintf(stderr, "Bad y4m header: %s\n", inputFilename);
                    goto cleanup;
                }
                if (!strcmp(tmpBuffer, "XCOLORRANGE=FULL")) {
                    range = AVIF_RANGE_FULL;
                }
                break;
            default:
                break;
        }

        // Advance past header section
        while ((*p != '\n') && (*p != ' ')) {
            ADVANCE(1);
        }
        if (*p == '\n') {
            // Done with y4m header
            break;
        }

        ADVANCE(1);
    }

    if (*p != '\n') {
        fprintf(stderr, "Truncated y4m header (no newline): %s\n", inputFilename);
        goto cleanup;
    }
    ADVANCE(1); // advance past newline

    size_t remainingBytes = end - p;
    if (remainingBytes < 6) {
        fprintf(stderr, "Truncated y4m (no room for frame header): %s\n", inputFilename);
        goto cleanup;
    }
    if (memcmp(p, "FRAME", 5) != 0) {
        fprintf(stderr, "Truncated y4m (no frame): %s\n", inputFilename);
        goto cleanup;
    }

    // Advance past frame header
    // TODO: Parse frame overrides similarly to header parsing above?
    while (*p != '\n') {
        ADVANCE(1);
    }
    if (*p != '\n') {
        fprintf(stderr, "Invalid y4m frame header: %s\n", inputFilename);
        goto cleanup;
    }
    ADVANCE(1); // advance past newline

    if ((width < 1) || (height < 1) || ((depth != 8) && (depth != 10) && (depth != 12)) || (format == AVIF_PIXEL_FORMAT_NONE)) {
        fprintf(stderr, "Failed to parse y4m header (not enough information): %s\n", inputFilename);
        goto cleanup;
    }

    avifImageFreePlanes(avif, AVIF_PLANES_YUV | AVIF_PLANES_A);
    avif->width = width;
    avif->height = height;
    avif->depth = depth;
    avif->yuvFormat = format;
    avif->yuvRange = range;
    avifImageAllocatePlanes(avif, AVIF_PLANES_YUV);

    avifPixelFormatInfo info;
    avifGetPixelFormatInfo(avif->yuvFormat, &info);

    uint32_t planeBytes[4];
    planeBytes[0] = avif->yuvRowBytes[0] * avif->height;
    planeBytes[1] = avif->yuvRowBytes[1] * (avif->height >> info.chromaShiftY);
    planeBytes[2] = avif->yuvRowBytes[2] * (avif->height >> info.chromaShiftY);
    if (hasAlpha) {
        planeBytes[3] = avif->alphaRowBytes * avif->height;
    } else {
        planeBytes[3] = 0;
    }

    uint32_t bytesNeeded = planeBytes[0] + planeBytes[1] + planeBytes[2] + planeBytes[3];
    remainingBytes = end - p;
    if (bytesNeeded > remainingBytes) {
        fprintf(stderr, "Not enough bytes in y4m for first frame: %s\n", inputFilename);
        goto cleanup;
    }

    for (int i = 0; i < 3; ++i) {
        memcpy(avif->yuvPlanes[i], p, planeBytes[i]);
        p += planeBytes[i];
    }
    if (hasAlpha) {
        avifImageAllocatePlanes(avif, AVIF_PLANES_A);
        memcpy(avif->alphaPlane, p, planeBytes[3]);
    }

    result = AVIF_TRUE;
cleanup:
    avifRWDataFree(&raw);
    return result;
}

avifBool y4mWrite(avifImage * avif, const char * outputFilename)
{
    avifBool swapUV = AVIF_FALSE;
    avifBool hasAlpha = (avif->alphaPlane != NULL) && (avif->alphaRowBytes > 0);
    avifBool writeAlpha = AVIF_FALSE;
    char * y4mHeaderFormat = NULL;

    if (hasAlpha && ((avif->depth != 8) || (avif->yuvFormat != AVIF_PIXEL_FORMAT_YUV444))) {
        fprintf(stderr, "WARNING: writing alpha is currently only supported in 8bpc YUV444, ignoring alpha channel: %s\n", outputFilename);
    }

    switch (avif->depth) {
        case 8:
            switch (avif->yuvFormat) {
                case AVIF_PIXEL_FORMAT_YUV444:
                    if (hasAlpha) {
                        y4mHeaderFormat = "C444alpha XYSCSS=444";
                        writeAlpha = AVIF_TRUE;
                    } else {
                        y4mHeaderFormat = "C444 XYSCSS=444";
                    }
                    break;
                case AVIF_PIXEL_FORMAT_YUV422:
                    y4mHeaderFormat = "C422 XYSCSS=422";
                    break;
                case AVIF_PIXEL_FORMAT_YUV420:
                    y4mHeaderFormat = "C420jpeg XYSCSS=420JPEG";
                    break;
                case AVIF_PIXEL_FORMAT_YV12:
                    y4mHeaderFormat = "C420jpeg XYSCSS=420JPEG";
                    swapUV = AVIF_TRUE;
                    break;
                case AVIF_PIXEL_FORMAT_NONE:
                    // will error later; this case is here for warning's sake
                    break;
            }
            break;
        case 10:
            switch (avif->yuvFormat) {
                case AVIF_PIXEL_FORMAT_YUV444:
                    y4mHeaderFormat = "C444p10 XYSCSS=444P10";
                    break;
                case AVIF_PIXEL_FORMAT_YUV422:
                    y4mHeaderFormat = "C422p10 XYSCSS=422P10";
                    break;
                case AVIF_PIXEL_FORMAT_YUV420:
                    y4mHeaderFormat = "C420p10 XYSCSS=420P10";
                    break;
                case AVIF_PIXEL_FORMAT_YV12:
                    y4mHeaderFormat = "C422p10 XYSCSS=422P10";
                    swapUV = AVIF_TRUE;
                    break;
                case AVIF_PIXEL_FORMAT_NONE:
                    // will error later; this case is here for warning's sake
                    break;
            }
            break;
        case 12:
            switch (avif->yuvFormat) {
                case AVIF_PIXEL_FORMAT_YUV444:
                    y4mHeaderFormat = "C444p12 XYSCSS=444P12";
                    break;
                case AVIF_PIXEL_FORMAT_YUV422:
                    y4mHeaderFormat = "C422p12 XYSCSS=422P12";
                    break;
                case AVIF_PIXEL_FORMAT_YUV420:
                    y4mHeaderFormat = "C420p12 XYSCSS=420P12";
                    break;
                case AVIF_PIXEL_FORMAT_YV12:
                    y4mHeaderFormat = "C422p12 XYSCSS=422P12";
                    swapUV = AVIF_TRUE;
                    break;
                case AVIF_PIXEL_FORMAT_NONE:
                    // will error later; this case is here for warning's sake
                    break;
            }
            break;
        default:
            fprintf(stderr, "ERROR: y4mWrite unsupported depth: %d\n", avif->depth);
            return AVIF_FALSE;
    }

    if (y4mHeaderFormat == NULL) {
        fprintf(stderr, "ERROR: unsupported format\n");
        return AVIF_FALSE;
    }

    const char * rangeString = "XCOLORRANGE=FULL";
    if (avif->yuvRange == AVIF_RANGE_LIMITED) {
        rangeString = "XCOLORRANGE=LIMITED";
    }

    avifPixelFormatInfo info;
    avifGetPixelFormatInfo(avif->yuvFormat, &info);

    FILE * f = fopen(outputFilename, "wb");
    if (!f) {
        fprintf(stderr, "Cannot open file for write: %s\n", outputFilename);
        return AVIF_FALSE;
    }

    avifBool success = AVIF_TRUE;
    if (fprintf(f, "YUV4MPEG2 W%d H%d F25:1 Ip A0:0 %s %s\nFRAME\n", avif->width, avif->height, y4mHeaderFormat, rangeString) < 0) {
        fprintf(stderr, "Cannot write to file: %s\n", outputFilename);
        success = AVIF_FALSE;
        goto cleanup;
    }

    uint8_t * planes[3];
    uint32_t planeBytes[3];
    planes[0] = avif->yuvPlanes[0];
    planes[1] = avif->yuvPlanes[1];
    planes[2] = avif->yuvPlanes[2];
    planeBytes[0] = avif->yuvRowBytes[0] * avif->height;
    planeBytes[1] = avif->yuvRowBytes[1] * (avif->height >> info.chromaShiftY);
    planeBytes[2] = avif->yuvRowBytes[2] * (avif->height >> info.chromaShiftY);
    if (swapUV) {
        uint8_t * tmpPtr;
        uint32_t tmp;
        tmpPtr = planes[1];
        tmp = planeBytes[1];
        planes[1] = planes[2];
        planeBytes[1] = planeBytes[2];
        planes[2] = tmpPtr;
        planeBytes[2] = tmp;
    }

    for (int i = 0; i < 3; ++i) {
        if (fwrite(planes[i], 1, planeBytes[i], f) != planeBytes[i]) {
            fprintf(stderr, "Failed to write %" PRIu32 " bytes: %s\n", planeBytes[i], outputFilename);
            success = AVIF_FALSE;
            goto cleanup;
        }
    }
    if (writeAlpha) {
        uint32_t alphaPlaneBytes = avif->alphaRowBytes * avif->height;
        if (fwrite(avif->alphaPlane, 1, alphaPlaneBytes, f) != alphaPlaneBytes) {
            fprintf(stderr, "Failed to write %" PRIu32 " bytes: %s\n", alphaPlaneBytes, outputFilename);
            success = AVIF_FALSE;
            goto cleanup;
        }
    }

cleanup:
    fclose(f);
    if (success) {
        printf("Wrote Y4M: %s\n", outputFilename);
    }
    return success;
}
