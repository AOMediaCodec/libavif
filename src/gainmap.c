// Copyright 2023 Google LLC
// SPDX-License-Identifier: BSD-2-Clause

#include "avif/internal.h"
#include <assert.h>
#include <float.h>
#include <math.h>
#include <string.h>

#if defined(AVIF_ENABLE_EXPERIMENTAL_GAIN_MAP)

avifBool avifGainMapMetadataDoubleToFractions(avifGainMapMetadata * dst, const avifGainMapMetadataDouble * src)
{
    AVIF_CHECK(dst != NULL && src != NULL);

    for (int i = 0; i < 3; ++i) {
        AVIF_CHECK(avifDoubleToSignedFraction(src->gainMapMin[i], &dst->gainMapMinN[i], &dst->gainMapMinD[i]));
        AVIF_CHECK(avifDoubleToSignedFraction(src->gainMapMax[i], &dst->gainMapMaxN[i], &dst->gainMapMaxD[i]));
        AVIF_CHECK(avifDoubleToUnsignedFraction(src->gainMapGamma[i], &dst->gainMapGammaN[i], &dst->gainMapGammaD[i]));
        AVIF_CHECK(avifDoubleToSignedFraction(src->baseOffset[i], &dst->baseOffsetN[i], &dst->baseOffsetD[i]));
        AVIF_CHECK(avifDoubleToSignedFraction(src->alternateOffset[i], &dst->alternateOffsetN[i], &dst->alternateOffsetD[i]));
    }
    AVIF_CHECK(avifDoubleToUnsignedFraction(src->baseHdrHeadroom, &dst->baseHdrHeadroomN, &dst->baseHdrHeadroomD));
    AVIF_CHECK(avifDoubleToUnsignedFraction(src->alternateHdrHeadroom, &dst->alternateHdrHeadroomN, &dst->alternateHdrHeadroomD));
    dst->backwardDirection = src->backwardDirection;
    dst->useBaseColorSpace = src->useBaseColorSpace;
    return AVIF_TRUE;
}

avifBool avifGainMapMetadataFractionsToDouble(avifGainMapMetadataDouble * dst, const avifGainMapMetadata * src)
{
    AVIF_CHECK(dst != NULL && src != NULL);

    AVIF_CHECK(src->baseHdrHeadroomD != 0);
    AVIF_CHECK(src->alternateHdrHeadroomD != 0);
    for (int i = 0; i < 3; ++i) {
        AVIF_CHECK(src->gainMapMaxD[i] != 0);
        AVIF_CHECK(src->gainMapGammaD[i] != 0);
        AVIF_CHECK(src->gainMapMinD[i] != 0);
        AVIF_CHECK(src->baseOffsetD[i] != 0);
        AVIF_CHECK(src->alternateOffsetD[i] != 0);
    }

    for (int i = 0; i < 3; ++i) {
        dst->gainMapMin[i] = (double)src->gainMapMinN[i] / src->gainMapMinD[i];
        dst->gainMapMax[i] = (double)src->gainMapMaxN[i] / src->gainMapMaxD[i];
        dst->gainMapGamma[i] = (double)src->gainMapGammaN[i] / src->gainMapGammaD[i];
        dst->baseOffset[i] = (double)src->baseOffsetN[i] / src->baseOffsetD[i];
        dst->alternateOffset[i] = (double)src->alternateOffsetN[i] / src->alternateOffsetD[i];
    }
    dst->baseHdrHeadroom = (double)src->baseHdrHeadroomN / src->baseHdrHeadroomD;
    dst->alternateHdrHeadroom = (double)src->alternateHdrHeadroomN / src->alternateHdrHeadroomD;
    dst->backwardDirection = src->backwardDirection;
    dst->useBaseColorSpace = src->useBaseColorSpace;
    return AVIF_TRUE;
}

