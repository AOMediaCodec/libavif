// Copyright 2019 Joe Drago. All rights reserved.
// SPDX-License-Identifier: BSD-2-Clause

#include "avif/internal.h"

#include <math.h>
#include <string.h>

// ------------------------------------------------------------------------------------------------
// Adapted from gb_math:
//
// gb_math.h - v0.07c - public domain C math library - no warranty implied; use at your own risk

typedef float gbFloat3[3];

typedef union gbVec3
{
    struct
    {
        float x, y, z;
    } xyz;
    float e[3];
} gbVec3;

typedef union gbMat3
{
    gbVec3 col[3];
    float e[9];
} gbMat3;

static gbFloat3 * gb_float33_m(gbMat3 * m)
{
    return (gbFloat3 *)m;
}

static void gb_float33_mul_vec3(gbVec3 * out, float m[3][3], gbVec3 v)
{
    out->xyz.x = m[0][0] * v.xyz.x + m[0][1] * v.xyz.y + m[0][2] * v.xyz.z;
    out->xyz.y = m[1][0] * v.xyz.x + m[1][1] * v.xyz.y + m[1][2] * v.xyz.z;
    out->xyz.z = m[2][0] * v.xyz.x + m[2][1] * v.xyz.y + m[2][2] * v.xyz.z;
}

static void gb_mat3_mul_vec3(gbVec3 * out, gbMat3 * m, gbVec3 in)
{
    gb_float33_mul_vec3(out, gb_float33_m(m), in);
}

static void gb_float33_transpose(float (*vec)[3])
{
    int i, j;
    for (j = 0; j < 3; j++) {
        for (i = j + 1; i < 3; i++) {
            float t = vec[i][j];
            vec[i][j] = vec[j][i];
            vec[j][i] = t;
        }
    }
}

static void gb_mat3_transpose(gbMat3 * m)
{
    gb_float33_transpose(gb_float33_m(m));
}

static float gb_mat3_determinant(gbMat3 * m)
{
    gbFloat3 * e = gb_float33_m(m);
    float d = +e[0][0] * (e[1][1] * e[2][2] - e[1][2] * e[2][1]) - e[0][1] * (e[1][0] * e[2][2] - e[1][2] * e[2][0]) +
              e[0][2] * (e[1][0] * e[2][1] - e[1][1] * e[2][0]);
    return d;
}

static void gb_float33_mul(float (*out)[3], float (*mat1)[3], float (*mat2)[3])
{
    int i, j;
    float temp1[3][3], temp2[3][3];
    if (mat1 == out) {
        memcpy(temp1, mat1, sizeof(temp1));
        mat1 = temp1;
    }
    if (mat2 == out) {
        memcpy(temp2, mat2, sizeof(temp2));
        mat2 = temp2;
    }
    for (j = 0; j < 3; j++) {
        for (i = 0; i < 3; i++) {
            out[j][i] = mat1[0][i] * mat2[j][0] + mat1[1][i] * mat2[j][1] + mat1[2][i] * mat2[j][2];
        }
    }
}

static void gb_mat3_mul(gbMat3 * out, gbMat3 * m1, gbMat3 * m2)
{
    gb_float33_mul(gb_float33_m(out), gb_float33_m(m1), gb_float33_m(m2));
}

static void gb_mat3_inverse(gbMat3 * out, gbMat3 * in)
{
    gbFloat3 * o = gb_float33_m(out);
    gbFloat3 * i = gb_float33_m(in);

    float ood = 1.0f / gb_mat3_determinant(in);

    o[0][0] = +(i[1][1] * i[2][2] - i[2][1] * i[1][2]) * ood;
    o[0][1] = -(i[1][0] * i[2][2] - i[2][0] * i[1][2]) * ood;
    o[0][2] = +(i[1][0] * i[2][1] - i[2][0] * i[1][1]) * ood;
    o[1][0] = -(i[0][1] * i[2][2] - i[2][1] * i[0][2]) * ood;
    o[1][1] = +(i[0][0] * i[2][2] - i[2][0] * i[0][2]) * ood;
    o[1][2] = -(i[0][0] * i[2][1] - i[2][0] * i[0][1]) * ood;
    o[2][0] = +(i[0][1] * i[1][2] - i[1][1] * i[0][2]) * ood;
    o[2][1] = -(i[0][0] * i[1][2] - i[1][0] * i[0][2]) * ood;
    o[2][2] = +(i[0][0] * i[1][1] - i[1][0] * i[0][1]) * ood;
}

