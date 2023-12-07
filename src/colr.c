// Copyright 2019 Joe Drago. All rights reserved.
// SPDX-License-Identifier: BSD-2-Clause

#include "avif/internal.h"

#include <assert.h>
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

avifResult avifTransferCharacteristicsGetGamma(avifTransferCharacteristics atc, float * gamma)
{
    switch (atc) {
        case AVIF_TRANSFER_CHARACTERISTICS_BT470M:
            *gamma = 2.2f;
            return AVIF_RESULT_OK;
        case AVIF_TRANSFER_CHARACTERISTICS_BT470BG:
            *gamma = 2.8f;
            return AVIF_RESULT_OK;
        case AVIF_TRANSFER_CHARACTERISTICS_LINEAR:
            *gamma = 1.0f;
            return AVIF_RESULT_OK;
        default:
            return AVIF_RESULT_INVALID_ARGUMENT;
    }
}

avifTransferCharacteristics avifTransferCharacteristicsFindByGamma(float gamma)
{
    if (matchesTo3RoundedPlaces(gamma, 2.2f)) {
        return AVIF_TRANSFER_CHARACTERISTICS_BT470M;
    } else if (matchesTo3RoundedPlaces(gamma, 1.0f)) {
        return AVIF_TRANSFER_CHARACTERISTICS_LINEAR;
    } else if (matchesTo3RoundedPlaces(gamma, 2.8f)) {
        return AVIF_TRANSFER_CHARACTERISTICS_BT470BG;
    }

    return AVIF_TRANSFER_CHARACTERISTICS_UNKNOWN;
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
        avifColorPrimariesComputeYCoeffs(image->colorPrimaries, coeffs);
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

// ---------------------------------------------------------------------------
// Transfer characteristics
//
// Transfer characteristics are defined in ITU-T H.273 https://www.itu.int/rec/T-REC-H.273-201612-S/en
// with formulas for linear to gamma conversion in Table 3.
// This is based on tongyuantongyu's implementation in https://github.com/AOMediaCodec/libavif/pull/444
// with some fixes/changes in the first commit:
// - Fixed 5 transfer curves where toLinear and toGamma functions were swapped (470M, 470BG, Log100,
//   Log100Sqrt10 and SMPTE428)
// - 'avifToLinearLog100' and 'avifToLinearLog100Sqrt10' were modified to return the middle of the
//   range of linear values that are gamma-encoded to 0.0 in order to reduce the max round trip error,
//   based on vrabaud's change in
//   https://chromium.googlesource.com/webm/libwebp/+/25d94f473b10882b8bee9288d00539001b692042
// - In this file, PQ and HLG return "extended SDR" linear values in [0.0, 10000/203] and
//   [0.0, 1000/203] respectively, where a value of 1.0 means SDR white brightness (203 nits), and any
//   value above 1.0 is brigther.
// See git history for further changes.

struct avifTransferCharacteristicsTable
{
    avifTransferCharacteristics transferCharacteristicsEnum;
    const char * name;
    avifTransferFunction toLinear;
    avifTransferFunction toGamma;
};

static float avifToLinear709(float gamma)
{
    if (gamma < 0.0f) {
        return 0.0f;
    } else if (gamma < 4.5f * 0.018053968510807f) {
        return gamma / 4.5f;
    } else if (gamma < 1.0f) {
        return powf((gamma + 0.09929682680944f) / 1.09929682680944f, 1.0f / 0.45f);
    } else {
        return 1.0f;
    }
}

static float avifToGamma709(float linear)
{
    if (linear < 0.0f) {
        return 0.0f;
    } else if (linear < 0.018053968510807f) {
        return linear * 4.5f;
    } else if (linear < 1.0f) {
        return 1.09929682680944f * powf(linear, 0.45f) - 0.09929682680944f;
    } else {
        return 1.0f;
    }
}

static float avifToLinear470M(float gamma)
{
    return powf(AVIF_CLAMP(gamma, 0.0f, 1.0f), 2.2f);
}

static float avifToGamma470M(float linear)
{
    return powf(AVIF_CLAMP(linear, 0.0f, 1.0f), 1.0f / 2.2f);
}

static float avifToLinear470BG(float gamma)
{
    return powf(AVIF_CLAMP(gamma, 0.0f, 1.0f), 2.8f);
}

static float avifToGamma470BG(float linear)
{
    return powf(AVIF_CLAMP(linear, 0.0f, 1.0f), 1.0f / 2.8f);
}

static float avifToLinearSMPTE240(float gamma)
{
    if (gamma < 0.0f) {
        return 0.0f;
    } else if (gamma < 4.0f * 0.022821585529445f) {
        return gamma / 4.0f;
    } else if (gamma < 1.0f) {
        return powf((gamma + 0.111572195921731f) / 1.111572195921731f, 1.0f / 0.45f);
    } else {
        return 1.0f;
    }
}

static float avifToGammaSMPTE240(float linear)
{
    if (linear < 0.0f) {
        return 0.0f;
    } else if (linear < 0.022821585529445f) {
        return linear * 4.0f;
    } else if (linear < 1.0f) {
        return 1.111572195921731f * powf(linear, 0.45f) - 0.111572195921731f;
    } else {
        return 1.0f;
    }
}

static float avifToGammaLinear(float gamma)
{
    return AVIF_CLAMP(gamma, 0.0f, 1.0f);
}

static float avifToLinearLog100(float gamma)
{
    // The function is non-bijective so choose the middle of [0, 0.01].
    const float mid_interval = 0.01f / 2.f;
    return (gamma <= 0.0f) ? mid_interval : powf(10.0f, 2.f * (AVIF_MIN(gamma, 1.f) - 1.0f));
}

static float avifToGammaLog100(float linear)
{
    return linear <= 0.01f ? 0.0f : 1.0f + log10f(AVIF_MIN(linear, 1.0f)) / 2.0f;
}

static float avifToLinearLog100Sqrt10(float gamma)
{
    // The function is non-bijective so choose the middle of [0, 0.00316227766f].
    const float mid_interval = 0.00316227766f / 2.f;
    return (gamma <= 0.0f) ? mid_interval : powf(10.0f, 2.5f * (AVIF_MIN(gamma, 1.f) - 1.0f));
}

static float avifToGammaLog100Sqrt10(float linear)
{
    return linear <= 0.00316227766f ? 0.0f : 1.0f + log10f(AVIF_MIN(linear, 1.0f)) / 2.5f;
}

static float avifToLinearIEC61966(float gamma)
{
    if (gamma < -4.5f * 0.018053968510807f) {
        return powf((-gamma + 0.09929682680944f) / -1.09929682680944f, 1.0f / 0.45f);
    } else if (gamma < 4.5f * 0.018053968510807f) {
        return gamma / 4.5f;
    } else {
        return powf((gamma + 0.09929682680944f) / 1.09929682680944f, 1.0f / 0.45f);
    }
}

static float avifToGammaIEC61966(float linear)
{
    if (linear < -0.018053968510807f) {
        return -1.09929682680944f * powf(-linear, 0.45f) + 0.09929682680944f;
    } else if (linear < 0.018053968510807f) {
        return linear * 4.5f;
    } else {
        return 1.09929682680944f * powf(linear, 0.45f) - 0.09929682680944f;
    }
}

static float avifToLinearBT1361(float gamma)
{
    if (gamma < -0.25f) {
        return -0.25f;
    } else if (gamma < 0.0f) {
        return powf((gamma - 0.02482420670236f) / -0.27482420670236f, 1.0f / 0.45f) / -4.0f;
    } else if (gamma < 4.5f * 0.018053968510807f) {
        return gamma / 4.5f;
    } else if (gamma < 1.0f) {
        return powf((gamma + 0.09929682680944f) / 1.09929682680944f, 1.0f / 0.45f);
    } else {
        return 1.0f;
    }
}

static float avifToGammaBT1361(float linear)
{
    if (linear < -0.25f) {
        return -0.25f;
    } else if (linear < 0.0f) {
        return -0.27482420670236f * powf(-4.0f * linear, 0.45f) + 0.02482420670236f;
    } else if (linear < 0.018053968510807f) {
        return linear * 4.5f;
    } else if (linear < 1.0f) {
        return 1.09929682680944f * powf(linear, 0.45f) - 0.09929682680944f;
    } else {
        return 1.0f;
    }
}

static float avifToLinearSRGB(float gamma)
{
    if (gamma < 0.0f) {
        return 0.0f;
    } else if (gamma < 12.92f * 0.0030412825601275209f) {
        return gamma / 12.92f;
    } else if (gamma < 1.0f) {
        return powf((gamma + 0.0550107189475866f) / 1.0550107189475866f, 2.4f);
    } else {
        return 1.0f;
    }
}

static float avifToGammaSRGB(float linear)
{
    if (linear < 0.0f) {
        return 0.0f;
    } else if (linear < 0.0030412825601275209f) {
        return linear * 12.92f;
    } else if (linear < 1.0f) {
        return 1.0550107189475866f * powf(linear, 1.0f / 2.4f) - 0.0550107189475866f;
    } else {
        return 1.0f;
    }
}

#define PQ_MAX_NITS 10000.0f
#define HLG_PEAK_LUMINANCE_NITS 1000.0f
#define SDR_WHITE_NITS 203.0f

static float avifToLinearPQ(float gamma)
{
    if (gamma > 0.0f) {
        const float powGamma = powf(gamma, 1.0f / 78.84375f);
        const float num = AVIF_MAX(powGamma - 0.8359375f, 0.0f);
        const float den = AVIF_MAX(18.8515625f - 18.6875f * powGamma, FLT_MIN);
        const float linear = powf(num / den, 1.0f / 0.1593017578125f);
        // Scale so that SDR white is 1.0 (extended SDR).
        return linear * PQ_MAX_NITS / SDR_WHITE_NITS;
    } else {
        return 0.0f;
    }
}

static float avifToGammaPQ(float linear)
{
    if (linear > 0.0f) {
        // Scale from extended SDR range to [0.0, 1.0].
        linear = AVIF_CLAMP(linear * SDR_WHITE_NITS / PQ_MAX_NITS, 0.0f, 1.0f);
        const float powLinear = powf(linear, 0.1593017578125f);
        const float num = 0.1640625f * powLinear - 0.1640625f;
        const float den = 1.0f + 18.6875f * powLinear;
        return powf(1.0f + num / den, 78.84375f);
    } else {
        return 0.0f;
    }
}

static float avifToLinearSMPTE428(float gamma)
{
    return powf(AVIF_MAX(gamma, 0.0f), 2.6f) / 0.91655527974030934f;
}

static float avifToGammaSMPTE428(float linear)
{
    return powf(0.91655527974030934f * AVIF_MAX(linear, 0.0f), 1.0f / 2.6f);
}

// Formula from ITU-R BT.2100-2
// Assumes Lw=1000 (max display luminance in nits).
// For simplicity, approximates Ys (which should be 0.2627*r+0.6780*g+0.0593*b)
// to the input value (r, g, or b depending on the current channel).
static float avifToLinearHLG(float gamma)
{
    // Inverse OETF followed by the OOTF, see Table 5 in ITU-R BT.2100-2 page 7.
    // Note that this differs slightly from  ITU-T H.273 which doesn't use the OOTF.
    if (gamma < 0.0f) {
        return 0.0f;
    }
    float linear = 0.0f;
    if (gamma <= 0.5f) {
        linear = powf((gamma * gamma) * (1.0f / 3.0f), 1.2f);
    } else {
        linear = powf((expf((gamma - 0.55991073f) / 0.17883277f) + 0.28466892f) / 12.0f, 1.2f);
    }
    // Scale so that SDR white is 1.0 (extended SDR).
    return linear * HLG_PEAK_LUMINANCE_NITS / SDR_WHITE_NITS;
}

static float avifToGammaHLG(float linear)
{
    // Scale from extended SDR range to [0.0, 1.0].
    linear = AVIF_CLAMP(linear * SDR_WHITE_NITS / HLG_PEAK_LUMINANCE_NITS, 0.0f, 1.0f);
    // Inverse OOTF followed by OETF see Table 5 and Note 5i in ITU-R BT.2100-2 page 7-8.
    linear = powf(linear, 1.0f / 1.2f);
    if (linear < 0.0f) {
        return 0.0f;
    } else if (linear <= (1.0f / 12.0f)) {
        return sqrtf(3.0f * linear);
    } else {
        return 0.17883277f * logf(12.0f * linear - 0.28466892f) + 0.55991073f;
    }
}

static const struct avifTransferCharacteristicsTable transferCharacteristicsTables[] = {
    { AVIF_TRANSFER_CHARACTERISTICS_BT709, "BT.709", avifToLinear709, avifToGamma709 },
    { AVIF_TRANSFER_CHARACTERISTICS_BT470M, "BT.470-6 System M", avifToLinear470M, avifToGamma470M },
    { AVIF_TRANSFER_CHARACTERISTICS_BT470BG, "BT.470-6 System BG", avifToLinear470BG, avifToGamma470BG },
    { AVIF_TRANSFER_CHARACTERISTICS_BT601, "BT.601", avifToLinear709, avifToGamma709 },
    { AVIF_TRANSFER_CHARACTERISTICS_SMPTE240, "SMPTE 240M", avifToLinearSMPTE240, avifToGammaSMPTE240 },
    { AVIF_TRANSFER_CHARACTERISTICS_LINEAR, "Linear", avifToGammaLinear, avifToGammaLinear },
    { AVIF_TRANSFER_CHARACTERISTICS_LOG100, "100:1 Log", avifToLinearLog100, avifToGammaLog100 },
    { AVIF_TRANSFER_CHARACTERISTICS_LOG100_SQRT10, "100sqrt(10):1 Log", avifToLinearLog100Sqrt10, avifToGammaLog100Sqrt10 },
    { AVIF_TRANSFER_CHARACTERISTICS_IEC61966, "IEC 61966-2-4", avifToLinearIEC61966, avifToGammaIEC61966 },
    { AVIF_TRANSFER_CHARACTERISTICS_BT1361, "BT.1361", avifToLinearBT1361, avifToGammaBT1361 },
    { AVIF_TRANSFER_CHARACTERISTICS_SRGB, "sRGB", avifToLinearSRGB, avifToGammaSRGB },
    { AVIF_TRANSFER_CHARACTERISTICS_BT2020_10BIT, "10bit BT.2020", avifToLinear709, avifToGamma709 },
    { AVIF_TRANSFER_CHARACTERISTICS_BT2020_12BIT, "12bit BT.2020", avifToLinear709, avifToGamma709 },
    { AVIF_TRANSFER_CHARACTERISTICS_SMPTE2084, "SMPTE ST 2084 (PQ)", avifToLinearPQ, avifToGammaPQ },
    { AVIF_TRANSFER_CHARACTERISTICS_SMPTE428, "SMPTE ST 428-1", avifToLinearSMPTE428, avifToGammaSMPTE428 },
    { AVIF_TRANSFER_CHARACTERISTICS_HLG, "ARIB STD-B67 (HLG)", avifToLinearHLG, avifToGammaHLG }
};

static const int avifTransferCharacteristicsTableSize =
    sizeof(transferCharacteristicsTables) / sizeof(transferCharacteristicsTables[0]);

avifTransferFunction avifTransferCharacteristicsGetGammaToLinearFunction(avifTransferCharacteristics atc)
{
    for (int i = 0; i < avifTransferCharacteristicsTableSize; ++i) {
        const struct avifTransferCharacteristicsTable * const table = &transferCharacteristicsTables[i];
        if (table->transferCharacteristicsEnum == atc) {
            return table->toLinear;
        }
    }
    return avifToLinear709; // Provide a reasonable default.
}

avifTransferFunction avifTransferCharacteristicsGetLinearToGammaFunction(avifTransferCharacteristics atc)
{
    for (int i = 0; i < avifTransferCharacteristicsTableSize; ++i) {
        const struct avifTransferCharacteristicsTable * const table = &transferCharacteristicsTables[i];
        if (table->transferCharacteristicsEnum == atc) {
            return table->toGamma;
        }
    }
    return avifToGamma709; // Provide a reasonable default.
}

void avifColorPrimariesComputeYCoeffs(avifColorPrimaries colorPrimaries, float coeffs[3])
{
    float primaries[8];
    avifColorPrimariesGetValues(colorPrimaries, primaries);
    const float rX = primaries[0];
    const float rY = primaries[1];
    const float gX = primaries[2];
    const float gY = primaries[3];
    const float bX = primaries[4];
    const float bY = primaries[5];
    const float wX = primaries[6];
    const float wY = primaries[7];
    const float rZ = 1.0f - (rX + rY); // (Eq. 34)
    const float gZ = 1.0f - (gX + gY); // (Eq. 35)
    const float bZ = 1.0f - (bX + bY); // (Eq. 36)
    const float wZ = 1.0f - (wX + wY); // (Eq. 37)
    const float kr = (rY * (wX * (gY * bZ - bY * gZ) + wY * (bX * gZ - gX * bZ) + wZ * (gX * bY - bX * gY))) /
                     (wY * (rX * (gY * bZ - bY * gZ) + gX * (bY * rZ - rY * bZ) + bX * (rY * gZ - gY * rZ)));
    // (Eq. 32)
    const float kb = (bY * (wX * (rY * gZ - gY * rZ) + wY * (gX * rZ - rX * gZ) + wZ * (rX * gY - gX * rY))) /
                     (wY * (rX * (gY * bZ - bY * gZ) + gX * (bY * rZ - rY * bZ) + bX * (rY * gZ - gY * rZ)));
    // (Eq. 33)
    coeffs[0] = kr;
    coeffs[2] = kb;
    coeffs[1] = 1.0f - coeffs[0] - coeffs[2];
}

static const double kEpsilon = 1e-12;

static avifBool xyToXYZ(const float xy[2], double XYZ[3])
{
    if (fabsf(xy[1]) < kEpsilon) {
        return AVIF_FALSE;
    }

    const double factor = 1 / xy[1];
    XYZ[0] = xy[0] * factor;
    XYZ[1] = 1;
    XYZ[2] = (1 - xy[0] - xy[1]) * factor;

    return AVIF_TRUE;
}

// Computes I = M^-1. Returns false if M seems to be singular.
static avifBool matInv(const double M[3][3], double I[3][3])
{
    double det = M[0][0] * (M[1][1] * M[2][2] - M[2][1] * M[1][2]) - M[0][1] * (M[1][0] * M[2][2] - M[1][2] * M[2][0]) +
                 M[0][2] * (M[1][0] * M[2][1] - M[1][1] * M[2][0]);
    if (fabs(det) < kEpsilon) {
        return AVIF_FALSE;
    }
    det = 1.0 / det;

    I[0][0] = (M[1][1] * M[2][2] - M[2][1] * M[1][2]) * det;
    I[0][1] = (M[0][2] * M[2][1] - M[0][1] * M[2][2]) * det;
    I[0][2] = (M[0][1] * M[1][2] - M[0][2] * M[1][1]) * det;
    I[1][0] = (M[1][2] * M[2][0] - M[1][0] * M[2][2]) * det;
    I[1][1] = (M[0][0] * M[2][2] - M[0][2] * M[2][0]) * det;
    I[1][2] = (M[1][0] * M[0][2] - M[0][0] * M[1][2]) * det;
    I[2][0] = (M[1][0] * M[2][1] - M[2][0] * M[1][1]) * det;
    I[2][1] = (M[2][0] * M[0][1] - M[0][0] * M[2][1]) * det;
    I[2][2] = (M[0][0] * M[1][1] - M[1][0] * M[0][1]) * det;

    return AVIF_TRUE;
}

// Computes C = A*B
static void matMul(const double A[3][3], const double B[3][3], double C[3][3])
{
    C[0][0] = A[0][0] * B[0][0] + A[0][1] * B[1][0] + A[0][2] * B[2][0];
    C[0][1] = A[0][0] * B[0][1] + A[0][1] * B[1][1] + A[0][2] * B[2][1];
    C[0][2] = A[0][0] * B[0][2] + A[0][1] * B[1][2] + A[0][2] * B[2][2];
    C[1][0] = A[1][0] * B[0][0] + A[1][1] * B[1][0] + A[1][2] * B[2][0];
    C[1][1] = A[1][0] * B[0][1] + A[1][1] * B[1][1] + A[1][2] * B[2][1];
    C[1][2] = A[1][0] * B[0][2] + A[1][1] * B[1][2] + A[1][2] * B[2][2];
    C[2][0] = A[2][0] * B[0][0] + A[2][1] * B[1][0] + A[2][2] * B[2][0];
    C[2][1] = A[2][0] * B[0][1] + A[2][1] * B[1][1] + A[2][2] * B[2][1];
    C[2][2] = A[2][0] * B[0][2] + A[2][1] * B[1][2] + A[2][2] * B[2][2];
}

// Set M to have values of d on the leading diagonal, and zero elsewhere.
static void matDiag(const double d[3], double M[3][3])
{
    M[0][0] = d[0];
    M[0][1] = 0;
    M[0][2] = 0;
    M[1][0] = 0;
    M[1][1] = d[1];
    M[1][2] = 0;
    M[2][0] = 0;
    M[2][1] = 0;
    M[2][2] = d[2];
}

// Computes y = M.x
static void vecMul(const double M[3][3], const double x[3], double y[3])
{
    y[0] = M[0][0] * x[0] + M[0][1] * x[1] + M[0][2] * x[2];
    y[1] = M[1][0] * x[0] + M[1][1] * x[1] + M[1][2] * x[2];
    y[2] = M[2][0] * x[0] + M[2][1] * x[1] + M[2][2] * x[2];
}

// Bradford chromatic adaptation matrix
// from https://www.researchgate.net/publication/253799640_A_uniform_colour_space_based_upon_CIECAM97s
static const double bradford[3][3] = {
    { 0.8951, 0.2664, -0.1614 },
    { -0.7502, 1.7135, 0.0367 },
    { 0.0389, -0.0685, 1.0296 },
};

// LMS values for D50 whitepoint
static const double lmsD50[3] = { 0.996284, 1.02043, 0.818644 };

avifBool avifColorPrimariesComputeRGBToXYZD50Matrix(avifColorPrimaries colorPrimaries, double coeffs[3][3])
{
    float primaries[8];
    avifColorPrimariesGetValues(colorPrimaries, primaries);

    double whitePointXYZ[3];
    AVIF_CHECK(xyToXYZ(&primaries[6], whitePointXYZ));

    double rgbPrimaries[3][3] = {
        { primaries[0], primaries[2], primaries[4] },
        { primaries[1], primaries[3], primaries[5] },
        { 1.0 - primaries[0] - primaries[1], 1.0 - primaries[2] - primaries[3], 1.0 - primaries[4] - primaries[5] }
    };

    double rgbPrimariesInv[3][3];
    AVIF_CHECK(matInv(rgbPrimaries, rgbPrimariesInv));

    double rgbCoefficients[3];
    vecMul(rgbPrimariesInv, whitePointXYZ, rgbCoefficients);

    double rgbCoefficientsMat[3][3];
    matDiag(rgbCoefficients, rgbCoefficientsMat);

    double rgbXYZ[3][3];
    matMul(rgbPrimaries, rgbCoefficientsMat, rgbXYZ);

    // ICC stores primaries XYZ under PCS.
    // Adapt using linear bradford transform
    // from https://onlinelibrary.wiley.com/doi/pdf/10.1002/9781119021780.app3
    double lms[3];
    vecMul(bradford, whitePointXYZ, lms);
    for (int i = 0; i < 3; ++i) {
        if (fabs(lms[i]) < kEpsilon) {
            assert(0);
        }
        lms[i] = lmsD50[i] / lms[i];
    }

    double adaptation[3][3];
    matDiag(lms, adaptation);

    double tmp[3][3];
    matMul(adaptation, bradford, tmp);

    double bradfordInv[3][3];
    if (!matInv(bradford, bradfordInv)) {
        assert(0);
    }
    matMul(bradfordInv, tmp, adaptation);

    matMul(adaptation, rgbXYZ, coeffs);

    return AVIF_TRUE;
}

avifBool avifColorPrimariesComputeXYZD50ToRGBMatrix(avifColorPrimaries colorPrimaries, double coeffs[3][3])
{
    double rgbToXyz[3][3];
    AVIF_CHECK(avifColorPrimariesComputeRGBToXYZD50Matrix(colorPrimaries, rgbToXyz));
    AVIF_CHECK(matInv(rgbToXyz, coeffs));
    return AVIF_TRUE;
}

avifBool avifColorPrimariesComputeRGBToRGBMatrix(avifColorPrimaries srcColorPrimaries,
                                                 avifColorPrimaries dstColorPrimaries,
                                                 double coeffs[3][3])
{
    // Note: no special casing for srcColorPrimaries == dstColorPrimaries to allow
    // testing that the computation actually produces the identity matrix.
    double srcRGBToXYZ[3][3];
    AVIF_CHECK(avifColorPrimariesComputeRGBToXYZD50Matrix(srcColorPrimaries, srcRGBToXYZ));
    double xyzToDstRGB[3][3];
    AVIF_CHECK(avifColorPrimariesComputeXYZD50ToRGBMatrix(dstColorPrimaries, xyzToDstRGB));
    // coeffs = yuvToDstRGB * srcRGBToYUV
    // i.e. srgRGB -> YUV -> dstRGB
    // matMulF(xyzToDstRGB, srcRGBToXYZ, coeffs);
    matMul(xyzToDstRGB, srcRGBToXYZ, coeffs);
    return AVIF_TRUE;
}

// Converts a linear RGBA pixel to a different color space. This function actually works for gamma encoded
// RGB as well but linear gives better results. Also, for gamma encoded values, it would be
// better to clamp the output to [0, 1]. Linear values don't need clamping because values
// > 1.0 are valid for HDR transfer curves, and the gamma compression function will do the
// clamping as necessary.
void avifLinearRGBAConvertColorSpace(float rgba[4], const double coeffs[3][3])
{
    const double rgb[3] = { rgba[0], rgba[1], rgba[2] }; // tmp copy
    rgba[0] = (float)(coeffs[0][0] * rgb[0] + coeffs[0][1] * rgb[1] + coeffs[0][2] * rgb[2]);
    rgba[1] = (float)(coeffs[1][0] * rgb[0] + coeffs[1][1] * rgb[1] + coeffs[1][2] * rgb[2]);
    rgba[2] = (float)(coeffs[2][0] * rgb[0] + coeffs[2][1] * rgb[1] + coeffs[2][2] * rgb[2]);
}
