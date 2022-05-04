// Copyright 2022 Google LLC. All rights reserved.
// SPDX-License-Identifier: BSD-2-Clause

#include "avif/avif.h"
#include "avifincrtest_helpers.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

//------------------------------------------------------------------------------

// Reads the file at path into bytes or returns false.
static avifBool readFile(const char * path, avifRWData * bytes)
{
    FILE * file;
    avifRWDataFree(bytes);
    file = fopen(path, "rb");
    if (!file) {
        return AVIF_FALSE;
    }
    if (fseek(file, 0, SEEK_END)) {
        fclose(file);
        return AVIF_FALSE;
    }
    avifRWDataRealloc(bytes, ftell(file));
    rewind(file);
    if (fread(bytes->data, bytes->size, 1, file) != 1) {
        avifRWDataFree(bytes);
        fclose(file);
        return AVIF_FALSE;
    }
    fclose(file);
    return AVIF_TRUE;
}

//------------------------------------------------------------------------------

// Encodes then decodes a window of width*height pixels at the middle of the image.
// Check that non-incremental and incremental decodings produce the same pixels.
static avifBool encodeDecodeNonIncrementallyAndIncrementally(const avifImage * image,
                                                             uint32_t width,
                                                             uint32_t height,
                                                             avifBool createAlphaIfNone,
                                                             avifBool flatCells,
                                                             avifBool encodedAvifIsPersistent,
                                                             avifBool giveSizeHint,
                                                             avifBool useNthImageApi)
{
    avifBool success = AVIF_FALSE;
    avifRWData encodedAvif = { 0 };
    uint32_t cellWidth, cellHeight;
    if (!encodeRectAsIncremental(image, width, height, createAlphaIfNone, flatCells, &encodedAvif, &cellWidth, &cellHeight)) {
        goto cleanup;
    }
    if (!decodeNonIncrementallyAndIncrementally(&encodedAvif, encodedAvifIsPersistent, giveSizeHint, useNthImageApi, cellHeight)) {
        goto cleanup;
    }
    success = AVIF_TRUE;
cleanup:
    avifRWDataFree(&encodedAvif);
    return success;
}

//------------------------------------------------------------------------------

int main(int argc, char * argv[])
{
    int exitCode = EXIT_FAILURE;
    avifRWData encodedAvif = { NULL, 0 };

    if (argc != 2) {
        printf("ERROR: bad arguments\n");
        printf("Usage: avifincrtest <AVIF>\n");
        goto cleanup;
    }
    const char * const avifFilePath = argv[1];

    if (!readFile(avifFilePath, &encodedAvif)) {
        printf("ERROR: cannot read AVIF: %s\n", avifFilePath);
        goto cleanup;
    }

    // First test: decode the input image incrementally and compare it with a non-incrementally decoded reference.
    avifImage * reference = avifImageCreateEmpty();
    if (!reference || !decodeNonIncrementally(&encodedAvif, reference)) {
        goto cleanup;
    }
    // Cell height is hardcoded because there is no API to extract it from an encoded payload.
    if (!decodeIncrementally(&encodedAvif,
                             /*isPersistent=*/AVIF_TRUE,
                             /*giveSizeHint=*/AVIF_TRUE,
                             /*useNthImageApi=*/AVIF_FALSE,
                             reference,
                             /*cellHeight=*/154)) {
        goto cleanup;
    }

    // Second test: encode a bunch of different dimension combinations and decode them incrementally and non-incrementally.
    // Chroma subsampling requires even dimensions. See ISO 23000-22 section 7.3.11.4.2.
    const uint32_t widths[] = { 1, 64, 66 };
    const uint32_t heights[] = { 1, 64, 66 };
    for (uint32_t w = 0; w < sizeof(widths) / sizeof(widths[0]); ++w) {
        for (uint32_t h = 0; h < sizeof(heights) / sizeof(heights[0]); ++h) {
            // avifEncoderAddImageInternal() only accepts grids of one unique cell, or grids where width and height are both at least 64.
            if ((widths[w] >= 64) != (heights[h] >= 64)) {
                continue;
            }

            for (avifBool createAlpha = AVIF_FALSE; createAlpha <= AVIF_TRUE; ++createAlpha) {
                for (avifBool flatCells = AVIF_FALSE; flatCells <= AVIF_TRUE; ++flatCells) {
                    for (avifBool encodedAvifIsPersistent = AVIF_FALSE; encodedAvifIsPersistent <= AVIF_TRUE; ++encodedAvifIsPersistent) {
                        for (avifBool giveSizeHint = AVIF_FALSE; giveSizeHint <= AVIF_TRUE; ++giveSizeHint) {
                            for (avifBool useNthImageApi = AVIF_FALSE; useNthImageApi <= AVIF_TRUE; ++useNthImageApi) {
                                if (!encodeDecodeNonIncrementallyAndIncrementally(reference,
                                                                                  widths[w],
                                                                                  heights[h],
                                                                                  createAlpha,
                                                                                  flatCells,
                                                                                  encodedAvifIsPersistent,
                                                                                  giveSizeHint,
                                                                                  useNthImageApi)) {
                                    goto cleanup;
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    // Third test: full image.
    for (avifBool flatCells = AVIF_FALSE; flatCells <= AVIF_TRUE; ++flatCells) {
        if (!encodeDecodeNonIncrementallyAndIncrementally(reference,
                                                          reference->width,
                                                          reference->height,
                                                          /*createAlpha=*/AVIF_TRUE,
                                                          flatCells,
                                                          /*encodedAvifIsPersistent=*/AVIF_TRUE,
                                                          /*giveSizeHint=*/AVIF_TRUE,
                                                          /*useNthImageApi=*/AVIF_FALSE)) {
            goto cleanup;
        }
    }

    exitCode = EXIT_SUCCESS;
cleanup:
    avifRWDataFree(&encodedAvif);
    return exitCode;
}
