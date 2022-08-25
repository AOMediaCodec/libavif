// Copyright 2020 Joe Drago. All rights reserved.
// SPDX-License-Identifier: BSD-2-Clause

#include "avifpng.h"
#include "avifutil.h"

#include "png.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// See libpng-manual.txt, section XI.
#if PNG_LIBPNG_VER_MAJOR > 1 || (PNG_LIBPNG_VER_MAJOR == 1 && PNG_LIBPNG_VER_MINOR >= 5)
typedef png_bytep png_iccp_datap;
#else
typedef png_charp png_iccp_datap;
#endif

static avifBool avifIsHexDigit(char hexDigit, uint8_t * decimalValue)
{
    if ((hexDigit >= 'A') && (hexDigit <= 'F')) {
        *decimalValue = (uint8_t)(hexDigit - 'A') + 10;
        return AVIF_TRUE;
    } else if ((hexDigit >= 'a') && (hexDigit <= 'f')) {
        *decimalValue = (uint8_t)(hexDigit - 'a') + 10;
        return AVIF_TRUE;
    } else if ((hexDigit >= '0') && (hexDigit <= '9')) {
        *decimalValue = (uint8_t)(hexDigit - '0');
        return AVIF_TRUE;
    } else {
        return AVIF_FALSE;
    }
}

// Converts a hexadecimal string which contains 2-byte character representations of hexadecimal values to raw data.
// hexString may contain values consisting of [A-F][a-f][0-9] in pairs, e.g., 7af2..., separated by any number of newlines.
// On success the bytes are filled and AVIF_TRUE is returned.
// AVIF_FALSE is returned if fewer than expectedLength hexadecimal pairs are converted.
static avifBool avifHexStringToBytes(const char * hexString, size_t expectedLength, avifRWData * bytes)
{
    avifRWDataRealloc(bytes, expectedLength);
    const char * src = hexString;
    size_t actualLength = 0;
    for (; (actualLength < expectedLength) && (*src != '\0'); ++actualLength) {
        while (*src == '\n') {
            ++src;
        }
        uint8_t mostSignificant, leastSignificant;
        if (!avifIsHexDigit(src[0], &mostSignificant) || !avifIsHexDigit(src[1], &leastSignificant)) {
            avifRWDataFree(bytes);
            fprintf(stderr, "Exif extraction failed: invalid token at " AVIF_FMT_ZU "\n", actualLength);
            return AVIF_FALSE;
        }
        src += 2;
        bytes->data[actualLength] = (mostSignificant << 4) | leastSignificant;
    }

    if (actualLength != expectedLength) {
        avifRWDataFree(bytes);
        fprintf(stderr, "Exif extraction failed: expected " AVIF_FMT_ZU " tokens but got " AVIF_FMT_ZU "\n", expectedLength, actualLength);
        return AVIF_FALSE;
    }
    return AVIF_TRUE;
}

// Parses the raw profile string of profileLen characters and extracts the payload.
static avifBool avifCopyRawProfile(const char * profile, size_t profileLen, avifRWData * payload)
{
    if (!profile || (profileLen < 3)) {
        fprintf(stderr, "Exif extraction failed: empty profile\n");
        return AVIF_FALSE;
    }

    // ImageMagick formats 'raw profiles' as "\n<name>\n<length>(%8lu)\n<hex payload>\n".
    const char * src = profile;
    if (*src != '\n') {
        fprintf(stderr, "Exif extraction failed: malformed raw profile, expected '\\n' but got '\\x%.2X'\n", *src);
        return AVIF_FALSE;
    }
    ++src;
    size_t numReadCharacters = 1;
    // Skip the profile name and extract the length.
    for (; (*src != '\0') && (*src != '\n'); ++src, ++numReadCharacters) {
        if (numReadCharacters == profileLen) {
            fprintf(stderr, "Exif extraction failed: truncated raw profile of size " AVIF_FMT_ZU "\n", profileLen);
            return AVIF_FALSE;
        }
    }
    char * end;
    const long expectedLength = strtol(src, &end, 10);
    if ((expectedLength <= 0) || ((unsigned long)expectedLength > SIZE_MAX)) {
        fprintf(stderr, "Exif extraction failed: invalid length %ld\n", expectedLength);
        return AVIF_FALSE;
    }
    if (*end != '\n') {
        fprintf(stderr, "Exif extraction failed: malformed raw profile, expected '\\n' but got '\\x%.2X'\n", *end);
        return AVIF_FALSE;
    }
    ++end;

    // 'end' now points to the profile payload.
    return avifHexStringToBytes(end, (size_t)expectedLength, payload);
}

