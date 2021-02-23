// Copyright 2021 Yuan Tong. All rights reserved.
// SPDX-License-Identifier: BSD-2-Clause

#include "avifwic.h"

#ifdef AVIF_WINCODEC_ENABLED
#include <stdio.h>
#define COBJMACROS
#include <wincodec.h>
#include <windows.h>

#define AVIF_MAKE_GUID(name, l, w1, w2, b1, b2, b3, b4, b5, b6, b7, b8) \
    {                                                                   \
        l, w1, w2, { b1, b2, b3, b4, b5, b6, b7, b8 }                   \
    }
#define AVIF_DEFINE_GUID(name, l, w1, w2, b1, b2, b3, b4, b5, b6, b7, b8) \
    static const GUID AVIF_##name = AVIF_MAKE_GUID(name, l, w1, w2, b1, b2, b3, b4, b5, b6, b7, b8)

// Pixel formats that has 10 bit precision. Make our own copy for compatibility with older SDK.
static const GUID avifWIC10BitDepthGUIDsTable[] = {
    AVIF_MAKE_GUID(GUID_WICPixelFormat32bppBGR101010, 0x6fddc324, 0x4e03, 0x4bfe, 0xb1, 0x85, 0x3d, 0x77, 0x76, 0x8d, 0xc9, 0x14),
    AVIF_MAKE_GUID(GUID_WICPixelFormat32bppRGBA1010102, 0x25238d72, 0xfcf9, 0x4522, 0xb5, 0x14, 0x55, 0x78, 0xe5, 0xad, 0x55, 0xe0),
    AVIF_MAKE_GUID(GUID_WICPixelFormat32bppRGBA1010102XR, 0x00de6b9a, 0xc101, 0x434b, 0xb5, 0x02, 0xd0, 0x16, 0x5e, 0xe1, 0x12, 0x2c),
    AVIF_MAKE_GUID(GUID_WICPixelFormat32bppR10G10B10A2, 0x604e1bb5, 0x8a3c, 0x4b65, 0xb1, 0x1c, 0xbc, 0x0b, 0x8d, 0xd7, 0x5b, 0x7f),
    AVIF_MAKE_GUID(GUID_WICPixelFormat32bppR10G10B10A2HDR10, 0x9c215c5d, 0x1acc, 0x4f0e, 0xa4, 0xbc, 0x70, 0xfb, 0x3a, 0xe8, 0xfd, 0x28),
};
static const uint32_t avifWIC10BitDepthGUIDsTableSize = sizeof(avifWIC10BitDepthGUIDsTable) / sizeof(avifWIC10BitDepthGUIDsTable[0]);

