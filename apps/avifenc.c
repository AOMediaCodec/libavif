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

#if defined(_WIN32)
// for setmode()
#include <fcntl.h>
#include <io.h>
#endif

#define NEXTARG()                                                     \
    if (((argIndex + 1) == argc) || (argv[argIndex + 1][0] == '-')) { \
        fprintf(stderr, "%s requires an argument.", arg);             \
        goto cleanup;                                                 \
    }                                                                 \
    arg = argv[++argIndex]

typedef struct avifInputFile
{
    const char * filename;
    int duration;
} avifInputFile;
static avifInputFile stdinFile;

typedef struct avifInput
{
    avifInputFile * files;
    int filesCount;
    int fileIndex;
    struct y4mFrameIterator * frameIter;
    avifPixelFormat requestedFormat;
    int requestedDepth;
    avifBool useStdin;
} avifInput;

static void syntax(void)
{
    printf("Syntax: avifenc [options] input.[jpg|jpeg|png|y4m] output.avif\n");
    printf("Options:\n");
    printf("    -h,--help                         : Show syntax help\n");
    printf("    -j,--jobs J                       : Number of jobs (worker threads, default: 1)\n");
    printf("    -o,--output FILENAME              : Instead of using the last filename given as output, use this filename\n");
    printf("    -l,--lossless                     : Set all defaults to encode losslessly, and emit warnings when settings/input don't allow for it\n");
    printf("    -d,--depth D                      : Output depth [8,10,12]. (JPEG/PNG only; For y4m or stdin, depth is retained)\n");
    printf("    -y,--yuv FORMAT                   : Output format [default=444, 422, 420, 400]. (JPEG/PNG only; For y4m or stdin, format is retained)\n");
    printf("    --stdin                           : Read y4m frames from stdin instead of files; no input filenames allowed, must set before offering output filename\n");
    printf("    --cicp,--nclx P/T/M               : Set CICP values (nclx colr box) (3 raw numbers, use -r to set range flag)\n");
    printf("                                        P = enum avifColorPrimaries\n");
    printf("                                        T = enum avifTransferCharacteristics\n");
    printf("                                        M = enum avifMatrixCoefficients\n");
    printf("                                        (use 2 for any you wish to leave unspecified)\n");
    printf("    -r,--range RANGE                  : YUV range [limited or l, full or f]. (JPEG/PNG only, default: full; For y4m or stdin, range is retained)\n");
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
    printf("    --tilerowslog2 R                  : Set log2 of number of tile rows (0-6, default: 0)\n");
    printf("    --tilecolslog2 C                  : Set log2 of number of tile columns (0-6, default: 0)\n");
    printf("    -s,--speed S                      : Encoder speed (%d-%d, slowest-fastest, 'default' or 'd' for codec internal defaults. default speed: 8)\n",
           AVIF_SPEED_SLOWEST,
           AVIF_SPEED_FASTEST);
    printf("    -c,--codec C                      : AV1 codec to use (choose from versions list below)\n");
    printf("    -a,--advanced KEY[=VALUE]         : Pass an advanced, codec-specific key/value string pair directly to the codec. avifenc will warn on any not used by the codec.\n");
    printf("    --duration D                      : Set all following frame durations (in timescales) to D; default 1. Can be set multiple times (before supplying each filename)\n");
    printf("    --timescale,--fps V               : Set the timescale to V. If all frames are 1 timescale in length, this is equivalent to frames per second\n");
    printf("    -k,--keyframe INTERVAL            : Set the forced keyframe interval (maximum frames between keyframes). Set to 0 to disable (default).\n");
    printf("    --ignore-icc                      : If the input file contains an embedded ICC profile, ignore it (no-op if absent)\n");
    printf("    --pasp H,V                        : Add pasp property (aspect ratio). H=horizontal spacing, V=vertical spacing\n");
    printf("    --clap WN,WD,HN,HD,HON,HOD,VON,VOD: Add clap property (clean aperture). Width, Height, HOffset, VOffset (in num/denom pairs)\n");
    printf("    --irot ANGLE                      : Add irot property (rotation). [0-3], makes (90 * ANGLE) degree rotation anti-clockwise\n");
    printf("    --imir AXIS                       : Add imir property (mirroring). 0=vertical, 1=horizontal\n");
    printf("\n");
    if (avifCodecName(AVIF_CODEC_CHOICE_AOM, 0)) {
        printf("aom-specific advanced options:\n");
        printf("    aq-mode=M                         : Adaptive quantization mode (0: off (default), 1: variance, 2: complexity, 3: cyclic refresh)\n");
        printf("    cq-level=Q                        : Constant/Constrained Quality level (0-63, end-usage must be set to cq or q)\n");
        printf("    end-usage=MODE                    : Rate control mode (vbr, cbr, cq, or q)\n");
        printf("    sharpness=S                       : Loop filter sharpness (0-7, default: 0)\n");
        printf("    tune=METRIC                       : Tune the encoder for distortion metric (psnr or ssim, default: psnr)\n");
        printf("\n");
    }
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

static avifInputFile * avifInputGetNextFile(avifInput * input)
{
    if (input->useStdin) {
        ungetc(fgetc(stdin), stdin); // Kick stdin to force EOF

        if (feof(stdin)) {
            return NULL;
        }
        return &stdinFile;
    }

    if (input->fileIndex >= input->filesCount) {
        return NULL;
    }
    return &input->files[input->fileIndex];
}

static avifAppFileFormat avifInputReadImage(avifInput * input, avifImage * image, uint32_t * outDepth)
{
    if (input->useStdin) {
        if (feof(stdin)) {
            return AVIF_APP_FILE_FORMAT_UNKNOWN;
        }
        if (!y4mRead(image, NULL, &input->frameIter)) {
            return AVIF_APP_FILE_FORMAT_UNKNOWN;
        }
        return AVIF_APP_FILE_FORMAT_Y4M;
    }

    if (input->fileIndex >= input->filesCount) {
        return AVIF_APP_FILE_FORMAT_UNKNOWN;
    }

    avifAppFileFormat nextInputFormat = avifGuessFileFormat(input->files[input->fileIndex].filename);
    if (nextInputFormat == AVIF_APP_FILE_FORMAT_Y4M) {
        if (!y4mRead(image, input->files[input->fileIndex].filename, &input->frameIter)) {
            return AVIF_APP_FILE_FORMAT_UNKNOWN;
        }
        if (outDepth) {
            *outDepth = image->depth;
        }
    } else if (nextInputFormat == AVIF_APP_FILE_FORMAT_JPEG) {
        if (!avifJPEGRead(image, input->files[input->fileIndex].filename, input->requestedFormat, input->requestedDepth)) {
            return AVIF_APP_FILE_FORMAT_UNKNOWN;
        }
        if (outDepth) {
            *outDepth = 8;
        }
    } else if (nextInputFormat == AVIF_APP_FILE_FORMAT_PNG) {
        if (!avifPNGRead(image, input->files[input->fileIndex].filename, input->requestedFormat, input->requestedDepth, outDepth)) {
            return AVIF_APP_FILE_FORMAT_UNKNOWN;
        }
    } else {
        fprintf(stderr, "Unrecognized file format: %s\n", input->files[input->fileIndex].filename);
        return AVIF_APP_FILE_FORMAT_UNKNOWN;
    }

    if (!input->frameIter) {
        ++input->fileIndex;
    }
    return nextInputFormat;
}

int main(int argc, char * argv[])
{
    if (argc < 2) {
        syntax();
        return 1;
    }

    const char * outputFilename = NULL;

    avifInput input;
    memset(&input, 0, sizeof(input));
    input.files = malloc(sizeof(avifInputFile) * argc);
    input.requestedFormat = AVIF_PIXEL_FORMAT_YUV444;

    int returnCode = 0;
    int jobs = 1;
    int minQuantizer = AVIF_QUANTIZER_BEST_QUALITY;
    int maxQuantizer = 10; // "High Quality", but not lossless
    int minQuantizerAlpha = AVIF_QUANTIZER_LOSSLESS;
    int maxQuantizerAlpha = AVIF_QUANTIZER_LOSSLESS;
    int tileRowsLog2 = 0;
    int tileColsLog2 = 0;
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
    avifEncoder * encoder = avifEncoderCreate();
    avifImage * image = NULL;
    avifImage * nextImage = NULL;
    avifRWData raw = AVIF_DATA_EMPTY;
    int duration = 1;  // in timescales, stored per-inputFile (see avifInputFile)
    int timescale = 1; // 1 fps by default
    int keyframeInterval = 0;
    avifBool cicpExplicitlySet = AVIF_FALSE;

    // By default, the color profile itself is unspecified, so CP/TC are set (to 2) accordingly.
    // However, if the end-user doesn't specify any CICP, we will convert to YUV using BT601
    // coefficients anyway (as MC:2 falls back to MC:5/6), so we might as well signal it explicitly.
    // See: ISO/IEC 23000-22:2019 Amendment 2, or the comment in avifCalcYUVCoefficients()
    avifColorPrimaries colorPrimaries = AVIF_COLOR_PRIMARIES_UNSPECIFIED;
    avifTransferCharacteristics transferCharacteristics = AVIF_TRANSFER_CHARACTERISTICS_UNSPECIFIED;
    avifMatrixCoefficients matrixCoefficients = AVIF_MATRIX_COEFFICIENTS_BT601;

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
        } else if (!strcmp(arg, "--stdin")) {
            input.useStdin = AVIF_TRUE;
        } else if (!strcmp(arg, "-o") || !strcmp(arg, "--output")) {
            NEXTARG();
            outputFilename = arg;
        } else if (!strcmp(arg, "-d") || !strcmp(arg, "--depth")) {
            NEXTARG();
            input.requestedDepth = atoi(arg);
            if ((input.requestedDepth != 8) && (input.requestedDepth != 10) && (input.requestedDepth != 12)) {
                fprintf(stderr, "ERROR: invalid depth: %s\n", arg);
                returnCode = 1;
                goto cleanup;
            }
        } else if (!strcmp(arg, "-y") || !strcmp(arg, "--yuv")) {
            NEXTARG();
            if (!strcmp(arg, "444")) {
                input.requestedFormat = AVIF_PIXEL_FORMAT_YUV444;
            } else if (!strcmp(arg, "422")) {
                input.requestedFormat = AVIF_PIXEL_FORMAT_YUV422;
            } else if (!strcmp(arg, "420")) {
                input.requestedFormat = AVIF_PIXEL_FORMAT_YUV420;
            } else if (!strcmp(arg, "400")) {
                input.requestedFormat = AVIF_PIXEL_FORMAT_YUV400;
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
        } else if (!strcmp(arg, "--tilerowslog2")) {
            NEXTARG();
            tileRowsLog2 = atoi(arg);
            if (tileRowsLog2 < 0) {
                tileRowsLog2 = 0;
            }
            if (tileRowsLog2 > 6) {
                tileRowsLog2 = 6;
            }
        } else if (!strcmp(arg, "--tilecolslog2")) {
            NEXTARG();
            tileColsLog2 = atoi(arg);
            if (tileColsLog2 < 0) {
                tileColsLog2 = 0;
            }
            if (tileColsLog2 > 6) {
                tileColsLog2 = 6;
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
            cicpExplicitlySet = AVIF_TRUE;
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
        } else if (!strcmp(arg, "-a") || !strcmp(arg, "--advanced")) {
            NEXTARG();
            char * tempBuffer = strdup(arg);
            char * value = strchr(tempBuffer, '=');
            if (value) {
                *value = 0; // remove equals sign,
                ++value;    // and move past it

            } else {
                value = ""; // Pass in a non-NULL, empty string. Codecs can use the
                            // mere existence of a key as a boolean value.
            }
            avifEncoderSetCodecSpecificOption(encoder, tempBuffer, value);
            free(tempBuffer);
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
            input.requestedFormat = AVIF_PIXEL_FORMAT_YUV444; // don't subsample when using AVIF_MATRIX_COEFFICIENTS_IDENTITY
            minQuantizer = AVIF_QUANTIZER_LOSSLESS;           // lossless
            maxQuantizer = AVIF_QUANTIZER_LOSSLESS;           // lossless
            minQuantizerAlpha = AVIF_QUANTIZER_LOSSLESS;      // lossless
            maxQuantizerAlpha = AVIF_QUANTIZER_LOSSLESS;      // lossless
            codecChoice = AVIF_CODEC_CHOICE_AOM;              // rav1e doesn't support lossless transform yet:
                                                              // https://github.com/xiph/rav1e/issues/151
            requestedRange = AVIF_RANGE_FULL;                 // avoid limited range
            matrixCoefficients = AVIF_MATRIX_COEFFICIENTS_IDENTITY; // this is key for lossless
        } else {
            // Positional argument
            input.files[input.filesCount].filename = arg;
            input.files[input.filesCount].duration = duration;
            ++input.filesCount;
        }

        ++argIndex;
    }

    stdinFile.filename = "(stdin)";
    stdinFile.duration = duration; // TODO: Allow arbitrary frame durations from stdin?

    if (!outputFilename) {
        if (((input.useStdin && (input.filesCount == 1)) || (!input.useStdin && (input.filesCount > 1)))) {
            --input.filesCount;
            outputFilename = input.files[input.filesCount].filename;
        }
    }

    if (!outputFilename || (input.useStdin && (input.filesCount > 0)) || (!input.useStdin && (input.filesCount < 1))) {
        syntax();
        returnCode = 1;
        goto cleanup;
    }

#if defined(_WIN32)
    if (input.useStdin) {
        setmode(fileno(stdin), O_BINARY);
    }
#endif

    image = avifImageCreateEmpty();

    // Set these in advance so any upcoming RGB -> YUV use the proper coefficients
    image->colorPrimaries = colorPrimaries;
    image->transferCharacteristics = transferCharacteristics;
    image->matrixCoefficients = matrixCoefficients;
    image->yuvRange = requestedRange;

    avifInputFile * firstFile = avifInputGetNextFile(&input);
    uint32_t sourceDepth = 0;
    avifAppFileFormat inputFormat = avifInputReadImage(&input, image, &sourceDepth);
    if (inputFormat == AVIF_APP_FILE_FORMAT_UNKNOWN) {
        fprintf(stderr, "Cannot determine input file format: %s\n", firstFile->filename);
        returnCode = 1;
        goto cleanup;
    }
    avifBool sourceWasRGB = (inputFormat != AVIF_APP_FILE_FORMAT_Y4M);

    printf("Successfully loaded: %s\n", firstFile->filename);

    if ((image->matrixCoefficients == AVIF_MATRIX_COEFFICIENTS_IDENTITY) && (image->yuvFormat != AVIF_PIXEL_FORMAT_YUV444)) {
        // matrixCoefficients was likely set to AVIF_MATRIX_COEFFICIENTS_IDENTITY as a side effect
        // of --lossless, and Identity is only valid with YUV444. Set this back to the default.
        image->matrixCoefficients = AVIF_MATRIX_COEFFICIENTS_BT601;

        if (cicpExplicitlySet) {
            // Only warn if someone explicitly asked for identity.
            printf("WARNING: matrixCoefficients may not be set to identity(0) when subsampling. Resetting MC to defaults.\n");
        }
    }

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
    avifBool using400 = (image->yuvFormat == AVIF_PIXEL_FORMAT_YUV400);
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
            if (!using444 && !using400) {
                fprintf(stderr, "WARNING: [--lossless] Input data was RGB and YUV subsampling (-y) isn't YUV444. Output might not be lossless.\n");
                lossless = AVIF_FALSE;
            }

            if (!usingFullRange) {
                fprintf(stderr, "WARNING: [--lossless] Input data was RGB and output range (-r) isn't full. Output might not be lossless.\n");
                lossless = AVIF_FALSE;
            }

            if (!usingIdentityMatrix && !using400) {
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

    printf("Encoding with AV1 codec '%s' speed [%d], color QP [%d (%s) <-> %d (%s)], alpha QP [%d (%s) <-> %d (%s)], tileRowsLog2 [%d], tileColsLog2 [%d], %d worker thread(s), please wait...\n",
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
           tileRowsLog2,
           tileColsLog2,
           jobs);
    encoder->maxThreads = jobs;
    encoder->minQuantizer = minQuantizer;
    encoder->maxQuantizer = maxQuantizer;
    encoder->minQuantizerAlpha = minQuantizerAlpha;
    encoder->maxQuantizerAlpha = maxQuantizerAlpha;
    encoder->tileRowsLog2 = tileRowsLog2;
    encoder->tileColsLog2 = tileColsLog2;
    encoder->codecChoice = codecChoice;
    encoder->speed = speed;
    encoder->timescale = (uint64_t)timescale;
    encoder->keyframeInterval = keyframeInterval;

    uint32_t addImageFlags = AVIF_ADD_IMAGE_FLAG_NONE;
    if (input.filesCount == 1) {
        addImageFlags |= AVIF_ADD_IMAGE_FLAG_SINGLE;
    }

    uint32_t firstDurationInTimescales = firstFile->duration;
    if (input.useStdin || (input.filesCount > 1)) {
        printf(" * Encoding frame 1 [%u/%d ts]: %s\n", firstDurationInTimescales, timescale, firstFile->filename);
    }
    avifResult addImageResult = avifEncoderAddImage(encoder, image, firstDurationInTimescales, addImageFlags);
    if (addImageResult != AVIF_RESULT_OK) {
        fprintf(stderr, "ERROR: Failed to encode image: %s\n", avifResultToString(addImageResult));
        goto cleanup;
    }

    avifInputFile * nextFile;
    int nextImageIndex = -1;
    while ((nextFile = avifInputGetNextFile(&input)) != NULL) {
        ++nextImageIndex;

        printf(" * Encoding frame %d [%u/%d ts]: %s\n", nextImageIndex + 1, nextFile->duration, timescale, nextFile->filename);

        if (nextImage) {
            avifImageDestroy(nextImage);
        }
        nextImage = avifImageCreateEmpty();
        nextImage->colorPrimaries = image->colorPrimaries;
        nextImage->transferCharacteristics = image->transferCharacteristics;
        nextImage->matrixCoefficients = image->matrixCoefficients;
        nextImage->yuvRange = image->yuvRange;

        avifAppFileFormat nextInputFormat = avifInputReadImage(&input, nextImage, NULL);
        if (nextInputFormat == AVIF_APP_FILE_FORMAT_UNKNOWN) {
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
                    nextFile->filename);
            goto cleanup;
        }
        if (image->depth != nextImage->depth) {
            fprintf(stderr, "ERROR: Image sequence depth mismatch, [%u] vs [%u]: %s\n", image->depth, nextImage->depth, nextFile->filename);
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
                    nextFile->filename);
            goto cleanup;
        }
        if (image->yuvRange != nextImage->yuvRange) {
            fprintf(stderr,
                    "ERROR: Image sequence range mismatch, [%s] vs [%s]: %s\n",
                    (image->yuvRange == AVIF_RANGE_FULL) ? "Full" : "Limited",
                    (nextImage->yuvRange == AVIF_RANGE_FULL) ? "Full" : "Limited",
                    nextFile->filename);
            goto cleanup;
        }

        avifResult nextImageResult = avifEncoderAddImage(encoder, nextImage, nextFile->duration, AVIF_ADD_IMAGE_FLAG_NONE);
        if (nextImageResult != AVIF_RESULT_OK) {
            fprintf(stderr, "ERROR: Failed to encode image: %s\n", avifResultToString(nextImageResult));
            goto cleanup;
        }
    }

    avifResult finishResult = avifEncoderFinish(encoder, &raw);
    if (finishResult != AVIF_RESULT_OK) {
        fprintf(stderr, "ERROR: Failed to finish encoding: %s\n", avifResultToString(finishResult));
        goto cleanup;
    }

    printf("Encoded successfully.\n");
    printf(" * Color AV1 total size: " AVIF_FMT_ZU " bytes\n", encoder->ioStats.colorOBUSize);
    printf(" * Alpha AV1 total size: " AVIF_FMT_ZU " bytes\n", encoder->ioStats.alphaOBUSize);
    FILE * f = fopen(outputFilename, "wb");
    if (!f) {
        fprintf(stderr, "ERROR: Failed to open file for write: %s\n", outputFilename);
        goto cleanup;
    }
    if (fwrite(raw.data, 1, raw.size, f) != raw.size) {
        fprintf(stderr, "Failed to write " AVIF_FMT_ZU " bytes: %s\n", raw.size, outputFilename);
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
    free((void *)input.files);
    return returnCode;
}