// ------------------------------------------------------------------------------------------------

struct avifColourPrimariesTable
{
    avifNclxColourPrimaries colourPrimariesEnum;
    const char * name;
    float primaries[8]; // rX, rY, gX, gY, bX, bY, wX, wY
};
static const struct avifColourPrimariesTable avifColourPrimariesTables[] = {
    { AVIF_NCLX_COLOUR_PRIMARIES_BT709, "BT.709", { 0.64f, 0.33f, 0.3f, 0.6f, 0.15f, 0.06f, 0.3127f, 0.329f } },
    { AVIF_NCLX_COLOUR_PRIMARIES_BT470M, "BT.470-6 System M", { 0.67f, 0.33f, 0.21f, 0.71f, 0.14f, 0.08f, 0.310f, 0.316f } },
    { AVIF_NCLX_COLOUR_PRIMARIES_BT470BG, "BT.470-6 System BG", { 0.64f, 0.33f, 0.29f, 0.60f, 0.15f, 0.06f, 0.3127f, 0.3290f } },
    { AVIF_NCLX_COLOUR_PRIMARIES_BT601, "BT.601", { 0.630f, 0.340f, 0.310f, 0.595f, 0.155f, 0.070f, 0.3127f, 0.3290f } },
    { AVIF_NCLX_COLOUR_PRIMARIES_SMPTE240, "SMPTE 240M", { 0.630f, 0.340f, 0.310f, 0.595f, 0.155f, 0.070f, 0.3127f, 0.3290f } },
    { AVIF_NCLX_COLOUR_PRIMARIES_GENERIC_FILM, "Generic film", { 0.681f, 0.319f, 0.243f, 0.692f, 0.145f, 0.049f, 0.310f, 0.316f } },
    { AVIF_NCLX_COLOUR_PRIMARIES_BT2020, "BT.2020", { 0.708f, 0.292f, 0.170f, 0.797f, 0.131f, 0.046f, 0.3127f, 0.3290f } },
    { AVIF_NCLX_COLOUR_PRIMARIES_XYZ, "XYZ", { 1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.3333f, 0.3333f } },
    { AVIF_NCLX_COLOUR_PRIMARIES_SMPTE431, "SMPTE RP 431-2", { 0.680f, 0.320f, 0.265f, 0.690f, 0.150f, 0.060f, 0.314f, 0.351f } },
    { AVIF_NCLX_COLOUR_PRIMARIES_SMPTE432, "SMPTE EG 432-1 (DCI P3)", { 0.680f, 0.320f, 0.265f, 0.690f, 0.150f, 0.060f, 0.3127f, 0.3290f } },
    { AVIF_NCLX_COLOUR_PRIMARIES_EBU3213, "EBU Tech. 3213-E", { 0.630f, 0.340f, 0.295f, 0.605f, 0.155f, 0.077f, 0.3127f, 0.3290f } }
};
static const int avifColourPrimariesTableSize = sizeof(avifColourPrimariesTables) / sizeof(avifColourPrimariesTables[0]);

