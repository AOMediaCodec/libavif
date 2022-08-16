// Copyright 2019 Joe Drago. All rights reserved.
// SPDX-License-Identifier: BSD-2-Clause

#include "avif/internal.h"

#if !defined(AVIF_LIBYUV_ENABLED)

// No libyuv!
avifResult avifImageRGBToYUVLibYUV(avifImage * image, const avifRGBImage * rgb)
{
    (void)image;
    (void)rgb;
    return AVIF_RESULT_NOT_IMPLEMENTED;
}
avifResult avifImageYUVToRGBLibYUV(const avifImage * image, avifRGBImage * rgb)
{
    (void)image;
    (void)rgb;
    return AVIF_RESULT_NOT_IMPLEMENTED;
}
avifResult avifRGBImagePremultiplyAlphaLibYUV(avifRGBImage * rgb)
{
    (void)rgb;
    return AVIF_RESULT_NOT_IMPLEMENTED;
}
avifResult avifRGBImageUnpremultiplyAlphaLibYUV(avifRGBImage * rgb)
{
    (void)rgb;
    return AVIF_RESULT_NOT_IMPLEMENTED;
}
avifResult avifRGBImageToF16LibYUV(avifRGBImage * rgb)
{
    (void)rgb;
    return AVIF_RESULT_NOT_IMPLEMENTED;
}
unsigned int avifLibYUVVersion(void)
{
    return 0;
}

#else

#include <assert.h>
#include <limits.h>

#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wstrict-prototypes" // "this function declaration is not a prototype"
// The newline at the end of libyuv/version.h was accidentally deleted in version 1792 and restored
// in version 1813:
// https://chromium-review.googlesource.com/c/libyuv/libyuv/+/3183182
// https://chromium-review.googlesource.com/c/libyuv/libyuv/+/3527834
#pragma clang diagnostic ignored "-Wnewline-eof"       // "no newline at end of file"
#endif
#include <libyuv.h>
#if defined(__clang__)
#pragma clang diagnostic pop
#endif

static avifResult avifImageRGBToYUVLibYUV8bpc(avifImage * image, const avifRGBImage * rgb);

avifResult avifImageRGBToYUVLibYUV(avifImage * image, const avifRGBImage * rgb)
{
    if ((rgb->chromaDownsampling != AVIF_CHROMA_DOWNSAMPLING_AUTOMATIC) && (rgb->chromaDownsampling != AVIF_CHROMA_DOWNSAMPLING_FASTEST)) {
        // libyuv uses integer/fixed-point averaging and RGB-to-YUV conversion.
        // We do not ensure a specific ordering of these two steps and libyuv
        // may perform one or the other depending on the implementation or
        // platform. Also libyuv trades a bit of accuracy for speed, so if the
        // end user requested best quality, avoid using libyuv as well.
        return AVIF_RESULT_NOT_IMPLEMENTED;
    }

    if ((image->depth == 8) && (rgb->depth == 8)) {
        return avifImageRGBToYUVLibYUV8bpc(image, rgb);
    }

    // This function didn't do anything; use the built-in conversion.
    return AVIF_RESULT_NOT_IMPLEMENTED;
}

// Two-step replacement for AVIF_RGB_FORMAT_RGBA to 8-bit BT.601 full range YUV, which is missing from libyuv.
static int avifABGRToJ420(const uint8_t * src_abgr,
                          int src_stride_abgr,
                          uint8_t * dst_y,
                          int dst_stride_y,
                          uint8_t * dst_u,
                          int dst_stride_u,
                          uint8_t * dst_v,
                          int dst_stride_v,
                          int width,
                          int height)
{
    // A temporary buffer is needed to swap the R and B channels before calling ARGBToJ420().
    uint8_t * src_argb;
    const int src_stride_argb = width * 4;
    const int soft_allocation_limit = 16384; // Arbitrarily chosen trade-off between CPU and memory footprints.
    int num_allocated_rows;
    if ((height == 1) || ((int64_t)src_stride_argb * height <= soft_allocation_limit)) {
        // Process the whole buffer in one go.
        num_allocated_rows = height;
    } else {
        if ((int64_t)src_stride_argb * 2 > INT_MAX) {
            return -1;
        }
        // The last row of an odd number of RGB rows to be converted to subsampled YUV is treated differently
        // by libyuv, so make sure all steps but the last one process an even number of rows.
        // Try to process as many row pairs as possible in a single step without allocating more than
        // soft_allocation_limit, unless two rows need more than that.
        num_allocated_rows = AVIF_MAX(1, soft_allocation_limit / (src_stride_argb * 2)) * 2;
    }
    src_argb = avifAlloc(num_allocated_rows * src_stride_argb);
    if (!src_argb) {
        return -1;
    }

    for (int y = 0; y < height; y += num_allocated_rows) {
        const int num_rows = AVIF_MIN(num_allocated_rows, height - y);
        if (ABGRToARGB(src_abgr, src_stride_abgr, src_argb, src_stride_argb, width, num_rows) ||
            ARGBToJ420(src_argb, src_stride_argb, dst_y, dst_stride_y, dst_u, dst_stride_u, dst_v, dst_stride_v, width, num_rows)) {
            avifFree(src_argb);
            return -1;
        }
        src_abgr += (size_t)num_rows * src_stride_abgr;
        dst_y += (size_t)num_rows * dst_stride_y;
        dst_u += (size_t)num_rows / 2 * dst_stride_u; // 4:2:0
        dst_v += (size_t)num_rows / 2 * dst_stride_v; // (either num_rows is even or this is the last iteration)
    }
    avifFree(src_argb);
    return 0;
}

