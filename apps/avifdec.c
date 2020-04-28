// Copyright 2019 Joe Drago. All rights reserved.
// SPDX-License-Identifier: BSD-2-Clause

#include "avif/avif.h"

#include "avifjpeg.h"
#include "avifpng.h"
#include "avifutil.h"
#include "y4m.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define DEFAULT_JPEG_QUALITY 90

#define NEXTARG()                                                     \
    if (((argIndex + 1) == argc) || (argv[argIndex + 1][0] == '-')) { \
        fprintf(stderr, "%s requires an argument.", arg);             \
        return 1;                                                     \
    }                                                                 \
    arg = argv[++argIndex]

static void syntax(void)
{
    printf("Syntax: avifdec [options] input.avif output.[jpg|jpeg|png|y4m]\n");
    printf("Options:\n");
    printf("    -h,--help         : Show syntax help\n");
    printf("    -c,--codec C      : AV1 codec to use (choose from versions list below)\n");
    printf("    -d,--depth D      : Output depth [8,16]. (PNG only; For y4m, depth is retained, and JPEG is always 8bpc)\n");
    printf("    -q,--quality Q    : Output quality [0-100]. (JPEG only, default: %d)\n", DEFAULT_JPEG_QUALITY);
    printf("\n");
    avifPrintVersions();
}

int main(int argc, char * argv[])
{
    const char * inputFilename = NULL;
    const char * outputFilename = NULL;
    int requestedDepth = 0;
    int jpegQuality = DEFAULT_JPEG_QUALITY;
    avifCodecChoice codecChoice = AVIF_CODEC_CHOICE_AUTO;

    if (argc < 2) {
        syntax();
        return 1;
    }

    int argIndex = 1;
    while (argIndex < argc) {
        const char * arg = argv[argIndex];

        if (!strcmp(arg, "-h") || !strcmp(arg, "--help")) {
            syntax();
            return 0;
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
        } else if (!strcmp(arg, "-d") || !strcmp(arg, "--depth")) {
            NEXTARG();
            requestedDepth = atoi(arg);
            if ((requestedDepth != 8) && (requestedDepth != 16)) {
                fprintf(stderr, "ERROR: invalid depth: %s\n", arg);
                return 1;
            }
        } else if (!strcmp(arg, "-q") || !strcmp(arg, "--quality")) {
            NEXTARG();
            jpegQuality = atoi(arg);
            if (jpegQuality < 0) {
                jpegQuality = 0;
            } else if (jpegQuality > 100) {
                jpegQuality = 100;
            }
        } else {
            // Positional argument
            if (!inputFilename) {
                inputFilename = arg;
            } else if (!outputFilename) {
                outputFilename = arg;
            } else {
                fprintf(stderr, "Too many positional arguments: %s\n", arg);
                syntax();
                return 1;
            }
        }

        ++argIndex;
    }

    if (!inputFilename || !outputFilename) {
        syntax();
        return 1;
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

    int returnCode = 0;
    avifImage * avif = avifImageCreateEmpty();
    avifDecoder * decoder = avifDecoderCreate();
    avifResult decodeResult = avifDecoderRead(decoder, avif, (avifROData *)&raw);
    if (decodeResult == AVIF_RESULT_OK) {
        printf("Image decoded: %s\n", inputFilename);
        printf("Image details:\n");
        avifImageDump(avif);

        avifAppFileFormat outputFormat = avifGuessFileFormat(outputFilename);
        if (outputFormat == AVIF_APP_FILE_FORMAT_UNKNOWN) {
            fprintf(stderr, "Cannot determine output file extension: %s\n", outputFilename);
            returnCode = 1;
        } else if (outputFormat == AVIF_APP_FILE_FORMAT_Y4M) {
            if (!y4mWrite(avif, outputFilename)) {
                returnCode = 1;
            }
        } else if (outputFormat == AVIF_APP_FILE_FORMAT_JPEG) {
            if (!avifJPEGWrite(avif, outputFilename, jpegQuality)) {
                returnCode = 1;
            }
        } else if (outputFormat == AVIF_APP_FILE_FORMAT_PNG) {
            if (!avifPNGWrite(avif, outputFilename, requestedDepth)) {
                returnCode = 1;
            }
        } else {
            fprintf(stderr, "Unrecognized file extension: %s\n", outputFilename);
            returnCode = 1;
        }
    } else {
        printf("ERROR: Failed to decode image: %s\n", avifResultToString(decodeResult));
        returnCode = 1;
    }
    avifRWDataFree(&raw);
    avifDecoderDestroy(decoder);
    avifImageDestroy(avif);
    return returnCode;
}
