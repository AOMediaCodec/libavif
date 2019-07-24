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
    uint32_t id;
    uint8_t type[4];
    uint32_t offset;
    uint32_t size;
    avifBool ispePresent;
    avifImageSpatialExtents ispe;
    avifBool auxCPresent;
    avifAuxiliaryType auxC;
    avifBool colrPresent;
    avifColourInformationBox colr;
    uint32_t thumbnailForID; // if non-zero, this item is a thumbnail for Item #{thumbnailForID}
    uint32_t auxForID;       // if non-zero, this item is an auxC plane for Item #{auxForID}
} avifItem;
AVIF_ARRAY_DECLARE(avifItemArray, avifItem, item);

// Temporary storage for ipco contents until they can be associated and memcpy'd to an avifItem
typedef struct avifProperty
{
    uint8_t type[4];
    avifImageSpatialExtents ispe;
    avifAuxiliaryType auxC;
    avifColourInformationBox colr;
} avifProperty;
AVIF_ARRAY_DECLARE(avifPropertyArray, avifProperty, prop);

// ---------------------------------------------------------------------------
// avifTrack

typedef struct avifSampleTableChunk
{
    uint64_t offset;
} avifSampleTableChunk;
AVIF_ARRAY_DECLARE(avifSampleTableChunkArray, avifSampleTableChunk, chunk);

typedef struct avifSampleTableSampleToChunk
{
    uint32_t firstChunk;
    uint32_t samplesPerChunk;
    uint32_t sampleDescriptionIndex;
} avifSampleTableSampleToChunk;
AVIF_ARRAY_DECLARE(avifSampleTableSampleToChunkArray, avifSampleTableSampleToChunk, sampleToChunk);

typedef struct avifSampleTableSampleSize
{
    uint32_t size;
} avifSampleTableSampleSize;
AVIF_ARRAY_DECLARE(avifSampleTableSampleSizeArray, avifSampleTableSampleSize, sampleSize);

typedef struct avifSampleTableTimeToSample
{
    uint32_t sampleCount;
    uint32_t sampleDelta;
} avifSampleTableTimeToSample;
AVIF_ARRAY_DECLARE(avifSampleTableTimeToSampleArray, avifSampleTableTimeToSample, timeToSample);

typedef struct avifSampleTable
{
    avifSampleTableChunkArray chunks;
    avifSampleTableSampleToChunkArray sampleToChunks;
    avifSampleTableSampleSizeArray sampleSizes;
    avifSampleTableTimeToSampleArray timeToSamples;
} avifSampleTable;

static avifSampleTable * avifSampleTableCreate()
{
    avifSampleTable * sampleTable = (avifSampleTable *)avifAlloc(sizeof(avifSampleTable));
    memset(sampleTable, 0, sizeof(avifSampleTable));
    avifArrayCreate(&sampleTable->chunks, sizeof(avifSampleTableChunk), 16);
    avifArrayCreate(&sampleTable->sampleToChunks, sizeof(avifSampleTableSampleToChunk), 16);
    avifArrayCreate(&sampleTable->sampleSizes, sizeof(avifSampleTableSampleSize), 16);
    avifArrayCreate(&sampleTable->timeToSamples, sizeof(avifSampleTableTimeToSample), 16);
    return sampleTable;
}

static void avifSampleTableDestroy(avifSampleTable * sampleTable)
{
    avifArrayDestroy(&sampleTable->chunks);
    avifArrayDestroy(&sampleTable->sampleToChunks);
    avifArrayDestroy(&sampleTable->sampleSizes);
    avifArrayDestroy(&sampleTable->timeToSamples);
    avifFree(sampleTable);
}

static uint32_t avifSampleTableGetImageDelta(avifSampleTable * sampleTable, int imageIndex)
{
    int maxSampleIndex = 0;
    for (uint32_t i = 0; i < sampleTable->timeToSamples.count; ++i) {
        avifSampleTableTimeToSample * timeToSample = &sampleTable->timeToSamples.timeToSample[i];
        maxSampleIndex += timeToSample->sampleCount;
        if ((imageIndex < maxSampleIndex) || (i == (sampleTable->timeToSamples.count - 1))) {
            return timeToSample->sampleDelta;
        }
    }

    // TODO: fail here?
    return 1;
}

// one video track ("trak" contents)
typedef struct avifTrack
{
    uint32_t id;
    uint32_t auxForID; // if non-zero, this item is an auxC plane for Track #{auxForID}
    uint32_t mediaTimescale;
    uint64_t mediaDuration;
    avifSampleTable * sampleTable;
} avifTrack;
AVIF_ARRAY_DECLARE(avifTrackArray, avifTrack, track);

// ---------------------------------------------------------------------------
// avifCodecDecodeInput

avifCodecDecodeInput * avifCodecDecodeInputCreate(void)
{
    avifCodecDecodeInput * decodeInput = (avifCodecDecodeInput *)avifAlloc(sizeof(avifCodecDecodeInput));
    memset(decodeInput, 0, sizeof(avifCodecDecodeInput));
    avifArrayCreate(&decodeInput->samples, sizeof(avifRawData), 1);
    return decodeInput;
}

void avifCodecDecodeInputDestroy(avifCodecDecodeInput * decodeInput)
{
    avifArrayDestroy(&decodeInput->samples);
    avifFree(decodeInput);
}

