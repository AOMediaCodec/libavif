// Copyright 2022 Google LLC. All rights reserved.
// SPDX-License-Identifier: BSD-2-Clause

#include "avif/avif.h"

#include <assert.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

//------------------------------------------------------------------------------

// ICC color profiles are not checked by libavif so the content does not matter. This is a truncated widespread ICC color profile.
static const uint8_t sampleICC[] = { 0x00, 0x00, 0x02, 0x0c, 0x6c, 0x63, 0x6d, 0x73, 0x02, 0x10, 0x00, 0x00,
                                     0x6d, 0x6e, 0x74, 0x72, 0x52, 0x47, 0x42, 0x20, 0x58, 0x59, 0x5a, 0x20 };
static const size_t sampleICCSize = sizeof(sampleICC) / sizeof(sampleICC[0]);

// Exif bytes are partially checked by libavif. This is a truncated widespread Exif metadata chunk.
static const uint8_t sampleExif[] = { 0xff, 0x1,  0x45, 0x78, 0x69, 0x76, 0x32, 0xff, 0xe1, 0x12, 0x5a, 0x45,
                                      0x78, 0x69, 0x66, 0x0,  0x0,  0x49, 0x49, 0x2a, 0x0,  0x8,  0x0,  0x0 };
static const size_t sampleExifSize = sizeof(sampleExif) / sizeof(sampleExif[0]);

// XMP bytes are not checked by libavif so the content does not matter. This is a truncated widespread XMP metadata chunk.
static const uint8_t sampleXMP[] = { 0x3c, 0x3f, 0x78, 0x70, 0x61, 0x63, 0x6b, 0x65, 0x74, 0x20, 0x62, 0x65,
                                     0x67, 0x69, 0x6e, 0x3d, 0x22, 0xef, 0xbb, 0xbf, 0x22, 0x20, 0x69, 0x64 };
static const size_t sampleXMPSize = sizeof(sampleXMP) / sizeof(sampleXMP[0]);

//------------------------------------------------------------------------------
// TODO(yguyon): Move these functions to a tests/common/helper.c shared with avifgridapitest.c

// Fills a plane with a repeating gradient.
static void fillPlane(int width, int height, int depth, uint8_t * row, uint32_t rowBytes)
{
    assert(depth == 8 || depth == 10 || depth == 12); // Values allowed by AV1.
    const int maxValuePlusOne = 1 << depth;
    for (int y = 0; y < height; ++y) {
        if (depth == 8) {
            memset(row, y % maxValuePlusOne, width);
        } else {
            for (int x = 0; x < width; ++x) {
                ((uint16_t *)row)[x] = (uint16_t)(y % maxValuePlusOne);
            }
        }
        row += rowBytes;
    }
}

// Creates an image where the pixel values are defined but do not matter.
// Returns false in case of memory failure.
static avifBool createImage(int width, int height, int depth, avifPixelFormat yuvFormat, avifBool createAlpha, avifImage ** image)
{
    *image = avifImageCreate(width, height, depth, yuvFormat);
    if (*image == NULL) {
        printf("ERROR: avifImageCreate() failed\n");
        return AVIF_FALSE;
    }
    avifImageAllocatePlanes(*image, createAlpha ? AVIF_PLANES_ALL : AVIF_PLANES_YUV);

    avifPixelFormatInfo formatInfo;
    avifGetPixelFormatInfo((*image)->yuvFormat, &formatInfo);
    uint32_t uvWidth = ((*image)->width + formatInfo.chromaShiftX) >> formatInfo.chromaShiftX;
    uint32_t uvHeight = ((*image)->height + formatInfo.chromaShiftY) >> formatInfo.chromaShiftY;

    const int planeCount = formatInfo.monochrome ? 1 : AVIF_PLANE_COUNT_YUV;
    for (int plane = 0; plane < planeCount; ++plane) {
        fillPlane((plane == AVIF_CHAN_Y) ? (*image)->width : uvWidth,
                  (plane == AVIF_CHAN_Y) ? (*image)->height : uvHeight,
                  (*image)->depth,
                  (*image)->yuvPlanes[plane],
                  (*image)->yuvRowBytes[plane]);
    }

    if (createAlpha) {
        fillPlane((*image)->width, (*image)->height, (*image)->depth, (*image)->alphaPlane, (*image)->alphaRowBytes);
    }
    return AVIF_TRUE;
}

