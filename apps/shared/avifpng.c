// Copyright 2020 Joe Drago. All rights reserved.
// SPDX-License-Identifier: BSD-2-Clause

#include "avifpng.h"

#include "png.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// This warning triggers false postives way too often in here.
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic ignored "-Wclobbered"
#endif

avifBool avifPNGRead(avifImage * avif, const char * inputFilename, avifPixelFormat requestedFormat, int requestedDepth)
{
    avifBool readResult = AVIF_FALSE;
    png_structp png = NULL;
    png_infop info = NULL;
    png_bytep * rowPointers = NULL;

    avifRGBImage rgb;
    memset(&rgb, 0, sizeof(avifRGBImage));

    FILE * f = fopen(inputFilename, "rb");
    if (!f) {
        fprintf(stderr, "Can't open PNG file for read: %s\n", inputFilename);
        goto cleanup;
    }

    uint8_t header[8];
    size_t bytesRead = fread(header, 1, 8, f);
    if (bytesRead != 8) {
        fprintf(stderr, "Can't read PNG header: %s\n", inputFilename);
        goto cleanup;
    }
    if (png_sig_cmp(header, 0, 8)) {
        fprintf(stderr, "Not a PNG: %s\n", inputFilename);
        goto cleanup;
    }

    png = png_create_read_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
    if (!png) {
        fprintf(stderr, "Cannot init libpng (png): %s\n", inputFilename);
        goto cleanup;
    }
    info = png_create_info_struct(png);
    if (!info) {
        fprintf(stderr, "Cannot init libpng (info): %s\n", inputFilename);
        goto cleanup;
    }

    if (setjmp(png_jmpbuf(png))) {
        fprintf(stderr, "Error reading PNG: %s\n", inputFilename);
        goto cleanup;
    }

    png_init_io(png, f);
    png_set_sig_bytes(png, 8);
    png_read_info(png, info);

    char * iccpProfileName = NULL;
    int iccpCompression = 0;
    unsigned char * iccpData = NULL;
    png_uint_32 iccpDataLen = 0;
    if (png_get_iCCP(png, info, &iccpProfileName, &iccpCompression, &iccpData, &iccpDataLen) == PNG_INFO_iCCP) {
        if (avif->profileFormat == AVIF_PROFILE_FORMAT_NONE) {
            avifImageSetProfileICC(avif, iccpData, iccpDataLen);
        } else {
            fprintf(stderr, "WARNING: PNG contains ICC profile which is being overridden with --nclx\n");
        }
    }

    int rawWidth = png_get_image_width(png, info);
    int rawHeight = png_get_image_height(png, info);
    png_byte rawColorType = png_get_color_type(png, info);
    png_byte rawBitDepth = png_get_bit_depth(png, info);

    if (rawColorType == PNG_COLOR_TYPE_PALETTE) {
        png_set_palette_to_rgb(png);
    }

    if ((rawColorType == PNG_COLOR_TYPE_GRAY) && (rawBitDepth < 8)) {
        png_set_expand_gray_1_2_4_to_8(png);
    }

    if (png_get_valid(png, info, PNG_INFO_tRNS)) {
        png_set_tRNS_to_alpha(png);
    }

    if ((rawColorType == PNG_COLOR_TYPE_RGB) || (rawColorType == PNG_COLOR_TYPE_GRAY) || (rawColorType == PNG_COLOR_TYPE_PALETTE)) {
        png_set_filler(png, 0xFFFF, PNG_FILLER_AFTER);
    }

    if ((rawColorType == PNG_COLOR_TYPE_GRAY) || (rawColorType == PNG_COLOR_TYPE_GRAY_ALPHA)) {
        png_set_gray_to_rgb(png);
    }

    int imgBitDepth = 8;
    if (rawBitDepth == 16) {
        png_set_swap(png);
        imgBitDepth = 16;
    }

    png_read_update_info(png, info);

    avif->width = rawWidth;
    avif->height = rawHeight;
    avif->yuvFormat = requestedFormat;
    avif->depth = requestedDepth;
    if (avif->depth == 0) {
        if (imgBitDepth == 8) {
            avif->depth = 8;
        } else {
            avif->depth = 12;
        }
    }

    avifRGBImageSetDefaults(&rgb, avif);
    rgb.depth = imgBitDepth;
    avifRGBImageAllocatePixels(&rgb);
    rowPointers = (png_bytep *)malloc(sizeof(png_bytep) * rgb.height);
    for (uint32_t y = 0; y < rgb.height; ++y) {
        rowPointers[y] = &rgb.pixels[y * rgb.rowBytes];
    }
    png_read_image(png, rowPointers);
    avifImageRGBToYUV(avif, &rgb);
    readResult = AVIF_TRUE;

cleanup:
    if (f) {
        fclose(f);
    }
    if (png) {
        png_destroy_read_struct(&png, &info, NULL);
    }
    if (rowPointers) {
        free(rowPointers);
    }
    avifRGBImageFreePixels(&rgb);
    return readResult;
}