static avifBool avifCodecDecodeInputGetSamples(avifCodecDecodeInput * decodeInput, avifSampleTable * sampleTable, avifRawData * rawInput)
{
    uint32_t sampleSizeIndex = 0;
    for (uint32_t chunkIndex = 0; chunkIndex < sampleTable->chunks.count; ++chunkIndex) {
        avifSampleTableChunk * chunk = &sampleTable->chunks.chunk[chunkIndex];

        // First, figure out how many samples are in this chunk
        uint32_t sampleCount = 0;
        for (int sampleToChunkIndex = sampleTable->sampleToChunks.count - 1; sampleToChunkIndex >= 0; --sampleToChunkIndex) {
            avifSampleTableSampleToChunk * sampleToChunk = &sampleTable->sampleToChunks.sampleToChunk[sampleToChunkIndex];
            if (sampleToChunk->firstChunk <= (chunkIndex + 1)) {
                sampleCount = sampleToChunk->samplesPerChunk;
                break;
            }
        }
        if (sampleCount == 0) {
            // chunks with 0 samples are invalid
            return AVIF_FALSE;
        }

        uint64_t sampleOffset = chunk->offset;
        for (uint32_t sampleIndex = 0; sampleIndex < sampleCount; ++sampleIndex) {
            if (sampleSizeIndex >= sampleTable->sampleSizes.count) {
                // We've run out of samples to sum
                return AVIF_FALSE;
            }

            avifSampleTableSampleSize * sampleSize = &sampleTable->sampleSizes.sampleSize[sampleSizeIndex];

            avifRawData * rawSample = (avifRawData *)avifArrayPushPtr(&decodeInput->samples);
            rawSample->data = rawInput->data + sampleOffset;
            rawSample->size = sampleSize->size;

            if (sampleOffset > (uint64_t)rawInput->size) {
                return AVIF_FALSE;
            }

            sampleOffset += sampleSize->size;
            ++sampleSizeIndex;
        }
    }
    return AVIF_TRUE;
}

// ---------------------------------------------------------------------------
// avifData

typedef struct avifData
{
    avifFileType ftyp;
    avifItemArray items;
    avifPropertyArray properties;
    avifTrackArray tracks;
    avifRawData rawInput;
    avifCodecDecodeInput * colorInput;
    avifCodecDecodeInput * alphaInput;
    avifDecoderSource source;
    avifSampleTable * sourceSampleTable; // NULL unless (source == AVIF_DECODER_SOURCE_TRACKS), owned by an avifTrack
    struct avifCodec * codec[AVIF_CODEC_PLANES_COUNT];
} avifData;

static avifData * avifDataCreate()
{
    avifData * data = (avifData *)avifAlloc(sizeof(avifData));
    memset(data, 0, sizeof(avifData));
    avifArrayCreate(&data->items, sizeof(avifItem), 8);
    avifArrayCreate(&data->properties, sizeof(avifProperty), 16);
    avifArrayCreate(&data->tracks, sizeof(avifTrack), 2);
    return data;
}

static void avifDataResetCodec(avifData * data)
{
    for (int i = 0; i < AVIF_CODEC_PLANES_COUNT; ++i) {
        if (data->codec[i]) {
            avifCodecDestroy(data->codec[i]);
            data->codec[i] = NULL;
        }
    }
}

static void avifDataDestroy(avifData * data)
{
    avifDataResetCodec(data);
    avifArrayDestroy(&data->items);
    avifArrayDestroy(&data->properties);
    for (uint32_t i = 0; i < data->tracks.count; ++i) {
        if (data->tracks.track[i].sampleTable) {
            avifSampleTableDestroy(data->tracks.track[i].sampleTable);
        }
    }
    avifArrayDestroy(&data->tracks);
    if (data->colorInput) {
        avifCodecDecodeInputDestroy(data->colorInput);
    }
    if (data->alphaInput) {
        avifCodecDecodeInputDestroy(data->alphaInput);
    }
    avifFree(data);
}

static avifItem * avifDataFindItem(avifData * data, uint32_t itemID)
{
    if (itemID == 0) {
        return NULL;
    }

    for (uint32_t i = 0; i < data->items.count; ++i) {
        if (data->items.item[i].id == itemID) {
            return &data->items.item[i];
        }
    }

    avifItem * item = (avifItem *)avifArrayPushPtr(&data->items);
    item->id = itemID;
    return item;
}

// ---------------------------------------------------------------------------
// URN

static avifBool isAlphaURN(char * urn)
{
    if (!strcmp(urn, URN_ALPHA0))
        return AVIF_TRUE;
    if (!strcmp(urn, URN_ALPHA1))
        return AVIF_TRUE;
    return AVIF_FALSE;
}

// ---------------------------------------------------------------------------
// BMFF Parsing

#define BEGIN_STREAM(VARNAME, PTR, SIZE) \
    avifStream VARNAME;                  \
    avifRawData VARNAME##_rawData;       \
    VARNAME##_rawData.data = PTR;        \
    VARNAME##_rawData.size = SIZE;       \
    avifStreamStart(&VARNAME, &VARNAME##_rawData)

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
        uint16_t dataReferenceIndex;                               // unsigned int(16) data_ref rence_index;
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

            avifItem * item = avifDataFindItem(data, itemID);
            if (!item) {
                return AVIF_FALSE;
            }
            item->id = itemID;
            item->offset = (uint32_t)(baseOffset + extentOffset);
            item->size = (uint32_t)extentLength;
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

    CHECK(avifStreamReadU32(&s, &data->properties.prop[propertyIndex].ispe.width));
    CHECK(avifStreamReadU32(&s, &data->properties.prop[propertyIndex].ispe.height));
    return AVIF_TRUE;
}

static avifBool avifParseAuxiliaryTypeProperty(avifData * data, uint8_t * raw, size_t rawLen, int propertyIndex)
{
    BEGIN_STREAM(s, raw, rawLen);
    CHECK(avifStreamReadAndEnforceVersion(&s, 0));

    CHECK(avifStreamReadString(&s, data->properties.prop[propertyIndex].auxC.auxType, AUXTYPE_SIZE));
    return AVIF_TRUE;
}