static void avifGainMapMetadataSetDefaults(avifGainMapMetadataDouble * metadata)
{
    memset(metadata, 0, sizeof(avifGainMapMetadata));
    for (int i = 0; i < 3; ++i) {
        metadata->baseOffset[i] = 0.015625;      // 1/64
        metadata->alternateOffset[i] = 0.015625; // 1/64
        metadata->gainMapGamma[i] = 1.0;
    }
    metadata->baseHdrHeadroom = 0.0;
    metadata->alternateHdrHeadroom = 1.0;
    metadata->useBaseColorSpace = AVIF_TRUE;
}

// ---------------------------------------------------------------------------
// Apply a gain map.

// Returns a weight in [-1.0, 1.0] that represents how much the gain map should be applied.
static float avifGetGainMapWeight(float hdrHeadroom, const avifGainMapMetadataDouble * metadata)
{
    const float baseHdrHeadroom = (float)metadata->baseHdrHeadroom;
    const float alternateHdrHeadroom = (float)metadata->alternateHdrHeadroom;
    float w = AVIF_CLAMP((hdrHeadroom - baseHdrHeadroom) / (alternateHdrHeadroom - baseHdrHeadroom), 0.0f, 1.0f);
    if (metadata->backwardDirection) {
        w *= -1.0f;
    }
    return w;
}

// Linear interpolation between 'a' and 'b' (returns 'a' if w == 0.0f, returns 'b' if w == 1.0f).
static inline float lerp(float a, float b, float w)
{
    return (1.0f - w) * a + w * b;
}

#define SDR_WHITE_NITS 203.0f

