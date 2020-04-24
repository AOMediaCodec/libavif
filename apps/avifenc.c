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

#define NEXTARG()                                                     \
    if (((argIndex + 1) == argc) || (argv[argIndex + 1][0] == '-')) { \
        fprintf(stderr, "%s requires an argument.", arg);             \
        return 1;                                                     \
    }                                                                 \
    arg = argv[++argIndex]

static void syntax(void)
{
    printf("Syntax: avifenc [options] input.[jpg|jpeg|png|y4m] output.avif\n");
    printf("Options:\n");
    printf("    -h,--help                         : Show syntax help\n");
    printf("    -j,--jobs J                       : Number of jobs (worker threads, default: 1)\n");
    printf("    -d,--depth D                      : Output depth [8,10,12]. (JPEG/PNG only; For y4m, depth is retained)\n");
    printf("    -y,--yuv FORMAT                   : Output format [default=444, 422, 420]. (JPEG/PNG only; For y4m, format is retained)\n");
    printf("    -n,--nclx P/T/M                   : Set nclx colr box values (3 raw numbers, use -r to set range flag)\n");
    printf("                                        P = enum avifNclxColourPrimaries\n");
    printf("                                        T = enum avifNclxTransferCharacteristics\n");
    printf("                                        M = enum avifNclxMatrixCoefficients\n");
    printf("    -r,--range RANGE                  : YUV range [limited or l, full or f]. (JPEG/PNG only, default: full; For y4m, range is retained)\n");
    printf("    --min Q                           : Set min quantizer for color (%d-%d, where %d is lossless)\n",
           AVIF_QUANTIZER_BEST_QUALITY,
           AVIF_QUANTIZER_WORST_QUALITY,
           AVIF_QUANTIZER_LOSSLESS);
    printf("    --max Q                           : Set max quantizer for color (%d-%d, where %d is lossless)\n",
           AVIF_QUANTIZER_BEST_QUALITY,
           AVIF_QUANTIZER_WORST_QUALITY,
           AVIF_QUANTIZER_LOSSLESS);
    printf("    --minalpha Q                      : Set min quantizer for alpha (%d-%d, where %d is lossless)\n",
           AVIF_QUANTIZER_BEST_QUALITY,
           AVIF_QUANTIZER_WORST_QUALITY,
           AVIF_QUANTIZER_LOSSLESS);
    printf("    --maxalpha Q                      : Set max quantizer for alpha (%d-%d, where %d is lossless)\n",
           AVIF_QUANTIZER_BEST_QUALITY,
           AVIF_QUANTIZER_WORST_QUALITY,
           AVIF_QUANTIZER_LOSSLESS);
    printf("    -s,--speed S                      : Encoder speed (%d-%d, slowest-fastest, 'default' for codec internal defaults. default speed: 8)\n",
           AVIF_SPEED_SLOWEST,
           AVIF_SPEED_FASTEST);
    printf("    -c,--codec C                      : AV1 codec to use (choose from versions list below)\n");
    printf("    --pasp H,V                        : Add pasp property (aspect ratio). H=horizontal spacing, V=vertical spacing\n");
    printf("    --clap WN,WD,HN,HD,HON,HOD,VON,VOD: Add clap property (clean aperture). Width, Height, HOffset, VOffset (in num/denom pairs)\n");
    printf("    --irot ANGLE                      : Add irot property (rotation). [0-3], makes (90 * ANGLE) degree rotation anti-clockwise\n");
    printf("    --imir AXIS                       : Add imir property (mirroring). 0=vertical, 1=horizontal\n");
    printf("\n");
    avifPrintVersions();
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

    int values[3];
    int index = 0;
    char * token = strtok(buffer, "/");
    while (token != NULL) {
        values[index] = atoi(token);
        ++index;
        if (index >= 3) {
            break;
        }

        token = strtok(NULL, "/");
    }

    if (index == 3) {
        nclx->colourPrimaries = (uint16_t)values[0];
        nclx->transferCharacteristics = (uint16_t)values[1];
        nclx->matrixCoefficients = (uint16_t)values[2];
        nclx->range = AVIF_RANGE_FULL; // This will be set later
        return AVIF_TRUE;
    }
    return AVIF_FALSE;
}