avifResult avifImageRGBToYUVLibYUV8bpc(avifImage * image, const avifRGBImage * rgb)
{
    assert((image->depth == 8) && (rgb->depth == 8));
    // libavif uses byte-order when describing pixel formats, such that the R in RGBA is the lowest address,
    // similar to PNG. libyuv orders in word-order, so libavif's RGBA would be referred to in libyuv as ABGR.

    if (image->yuvFormat == AVIF_PIXEL_FORMAT_YUV400) {
        // Generic mapping from any RGB layout (with or without alpha) to monochrome.
        int (*RGBtoY)(const uint8_t *, int, uint8_t *, int, int, int) = NULL;

        if (image->yuvRange == AVIF_RANGE_LIMITED) {
            if (rgb->format == AVIF_RGB_FORMAT_BGRA) {
                RGBtoY = ARGBToI400;
            }
        } else { // image->yuvRange == AVIF_RANGE_FULL
            if (rgb->format == AVIF_RGB_FORMAT_BGRA) {
                RGBtoY = ARGBToJ400;
            }
        }

        if (!RGBtoY) {
            return AVIF_RESULT_NOT_IMPLEMENTED;
        }
        if (RGBtoY(rgb->pixels, rgb->rowBytes, image->yuvPlanes[AVIF_CHAN_Y], image->yuvRowBytes[AVIF_CHAN_Y], image->width, image->height)) {
            return AVIF_RESULT_REFORMAT_FAILED;
        }
        return AVIF_RESULT_OK;
    }

    // Generic mapping from any RGB layout (with or without alpha) to any YUV layout (subsampled or not).
    int (*RGBtoYUV)(const uint8_t *, int, uint8_t *, int, uint8_t *, int, uint8_t *, int, int, int) = NULL;

    // libyuv only handles BT.601 for RGB to YUV, and not all range/order/subsampling combinations.
    // BT.470BG has the same coefficients as BT.601.
    if ((image->matrixCoefficients == AVIF_MATRIX_COEFFICIENTS_BT470BG) || (image->matrixCoefficients == AVIF_MATRIX_COEFFICIENTS_BT601)) {
        if (rgb->format == AVIF_RGB_FORMAT_BGRA) {
            if (image->yuvFormat == AVIF_PIXEL_FORMAT_YUV420) {
                if (image->yuvRange == AVIF_RANGE_FULL) {
                    RGBtoYUV = ARGBToJ420;
                } else {
                    RGBtoYUV = ARGBToI420;
                }
            } else if (image->yuvFormat == AVIF_PIXEL_FORMAT_YUV422) {
                if (image->yuvRange == AVIF_RANGE_FULL) {
                    RGBtoYUV = ARGBToJ422;
                } else {
                    RGBtoYUV = ARGBToI422;
                }
            } else if (image->yuvFormat == AVIF_PIXEL_FORMAT_YUV444) {
                if (image->yuvRange == AVIF_RANGE_LIMITED) {
                    RGBtoYUV = ARGBToI444;
                }
            }
        } else if (rgb->format == AVIF_RGB_FORMAT_BGR) {
            if (image->yuvFormat == AVIF_PIXEL_FORMAT_YUV420) {
                if (image->yuvRange == AVIF_RANGE_FULL) {
                    RGBtoYUV = RGB24ToJ420;
                } else {
                    RGBtoYUV = RGB24ToI420;
                }
            }
        } else if (rgb->format == AVIF_RGB_FORMAT_RGBA) {
            if (image->yuvFormat == AVIF_PIXEL_FORMAT_YUV420) {
                if (image->yuvRange == AVIF_RANGE_FULL) {
                    RGBtoYUV = avifABGRToJ420;
                } else {
                    RGBtoYUV = ABGRToI420;
                }
            }
        } else if (rgb->format == AVIF_RGB_FORMAT_ARGB) {
            if (image->yuvFormat == AVIF_PIXEL_FORMAT_YUV420) {
                if (image->yuvRange == AVIF_RANGE_LIMITED) {
                    RGBtoYUV = BGRAToI420;
                }
            }
        } else if (rgb->format == AVIF_RGB_FORMAT_ABGR) {
            if (image->yuvFormat == AVIF_PIXEL_FORMAT_YUV420) {
                if (image->yuvRange == AVIF_RANGE_LIMITED) {
                    RGBtoYUV = RGBAToI420;
                }
            }
        }
    }
    // TODO: Use SplitRGBPlane() for AVIF_MATRIX_COEFFICIENTS_IDENTITY if faster than the current implementation

    if (!RGBtoYUV) {
        return AVIF_RESULT_NOT_IMPLEMENTED;
    }
    if (RGBtoYUV(rgb->pixels,
                 rgb->rowBytes,
                 image->yuvPlanes[AVIF_CHAN_Y],
                 image->yuvRowBytes[AVIF_CHAN_Y],
                 image->yuvPlanes[AVIF_CHAN_U],
                 image->yuvRowBytes[AVIF_CHAN_U],
                 image->yuvPlanes[AVIF_CHAN_V],
                 image->yuvRowBytes[AVIF_CHAN_V],
                 image->width,
                 image->height)) {
        return AVIF_RESULT_REFORMAT_FAILED;
    }
    return AVIF_RESULT_OK;
}

