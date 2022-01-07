// Copyright 2019 Joe Drago. All rights reserved.
// SPDX-License-Identifier: BSD-2-Clause

#include "avif/internal.h"
#include "box.h"
#include "decoderdata.h"
#include "layout.h"
#include "meta.h"

#include <assert.h>
#include <inttypes.h>
#include <limits.h>
#include <string.h>

// class VisualSampleEntry(codingname) extends SampleEntry(codingname) {
//     unsigned int(16) pre_defined = 0;
//     const unsigned int(16) reserved = 0;
//     unsigned int(32)[3] pre_defined = 0;
//     unsigned int(16) width;
//     unsigned int(16) height;
//     template unsigned int(32) horizresolution = 0x00480000; // 72 dpi
//     template unsigned int(32) vertresolution = 0x00480000;  // 72 dpi
//     const unsigned int(32) reserved = 0;
//     template unsigned int(16) frame_count = 1;
//     string[32] compressorname;
//     template unsigned int(16) depth = 0x0018;
//     int(16) pre_defined = -1;
//     // other boxes from derived specifications
//     CleanApertureBox clap;    // optional
//     PixelAspectRatioBox pasp; // optional
// }
static const size_t VISUALSAMPLEENTRY_SIZE = 78;

// The only supported ipma box values for both version and flags are [0,1], so there technically
// can't be more than 4 unique tuples right now.
#define MAX_IPMA_VERSION_AND_FLAGS_SEEN 4

// ---------------------------------------------------------------------------
// Helper macros / functions

// Use this to keep track of whether or not a child box that must be unique (0 or 1 present) has
// been seen yet, when parsing a parent box. If the "seen" bit is already set for a given box when
// it is encountered during parse, an error is thrown. Which bit corresponds to which box is
// dictated entirely by the calling function.
static avifBool uniqueBoxSeen(uint32_t * uniqueBoxFlags, uint32_t whichFlag, const char * parentBoxType, const char * boxType, avifDiagnostics * diagnostics)
{
    const uint32_t flag = 1 << whichFlag;
    if (*uniqueBoxFlags & flag) {
        // This box has already been seen. Error!
        avifDiagnosticsPrintf(diagnostics, "Box[%s] contains a duplicate unique box of type '%s'", parentBoxType, boxType);
        return AVIF_FALSE;
    }

    // Mark this box as seen.
    *uniqueBoxFlags |= flag;
    return AVIF_TRUE;
}

// ---------------------------------------------------------------------------
// BMFF Parsing

static avifBool avifParseHandlerBox(const uint8_t * raw, size_t rawLen, avifDiagnostics * diag)
{
    BEGIN_STREAM(s, raw, rawLen, diag, "Box[hdlr]");

    CHECK(avifROStreamReadAndEnforceVersion(&s, 0));

    uint32_t predefined;
    CHECK(avifROStreamReadU32(&s, &predefined)); // unsigned int(32) pre_defined = 0;
    if (predefined != 0) {
        avifDiagnosticsPrintf(diag, "Box[hdlr] contains a pre_defined value that is nonzero");
        return AVIF_FALSE;
    }

    uint8_t handlerType[4];
    CHECK(avifROStreamRead(&s, handlerType, 4)); // unsigned int(32) handler_type;
    if (memcmp(handlerType, "pict", 4) != 0) {
        avifDiagnosticsPrintf(diag, "Box[hdlr] handler_type is not 'pict'");
        return AVIF_FALSE;
    }

    for (int i = 0; i < 3; ++i) {
        uint32_t reserved;
        CHECK(avifROStreamReadU32(&s, &reserved)); // const unsigned int(32)[3] reserved = 0;
    }

    // Verify that a valid string is here, but don't bother to store it
    CHECK(avifROStreamReadString(&s, NULL, 0)); // string name;
    return AVIF_TRUE;
}

static avifBool avifParseItemLocationBox(avifMeta * meta, const uint8_t * raw, size_t rawLen, avifDiagnostics * diag)
{
    BEGIN_STREAM(s, raw, rawLen, diag, "Box[iloc]");

    uint8_t version;
    CHECK(avifROStreamReadVersionAndFlags(&s, &version, NULL));
    if (version > 2) {
        avifDiagnosticsPrintf(diag, "Box[iloc] has an unsupported version [%u]", version);
        return AVIF_FALSE;
    }

    uint8_t offsetSizeAndLengthSize;
    CHECK(avifROStreamRead(&s, &offsetSizeAndLengthSize, 1));
    uint8_t offsetSize = (offsetSizeAndLengthSize >> 4) & 0xf; // unsigned int(4) offset_size;
    uint8_t lengthSize = (offsetSizeAndLengthSize >> 0) & 0xf; // unsigned int(4) length_size;

    uint8_t baseOffsetSizeAndIndexSize;
    CHECK(avifROStreamRead(&s, &baseOffsetSizeAndIndexSize, 1));
    uint8_t baseOffsetSize = (baseOffsetSizeAndIndexSize >> 4) & 0xf; // unsigned int(4) base_offset_size;
    uint8_t indexSize = 0;
    if ((version == 1) || (version == 2)) {
        indexSize = baseOffsetSizeAndIndexSize & 0xf; // unsigned int(4) index_size;
        if (indexSize != 0) {
            // extent_index unsupported
            avifDiagnosticsPrintf(diag, "Box[iloc] has an unsupported extent_index");
            return AVIF_FALSE;
        }
    }

    uint16_t tmp16;
    uint32_t itemCount;
    if (version < 2) {
        CHECK(avifROStreamReadU16(&s, &tmp16)); // unsigned int(16) item_count;
        itemCount = tmp16;
    } else {
        CHECK(avifROStreamReadU32(&s, &itemCount)); // unsigned int(32) item_count;
    }
    for (uint32_t i = 0; i < itemCount; ++i) {
        uint32_t itemID;
        if (version < 2) {
            CHECK(avifROStreamReadU16(&s, &tmp16)); // unsigned int(16) item_ID;
            itemID = tmp16;
        } else {
            CHECK(avifROStreamReadU32(&s, &itemID)); // unsigned int(32) item_ID;
        }

        avifDecoderItem * item = avifMetaFindItem(meta, itemID);
        if (!item) {
            avifDiagnosticsPrintf(diag, "Box[iloc] has an invalid item ID [%u]", itemID);
            return AVIF_FALSE;
        }
        if (item->extents.count > 0) {
            // This item has already been given extents via this iloc box. This is invalid.
            avifDiagnosticsPrintf(diag, "Item ID [%u] contains duplicate sets of extents", itemID);
            return AVIF_FALSE;
        }

        if ((version == 1) || (version == 2)) {
            uint8_t ignored;
            uint8_t constructionMethod;
            CHECK(avifROStreamRead(&s, &ignored, 1));            // unsigned int(12) reserved = 0;
            CHECK(avifROStreamRead(&s, &constructionMethod, 1)); // unsigned int(4) construction_method;
            constructionMethod = constructionMethod & 0xf;
            if ((constructionMethod != 0 /* file */) && (constructionMethod != 1 /* idat */)) {
                // construction method item(2) unsupported
                avifDiagnosticsPrintf(diag, "Box[iloc] has an unsupported construction method [%u]", constructionMethod);
                return AVIF_FALSE;
            }
            if (constructionMethod == 1) {
                item->idatStored = AVIF_TRUE;
            }
        }

        uint16_t dataReferenceIndex;                                 // unsigned int(16) data_ref rence_index;
        CHECK(avifROStreamReadU16(&s, &dataReferenceIndex));         //
        uint64_t baseOffset;                                         // unsigned int(base_offset_size*8) base_offset;
        CHECK(avifROStreamReadUX8(&s, &baseOffset, baseOffsetSize)); //
        uint16_t extentCount;                                        // unsigned int(16) extent_count;
        CHECK(avifROStreamReadU16(&s, &extentCount));                //
        for (int extentIter = 0; extentIter < extentCount; ++extentIter) {
            // If extent_index is ever supported, this spec must be implemented here:
            // ::  if (((version == 1) || (version == 2)) && (index_size > 0)) {
            // ::      unsigned int(index_size*8) extent_index;
            // ::  }

            uint64_t extentOffset; // unsigned int(offset_size*8) extent_offset;
            CHECK(avifROStreamReadUX8(&s, &extentOffset, offsetSize));
            uint64_t extentLength; // unsigned int(offset_size*8) extent_length;
            CHECK(avifROStreamReadUX8(&s, &extentLength, lengthSize));

            avifExtent * extent = (avifExtent *)avifArrayPushPtr(&item->extents);
            if (extentOffset > UINT64_MAX - baseOffset) {
                avifDiagnosticsPrintf(diag,
                                      "Item ID [%u] contains an extent offset which overflows: [base: %" PRIu64 " offset:%" PRIu64 "]",
                                      itemID,
                                      baseOffset,
                                      extentOffset);
                return AVIF_FALSE;
            }
            uint64_t offset = baseOffset + extentOffset;
            extent->offset = offset;
            if (extentLength > SIZE_MAX) {
                avifDiagnosticsPrintf(diag, "Item ID [%u] contains an extent length which overflows: [%" PRIu64 "]", itemID, extentLength);
                return AVIF_FALSE;
            }
            extent->size = (size_t)extentLength;
            if (extent->size > SIZE_MAX - item->size) {
                avifDiagnosticsPrintf(diag,
                                      "Item ID [%u] contains an extent length which overflows the item size: [%zu, %zu]",
                                      itemID,
                                      extent->size,
                                      item->size);
                return AVIF_FALSE;
            }
            item->size += extent->size;
        }
    }
    return AVIF_TRUE;
}