// Pixel formats that has more than 10 bit precision.
static const GUID avifWICHighDepthGUIDsTable[] = {
    AVIF_MAKE_GUID(GUID_WICPixelFormat16bppGray, 0x6fddc324, 0x4e03, 0x4bfe, 0xb1, 0x85, 0x3d, 0x77, 0x76, 0x8d, 0xc9, 0x0b),
    AVIF_MAKE_GUID(GUID_WICPixelFormat32bppGrayFloat, 0x6fddc324, 0x4e03, 0x4bfe, 0xb1, 0x85, 0x3d, 0x77, 0x76, 0x8d, 0xc9, 0x11),
    AVIF_MAKE_GUID(GUID_WICPixelFormat48bppRGB, 0x6fddc324, 0x4e03, 0x4bfe, 0xb1, 0x85, 0x3d, 0x77, 0x76, 0x8d, 0xc9, 0x15),
    AVIF_MAKE_GUID(GUID_WICPixelFormat48bppBGR, 0xe605a384, 0xb468, 0x46ce, 0xbb, 0x2e, 0x36, 0xf1, 0x80, 0xe6, 0x43, 0x13),
    AVIF_MAKE_GUID(GUID_WICPixelFormat64bppRGB, 0xa1182111, 0x186d, 0x4d42, 0xbc, 0x6a, 0x9c, 0x83, 0x03, 0xa8, 0xdf, 0xf9),
    AVIF_MAKE_GUID(GUID_WICPixelFormat64bppRGBA, 0x6fddc324, 0x4e03, 0x4bfe, 0xb1, 0x85, 0x3d, 0x77, 0x76, 0x8d, 0xc9, 0x16),
    AVIF_MAKE_GUID(GUID_WICPixelFormat64bppBGRA, 0x1562ff7c, 0xd352, 0x46f9, 0x97, 0x9e, 0x42, 0x97, 0x6b, 0x79, 0x22, 0x46),
    AVIF_MAKE_GUID(GUID_WICPixelFormat64bppPRGBA, 0x6fddc324, 0x4e03, 0x4bfe, 0xb1, 0x85, 0x3d, 0x77, 0x76, 0x8d, 0xc9, 0x17),
    AVIF_MAKE_GUID(GUID_WICPixelFormat64bppPBGRA, 0x8c518e8e, 0xa4ec, 0x468b, 0xae, 0x70, 0xc9, 0xa3, 0x5a, 0x9c, 0x55, 0x30),
    AVIF_MAKE_GUID(GUID_WICPixelFormat16bppGrayFixedPoint, 0x6fddc324, 0x4e03, 0x4bfe, 0xb1, 0x85, 0x3d, 0x77, 0x76, 0x8d, 0xc9, 0x13),
    AVIF_MAKE_GUID(GUID_WICPixelFormat48bppRGBFixedPoint, 0x6fddc324, 0x4e03, 0x4bfe, 0xb1, 0x85, 0x3d, 0x77, 0x76, 0x8d, 0xc9, 0x12),
    AVIF_MAKE_GUID(GUID_WICPixelFormat48bppBGRFixedPoint, 0x49ca140e, 0xcab6, 0x493b, 0x9d, 0xdf, 0x60, 0x18, 0x7c, 0x37, 0x53, 0x2a),
    AVIF_MAKE_GUID(GUID_WICPixelFormat96bppRGBFixedPoint, 0x6fddc324, 0x4e03, 0x4bfe, 0xb1, 0x85, 0x3d, 0x77, 0x76, 0x8d, 0xc9, 0x18),
    AVIF_MAKE_GUID(GUID_WICPixelFormat96bppRGBFloat, 0xe3fed78f, 0xe8db, 0x4acf, 0x84, 0xc1, 0xe9, 0x7f, 0x61, 0x36, 0xb3, 0x27),
    AVIF_MAKE_GUID(GUID_WICPixelFormat128bppRGBAFloat, 0x6fddc324, 0x4e03, 0x4bfe, 0xb1, 0x85, 0x3d, 0x77, 0x76, 0x8d, 0xc9, 0x19),
    AVIF_MAKE_GUID(GUID_WICPixelFormat128bppPRGBAFloat, 0x6fddc324, 0x4e03, 0x4bfe, 0xb1, 0x85, 0x3d, 0x77, 0x76, 0x8d, 0xc9, 0x1a),
    AVIF_MAKE_GUID(GUID_WICPixelFormat128bppRGBFloat, 0x6fddc324, 0x4e03, 0x4bfe, 0xb1, 0x85, 0x3d, 0x77, 0x76, 0x8d, 0xc9, 0x1b),
    AVIF_MAKE_GUID(GUID_WICPixelFormat64bppRGBAFixedPoint, 0x6fddc324, 0x4e03, 0x4bfe, 0xb1, 0x85, 0x3d, 0x77, 0x76, 0x8d, 0xc9, 0x1d),
    AVIF_MAKE_GUID(GUID_WICPixelFormat64bppBGRAFixedPoint, 0x356de33c, 0x54d2, 0x4a23, 0xbb, 0x4, 0x9b, 0x7b, 0xf9, 0xb1, 0xd4, 0x2d),
    AVIF_MAKE_GUID(GUID_WICPixelFormat64bppRGBFixedPoint, 0x6fddc324, 0x4e03, 0x4bfe, 0xb1, 0x85, 0x3d, 0x77, 0x76, 0x8d, 0xc9, 0x40),
    AVIF_MAKE_GUID(GUID_WICPixelFormat128bppRGBAFixedPoint, 0x6fddc324, 0x4e03, 0x4bfe, 0xb1, 0x85, 0x3d, 0x77, 0x76, 0x8d, 0xc9, 0x1e),
    AVIF_MAKE_GUID(GUID_WICPixelFormat128bppRGBFixedPoint, 0x6fddc324, 0x4e03, 0x4bfe, 0xb1, 0x85, 0x3d, 0x77, 0x76, 0x8d, 0xc9, 0x41),
    AVIF_MAKE_GUID(GUID_WICPixelFormat64bppRGBAHalf, 0x6fddc324, 0x4e03, 0x4bfe, 0xb1, 0x85, 0x3d, 0x77, 0x76, 0x8d, 0xc9, 0x3a),
    AVIF_MAKE_GUID(GUID_WICPixelFormat64bppPRGBAHalf, 0x58ad26c2, 0xc623, 0x4d9d, 0xb3, 0x20, 0x38, 0x7e, 0x49, 0xf8, 0xc4, 0x42),
    AVIF_MAKE_GUID(GUID_WICPixelFormat64bppRGBHalf, 0x6fddc324, 0x4e03, 0x4bfe, 0xb1, 0x85, 0x3d, 0x77, 0x76, 0x8d, 0xc9, 0x42),
    AVIF_MAKE_GUID(GUID_WICPixelFormat48bppRGBHalf, 0x6fddc324, 0x4e03, 0x4bfe, 0xb1, 0x85, 0x3d, 0x77, 0x76, 0x8d, 0xc9, 0x3b),
    AVIF_MAKE_GUID(GUID_WICPixelFormat16bppGrayHalf, 0x6fddc324, 0x4e03, 0x4bfe, 0xb1, 0x85, 0x3d, 0x77, 0x76, 0x8d, 0xc9, 0x3e),
    AVIF_MAKE_GUID(GUID_WICPixelFormat32bppGrayFixedPoint, 0x6fddc324, 0x4e03, 0x4bfe, 0xb1, 0x85, 0x3d, 0x77, 0x76, 0x8d, 0xc9, 0x3f),
    AVIF_MAKE_GUID(GUID_WICPixelFormat64bppCMYK, 0x6fddc324, 0x4e03, 0x4bfe, 0xb1, 0x85, 0x3d, 0x77, 0x76, 0x8d, 0xc9, 0x1f),
    AVIF_MAKE_GUID(GUID_WICPixelFormat80bppCMYKAlpha, 0x6fddc324, 0x4e03, 0x4bfe, 0xb1, 0x85, 0x3d, 0x77, 0x76, 0x8d, 0xc9, 0x2d),
};