// Returns AVIF_TRUE if there was no Exif metadata located at info or if the Exif metadata located at info
// was correctly parsed and imported to avif->exif. Returns AVIF_FALSE in case of error.
static avifBool avifExtractExif(png_structp png, png_infop const info, avifImage * avif)
{
    png_textp text = NULL;
    const png_uint_32 numTextChunks = png_get_text(png, info, &text, NULL);
    for (png_uint_32 i = 0; i < numTextChunks; ++i, ++text) {
        if (!strcmp(text->key, "Raw profile type exif") || !strcmp(text->key, "Raw profile type APP1")) {
            png_size_t textLength;
            switch (text->compression) {
#ifdef PNG_iTXt_SUPPORTED
                case PNG_ITXT_COMPRESSION_NONE:
                case PNG_ITXT_COMPRESSION_zTXt:
                    textLength = text->itxt_length;
                    break;
#endif
                case PNG_TEXT_COMPRESSION_NONE:
                case PNG_TEXT_COMPRESSION_zTXt:
                default:
                    textLength = text->text_length;
                    break;
            }
            return avifCopyRawProfile(text->text, textLength, &avif->exif);
        }
    }
    return AVIF_TRUE;
}

// Note on setjmp() and volatile variables:
//
// K & R, The C Programming Language 2nd Ed, p. 254 says:
//   ... Accessible objects have the values they had when longjmp was called,
//   except that non-volatile automatic variables in the function calling setjmp
//   become undefined if they were changed after the setjmp call.
//
// Therefore, 'rowPointers' is declared as volatile. 'rgb' should be declared as
// volatile, but doing so would be inconvenient (try it) and since it is a
// struct, the compiler is unlikely to put it in a register. 'readResult' and
// 'writeResult' do not need to be declared as volatile because they are not
// modified between setjmp and longjmp. But GCC's -Wclobbered warning may have
// trouble figuring that out, so we preemptively declare them as volatile.