avifBool avifParseImageGridBox(avifImageGrid * grid, const uint8_t * raw, size_t rawLen, uint32_t imageSizeLimit, avifDiagnostics * diag)
{
    BEGIN_STREAM(s, raw, rawLen, diag, "Box[grid]");

    uint8_t version, flags;
    CHECK(avifROStreamRead(&s, &version, 1)); // unsigned int(8) version = 0;
    if (version != 0) {
        avifDiagnosticsPrintf(diag, "Box[grid] has unsupported version [%u]", version);
        return AVIF_FALSE;
    }
    uint8_t rowsMinusOne, columnsMinusOne;
    CHECK(avifROStreamRead(&s, &flags, 1));           // unsigned int(8) flags;
    CHECK(avifROStreamRead(&s, &rowsMinusOne, 1));    // unsigned int(8) rows_minus_one;
    CHECK(avifROStreamRead(&s, &columnsMinusOne, 1)); // unsigned int(8) columns_minus_one;
    grid->rows = (uint32_t)rowsMinusOne + 1;
    grid->columns = (uint32_t)columnsMinusOne + 1;

    uint32_t fieldLength = ((flags & 1) + 1) * 16;
    if (fieldLength == 16) {
        uint16_t outputWidth16, outputHeight16;
        CHECK(avifROStreamReadU16(&s, &outputWidth16));  // unsigned int(FieldLength) output_width;
        CHECK(avifROStreamReadU16(&s, &outputHeight16)); // unsigned int(FieldLength) output_height;
        grid->outputWidth = outputWidth16;
        grid->outputHeight = outputHeight16;
    } else {
        if (fieldLength != 32) {
            // This should be impossible
            avifDiagnosticsPrintf(diag, "Grid box contains illegal field length: [%u]", fieldLength);
            return AVIF_FALSE;
        }
        CHECK(avifROStreamReadU32(&s, &grid->outputWidth));  // unsigned int(FieldLength) output_width;
        CHECK(avifROStreamReadU32(&s, &grid->outputHeight)); // unsigned int(FieldLength) output_height;
    }
    if ((grid->outputWidth == 0) || (grid->outputHeight == 0)) {
        avifDiagnosticsPrintf(diag, "Grid box contains illegal dimensions: [%u x %u]", grid->outputWidth, grid->outputHeight);
        return AVIF_FALSE;
    }
    if (grid->outputWidth > (imageSizeLimit / grid->outputHeight)) {
        avifDiagnosticsPrintf(diag, "Grid box dimensions are too large: [%u x %u]", grid->outputWidth, grid->outputHeight);
        return AVIF_FALSE;
    }
    return avifROStreamRemainingBytes(&s) == 0;
}

static avifBool avifParseImageSpatialExtentsProperty(avifProperty * prop, const uint8_t * raw, size_t rawLen, avifDiagnostics * diag)
{
    BEGIN_STREAM(s, raw, rawLen, diag, "Box[ispe]");
    CHECK(avifROStreamReadAndEnforceVersion(&s, 0));

    avifImageSpatialExtents * ispe = &prop->u.ispe;
    CHECK(avifROStreamReadU32(&s, &ispe->width));
    CHECK(avifROStreamReadU32(&s, &ispe->height));
    return AVIF_TRUE;
}

static avifBool avifParseAuxiliaryTypeProperty(avifProperty * prop, const uint8_t * raw, size_t rawLen, avifDiagnostics * diag)
{
    BEGIN_STREAM(s, raw, rawLen, diag, "Box[auxC]");
    CHECK(avifROStreamReadAndEnforceVersion(&s, 0));

    CHECK(avifROStreamReadString(&s, prop->u.auxC.auxType, AUXTYPE_SIZE));
    return AVIF_TRUE;
}

static avifBool avifParseColourInformationBox(avifProperty * prop, const uint8_t * raw, size_t rawLen, avifDiagnostics * diag)
{
    BEGIN_STREAM(s, raw, rawLen, diag, "Box[colr]");

    avifColourInformationBox * colr = &prop->u.colr;
    colr->hasICC = AVIF_FALSE;
    colr->hasNCLX = AVIF_FALSE;

    uint8_t colorType[4]; // unsigned int(32) colour_type;
    CHECK(avifROStreamRead(&s, colorType, 4));
    if (!memcmp(colorType, "rICC", 4) || !memcmp(colorType, "prof", 4)) {
        colr->hasICC = AVIF_TRUE;
        colr->icc = avifROStreamCurrent(&s);
        colr->iccSize = avifROStreamRemainingBytes(&s);
    } else if (!memcmp(colorType, "nclx", 4)) {
        CHECK(avifROStreamReadU16(&s, &colr->colorPrimaries));          // unsigned int(16) colour_primaries;
        CHECK(avifROStreamReadU16(&s, &colr->transferCharacteristics)); // unsigned int(16) transfer_characteristics;
        CHECK(avifROStreamReadU16(&s, &colr->matrixCoefficients));      // unsigned int(16) matrix_coefficients;
        // unsigned int(1) full_range_flag;
        // unsigned int(7) reserved = 0;
        uint8_t tmp8;
        CHECK(avifROStreamRead(&s, &tmp8, 1));
        colr->range = (tmp8 & 0x80) ? AVIF_RANGE_FULL : AVIF_RANGE_LIMITED;
        colr->hasNCLX = AVIF_TRUE;
    }
    return AVIF_TRUE;
}

static avifBool avifParseAV1CodecConfigurationBox(const uint8_t * raw, size_t rawLen, avifCodecConfigurationBox * av1C, avifDiagnostics * diag)
{
    BEGIN_STREAM(s, raw, rawLen, diag, "Box[av1C]");

    uint8_t markerAndVersion = 0;
    CHECK(avifROStreamRead(&s, &markerAndVersion, 1));
    uint8_t seqProfileAndIndex = 0;
    CHECK(avifROStreamRead(&s, &seqProfileAndIndex, 1));
    uint8_t rawFlags = 0;
    CHECK(avifROStreamRead(&s, &rawFlags, 1));

    if (markerAndVersion != 0x81) {
        // Marker and version must both == 1
        avifDiagnosticsPrintf(diag, "av1C contains illegal marker and version pair: [%u]", markerAndVersion);
        return AVIF_FALSE;
    }

    av1C->seqProfile = (seqProfileAndIndex >> 5) & 0x7;    // unsigned int (3) seq_profile;
    av1C->seqLevelIdx0 = (seqProfileAndIndex >> 0) & 0x1f; // unsigned int (5) seq_level_idx_0;
    av1C->seqTier0 = (rawFlags >> 7) & 0x1;                // unsigned int (1) seq_tier_0;
    av1C->highBitdepth = (rawFlags >> 6) & 0x1;            // unsigned int (1) high_bitdepth;
    av1C->twelveBit = (rawFlags >> 5) & 0x1;               // unsigned int (1) twelve_bit;
    av1C->monochrome = (rawFlags >> 4) & 0x1;              // unsigned int (1) monochrome;
    av1C->chromaSubsamplingX = (rawFlags >> 3) & 0x1;      // unsigned int (1) chroma_subsampling_x;
    av1C->chromaSubsamplingY = (rawFlags >> 2) & 0x1;      // unsigned int (1) chroma_subsampling_y;
    av1C->chromaSamplePosition = (rawFlags >> 0) & 0x3;    // unsigned int (2) chroma_sample_position;
    return AVIF_TRUE;
}

static avifBool avifParseAV1CodecConfigurationBoxProperty(avifProperty * prop, const uint8_t * raw, size_t rawLen, avifDiagnostics * diag)
{
    return avifParseAV1CodecConfigurationBox(raw, rawLen, &prop->u.av1C, diag);
}

static avifBool avifParsePixelAspectRatioBoxProperty(avifProperty * prop, const uint8_t * raw, size_t rawLen, avifDiagnostics * diag)
{
    BEGIN_STREAM(s, raw, rawLen, diag, "Box[pasp]");

    avifPixelAspectRatioBox * pasp = &prop->u.pasp;
    CHECK(avifROStreamReadU32(&s, &pasp->hSpacing)); // unsigned int(32) hSpacing;
    CHECK(avifROStreamReadU32(&s, &pasp->vSpacing)); // unsigned int(32) vSpacing;
    return AVIF_TRUE;
}

static avifBool avifParseCleanApertureBoxProperty(avifProperty * prop, const uint8_t * raw, size_t rawLen, avifDiagnostics * diag)
{
    BEGIN_STREAM(s, raw, rawLen, diag, "Box[clap]");

    avifCleanApertureBox * clap = &prop->u.clap;
    CHECK(avifROStreamReadU32(&s, &clap->widthN));    // unsigned int(32) cleanApertureWidthN;
    CHECK(avifROStreamReadU32(&s, &clap->widthD));    // unsigned int(32) cleanApertureWidthD;
    CHECK(avifROStreamReadU32(&s, &clap->heightN));   // unsigned int(32) cleanApertureHeightN;
    CHECK(avifROStreamReadU32(&s, &clap->heightD));   // unsigned int(32) cleanApertureHeightD;
    CHECK(avifROStreamReadU32(&s, &clap->horizOffN)); // unsigned int(32) horizOffN;
    CHECK(avifROStreamReadU32(&s, &clap->horizOffD)); // unsigned int(32) horizOffD;
    CHECK(avifROStreamReadU32(&s, &clap->vertOffN));  // unsigned int(32) vertOffN;
    CHECK(avifROStreamReadU32(&s, &clap->vertOffD));  // unsigned int(32) vertOffD;
    return AVIF_TRUE;
}