static const uint32_t avifWICHighDepthGUIDsTableSize = sizeof(avifWICHighDepthGUIDsTable) / sizeof(avifWICHighDepthGUIDsTable[0]);

// Pixel formats that we are able to handle directly.
AVIF_DEFINE_GUID(GUID_WICPixelFormat24bppBGR, 0x6fddc324, 0x4e03, 0x4bfe, 0xb1, 0x85, 0x3d, 0x77, 0x76, 0x8d, 0xc9, 0x0c);
AVIF_DEFINE_GUID(GUID_WICPixelFormat24bppRGB, 0x6fddc324, 0x4e03, 0x4bfe, 0xb1, 0x85, 0x3d, 0x77, 0x76, 0x8d, 0xc9, 0x0d);
AVIF_DEFINE_GUID(GUID_WICPixelFormat32bppBGR, 0x6fddc324, 0x4e03, 0x4bfe, 0xb1, 0x85, 0x3d, 0x77, 0x76, 0x8d, 0xc9, 0x0e);
AVIF_DEFINE_GUID(GUID_WICPixelFormat32bppBGRA, 0x6fddc324, 0x4e03, 0x4bfe, 0xb1, 0x85, 0x3d, 0x77, 0x76, 0x8d, 0xc9, 0x0f);
AVIF_DEFINE_GUID(GUID_WICPixelFormat32bppPBGRA, 0x6fddc324, 0x4e03, 0x4bfe, 0xb1, 0x85, 0x3d, 0x77, 0x76, 0x8d, 0xc9, 0x10);
AVIF_DEFINE_GUID(GUID_WICPixelFormat32bppRGB, 0xd98c6b95, 0x3efe, 0x47d6, 0xbb, 0x25, 0xeb, 0x17, 0x48, 0xab, 0x0c, 0xf1);
AVIF_DEFINE_GUID(GUID_WICPixelFormat32bppRGBA, 0xf5c7ad2d, 0x6a8d, 0x43dd, 0xa7, 0xa8, 0xa2, 0x99, 0x35, 0x26, 0x1a, 0xe9);
AVIF_DEFINE_GUID(GUID_WICPixelFormat32bppPRGBA, 0x3cc4a650, 0xa527, 0x4d37, 0xa9, 0x16, 0x31, 0x42, 0xc7, 0xeb, 0xed, 0xba);
AVIF_DEFINE_GUID(GUID_WICPixelFormat48bppRGB, 0x6fddc324, 0x4e03, 0x4bfe, 0xb1, 0x85, 0x3d, 0x77, 0x76, 0x8d, 0xc9, 0x15);
AVIF_DEFINE_GUID(GUID_WICPixelFormat48bppBGR, 0xe605a384, 0xb468, 0x46ce, 0xbb, 0x2e, 0x36, 0xf1, 0x80, 0xe6, 0x43, 0x13);
AVIF_DEFINE_GUID(GUID_WICPixelFormat64bppRGB, 0xa1182111, 0x186d, 0x4d42, 0xbc, 0x6a, 0x9c, 0x83, 0x03, 0xa8, 0xdf, 0xf9);
AVIF_DEFINE_GUID(GUID_WICPixelFormat64bppRGBA, 0x6fddc324, 0x4e03, 0x4bfe, 0xb1, 0x85, 0x3d, 0x77, 0x76, 0x8d, 0xc9, 0x16);
AVIF_DEFINE_GUID(GUID_WICPixelFormat64bppBGRA, 0x1562ff7c, 0xd352, 0x46f9, 0x97, 0x9e, 0x42, 0x97, 0x6b, 0x79, 0x22, 0x46);
AVIF_DEFINE_GUID(GUID_WICPixelFormat64bppPRGBA, 0x6fddc324, 0x4e03, 0x4bfe, 0xb1, 0x85, 0x3d, 0x77, 0x76, 0x8d, 0xc9, 0x17);
AVIF_DEFINE_GUID(GUID_WICPixelFormat64bppPBGRA, 0x8c518e8e, 0xa4ec, 0x468b, 0xae, 0x70, 0xc9, 0xa3, 0x5a, 0x9c, 0x55, 0x30);