static avifBool avifParseColourInformationBox(avifData * data, uint8_t * raw, size_t rawLen, int propertyIndex)
{
    BEGIN_STREAM(s, raw, rawLen);

    data->properties.prop[propertyIndex].colr.format = AVIF_PROFILE_FORMAT_NONE;

    uint8_t colourType[4]; // unsigned int(32) colour_type;
    CHECK(avifStreamRead(&s, colourType, 4));
    if (!memcmp(colourType, "rICC", 4) || !memcmp(colourType, "prof", 4)) {
        data->properties.prop[propertyIndex].colr.format = AVIF_PROFILE_FORMAT_ICC;
        data->properties.prop[propertyIndex].colr.icc = avifStreamCurrent(&s);
        data->properties.prop[propertyIndex].colr.iccSize = avifStreamRemainingBytes(&s);
    } else if (!memcmp(colourType, "nclx", 4)) {
        // unsigned int(16) colour_primaries;
        CHECK(avifStreamReadU16(&s, &data->properties.prop[propertyIndex].colr.nclx.colourPrimaries));
        // unsigned int(16) transfer_characteristics;
        CHECK(avifStreamReadU16(&s, &data->properties.prop[propertyIndex].colr.nclx.transferCharacteristics));
        // unsigned int(16) matrix_coefficients;
        CHECK(avifStreamReadU16(&s, &data->properties.prop[propertyIndex].colr.nclx.matrixCoefficients));
        // unsigned int(1) full_range_flag;
        // unsigned int(7) reserved = 0;
        CHECK(avifStreamRead(&s, &data->properties.prop[propertyIndex].colr.nclx.fullRangeFlag, 1));
        data->properties.prop[propertyIndex].colr.nclx.fullRangeFlag |= 0x80;
        data->properties.prop[propertyIndex].colr.format = AVIF_PROFILE_FORMAT_NCLX;
    }
    return AVIF_TRUE;
}

