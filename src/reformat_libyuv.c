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
avifResult avifImageIdentity8ToRGB8ColorFullRangeLibYUV(const avifImage * image, avifRGBImage * rgb, avifReformatState * state)
{
    (void)image;
    (void)rgb;
    (void)state;
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

#if LIBYUV_VERSION < 1775
#error libyuv version too old. Need at least 1775.
#endif

avifResult avifImageYUVToRGBLibYUV(const avifImage * image, avifRGBImage * rgb)
{
    // See if the current settings can be accomplished with libyuv, and use it (if possible).

    if (rgb->depth != 8) {
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
            case AVIF_MATRIX_COEFFICIENTS_BT709:
                matrixYUV = &kYuvF709Constants;
                matrixYVU = &kYvuF709Constants;
                break;
            case AVIF_MATRIX_COEFFICIENTS_BT2020_NCL:
                matrixYUV = &kYuvV2020Constants;
                matrixYVU = &kYvuV2020Constants;
                break;
            case AVIF_MATRIX_COEFFICIENTS_CHROMA_DERIVED_NCL:
                switch (image->colorPrimaries) {
                    case AVIF_COLOR_PRIMARIES_BT709:
                    case AVIF_COLOR_PRIMARIES_UNSPECIFIED:
                        matrixYUV = &kYuvF709Constants;
                        matrixYVU = &kYvuF709Constants;
                        break;

                    case AVIF_COLOR_PRIMARIES_BT470BG:
                    case AVIF_COLOR_PRIMARIES_BT601:
                        matrixYUV = &kYuvJPEGConstants;
                        matrixYVU = &kYvuJPEGConstants;
                        break;

                    case AVIF_COLOR_PRIMARIES_BT2020:
                        matrixYUV = &kYuvV2020Constants;
                        matrixYVU = &kYvuV2020Constants;
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

    enum
    {
        YUV,
        YUVA,
        Y,
        YUV16
    } convertFunctionType = YUV;

    union
    {
        int (*YUVToRGBA)(const uint8_t * src_y,
                         int src_stride_y,
                         const uint8_t * src_u,
                         int src_stride_u,
                         const uint8_t * src_v,
                         int src_stride_v,
                         uint8_t * dst_argb,
                         int dst_stride_argb,
                         const struct YuvConstants * yuvconstants,
                         int width,
                         int height);
        int (*YUVAToRGBA)(const uint8_t * src_y,
                          int src_stride_y,
                          const uint8_t * src_u,
                          int src_stride_u,
                          const uint8_t * src_v,
                          int src_stride_v,
                          const uint8_t * src_a,
                          int src_stride_a,
                          uint8_t * dst_argb,
                          int dst_stride_argb,
                          const struct YuvConstants * yuvconstants,
                          int width,
                          int height,
                          int attenuate);
        int (*YToRGBA)(const uint8_t * src_y,
                       int src_stride_y,
                       uint8_t * dst_argb,
                       int dst_stride_argb,
                       const struct YuvConstants * yuvconstants,
                       int width,
                       int height);
        int (*YUV16ToRGBA)(const uint16_t * src_y,
                           int src_stride_y,
                           const uint16_t * src_u,
                           int src_stride_u,
                           const uint16_t * src_v,
                           int src_stride_v,
                           uint8_t * dst_argb,
                           int dst_stride_argb,
                           const struct YuvConstants * yuvconstants,
                           int width,
                           int height);
    } convertFunction = { NULL };

    // If we should use the matrixYVU and flip U and V pointer
    avifBool flipUV;

    // separate post step to process alpha
    enum
    {
        None = 0,
        Multiply = 1,
        Unmultiply = 2,
        CopyAlpha = 4,
    };
    int alphaPostStep = None;

    // do premultiply during conversion
    avifBool attenuate = AVIF_FALSE;

    if (rgb->format == AVIF_RGB_FORMAT_BGRA || rgb->format == AVIF_RGB_FORMAT_RGBA) {
        flipUV = rgb->format == AVIF_RGB_FORMAT_RGBA;

        if (image->depth == 8) {
            // If source don't have alpha, or we don't need alpha (alpha is ignored, and no premultiply process is need)
            if (image->alphaPlane == NULL || (rgb->ignoreAlpha && image->alphaPremultiplied)) {
                switch (image->yuvFormat) {
                    case AVIF_PIXEL_FORMAT_YUV444:
                        convertFunctionType = YUV;
                        convertFunction.YUVToRGBA = I444ToARGBMatrix;
                        break;
                    case AVIF_PIXEL_FORMAT_YUV422:
                        convertFunctionType = YUV;
                        convertFunction.YUVToRGBA = I422ToARGBMatrix;
                        break;
                    case AVIF_PIXEL_FORMAT_YUV420:
                        convertFunctionType = YUV;
                        convertFunction.YUVToRGBA = I420ToARGBMatrix;
                        break;
                    case AVIF_PIXEL_FORMAT_YUV400:
                        convertFunctionType = Y;
                        convertFunction.YToRGBA = I400ToARGBMatrix;
                        break;
                    case AVIF_PIXEL_FORMAT_NONE:
                        // not possible. only for compiler warning
                        return AVIF_RESULT_REFORMAT_FAILED;
                }
            } else {
                switch (image->yuvFormat) {
                    case AVIF_PIXEL_FORMAT_YUV444:
                        convertFunctionType = YUVA;
                        convertFunction.YUVAToRGBA = I444AlphaToARGBMatrix;
                        break;
                    case AVIF_PIXEL_FORMAT_YUV422:
                        convertFunctionType = YUVA;
                        convertFunction.YUVAToRGBA = I422AlphaToARGBMatrix;
                        break;
                    case AVIF_PIXEL_FORMAT_YUV420:
                        convertFunctionType = YUVA;
                        convertFunction.YUVAToRGBA = I420AlphaToARGBMatrix;
                        break;
                    case AVIF_PIXEL_FORMAT_YUV400:
                        convertFunctionType = Y;
                        alphaPostStep |= CopyAlpha;
                        convertFunction.YToRGBA = I400ToARGBMatrix;
                        break;
                    case AVIF_PIXEL_FORMAT_NONE:
                        // not possible. only for compiler warning
                        return AVIF_RESULT_REFORMAT_FAILED;
                }
            }
        } else if (image->depth == 10) {
            convertFunctionType = YUV16;
            switch (image->yuvFormat) {
                case AVIF_PIXEL_FORMAT_YUV444:
                    // This doesn't currently exist in libyuv
#if 0
                    convertFunctionType = YUV;
                    convertFunction.YUVToRGBA = I410ToRGBAMatrix;
#else
                    return AVIF_RESULT_NOT_IMPLEMENTED;
#endif

                case AVIF_PIXEL_FORMAT_YUV422:
                    convertFunction.YUV16ToRGBA = I210ToARGBMatrix;
                    break;
                case AVIF_PIXEL_FORMAT_YUV420:
                    convertFunction.YUV16ToRGBA = I010ToARGBMatrix;
                    break;
                case AVIF_PIXEL_FORMAT_YUV400:
                    // This doesn't currently exist in libyuv
#if 0
                    convertFunctionType = Y;
                    convertFunction.YToRGBA = I010ToRGBAMatrix;
#else
                    return AVIF_RESULT_NOT_IMPLEMENTED;
#endif
                case AVIF_PIXEL_FORMAT_NONE:
                    // not possible. only for compiler warning
                    return AVIF_RESULT_REFORMAT_FAILED;
            }

            if (!(image->alphaPlane == NULL || (rgb->ignoreAlpha && image->alphaPremultiplied))) {
                alphaPostStep |= CopyAlpha;
            }
        } else {
            return AVIF_RESULT_NOT_IMPLEMENTED;
        }

    } else if (rgb->format == AVIF_RGB_FORMAT_ABGR || rgb->format == AVIF_RGB_FORMAT_ARGB) {
        flipUV = rgb->format == AVIF_RGB_FORMAT_ARGB;

        switch (image->yuvFormat) {
            case AVIF_PIXEL_FORMAT_YUV444:
                // This doesn't currently exist in libyuv
#if 0
            convertFunctionType = YUV;
                convertFunction.YUVToRGBA = I444ToRGBAMatrix;
#else
                return AVIF_RESULT_NOT_IMPLEMENTED;
#endif

            case AVIF_PIXEL_FORMAT_YUV422:
                convertFunctionType = YUV;
                convertFunction.YUVToRGBA = I422ToRGBAMatrix;
                break;
            case AVIF_PIXEL_FORMAT_YUV420:
                convertFunctionType = YUV;
                convertFunction.YUVToRGBA = I420ToRGBAMatrix;
                break;
            case AVIF_PIXEL_FORMAT_YUV400:
#if 0
                convertFunctionType = Y;
                convertFunction.YToRGBA = I400ToRGBAMatrix;
#else
                return AVIF_RESULT_NOT_IMPLEMENTED;
#endif
            case AVIF_PIXEL_FORMAT_NONE:
                // not possible. only for compiler warning
                return AVIF_RESULT_REFORMAT_FAILED;
        }

        if (!(image->alphaPlane == NULL || (rgb->ignoreAlpha && image->alphaPremultiplied))) {
            alphaPostStep |= CopyAlpha;
        }
    } else {
        return AVIF_RESULT_NOT_IMPLEMENTED;
    }

    if (!image->alphaPremultiplied && rgb->alphaPremultiplied) {
        if (convertFunctionType == YUVA) {
            attenuate = AVIF_TRUE;
        } else {
            alphaPostStep |= Multiply;
        }
    } else if (image->alphaPremultiplied && !rgb->alphaPremultiplied) {
        alphaPostStep |= Unmultiply;
    }

    switch (convertFunctionType) {
        case YUV:
            if (flipUV) {
                if (convertFunction.YUVToRGBA(image->yuvPlanes[AVIF_CHAN_Y],
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
            } else {
                if (convertFunction.YUVToRGBA(image->yuvPlanes[AVIF_CHAN_Y],
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
            }
            break;

        case YUVA:
            if (flipUV) {
                if (convertFunction.YUVAToRGBA(image->yuvPlanes[AVIF_CHAN_Y],
                                               image->yuvRowBytes[AVIF_CHAN_Y],
                                               image->yuvPlanes[AVIF_CHAN_V],
                                               image->yuvRowBytes[AVIF_CHAN_V],
                                               image->yuvPlanes[AVIF_CHAN_U],
                                               image->yuvRowBytes[AVIF_CHAN_U],
                                               image->alphaPlane,
                                               image->alphaRowBytes,
                                               rgb->pixels,
                                               rgb->rowBytes,
                                               matrixYVU,
                                               image->width,
                                               image->height,
                                               attenuate) != 0) {
                    return AVIF_RESULT_REFORMAT_FAILED;
                }
            } else {
                if (convertFunction.YUVAToRGBA(image->yuvPlanes[AVIF_CHAN_Y],
                                               image->yuvRowBytes[AVIF_CHAN_Y],
                                               image->yuvPlanes[AVIF_CHAN_U],
                                               image->yuvRowBytes[AVIF_CHAN_U],
                                               image->yuvPlanes[AVIF_CHAN_V],
                                               image->yuvRowBytes[AVIF_CHAN_V],
                                               image->alphaPlane,
                                               image->alphaRowBytes,
                                               rgb->pixels,
                                               rgb->rowBytes,
                                               matrixYUV,
                                               image->width,
                                               image->height,
                                               attenuate) != 0) {
                    return AVIF_RESULT_REFORMAT_FAILED;
                }
            }
            break;

        case Y:
            if (convertFunction.YToRGBA(image->yuvPlanes[AVIF_CHAN_Y],
                                        image->yuvRowBytes[AVIF_CHAN_Y],
                                        rgb->pixels,
                                        rgb->rowBytes,
                                        matrixYUV,
                                        image->width,
                                        image->height) != 0) {
                return AVIF_RESULT_REFORMAT_FAILED;
            }
            break;
        case YUV16:
            // libyuv is expecting stride as ptrdiff_t
            if (flipUV) {
                if (convertFunction.YUV16ToRGBA((uint16_t *)image->yuvPlanes[AVIF_CHAN_Y],
                                                image->yuvRowBytes[AVIF_CHAN_Y] / 2,
                                                (uint16_t *)image->yuvPlanes[AVIF_CHAN_V],
                                                image->yuvRowBytes[AVIF_CHAN_V] / 2,
                                                (uint16_t *)image->yuvPlanes[AVIF_CHAN_U],
                                                image->yuvRowBytes[AVIF_CHAN_U] / 2,
                                                rgb->pixels,
                                                rgb->rowBytes,
                                                matrixYVU,
                                                image->width,
                                                image->height) != 0) {
                    return AVIF_RESULT_REFORMAT_FAILED;
                }
            } else {
                if (convertFunction.YUV16ToRGBA((uint16_t *)image->yuvPlanes[AVIF_CHAN_Y],
                                                image->yuvRowBytes[AVIF_CHAN_Y] / 2,
                                                (uint16_t *)image->yuvPlanes[AVIF_CHAN_U],
                                                image->yuvRowBytes[AVIF_CHAN_U] / 2,
                                                (uint16_t *)image->yuvPlanes[AVIF_CHAN_V],
                                                image->yuvRowBytes[AVIF_CHAN_V] / 2,
                                                rgb->pixels,
                                                rgb->rowBytes,
                                                matrixYUV,
                                                image->width,
                                                image->height) != 0) {
                    return AVIF_RESULT_REFORMAT_FAILED;
                }
            }
            break;
    }

    if ((alphaPostStep & CopyAlpha) && image->depth == 8 && image->alphaRange == AVIF_RANGE_FULL &&
        (rgb->format == AVIF_RGB_FORMAT_BGRA || rgb->format == AVIF_RGB_FORMAT_RGBA)) {
        alphaPostStep &= ~CopyAlpha;
        ARGBCopyYToAlpha(image->alphaPlane, image->alphaRowBytes, rgb->pixels, rgb->rowBytes, image->width, image->height);
        if (alphaPostStep & Multiply) {
            return avifRGBImagePremultiplyAlpha(rgb);
        } else if (alphaPostStep & Unmultiply) {
            return avifRGBImageUnpremultiplyAlpha(rgb);
        }
    }

    if (alphaPostStep & CopyAlpha) {
        return AVIF_RESULT_NEED_POST_PROCESS;
    }

    return AVIF_RESULT_OK;
}

avifResult avifImageIdentity8ToRGB8ColorFullRangeLibYUV(const avifImage * image, avifRGBImage * rgb, avifReformatState * state)
{
    const uint8_t * planePtr[4];
    int planeRowBytes[4];

    // state->rgbOffsetBytesA is 0 is RGB format doesn't have alpha.
    // Place it first, so that it can be overwritten.
    planePtr[state->rgbOffsetBytesA] = image->alphaPlane;
    planeRowBytes[state->rgbOffsetBytesA] = image->alphaRowBytes;

    planePtr[state->rgbOffsetBytesR] = image->yuvPlanes[AVIF_CHAN_V];
    planeRowBytes[state->rgbOffsetBytesR] = image->yuvRowBytes[AVIF_CHAN_V];
    planePtr[state->rgbOffsetBytesG] = image->yuvPlanes[AVIF_CHAN_Y];
    planeRowBytes[state->rgbOffsetBytesG] = image->yuvRowBytes[AVIF_CHAN_Y];
    planePtr[state->rgbOffsetBytesB] = image->yuvPlanes[AVIF_CHAN_U];
    planeRowBytes[state->rgbOffsetBytesB] = image->yuvRowBytes[AVIF_CHAN_U];

    // libavif uses byte-order when describing 4 bytes pixel formats, such that the R in RGBA is the lowest address,
    // similar to PNG. libyuv orders in word-order, so libavif's OffsetBytes and libyuv's are reversed.

    if (avifRGBFormatHasAlpha(rgb->format)) {
        // Only src_a(planePtr[3]) is allowed to be NULL in MergeARGBPlane.
        if (planePtr[0] == NULL || planePtr[1] == NULL || planePtr[2] == NULL) {
            return AVIF_RESULT_NOT_IMPLEMENTED;
        }

        // BGRA in libavif.
        MergeARGBPlane(planePtr[2],
                       planeRowBytes[2],
                       planePtr[1],
                       planeRowBytes[1],
                       planePtr[0],
                       planeRowBytes[0],
                       planePtr[3],
                       planeRowBytes[3],
                       rgb->pixels,
                       rgb->rowBytes,
                       image->width,
                       image->height);
    } else {
        // RGB in libavif.
        MergeRGBPlane(planePtr[0],
                      planeRowBytes[0],
                      planePtr[1],
                      planeRowBytes[1],
                      planePtr[2],
                      planeRowBytes[2],
                      rgb->pixels,
                      rgb->rowBytes,
                      image->width,
                      image->height);
    }

    return AVIF_RESULT_OK;
}

avifResult avifRGBImagePremultiplyAlphaLibYUV(avifRGBImage * rgb)
{
    // See if the current settings can be accomplished with libyuv, and use it (if possible).

    if (rgb->depth != 8) {
        return AVIF_RESULT_NOT_IMPLEMENTED;
    }

    // libavif uses byte-order when describing pixel formats, such that the R in RGBA is the lowest address,
    // similar to PNG. libyuv orders in word-order, so libavif's RGBA would be referred to in libyuv as ABGR.

    // order of RGB doesn't matter here.
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

unsigned int avifLibYUVVersion(void)
{
    return (unsigned int)LIBYUV_VERSION;
}

#endif
