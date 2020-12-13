// Copyright 2019 Joe Drago. All rights reserved.
// SPDX-License-Identifier: BSD-2-Clause

#include "avif/internal.h"

#include <float.h>
#include <math.h>
#include <string.h>

struct avifColorPrimariesTable
{
    avifColorPrimaries colorPrimariesEnum;
    const char * name;
    float primaries[8]; // rX, rY, gX, gY, bX, bY, wX, wY
};
static const struct avifColorPrimariesTable avifColorPrimariesTables[] = {
    { AVIF_COLOR_PRIMARIES_BT709, "BT.709", { 0.64f, 0.33f, 0.3f, 0.6f, 0.15f, 0.06f, 0.3127f, 0.329f } },
    { AVIF_COLOR_PRIMARIES_BT470M, "BT.470-6 System M", { 0.67f, 0.33f, 0.21f, 0.71f, 0.14f, 0.08f, 0.310f, 0.316f } },
    { AVIF_COLOR_PRIMARIES_BT470BG, "BT.470-6 System BG", { 0.64f, 0.33f, 0.29f, 0.60f, 0.15f, 0.06f, 0.3127f, 0.3290f } },
    { AVIF_COLOR_PRIMARIES_BT601, "BT.601", { 0.630f, 0.340f, 0.310f, 0.595f, 0.155f, 0.070f, 0.3127f, 0.3290f } },
    { AVIF_COLOR_PRIMARIES_SMPTE240, "SMPTE 240M", { 0.630f, 0.340f, 0.310f, 0.595f, 0.155f, 0.070f, 0.3127f, 0.3290f } },
    { AVIF_COLOR_PRIMARIES_GENERIC_FILM, "Generic film", { 0.681f, 0.319f, 0.243f, 0.692f, 0.145f, 0.049f, 0.310f, 0.316f } },
    { AVIF_COLOR_PRIMARIES_BT2020, "BT.2020", { 0.708f, 0.292f, 0.170f, 0.797f, 0.131f, 0.046f, 0.3127f, 0.3290f } },
    { AVIF_COLOR_PRIMARIES_XYZ, "XYZ", { 1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.3333f, 0.3333f } },
    { AVIF_COLOR_PRIMARIES_SMPTE431, "SMPTE RP 431-2", { 0.680f, 0.320f, 0.265f, 0.690f, 0.150f, 0.060f, 0.314f, 0.351f } },
    { AVIF_COLOR_PRIMARIES_SMPTE432, "SMPTE EG 432-1 (DCI P3)", { 0.680f, 0.320f, 0.265f, 0.690f, 0.150f, 0.060f, 0.3127f, 0.3290f } },
    { AVIF_COLOR_PRIMARIES_EBU3213, "EBU Tech. 3213-E", { 0.630f, 0.340f, 0.295f, 0.605f, 0.155f, 0.077f, 0.3127f, 0.3290f } }
};
static const int avifColorPrimariesTableSize = sizeof(avifColorPrimariesTables) / sizeof(avifColorPrimariesTables[0]);

void avifColorPrimariesGetValues(avifColorPrimaries acp, float outPrimaries[8])
{
    for (int i = 0; i < avifColorPrimariesTableSize; ++i) {
        if (avifColorPrimariesTables[i].colorPrimariesEnum == acp) {
            memcpy(outPrimaries, avifColorPrimariesTables[i].primaries, sizeof(avifColorPrimariesTables[i].primaries));
            return;
        }
    }

    // if we get here, the color primaries are unknown. Just return a reasonable default.
    memcpy(outPrimaries, avifColorPrimariesTables[0].primaries, sizeof(avifColorPrimariesTables[0].primaries));
}

static avifBool matchesTo3RoundedPlaces(float a, float b)
{
    return (fabsf(a - b) < 0.001f);
}

static avifBool primariesMatch(const float p1[8], const float p2[8])
{
    return matchesTo3RoundedPlaces(p1[0], p2[0]) && matchesTo3RoundedPlaces(p1[1], p2[1]) &&
           matchesTo3RoundedPlaces(p1[2], p2[2]) && matchesTo3RoundedPlaces(p1[3], p2[3]) && matchesTo3RoundedPlaces(p1[4], p2[4]) &&
           matchesTo3RoundedPlaces(p1[5], p2[5]) && matchesTo3RoundedPlaces(p1[6], p2[6]) && matchesTo3RoundedPlaces(p1[7], p2[7]);
}

