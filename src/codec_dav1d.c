// Copyright 2019 Joe Drago. All rights reserved.
// SPDX-License-Identifier: BSD-2-Clause

#include "avif/internal.h"

#if defined(_MSC_VER)
#pragma warning(disable : 4201) // nonstandard extension used: nameless struct/union
#endif
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wc11-extensions" // C11 extension used: nameless struct/union
#endif
#include "dav1d/dav1d.h"
#if defined(__clang__)
#pragma clang diagnostic pop
#endif

#include <string.h>

// For those building with an older version of dav1d (not recommended).
#ifndef DAV1D_ERR
#define DAV1D_ERR(e) (-(e))
#endif

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

static void avifDav1dFreeCallback(const uint8_t * buf, void * cookie)
{
    // This data is owned by the decoder; nothing to free here
    (void)buf;
    (void)cookie;
}

static void dav1dCodecDestroyInternal(avifCodec * codec)
{
    if (codec->internal->dav1dData.sz) {
        dav1d_data_unref(&codec->internal->dav1dData);
    }
    if (codec->internal->hasPicture) {
        dav1d_picture_unref(&codec->internal->dav1dPicture);
    }
    if (codec->internal->dav1dContext) {
        dav1d_close(&codec->internal->dav1dContext);
    }
    avifFree(codec->internal);
}

// returns AVIF_FALSE if there's nothing left to feed, or feeding fatally fails (say that five times fast)
static avifBool dav1dFeedData(avifCodec * codec)
{
    if (!codec->internal->dav1dData.sz) {
        if (codec->internal->inputSampleIndex < codec->decodeInput->samples.count) {
            avifDecodeSample * sample = &codec->decodeInput->samples.sample[codec->internal->inputSampleIndex];
            ++codec->internal->inputSampleIndex;

            if (dav1d_data_wrap(&codec->internal->dav1dData, sample->data.data, sample->data.size, avifDav1dFreeCallback, NULL) != 0) {
                return AVIF_FALSE;
            }
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

static avifBool dav1dCodecOpen(avifCodec * codec, uint32_t firstSampleIndex)
{
    if (codec->internal->dav1dContext == NULL) {
        if (dav1d_open(&codec->internal->dav1dContext, &codec->internal->dav1dSettings) != 0) {
            return AVIF_FALSE;
        }
    }

    codec->internal->inputSampleIndex = firstSampleIndex;
    return AVIF_TRUE;
}

static avifBool dav1dCodecGetNextImage(avifCodec * codec, avifImage * image)
{
    avifBool gotPicture = AVIF_FALSE;
    Dav1dPicture nextFrame;
    memset(&nextFrame, 0, sizeof(Dav1dPicture));

    for (;;) {
        avifBool sentData = dav1dFeedData(codec);
        int res = dav1d_get_picture(codec->internal->dav1dContext, &nextFrame);
        if ((res == DAV1D_ERR(EAGAIN)) && sentData) {
            // send more data
            continue;
        } else if (res < 0) {
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
                yuvFormat = AVIF_PIXEL_FORMAT_YUV400;
                break;
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
            if ((image->width != (uint32_t)dav1dImage->p.w) || (image->height != (uint32_t)dav1dImage->p.h) ||
                (image->depth != (uint32_t)dav1dImage->p.bpc) || (image->yuvFormat != yuvFormat)) {
                // Throw it all out
                avifImageFreePlanes(image, AVIF_PLANES_ALL);
            }
        }
        image->width = dav1dImage->p.w;
        image->height = dav1dImage->p.h;
        image->depth = dav1dImage->p.bpc;

        image->yuvFormat = yuvFormat;
        image->yuvRange = codec->internal->colorRange;

        image->colorPrimaries = (avifColorPrimaries)dav1dImage->seq_hdr->pri;
        image->transferCharacteristics = (avifTransferCharacteristics)dav1dImage->seq_hdr->trc;
        image->matrixCoefficients = (avifMatrixCoefficients)dav1dImage->seq_hdr->mtrx;

        avifPixelFormatInfo formatInfo;
        avifGetPixelFormatInfo(yuvFormat, &formatInfo);

        avifImageFreePlanes(image, AVIF_PLANES_YUV);
        int yuvPlaneCount = (yuvFormat == AVIF_PIXEL_FORMAT_YUV400) ? 1 : 3;
        for (int yuvPlane = 0; yuvPlane < yuvPlaneCount; ++yuvPlane) {
            image->yuvPlanes[yuvPlane] = dav1dImage->data[yuvPlane];
            image->yuvRowBytes[yuvPlane] = (uint32_t)dav1dImage->stride[(yuvPlane == AVIF_CHAN_Y) ? 0 : 1];
        }
        image->imageOwnsYUVPlanes = AVIF_FALSE;
    } else {
        // Alpha plane - ensure image is correct size, fill color

        if (image->width && image->height) {
            if ((image->width != (uint32_t)dav1dImage->p.w) || (image->height != (uint32_t)dav1dImage->p.h) ||
                (image->depth != (uint32_t)dav1dImage->p.bpc)) {
                // Alpha plane doesn't match previous alpha plane decode, bail out
                return AVIF_FALSE;
            }
        }
        image->width = dav1dImage->p.w;
        image->height = dav1dImage->p.h;
        image->depth = dav1dImage->p.bpc;

        avifImageFreePlanes(image, AVIF_PLANES_A);
        image->alphaPlane = dav1dImage->data[0];
        image->alphaRowBytes = (uint32_t)dav1dImage->stride[0];
        image->alphaRange = codec->internal->colorRange;
        image->imageOwnsAlphaPlane = AVIF_FALSE;
    }
    return AVIF_TRUE;
}

const char * avifCodecVersionDav1d(void)
{
    return dav1d_version();
}

avifCodec * avifCodecCreateDav1d(void)
{
    avifCodec * codec = (avifCodec *)avifAlloc(sizeof(avifCodec));
    memset(codec, 0, sizeof(struct avifCodec));
    codec->open = dav1dCodecOpen;
    codec->getNextImage = dav1dCodecGetNextImage;
    codec->destroyInternal = dav1dCodecDestroyInternal;

    codec->internal = (struct avifCodecInternal *)avifAlloc(sizeof(struct avifCodecInternal));
    memset(codec->internal, 0, sizeof(struct avifCodecInternal));
    dav1d_default_settings(&codec->internal->dav1dSettings);

    // Set a maximum frame size limit to avoid OOM'ing fuzzers.
    codec->internal->dav1dSettings.frame_size_limit = AVIF_MAX_IMAGE_SIZE;

    // Ensure that we only get the "highest spatial layer" as a single frame
    // for each input sample, instead of getting each spatial layer as its own
    // frame one at a time ("all layers").
    codec->internal->dav1dSettings.all_layers = 0;
    return codec;
}
