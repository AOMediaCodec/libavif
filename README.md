# libavif [![AppVeyor Build Status](https://ci.appveyor.com/api/projects/status/github/louquillio/libavif?branch=master&svg=true)](https://ci.appveyor.com/project/louquillio/libavif) [![Travis Build Status](https://travis-ci.com/AOMediaCodec/libavif.svg?branch=master)](https://travis-ci.com/AOMediaCodec/libavif)

This library aims to be a friendly, portable C implementation of the AV1 Image File Format, as described here:

<https://aomediacodec.github.io/av1-avif/>

It is a work-in-progress, but can already encode and decode all AOM supported YUV formats and bit depths (with alpha).

For now, it is recommended that you checkout/use [tagged releases](https://github.com/AOMediaCodec/libavif/releases) instead of just using the master branch. I will regularly create new versions as bugfixes and features are added.

## Usage

### Basic Decoding (Single Image)

```c
#include "avif/avif.h"

// NOTE: avifDecoderRead() offers the simplest means to get an avifImage that is complete independent of
// an avifDecoder, but at the cost of additional allocations and copies, and no support for image sequences.
// If you don't mind keeping around the avifDecoder while you read in the image and/or need image sequence
// support, skip ahead to the Advanced Decoding example. It is only one additional function call, and the
// avifImage is owned by the avifDecoder.

// point raw.data and raw.size to the contents of an .avif(s)
avifROData raw;
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

    // Option 1: Use YUV planes directly
    ... image->yuvPlanes;
    ... image->yuvRowBytes;
    ... image->yuvRange;
    ... image->yuvFormat;          // U and V planes might be smaller than Y based on format,
                                   // use avifGetPixelFormatInfo() to find out in a generic way
    ... image->matrixCoefficients; // specifies how to convert YUV planes to RGB
    if (image->alphaPlane) {       // Use alpha plane, if present.
        ... image->alphaPlane;
        ... image->alphaRowBytes;
        ... image->alphaRange;     // Note: This might be limited range!
    }

    // Option 2: Convert to interleaved RGB(A)/BGR(A) using a libavif-allocated buffer.
    avifRGBImage rgb;
    avifRGBImageSetDefaults(&rgb, image);
    rgb.format = ...;                 // See choices in avif.h
    rgb.depth = ...;                  // [8, 10, 12, 16]; Does not need to match image->depth.
                                      // If >8, rgb->pixels is uint16_t*
    avifRGBImageAllocatePixels(&rgb); // You can supply your own pixels/rowBytes, see Option 3
    avifImageYUVToRGB(image, &rgb);
    ... rgb.pixels;                   // Pixels in interleaved rgbFormat chosen above;
    ... rgb.rowBytes;                 // all channels are always full range
    avifRGBImageFreePixels(&rgb);

    // Option 3: Convert directly into your own pre-existing interleaved RGB(A)/BGR(A) buffer
    avifRGBImage rgb;
    avifRGBImageSetDefaults(&rgb, image);
    rgb.format = ...;                 // See choices in avif.h
    rgb.depth = ...;                  // [8, 10, 12, 16]; Does not need to match image->depth.
                                      // If >8, rgb->pixels is uint16_t*
    rgb.pixels = ...;                 // Point at your RGB(A)/BGR(A) pixels here
    rgb.rowBytes = ...;
    avifImageYUVToRGB(image, &rgb);
    ... rgb.pixels;                   // Pixels in interleaved rgbFormat chosen above;
    ... rgb.rowBytes;                 // all channels are always full range
    // Use your own buffer; no need to call avifRGBImageFreePixels()

    // Color profile information
    ... image->icc.data;                // * If present and you support ICC,
    ... image->icc.size;                //   honor this ICC profile payload
    ... image->colorPrimaries;          // * Otherwise, leverage these two values,
    ... image->transferCharacteristics; //   if not set to unspecified

    // Optional: Exif and XMP metadata querying
    if(image->exif.size > 0) {
        // Parse Exif payload
        ... image->exif.data;
        ... image->exif.size;
    }
    if(image->xmp.size > 0) {
        // Parse XMP document
        ... image->xmp.data;
        ... image->xmp.size;
    }
} else {
    printf("ERROR: Failed to decode: %s\n", avifResultToString(result));
}

avifImageDestroy(image);
avifDecoderDestroy(decoder);
```

### Advanced Decoding (Image Sequences & Avoiding Copies)

```c
#include "avif/avif.h"

// point raw.data and raw.size to the contents of an .avif(s)
avifROData raw;
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

        // Option 1: Use YUV planes directly
        ... decoder->image->yuvPlanes;
        ... decoder->image->yuvRowBytes;
        ... decoder->image->yuvRange;
        ... decoder->image->yuvFormat;          // U and V planes might be smaller than Y based on format,
                                                // use avifGetPixelFormatInfo() to find out in a generic way
        ... decoder->image->matrixCoefficients; // specifies how to convert YUV planes to RGB
        if (decoder->image->alphaPlane) {       // Use alpha plane, if present.
            ... decoder->image->alphaPlane;
            ... decoder->image->alphaRowBytes;
            ... decoder->image->alphaRange;     // Note: This might be limited range!
        }

        // Option 2: Convert to interleaved RGB(A)/BGR(A) using a libavif-allocated buffer.
        avifRGBImage rgb;
        avifRGBImageSetDefaults(&rgb, decoder->image);
        rgb.format = ...;                 // See choices in avif.h
        rgb.depth = ...;                  // [8, 10, 12, 16]; Does not need to match image->depth.
                                          // If >8, rgb->pixels is uint16_t*
        avifRGBImageAllocatePixels(&rgb); // You can supply your own pixels/rowBytes, see Option 3
        avifImageYUVToRGB(image, &rgb);
        ... rgb.pixels;                   // Pixels in interleaved rgbFormat chosen above;
        ... rgb.rowBytes;                 // all channels are always full range
        avifRGBImageFreePixels(&rgb);

        // Option 3: Convert directly into your own pre-existing interleaved RGB(A)/BGR(A) buffer
        avifRGBImage rgb;
        avifRGBImageSetDefaults(&rgb, decoder->image);
        rgb.format = ...;                 // See choices in avif.h
        rgb.depth = ...;                  // [8, 10, 12, 16]; Does not need to match image->depth.
                                          // If >8, rgb->pixels is uint16_t*
        rgb.pixels = ...;                 // Point at your RGB(A)/BGR(A) pixels here
        rgb.rowBytes = ...;
        avifImageYUVToRGB(decoder->image, &rgb);
        ... rgb.pixels;                   // Pixels in interleaved rgbFormat chosen above;
        ... rgb.rowBytes;                 // all channels are always full range
        // Use your own buffer; no need to call avifRGBImageFreePixels()

        // Color profile information
        ... decoder->image->icc.data;                // * If present and you support ICC,
        ... decoder->image->icc.size;                //   honor this ICC profile payload
        ... decoder->image->colorPrimaries;          // * Otherwise, leverage these two values,
        ... decoder->image->transferCharacteristics; //   if not set to unspecified

        // Optional: Exif and XMP metadata querying
        if(decoder->image->exif.size > 0) {
            // Parse Exif payload
            ... decoder->image->exif.data;
            ... decoder->image->exif.size;
        }
        if(decoder->image->xmp.size > 0) {
            // Parse XMP document
            ... decoder->image->xmp.data;
            ... decoder->image->xmp.size;
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

### Encoding

```c
#include "avif/avif.h"

int width = 32;
int height = 32;
int depth = 8;
avifPixelFormat format = AVIF_PIXEL_FORMAT_YUV420;
avifImage * image = avifImageCreate(width, height, depth, format);

// (Semi-)optional: Describe the color profile, YUV<->RGB conversion, and range.
// These default to "unspecified" and full range. You should at least set the
// matrixCoefficients to indicate how you would like YUV<->RGB conversion to be done.
image->colorPrimaries = AVIF_COLOR_PRIMARIES_BT709;
image->transferCharacteristics = AVIF_TRANSFER_CHARACTERISTICS_SRGB;
image->matrixCoefficients = AVIF_MATRIX_COEFFICIENTS_BT709;
image->yuvRange = AVIF_RANGE_FULL;

// Option 1: Populate YUV planes
avifImageAllocatePlanes(image, AVIF_PLANES_YUV);
... image->yuvPlanes;
... image->yuvRowBytes;

// Option 2: Convert from interleaved RGB(A)/BGR(A) using a libavif-allocated buffer.
avifRGBImage rgb;
avifRGBImageSetDefaults(&rgb, image);
rgb.depth = ...;   // [8, 10, 12, 16]; Does not need to match image->depth.
                   // If >8, rgb->pixels is uint16_t*
rgb.format = ...;  // See choices in avif.h
avifRGBImageAllocatePixels(&rgb);
... rgb->pixels;  // fill these pixels; all channel data must be full range
... rgb->rowBytes;
avifImageRGBToYUV(image, &rgb); // if alpha is present, it will also be copied/converted
avifRGBImageFreePixels(&rgb);

// Option 3: Convert directly from your own pre-existing interleaved RGB(A)/BGR(A) buffer
avifRGBImage rgb;
avifRGBImageSetDefaults(&rgb, image);
rgb.depth = ...;   // [8, 10, 12, 16]; Does not need to match image->depth.
                   // If >8, rgb->pixels is uint16_t*
rgb.format = ...;  // See choices in avif.h, match to your buffer's pixel format
rgb.pixels = ...;  // Point at your RGB(A)/BGR(A) pixels here
rgb.rowBytes = ...;
avifImageRGBToYUV(image, &rgb); // if alpha is present, it will also be copied/converted
// no need to cleanup avifRGBImage

// Optional: Populate alpha plane
// Note: This step is unnecessary if you used avifImageRGBToYUV from an
//       avifRGBImage containing an alpha channel.
avifImageAllocatePlanes(image, AVIF_PLANES_A);
... image->alphaPlane;
... image->alphaRowBytes;
... image->alphaRange;

// Optional: Set an ICC profile
uint8_t * icc = ...;  // raw ICC profile data
size_t iccSize = ...; // Length of raw ICC profile data
avifImageSetProfileICC(image, icc, iccSize);

// Optional: Set Exif and/or XMP metadata
uint8_t * exif = ...;  // raw Exif payload
size_t exifSize = ...; // Length of raw Exif payload
avifImageSetMetadataExif(image, exif, exifSize);
uint8_t * xmp = ...;  // raw XMP document
size_t xmpSize = ...; // Length of raw XMP document
avifImageSetMetadataXMP(image, xmp, xmpSize);

avifRWData output = AVIF_DATA_EMPTY;
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
avifImageDestroy(image);
avifRWDataFree(&output);
avifEncoderDestroy(encoder);
```

## Build Notes

Building libavif requires [CMake](https://cmake.org/).

No AV1 codecs are enabled by default. Enable them by enabling any of the
following CMake options:

* `AVIF_CODEC_AOM` - requires CMake, NASM
* `AVIF_CODEC_DAV1D` - requires Meson, Ninja, NASM
* `AVIF_CODEC_LIBGAV1` - requires CMake, Ninja
* `AVIF_CODEC_RAV1E` - requires cargo (Rust), NASM

These libraries (in their C API form) must be externally available
(discoverable via CMake's `FIND_LIBRARY`) to use them, or if libavif is
a child CMake project, the appropriate CMake target must already exist
by the time libavif's CMake scripts are executed.

## Local / Static Builds

The `ext/` subdirectory contains a handful of basic scripts which each pull
down a known-good copy of an AV1 codec and make a local static library build.
If you want to statically link any codec into your local (static) build of
libavif, building using one of these scripts and then enabling the associated
`AVIF_LOCAL_*` is a convenient method, but you must make sure to disable
`BUILD_SHARED_LIBS` in CMake to instruct it to make a static libavif library.

If you want to build/install shared libraries for AV1 codecs, you can still
peek inside of each script to see where the current known-good SHA is for each
codec.

## Prebuilt Library (Windows)

If you're building on Windows with Visual Studio 2019 and want to try out libavif without going through the build process, static library builds for both Debug and Release are available on [AppVeyor](https://ci.appveyor.com/project/louquillio/libavif).

---

## License

Released under the BSD License.

```markdown
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
```