avifColorPrimaries avifColorPrimariesFind(const float inPrimaries[8], const char ** outName)
{
    if (outName) {
        *outName = NULL;
    }

    for (int i = 0; i < avifColorPrimariesTableSize; ++i) {
        if (primariesMatch(inPrimaries, avifColorPrimariesTables[i].primaries)) {
            if (outName) {
                *outName = avifColorPrimariesTables[i].name;
            }
            return avifColorPrimariesTables[i].colorPrimariesEnum;
        }
    }
    return AVIF_COLOR_PRIMARIES_UNKNOWN;
}

struct avifMatrixCoefficientsTable
{
    avifMatrixCoefficients matrixCoefficientsEnum;
    const char * name;
    const float kr;
    const float kb;
};

// https://www.itu.int/rec/T-REC-H.273-201612-I/en
static const struct avifMatrixCoefficientsTable matrixCoefficientsTables[] = {
    //{ AVIF_MATRIX_COEFFICIENTS_IDENTITY, "Identity", 0.0f, 0.0f, }, // Handled elsewhere
    { AVIF_MATRIX_COEFFICIENTS_BT709, "BT.709", 0.2126f, 0.0722f },
    { AVIF_MATRIX_COEFFICIENTS_FCC, "FCC USFC 73.682", 0.30f, 0.11f },
    { AVIF_MATRIX_COEFFICIENTS_BT470BG, "BT.470-6 System BG", 0.299f, 0.114f },
    { AVIF_MATRIX_COEFFICIENTS_BT601, "BT.601", 0.299f, 0.114f },
    { AVIF_MATRIX_COEFFICIENTS_SMPTE240, "SMPTE ST 240", 0.212f, 0.087f },
    //{ AVIF_MATRIX_COEFFICIENTS_YCGCO, "YCgCo", 0.0f, 0.0f, }, // Handled elsewhere
    { AVIF_MATRIX_COEFFICIENTS_BT2020_NCL, "BT.2020 (non-constant luminance)", 0.2627f, 0.0593f },
    //{ AVIF_MATRIX_COEFFICIENTS_BT2020_CL, "BT.2020 (constant luminance)", 0.2627f, 0.0593f }, // FIXME: It is not an linear transformation.
    //{ AVIF_MATRIX_COEFFICIENTS_SMPTE2085, "ST 2085", 0.0f, 0.0f }, // FIXME: ST2085 can't represent using Kr and Kb.
    //{ AVIF_MATRIX_COEFFICIENTS_CHROMA_DERIVED_CL, "Chromaticity-derived constant luminance system", 0.0f, 0.0f } // FIXME: It is not an linear transformation.
    //{ AVIF_MATRIX_COEFFICIENTS_ICTCP, "BT.2100-0 ICtCp", 0.0f, 0.0f }, // FIXME: This can't represent using Kr and Kb.
};

static const int avifMatrixCoefficientsTableSize = sizeof(matrixCoefficientsTables) / sizeof(matrixCoefficientsTables[0]);

static avifBool calcYUVInfoFromCICP(const avifImage * image, float coeffs[3])
{
    if (image->matrixCoefficients == AVIF_MATRIX_COEFFICIENTS_CHROMA_DERIVED_NCL) {
        float primaries[8];
        avifColorPrimariesGetValues(image->colorPrimaries, primaries);
        float const rX = primaries[0];
        float const rY = primaries[1];
        float const gX = primaries[2];
        float const gY = primaries[3];
        float const bX = primaries[4];
        float const bY = primaries[5];
        float const wX = primaries[6];
        float const wY = primaries[7];
        float const rZ = 1.0f - (rX + rY); // (Eq. 34)
        float const gZ = 1.0f - (gX + gY); // (Eq. 35)
        float const bZ = 1.0f - (bX + bY); // (Eq. 36)
        float const wZ = 1.0f - (wX + wY); // (Eq. 37)
        float const kr = (rY * (wX * (gY * bZ - bY * gZ) + wY * (bX * gZ - gX * bZ) + wZ * (gX * bY - bX * gY))) /
                         (wY * (rX * (gY * bZ - bY * gZ) + gX * (bY * rZ - rY * bZ) + bX * (rY * gZ - gY * rZ)));
        // (Eq. 32)
        float const kb = (bY * (wX * (rY * gZ - gY * rZ) + wY * (gX * rZ - rX * gZ) + wZ * (rX * gY - gX * rY))) /
                         (wY * (rX * (gY * bZ - bY * gZ) + gX * (bY * rZ - rY * bZ) + bX * (rY * gZ - gY * rZ)));
        // (Eq. 33)
        coeffs[0] = kr;
        coeffs[2] = kb;
        coeffs[1] = 1.0f - coeffs[0] - coeffs[2];
        return AVIF_TRUE;
    } else {
        for (int i = 0; i < avifMatrixCoefficientsTableSize; ++i) {
            const struct avifMatrixCoefficientsTable * const table = &matrixCoefficientsTables[i];
            if (table->matrixCoefficientsEnum == image->matrixCoefficients) {
                coeffs[0] = table->kr;
                coeffs[2] = table->kb;
                coeffs[1] = 1.0f - coeffs[0] - coeffs[2];
                return AVIF_TRUE;
            }
        }
    }
    return AVIF_FALSE;
}

