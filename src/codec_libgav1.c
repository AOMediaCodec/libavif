// Copyright 2020 Google LLC. All rights reserved.
// SPDX-License-Identifier: BSD-2-Clause

#include "avif/internal.h"

#include "gav1/decoder.h"

#include <string.h>

struct avifCodecInternal
{
    Libgav1DecoderSettings gav1Settings;
    Libgav1Decoder * gav1Decoder;
    const Libgav1DecoderBuffer * gav1Image;
    avifRange colorRange;
    uint32_t inputSampleIndex;
};

static void gav1CodecDestroyInternal(avifCodec * codec)
{
    if (codec->internal->gav1Decoder != NULL) {
        Libgav1DecoderDestroy(codec->internal->gav1Decoder);
    }
    avifFree(codec->internal);
}

static avifBool gav1CodecOpen(avifCodec * codec, uint32_t firstSampleIndex)
{
    if (codec->internal->gav1Decoder == NULL) {
        if (Libgav1DecoderCreate(&codec->internal->gav1Settings, &codec->internal->gav1Decoder) != kLibgav1StatusOk) {
            return AVIF_FALSE;
        }
    }

    codec->internal->inputSampleIndex = firstSampleIndex;
    return AVIF_TRUE;
}

static avifBool gav1CodecGetNextImage(avifCodec * codec, avifImage * image)
{
    const Libgav1DecoderBuffer * nextFrame = NULL;
    // Check if there are more samples to feed
    if (codec->internal->inputSampleIndex < codec->decodeInput->samples.count) {
        // Feed another sample
        avifSample * sample = &codec->decodeInput->samples.sample[codec->internal->inputSampleIndex];
        ++codec->internal->inputSampleIndex;
        if (Libgav1DecoderEnqueueFrame(codec->internal->gav1Decoder,
                                       sample->data.data,
                                       sample->data.size,
                                       /*user_private_data=*/0,
                                       /*buffer_private_data=*/NULL) != kLibgav1StatusOk) {
            return AVIF_FALSE;
        }
        // Each Libgav1DecoderDequeueFrame() call invalidates the output frame
        // returned by the previous Libgav1DecoderDequeueFrame() call. Clear
        // our pointer to the previous output frame.
        codec->internal->gav1Image = NULL;
        if (Libgav1DecoderDequeueFrame(codec->internal->gav1Decoder, &nextFrame) != kLibgav1StatusOk) {
            return AVIF_FALSE;
        }
        // Got an image!
    }

    if (nextFrame) {
        codec->internal->gav1Image = nextFrame;
        codec->internal->colorRange = (nextFrame->color_range == kLibgav1ColorRangeStudio) ? AVIF_RANGE_LIMITED : AVIF_RANGE_FULL;
    } else {
        if (codec->decodeInput->alpha && codec->internal->gav1Image) {
            // Special case: reuse last alpha frame
        } else {
            return AVIF_FALSE;
        }
    }

    const Libgav1DecoderBuffer * gav1Image = codec->internal->gav1Image;
    avifBool isColor = !codec->decodeInput->alpha;
    if (isColor) {
        // Color (YUV) planes - set image to correct size / format, fill color

        avifPixelFormat yuvFormat = AVIF_PIXEL_FORMAT_NONE;
        switch (gav1Image->image_format) {
            case kLibgav1ImageFormatMonochrome400:
            case kLibgav1ImageFormatYuv420:
                yuvFormat = AVIF_PIXEL_FORMAT_YUV420;
                break;
            case kLibgav1ImageFormatYuv422:
                yuvFormat = AVIF_PIXEL_FORMAT_YUV422;
                break;
            case kLibgav1ImageFormatYuv444:
                yuvFormat = AVIF_PIXEL_FORMAT_YUV444;
                break;
        }

        if (image->width && image->height) {
            if ((image->width != (uint32_t)gav1Image->displayed_width[0]) ||
                (image->height != (uint32_t)gav1Image->displayed_height[0]) || (image->depth != (uint32_t)gav1Image->bitdepth) ||
                (image->yuvFormat != yuvFormat)) {
                // Throw it all out
                avifImageFreePlanes(image, AVIF_PLANES_ALL);
            }
        }
        image->width = gav1Image->displayed_width[0];
        image->height = gav1Image->displayed_height[0];
        image->depth = gav1Image->bitdepth;

        image->yuvFormat = yuvFormat;
        image->yuvRange = codec->internal->colorRange;

        if (image->profileFormat == AVIF_PROFILE_FORMAT_NONE) {
            // If the AVIF container doesn't provide a color profile, allow the AV1 OBU to provide one as a fallback
            avifNclxColorProfile nclx;
            nclx.colourPrimaries = (avifNclxColourPrimaries)gav1Image->color_primary;
            nclx.transferCharacteristics = (avifNclxTransferCharacteristics)gav1Image->transfer_characteristics;
            nclx.matrixCoefficients = (avifNclxMatrixCoefficients)gav1Image->matrix_coefficients;
            nclx.range = image->yuvRange;
            avifImageSetProfileNCLX(image, &nclx);
        }

        avifPixelFormatInfo formatInfo;
        avifGetPixelFormatInfo(yuvFormat, &formatInfo);

        // Steal the pointers from the decoder's image directly
        avifImageFreePlanes(image, AVIF_PLANES_YUV);
        for (int yuvPlane = 0; yuvPlane < 3; ++yuvPlane) {
            image->yuvPlanes[yuvPlane] = gav1Image->plane[yuvPlane];
            image->yuvRowBytes[yuvPlane] = gav1Image->stride[yuvPlane];
        }
        image->decoderOwnsYUVPlanes = AVIF_TRUE;
    } else {
        // Alpha plane - ensure image is correct size, fill color

        if (image->width && image->height) {
            if ((image->width != (uint32_t)gav1Image->displayed_width[0]) ||
                (image->height != (uint32_t)gav1Image->displayed_height[0]) || (image->depth != (uint32_t)gav1Image->bitdepth)) {
                // Alpha plane doesn't match previous alpha plane decode, bail out
                return AVIF_FALSE;
            }
        }
        image->width = gav1Image->displayed_width[0];
        image->height = gav1Image->displayed_height[0];
        image->depth = gav1Image->bitdepth;

        avifImageFreePlanes(image, AVIF_PLANES_A);
        image->alphaPlane = gav1Image->plane[0];
        image->alphaRowBytes = gav1Image->stride[0];
        image->alphaRange = codec->internal->colorRange;
        image->decoderOwnsAlphaPlane = AVIF_TRUE;
    }

    return AVIF_TRUE;
}

const char * avifCodecVersionGav1(void)
{
    return Libgav1GetVersionString();
}

avifCodec * avifCodecCreateGav1(void)
{
    avifCodec * codec = (avifCodec *)avifAlloc(sizeof(avifCodec));
    memset(codec, 0, sizeof(struct avifCodec));
    codec->open = gav1CodecOpen;
    codec->getNextImage = gav1CodecGetNextImage;
    codec->destroyInternal = gav1CodecDestroyInternal;

    codec->internal = (struct avifCodecInternal *)avifAlloc(sizeof(struct avifCodecInternal));
    memset(codec->internal, 0, sizeof(struct avifCodecInternal));
    Libgav1DecoderSettingsInitDefault(&codec->internal->gav1Settings);
    // The number of threads (default to 1) should depend on the number of
    // processor cores. For now use a hardcoded value of 2.
    codec->internal->gav1Settings.threads = 2;
    return codec;
}