static avifBool avifParseImageRotationProperty(avifProperty * prop, const uint8_t * raw, size_t rawLen, avifDiagnostics * diag)
{
    BEGIN_STREAM(s, raw, rawLen, diag, "Box[irot]");

    avifImageRotation * irot = &prop->u.irot;
    CHECK(avifROStreamRead(&s, &irot->angle, 1)); // unsigned int (6) reserved = 0; unsigned int (2) angle;
    if ((irot->angle & 0xfc) != 0) {
        // reserved bits must be 0
        avifDiagnosticsPrintf(diag, "Box[irot] contains nonzero reserved bits [%u]", irot->angle);
        return AVIF_FALSE;
    }
    return AVIF_TRUE;
}

static avifBool avifParseImageMirrorProperty(avifProperty * prop, const uint8_t * raw, size_t rawLen, avifDiagnostics * diag)
{
    BEGIN_STREAM(s, raw, rawLen, diag, "Box[imir]");

    avifImageMirror * imir = &prop->u.imir;
    CHECK(avifROStreamRead(&s, &imir->mode, 1)); // unsigned int (7) reserved = 0; unsigned int (1) mode;
    if ((imir->mode & 0xfe) != 0) {
        // reserved bits must be 0
        avifDiagnosticsPrintf(diag, "Box[imir] contains nonzero reserved bits [%u]", imir->mode);
        return AVIF_FALSE;
    }
    return AVIF_TRUE;
}

static avifBool avifParsePixelInformationProperty(avifProperty * prop, const uint8_t * raw, size_t rawLen, avifDiagnostics * diag)
{
    BEGIN_STREAM(s, raw, rawLen, diag, "Box[pixi]");
    CHECK(avifROStreamReadAndEnforceVersion(&s, 0));

    avifPixelInformationProperty * pixi = &prop->u.pixi;
    CHECK(avifROStreamRead(&s, &pixi->planeCount, 1)); // unsigned int (8) num_channels;
    if (pixi->planeCount > MAX_PIXI_PLANE_DEPTHS) {
        avifDiagnosticsPrintf(diag, "Box[pixi] contains unsupported plane count [%u]", pixi->planeCount);
        return AVIF_FALSE;
    }
    for (uint8_t i = 0; i < pixi->planeCount; ++i) {
        CHECK(avifROStreamRead(&s, &pixi->planeDepths[i], 1)); // unsigned int (8) bits_per_channel;
    }
    return AVIF_TRUE;
}

static avifBool avifParseOperatingPointSelectorProperty(avifProperty * prop, const uint8_t * raw, size_t rawLen, avifDiagnostics * diag)
{
    BEGIN_STREAM(s, raw, rawLen, diag, "Box[a1op]");

    avifOperatingPointSelectorProperty * a1op = &prop->u.a1op;
    CHECK(avifROStreamRead(&s, &a1op->opIndex, 1));
    if (a1op->opIndex > 31) { // 31 is AV1's max operating point value
        avifDiagnosticsPrintf(diag, "Box[a1op] contains an unsupported operating point [%u]", a1op->opIndex);
        return AVIF_FALSE;
    }
    return AVIF_TRUE;
}

static avifBool avifParseLayerSelectorProperty(avifProperty * prop, const uint8_t * raw, size_t rawLen, avifDiagnostics * diag)
{
    BEGIN_STREAM(s, raw, rawLen, diag, "Box[lsel]");

    avifLayerSelectorProperty * lsel = &prop->u.lsel;
    CHECK(avifROStreamReadU16(&s, &lsel->layerID));
    if (lsel->layerID >= MAX_AV1_LAYER_COUNT) {
        avifDiagnosticsPrintf(diag, "Box[lsel] contains an unsupported layer [%u]", lsel->layerID);
        return AVIF_FALSE;
    }
    return AVIF_TRUE;
}

static avifBool avifParseAV1LayeredImageIndexingProperty(avifProperty * prop, const uint8_t * raw, size_t rawLen, avifDiagnostics * diag)
{
    BEGIN_STREAM(s, raw, rawLen, diag, "Box[a1lx]");

    avifAV1LayeredImageIndexingProperty * a1lx = &prop->u.a1lx;

    uint8_t largeSize = 0;
    CHECK(avifROStreamRead(&s, &largeSize, 1));
    if (largeSize & 0xFE) {
        avifDiagnosticsPrintf(diag, "Box[a1lx] has bits set in the reserved section [%u]", largeSize);
        return AVIF_FALSE;
    }

    for (int i = 0; i < 3; ++i) {
        if (largeSize) {
            CHECK(avifROStreamReadU32(&s, &a1lx->layerSize[i]));
        } else {
            uint16_t layerSize16;
            CHECK(avifROStreamReadU16(&s, &layerSize16));
            a1lx->layerSize[i] = (uint32_t)layerSize16;
        }
    }

    // Layer sizes will be validated layer (when the item's size is known)
    return AVIF_TRUE;
}

static avifBool avifParseItemPropertyContainerBox(avifPropertyArray * properties, const uint8_t * raw, size_t rawLen, avifDiagnostics * diag)
{
    BEGIN_STREAM(s, raw, rawLen, diag, "Box[ipco]");

    while (avifROStreamHasBytesLeft(&s, 1)) {
        avifBoxHeader header;
        CHECK(avifROStreamReadBoxHeader(&s, &header));

        int propertyIndex = avifArrayPushIndex(properties);
        avifProperty * prop = &properties->prop[propertyIndex];
        memcpy(prop->type, header.type, 4);
        if (!memcmp(header.type, "ispe", 4)) {
            CHECK(avifParseImageSpatialExtentsProperty(prop, avifROStreamCurrent(&s), header.size, diag));
        } else if (!memcmp(header.type, "auxC", 4)) {
            CHECK(avifParseAuxiliaryTypeProperty(prop, avifROStreamCurrent(&s), header.size, diag));
        } else if (!memcmp(header.type, "colr", 4)) {
            CHECK(avifParseColourInformationBox(prop, avifROStreamCurrent(&s), header.size, diag));
        } else if (!memcmp(header.type, "av1C", 4)) {
            CHECK(avifParseAV1CodecConfigurationBoxProperty(prop, avifROStreamCurrent(&s), header.size, diag));
        } else if (!memcmp(header.type, "pasp", 4)) {
            CHECK(avifParsePixelAspectRatioBoxProperty(prop, avifROStreamCurrent(&s), header.size, diag));
        } else if (!memcmp(header.type, "clap", 4)) {
            CHECK(avifParseCleanApertureBoxProperty(prop, avifROStreamCurrent(&s), header.size, diag));
        } else if (!memcmp(header.type, "irot", 4)) {
            CHECK(avifParseImageRotationProperty(prop, avifROStreamCurrent(&s), header.size, diag));
        } else if (!memcmp(header.type, "imir", 4)) {
            CHECK(avifParseImageMirrorProperty(prop, avifROStreamCurrent(&s), header.size, diag));
        } else if (!memcmp(header.type, "pixi", 4)) {
            CHECK(avifParsePixelInformationProperty(prop, avifROStreamCurrent(&s), header.size, diag));
        } else if (!memcmp(header.type, "a1op", 4)) {
            CHECK(avifParseOperatingPointSelectorProperty(prop, avifROStreamCurrent(&s), header.size, diag));
        } else if (!memcmp(header.type, "lsel", 4)) {
            CHECK(avifParseLayerSelectorProperty(prop, avifROStreamCurrent(&s), header.size, diag));
        } else if (!memcmp(header.type, "a1lx", 4)) {
            CHECK(avifParseAV1LayeredImageIndexingProperty(prop, avifROStreamCurrent(&s), header.size, diag));
        }

        CHECK(avifROStreamSkip(&s, header.size));
    }
    return AVIF_TRUE;
}

