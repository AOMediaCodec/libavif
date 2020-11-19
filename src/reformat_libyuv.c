// Copyright 2019 Joe Drago. All rights reserved.
// SPDX-License-Identifier: BSD-2-Clause

#include "avif/internal.h"

#if !defined(AVIF_LIBYUV_ENABLED)

// No libyuv!
avifResult avifImageYUVToRGBLibYUV(const avifImage * image, avifRGBImage * rgb)
{
    (void)image;
    (void)rgb;
    return AVIF_RESULT_NOT_IMPLEMENTED;
}
unsigned int avifLibYUVVersion(void)
{
    return 0;
}

#else

#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wstrict-prototypes" // "this function declaration is not a prototype"
#endif
#include <libyuv.h>
#if defined(__clang__)
#pragma clang diagnostic pop
#endif

avifResult avifImageYUVToRGBLibYUV(const avifImage * image, avifRGBImage * rgb)
{
    // See if the current settings can be accomplished with libyuv, and use it (if possible).

    if ((image->depth != 8) || (rgb->depth != 8)) {
        return AVIF_RESULT_NOT_IMPLEMENTED;
    }

    if ((rgb->chromaUpsampling != AVIF_CHROMA_UPSAMPLING_AUTOMATIC) && (rgb->chromaUpsampling != AVIF_CHROMA_UPSAMPLING_FASTEST)) {
        // libyuv uses its own upsampling filter. If the enduser chose a specific one, avoid using libyuv.
        return AVIF_RESULT_NOT_IMPLEMENTED;
    }

    // Find the correct libyuv YuvConstants, based on range and CP/MC
    const struct YuvConstants * matrixYUV = NULL;
    const struct YuvConstants * matrixYVU = NULL;
    if (image->yuvRange == AVIF_RANGE_FULL) {
        switch (image->matrixCoefficients) {
            case AVIF_MATRIX_COEFFICIENTS_BT470BG:
            case AVIF_MATRIX_COEFFICIENTS_BT601:
            case AVIF_MATRIX_COEFFICIENTS_UNSPECIFIED:
                matrixYUV = &kYuvJPEGConstants;
                matrixYVU = &kYvuJPEGConstants;
                break;
            case AVIF_MATRIX_COEFFICIENTS_CHROMA_DERIVED_NCL:
                switch (image->colorPrimaries) {
                    case AVIF_COLOR_PRIMARIES_BT470BG:
                    case AVIF_COLOR_PRIMARIES_BT601:
                        matrixYUV = &kYuvJPEGConstants;
                        matrixYVU = &kYvuJPEGConstants;
                        break;

                    case AVIF_COLOR_PRIMARIES_UNSPECIFIED:
                    case AVIF_COLOR_PRIMARIES_UNKNOWN:
                    case AVIF_COLOR_PRIMARIES_BT709:
                    case AVIF_COLOR_PRIMARIES_BT470M:
                    case AVIF_COLOR_PRIMARIES_SMPTE240:
                    case AVIF_COLOR_PRIMARIES_GENERIC_FILM:
                    case AVIF_COLOR_PRIMARIES_BT2020:
                    case AVIF_COLOR_PRIMARIES_XYZ:
                    case AVIF_COLOR_PRIMARIES_SMPTE431:
                    case AVIF_COLOR_PRIMARIES_SMPTE432:
                    case AVIF_COLOR_PRIMARIES_EBU3213:
                        break;
                }
                break;

            case AVIF_MATRIX_COEFFICIENTS_IDENTITY:
            case AVIF_MATRIX_COEFFICIENTS_BT709:
            case AVIF_MATRIX_COEFFICIENTS_FCC:
            case AVIF_MATRIX_COEFFICIENTS_SMPTE240:
            case AVIF_MATRIX_COEFFICIENTS_YCGCO:
            case AVIF_MATRIX_COEFFICIENTS_BT2020_NCL:
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

    // This following section might be a bit complicated to audit without a bit of explanation:
    //
    // libavif uses byte-order when describing pixel formats, such that the R in RGBA is the lowest address,
    // similar to PNG. libyuv orders in word-order, so libavif's RGBA would be referred to in libyuv as ABGR.
    // In addition, swapping U and V in any of these calls, along with using the Yvu matrix instead of Yuv matrix,
    // swaps B and R in these orderings as well. This table summarizes this block's intent:
    //
    // libavif format        libyuv Func     UV matrix (and UV argument ordering)
    // --------------------  -------------   ------------------------------------
    // AVIF_RGB_FORMAT_RGB   n/a             n/a
    // AVIF_RGB_FORMAT_BGR   n/a             n/a
    // AVIF_RGB_FORMAT_BGRA  *ToARGBMatrix   matrixYUV
    // AVIF_RGB_FORMAT_RGBA  *ToARGBMatrix   matrixYVU
    // AVIF_RGB_FORMAT_ABGR  *ToRGBAMatrix   matrixYUV
    // AVIF_RGB_FORMAT_ARGB  *ToRGBAMatrix   matrixYVU

    if (rgb->format == AVIF_RGB_FORMAT_BGRA) {
        // AVIF_RGB_FORMAT_BGRA  *ToARGBMatrix   matrixYUV

        if (image->yuvFormat == AVIF_PIXEL_FORMAT_YUV444) {
            if (I444ToARGBMatrix(image->yuvPlanes[AVIF_CHAN_Y],
                                 image->yuvRowBytes[AVIF_CHAN_Y],
                                 image->yuvPlanes[AVIF_CHAN_U],
                                 image->yuvRowBytes[AVIF_CHAN_U],
                                 image->yuvPlanes[AVIF_CHAN_V],
                                 image->yuvRowBytes[AVIF_CHAN_V],
                                 rgb->pixels,
                                 rgb->rowBytes,
                                 matrixYUV,
                                 image->width,
                                 image->height) != 0) {
                return AVIF_RESULT_REFORMAT_FAILED;
            }
            return AVIF_RESULT_OK;
        } else if (image->yuvFormat == AVIF_PIXEL_FORMAT_YUV422) {
            if (I422ToARGBMatrix(image->yuvPlanes[AVIF_CHAN_Y],
                                 image->yuvRowBytes[AVIF_CHAN_Y],
                                 image->yuvPlanes[AVIF_CHAN_U],
                                 image->yuvRowBytes[AVIF_CHAN_U],
                                 image->yuvPlanes[AVIF_CHAN_V],
                                 image->yuvRowBytes[AVIF_CHAN_V],
                                 rgb->pixels,
                                 rgb->rowBytes,
                                 matrixYUV,
                                 image->width,
                                 image->height) != 0) {
                return AVIF_RESULT_REFORMAT_FAILED;
            }
            return AVIF_RESULT_OK;
        } else if (image->yuvFormat == AVIF_PIXEL_FORMAT_YUV420) {
            if (I420ToARGBMatrix(image->yuvPlanes[AVIF_CHAN_Y],
                                 image->yuvRowBytes[AVIF_CHAN_Y],
                                 image->yuvPlanes[AVIF_CHAN_U],
                                 image->yuvRowBytes[AVIF_CHAN_U],
                                 image->yuvPlanes[AVIF_CHAN_V],
                                 image->yuvRowBytes[AVIF_CHAN_V],
                                 rgb->pixels,
                                 rgb->rowBytes,
                                 matrixYUV,
                                 image->width,
                                 image->height) != 0) {
                return AVIF_RESULT_REFORMAT_FAILED;
            }
            return AVIF_RESULT_OK;
        } else if (image->yuvFormat == AVIF_PIXEL_FORMAT_YUV400) {
            if (I400ToARGBMatrix(image->yuvPlanes[AVIF_CHAN_Y],
                                 image->yuvRowBytes[AVIF_CHAN_Y],
                                 rgb->pixels,
                                 rgb->rowBytes,
                                 matrixYUV,
                                 image->width,
                                 image->height) != 0) {
                return AVIF_RESULT_REFORMAT_FAILED;
            }
            return AVIF_RESULT_OK;
        }
    } else if (rgb->format == AVIF_RGB_FORMAT_RGBA) {
        // AVIF_RGB_FORMAT_RGBA  *ToARGBMatrix   matrixYVU

        if (image->yuvFormat == AVIF_PIXEL_FORMAT_YUV444) {
            if (I444ToARGBMatrix(image->yuvPlanes[AVIF_CHAN_Y],
                                 image->yuvRowBytes[AVIF_CHAN_Y],
                                 image->yuvPlanes[AVIF_CHAN_V],
                                 image->yuvRowBytes[AVIF_CHAN_V],
                                 image->yuvPlanes[AVIF_CHAN_U],
                                 image->yuvRowBytes[AVIF_CHAN_U],
                                 rgb->pixels,
                                 rgb->rowBytes,
                                 matrixYVU,
                                 image->width,
                                 image->height) != 0) {
                return AVIF_RESULT_REFORMAT_FAILED;
            }
            return AVIF_RESULT_OK;
        } else if (image->yuvFormat == AVIF_PIXEL_FORMAT_YUV422) {
            if (I422ToARGBMatrix(image->yuvPlanes[AVIF_CHAN_Y],
                                 image->yuvRowBytes[AVIF_CHAN_Y],
                                 image->yuvPlanes[AVIF_CHAN_V],
                                 image->yuvRowBytes[AVIF_CHAN_V],
                                 image->yuvPlanes[AVIF_CHAN_U],
                                 image->yuvRowBytes[AVIF_CHAN_U],
                                 rgb->pixels,
                                 rgb->rowBytes,
                                 matrixYVU,
                                 image->width,
                                 image->height) != 0) {
                return AVIF_RESULT_REFORMAT_FAILED;
            }
            return AVIF_RESULT_OK;
        } else if (image->yuvFormat == AVIF_PIXEL_FORMAT_YUV420) {
            if (I420ToARGBMatrix(image->yuvPlanes[AVIF_CHAN_Y],
                                 image->yuvRowBytes[AVIF_CHAN_Y],
                                 image->yuvPlanes[AVIF_CHAN_V],
                                 image->yuvRowBytes[AVIF_CHAN_V],
                                 image->yuvPlanes[AVIF_CHAN_U],
                                 image->yuvRowBytes[AVIF_CHAN_U],
                                 rgb->pixels,
                                 rgb->rowBytes,
                                 matrixYVU,
                                 image->width,
                                 image->height) != 0) {
                return AVIF_RESULT_REFORMAT_FAILED;
            }
            return AVIF_RESULT_OK;
        } else if (image->yuvFormat == AVIF_PIXEL_FORMAT_YUV400) {
            if (I400ToARGBMatrix(image->yuvPlanes[AVIF_CHAN_Y],
                                 image->yuvRowBytes[AVIF_CHAN_Y],
                                 rgb->pixels,
                                 rgb->rowBytes,
                                 matrixYVU,
                                 image->width,
                                 image->height) != 0) {
                return AVIF_RESULT_REFORMAT_FAILED;
            }
            return AVIF_RESULT_OK;
        }
    } else if (rgb->format == AVIF_RGB_FORMAT_ABGR) {
        // AVIF_RGB_FORMAT_ABGR  *ToRGBAMatrix   matrixYUV

        if (image->yuvFormat == AVIF_PIXEL_FORMAT_YUV444) {
            // This doesn't currently exist in libyuv
#if 0
            if (I444ToRGBAMatrix(image->yuvPlanes[AVIF_CHAN_Y],
                                 image->yuvRowBytes[AVIF_CHAN_Y],
                                 image->yuvPlanes[AVIF_CHAN_U],
                                 image->yuvRowBytes[AVIF_CHAN_U],
                                 image->yuvPlanes[AVIF_CHAN_V],
                                 image->yuvRowBytes[AVIF_CHAN_V],
                                 rgb->pixels,
                                 rgb->rowBytes,
                                 matrixYUV,
                                 image->width,
                                 image->height) != 0) {
                return AVIF_RESULT_REFORMAT_FAILED;
            }
            return AVIF_RESULT_OK;
#endif
        } else if (image->yuvFormat == AVIF_PIXEL_FORMAT_YUV422) {
            if (I422ToRGBAMatrix(image->yuvPlanes[AVIF_CHAN_Y],
                                 image->yuvRowBytes[AVIF_CHAN_Y],
                                 image->yuvPlanes[AVIF_CHAN_U],
                                 image->yuvRowBytes[AVIF_CHAN_U],
                                 image->yuvPlanes[AVIF_CHAN_V],
                                 image->yuvRowBytes[AVIF_CHAN_V],
                                 rgb->pixels,
                                 rgb->rowBytes,
                                 matrixYUV,
                                 image->width,
                                 image->height) != 0) {
                return AVIF_RESULT_REFORMAT_FAILED;
            }
            return AVIF_RESULT_OK;
        } else if (image->yuvFormat == AVIF_PIXEL_FORMAT_YUV420) {
            if (I420ToRGBAMatrix(image->yuvPlanes[AVIF_CHAN_Y],
                                 image->yuvRowBytes[AVIF_CHAN_Y],
                                 image->yuvPlanes[AVIF_CHAN_U],
                                 image->yuvRowBytes[AVIF_CHAN_U],
                                 image->yuvPlanes[AVIF_CHAN_V],
                                 image->yuvRowBytes[AVIF_CHAN_V],
                                 rgb->pixels,
                                 rgb->rowBytes,
                                 matrixYUV,
                                 image->width,
                                 image->height) != 0) {
                return AVIF_RESULT_REFORMAT_FAILED;
            }
            return AVIF_RESULT_OK;
        } else if (image->yuvFormat == AVIF_PIXEL_FORMAT_YUV400) {
            // This doesn't currently exist in libyuv
#if 0
            if (I400ToRGBAMatrix(image->yuvPlanes[AVIF_CHAN_Y],
                                 image->yuvRowBytes[AVIF_CHAN_Y],
                                 rgb->pixels,
                                 rgb->rowBytes,
                                 matrixYUV,
                                 image->width,
                                 image->height) != 0) {
                return AVIF_RESULT_REFORMAT_FAILED;
            }
            return AVIF_RESULT_OK;
#endif
        }
    } else if (rgb->format == AVIF_RGB_FORMAT_ARGB) {
        // AVIF_RGB_FORMAT_ARGB  *ToRGBAMatrix   matrixYVU

        if (image->yuvFormat == AVIF_PIXEL_FORMAT_YUV444) {
            // This doesn't currently exist in libyuv
#if 0
            if (I444ToRGBAMatrix(image->yuvPlanes[AVIF_CHAN_Y],
                                 image->yuvRowBytes[AVIF_CHAN_Y],
                                 image->yuvPlanes[AVIF_CHAN_V],
                                 image->yuvRowBytes[AVIF_CHAN_V],
                                 image->yuvPlanes[AVIF_CHAN_U],
                                 image->yuvRowBytes[AVIF_CHAN_U],
                                 rgb->pixels,
                                 rgb->rowBytes,
                                 matrixYVU,
                                 image->width,
                                 image->height) != 0) {
                return AVIF_RESULT_REFORMAT_FAILED;
            }
            return AVIF_RESULT_OK;
#endif
        } else if (image->yuvFormat == AVIF_PIXEL_FORMAT_YUV422) {
            if (I422ToRGBAMatrix(image->yuvPlanes[AVIF_CHAN_Y],
                                 image->yuvRowBytes[AVIF_CHAN_Y],
                                 image->yuvPlanes[AVIF_CHAN_V],
                                 image->yuvRowBytes[AVIF_CHAN_V],
                                 image->yuvPlanes[AVIF_CHAN_U],
                                 image->yuvRowBytes[AVIF_CHAN_U],
                                 rgb->pixels,
                                 rgb->rowBytes,
                                 matrixYVU,
                                 image->width,
                                 image->height) != 0) {
                return AVIF_RESULT_REFORMAT_FAILED;
            }
            return AVIF_RESULT_OK;
        } else if (image->yuvFormat == AVIF_PIXEL_FORMAT_YUV420) {
            if (I420ToRGBAMatrix(image->yuvPlanes[AVIF_CHAN_Y],
                                 image->yuvRowBytes[AVIF_CHAN_Y],
                                 image->yuvPlanes[AVIF_CHAN_V],
                                 image->yuvRowBytes[AVIF_CHAN_V],
                                 image->yuvPlanes[AVIF_CHAN_U],
                                 image->yuvRowBytes[AVIF_CHAN_U],
                                 rgb->pixels,
                                 rgb->rowBytes,
                                 matrixYVU,
                                 image->width,
                                 image->height) != 0) {
                return AVIF_RESULT_REFORMAT_FAILED;
            }
            return AVIF_RESULT_OK;
        } else if (image->yuvFormat == AVIF_PIXEL_FORMAT_YUV400) {
            // This doesn't currently exist in libyuv
#if 0
            if (I400ToRGBAMatrix(image->yuvPlanes[AVIF_CHAN_Y],
                                 image->yuvRowBytes[AVIF_CHAN_Y],
                                 rgb->pixels,
                                 rgb->rowBytes,
                                 matrixYVU,
                                 image->width,
                                 image->height) != 0) {
                return AVIF_RESULT_REFORMAT_FAILED;
            }
            return AVIF_RESULT_OK;
#endif
        }
    }

    // This function didn't do anything; use the built-in YUV conversion
    return AVIF_RESULT_NOT_IMPLEMENTED;
}

unsigned int avifLibYUVVersion(void)
{
    return (unsigned int)LIBYUV_VERSION;
}

#endif
