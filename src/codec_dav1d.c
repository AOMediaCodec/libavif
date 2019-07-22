// Copyright 2019 Joe Drago. All rights reserved.
// SPDX-License-Identifier: BSD-2-Clause

#include "avif/internal.h"

#include "dav1d/dav1d.h"

#include <string.h>

struct avifCodecInternal
{
    Dav1dSettings dav1dSettings;
    Dav1dContext * dav1dContext;
    Dav1dPicture dav1dPicture;
    avifBool hasPicture;
    avifRange colorRange;
    Dav1dData dav1dData;
    uint32_t inputSampleIndex;
};

static void dav1dCodecDestroyInternal(avifCodec * codec)
{
    if (codec->internal->dav1dContext) {
        dav1d_close(&codec->internal->dav1dContext);
    }
    avifFree(codec->internal);
}

// returns AVIF_FALSE if there's nothing left to feed, or feeding fatally fails (say that five times fast)
static avifBool dav1dFeedData(avifCodec * codec)
{
    if (!codec->internal->dav1dData.sz) {
        dav1d_data_unref(&codec->internal->dav1dData);

        if (codec->internal->inputSampleIndex < codec->decodeInput->samples.count) {
            avifRawData * sample = &codec->decodeInput->samples.raw[codec->internal->inputSampleIndex];
            ++codec->internal->inputSampleIndex;

            // OPTIMIZE: Carefully switch this to use dav1d_data_wrap or dav1d_data_wrap_user_data
            uint8_t * dav1dDataPtr = dav1d_data_create(&codec->internal->dav1dData, sample->size);
            memcpy(dav1dDataPtr, sample->data, sample->size);
        } else {
            // No more data
            return AVIF_FALSE;
        }
    }

    int res = dav1d_send_data(codec->internal->dav1dContext, &codec->internal->dav1dData);
    if ((res < 0) && (res != DAV1D_ERR(EAGAIN))) {
        return AVIF_FALSE;
    }
    return AVIF_TRUE;
}

static avifBool dav1dCodecDecode(avifCodec * codec)
{
    if (codec->internal->dav1dContext == NULL) {
        if (dav1d_open(&codec->internal->dav1dContext, &codec->internal->dav1dSettings) != 0) {
            return AVIF_FALSE;
        }
    }

    codec->internal->inputSampleIndex = 0;
    return dav1dFeedData(codec);
}

static avifBool dav1dCodecAlphaLimitedRange(avifCodec * codec)
{
    if (codec->decodeInput->alpha && codec->internal->hasPicture && (codec->internal->colorRange == AVIF_RANGE_LIMITED)) {
        return AVIF_TRUE;
    }
    return AVIF_FALSE;
}

static avifBool dav1dCodecGetNextImage(avifCodec * codec, avifImage * image)
{
    avifBool gotPicture = AVIF_FALSE;
    Dav1dPicture nextFrame = { 0 };

    for (;;) {
        avifBool sentData = dav1dFeedData(codec);
        int res = dav1d_get_picture(codec->internal->dav1dContext, &nextFrame);
        if ((res < 0) && (res != DAV1D_ERR(EAGAIN)) && !sentData) {
            // No more frames
            return AVIF_FALSE;
        } else {
            // Got a picture!
            gotPicture = AVIF_TRUE;
            break;
        }
    }

    if (gotPicture) {
        dav1d_picture_unref(&codec->internal->dav1dPicture);
        codec->internal->dav1dPicture = nextFrame;
        codec->internal->colorRange = codec->internal->dav1dPicture.seq_hdr->color_range ? AVIF_RANGE_FULL : AVIF_RANGE_LIMITED;
        codec->internal->hasPicture = AVIF_TRUE;
    } else {
        if (codec->decodeInput->alpha && codec->internal->hasPicture) {
            // Special case: reuse last alpha frame
        } else {
            return AVIF_FALSE;
        }
    }

    Dav1dPicture * dav1dImage = &codec->internal->dav1dPicture;
    avifBool isColor = !codec->decodeInput->alpha;
    if (isColor) {
        // Color (YUV) planes - set image to correct size / format, fill color

        avifPixelFormat yuvFormat = AVIF_PIXEL_FORMAT_NONE;
        switch (dav1dImage->p.layout) {
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

        if (image->width && image->height) {
            if ((image->width != dav1dImage->p.w) || (image->height != dav1dImage->p.h) || (image->depth != dav1dImage->p.bpc) ||
                (image->yuvFormat != yuvFormat)) {
                // Throw it all out
                avifImageFreePlanes(image, AVIF_PLANES_ALL);
            }
        }

        image->width = dav1dImage->p.w;
        image->height = dav1dImage->p.h;
        image->depth = dav1dImage->p.bpc;
        image->yuvFormat = yuvFormat;
        image->yuvRange = codec->internal->colorRange;

        avifPixelFormatInfo formatInfo;
        avifGetPixelFormatInfo(yuvFormat, &formatInfo);

        int uvHeight = image->height >> formatInfo.chromaShiftY;

        avifImageFreePlanes(image, AVIF_PLANES_YUV);
        for (int yuvPlane = 0; yuvPlane < 3; ++yuvPlane) {
            image->yuvPlanes[yuvPlane] = dav1dImage->data[yuvPlane];
            image->yuvRowBytes[yuvPlane] = dav1dImage->stride[(yuvPlane == AVIF_CHAN_Y) ? 0 : 1];
        }
        image->decoderOwnsYUVPlanes = AVIF_TRUE;
    } else {
        // Alpha plane - ensure image is correct size, fill color

        avifImageFreePlanes(image, AVIF_PLANES_A);
        image->alphaPlane = dav1dImage->data[0];
        image->alphaRowBytes = dav1dImage->stride[0];
        image->decoderOwnsAlphaPlane = AVIF_TRUE;
    }
    return AVIF_TRUE;
}

avifCodec * avifCodecCreateDav1d()
{
    avifCodec * codec = (avifCodec *)avifAlloc(sizeof(avifCodec));
    memset(codec, 0, sizeof(struct avifCodec));
    codec->decode = dav1dCodecDecode;
    codec->alphaLimitedRange = dav1dCodecAlphaLimitedRange;
    codec->getNextImage = dav1dCodecGetNextImage;
    codec->destroyInternal = dav1dCodecDestroyInternal;

    codec->internal = (struct avifCodecInternal *)avifAlloc(sizeof(struct avifCodecInternal));
    memset(codec->internal, 0, sizeof(struct avifCodecInternal));
    dav1d_default_settings(&codec->internal->dav1dSettings);
    return codec;
}