static avifBool avifParseItemPropertyAssociation(avifMeta * meta, const uint8_t * raw, size_t rawLen, avifDiagnostics * diag, uint32_t * outVersionAndFlags)
{
    // NOTE: If this function ever adds support for versions other than [0,1] or flags other than
    //       [0,1], please increase the value of MAX_IPMA_VERSION_AND_FLAGS_SEEN accordingly.

    BEGIN_STREAM(s, raw, rawLen, diag, "Box[ipma]");

    uint8_t version;
    uint32_t flags;
    CHECK(avifROStreamReadVersionAndFlags(&s, &version, &flags));
    avifBool propertyIndexIsU16 = ((flags & 0x1) != 0);
    *outVersionAndFlags = ((uint32_t)version << 24) | flags;

    uint32_t entryCount;
    CHECK(avifROStreamReadU32(&s, &entryCount));
    unsigned int prevItemID = 0;
    for (uint32_t entryIndex = 0; entryIndex < entryCount; ++entryIndex) {
        // ISO/IEC 23008-12, First edition, 2017-12, Section 9.3.1:
        //   Each ItemPropertyAssociation box shall be ordered by increasing item_ID, and there shall
        //   be at most one association box for each item_ID, in any ItemPropertyAssociation box.
        unsigned int itemID;
        if (version < 1) {
            uint16_t tmp;
            CHECK(avifROStreamReadU16(&s, &tmp));
            itemID = tmp;
        } else {
            CHECK(avifROStreamReadU32(&s, &itemID));
        }
        if (itemID <= prevItemID) {
            avifDiagnosticsPrintf(diag, "Box[ipma] item IDs are not ordered by increasing ID");
            return AVIF_FALSE;
        }
        prevItemID = itemID;

        avifDecoderItem * item = avifMetaFindItem(meta, itemID);
        if (!item) {
            avifDiagnosticsPrintf(diag, "Box[ipma] has an invalid item ID [%u]", itemID);
            return AVIF_FALSE;
        }
        if (item->ipmaSeen) {
            avifDiagnosticsPrintf(diag, "Duplicate Box[ipma] for item ID [%u]", itemID);
            return AVIF_FALSE;
        }
        item->ipmaSeen = AVIF_TRUE;

        uint8_t associationCount;
        CHECK(avifROStreamRead(&s, &associationCount, 1));
        for (uint8_t associationIndex = 0; associationIndex < associationCount; ++associationIndex) {
            avifBool essential = AVIF_FALSE;
            uint16_t propertyIndex = 0;
            if (propertyIndexIsU16) {
                CHECK(avifROStreamReadU16(&s, &propertyIndex));
                essential = ((propertyIndex & 0x8000) != 0);
                propertyIndex &= 0x7fff;
            } else {
                uint8_t tmp;
                CHECK(avifROStreamRead(&s, &tmp, 1));
                essential = ((tmp & 0x80) != 0);
                propertyIndex = tmp & 0x7f;
            }

            if (propertyIndex == 0) {
                // Not associated with any item
                continue;
            }
            --propertyIndex; // 1-indexed

            if (propertyIndex >= meta->properties.count) {
                avifDiagnosticsPrintf(diag,
                                      "Box[ipma] for item ID [%u] contains an illegal property index [%u] (out of [%u] properties)",
                                      itemID,
                                      propertyIndex,
                                      meta->properties.count);
                return AVIF_FALSE;
            }

            // Copy property to item
            avifProperty * srcProp = &meta->properties.prop[propertyIndex];

            static const char * supportedTypes[] = { "ispe", "auxC", "colr", "av1C", "pasp", "clap",
                                                     "irot", "imir", "pixi", "a1op", "lsel", "a1lx" };
            size_t supportedTypesCount = sizeof(supportedTypes) / sizeof(supportedTypes[0]);
            avifBool supportedType = AVIF_FALSE;
            for (size_t i = 0; i < supportedTypesCount; ++i) {
                if (!memcmp(srcProp->type, supportedTypes[i], 4)) {
                    supportedType = AVIF_TRUE;
                    break;
                }
            }
            if (supportedType) {
                if (essential) {
                    // Verify that it is legal for this property to be flagged as essential. Any
                    // types in this list are *required* in the spec to not be flagged as essential
                    // when associated with an item.
                    static const char * const nonessentialTypes[] = {

                        // AVIF: Section 2.3.2.3.2: "If associated, it shall not be marked as essential."
                        "a1lx"

                    };
                    size_t nonessentialTypesCount = sizeof(nonessentialTypes) / sizeof(nonessentialTypes[0]);
                    for (size_t i = 0; i < nonessentialTypesCount; ++i) {
                        if (!memcmp(srcProp->type, nonessentialTypes[i], 4)) {
                            avifDiagnosticsPrintf(diag,
                                                  "Item ID [%u] has a %s property association which must not be marked essential, but is",
                                                  itemID,
                                                  nonessentialTypes[i]);
                            return AVIF_FALSE;
                        }
                    }
                } else {
                    // Verify that it is legal for this property to not be flagged as essential. Any
                    // types in this list are *required* in the spec to be flagged as essential when
                    // associated with an item.
                    static const char * const essentialTypes[] = {

                        // AVIF: Section 2.3.2.1.1: "If associated, it shall be marked as essential."
                        "a1op",

                        // HEIF: Section 6.5.11.1: "essential shall be equal to 1 for an 'lsel' item property."
                        "lsel"

                    };
                    size_t essentialTypesCount = sizeof(essentialTypes) / sizeof(essentialTypes[0]);
                    for (size_t i = 0; i < essentialTypesCount; ++i) {
                        if (!memcmp(srcProp->type, essentialTypes[i], 4)) {
                            avifDiagnosticsPrintf(diag,
                                                  "Item ID [%u] has a %s property association which must be marked essential, but is not",
                                                  itemID,
                                                  essentialTypes[i]);
                            return AVIF_FALSE;
                        }
                    }
                }

                // Supported and valid; associate it with this item.
                avifProperty * dstProp = (avifProperty *)avifArrayPushPtr(&item->properties);
                memcpy(dstProp, srcProp, sizeof(avifProperty));
            } else {
                if (essential) {
                    // Discovered an essential item property that libavif doesn't support!
                    // Make a note to ignore this item later.
                    item->hasUnsupportedEssentialProperty = AVIF_TRUE;
                }
            }
        }
    }
    return AVIF_TRUE;
}

static avifBool avifParsePrimaryItemBox(avifMeta * meta, const uint8_t * raw, size_t rawLen, avifDiagnostics * diag)
{
    if (meta->primaryItemID > 0) {
        // Illegal to have multiple pitm boxes, bail out
        avifDiagnosticsPrintf(diag, "Multiple boxes of unique Box[pitm] found");
        return AVIF_FALSE;
    }

    BEGIN_STREAM(s, raw, rawLen, diag, "Box[pitm]");

    uint8_t version;
    CHECK(avifROStreamReadVersionAndFlags(&s, &version, NULL));

    if (version == 0) {
        uint16_t tmp16;
        CHECK(avifROStreamReadU16(&s, &tmp16)); // unsigned int(16) item_ID;
        meta->primaryItemID = tmp16;
    } else {
        CHECK(avifROStreamReadU32(&s, &meta->primaryItemID)); // unsigned int(32) item_ID;
    }
    return AVIF_TRUE;
}

static avifBool avifParseItemDataBox(avifMeta * meta, const uint8_t * raw, size_t rawLen, avifDiagnostics * diag)
{
    // Check to see if we've already seen an idat box for this meta box. If so, bail out
    if (meta->idat.size > 0) {
        avifDiagnosticsPrintf(diag, "Meta box contains multiple idat boxes");
        return AVIF_FALSE;
    }
    if (rawLen == 0) {
        avifDiagnosticsPrintf(diag, "idat box has a length of 0");
        return AVIF_FALSE;
    }

    avifRWDataSet(&meta->idat, raw, rawLen);
    return AVIF_TRUE;
}

static avifBool avifParseItemPropertiesBox(avifMeta * meta, const uint8_t * raw, size_t rawLen, avifDiagnostics * diag)
{
    BEGIN_STREAM(s, raw, rawLen, diag, "Box[iprp]");

    avifBoxHeader ipcoHeader;
    CHECK(avifROStreamReadBoxHeader(&s, &ipcoHeader));
    if (memcmp(ipcoHeader.type, "ipco", 4)) {
        avifDiagnosticsPrintf(diag, "Failed to find Box[ipco] as the first box in Box[iprp]");
        return AVIF_FALSE;
    }

    // Read all item properties inside of ItemPropertyContainerBox
    CHECK(avifParseItemPropertyContainerBox(&meta->properties, avifROStreamCurrent(&s), ipcoHeader.size, diag));
    CHECK(avifROStreamSkip(&s, ipcoHeader.size));

    uint32_t versionAndFlagsSeen[MAX_IPMA_VERSION_AND_FLAGS_SEEN];
    uint32_t versionAndFlagsSeenCount = 0;

    // Now read all ItemPropertyAssociation until the end of the box, and make associations
    while (avifROStreamHasBytesLeft(&s, 1)) {
        avifBoxHeader ipmaHeader;
        CHECK(avifROStreamReadBoxHeader(&s, &ipmaHeader));

        if (!memcmp(ipmaHeader.type, "ipma", 4)) {
            uint32_t versionAndFlags;
            CHECK(avifParseItemPropertyAssociation(meta, avifROStreamCurrent(&s), ipmaHeader.size, diag, &versionAndFlags));
            for (uint32_t i = 0; i < versionAndFlagsSeenCount; ++i) {
                if (versionAndFlagsSeen[i] == versionAndFlags) {
                    // HEIF (ISO 23008-12:2017) 9.3.1 - There shall be at most one
                    // ItemPropertyAssociation box with a given pair of values of version and
                    // flags.
                    avifDiagnosticsPrintf(diag, "Multiple Box[ipma] with a given pair of values of version and flags. See HEIF (ISO 23008-12:2017) 9.3.1");
                    return AVIF_FALSE;
                }
            }
            if (versionAndFlagsSeenCount == MAX_IPMA_VERSION_AND_FLAGS_SEEN) {
                avifDiagnosticsPrintf(diag, "Exceeded possible count of unique ipma version and flags tuples");
                return AVIF_FALSE;
            }
            versionAndFlagsSeen[versionAndFlagsSeenCount] = versionAndFlags;
            ++versionAndFlagsSeenCount;
        } else {
            // These must all be type ipma
            avifDiagnosticsPrintf(diag, "Box[iprp] contains a box that isn't type 'ipma'");
            return AVIF_FALSE;
        }

        CHECK(avifROStreamSkip(&s, ipmaHeader.size));
    }
    return AVIF_TRUE;
}