#define CHECK_HR(A)       \
    do {                  \
        hr = (A);         \
        if (FAILED(hr))   \
            goto cleanup; \
    } while (0)

static avifBool avifSetRGBImageFormat(avifRGBImage * rgb, WICPixelFormatGUID * format, uint32_t preferDepth)
{
    if (IsEqualGUID(format, &AVIF_GUID_WICPixelFormat24bppBGR)) {
        rgb->format = AVIF_RGB_FORMAT_BGR;
        rgb->depth = 8;
        rgb->alphaPremultiplied = AVIF_FALSE;
        return AVIF_FALSE;
    } else if (IsEqualGUID(format, &AVIF_GUID_WICPixelFormat24bppRGB)) {
        rgb->format = AVIF_RGB_FORMAT_RGB;
        rgb->depth = 8;
        rgb->alphaPremultiplied = AVIF_FALSE;
        return AVIF_FALSE;
    } else if (IsEqualGUID(format, &AVIF_GUID_WICPixelFormat32bppBGR)) {
        rgb->format = AVIF_RGB_FORMAT_BGRA;
        rgb->depth = 8;
        rgb->ignoreAlpha = AVIF_TRUE;
        rgb->alphaPremultiplied = AVIF_FALSE;
        return AVIF_FALSE;
    } else if (IsEqualGUID(format, &AVIF_GUID_WICPixelFormat32bppBGRA)) {
        rgb->format = AVIF_RGB_FORMAT_BGRA;
        rgb->depth = 8;
        rgb->ignoreAlpha = AVIF_FALSE;
        rgb->alphaPremultiplied = AVIF_FALSE;
        return AVIF_FALSE;
    } else if (IsEqualGUID(format, &AVIF_GUID_WICPixelFormat32bppPBGRA)) {
        rgb->format = AVIF_RGB_FORMAT_BGRA;
        rgb->depth = 8;
        rgb->ignoreAlpha = AVIF_FALSE;
        rgb->alphaPremultiplied = AVIF_TRUE;
        return AVIF_FALSE;
    } else if (IsEqualGUID(format, &AVIF_GUID_WICPixelFormat32bppRGB)) {
        rgb->format = AVIF_RGB_FORMAT_RGBA;
        rgb->depth = 8;
        rgb->ignoreAlpha = AVIF_TRUE;
        rgb->alphaPremultiplied = AVIF_FALSE;
        return AVIF_FALSE;
    } else if (IsEqualGUID(format, &AVIF_GUID_WICPixelFormat32bppRGBA)) {
        rgb->format = AVIF_RGB_FORMAT_RGBA;
        rgb->depth = 8;
        rgb->ignoreAlpha = AVIF_FALSE;
        rgb->alphaPremultiplied = AVIF_FALSE;
        return AVIF_FALSE;
    } else if (IsEqualGUID(format, &AVIF_GUID_WICPixelFormat32bppPRGBA)) {
        rgb->format = AVIF_RGB_FORMAT_RGBA;
        rgb->depth = 8;
        rgb->ignoreAlpha = AVIF_FALSE;
        rgb->alphaPremultiplied = AVIF_TRUE;
        return AVIF_FALSE;
    } else if (IsEqualGUID(format, &AVIF_GUID_WICPixelFormat48bppBGR)) {
        rgb->format = AVIF_RGB_FORMAT_BGR;
        rgb->depth = 16;
        rgb->alphaPremultiplied = AVIF_FALSE;
        return AVIF_FALSE;
    } else if (IsEqualGUID(format, &AVIF_GUID_WICPixelFormat48bppRGB)) {
        rgb->format = AVIF_RGB_FORMAT_RGB;
        rgb->depth = 16;
        rgb->alphaPremultiplied = AVIF_FALSE;
        return AVIF_FALSE;
    } else if (IsEqualGUID(format, &AVIF_GUID_WICPixelFormat64bppBGRA)) {
        rgb->format = AVIF_RGB_FORMAT_BGRA;
        rgb->depth = 16;
        rgb->ignoreAlpha = AVIF_FALSE;
        rgb->alphaPremultiplied = AVIF_FALSE;
        return AVIF_FALSE;
    } else if (IsEqualGUID(format, &AVIF_GUID_WICPixelFormat64bppPBGRA)) {
        rgb->format = AVIF_RGB_FORMAT_BGRA;
        rgb->depth = 16;
        rgb->ignoreAlpha = AVIF_FALSE;
        rgb->alphaPremultiplied = AVIF_TRUE;
        return AVIF_FALSE;
    } else if (IsEqualGUID(format, &AVIF_GUID_WICPixelFormat64bppRGB)) {
        rgb->format = AVIF_RGB_FORMAT_RGBA;
        rgb->depth = 16;
        rgb->ignoreAlpha = AVIF_TRUE;
        rgb->alphaPremultiplied = AVIF_FALSE;
        return AVIF_FALSE;
    } else if (IsEqualGUID(format, &AVIF_GUID_WICPixelFormat64bppRGBA)) {
        rgb->format = AVIF_RGB_FORMAT_RGBA;
        rgb->depth = 16;
        rgb->ignoreAlpha = AVIF_FALSE;
        rgb->alphaPremultiplied = AVIF_FALSE;
        return AVIF_FALSE;
    } else if (IsEqualGUID(format, &AVIF_GUID_WICPixelFormat64bppPRGBA)) {
        rgb->format = AVIF_RGB_FORMAT_RGBA;
        rgb->depth = 16;
        rgb->ignoreAlpha = AVIF_FALSE;
        rgb->alphaPremultiplied = AVIF_TRUE;
        return AVIF_FALSE;
    } else if (preferDepth == 8) {
        rgb->format = AVIF_RGB_FORMAT_RGBA;
        rgb->depth = 8;
        rgb->ignoreAlpha = AVIF_FALSE;
        rgb->alphaPremultiplied = AVIF_FALSE;
        return AVIF_TRUE;
    } else {
        rgb->format = AVIF_RGB_FORMAT_RGBA;
        rgb->depth = 16;
        rgb->ignoreAlpha = AVIF_FALSE;
        rgb->alphaPremultiplied = AVIF_FALSE;
        return AVIF_TRUE;
    }
}

