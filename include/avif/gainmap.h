// Copyright 2023 Google LLC
// SPDX-License-Identifier: BSD-2-Clause

// Utilities to deal with gain maps.
// This API is experimental and may change or be removed in the future.

#ifndef AVIF_GAINMAP_H
#define AVIF_GAINMAP_H

#include "avif/avif.h"

#ifdef __cplusplus
extern "C" {
#endif

#if defined(AVIF_ENABLE_EXPERIMENTAL_GAIN_MAP)

// Same as avifGainMapMetadata, but with fields of type double instead of uint32_t fractions.
// Use avifGainMapMetadataDoubleToFractions() to convert this to a avifGainMapMetadata.
// See avifGainMapMetadata in avif.h for detailed descriptions of fields.
typedef struct avifGainMapMetadataDouble
{
    double gainMapMin[3];
    double gainMapMax[3];
    double gainMapGamma[3];
    double offsetSdr[3];
    double offsetHdr[3];
    double hdrCapacityMin;
    double hdrCapacityMax;
    avifBool baseRenditionIsHDR;
} avifGainMapMetadataDouble;

// Converts a avifGainMapMetadataDouble to avifGainMapMetadata by converting double values
// to the closest uint32_t fractions.
// Returns AVIF_FALSE if some field values are < 0 or > UINT32_MAX.
AVIF_API avifBool avifGainMapMetadataDoubleToFractions(avifGainMapMetadata * dst, const avifGainMapMetadataDouble * src);

#endif // AVIF_ENABLE_EXPERIMENTAL_GAIN_MAP

#ifdef __cplusplus
} // extern "C"
#endif

#endif // ifndef AVIF_GAINMAP_H