avifResult avifRGBImageApplyGainMap(const avifRGBImage * baseImage,
                                    avifTransferCharacteristics transferCharacteristics,
                                    const avifGainMap * gainMap,
                                    float hdrHeadroom,
                                    avifTransferCharacteristics outputTransferCharacteristics,
                                    avifRGBImage * toneMappedImage,
                                    avifContentLightLevelInformationBox * clli,
                                    avifDiagnostics * diag)
{
    avifDiagnosticsClearError(diag);

    if (hdrHeadroom < 0.0f) {
        avifDiagnosticsPrintf(diag, "hdrHeadroom should be >= 0, got %f", hdrHeadroom);
        return AVIF_RESULT_INVALID_ARGUMENT;
    }
    if (baseImage == NULL || gainMap == NULL || toneMappedImage == NULL) {
        avifDiagnosticsPrintf(diag, "NULL input image");
        return AVIF_RESULT_INVALID_ARGUMENT;
    }

    avifGainMapMetadataDouble metadata;
    if (!avifGainMapMetadataFractionsToDouble(&metadata, &gainMap->metadata)) {
        avifDiagnosticsPrintf(diag, "Invalid gain map metadata, a denominator value is zero");
        return AVIF_RESULT_INVALID_ARGUMENT;
    }
    for (int i = 0; i < 3; ++i) {
        if (metadata.gainMapGamma[i] <= 0) {
            avifDiagnosticsPrintf(diag, "Invalid gain map metadata, gamma should be strictly positive");
            return AVIF_RESULT_INVALID_ARGUMENT;
        }
    }

    const uint32_t width = baseImage->width;
    const uint32_t height = baseImage->height;
    avifImage * rescaledGainMap = NULL;
    avifRGBImage rgbGainMap;
    // Basic zero-initialization for now, avifRGBImageSetDefaults() is called later on.
    memset(&rgbGainMap, 0, sizeof(rgbGainMap));

    avifResult res = AVIF_RESULT_OK;
    toneMappedImage->width = width;
    toneMappedImage->height = height;
    AVIF_CHECKRES(avifRGBImageAllocatePixels(toneMappedImage));

    // --- After this point, the function should exit with 'goto cleanup' to free allocated pixels.

    const float weight = avifGetGainMapWeight(hdrHeadroom, &metadata);

    // Early exit if the gain map does not need to be applied and the pixel format is the same.
    if (weight == 0.0f && outputTransferCharacteristics == transferCharacteristics && baseImage->format == toneMappedImage->format &&
        baseImage->depth == toneMappedImage->depth && baseImage->isFloat == toneMappedImage->isFloat) {
        assert(baseImage->rowBytes == toneMappedImage->rowBytes);
        assert(baseImage->height == toneMappedImage->height);
        // Copy the base image.
        memcpy(toneMappedImage->pixels, baseImage->pixels, baseImage->rowBytes * baseImage->height);
        goto cleanup;
    }

    avifRGBColorSpaceInfo baseRGBInfo;
    avifRGBColorSpaceInfo toneMappedPixelRGBInfo;
    if (!avifGetRGBColorSpaceInfo(baseImage, &baseRGBInfo) || !avifGetRGBColorSpaceInfo(toneMappedImage, &toneMappedPixelRGBInfo)) {
        avifDiagnosticsPrintf(diag, "Unsupported RGB color space");
        res = AVIF_RESULT_NOT_IMPLEMENTED;
        goto cleanup;
    }

    const avifTransferFunction gammaToLinear = avifTransferCharacteristicsGetGammaToLinearFunction(transferCharacteristics);
    const avifTransferFunction linearToGamma = avifTransferCharacteristicsGetLinearToGammaFunction(outputTransferCharacteristics);

    // Early exit if the gain map does not need to be applied.
    if (weight == 0.0f) {
        // Just convert from one rgb format to another.
        for (uint32_t j = 0; j < height; ++j) {
            for (uint32_t i = 0; i < width; ++i) {
                float basePixelRGBA[4];
                avifGetRGBAPixel(baseImage, i, j, &baseRGBInfo, basePixelRGBA);
                if (outputTransferCharacteristics != transferCharacteristics) {
                    for (int c = 0; c < 3; ++c) {
                        basePixelRGBA[c] = AVIF_CLAMP(linearToGamma(gammaToLinear(basePixelRGBA[c])), 0.0f, 1.0f);
                    }
                }
                avifSetRGBAPixel(toneMappedImage, i, j, &toneMappedPixelRGBInfo, basePixelRGBA);
            }
        }
        goto cleanup;
    }

    if (gainMap->image->width != width || gainMap->image->height != height) {
        rescaledGainMap = avifImageCreateEmpty();
        const avifCropRect rect = { 0, 0, gainMap->image->width, gainMap->image->height };
        res = avifImageSetViewRect(rescaledGainMap, gainMap->image, &rect);
        if (res != AVIF_RESULT_OK) {
            goto cleanup;
        }
        res = avifImageScale(rescaledGainMap, width, height, diag);
        if (res != AVIF_RESULT_OK) {
            goto cleanup;
        }
    }
    const avifImage * const gainMapImage = (rescaledGainMap != NULL) ? rescaledGainMap : gainMap->image;

    avifRGBImageSetDefaults(&rgbGainMap, gainMapImage);
    res = avifRGBImageAllocatePixels(&rgbGainMap);
    if (res != AVIF_RESULT_OK) {
        goto cleanup;
    }
    res = avifImageYUVToRGB(gainMapImage, &rgbGainMap);
    if (res != AVIF_RESULT_OK) {
        goto cleanup;
    }

    avifRGBColorSpaceInfo gainMapRGBInfo;
    if (!avifGetRGBColorSpaceInfo(&rgbGainMap, &gainMapRGBInfo)) {
        avifDiagnosticsPrintf(diag, "Unsupported RGB color space");
        res = AVIF_RESULT_NOT_IMPLEMENTED;
        goto cleanup;
    }

    float rgbMaxLinear = 0; // Max tone mapped pixel value across R, G and B channels.
    float rgbSumLinear = 0; // Sum of max(r, g, b) for mapped pixels.
    const float gammaInv[3] = { 1.0f / (float)metadata.gainMapGamma[0],
                                1.0f / (float)metadata.gainMapGamma[1],
                                1.0f / (float)metadata.gainMapGamma[2] };

    for (uint32_t j = 0; j < height; ++j) {
        for (uint32_t i = 0; i < width; ++i) {
            float basePixelRGBA[4];
            avifGetRGBAPixel(baseImage, i, j, &baseRGBInfo, basePixelRGBA);
            float gainMapRGBA[4];
            avifGetRGBAPixel(&rgbGainMap, i, j, &gainMapRGBInfo, gainMapRGBA);

            // Apply gain map.
            float toneMappedPixelRGBA[4];
            float pixelRgbMaxLinear = 0.0f; //  = max(r, g, b) for this pixel
            for (int c = 0; c < 3; ++c) {
                const float baseLinear = gammaToLinear(basePixelRGBA[c]);
                const float gainMapValue = gainMapRGBA[c];

                // Undo gamma & affine transform; the result is in log2 space.
                const float gainMapLog2 =
                    lerp((float)metadata.gainMapMin[c], (float)metadata.gainMapMax[c], powf(gainMapValue, gammaInv[c]));
                const float toneMappedLinear = (baseLinear + (float)metadata.baseOffset[c]) * exp2f(gainMapLog2 * weight) -
                                               (float)metadata.alternateOffset[c];

                if (toneMappedLinear > rgbMaxLinear) {
                    rgbMaxLinear = toneMappedLinear;
                }
                if (toneMappedLinear > pixelRgbMaxLinear) {
                    pixelRgbMaxLinear = toneMappedLinear;
                }

                const float toneMappedGamma = linearToGamma(toneMappedLinear);
                toneMappedPixelRGBA[c] = AVIF_CLAMP(toneMappedGamma, 0.0f, 1.0f);
            }
            toneMappedPixelRGBA[3] = basePixelRGBA[3]; // Alpha is unaffected by tone mapping.
            rgbSumLinear += pixelRgbMaxLinear;
            avifSetRGBAPixel(toneMappedImage, i, j, &toneMappedPixelRGBInfo, toneMappedPixelRGBA);
        }
    }
    if (clli != NULL) {
        // For exact CLLI value definitions, see ISO/IEC 23008-2 section D.3.35
        // at https://standards.iso.org/ittf/PubliclyAvailableStandards/index.html
        // See also discussion in https://github.com/AOMediaCodec/libavif/issues/1727

        // Convert extended SDR (where 1.0 is SDR white) to nits.
        clli->maxCLL = (uint16_t)AVIF_CLAMP(avifRoundf(rgbMaxLinear * SDR_WHITE_NITS), 0.0f, (float)UINT16_MAX);
        const float rgbAverageLinear = rgbSumLinear / (width * height);
        clli->maxPALL = (uint16_t)AVIF_CLAMP(avifRoundf(rgbAverageLinear * SDR_WHITE_NITS), 0.0f, (float)UINT16_MAX);
    }

cleanup:
    avifRGBImageFreePixels(&rgbGainMap);
    if (rescaledGainMap != NULL) {
        avifImageDestroy(rescaledGainMap);
    }

    return res;
}

