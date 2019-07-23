# libavif [![AppVeyor Build Status](https://ci.appveyor.com/api/projects/status/github/louquillio/libavif?branch=master&svg=true)](https://ci.appveyor.com/project/louquillio/libavif) [![Travis Build Status](https://travis-ci.com/AOMediaCodec/libavif.svg?branch=master)](https://travis-ci.com/AOMediaCodec/libavif)

This library aims to be a friendly, portable C implementation of the AV1 Image File Format, as described here:

https://aomediacodec.github.io/av1-avif/

It is a work-in-progress, but can already encode and decode all AOM supported YUV formats and bit depths (with alpha).

For now, it is recommended that you checkout/use [tagged releases](https://github.com/AOMediaCodec/libavif) instead of just using the master branch. I will regularly create new versions as bugfixes and features are added.

# Usage

## Basic Decoding (Single Image)

```c
    #include "avif/avif.h"

    // point raw.data and raw.size to the contents of an .avif(s)
    avifRawData raw;
    raw.data = ...;
    raw.size = ...;

    avifImage * image = avifImageCreateEmpty();
    avifDecoder * decoder = avifDecoderCreate();
    avifResult decodeResult = avifDecoderRead(decoder, image, &raw);
    if (decodeResult == AVIF_RESULT_OK) {
        // image is an independent copy of decoded data, decoder may be destroyed here

        ... image->width;
        ... image->height;
        ... image->depth;     // If >8, all plane ptrs below are uint16_t*
        ... image->yuvFormat; // U and V planes might be smaller than Y based on format,
                              // use avifGetPixelFormatInfo() to find out in a generic way

        // Option 1: Use YUV planes directly
        ... image->yuvPlanes;
        ... image->yuvRowBytes;

        // Option 2: Convert to RGB and use RGB planes
        avifImageYUVToRGB(image);
        ... image->rgbPlanes;
        ... image->rgbRowBytes;

        // Use alpha plane, if present
        if (image->alphaPlane) {
            ... image->alphaPlane;
            ... image->alphaRowBytes;
        }

        // Optional: query color profile
        if (image->profileFormat == AVIF_PROFILE_FORMAT_ICC) {
            // ICC profile present
            ... image->icc.data;
            ... image->icc.size;
        } else if (image->profileFormat == AVIF_PROFILE_FORMAT_NCLX) {
            // NCLX profile present
            ... image->nclx.colourPrimaries;
            ... image->nclx.transferCharacteristics;
            ... image->nclx.matrixCoefficients;
            ... image->nclx.fullRangeFlag;
        }
    } else {
        printf("ERROR: Failed to decode: %s\n", avifResultToString(result));
    }

    avifImageDestroy(image);
    avifDecoderDestroy(decoder);
```

## Advanced Decoding (Image Sequences & Avoiding Copies)
```c
    #include "avif/avif.h"

    // point raw.data and raw.size to the contents of an .avif(s)
    avifRawData raw;
    raw.data = ...;
    raw.size = ...;

    avifDecoder * decoder = avifDecoderCreate();
    avifResult decodeResult = avifDecoderParse(decoder, &raw);
    if (decodeResult == AVIF_RESULT_OK) {
        // Timing and frame information
        ... decoder->imageCount; // Total images expected to decode
        ... decoder->duration;   // Duration of entire sequence (seconds)

        for (;;) {
            avifResult nextImageResult = avifDecoderNextImage(decoder);
            if (nextImageResult == AVIF_RESULT_NO_IMAGES_REMAINING) {
                // No more images, bail out. Verify that you got the expected amount of images decoded.
                break;
            } else if (nextImageResult != AVIF_RESULT_OK) {
                printf("ERROR: Failed to decode all frames: %s\n", avifResultToString(nextImageResult));
                break;
            }

            // decoder->image now points at decoder owned planes, and the image itself
            // is also owned and dependent on decoder. decoder->image's data/pointers are
            // likely to be completely different after each call to avifDecoderNextImage().

            ... decoder->image->width;
            ... decoder->image->height;
            ... decoder->image->depth;     // If >8, all plane ptrs below are uint16_t*
            ... decoder->image->yuvFormat; // U and V planes might be smaller than Y based on format,
                                           // use avifGetPixelFormatInfo() to find out in a generic way

            // See Basic Decoding example for color profile querying

            // Option 1: Use YUV planes directly
            ... decoder->image->yuvPlanes;
            ... decoder->image->yuvRowBytes;

            // Option 2: Convert to RGB and use RGB planes
            avifImageYUVToRGB(decoder->image); // (this is legal to call on decoder->image)
            ... decoder->image->rgbPlanes;
            ... decoder->image->rgbRowBytes;

            // Use alpha plane, if present
            if (decoder->image->alphaPlane) {
                ... decoder->image->alphaPlane;
                ... decoder->image->alphaRowBytes;
            }

            // Timing and frame information
            ... decoder->imageIndex;           // Current index (0-based)
            ... decoder->imageTiming.pts;      // Current image's presentation timestamp (seconds)
            ... decoder->imageTiming.duration; // Current image's duration (seconds)

            // Optional: If you want to have a decoder-independent copy of image data
            avifImage * image = avifImageCreateEmpty();
            avifImageCopy(image, decoder->image);
            ... image;                         // do something with image
            avifImageDestroy(image);           // destroy later
        }
    } else {
        printf("ERROR: Failed to decode: %s\n", avifResultToString(result));
    }

    avifDecoderDestroy(decoder);
```

## Encoding
```c
    #include "avif/avif.h"

    int width = 32;
    int height = 32;
    int depth = 8;
    avifPixelFormat format = AVIF_PIXEL_FORMAT_YUV420;
    avifImage * image = avifImageCreate(width, height, depth, format);

    // Option 1: Populate YUV planes
    avifImageAllocatePlanes(image, AVIF_PLANES_YUV);
    ... image->yuvPlanes;
    ... image->yuvRowBytes;

    // Option 2: Populate RGB planes (if YUV planes are absent, RGB->YUV conversion will automatically happen)
    avifImageAllocatePlanes(image, AVIF_PLANES_RGB);
    ... image->rgbPlanes;
    ... image->rgbRowBytes;

    // Optional: Populate alpha plane
    avifImageAllocatePlanes(image, AVIF_PLANES_A);
    ... image->alphaPlane;
    ... image->alphaRowBytes;

    // Optional: Set color profile based on NCLX box
    avifNclxColorProfile nclx;
    nclx.colourPrimaries = AVIF_NCLX_COLOUR_PRIMARIES_BT709;
    nclx.transferCharacteristics = AVIF_NCLX_TRANSFER_CHARACTERISTICS_GAMMA22;
    nclx.matrixCoefficients = AVIF_NCLX_MATRIX_COEFFICIENTS_BT709;
    nclx.fullRangeFlag = AVIF_NCLX_FULL_RANGE;
    avifImageSetProfileNCLX(image, &nclx);

    // Optional: Set color profile based on ICC profile
    uint8_t * icc = ...;  // raw ICC profile data
    size_t iccSize = ...; // Length of raw ICC profile data
    avifImageSetProfileICC(image, icc, iccSize);

    avifRawData output = AVIF_RAW_DATA_EMPTY;
    avifEncoder * encoder = avifEncoderCreate();
    encoder->maxThreads = ...; // Choose max encoder threads, 1 to disable multithreading
    encoder->minQuantizer = AVIF_QUANTIZER_LOSSLESS;
    encoder->maxQuantizer = AVIF_QUANTIZER_LOSSLESS;
    avifResult encodeResult = avifEncoderWrite(encoder, image, &output);
    if (encodeResult == AVIF_RESULT_OK) {
        // output contains a valid .avif file's contents
        ... output.data;
        ... output.size;
    } else {
        printf("ERROR: Failed to encode: %s\n", avifResultToString(encodeResult));
    }
    avifRawDataFree(&output);
    avifEncoderDestroy(encoder);
```

# Build Notes

Building libavif requires [NASM](https://nasm.us/) and [CMake](https://cmake.org/).

Make sure nasm is available and in your PATH on your machine, then use CMake to do a basic build (Debug or Release).

You can choose between using `libaom` or `libdav1d` by using CMake options `AVIF_CODEC_AOM` and `AVIF_CODEC_DAV1D`.
`libaom` must be enabled in order for encoding to work, and `libdav1d` overrides `libaom` for decoding if both are
enabled. Currently `libdav1d` must be externally available (discoverable via CMake's `FIND_LIBRARY`) to use it.

# Prebuilt Library (Windows)

If you're building on Windows with VS2017 and want to try out libavif without going through the build process, static library builds for both Debug and Release are available on [Appveyor](https://ci.appveyor.com/project/joedrago/avif).

---

# License

Released under the BSD License.

    Copyright 2019 Joe Drago. All rights reserved.

    Redistribution and use in source and binary forms, with or without
    modification, are permitted provided that the following conditions are met:

    1. Redistributions of source code must retain the above copyright notice, this
    list of conditions and the following disclaimer.

    2. Redistributions in binary form must reproduce the above copyright notice,
    this list of conditions and the following disclaimer in the documentation
    and/or other materials provided with the distribution.

    THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
    AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
    IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
    DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
    FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
    DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
    SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
    CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
    OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
    OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