void avifCalcYUVCoefficients(const avifImage * image, float * outR, float * outG, float * outB)
{
    // (As of ISO/IEC 23000-22:2019 Amendment 2)
    // MIAF Section 7.3.6.4 "Colour information property":
    //
    // If a coded image has no associated colour property, the default property is defined as having
    // colour_type equal to 'nclx' with properties as follows:
    // -   colour_primaries equal to 1,
    // -   transfer_characteristics equal to 13,
    // -   matrix_coefficients equal to 5 or 6 (which are functionally identical), and
    // -   full_range_flag equal to 1.
    // Only if the colour information property of the image matches these default values, the colour
    // property may be omitted; all other images shall have an explicitly declared colour space via
    // association with a property of this type.
    //
    // See here for the discussion: https://github.com/AOMediaCodec/av1-avif/issues/77#issuecomment-676526097

    // matrix_coefficients of [5,6] == BT.601:
    float kr = 0.299f;
    float kb = 0.114f;
    float kg = 1.0f - kr - kb;

    float coeffs[3];
    if (calcYUVInfoFromCICP(image, coeffs)) {
        kr = coeffs[0];
        kg = coeffs[1];
        kb = coeffs[2];
    }

    *outR = kr;
    *outG = kg;
    *outB = kb;
}

struct avifTransferCharacteristicsTable
{
    avifTransferCharacteristics transferCharacteristicsEnum;
    const char * name;
    float (*const toLinear)(float);
    float (*const fromLinear)(float);
};

static float toLinear709(float gamma)
{
    if (gamma < 0.f) {
        return 0.f;
    } else if (gamma < 4.5f * 0.018053968510807f) {
        return gamma / 4.5f;
    } else if (gamma < 1.f) {
        return powf((gamma + 0.09929682680944f) / 1.09929682680944f, 1.f / 0.45f);
    } else {
        return 1.f;
    }
}

static float fromLinear709(float linear)
{
    if (linear < 0.f) {
        return 0.f;
    } else if (linear < 0.018053968510807f) {
        return linear * 4.5f;
    } else if (linear < 1.f) {
        return 1.09929682680944f * powf(linear, 0.45f) - 0.09929682680944f;
    } else {
        return 1.f;
    }
}

static float toLinear470M(float gamma)
{
    return powf(AVIF_CLAMP(gamma, 0.f, 1.f), 1.f / 2.2f);
}

static float fromLinear470M(float linear)
{
    return powf(AVIF_CLAMP(linear, 0.f, 1.f), 2.2f);
}

static float toLinear470BG(float gamma)
{
    return powf(AVIF_CLAMP(gamma, 0.f, 1.f), 1.f / 2.8f);
}

static float fromLinear470BG(float linear)
{
    return powf(AVIF_CLAMP(linear, 0.f, 1.f), 2.8f);
}

static float toLinearSMPTE240(float gamma)
{
    if (gamma < 0.f) {
        return 0.f;
    } else if (gamma < 4.f * 0.022821585529445f) {
        return gamma / 4.f;
    } else if (gamma < 1.f) {
        return powf((gamma + 0.111572195921731f) / 1.111572195921731f, 1.f / 0.45f);
    } else {
        return 1.f;
    }
}

static float fromLinearSMPTE240(float linear)
{
    if (linear < 0.f) {
        return 0.f;
    } else if (linear < 0.022821585529445f) {
        return linear * 4.f;
    } else if (linear < 1.f) {
        return 1.111572195921731f * powf(linear, 0.45f) - 0.111572195921731f;
    } else {
        return 1.f;
    }
}

