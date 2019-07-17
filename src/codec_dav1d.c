// Copyright 2019 Joe Drago. All rights reserved.
// SPDX-License-Identifier: BSD-2-Clause

#include "avif/internal.h"

#include "dav1d/dav1d.h"

#include <string.h>

struct avifCodecInternal
{
    Dav1dSettings dav1dSettings;
    Dav1dContext * dav1dContext[AVIF_CODEC_PLANES_COUNT];
    Dav1dPicture dav1dPicture[AVIF_CODEC_PLANES_COUNT];
    avifBool hasPicture[AVIF_CODEC_PLANES_COUNT];
    avifRange colorRange[AVIF_CODEC_PLANES_COUNT];
};

static void dav1dCodecDestroyInternal(avifCodec * codec)
{
    for (int i = 0; i < AVIF_CODEC_PLANES_COUNT; ++i) {
        if (codec->internal->dav1dContext[i]) {
            dav1d_close(&codec->internal->dav1dContext[i]);
        }
    }
    avifFree(codec->internal);
}

static avifBool dav1dCodecDecode(avifCodec * codec, avifCodecPlanes planes, avifRawData * obu)
{
    if (codec->internal->dav1dContext[planes] == NULL) {
        if (dav1d_open(&codec->internal->dav1dContext[planes], &codec->internal->dav1dSettings) != 0) {
            return AVIF_FALSE;
        }
    }

    avifBool result = AVIF_FALSE;

    // OPTIMIZE: Carefully switch this to use dav1d_data_wrap or dav1d_data_wrap_user_data
    Dav1dData dav1dData;
    uint8_t * dav1dDataPtr = dav1d_data_create(&dav1dData, obu->size);
    memcpy(dav1dDataPtr, obu->data, obu->size);

    if (dav1d_send_data(codec->internal->dav1dContext[planes], &dav1dData) != 0) {
        // This could return DAV1D_ERR(EAGAIN) and not be a failure if we weren't sending the entire payload
        goto cleanup;
    }

    if (dav1d_get_picture(codec->internal->dav1dContext[planes], &codec->internal->dav1dPicture[planes]) != 0) {
        goto cleanup;
    }

    codec->internal->hasPicture[planes] = AVIF_TRUE;
    codec->internal->colorRange[planes] = codec->internal->dav1dPicture[planes].seq_hdr->color_range ? AVIF_RANGE_FULL : AVIF_RANGE_LIMITED;
    result = AVIF_TRUE;
cleanup:
    dav1d_data_unref(&dav1dData);
    return result;
}

static avifCodecImageSize dav1dCodecGetImageSize(avifCodec * codec, avifCodecPlanes planes)
{
    avifCodecImageSize size;
    size.width = codec->internal->dav1dPicture[planes].p.w;
    size.height = codec->internal->dav1dPicture[planes].p.h;
    return size;
}

static avifBool dav1dCodecAlphaLimitedRange(avifCodec * codec)
{
    if (codec->internal->hasPicture[AVIF_CODEC_PLANES_ALPHA] &&
        (codec->internal->colorRange[AVIF_CODEC_PLANES_ALPHA] == AVIF_RANGE_LIMITED)) {
        return AVIF_TRUE;
    }
    return AVIF_FALSE;
}

static avifResult dav1dCodecGetDecodedImage(avifCodec * codec, avifImage * image)
{
    Dav1dPicture * colorImage = &codec->internal->dav1dPicture[AVIF_CODEC_PLANES_COLOR];
    Dav1dPicture * alphaImage = &codec->internal->dav1dPicture[AVIF_CODEC_PLANES_ALPHA];
    avifBool hasAlpha = codec->internal->hasPicture[AVIF_CODEC_PLANES_ALPHA];

    avifPixelFormat yuvFormat = AVIF_PIXEL_FORMAT_NONE;
    switch (colorImage->p.layout) {
        case DAV1D_PIXEL_LAYOUT_I400:
        case DAV1D_PIXEL_LAYOUT_I420:
            yuvFormat = AVIF_PIXEL_FORMAT_YUV420;
            break;
        case DAV1D_PIXEL_LAYOUT_I422:
            yuvFormat = AVIF_PIXEL_FORMAT_YUV422;
            break;
        case DAV1D_PIXEL_LAYOUT_I444:
            yuvFormat = AVIF_PIXEL_FORMAT_YUV444;
            break;
    }

    image->width = colorImage->p.w;
    image->height = colorImage->p.h;
    image->depth = colorImage->p.bpc;
    image->yuvFormat = yuvFormat;
    image->yuvRange = codec->internal->colorRange[AVIF_CODEC_PLANES_COLOR];

    avifPixelFormatInfo formatInfo;
    avifGetPixelFormatInfo(yuvFormat, &formatInfo);

    int uvHeight = image->height >> formatInfo.chromaShiftY;
    avifImageAllocatePlanes(image, AVIF_PLANES_YUV);

    for (int yuvPlane = 0; yuvPlane < 3; ++yuvPlane) {
        int planeHeight = image->height;
        if (yuvPlane != AVIF_CHAN_Y) {
            planeHeight = uvHeight;
        }

        uint8_t * srcPixels = (uint8_t *)colorImage->data[yuvPlane];
        ptrdiff_t stride = colorImage->stride[(yuvPlane == AVIF_CHAN_Y) ? 0 : 1];
        for (int j = 0; j < planeHeight; ++j) {
            uint8_t * srcRow = &srcPixels[j * stride];
            uint8_t * dstRow = &image->yuvPlanes[yuvPlane][j * image->yuvRowBytes[yuvPlane]];
            memcpy(dstRow, srcRow, image->yuvRowBytes[yuvPlane]);
        }

        if (colorImage->p.layout == DAV1D_PIXEL_LAYOUT_I400) {
            // Don't memcpy the chroma, its not going to be there
            break;
        }
    }

    if (hasAlpha) {
        avifImageAllocatePlanes(image, AVIF_PLANES_A);
        uint8_t * srcAlphaPixels = (uint8_t *)&alphaImage->data[0];
        for (int j = 0; j < image->height; ++j) {
            uint8_t * srcAlphaRow = &srcAlphaPixels[j * alphaImage->stride[0]];
            uint8_t * dstAlphaRow = &image->alphaPlane[j * image->alphaRowBytes];
            memcpy(dstAlphaRow, srcAlphaRow, image->alphaRowBytes);
        }
    }
    return AVIF_RESULT_OK;
}

avifCodec * avifCodecCreateDav1d()
{
    avifCodec * codec = (avifCodec *)avifAlloc(sizeof(avifCodec));
    memset(codec, 0, sizeof(struct avifCodec));
    codec->decode = dav1dCodecDecode;
    codec->getImageSize = dav1dCodecGetImageSize;
    codec->alphaLimitedRange = dav1dCodecAlphaLimitedRange;
    codec->getDecodedImage = dav1dCodecGetDecodedImage;
    codec->destroyInternal = dav1dCodecDestroyInternal;

    codec->internal = (struct avifCodecInternal *)avifAlloc(sizeof(struct avifCodecInternal));
    memset(codec->internal, 0, sizeof(struct avifCodecInternal));
    dav1d_default_settings(&codec->internal->dav1dSettings);
    return codec;
}
