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
                if (image->yuvRange == AVIF_RANGE_LIMITED) {
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

avifResult avifImageYUVToRGBLibYUV8bpc(const avifImage * image,
                                       avifRGBImage * rgb,
                                       const struct YuvConstants * matrixYUV,
                                       const struct YuvConstants * matrixYVU)
{
    // See if the current settings can be accomplished with libyuv, and use it (if possible).

    assert((image->depth == 8) && (rgb->depth == 8));

#if LIBYUV_VERSION >= 1813
    enum FilterMode filter = kFilterBilinear;
    if (rgb->chromaUpsampling == AVIF_CHROMA_UPSAMPLING_FASTEST) {
        // 'None' (Nearest neighbor) filter is faster than bilinear.
        filter = kFilterNone;
    }
#endif

    // This following section might be a bit complicated to audit without a bit of explanation:
    //
    // libavif uses byte-order when describing pixel formats, such that the R in RGBA is the lowest address,
    // similar to PNG. libyuv orders in word-order, so libavif's RGBA would be referred to in libyuv as ABGR.
    // In addition, swapping U and V in any of these calls, along with using the Yvu matrix instead of Yuv matrix,
    // swaps B and R in these orderings as well. This table summarizes this block's intent:
    //
    // libavif format        libyuv Func     UV matrix (and UV argument ordering)
    // --------------------  -------------   ------------------------------------
    // AVIF_RGB_FORMAT_RGB   *ToRGB24Matrix  matrixYVU
    // AVIF_RGB_FORMAT_BGR   *ToRGB24Matrix  matrixYUV
    // AVIF_RGB_FORMAT_BGRA  *ToARGBMatrix   matrixYUV
    // AVIF_RGB_FORMAT_RGBA  *ToARGBMatrix   matrixYVU
    // AVIF_RGB_FORMAT_ABGR  *ToRGBAMatrix   matrixYUV
    // AVIF_RGB_FORMAT_ARGB  *ToRGBAMatrix   matrixYVU

    if (rgb->format == AVIF_RGB_FORMAT_RGB) {
        // AVIF_RGB_FORMAT_RGB   *ToRGB24Matrix  matrixYVU

        if (image->yuvFormat == AVIF_PIXEL_FORMAT_YUV420) {
            if (I420ToRGB24Matrix(image->yuvPlanes[AVIF_CHAN_Y],
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
        }
    } else if (rgb->format == AVIF_RGB_FORMAT_BGR) {
        // AVIF_RGB_FORMAT_BGR   *ToRGB24Matrix  matrixYUV

        if (image->yuvFormat == AVIF_PIXEL_FORMAT_YUV420) {
            if (I420ToRGB24Matrix(image->yuvPlanes[AVIF_CHAN_Y],
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
        }
    } else if (rgb->format == AVIF_RGB_FORMAT_BGRA) {
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
#if LIBYUV_VERSION >= 1813
            if (I422ToARGBMatrixFilter(image->yuvPlanes[AVIF_CHAN_Y],
                                       image->yuvRowBytes[AVIF_CHAN_Y],
                                       image->yuvPlanes[AVIF_CHAN_U],
                                       image->yuvRowBytes[AVIF_CHAN_U],
                                       image->yuvPlanes[AVIF_CHAN_V],
                                       image->yuvRowBytes[AVIF_CHAN_V],
                                       rgb->pixels,
                                       rgb->rowBytes,
                                       matrixYUV,
                                       image->width,
                                       image->height,
                                       filter) != 0) {
                return AVIF_RESULT_REFORMAT_FAILED;
            }
#else
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
#endif
            return AVIF_RESULT_OK;
        } else if (image->yuvFormat == AVIF_PIXEL_FORMAT_YUV420) {
#if LIBYUV_VERSION >= 1813
            if (I420ToARGBMatrixFilter(image->yuvPlanes[AVIF_CHAN_Y],
                                       image->yuvRowBytes[AVIF_CHAN_Y],
                                       image->yuvPlanes[AVIF_CHAN_U],
                                       image->yuvRowBytes[AVIF_CHAN_U],
                                       image->yuvPlanes[AVIF_CHAN_V],
                                       image->yuvRowBytes[AVIF_CHAN_V],
                                       rgb->pixels,
                                       rgb->rowBytes,
                                       matrixYUV,
                                       image->width,
                                       image->height,
                                       filter) != 0) {
                return AVIF_RESULT_REFORMAT_FAILED;
            }
#else
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
#endif
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
#if LIBYUV_VERSION >= 1813
            if (I422ToARGBMatrixFilter(image->yuvPlanes[AVIF_CHAN_Y],
                                       image->yuvRowBytes[AVIF_CHAN_Y],
                                       image->yuvPlanes[AVIF_CHAN_V],
                                       image->yuvRowBytes[AVIF_CHAN_V],
                                       image->yuvPlanes[AVIF_CHAN_U],
                                       image->yuvRowBytes[AVIF_CHAN_U],
                                       rgb->pixels,
                                       rgb->rowBytes,
                                       matrixYVU,
                                       image->width,
                                       image->height,
                                       filter) != 0) {
                return AVIF_RESULT_REFORMAT_FAILED;
            }
#else
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
#endif
            return AVIF_RESULT_OK;
        } else if (image->yuvFormat == AVIF_PIXEL_FORMAT_YUV420) {
#if LIBYUV_VERSION >= 1813
            if (I420ToARGBMatrixFilter(image->yuvPlanes[AVIF_CHAN_Y],
                                       image->yuvRowBytes[AVIF_CHAN_Y],
                                       image->yuvPlanes[AVIF_CHAN_V],
                                       image->yuvRowBytes[AVIF_CHAN_V],
                                       image->yuvPlanes[AVIF_CHAN_U],
                                       image->yuvRowBytes[AVIF_CHAN_U],
                                       rgb->pixels,
                                       rgb->rowBytes,
                                       matrixYVU,
                                       image->width,
                                       image->height,
                                       filter) != 0) {
                return AVIF_RESULT_REFORMAT_FAILED;
            }
#else
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
#endif
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

avifResult avifImageYUVToRGBLibYUV10bpc(const avifImage * image,
                                        avifRGBImage * rgb,
                                        const struct YuvConstants * matrixYUV,
                                        const struct YuvConstants * matrixYVU)
{
    // See if the current settings can be accomplished with libyuv, and use it (if possible).

    assert((image->depth == 10) && (rgb->depth == 8));

#if LIBYUV_VERSION >= 1813
    enum FilterMode filter = kFilterBilinear;
    if (rgb->chromaUpsampling == AVIF_CHROMA_UPSAMPLING_FASTEST) {
        // 'None' (Nearest neighbor) filter is faster than bilinear.
        filter = kFilterNone;
    }
#endif

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
#if LIBYUV_VERSION >= 1780
            if (I410ToARGBMatrix((const uint16_t *)image->yuvPlanes[AVIF_CHAN_Y],
                                 image->yuvRowBytes[AVIF_CHAN_Y] / 2,
                                 (const uint16_t *)image->yuvPlanes[AVIF_CHAN_U],
                                 image->yuvRowBytes[AVIF_CHAN_U] / 2,
                                 (const uint16_t *)image->yuvPlanes[AVIF_CHAN_V],
                                 image->yuvRowBytes[AVIF_CHAN_V] / 2,
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
#if LIBYUV_VERSION >= 1813
            if (I210ToARGBMatrixFilter((const uint16_t *)image->yuvPlanes[AVIF_CHAN_Y],
                                       image->yuvRowBytes[AVIF_CHAN_Y] / 2,
                                       (const uint16_t *)image->yuvPlanes[AVIF_CHAN_U],
                                       image->yuvRowBytes[AVIF_CHAN_U] / 2,
                                       (const uint16_t *)image->yuvPlanes[AVIF_CHAN_V],
                                       image->yuvRowBytes[AVIF_CHAN_V] / 2,
                                       rgb->pixels,
                                       rgb->rowBytes,
                                       matrixYUV,
                                       image->width,
                                       image->height,
                                       filter) != 0) {
                return AVIF_RESULT_REFORMAT_FAILED;
            }
#else
            if (I210ToARGBMatrix((const uint16_t *)image->yuvPlanes[AVIF_CHAN_Y],
                                 image->yuvRowBytes[AVIF_CHAN_Y] / 2,
                                 (const uint16_t *)image->yuvPlanes[AVIF_CHAN_U],
                                 image->yuvRowBytes[AVIF_CHAN_U] / 2,
                                 (const uint16_t *)image->yuvPlanes[AVIF_CHAN_V],
                                 image->yuvRowBytes[AVIF_CHAN_V] / 2,
                                 rgb->pixels,
                                 rgb->rowBytes,
                                 matrixYUV,
                                 image->width,
                                 image->height) != 0) {
                return AVIF_RESULT_REFORMAT_FAILED;
            }
#endif
            return AVIF_RESULT_OK;
        } else if (image->yuvFormat == AVIF_PIXEL_FORMAT_YUV420) {
#if LIBYUV_VERSION >= 1813
            if (I010ToARGBMatrixFilter((const uint16_t *)image->yuvPlanes[AVIF_CHAN_Y],
                                       image->yuvRowBytes[AVIF_CHAN_Y] / 2,
                                       (const uint16_t *)image->yuvPlanes[AVIF_CHAN_U],
                                       image->yuvRowBytes[AVIF_CHAN_U] / 2,
                                       (const uint16_t *)image->yuvPlanes[AVIF_CHAN_V],
                                       image->yuvRowBytes[AVIF_CHAN_V] / 2,
                                       rgb->pixels,
                                       rgb->rowBytes,
                                       matrixYUV,
                                       image->width,
                                       image->height,
                                       filter) != 0) {
                return AVIF_RESULT_REFORMAT_FAILED;
            }
#else
            if (I010ToARGBMatrix((const uint16_t *)image->yuvPlanes[AVIF_CHAN_Y],
                                 image->yuvRowBytes[AVIF_CHAN_Y] / 2,
                                 (const uint16_t *)image->yuvPlanes[AVIF_CHAN_U],
                                 image->yuvRowBytes[AVIF_CHAN_U] / 2,
                                 (const uint16_t *)image->yuvPlanes[AVIF_CHAN_V],
                                 image->yuvRowBytes[AVIF_CHAN_V] / 2,
                                 rgb->pixels,
                                 rgb->rowBytes,
                                 matrixYUV,
                                 image->width,
                                 image->height) != 0) {
                return AVIF_RESULT_REFORMAT_FAILED;
            }
#endif
            return AVIF_RESULT_OK;
        } else if (image->yuvFormat == AVIF_PIXEL_FORMAT_YUV400) {
            // This doesn't currently exist in libyuv
        }
    } else if (rgb->format == AVIF_RGB_FORMAT_RGBA) {
        // AVIF_RGB_FORMAT_RGBA  *ToARGBMatrix   matrixYVU

        if (image->yuvFormat == AVIF_PIXEL_FORMAT_YUV444) {
#if LIBYUV_VERSION >= 1780
            if (I410ToARGBMatrix((const uint16_t *)image->yuvPlanes[AVIF_CHAN_Y],
                                 image->yuvRowBytes[AVIF_CHAN_Y] / 2,
                                 (const uint16_t *)image->yuvPlanes[AVIF_CHAN_V],
                                 image->yuvRowBytes[AVIF_CHAN_V] / 2,
                                 (const uint16_t *)image->yuvPlanes[AVIF_CHAN_U],
                                 image->yuvRowBytes[AVIF_CHAN_U] / 2,
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
#if LIBYUV_VERSION >= 1813
            if (I210ToARGBMatrixFilter((const uint16_t *)image->yuvPlanes[AVIF_CHAN_Y],
                                       image->yuvRowBytes[AVIF_CHAN_Y] / 2,
                                       (const uint16_t *)image->yuvPlanes[AVIF_CHAN_V],
                                       image->yuvRowBytes[AVIF_CHAN_V] / 2,
                                       (const uint16_t *)image->yuvPlanes[AVIF_CHAN_U],
                                       image->yuvRowBytes[AVIF_CHAN_U] / 2,
                                       rgb->pixels,
                                       rgb->rowBytes,
                                       matrixYVU,
                                       image->width,
                                       image->height,
                                       filter) != 0) {
                return AVIF_RESULT_REFORMAT_FAILED;
            }
#else
            if (I210ToARGBMatrix((const uint16_t *)image->yuvPlanes[AVIF_CHAN_Y],
                                 image->yuvRowBytes[AVIF_CHAN_Y] / 2,
                                 (const uint16_t *)image->yuvPlanes[AVIF_CHAN_V],
                                 image->yuvRowBytes[AVIF_CHAN_V] / 2,
                                 (const uint16_t *)image->yuvPlanes[AVIF_CHAN_U],
                                 image->yuvRowBytes[AVIF_CHAN_U] / 2,
                                 rgb->pixels,
                                 rgb->rowBytes,
                                 matrixYVU,
                                 image->width,
                                 image->height) != 0) {
                return AVIF_RESULT_REFORMAT_FAILED;
            }
#endif
            return AVIF_RESULT_OK;
        } else if (image->yuvFormat == AVIF_PIXEL_FORMAT_YUV420) {
#if LIBYUV_VERSION >= 1813
            if (I010ToARGBMatrixFilter((const uint16_t *)image->yuvPlanes[AVIF_CHAN_Y],
                                       image->yuvRowBytes[AVIF_CHAN_Y] / 2,
                                       (const uint16_t *)image->yuvPlanes[AVIF_CHAN_V],
                                       image->yuvRowBytes[AVIF_CHAN_V] / 2,
                                       (const uint16_t *)image->yuvPlanes[AVIF_CHAN_U],
                                       image->yuvRowBytes[AVIF_CHAN_U] / 2,
                                       rgb->pixels,
                                       rgb->rowBytes,
                                       matrixYVU,
                                       image->width,
                                       image->height,
                                       filter) != 0) {
                return AVIF_RESULT_REFORMAT_FAILED;
            }
#else
            if (I010ToARGBMatrix((const uint16_t *)image->yuvPlanes[AVIF_CHAN_Y],
                                 image->yuvRowBytes[AVIF_CHAN_Y] / 2,
                                 (const uint16_t *)image->yuvPlanes[AVIF_CHAN_V],
                                 image->yuvRowBytes[AVIF_CHAN_V] / 2,
                                 (const uint16_t *)image->yuvPlanes[AVIF_CHAN_U],
                                 image->yuvRowBytes[AVIF_CHAN_U] / 2,
                                 rgb->pixels,
                                 rgb->rowBytes,
                                 matrixYVU,
                                 image->width,
                                 image->height) != 0) {
                return AVIF_RESULT_REFORMAT_FAILED;
            }
#endif
            return AVIF_RESULT_OK;
        } else if (image->yuvFormat == AVIF_PIXEL_FORMAT_YUV400) {
            // This doesn't currently exist in libyuv
        }
    }

    // This function didn't do anything; use the built-in YUV conversion
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