avifBool avifPNGWrite(avifImage * avif, const char * outputFilename, int requestedDepth)
{
    avifBool writeResult = AVIF_FALSE;
    png_structp png = NULL;
    png_infop info = NULL;
    png_bytep * rowPointers = NULL;

    avifRGBImage rgb;
    memset(&rgb, 0, sizeof(avifRGBImage));

    FILE * f = fopen(outputFilename, "wb");
    if (!f) {
        fprintf(stderr, "Can't open PNG file for write: %s\n", outputFilename);
        goto cleanup;
    }

    png = png_create_write_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
    if (!png) {
        fprintf(stderr, "Cannot init libpng (png): %s\n", outputFilename);
        goto cleanup;
    }
    info = png_create_info_struct(png);
    if (!info) {
        fprintf(stderr, "Cannot init libpng (info): %s\n", outputFilename);
        goto cleanup;
    }

    if (setjmp(png_jmpbuf(png))) {
        fprintf(stderr, "Error writing PNG: %s\n", outputFilename);
        goto cleanup;
    }

    png_init_io(png, f);

    int rgbDepth = requestedDepth;
    if (rgbDepth == 0) {
        if (avif->depth > 8) {
            rgbDepth = 16;
        } else {
            rgbDepth = 8;
        }
    }

    png_set_IHDR(
        png, info, avif->width, avif->height, rgbDepth, PNG_COLOR_TYPE_RGBA, PNG_INTERLACE_NONE, PNG_COMPRESSION_TYPE_DEFAULT, PNG_FILTER_TYPE_DEFAULT);
    if (avif->profileFormat == AVIF_PROFILE_FORMAT_ICC) {
        png_set_iCCP(png, info, "libavif", 0, avif->icc.data, (png_uint_32)avif->icc.size);
    }
    png_write_info(png, info);

    avifRGBImageSetDefaults(&rgb, avif);
    rgb.depth = rgbDepth;
    avifRGBImageAllocatePixels(&rgb);
    avifImageYUVToRGB(avif, &rgb);
    rowPointers = (png_bytep *)malloc(sizeof(png_bytep) * rgb.height);
    for (uint32_t y = 0; y < rgb.height; ++y) {
        rowPointers[y] = &rgb.pixels[y * rgb.rowBytes];
    }

    if (rgbDepth > 8) {
        png_set_swap(png);
    }

    png_write_image(png, rowPointers);
    png_write_end(png, NULL);

    writeResult = AVIF_TRUE;
    printf("Wrote PNG: %s\n", outputFilename);
cleanup:
    if (f) {
        fclose(f);
    }
    if (png) {
        png_destroy_write_struct(&png, &info);
    }
    if (rowPointers) {
        free(rowPointers);
    }
    avifRGBImageFreePixels(&rgb);
    return writeResult;
}
