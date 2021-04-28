// Copyright 2019 Joe Drago. All rights reserved.
// SPDX-License-Identifier: BSD-2-Clause

#include "avif/avif.h"

#include "avifjpeg.h"
#include "avifpng.h"
#include "avifutil.h"
#include "y4m.h"

#include <inttypes.h>
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
    printf("        avifdec --info    input.avif\n");
    printf("Options:\n");
    printf("    -h,--help         : Show syntax help\n");
    printf("    -V,--version      : Show the version number\n");
    printf("    -j,--jobs J       : Number of jobs (worker threads, default: 1)\n");
    printf("    -c,--codec C      : AV1 codec to use (choose from versions list below)\n");
    printf("    -d,--depth D      : Output depth [8,16]. (PNG only; For y4m, depth is retained, and JPEG is always 8bpc)\n");
    printf("    -q,--quality Q    : Output quality [0-100]. (JPEG only, default: %d)\n", DEFAULT_JPEG_QUALITY);
    printf("    -u,--upsampling U : Chroma upsampling (for 420/422). automatic (default), fastest, best, nearest, or bilinear\n");
    printf("    -r,--raw-color    : Output raw RGB values instead of multiplying by alpha when saving to opaque formats\n");
    printf("                        (JPEG only; not applicable to y4m)\n");
    printf("    -i,--info         : Decode all frames and display all image information instead of saving to disk\n");
    printf("    --ignore-icc      : If the input file contains an embedded ICC profile, ignore it (no-op if absent)\n");
    printf("\n");
    avifPrintVersions();
}

