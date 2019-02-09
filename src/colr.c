// Copyright 2019 Joe Drago. All rights reserved.
// SPDX-License-Identifier: BSD-2-Clause

#include "avif/internal.h"

#include "gb_math.h"

#include <string.h>

static float fixedToFloat(int32_t fixed)
{
    float sign =  1.0f;
    if (fixed < 0) {
        sign = -1.0f;
        fixed *= -1;
    }
    return sign * ((float)((fixed >> 16) & 0xffff) + ((float)(fixed & 0xffff) / 65536.0f));
}

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

static float calcMaxY(float r, float g, float b, gbMat3 * colorants)
{
    gbVec3 rgb, XYZ;
    rgb.e[0] = r;
    rgb.e[1] = g;
    rgb.e[2] = b;
    gb_mat3_mul_vec3(&XYZ, colorants, rgb);
    float xyY[3];
    convertXYZToXYY(&XYZ.e[0], xyY, 0.0f, 0.0f);
    return xyY[2];
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

    float xyY[3];
    convertXYZToXYY(xyz, xyY, 0.0f, 0.0f);
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

void avifCalcYUVCoefficients(avifImage * image, float * outR, float * outG, float * outB)
{
    // sRGB (BT.709) defaults
    float kr = 0.2126f;
    float kb = 0.0722f;
    float kg = 1.0f - kr - kb;

    if ((image->profileFormat == AVIF_PROFILE_FORMAT_ICC) && image->icc.data && image->icc.size) {
        float coeffs[3];
        if (calcYUVInfoFromICC(&image->icc, coeffs)) {
            kr = coeffs[0];
            kg = coeffs[1];
            kb = coeffs[2];
        }
    }

    *outR = kr;
    *outG = kg;
    *outB = kb;
}