static avifBool avifParseItemInfoEntry(avifMeta * meta, const uint8_t * raw, size_t rawLen, avifDiagnostics * diag)
{
    BEGIN_STREAM(s, raw, rawLen, diag, "Box[infe]");

    CHECK(avifROStreamReadAndEnforceVersion(&s, 2)); // TODO: support version > 2? 2+ is required for item_type

    uint16_t itemID;                                      // unsigned int(16) item_ID;
    CHECK(avifROStreamReadU16(&s, &itemID));              //
    uint16_t itemProtectionIndex;                         // unsigned int(16) item_protection_index;
    CHECK(avifROStreamReadU16(&s, &itemProtectionIndex)); //
    uint8_t itemType[4];                                  // unsigned int(32) item_type;
    CHECK(avifROStreamRead(&s, itemType, 4));             //

    avifContentType contentType;
    if (!memcmp(itemType, "mime", 4)) {
        CHECK(avifROStreamReadString(&s, NULL, 0));                                   // string item_name; (skipped)
        CHECK(avifROStreamReadString(&s, contentType.contentType, CONTENTTYPE_SIZE)); // string content_type;
    } else {
        memset(&contentType, 0, sizeof(contentType));
    }

    avifDecoderItem * item = avifMetaFindItem(meta, itemID);
    if (!item) {
        avifDiagnosticsPrintf(diag, "Box[infe] has an invalid item ID [%u]", itemID);
        return AVIF_FALSE;
    }

    memcpy(item->type, itemType, sizeof(itemType));
    memcpy(&item->contentType, &contentType, sizeof(contentType));
    return AVIF_TRUE;
}

static avifBool avifParseItemInfoBox(avifMeta * meta, const uint8_t * raw, size_t rawLen, avifDiagnostics * diag)
{
    BEGIN_STREAM(s, raw, rawLen, diag, "Box[iinf]");

    uint8_t version;
    CHECK(avifROStreamReadVersionAndFlags(&s, &version, NULL));
    uint32_t entryCount;
    if (version == 0) {
        uint16_t tmp;
        CHECK(avifROStreamReadU16(&s, &tmp)); // unsigned int(16) entry_count;
        entryCount = tmp;
    } else if (version == 1) {
        CHECK(avifROStreamReadU32(&s, &entryCount)); // unsigned int(32) entry_count;
    } else {
        avifDiagnosticsPrintf(diag, "Box[iinf] has an unsupported version %u", version);
        return AVIF_FALSE;
    }

    for (uint32_t entryIndex = 0; entryIndex < entryCount; ++entryIndex) {
        avifBoxHeader infeHeader;
        CHECK(avifROStreamReadBoxHeader(&s, &infeHeader));

        if (!memcmp(infeHeader.type, "infe", 4)) {
            CHECK(avifParseItemInfoEntry(meta, avifROStreamCurrent(&s), infeHeader.size, diag));
        } else {
            // These must all be type infe
            avifDiagnosticsPrintf(diag, "Box[iinf] contains a box that isn't type 'infe'");
            return AVIF_FALSE;
        }

        CHECK(avifROStreamSkip(&s, infeHeader.size));
    }

    return AVIF_TRUE;
}

static avifBool avifParseItemReferenceBox(avifMeta * meta, const uint8_t * raw, size_t rawLen, avifDiagnostics * diag)
{
    BEGIN_STREAM(s, raw, rawLen, diag, "Box[iref]");

    uint8_t version;
    CHECK(avifROStreamReadVersionAndFlags(&s, &version, NULL));

    while (avifROStreamHasBytesLeft(&s, 1)) {
        avifBoxHeader irefHeader;
        CHECK(avifROStreamReadBoxHeader(&s, &irefHeader));

        uint32_t fromID = 0;
        if (version == 0) {
            uint16_t tmp;
            CHECK(avifROStreamReadU16(&s, &tmp)); // unsigned int(16) from_item_ID;
            fromID = tmp;
        } else if (version == 1) {
            CHECK(avifROStreamReadU32(&s, &fromID)); // unsigned int(32) from_item_ID;
        } else {
            // unsupported iref version, skip it
            break;
        }

        uint16_t referenceCount = 0;
        CHECK(avifROStreamReadU16(&s, &referenceCount)); // unsigned int(16) reference_count;

        for (uint16_t refIndex = 0; refIndex < referenceCount; ++refIndex) {
            uint32_t toID = 0;
            if (version == 0) {
                uint16_t tmp;
                CHECK(avifROStreamReadU16(&s, &tmp)); // unsigned int(16) to_item_ID;
                toID = tmp;
            } else if (version == 1) {
                CHECK(avifROStreamReadU32(&s, &toID)); // unsigned int(32) to_item_ID;
            } else {
                // unsupported iref version, skip it
                break;
            }

            // Read this reference as "{fromID} is a {irefType} for {toID}"
            if (fromID && toID) {
                avifDecoderItem * item = avifMetaFindItem(meta, fromID);
                if (!item) {
                    avifDiagnosticsPrintf(diag, "Box[iref] has an invalid item ID [%u]", fromID);
                    return AVIF_FALSE;
                }

                if (!memcmp(irefHeader.type, "thmb", 4)) {
                    item->thumbnailForID = toID;
                } else if (!memcmp(irefHeader.type, "auxl", 4)) {
                    item->auxForID = toID;
                } else if (!memcmp(irefHeader.type, "cdsc", 4)) {
                    item->descForID = toID;
                } else if (!memcmp(irefHeader.type, "dimg", 4)) {
                    // derived images refer in the opposite direction
                    avifDecoderItem * dimg = avifMetaFindItem(meta, toID);
                    if (!dimg) {
                        avifDiagnosticsPrintf(diag, "Box[iref] has an invalid item ID dimg ref [%u]", toID);
                        return AVIF_FALSE;
                    }

                    dimg->dimgForID = fromID;
                } else if (!memcmp(irefHeader.type, "prem", 4)) {
                    item->premByID = toID;
                }
            }
        }
    }

    return AVIF_TRUE;
}

static avifBool avifParseMetaBox(avifMeta * meta, const uint8_t * raw, size_t rawLen, avifDiagnostics * diag)
{
    BEGIN_STREAM(s, raw, rawLen, diag, "Box[meta]");

    CHECK(avifROStreamReadAndEnforceVersion(&s, 0));

    ++meta->idatID; // for tracking idat

    avifBool firstBox = AVIF_TRUE;
    uint32_t uniqueBoxFlags = 0;
    while (avifROStreamHasBytesLeft(&s, 1)) {
        avifBoxHeader header;
        CHECK(avifROStreamReadBoxHeader(&s, &header));

        if (firstBox) {
            if (!memcmp(header.type, "hdlr", 4)) {
                CHECK(uniqueBoxSeen(&uniqueBoxFlags, 0, "meta", "hdlr", diag));
                CHECK(avifParseHandlerBox(avifROStreamCurrent(&s), header.size, diag));
                firstBox = AVIF_FALSE;
            } else {
                // hdlr must be the first box!
                avifDiagnosticsPrintf(diag, "Box[meta] does not have a Box[hdlr] as its first child box");
                return AVIF_FALSE;
            }
        } else if (!memcmp(header.type, "iloc", 4)) {
            CHECK(uniqueBoxSeen(&uniqueBoxFlags, 1, "meta", "iloc", diag));
            CHECK(avifParseItemLocationBox(meta, avifROStreamCurrent(&s), header.size, diag));
        } else if (!memcmp(header.type, "pitm", 4)) {
            CHECK(uniqueBoxSeen(&uniqueBoxFlags, 2, "meta", "pitm", diag));
            CHECK(avifParsePrimaryItemBox(meta, avifROStreamCurrent(&s), header.size, diag));
        } else if (!memcmp(header.type, "idat", 4)) {
            CHECK(uniqueBoxSeen(&uniqueBoxFlags, 3, "meta", "idat", diag));
            CHECK(avifParseItemDataBox(meta, avifROStreamCurrent(&s), header.size, diag));
        } else if (!memcmp(header.type, "iprp", 4)) {
            CHECK(uniqueBoxSeen(&uniqueBoxFlags, 4, "meta", "iprp", diag));
            CHECK(avifParseItemPropertiesBox(meta, avifROStreamCurrent(&s), header.size, diag));
        } else if (!memcmp(header.type, "iinf", 4)) {
            CHECK(uniqueBoxSeen(&uniqueBoxFlags, 5, "meta", "iinf", diag));
            CHECK(avifParseItemInfoBox(meta, avifROStreamCurrent(&s), header.size, diag));
        } else if (!memcmp(header.type, "iref", 4)) {
            CHECK(uniqueBoxSeen(&uniqueBoxFlags, 6, "meta", "iref", diag));
            CHECK(avifParseItemReferenceBox(meta, avifROStreamCurrent(&s), header.size, diag));
        }

        CHECK(avifROStreamSkip(&s, header.size));
    }
    if (firstBox) {
        // The meta box must not be empty (it must contain at least a hdlr box)
        avifDiagnosticsPrintf(diag, "Box[meta] has no child boxes");
        return AVIF_FALSE;
    }
    return AVIF_TRUE;
}