avifResult avifImageApplyGainMap(const avifImage * baseImage,
                                 const avifGainMap * gainMap,
                                 float hdrHeadroom,
                                 avifTransferCharacteristics outputTransferCharacteristics,
                                 avifRGBImage * toneMappedImage,
                                 avifContentLightLevelInformationBox * clli,
                                 avifDiagnostics * diag)
{
    avifDiagnosticsClearError(diag);

    avifRGBImage baseImageRgb;
    avifRGBImageSetDefaults(&baseImageRgb, baseImage);
    AVIF_CHECKRES(avifRGBImageAllocatePixels(&baseImageRgb));
    avifResult res = avifImageYUVToRGB(baseImage, &baseImageRgb);
    if (res != AVIF_RESULT_OK) {
        goto cleanup;
    }

    res = avifRGBImageApplyGainMap(&baseImageRgb,
                                   baseImage->transferCharacteristics,
                                   gainMap,
                                   hdrHeadroom,
                                   outputTransferCharacteristics,
                                   toneMappedImage,
                                   clli,
                                   diag);

cleanup:
    avifRGBImageFreePixels(&baseImageRgb);

    return res;
}

// ---------------------------------------------------------------------------
// Create a gain map.

// A histogram of gain map values is used to remove outliers, which helps with
// gain map accuracy/compression.
#define NUM_HISTOGRAM_BUCKETS 1000 // Empirical value
// Arbitrary max value, roughly equivalent to bringing SDR white to max PQ white (log2(10000/203) ~= 5.62)
#define BUCKET_MAX_VALUE 6.0f
#define BUCKET_MIN_VALUE -BUCKET_MAX_VALUE
#define BUCKET_RANGE (BUCKET_MAX_VALUE - BUCKET_MIN_VALUE)
#define MAX_OUTLIERS_RATIO 0.001f // 0.1%

