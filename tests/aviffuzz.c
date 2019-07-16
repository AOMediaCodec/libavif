// Copyright 2019 Joe Drago. All rights reserved.
// SPDX-License-Identifier: BSD-2-Clause

#include "avif/avif.h"

#include <string.h>
#include <stdio.h>

int syntax(void)
{
    printf("Syntax: aviffuzz input.avif\n");
    return 0;
}

int main(int argc, char * argv[])
{
    const char * inputFilename = NULL;
    if (argc < 2) {
        return 0;
    }
    inputFilename = argv[1];

    FILE * inputFile = fopen(inputFilename, "rb");
    if (!inputFile) {
        fprintf(stderr, "Cannot open file for read: %s\n", inputFilename);
        return 1;
    }
    fseek(inputFile, 0, SEEK_END);
    size_t inputFileSize = ftell(inputFile);
    fseek(inputFile, 0, SEEK_SET);

    if (inputFileSize < 1) {
        fprintf(stderr, "File too small: %s\n", inputFilename);
        fclose(inputFile);
        return 1;
    }

    avifRawData raw = AVIF_RAW_DATA_EMPTY;
    avifRawDataRealloc(&raw, inputFileSize);
    if (fread(raw.data, 1, inputFileSize, inputFile) != inputFileSize) {
        fprintf(stderr, "Failed to read %zu bytes: %s\n", inputFileSize, inputFilename);
        fclose(inputFile);
        avifRawDataFree(&raw);
        return 1;
    }

    fclose(inputFile);
    inputFile = NULL;

    avifImage * avif = avifImageCreateEmpty();
    avifDecoder * decoder = avifDecoderCreate();
    avifResult decodeResult = avifDecoderRead(decoder, avif, &raw);
    if (decodeResult == AVIF_RESULT_OK) {
        printf("Image decoded: %s\n", inputFilename);
    } else {
        printf("ERROR: Failed to decode image: %s\n", avifResultToString(decodeResult));
    }
    avifRawDataFree(&raw);
    avifDecoderDestroy(decoder);
    avifImageDestroy(avif);
    return 0;
}