void avifNclxColourPrimariesGetValues(avifNclxColourPrimaries ancp, float outPrimaries[8])
{
    for (int i = 0; i < avifColourPrimariesTableSize; ++i) {
        if (avifColourPrimariesTables[i].colourPrimariesEnum == ancp) {
            memcpy(outPrimaries, avifColourPrimariesTables[i].primaries, sizeof(avifColourPrimariesTables[i].primaries));
            return;
        }
    }

    // if we get here, the color primaries are unknown. Just return a reasonable default.
    memcpy(outPrimaries, avifColourPrimariesTables[0].primaries, sizeof(avifColourPrimariesTables[0].primaries));
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

avifNclxColourPrimaries avifNclxColourPrimariesFind(float inPrimaries[8], const char ** outName)
{
    if (outName) {
        *outName = NULL;
    }

    for (int i = 0; i < avifColourPrimariesTableSize; ++i) {
        if (primariesMatch(inPrimaries, avifColourPrimariesTables[i].primaries)) {
            if (outName) {
                *outName = avifColourPrimariesTables[i].name;
            }
            return avifColourPrimariesTables[i].colourPrimariesEnum;
        }
    }
    return AVIF_NCLX_COLOUR_PRIMARIES_UNKNOWN;
}

static float fixedToFloat(int32_t fixed)
{
    float sign = 1.0f;
    if (fixed < 0) {
        sign = -1.0f;
        fixed *= -1;
    }
    return sign * ((float)((fixed >> 16) & 0xffff) + ((float)(fixed & 0xffff) / 65536.0f));
}

#if 0
static void convertXYZToXYY(float XYZ[3], float xyY[3], float whitePointX, float whitePointY)
{
    float sum = XYZ[0] + XYZ[1] + XYZ[2];
    if (sum <= 0.0f) {
        xyY[0] = whitePointX;
        xyY[1] = whitePointY;
        xyY[2] = 0.0f;
        return;
    }
    xyY[0] = XYZ[0] / sum;
    xyY[1] = XYZ[1] / sum;
    xyY[2] = XYZ[1];
}

static void convertXYYToXYZ(float * xyY, float * XYZ)
{
    if (xyY[2] <= 0.0f) {
        XYZ[0] = 0.0f;
        XYZ[1] = 0.0f;
        XYZ[2] = 0.0f;
        return;
    }
    XYZ[0] = (xyY[0] * xyY[2]) / xyY[1];
    XYZ[1] = xyY[2];
    XYZ[2] = ((1 - xyY[0] - xyY[1]) * xyY[2]) / xyY[1];
}

static void convertMaxXYToXYZ(float x, float y, float * XYZ)
{
    float xyY[3];
    xyY[0] = x;
    xyY[1] = y;
    xyY[2] = 1.0f;
    convertXYYToXYZ(xyY, XYZ);
}

static void convertXYZToXY(float XYZ[3], float xy[2], float whitePointX, float whitePointY)
{
    float xyY[3];
    convertXYZToXYY(XYZ, xyY, whitePointX, whitePointY);
    xy[0] = xyY[0];
    xy[1] = xyY[1];
}
#endif /* if 0 */

static float calcMaxY(float r, float g, float b, gbMat3 * colorants)
{
    gbVec3 rgb, XYZ;
    rgb.e[0] = r;
    rgb.e[1] = g;
    rgb.e[2] = b;
    gb_mat3_mul_vec3(&XYZ, colorants, rgb);
    return XYZ.xyz.y;
}

static avifBool readXYZ(const uint8_t * data, size_t size, float xyz[3])
{
    avifROData xyzData;
    xyzData.data = data;
    xyzData.size = size;
    avifROStream s;
    avifROStreamStart(&s, &xyzData);
    CHECK(avifROStreamSkip(&s, 8));

    int32_t fixedXYZ[3];
    CHECK(avifROStreamReadU32(&s, (uint32_t *)&fixedXYZ[0]));
    CHECK(avifROStreamReadU32(&s, (uint32_t *)&fixedXYZ[1]));
    CHECK(avifROStreamReadU32(&s, (uint32_t *)&fixedXYZ[2]));

    xyz[0] = fixedToFloat(fixedXYZ[0]);
    xyz[1] = fixedToFloat(fixedXYZ[1]);
    xyz[2] = fixedToFloat(fixedXYZ[2]);
    return AVIF_TRUE;
}

static avifBool readMat3(const uint8_t * data, size_t size, gbMat3 * m)
{
    avifROData xyzData;
    xyzData.data = data;
    xyzData.size = size;
    avifROStream s;
    avifROStreamStart(&s, &xyzData);
    CHECK(avifROStreamSkip(&s, 8));

    for (int i = 0; i < 9; ++i) {
        int32_t fixedXYZ;
        CHECK(avifROStreamReadU32(&s, (uint32_t *)&fixedXYZ));
        m->e[i] = fixedToFloat(fixedXYZ);
    }
    return AVIF_TRUE;
}

static avifBool calcYUVInfoFromICC(const uint8_t * iccData, size_t iccSize, float coeffs[3])
{
    avifROData icc;
    icc.data = iccData;
    icc.size = iccSize;

    avifROStream s;
    avifROStreamStart(&s, &icc);

    uint8_t iccMajorVersion;
    CHECK(avifROStreamSkip(&s, 8)); // skip to version
    CHECK(avifROStreamRead(&s, &iccMajorVersion, 1));

    avifROStreamStart(&s, &icc);      // start stream over
    CHECK(avifROStreamSkip(&s, 128)); // skip past the ICC header

    uint32_t tagCount;
    CHECK(avifROStreamReadU32(&s, &tagCount));

    avifBool rXYZPresent = AVIF_FALSE;
    avifBool gXYZPresent = AVIF_FALSE;
    avifBool bXYZPresent = AVIF_FALSE;
    avifBool wtptPresent = AVIF_FALSE;
    avifBool chadPresent = AVIF_FALSE;
    gbMat3 colorants;
    gbMat3 chad, invChad;
    gbVec3 wtpt;
    for (int i = 0; i < 9; ++i) {
        colorants.e[i] = 0.0f;
        chad.e[i] = 0.0f;
    }
    wtpt.e[0] = 0.0f;
    wtpt.e[1] = 0.0f;
    wtpt.e[2] = 0.0f;

    for (uint32_t tagIndex = 0; tagIndex < tagCount; ++tagIndex) {
        uint8_t tagSignature[4];
        uint32_t tagOffset;
        uint32_t tagSize;
        CHECK(avifROStreamRead(&s, tagSignature, 4));
        CHECK(avifROStreamReadU32(&s, &tagOffset));
        CHECK(avifROStreamReadU32(&s, &tagSize));
        if ((tagOffset + tagSize) > icc.size) {
            return AVIF_FALSE;
        }
        if (!memcmp(tagSignature, "rXYZ", 4)) {
            CHECK(readXYZ(icc.data + tagOffset, tagSize, &colorants.e[0]));
            rXYZPresent = AVIF_TRUE;
        } else if (!memcmp(tagSignature, "gXYZ", 4)) {
            CHECK(readXYZ(icc.data + tagOffset, tagSize, &colorants.e[3]));
            gXYZPresent = AVIF_TRUE;
        } else if (!memcmp(tagSignature, "bXYZ", 4)) {
            CHECK(readXYZ(icc.data + tagOffset, tagSize, &colorants.e[6]));
            bXYZPresent = AVIF_TRUE;
        } else if (!memcmp(tagSignature, "wtpt", 4)) {
            CHECK(readXYZ(icc.data + tagOffset, tagSize, &wtpt.e[0]));
            wtptPresent = AVIF_TRUE;
        } else if (!memcmp(tagSignature, "chad", 4)) {
            CHECK(readMat3(icc.data + tagOffset, tagSize, &chad));
            chadPresent = AVIF_TRUE;
        }
    }

    if (!rXYZPresent || !gXYZPresent || !bXYZPresent || !wtptPresent) {
        return AVIF_FALSE;
    }

    // These are read in column order, transpose to fix
    gb_mat3_transpose(&colorants);
    gb_mat3_transpose(&chad);

    gb_mat3_inverse(&invChad, &chad);

    if (chadPresent) {
        // TODO: make sure ICC profiles with no chad still behave?

        gbMat3 tmpColorants;
        memcpy(&tmpColorants, &colorants, sizeof(tmpColorants));
        gb_mat3_mul(&colorants, &tmpColorants, &invChad);

        // TODO: make sure older versions work well?
        if (iccMajorVersion >= 4) {
            gbVec3 tmp;
            memcpy(&tmp, &wtpt, sizeof(tmp));
            gb_mat3_mul_vec3(&wtpt, &invChad, tmp);
        }
    }

    // white point and color primaries harvesting (unnecessary for YUV coefficients)
#if 0
    float whitePoint[2];
    convertXYZToXY(&wtpt.e[0], &whitePoint, 0.0f, 0.0f);

    float primaries[6];
    {
        // transpose to get sets of 3-tuples for R, G, B
        gb_mat3_transpose(&colorants);

        convertXYZToXY(&colorants.e[0], &primaries[0], whitePoint[0], whitePoint[1]);
        convertXYZToXY(&colorants.e[3], &primaries[2], whitePoint[0], whitePoint[1]);
        convertXYZToXY(&colorants.e[6], &primaries[4], whitePoint[0], whitePoint[1]);

        // put it back
        gb_mat3_transpose(&colorants);
    }
#endif

    // YUV coefficients are simply the brightest Y that a primary can be (where the white point's Y is 1.0)
    coeffs[0] = calcMaxY(1.0f, 0.0f, 0.0f, &colorants);
    coeffs[2] = calcMaxY(0.0f, 0.0f, 1.0f, &colorants);
    coeffs[1] = 1.0f - coeffs[0] - coeffs[2];
    return AVIF_TRUE;
}

struct avifMatrixCoefficientsTable
{
    avifNclxMatrixCoefficients matrixCoefficientsEnum;
    const char * name;
    const float kr;
    const float kb;
};

// https://www.itu.int/rec/T-REC-H.273-201612-I/en
static const struct avifMatrixCoefficientsTable matrixCoefficientsTables[] = {
    //{ AVIF_NCLX_MATRIX_COEFFICIENTS_IDENTITY, "Identity", 0.0f, 0.0f, }, // Handled elsewhere
    { AVIF_NCLX_MATRIX_COEFFICIENTS_BT709, "BT.709", 0.2126f, 0.0722f },
    { AVIF_NCLX_MATRIX_COEFFICIENTS_FCC, "FCC USFC 73.682", 0.30f, 0.11f },
    { AVIF_NCLX_MATRIX_COEFFICIENTS_BT470BG, "BT.470-6 System BG", 0.299f, 0.114f },
    { AVIF_NCLX_MATRIX_COEFFICIENTS_BT601, "BT.601", 0.299f, 0.144f },
    { AVIF_NCLX_MATRIX_COEFFICIENTS_SMPTE240, "SMPTE ST 240", 0.212f, 0.087f },
    { AVIF_NCLX_MATRIX_COEFFICIENTS_BT2020_NCL, "BT.2020 (non-constant luminance)", 0.2627f, 0.0593f },
    //{ AVIF_NCLX_MATRIX_COEFFICIENTS_BT2020_CL, "BT.2020 (constant luminance)", 0.2627f, 0.0593f }, // FIXME: It is not an linear transformation.
    //{ AVIF_NCLX_MATRIX_COEFFICIENTS_ST2085, "ST 2085", 0.0f, 0.0f }, // FIXME: ST2085 can't represent using Kr and Kb.
    //{ AVIF_NCLX_MATRIX_COEFFICIENTS_CHROMA_DERIVED_CL, "Chromaticity-derived constant luminance system", 0.0f, 0.0f } // FIXME: It is not an linear transformation.
    //{ AVIF_NCLX_MATRIX_COEFFICIENTS_ICTCP, "BT.2100-0 ICtCp", 0.0f, 0.0f }, // FIXME: This can't represent using Kr and Kb.
};

static const int avifMatrixCoefficientsTableSize = sizeof(matrixCoefficientsTables) / sizeof(matrixCoefficientsTables[0]);

static avifBool calcYUVInfoFromNCLX(avifNclxColorProfile * nclx, float coeffs[3])
{
    if (nclx->matrixCoefficients == AVIF_NCLX_MATRIX_COEFFICIENTS_CHROMA_DERIVED_NCL) {
        float primaries[8];
        avifNclxColourPrimariesGetValues(nclx->colourPrimaries, primaries);
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
            if (table->matrixCoefficientsEnum == nclx->matrixCoefficients) {
                coeffs[0] = table->kr;
                coeffs[2] = table->kb;
                coeffs[1] = 1.0f - coeffs[0] - coeffs[2];
                return AVIF_TRUE;
            }
        }
    }
    return AVIF_FALSE;
}

void avifCalcYUVCoefficients(avifImage * image, float * outR, float * outG, float * outB)
{
    // sRGB (BT.709) defaults
    float kr = 0.2126f;
    float kb = 0.0722f;
    float kg = 1.0f - kr - kb;

    float coeffs[3];
    if ((image->profileFormat == AVIF_PROFILE_FORMAT_ICC) && image->icc.data && image->icc.size) {
        if (calcYUVInfoFromICC(image->icc.data, image->icc.size, coeffs)) {
            kr = coeffs[0];
            kg = coeffs[1];
            kb = coeffs[2];
        }
    } else if (image->profileFormat == AVIF_PROFILE_FORMAT_NCLX) {
        if (calcYUVInfoFromNCLX(&image->nclx, coeffs)) {
            kr = coeffs[0];
            kg = coeffs[1];
            kb = coeffs[2];
        }
    }

    *outR = kr;
    *outG = kg;
    *outB = kb;
}
