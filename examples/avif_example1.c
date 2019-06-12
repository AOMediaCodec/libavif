// Copyright 2019 Joe Drago. All rights reserved.
// SPDX-License-Identifier: BSD-2-Clause

#include "avif/avif.h"

#include <string.h>
#include <stdio.h>

int main(int argc, char * argv[])
{
    (void)argc;
    (void)argv;

    printf("avif version: %s\n", avifVersion());

#if 1
    int width = 32;
    int height = 32;
    int depth = 8;

    // Encode an orange, 8-bit, full opacity image
    avifImage * image = avifImageCreate(width, height, depth, AVIF_PIXEL_FORMAT_YUV444);
    avifImageAllocatePlanes(image, AVIF_PLANES_RGB | AVIF_PLANES_A);
    for (int j = 0; j < height; ++j) {
        for (int i = 0; i < width; ++i) {
            image->rgbPlanes[0][i + (j * image->rgbRowBytes[0])] = 255; // R
            image->rgbPlanes[1][i + (j * image->rgbRowBytes[1])] = 128; // G
            image->rgbPlanes[2][i + (j * image->rgbRowBytes[2])] = 0;   // B
            image->alphaPlane[i + (j * image->alphaRowBytes)] = 255;    // A
        }
    }

    // uint8_t * fakeICC = "abcdefg";
    // uint32_t fakeICCSize = (uint32_t)strlen(fakeICC);
    // apgImageSetICC(image, fakeICC, fakeICCSize);

    avifRawData raw = AVIF_RAW_DATA_EMPTY;
    avifEncoder * encoder = avifEncoderCreate();
    encoder->maxThreads = 1;
    encoder->quality = AVIF_BEST_QUALITY;
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

    if (res == AVIF_RESULT_OK) {
        // Decode it
        avifImage * decoded = avifImageCreateEmpty();
        avifDecoder * decoder = avifDecoderCreate();
        avifResult decodeResult = avifDecoderRead(decoder, decoded, &raw);
        avifDecoderDestroy(decoder);

        if (decodeResult == AVIF_RESULT_OK) {
            avifImageYUVToRGB(decoded);
            for (int j = 0; j < height; ++j) {
                for (int i = 0; i < width; ++i) {
                    for (int plane = 0; plane < 3; ++plane) {
                        uint32_t src = image->rgbPlanes[plane][i + (j * image->rgbRowBytes[plane])];
                        uint32_t dst = decoded->rgbPlanes[plane][i + (j * decoded->rgbRowBytes[plane])];
                        if (src != dst) {
                            printf("(%d,%d,p%d)   %d != %d\n", i, j, plane, src, dst);
                        }
                    }
                }
            }
        }

        avifImageDestroy(decoded);
    }

    avifImageDestroy(image);
#else /* if 1 */

    FILE * f = fopen("test.avif", "rb");
    if (!f)
        return 0;

    fseek(f, 0, SEEK_END);
    uint32_t size = ftell(f);
    fseek(f, 0, SEEK_SET);
    avifRawData raw = AVIF_RAW_DATA_EMPTY;
    avifRawDataRealloc(&raw, size);
    fread(raw.data, 1, size, f);
    fclose(f);

    avifImage * decoded = avifImageCreate();
    avifResult decodeResult = avifImageRead(decoded, &raw);
    avifRawDataFree(&raw);
#endif /* if 1 */
    return 0;
}