avifBool avifWICRead(const char * inputFilename, avifImage * avif, avifPixelFormat requestedFormat, uint32_t requestedDepth, uint32_t * outDepth)
{
    // variables
    avifBool ret = AVIF_FALSE;
    HRESULT hr;
    IWICImagingFactory * pFactory = NULL;
    IWICColorContext ** colorContexts = NULL;
    UINT colorContextCount = 0;
    IWICBitmapDecoder * pDecoder = NULL;
    IWICBitmapFrameDecode * pFrame = NULL;
    IWICFormatConverter * pConverter = NULL;
    UINT frameCount = 0;
    WICPixelFormatGUID srcFormat = GUID_WICPixelFormatUndefined;
    avifRGBImage rgb;

    // read file
    HANDLE f = CreateFileA(inputFilename, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (f == INVALID_HANDLE_VALUE) {
        fprintf(stderr, "Can't open file for read: %s\n", inputFilename);
        return ret;
    }

    // set up
    CHECK_HR(CoInitialize(NULL));
    CHECK_HR(CoCreateInstance(&(CLSID_WICImagingFactory), NULL, CLSCTX_INPROC_SERVER, &(IID_IWICImagingFactory), (LPVOID *)(&pFactory)));
    CHECK_HR(IWICImagingFactory_CreateDecoderFromFileHandle(pFactory, (ULONG_PTR)f, NULL, WICDecodeMetadataCacheOnDemand, &pDecoder));
    CHECK_HR(IWICBitmapDecoder_GetFrameCount(pDecoder, &frameCount));
    if (frameCount == 0) {
        fprintf(stderr, "No frame presented in file: %s\n", inputFilename);
        goto cleanup;
    }
    CHECK_HR(IWICBitmapDecoder_GetFrame(pDecoder, 0, &pFrame));

    // read icc
    hr = IWICBitmapFrameDecode_GetColorContexts(pFrame, 0, NULL, &colorContextCount);
    if (SUCCEEDED(hr) && colorContextCount != 0) {
        colorContexts = avifAlloc(colorContextCount * sizeof(IWICColorContext *));
        memset(colorContexts, 0, colorContextCount * sizeof(IWICColorContext *));
        for (uint32_t i = 0; i < colorContextCount; ++i) {
            CHECK_HR(IWICImagingFactory_CreateColorContext(pFactory, &colorContexts[i]));
        }

        CHECK_HR(IWICBitmapFrameDecode_GetColorContexts(pFrame, colorContextCount, colorContexts, &colorContextCount));
        for (uint32_t i = 0; i < colorContextCount; ++i) {
            WICColorContextType type;
            hr = IWICColorContext_GetType(colorContexts[i], &type);
            if (!SUCCEEDED(hr) || type != WICColorContextProfile) {
                continue;
            }

            UINT iccSize = 0;
            hr = IWICColorContext_GetProfileBytes(colorContexts[i], 0, NULL, &iccSize);
            if (!SUCCEEDED(hr) || iccSize == 0) {
                continue;
            }

            uint8_t * iccData = avifAlloc(iccSize);
            hr = IWICColorContext_GetProfileBytes(colorContexts[i], iccSize, iccData, &iccSize);
            if (!SUCCEEDED(hr)) {
                continue;
            }

            avifImageSetProfileICC(avif, iccData, iccSize);
            break;
        }
    }

    // read info
    CHECK_HR(IWICBitmapFrameDecode_GetSize(pFrame, &avif->width, &avif->height));
    CHECK_HR(IWICBitmapFrameDecode_GetPixelFormat(pFrame, &srcFormat));
    if (avif->width == 0 || avif->height == 0) {
        fprintf(stderr, "Image has zero size %s\n", inputFilename);
        goto cleanup;
    }
    avif->yuvFormat = requestedFormat;
    if (requestedDepth) {
        avif->depth = requestedDepth;
    } else {
        avif->depth = 8;
        for (uint32_t i = 0; i < avifWIC10BitDepthGUIDsTableSize; ++i) {
            if (IsEqualGUID(&srcFormat, &avifWIC10BitDepthGUIDsTable[i])) {
                avif->depth = 10;
            }
        }

        if (avif->depth != 10) {
            for (uint32_t i = 0; i < avifWICHighDepthGUIDsTableSize; ++i) {
                if (IsEqualGUID(&srcFormat, &avifWICHighDepthGUIDsTable[i])) {
                    avif->depth = 12;
                }
            }
        }
        if (outDepth) {
            *outDepth = avif->depth == 12 ? 16 : avif->depth;
        }
    }
    avifRGBImageSetDefaults(&rgb, avif);
    avifBool needConversion = avifSetRGBImageFormat(&rgb, &srcFormat, avif->depth);
    avifRGBImageAllocatePixels(&rgb);

    // read pixels
    if (!needConversion) {
        CHECK_HR(IWICBitmapFrameDecode_CopyPixels(pFrame, NULL, rgb.rowBytes, rgb.rowBytes * rgb.height, rgb.pixels));
    } else {
        CHECK_HR(IWICImagingFactory_CreateFormatConverter(pFactory, &pConverter));
        avifBool canConvert = AVIF_FALSE;
        const WICPixelFormatGUID * dstFormat = NULL;
        if (rgb.depth == 16) {
            dstFormat = &AVIF_GUID_WICPixelFormat64bppRGBA;
            CHECK_HR(IWICFormatConverter_CanConvert(pConverter, &srcFormat, dstFormat, &canConvert));
        }

        if (!canConvert) {
            dstFormat = &AVIF_GUID_WICPixelFormat32bppRGBA;
            CHECK_HR(IWICFormatConverter_CanConvert(pConverter, &srcFormat, dstFormat, &canConvert));
        }

        if (!canConvert) {
            fprintf(stderr, "Image pixel format unsupported: %s\n", inputFilename);
            goto cleanup;
        }

        CHECK_HR(IWICFormatConverter_Initialize(pConverter, (IWICBitmapSource *)pFrame, dstFormat, WICBitmapDitherTypeNone, NULL, 0.0, WICBitmapPaletteTypeCustom));
        CHECK_HR(IWICFormatConverter_CopyPixels(pConverter, NULL, rgb.rowBytes, rgb.rowBytes * rgb.height, rgb.pixels));
    }

    // convert to YUV
    if (avifImageRGBToYUV(avif, &rgb) != AVIF_RESULT_OK) {
        fprintf(stderr, "Conversion to YUV failed: %s\n", inputFilename);
        goto cleanup;
    }

    ret = AVIF_TRUE;
cleanup:
    if (pConverter != NULL) {
        IUnknown_Release(pConverter);
    }
    if (pFrame != NULL) {
        IUnknown_Release(pFrame);
    }
    if (pDecoder != NULL) {
        IUnknown_Release(pDecoder);
    }
    if (colorContexts != NULL) {
        for (uint32_t i = 0; i < colorContextCount; ++i) {
            if (colorContexts[i] != NULL) {
                IUnknown_Release(colorContexts[i]);
            }
        }
        avifFree(colorContexts);
    }
    if (pFactory != NULL) {
        IUnknown_Release(pFactory);
    }
    return ret;
}
#else
avifBool avifWICRead(const char * inputFilename, avifImage * avif, avifPixelFormat requestedFormat, uint32_t requestedDepth)
{
    (void)inputFilename;
    (void)avif;
    (void)requestedFormat;
    (void)requestedDepth;
    return AVIF_FALSE;
}
#endif
