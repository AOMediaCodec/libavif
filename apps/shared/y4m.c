// Copyright 2019 Joe Drago. All rights reserved.
// SPDX-License-Identifier: BSD-2-Clause

// This is a barebones y4m reader/writer for basic libavif testing. It is NOT comprehensive!

#include "y4m.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define Y4M_MAX_LINE_SIZE 2048 // Arbitrary limit. Y4M headers should be much smaller than this

struct y4mFrameIterator
{
    int width;
    int height;
    int depth;
    avifBool hasAlpha;
    avifPixelFormat format;
    avifRange range;

    FILE * inputFile;
    const char * displayFilename;
};

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
    if (!strcmp(formatString, "Cmono")) {
        *format = AVIF_PIXEL_FORMAT_YUV400;
        *depth = 8;
        return AVIF_TRUE;
    }
    if (!strcmp(formatString, "Cmono10")) {
        *format = AVIF_PIXEL_FORMAT_YUV400;
        *depth = 10;
        return AVIF_TRUE;
    }
    if (!strcmp(formatString, "Cmono12")) {
        *format = AVIF_PIXEL_FORMAT_YUV400;
        *depth = 12;
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

static int y4mReadLine(FILE * inputFile, avifRWData * raw, const char * displayFilename)
{
    static const int maxBytes = Y4M_MAX_LINE_SIZE;
    int bytesRead = 0;
    uint8_t * front = raw->data;

    for (;;) {
        if (fread(front, 1, 1, inputFile) != 1) {
            fprintf(stderr, "Failed to read line: %s\n", displayFilename);
            break;
        }

        ++bytesRead;
        if (bytesRead >= maxBytes) {
            break;
        }

        if (*front == '\n') {
            return bytesRead;
        }
        ++front;
    }
    return -1;
}

#define ADVANCE(BYTES)    \
    do {                  \
        p += BYTES;       \
        if (p >= end)     \
            goto cleanup; \
    } while (0)

avifBool y4mRead(avifImage * avif, const char * inputFilename, struct y4mFrameIterator ** iter)
{
    avifBool result = AVIF_FALSE;

    struct y4mFrameIterator frame;
    frame.width = -1;
    frame.height = -1;
    frame.depth = -1;
    frame.hasAlpha = AVIF_FALSE;
    frame.format = AVIF_PIXEL_FORMAT_NONE;
    frame.range = AVIF_RANGE_LIMITED;
    frame.inputFile = NULL;
    frame.displayFilename = inputFilename;

    avifRWData raw = AVIF_DATA_EMPTY;
    avifRWDataRealloc(&raw, Y4M_MAX_LINE_SIZE);

    if (iter && *iter) {
        // Continue reading FRAMEs from this y4m stream
        memcpy(&frame, *iter, sizeof(struct y4mFrameIterator));
    } else {
        // Open a fresh y4m and read its header

        if (inputFilename) {
            frame.inputFile = fopen(inputFilename, "rb");
            if (!frame.inputFile) {
                fprintf(stderr, "Cannot open file for read: %s\n", inputFilename);
                goto cleanup;
            }
        } else {
            frame.inputFile = stdin;
            frame.displayFilename = "(stdin)";
        }

        int headerBytes = y4mReadLine(frame.inputFile, &raw, frame.displayFilename);
        if (headerBytes < 0) {
            fprintf(stderr, "Y4M header too large: %s\n", frame.displayFilename);
            goto cleanup;
        }
        if (headerBytes < 10) {
            fprintf(stderr, "Y4M header too small: %s\n", frame.displayFilename);
            goto cleanup;
        }

        uint8_t * end = raw.data + headerBytes;
        uint8_t * p = raw.data;

        if (memcmp(p, "YUV4MPEG2 ", 10) != 0) {
            fprintf(stderr, "Not a y4m file: %s\n", frame.displayFilename);
            goto cleanup;
        }
        ADVANCE(10); // skip past header

        char tmpBuffer[32];

        while (p != end) {
            switch (*p) {
                case 'W': // width
                    frame.width = atoi((const char *)p + 1);
                    break;
                case 'H': // height
                    frame.height = atoi((const char *)p + 1);
                    break;
                case 'C': // color space
                    if (!getHeaderString(p, end, tmpBuffer, 31)) {
                        fprintf(stderr, "Bad y4m header: %s\n", frame.displayFilename);
                        goto cleanup;
                    }
                    if (!y4mColorSpaceParse(tmpBuffer, &frame.format, &frame.depth, &frame.hasAlpha)) {
                        fprintf(stderr, "Unsupported y4m pixel format: %s\n", frame.displayFilename);
                        goto cleanup;
                    }
                    break;
                case 'X':
                    if (!getHeaderString(p, end, tmpBuffer, 31)) {
                        fprintf(stderr, "Bad y4m header: %s\n", frame.displayFilename);
                        goto cleanup;
                    }
                    if (!strcmp(tmpBuffer, "XCOLORRANGE=FULL")) {
                        frame.range = AVIF_RANGE_FULL;
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
            fprintf(stderr, "Truncated y4m header (no newline): %s\n", frame.displayFilename);
            goto cleanup;
        }
    }

    int frameHeaderBytes = y4mReadLine(frame.inputFile, &raw, frame.displayFilename);
    if (frameHeaderBytes < 0) {
        fprintf(stderr, "Y4M frame header too large: %s\n", frame.displayFilename);
        goto cleanup;
    }
    if (frameHeaderBytes < 6) {
        fprintf(stderr, "Y4M frame header too small: %s\n", frame.displayFilename);
        goto cleanup;
    }
    if (memcmp(raw.data, "FRAME", 5) != 0) {
        fprintf(stderr, "Truncated y4m (no frame): %s\n", frame.displayFilename);
        goto cleanup;
    }

    if ((frame.width < 1) || (frame.height < 1) || ((frame.depth != 8) && (frame.depth != 10) && (frame.depth != 12)) ||
        (frame.format == AVIF_PIXEL_FORMAT_NONE)) {
        fprintf(stderr, "Failed to parse y4m header (not enough information): %s\n", frame.displayFilename);
        goto cleanup;
    }

    avifImageFreePlanes(avif, AVIF_PLANES_YUV | AVIF_PLANES_A);
    avif->width = frame.width;
    avif->height = frame.height;
    avif->depth = frame.depth;
    avif->yuvFormat = frame.format;
    avif->yuvRange = frame.range;
    avifImageAllocatePlanes(avif, AVIF_PLANES_YUV);

    avifPixelFormatInfo info;
    avifGetPixelFormatInfo(avif->yuvFormat, &info);

    uint32_t planeBytes[4];
    planeBytes[0] = avif->yuvRowBytes[0] * avif->height;
    planeBytes[1] = avif->yuvRowBytes[1] * ((avif->height + info.chromaShiftY) >> info.chromaShiftY);
    planeBytes[2] = avif->yuvRowBytes[2] * ((avif->height + info.chromaShiftY) >> info.chromaShiftY);
    if (frame.hasAlpha) {
        planeBytes[3] = avif->alphaRowBytes * avif->height;
    } else {
        planeBytes[3] = 0;
    }

    for (int i = 0; i < 3; ++i) {
        uint32_t bytesRead = (uint32_t)fread(avif->yuvPlanes[i], 1, planeBytes[i], frame.inputFile);
        if (bytesRead != planeBytes[i]) {
            fprintf(stderr, "Failed to read y4m plane (not enough data, wanted %d, got %d): %s\n", planeBytes[i], bytesRead, frame.displayFilename);
            goto cleanup;
        }
    }
    if (frame.hasAlpha) {
        avifImageAllocatePlanes(avif, AVIF_PLANES_A);
        if (fread(avif->alphaPlane, 1, planeBytes[3], frame.inputFile) != planeBytes[3]) {
            fprintf(stderr, "Failed to read y4m plane (not enough data): %s\n", frame.displayFilename);
            goto cleanup;
        }
    }

    result = AVIF_TRUE;
cleanup:
    if (iter) {
        if (*iter) {
            free(*iter);
            *iter = NULL;
        }

        if (result && frame.inputFile) {
            ungetc(fgetc(frame.inputFile), frame.inputFile); // Kick frame.inputFile to force EOF

            if (!feof(frame.inputFile)) {
                // Remember y4m state for next time
                *iter = malloc(sizeof(struct y4mFrameIterator));
                memcpy(*iter, &frame, sizeof(struct y4mFrameIterator));
            }
        }
    }

    if (inputFilename && frame.inputFile && (!iter || !(*iter))) {
        fclose(frame.inputFile);
    }
    avifRWDataFree(&raw);
    return result;
}

avifBool y4mWrite(avifImage * avif, const char * outputFilename)
{
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
                case AVIF_PIXEL_FORMAT_YUV400:
                    y4mHeaderFormat = "Cmono XYSCSS=400";
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
                case AVIF_PIXEL_FORMAT_YUV400:
                    y4mHeaderFormat = "Cmono10 XYSCSS=400";
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
                case AVIF_PIXEL_FORMAT_YUV400:
                    y4mHeaderFormat = "Cmono12 XYSCSS=400";
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