// Returns the index of the histogram bucket for a given value.
static int avifValueToBucketIdx(float v)
{
    v = AVIF_CLAMP(v, BUCKET_MIN_VALUE, BUCKET_MAX_VALUE);
    return AVIF_MIN((int)avifRoundf((v - BUCKET_MIN_VALUE) / BUCKET_RANGE * NUM_HISTOGRAM_BUCKETS), NUM_HISTOGRAM_BUCKETS - 1);
}
// Returns the lower end of the value range belonging to the given histogram bucket.
static float avifBucketIdxToValue(int idx)
{
    return idx * BUCKET_RANGE / NUM_HISTOGRAM_BUCKETS + BUCKET_MIN_VALUE;
}

// Finds the approximate min/max values from the given histogram, excluding outliers.
// Outliers have at least one empty bucket between them and the rest of the distribution.
// At most 0.1% of values can be removed as outliers.
static void avifFindMinMaxWithoutOutliers(int histogram[NUM_HISTOGRAM_BUCKETS], float * rangeMin, float * rangeMax)
{
    int totalCount = 0;
    for (int i = 0; i < NUM_HISTOGRAM_BUCKETS; ++i) {
        totalCount += histogram[i];
    }

    const int maxOutliersOnEachSide = (int)avifRoundf(totalCount * MAX_OUTLIERS_RATIO / 2.0f);

    int leftOutliers = 0;
    *rangeMin = BUCKET_MIN_VALUE;
    for (int i = 0; i < NUM_HISTOGRAM_BUCKETS; ++i) {
        leftOutliers += histogram[i];
        if (leftOutliers > maxOutliersOnEachSide) {
            break;
        }
        if (histogram[i] == 0) {
            *rangeMin = avifBucketIdxToValue(i + 1); // +1 to get the higher end of the bucket.
        }
    }

    int rightOutliers = 0;
    *rangeMax = BUCKET_MAX_VALUE;
    for (int i = NUM_HISTOGRAM_BUCKETS - 1; i >= 0; --i) {
        rightOutliers += histogram[i];
        if (rightOutliers > maxOutliersOnEachSide) {
            break;
        }
        if (histogram[i] == 0) {
            *rangeMax = avifBucketIdxToValue(i);
        }
    }
}

