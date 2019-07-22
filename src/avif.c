// Copyright 2019 Joe Drago. All rights reserved.
// SPDX-License-Identifier: BSD-2-Clause

#include "avif/internal.h"

#include <string.h>

#define STR_HELPER(x) #x
#define STR(x) STR_HELPER(x)
#define AVIF_VERSION_STRING (STR(AVIF_VERSION_MAJOR) "." STR(AVIF_VERSION_MINOR) "." STR(AVIF_VERSION_PATCH))

const char * avifVersion(void)
{
    return AVIF_VERSION_STRING;
}

const char * avifPixelFormatToString(avifPixelFormat format)
{
    switch (format) {
        case AVIF_PIXEL_FORMAT_YUV444:
            return "YUV444";
        case AVIF_PIXEL_FORMAT_YUV420:
            return "YUV420";
        case AVIF_PIXEL_FORMAT_YUV422:
            return "YUV422";
        case AVIF_PIXEL_FORMAT_YV12:
            return "YV12";
        case AVIF_PIXEL_FORMAT_NONE:
        default:
            break;
    }
    return "Unknown";
}

void avifGetPixelFormatInfo(avifPixelFormat format, avifPixelFormatInfo * info)
{
    memset(info, 0, sizeof(avifPixelFormatInfo));
    info->aomIndexU = 1;
    info->aomIndexV = 2;

    switch (format) {
        case AVIF_PIXEL_FORMAT_YUV444:
            info->chromaShiftX = 0;
            info->chromaShiftY = 0;
            break;

        case AVIF_PIXEL_FORMAT_YUV422:
            info->chromaShiftX = 1;
            info->chromaShiftY = 0;
            break;

        case AVIF_PIXEL_FORMAT_YUV420:
            info->chromaShiftX = 1;
            info->chromaShiftY = 1;
            break;

        case AVIF_PIXEL_FORMAT_YV12:
            info->chromaShiftX = 1;
            info->chromaShiftY = 1;
            info->aomIndexU = 2;
            info->aomIndexV = 1;
            break;

        case AVIF_PIXEL_FORMAT_NONE:
        default:
            break;
    }
}

const char * avifResultToString(avifResult result)
{
    // clang-format off
    switch (result) {
        case AVIF_RESULT_OK:                        return "OK";
        case AVIF_RESULT_INVALID_FTYP:              return "Invalid ftyp";
        case AVIF_RESULT_NO_CONTENT:                return "No content";
        case AVIF_RESULT_NO_YUV_FORMAT_SELECTED:    return "No YUV format selected";
        case AVIF_RESULT_REFORMAT_FAILED:           return "Reformat failed";
        case AVIF_RESULT_UNSUPPORTED_DEPTH:         return "Unsupported depth";
        case AVIF_RESULT_ENCODE_COLOR_FAILED:       return "Encoding of color planes failed";
        case AVIF_RESULT_ENCODE_ALPHA_FAILED:       return "Encoding of alpha plane failed";
        case AVIF_RESULT_BMFF_PARSE_FAILED:         return "BMFF parsing failed";
        case AVIF_RESULT_NO_AV1_ITEMS_FOUND:        return "No AV1 items found";
        case AVIF_RESULT_DECODE_COLOR_FAILED:       return "Decoding of color planes failed";
        case AVIF_RESULT_DECODE_ALPHA_FAILED:       return "Decoding of alpha plane failed";
        case AVIF_RESULT_COLOR_ALPHA_SIZE_MISMATCH: return "Color and alpha planes size mismatch";
        case AVIF_RESULT_ISPE_SIZE_MISMATCH:        return "Plane sizes don't match ispe values";
        case AVIF_RESULT_NO_CODEC_AVAILABLE:        return "No codec available";
        case AVIF_RESULT_NO_IMAGES_REMAINING:       return "No images remaining";
        case AVIF_RESULT_UNKNOWN_ERROR:
        default:
            break;
    }
    // clang-format on
    return "Unknown Error";
}

