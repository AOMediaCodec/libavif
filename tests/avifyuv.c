// Copyright 2020 Joe Drago. All rights reserved.
// SPDX-License-Identifier: BSD-2-Clause

#include "avif/avif.h"

#include <stdio.h>
#include <string.h>

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

#if 0
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
#else

    uint32_t originalWidth = 32;
    uint32_t originalHeight = 32;
    avifBool showAllResults = AVIF_TRUE;

    avifImage * image = avifImageCreate(originalWidth, originalHeight, 8, AVIF_PIXEL_FORMAT_YUV444);

    uint32_t yuvDepths[3] = { 8, 10, 12 };
    for (int yuvDepthIndex = 0; yuvDepthIndex < 3; ++yuvDepthIndex) {
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
#endif
    return 0;
}
