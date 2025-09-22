// Copyright 2019 Joe Drago. All rights reserved.
// SPDX-License-Identifier: BSD-2-Clause

#include "avif/avif.h"

#include "avifjpeg.h"
#include "avifpng.h"
#include "avifutil.h"
#include "y4m.h"

#include <assert.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define DEFAULT_JPEG_QUALITY 90
#define DECODE_ALL_FRAMES -1

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
    printf("    -j,--jobs J       : Number of jobs (worker threads), or 'all' to potentially use as many cores as possible. (Default: all)\n");
    printf("    -c,--codec C      : Codec to use (choose from versions list below)\n");
    printf("    -d,--depth D      : Output depth, either 8 or 16. (PNG only; For y4m, depth is retained, and JPEG is always 8bpc)\n");
    printf("    -q,--quality Q    : Output quality in 0..100. (JPEG only, default: %d)\n", DEFAULT_JPEG_QUALITY);
    printf("    --png-compress L  : PNG compression level in 0..9 (PNG only; 0=none, 9=max). Defaults to libpng's builtin default\n");
    printf("    -u,--upsampling U : Chroma upsampling (for 420/422). One of 'automatic' (default), 'fastest', 'best', 'nearest', or 'bilinear'\n");
    printf("    -r,--raw-color    : Output raw RGB values instead of multiplying by alpha when saving to opaque formats\n");
    printf("                        (JPEG only; not applicable to y4m)\n");
    printf("    --index I         : When decoding an image sequence or progressive image, specify which frame index to decode, where the first frame has index 0, or 'all' to decode all frames. (Default: 0)\n");
    printf("    --progressive     : Enable progressive AVIF processing. If a progressive image is encountered and --progressive is passed,\n");
    printf("                        avifdec will use --index to choose which layer to decode (in progressive order).\n");
    printf("    --no-strict       : Disable strict decoding, which disables strict validation checks and errors\n");
    printf("    -i,--info         : Decode all frames and display all image information instead of saving to disk\n");
    printf("    --icc FILENAME    : Provide an ICC profile payload (implies --ignore-icc)\n");
    printf("    --ignore-icc      : If the input file contains an embedded ICC profile, ignore it (no-op if absent)\n");
    printf("    --size-limit C    : Maximum image size (in total pixels) that should be tolerated. (Default: %u)\n",
           AVIF_DEFAULT_IMAGE_SIZE_LIMIT);
    printf("  --dimension-limit C : Maximum image dimension (width or height) that should be tolerated.\n");
    printf("                        Set to 0 to ignore. (Default: %u)\n", AVIF_DEFAULT_IMAGE_DIMENSION_LIMIT);
    printf("    --                : Signal the end of options. Everything after this is interpreted as file names.\n");
    printf("\n");
    avifPrintVersions();
}

avifBool avifWriteToFile(avifAppFileFormat outputFormat,
                         const char * outputFilename,
                         avifImage * image,
                         avifBool rawColor,
                         int jpegQuality,
                         int pngCompressionLevel,
                         int requestedDepth,
                         int chromaUpsampling)
{
    if (outputFormat == AVIF_APP_FILE_FORMAT_Y4M) {
        if (image->icc.size || image->exif.size || image->xmp.size) {
            fprintf(stderr, "Warning: metadata dropped when saving to y4m.\n");
        }
        return y4mWrite(outputFilename, image);
    } else if (outputFormat == AVIF_APP_FILE_FORMAT_JPEG) {
        // Bypass alpha multiply step during conversion
        if (rawColor) {
            image->alphaPremultiplied = AVIF_TRUE;
        }
        return avifJPEGWrite(outputFilename, image, jpegQuality, chromaUpsampling);
    } else if (outputFormat == AVIF_APP_FILE_FORMAT_PNG) {
        return avifPNGWrite(outputFilename, image, requestedDepth, chromaUpsampling, pngCompressionLevel);
    } else {
        fprintf(stderr, "Unsupported output file extension: %s\n", outputFilename);
        return AVIF_FALSE;
    }
}

