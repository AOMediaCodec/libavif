// Copyright 2019 Joe Drago. All rights reserved.
// SPDX-License-Identifier: BSD-2-Clause

#include "avif/avif.h"

#include <string.h>
#include <stdio.h>

int main(int argc, char * argv[])
{
#if 1
    int width = 32;
    int height = 32;
    int depth = 12;

    // Encode an orange, 8-bit, full opacity image
    avifImage * image = avifImageCreate();
    avifImageCreatePixels(image, AVIF_PIXEL_FORMAT_RGBA, width, height, depth);
    for (int j = 0; j < height; ++j) {
        for (int i = 0; i < width; ++i) {
            image->planes[0][i + (j * image->strides[0])] = 4095; // R
            image->planes[1][i + (j * image->strides[1])] = 2000; // G
            image->planes[2][i + (j * image->strides[2])] = 0;    // B
            image->planes[3][i + (j * image->strides[3])] = 4095; // A
        }
    }

    // uint8_t * fakeICC = "abcdefg";
    // uint32_t fakeICCSize = (uint32_t)strlen(fakeICC);
    // apgImageSetICC(image, fakeICC, fakeICCSize);

    avifRawData raw = AVIF_RAW_DATA_EMPTY;
    avifResult res = avifImageWrite(image, &raw, 50);

    // debug
    {
        FILE * f = fopen("out.avif", "wb");
        if (f) {
            fwrite(raw.data, 1, raw.size, f);
            fclose(f);
        }
    }

    if (res == AVIF_RESULT_OK) {
        // Decode it
        avifImage * decoded = avifImageCreate();
        avifResult decodeResult = avifImageRead(decoded, &raw);
        if (decodeResult == AVIF_RESULT_OK) {
            avifImage * rgbImage = avifImageCreate();
            avifResult reformatResult = avifImageReformatPixels(decoded, rgbImage, AVIF_PIXEL_FORMAT_RGBA, depth);
            if (reformatResult == AVIF_RESULT_OK) {
                for (int j = 0; j < height; ++j) {
                    for (int i = 0; i < width; ++i) {
                        for (int plane = 0; plane < 3; ++plane) {
                            uint32_t src = image->planes[plane][i + (j * image->strides[plane])];
                            uint32_t dst = rgbImage->planes[plane][i + (j * rgbImage->strides[plane])];
                            if (src != dst) {
                                printf("(%d,%d,p%d)   %d != %d\n", i, j, plane, src, dst);
                            }
                        }
                    }
                }
            }
            avifImageDestroy(rgbImage);
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
