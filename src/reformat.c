// Copyright 2019 Joe Drago. All rights reserved.
// SPDX-License-Identifier: BSD-2-Clause

#include "avif/avif.h"

#include <string.h>

static avifResult reformatRGBAToYUV444(avifImage * srcImage, avifImage * dstImage, int dstDepth)
{
    // TODO: calculate coefficients
    const float kr = 0.2126f;
    const float kb = 0.0722f;
    const float kg = 1.0f - kr - kb;

    float yuvPixel[3];
    float rgbPixel[3];
    float srcMaxChannel = (float)((1 << srcImage->depth) - 1);
    float dstMaxChannel = (float)((1 << dstImage->depth) - 1);
    for (int j = 0; j < srcImage->height; ++j) {
        for (int i = 0; i < srcImage->width; ++i) {
            // Unpack RGB into normalized float
            rgbPixel[0] = srcImage->planes[0][i + (j * srcImage->strides[0])] / srcMaxChannel;
            rgbPixel[1] = srcImage->planes[1][i + (j * srcImage->strides[1])] / srcMaxChannel;
            rgbPixel[2] = srcImage->planes[2][i + (j * srcImage->strides[2])] / srcMaxChannel;

            // RGB -> YUV conversion
            float Y = (kr * rgbPixel[0]) + (kg * rgbPixel[1]) + (kb * rgbPixel[2]);
            yuvPixel[0] = Y;
            yuvPixel[1] = (rgbPixel[2] - Y) / (2 * (1 - kb));
            yuvPixel[2] = (rgbPixel[0] - Y) / (2 * (1 - kr));

            // Stuff YUV into unorm16 color layer
            yuvPixel[0] = AVIF_CLAMP(yuvPixel[0], 0.0f, 1.0f);
            yuvPixel[1] += 0.5f;
            yuvPixel[1] = AVIF_CLAMP(yuvPixel[1], 0.0f, 1.0f);
            yuvPixel[2] += 0.5f;
            yuvPixel[2] = AVIF_CLAMP(yuvPixel[2], 0.0f, 1.0f);
            for (int plane = 0; plane < 3; ++plane) {
                dstImage->planes[plane][i + (j * dstImage->strides[plane])] = (uint16_t)avifRoundf(yuvPixel[plane] * dstMaxChannel);
            }

            // reformat alpha
            float alpha = (float)srcImage->planes[3][i + (j * srcImage->strides[3])] / srcMaxChannel;
            dstImage->planes[3][i + (j * dstImage->strides[3])] = (uint16_t)avifRoundf(alpha * dstMaxChannel);
        }
    }

    return AVIF_RESULT_OK;
}

static avifResult reformatYUV444ToRGBA(avifImage * srcImage, avifImage * dstImage, int dstDepth)
{
    // TODO: calculate coefficients
    const float kr = 0.2126f;
    const float kb = 0.0722f;
    const float kg = 1.0f - kr - kb;

    float yuvPixel[3];
    float rgbPixel[3];
    float srcMaxChannel = (float)((1 << srcImage->depth) - 1);
    float dstMaxChannel = (float)((1 << dstImage->depth) - 1);
    for (int j = 0; j < srcImage->height; ++j) {
        for (int i = 0; i < srcImage->width; ++i) {
            // Unpack YUV into normalized float
            yuvPixel[0] = srcImage->planes[0][i + (j * srcImage->strides[0])] / srcMaxChannel;
            yuvPixel[1] = srcImage->planes[1][i + (j * srcImage->strides[1])] / srcMaxChannel;
            yuvPixel[2] = srcImage->planes[2][i + (j * srcImage->strides[2])] / srcMaxChannel;
            yuvPixel[1] -= 0.5f;
            yuvPixel[2] -= 0.5f;

            float Y  = yuvPixel[0];
            float Cb = yuvPixel[1];
            float Cr = yuvPixel[2];

            float R = Y + (2 * (1 - kr)) * Cr;
            float B = Y + (2 * (1 - kb)) * Cb;
            float G = Y - (
                (2 * ((kr * (1 - kr) * Cr) + (kb * (1 - kb) * Cb)))
                /
                kg);

            rgbPixel[0] = AVIF_CLAMP(R, 0.0f, 1.0f);
            rgbPixel[1] = AVIF_CLAMP(G, 0.0f, 1.0f);
            rgbPixel[2] = AVIF_CLAMP(B, 0.0f, 1.0f);
            for (int plane = 0; plane < 3; ++plane) {
                dstImage->planes[plane][i + (j * dstImage->strides[plane])] = (uint16_t)avifRoundf(rgbPixel[plane] * dstMaxChannel);
            }

            // reformat alpha
            float alpha = (float)srcImage->planes[3][i + (j * srcImage->strides[3])] / srcMaxChannel;
            dstImage->planes[3][i + (j * dstImage->strides[3])] = (uint16_t)avifRoundf(alpha * dstMaxChannel);
        }
    }

    return AVIF_RESULT_OK;
}