// This function assumes nothing in this struct needs to be freed; use avifImageClear() externally
static void avifImageSetDefaults(avifImage * image)
{
    memset(image, 0, sizeof(avifImage));
    image->yuvRange = AVIF_RANGE_FULL;
}

avifImage * avifImageCreate(int width, int height, int depth, avifPixelFormat yuvFormat)
{
    avifImage * image = (avifImage *)avifAlloc(sizeof(avifImage));
    avifImageSetDefaults(image);
    image->width = width;
    image->height = height;
    image->depth = depth;
    image->yuvFormat = yuvFormat;
    return image;
}

avifImage * avifImageCreateEmpty(void)
{
    return avifImageCreate(0, 0, 0, AVIF_PIXEL_FORMAT_NONE);
}

void avifImageCopy(avifImage * dstImage, avifImage * srcImage)
{
    avifImageFreePlanes(dstImage, AVIF_PLANES_ALL);

    dstImage->width = srcImage->width;
    dstImage->height = srcImage->height;
    dstImage->depth = srcImage->depth;
    dstImage->yuvFormat = srcImage->yuvFormat;
    dstImage->yuvRange = srcImage->yuvRange;

    if (srcImage->profileFormat == AVIF_PROFILE_FORMAT_ICC) {
        avifImageSetProfileICC(dstImage, srcImage->icc.data, srcImage->icc.size);
    } else if (srcImage->profileFormat == AVIF_PROFILE_FORMAT_NCLX) {
        avifImageSetProfileNCLX(dstImage, &srcImage->nclx);
    } else {
        avifImageSetProfileNone(dstImage);
    }

    if (srcImage->rgbPlanes[AVIF_CHAN_R] && srcImage->rgbPlanes[AVIF_CHAN_G] && srcImage->rgbPlanes[AVIF_CHAN_B]) {
        avifImageAllocatePlanes(dstImage, AVIF_PLANES_RGB);

        for (int plane = 0; plane < 3; ++plane) {
            uint32_t planeBytes = srcImage->rgbRowBytes[plane] * srcImage->height;
            memcpy(dstImage->rgbPlanes[plane], srcImage->rgbPlanes[plane], planeBytes);
        }
    }

    if (srcImage->yuvPlanes[AVIF_CHAN_Y] && srcImage->yuvPlanes[AVIF_CHAN_U] && srcImage->yuvPlanes[AVIF_CHAN_V]) {
        avifImageAllocatePlanes(dstImage, AVIF_PLANES_YUV);

        avifPixelFormatInfo formatInfo;
        avifGetPixelFormatInfo(srcImage->yuvFormat, &formatInfo);
        int uvHeight = dstImage->height >> formatInfo.chromaShiftY;
        for (int yuvPlane = 0; yuvPlane < 3; ++yuvPlane) {
            int aomPlaneIndex = yuvPlane;
            int planeHeight = dstImage->height;
            if (yuvPlane == AVIF_CHAN_U) {
                aomPlaneIndex = formatInfo.aomIndexU;
                planeHeight = uvHeight;
            } else if (yuvPlane == AVIF_CHAN_V) {
                aomPlaneIndex = formatInfo.aomIndexV;
                planeHeight = uvHeight;
            }

            if (!srcImage->yuvRowBytes[aomPlaneIndex]) {
                // plane is absent, move on
                continue;
            }

            for (int j = 0; j < planeHeight; ++j) {
                uint8_t * srcRow = &srcImage->yuvPlanes[aomPlaneIndex][j * srcImage->yuvRowBytes[aomPlaneIndex]];
                uint8_t * dstRow = &dstImage->yuvPlanes[yuvPlane][j * dstImage->yuvRowBytes[yuvPlane]];
                memcpy(dstRow, srcRow, dstImage->yuvRowBytes[yuvPlane]);
            }
        }
    }

    if (srcImage->alphaPlane) {
        avifImageAllocatePlanes(dstImage, AVIF_PLANES_A);
        for (int j = 0; j < dstImage->height; ++j) {
            uint8_t * srcAlphaRow = &srcImage->alphaPlane[j * srcImage->alphaRowBytes];
            uint8_t * dstAlphaRow = &dstImage->alphaPlane[j * dstImage->alphaRowBytes];
            memcpy(dstAlphaRow, srcAlphaRow, dstImage->alphaRowBytes);
        }
    }
}