static avifBool avifParseItemPropertyContainerBox(avifData * data, uint8_t * raw, size_t rawLen)
{
    BEGIN_STREAM(s, raw, rawLen);

    while (avifStreamHasBytesLeft(&s, 1)) {
        avifBoxHeader header;
        CHECK(avifStreamReadBoxHeader(&s, &header));

        int propertyIndex = avifArrayPushIndex(&data->properties);
        memcpy(data->properties.prop[propertyIndex].type, header.type, 4);
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
            // avifBool essential = AVIF_FALSE; // currently unused
            uint16_t propertyIndex = 0;
            if (propertyIndexIsU16) {
                CHECK(avifStreamReadU16(&s, &propertyIndex));
                // essential = (propertyIndex & 0x8000) ? AVIF_TRUE : AVIF_FALSE;
                propertyIndex &= 0x7fff;
            } else {
                uint8_t tmp;
                CHECK(avifStreamRead(&s, &tmp, 1));
                // essential = (tmp & 0x80) ? AVIF_TRUE : AVIF_FALSE;
                propertyIndex = tmp & 0x7f;
            }

            if (propertyIndex == 0) {
                // Not associated with any item
                continue;
            }
            --propertyIndex; // 1-indexed

            if (propertyIndex >= data->properties.count) {
                return AVIF_FALSE;
            }

            avifItem * item = avifDataFindItem(data, itemID);
            if (!item) {
                return AVIF_FALSE;
            }

            // Associate property with item
            avifProperty * prop = &data->properties.prop[propertyIndex];
            if (!memcmp(prop->type, "ispe", 4)) {
                item->ispePresent = AVIF_TRUE;
                memcpy(&item->ispe, &prop->ispe, sizeof(avifImageSpatialExtents));
            } else if (!memcmp(prop->type, "auxC", 4)) {
                item->auxCPresent = AVIF_TRUE;
                memcpy(&item->auxC, &prop->auxC, sizeof(avifAuxiliaryType));
            } else if (!memcmp(prop->type, "colr", 4)) {
                item->colrPresent = AVIF_TRUE;
                memcpy(&item->colr, &prop->colr, sizeof(avifColourInformationBox));
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

    avifItem * item = avifDataFindItem(data, itemID);
    if (!item) {
        return AVIF_FALSE;
    }

    memcpy(item->type, itemType, sizeof(itemType));
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
        CHECK(avifStreamReadU32(&s, &entryCount)); // unsigned int(32) entry_count;
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
            CHECK(avifStreamReadU32(&s, &fromID)); // unsigned int(32) from_item_ID;
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
                CHECK(avifStreamReadU32(&s, &toID)); // unsigned int(32) to_item_ID;
            } else {
                // unsupported iref version, skip it
                break;
            }

            // Read this reference as "{fromID} is a {irefType} for {toID}"
            if (fromID && toID) {
                avifItem * item = avifDataFindItem(data, fromID);
                if (!item) {
                    return AVIF_FALSE;
                }

                if (!memcmp(irefHeader.type, "thmb", 4)) {
                    item->thumbnailForID = toID;
                }
                if (!memcmp(irefHeader.type, "auxl", 4)) {
                    item->auxForID = toID;
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

static avifBool avifParseTrackHeaderBox(avifData * data, avifTrack * track, uint8_t * raw, size_t rawLen)
{
    BEGIN_STREAM(s, raw, rawLen);
    (void)data;

    uint8_t version;
    uint8_t flags[3];
    CHECK(avifStreamReadVersionAndFlags(&s, &version, flags));

    uint32_t ignored32, trackID;
    uint64_t ignored64;
    if (version == 1) {
        CHECK(avifStreamReadU64(&s, &ignored64)); // unsigned int(64) creation_time;
        CHECK(avifStreamReadU64(&s, &ignored64)); // unsigned int(64) modification_time;
        CHECK(avifStreamReadU32(&s, &trackID));   // unsigned int(32) track_ID;
    } else if (version == 0) {
        CHECK(avifStreamReadU32(&s, &ignored32)); // unsigned int(32) creation_time;
        CHECK(avifStreamReadU32(&s, &ignored32)); // unsigned int(32) modification_time;
        CHECK(avifStreamReadU32(&s, &trackID));   // unsigned int(32) track_ID;
    } else {
        // Unsupported version
        return AVIF_FALSE;
    }

    // TODO: support scaling based on width/height track header info?

    track->id = trackID;
    return AVIF_TRUE;
}

static avifBool avifParseMediaHeaderBox(avifData * data, avifTrack * track, uint8_t * raw, size_t rawLen)
{
    BEGIN_STREAM(s, raw, rawLen);
    (void)data;

    uint8_t version;
    uint8_t flags[3];
    CHECK(avifStreamReadVersionAndFlags(&s, &version, flags));

    uint32_t ignored32, mediaTimescale, mediaDuration32;
    uint64_t ignored64, mediaDuration64;
    if (version == 1) {
        CHECK(avifStreamReadU64(&s, &ignored64));       // unsigned int(64) creation_time;
        CHECK(avifStreamReadU64(&s, &ignored64));       // unsigned int(64) modification_time;
        CHECK(avifStreamReadU32(&s, &mediaTimescale));  // unsigned int(32) timescale;
        CHECK(avifStreamReadU64(&s, &mediaDuration64)); // unsigned int(64) duration;
        track->mediaDuration = mediaDuration64;
    } else if (version == 0) {
        CHECK(avifStreamReadU32(&s, &ignored32));       // unsigned int(32) creation_time;
        CHECK(avifStreamReadU32(&s, &ignored32));       // unsigned int(32) modification_time;
        CHECK(avifStreamReadU32(&s, &mediaTimescale));  // unsigned int(32) timescale;
        CHECK(avifStreamReadU32(&s, &mediaDuration32)); // unsigned int(32) duration;
        track->mediaDuration = (uint64_t)mediaDuration32;
    } else {
        // Unsupported version
        return AVIF_FALSE;
    }

    track->mediaTimescale = mediaTimescale;
    return AVIF_TRUE;
}

static avifBool avifParseChunkOffsetBox(avifData * data, avifSampleTable * sampleTable, avifBool largeOffsets, uint8_t * raw, size_t rawLen)
{
    BEGIN_STREAM(s, raw, rawLen);
    (void)data;

    CHECK(avifStreamReadAndEnforceVersion(&s, 0));

    uint32_t entryCount;
    CHECK(avifStreamReadU32(&s, &entryCount)); // unsigned int(32) entry_count;
    for (uint32_t i = 0; i < entryCount; ++i) {
        uint64_t offset;
        if (largeOffsets) {
            CHECK(avifStreamReadU64(&s, &offset)); // unsigned int(32) chunk_offset;
        } else {
            uint32_t offset32;
            CHECK(avifStreamReadU32(&s, &offset32)); // unsigned int(32) chunk_offset;
            offset = (uint64_t)offset32;
        }

        avifSampleTableChunk * chunk = (avifSampleTableChunk *)avifArrayPushPtr(&sampleTable->chunks);
        chunk->offset = offset;
    }
    return AVIF_TRUE;
}

static avifBool avifParseSampleToChunkBox(avifData * data, avifSampleTable * sampleTable, uint8_t * raw, size_t rawLen)
{
    BEGIN_STREAM(s, raw, rawLen);
    (void)data;

    CHECK(avifStreamReadAndEnforceVersion(&s, 0));

    uint32_t entryCount;
    CHECK(avifStreamReadU32(&s, &entryCount)); // unsigned int(32) entry_count;
    for (uint32_t i = 0; i < entryCount; ++i) {
        avifSampleTableSampleToChunk * sampleToChunk = (avifSampleTableSampleToChunk *)avifArrayPushPtr(&sampleTable->sampleToChunks);
        CHECK(avifStreamReadU32(&s, &sampleToChunk->firstChunk));             // unsigned int(32) first_chunk;
        CHECK(avifStreamReadU32(&s, &sampleToChunk->samplesPerChunk));        // unsigned int(32) samples_per_chunk;
        CHECK(avifStreamReadU32(&s, &sampleToChunk->sampleDescriptionIndex)); // unsigned int(32) sample_description_index;
    }
    return AVIF_TRUE;
}

static avifBool avifParseSampleSizeBox(avifData * data, avifSampleTable * sampleTable, uint8_t * raw, size_t rawLen)
{
    BEGIN_STREAM(s, raw, rawLen);
    (void)data;

    CHECK(avifStreamReadAndEnforceVersion(&s, 0));

    uint32_t allSamplesSize, entryCount;
    CHECK(avifStreamReadU32(&s, &allSamplesSize)); // unsigned int(32) sample_size;
    CHECK(avifStreamReadU32(&s, &entryCount));     // unsigned int(32) entry_count;

    for (uint32_t i = 0; i < entryCount; ++i) {
        avifSampleTableSampleSize * sampleSize = (avifSampleTableSampleSize *)avifArrayPushPtr(&sampleTable->sampleSizes);
        if (allSamplesSize == 0) {
            CHECK(avifStreamReadU32(&s, &sampleSize->size)); // unsigned int(32) entry_size;
        } else {
            // This could be done more efficiently, memory-wise.
            sampleSize->size = allSamplesSize;
        }
    }
    return AVIF_TRUE;
}

static avifBool avifParseTimeToSampleBox(avifData * data, avifSampleTable * sampleTable, uint8_t * raw, size_t rawLen)
{
    BEGIN_STREAM(s, raw, rawLen);
    (void)data;

    CHECK(avifStreamReadAndEnforceVersion(&s, 0));

    uint32_t entryCount;
    CHECK(avifStreamReadU32(&s, &entryCount)); // unsigned int(32) entry_count;

    for (uint32_t i = 0; i < entryCount; ++i) {
        avifSampleTableTimeToSample * timeToSample = (avifSampleTableTimeToSample *)avifArrayPushPtr(&sampleTable->timeToSamples);
        CHECK(avifStreamReadU32(&s, &timeToSample->sampleCount)); // unsigned int(32) sample_count;
        CHECK(avifStreamReadU32(&s, &timeToSample->sampleDelta)); // unsigned int(32) sample_delta;
    }
    return AVIF_TRUE;
}

static avifBool avifParseSampleTableBox(avifData * data, avifTrack * track, uint8_t * raw, size_t rawLen)
{
    if (track->sampleTable) {
        // A TrackBox may only have one SampleTable
        return AVIF_FALSE;
    }
    track->sampleTable = avifSampleTableCreate();

    BEGIN_STREAM(s, raw, rawLen);

    while (avifStreamHasBytesLeft(&s, 1)) {
        avifBoxHeader header;
        CHECK(avifStreamReadBoxHeader(&s, &header));

        if (!memcmp(header.type, "stco", 4)) {
            CHECK(avifParseChunkOffsetBox(data, track->sampleTable, AVIF_FALSE, avifStreamCurrent(&s), header.size));
        } else if (!memcmp(header.type, "co64", 4)) {
            CHECK(avifParseChunkOffsetBox(data, track->sampleTable, AVIF_TRUE, avifStreamCurrent(&s), header.size));
        } else if (!memcmp(header.type, "stsc", 4)) {
            CHECK(avifParseSampleToChunkBox(data, track->sampleTable, avifStreamCurrent(&s), header.size));
        } else if (!memcmp(header.type, "stsz", 4)) {
            CHECK(avifParseSampleSizeBox(data, track->sampleTable, avifStreamCurrent(&s), header.size));
        } else if (!memcmp(header.type, "stts", 4)) {
            CHECK(avifParseTimeToSampleBox(data, track->sampleTable, avifStreamCurrent(&s), header.size));
        }

        CHECK(avifStreamSkip(&s, header.size));
    }
    return AVIF_TRUE;
}

static avifBool avifParseMediaInformationBox(avifData * data, avifTrack * track, uint8_t * raw, size_t rawLen)
{
    BEGIN_STREAM(s, raw, rawLen);

    while (avifStreamHasBytesLeft(&s, 1)) {
        avifBoxHeader header;
        CHECK(avifStreamReadBoxHeader(&s, &header));

        if (!memcmp(header.type, "stbl", 4)) {
            CHECK(avifParseSampleTableBox(data, track, avifStreamCurrent(&s), header.size));
        }

        CHECK(avifStreamSkip(&s, header.size));
    }
    return AVIF_TRUE;
}

static avifBool avifParseMediaBox(avifData * data, avifTrack * track, uint8_t * raw, size_t rawLen)
{
    BEGIN_STREAM(s, raw, rawLen);

    while (avifStreamHasBytesLeft(&s, 1)) {
        avifBoxHeader header;
        CHECK(avifStreamReadBoxHeader(&s, &header));

        if (!memcmp(header.type, "mdhd", 4)) {
            CHECK(avifParseMediaHeaderBox(data, track, avifStreamCurrent(&s), header.size));
        } else if (!memcmp(header.type, "minf", 4)) {
            CHECK(avifParseMediaInformationBox(data, track, avifStreamCurrent(&s), header.size));
        }

        CHECK(avifStreamSkip(&s, header.size));
    }
    return AVIF_TRUE;
}

static avifBool avifTrackReferenceBox(avifData * data, avifTrack * track, uint8_t * raw, size_t rawLen)
{
    BEGIN_STREAM(s, raw, rawLen);
    (void)data;

    while (avifStreamHasBytesLeft(&s, 1)) {
        avifBoxHeader header;
        CHECK(avifStreamReadBoxHeader(&s, &header));

        if (!memcmp(header.type, "auxl", 4)) {
            uint32_t toID;
            CHECK(avifStreamReadU32(&s, &toID));                       // unsigned int(32) track_IDs[]
            CHECK(avifStreamSkip(&s, header.size - sizeof(uint32_t))); // just take the first one
            track->auxForID = toID;
        } else {
            CHECK(avifStreamSkip(&s, header.size));
        }
    }
    return AVIF_TRUE;
}

static avifBool avifParseTrackBox(avifData * data, uint8_t * raw, size_t rawLen)
{
    BEGIN_STREAM(s, raw, rawLen);

    avifTrack * track = (avifTrack *)avifArrayPushPtr(&data->tracks);

    while (avifStreamHasBytesLeft(&s, 1)) {
        avifBoxHeader header;
        CHECK(avifStreamReadBoxHeader(&s, &header));

        if (!memcmp(header.type, "tkhd", 4)) {
            CHECK(avifParseTrackHeaderBox(data, track, avifStreamCurrent(&s), header.size));
        } else if (!memcmp(header.type, "mdia", 4)) {
            CHECK(avifParseMediaBox(data, track, avifStreamCurrent(&s), header.size));
        } else if (!memcmp(header.type, "tref", 4)) {
            CHECK(avifTrackReferenceBox(data, track, avifStreamCurrent(&s), header.size));
        }

        CHECK(avifStreamSkip(&s, header.size));
    }
    return AVIF_TRUE;
}

static avifBool avifParseMoovBox(avifData * data, uint8_t * raw, size_t rawLen)
{
    BEGIN_STREAM(s, raw, rawLen);

    while (avifStreamHasBytesLeft(&s, 1)) {
        avifBoxHeader header;
        CHECK(avifStreamReadBoxHeader(&s, &header));

        if (!memcmp(header.type, "trak", 4)) {
            CHECK(avifParseTrackBox(data, avifStreamCurrent(&s), header.size));
        }

        CHECK(avifStreamSkip(&s, header.size));
    }
    return AVIF_TRUE;
}

static avifBool avifParseFileTypeBox(avifFileType * ftyp, uint8_t * raw, size_t rawLen)
{
    BEGIN_STREAM(s, raw, rawLen);

    CHECK(avifStreamRead(&s, ftyp->majorBrand, 4));
    CHECK(avifStreamReadU32(&s, &ftyp->minorVersion));

    size_t compatibleBrandsBytes = avifStreamRemainingBytes(&s);
    if ((compatibleBrandsBytes % 4) != 0) {
        return AVIF_FALSE;
    }
    if (compatibleBrandsBytes > (4 * MAX_COMPATIBLE_BRANDS)) {
        // TODO: stop clamping and resize this
        compatibleBrandsBytes = (4 * MAX_COMPATIBLE_BRANDS);
    }
    CHECK(avifStreamRead(&s, ftyp->compatibleBrands, compatibleBrandsBytes));
    ftyp->compatibleBrandsCount = (int)compatibleBrandsBytes / 4;

    return AVIF_TRUE;
}

static avifBool avifParse(avifData * data, uint8_t * raw, size_t rawLen)
{
    BEGIN_STREAM(s, raw, rawLen);

    while (avifStreamHasBytesLeft(&s, 1)) {
        avifBoxHeader header;
        CHECK(avifStreamReadBoxHeader(&s, &header));

        if (!memcmp(header.type, "ftyp", 4)) {
            CHECK(avifParseFileTypeBox(&data->ftyp, avifStreamCurrent(&s), header.size));
        } else if (!memcmp(header.type, "meta", 4)) {
            CHECK(avifParseMetaBox(data, avifStreamCurrent(&s), header.size));
        } else if (!memcmp(header.type, "moov", 4)) {
            CHECK(avifParseMoovBox(data, avifStreamCurrent(&s), header.size));
        }

        CHECK(avifStreamSkip(&s, header.size));
    }
    return AVIF_TRUE;
}

// ---------------------------------------------------------------------------

static avifBool avifFileTypeIsCompatible(avifFileType * ftyp)
{
    avifBool avifCompatible = (memcmp(ftyp->majorBrand, "avif", 4) == 0) ? AVIF_TRUE : AVIF_FALSE;
    if (!avifCompatible) {
        avifCompatible = (memcmp(ftyp->majorBrand, "avis", 4) == 0) ? AVIF_TRUE : AVIF_FALSE;
        if (!avifCompatible) {
            for (int compatibleBrandIndex = 0; compatibleBrandIndex < ftyp->compatibleBrandsCount; ++compatibleBrandIndex) {
                uint8_t * compatibleBrand = &ftyp->compatibleBrands[4 * compatibleBrandIndex];
                if (!memcmp(compatibleBrand, "avif", 4)) {
                    avifCompatible = AVIF_TRUE;
                    break;
                }
                if (!memcmp(compatibleBrand, "avis", 4)) {
                    avifCompatible = AVIF_TRUE;
                    break;
                }
            }
        }
    }
    return avifCompatible;
}

avifBool avifPeekCompatibleFileType(avifRawData * input)
{
    BEGIN_STREAM(s, input->data, input->size);

    avifBoxHeader header;
    CHECK(avifStreamReadBoxHeader(&s, &header));
    if (memcmp(header.type, "ftyp", 4) != 0) {
        return AVIF_FALSE;
    }

    avifFileType ftyp;
    memset(&ftyp, 0, sizeof(avifFileType));
    avifBool parsed = avifParseFileTypeBox(&ftyp, avifStreamCurrent(&s), header.size);
    if (!parsed) {
        return AVIF_FALSE;
    }
    return avifFileTypeIsCompatible(&ftyp);
}

// ---------------------------------------------------------------------------

avifDecoder * avifDecoderCreate(void)
{
    avifDecoder * decoder = (avifDecoder *)avifAlloc(sizeof(avifDecoder));
    memset(decoder, 0, sizeof(avifDecoder));
    return decoder;
}

static void avifDecoderCleanup(avifDecoder * decoder)
{
    if (decoder->data) {
        avifDataDestroy(decoder->data);
        decoder->data = NULL;
    }

    if (decoder->image) {
        avifImageDestroy(decoder->image);
        decoder->image = NULL;
    }
}

void avifDecoderDestroy(avifDecoder * decoder)
{
    avifDecoderCleanup(decoder);
    avifFree(decoder);
}

avifResult avifDecoderSetSource(avifDecoder * decoder, avifDecoderSource source)
{
    decoder->requestedSource = source;
    return avifDecoderReset(decoder);
}

avifResult avifDecoderParse(avifDecoder * decoder, avifRawData * rawInput)
{
#if !defined(AVIF_CODEC_AOM) && !defined(AVIF_CODEC_DAV1D)
    // Just bail out early, we're not surviving this function without a decoder compiled in
    return AVIF_RESULT_NO_CODEC_AVAILABLE;
#endif

    // Cleanup anything lingering in the decoder
    avifDecoderCleanup(decoder);

    // -----------------------------------------------------------------------
    // Parse BMFF boxes

    decoder->data = avifDataCreate();

    // Shallow copy, on purpose
    memcpy(&decoder->data->rawInput, rawInput, sizeof(avifRawData));

    if (!avifParse(decoder->data, decoder->data->rawInput.data, decoder->data->rawInput.size)) {
        return AVIF_RESULT_BMFF_PARSE_FAILED;
    }

    avifBool avifCompatible = avifFileTypeIsCompatible(&decoder->data->ftyp);
    if (!avifCompatible) {
        return AVIF_RESULT_INVALID_FTYP;
    }

    // Sanity check items
    for (uint32_t itemIndex = 0; itemIndex < decoder->data->items.count; ++itemIndex) {
        avifItem * item = &decoder->data->items.item[itemIndex];
        if (item->offset > decoder->data->rawInput.size) {
            return AVIF_RESULT_BMFF_PARSE_FAILED;
        }
        uint64_t offsetSize = (uint64_t)item->offset + (uint64_t)item->size;
        if (offsetSize > (uint64_t)decoder->data->rawInput.size) {
            return AVIF_RESULT_BMFF_PARSE_FAILED;
        }
    }

    // Sanity check tracks
    for (uint32_t trackIndex = 0; trackIndex < decoder->data->tracks.count; ++trackIndex) {
        avifTrack * track = &decoder->data->tracks.track[trackIndex];
        if (!track->sampleTable) {
            continue;
        }

        for (uint32_t chunkIndex = 0; chunkIndex < track->sampleTable->chunks.count; ++chunkIndex) {
            avifSampleTableChunk * chunk = &track->sampleTable->chunks.chunk[chunkIndex];
            if (chunk->offset > decoder->data->rawInput.size) {
                return AVIF_RESULT_BMFF_PARSE_FAILED;
            }
        }
    }
    return avifDecoderReset(decoder);
}

static avifCodec * avifCodecCreateForDecode(avifCodecDecodeInput * decodeInput)
{
    avifCodec * codec = NULL;
#if defined(AVIF_CODEC_DAV1D)
    codec = avifCodecCreateDav1d();
#elif defined(AVIF_CODEC_AOM)
    codec = avifCodecCreateAOM();
#else
#error No decoder available!
#endif
    if (codec) {
        codec->decodeInput = decodeInput;
    }
    return codec;
}

avifResult avifDecoderReset(avifDecoder * decoder)
{
    avifData * data = decoder->data;
    if (!data) {
        // Nothing to reset.
        return AVIF_RESULT_OK;
    }

    avifDataResetCodec(data);
    if (!decoder->image) {
        decoder->image = avifImageCreateEmpty();
    }

    memset(&decoder->ioStats, 0, sizeof(decoder->ioStats));

    // -----------------------------------------------------------------------
    // Build decode input

    data->sourceSampleTable = NULL; // Reset
    if (decoder->requestedSource == AVIF_DECODER_SOURCE_AUTO) {
        if (data->tracks.count > 0) {
            data->source = AVIF_DECODER_SOURCE_TRACKS;
        } else {
            data->source = AVIF_DECODER_SOURCE_PRIMARY_ITEM;
        }
    } else {
        data->source = decoder->requestedSource;
    }

    if (data->source == AVIF_DECODER_SOURCE_TRACKS) {
        avifTrack * colorTrack = NULL;
        avifTrack * alphaTrack = NULL;

        // Find primary track - this probably needs some better detection
        uint32_t colorTrackIndex = 0;
        for (; colorTrackIndex < decoder->data->tracks.count; ++colorTrackIndex) {
            avifTrack * track = &decoder->data->tracks.track[colorTrackIndex];
            if (!track->sampleTable) {
                continue;
            }
            if (!track->sampleTable->chunks.count) {
                continue;
            }
            if (track->auxForID != 0) {
                continue;
            }

            // Found one!
            break;
        }
        if (colorTrackIndex == decoder->data->tracks.count) {
            return AVIF_RESULT_NO_CONTENT;
        }
        colorTrack = &decoder->data->tracks.track[colorTrackIndex];

        uint32_t alphaTrackIndex = 0;
        for (; alphaTrackIndex < decoder->data->tracks.count; ++alphaTrackIndex) {
            avifTrack * track = &decoder->data->tracks.track[alphaTrackIndex];
            if (!track->sampleTable) {
                continue;
            }
            if (!track->sampleTable->chunks.count) {
                continue;
            }
            if (track->auxForID == colorTrack->id) {
                // Found it!
                break;
            }
        }
        if (alphaTrackIndex != decoder->data->tracks.count) {
            alphaTrack = &decoder->data->tracks.track[alphaTrackIndex];
        }

        // TODO: We must get color profile information from somewhere; likely the color OBU as a fallback

        data->colorInput = avifCodecDecodeInputCreate();
        if (!avifCodecDecodeInputGetSamples(data->colorInput, colorTrack->sampleTable, &decoder->data->rawInput)) {
            return AVIF_RESULT_BMFF_PARSE_FAILED;
        }

        if (alphaTrack) {
            data->alphaInput = avifCodecDecodeInputCreate();
            if (!avifCodecDecodeInputGetSamples(data->alphaInput, alphaTrack->sampleTable, &decoder->data->rawInput)) {
                return AVIF_RESULT_BMFF_PARSE_FAILED;
            }
            data->alphaInput->alpha = AVIF_TRUE;
        }

        // Stash off sample table for future timing information
        data->sourceSampleTable = colorTrack->sampleTable;

        // Image sequence timing
        decoder->imageIndex = -1;
        decoder->imageCount = data->colorInput->samples.count;
        decoder->timescale = colorTrack->mediaTimescale;
        decoder->durationInTimescales = colorTrack->mediaDuration;
        if (colorTrack->mediaTimescale) {
            decoder->duration = (double)decoder->durationInTimescales / (double)colorTrack->mediaTimescale;
        } else {
            decoder->duration = 0;
        }
        memset(&decoder->imageTiming, 0, sizeof(decoder->imageTiming)); // to be set in avifDecoderNextImage()
    } else {
        // Create from items

        avifRawData colorOBU = AVIF_RAW_DATA_EMPTY;
        avifRawData alphaOBU = AVIF_RAW_DATA_EMPTY;
        avifItem * colorOBUItem = NULL;

        // Find the colorOBU item
        for (uint32_t itemIndex = 0; itemIndex < data->items.count; ++itemIndex) {
            avifItem * item = &data->items.item[itemIndex];
            if (!item->id || !item->size) {
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
            colorOBU.data = data->rawInput.data + item->offset;
            colorOBU.size = item->size;
            break;
        }

        // Find the alphaOBU item, if any
        if (colorOBUItem) {
            for (uint32_t itemIndex = 0; itemIndex < data->items.count; ++itemIndex) {
                avifItem * item = &data->items.item[itemIndex];
                if (!item->id || !item->size) {
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
                    alphaOBU.data = data->rawInput.data + item->offset;
                    alphaOBU.size = item->size;
                    break;
                }
            }
        }

        if (colorOBU.size == 0) {
            return AVIF_RESULT_NO_AV1_ITEMS_FOUND;
        }

        if (colorOBUItem->colrPresent) {
            if (colorOBUItem->colr.format == AVIF_PROFILE_FORMAT_ICC) {
                avifImageSetProfileICC(decoder->image, colorOBUItem->colr.icc, colorOBUItem->colr.iccSize);
            } else if (colorOBUItem->colr.format == AVIF_PROFILE_FORMAT_NCLX) {
                avifImageSetProfileNCLX(decoder->image, &colorOBUItem->colr.nclx);
            }
        }

        data->colorInput = avifCodecDecodeInputCreate();
        avifRawData * rawColorInput = (avifRawData *)avifArrayPushPtr(&data->colorInput->samples);
        memcpy(rawColorInput, &colorOBU, sizeof(avifRawData));
        if (alphaOBU.size > 0) {
            data->alphaInput = avifCodecDecodeInputCreate();
            avifRawData * rawAlphaInput = (avifRawData *)avifArrayPushPtr(&data->alphaInput->samples);
            memcpy(rawAlphaInput, &alphaOBU, sizeof(avifRawData));
            data->alphaInput->alpha = AVIF_TRUE;
        }

        // Set all counts and timing to safe-but-uninteresting values
        decoder->imageIndex = -1;
        decoder->imageCount = 1;
        decoder->imageTiming.timescale = 1;
        decoder->imageTiming.pts = 0;
        decoder->imageTiming.ptsInTimescales = 0;
        decoder->imageTiming.duration = 1;
        decoder->imageTiming.durationInTimescales = 1;
        decoder->timescale = 1;
        decoder->duration = 1;
        decoder->durationInTimescales = 1;

        decoder->ioStats.colorOBUSize = colorOBU.size;
        decoder->ioStats.alphaOBUSize = alphaOBU.size;
    }

    data->codec[AVIF_CODEC_PLANES_COLOR] = avifCodecCreateForDecode(data->colorInput);
    if (!data->codec[AVIF_CODEC_PLANES_COLOR]->decode(data->codec[AVIF_CODEC_PLANES_COLOR])) {
        return AVIF_RESULT_DECODE_COLOR_FAILED;
    }

    if (data->alphaInput) {
        decoder->data->codec[AVIF_CODEC_PLANES_ALPHA] = avifCodecCreateForDecode(data->alphaInput);
        if (!data->codec[AVIF_CODEC_PLANES_ALPHA]->decode(data->codec[AVIF_CODEC_PLANES_ALPHA])) {
            return AVIF_RESULT_DECODE_ALPHA_FAILED;
        }
    }
    return AVIF_RESULT_OK;
}

avifResult avifDecoderNextImage(avifDecoder * decoder)
{
    avifCodec * colorCodec = decoder->data->codec[AVIF_CODEC_PLANES_COLOR];
    if (!colorCodec->getNextImage(colorCodec, decoder->image)) {
        if (decoder->image->width) {
            // We've sent at least one image, but we've run out now.
            return AVIF_RESULT_NO_IMAGES_REMAINING;
        }
        return AVIF_RESULT_DECODE_COLOR_FAILED;
    }

    avifCodec * alphaCodec = decoder->data->codec[AVIF_CODEC_PLANES_ALPHA];
    if (alphaCodec) {
        if (!alphaCodec->getNextImage(alphaCodec, decoder->image)) {
            return AVIF_RESULT_DECODE_ALPHA_FAILED;
        }
    } else {
        avifImageFreePlanes(decoder->image, AVIF_PLANES_A);
    }

#if defined(AVIF_FIX_STUDIO_ALPHA)
    if (alphaCodec && alphaCodec->alphaLimitedRange(alphaCodec)) {
        // Naughty! Alpha planes are supposed to be full range. Correct that here.
        avifImageCopyDecoderAlpha(decoder->image);
        if (avifImageUsesU16(decoder->image)) {
            for (uint32_t j = 0; j < decoder->image->height; ++j) {
                for (uint32_t i = 0; i < decoder->image->height; ++i) {
                    uint16_t * alpha = (uint16_t *)&decoder->image->alphaPlane[(i * 2) + (j * decoder->image->alphaRowBytes)];
                    *alpha = (uint16_t)avifLimitedToFullY(decoder->image->depth, *alpha);
                }
            }
        } else {
            for (uint32_t j = 0; j < decoder->image->height; ++j) {
                for (uint32_t i = 0; i < decoder->image->height; ++i) {
                    uint8_t * alpha = &decoder->image->alphaPlane[i + (j * decoder->image->alphaRowBytes)];
                    *alpha = (uint8_t)avifLimitedToFullY(decoder->image->depth, *alpha);
                }
            }
        }
    }
#endif

    ++decoder->imageIndex;
    if (decoder->data->sourceSampleTable) {
        // Decoding from a track! Provide timing information.

        decoder->imageTiming.timescale = decoder->timescale;
        decoder->imageTiming.ptsInTimescales += decoder->imageTiming.durationInTimescales;
        decoder->imageTiming.durationInTimescales = avifSampleTableGetImageDelta(decoder->data->sourceSampleTable, decoder->imageIndex);

        if (decoder->imageTiming.timescale > 0) {
            decoder->imageTiming.pts = (double)decoder->imageTiming.ptsInTimescales / (double)decoder->imageTiming.timescale;
            decoder->imageTiming.duration = (double)decoder->imageTiming.durationInTimescales / (double)decoder->imageTiming.timescale;
        } else {
            decoder->imageTiming.pts = 0.0;
            decoder->imageTiming.duration = 0.0;
        }
    }
    return AVIF_RESULT_OK;
}

avifResult avifDecoderRead(avifDecoder * decoder, avifImage * image, avifRawData * input)
{
    avifResult result = avifDecoderParse(decoder, input);
    if (result != AVIF_RESULT_OK) {
        return result;
    }
    result = avifDecoderNextImage(decoder);
    if (result != AVIF_RESULT_OK) {
        return result;
    }
    if (!decoder->image) {
        return AVIF_RESULT_NO_IMAGES_REMAINING;
    }
    avifImageCopy(image, decoder->image);
    return AVIF_RESULT_OK;
}
