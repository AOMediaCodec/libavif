// Copyright 2019 Joe Drago. All rights reserved.
// SPDX-License-Identifier: BSD-2-Clause

#include "avif/avif.h"

#include "avifutil.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

static int syntax(void)
{
    printf("Syntax: avifdump input.avif\n");
    return 0;
}

int main(int argc, char * argv[])
{
    const char * inputFilename = NULL;
    if (argc != 2) {
        syntax();
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

    avifRWData raw = AVIF_DATA_EMPTY;
    avifRWDataRealloc(&raw, inputFileSize);
    if (fread(raw.data, 1, inputFileSize, inputFile) != inputFileSize) {
        fprintf(stderr, "Failed to read %zu bytes: %s\n", inputFileSize, inputFilename);
        fclose(inputFile);
        avifRWDataFree(&raw);
        return 1;
    }

    fclose(inputFile);
    inputFile = NULL;

    avifDecoder * decoder = avifDecoderCreate();
    avifResult result = avifDecoderParse(decoder, (avifROData *)&raw);
    if (result == AVIF_RESULT_OK) {
        printf("Image decoded: %s\n", inputFilename);

        int frameIndex = 0;
        avifBool firstImage = AVIF_TRUE;
        while (avifDecoderNextImage(decoder) == AVIF_RESULT_OK) {
            if (firstImage) {
                firstImage = AVIF_FALSE;
                avifImageDump(decoder->image);

                printf(" * %" PRIu64 " timescales per second, %2.2f seconds (%" PRIu64 " timescales), %d frame%s\n",
                       decoder->timescale,
                       decoder->duration,
                       decoder->durationInTimescales,
                       decoder->imageCount,
                       (decoder->imageCount == 1) ? "" : "s");
                printf(" * Frames:\n");
            }

            printf("   * Decoded frame [%d] [pts %2.2f (%" PRIu64 " timescales)] [duration %2.2f (%" PRIu64 " timescales)]\n",
                   frameIndex,
                   decoder->imageTiming.pts,
                   decoder->imageTiming.ptsInTimescales,
                   decoder->imageTiming.duration,
                   decoder->imageTiming.durationInTimescales);
            ++frameIndex;
        }
    } else {
        printf("ERROR: Failed to decode image: %s\n", avifResultToString(result));
    }

    avifRWDataFree(&raw);
    avifDecoderDestroy(decoder);
    return 0;
}