static int info(const char * inputFilename)
{
    avifDecoder * decoder = avifDecoderCreate();
    avifResult result = avifDecoderSetIOFile(decoder, inputFilename);
    if (result != AVIF_RESULT_OK) {
        fprintf(stderr, "Cannot open file for read: %s\n", inputFilename);
        avifDecoderDestroy(decoder);
        return 1;
    }
    result = avifDecoderParse(decoder);
    if (result == AVIF_RESULT_OK) {
        printf("Image decoded: %s\n", inputFilename);
        avifContainerDump(decoder);

        printf(" * %" PRIu64 " timescales per second, %2.2f seconds (%" PRIu64 " timescales), %d frame%s\n",
               decoder->timescale,
               decoder->duration,
               decoder->durationInTimescales,
               decoder->imageCount,
               (decoder->imageCount == 1) ? "" : "s");
        printf(" * Frames:\n");

        int frameIndex = 0;
        while (avifDecoderNextImage(decoder) == AVIF_RESULT_OK) {
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
        avifDumpDiagnostics(&decoder->diag);
    }

    avifDecoderDestroy(decoder);
    return 0;
}

int main(int argc, char * argv[])
{
    const char * inputFilename = NULL;
    const char * outputFilename = NULL;
    int requestedDepth = 0;
    int jobs = 1;
    int jpegQuality = DEFAULT_JPEG_QUALITY;
    avifCodecChoice codecChoice = AVIF_CODEC_CHOICE_AUTO;
    avifBool infoOnly = AVIF_FALSE;
    avifChromaUpsampling chromaUpsampling = AVIF_CHROMA_UPSAMPLING_AUTOMATIC;
    avifBool ignoreICC = AVIF_FALSE;
    avifBool rawColor = AVIF_FALSE;

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
        } else if (!strcmp(arg, "-V") || !strcmp(arg, "--version")) {
            avifPrintVersions();
            return 0;
        } else if (!strcmp(arg, "-j") || !strcmp(arg, "--jobs")) {
            NEXTARG();
            jobs = atoi(arg);
            if (jobs < 1) {
                jobs = 1;
            }
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
        } else if (!strcmp(arg, "-u") || !strcmp(arg, "--upsampling")) {
            NEXTARG();
            if (!strcmp(arg, "automatic")) {
                chromaUpsampling = AVIF_CHROMA_UPSAMPLING_AUTOMATIC;
            } else if (!strcmp(arg, "fastest")) {
                chromaUpsampling = AVIF_CHROMA_UPSAMPLING_FASTEST;
            } else if (!strcmp(arg, "best")) {
                chromaUpsampling = AVIF_CHROMA_UPSAMPLING_BEST_QUALITY;
            } else if (!strcmp(arg, "nearest")) {
                chromaUpsampling = AVIF_CHROMA_UPSAMPLING_NEAREST;
            } else if (!strcmp(arg, "bilinear")) {
                chromaUpsampling = AVIF_CHROMA_UPSAMPLING_BILINEAR;
            } else {
                fprintf(stderr, "ERROR: invalid upsampling: %s\n", arg);
                return 1;
            }
        } else if (!strcmp(arg, "-r") || !strcmp(arg, "--raw-color")) {
            rawColor = AVIF_TRUE;
        } else if (!strcmp(arg, "-i") || !strcmp(arg, "--info")) {
            infoOnly = AVIF_TRUE;
        } else if (!strcmp(arg, "--ignore-icc")) {
            ignoreICC = AVIF_TRUE;
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

    if (!inputFilename) {
        syntax();
        return 1;
    }

    if (infoOnly) {
        if (!inputFilename || outputFilename) {
            syntax();
            return 1;
        }
        return info(inputFilename);
    } else {
        if (!inputFilename || !outputFilename) {
            syntax();
            return 1;
        }
    }

    printf("Decoding with AV1 codec '%s' (%d worker thread%s), please wait...\n",
           avifCodecName(codecChoice, AVIF_CODEC_FLAG_CAN_DECODE),
           jobs,
           (jobs == 1) ? "" : "s");

    int returnCode = 0;
    avifImage * avif = avifImageCreateEmpty();
    avifDecoder * decoder = avifDecoderCreate();
    decoder->maxThreads = jobs;
    decoder->codecChoice = codecChoice;
    avifResult decodeResult = avifDecoderReadFile(decoder, avif, inputFilename);
    if (decodeResult == AVIF_RESULT_OK) {
        printf("Image decoded: %s\n", inputFilename);
        printf("Image details:\n");
        avifImageDump(avif, 0, 0);

        if (ignoreICC && (avif->icc.size > 0)) {
            printf("[--ignore-icc] Discarding ICC profile.\n");
            avifImageSetProfileICC(avif, NULL, 0);
        }

        avifAppFileFormat outputFormat = avifGuessFileFormat(outputFilename);
        if (outputFormat == AVIF_APP_FILE_FORMAT_UNKNOWN) {
            fprintf(stderr, "Cannot determine output file extension: %s\n", outputFilename);
            returnCode = 1;
        } else if (outputFormat == AVIF_APP_FILE_FORMAT_Y4M) {
            if (!y4mWrite(outputFilename, avif)) {
                returnCode = 1;
            }
        } else if (outputFormat == AVIF_APP_FILE_FORMAT_JPEG) {
            // Bypass alpha multiply step during conversion
            if (rawColor) {
                avif->alphaPremultiplied = AVIF_TRUE;
            }
            if (!avifJPEGWrite(outputFilename, avif, jpegQuality, chromaUpsampling)) {
                returnCode = 1;
            }
        } else if (outputFormat == AVIF_APP_FILE_FORMAT_PNG) {
            if (!avifPNGWrite(outputFilename, avif, requestedDepth, chromaUpsampling)) {
                returnCode = 1;
            }
        } else {
            fprintf(stderr, "Unrecognized file extension: %s\n", outputFilename);
            returnCode = 1;
        }
    } else {
        printf("ERROR: Failed to decode image: %s\n", avifResultToString(decodeResult));
        avifDumpDiagnostics(&decoder->diag);
        returnCode = 1;
    }
    avifDecoderDestroy(decoder);
    avifImageDestroy(avif);
    return returnCode;
}
