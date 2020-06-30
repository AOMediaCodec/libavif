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
        goto cleanup;                                                 \
    }                                                                 \
    arg = argv[++argIndex]

static void syntax(void)
{
    printf("Syntax: avifenc [options] input.[jpg|jpeg|png|y4m] output.avif\n");
    printf("Options:\n");
    printf("    -h,--help                         : Show syntax help\n");
    printf("    -j,--jobs J                       : Number of jobs (worker threads, default: 1)\n");
    printf("    -o,--output FILENAME              : Instead of using the last filename given as output, use this filename\n");
    printf("    -l,--lossless                     : Set all defaults to encode losslessly, and emit warnings when settings/input don't allow for it\n");
    printf("    -d,--depth D                      : Output depth [8,10,12]. (JPEG/PNG only; For y4m, depth is retained)\n");
    printf("    -y,--yuv FORMAT                   : Output format [default=444, 422, 420, 400]. (JPEG/PNG only; For y4m, format is retained)\n");
    printf("    --cicp,--nclx P/T/M               : Set CICP values (nclx colr box) (3 raw numbers, use -r to set range flag)\n");
    printf("                                        P = enum avifColorPrimaries\n");
    printf("                                        T = enum avifTransferCharacteristics\n");
    printf("                                        M = enum avifMatrixCoefficients\n");
    printf("                                        (use 2 for any you wish to leave unspecified)\n");
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
    printf("    -s,--speed S                      : Encoder speed (%d-%d, slowest-fastest, 'default' or 'd' for codec internal defaults. default speed: 8)\n",
           AVIF_SPEED_SLOWEST,
           AVIF_SPEED_FASTEST);
    printf("    -c,--codec C                      : AV1 codec to use (choose from versions list below)\n");
    printf("    --duration D                      : Set all following frame durations (in timescales) to D; default 1. Can be set multiple times (before supplying each filename)\n");
    printf("    --timescale,--fps V               : Set the timescale to V. If all frames are 1 timescale in length, this is equivalent to frames per second\n");
    printf("    -k,--keyframe INTERVAL            : Set the forced keyframe interval (maximum frames between keyframes). Set to 0 to disable (default).\n");
    printf("    --ignore-icc                      : If the input file contains an embedded ICC profile, ignore it (no-op if absent)\n");
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

static avifBool parseCICP(int cicp[3], const char * arg)
{
    char buffer[128];
    strncpy(buffer, arg, 127);
    buffer[127] = 0;

    int index = 0;
    char * token = strtok(buffer, "/");
    while (token != NULL) {
        cicp[index] = atoi(token);
        ++index;
        if (index >= 3) {
            break;
        }

        token = strtok(NULL, "/");
    }

    if (index == 3) {
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

struct avifInputFile
{
    const char * filename;
    int duration;
};

int main(int argc, char * argv[])
{
    int inputFilesCount = 0;
    struct avifInputFile * inputFiles = NULL;
    const char * outputFilename = NULL;

    if (argc < 2) {
        syntax();
        return 1;
    }

    inputFiles = malloc(sizeof(struct avifInputFile) * argc);

    int returnCode = 0;
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
    avifBool lossless = AVIF_FALSE;
    avifBool ignoreICC = AVIF_FALSE;
    avifEncoder * encoder = NULL;
    avifImage * image = NULL;
    avifImage * nextImage = NULL;
    avifRWData raw = AVIF_DATA_EMPTY;
    int duration = 1;  // in timescales, stored per-inputFile (see struct avifInputFile)
    int timescale = 1; // 1 fps by default
    int keyframeInterval = 0;

    // By default, the color profile itself is unspecified, so CP/TC are set (to 2) accordingly.
    // However, if the end-user doesn't specify any CICP, we will convert to YUV using BT709
    // coefficients anyway (as MC:2 falls back to MC:1), so we might as well signal it explicitly.
    avifColorPrimaries colorPrimaries = AVIF_COLOR_PRIMARIES_UNSPECIFIED;
    avifTransferCharacteristics transferCharacteristics = AVIF_TRANSFER_CHARACTERISTICS_UNSPECIFIED;
    avifMatrixCoefficients matrixCoefficients = AVIF_MATRIX_COEFFICIENTS_BT709;

    int argIndex = 1;
    while (argIndex < argc) {
        const char * arg = argv[argIndex];

        if (!strcmp(arg, "-h") || !strcmp(arg, "--help")) {
            syntax();
            goto cleanup;
        } else if (!strcmp(arg, "-j") || !strcmp(arg, "--jobs")) {
            NEXTARG();
            jobs = atoi(arg);
            if (jobs < 1) {
                jobs = 1;
            }
        } else if (!strcmp(arg, "-o") || !strcmp(arg, "--output")) {
            NEXTARG();
            outputFilename = arg;
        } else if (!strcmp(arg, "-d") || !strcmp(arg, "--depth")) {
            NEXTARG();
            requestedDepth = atoi(arg);
            if ((requestedDepth != 8) && (requestedDepth != 10) && (requestedDepth != 12)) {
                fprintf(stderr, "ERROR: invalid depth: %s\n", arg);
                returnCode = 1;
                goto cleanup;
            }
        } else if (!strcmp(arg, "-y") || !strcmp(arg, "--yuv")) {
            NEXTARG();
            if (!strcmp(arg, "444")) {
                requestedFormat = AVIF_PIXEL_FORMAT_YUV444;
            } else if (!strcmp(arg, "422")) {
                requestedFormat = AVIF_PIXEL_FORMAT_YUV422;
            } else if (!strcmp(arg, "420")) {
                requestedFormat = AVIF_PIXEL_FORMAT_YUV420;
            } else if (!strcmp(arg, "400")) {
                requestedFormat = AVIF_PIXEL_FORMAT_YUV400;
            } else {
                fprintf(stderr, "ERROR: invalid format: %s\n", arg);
                returnCode = 1;
                goto cleanup;
            }
        } else if (!strcmp(arg, "-k") || !strcmp(arg, "--keyframe")) {
            NEXTARG();
            keyframeInterval = atoi(arg);
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
        } else if (!strcmp(arg, "--cicp") || !strcmp(arg, "--nclx")) {
            NEXTARG();
            int cicp[3];
            if (!parseCICP(cicp, arg)) {
                returnCode = 1;
                goto cleanup;
            }
            colorPrimaries = (avifColorPrimaries)cicp[0];
            transferCharacteristics = (avifTransferCharacteristics)cicp[1];
            matrixCoefficients = (avifMatrixCoefficients)cicp[2];
        } else if (!strcmp(arg, "-r") || !strcmp(arg, "--range")) {
            NEXTARG();
            if (!strcmp(arg, "limited") || !strcmp(arg, "l")) {
                requestedRange = AVIF_RANGE_LIMITED;
            } else if (!strcmp(arg, "full") || !strcmp(arg, "f")) {
                requestedRange = AVIF_RANGE_FULL;
            } else {
                fprintf(stderr, "ERROR: Unknown range: %s\n", arg);
                returnCode = 1;
                goto cleanup;
            }
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
        } else if (!strcmp(arg, "--duration")) {
            NEXTARG();
            duration = atoi(arg);
            if (duration < 1) {
                fprintf(stderr, "ERROR: Invalid duration: %d\n", duration);
                returnCode = 1;
                goto cleanup;
            }
        } else if (!strcmp(arg, "--timescale") || !strcmp(arg, "--fps")) {
            NEXTARG();
            timescale = atoi(arg);
            if (timescale < 1) {
                fprintf(stderr, "ERROR: Invalid timescale: %d\n", timescale);
                returnCode = 1;
                goto cleanup;
            }
        } else if (!strcmp(arg, "-c") || !strcmp(arg, "--codec")) {
            NEXTARG();
            codecChoice = avifCodecChoiceFromName(arg);
            if (codecChoice == AVIF_CODEC_CHOICE_AUTO) {
                fprintf(stderr, "ERROR: Unrecognized codec: %s\n", arg);
                returnCode = 1;
                goto cleanup;
            } else {
                const char * codecName = avifCodecName(codecChoice, AVIF_CODEC_FLAG_CAN_ENCODE);
                if (codecName == NULL) {
                    fprintf(stderr, "ERROR: AV1 Codec cannot encode: %s\n", arg);
                    returnCode = 1;
                    goto cleanup;
                }
            }
        } else if (!strcmp(arg, "--ignore-icc")) {
            ignoreICC = AVIF_TRUE;
        } else if (!strcmp(arg, "--pasp")) {
            NEXTARG();
            paspCount = parseU32List(paspValues, arg);
            if (paspCount != 2) {
                fprintf(stderr, "ERROR: Invalid pasp values: %s\n", arg);
                returnCode = 1;
                goto cleanup;
            }
        } else if (!strcmp(arg, "--clap")) {
            NEXTARG();
            clapCount = parseU32List(clapValues, arg);
            if (clapCount != 8) {
                fprintf(stderr, "ERROR: Invalid clap values: %s\n", arg);
                returnCode = 1;
                goto cleanup;
            }
        } else if (!strcmp(arg, "--irot")) {
            NEXTARG();
            irotAngle = (uint8_t)atoi(arg);
            if (irotAngle > 3) {
                fprintf(stderr, "ERROR: Invalid irot angle: %s\n", arg);
                returnCode = 1;
                goto cleanup;
            }
        } else if (!strcmp(arg, "--imir")) {
            NEXTARG();
            imirAxis = (uint8_t)atoi(arg);
            if (imirAxis > 1) {
                fprintf(stderr, "ERROR: Invalid imir axis: %s\n", arg);
                returnCode = 1;
                goto cleanup;
            }
        } else if (!strcmp(arg, "-l") || !strcmp(arg, "--lossless")) {
            lossless = AVIF_TRUE;

            // Set defaults, and warn later on if anything looks incorrect
            requestedFormat = AVIF_PIXEL_FORMAT_YUV444;  // don't subsample when using AVIF_MATRIX_COEFFICIENTS_IDENTITY
            minQuantizer = AVIF_QUANTIZER_LOSSLESS;      // lossless
            maxQuantizer = AVIF_QUANTIZER_LOSSLESS;      // lossless
            minQuantizerAlpha = AVIF_QUANTIZER_LOSSLESS; // lossless
            maxQuantizerAlpha = AVIF_QUANTIZER_LOSSLESS; // lossless
            codecChoice = AVIF_CODEC_CHOICE_AOM;         // rav1e doesn't support lossless transform yet:
                                                         // https://github.com/xiph/rav1e/issues/151
            requestedRange = AVIF_RANGE_FULL;            // avoid limited range
            matrixCoefficients = AVIF_MATRIX_COEFFICIENTS_IDENTITY; // this is key for lossless
        } else {
            // Positional argument
            inputFiles[inputFilesCount].filename = arg;
            inputFiles[inputFilesCount].duration = duration;
            ++inputFilesCount;
        }

        ++argIndex;
    }

    if (!outputFilename && (inputFilesCount > 1)) {
        --inputFilesCount;
        outputFilename = inputFiles[inputFilesCount].filename;
    }

    if ((inputFilesCount < 1) || !outputFilename) {
        syntax();
        returnCode = 1;
        goto cleanup;
    }

    image = avifImageCreateEmpty();

    uint32_t sourceDepth = 0;
    avifBool sourceWasRGB = AVIF_TRUE;
    avifAppFileFormat inputFormat = avifGuessFileFormat(inputFiles[0].filename);
    if (inputFormat == AVIF_APP_FILE_FORMAT_UNKNOWN) {
        fprintf(stderr, "Cannot determine input file extension: %s\n", inputFiles[0].filename);
        returnCode = 1;
        goto cleanup;
    }

    // Set these in advance so any upcoming RGB -> YUV use the proper coefficients
    image->colorPrimaries = colorPrimaries;
    image->transferCharacteristics = transferCharacteristics;
    image->matrixCoefficients = matrixCoefficients;
    image->yuvRange = requestedRange;

    if (inputFormat == AVIF_APP_FILE_FORMAT_Y4M) {
        if (!y4mRead(image, inputFiles[0].filename)) {
            returnCode = 1;
            goto cleanup;
        }
        sourceDepth = image->depth;
        sourceWasRGB = AVIF_FALSE;
    } else if (inputFormat == AVIF_APP_FILE_FORMAT_JPEG) {
        if (!avifJPEGRead(image, inputFiles[0].filename, requestedFormat, requestedDepth)) {
            returnCode = 1;
            goto cleanup;
        }
        sourceDepth = 8;
    } else if (inputFormat == AVIF_APP_FILE_FORMAT_PNG) {
        if (!avifPNGRead(image, inputFiles[0].filename, requestedFormat, requestedDepth, &sourceDepth)) {
            returnCode = 1;
            goto cleanup;
        }
    } else {
        fprintf(stderr, "Unrecognized file extension: %s\n", inputFiles[0].filename);
        returnCode = 1;
        goto cleanup;
    }
    printf("Successfully loaded: %s\n", inputFiles[0].filename);

    if (ignoreICC) {
        avifImageSetProfileICC(image, NULL, 0);
    }

    if (paspCount == 2) {
        image->transformFlags |= AVIF_TRANSFORM_PASP;
        image->pasp.hSpacing = paspValues[0];
        image->pasp.vSpacing = paspValues[1];
    }
    if (clapCount == 8) {
        image->transformFlags |= AVIF_TRANSFORM_CLAP;
        image->clap.widthN = clapValues[0];
        image->clap.widthD = clapValues[1];
        image->clap.heightN = clapValues[2];
        image->clap.heightD = clapValues[3];
        image->clap.horizOffN = clapValues[4];
        image->clap.horizOffD = clapValues[5];
        image->clap.vertOffN = clapValues[6];
        image->clap.vertOffD = clapValues[7];
    }
    if (irotAngle != 0xff) {
        image->transformFlags |= AVIF_TRANSFORM_IROT;
        image->irot.angle = irotAngle;
    }
    if (imirAxis != 0xff) {
        image->transformFlags |= AVIF_TRANSFORM_IMIR;
        image->imir.axis = imirAxis;
    }

    avifBool usingAOM = AVIF_FALSE;
    const char * codecName = avifCodecName(codecChoice, AVIF_CODEC_FLAG_CAN_ENCODE);
    if (codecName && !strcmp(codecName, "aom")) {
        usingAOM = AVIF_TRUE;
    }
    avifBool hasAlpha = (image->alphaPlane && image->alphaRowBytes);
    avifBool losslessColorQP = (minQuantizer == AVIF_QUANTIZER_LOSSLESS) && (maxQuantizer == AVIF_QUANTIZER_LOSSLESS);
    avifBool losslessAlphaQP = (minQuantizerAlpha == AVIF_QUANTIZER_LOSSLESS) && (maxQuantizerAlpha == AVIF_QUANTIZER_LOSSLESS);
    avifBool depthMatches = (sourceDepth == image->depth);
    avifBool using444 = (image->yuvFormat == AVIF_PIXEL_FORMAT_YUV444);
    avifBool usingFullRange = (image->yuvRange == AVIF_RANGE_FULL);
    avifBool usingIdentityMatrix = (image->matrixCoefficients == AVIF_MATRIX_COEFFICIENTS_IDENTITY);

    // Guess if the enduser is asking for lossless and enable it so that warnings can be emitted
    if (!lossless && losslessColorQP && (!hasAlpha || losslessAlphaQP)) {
        // The enduser is probably expecting lossless. Turn it on and emit warnings
        printf("Min/max QPs set to %d, assuming --lossless to enable warnings on potential lossless issues.\n", AVIF_QUANTIZER_LOSSLESS);
        lossless = AVIF_TRUE;
    }

    // Check for any reasons lossless will fail, and complain loudly
    if (lossless) {
        if (!usingAOM) {
            fprintf(stderr, "WARNING: [--lossless] Only aom (-c) supports lossless transforms. Output might not be lossless.\n");
            lossless = AVIF_FALSE;
        }

        if (!losslessColorQP) {
            fprintf(stderr,
                    "WARNING: [--lossless] Color quantizer range (--min, --max) not set to %d. Color output might not be lossless.\n",
                    AVIF_QUANTIZER_LOSSLESS);
            lossless = AVIF_FALSE;
        }

        if (hasAlpha && !losslessAlphaQP) {
            fprintf(stderr,
                    "WARNING: [--lossless] Alpha present and alpha quantizer range (--minalpha, --maxalpha) not set to %d. Alpha output might not be lossless.\n",
                    AVIF_QUANTIZER_LOSSLESS);
            lossless = AVIF_FALSE;
        }

        if (!depthMatches) {
            fprintf(stderr,
                    "WARNING: [--lossless] Input depth (%d) does not match output depth (%d). Output might not be lossless.\n",
                    sourceDepth,
                    image->depth);
            lossless = AVIF_FALSE;
        }

        if (sourceWasRGB) {
            if (!using444) {
                fprintf(stderr, "WARNING: [--lossless] Input data was RGB and YUV subsampling (-y) isn't YUV444. Output might not be lossless.\n");
                lossless = AVIF_FALSE;
            }

            if (!usingFullRange) {
                fprintf(stderr, "WARNING: [--lossless] Input data was RGB and output range (-r) isn't full. Output might not be lossless.\n");
                lossless = AVIF_FALSE;
            }

            if (!usingIdentityMatrix) {
                fprintf(stderr, "WARNING: [--lossless] Input data was RGB and matrixCoefficients isn't set to identity (--cicp x/x/0); Output might not be lossless.\n");
                lossless = AVIF_FALSE;
            }
        }
    }

    const char * lossyHint = " (Lossy)";
    if (lossless) {
        lossyHint = " (Lossless)";
    }
    printf("AVIF to be written:%s\n", lossyHint);
    avifImageDump(image);

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
    encoder->timescale = (uint64_t)timescale;
    encoder->keyframeInterval = keyframeInterval;

    uint32_t addImageFlags = AVIF_ADD_IMAGE_FLAG_NONE;
    if (inputFilesCount == 1) {
        addImageFlags |= AVIF_ADD_IMAGE_FLAG_SINGLE;
    }

    uint32_t firstDurationInTimescales = inputFiles[0].duration;
    if (inputFilesCount > 1) {
        printf(" * Encoding frame 1 [%u/%d ts]: %s\n", firstDurationInTimescales, timescale, inputFiles[0].filename);
    }
    avifResult addImageResult = avifEncoderAddImage(encoder, image, firstDurationInTimescales, addImageFlags);
    if (addImageResult != AVIF_RESULT_OK) {
        fprintf(stderr, "ERROR: Failed to encode image: %s\n", avifResultToString(addImageResult));
        goto cleanup;
    }

    if (inputFilesCount > 1) {
        for (int nextImageIndex = 1; nextImageIndex < inputFilesCount; ++nextImageIndex) {
            const char * nextImageFilename = inputFiles[nextImageIndex].filename;
            uint32_t nextDurationInTimescales = inputFiles[nextImageIndex].duration;

            printf(" * Encoding frame %d [%u/%d ts]: %s\n", nextImageIndex + 1, nextDurationInTimescales, timescale, nextImageFilename);

            avifAppFileFormat nextInputFormat = avifGuessFileFormat(nextImageFilename);
            if (nextInputFormat == AVIF_APP_FILE_FORMAT_UNKNOWN) {
                fprintf(stderr, "Cannot determine input file extension: %s\n", nextImageFilename);
                returnCode = 1;
                goto cleanup;
            }

            nextImage = avifImageCreateEmpty();
            nextImage->colorPrimaries = image->colorPrimaries;
            nextImage->transferCharacteristics = image->transferCharacteristics;
            nextImage->matrixCoefficients = image->matrixCoefficients;
            nextImage->yuvRange = image->yuvRange;

            if (nextInputFormat == AVIF_APP_FILE_FORMAT_Y4M) {
                if (!y4mRead(nextImage, nextImageFilename)) {
                    returnCode = 1;
                    goto cleanup;
                }
            } else if (nextInputFormat == AVIF_APP_FILE_FORMAT_JPEG) {
                if (!avifJPEGRead(nextImage, nextImageFilename, requestedFormat, requestedDepth)) {
                    returnCode = 1;
                    goto cleanup;
                }
                sourceDepth = 8;
            } else if (nextInputFormat == AVIF_APP_FILE_FORMAT_PNG) {
                if (!avifPNGRead(nextImage, nextImageFilename, requestedFormat, requestedDepth, &sourceDepth)) {
                    returnCode = 1;
                    goto cleanup;
                }
            } else {
                fprintf(stderr, "Unrecognized file extension: %s\n", nextImageFilename);
                returnCode = 1;
                goto cleanup;
            }

            // Verify that this frame's properties matches the first frame's properties
            if ((image->width != nextImage->width) || (image->height != nextImage->height)) {
                fprintf(stderr,
                        "ERROR: Image sequence dimensions mismatch, [%ux%u] vs [%ux%u]: %s\n",
                        image->width,
                        image->height,
                        nextImage->width,
                        nextImage->height,
                        nextImageFilename);
                goto cleanup;
            }
            if (image->depth != nextImage->depth) {
                fprintf(stderr, "ERROR: Image sequence depth mismatch, [%u] vs [%u]: %s\n", image->depth, nextImage->depth, nextImageFilename);
                goto cleanup;
            }
            if ((image->colorPrimaries != nextImage->colorPrimaries) ||
                (image->transferCharacteristics != nextImage->transferCharacteristics) ||
                (image->matrixCoefficients != nextImage->matrixCoefficients)) {
                fprintf(stderr,
                        "ERROR: Image sequence CICP mismatch, [%u/%u/%u] vs [%u/%u/%u]: %s\n",
                        image->colorPrimaries,
                        image->matrixCoefficients,
                        image->transferCharacteristics,
                        nextImage->colorPrimaries,
                        nextImage->transferCharacteristics,
                        nextImage->matrixCoefficients,
                        nextImageFilename);
                goto cleanup;
            }
            if (image->yuvRange != nextImage->yuvRange) {
                fprintf(stderr,
                        "ERROR: Image sequence range mismatch, [%s] vs [%s]: %s\n",
                        (image->yuvRange == AVIF_RANGE_FULL) ? "Full" : "Limited",
                        (nextImage->yuvRange == AVIF_RANGE_FULL) ? "Full" : "Limited",
                        nextImageFilename);
                goto cleanup;
            }

            avifResult nextImageResult = avifEncoderAddImage(encoder, nextImage, nextDurationInTimescales, AVIF_ADD_IMAGE_FLAG_NONE);
            if (nextImageResult != AVIF_RESULT_OK) {
                fprintf(stderr, "ERROR: Failed to encode image: %s\n", avifResultToString(nextImageResult));
                goto cleanup;
            }
        }
    }

    avifResult finishResult = avifEncoderFinish(encoder, &raw);
    if (finishResult != AVIF_RESULT_OK) {
        fprintf(stderr, "ERROR: Failed to finish encoding: %s\n", avifResultToString(finishResult));
        goto cleanup;
    }

    printf("Encoded successfully.\n");
    printf(" * Color AV1 total size: %zu bytes\n", encoder->ioStats.colorOBUSize);
    printf(" * Alpha AV1 total size: %zu bytes\n", encoder->ioStats.alphaOBUSize);
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
    if (image) {
        avifImageDestroy(image);
    }
    if (nextImage) {
        avifImageDestroy(nextImage);
    }
    avifRWDataFree(&raw);
    free((void *)inputFiles);
    return returnCode;
}
