// Copyright 2019 Joe Drago. All rights reserved.
// SPDX-License-Identifier: BSD-2-Clause

#include "avif/internal.h"

#include "gb_math.h"

#include <string.h>

struct avifColourPrimariesTable
{
    avifNclxColourPrimaries colourPrimariesEnum;
    const char * name;
    float primaries[8]; // rX, rY, gX, gY, bX, bY, wX, wY
};
static const struct avifColourPrimariesTable table[] = {
    { AVIF_NCLX_COLOUR_PRIMARIES_BT709, "BT.709", { 0.64f, 0.33f, 0.3f, 0.6f, 0.15f, 0.06f, 0.3127f, 0.329f } },
    { AVIF_NCLX_COLOUR_PRIMARIES_BT470_6M, "BT470-6 System M", { 0.67f, 0.33f, 0.21f, 0.71f, 0.14f, 0.08f, 0.310f, 0.316f } },
    { AVIF_NCLX_COLOUR_PRIMARIES_BT601_7_625, "BT.601-7 625", { 0.64f, 0.33f, 0.29f, 0.60f, 0.15f, 0.06f, 0.3127f, 0.3290f } },
    { AVIF_NCLX_COLOUR_PRIMARIES_BT601_7_525, "BT.601-7 525", { 0.630f, 0.340f, 0.310f, 0.595f, 0.155f, 0.070f, 0.3127f, 0.3290f } },
    { AVIF_NCLX_COLOUR_PRIMARIES_ST240, "ST 240", { 0.630f, 0.340f, 0.310f, 0.595f, 0.155f, 0.070f, 0.3127f, 0.3290f } },
    { AVIF_NCLX_COLOUR_PRIMARIES_GENERIC_FILM, "Generic film", { 0.681f, 0.319f, 0.243f, 0.692f, 0.145f, 0.049f, 0.310f, 0.316f } },
    { AVIF_NCLX_COLOUR_PRIMARIES_BT2020, "BT.2020", { 0.708f, 0.292f, 0.170f, 0.797f, 0.131f, 0.046f, 0.3127f, 0.3290f } },
    { AVIF_NCLX_COLOUR_PRIMARIES_XYZ, "XYZ", { 1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.3333f, 0.3333f } },
    { AVIF_NCLX_COLOUR_PRIMARIES_RP431_2, "RP 431-2", { 0.680f, 0.320f, 0.265f, 0.690f, 0.150f, 0.060f, 0.314f, 0.351f } },
    { AVIF_NCLX_COLOUR_PRIMARIES_EG432_1, "EG 432-1 (P3)", { 0.680f, 0.320f, 0.265f, 0.690f, 0.150f, 0.060f, 0.3127f, 0.3290f } },
    { AVIF_NCLX_COLOUR_PRIMARIES_EBU3213E, "EBU 3213-E", { 0.630f, 0.340f, 0.295f, 0.605f, 0.155f, 0.077f, 0.3127f, 0.3290f } }
};
static const int tableSize = sizeof(table) / sizeof(table[0]);

void avifNclxColourPrimariesGetValues(avifNclxColourPrimaries ancp, float outPrimaries[8])
{
    for (int i = 0; i < tableSize; ++i) {
        if (table[i].colourPrimariesEnum == ancp) {
            memcpy(outPrimaries, table[i].primaries, sizeof(table[i].primaries));
            return;
        }
    }

    // if we get here, the color primaries are unknown. Just return a reasonable default.
    memcpy(outPrimaries, table[0].primaries, sizeof(table[0].primaries));
}

