// Copyright 2019 Joe Drago. All rights reserved.
// SPDX-License-Identifier: BSD-2-Clause

#include "avif/avif.h"

extern "C" int LLVMFuzzerTestOneInput(const uint8_t * Data, size_t Size)
{
    static avifRGBFormat rgbFormats[] = { AVIF_RGB_FORMAT_RGB, AVIF_RGB_FORMAT_RGBA };
    static size_t rgbFormatsCount = sizeof(rgbFormats) / sizeof(rgbFormats[0]);

    static avifChromaUpsampling upsamplings[] = { AVIF_CHROMA_UPSAMPLING_BILINEAR, AVIF_CHROMA_UPSAMPLING_NEAREST };
    static size_t upsamplingsCount = sizeof(upsamplings) / sizeof(upsamplings[0]);

    static uint32_t rgbDepths[] = { 8, 10 };
    static size_t rgbDepthsCount = sizeof(rgbDepths) / sizeof(rgbDepths[0]);

    static uint32_t yuvDepths[] = { 8, 10 };
    static size_t yuvDepthsCount = sizeof(yuvDepths) / sizeof(yuvDepths[0]);

    avifDecoder * decoder = avifDecoderCreate();
    avifResult result = avifDecoderSetIOMemory(decoder, Data, Size);
    if (result == AVIF_RESULT_OK) {
        result = avifDecoderParse(decoder);
    }
    if (result == AVIF_RESULT_OK) {
        for (int loop = 0; loop < 2; ++loop) {
            while (avifDecoderNextImage(decoder) == AVIF_RESULT_OK) {
                avifRGBImage rgb;
                avifRGBImageSetDefaults(&rgb, decoder->image);

                for (size_t rgbFormatsIndex = 0; rgbFormatsIndex < rgbFormatsCount; ++rgbFormatsIndex) {
                    for (size_t upsamplingsIndex = 0; upsamplingsIndex < upsamplingsCount; ++upsamplingsIndex) {
                        for (size_t rgbDepthsIndex = 0; rgbDepthsIndex < rgbDepthsCount; ++rgbDepthsIndex) {
                            // Convert to RGB
                            rgb.format = rgbFormats[rgbFormatsIndex];
                            rgb.depth = rgbDepths[rgbDepthsIndex];
                            rgb.chromaUpsampling = upsamplings[upsamplingsIndex];
                            avifRGBImageAllocatePixels(&rgb);
                            avifResult rgbResult = avifImageYUVToRGB(decoder->image, &rgb);
                            if (rgbResult == AVIF_RESULT_OK) {
                                for (size_t yuvDepthsIndex = 0; yuvDepthsIndex < yuvDepthsCount; ++yuvDepthsIndex) {
                                    // ... and back to YUV
                                    avifImage * tempImage = avifImageCreate(
                                        decoder->image->width, decoder->image->height, yuvDepths[yuvDepthsIndex], decoder->image->yuvFormat);
                                    avifResult yuvResult = avifImageRGBToYUV(tempImage, &rgb);
                                    if (yuvResult != AVIF_RESULT_OK) {
                                    }
                                    avifImageDestroy(tempImage);
                                }
                            }

                            avifRGBImageFreePixels(&rgb);
                        }
                    }
                }
            }

            if (loop != 1) {
                result = avifDecoderReset(decoder);
                if (result == AVIF_RESULT_OK) {
                } else {
                    break;
                }
            }
        }
    }

    avifDecoderDestroy(decoder);
    return 0; // Non-zero return values are reserved for future use.
}