int main(int argc, char * argv[])
{
    const char * inputFilename = NULL;
    const char * outputFilename = NULL;
    int requestedDepth = 0;
    int jobs = -1;
    int jpegQuality = DEFAULT_JPEG_QUALITY;
    int pngCompressionLevel = -1; // -1 is a sentinel to avifPNGWrite() to skip calling png_set_compression_level()
    avifCodecChoice codecChoice = AVIF_CODEC_CHOICE_AUTO;
    avifBool infoOnly = AVIF_FALSE;
    avifChromaUpsampling chromaUpsampling = AVIF_CHROMA_UPSAMPLING_AUTOMATIC;
    const char * iccOverrideFilename = NULL;
    avifBool ignoreICC = AVIF_FALSE;
    avifBool rawColor = AVIF_FALSE;
    avifBool allowProgressive = AVIF_FALSE;
    avifStrictFlags strictFlags = AVIF_STRICT_ENABLED;
    int frameIndex = 0;                        // Decode the first frame by default.
    avifBool frameIndexSpecified = AVIF_FALSE; // Whether the --index flag was passed.
    uint32_t imageSizeLimit = AVIF_DEFAULT_IMAGE_SIZE_LIMIT;
    uint32_t imageDimensionLimit = AVIF_DEFAULT_IMAGE_DIMENSION_LIMIT;
    avifRWData iccOverride = AVIF_DATA_EMPTY;

    if (argc < 2) {
        syntax();
        return 1;
    }

    int argIndex = 1;
    while (argIndex < argc) {
        const char * arg = argv[argIndex];

        if (!strcmp(arg, "--")) {
            // Stop parsing flags, everything after this is positional arguments
            ++argIndex;
            // Parse additional positional arguments if any.
            while (argIndex < argc) {
                arg = argv[argIndex];
                if (!inputFilename) {
                    inputFilename = arg;
                } else if (!outputFilename) {
                    outputFilename = arg;
                } else {
                    fprintf(stderr, "Too many positional arguments: %s\n\n", arg);
                    syntax();
                    return 1;
                }
                ++argIndex;
            }
            break;
        } else if (!strcmp(arg, "-h") || !strcmp(arg, "--help")) {
            syntax();
            return 0;
        } else if (!strcmp(arg, "-V") || !strcmp(arg, "--version")) {
            avifPrintVersions();
            return 0;
        } else if (!strcmp(arg, "-j") || !strcmp(arg, "--jobs")) {
            NEXTARG();
            if (!strcmp(arg, "all")) {
                jobs = avifQueryCPUCount();
            } else {
                jobs = atoi(arg);
                if (jobs < 1) {
                    jobs = 1;
                }
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
                    fprintf(stderr, "ERROR: Codec cannot decode: %s\n", arg);
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
        } else if (!strcmp(arg, "--png-compress")) {
            NEXTARG();
            pngCompressionLevel = atoi(arg);
            if (pngCompressionLevel < 0) {
                pngCompressionLevel = 0;
            } else if (pngCompressionLevel > 9) {
                pngCompressionLevel = 9;
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
        } else if (!strcmp(arg, "--progressive")) {
            allowProgressive = AVIF_TRUE;
        } else if (!strcmp(arg, "--index")) {
            NEXTARG();
            if (!strcmp(arg, "all")) {
                frameIndex = DECODE_ALL_FRAMES;
            } else {
                frameIndex = (uint32_t)atoi(arg);
            }
            frameIndexSpecified = AVIF_TRUE;
        } else if (!strcmp(arg, "--no-strict")) {
            strictFlags = AVIF_STRICT_DISABLED;
        } else if (!strcmp(arg, "-i") || !strcmp(arg, "--info")) {
            infoOnly = AVIF_TRUE;
        } else if (!strcmp(arg, "--icc")) {
            NEXTARG();
            iccOverrideFilename = arg;
            ignoreICC = AVIF_TRUE;
        } else if (!strcmp(arg, "--ignore-icc")) {
            ignoreICC = AVIF_TRUE;
        } else if (!strcmp(arg, "--size-limit")) {
            NEXTARG();
            unsigned long value = strtoul(arg, NULL, 10);
            if ((value > AVIF_DEFAULT_IMAGE_SIZE_LIMIT) || (value == 0)) {
                fprintf(stderr, "ERROR: invalid image size limit: %s\n", arg);
                return 1;
            }
            imageSizeLimit = (uint32_t)value;
        } else if (!strcmp(arg, "--dimension-limit")) {
            NEXTARG();
            unsigned long value = strtoul(arg, NULL, 10);
            if (value > UINT32_MAX) {
                fprintf(stderr, "ERROR: invalid image dimension limit: %s\n", arg);
                return 1;
            }
            imageDimensionLimit = (uint32_t)value;
        } else if (arg[0] == '-') {
            fprintf(stderr, "ERROR: unrecognized option %s\n\n", arg);
            syntax();
            return 1;
        } else {
            // Positional argument
            if (!inputFilename) {
                inputFilename = arg;
            } else if (!outputFilename) {
                outputFilename = arg;
            } else {
                fprintf(stderr, "Too many positional arguments: %s\n\n", arg);
                syntax();
                return 1;
            }
        }

        ++argIndex;
    }

    if (jobs == -1) {
        jobs = avifQueryCPUCount();
    }

    if (!inputFilename) {
        syntax();
        return 1;
    }

    if (!inputFilename) {
        fprintf(stderr, "Missing input filename\n");
        syntax();
        return 1;
    }

    avifAppFileFormat outputFormat = AVIF_APP_FILE_FORMAT_UNKNOWN;
    if (infoOnly) {
        if (outputFilename) {
            fprintf(stderr, "ERROR: info requested (-i or --info) but output filename also provided (%s)\n", outputFilename);
            syntax();
            return 1;
        }
    } else {
        if (!outputFilename) {
            fprintf(stderr, "Missing output filename\n");
            syntax();
            return 1;
        }
        outputFormat = avifGuessFileFormat(outputFilename);
        if (outputFormat == AVIF_APP_FILE_FORMAT_UNKNOWN) {
            fprintf(stderr, "Cannot determine output file extension: %s\n", outputFilename);
            return 1;
        }
    }

    printf("Decoding with codec '%s' (%d worker thread%s), please wait...\n",
           avifCodecName(codecChoice, AVIF_CODEC_FLAG_CAN_DECODE),
           jobs,
           (jobs == 1) ? "" : "s");

    // ------ After this point, use 'goto cleanup;' in case of failure ------
    int returnCode = 1;
    avifDecoder * decoder = avifDecoderCreate();
    if (!decoder) {
        fprintf(stderr, "Memory allocation failure\n");
        goto cleanup;
    }
    decoder->maxThreads = jobs;
    decoder->codecChoice = codecChoice;
    decoder->imageSizeLimit = imageSizeLimit;
    decoder->imageDimensionLimit = imageDimensionLimit;
    decoder->strictFlags = strictFlags;
    decoder->allowProgressive = allowProgressive;
    if (infoOnly) {
        decoder->imageContentToDecode = AVIF_IMAGE_CONTENT_ALL;
    }

    avifResult result = avifDecoderSetIOFile(decoder, inputFilename);
    if (result != AVIF_RESULT_OK) {
        fprintf(stderr, "Cannot open file for read: %s\n", inputFilename);
        goto cleanup;
    }

    result = avifDecoderParse(decoder);
    if (result != AVIF_RESULT_OK) {
        fprintf(stderr, "ERROR: Failed to parse image: %s\n", avifResultToString(result));
        goto cleanup;
    }

    printf("Image decoded: %s\n", inputFilename);
    avifContainerDump(decoder);

    const avifBool isSequence = decoder->imageCount > 1;
    printf(" * %" PRIu64 " timescales per second, %2.2f seconds (%" PRIu64 " timescales), %d frame%s\n",
           decoder->timescale,
           decoder->duration,
           decoder->durationInTimescales,
           decoder->imageCount,
           (decoder->imageCount == 1) ? "" : "s");
    if (isSequence) {
        printf(" * %s Frames: (%u expected frames)\n",
               (decoder->progressiveState != AVIF_PROGRESSIVE_STATE_UNAVAILABLE) ? "Progressive Image" : "Image Sequence",
               decoder->imageCount);
    } else {
        printf(" * Frame:\n");
    }

    if (iccOverrideFilename) {
        if (!avifReadEntireFile(iccOverrideFilename, &iccOverride)) {
            fprintf(stderr, "ERROR: Unable to read ICC: %s\n", iccOverrideFilename);
            avifRWDataFree(&iccOverride);
            goto cleanup;
        }
    }

    if (infoOnly && !frameIndexSpecified) {
        frameIndex = DECODE_ALL_FRAMES; // Decode all frames by default in 'info only' mode.
    }

    const avifBool decodeAllFrames = frameIndex == DECODE_ALL_FRAMES;
    int currIndex = decodeAllFrames ? 0 : frameIndex;
    while (AVIF_TRUE) {
        result = decodeAllFrames ? avifDecoderNextImage(decoder) : avifDecoderNthImage(decoder, frameIndex);
        if (result != AVIF_RESULT_OK) {
            break;
        }

        printf("   * Decoded frame [%d] [pts %2.2f (%" PRIu64 " timescales)] [duration %2.2f (%" PRIu64 " timescales)] [%ux%u]\n",
               currIndex,
               decoder->imageTiming.pts,
               decoder->imageTiming.ptsInTimescales,
               decoder->imageTiming.duration,
               decoder->imageTiming.durationInTimescales,
               decoder->image->width,
               decoder->image->height);
        if (infoOnly) {
            ++currIndex;
            if (decodeAllFrames) {
                continue;
            } else {
                break;
            }
        }

        if (decoder->image->transformFlags & AVIF_TRANSFORM_CLAP) {
            avifCropRect cropRect;
            if (!avifCropRectFromCleanApertureBox(&cropRect,
                                                  &decoder->image->clap,
                                                  decoder->image->width,
                                                  decoder->image->height,
                                                  &decoder->diag)) {
                // Should happen only if AVIF_STRICT_CLAP_VALID is disabled.
                fprintf(stderr, "Warning: Invalid Clean Aperture values\n");
            }
        }

        if (ignoreICC && (decoder->image->icc.size > 0)) {
            printf("[--ignore-icc] Discarding ICC profile.\n");
            // This cannot fail.
            result = avifImageSetProfileICC(decoder->image, NULL, 0);
            assert(result == AVIF_RESULT_OK);
        }

        if (iccOverrideFilename) {
            printf("[--icc] Setting ICC profile: %s\n", iccOverrideFilename);
            result = avifImageSetProfileICC(decoder->image, iccOverride.data, iccOverride.size);
            if (result != AVIF_RESULT_OK) {
                fprintf(stderr, "ERROR: Failed to set ICC: %s\n", avifResultToString(result));
                goto cleanup;
            }
        }

        if (decodeAllFrames) {
            // Create filename for individual frames, in the form path/to/output-0000000000.ext
            char * lastDot = strrchr(outputFilename, '.');
            const size_t dotPos = (lastDot != NULL) ? (size_t)(lastDot - outputFilename) : strlen(outputFilename);
            const char * extension = (lastDot != NULL) ? lastDot + 1 : "";
            const int maxFilenameWithoutExtensionLength = 1000;
            const int maxExtensionLength = 10;
            char frameFilename[1024];
            int res = snprintf(frameFilename,
                               sizeof(frameFilename),
                               "%.*s-%010d.%.*s",
                               ((int)dotPos > maxFilenameWithoutExtensionLength ? maxFilenameWithoutExtensionLength : (int)dotPos),
                               outputFilename,
                               currIndex,
                               maxExtensionLength,
                               extension);
            if (res < 0) {
                fprintf(stderr, "ERROR: Unable to generate output filename\n");
                goto cleanup;
            }
            if (!avifWriteToFile(outputFormat, frameFilename, decoder->image, rawColor, jpegQuality, pngCompressionLevel, requestedDepth, chromaUpsampling)) {
                goto cleanup;
            }
        } else {
            if (!avifWriteToFile(outputFormat, outputFilename, decoder->image, rawColor, jpegQuality, pngCompressionLevel, requestedDepth, chromaUpsampling)) {
                goto cleanup;
            }
            if (isSequence && !frameIndexSpecified) {
                fprintf(stderr,
                        "INFO: Decoded the first frame of an image sequence with %d frames. To output all frames, use --index all. To silence this message, use --index 0.\n",
                        decoder->imageCount);
            }
            break;
        }
        ++currIndex;
    }

    if (result == AVIF_RESULT_NO_IMAGES_REMAINING) {
        if (decodeAllFrames) {
            result = AVIF_RESULT_OK;
        } else {
            fprintf(stderr,
                    "ERROR: Frame at index %d requested but the file does not contain enough frames (signalled frame count: %d)\n",
                    frameIndex,
                    decoder->imageCount);
            goto cleanup;
        }
    }
    if (result != AVIF_RESULT_OK) {
        fprintf(stderr, "ERROR: Failed to decode %s: %s\n", isSequence ? "frame" : "image", avifResultToString(result));
        goto cleanup;
    }

    returnCode = 0;

cleanup:
    if (decoder != NULL) {
        if (returnCode != 0) {
            avifDumpDiagnostics(&decoder->diag);
        }
        avifDecoderDestroy(decoder);
    }
    avifRWDataFree(&iccOverride);
    return returnCode;
}
