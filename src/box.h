// Copyright 2019 Joe Drago. All rights reserved.
// SPDX-License-Identifier: BSD-2-Clause

#ifndef SRC_BOX_H_
#define SRC_BOX_H_

#include "avif/internal.h"

#include <assert.h>
#include <inttypes.h>
#include <limits.h>
#include <string.h>

#define AUXTYPE_SIZE 64
#define CONTENTTYPE_SIZE 64

// ---------------------------------------------------------------------------
// Box data structures

// ftyp
typedef struct avifFileType
{
    uint8_t majorBrand[4];
    uint32_t minorVersion;
    // If not null, points to a memory block of 4 * compatibleBrandsCount bytes.
    const uint8_t * compatibleBrands;
    int compatibleBrandsCount;
} avifFileType;

// ispe
typedef struct avifImageSpatialExtents
{
    uint32_t width;
    uint32_t height;
} avifImageSpatialExtents;

// auxC
typedef struct avifAuxiliaryType
{
    char auxType[AUXTYPE_SIZE];
} avifAuxiliaryType;

// infe mime content_type
typedef struct avifContentType
{
    char contentType[CONTENTTYPE_SIZE];
} avifContentType;

// colr
typedef struct avifColourInformationBox
{
    avifBool hasICC;
    const uint8_t * icc;
    size_t iccSize;

    avifBool hasNCLX;
    avifColorPrimaries colorPrimaries;
    avifTransferCharacteristics transferCharacteristics;
    avifMatrixCoefficients matrixCoefficients;
    avifRange range;
} avifColourInformationBox;

#define MAX_PIXI_PLANE_DEPTHS 4
typedef struct avifPixelInformationProperty
{
    uint8_t planeDepths[MAX_PIXI_PLANE_DEPTHS];
    uint8_t planeCount;
} avifPixelInformationProperty;

typedef struct avifOperatingPointSelectorProperty
{
    uint8_t opIndex;
} avifOperatingPointSelectorProperty;

typedef struct avifLayerSelectorProperty
{
    uint16_t layerID;
} avifLayerSelectorProperty;

typedef struct avifAV1LayeredImageIndexingProperty
{
    uint32_t layerSize[3];
} avifAV1LayeredImageIndexingProperty;

// ---------------------------------------------------------------------------
// Top-level structures

// Temporary storage for ipco/stsd contents until they can be associated and memcpy'd to an avifDecoderItem
typedef struct avifProperty
{
    uint8_t type[4];
    union
    {
        avifImageSpatialExtents ispe;
        avifAuxiliaryType auxC;
        avifColourInformationBox colr;
        avifCodecConfigurationBox av1C;
        avifPixelAspectRatioBox pasp;
        avifCleanApertureBox clap;
        avifImageRotation irot;
        avifImageMirror imir;
        avifPixelInformationProperty pixi;
        avifOperatingPointSelectorProperty a1op;
        avifLayerSelectorProperty lsel;
        avifAV1LayeredImageIndexingProperty a1lx;
    } u;
} avifProperty;
AVIF_ARRAY_DECLARE(avifPropertyArray, avifProperty, prop);

const avifProperty * avifPropertyArrayFind(const avifPropertyArray * properties, const char * type);

// ---------------------------------------------------------------------------
// Helper macros / functions

#define BEGIN_STREAM(VARNAME, PTR, SIZE, DIAG, CONTEXT) \
    avifROStream VARNAME;                               \
    avifROData VARNAME##_roData;                        \
    VARNAME##_roData.data = PTR;                        \
    VARNAME##_roData.size = SIZE;                       \
    avifROStreamStart(&VARNAME, &VARNAME##_roData, DIAG, CONTEXT)

// ---------------------------------------------------------------------------
// URN

avifBool isAlphaURN(const char * urn);

// ---------------------------------------------------------------------------

avifBool avifFileTypeHasBrand(avifFileType * ftyp, const char * brand);
avifBool avifFileTypeIsCompatible(avifFileType * ftyp);

#endif  // SRC_BOX_H_