static avifBool avifParseTrackHeaderBox(avifTrack * track, const uint8_t * raw, size_t rawLen, uint32_t imageSizeLimit, avifDiagnostics * diag)
{
    BEGIN_STREAM(s, raw, rawLen, diag, "Box[tkhd]");

    uint8_t version;
    CHECK(avifROStreamReadVersionAndFlags(&s, &version, NULL));

    uint32_t ignored32, trackID;
    uint64_t ignored64;
    if (version == 1) {
        CHECK(avifROStreamReadU64(&s, &ignored64)); // unsigned int(64) creation_time;
        CHECK(avifROStreamReadU64(&s, &ignored64)); // unsigned int(64) modification_time;
        CHECK(avifROStreamReadU32(&s, &trackID));   // unsigned int(32) track_ID;
        CHECK(avifROStreamReadU32(&s, &ignored32)); // const unsigned int(32) reserved = 0;
        CHECK(avifROStreamReadU64(&s, &ignored64)); // unsigned int(64) duration;
    } else if (version == 0) {
        CHECK(avifROStreamReadU32(&s, &ignored32)); // unsigned int(32) creation_time;
        CHECK(avifROStreamReadU32(&s, &ignored32)); // unsigned int(32) modification_time;
        CHECK(avifROStreamReadU32(&s, &trackID));   // unsigned int(32) track_ID;
        CHECK(avifROStreamReadU32(&s, &ignored32)); // const unsigned int(32) reserved = 0;
        CHECK(avifROStreamReadU32(&s, &ignored32)); // unsigned int(32) duration;
    } else {
        // Unsupported version
        avifDiagnosticsPrintf(diag, "Box[tkhd] has an unsupported version [%u]", version);
        return AVIF_FALSE;
    }

    // Skipping the following 52 bytes here:
    // ------------------------------------
    // const unsigned int(32)[2] reserved = 0;
    // template int(16) layer = 0;
    // template int(16) alternate_group = 0;
    // template int(16) volume = {if track_is_audio 0x0100 else 0};
    // const unsigned int(16) reserved = 0;
    // template int(32)[9] matrix= { 0x00010000,0,0,0,0x00010000,0,0,0,0x40000000 }; // unity matrix
    CHECK(avifROStreamSkip(&s, 52));

    uint32_t width, height;
    CHECK(avifROStreamReadU32(&s, &width));  // unsigned int(32) width;
    CHECK(avifROStreamReadU32(&s, &height)); // unsigned int(32) height;
    track->width = width >> 16;
    track->height = height >> 16;

    if ((track->width == 0) || (track->height == 0)) {
        avifDiagnosticsPrintf(diag, "Track ID [%u] has an invalid size [%ux%u]", track->id, track->width, track->height);
        return AVIF_FALSE;
    }
    if (track->width > (imageSizeLimit / track->height)) {
        avifDiagnosticsPrintf(diag, "Track ID [%u] size is too large [%ux%u]", track->id, track->width, track->height);
        return AVIF_FALSE;
    }

    // TODO: support scaling based on width/height track header info?

    track->id = trackID;
    return AVIF_TRUE;
}

static avifBool avifParseMediaHeaderBox(avifTrack * track, const uint8_t * raw, size_t rawLen, avifDiagnostics * diag)
{
    BEGIN_STREAM(s, raw, rawLen, diag, "Box[mdhd]");

    uint8_t version;
    CHECK(avifROStreamReadVersionAndFlags(&s, &version, NULL));

    uint32_t ignored32, mediaTimescale, mediaDuration32;
    uint64_t ignored64, mediaDuration64;
    if (version == 1) {
        CHECK(avifROStreamReadU64(&s, &ignored64));       // unsigned int(64) creation_time;
        CHECK(avifROStreamReadU64(&s, &ignored64));       // unsigned int(64) modification_time;
        CHECK(avifROStreamReadU32(&s, &mediaTimescale));  // unsigned int(32) timescale;
        CHECK(avifROStreamReadU64(&s, &mediaDuration64)); // unsigned int(64) duration;
        track->mediaDuration = mediaDuration64;
    } else if (version == 0) {
        CHECK(avifROStreamReadU32(&s, &ignored32));       // unsigned int(32) creation_time;
        CHECK(avifROStreamReadU32(&s, &ignored32));       // unsigned int(32) modification_time;
        CHECK(avifROStreamReadU32(&s, &mediaTimescale));  // unsigned int(32) timescale;
        CHECK(avifROStreamReadU32(&s, &mediaDuration32)); // unsigned int(32) duration;
        track->mediaDuration = (uint64_t)mediaDuration32;
    } else {
        // Unsupported version
        avifDiagnosticsPrintf(diag, "Box[mdhd] has an unsupported version [%u]", version);
        return AVIF_FALSE;
    }

    track->mediaTimescale = mediaTimescale;
    return AVIF_TRUE;
}

static avifBool avifParseChunkOffsetBox(avifSampleTable * sampleTable, avifBool largeOffsets, const uint8_t * raw, size_t rawLen, avifDiagnostics * diag)
{
    BEGIN_STREAM(s, raw, rawLen, diag, largeOffsets ? "Box[co64]" : "Box[stco]");

    CHECK(avifROStreamReadAndEnforceVersion(&s, 0));

    uint32_t entryCount;
    CHECK(avifROStreamReadU32(&s, &entryCount)); // unsigned int(32) entry_count;
    for (uint32_t i = 0; i < entryCount; ++i) {
        uint64_t offset;
        if (largeOffsets) {
            CHECK(avifROStreamReadU64(&s, &offset)); // unsigned int(32) chunk_offset;
        } else {
            uint32_t offset32;
            CHECK(avifROStreamReadU32(&s, &offset32)); // unsigned int(32) chunk_offset;
            offset = (uint64_t)offset32;
        }

        avifSampleTableChunk * chunk = (avifSampleTableChunk *)avifArrayPushPtr(&sampleTable->chunks);
        chunk->offset = offset;
    }
    return AVIF_TRUE;
}

static avifBool avifParseSampleToChunkBox(avifSampleTable * sampleTable, const uint8_t * raw, size_t rawLen, avifDiagnostics * diag)
{
    BEGIN_STREAM(s, raw, rawLen, diag, "Box[stsc]");

    CHECK(avifROStreamReadAndEnforceVersion(&s, 0));

    uint32_t entryCount;
    CHECK(avifROStreamReadU32(&s, &entryCount)); // unsigned int(32) entry_count;
    uint32_t prevFirstChunk = 0;
    for (uint32_t i = 0; i < entryCount; ++i) {
        avifSampleTableSampleToChunk * sampleToChunk = (avifSampleTableSampleToChunk *)avifArrayPushPtr(&sampleTable->sampleToChunks);
        CHECK(avifROStreamReadU32(&s, &sampleToChunk->firstChunk));             // unsigned int(32) first_chunk;
        CHECK(avifROStreamReadU32(&s, &sampleToChunk->samplesPerChunk));        // unsigned int(32) samples_per_chunk;
        CHECK(avifROStreamReadU32(&s, &sampleToChunk->sampleDescriptionIndex)); // unsigned int(32) sample_description_index;
        // The first_chunk fields should start with 1 and be strictly increasing.
        if (i == 0) {
            if (sampleToChunk->firstChunk != 1) {
                avifDiagnosticsPrintf(diag, "Box[stsc] does not begin with chunk 1 [%u]", sampleToChunk->firstChunk);
                return AVIF_FALSE;
            }
        } else {
            if (sampleToChunk->firstChunk <= prevFirstChunk) {
                avifDiagnosticsPrintf(diag, "Box[stsc] chunks are not strictly increasing");
                return AVIF_FALSE;
            }
        }
        prevFirstChunk = sampleToChunk->firstChunk;
    }
    return AVIF_TRUE;
}

static avifBool avifParseSampleSizeBox(avifSampleTable * sampleTable, const uint8_t * raw, size_t rawLen, avifDiagnostics * diag)
{
    BEGIN_STREAM(s, raw, rawLen, diag, "Box[stsz]");

    CHECK(avifROStreamReadAndEnforceVersion(&s, 0));

    uint32_t allSamplesSize, sampleCount;
    CHECK(avifROStreamReadU32(&s, &allSamplesSize)); // unsigned int(32) sample_size;
    CHECK(avifROStreamReadU32(&s, &sampleCount));    // unsigned int(32) sample_count;

    if (allSamplesSize > 0) {
        sampleTable->allSamplesSize = allSamplesSize;
    } else {
        for (uint32_t i = 0; i < sampleCount; ++i) {
            avifSampleTableSampleSize * sampleSize = (avifSampleTableSampleSize *)avifArrayPushPtr(&sampleTable->sampleSizes);
            CHECK(avifROStreamReadU32(&s, &sampleSize->size)); // unsigned int(32) entry_size;
        }
    }
    return AVIF_TRUE;
}

static avifBool avifParseSyncSampleBox(avifSampleTable * sampleTable, const uint8_t * raw, size_t rawLen, avifDiagnostics * diag)
{
    BEGIN_STREAM(s, raw, rawLen, diag, "Box[stss]");

    CHECK(avifROStreamReadAndEnforceVersion(&s, 0));

    uint32_t entryCount;
    CHECK(avifROStreamReadU32(&s, &entryCount)); // unsigned int(32) entry_count;

    for (uint32_t i = 0; i < entryCount; ++i) {
        uint32_t sampleNumber = 0;
        CHECK(avifROStreamReadU32(&s, &sampleNumber)); // unsigned int(32) sample_number;
        avifSyncSample * syncSample = (avifSyncSample *)avifArrayPushPtr(&sampleTable->syncSamples);
        syncSample->sampleNumber = sampleNumber;
    }
    return AVIF_TRUE;
}

