// Copyright 2022 Google LLC
// SPDX-License-Identifier: BSD-2-Clause

#include "avif/internal.h"

#if defined(AVIF_LIBSHARPYUV_ENABLED)
#include <sharpyuv/sharpyuv.h>
#include <sharpyuv/sharpyuv_csp.h>

avifResult avifImageRGBToYUVLibSharpYUV(avifImage * image, const avifRGBImage * rgb, const avifReformatState * state)
{
    const SharpYuvColorSpace color_space = {
        state->kr, state->kb, image->depth, (state->yuvRange == AVIF_RANGE_LIMITED) ? kSharpYuvRangeLimited : kSharpYuvRangeFull
    };

    SharpYuvConversionMatrix matrix;
    // Fills in 'matrix' for the given YUVColorSpace.
    SharpYuvComputeConversionMatrix(&color_space, &matrix);
#if SHARPYUV_VERSION >= SHARPYUV_MAKE_VERSION(0, 3, 0)
    SharpYuvOptions options;
    options.yuv_matrix = &matrix;
    if (image->transferCharacteristics == AVIF_TRANSFER_CHARACTERISTICS_UNSPECIFIED) {
        // Set to SRGB for backward compatibility.
        options.transfer_type = kSharpYuvTransferFunctionSrgb;
    } else {
        options.transfer_type = (SharpYuvTransferFunctionType)image->transferCharacteristics;
    }
    if (!SharpYuvConvertWithOptions(&rgb->pixels[state->rgbOffsetBytesR],
                                    &rgb->pixels[state->rgbOffsetBytesG],
                                    &rgb->pixels[state->rgbOffsetBytesB],
                                    state->rgbPixelBytes,
                                    rgb->rowBytes,
                                    rgb->depth,
                                    image->yuvPlanes[AVIF_CHAN_Y],
                                    image->yuvRowBytes[AVIF_CHAN_Y],
                                    image->yuvPlanes[AVIF_CHAN_U],
                                    image->yuvRowBytes[AVIF_CHAN_U],
                                    image->yuvPlanes[AVIF_CHAN_V],
                                    image->yuvRowBytes[AVIF_CHAN_V],
                                    image->depth,
                                    rgb->width,
                                    rgb->height,
                                    &options))
#else
    if (!SharpYuvConvert(&rgb->pixels[state->rgbOffsetBytesR],
                         &rgb->pixels[state->rgbOffsetBytesG],
                         &rgb->pixels[state->rgbOffsetBytesB],
                         state->rgbPixelBytes,
                         rgb->rowBytes,
                         rgb->depth,
                         image->yuvPlanes[AVIF_CHAN_Y],
                         image->yuvRowBytes[AVIF_CHAN_Y],
                         image->yuvPlanes[AVIF_CHAN_U],
                         image->yuvRowBytes[AVIF_CHAN_U],
                         image->yuvPlanes[AVIF_CHAN_V],
                         image->yuvRowBytes[AVIF_CHAN_V],
                         image->depth,
                         rgb->width,
                         rgb->height,
                         &matrix))
#endif
    {
        return AVIF_RESULT_REFORMAT_FAILED;
    }

    return AVIF_RESULT_OK;
}

#else

avifResult avifImageRGBToYUVLibSharpYUV(avifImage * image, const avifRGBImage * rgb, const avifReformatState * state)
{
    (void)image;
    (void)rgb;
    (void)state;
    return AVIF_RESULT_NOT_IMPLEMENTED;
}
#endif