static avifResult avifImageYUVToRGBLibYUV8bpc(const avifImage * image,
                                              avifRGBImage * rgb,
                                              const struct YuvConstants * matrixYUV,
                                              const struct YuvConstants * matrixYVU);
static avifResult avifImageYUVToRGBLibYUV10bpc(const avifImage * image,
                                               avifRGBImage * rgb,
                                               const struct YuvConstants * matrixYUV,
                                               const struct YuvConstants * matrixYVU);

avifResult avifImageYUVToRGBLibYUV(const avifImage * image, avifRGBImage * rgb)
{
    // See if the current settings can be accomplished with libyuv, and use it (if possible).

    if ((rgb->chromaUpsampling != AVIF_CHROMA_UPSAMPLING_AUTOMATIC) && (rgb->chromaUpsampling != AVIF_CHROMA_UPSAMPLING_FASTEST)) {
        // We do not ensure a specific upsampling filter is used when calling libyuv, so if the end
        // user chose a specific one, avoid using libyuv. Also libyuv trades a bit of accuracy for
        // speed, so if the end user requested best quality, avoid using libyuv as well.
        return AVIF_RESULT_NOT_IMPLEMENTED;
    }

    // Find the correct libyuv YuvConstants, based on range and CP/MC
    const struct YuvConstants * matrixYUV = NULL;
    const struct YuvConstants * matrixYVU = NULL;
    if (image->yuvRange == AVIF_RANGE_FULL) {
        switch (image->matrixCoefficients) {
            // BT.709 full range YuvConstants were added in libyuv version 1772.
            // See https://chromium-review.googlesource.com/c/libyuv/libyuv/+/2646472.
            case AVIF_MATRIX_COEFFICIENTS_BT709:
#if LIBYUV_VERSION >= 1772
                matrixYUV = &kYuvF709Constants;
                matrixYVU = &kYvuF709Constants;
#endif
                break;
            case AVIF_MATRIX_COEFFICIENTS_BT470BG:
            case AVIF_MATRIX_COEFFICIENTS_BT601:
            case AVIF_MATRIX_COEFFICIENTS_UNSPECIFIED:
                matrixYUV = &kYuvJPEGConstants;
                matrixYVU = &kYvuJPEGConstants;
                break;
            // BT.2020 full range YuvConstants were added in libyuv version 1775.
            // See https://chromium-review.googlesource.com/c/libyuv/libyuv/+/2678859.
            case AVIF_MATRIX_COEFFICIENTS_BT2020_NCL:
#if LIBYUV_VERSION >= 1775
                matrixYUV = &kYuvV2020Constants;
                matrixYVU = &kYvuV2020Constants;
#endif
                break;
            case AVIF_MATRIX_COEFFICIENTS_CHROMA_DERIVED_NCL:
                switch (image->colorPrimaries) {
                    case AVIF_COLOR_PRIMARIES_BT709:
                    case AVIF_COLOR_PRIMARIES_UNSPECIFIED:
#if LIBYUV_VERSION >= 1772
                        matrixYUV = &kYuvF709Constants;
                        matrixYVU = &kYvuF709Constants;
#endif
                        break;
                    case AVIF_COLOR_PRIMARIES_BT470BG:
                    case AVIF_COLOR_PRIMARIES_BT601:
                        matrixYUV = &kYuvJPEGConstants;
                        matrixYVU = &kYvuJPEGConstants;
                        break;
                    case AVIF_COLOR_PRIMARIES_BT2020:
#if LIBYUV_VERSION >= 1775
                        matrixYUV = &kYuvV2020Constants;
                        matrixYVU = &kYvuV2020Constants;
#endif
                        break;

                    case AVIF_COLOR_PRIMARIES_UNKNOWN:
                    case AVIF_COLOR_PRIMARIES_BT470M:
                    case AVIF_COLOR_PRIMARIES_SMPTE240:
                    case AVIF_COLOR_PRIMARIES_GENERIC_FILM:
                    case AVIF_COLOR_PRIMARIES_XYZ:
                    case AVIF_COLOR_PRIMARIES_SMPTE431:
                    case AVIF_COLOR_PRIMARIES_SMPTE432:
                    case AVIF_COLOR_PRIMARIES_EBU3213:
                        break;
                }
                break;

            case AVIF_MATRIX_COEFFICIENTS_IDENTITY:
            case AVIF_MATRIX_COEFFICIENTS_FCC:
            case AVIF_MATRIX_COEFFICIENTS_SMPTE240:
            case AVIF_MATRIX_COEFFICIENTS_YCGCO:
            case AVIF_MATRIX_COEFFICIENTS_BT2020_CL:
            case AVIF_MATRIX_COEFFICIENTS_SMPTE2085:
            case AVIF_MATRIX_COEFFICIENTS_CHROMA_DERIVED_CL:
            case AVIF_MATRIX_COEFFICIENTS_ICTCP:
                break;
        }
    } else {
        switch (image->matrixCoefficients) {
            case AVIF_MATRIX_COEFFICIENTS_BT709:
                matrixYUV = &kYuvH709Constants;
                matrixYVU = &kYvuH709Constants;
                break;
            case AVIF_MATRIX_COEFFICIENTS_BT470BG:
            case AVIF_MATRIX_COEFFICIENTS_BT601:
            case AVIF_MATRIX_COEFFICIENTS_UNSPECIFIED:
                matrixYUV = &kYuvI601Constants;
                matrixYVU = &kYvuI601Constants;
                break;
            case AVIF_MATRIX_COEFFICIENTS_BT2020_NCL:
                matrixYUV = &kYuv2020Constants;
                matrixYVU = &kYvu2020Constants;
                break;
            case AVIF_MATRIX_COEFFICIENTS_CHROMA_DERIVED_NCL:
                switch (image->colorPrimaries) {
                    case AVIF_COLOR_PRIMARIES_BT709:
                    case AVIF_COLOR_PRIMARIES_UNSPECIFIED:
                        matrixYUV = &kYuvH709Constants;
                        matrixYVU = &kYvuH709Constants;
                        break;
                    case AVIF_COLOR_PRIMARIES_BT470BG:
                    case AVIF_COLOR_PRIMARIES_BT601:
                        matrixYUV = &kYuvI601Constants;
                        matrixYVU = &kYvuI601Constants;
                        break;
                    case AVIF_COLOR_PRIMARIES_BT2020:
                        matrixYUV = &kYuv2020Constants;
                        matrixYVU = &kYvu2020Constants;
                        break;

                    case AVIF_COLOR_PRIMARIES_UNKNOWN:
                    case AVIF_COLOR_PRIMARIES_BT470M:
                    case AVIF_COLOR_PRIMARIES_SMPTE240:
                    case AVIF_COLOR_PRIMARIES_GENERIC_FILM:
                    case AVIF_COLOR_PRIMARIES_XYZ:
                    case AVIF_COLOR_PRIMARIES_SMPTE431:
                    case AVIF_COLOR_PRIMARIES_SMPTE432:
                    case AVIF_COLOR_PRIMARIES_EBU3213:
                        break;
                }
                break;
            case AVIF_MATRIX_COEFFICIENTS_IDENTITY:
            case AVIF_MATRIX_COEFFICIENTS_FCC:
            case AVIF_MATRIX_COEFFICIENTS_SMPTE240:
            case AVIF_MATRIX_COEFFICIENTS_YCGCO:
            case AVIF_MATRIX_COEFFICIENTS_BT2020_CL:
            case AVIF_MATRIX_COEFFICIENTS_SMPTE2085:
            case AVIF_MATRIX_COEFFICIENTS_CHROMA_DERIVED_CL:
            case AVIF_MATRIX_COEFFICIENTS_ICTCP:
                break;
        }
    }

    if (!matrixYVU) {
        // No YuvConstants exist for the current image; use the built-in YUV conversion
        return AVIF_RESULT_NOT_IMPLEMENTED;
    }

    if ((image->depth == 8) && (rgb->depth == 8)) {
        return avifImageYUVToRGBLibYUV8bpc(image, rgb, matrixYUV, matrixYVU);
    }

    if ((image->depth == 10) && (rgb->depth == 8)) {
        return avifImageYUVToRGBLibYUV10bpc(image, rgb, matrixYUV, matrixYVU);
    }

    // This function didn't do anything; use the built-in YUV conversion
    return AVIF_RESULT_NOT_IMPLEMENTED;
}