static avifBool avifParseTimeToSampleBox(avifSampleTable * sampleTable, const uint8_t * raw, size_t rawLen, avifDiagnostics * diag)
{
    BEGIN_STREAM(s, raw, rawLen, diag, "Box[stts]");

    CHECK(avifROStreamReadAndEnforceVersion(&s, 0));

    uint32_t entryCount;
    CHECK(avifROStreamReadU32(&s, &entryCount)); // unsigned int(32) entry_count;

    for (uint32_t i = 0; i < entryCount; ++i) {
        avifSampleTableTimeToSample * timeToSample = (avifSampleTableTimeToSample *)avifArrayPushPtr(&sampleTable->timeToSamples);
        CHECK(avifROStreamReadU32(&s, &timeToSample->sampleCount)); // unsigned int(32) sample_count;
        CHECK(avifROStreamReadU32(&s, &timeToSample->sampleDelta)); // unsigned int(32) sample_delta;
    }
    return AVIF_TRUE;
}

static avifBool avifParseSampleDescriptionBox(avifSampleTable * sampleTable, const uint8_t * raw, size_t rawLen, avifDiagnostics * diag)
{
    BEGIN_STREAM(s, raw, rawLen, diag, "Box[stsd]");

    CHECK(avifROStreamReadAndEnforceVersion(&s, 0));

    uint32_t entryCount;
    CHECK(avifROStreamReadU32(&s, &entryCount)); // unsigned int(32) entry_count;

    for (uint32_t i = 0; i < entryCount; ++i) {
        avifBoxHeader sampleEntryHeader;
        CHECK(avifROStreamReadBoxHeader(&s, &sampleEntryHeader));

        avifSampleDescription * description = (avifSampleDescription *)avifArrayPushPtr(&sampleTable->sampleDescriptions);
        avifArrayCreate(&description->properties, sizeof(avifProperty), 16);
        memcpy(description->format, sampleEntryHeader.type, sizeof(description->format));
        size_t remainingBytes = avifROStreamRemainingBytes(&s);
        if (!memcmp(description->format, "av01", 4) && (remainingBytes > VISUALSAMPLEENTRY_SIZE)) {
            CHECK(avifParseItemPropertyContainerBox(&description->properties,
                                                    avifROStreamCurrent(&s) + VISUALSAMPLEENTRY_SIZE,
                                                    remainingBytes - VISUALSAMPLEENTRY_SIZE,
                                                    diag));
        }

        CHECK(avifROStreamSkip(&s, sampleEntryHeader.size));
    }
    return AVIF_TRUE;
}

static avifBool avifParseSampleTableBox(avifTrack * track, const uint8_t * raw, size_t rawLen, avifDiagnostics * diag)
{
    if (track->sampleTable) {
        // A TrackBox may only have one SampleTable
        avifDiagnosticsPrintf(diag, "Duplicate Box[stbl] for a single track detected");
        return AVIF_FALSE;
    }
    track->sampleTable = avifSampleTableCreate();

    BEGIN_STREAM(s, raw, rawLen, diag, "Box[stbl]");

    while (avifROStreamHasBytesLeft(&s, 1)) {
        avifBoxHeader header;
        CHECK(avifROStreamReadBoxHeader(&s, &header));

        if (!memcmp(header.type, "stco", 4)) {
            CHECK(avifParseChunkOffsetBox(track->sampleTable, AVIF_FALSE, avifROStreamCurrent(&s), header.size, diag));
        } else if (!memcmp(header.type, "co64", 4)) {
            CHECK(avifParseChunkOffsetBox(track->sampleTable, AVIF_TRUE, avifROStreamCurrent(&s), header.size, diag));
        } else if (!memcmp(header.type, "stsc", 4)) {
            CHECK(avifParseSampleToChunkBox(track->sampleTable, avifROStreamCurrent(&s), header.size, diag));
        } else if (!memcmp(header.type, "stsz", 4)) {
            CHECK(avifParseSampleSizeBox(track->sampleTable, avifROStreamCurrent(&s), header.size, diag));
        } else if (!memcmp(header.type, "stss", 4)) {
            CHECK(avifParseSyncSampleBox(track->sampleTable, avifROStreamCurrent(&s), header.size, diag));
        } else if (!memcmp(header.type, "stts", 4)) {
            CHECK(avifParseTimeToSampleBox(track->sampleTable, avifROStreamCurrent(&s), header.size, diag));
        } else if (!memcmp(header.type, "stsd", 4)) {
            CHECK(avifParseSampleDescriptionBox(track->sampleTable, avifROStreamCurrent(&s), header.size, diag));
        }

        CHECK(avifROStreamSkip(&s, header.size));
    }
    return AVIF_TRUE;
}

static avifBool avifParseMediaInformationBox(avifTrack * track, const uint8_t * raw, size_t rawLen, avifDiagnostics * diag)
{
    BEGIN_STREAM(s, raw, rawLen, diag, "Box[minf]");

    while (avifROStreamHasBytesLeft(&s, 1)) {
        avifBoxHeader header;
        CHECK(avifROStreamReadBoxHeader(&s, &header));

        if (!memcmp(header.type, "stbl", 4)) {
            CHECK(avifParseSampleTableBox(track, avifROStreamCurrent(&s), header.size, diag));
        }

        CHECK(avifROStreamSkip(&s, header.size));
    }
    return AVIF_TRUE;
}

static avifBool avifParseMediaBox(avifTrack * track, const uint8_t * raw, size_t rawLen, avifDiagnostics * diag)
{
    BEGIN_STREAM(s, raw, rawLen, diag, "Box[mdia]");

    while (avifROStreamHasBytesLeft(&s, 1)) {
        avifBoxHeader header;
        CHECK(avifROStreamReadBoxHeader(&s, &header));

        if (!memcmp(header.type, "mdhd", 4)) {
            CHECK(avifParseMediaHeaderBox(track, avifROStreamCurrent(&s), header.size, diag));
        } else if (!memcmp(header.type, "minf", 4)) {
            CHECK(avifParseMediaInformationBox(track, avifROStreamCurrent(&s), header.size, diag));
        }

        CHECK(avifROStreamSkip(&s, header.size));
    }
    return AVIF_TRUE;
}

static avifBool avifTrackReferenceBox(avifTrack * track, const uint8_t * raw, size_t rawLen, avifDiagnostics * diag)
{
    BEGIN_STREAM(s, raw, rawLen, diag, "Box[tref]");

    while (avifROStreamHasBytesLeft(&s, 1)) {
        avifBoxHeader header;
        CHECK(avifROStreamReadBoxHeader(&s, &header));

        if (!memcmp(header.type, "auxl", 4)) {
            uint32_t toID;
            CHECK(avifROStreamReadU32(&s, &toID));                       // unsigned int(32) track_IDs[];
            CHECK(avifROStreamSkip(&s, header.size - sizeof(uint32_t))); // just take the first one
            track->auxForID = toID;
        } else if (!memcmp(header.type, "prem", 4)) {
            uint32_t byID;
            CHECK(avifROStreamReadU32(&s, &byID));                       // unsigned int(32) track_IDs[];
            CHECK(avifROStreamSkip(&s, header.size - sizeof(uint32_t))); // just take the first one
            track->premByID = byID;
        } else {
            CHECK(avifROStreamSkip(&s, header.size));
        }
    }
    return AVIF_TRUE;
}

static avifBool avifParseTrackBox(avifDecoderData * data, const uint8_t * raw, size_t rawLen, uint32_t imageSizeLimit)
{
    BEGIN_STREAM(s, raw, rawLen, data->diag, "Box[trak]");

    avifTrack * track = avifDecoderDataCreateTrack(data);

    while (avifROStreamHasBytesLeft(&s, 1)) {
        avifBoxHeader header;
        CHECK(avifROStreamReadBoxHeader(&s, &header));

        if (!memcmp(header.type, "tkhd", 4)) {
            CHECK(avifParseTrackHeaderBox(track, avifROStreamCurrent(&s), header.size, imageSizeLimit, data->diag));
        } else if (!memcmp(header.type, "meta", 4)) {
            CHECK(avifParseMetaBox(track->meta, avifROStreamCurrent(&s), header.size, data->diag));
        } else if (!memcmp(header.type, "mdia", 4)) {
            CHECK(avifParseMediaBox(track, avifROStreamCurrent(&s), header.size, data->diag));
        } else if (!memcmp(header.type, "tref", 4)) {
            CHECK(avifTrackReferenceBox(track, avifROStreamCurrent(&s), header.size, data->diag));
        }

        CHECK(avifROStreamSkip(&s, header.size));
    }
    return AVIF_TRUE;
}

static avifBool avifParseMovieBox(avifDecoderData * data, const uint8_t * raw, size_t rawLen, uint32_t imageSizeLimit)
{
    BEGIN_STREAM(s, raw, rawLen, data->diag, "Box[moov]");

    while (avifROStreamHasBytesLeft(&s, 1)) {
        avifBoxHeader header;
        CHECK(avifROStreamReadBoxHeader(&s, &header));

        if (!memcmp(header.type, "trak", 4)) {
            CHECK(avifParseTrackBox(data, avifROStreamCurrent(&s), header.size, imageSizeLimit));
        }

        CHECK(avifROStreamSkip(&s, header.size));
    }
    return AVIF_TRUE;
}

