// Copyright 2020 Joe Drago. All rights reserved.
// SPDX-License-Identifier: BSD-2-Clause

#include "avifjpeg.h"

#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "jpeglib.h"

#include "iccjpeg.h"

#define AVIF_MIN(a, b) (((a) < (b)) ? (a) : (b))
#define AVIF_MAX(a, b) (((a) > (b)) ? (a) : (b))

struct my_error_mgr
{
    struct jpeg_error_mgr pub;
    jmp_buf setjmp_buffer;
};
typedef struct my_error_mgr * my_error_ptr;
static void my_error_exit(j_common_ptr cinfo)
{
    my_error_ptr myerr = (my_error_ptr)cinfo->err;
    (*cinfo->err->output_message)(cinfo);
    longjmp(myerr->setjmp_buffer, 1);
}

#if JPEG_LIB_VERSION >= 70
#define AVIF_LIBJPEG_DCT_v_scaled_size DCT_v_scaled_size
#define AVIF_LIBJPEG_DCT_h_scaled_size DCT_h_scaled_size
#else
#define AVIF_LIBJPEG_DCT_h_scaled_size DCT_scaled_size
#define AVIF_LIBJPEG_DCT_v_scaled_size DCT_scaled_size
#endif
static void avifJPEGCopyPixels(avifImage * avif, struct jpeg_decompress_struct * cinfo)
{
    cinfo->raw_data_out = TRUE;
    jpeg_start_decompress(cinfo);

    avif->width = cinfo->image_width;
    avif->height = cinfo->image_height;

    int workComponents = avif->yuvFormat == AVIF_PIXEL_FORMAT_YUV400 ? 1 : cinfo->num_components;

    JSAMPIMAGE buffer = (*cinfo->mem->alloc_small)((j_common_ptr)cinfo, JPOOL_IMAGE, sizeof(JSAMPARRAY) * workComponents);

    // lines of output image attempt to read per jpeg_read_raw_data call
    int readLines = 0;
    // lines of sample to read per call for each channel
    int linesPerCall[3] = { 0, 0, 0 };
    // lines of sample expect to get for each channel
    int targetRead[3] = { 0, 0, 0 };
    // already read lines for each channel
    int alreadyRead[3] = { 0, 0, 0 };
    // which avif channel to write to for each jpeg channel
    enum avifChannelIndex targetChannel[3] = { AVIF_CHAN_R, AVIF_CHAN_R, AVIF_CHAN_R };
    for (int i = 0; i < workComponents; ++i) {
        jpeg_component_info * comp = &cinfo->comp_info[i];

        linesPerCall[i] = comp->v_samp_factor * comp->AVIF_LIBJPEG_DCT_v_scaled_size;
        targetRead[i] = comp->downsampled_height;
        buffer[i] = (*cinfo->mem->alloc_sarray)((j_common_ptr)cinfo,
                                                JPOOL_IMAGE,
                                                comp->width_in_blocks * comp->AVIF_LIBJPEG_DCT_h_scaled_size,
                                                linesPerCall[i]);
        readLines = AVIF_MAX(readLines, linesPerCall[i]);
    }

    avifImageAllocatePlanes(avif, AVIF_PLANES_YUV);

    if (cinfo->jpeg_color_space == JCS_YCbCr) {
        targetChannel[0] = AVIF_CHAN_Y;
        targetChannel[1] = AVIF_CHAN_U;
        targetChannel[2] = AVIF_CHAN_V;
    } else if (cinfo->jpeg_color_space == JCS_GRAYSCALE) {
        targetChannel[0] = AVIF_CHAN_Y;
    } else /* cinfo->jpeg_color_space == JCS_RGB */ {
        targetChannel[0] = AVIF_CHAN_V;
        targetChannel[1] = AVIF_CHAN_Y;
        targetChannel[2] = AVIF_CHAN_U;
    }

    while (cinfo->output_scanline < cinfo->output_height) {
        jpeg_read_raw_data(cinfo, buffer, readLines);

        for (int i = 0; i < workComponents; ++i) {
            int linesRead = AVIF_MIN(targetRead[i] - alreadyRead[i], linesPerCall[i]);
            for (int j = 0; j < linesRead; ++j) {
                memcpy(&avif->yuvPlanes[targetChannel[i]][avif->yuvRowBytes[targetChannel[i]] * (alreadyRead[i] + j)],
                       buffer[i][j],
                       avif->yuvRowBytes[targetChannel[i]]);
            }
            alreadyRead[i] += linesPerCall[i];
        }
    }

    jpeg_finish_decompress(cinfo);
}

