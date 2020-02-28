// Copyright 2019 Joe Drago. All rights reserved.
// SPDX-License-Identifier: BSD-2-Clause

#include "avif/avif.h"

#include "avifutil.h"
#include "y4m.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define NEXTARG()                                                     \
    if (((argIndex + 1) == argc) || (argv[argIndex + 1][0] == '-')) { \
        fprintf(stderr, "%s requires an argument.", arg);             \
        return 1;                                                     \
    }                                                                 \
    arg = argv[++argIndex]

static int syntax(void)
{
    printf("Syntax: avifenc [options] input.y4m output.avif\n");
    printf("Options:\n");
    printf("    -h,--help         : Show syntax help\n");
    printf("    -j,--jobs J       : Number of jobs (worker threads, default: 1)\n");
    printf("    -n,--nclx P/T/M/R : Set nclx colr box values (4 raw numbers)\n");
    printf("                        P = enum avifNclxColourPrimaries\n");
    printf("                        T = enum avifNclxTransferCharacteristics\n");
    printf("                        M = enum avifNclxMatrixCoefficients\n");
    printf("                        R = avifNclxRangeFlag (any nonzero value becomes AVIF_NCLX_FULL_RANGE)\n");
    printf("    --min Q           : Set min quantizer (%d-%d, where %d is lossless)\n",
           AVIF_QUANTIZER_BEST_QUALITY,
           AVIF_QUANTIZER_WORST_QUALITY,
           AVIF_QUANTIZER_LOSSLESS);
    printf("    --max Q           : Set max quantizer (%d-%d, where %d is lossless)\n",
           AVIF_QUANTIZER_BEST_QUALITY,
           AVIF_QUANTIZER_WORST_QUALITY,
           AVIF_QUANTIZER_LOSSLESS);
    printf("    -s,--speed S      : Encoder speed (%d-%d, slowest to fastest)\n", AVIF_SPEED_SLOWEST, AVIF_SPEED_FASTEST);
    printf("    -c,--codec C      : AV1 codec to use (choose from versions list below)\n");
    printf("\n");
    avifPrintVersions();
    return 0;
}

// This is *very* arbitrary, I just want to set people's expectations a bit
static const char * quantizerString(int quantizer)
{
    if (quantizer == 0) {
        return "Lossless";
    }
    if (quantizer <= 12) {
        return "High";
    }
    if (quantizer <= 32) {
        return "Medium";
    }
    if (quantizer == AVIF_QUANTIZER_WORST_QUALITY) {
        return "Worst";
    }
    return "Low";
}

static avifBool parseNCLX(avifNclxColorProfile * nclx, const char * arg)
{
    char buffer[128];
    strncpy(buffer, arg, 127);
    buffer[127] = 0;

    int values[4];
    int index = 0;
    char * token = strtok(buffer, "/");
    while (token != NULL) {
        values[index] = atoi(token);
        ++index;
        if (index >= 4) {
            break;
        }

        token = strtok(NULL, "/");
    }

    if (index == 4) {
        nclx->colourPrimaries = (uint16_t)values[0];
        nclx->transferCharacteristics = (uint16_t)values[1];
        nclx->matrixCoefficients = (uint16_t)values[2];
        nclx->fullRangeFlag = values[3] ? AVIF_NCLX_FULL_RANGE : AVIF_NCLX_LIMITED_RANGE;
        return AVIF_TRUE;
    }
    return AVIF_FALSE;
}