// These defines are used to create a NULL reference to libyuv functions that
// did not exist prior to a particular version of libyuv.
#if LIBYUV_VERSION < 1838
#define I422ToRGB565Matrix NULL
#endif
#if LIBYUV_VERSION < 1813
#define I422ToARGBMatrixFilter NULL
#define I420ToARGBMatrixFilter NULL
#define I210ToARGBMatrixFilter NULL
#define I010ToARGBMatrixFilter NULL
#endif
#if LIBYUV_VERSION < 1780
#define I410ToARGBMatrix NULL
#endif
#if LIBYUV_VERSION < 1756
#define I400ToARGBMatrix NULL
#endif

// Lookup table for isYVU. If the entry in this table is AVIF_TRUE, then it
// means that we are using a libyuv function with R and B channels swapped,
// which requires U and V planes also be swapped.
static const avifBool lutIsYVU[AVIF_RGB_FORMAT_COUNT] = {
    AVIF_TRUE,  // RGB
    AVIF_TRUE,  // RGBA
    AVIF_TRUE,  // ARGB
    AVIF_FALSE, // BGR
    AVIF_FALSE, // BGRA
    AVIF_FALSE, // ABGR
    AVIF_FALSE, // RGB_565
};

avifResult avifImageYUVToRGBLibYUV8bpc(const avifImage * image,
                                       avifRGBImage * rgb,
                                       const struct YuvConstants * matrixYUV,
                                       const struct YuvConstants * matrixYVU)
{
    // See if the current settings can be accomplished with libyuv, and use it (if possible).

    assert((image->depth == 8) && (rgb->depth == 8));

    // libavif uses byte-order when describing pixel formats, such that the R in RGBA is the lowest address,
    // similar to PNG. libyuv orders in word-order, so libavif's RGBA would be referred to in libyuv as ABGR.
    // In addition, swapping U and V in any of these calls, along with using the Yvu matrix instead of Yuv matrix,
    // swaps B and R in these orderings as well. This table summarizes the lookup tables that follow:
    //
    // libavif format            libyuv Func      UV matrix (and UV argument ordering)
    // --------------------      -------------    ------------------------------------
    // AVIF_RGB_FORMAT_RGB       *ToRGB24Matrix   matrixYVU
    // AVIF_RGB_FORMAT_RGBA      *ToARGBMatrix    matrixYVU
    // AVIF_RGB_FORMAT_ARGB      *ToRGBAMatrix    matrixYVU
    // AVIF_RGB_FORMAT_BGR       *ToRGB24Matrix   matrixYUV
    // AVIF_RGB_FORMAT_BGRA      *ToARGBMatrix    matrixYUV
    // AVIF_RGB_FORMAT_ABGR      *ToRGBAMatrix    matrixYUV
    // AVIF_RGB_FORMAT_RGB_565   *ToRGB565Matrix  matrixYUV

    avifBool isYVU = lutIsYVU[rgb->format];
    const struct YuvConstants * matrix = isYVU ? matrixYVU : matrixYUV;
    if (image->yuvFormat == AVIF_PIXEL_FORMAT_YUV400) {
        // Lookup table for YUV400 to RGB Matrix.
        typedef int (*YUV400ToRGBMatrix)(const uint8_t *, int, uint8_t *, int, const struct YuvConstants *, int, int);
        YUV400ToRGBMatrix lutYuv400ToRgbMatrix[AVIF_RGB_FORMAT_COUNT] = {
            NULL,             // RGB
            I400ToARGBMatrix, // RGBA
            NULL,             // ARGB
            NULL,             // BGR
            I400ToARGBMatrix, // BGRA
            NULL,             // ABGR
            NULL,             // RGB_565
        };
        YUV400ToRGBMatrix yuv400ToRgbMatrix = lutYuv400ToRgbMatrix[rgb->format];
        if (yuv400ToRgbMatrix != NULL) {
            if (yuv400ToRgbMatrix(image->yuvPlanes[AVIF_CHAN_Y],
                                  image->yuvRowBytes[AVIF_CHAN_Y],
                                  rgb->pixels,
                                  rgb->rowBytes,
                                  matrix,
                                  image->width,
                                  image->height) != 0) {
                return AVIF_RESULT_REFORMAT_FAILED;
            }
            return AVIF_RESULT_OK;
        }
    } else {
        int uPlaneIndex = isYVU ? AVIF_CHAN_V : AVIF_CHAN_U;
        int vPlaneIndex = isYVU ? AVIF_CHAN_U : AVIF_CHAN_V;
        // Lookup table for YUV To RGB Matrix (with filter).
        typedef int (*YUVToRGBMatrixFilter)(const uint8_t *,
                                            int,
                                            const uint8_t *,
                                            int,
                                            const uint8_t *,
                                            int,
                                            uint8_t *,
                                            int,
                                            const struct YuvConstants *,
                                            int,
                                            int,
                                            enum FilterMode);
        YUVToRGBMatrixFilter lutYuvToRgbMatrixFilter[AVIF_RGB_FORMAT_COUNT][AVIF_PIXEL_FORMAT_COUNT] = {
            { NULL, NULL, NULL, NULL, NULL },                                     // RGB
            { NULL, NULL, I422ToARGBMatrixFilter, I420ToARGBMatrixFilter, NULL }, // RGBA
            { NULL, NULL, NULL, NULL, NULL },                                     // ARGB
            { NULL, NULL, NULL, NULL, NULL },                                     // BGR
            { NULL, NULL, I422ToARGBMatrixFilter, I420ToARGBMatrixFilter, NULL }, // BGRA
            { NULL, NULL, NULL, NULL, NULL },                                     // ABGR
            { NULL, NULL, NULL, NULL, NULL },                                     // RGB_565
        };
        YUVToRGBMatrixFilter yuvToRgbMatrixFilter = lutYuvToRgbMatrixFilter[rgb->format][image->yuvFormat];
        if (yuvToRgbMatrixFilter != NULL) {
            // 'None' (Nearest neighbor) filter is faster than bilinear.
            enum FilterMode filter = (rgb->chromaUpsampling == AVIF_CHROMA_UPSAMPLING_FASTEST) ? kFilterNone : kFilterBilinear;
            if (yuvToRgbMatrixFilter(image->yuvPlanes[AVIF_CHAN_Y],
                                     image->yuvRowBytes[AVIF_CHAN_Y],
                                     image->yuvPlanes[uPlaneIndex],
                                     image->yuvRowBytes[uPlaneIndex],
                                     image->yuvPlanes[vPlaneIndex],
                                     image->yuvRowBytes[vPlaneIndex],
                                     rgb->pixels,
                                     rgb->rowBytes,
                                     matrix,
                                     image->width,
                                     image->height,
                                     filter) != 0) {
                return AVIF_RESULT_REFORMAT_FAILED;
            }
            return AVIF_RESULT_OK;
        }

        // Lookup table for YUV To RGB Matrix (without filter).
        typedef int (
            *YUVToRGBMatrix)(const uint8_t *, int, const uint8_t *, int, const uint8_t *, int, uint8_t *, int, const struct YuvConstants *, int, int);
        YUVToRGBMatrix lutYuvToRgbMatrix[AVIF_RGB_FORMAT_COUNT][AVIF_PIXEL_FORMAT_COUNT] = {
            { NULL, NULL, NULL, I420ToRGB24Matrix, NULL },                        // RGB
            { NULL, I444ToARGBMatrix, I422ToARGBMatrix, I420ToARGBMatrix, NULL }, // RGBA
            { NULL, NULL, I422ToRGBAMatrix, I420ToRGBAMatrix, NULL },             // ARGB
            { NULL, NULL, NULL, I420ToRGB24Matrix, NULL },                        // BGR
            { NULL, I444ToARGBMatrix, I422ToARGBMatrix, I420ToARGBMatrix, NULL }, // BGRA
            { NULL, NULL, I422ToRGBAMatrix, I420ToRGBAMatrix, NULL },             // ABGR
            { NULL, NULL, I422ToRGB565Matrix, I420ToRGB565Matrix, NULL },         // RGB_565
        };
        YUVToRGBMatrix yuvToRgbMatrix = lutYuvToRgbMatrix[rgb->format][image->yuvFormat];
        if (yuvToRgbMatrix != NULL) {
            if (yuvToRgbMatrix(image->yuvPlanes[AVIF_CHAN_Y],
                               image->yuvRowBytes[AVIF_CHAN_Y],
                               image->yuvPlanes[uPlaneIndex],
                               image->yuvRowBytes[uPlaneIndex],
                               image->yuvPlanes[vPlaneIndex],
                               image->yuvRowBytes[vPlaneIndex],
                               rgb->pixels,
                               rgb->rowBytes,
                               matrix,
                               image->width,
                               image->height) != 0) {
                return AVIF_RESULT_REFORMAT_FAILED;
            }
            return AVIF_RESULT_OK;
        }
    }

    // This function didn't do anything; use the built-in YUV conversion
    return AVIF_RESULT_NOT_IMPLEMENTED;
}