static avifBool matchesTo3RoundedPlaces(float a, float b)
{
    return (fabsf(a - b) < 0.001f) ? AVIF_TRUE : AVIF_FALSE;
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

    for (int i = 0; i < tableSize; ++i) {
        if (primariesMatch(inPrimaries, table[i].primaries)) {
            if (outName) {
                *outName = table[i].name;
            }
            return table[i].colourPrimariesEnum;
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
    return XYZ.y;
}

static avifBool readXYZ(uint8_t * data, size_t size, float xyz[3])
{
    avifRawData xyzData;
    xyzData.data = data;
    xyzData.size = size;
    avifStream s;
    avifStreamStart(&s, &xyzData);
    CHECK(avifStreamSkip(&s, 8));

    int32_t fixedXYZ[3];
    CHECK(avifStreamReadU32(&s, (uint32_t *)&fixedXYZ[0]));
    CHECK(avifStreamReadU32(&s, (uint32_t *)&fixedXYZ[1]));
    CHECK(avifStreamReadU32(&s, (uint32_t *)&fixedXYZ[2]));

    xyz[0] = fixedToFloat(fixedXYZ[0]);
    xyz[1] = fixedToFloat(fixedXYZ[1]);
    xyz[2] = fixedToFloat(fixedXYZ[2]);
    return AVIF_TRUE;
}

static avifBool readMat3(uint8_t * data, size_t size, gbMat3 * m)
{
    avifRawData xyzData;
    xyzData.data = data;
    xyzData.size = size;
    avifStream s;
    avifStreamStart(&s, &xyzData);
    CHECK(avifStreamSkip(&s, 8));

    for (int i = 0; i < 9; ++i) {
        int32_t fixedXYZ;
        CHECK(avifStreamReadU32(&s, (uint32_t *)&fixedXYZ));
        m->e[i] = fixedToFloat(fixedXYZ);
    }
    return AVIF_TRUE;
}

static avifBool calcYUVInfoFromICC(avifRawData * icc, float coeffs[3])
{
    avifStream s;

    uint8_t iccMajorVersion;
    avifStreamStart(&s, icc);
    CHECK(avifStreamSkip(&s, 8)); // skip to version
    CHECK(avifStreamRead(&s, &iccMajorVersion, 1));

    avifStreamStart(&s, icc);       // start stream over
    CHECK(avifStreamSkip(&s, 128)); // skip past the ICC header

    uint32_t tagCount;
    CHECK(avifStreamReadU32(&s, &tagCount));

    avifBool rXYZPresent = AVIF_FALSE;
    avifBool gXYZPresent = AVIF_FALSE;
    avifBool bXYZPresent = AVIF_FALSE;
    avifBool wtptPresent = AVIF_FALSE;
    avifBool chadPresent = AVIF_FALSE;
    gbMat3 colorants;
    gbMat3 chad, invChad;
    gbVec3 wtpt;

    for (uint32_t tagIndex = 0; tagIndex < tagCount; ++tagIndex) {
        uint8_t tagSignature[4];
        uint32_t tagOffset;
        uint32_t tagSize;
        CHECK(avifStreamRead(&s, tagSignature, 4));
        CHECK(avifStreamReadU32(&s, &tagOffset));
        CHECK(avifStreamReadU32(&s, &tagSize));
        if ((tagOffset + tagSize) > icc->size) {
            return AVIF_FALSE;
        }
        if (!memcmp(tagSignature, "rXYZ", 4)) {
            CHECK(readXYZ(icc->data + tagOffset, tagSize, &colorants.e[0]));
            rXYZPresent = AVIF_TRUE;
        } else if (!memcmp(tagSignature, "gXYZ", 4)) {
            CHECK(readXYZ(icc->data + tagOffset, tagSize, &colorants.e[3]));
            gXYZPresent = AVIF_TRUE;
        } else if (!memcmp(tagSignature, "bXYZ", 4)) {
            CHECK(readXYZ(icc->data + tagOffset, tagSize, &colorants.e[6]));
            bXYZPresent = AVIF_TRUE;
        } else if (!memcmp(tagSignature, "wtpt", 4)) {
            CHECK(readXYZ(icc->data + tagOffset, tagSize, &wtpt.e[0]));
            wtptPresent = AVIF_TRUE;
        } else if (!memcmp(tagSignature, "chad", 4)) {
            CHECK(readMat3(icc->data + tagOffset, tagSize, &chad));
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

// From http://docs-hoffmann.de/ciexyz29082000.pdf, Section 11.4
static void deriveXYZMatrix(gbMat3 * colorants, float primaries[8])
{
    gbVec3 U, W;
    gbMat3 P, PInv, D;

    P.col[0].x = primaries[0];
    P.col[0].y = primaries[1];
    P.col[0].z = 1 - primaries[0] - primaries[1];
    P.col[1].x = primaries[2];
    P.col[1].y = primaries[3];
    P.col[1].z = 1 - primaries[2] - primaries[3];
    P.col[2].x = primaries[4];
    P.col[2].y = primaries[5];
    P.col[2].z = 1 - primaries[4] - primaries[5];

    gb_mat3_inverse(&PInv, &P);

    W.x = primaries[6];
    W.y = primaries[7];
    W.z = 1 - primaries[6] - primaries[7];

    gb_mat3_mul_vec3(&U, &PInv, W);

    memset(&D, 0, sizeof(D));
    D.col[0].x = U.x / W.y;
    D.col[1].y = U.y / W.y;
    D.col[2].z = U.z / W.y;

    gb_mat3_mul(colorants, &P, &D);
    gb_mat3_transpose(colorants);
}

static avifBool calcYUVInfoFromNCLX(avifNclxColorProfile * nclx, float coeffs[3])
{
    float primaries[8];
    avifNclxColourPrimariesGetValues(nclx->colourPrimaries, primaries);

    gbMat3 colorants;
    deriveXYZMatrix(&colorants, primaries);

    // YUV coefficients are simply the brightest Y that a primary can be (where the white point's Y is 1.0)
    coeffs[0] = calcMaxY(1.0f, 0.0f, 0.0f, &colorants);
    coeffs[2] = calcMaxY(0.0f, 0.0f, 1.0f, &colorants);
    coeffs[1] = 1.0f - coeffs[0] - coeffs[2];
    return AVIF_TRUE;
}

void avifCalcYUVCoefficients(avifImage * image, float * outR, float * outG, float * outB)
{
    // sRGB (BT.709) defaults
    float kr = 0.2126f;
    float kb = 0.0722f;
    float kg = 1.0f - kr - kb;

    float coeffs[3];
    if ((image->profileFormat == AVIF_PROFILE_FORMAT_ICC) && image->icc.data && image->icc.size) {
        if (calcYUVInfoFromICC(&image->icc, coeffs)) {
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
