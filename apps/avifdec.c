// Copyright 2019 Joe Drago. All rights reserved.
// SPDX-License-Identifier: BSD-2-Clause

#include "avif/avif.h"

#include "avifutil.h"
#include "y4m.h"

#include <stdio.h>
#include <string.h>

#define NEXTARG()                                                     \
    if (((argIndex + 1) == argc) || (argv[argIndex + 1][0] == '-')) { \
        fprintf(stderr, "%s requires an argument.", arg);             \
        return 1;                                                     \
    }                                                                 \
    arg = argv[++argIndex]

static int syntax(void)
{
    printf("Syntax: avifdec [options] input.avif output.y4m\n");
    printf("Options:\n");
    printf("    -h,--help         : Show syntax help\n");
    printf("    -c,--codec C      : AV1 codec to use (choose from versions list below)\n");
    printf("\n");
    avifPrintVersions();
    return 0;
}

int main(int argc, char * argv[])
{
    const char * inputFilename = NULL;
    const char * outputFilename = NULL;
    avifCodecChoice codecChoice = AVIF_CODEC_CHOICE_AUTO;

    if (argc < 2) {
        return syntax();
    }

    int argIndex = 1;
    while (argIndex < argc) {
        const char * arg = argv[argIndex];

        if (!strcmp(arg, "-h") || !strcmp(arg, "--help")) {
            return syntax();
        } else if (!strcmp(arg, "-c") || !strcmp(arg, "--codec")) {
            NEXTARG();
            codecChoice = avifCodecChoiceFromName(arg);
            if (codecChoice == AVIF_CODEC_CHOICE_AUTO) {
                fprintf(stderr, "ERROR: Unrecognized codec: %s\n", arg);
                return 1;
            } else {
                const char * codecName = avifCodecName(codecChoice, AVIF_CODEC_FLAG_CAN_DECODE);
                if (codecName == NULL) {
                    fprintf(stderr, "ERROR: AV1 Codec cannot decode: %s\n", arg);
                    return 1;
                }
            }
        } else {
            // Positional argument
            if (!inputFilename) {
                inputFilename = arg;
            } else if (!outputFilename) {
                outputFilename = arg;
            } else {
                fprintf(stderr, "Too many positional arguments: %s\n", arg);
                return 1;
            }
        }

        ++argIndex;
    }

    if (!inputFilename || !outputFilename) {
        return syntax();
    }

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

    printf("Decoding with AV1 codec '%s', please wait...\n", avifCodecName(codecChoice, AVIF_CODEC_FLAG_CAN_DECODE));

    avifImage * avif = avifImageCreateEmpty();
    avifDecoder * decoder = avifDecoderCreate();
    avifResult decodeResult = avifDecoderRead(decoder, avif, (avifROData *)&raw);
    if (decodeResult == AVIF_RESULT_OK) {
        printf("Image decoded: %s\n", inputFilename);
        printf("Image details:\n");
        avifImageDump(avif);
        y4mWrite(avif, outputFilename);
    } else {
        printf("ERROR: Failed to decode image: %s\n", avifResultToString(decodeResult));
    }
    avifRWDataFree(&raw);
    avifDecoderDestroy(decoder);
    avifImageDestroy(avif);
    return 0;
}
