// Copyright 2023 Google LLC
// SPDX-License-Identifier: BSD-2-Clause

#include "avif/internal.h"

#if defined(AVIF_ENABLE_EXPERIMENTAL_GAIN_MAP)

avifBool avifGainMapMetadataDoubleToFractions(avifGainMapMetadata * dst, const avifGainMapMetadataDouble * src)
{
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

#endif // AVIF_ENABLE_EXPERIMENTAL_GAIN_MAP