// Returns the count of uint32_t (up to 8)
static int parseU32List(uint32_t output[8], const char * arg)
{
    char buffer[128];
    strncpy(buffer, arg, 127);
    buffer[127] = 0;

    int index = 0;
    char * token = strtok(buffer, ",");
    while (token != NULL) {
        output[index] = (uint32_t)atoi(token);
        ++index;
        if (index >= 8) {
            break;
        }

        token = strtok(NULL, ",");
    }
    return index;
}

int main(int argc, char * argv[])
{
    const char * inputFilename = NULL;
    const char * outputFilename = NULL;

    if (argc < 2) {
        syntax();
        return 1;
    }

    int jobs = 1;
    avifPixelFormat requestedFormat = AVIF_PIXEL_FORMAT_YUV444;
    int requestedDepth = 0;
    int minQuantizer = AVIF_QUANTIZER_BEST_QUALITY;
    int maxQuantizer = 10; // "High Quality", but not lossless
    int minQuantizerAlpha = AVIF_QUANTIZER_LOSSLESS;
    int maxQuantizerAlpha = AVIF_QUANTIZER_LOSSLESS;
    int speed = 8;
    int paspCount = 0;
    uint32_t paspValues[8]; // only the first two are used
    int clapCount = 0;
    uint32_t clapValues[8];
    uint8_t irotAngle = 0xff; // sentinel value indicating "unused"
    uint8_t imirAxis = 0xff;  // sentinel value indicating "unused"
    avifCodecChoice codecChoice = AVIF_CODEC_CHOICE_AUTO;
    avifRange requestedRange = AVIF_RANGE_FULL;
    avifBool requestedRangeSet = AVIF_FALSE;
    avifBool nclxSet = AVIF_FALSE;
    avifEncoder * encoder = NULL;

    avifNclxColorProfile nclx;
    memset(&nclx, 0, sizeof(avifNclxColorProfile));

    int argIndex = 1;
    while (argIndex < argc) {
        const char * arg = argv[argIndex];

        if (!strcmp(arg, "-h") || !strcmp(arg, "--help")) {
            syntax();
            return 0;
        } else if (!strcmp(arg, "-j") || !strcmp(arg, "--jobs")) {
            NEXTARG();
            jobs = atoi(arg);
            if (jobs < 1) {
                jobs = 1;
            }
        } else if (!strcmp(arg, "-d") || !strcmp(arg, "--depth")) {
            NEXTARG();
            requestedDepth = atoi(arg);
            if ((requestedDepth != 8) && (requestedDepth != 10) && (requestedDepth != 12)) {
                fprintf(stderr, "ERROR: invalid depth: %s\n", arg);
                return 1;
            }
        } else if (!strcmp(arg, "-y") || !strcmp(arg, "--yuv")) {
            NEXTARG();
            if (!strcmp(arg, "444")) {
                requestedFormat = AVIF_PIXEL_FORMAT_YUV444;
            } else if (!strcmp(arg, "422")) {
                requestedFormat = AVIF_PIXEL_FORMAT_YUV422;
            } else if (!strcmp(arg, "420")) {
                requestedFormat = AVIF_PIXEL_FORMAT_YUV420;
            } else {
                fprintf(stderr, "ERROR: invalid format: %s\n", arg);
                return 1;
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
        } else if (!strcmp(arg, "--minalpha")) {
            NEXTARG();
            minQuantizerAlpha = atoi(arg);
            if (minQuantizerAlpha < AVIF_QUANTIZER_BEST_QUALITY) {
                minQuantizerAlpha = AVIF_QUANTIZER_BEST_QUALITY;
            }
            if (minQuantizerAlpha > AVIF_QUANTIZER_WORST_QUALITY) {
                minQuantizerAlpha = AVIF_QUANTIZER_WORST_QUALITY;
            }
        } else if (!strcmp(arg, "--maxalpha")) {
            NEXTARG();
            maxQuantizerAlpha = atoi(arg);
            if (maxQuantizerAlpha < AVIF_QUANTIZER_BEST_QUALITY) {
                maxQuantizerAlpha = AVIF_QUANTIZER_BEST_QUALITY;
            }
            if (maxQuantizerAlpha > AVIF_QUANTIZER_WORST_QUALITY) {
                maxQuantizerAlpha = AVIF_QUANTIZER_WORST_QUALITY;
            }
        } else if (!strcmp(arg, "-n") || !strcmp(arg, "--nclx")) {
            NEXTARG();
            if (!parseNCLX(&nclx, arg)) {
                return 1;
            }
            nclxSet = AVIF_TRUE;
        } else if (!strcmp(arg, "-r") || !strcmp(arg, "--range")) {
            NEXTARG();
            if (!strcmp(arg, "limited") || !strcmp(arg, "l")) {
                requestedRange = AVIF_RANGE_LIMITED;
            } else if (!strcmp(arg, "full") || !strcmp(arg, "f")) {
                requestedRange = AVIF_RANGE_FULL;
            } else {
                fprintf(stderr, "ERROR: Unknown range: %s\n", arg);
                return 1;
            }
            requestedRangeSet = AVIF_TRUE;
        } else if (!strcmp(arg, "-s") || !strcmp(arg, "--speed")) {
            NEXTARG();
            if (!strcmp(arg, "default") || !strcmp(arg, "d")) {
                speed = AVIF_SPEED_DEFAULT;
            } else {
                speed = atoi(arg);
                if (speed > AVIF_SPEED_FASTEST) {
                    speed = AVIF_SPEED_FASTEST;
                }
                if (speed < AVIF_SPEED_SLOWEST) {
                    speed = AVIF_SPEED_SLOWEST;
                }
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
        } else if (!strcmp(arg, "--pasp")) {
            NEXTARG();
            paspCount = parseU32List(paspValues, arg);
            if (paspCount != 2) {
                fprintf(stderr, "ERROR: Invalid pasp values: %s\n", arg);
                return 1;
            }
        } else if (!strcmp(arg, "--clap")) {
            NEXTARG();
            clapCount = parseU32List(clapValues, arg);
            if (clapCount != 8) {
                fprintf(stderr, "ERROR: Invalid clap values: %s\n", arg);
                return 1;
            }
        } else if (!strcmp(arg, "--irot")) {
            NEXTARG();
            irotAngle = (uint8_t)atoi(arg);
            if (irotAngle > 3) {
                fprintf(stderr, "ERROR: Invalid irot angle: %s\n", arg);
                return 1;
            }
        } else if (!strcmp(arg, "--imir")) {
            NEXTARG();
            imirAxis = (uint8_t)atoi(arg);
            if (imirAxis > 1) {
                fprintf(stderr, "ERROR: Invalid imir axis: %s\n", arg);
                return 1;
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

    int returnCode = 0;
    avifImage * avif = avifImageCreateEmpty();
    avifRWData raw = AVIF_DATA_EMPTY;

    // Set range and nclx in advance so any upcoming RGB -> YUV use the proper coefficients
    if (requestedRangeSet) {
        avif->yuvRange = requestedRange;
    }
    if (nclxSet) {
        nclx.range = avif->yuvRange;
        avifImageSetProfileNCLX(avif, &nclx);
    }

    avifAppFileFormat inputFormat = avifGuessFileFormat(inputFilename);
    if (inputFormat == AVIF_APP_FILE_FORMAT_UNKNOWN) {
        fprintf(stderr, "Cannot determine input file extension: %s\n", inputFilename);
        returnCode = 1;
        goto cleanup;
    }
    if (inputFormat == AVIF_APP_FILE_FORMAT_Y4M) {
        if (requestedRangeSet) {
            fprintf(stderr, "WARNING: Ignoring range (-r) value when encoding from y4m content.\n");
        }
        if (!y4mRead(avif, inputFilename)) {
            returnCode = 1;
            goto cleanup;
        }
        if (nclxSet && (nclx.range != avif->yuvRange)) {
            // Update the NCLX profile based on the new range from the y4m file
            nclx.range = avif->yuvRange;
            avifImageSetProfileNCLX(avif, &nclx);
        }
    } else if (inputFormat == AVIF_APP_FILE_FORMAT_JPEG) {
        if (!avifJPEGRead(avif, inputFilename, requestedFormat, requestedDepth)) {
            returnCode = 1;
            goto cleanup;
        }
    } else if (inputFormat == AVIF_APP_FILE_FORMAT_PNG) {
        if (!avifPNGRead(avif, inputFilename, requestedFormat, requestedDepth)) {
            returnCode = 1;
            goto cleanup;
        }
    } else {
        fprintf(stderr, "Unrecognized file extension: %s\n", inputFilename);
        returnCode = 1;
        goto cleanup;
    }
    printf("Successfully loaded: %s\n", inputFilename);

    if (paspCount == 2) {
        avif->transformFlags |= AVIF_TRANSFORM_PASP;
        avif->pasp.hSpacing = paspValues[0];
        avif->pasp.vSpacing = paspValues[1];
    }
    if (clapCount == 8) {
        avif->transformFlags |= AVIF_TRANSFORM_CLAP;
        avif->clap.widthN = clapValues[0];
        avif->clap.widthD = clapValues[1];
        avif->clap.heightN = clapValues[2];
        avif->clap.heightD = clapValues[3];
        avif->clap.horizOffN = clapValues[4];
        avif->clap.horizOffD = clapValues[5];
        avif->clap.vertOffN = clapValues[6];
        avif->clap.vertOffD = clapValues[7];
    }
    if (irotAngle != 0xff) {
        avif->transformFlags |= AVIF_TRANSFORM_IROT;
        avif->irot.angle = irotAngle;
    }
    if (imirAxis != 0xff) {
        avif->transformFlags |= AVIF_TRANSFORM_IMIR;
        avif->imir.axis = imirAxis;
    }

    printf("AVIF to be written:\n");
    avifImageDump(avif);

    printf("Encoding with AV1 codec '%s' speed [%d], color QP [%d (%s) <-> %d (%s)], alpha QP [%d (%s) <-> %d (%s)], %d worker thread(s), please wait...\n",
           avifCodecName(codecChoice, AVIF_CODEC_FLAG_CAN_ENCODE),
           speed,
           minQuantizer,
           quantizerString(minQuantizer),
           maxQuantizer,
           quantizerString(maxQuantizer),
           minQuantizerAlpha,
           quantizerString(minQuantizerAlpha),
           maxQuantizerAlpha,
           quantizerString(maxQuantizerAlpha),
           jobs);
    encoder = avifEncoderCreate();
    encoder->maxThreads = jobs;
    encoder->minQuantizer = minQuantizer;
    encoder->maxQuantizer = maxQuantizer;
    encoder->minQuantizerAlpha = minQuantizerAlpha;
    encoder->maxQuantizerAlpha = maxQuantizerAlpha;
    encoder->codecChoice = codecChoice;
    encoder->speed = speed;
    avifResult encodeResult = avifEncoderWrite(encoder, avif, &raw);
    if (encodeResult != AVIF_RESULT_OK) {
        fprintf(stderr, "ERROR: Failed to encode image: %s\n", avifResultToString(encodeResult));
        goto cleanup;
    }

    printf("Encoded successfully.\n");
    printf(" * ColorOBU size: %zu bytes\n", encoder->ioStats.colorOBUSize);
    printf(" * AlphaOBU size: %zu bytes\n", encoder->ioStats.alphaOBUSize);
    FILE * f = fopen(outputFilename, "wb");
    if (!f) {
        fprintf(stderr, "ERROR: Failed to open file for write: %s\n", outputFilename);
        goto cleanup;
    }
    if (fwrite(raw.data, 1, raw.size, f) != raw.size) {
        fprintf(stderr, "Failed to write %zu bytes: %s\n", raw.size, outputFilename);
        returnCode = 1;
    } else {
        printf("Wrote AVIF: %s\n", outputFilename);
    }
    fclose(f);

cleanup:
    if (encoder) {
        avifEncoderDestroy(encoder);
    }
    avifImageDestroy(avif);
    avifRWDataFree(&raw);
    return returnCode;
}