static avifBool avifJPEGReadCopy(avifImage * avif, struct jpeg_decompress_struct * cinfo)
{
    if (avif->depth != 8) {
        return AVIF_FALSE;
    }

    if (cinfo->jpeg_color_space == JCS_YCbCr) {
        // Import from YUV: must using compatible matrixCoefficients.
        if ((avif->matrixCoefficients == AVIF_MATRIX_COEFFICIENTS_BT601) ||
            (avif->matrixCoefficients == AVIF_MATRIX_COEFFICIENTS_CHROMA_DERIVED_NCL &&
             avif->colorPrimaries == AVIF_COLOR_PRIMARIES_BT470M)) {
            // YUV->YUV: require precise match for pixel format.
            if (((avif->yuvFormat == AVIF_PIXEL_FORMAT_YUV444) &&
                 (cinfo->comp_info[0].h_samp_factor == 1 && cinfo->comp_info[0].v_samp_factor == 1 &&
                  cinfo->comp_info[1].h_samp_factor == 1 && cinfo->comp_info[1].v_samp_factor == 1 &&
                  cinfo->comp_info[2].h_samp_factor == 1 && cinfo->comp_info[2].v_samp_factor == 1)) ||
                ((avif->yuvFormat == AVIF_PIXEL_FORMAT_YUV422) &&
                 (cinfo->comp_info[0].h_samp_factor == 2 && cinfo->comp_info[0].v_samp_factor == 1 &&
                  cinfo->comp_info[1].h_samp_factor == 1 && cinfo->comp_info[1].v_samp_factor == 1 &&
                  cinfo->comp_info[2].h_samp_factor == 1 && cinfo->comp_info[2].v_samp_factor == 1)) ||
                ((avif->yuvFormat == AVIF_PIXEL_FORMAT_YUV420) &&
                 (cinfo->comp_info[0].h_samp_factor == 2 && cinfo->comp_info[0].v_samp_factor == 2 &&
                  cinfo->comp_info[1].h_samp_factor == 1 && cinfo->comp_info[1].v_samp_factor == 1 &&
                  cinfo->comp_info[2].h_samp_factor == 1 && cinfo->comp_info[2].v_samp_factor == 1))) {
                cinfo->out_color_space = JCS_YCbCr;
                avifJPEGCopyPixels(avif, cinfo);

                return AVIF_TRUE;
            }

            // YUV->Grayscale: subsample Y plane not allowed.
            if ((avif->yuvFormat == AVIF_PIXEL_FORMAT_YUV400) && (cinfo->comp_info[0].h_samp_factor == cinfo->max_h_samp_factor &&
                                                                  cinfo->comp_info[0].v_samp_factor == cinfo->max_v_samp_factor)) {
                cinfo->out_color_space = JCS_YCbCr;
                avifJPEGCopyPixels(avif, cinfo);

                return AVIF_TRUE;
            }
        }
    } else if (cinfo->jpeg_color_space == JCS_GRAYSCALE) {
        // Import from Grayscale: subsample not allowed.
        if ((cinfo->comp_info[0].h_samp_factor == cinfo->max_h_samp_factor &&
             cinfo->comp_info[0].v_samp_factor == cinfo->max_v_samp_factor)) {
            // Import to YUV/Grayscale: must using compatible matrixCoefficients.
            if (((avif->matrixCoefficients == AVIF_MATRIX_COEFFICIENTS_BT601) ||
                 (avif->matrixCoefficients == AVIF_MATRIX_COEFFICIENTS_CHROMA_DERIVED_NCL &&
                  avif->colorPrimaries == AVIF_COLOR_PRIMARIES_BT470M))) {
                // Grayscale->Grayscale: direct copy.
                if (avif->yuvFormat == AVIF_PIXEL_FORMAT_YUV400) {
                    cinfo->out_color_space = JCS_GRAYSCALE;
                    avifJPEGCopyPixels(avif, cinfo);

                    return AVIF_TRUE;
                }

                // Grayscale->YUV: copy Y, fill UV with monochrome value.
                if ((avif->yuvFormat == AVIF_PIXEL_FORMAT_YUV444) || (avif->yuvFormat == AVIF_PIXEL_FORMAT_YUV422) ||
                    (avif->yuvFormat == AVIF_PIXEL_FORMAT_YUV420)) {
                    cinfo->out_color_space = JCS_GRAYSCALE;
                    avifJPEGCopyPixels(avif, cinfo);

                    avifPixelFormatInfo info;
                    avifGetPixelFormatInfo(avif->yuvFormat, &info);
                    uint32_t uvHeight = (avif->height + info.chromaShiftY) >> info.chromaShiftY;
                    memset(avif->yuvPlanes[AVIF_CHAN_U], 128, avif->yuvRowBytes[AVIF_CHAN_U] * uvHeight);
                    memset(avif->yuvPlanes[AVIF_CHAN_V], 128, avif->yuvRowBytes[AVIF_CHAN_V] * uvHeight);

                    return AVIF_TRUE;
                }
            }

            // Grayscale->RGB: copy Y to G, duplicate to R and B.
            if (avif->matrixCoefficients == AVIF_MATRIX_COEFFICIENTS_IDENTITY) {
                cinfo->out_color_space = JCS_GRAYSCALE;
                avifJPEGCopyPixels(avif, cinfo);

                memcpy(avif->yuvPlanes[AVIF_CHAN_U], avif->yuvPlanes[AVIF_CHAN_Y], avif->yuvRowBytes[AVIF_CHAN_U] * avif->height);
                memcpy(avif->yuvPlanes[AVIF_CHAN_V], avif->yuvPlanes[AVIF_CHAN_Y], avif->yuvRowBytes[AVIF_CHAN_V] * avif->height);

                return AVIF_TRUE;
            }
        }
    } else if (cinfo->jpeg_color_space == JCS_RGB) {
        // RGB->RGB: subsample not allowed.
        if (avif->matrixCoefficients == AVIF_MATRIX_COEFFICIENTS_IDENTITY &&
            (cinfo->comp_info[0].h_samp_factor == 1 && cinfo->comp_info[0].v_samp_factor == 1 &&
             cinfo->comp_info[1].h_samp_factor == 1 && cinfo->comp_info[1].v_samp_factor == 1 &&
             cinfo->comp_info[2].h_samp_factor == 1 && cinfo->comp_info[2].v_samp_factor == 1)) {
            cinfo->out_color_space = JCS_RGB;
            avifJPEGCopyPixels(avif, cinfo);

            return AVIF_TRUE;
        }
    }

    return AVIF_FALSE;
}

