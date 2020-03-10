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
        case AVIF_RESULT_INVALID_EXIF_PAYLOAD:      return "Invalid Exif payload";
        case AVIF_RESULT_INVALID_IMAGE_GRID:        return "Invalid image grid";
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
    image->alphaRange = AVIF_RANGE_FULL;
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
    dstImage->alphaRange = srcImage->alphaRange;

    dstImage->transformFlags = srcImage->transformFlags;
    memcpy(&dstImage->pasp, &srcImage->pasp, sizeof(dstImage->pasp));
    memcpy(&dstImage->clap, &srcImage->clap, sizeof(dstImage->clap));
    memcpy(&dstImage->irot, &srcImage->irot, sizeof(dstImage->irot));
    memcpy(&dstImage->imir, &srcImage->imir, sizeof(dstImage->pasp));

    if (srcImage->profileFormat == AVIF_PROFILE_FORMAT_ICC) {
        avifImageSetProfileICC(dstImage, srcImage->icc.data, srcImage->icc.size);
    } else if (srcImage->profileFormat == AVIF_PROFILE_FORMAT_NCLX) {
        avifImageSetProfileNCLX(dstImage, &srcImage->nclx);
    } else {
        avifImageSetProfileNone(dstImage);
    }

    avifImageSetMetadataExif(dstImage, srcImage->exif.data, srcImage->exif.size);
    avifImageSetMetadataXMP(dstImage, srcImage->xmp.data, srcImage->xmp.size);

    if (srcImage->yuvPlanes[AVIF_CHAN_Y]) {
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
                // plane is absent. If we're copying from a source without
                // them, mimic the source image's state by removing our copy.
                avifFree(dstImage->yuvPlanes[aomPlaneIndex]);
                dstImage->yuvPlanes[aomPlaneIndex] = NULL;
                dstImage->yuvRowBytes[aomPlaneIndex] = 0;
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
        for (uint32_t j = 0; j < dstImage->height; ++j) {
            uint8_t * srcAlphaRow = &srcImage->alphaPlane[j * srcImage->alphaRowBytes];
            uint8_t * dstAlphaRow = &dstImage->alphaPlane[j * dstImage->alphaRowBytes];
            memcpy(dstAlphaRow, srcAlphaRow, dstImage->alphaRowBytes);
        }
    }
}

void avifImageDestroy(avifImage * image)
{
    avifImageFreePlanes(image, AVIF_PLANES_ALL);
    avifRWDataFree(&image->icc);
    avifRWDataFree(&image->exif);
    avifRWDataFree(&image->xmp);
    avifFree(image);
}

void avifImageSetProfileNone(avifImage * image)
{
    image->profileFormat = AVIF_PROFILE_FORMAT_NONE;
    avifRWDataFree(&image->icc);
}

void avifImageSetProfileICC(avifImage * image, const uint8_t * icc, size_t iccSize)
{
    avifImageSetProfileNone(image);
    if (iccSize) {
        image->profileFormat = AVIF_PROFILE_FORMAT_ICC;
        avifRWDataSet(&image->icc, icc, iccSize);
    }
}

void avifImageSetProfileNCLX(avifImage * image, avifNclxColorProfile * nclx)
{
    avifImageSetProfileNone(image);
    image->profileFormat = AVIF_PROFILE_FORMAT_NCLX;
    memcpy(&image->nclx, nclx, sizeof(avifNclxColorProfile));
}

void avifImageSetMetadataExif(avifImage * image, const uint8_t * exif, size_t exifSize)
{
    avifRWDataSet(&image->exif, exif, exifSize);
}

void avifImageSetMetadataXMP(avifImage * image, const uint8_t * xmp, size_t xmpSize)
{
    avifRWDataSet(&image->xmp, xmp, xmpSize);
}