static avifBool avifParseFileTypeBox(avifFileType * ftyp, const uint8_t * raw, size_t rawLen, avifDiagnostics * diag)
{
    BEGIN_STREAM(s, raw, rawLen, diag, "Box[ftyp]");

    CHECK(avifROStreamRead(&s, ftyp->majorBrand, 4));
    CHECK(avifROStreamReadU32(&s, &ftyp->minorVersion));

    size_t compatibleBrandsBytes = avifROStreamRemainingBytes(&s);
    if ((compatibleBrandsBytes % 4) != 0) {
        avifDiagnosticsPrintf(diag, "Box[ftyp] contains a compatible brands section that isn't divisible by 4 [%zu]", compatibleBrandsBytes);
        return AVIF_FALSE;
    }
    ftyp->compatibleBrands = avifROStreamCurrent(&s);
    CHECK(avifROStreamSkip(&s, compatibleBrandsBytes));
    ftyp->compatibleBrandsCount = (int)compatibleBrandsBytes / 4;

    return AVIF_TRUE;
}

static avifResult avifParse(avifDecoder * decoder)
{
    // Note: this top-level function is the only avifParse*() function that returns avifResult instead of avifBool.
    // Be sure to use CHECKERR() in this function with an explicit error result instead of simply using CHECK().

    avifResult readResult;
    uint64_t parseOffset = 0;
    avifDecoderData * data = decoder->data;
    avifBool ftypSeen = AVIF_FALSE;
    avifBool metaSeen = AVIF_FALSE;
    avifBool moovSeen = AVIF_FALSE;
    avifBool needsMeta = AVIF_FALSE;
    avifBool needsMoov = AVIF_FALSE;

    for (;;) {
        // Read just enough to get the next box header (a max of 32 bytes)
        avifROData headerContents;
        if ((decoder->io->sizeHint > 0) && (parseOffset > decoder->io->sizeHint)) {
            return AVIF_RESULT_BMFF_PARSE_FAILED;
        }
        readResult = decoder->io->read(decoder->io, 0, parseOffset, 32, &headerContents);
        if (readResult != AVIF_RESULT_OK) {
            return readResult;
        }
        if (!headerContents.size) {
            // If we got AVIF_RESULT_OK from the reader but received 0 bytes,
            // we've reached the end of the file with no errors. Hooray!
            break;
        }

        // Parse the header, and find out how many bytes it actually was
        BEGIN_STREAM(headerStream, headerContents.data, headerContents.size, &decoder->diag, "File-level box header");
        avifBoxHeader header;
        CHECKERR(avifROStreamReadBoxHeaderPartial(&headerStream, &header), AVIF_RESULT_BMFF_PARSE_FAILED);
        parseOffset += headerStream.offset;
        assert((decoder->io->sizeHint == 0) || (parseOffset <= decoder->io->sizeHint));

        // Try to get the remainder of the box, if necessary
        avifROData boxContents = AVIF_DATA_EMPTY;

        // TODO: reorg this code to only do these memcmps once each
        if (!memcmp(header.type, "ftyp", 4) || !memcmp(header.type, "meta", 4) || !memcmp(header.type, "moov", 4)) {
            readResult = decoder->io->read(decoder->io, 0, parseOffset, header.size, &boxContents);
            if (readResult != AVIF_RESULT_OK) {
                return readResult;
            }
            if (boxContents.size != header.size) {
                // A truncated box, bail out
                return AVIF_RESULT_TRUNCATED_DATA;
            }
        } else if (header.size > (UINT64_MAX - parseOffset)) {
            return AVIF_RESULT_BMFF_PARSE_FAILED;
        }
        parseOffset += header.size;

        if (!memcmp(header.type, "ftyp", 4)) {
            CHECKERR(!ftypSeen, AVIF_RESULT_BMFF_PARSE_FAILED);
            avifFileType ftyp;
            CHECKERR(avifParseFileTypeBox(&ftyp, boxContents.data, boxContents.size, data->diag), AVIF_RESULT_BMFF_PARSE_FAILED);
            if (!avifFileTypeIsCompatible(&ftyp)) {
                return AVIF_RESULT_INVALID_FTYP;
            }
            ftypSeen = AVIF_TRUE;
            memcpy(data->majorBrand, ftyp.majorBrand, 4); // Remember the major brand for future AVIF_DECODER_SOURCE_AUTO decisions
            needsMeta = avifFileTypeHasBrand(&ftyp, "avif");
            needsMoov = avifFileTypeHasBrand(&ftyp, "avis");
        } else if (!memcmp(header.type, "meta", 4)) {
            CHECKERR(!metaSeen, AVIF_RESULT_BMFF_PARSE_FAILED);
            CHECKERR(avifParseMetaBox(data->meta, boxContents.data, boxContents.size, data->diag), AVIF_RESULT_BMFF_PARSE_FAILED);
            metaSeen = AVIF_TRUE;
        } else if (!memcmp(header.type, "moov", 4)) {
            CHECKERR(!moovSeen, AVIF_RESULT_BMFF_PARSE_FAILED);
            CHECKERR(avifParseMovieBox(data, boxContents.data, boxContents.size, decoder->imageSizeLimit), AVIF_RESULT_BMFF_PARSE_FAILED);
            moovSeen = AVIF_TRUE;
        }

        // See if there is enough information to consider Parse() a success and early-out:
        // * If the brand 'avif' is present, require a meta box
        // * If the brand 'avis' is present, require a moov box
        if (ftypSeen && (!needsMeta || metaSeen) && (!needsMoov || moovSeen)) {
            return AVIF_RESULT_OK;
        }
    }
    if (!ftypSeen) {
        return AVIF_RESULT_INVALID_FTYP;
    }
    if ((needsMeta && !metaSeen) || (needsMoov && !moovSeen)) {
        return AVIF_RESULT_TRUNCATED_DATA;
    }
    return AVIF_RESULT_OK;
}

// ---------------------------------------------------------------------------

avifBool avifPeekCompatibleFileType(const avifROData * input)
{
    BEGIN_STREAM(s, input->data, input->size, NULL, NULL);

    avifBoxHeader header;
    CHECK(avifROStreamReadBoxHeader(&s, &header));
    if (memcmp(header.type, "ftyp", 4)) {
        return AVIF_FALSE;
    }

    avifFileType ftyp;
    memset(&ftyp, 0, sizeof(avifFileType));
    avifBool parsed = avifParseFileTypeBox(&ftyp, avifROStreamCurrent(&s), header.size, NULL);
    if (!parsed) {
        return AVIF_FALSE;
    }
    return avifFileTypeIsCompatible(&ftyp);
}

// ---------------------------------------------------------------------------

avifResult avifDecoderParse(avifDecoder * decoder)
{
    avifDiagnosticsClearError(&decoder->diag);

    // An imageSizeLimit greater than AVIF_DEFAULT_IMAGE_SIZE_LIMIT and the special value of 0 to
    // disable the limit are not yet implemented.
    if ((decoder->imageSizeLimit > AVIF_DEFAULT_IMAGE_SIZE_LIMIT) || (decoder->imageSizeLimit == 0)) {
        return AVIF_RESULT_NOT_IMPLEMENTED;
    }
    if (!decoder->io || !decoder->io->read) {
        return AVIF_RESULT_IO_NOT_SET;
    }

    // Cleanup anything lingering in the decoder
    avifDecoderCleanup(decoder);

    // -----------------------------------------------------------------------
    // Parse BMFF boxes

    decoder->data = avifDecoderDataCreate();
    decoder->data->diag = &decoder->diag;

    avifResult parseResult = avifParse(decoder);
    if (parseResult != AVIF_RESULT_OK) {
        return parseResult;
    }

    // Walk the decoded items (if any) and harvest ispe
    avifDecoderData * data = decoder->data;
    for (uint32_t itemIndex = 0; itemIndex < data->meta->items.count; ++itemIndex) {
        avifDecoderItem * item = &data->meta->items.item[itemIndex];
        if (!item->size) {
            continue;
        }
        if (item->hasUnsupportedEssentialProperty) {
            // An essential property isn't supported by libavif; ignore the item.
            continue;
        }
        avifBool isGrid = (memcmp(item->type, "grid", 4) == 0);
        if (memcmp(item->type, "av01", 4) && !isGrid) {
            // probably exif or some other data
            continue;
        }

        const avifProperty * ispeProp = avifPropertyArrayFind(&item->properties, "ispe");
        if (ispeProp) {
            item->width = ispeProp->u.ispe.width;
            item->height = ispeProp->u.ispe.height;

            if ((item->width == 0) || (item->height == 0)) {
                avifDiagnosticsPrintf(data->diag, "Item ID [%u] has an invalid size [%ux%u]", item->id, item->width, item->height);
                return AVIF_RESULT_BMFF_PARSE_FAILED;
            }
            if (item->width > (decoder->imageSizeLimit / item->height)) {
                avifDiagnosticsPrintf(data->diag, "Item ID [%u] size is too large [%ux%u]", item->id, item->width, item->height);
                return AVIF_RESULT_BMFF_PARSE_FAILED;
            }
        } else {
            const avifProperty * auxCProp = avifPropertyArrayFind(&item->properties, "auxC");
            if (auxCProp && isAlphaURN(auxCProp->u.auxC.auxType)) {
                if (decoder->strictFlags & AVIF_STRICT_ALPHA_ISPE_REQUIRED) {
                    avifDiagnosticsPrintf(data->diag,
                                          "[Strict] Alpha auxiliary image item ID [%u] is missing a mandatory ispe property",
                                          item->id);
                    return AVIF_RESULT_BMFF_PARSE_FAILED;
                }
            } else {
                avifDiagnosticsPrintf(data->diag, "Item ID [%u] is missing a mandatory ispe property", item->id);
                return AVIF_RESULT_BMFF_PARSE_FAILED;
            }
        }
    }
    return avifDecoderReset(decoder);
}