avifResult avifComputeGainMapRGB(const avifRGBImage * baseRgbImage,
                                 avifTransferCharacteristics baseTransferCharacteristics,
                                 const avifRGBImage * altRgbImage,
                                 avifTransferCharacteristics altTransferCharacteristics,
                                 avifColorPrimaries colorPrimaries,
                                 avifGainMap * gainMap,
                                 avifDiagnostics * diag)
{
    avifDiagnosticsClearError(diag);

    AVIF_CHECKERR(baseRgbImage != NULL && altRgbImage != NULL && gainMap != NULL && gainMap->image != NULL, AVIF_RESULT_INVALID_ARGUMENT);
    if (baseRgbImage->width != altRgbImage->width || baseRgbImage->height != altRgbImage->height) {
        avifDiagnosticsPrintf(diag, "Both images should have the same dimensions");
        return AVIF_RESULT_INVALID_ARGUMENT;
    }
    if (gainMap->image->width == 0 || gainMap->image->height == 0 || gainMap->image->depth == 0 ||
        gainMap->image->yuvFormat <= AVIF_PIXEL_FORMAT_NONE || gainMap->image->yuvFormat >= AVIF_PIXEL_FORMAT_COUNT) {
        avifDiagnosticsPrintf(diag, "gianMap->image should be non null with desired width, height, depth and yuvFormat set");
        return AVIF_RESULT_INVALID_ARGUMENT;
    }

    const int width = baseRgbImage->width;
    const int height = baseRgbImage->height;

    avifRGBColorSpaceInfo baseRGBInfo;
    avifRGBColorSpaceInfo altRGBInfo;
    if (!avifGetRGBColorSpaceInfo(baseRgbImage, &baseRGBInfo) || !avifGetRGBColorSpaceInfo(altRgbImage, &altRGBInfo)) {
        avifDiagnosticsPrintf(diag, "Unsupported RGB color space");
        return AVIF_RESULT_NOT_IMPLEMENTED;
    }

    float * gainMapF[3] = { 0 }; // Temporary buffers for the gain map as floating point values, one per RGB channel.
    avifRGBImage gainMapRGB;
    memset(&gainMapRGB, 0, sizeof(gainMapRGB));
    avifImage * gainMapImage = gainMap->image;

    avifResult res = AVIF_RESULT_OK;
    // --- After this point, the function should exit with 'goto cleanup' to free allocated resources.

    const avifBool singleChannel = (gainMap->image->yuvFormat == AVIF_PIXEL_FORMAT_YUV400);
    const int numGainMapChannels = singleChannel ? 1 : 3;
    for (int c = 0; c < numGainMapChannels; ++c) {
        gainMapF[c] = avifAlloc(width * height * sizeof(float));
        if (gainMapF[c] == NULL) {
            res = AVIF_RESULT_OUT_OF_MEMORY;
            goto cleanup;
        }
    }

    avifGainMapMetadataDouble gainMapMetadata;
    avifGainMapMetadataSetDefaults(&gainMapMetadata);

    float (*baseGammaToLinear)(float) = avifTransferCharacteristicsGetGammaToLinearFunction(baseTransferCharacteristics);
    float (*altGammaToLinear)(float) = avifTransferCharacteristicsGetGammaToLinearFunction(altTransferCharacteristics);
    float y_coeffs[3];
    avifColorPrimariesComputeYCoeffs(colorPrimaries, y_coeffs);

    // Compute histograms to find and remove outliers.
    int histograms[3][NUM_HISTOGRAM_BUCKETS] = { 0 };

    // Compute raw gain map values.
    float baseMax = 1.0f;
    float altMax = 1.0f;
    for (int j = 0; j < height; ++j) {
        for (int i = 0; i < width; ++i) {
            float baseRGBA[4];
            avifGetRGBAPixel(baseRgbImage, i, j, &baseRGBInfo, baseRGBA);
            float altRGBA[4];
            avifGetRGBAPixel(altRgbImage, i, j, &altRGBInfo, altRGBA);

            // Convert to linear.
            for (int c = 0; c < 3; ++c) {
                baseRGBA[c] = baseGammaToLinear(baseRGBA[c]);
                altRGBA[c] = altGammaToLinear(altRGBA[c]);
            }

            for (int c = 0; c < numGainMapChannels; ++c) {
                float base = baseRGBA[c];
                float alt = altRGBA[c];
                if (singleChannel) {
                    // Convert to grayscale.
                    base = y_coeffs[0] * baseRGBA[0] + y_coeffs[1] * baseRGBA[1] + y_coeffs[2] * baseRGBA[2];
                    alt = y_coeffs[0] * altRGBA[0] + y_coeffs[1] * altRGBA[1] + y_coeffs[2] * altRGBA[2];
                }
                if (base > baseMax) {
                    baseMax = base;
                }
                if (alt > altMax) {
                    altMax = alt;
                }
                const float ratioLog2 =
                    log2f((alt + (float)gainMapMetadata.alternateOffset[c]) / (base + (float)gainMapMetadata.baseOffset[c]));
                ++(histograms[c][avifValueToBucketIdx(ratioLog2)]);

                gainMapF[c][j * width + i] = ratioLog2;
            }
        }
    }

    // Find approximate min/max for each channel, discarding outliers.
    float gainMapMinLog2[3] = { 0.0f, 0.0f, 0.0f };
    float gainMapMaxLog2[3] = { 0.0f, 0.0f, 0.0f };
    for (int c = 0; c < numGainMapChannels; ++c) {
        avifFindMinMaxWithoutOutliers(histograms[c], &gainMapMinLog2[c], &gainMapMaxLog2[c]);
    }

    // Fill in the gain map's metadata.
    for (int c = 0; c < 3; ++c) {
        gainMapMetadata.gainMapMin[c] = gainMapMinLog2[singleChannel ? 0 : c];
        gainMapMetadata.gainMapMax[c] = gainMapMaxLog2[singleChannel ? 0 : c];
        gainMapMetadata.baseHdrHeadroom = avifRoundf(log2f(baseMax));
        gainMapMetadata.alternateHdrHeadroom = avifRoundf(log2f(altMax));
        // baseOffset, alternateOffset and gainMapGamma are all left to their default values.
        // They could be tweaked based on the images to optimize quality/compression.
    }
    if (!avifGainMapMetadataDoubleToFractions(&gainMap->metadata, &gainMapMetadata)) {
        res = AVIF_RESULT_UNKNOWN_ERROR;
        goto cleanup;
    }

    // Scale the gain map values to map [min, max] range to [0, 1].
    for (int c = 0; c < numGainMapChannels; ++c) {
        const float range = gainMapMaxLog2[c] - gainMapMinLog2[c];
        if (range <= 0.0f) {
            continue;
        }

        for (int j = 0; j < height; ++j) {
            for (int i = 0; i < width; ++i) {
                // Remap [min; max] range to [0; 1]
                const float v = AVIF_CLAMP(gainMapF[c][j * width + i], gainMapMinLog2[c], gainMapMaxLog2[c]);
                gainMapF[c][j * width + i] = powf((v - gainMapMinLog2[c]) / range, (float)gainMapMetadata.gainMapGamma[c]);
            }
        }
    }

    // Convert the gain map to YUV.
    const uint32_t requestedWidth = gainMapImage->width;
    const uint32_t requestedHeight = gainMapImage->height;
    gainMapImage->width = width;
    gainMapImage->height = height;

    avifImageFreePlanes(gainMapImage, AVIF_PLANES_ALL); // Free planes in case they were already allocated.
    res = avifImageAllocatePlanes(gainMapImage, AVIF_PLANES_YUV);
    if (res != AVIF_RESULT_OK) {
        goto cleanup;
    }

    avifRGBImageSetDefaults(&gainMapRGB, gainMapImage);
    res = avifRGBImageAllocatePixels(&gainMapRGB);
    if (res != AVIF_RESULT_OK) {
        goto cleanup;
    }

    avifRGBColorSpaceInfo gainMapRGBInfo;
    if (!avifGetRGBColorSpaceInfo(&gainMapRGB, &gainMapRGBInfo)) {
        avifDiagnosticsPrintf(diag, "Unsupported RGB color space");
        return AVIF_RESULT_NOT_IMPLEMENTED;
    }
    for (int j = 0; j < height; ++j) {
        for (int i = 0; i < width; ++i) {
            const int offset = j * width + i;
            const float r = gainMapF[0][offset];
            const float g = singleChannel ? r : gainMapF[1][offset];
            const float b = singleChannel ? r : gainMapF[2][offset];
            const float rgbaPixel[4] = { r, g, b, 1.0f };
            avifSetRGBAPixel(&gainMapRGB, i, j, &gainMapRGBInfo, rgbaPixel);
        }
    }

    res = avifImageRGBToYUV(gainMapImage, &gainMapRGB);
    if (res != AVIF_RESULT_OK) {
        goto cleanup;
    }

    // Scale down the gain map if requested.
    // Another way would be to scale the source images, but it seems to perform worse.
    if (requestedWidth != gainMapImage->width || requestedHeight != gainMapImage->height) {
        AVIF_CHECKRES(avifImageScale(gainMap->image, requestedWidth, requestedHeight, diag));
    }

cleanup:
    for (int c = 0; c < 3; ++c) {
        avifFree(gainMapF[c]);
    }
    avifRGBImageFreePixels(&gainMapRGB);
    if (res != AVIF_RESULT_OK) {
        avifImageFreePlanes(gainMapImage, AVIF_PLANES_ALL);
    }

    return res;
}