void avifImageAllocatePlanes(avifImage * image, uint32_t planes)
{
    int channelSize = avifImageUsesU16(image) ? 2 : 1;
    int fullRowBytes = channelSize * image->width;
    int fullSize = fullRowBytes * image->height;
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
        }
        if (!image->yuvPlanes[AVIF_CHAN_U]) {
            image->yuvRowBytes[AVIF_CHAN_U] = uvRowBytes;
            image->yuvPlanes[AVIF_CHAN_U] = avifAlloc(uvSize);
        }
        if (!image->yuvPlanes[AVIF_CHAN_V]) {
            image->yuvRowBytes[AVIF_CHAN_V] = uvRowBytes;
            image->yuvPlanes[AVIF_CHAN_V] = avifAlloc(uvSize);
        }
    }
    if (planes & AVIF_PLANES_A) {
        if (!image->alphaPlane) {
            image->alphaRowBytes = fullRowBytes;
            image->alphaPlane = avifAlloc(fullRowBytes * image->height);
        }
    }
}

void avifImageFreePlanes(avifImage * image, uint32_t planes)
{
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

void avifImageStealPlanes(avifImage * dstImage, avifImage * srcImage, uint32_t planes)
{
    avifImageFreePlanes(dstImage, planes);

    if (planes & AVIF_PLANES_YUV) {
        dstImage->yuvPlanes[AVIF_CHAN_Y] = srcImage->yuvPlanes[AVIF_CHAN_Y];
        dstImage->yuvRowBytes[AVIF_CHAN_Y] = srcImage->yuvRowBytes[AVIF_CHAN_Y];
        dstImage->yuvPlanes[AVIF_CHAN_U] = srcImage->yuvPlanes[AVIF_CHAN_U];
        dstImage->yuvRowBytes[AVIF_CHAN_U] = srcImage->yuvRowBytes[AVIF_CHAN_U];
        dstImage->yuvPlanes[AVIF_CHAN_V] = srcImage->yuvPlanes[AVIF_CHAN_V];
        dstImage->yuvRowBytes[AVIF_CHAN_V] = srcImage->yuvRowBytes[AVIF_CHAN_V];

        srcImage->yuvPlanes[AVIF_CHAN_Y] = NULL;
        srcImage->yuvRowBytes[AVIF_CHAN_Y] = 0;
        srcImage->yuvPlanes[AVIF_CHAN_U] = NULL;
        srcImage->yuvRowBytes[AVIF_CHAN_U] = 0;
        srcImage->yuvPlanes[AVIF_CHAN_V] = NULL;
        srcImage->yuvRowBytes[AVIF_CHAN_V] = 0;

        dstImage->yuvFormat = srcImage->yuvFormat;
        dstImage->yuvRange = srcImage->yuvRange;
        dstImage->decoderOwnsYUVPlanes = srcImage->decoderOwnsYUVPlanes;
        srcImage->decoderOwnsYUVPlanes = AVIF_FALSE;
    }
    if (planes & AVIF_PLANES_A) {
        dstImage->alphaPlane = srcImage->alphaPlane;
        dstImage->alphaRowBytes = srcImage->alphaRowBytes;
        dstImage->alphaRange = srcImage->alphaRange;

        srcImage->alphaPlane = NULL;
        srcImage->alphaRowBytes = 0;

        dstImage->decoderOwnsAlphaPlane = srcImage->decoderOwnsAlphaPlane;
        srcImage->decoderOwnsAlphaPlane = AVIF_FALSE;
    }
}

avifBool avifImageUsesU16(avifImage * image)
{
    return (image->depth > 8);
}

// avifCodecCreate*() functions are in their respective codec_*.c files

void avifCodecDestroy(avifCodec * codec)
{
    if (codec && codec->destroyInternal) {
        codec->destroyInternal(codec);
    }
    avifFree(codec);
}

// ---------------------------------------------------------------------------
// avifRGBImage

avifBool avifRGBFormatHasAlpha(avifRGBFormat format)
{
    return (format != AVIF_RGB_FORMAT_RGB) && (format != AVIF_RGB_FORMAT_BGR);
}

uint32_t avifRGBFormatChannelCount(avifRGBFormat format)
{
    return avifRGBFormatHasAlpha(format) ? 4 : 3;
}

uint32_t avifRGBImagePixelSize(avifRGBImage * rgb)
{
    return avifRGBFormatChannelCount(rgb->format) * ((rgb->depth > 8) ? 2 : 1);
}

void avifRGBImageSetDefaults(avifRGBImage * rgb, avifImage * image)
{
    rgb->width = image->width;
    rgb->height = image->height;
    rgb->depth = image->depth;
    rgb->format = AVIF_RGB_FORMAT_RGBA;
    rgb->pixels = NULL;
    rgb->rowBytes = 0;
}

void avifRGBImageAllocatePixels(avifRGBImage * rgb)
{
    if (rgb->pixels) {
        avifFree(rgb->pixels);
    }

    rgb->rowBytes = rgb->width * avifRGBImagePixelSize(rgb);
    rgb->pixels = avifAlloc(rgb->rowBytes * rgb->height);
}

void avifRGBImageFreePixels(avifRGBImage * rgb)
{
    if (rgb->pixels) {
        avifFree(rgb->pixels);
    }

    rgb->pixels = NULL;
    rgb->rowBytes = 0;
}

// ---------------------------------------------------------------------------
// Codec availability and versions

typedef const char * (*versionFunc)(void);
typedef avifCodec * (*avifCodecCreateFunc)(void);

struct AvailableCodec
{
    avifCodecChoice choice;
    const char * name;
    versionFunc version;
    avifCodecCreateFunc create;
    uint32_t flags;
};

// This is the main codec table; it determines all usage/availability in libavif.

static struct AvailableCodec availableCodecs[] = {
// Ordered by preference (for AUTO)

#if defined(AVIF_CODEC_DAV1D)
    { AVIF_CODEC_CHOICE_DAV1D, "dav1d", avifCodecVersionDav1d, avifCodecCreateDav1d, AVIF_CODEC_FLAG_CAN_DECODE },
#endif
#if defined(AVIF_CODEC_LIBGAV1)
    { AVIF_CODEC_CHOICE_LIBGAV1, "libgav1", avifCodecVersionGav1, avifCodecCreateGav1, AVIF_CODEC_FLAG_CAN_DECODE },
#endif
#if defined(AVIF_CODEC_RAV1E)
    { AVIF_CODEC_CHOICE_RAV1E, "rav1e", avifCodecVersionRav1e, avifCodecCreateRav1e, AVIF_CODEC_FLAG_CAN_ENCODE },
#endif
#if defined(AVIF_CODEC_AOM)
    { AVIF_CODEC_CHOICE_AOM, "aom", avifCodecVersionAOM, avifCodecCreateAOM, AVIF_CODEC_FLAG_CAN_DECODE | AVIF_CODEC_FLAG_CAN_ENCODE },
#endif
    { AVIF_CODEC_CHOICE_AUTO, NULL, NULL, NULL, 0 }
};

static const int availableCodecsCount = (sizeof(availableCodecs) / sizeof(availableCodecs[0])) - 1;

static struct AvailableCodec * findAvailableCodec(avifCodecChoice choice, uint32_t requiredFlags)
{
    for (int i = 0; i < availableCodecsCount; ++i) {
        if ((choice != AVIF_CODEC_CHOICE_AUTO) && (availableCodecs[i].choice != choice)) {
            continue;
        }
        if (requiredFlags && ((availableCodecs[i].flags & requiredFlags) != requiredFlags)) {
            continue;
        }
        return &availableCodecs[i];
    }
    return NULL;
}

const char * avifCodecName(avifCodecChoice choice, uint32_t requiredFlags)
{
    struct AvailableCodec * availableCodec = findAvailableCodec(choice, requiredFlags);
    if (availableCodec) {
        return availableCodec->name;
    }
    return NULL;
}

avifCodecChoice avifCodecChoiceFromName(const char * name)
{
    for (int i = 0; i < availableCodecsCount; ++i) {
        if (!strcmp(availableCodecs[i].name, name)) {
            return availableCodecs[i].choice;
        }
    }
    return AVIF_CODEC_CHOICE_AUTO;
}

avifCodec * avifCodecCreate(avifCodecChoice choice, uint32_t requiredFlags)
{
    struct AvailableCodec * availableCodec = findAvailableCodec(choice, requiredFlags);
    if (availableCodec) {
        return availableCodec->create();
    }
    return NULL;
}

void avifCodecVersions(char outBuffer[256])
{
    outBuffer[0] = 0;
    for (int i = 0; i < availableCodecsCount; ++i) {
        if (i > 0) {
            strcat(outBuffer, ", ");
        }
        strcat(outBuffer, availableCodecs[i].name);
        strcat(outBuffer, ":");
        strcat(outBuffer, availableCodecs[i].version());
    }
}