// Note on setjmp() and volatile variables:
//
// K & R, The C Programming Language 2nd Ed, p. 254 says:
//   ... Accessible objects have the values they had when longjmp was called,
//   except that non-volatile automatic variables in the function calling setjmp
//   become undefined if they were changed after the setjmp call.
//
// Therefore, 'iccData' is declared as volatile. 'rgb' should be declared as
// volatile, but doing so would be inconvenient (try it) and since it is a
// struct, the compiler is unlikely to put it in a register. 'ret' does not need
// to be declared as volatile because it is not modified between setjmp and
// longjmp. But GCC's -Wclobbered warning may have trouble figuring that out, so
// we preemptively declare it as volatile.

avifBool avifJPEGRead(const char * inputFilename, avifImage * avif, avifPixelFormat requestedFormat, uint32_t requestedDepth)
{
    volatile avifBool ret = AVIF_FALSE;
    uint8_t * volatile iccData = NULL;

    avifRGBImage rgb;
    memset(&rgb, 0, sizeof(avifRGBImage));

    FILE * f = fopen(inputFilename, "rb");
    if (!f) {
        fprintf(stderr, "Can't open JPEG file for read: %s\n", inputFilename);
        return ret;
    }

    struct my_error_mgr jerr;
    struct jpeg_decompress_struct cinfo;
    cinfo.err = jpeg_std_error(&jerr.pub);
    jerr.pub.error_exit = my_error_exit;
    if (setjmp(jerr.setjmp_buffer)) {
        goto cleanup;
    }

    jpeg_create_decompress(&cinfo);

    setup_read_icc_profile(&cinfo);
    jpeg_stdio_src(&cinfo, f);
    jpeg_read_header(&cinfo, TRUE);

    uint8_t * iccDataTmp;
    unsigned int iccDataLen;
    if (read_icc_profile(&cinfo, &iccDataTmp, &iccDataLen)) {
        iccData = iccDataTmp;
        avifImageSetProfileICC(avif, iccDataTmp, (size_t)iccDataLen);
    }

    avif->yuvFormat = requestedFormat;
    avif->depth = requestedDepth ? requestedDepth : 8;
    // JPEG doesn't have alpha. Prevent confusion.
    avif->alphaPremultiplied = AVIF_FALSE;

    if (avifJPEGReadCopy(avif, &cinfo)) {
        ret = AVIF_TRUE;
        goto cleanup;
    }

    cinfo.out_color_space = JCS_RGB;
    jpeg_start_decompress(&cinfo);

    avif->width = cinfo.output_width;
    avif->height = cinfo.output_height;

    int row_stride = cinfo.output_width * cinfo.output_components;
    JSAMPARRAY buffer = (*cinfo.mem->alloc_sarray)((j_common_ptr)&cinfo, JPOOL_IMAGE, row_stride, 1);

    avif->width = cinfo.output_width;
    avif->height = cinfo.output_height;
    avif->yuvFormat = requestedFormat;
    avif->depth = requestedDepth ? requestedDepth : 8;
    avifRGBImageSetDefaults(&rgb, avif);
    rgb.format = AVIF_RGB_FORMAT_RGB;
    rgb.depth = 8;
    avifRGBImageAllocatePixels(&rgb);

    int row = 0;
    while (cinfo.output_scanline < cinfo.output_height) {
        jpeg_read_scanlines(&cinfo, buffer, 1);
        uint8_t * pixelRow = &rgb.pixels[row * rgb.rowBytes];
        memcpy(pixelRow, buffer[0], rgb.rowBytes);
        ++row;
    }
    if (avifImageRGBToYUV(avif, &rgb) != AVIF_RESULT_OK) {
        fprintf(stderr, "Conversion to YUV failed: %s\n", inputFilename);
        goto cleanup;
    }

    jpeg_finish_decompress(&cinfo);
    ret = AVIF_TRUE;
cleanup:
    jpeg_destroy_decompress(&cinfo);
    if (f) {
        fclose(f);
    }
    free(iccData);
    avifRGBImageFreePixels(&rgb);
    return ret;
}