avifResult avifComputeGainMap(const avifImage * baseImage, const avifImage * altImage, avifGainMap * gainMap, avifDiagnostics * diag)
{
    avifDiagnosticsClearError(diag);

    if (baseImage->icc.size > 0 || altImage->icc.size > 0) {
        avifDiagnosticsPrintf(diag, "Computing gain maps for images with ICC profiles is not supported");
        return AVIF_RESULT_NOT_IMPLEMENTED;
    }
    if (baseImage->colorPrimaries != altImage->colorPrimaries) {
        avifDiagnosticsPrintf(diag,
                              "Computing gain maps for images with different color primaries is not supported, got %d and %d",
                              baseImage->colorPrimaries,
                              altImage->colorPrimaries);
        return AVIF_RESULT_NOT_IMPLEMENTED;
    }
    if (baseImage->width != altImage->width || baseImage->height != altImage->height) {
        avifDiagnosticsPrintf(diag,
                              "Image dimensions don't match, got %dx%d and %dx%d",
                              baseImage->width,
                              baseImage->height,
                              altImage->width,
                              altImage->height);
        return AVIF_RESULT_INVALID_ARGUMENT;
    }

    avifResult res = AVIF_RESULT_OK;

    avifRGBImage baseImageRgb;
    avifRGBImageSetDefaults(&baseImageRgb, baseImage);
    avifRGBImage altImageRgb;
    avifRGBImageSetDefaults(&altImageRgb, altImage);

    AVIF_CHECKRES(avifRGBImageAllocatePixels(&baseImageRgb));
    // --- After this point, the function should exit with 'goto cleanup' to free allocated resources.

    res = avifImageYUVToRGB(baseImage, &baseImageRgb);
    if (res != AVIF_RESULT_OK) {
        goto cleanup;
    }
    res = avifRGBImageAllocatePixels(&altImageRgb);
    if (res != AVIF_RESULT_OK) {
        goto cleanup;
    }
    res = avifImageYUVToRGB(altImage, &altImageRgb);
    if (res != AVIF_RESULT_OK) {
        goto cleanup;
    }

    res = avifComputeGainMapRGB(&baseImageRgb,
                                baseImage->transferCharacteristics,
                                &altImageRgb,
                                altImage->transferCharacteristics,
                                baseImage->colorPrimaries,
                                gainMap,
                                diag);

cleanup:
    avifRGBImageFreePixels(&baseImageRgb);
    avifRGBImageFreePixels(&altImageRgb);
    return res;
}

#endif // AVIF_ENABLE_EXPERIMENTAL_GAIN_MAP