static float toFromLinearLinear(float gamma)
{
    return AVIF_CLAMP(gamma, 0.f, 1.f);
}

static float toLinearLog100(float gamma)
{
    return gamma <= 0.01f ? 0.0f : 1.0f + log10f(AVIF_MIN(gamma, 1.f)) / 2.0f;
}

static float fromLinearLog100(float linear)
{
    return linear <= 0.0f ? 0.01f : powf(10.0f, 2.f * (AVIF_MIN(linear, 1.f) - 1.0f));
}

static float toLinearLog100Sqrt10(float gamma)
{
    return gamma <= 0.00316227766f ? 0.0f : 1.0f + log10f(AVIF_MIN(gamma, 1.f)) / 2.5f;
}

static float fromLinearLog100Sqrt10(float linear)
{
    return linear <= 0.0f ? 0.00316227766f : powf(10.0f, 2.5f * (AVIF_MIN(linear, 1.f) - 1.0f));
}

static float toLinearIEC61966(float gamma)
{
    if (gamma < -4.5f * 0.018053968510807f) {
        return powf((-gamma + 0.09929682680944f) / -1.09929682680944f, 1.f / 0.45f);
    } else if (gamma < 4.5f * 0.018053968510807f) {
        return gamma / 4.5f;
    } else {
        return powf((gamma + 0.09929682680944f) / 1.09929682680944f, 1.f / 0.45f);
    }
}

static float fromLinearIEC61966(float linear)
{
    if (linear < -0.018053968510807f) {
        return -1.09929682680944f * powf(-linear, 0.45f) + 0.09929682680944f;
    } else if (linear < 0.018053968510807f) {
        return linear * 4.5f;
    } else {
        return 1.09929682680944f * powf(linear, 0.45f) - 0.09929682680944f;
    }
}

static float toLinearBT1361(float gamma)
{
    if (gamma < -0.25f) {
        return -0.25f;
    } else if (gamma < 0.f) {
        return powf((gamma - 0.02482420670236f) / -0.27482420670236f, 1.f / 0.45f) / -4.f;
    } else if (gamma < 4.5f * 0.018053968510807f) {
        return gamma / 4.5f;
    } else if (gamma < 1.f) {
        return powf((gamma + 0.09929682680944f) / 1.09929682680944f, 1.f / 0.45f);
    } else {
        return 1.f;
    }
}

static float fromLinearBT1361(float linear)
{
    if (linear < -0.25f) {
        return -0.25f;
    } else if (linear < 0.f) {
        return -0.27482420670236f * powf(-4.f * linear, 0.45f) + 0.02482420670236f;
    } else if (linear < 0.018053968510807f) {
        return linear * 4.5f;
    } else if (linear < 1.f) {
        return 1.09929682680944f * powf(linear, 0.45f) - 0.09929682680944f;
    } else {
        return 1.f;
    }
}

static float toLinearSRGB(float gamma)
{
    if (gamma < 0.f) {
        return 0.f;
    } else if (gamma < 12.92f * 0.0030412825601275209f) {
        return gamma / 12.92f;
    } else if (gamma < 1.f) {
        return powf((gamma + 0.0550107189475866f) / 1.0550107189475866f, 2.4f);
    } else {
        return 1.f;
    }
}

static float fromLinearSRGB(float linear)
{
    if (linear < 0.f) {
        return 0.f;
    } else if (linear < 0.0030412825601275209f) {
        return linear * 12.92f;
    } else if (linear < 1.f) {
        return 1.0550107189475866f * powf(linear, 1.f / 2.4f) - 0.0550107189475866f;
    } else {
        return 1.f;
    }
}

static float toLinearPQ(float gamma)
{
    if (gamma > 0.f) {
        float powGamma = powf(gamma, 1.0f / 78.84375f);
        float num = AVIF_MAX(powGamma - 0.8359375f, 0.0f);
        float den = AVIF_MAX(18.8515625f - 18.6875f * powGamma, FLT_MIN);
        return powf(num / den, 1.0f / 0.1593017578125f);
    } else {
        return 0.f;
    }
}

static float fromLinearPQ(float linear)
{
    if (linear > 0.f) {
        float powLinear = powf(linear, 0.1593017578125f);
        float num = 0.1640625f * powLinear - 0.1640625f;
        float den = 1.0f + 18.6875f * powLinear;
        return powf(1.0f + num / den, 78.84375f);
    } else {
        return 0.f;
    }
}

