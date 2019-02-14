// Copyright 2019 Joe Drago. All rights reserved.
// SPDX-License-Identifier: BSD-2-Clause

#include "avif/internal.h"

#include <string.h>

// from the MIAF spec:
// ---
// Section 6.7
// "α is an alpha plane value, scaled into the range of 0 (fully transparent) to 1 (fully opaque), inclusive"
// ---
// Section 7.3.5.2
// "the sample values of the alpha plane divided by the maximum value (e.g. by 255 for 8-bit sample
// values) provides the multiplier to be used to obtain the intensity for the associated master image"
// ---
// The define AVIF_FIX_STUDIO_ALPHA detects when the alpha OBU is incorrectly using studio range
// and corrects it before returning the alpha pixels to the caller.
#define AVIF_FIX_STUDIO_ALPHA

#define AUXTYPE_SIZE 64
#define MAX_COMPATIBLE_BRANDS 32
#define MAX_ITEMS 8
#define MAX_PROPERTIES 24

// ---------------------------------------------------------------------------
// Box data structures

// ftyp
typedef struct avifFileType
{
    uint8_t majorBrand[4];
    uint32_t minorVersion;
    uint8_t compatibleBrands[4 * MAX_COMPATIBLE_BRANDS];
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

// colr
typedef struct avifColourInformationBox
{
    avifProfileFormat format;
    uint8_t * icc;
    size_t iccSize;
    avifNclxColorProfile nclx;
} avifColourInformationBox;

// ---------------------------------------------------------------------------
// Top-level structures

// one "item" worth (all iref, iloc, iprp, etc refer to one of these)
typedef struct avifItem
{
    int id;
    uint8_t type[4];
    uint32_t offset;
    uint32_t size;
    avifBool ispePresent;
    avifImageSpatialExtents ispe;
    avifBool auxCPresent;
    avifAuxiliaryType auxC;
    avifBool colrPresent;
    avifColourInformationBox colr;
    int thumbnailForID; // if non-zero, this item is a thumbnail for Item #{thumbnailForID}
    int auxForID;       // if non-zero, this item is an auxC plane for Item #{auxForID}
} avifItem;

// Temporary storage for ipco contents until they can be associated and memcpy'd to an avifItem
typedef struct avifProperty
{
    uint8_t type[4];
    avifImageSpatialExtents ispe;
    avifAuxiliaryType auxC;
    avifColourInformationBox colr;
} avifProperty;

typedef struct avifData
{
    // TODO: Everything in here using a MAX_* constant is a bit lazy; it should all be dynamic

    avifFileType ftyp;
    avifItem items[MAX_ITEMS];
    avifProperty properties[MAX_PROPERTIES];
    int propertyCount;
} avifData;

int findItemID(avifData * data, int itemID)
{
    if (itemID == 0) {
        return -1;
    }

    for (int i = 0; i < MAX_ITEMS; ++i) {
        if (data->items[i].id == itemID) {
            return i;
        }
    }

    for (int i = 0; i < MAX_ITEMS; ++i) {
        if (data->items[i].id == 0) {
            data->items[i].id = itemID;
            return i;
        }
    }

    return -1;
}

// ---------------------------------------------------------------------------
// URN

static avifBool isAlphaURN(char * urn)
{
    if (!strcmp(urn, URN_ALPHA0)) return AVIF_TRUE;
    if (!strcmp(urn, URN_ALPHA1)) return AVIF_TRUE;
    return AVIF_FALSE;
}

// ---------------------------------------------------------------------------
// BMFF Parsing

#define BEGIN_STREAM(VARNAME, PTR, SIZE)             \
    avifStream VARNAME;                              \
    avifRawData VARNAME ## _rawData = { PTR, SIZE }; \
    avifStreamStart(&VARNAME, &VARNAME ## _rawData)

static avifBool avifParseItemLocationBox(avifData * data, uint8_t * raw, size_t rawLen)
{
    BEGIN_STREAM(s, raw, rawLen);

    CHECK(avifStreamReadAndEnforceVersion(&s, 0));

    uint8_t offsetSizeAndLengthSize;
    CHECK(avifStreamRead(&s, &offsetSizeAndLengthSize, 1));
    uint8_t offsetSize = (offsetSizeAndLengthSize >> 4) & 0xf; // unsigned int(4) offset_size;
    uint8_t lengthSize = (offsetSizeAndLengthSize >> 0) & 0xf; // unsigned int(4) length_size;

    uint8_t baseOffsetSizeAndReserved;
    CHECK(avifStreamRead(&s, &baseOffsetSizeAndReserved, 1));
    uint8_t baseOffsetSize = (baseOffsetSizeAndReserved >> 4) & 0xf; // unsigned int(4) base_offset_size;

    uint16_t itemCount;
    CHECK(avifStreamReadU16(&s, &itemCount)); // unsigned int(16) item_count;
    for (int i = 0; i < itemCount; ++i) {
        uint16_t itemID;                                           // unsigned int(16) item_ID;
        CHECK(avifStreamReadU16(&s, &itemID));                     //
        uint16_t dataReferenceIndex;                               // unsigned int(16) data_reference_index;
        CHECK(avifStreamReadU16(&s, &dataReferenceIndex));         //
        uint64_t baseOffset;                                       // unsigned int(base_offset_size*8) base_offset;
        CHECK(avifStreamReadUX8(&s, &baseOffset, baseOffsetSize)); //
        uint16_t extentCount;                                      // unsigned int(16) extent_count;
        CHECK(avifStreamReadU16(&s, &extentCount));                //
        if (extentCount == 1) {
            uint64_t extentOffset; // unsigned int(offset_size*8) extent_offset;
            CHECK(avifStreamReadUX8(&s, &extentOffset, offsetSize));
            uint64_t extentLength; // unsigned int(offset_size*8) extent_length;
            CHECK(avifStreamReadUX8(&s, &extentLength, lengthSize));

            int itemIndex = findItemID(data, itemID);
            if (itemIndex == -1) {
                return AVIF_FALSE;
            }
            data->items[itemIndex].id = itemID;
            data->items[itemIndex].offset = (uint32_t)(baseOffset + extentOffset);
            data->items[itemIndex].size = (uint32_t)extentLength;
        } else {
            // TODO: support more than one extent
            return AVIF_FALSE;
        }
    }
    return AVIF_TRUE;
}

static avifBool avifParseImageSpatialExtentsProperty(avifData * data, uint8_t * raw, size_t rawLen, int propertyIndex)
{
    BEGIN_STREAM(s, raw, rawLen);
    CHECK(avifStreamReadAndEnforceVersion(&s, 0));

    CHECK(avifStreamReadU32(&s, &data->properties[propertyIndex].ispe.width));
    CHECK(avifStreamReadU32(&s, &data->properties[propertyIndex].ispe.height));
    return AVIF_TRUE;
}

static avifBool avifParseAuxiliaryTypeProperty(avifData * data, uint8_t * raw, size_t rawLen, int propertyIndex)
{
    BEGIN_STREAM(s, raw, rawLen);
    CHECK(avifStreamReadAndEnforceVersion(&s, 0));

    CHECK(avifStreamReadString(&s, data->properties[propertyIndex].auxC.auxType, AUXTYPE_SIZE));
    return AVIF_TRUE;
}

static avifBool avifParseColourInformationBox(avifData * data, uint8_t * raw, size_t rawLen, int propertyIndex)
{
    BEGIN_STREAM(s, raw, rawLen);

    data->properties[propertyIndex].colr.format = AVIF_PROFILE_FORMAT_NONE;

    uint8_t colourType[4]; // unsigned int(32) colour_type;
    CHECK(avifStreamRead(&s, colourType, 4));
    if (!memcmp(colourType, "rICC", 4) || !memcmp(colourType, "prof", 4)) {
        data->properties[propertyIndex].colr.format = AVIF_PROFILE_FORMAT_ICC;
        data->properties[propertyIndex].colr.icc = avifStreamCurrent(&s);
        data->properties[propertyIndex].colr.iccSize = avifStreamRemainingBytes(&s);
    } else if (!memcmp(colourType, "nclx", 4)) {
        CHECK(avifStreamReadU16(&s, &data->properties[propertyIndex].colr.nclx.colourPrimaries));         // unsigned int(16) colour_primaries;
        CHECK(avifStreamReadU16(&s, &data->properties[propertyIndex].colr.nclx.transferCharacteristics)); // unsigned int(16) transfer_characteristics;
        CHECK(avifStreamReadU16(&s, &data->properties[propertyIndex].colr.nclx.matrixCoefficients));      // unsigned int(16) matrix_coefficients;
        CHECK(avifStreamRead(&s, &data->properties[propertyIndex].colr.nclx.fullRangeFlag, 1));           // unsigned int(16) matrix_coefficients;
        data->properties[propertyIndex].colr.nclx.fullRangeFlag |= 0x80;
        data->properties[propertyIndex].colr.format = AVIF_PROFILE_FORMAT_NCLX;
    }
    return AVIF_TRUE;
}

static avifBool avifParseItemPropertyContainerBox(avifData * data, uint8_t * raw, size_t rawLen)
{
    BEGIN_STREAM(s, raw, rawLen);

    data->propertyCount = 0;

    while (avifStreamHasBytesLeft(&s, 1)) {
        avifBoxHeader header;
        CHECK(avifStreamReadBoxHeader(&s, &header));

        if (data->propertyCount >= MAX_PROPERTIES) {
            return AVIF_FALSE;
        }
        int propertyIndex = data->propertyCount;
        ++data->propertyCount;

        memcpy(data->properties[propertyIndex].type, header.type, 4);
        if (!memcmp(header.type, "ispe", 4)) {
            CHECK(avifParseImageSpatialExtentsProperty(data, avifStreamCurrent(&s), header.size, propertyIndex));
        }
        if (!memcmp(header.type, "auxC", 4)) {
            CHECK(avifParseAuxiliaryTypeProperty(data, avifStreamCurrent(&s), header.size, propertyIndex));
        }
        if (!memcmp(header.type, "colr", 4)) {
            CHECK(avifParseColourInformationBox(data, avifStreamCurrent(&s), header.size, propertyIndex));
        }

        CHECK(avifStreamSkip(&s, header.size));
    }
    return AVIF_TRUE;
}

static avifBool avifParseItemPropertyAssociation(avifData * data, uint8_t * raw, size_t rawLen)
{
    BEGIN_STREAM(s, raw, rawLen);

    uint8_t version;
    uint8_t flags[3];
    CHECK(avifStreamReadVersionAndFlags(&s, &version, flags));
    avifBool propertyIndexIsU16 = (flags[2] & 0x1) ? AVIF_TRUE : AVIF_FALSE; // is flags[2] correct?

    uint32_t entryCount;
    CHECK(avifStreamReadU32(&s, &entryCount));
    for (uint32_t entryIndex = 0; entryIndex < entryCount; ++entryIndex) {
        unsigned int itemID;
        if (version < 1) {
            uint16_t tmp;
            CHECK(avifStreamReadU16(&s, &tmp));
            itemID = tmp;
        } else {
            CHECK(avifStreamReadU32(&s, &itemID));
        }
        uint8_t associationCount;
        CHECK(avifStreamRead(&s, &associationCount, 1));
        for (uint8_t associationIndex = 0; associationIndex < associationCount; ++associationIndex) {
            avifBool essential = AVIF_FALSE;
            uint16_t propertyIndex = 0;
            if (propertyIndexIsU16) {
                CHECK(avifStreamReadU16(&s, &propertyIndex));
                essential = (propertyIndex & 0x8000) ? AVIF_TRUE : AVIF_FALSE;
                propertyIndex &= 0x7fff;
            } else {
                uint8_t tmp;
                CHECK(avifStreamRead(&s, &tmp, 1));
                essential = (tmp & 0x80) ? AVIF_TRUE : AVIF_FALSE;
                propertyIndex = tmp & 0x7f;
            }

            if (propertyIndex == 0) {
                // Not associated with any item
                continue;
            }
            --propertyIndex; // 1-indexed

            if (propertyIndex >= MAX_PROPERTIES) {
                return AVIF_FALSE;
            }

            int itemIndex = findItemID(data, itemID);
            if (itemIndex == -1) {
                return AVIF_FALSE;
            }

            // Associate property with item
            avifProperty * prop = &data->properties[propertyIndex];
            if (!memcmp(prop->type, "ispe", 4)) {
                data->items[itemIndex].ispePresent = AVIF_TRUE;
                memcpy(&data->items[itemIndex].ispe, &prop->ispe, sizeof(avifImageSpatialExtents));
            } else if (!memcmp(prop->type, "auxC", 4)) {
                data->items[itemIndex].auxCPresent = AVIF_TRUE;
                memcpy(&data->items[itemIndex].auxC, &prop->auxC, sizeof(avifAuxiliaryType));
            } else if (!memcmp(prop->type, "colr", 4)) {
                data->items[itemIndex].colrPresent = AVIF_TRUE;
                memcpy(&data->items[itemIndex].colr, &prop->colr, sizeof(avifColourInformationBox));
            }
        }
    }

    return AVIF_TRUE;
}

static avifBool avifParseItemPropertiesBox(avifData * data, uint8_t * raw, size_t rawLen)
{
    BEGIN_STREAM(s, raw, rawLen);

    avifBoxHeader ipcoHeader;
    CHECK(avifStreamReadBoxHeader(&s, &ipcoHeader));
    if (memcmp(ipcoHeader.type, "ipco", 4) != 0) {
        return AVIF_FALSE;
    }

    // Read all item properties inside of ItemPropertyContainerBox
    CHECK(avifParseItemPropertyContainerBox(data, avifStreamCurrent(&s), ipcoHeader.size));
    CHECK(avifStreamSkip(&s, ipcoHeader.size));

    // Now read all ItemPropertyAssociation until the end of the box, and make associations
    while (avifStreamHasBytesLeft(&s, 1)) {
        avifBoxHeader ipmaHeader;
        CHECK(avifStreamReadBoxHeader(&s, &ipmaHeader));

        if (!memcmp(ipmaHeader.type, "ipma", 4)) {
            CHECK(avifParseItemPropertyAssociation(data, avifStreamCurrent(&s), ipmaHeader.size));
        } else {
            // These must all be type ipma
            return AVIF_FALSE;
        }

        CHECK(avifStreamSkip(&s, ipmaHeader.size));
    }
    return AVIF_TRUE;
}

static avifBool avifParseItemInfoEntry(avifData * data, uint8_t * raw, size_t rawLen)
{
    BEGIN_STREAM(s, raw, rawLen);

    CHECK(avifStreamReadAndEnforceVersion(&s, 2)); // TODO: support version > 2? 2+ is required for item_type

    uint16_t itemID;                                    // unsigned int(16) item_ID;
    CHECK(avifStreamReadU16(&s, &itemID));              //
    uint16_t itemProtectionIndex;                       // unsigned int(16) item_protection_index;
    CHECK(avifStreamReadU16(&s, &itemProtectionIndex)); //
    uint8_t itemType[4];                                // unsigned int(32) item_type;
    CHECK(avifStreamRead(&s, itemType, 4));             //

    int itemIndex = findItemID(data, itemID);
    if (itemIndex == -1) {
        return AVIF_FALSE;
    }
    memcpy(data->items[itemIndex].type, itemType, sizeof(itemType));

    return AVIF_TRUE;
}

static avifBool avifParseItemInfoBox(avifData * data, uint8_t * raw, size_t rawLen)
{
    BEGIN_STREAM(s, raw, rawLen);

    uint8_t version;
    CHECK(avifStreamReadVersionAndFlags(&s, &version, NULL));
    uint32_t entryCount;
    if (version == 0) {
        uint16_t tmp;
        CHECK(avifStreamReadU16(&s, &tmp)); // unsigned int(16) entry_count;
        entryCount = tmp;
    } else if (version == 1) {
        CHECK(avifStreamReadU32(&s, &entryCount)); // unsigned int(16) entry_count;
    } else {
        return AVIF_FALSE;
    }

    for (uint32_t entryIndex = 0; entryIndex < entryCount; ++entryIndex) {
        avifBoxHeader infeHeader;
        CHECK(avifStreamReadBoxHeader(&s, &infeHeader));

        if (!memcmp(infeHeader.type, "infe", 4)) {
            CHECK(avifParseItemInfoEntry(data, avifStreamCurrent(&s), infeHeader.size));
        } else {
            // These must all be type ipma
            return AVIF_FALSE;
        }

        CHECK(avifStreamSkip(&s, infeHeader.size));
    }

    return AVIF_TRUE;
}

static avifBool avifParseItemReferenceBox(avifData * data, uint8_t * raw, size_t rawLen)
{
    BEGIN_STREAM(s, raw, rawLen);

    uint8_t version;
    CHECK(avifStreamReadVersionAndFlags(&s, &version, NULL));

    while (avifStreamHasBytesLeft(&s, 1)) {
        avifBoxHeader irefHeader;
        CHECK(avifStreamReadBoxHeader(&s, &irefHeader));

        uint32_t fromID = 0;
        if (version == 0) {
            uint16_t tmp;
            CHECK(avifStreamReadU16(&s, &tmp)); // unsigned int(16) from_item_ID;
            fromID = tmp;
        } else if (version == 1) {
            CHECK(avifStreamReadU32(&s, &fromID)); //  unsigned int(32) from_item_ID;
        } else {
            // unsupported iref version, skip it
            break;
        }

        uint16_t referenceCount = 0;
        CHECK(avifStreamReadU16(&s, &referenceCount)); // unsigned int(16) reference_count;

        for (uint16_t refIndex = 0; refIndex < referenceCount; ++refIndex) {
            uint32_t toID = 0;
            if (version == 0) {
                uint16_t tmp;
                CHECK(avifStreamReadU16(&s, &tmp)); // unsigned int(16) to_item_ID;
                toID = tmp;
            } else if (version == 1) {
                CHECK(avifStreamReadU32(&s, &toID)); //  unsigned int(32) to_item_ID;
            } else {
                // unsupported iref version, skip it
                break;
            }

            // Read this reference as "{fromID} is a {irefType} for {toID}"
            if (fromID && toID) {
                int itemIndex = findItemID(data, fromID);
                if (itemIndex == -1) {
                    return AVIF_FALSE;
                }
                if (!memcmp(irefHeader.type, "thmb", 4)) {
                    data->items[itemIndex].thumbnailForID = toID;
                }
                if (!memcmp(irefHeader.type, "auxl", 4)) {
                    data->items[itemIndex].auxForID = toID;
                }
            }
        }
    }

    return AVIF_TRUE;
}

static avifBool avifParseMetaBox(avifData * data, uint8_t * raw, size_t rawLen)
{
    BEGIN_STREAM(s, raw, rawLen);

    CHECK(avifStreamReadAndEnforceVersion(&s, 0));

    while (avifStreamHasBytesLeft(&s, 1)) {
        avifBoxHeader header;
        CHECK(avifStreamReadBoxHeader(&s, &header));

        if (!memcmp(header.type, "iloc", 4)) {
            CHECK(avifParseItemLocationBox(data, avifStreamCurrent(&s), header.size));
        } else if (!memcmp(header.type, "iprp", 4)) {
            CHECK(avifParseItemPropertiesBox(data, avifStreamCurrent(&s), header.size));
        } else if (!memcmp(header.type, "iinf", 4)) {
            CHECK(avifParseItemInfoBox(data, avifStreamCurrent(&s), header.size));
        } else if (!memcmp(header.type, "iref", 4)) {
            CHECK(avifParseItemReferenceBox(data, avifStreamCurrent(&s), header.size));
        }

        CHECK(avifStreamSkip(&s, header.size));
    }
    return AVIF_TRUE;
}

static avifBool avifParseFileTypeBox(avifData * data, uint8_t * raw, size_t rawLen)
{
    BEGIN_STREAM(s, raw, rawLen);

    CHECK(avifStreamRead(&s, data->ftyp.majorBrand, 4));
    CHECK(avifStreamReadU32(&s, &data->ftyp.minorVersion));

    size_t compatibleBrandsBytes = avifStreamRemainingBytes(&s);
    if ((compatibleBrandsBytes % 4) != 0) {
        return AVIF_FALSE;
    }
    if (compatibleBrandsBytes > (4 * MAX_COMPATIBLE_BRANDS)) {
        // TODO: stop clamping and resize this
        compatibleBrandsBytes = (4 * MAX_COMPATIBLE_BRANDS);
    }
    CHECK(avifStreamRead(&s, data->ftyp.compatibleBrands, compatibleBrandsBytes));
    data->ftyp.compatibleBrandsCount = (int)compatibleBrandsBytes / 4;

    return AVIF_TRUE;
}

static avifBool avifParse(avifData * data, uint8_t * raw, size_t rawLen)
{
    BEGIN_STREAM(s, raw, rawLen);

    while (avifStreamHasBytesLeft(&s, 1)) {
        avifBoxHeader header;
        CHECK(avifStreamReadBoxHeader(&s, &header));

        if (!memcmp(header.type, "ftyp", 4)) {
            CHECK(avifParseFileTypeBox(data, avifStreamCurrent(&s), header.size));
        } else if (!memcmp(header.type, "meta", 4)) {
            CHECK(avifParseMetaBox(data, avifStreamCurrent(&s), header.size));
        }

        CHECK(avifStreamSkip(&s, header.size));
    }
    return AVIF_TRUE;
}

// ---------------------------------------------------------------------------

avifResult avifImageRead(avifImage * image, avifRawData * input)
{
    avifCodec * codec = NULL;

    // -----------------------------------------------------------------------
    // Parse BMFF boxes

    avifData data;
    memset(&data, 0, sizeof(data));
    if (!avifParse(&data, input->data, input->size)) {
        return AVIF_RESULT_BMFF_PARSE_FAILED;
    }

    avifBool avifCompatible = (memcmp(data.ftyp.majorBrand, "avif", 4) == 0) ? AVIF_TRUE : AVIF_FALSE;
    if (!avifCompatible) {
        for (int compatibleBrandIndex = 0; compatibleBrandIndex < data.ftyp.compatibleBrandsCount; ++compatibleBrandIndex) {
            uint8_t * compatibleBrand = &data.ftyp.compatibleBrands[4 * compatibleBrandIndex];
            if (!memcmp(compatibleBrand, "avif", 4)) {
                avifCompatible = AVIF_TRUE;
                break;
            }
        }
    }
    if (!avifCompatible) {
        return AVIF_RESULT_INVALID_FTYP;
    }

    // -----------------------------------------------------------------------

    avifRawData colorOBU = AVIF_RAW_DATA_EMPTY;
    avifRawData alphaOBU = AVIF_RAW_DATA_EMPTY;
    avifItem * colorOBUItem = NULL;
    avifItem * alphaOBUItem = NULL;

    // Find the colorOBU item
    for (int itemIndex = 0; itemIndex < MAX_ITEMS; ++itemIndex) {
        avifItem * item = &data.items[itemIndex];
        if (!item->id || !item->size) {
            break;
        }
        if (item->offset > input->size) {
            break;
        }
        if ((item->offset + item->size) > input->size) {
            break;
        }
        if (memcmp(item->type, "av01", 4)) {
            // probably exif or some other data
            continue;
        }
        if (item->thumbnailForID != 0) {
            // It's a thumbnail, skip it
            continue;
        }

        colorOBUItem = item;
        colorOBU.data = input->data + item->offset;
        colorOBU.size = item->size;
        break;
    }

    // Find the alphaOBU item, if any
    if (colorOBUItem) {
        for (int itemIndex = 0; itemIndex < MAX_ITEMS; ++itemIndex) {
            avifItem * item = &data.items[itemIndex];
            if (!item->id || !item->size) {
                break;
            }
            if (item->offset > input->size) {
                break;
            }
            if ((item->offset + item->size) > input->size) {
                break;
            }
            if (memcmp(item->type, "av01", 4)) {
                // probably exif or some other data
                continue;
            }
            if (item->thumbnailForID != 0) {
                // It's a thumbnail, skip it
                continue;
            }

            if (isAlphaURN(item->auxC.auxType) && (item->auxForID == colorOBUItem->id)) {
                alphaOBUItem = item;
                alphaOBU.data = input->data + item->offset;
                alphaOBU.size = item->size;
                break;
            }
        }
    }

    if (colorOBU.size == 0) {
        return AVIF_RESULT_NO_AV1_ITEMS_FOUND;
    }
    avifBool hasAlpha = (alphaOBU.size > 0) ? AVIF_TRUE : AVIF_FALSE;

    codec = avifCodecCreate();
    if (!avifCodecDecode(codec, AVIF_CODEC_PLANES_COLOR, &colorOBU)) {
        avifCodecDestroy(codec);
        return AVIF_RESULT_DECODE_COLOR_FAILED;
    }
    avifCodecImageSize colorPlanesSize = avifCodecGetImageSize(codec, AVIF_CODEC_PLANES_COLOR);

    avifCodecImageSize alphaPlanesSize;
    memset(&alphaPlanesSize, 0, sizeof(alphaPlanesSize));
    if (hasAlpha) {
        if (!avifCodecDecode(codec, AVIF_CODEC_PLANES_ALPHA, &alphaOBU)) {
            avifCodecDestroy(codec);
            return AVIF_RESULT_DECODE_ALPHA_FAILED;
        }
        alphaPlanesSize = avifCodecGetImageSize(codec, AVIF_CODEC_PLANES_ALPHA);

        if ((colorPlanesSize.width != alphaPlanesSize.width) || (colorPlanesSize.height != alphaPlanesSize.height)) {
            avifCodecDestroy(codec);
            return AVIF_RESULT_COLOR_ALPHA_SIZE_MISMATCH;
        }
    }

    if ((colorOBUItem && colorOBUItem->ispePresent && ((colorOBUItem->ispe.width != colorPlanesSize.width) || (colorOBUItem->ispe.height != colorPlanesSize.height))) ||
        (alphaOBUItem && alphaOBUItem->ispePresent && ((alphaOBUItem->ispe.width != alphaPlanesSize.width) || (alphaOBUItem->ispe.height != alphaPlanesSize.height))))
    {
        avifCodecDestroy(codec);
        return AVIF_RESULT_ISPE_SIZE_MISMATCH;
    }

    if (colorOBUItem->colrPresent) {
        if (colorOBUItem->colr.format == AVIF_PROFILE_FORMAT_ICC) {
            avifImageSetProfileICC(image, colorOBUItem->colr.icc, colorOBUItem->colr.iccSize);
        } else if (colorOBUItem->colr.format == AVIF_PROFILE_FORMAT_NCLX) {
            avifImageSetProfileNCLX(image, &colorOBUItem->colr.nclx);
        }
    }

    avifImageFreePlanes(image, AVIF_PLANES_ALL);

    avifResult imageResult = avifCodecGetDecodedImage(codec, image);
    if (imageResult != AVIF_RESULT_OK) {
        avifCodecDestroy(codec);
        return imageResult;
    }

#if defined(AVIF_FIX_STUDIO_ALPHA)
    if (hasAlpha && avifCodecAlphaLimitedRange(codec)) {
        // Naughty! Alpha planes are supposed to be full range. Correct that here.
        if (avifImageUsesU16(image)) {
            for (int j = 0; j < image->height; ++j) {
                for (int i = 0; i < image->height; ++i) {
                    uint16_t * alpha = (uint16_t *)&image->alphaPlane[(i * 2) + (j * image->alphaRowBytes)];
                    *alpha = (uint16_t)avifLimitedToFull(image->depth, *alpha);
                }
            }
        } else {
            for (int j = 0; j < image->height; ++j) {
                for (int i = 0; i < image->height; ++i) {
                    uint8_t * alpha = &image->alphaPlane[i + (j * image->alphaRowBytes)];
                    *alpha = (uint8_t)avifLimitedToFull(image->depth, *alpha);
                }
            }
        }
    }
#endif

    // Make this optional?
    avifImageYUVToRGB(image);

    if (codec) {
        avifCodecDestroy(codec);
    }
    return AVIF_RESULT_OK;
}
