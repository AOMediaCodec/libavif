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
        AVIF_CHECK(avifDoubleToUnsignedFraction(src->gainMapMin[i], &dst->gainMapMinN[i], &dst->gainMapMinD[i]));
        AVIF_CHECK(avifDoubleToUnsignedFraction(src->gainMapMax[i], &dst->gainMapMaxN[i], &dst->gainMapMaxD[i]));
        AVIF_CHECK(avifDoubleToUnsignedFraction(src->gainMapGamma[i], &dst->gainMapGammaN[i], &dst->gainMapGammaD[i]));
        AVIF_CHECK(avifDoubleToUnsignedFraction(src->offsetSdr[i], &dst->offsetSdrN[i], &dst->offsetSdrD[i]));
        AVIF_CHECK(avifDoubleToUnsignedFraction(src->offsetHdr[i], &dst->offsetHdrN[i], &dst->offsetHdrD[i]));
    }
    AVIF_CHECK(avifDoubleToUnsignedFraction(src->hdrCapacityMin, &dst->hdrCapacityMinN, &dst->hdrCapacityMinD));
    AVIF_CHECK(avifDoubleToUnsignedFraction(src->hdrCapacityMax, &dst->hdrCapacityMaxN, &dst->hdrCapacityMaxD));
    dst->baseRenditionIsHDR = src->baseRenditionIsHDR;
    return AVIF_TRUE;
}

avifBool avifGainMapMetadataFractionsToDouble(avifGainMapMetadataDouble * dst, const avifGainMapMetadata * src)
{
    AVIF_CHECK(dst != NULL && src != NULL);

    AVIF_CHECK(src->hdrCapacityMinD != 0);
    AVIF_CHECK(src->hdrCapacityMaxD != 0);
    for (int i = 0; i < 3; ++i) {
        AVIF_CHECK(src->gainMapMaxD[i] != 0);
        AVIF_CHECK(src->gainMapGammaD[i] != 0);
        AVIF_CHECK(src->gainMapMinD[i] != 0);
        AVIF_CHECK(src->offsetSdrD[i] != 0);
        AVIF_CHECK(src->offsetHdrD[i] != 0);
    }

    for (int i = 0; i < 3; ++i) {
        dst->gainMapMin[i] = (double)src->gainMapMinN[i] / src->gainMapMinD[i];
        dst->gainMapMax[i] = (double)src->gainMapMaxN[i] / src->gainMapMaxD[i];
        dst->gainMapGamma[i] = (double)src->gainMapGammaN[i] / src->gainMapGammaD[i];
        dst->offsetSdr[i] = (double)src->offsetSdrN[i] / src->offsetSdrD[i];
        dst->offsetHdr[i] = (double)src->offsetHdrN[i] / src->offsetHdrD[i];
    }
    dst->hdrCapacityMin = (double)src->hdrCapacityMinN / src->hdrCapacityMinD;
    dst->hdrCapacityMax = (double)src->hdrCapacityMaxN / src->hdrCapacityMaxD;
    dst->baseRenditionIsHDR = src->baseRenditionIsHDR;
    return AVIF_TRUE;
}

// ---------------------------------------------------------------------------

// Returns a weight in [-1.0, 1.0] that represents how much the gain map should be applied.
static float avifGetGainMapWeight(float hdrCapacity, const avifGainMapMetadataDouble * metadata)
{
    const float hdrCapacityMin = (float)metadata->hdrCapacityMin;
    const float hdrCapacityMax = (float)metadata->hdrCapacityMax;
    float w = 0.0f;
    if (hdrCapacity > hdrCapacityMin) {
        if (hdrCapacity < hdrCapacityMax) {
            w = (logf(hdrCapacity) - logf(hdrCapacityMin)) / (logf(hdrCapacityMax) - logf(hdrCapacityMin));
        } else {
            w = 1.0f;
        }
    }

    if (metadata->baseRenditionIsHDR) {
        w -= 1.0f;
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
                                    float hdrCapacity,
                                    avifTransferCharacteristics outputTransferCharacteristics,
                                    avifRGBImage * toneMappedImage,
                                    avifContentLightLevelInformationBox * clli,
                                    avifDiagnostics * diag)
{
    avifDiagnosticsClearError(diag);

    if (hdrCapacity < 1.0) {
        avifDiagnosticsPrintf(diag, "hdrCapacity should be >= 1, got %f", hdrCapacity);
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
    if (metadata.hdrCapacityMin > metadata.hdrCapacityMax) {
        avifDiagnosticsPrintf(diag,
                              "Invalid gain map metadata, hdrCapacityMin should be <= hdrCapacityMax, got min %f and max %f",
                              metadata.hdrCapacityMin,
                              metadata.hdrCapacityMax);
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

    const float weight = avifGetGainMapWeight(hdrCapacity, &metadata);

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

    float gainMapMinLog[3];
    float gainMapMaxLog[3];
    for (int i = 0; i < 3; ++i) {
        gainMapMinLog[i] = (float)log(metadata.gainMapMin[i]);
        gainMapMaxLog[i] = (float)log(metadata.gainMapMax[i]);
    }

    const double * offsetBase = gainMap->metadata.baseRenditionIsHDR ? metadata.offsetHdr : metadata.offsetSdr;
    const double * offsetOther = gainMap->metadata.baseRenditionIsHDR ? metadata.offsetSdr : metadata.offsetHdr;

    float rgbMaxLinear = 0; // Max tone mapped pixel value across R, G and B channels.
    float rgbSumLinear = 0; // Sum of max(r, g, b) for mapped pixels.

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

                // Undo gamma & affine transform; the result is in log space.
                const float gainMapLog = lerp(gainMapMinLog[c], gainMapMaxLog[c], powf(gainMapValue, (float)metadata.gainMapGamma[c]));
                const float toneMappedLinear = (baseLinear + (float)offsetBase[c]) * expf(gainMapLog * weight) - (float)offsetOther[c];

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
                                 float hdrCapacity,
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
                                   hdrCapacity,
                                   outputTransferCharacteristics,
                                   toneMappedImage,
                                   clli,
                                   diag);

cleanup:
    avifRGBImageFreePixels(&baseImageRgb);

    return res;
}

#endif // AVIF_ENABLE_EXPERIMENTAL_GAIN_MAP