static float toLinearSMPTE428(float gamma)
{
    return powf(0.91655527974030934f * AVIF_MAX(gamma, 0.f), 1.f / 2.6f);
}

static float fromLinearSMPTE428(float linear)
{
    return powf(AVIF_MAX(linear, 0.f), 2.6f) / 0.91655527974030934f;
}

// Conversion in BT.2100 requires RGB info. Simplify to gamma correction here.
static float toLinearHLG(float gamma)
{
    if (gamma < 0.f) {
        return 0.f;
    } else if (gamma <= 0.5f) {
        return powf((gamma * gamma) * (1.f / 3.f), 1.2f);
    } else {
        return powf((expf((gamma - 0.55991073f) / 0.17883277f) + 0.28466892f) / 12.0f, 1.2f);
    }
}

static float fromLinearHLG(float linear)
{
    linear = powf(linear, 1.f / 1.2f);
    if (linear < 0.f) {
        return 0.f;
    } else if (linear <= (1.f / 12.f)) {
        return sqrtf(3.f * linear);
    } else {
        return 0.17883277f * logf(12.f * linear - 0.28466892f) + 0.55991073f;
    }
}

static const struct avifTransferCharacteristicsTable transferCharacteristicsTables[] = {
    { AVIF_TRANSFER_CHARACTERISTICS_BT709, "BT.709", toLinear709, fromLinear709 },
    { AVIF_TRANSFER_CHARACTERISTICS_BT470M, "BT.470-6 System M", toLinear470M, fromLinear470M },
    { AVIF_TRANSFER_CHARACTERISTICS_BT470BG, "BT.470-6 System BG", toLinear470BG, fromLinear470BG },
    { AVIF_TRANSFER_CHARACTERISTICS_BT601, "BT.601", toLinear709, fromLinear709 },
    { AVIF_TRANSFER_CHARACTERISTICS_SMPTE240, "SMPTE 240M", toLinearSMPTE240, fromLinearSMPTE240 },
    { AVIF_TRANSFER_CHARACTERISTICS_LINEAR, "Linear", toFromLinearLinear, toFromLinearLinear },
    { AVIF_TRANSFER_CHARACTERISTICS_LOG100, "100:1 Log", toLinearLog100, fromLinearLog100 },
    { AVIF_TRANSFER_CHARACTERISTICS_LOG100_SQRT10, "100sqrt(10):1 Log", toLinearLog100Sqrt10, fromLinearLog100Sqrt10 },
    { AVIF_TRANSFER_CHARACTERISTICS_IEC61966, "IEC 61966-2-4", toLinearIEC61966, fromLinearIEC61966 },
    { AVIF_TRANSFER_CHARACTERISTICS_BT1361, "BT.1361", toLinearBT1361, fromLinearBT1361 },
    { AVIF_TRANSFER_CHARACTERISTICS_SRGB, "sRGB", toLinearSRGB, fromLinearSRGB },
    { AVIF_TRANSFER_CHARACTERISTICS_BT2020_10BIT, "10bit BT.2020", toLinear709, fromLinear709 },
    { AVIF_TRANSFER_CHARACTERISTICS_BT2020_12BIT, "12bit BT.2020", toLinear709, fromLinear709 },
    { AVIF_TRANSFER_CHARACTERISTICS_SMPTE2084, "SMPTE ST 2084 (PQ)", toLinearPQ, fromLinearPQ },
    { AVIF_TRANSFER_CHARACTERISTICS_SMPTE428, "SMPTE ST 428-1", toLinearSMPTE428, fromLinearSMPTE428 },
    { AVIF_TRANSFER_CHARACTERISTICS_HLG, "ARIB STD-B67 (HLG)", toLinearHLG, fromLinearHLG }
};

static const int avifTransferCharacteristicsTableSize =
    sizeof(transferCharacteristicsTables) / sizeof(transferCharacteristicsTables[0]);

avifBool avifTransferCharacteristicsGetConverter(avifTransferCharacteristics atc, float (*converter[2])(float))
{
    for (int i = 0; i < avifTransferCharacteristicsTableSize; ++i) {
        const struct avifTransferCharacteristicsTable * const table = &transferCharacteristicsTables[i];
        if (table->transferCharacteristicsEnum == atc) {
            converter[0] = table->toLinear;
            converter[1] = table->fromLinear;
            return AVIF_TRUE;
        }
    }

    // provided a reasonable default
    converter[0] = toLinear709;
    converter[1] = fromLinear709;

    return AVIF_TRUE;
}