static avifBool createImage1x1(avifImage ** image)
{
    return createImage(/*width=*/1, /*height=*/1, /*depth=*/10, AVIF_PIXEL_FORMAT_YUV444, /*createAlpha=*/AVIF_TRUE, image);
}

//------------------------------------------------------------------------------

// Encodes the image. Returns false in case of failure.
static avifBool encode(const avifImage * image, avifRWData * output)
{
    avifBool success = AVIF_FALSE;
    avifEncoder * encoder = NULL;

    encoder = avifEncoderCreate();
    if (encoder == NULL) {
        printf("ERROR: avifEncoderCreate() failed\n");
        goto cleanup;
    }
    encoder->speed = AVIF_SPEED_FASTEST;
    if (avifEncoderWrite(encoder, image, output) != AVIF_RESULT_OK) {
        printf("ERROR: avifEncoderWrite() failed\n");
        goto cleanup;
    }

    success = AVIF_TRUE;
cleanup:
    if (encoder != NULL) {
        avifEncoderDestroy(encoder);
    }
    return success;
}

// Decodes the data. Returns false in case of failure.
static avifBool decode(const avifRWData * encodedAvif, avifImage ** image)
{
    avifBool success = AVIF_FALSE;
    *image = avifImageCreateEmpty();
    avifDecoder * const decoder = avifDecoderCreate();
    if (*image == NULL || decoder == NULL) {
        printf("ERROR: memory allocation failed\n");
        goto cleanup;
    }
    if (avifDecoderReadMemory(decoder, *image, encodedAvif->data, encodedAvif->size) != AVIF_RESULT_OK) {
        printf("ERROR: avifDecoderReadMemory() failed\n");
        goto cleanup;
    }
    success = AVIF_TRUE;
cleanup:
    if (decoder != NULL) {
        avifDecoderDestroy(decoder);
    }
    return success;
}

//------------------------------------------------------------------------------

// Returns true if the decoded metadata matches the expected output metadata given the input metadata.
static avifBool metadataIsEqual(avifRWData legacyInputItem, const avifRWData * inputItems, avifRWData legacyOutputItem, const avifRWData * outputItems)
{
    if (inputItems) {
        // legacyInputItem should be ignored.
        const avifRWData * inputItem = inputItems;
        const avifRWData * outputItem = outputItems;
        while (inputItem->size != 0) {
            if (!outputItem || (outputItem->size != inputItem->size) ||
                (memcmp(outputItem->data, inputItem->data, outputItem->size) != 0)) {
                return AVIF_FALSE;
            }
            ++inputItem;
            ++outputItem;
        }
        if (outputItem->size != 0) {
            return AVIF_FALSE;
        }
    } else if (legacyInputItem.size != 0) {
        // Legacy metadata input only.
        if (!outputItems || (outputItems[0].size != legacyInputItem.size) ||
            (memcmp(outputItems[0].data, legacyInputItem.data, outputItems[0].size) != 0)) {
            return AVIF_FALSE;
        }
        if (outputItems[1].size != 0) {
            return AVIF_FALSE;
        }
    } else {
        // No metadata input.
        if (outputItems) {
            return AVIF_FALSE;
        }
    }

    // Verify legacy decoding API is behaving according to documentation.
    if ((legacyOutputItem.size != 0) != (outputItems && (outputItems[0].size != 0))) {
        return AVIF_FALSE;
    }
    if ((legacyOutputItem.size != 0) && (memcmp(legacyOutputItem.data, outputItems[0].data, legacyOutputItem.size) != 0)) {
        return AVIF_FALSE;
    }
    return AVIF_TRUE;
}