void avifImageDestroy(avifImage * image)
{
    avifImageFreePlanes(image, AVIF_PLANES_ALL);
    avifRawDataFree(&image->icc);
    avifFree(image);
}

void avifImageSetProfileNone(avifImage * image)
{
    image->profileFormat = AVIF_PROFILE_FORMAT_NONE;
    avifRawDataFree(&image->icc);
}

void avifImageSetProfileICC(avifImage * image, uint8_t * icc, size_t iccSize)
{
    avifImageSetProfileNone(image);
    if (iccSize) {
        image->profileFormat = AVIF_PROFILE_FORMAT_ICC;
        avifRawDataSet(&image->icc, icc, iccSize);
    }
}

void avifImageSetProfileNCLX(avifImage * image, avifNclxColorProfile * nclx)
{
    avifImageSetProfileNone(image);
    image->profileFormat = AVIF_PROFILE_FORMAT_NCLX;
    memcpy(&image->nclx, nclx, sizeof(avifNclxColorProfile));
}

void avifImageAllocatePlanes(avifImage * image, uint32_t planes)
{
    int channelSize = avifImageUsesU16(image) ? 2 : 1;
    int fullRowBytes = channelSize * image->width;
    int fullSize = fullRowBytes * image->height;
    if (planes & AVIF_PLANES_RGB) {
        if (!image->rgbPlanes[AVIF_CHAN_R]) {
            image->rgbRowBytes[AVIF_CHAN_R] = fullRowBytes;
            image->rgbPlanes[AVIF_CHAN_R] = avifAlloc(fullSize);
            memset(image->rgbPlanes[AVIF_CHAN_R], 0, fullSize);
        }
        if (!image->rgbPlanes[AVIF_CHAN_G]) {
            image->rgbRowBytes[AVIF_CHAN_G] = fullRowBytes;
            image->rgbPlanes[AVIF_CHAN_G] = avifAlloc(fullSize);
            memset(image->rgbPlanes[AVIF_CHAN_G], 0, fullSize);
        }
        if (!image->rgbPlanes[AVIF_CHAN_B]) {
            image->rgbRowBytes[AVIF_CHAN_B] = fullRowBytes;
            image->rgbPlanes[AVIF_CHAN_B] = avifAlloc(fullSize);
            memset(image->rgbPlanes[AVIF_CHAN_B], 0, fullSize);
        }
    }
    if ((planes & AVIF_PLANES_YUV) && (image->yuvFormat != AVIF_PIXEL_FORMAT_NONE)) {
        avifPixelFormatInfo info;
        avifGetPixelFormatInfo(image->yuvFormat, &info);

        int shiftedW = image->width;
        if (info.chromaShiftX) {
            shiftedW = (image->width + 1) >> info.chromaShiftX;
        }
        int shiftedH = image->height;
        if (info.chromaShiftY) {
            shiftedH = (image->height + 1) >> info.chromaShiftY;
        }

        int uvRowBytes = channelSize * shiftedW;
        int uvSize = uvRowBytes * shiftedH;

        if (!image->yuvPlanes[AVIF_CHAN_Y]) {
            image->yuvRowBytes[AVIF_CHAN_Y] = fullRowBytes;
            image->yuvPlanes[AVIF_CHAN_Y] = avifAlloc(fullSize);
            memset(image->yuvPlanes[AVIF_CHAN_Y], 0, fullSize);
        }
        if (!image->yuvPlanes[AVIF_CHAN_U]) {
            image->yuvRowBytes[AVIF_CHAN_U] = uvRowBytes;
            image->yuvPlanes[AVIF_CHAN_U] = avifAlloc(uvSize);
            memset(image->yuvPlanes[AVIF_CHAN_U], 0, uvSize);
        }
        if (!image->yuvPlanes[AVIF_CHAN_V]) {
            image->yuvRowBytes[AVIF_CHAN_V] = uvRowBytes;
            image->yuvPlanes[AVIF_CHAN_V] = avifAlloc(uvSize);
            memset(image->yuvPlanes[AVIF_CHAN_V], 0, uvSize);
        }
    }
    if (planes & AVIF_PLANES_A) {
        if (!image->alphaPlane) {
            image->alphaRowBytes = fullRowBytes;
            image->alphaPlane = avifAlloc(fullRowBytes * image->height);
            memset(image->alphaPlane, 0, fullRowBytes * image->height);
        }
    }
}