avifResult avifImageYUVToRGBLibYUV10bpc(const avifImage * image,
                                        avifRGBImage * rgb,
                                        const struct YuvConstants * matrixYUV,
                                        const struct YuvConstants * matrixYVU)
{
    // See if the current settings can be accomplished with libyuv, and use it (if possible).

    assert((image->depth == 10) && (rgb->depth == 8));

    // libavif uses byte-order when describing pixel formats, such that the R in RGBA is the lowest address,
    // similar to PNG. libyuv orders in word-order, so libavif's RGBA would be referred to in libyuv as ABGR.
    // In addition, swapping U and V in any of these calls, along with using the Yvu matrix instead of Yuv matrix,
    // swaps B and R in these orderings as well. This table summarizes the lookup tables that follow:
    //
    // libavif format        libyuv Func     UV matrix (and UV argument ordering)
    // --------------------  -------------   ------------------------------------
    // AVIF_RGB_FORMAT_RGB   n/a             n/a
    // AVIF_RGB_FORMAT_RGBA  *ToARGBMatrix   matrixYVU
    // AVIF_RGB_FORMAT_ARGB  n/a             n/a
    // AVIF_RGB_FORMAT_BGR   n/a             n/a
    // AVIF_RGB_FORMAT_BGRA  *ToARGBMatrix   matrixYUV
    // AVIF_RGB_FORMAT_ABGR  n/a             n/a
    // AVIF_RGB_FORMAT_565   n/a             n/a

    avifBool isYVU = lutIsYVU[rgb->format];
    const struct YuvConstants * matrix = isYVU ? matrixYVU : matrixYUV;
    int uPlaneIndex = isYVU ? AVIF_CHAN_V : AVIF_CHAN_U;
    int vPlaneIndex = isYVU ? AVIF_CHAN_U : AVIF_CHAN_V;

    // Lookup table for YUV To RGB Matrix (with filter).
    typedef int (*YUVToRGBMatrixFilter)(const uint16_t *,
                                        int,
                                        const uint16_t *,
                                        int,
                                        const uint16_t *,
                                        int,
                                        uint8_t *,
                                        int,
                                        const struct YuvConstants *,
                                        int,
                                        int,
                                        enum FilterMode);
    YUVToRGBMatrixFilter lutYuvToRgbMatrixFilter[AVIF_RGB_FORMAT_COUNT][AVIF_PIXEL_FORMAT_COUNT] = {
        { NULL, NULL, NULL, NULL, NULL },                                     // RGB
        { NULL, NULL, I210ToARGBMatrixFilter, I010ToARGBMatrixFilter, NULL }, // RGBA
        { NULL, NULL, NULL, NULL, NULL },                                     // ARGB
        { NULL, NULL, NULL, NULL, NULL },                                     // BGR
        { NULL, NULL, I210ToARGBMatrixFilter, I010ToARGBMatrixFilter, NULL }, // BGRA
        { NULL, NULL, NULL, NULL, NULL },                                     // ABGR
        { NULL, NULL, NULL, NULL, NULL },                                     // RGB_565
    };
    YUVToRGBMatrixFilter yuvToRgbMatrixFilter = lutYuvToRgbMatrixFilter[rgb->format][image->yuvFormat];
    if (yuvToRgbMatrixFilter != NULL) {
        // 'None' (Nearest neighbor) filter is faster than bilinear.
        enum FilterMode filter = (rgb->chromaUpsampling == AVIF_CHROMA_UPSAMPLING_FASTEST) ? kFilterNone : kFilterBilinear;
        if (yuvToRgbMatrixFilter((const uint16_t *)image->yuvPlanes[AVIF_CHAN_Y],
                                 image->yuvRowBytes[AVIF_CHAN_Y] / 2,
                                 (const uint16_t *)image->yuvPlanes[uPlaneIndex],
                                 image->yuvRowBytes[uPlaneIndex] / 2,
                                 (const uint16_t *)image->yuvPlanes[vPlaneIndex],
                                 image->yuvRowBytes[vPlaneIndex] / 2,
                                 rgb->pixels,
                                 rgb->rowBytes,
                                 matrix,
                                 image->width,
                                 image->height,
                                 filter) != 0) {
            return AVIF_RESULT_REFORMAT_FAILED;
        }
        return AVIF_RESULT_OK;
    }

    // Lookup table for YUV To RGB Matrix (without filter).
    typedef int (
        *YUVToRGBMatrix)(const uint16_t *, int, const uint16_t *, int, const uint16_t *, int, uint8_t *, int, const struct YuvConstants *, int, int);
    YUVToRGBMatrix lutYuvToRgbMatrix[AVIF_RGB_FORMAT_COUNT][AVIF_PIXEL_FORMAT_COUNT] = {
        { NULL, NULL, NULL, NULL, NULL },                                     // RGB
        { NULL, I410ToARGBMatrix, I210ToARGBMatrix, I010ToARGBMatrix, NULL }, // RGBA
        { NULL, NULL, NULL, NULL, NULL },                                     // ARGB
        { NULL, NULL, NULL, NULL, NULL },                                     // BGR
        { NULL, I410ToARGBMatrix, I210ToARGBMatrix, I010ToARGBMatrix, NULL }, // BGRA
        { NULL, NULL, NULL, NULL, NULL },                                     // ABGR
        { NULL, NULL, NULL, NULL, NULL },                                     // RGB_565
    };
    YUVToRGBMatrix yuvToRgbMatrix = lutYuvToRgbMatrix[rgb->format][image->yuvFormat];
    if (yuvToRgbMatrix != NULL) {
        if (yuvToRgbMatrix((const uint16_t *)image->yuvPlanes[AVIF_CHAN_Y],
                           image->yuvRowBytes[AVIF_CHAN_Y] / 2,
                           (const uint16_t *)image->yuvPlanes[uPlaneIndex],
                           image->yuvRowBytes[uPlaneIndex] / 2,
                           (const uint16_t *)image->yuvPlanes[vPlaneIndex],
                           image->yuvRowBytes[vPlaneIndex] / 2,
                           rgb->pixels,
                           rgb->rowBytes,
                           matrix,
                           image->width,
                           image->height) != 0) {
            return AVIF_RESULT_REFORMAT_FAILED;
        }
        return AVIF_RESULT_OK;
    }

    return AVIF_RESULT_NOT_IMPLEMENTED;
}