// Encodes, decodes then compares the metadata of the input and decoded images.
static avifBool encodeDecode(const avifImage * image)
{
    avifBool success = AVIF_FALSE;
    avifRWData encodedAvif = { 0 };
    avifImage * decodedImage = NULL;
    if (!encode(image, &encodedAvif)) {
        goto cleanup;
    }
    if (!decode(&encodedAvif, &decodedImage)) {
        goto cleanup;
    }

    if ((image->icc.size != 0) != (decodedImage->icc.size != 0)) {
        printf("ERROR: icc mismatch\n");
        goto cleanup;
    }
    if (!metadataIsEqual(image->exif, image->exifItems, decodedImage->exif, decodedImage->exifItems)) {
        printf("ERROR: Exif metadata mismatch\n");
        goto cleanup;
    }
    if (!metadataIsEqual(image->xmp, image->xmpItems, decodedImage->xmp, decodedImage->xmpItems)) {
        printf("ERROR: XMP metadata mismatch\n");
        goto cleanup;
    }
    success = AVIF_TRUE;
cleanup:
    avifRWDataFree(&encodedAvif);
    if (decodedImage) {
        avifImageDestroy(decodedImage);
    }
    return success;
}

//------------------------------------------------------------------------------

// Encodes, decodes then verifies that the output metadata matches the input metadata defined by the arguments.
static avifBool encodeDecodeMetadataItems(avifBool useICC, avifBool useLegacyExif, avifBool useLegacyXMP, size_t exifItemCount, size_t xmpItemCount)
{
    avifBool success = AVIF_FALSE;
    avifImage * image;
    if (!createImage1x1(&image)) {
        goto cleanup;
    }

    if (useICC) {
        avifImageSetProfileICC(image, sampleICC, sampleICCSize);
    }

    if (useLegacyExif) {
        avifImageSetMetadataExif(image, sampleExif, sampleExifSize);
        exifItemCount = (exifItemCount != 0) ? exifItemCount - 1 : 0;
    }
    for (size_t i = 0; i < exifItemCount; ++i) {
        if (avifImagePushMetadataExif(image, sampleExif, sampleExifSize) != AVIF_RESULT_OK) {
            goto cleanup;
        }
        image->exifItems[i].data[image->exifItems[i].size - 1] = i; // Make Exif item unique.
    }

    if (useLegacyXMP) {
        avifImageSetMetadataXMP(image, sampleXMP, sampleXMPSize);
        xmpItemCount = (xmpItemCount != 0) ? xmpItemCount - 1 : 0;
    }
    for (size_t i = 0; i < xmpItemCount; ++i) {
        if (avifImagePushMetadataXMP(image, sampleXMP, sampleXMPSize) != AVIF_RESULT_OK) {
            goto cleanup;
        }
        image->xmpItems[i].data[image->xmpItems[i].size - 1] = i; // Make XMP item unique.
    }

    if (!encodeDecode(image)) {
        goto cleanup;
    }
    success = AVIF_TRUE;
cleanup:
    if (image) {
        avifImageDestroy(image);
    }
    return success;
}

int main(void)
{
    for (avifBool useICC = AVIF_FALSE; useICC <= AVIF_TRUE; ++useICC) {
        for (avifBool useLegacyExif = AVIF_FALSE; useLegacyExif <= AVIF_TRUE; ++useLegacyExif) {
            for (avifBool useLegacyXMP = AVIF_FALSE; useLegacyXMP <= AVIF_TRUE; ++useLegacyXMP) {
                for (size_t exifItemCount = 0; exifItemCount <= 3; ++exifItemCount) {
                    for (size_t xmpItemCount = 0; xmpItemCount <= 3; ++xmpItemCount) {
                        if (!encodeDecodeMetadataItems(useICC, useLegacyExif, useLegacyXMP, exifItemCount, xmpItemCount)) {
                            return EXIT_FAILURE;
                        }
                    }
                }
            }
        }
    }
    return EXIT_SUCCESS;
}