int main(int argc, char * argv[])
{
    const char * inputFilename = NULL;
    const char * outputFilename = NULL;

    if (argc < 2) {
        return syntax();
    }

    int jobs = 1;
    int minQuantizer = AVIF_QUANTIZER_BEST_QUALITY;
    int maxQuantizer = AVIF_QUANTIZER_BEST_QUALITY;
    int speed = AVIF_SPEED_DEFAULT;
    avifCodecChoice codecChoice = AVIF_CODEC_CHOICE_AUTO;
    avifBool nclxSet = AVIF_FALSE;
    avifEncoder * encoder = NULL;

    avifNclxColorProfile nclx;
    memset(&nclx, 0, sizeof(avifNclxColorProfile));

    int argIndex = 1;
    while (argIndex < argc) {
        const char * arg = argv[argIndex];

        if (!strcmp(arg, "-h") || !strcmp(arg, "--help")) {
            return syntax();
        } else if (!strcmp(arg, "-j") || !strcmp(arg, "--jobs")) {
            NEXTARG();
            jobs = atoi(arg);
            if (jobs < 1) {
                jobs = 1;
            }
        } else if (!strcmp(arg, "--min")) {
            NEXTARG();
            minQuantizer = atoi(arg);
            if (minQuantizer < AVIF_QUANTIZER_BEST_QUALITY) {
                minQuantizer = AVIF_QUANTIZER_BEST_QUALITY;
            }
            if (minQuantizer > AVIF_QUANTIZER_WORST_QUALITY) {
                minQuantizer = AVIF_QUANTIZER_WORST_QUALITY;
            }
        } else if (!strcmp(arg, "--max")) {
            NEXTARG();
            maxQuantizer = atoi(arg);
            if (maxQuantizer < AVIF_QUANTIZER_BEST_QUALITY) {
                maxQuantizer = AVIF_QUANTIZER_BEST_QUALITY;
            }
            if (maxQuantizer > AVIF_QUANTIZER_WORST_QUALITY) {
                maxQuantizer = AVIF_QUANTIZER_WORST_QUALITY;
            }
        } else if (!strcmp(arg, "-n") || !strcmp(arg, "--nclx")) {
            NEXTARG();
            if (!parseNCLX(&nclx, arg)) {
                return 1;
            }
            nclxSet = AVIF_TRUE;
        } else if (!strcmp(arg, "-s") || !strcmp(arg, "--speed")) {
            NEXTARG();
            speed = atoi(arg);
            if (speed > AVIF_SPEED_FASTEST) {
                speed = AVIF_SPEED_FASTEST;
            }
            if (speed < AVIF_SPEED_SLOWEST) {
                speed = AVIF_SPEED_SLOWEST;
            }
        } else if (!strcmp(arg, "-c") || !strcmp(arg, "--codec")) {
            NEXTARG();
            codecChoice = avifCodecChoiceFromName(arg);
            if (codecChoice == AVIF_CODEC_CHOICE_AUTO) {
                fprintf(stderr, "ERROR: Unrecognized codec: %s\n", arg);
                return 1;
            } else {
                const char * codecName = avifCodecName(codecChoice, AVIF_CODEC_FLAG_CAN_ENCODE);
                if (codecName == NULL) {
                    fprintf(stderr, "ERROR: AV1 Codec cannot encode: %s\n", arg);
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

    int returnCode = 0;
    avifImage * avif = avifImageCreateEmpty();
    avifRWData raw = AVIF_DATA_EMPTY;

    if (!y4mRead(avif, inputFilename)) {
        returnCode = 1;
        goto cleanup;
    }
    printf("Successfully loaded: %s\n", inputFilename);

    if (nclxSet) {
        avif->profileFormat = AVIF_PROFILE_FORMAT_NCLX;
        memcpy(&avif->nclx, &nclx, sizeof(nclx));
    }

    printf("AVIF to be written:\n");
    avifImageDump(avif);

    printf("Encoding with AV1 codec '%s', quantizer range [%d (%s) <-> %d (%s)], %d worker thread(s), please wait...\n",
           avifCodecName(codecChoice, AVIF_CODEC_FLAG_CAN_ENCODE),
           minQuantizer,
           quantizerString(minQuantizer),
           maxQuantizer,
           quantizerString(maxQuantizer),
           jobs);
    encoder = avifEncoderCreate();
    encoder->maxThreads = jobs;
    encoder->minQuantizer = minQuantizer;
    encoder->maxQuantizer = maxQuantizer;
    encoder->codecChoice = codecChoice;
    encoder->speed = speed;
    avifResult encodeResult = avifEncoderWrite(encoder, avif, &raw);
    if (encodeResult != AVIF_RESULT_OK) {
        fprintf(stderr, "ERROR: Failed to encode image: %s\n", avifResultToString(encodeResult));
        goto cleanup;
    }

    printf("Encoded successfully.\n");
    printf(" * ColorOBU size: %zu bytes\n", encoder->ioStats.colorOBUSize);
    FILE * f = fopen(outputFilename, "wb");
    if (!f) {
        fprintf(stderr, "ERROR: Failed to open file for write: %s\n", outputFilename);
        goto cleanup;
    }
    fwrite(raw.data, 1, raw.size, f);
    fclose(f);
    printf("Wrote: %s\n", outputFilename);

cleanup:
    if (encoder) {
        avifEncoderDestroy(encoder);
    }
    avifImageDestroy(avif);
    avifRWDataFree(&raw);
    return returnCode;
}