static avifResult reformatRGBAToYUV420(avifImage * srcImage, avifImage * dstImage, int dstDepth)
{
    // TODO: calculate coefficients
    const float kr = 0.2126f;
    const float kb = 0.0722f;
    const float kg = 1.0f - kr - kb;

    float yuvPixel[3];
    float rgbPixel[3];
    float srcMaxChannel = (float)((1 << srcImage->depth) - 1);
    float dstMaxChannel = (float)((1 << dstImage->depth) - 1);
    for (int j = 0; j < srcImage->height; ++j) {
        for (int i = 0; i < srcImage->width; ++i) {
            // Unpack RGB into normalized float
            rgbPixel[0] = srcImage->planes[0][i + (j * srcImage->strides[0])] / srcMaxChannel;
            rgbPixel[1] = srcImage->planes[1][i + (j * srcImage->strides[1])] / srcMaxChannel;
            rgbPixel[2] = srcImage->planes[2][i + (j * srcImage->strides[2])] / srcMaxChannel;

            // RGB -> YUV conversion
            float Y = (kr * rgbPixel[0]) + (kg * rgbPixel[1]) + (kb * rgbPixel[2]);
            yuvPixel[0] = Y;
            yuvPixel[1] = (rgbPixel[2] - Y) / (2 * (1 - kb));
            yuvPixel[2] = (rgbPixel[0] - Y) / (2 * (1 - kr));

            // Stuff YUV into unorm16 color layer
            yuvPixel[0] = AVIF_CLAMP(yuvPixel[0], 0.0f, 1.0f);
            yuvPixel[1] += 0.5f;
            yuvPixel[1] = AVIF_CLAMP(yuvPixel[1], 0.0f, 1.0f);
            yuvPixel[2] += 0.5f;
            yuvPixel[2] = AVIF_CLAMP(yuvPixel[2], 0.0f, 1.0f);

            dstImage->planes[0][i + (j * dstImage->strides[0])] = (uint16_t)avifRoundf(yuvPixel[0] * dstMaxChannel);

            int x = i >> 1;
            int y = j >> 1;
            dstImage->planes[1][x + (y * dstImage->strides[1])] = (uint16_t)avifRoundf(yuvPixel[1] * dstMaxChannel);
            dstImage->planes[2][x + (y * dstImage->strides[2])] = (uint16_t)avifRoundf(yuvPixel[2] * dstMaxChannel);

            // reformat alpha
            float alpha = (float)srcImage->planes[3][i + (j * srcImage->strides[3])] / srcMaxChannel;
            dstImage->planes[3][i + (j * dstImage->strides[3])] = (uint16_t)avifRoundf(alpha * dstMaxChannel);
        }
    }

    return AVIF_RESULT_OK;
}

static avifResult reformatYUV420ToRGBA(avifImage * srcImage, avifImage * dstImage, int dstDepth)
{
    // TODO: calculate coefficients
    const float kr = 0.2126f;
    const float kb = 0.0722f;
    const float kg = 1.0f - kr - kb;

    float yuvPixel[3];
    float rgbPixel[3];
    float srcMaxChannel = (float)((1 << srcImage->depth) - 1);
    float dstMaxChannel = (float)((1 << dstImage->depth) - 1);
    for (int j = 0; j < srcImage->height; ++j) {
        for (int i = 0; i < srcImage->width; ++i) {
            // Unpack YUV into normalized float
            int x = i >> 1;
            int y = j >> 1;
            yuvPixel[0] = srcImage->planes[0][i + (j * srcImage->strides[0])] / srcMaxChannel;
            yuvPixel[1] = srcImage->planes[1][x + (y * srcImage->strides[1])] / srcMaxChannel;
            yuvPixel[2] = srcImage->planes[2][x + (y * srcImage->strides[2])] / srcMaxChannel;
            yuvPixel[1] -= 0.5f;
            yuvPixel[2] -= 0.5f;

            float Y  = yuvPixel[0];
            float Cb = yuvPixel[1];
            float Cr = yuvPixel[2];

            float R = Y + (2 * (1 - kr)) * Cr;
            float B = Y + (2 * (1 - kb)) * Cb;
            float G = Y - (
                (2 * ((kr * (1 - kr) * Cr) + (kb * (1 - kb) * Cb)))
                /
                kg);

            rgbPixel[0] = AVIF_CLAMP(R, 0.0f, 1.0f);
            rgbPixel[1] = AVIF_CLAMP(G, 0.0f, 1.0f);
            rgbPixel[2] = AVIF_CLAMP(B, 0.0f, 1.0f);
            for (int plane = 0; plane < 3; ++plane) {
                dstImage->planes[plane][i + (j * dstImage->strides[plane])] = (uint16_t)avifRoundf(rgbPixel[plane] * dstMaxChannel);
            }

            // reformat alpha
            float alpha = (float)srcImage->planes[3][i + (j * srcImage->strides[3])] / srcMaxChannel;
            dstImage->planes[3][i + (j * dstImage->strides[3])] = (uint16_t)avifRoundf(alpha * dstMaxChannel);
        }
    }

    return AVIF_RESULT_OK;
}

avifResult avifImageReformatPixels(avifImage * srcImage, avifImage * dstImage, avifPixelFormat dstPixelFormat, int dstDepth)
{
    avifImageCreatePixels(dstImage, dstPixelFormat, srcImage->width, srcImage->height, dstDepth);
    switch (srcImage->pixelFormat) {
        case AVIF_PIXEL_FORMAT_RGBA:
            switch (dstPixelFormat) {
                case AVIF_PIXEL_FORMAT_YUV444:
                    return reformatRGBAToYUV444(srcImage, dstImage, dstDepth);
                case AVIF_PIXEL_FORMAT_YUV420:
                    return reformatRGBAToYUV420(srcImage, dstImage, dstDepth);
            }
        case AVIF_PIXEL_FORMAT_YUV444:
            switch (dstPixelFormat) {
                case AVIF_PIXEL_FORMAT_RGBA:
                    return reformatYUV444ToRGBA(srcImage, dstImage, dstDepth);
            }
        case AVIF_PIXEL_FORMAT_YUV420:
            switch (dstPixelFormat) {
                case AVIF_PIXEL_FORMAT_RGBA:
                    return reformatYUV420ToRGBA(srcImage, dstImage, dstDepth);
            }
    }
    return AVIF_RESULT_REFORMAT_FAILED;
}
