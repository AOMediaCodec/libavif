// Copyright 2020 Joe Drago. All rights reserved.
// SPDX-License-Identifier: BSD-2-Clause

#include "avif/avif.h"

#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_DRIFT 10

#define NEXTARG()                                                     \
    if (((argIndex + 1) == argc) || (argv[argIndex + 1][0] == '-')) { \
        fprintf(stderr, "%s requires an argument.", arg);             \
        return 1;                                                     \
    }                                                                 \
    arg = argv[++argIndex]

// avifyuv:
// The goal here isn't to get perfect matches, as some codepoints will drift due to depth rescaling and/or YUV conversion.
// The "Matches"/"NoMatches" is just there as a quick visual confirmation when scanning the results.
// If you choose a more friendly starting color instead of orange (red, perhaps), you get considerably more matches,
// except in the cases where it doesn't make sense (going to RGB/BGR will forget the alpha / make it opaque).

static const char * rgbFormatToString(avifRGBFormat format)
{
    switch (format) {
        case AVIF_RGB_FORMAT_RGB:
            return "RGB ";
        case AVIF_RGB_FORMAT_RGBA:
            return "RGBA";
        case AVIF_RGB_FORMAT_ARGB:
            return "ARGB";
        case AVIF_RGB_FORMAT_BGR:
            return "BGR ";
        case AVIF_RGB_FORMAT_BGRA:
            return "BGRA";
        case AVIF_RGB_FORMAT_ABGR:
            return "ABGR";
    }
    return "Unknown";
}