void avifImageCopyDecoderAlpha(avifImage * image)
{
    if (image->alphaPlane && image->alphaRowBytes && image->height && image->decoderOwnsAlphaPlane) {
        int fullSize = image->alphaRowBytes * image->height;
        uint8_t * oldAlphaPlane = image->alphaPlane;
        image->alphaPlane = avifAlloc(fullSize);
        memcpy(image->alphaPlane, oldAlphaPlane, fullSize);
        image->decoderOwnsAlphaPlane = AVIF_FALSE;
    }
}

void avifImageFreePlanes(avifImage * image, uint32_t planes)
{
    if (planes & AVIF_PLANES_RGB) {
        avifFree(image->rgbPlanes[AVIF_CHAN_R]);
        image->rgbPlanes[AVIF_CHAN_R] = NULL;
        image->rgbRowBytes[AVIF_CHAN_R] = 0;
        avifFree(image->rgbPlanes[AVIF_CHAN_G]);
        image->rgbPlanes[AVIF_CHAN_G] = NULL;
        image->rgbRowBytes[AVIF_CHAN_G] = 0;
        avifFree(image->rgbPlanes[AVIF_CHAN_B]);
        image->rgbPlanes[AVIF_CHAN_B] = NULL;
        image->rgbRowBytes[AVIF_CHAN_B] = 0;
    }
    if ((planes & AVIF_PLANES_YUV) && (image->yuvFormat != AVIF_PIXEL_FORMAT_NONE)) {
        if (!image->decoderOwnsYUVPlanes) {
            avifFree(image->yuvPlanes[AVIF_CHAN_Y]);
            avifFree(image->yuvPlanes[AVIF_CHAN_U]);
            avifFree(image->yuvPlanes[AVIF_CHAN_V]);
        }
        image->yuvPlanes[AVIF_CHAN_Y] = NULL;
        image->yuvRowBytes[AVIF_CHAN_Y] = 0;
        image->yuvPlanes[AVIF_CHAN_U] = NULL;
        image->yuvRowBytes[AVIF_CHAN_U] = 0;
        image->yuvPlanes[AVIF_CHAN_V] = NULL;
        image->yuvRowBytes[AVIF_CHAN_V] = 0;
        image->decoderOwnsYUVPlanes = AVIF_FALSE;
    }
    if (planes & AVIF_PLANES_A) {
        if (!image->decoderOwnsAlphaPlane) {
            avifFree(image->alphaPlane);
        }
        image->alphaPlane = NULL;
        image->alphaRowBytes = 0;
        image->decoderOwnsAlphaPlane = AVIF_FALSE;
    }
}

avifBool avifImageUsesU16(avifImage * image)
{
    return (image->depth > 8) ? AVIF_TRUE : AVIF_FALSE;
}

// avifCodecCreate*() functions are in their respective codec_*.c files

void avifCodecDestroy(avifCodec * codec)
{
    if (codec && codec->destroyInternal) {
        codec->destroyInternal(codec);
    }
    avifFree(codec);
}
