// Copyright 2023 Google LLC
// SPDX-License-Identifier: BSD-2-Clause

#include "avif/internal.h"

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

#endif // AVIF_ENABLE_EXPERIMENTAL_GAIN_MAP
