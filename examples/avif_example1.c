// Copyright 2019 Joe Drago. All rights reserved.
// SPDX-License-Identifier: BSD-2-Clause

#include "avif/avif.h"

#include <stdio.h>
#include <string.h>

int main(int argc, char * argv[])
{
    (void)argc;
    (void)argv;
    int exitStatus = 0;

    printf("avif version: %s\n", avifVersion());

#if 1
    int width = 32;
    int height = 32;
    int depth = 8;

    // Encode an orange, 8-bit, full opacity image
    avifImage * image = avifImageCreate(width, height, depth, AVIF_PIXEL_FORMAT_YUV444);

    avifRGBImage srcRGB;
    avifRGBImageSetDefaults(&srcRGB, image);
    avifRGBImageAllocatePixels(&srcRGB);

    avifRGBImage dstRGB;
    avifRGBImageSetDefaults(&dstRGB, image);
    avifRGBImageAllocatePixels(&dstRGB);

    for (int j = 0; j < height; ++j) {
        for (int i = 0; i < width; ++i) {
            uint8_t * pixel = &srcRGB.pixels[(4 * i) + (srcRGB.rowBytes * j)];
            pixel[0] = 255; // R
            pixel[1] = 128; // G
            pixel[2] = 0;   // B
            pixel[3] = 255; // A
        }
    }
    avifImageRGBToYUV(image, &srcRGB);

    // uint8_t * fakeICC = "abcdefg";
    // uint32_t fakeICCSize = (uint32_t)strlen(fakeICC);
    // apgImageSetICC(image, fakeICC, fakeICCSize);

    avifRWData raw = AVIF_DATA_EMPTY;
    avifEncoder * encoder = avifEncoderCreate();
    encoder->maxThreads = 1;
    encoder->minQuantizer = AVIF_QUANTIZER_BEST_QUALITY;
    encoder->maxQuantizer = AVIF_QUANTIZER_BEST_QUALITY;
    avifResult res = avifEncoderWrite(encoder, image, &raw);
    avifEncoderDestroy(encoder);

#if 0
    // debug
    {
        FILE * f = fopen("out.avif", "wb");
        if (f) {
            fwrite(raw.data, 1, raw.size, f);
            fclose(f);
        }
    }
#endif

    if (res != AVIF_RESULT_OK) {
        exitStatus = 1;
        goto encodeCleanup;
    }

    // Decode it
    avifImage * decoded = avifImageCreateEmpty();
    avifDecoder * decoder = avifDecoderCreate();
    avifResult decodeResult = avifDecoderRead(decoder, decoded, (avifROData *)&raw);
    avifDecoderDestroy(decoder);

    if (decodeResult != AVIF_RESULT_OK) {
        exitStatus = 1;
        goto decodeCleanup;
    }
    if (avifImageYUVToRGB(decoded, &dstRGB) != AVIF_RESULT_OK) {
        exitStatus = 1;
        goto decodeCleanup;
    }
    for (int j = 0; j < height; ++j) {
        for (int i = 0; i < width; ++i) {
            for (int plane = 0; plane < 3; ++plane) {
                uint8_t * srcPixel = &srcRGB.pixels[(4 * i) + (srcRGB.rowBytes * j)];
                uint8_t * dstPixel = &dstRGB.pixels[(4 * i) + (dstRGB.rowBytes * j)];
                if (memcmp(srcPixel, dstPixel, 4) != 0) {
                    printf("(%d,%d)   (%d, %d, %d, %d) != (%d, %d, %d, %d)\n",
                           i,
                           j,
                           srcPixel[0],
                           srcPixel[1],
                           srcPixel[2],
                           srcPixel[3],
                           dstPixel[0],
                           dstPixel[1],
                           dstPixel[2],
                           dstPixel[3]);
                    exitStatus = 1;
                }
            }
        }
    }

decodeCleanup:
    avifImageDestroy(decoded);
    avifRWDataFree(&raw);

encodeCleanup:
    avifImageDestroy(image);
    avifRGBImageFreePixels(&srcRGB);
    avifRGBImageFreePixels(&dstRGB);
#else  /* if 1 */

    FILE * f = fopen("test.avif", "rb");
    if (!f)
        return 1;

    fseek(f, 0, SEEK_END);
    uint32_t size = ftell(f);
    fseek(f, 0, SEEK_SET);
    avifRWData raw = AVIF_DATA_EMPTY;
    avifRWDataRealloc(&raw, size);
    fread(raw.data, 1, size, f);
    fclose(f);

    avifImage * decoded = avifImageCreate();
    avifResult decodeResult = avifImageRead(decoded, &raw);
    avifRWDataFree(&raw);
#endif /* if 1 */
    return exitStatus;
}