int main(int argc, char * argv[])
{
    (void)argc;
    (void)argv;

    printf("avif version: %s\n", avifVersion());

    int mode = 0;
    avifBool verbose = AVIF_FALSE;

    int argIndex = 1;
    while (argIndex < argc) {
        const char * arg = argv[argIndex];

        if (!strcmp(arg, "-m") || !strcmp(arg, "--mode")) {
            NEXTARG();
            if (!strcmp(arg, "limited")) {
                mode = 0;
            } else if (!strcmp(arg, "drift")) {
                mode = 1;
            } else if (!strcmp(arg, "rgb")) {
                mode = 2;
            } else {
                mode = atoi(arg);
            }
        } else if (!strcmp(arg, "-v") || !strcmp(arg, "--verbose")) {
            verbose = AVIF_TRUE;
        }

        ++argIndex;
    }

    const uint32_t yuvDepths[] = { 8, 10, 12 };
    const int yuvDepthsCount = (int)(sizeof(yuvDepths) / sizeof(yuvDepths[0]));
    const uint32_t rgbDepths[] = { 8, 10, 12 };
    const int rgbDepthsCount = (int)(sizeof(rgbDepths) / sizeof(rgbDepths[0]));

    if (mode == 0) {
        // Limited to full conversion roundtripping test

        int depth = 8;
        int maxChannel = (1 << depth) - 1;
        for (int i = 0; i <= maxChannel; ++i) {
            int li = avifFullToLimitedY(depth, i);
            int fi = avifLimitedToFullY(depth, li);
            const char * prefix = "x";
            if (i == fi) {
                prefix = ".";
            }
            printf("%s %d -> %d -> %d\n", prefix, i, li, fi);
        }
    } else if (mode == 1) {
        // Calculate maximum codepoint drift on different combinations of depth and matrixCoefficients

        for (int rgbDepthIndex = 0; rgbDepthIndex < rgbDepthsCount; ++rgbDepthIndex) {
            uint32_t rgbDepth = rgbDepths[rgbDepthIndex];
            for (int yuvDepthIndex = 0; yuvDepthIndex < yuvDepthsCount; ++yuvDepthIndex) {
                uint32_t yuvDepth = yuvDepths[yuvDepthIndex];
                if (yuvDepth < rgbDepth) {
                    // skip it
                    continue;
                }

                const avifMatrixCoefficients matrixCoeffsList[] = { AVIF_MATRIX_COEFFICIENTS_BT709,
                                                                    AVIF_MATRIX_COEFFICIENTS_BT601,
                                                                    AVIF_MATRIX_COEFFICIENTS_BT2020_NCL,
                                                                    AVIF_MATRIX_COEFFICIENTS_IDENTITY };
                const int matrixCoeffsCount = (int)(sizeof(matrixCoeffsList) / sizeof(matrixCoeffsList[0]));

                for (int matrixCoeffsIndex = 0; matrixCoeffsIndex < matrixCoeffsCount; ++matrixCoeffsIndex) {
                    avifMatrixCoefficients matrixCoeffs = matrixCoeffsList[matrixCoeffsIndex];

                    int dim = 1 << rgbDepth;
                    int maxDrift = 0;

                    avifImage * image = avifImageCreate(dim, dim, yuvDepth, AVIF_PIXEL_FORMAT_YUV444);
                    image->colorPrimaries = AVIF_COLOR_PRIMARIES_BT709;
                    image->transferCharacteristics = AVIF_TRANSFER_CHARACTERISTICS_SRGB;
                    image->matrixCoefficients = matrixCoeffs;
                    image->yuvRange = AVIF_RANGE_FULL;
                    avifImageAllocatePlanes(image, AVIF_PLANES_YUV);

                    avifRGBImage srcRGB;
                    avifRGBImageSetDefaults(&srcRGB, image);
                    srcRGB.format = AVIF_RGB_FORMAT_RGB;
                    srcRGB.depth = rgbDepth;
                    avifRGBImageAllocatePixels(&srcRGB);

                    avifRGBImage dstRGB;
                    avifRGBImageSetDefaults(&dstRGB, image);
                    dstRGB.format = AVIF_RGB_FORMAT_RGB;
                    dstRGB.depth = rgbDepth;
                    avifRGBImageAllocatePixels(&dstRGB);

                    uint64_t driftPixelCounts[MAX_DRIFT];
                    for (int i = 0; i < MAX_DRIFT; ++i) {
                        driftPixelCounts[i] = 0;
                    }

                    for (int r = 0; r < dim; ++r) {
                        if (verbose) {
                            printf("[%4d/%4d] RGB depth: %d, YUV depth: %d, matrixCoeffs: %d\r", r + 1, dim, rgbDepth, yuvDepth, matrixCoeffs);
                        }

                        for (int g = 0; g < dim; ++g) {
                            uint8_t * row = &srcRGB.pixels[g * srcRGB.rowBytes];
                            for (int b = 0; b < dim; ++b) {
                                if (rgbDepth == 8) {
                                    uint8_t * pixel = &row[b * sizeof(uint8_t) * 3];
                                    pixel[0] = (uint8_t)r;
                                    pixel[1] = (uint8_t)g;
                                    pixel[2] = (uint8_t)b;
                                } else {
                                    uint16_t * pixel = (uint16_t *)&row[b * sizeof(uint16_t) * 3];
                                    pixel[0] = (uint16_t)r;
                                    pixel[1] = (uint16_t)g;
                                    pixel[2] = (uint16_t)b;
                                }
                            }
                        }

                        avifImageRGBToYUV(image, &srcRGB);
                        avifImageYUVToRGB(image, &dstRGB);

                        for (int y = 0; y < dim; ++y) {
                            const uint8_t * srcRow = &srcRGB.pixels[y * srcRGB.rowBytes];
                            const uint8_t * dstRow = &dstRGB.pixels[y * dstRGB.rowBytes];
                            for (int x = 0; x < dim; ++x) {
                                int drift = 0;
                                if (rgbDepth == 8) {
                                    const uint8_t * srcPixel = &srcRow[x * sizeof(uint8_t) * 3];
                                    const uint8_t * dstPixel = &dstRow[x * sizeof(uint8_t) * 3];

                                    const int driftR = abs((int)srcPixel[0] - (int)dstPixel[0]);
                                    if (drift < driftR) {
                                        drift = driftR;
                                    }
                                    const int driftG = abs((int)srcPixel[1] - (int)dstPixel[1]);
                                    if (drift < driftG) {
                                        drift = driftG;
                                    }
                                    const int driftB = abs((int)srcPixel[2] - (int)dstPixel[2]);
                                    if (drift < driftB) {
                                        drift = driftB;
                                    }
                                } else {
                                    const uint16_t * srcPixel = (const uint16_t *)&srcRow[x * sizeof(uint16_t) * 3];
                                    const uint16_t * dstPixel = (const uint16_t *)&dstRow[x * sizeof(uint16_t) * 3];

                                    const int driftR = abs((int)srcPixel[0] - (int)dstPixel[0]);
                                    if (drift < driftR) {
                                        drift = driftR;
                                    }
                                    const int driftG = abs((int)srcPixel[1] - (int)dstPixel[1]);
                                    if (drift < driftG) {
                                        drift = driftG;
                                    }
                                    const int driftB = abs((int)srcPixel[2] - (int)dstPixel[2]);
                                    if (drift < driftB) {
                                        drift = driftB;
                                    }
                                }

                                if (drift < MAX_DRIFT) {
                                    ++driftPixelCounts[drift];
                                    if (maxDrift < drift) {
                                        maxDrift = drift;
                                    }
                                } else {
                                    printf("ERROR: Encountered a drift greater than MAX_DRIFT(%d): %d\n", MAX_DRIFT, drift);
                                    return 1;
                                }
                            }
                        }
                    }

                    if (verbose) {
                        printf("\n");
                    }

                    printf(" * RGB depth: %d, YUV depth: %d, matrixCoeffs: %d, maxDrift: %2d\n", rgbDepth, yuvDepth, matrixCoeffs, maxDrift);

                    const uint64_t totalPixelCount = (uint64_t)dim * dim * dim;
                    for (int i = 0; i < MAX_DRIFT; ++i) {
                        if (verbose && (driftPixelCounts[i] > 0)) {
                            printf("   * drift: %2d -> %12" PRIu64 " / %12" PRIu64 " pixels (%.2f %%)\n",
                                   i,
                                   driftPixelCounts[i],
                                   totalPixelCount,
                                   (double)driftPixelCounts[i] * 100.0 / (double)totalPixelCount);
                        }
                    }

                    avifRGBImageFreePixels(&srcRGB);
                    avifRGBImageFreePixels(&dstRGB);
                    avifImageDestroy(image);
                }
            }
        }

    } else if (mode == 2) {
        // Stress test all RGB depths

        uint32_t originalWidth = 32;
        uint32_t originalHeight = 32;
        avifBool showAllResults = AVIF_TRUE;

        avifImage * image = avifImageCreate(originalWidth, originalHeight, 8, AVIF_PIXEL_FORMAT_YUV444);

        for (int yuvDepthIndex = 0; yuvDepthIndex < yuvDepthsCount; ++yuvDepthIndex) {
            uint32_t yuvDepth = yuvDepths[yuvDepthIndex];

            avifRGBImage srcRGB;
            avifRGBImageSetDefaults(&srcRGB, image);
            srcRGB.depth = yuvDepth;
            avifRGBImageAllocatePixels(&srcRGB);
            if (yuvDepth > 8) {
                float maxChannelF = (float)((1 << yuvDepth) - 1);
                for (uint32_t j = 0; j < srcRGB.height; ++j) {
                    for (uint32_t i = 0; i < srcRGB.width; ++i) {
                        uint16_t * pixel = (uint16_t *)&srcRGB.pixels[(8 * i) + (srcRGB.rowBytes * j)];
                        pixel[0] = (uint16_t)maxChannelF;          // R
                        pixel[1] = (uint16_t)(maxChannelF * 0.5f); // G
                        pixel[2] = 0;                              // B
                        pixel[3] = (uint16_t)(maxChannelF * 0.5f); // A
                    }
                }
            } else {
                for (uint32_t j = 0; j < srcRGB.height; ++j) {
                    for (uint32_t i = 0; i < srcRGB.width; ++i) {
                        uint8_t * pixel = &srcRGB.pixels[(4 * i) + (srcRGB.rowBytes * j)];
                        pixel[0] = 255; // R
                        pixel[1] = 128; // G
                        pixel[2] = 0;   // B
                        pixel[3] = 128; // A
                    }
                }
            }

            uint32_t depths[4] = { 8, 10, 12, 16 };
            for (int depthIndex = 0; depthIndex < 4; ++depthIndex) {
                uint32_t rgbDepth = depths[depthIndex];

                avifRange ranges[2] = { AVIF_RANGE_FULL, AVIF_RANGE_LIMITED };
                for (int rangeIndex = 0; rangeIndex < 2; ++rangeIndex) {
                    avifRange yuvRange = ranges[rangeIndex];

                    avifRGBFormat rgbFormats[6] = { AVIF_RGB_FORMAT_RGB, AVIF_RGB_FORMAT_RGBA, AVIF_RGB_FORMAT_ARGB,
                                                    AVIF_RGB_FORMAT_BGR, AVIF_RGB_FORMAT_BGRA, AVIF_RGB_FORMAT_ABGR };
                    for (int rgbFormatIndex = 0; rgbFormatIndex < 6; ++rgbFormatIndex) {
                        avifRGBFormat rgbFormat = rgbFormats[rgbFormatIndex];

                        // ----------------------------------------------------------------------

                        avifImageFreePlanes(image, AVIF_PLANES_ALL);
                        image->depth = yuvDepth;
                        image->yuvRange = yuvRange;
                        image->alphaRange = yuvRange;
                        avifImageRGBToYUV(image, &srcRGB);

                        avifRGBImage intermediateRGB;
                        avifRGBImageSetDefaults(&intermediateRGB, image);
                        intermediateRGB.depth = rgbDepth;
                        intermediateRGB.format = rgbFormat;
                        avifRGBImageAllocatePixels(&intermediateRGB);
                        avifImageYUVToRGB(image, &intermediateRGB);

                        avifImageFreePlanes(image, AVIF_PLANES_ALL);
                        avifImageRGBToYUV(image, &intermediateRGB);

                        avifRGBImage dstRGB;
                        avifRGBImageSetDefaults(&dstRGB, image);
                        dstRGB.depth = yuvDepth;
                        avifRGBImageAllocatePixels(&dstRGB);
                        avifImageYUVToRGB(image, &dstRGB);

                        avifBool moveOn = AVIF_FALSE;
                        for (uint32_t j = 0; j < originalHeight; ++j) {
                            if (moveOn)
                                break;
                            for (uint32_t i = 0; i < originalWidth; ++i) {
                                if (yuvDepth > 8) {
                                    uint16_t * srcPixel = (uint16_t *)&srcRGB.pixels[(8 * i) + (srcRGB.rowBytes * j)];
                                    uint16_t * dstPixel = (uint16_t *)&dstRGB.pixels[(8 * i) + (dstRGB.rowBytes * j)];
                                    avifBool matches = (memcmp(srcPixel, dstPixel, 8) == 0);
                                    if (showAllResults || !matches) {
                                        printf("yuvDepth:%2d rgbFormat:%s rgbDepth:%2d yuvRange:%7s (%d,%d) [%7s] (%d, %d, %d, %d) -> (%d, %d, %d, %d)\n",
                                               yuvDepth,
                                               rgbFormatToString(rgbFormat),
                                               rgbDepth,
                                               (yuvRange == AVIF_RANGE_LIMITED) ? "Limited" : "Full",
                                               i,
                                               j,
                                               matches ? "Match" : "NoMatch",
                                               srcPixel[0],
                                               srcPixel[1],
                                               srcPixel[2],
                                               srcPixel[3],
                                               dstPixel[0],
                                               dstPixel[1],
                                               dstPixel[2],
                                               dstPixel[3]);
                                        moveOn = AVIF_TRUE;
                                        break;
                                    }
                                } else {
                                    uint8_t * srcPixel = &srcRGB.pixels[(4 * i) + (srcRGB.rowBytes * j)];
                                    uint8_t * dstPixel = &dstRGB.pixels[(4 * i) + (dstRGB.rowBytes * j)];
                                    avifBool matches = (memcmp(srcPixel, dstPixel, 4) == 0);
                                    if (showAllResults || !matches) {
                                        printf("yuvDepth:%2d rgbFormat:%s rgbDepth:%2d yuvRange:%7s (%d,%d) [%7s] (%d, %d, %d, %d) -> (%d, %d, %d, %d)\n",
                                               yuvDepth,
                                               rgbFormatToString(rgbFormat),
                                               rgbDepth,
                                               (yuvRange == AVIF_RANGE_LIMITED) ? "Limited" : "Full",
                                               i,
                                               j,
                                               matches ? "Match" : "NoMatch",
                                               srcPixel[0],
                                               srcPixel[1],
                                               srcPixel[2],
                                               srcPixel[3],
                                               dstPixel[0],
                                               dstPixel[1],
                                               dstPixel[2],
                                               dstPixel[3]);
                                        moveOn = AVIF_TRUE;
                                        break;
                                    }
                                }
                            }
                        }

                        avifRGBImageFreePixels(&intermediateRGB);
                        avifRGBImageFreePixels(&dstRGB);

                        // ----------------------------------------------------------------------
                    }
                }
            }

            avifRGBImageFreePixels(&srcRGB);
        }
        avifImageDestroy(image);
    }
    return 0;
}