avifBool avifJPEGWrite(const char * outputFilename, const avifImage * avif, int jpegQuality, avifChromaUpsampling chromaUpsampling)
{
    avifBool ret = AVIF_FALSE;
    FILE * f = NULL;

    struct jpeg_compress_struct cinfo;
    struct jpeg_error_mgr jerr;
    JSAMPROW row_pointer[1];
    cinfo.err = jpeg_std_error(&jerr);
    jpeg_create_compress(&cinfo);

    avifRGBImage rgb;
    avifRGBImageSetDefaults(&rgb, avif);
    rgb.format = AVIF_RGB_FORMAT_RGB;
    rgb.chromaUpsampling = chromaUpsampling;
    rgb.depth = 8;
    avifRGBImageAllocatePixels(&rgb);
    if (avifImageYUVToRGB(avif, &rgb) != AVIF_RESULT_OK) {
        fprintf(stderr, "Conversion to RGB failed: %s\n", outputFilename);
        goto cleanup;
    }

    f = fopen(outputFilename, "wb");
    if (!f) {
        fprintf(stderr, "Can't open JPEG file for write: %s\n", outputFilename);
        goto cleanup;
    }

    jpeg_stdio_dest(&cinfo, f);
    cinfo.image_width = avif->width;
    cinfo.image_height = avif->height;
    cinfo.input_components = 3;
    cinfo.in_color_space = JCS_RGB;
    jpeg_set_defaults(&cinfo);
    jpeg_set_quality(&cinfo, jpegQuality, TRUE);
    jpeg_start_compress(&cinfo, TRUE);

    if (avif->icc.data && (avif->icc.size > 0)) {
        write_icc_profile(&cinfo, avif->icc.data, (unsigned int)avif->icc.size);
    }

    while (cinfo.next_scanline < cinfo.image_height) {
        row_pointer[0] = &rgb.pixels[cinfo.next_scanline * rgb.rowBytes];
        (void)jpeg_write_scanlines(&cinfo, row_pointer, 1);
    }

    jpeg_finish_compress(&cinfo);
    ret = AVIF_TRUE;
    printf("Wrote JPEG: %s\n", outputFilename);
cleanup:
    if (f) {
        fclose(f);
    }
    jpeg_destroy_compress(&cinfo);
    avifRGBImageFreePixels(&rgb);
    return ret;
}