avifResult avifRGBImagePremultiplyAlphaLibYUV(avifRGBImage * rgb)
{
    // See if the current settings can be accomplished with libyuv, and use it (if possible).

    if (rgb->depth != 8) {
        return AVIF_RESULT_NOT_IMPLEMENTED;
    }

    // libavif uses byte-order when describing pixel formats, such that the R in RGBA is the lowest address,
    // similar to PNG. libyuv orders in word-order, so libavif's RGBA would be referred to in libyuv as ABGR.

    // Order of RGB doesn't matter here.
    if (rgb->format == AVIF_RGB_FORMAT_RGBA || rgb->format == AVIF_RGB_FORMAT_BGRA) {
        if (ARGBAttenuate(rgb->pixels, rgb->rowBytes, rgb->pixels, rgb->rowBytes, rgb->width, rgb->height) != 0) {
            return AVIF_RESULT_REFORMAT_FAILED;
        }
        return AVIF_RESULT_OK;
    }

    return AVIF_RESULT_NOT_IMPLEMENTED;
}

avifResult avifRGBImageUnpremultiplyAlphaLibYUV(avifRGBImage * rgb)
{
    // See if the current settings can be accomplished with libyuv, and use it (if possible).

    if (rgb->depth != 8) {
        return AVIF_RESULT_NOT_IMPLEMENTED;
    }

    // libavif uses byte-order when describing pixel formats, such that the R in RGBA is the lowest address,
    // similar to PNG. libyuv orders in word-order, so libavif's RGBA would be referred to in libyuv as ABGR.

    if (rgb->format == AVIF_RGB_FORMAT_RGBA || rgb->format == AVIF_RGB_FORMAT_BGRA) {
        if (ARGBUnattenuate(rgb->pixels, rgb->rowBytes, rgb->pixels, rgb->rowBytes, rgb->width, rgb->height) != 0) {
            return AVIF_RESULT_REFORMAT_FAILED;
        }
        return AVIF_RESULT_OK;
    }

    return AVIF_RESULT_NOT_IMPLEMENTED;
}

avifResult avifRGBImageToF16LibYUV(avifRGBImage * rgb)
{
    const float scale = 1.0f / ((1 << rgb->depth) - 1);
    const int result = HalfFloatPlane((const uint16_t *)rgb->pixels,
                                      rgb->rowBytes,
                                      (uint16_t *)rgb->pixels,
                                      rgb->rowBytes,
                                      scale,
                                      rgb->width * avifRGBFormatChannelCount(rgb->format),
                                      rgb->height);
    return (result == 0) ? AVIF_RESULT_OK : AVIF_RESULT_INVALID_ARGUMENT;
}

unsigned int avifLibYUVVersion(void)
{
    return (unsigned int)LIBYUV_VERSION;
}

#endif