avifBool avifPNGRead(const char * inputFilename,
                     avifImage * avif,
                     avifPixelFormat requestedFormat,
                     uint32_t requestedDepth,
                     avifRGBToYUVFlags flags,
                     avifBool ignoreExif,
                     uint32_t * outPNGDepth)
{
    volatile avifBool readResult = AVIF_FALSE;
    png_structp png = NULL;
    png_infop info = NULL;
    png_infop info_end = NULL;
    png_bytep * volatile rowPointers = NULL;

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
    if (png_get_iCCP(png, info, &iccpProfileName, &iccpCompression, (png_iccp_datap *)&iccpData, &iccpDataLen) == PNG_INFO_iCCP) {
        avifImageSetProfileICC(avif, iccpData, iccpDataLen);
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

    if (outPNGDepth) {
        *outPNGDepth = imgBitDepth;
    }

    png_read_update_info(png, info);

    avif->width = rawWidth;
    avif->height = rawHeight;
    avif->yuvFormat = requestedFormat;
    if (avif->yuvFormat == AVIF_PIXEL_FORMAT_NONE) {
        if (avif->matrixCoefficients == AVIF_MATRIX_COEFFICIENTS_IDENTITY) {
            // Identity is only valid with YUV444.
            avif->yuvFormat = AVIF_PIXEL_FORMAT_YUV444;
        } else if ((rawColorType == PNG_COLOR_TYPE_GRAY) || (rawColorType == PNG_COLOR_TYPE_GRAY_ALPHA)) {
            avif->yuvFormat = AVIF_PIXEL_FORMAT_YUV400;
        } else {
            avif->yuvFormat = AVIF_APP_DEFAULT_PIXEL_FORMAT;
        }
    }
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
    if (avifImageRGBToYUV(avif, &rgb, flags) != AVIF_RESULT_OK) {
        fprintf(stderr, "Conversion to YUV failed: %s\n", inputFilename);
        goto cleanup;
    }

    if (!ignoreExif) {
        // Read Exif metadata at the beginning of the file.
        if (!avifExtractExif(png, info, avif)) {
            goto cleanup;
        }
        // Read Exif metadata at the end of the file if there was none at the beginning.
        if (!avif->exif.data) {
            info_end = png_create_info_struct(png);
            if (!info_end) {
                fprintf(stderr, "Cannot init libpng (info_end): %s\n", inputFilename);
                goto cleanup;
            }
            png_read_end(png, info_end);
            if (!avifExtractExif(png, info_end, avif)) {
                goto cleanup;
            }
        }
    }
    readResult = AVIF_TRUE;

cleanup:
    if (f) {
        fclose(f);
    }
    if (png) {
        png_destroy_read_struct(&png, &info, NULL);
        png_destroy_read_struct(&png, &info_end, NULL);
    }
    if (rowPointers) {
        free(rowPointers);
    }
    avifRGBImageFreePixels(&rgb);
    return readResult;
}

avifBool avifPNGWrite(const char * outputFilename, const avifImage * avif, uint32_t requestedDepth, avifYUVToRGBFlags conversionFlags, int compressionLevel)
{
    volatile avifBool writeResult = AVIF_FALSE;
    png_structp png = NULL;
    png_infop info = NULL;
    png_bytep * volatile rowPointers = NULL;
    FILE * volatile f = NULL;

    avifRGBImage rgb;
    memset(&rgb, 0, sizeof(avifRGBImage));

    int rgbDepth = requestedDepth;
    if (rgbDepth == 0) {
        if (avif->depth > 8) {
            rgbDepth = 16;
        } else {
            rgbDepth = 8;
        }
    }

    avifRGBImageSetDefaults(&rgb, avif);
    rgb.depth = rgbDepth;
    int colorType = PNG_COLOR_TYPE_RGBA;
    if (!avif->alphaPlane) {
        colorType = PNG_COLOR_TYPE_RGB;
        rgb.format = AVIF_RGB_FORMAT_RGB;
    }
    avifRGBImageAllocatePixels(&rgb);
    if (avifImageYUVToRGB(avif, &rgb, conversionFlags) != AVIF_RESULT_OK) {
        fprintf(stderr, "Conversion to RGB failed: %s\n", outputFilename);
        goto cleanup;
    }

    f = fopen(outputFilename, "wb");
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

    // Don't bother complaining about ICC profile's contents when transferring from AVIF to PNG.
    // It is up to the enduser to decide if they want to keep their ICC profiles or not.
#if defined(PNG_SKIP_sRGB_CHECK_PROFILE) && defined(PNG_SET_OPTION_SUPPORTED) // See libpng-manual.txt, section XII.
    png_set_option(png, PNG_SKIP_sRGB_CHECK_PROFILE, PNG_OPTION_ON);
#endif

    if (compressionLevel >= 0) {
        png_set_compression_level(png, compressionLevel);
    }

    png_set_IHDR(png, info, avif->width, avif->height, rgb.depth, colorType, PNG_INTERLACE_NONE, PNG_COMPRESSION_TYPE_DEFAULT, PNG_FILTER_TYPE_DEFAULT);
    if (avif->icc.data && (avif->icc.size > 0)) {
        png_set_iCCP(png, info, "libavif", 0, (png_iccp_datap)avif->icc.data, (png_uint_32)avif->icc.size);
    }
    png_write_info(png, info);

    rowPointers = (png_bytep *)malloc(sizeof(png_bytep) * rgb.height);
    for (uint32_t y = 0; y < rgb.height; ++y) {
        rowPointers[y] = &rgb.pixels[y * rgb.rowBytes];
    }

    if (rgb.depth > 8) {
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
