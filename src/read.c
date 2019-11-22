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
#define CONTENTTYPE_SIZE 64
#define MAX_COMPATIBLE_BRANDS 32

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

static const char xmpContentType[] = CONTENT_TYPE_XMP;
static const size_t xmpContentTypeSize = sizeof(xmpContentType);

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

// infe mime content_type
typedef struct avifContentType
{
    char contentType[CONTENTTYPE_SIZE];
} avifContentType;

// colr
typedef struct avifColourInformationBox
{
    avifProfileFormat format;
    const uint8_t * icc;
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
    uint32_t idatID; // If non-zero, offset is relative to this idat box (iloc construction_method==1)
    avifBool ispePresent;
    avifImageSpatialExtents ispe;
    avifBool auxCPresent;
    avifAuxiliaryType auxC;
    avifContentType contentType;
    avifBool colrPresent;
    avifColourInformationBox colr;
    avifBool av1CPresent;
    avifCodecConfigurationBox av1C;
    uint32_t thumbnailForID; // if non-zero, this item is a thumbnail for Item #{thumbnailForID}
    uint32_t auxForID;       // if non-zero, this item is an auxC plane for Item #{auxForID}
    uint32_t descForID;      // if non-zero, this item is a content description for Item #{descForID}
} avifItem;
AVIF_ARRAY_DECLARE(avifItemArray, avifItem, item);

// Temporary storage for ipco contents until they can be associated and memcpy'd to an avifItem
typedef struct avifProperty
{
    uint8_t type[4];
    avifImageSpatialExtents ispe;
    avifAuxiliaryType auxC;
    avifColourInformationBox colr;
    avifCodecConfigurationBox av1C;
} avifProperty;
AVIF_ARRAY_DECLARE(avifPropertyArray, avifProperty, prop);

// idat storage
typedef struct avifItemData
{
    uint32_t id;
    avifROData data;
} avifItemData;
AVIF_ARRAY_DECLARE(avifItemDataArray, avifItemData, idat);

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

typedef struct avifSyncSample
{
    uint32_t sampleNumber;
} avifSyncSample;
AVIF_ARRAY_DECLARE(avifSyncSampleArray, avifSyncSample, syncSample);

typedef struct avifSampleDescription
{
    uint8_t format[4];
    avifBool av1CPresent;
    avifCodecConfigurationBox av1C;
} avifSampleDescription;
AVIF_ARRAY_DECLARE(avifSampleDescriptionArray, avifSampleDescription, description);

typedef struct avifSampleTable
{
    avifSampleTableChunkArray chunks;
    avifSampleDescriptionArray sampleDescriptions;
    avifSampleTableSampleToChunkArray sampleToChunks;
    avifSampleTableSampleSizeArray sampleSizes;
    avifSampleTableTimeToSampleArray timeToSamples;
    avifSyncSampleArray syncSamples;
} avifSampleTable;

static avifSampleTable * avifSampleTableCreate()
{
    avifSampleTable * sampleTable = (avifSampleTable *)avifAlloc(sizeof(avifSampleTable));
    memset(sampleTable, 0, sizeof(avifSampleTable));
    avifArrayCreate(&sampleTable->chunks, sizeof(avifSampleTableChunk), 16);
    avifArrayCreate(&sampleTable->sampleDescriptions, sizeof(avifSampleDescription), 2);
    avifArrayCreate(&sampleTable->sampleToChunks, sizeof(avifSampleTableSampleToChunk), 16);
    avifArrayCreate(&sampleTable->sampleSizes, sizeof(avifSampleTableSampleSize), 16);
    avifArrayCreate(&sampleTable->timeToSamples, sizeof(avifSampleTableTimeToSample), 16);
    avifArrayCreate(&sampleTable->syncSamples, sizeof(avifSyncSample), 16);
    return sampleTable;
}

static void avifSampleTableDestroy(avifSampleTable * sampleTable)
{
    avifArrayDestroy(&sampleTable->chunks);
    avifArrayDestroy(&sampleTable->sampleDescriptions);
    avifArrayDestroy(&sampleTable->sampleToChunks);
    avifArrayDestroy(&sampleTable->sampleSizes);
    avifArrayDestroy(&sampleTable->timeToSamples);
    avifArrayDestroy(&sampleTable->syncSamples);
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

static avifBool avifSampleTableHasFormat(avifSampleTable * sampleTable, const char * format)
{
    for (uint32_t i = 0; i < sampleTable->sampleDescriptions.count; ++i) {
        if (!memcmp(sampleTable->sampleDescriptions.description[i].format, format, 4)) {
            return AVIF_TRUE;
        }
    }
    return AVIF_FALSE;
}

static uint32_t avifCodecConfigurationBoxGetDepth(avifCodecConfigurationBox * av1C)
{
    if (av1C->twelveBit) {
        return 12;
    } else if (av1C->highBitdepth) {
        return 10;
    }
    return 8;
}

static uint32_t avifSampleTableGetDepth(avifSampleTable * sampleTable)
{
    for (uint32_t i = 0; i < sampleTable->sampleDescriptions.count; ++i) {
        avifSampleDescription * description = &sampleTable->sampleDescriptions.description[i];
        if (!memcmp(description->format, "av01", 4) && description->av1CPresent) {
            return avifCodecConfigurationBoxGetDepth(&description->av1C);
        }
    }
    return 0;
}

// one video track ("trak" contents)
typedef struct avifTrack
{
    uint32_t id;
    uint32_t auxForID; // if non-zero, this item is an auxC plane for Track #{auxForID}
    uint32_t mediaTimescale;
    uint64_t mediaDuration;
    uint32_t width;
    uint32_t height;
    avifSampleTable * sampleTable;
} avifTrack;
AVIF_ARRAY_DECLARE(avifTrackArray, avifTrack, track);

// ---------------------------------------------------------------------------
// avifCodecDecodeInput

avifCodecDecodeInput * avifCodecDecodeInputCreate(void)
{
    avifCodecDecodeInput * decodeInput = (avifCodecDecodeInput *)avifAlloc(sizeof(avifCodecDecodeInput));
    memset(decodeInput, 0, sizeof(avifCodecDecodeInput));
    avifArrayCreate(&decodeInput->samples, sizeof(avifSample), 1);
    return decodeInput;
}

void avifCodecDecodeInputDestroy(avifCodecDecodeInput * decodeInput)
{
    avifArrayDestroy(&decodeInput->samples);
    avifFree(decodeInput);
}

static avifBool avifCodecDecodeInputGetSamples(avifCodecDecodeInput * decodeInput, avifSampleTable * sampleTable, avifROData * rawInput)
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

            avifSample * sample = (avifSample *)avifArrayPushPtr(&decodeInput->samples);
            sample->data.data = rawInput->data + sampleOffset;
            sample->data.size = sampleSize->size;
            sample->sync = AVIF_FALSE; // to potentially be set to true following the outer loop

            if (sampleOffset > (uint64_t)rawInput->size) {
                return AVIF_FALSE;
            }

            sampleOffset += sampleSize->size;
            ++sampleSizeIndex;
        }
    }

    // Mark appropriate samples as sync
    for (uint32_t syncSampleIndex = 0; syncSampleIndex < sampleTable->syncSamples.count; ++syncSampleIndex) {
        uint32_t frameIndex = sampleTable->syncSamples.syncSample[syncSampleIndex].sampleNumber - 1; // sampleNumber is 1-based
        if (frameIndex < decodeInput->samples.count) {
            decodeInput->samples.sample[frameIndex].sync = AVIF_TRUE;
        }
    }

    // Assume frame 0 is sync, just in case the stss box is absent in the BMFF. (Unnecessary?)
    if (decodeInput->samples.count > 0) {
        decodeInput->samples.sample[0].sync = AVIF_TRUE;
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
    avifItemDataArray idats;
    avifTrackArray tracks;
    avifROData rawInput;
    avifCodecDecodeInput * colorInput;
    avifCodecDecodeInput * alphaInput;
    avifDecoderSource source;
    avifSampleTable * sourceSampleTable; // NULL unless (source == AVIF_DECODER_SOURCE_TRACKS), owned by an avifTrack
    uint32_t primaryItemID;
    uint32_t metaBoxID; // Ever-incrementing ID for tracking which 'meta' box contains an idat, and which idat an iloc might refer to
    struct avifCodec * codec[AVIF_CODEC_PLANES_COUNT];
} avifData;

static avifData * avifDataCreate()
{
    avifData * data = (avifData *)avifAlloc(sizeof(avifData));
    memset(data, 0, sizeof(avifData));
    avifArrayCreate(&data->items, sizeof(avifItem), 8);
    avifArrayCreate(&data->properties, sizeof(avifProperty), 16);
    avifArrayCreate(&data->idats, sizeof(avifItemData), 1);
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
    avifArrayDestroy(&data->idats);
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

static const uint8_t * avifDataCalcItemPtr(avifData * data, avifItem * item)
{
    avifROData * offsetBuffer = NULL;
    if (item->idatID == 0) {
        // construction_method: file(0)

        offsetBuffer = &data->rawInput;
    } else {
        // construction_method: idat(1)

        // Find associated idat block
        for (uint32_t i = 0; i < data->idats.count; ++i) {
            if (data->idats.idat[i].id == item->idatID) {
                offsetBuffer = &data->idats.idat[i].data;
                break;
            }
        }

        if (offsetBuffer == NULL) {
            // no idat box was found in this meta box, bail out
            return NULL;
        }
    }

    if (item->offset > offsetBuffer->size) {
        return NULL;
    }
    uint64_t offsetSize = (uint64_t)item->offset + (uint64_t)item->size;
    if (offsetSize > (uint64_t)offsetBuffer->size) {
        return NULL;
    }
    return offsetBuffer->data + item->offset;
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
    avifROStream VARNAME;                \
    avifROData VARNAME##_roData;         \
    VARNAME##_roData.data = PTR;         \
    VARNAME##_roData.size = SIZE;        \
    avifROStreamStart(&VARNAME, &VARNAME##_roData)

static avifBool avifParseItemLocationBox(avifData * data, const uint8_t * raw, size_t rawLen)
{
    BEGIN_STREAM(s, raw, rawLen);

    uint8_t version;
    uint8_t flags[3];
    CHECK(avifROStreamReadVersionAndFlags(&s, &version, flags));
    if (version > 2) {
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
        uint32_t idatID = 0;
        if (version < 2) {
            CHECK(avifROStreamReadU16(&s, &tmp16)); // unsigned int(16) item_ID;
            itemID = tmp16;
        } else {
            CHECK(avifROStreamReadU32(&s, &itemID)); // unsigned int(32) item_ID;
        }

        if ((version == 1) || (version == 2)) {
            uint8_t ignored;
            uint8_t constructionMethod;
            CHECK(avifROStreamRead(&s, &ignored, 1));            // unsigned int(12) reserved = 0;
            CHECK(avifROStreamRead(&s, &constructionMethod, 1)); // unsigned int(4) construction_method;
            constructionMethod = constructionMethod & 0xf;
            if ((constructionMethod != 0 /* file */) && (constructionMethod != 1 /* idat */)) {
                // construction method item(2) unsupported
                return AVIF_FALSE;
            }
            if (constructionMethod == 1) {
                idatID = data->metaBoxID;
            }
        }

        uint16_t dataReferenceIndex;                                 // unsigned int(16) data_ref rence_index;
        CHECK(avifROStreamReadU16(&s, &dataReferenceIndex));         //
        uint64_t baseOffset;                                         // unsigned int(base_offset_size*8) base_offset;
        CHECK(avifROStreamReadUX8(&s, &baseOffset, baseOffsetSize)); //
        uint16_t extentCount;                                        // unsigned int(16) extent_count;
        CHECK(avifROStreamReadU16(&s, &extentCount));                //
        if (extentCount == 1) {
            // If extent_index is ever supported, this spec must be implemented here:
            // ::  if (((version == 1) || (version == 2)) && (index_size > 0)) {
            // ::      unsigned int(index_size*8) extent_index;
            // ::  }

            uint64_t extentOffset; // unsigned int(offset_size*8) extent_offset;
            CHECK(avifROStreamReadUX8(&s, &extentOffset, offsetSize));
            uint64_t extentLength; // unsigned int(offset_size*8) extent_length;
            CHECK(avifROStreamReadUX8(&s, &extentLength, lengthSize));

            avifItem * item = avifDataFindItem(data, itemID);
            if (!item) {
                return AVIF_FALSE;
            }
            item->id = itemID;
            item->offset = (uint32_t)(baseOffset + extentOffset);
            item->size = (uint32_t)extentLength;
            item->idatID = idatID;
        } else {
            // TODO: support more than one extent
            return AVIF_FALSE;
        }
    }
    return AVIF_TRUE;
}

static avifBool avifParseImageSpatialExtentsProperty(avifData * data, const uint8_t * raw, size_t rawLen, int propertyIndex)
{
    BEGIN_STREAM(s, raw, rawLen);
    CHECK(avifROStreamReadAndEnforceVersion(&s, 0));

    CHECK(avifROStreamReadU32(&s, &data->properties.prop[propertyIndex].ispe.width));
    CHECK(avifROStreamReadU32(&s, &data->properties.prop[propertyIndex].ispe.height));
    return AVIF_TRUE;
}

static avifBool avifParseAuxiliaryTypeProperty(avifData * data, const uint8_t * raw, size_t rawLen, int propertyIndex)
{
    BEGIN_STREAM(s, raw, rawLen);
    CHECK(avifROStreamReadAndEnforceVersion(&s, 0));

    CHECK(avifROStreamReadString(&s, data->properties.prop[propertyIndex].auxC.auxType, AUXTYPE_SIZE));
    return AVIF_TRUE;
}

static avifBool avifParseColourInformationBox(avifData * data, const uint8_t * raw, size_t rawLen, int propertyIndex)
{
    BEGIN_STREAM(s, raw, rawLen);

    data->properties.prop[propertyIndex].colr.format = AVIF_PROFILE_FORMAT_NONE;

    uint8_t colourType[4]; // unsigned int(32) colour_type;
    CHECK(avifROStreamRead(&s, colourType, 4));
    if (!memcmp(colourType, "rICC", 4) || !memcmp(colourType, "prof", 4)) {
        data->properties.prop[propertyIndex].colr.format = AVIF_PROFILE_FORMAT_ICC;
        data->properties.prop[propertyIndex].colr.icc = avifROStreamCurrent(&s);
        data->properties.prop[propertyIndex].colr.iccSize = avifROStreamRemainingBytes(&s);
    } else if (!memcmp(colourType, "nclx", 4)) {
        // unsigned int(16) colour_primaries;
        CHECK(avifROStreamReadU16(&s, &data->properties.prop[propertyIndex].colr.nclx.colourPrimaries));
        // unsigned int(16) transfer_characteristics;
        CHECK(avifROStreamReadU16(&s, &data->properties.prop[propertyIndex].colr.nclx.transferCharacteristics));
        // unsigned int(16) matrix_coefficients;
        CHECK(avifROStreamReadU16(&s, &data->properties.prop[propertyIndex].colr.nclx.matrixCoefficients));
        // unsigned int(1) full_range_flag;
        // unsigned int(7) reserved = 0;
        CHECK(avifROStreamRead(&s, &data->properties.prop[propertyIndex].colr.nclx.fullRangeFlag, 1));
        data->properties.prop[propertyIndex].colr.nclx.fullRangeFlag |= 0x80;
        data->properties.prop[propertyIndex].colr.format = AVIF_PROFILE_FORMAT_NCLX;
    }
    return AVIF_TRUE;
}

static avifBool avifParseAV1CodecConfigurationBox(const uint8_t * raw, size_t rawLen, avifCodecConfigurationBox * av1C)
{
    BEGIN_STREAM(s, raw, rawLen);

    uint8_t markerAndVersion = 0;
    CHECK(avifROStreamRead(&s, &markerAndVersion, 1));
    uint8_t seqProfileAndIndex = 0;
    CHECK(avifROStreamRead(&s, &seqProfileAndIndex, 1));
    uint8_t rawFlags = 0;
    CHECK(avifROStreamRead(&s, &rawFlags, 1));

    if (markerAndVersion != 0x81) {
        // Marker and version must both == 1
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

static avifBool avifParseAV1CodecConfigurationBoxProperty(avifData * data, const uint8_t * raw, size_t rawLen, int propertyIndex)
{
    return avifParseAV1CodecConfigurationBox(raw, rawLen, &data->properties.prop[propertyIndex].av1C);
}

static avifBool avifParseItemPropertyContainerBox(avifData * data, const uint8_t * raw, size_t rawLen)
{
    BEGIN_STREAM(s, raw, rawLen);

    while (avifROStreamHasBytesLeft(&s, 1)) {
        avifBoxHeader header;
        CHECK(avifROStreamReadBoxHeader(&s, &header));

        int propertyIndex = avifArrayPushIndex(&data->properties);
        memcpy(data->properties.prop[propertyIndex].type, header.type, 4);
        if (!memcmp(header.type, "ispe", 4)) {
            CHECK(avifParseImageSpatialExtentsProperty(data, avifROStreamCurrent(&s), header.size, propertyIndex));
        }
        if (!memcmp(header.type, "auxC", 4)) {
            CHECK(avifParseAuxiliaryTypeProperty(data, avifROStreamCurrent(&s), header.size, propertyIndex));
        }
        if (!memcmp(header.type, "colr", 4)) {
            CHECK(avifParseColourInformationBox(data, avifROStreamCurrent(&s), header.size, propertyIndex));
        }
        if (!memcmp(header.type, "av1C", 4)) {
            CHECK(avifParseAV1CodecConfigurationBoxProperty(data, avifROStreamCurrent(&s), header.size, propertyIndex));
        }

        CHECK(avifROStreamSkip(&s, header.size));
    }
    return AVIF_TRUE;
}

static avifBool avifParseItemPropertyAssociation(avifData * data, const uint8_t * raw, size_t rawLen)
{
    BEGIN_STREAM(s, raw, rawLen);

    uint8_t version;
    uint8_t flags[3];
    CHECK(avifROStreamReadVersionAndFlags(&s, &version, flags));
    avifBool propertyIndexIsU16 = (flags[2] & 0x1) ? AVIF_TRUE : AVIF_FALSE; // is flags[2] correct?

    uint32_t entryCount;
    CHECK(avifROStreamReadU32(&s, &entryCount));
    for (uint32_t entryIndex = 0; entryIndex < entryCount; ++entryIndex) {
        unsigned int itemID;
        if (version < 1) {
            uint16_t tmp;
            CHECK(avifROStreamReadU16(&s, &tmp));
            itemID = tmp;
        } else {
            CHECK(avifROStreamReadU32(&s, &itemID));
        }
        uint8_t associationCount;
        CHECK(avifROStreamRead(&s, &associationCount, 1));
        for (uint8_t associationIndex = 0; associationIndex < associationCount; ++associationIndex) {
            // avifBool essential = AVIF_FALSE; // currently unused
            uint16_t propertyIndex = 0;
            if (propertyIndexIsU16) {
                CHECK(avifROStreamReadU16(&s, &propertyIndex));
                // essential = (propertyIndex & 0x8000) ? AVIF_TRUE : AVIF_FALSE;
                propertyIndex &= 0x7fff;
            } else {
                uint8_t tmp;
                CHECK(avifROStreamRead(&s, &tmp, 1));
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
            } else if (!memcmp(prop->type, "av1C", 4)) {
                item->av1CPresent = AVIF_TRUE;
                memcpy(&item->av1C, &prop->av1C, sizeof(avifCodecConfigurationBox));
            }
        }
    }

    return AVIF_TRUE;
}

static avifBool avifParsePrimaryItemBox(avifData * data, const uint8_t * raw, size_t rawLen)
{
    if (data->primaryItemID > 0) {
        // Illegal to have multiple pitm boxes, bail out
        return AVIF_FALSE;
    }

    BEGIN_STREAM(s, raw, rawLen);

    uint8_t version;
    CHECK(avifROStreamReadVersionAndFlags(&s, &version, NULL));

    if (version == 0) {
        uint16_t tmp16;
        CHECK(avifROStreamReadU16(&s, &tmp16)); // unsigned int(16) item_ID;
        data->primaryItemID = tmp16;
    } else {
        CHECK(avifROStreamReadU32(&s, &data->primaryItemID)); // unsigned int(32) item_ID;
    }
    return AVIF_TRUE;
}

static avifBool avifParseItemDataBox(avifData * data, const uint8_t * raw, size_t rawLen)
{
    uint32_t idatID = data->metaBoxID;

    // Check to see if we've already seen an idat box for this meta box. If so, bail out
    for (uint32_t i = 0; i < data->idats.count; ++i) {
        if (data->idats.idat[i].id == idatID) {
            return AVIF_FALSE;
        }
    }

    int index = avifArrayPushIndex(&data->idats);
    avifItemData * idat = &data->idats.idat[index];
    idat->id = idatID;
    idat->data.data = raw;
    idat->data.size = rawLen;
    return AVIF_TRUE;
}

static avifBool avifParseItemPropertiesBox(avifData * data, const uint8_t * raw, size_t rawLen)
{
    BEGIN_STREAM(s, raw, rawLen);

    avifBoxHeader ipcoHeader;
    CHECK(avifROStreamReadBoxHeader(&s, &ipcoHeader));
    if (memcmp(ipcoHeader.type, "ipco", 4) != 0) {
        return AVIF_FALSE;
    }

    // Read all item properties inside of ItemPropertyContainerBox
    CHECK(avifParseItemPropertyContainerBox(data, avifROStreamCurrent(&s), ipcoHeader.size));
    CHECK(avifROStreamSkip(&s, ipcoHeader.size));

    // Now read all ItemPropertyAssociation until the end of the box, and make associations
    while (avifROStreamHasBytesLeft(&s, 1)) {
        avifBoxHeader ipmaHeader;
        CHECK(avifROStreamReadBoxHeader(&s, &ipmaHeader));

        if (!memcmp(ipmaHeader.type, "ipma", 4)) {
            CHECK(avifParseItemPropertyAssociation(data, avifROStreamCurrent(&s), ipmaHeader.size));
        } else {
            // These must all be type ipma
            return AVIF_FALSE;
        }

        CHECK(avifROStreamSkip(&s, ipmaHeader.size));
    }
    return AVIF_TRUE;
}

static avifBool avifParseItemInfoEntry(avifData * data, const uint8_t * raw, size_t rawLen)
{
    BEGIN_STREAM(s, raw, rawLen);

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

    avifItem * item = avifDataFindItem(data, itemID);
    if (!item) {
        return AVIF_FALSE;
    }

    memcpy(item->type, itemType, sizeof(itemType));
    memcpy(&item->contentType, &contentType, sizeof(contentType));
    return AVIF_TRUE;
}

static avifBool avifParseItemInfoBox(avifData * data, const uint8_t * raw, size_t rawLen)
{
    BEGIN_STREAM(s, raw, rawLen);

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
        return AVIF_FALSE;
    }

    for (uint32_t entryIndex = 0; entryIndex < entryCount; ++entryIndex) {
        avifBoxHeader infeHeader;
        CHECK(avifROStreamReadBoxHeader(&s, &infeHeader));

        if (!memcmp(infeHeader.type, "infe", 4)) {
            CHECK(avifParseItemInfoEntry(data, avifROStreamCurrent(&s), infeHeader.size));
        } else {
            // These must all be type ipma
            return AVIF_FALSE;
        }

        CHECK(avifROStreamSkip(&s, infeHeader.size));
    }

    return AVIF_TRUE;
}

static avifBool avifParseItemReferenceBox(avifData * data, const uint8_t * raw, size_t rawLen)
{
    BEGIN_STREAM(s, raw, rawLen);

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
                if (!memcmp(irefHeader.type, "cdsc", 4)) {
                    item->descForID = toID;
                }
            }
        }
    }

    return AVIF_TRUE;
}

static avifBool avifParseMetaBox(avifData * data, const uint8_t * raw, size_t rawLen)
{
    BEGIN_STREAM(s, raw, rawLen);

    CHECK(avifROStreamReadAndEnforceVersion(&s, 0));

    ++data->metaBoxID; // for tracking idat

    while (avifROStreamHasBytesLeft(&s, 1)) {
        avifBoxHeader header;
        CHECK(avifROStreamReadBoxHeader(&s, &header));

        if (!memcmp(header.type, "iloc", 4)) {
            CHECK(avifParseItemLocationBox(data, avifROStreamCurrent(&s), header.size));
        } else if (!memcmp(header.type, "pitm", 4)) {
            CHECK(avifParsePrimaryItemBox(data, avifROStreamCurrent(&s), header.size));
        } else if (!memcmp(header.type, "idat", 4)) {
            CHECK(avifParseItemDataBox(data, avifROStreamCurrent(&s), header.size));
        } else if (!memcmp(header.type, "iprp", 4)) {
            CHECK(avifParseItemPropertiesBox(data, avifROStreamCurrent(&s), header.size));
        } else if (!memcmp(header.type, "iinf", 4)) {
            CHECK(avifParseItemInfoBox(data, avifROStreamCurrent(&s), header.size));
        } else if (!memcmp(header.type, "iref", 4)) {
            CHECK(avifParseItemReferenceBox(data, avifROStreamCurrent(&s), header.size));
        }

        CHECK(avifROStreamSkip(&s, header.size));
    }
    return AVIF_TRUE;
}

static avifBool avifParseTrackHeaderBox(avifData * data, avifTrack * track, const uint8_t * raw, size_t rawLen)
{
    BEGIN_STREAM(s, raw, rawLen);
    (void)data;

    uint8_t version;
    uint8_t flags[3];
    CHECK(avifROStreamReadVersionAndFlags(&s, &version, flags));

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

    // TODO: support scaling based on width/height track header info?

    track->id = trackID;
    return AVIF_TRUE;
}

static avifBool avifParseMediaHeaderBox(avifData * data, avifTrack * track, const uint8_t * raw, size_t rawLen)
{
    BEGIN_STREAM(s, raw, rawLen);
    (void)data;

    uint8_t version;
    uint8_t flags[3];
    CHECK(avifROStreamReadVersionAndFlags(&s, &version, flags));

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
        return AVIF_FALSE;
    }

    track->mediaTimescale = mediaTimescale;
    return AVIF_TRUE;
}

static avifBool avifParseChunkOffsetBox(avifData * data, avifSampleTable * sampleTable, avifBool largeOffsets, const uint8_t * raw, size_t rawLen)
{
    BEGIN_STREAM(s, raw, rawLen);
    (void)data;

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

static avifBool avifParseSampleToChunkBox(avifData * data, avifSampleTable * sampleTable, const uint8_t * raw, size_t rawLen)
{
    BEGIN_STREAM(s, raw, rawLen);
    (void)data;

    CHECK(avifROStreamReadAndEnforceVersion(&s, 0));

    uint32_t entryCount;
    CHECK(avifROStreamReadU32(&s, &entryCount)); // unsigned int(32) entry_count;
    for (uint32_t i = 0; i < entryCount; ++i) {
        avifSampleTableSampleToChunk * sampleToChunk = (avifSampleTableSampleToChunk *)avifArrayPushPtr(&sampleTable->sampleToChunks);
        CHECK(avifROStreamReadU32(&s, &sampleToChunk->firstChunk));             // unsigned int(32) first_chunk;
        CHECK(avifROStreamReadU32(&s, &sampleToChunk->samplesPerChunk));        // unsigned int(32) samples_per_chunk;
        CHECK(avifROStreamReadU32(&s, &sampleToChunk->sampleDescriptionIndex)); // unsigned int(32) sample_description_index;
    }
    return AVIF_TRUE;
}

static avifBool avifParseSampleSizeBox(avifData * data, avifSampleTable * sampleTable, const uint8_t * raw, size_t rawLen)
{
    BEGIN_STREAM(s, raw, rawLen);
    (void)data;

    CHECK(avifROStreamReadAndEnforceVersion(&s, 0));

    uint32_t allSamplesSize, entryCount;
    CHECK(avifROStreamReadU32(&s, &allSamplesSize)); // unsigned int(32) sample_size;
    CHECK(avifROStreamReadU32(&s, &entryCount));     // unsigned int(32) entry_count;

    for (uint32_t i = 0; i < entryCount; ++i) {
        avifSampleTableSampleSize * sampleSize = (avifSampleTableSampleSize *)avifArrayPushPtr(&sampleTable->sampleSizes);
        if (allSamplesSize == 0) {
            CHECK(avifROStreamReadU32(&s, &sampleSize->size)); // unsigned int(32) entry_size;
        } else {
            // This could be done more efficiently, memory-wise.
            sampleSize->size = allSamplesSize;
        }
    }
    return AVIF_TRUE;
}

static avifBool avifParseSyncSampleBox(avifData * data, avifSampleTable * sampleTable, const uint8_t * raw, size_t rawLen)
{
    BEGIN_STREAM(s, raw, rawLen);
    (void)data;

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

static avifBool avifParseTimeToSampleBox(avifData * data, avifSampleTable * sampleTable, const uint8_t * raw, size_t rawLen)
{
    BEGIN_STREAM(s, raw, rawLen);
    (void)data;

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

static avifBool avifParseSampleDescriptionBox(avifData * data, avifSampleTable * sampleTable, const uint8_t * raw, size_t rawLen)
{
    BEGIN_STREAM(s, raw, rawLen);
    (void)data;

    CHECK(avifROStreamReadAndEnforceVersion(&s, 0));

    uint32_t entryCount;
    CHECK(avifROStreamReadU32(&s, &entryCount)); // unsigned int(32) entry_count;

    for (uint32_t i = 0; i < entryCount; ++i) {
        avifBoxHeader sampleEntryHeader;
        CHECK(avifROStreamReadBoxHeader(&s, &sampleEntryHeader));

        avifSampleDescription * description = (avifSampleDescription *)avifArrayPushPtr(&sampleTable->sampleDescriptions);
        memcpy(description->format, sampleEntryHeader.type, sizeof(description->format));
        size_t remainingBytes = avifROStreamRemainingBytes(&s);
        if (!memcmp(description->format, "av01", 4) && (remainingBytes > VISUALSAMPLEENTRY_SIZE)) {
            BEGIN_STREAM(av01Stream, avifROStreamCurrent(&s) + VISUALSAMPLEENTRY_SIZE, remainingBytes - VISUALSAMPLEENTRY_SIZE);
            while (avifROStreamHasBytesLeft(&av01Stream, 1)) {
                avifBoxHeader av01ChildHeader;
                CHECK(avifROStreamReadBoxHeader(&av01Stream, &av01ChildHeader));

                if (!memcmp(av01ChildHeader.type, "av1C", 4)) {
                    CHECK(avifParseAV1CodecConfigurationBox(avifROStreamCurrent(&av01Stream), av01ChildHeader.size, &description->av1C));
                    description->av1CPresent = AVIF_TRUE;
                }

                CHECK(avifROStreamSkip(&av01Stream, av01ChildHeader.size));
            }
        }

        CHECK(avifROStreamSkip(&s, sampleEntryHeader.size));
    }
    return AVIF_TRUE;
}

static avifBool avifParseSampleTableBox(avifData * data, avifTrack * track, const uint8_t * raw, size_t rawLen)
{
    if (track->sampleTable) {
        // A TrackBox may only have one SampleTable
        return AVIF_FALSE;
    }
    track->sampleTable = avifSampleTableCreate();

    BEGIN_STREAM(s, raw, rawLen);

    while (avifROStreamHasBytesLeft(&s, 1)) {
        avifBoxHeader header;
        CHECK(avifROStreamReadBoxHeader(&s, &header));

        if (!memcmp(header.type, "stco", 4)) {
            CHECK(avifParseChunkOffsetBox(data, track->sampleTable, AVIF_FALSE, avifROStreamCurrent(&s), header.size));
        } else if (!memcmp(header.type, "co64", 4)) {
            CHECK(avifParseChunkOffsetBox(data, track->sampleTable, AVIF_TRUE, avifROStreamCurrent(&s), header.size));
        } else if (!memcmp(header.type, "stsc", 4)) {
            CHECK(avifParseSampleToChunkBox(data, track->sampleTable, avifROStreamCurrent(&s), header.size));
        } else if (!memcmp(header.type, "stsz", 4)) {
            CHECK(avifParseSampleSizeBox(data, track->sampleTable, avifROStreamCurrent(&s), header.size));
        } else if (!memcmp(header.type, "stss", 4)) {
            CHECK(avifParseSyncSampleBox(data, track->sampleTable, avifROStreamCurrent(&s), header.size));
        } else if (!memcmp(header.type, "stts", 4)) {
            CHECK(avifParseTimeToSampleBox(data, track->sampleTable, avifROStreamCurrent(&s), header.size));
        } else if (!memcmp(header.type, "stsd", 4)) {
            CHECK(avifParseSampleDescriptionBox(data, track->sampleTable, avifROStreamCurrent(&s), header.size));
        }

        CHECK(avifROStreamSkip(&s, header.size));
    }
    return AVIF_TRUE;
}

static avifBool avifParseMediaInformationBox(avifData * data, avifTrack * track, const uint8_t * raw, size_t rawLen)
{
    BEGIN_STREAM(s, raw, rawLen);

    while (avifROStreamHasBytesLeft(&s, 1)) {
        avifBoxHeader header;
        CHECK(avifROStreamReadBoxHeader(&s, &header));

        if (!memcmp(header.type, "stbl", 4)) {
            CHECK(avifParseSampleTableBox(data, track, avifROStreamCurrent(&s), header.size));
        }

        CHECK(avifROStreamSkip(&s, header.size));
    }
    return AVIF_TRUE;
}

static avifBool avifParseMediaBox(avifData * data, avifTrack * track, const uint8_t * raw, size_t rawLen)
{
    BEGIN_STREAM(s, raw, rawLen);

    while (avifROStreamHasBytesLeft(&s, 1)) {
        avifBoxHeader header;
        CHECK(avifROStreamReadBoxHeader(&s, &header));

        if (!memcmp(header.type, "mdhd", 4)) {
            CHECK(avifParseMediaHeaderBox(data, track, avifROStreamCurrent(&s), header.size));
        } else if (!memcmp(header.type, "minf", 4)) {
            CHECK(avifParseMediaInformationBox(data, track, avifROStreamCurrent(&s), header.size));
        }

        CHECK(avifROStreamSkip(&s, header.size));
    }
    return AVIF_TRUE;
}

static avifBool avifTrackReferenceBox(avifData * data, avifTrack * track, const uint8_t * raw, size_t rawLen)
{
    BEGIN_STREAM(s, raw, rawLen);
    (void)data;

    while (avifROStreamHasBytesLeft(&s, 1)) {
        avifBoxHeader header;
        CHECK(avifROStreamReadBoxHeader(&s, &header));

        if (!memcmp(header.type, "auxl", 4)) {
            uint32_t toID;
            CHECK(avifROStreamReadU32(&s, &toID));                       // unsigned int(32) track_IDs[]
            CHECK(avifROStreamSkip(&s, header.size - sizeof(uint32_t))); // just take the first one
            track->auxForID = toID;
        } else {
            CHECK(avifROStreamSkip(&s, header.size));
        }
    }
    return AVIF_TRUE;
}

static avifBool avifParseTrackBox(avifData * data, const uint8_t * raw, size_t rawLen)
{
    BEGIN_STREAM(s, raw, rawLen);

    avifTrack * track = (avifTrack *)avifArrayPushPtr(&data->tracks);

    while (avifROStreamHasBytesLeft(&s, 1)) {
        avifBoxHeader header;
        CHECK(avifROStreamReadBoxHeader(&s, &header));

        if (!memcmp(header.type, "tkhd", 4)) {
            CHECK(avifParseTrackHeaderBox(data, track, avifROStreamCurrent(&s), header.size));
        } else if (!memcmp(header.type, "mdia", 4)) {
            CHECK(avifParseMediaBox(data, track, avifROStreamCurrent(&s), header.size));
        } else if (!memcmp(header.type, "tref", 4)) {
            CHECK(avifTrackReferenceBox(data, track, avifROStreamCurrent(&s), header.size));
        }

        CHECK(avifROStreamSkip(&s, header.size));
    }
    return AVIF_TRUE;
}

static avifBool avifParseMoovBox(avifData * data, const uint8_t * raw, size_t rawLen)
{
    BEGIN_STREAM(s, raw, rawLen);

    while (avifROStreamHasBytesLeft(&s, 1)) {
        avifBoxHeader header;
        CHECK(avifROStreamReadBoxHeader(&s, &header));

        if (!memcmp(header.type, "trak", 4)) {
            CHECK(avifParseTrackBox(data, avifROStreamCurrent(&s), header.size));
        }

        CHECK(avifROStreamSkip(&s, header.size));
    }
    return AVIF_TRUE;
}

static avifBool avifParseFileTypeBox(avifFileType * ftyp, const uint8_t * raw, size_t rawLen)
{
    BEGIN_STREAM(s, raw, rawLen);

    CHECK(avifROStreamRead(&s, ftyp->majorBrand, 4));
    CHECK(avifROStreamReadU32(&s, &ftyp->minorVersion));

    size_t compatibleBrandsBytes = avifROStreamRemainingBytes(&s);
    if ((compatibleBrandsBytes % 4) != 0) {
        return AVIF_FALSE;
    }
    if (compatibleBrandsBytes > (4 * MAX_COMPATIBLE_BRANDS)) {
        // TODO: stop clamping and resize this
        compatibleBrandsBytes = (4 * MAX_COMPATIBLE_BRANDS);
    }
    CHECK(avifROStreamRead(&s, ftyp->compatibleBrands, compatibleBrandsBytes));
    ftyp->compatibleBrandsCount = (int)compatibleBrandsBytes / 4;

    return AVIF_TRUE;
}

static avifBool avifParse(avifData * data, const uint8_t * raw, size_t rawLen)
{
    BEGIN_STREAM(s, raw, rawLen);

    while (avifROStreamHasBytesLeft(&s, 1)) {
        avifBoxHeader header;
        CHECK(avifROStreamReadBoxHeader(&s, &header));

        if (!memcmp(header.type, "ftyp", 4)) {
            CHECK(avifParseFileTypeBox(&data->ftyp, avifROStreamCurrent(&s), header.size));
        } else if (!memcmp(header.type, "meta", 4)) {
            CHECK(avifParseMetaBox(data, avifROStreamCurrent(&s), header.size));
        } else if (!memcmp(header.type, "moov", 4)) {
            CHECK(avifParseMoovBox(data, avifROStreamCurrent(&s), header.size));
        }

        CHECK(avifROStreamSkip(&s, header.size));
    }
    return AVIF_TRUE;
}

// ---------------------------------------------------------------------------

static avifBool avifFileTypeIsCompatible(avifFileType * ftyp)
{
    avifBool avifCompatible = (memcmp(ftyp->majorBrand, "avif", 4) == 0) ? AVIF_TRUE : AVIF_FALSE;
    if (!avifCompatible) {
        avifCompatible = (memcmp(ftyp->majorBrand, "avis", 4) == 0) ? AVIF_TRUE : AVIF_FALSE;
    }
    if (!avifCompatible) {
        avifCompatible = (memcmp(ftyp->majorBrand, "av01", 4) == 0) ? AVIF_TRUE : AVIF_FALSE;
    }
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
            if (!memcmp(compatibleBrand, "av01", 4)) {
                avifCompatible = AVIF_TRUE;
                break;
            }
        }
    }
    return avifCompatible;
}

avifBool avifPeekCompatibleFileType(avifROData * input)
{
    BEGIN_STREAM(s, input->data, input->size);

    avifBoxHeader header;
    CHECK(avifROStreamReadBoxHeader(&s, &header));
    if (memcmp(header.type, "ftyp", 4) != 0) {
        return AVIF_FALSE;
    }

    avifFileType ftyp;
    memset(&ftyp, 0, sizeof(avifFileType));
    avifBool parsed = avifParseFileTypeBox(&ftyp, avifROStreamCurrent(&s), header.size);
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

avifResult avifDecoderParse(avifDecoder * decoder, avifROData * rawInput)
{
    // Cleanup anything lingering in the decoder
    avifDecoderCleanup(decoder);

    // -----------------------------------------------------------------------
    // Parse BMFF boxes

    decoder->data = avifDataCreate();

    // Shallow copy, on purpose
    memcpy(&decoder->data->rawInput, rawInput, sizeof(avifROData));

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
        const uint8_t * p = avifDataCalcItemPtr(decoder->data, item);
        if (p == NULL) {
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

static avifCodec * avifCodecCreateInternal(avifCodecChoice choice, avifCodecDecodeInput * decodeInput)
{
    avifCodec * codec = avifCodecCreate(choice, AVIF_CODEC_FLAG_CAN_DECODE);
    if (codec) {
        codec->decodeInput = decodeInput;
    }
    return codec;
}

static avifResult avifDecoderFlush(avifDecoder * decoder)
{
    avifDataResetCodec(decoder->data);

    decoder->data->codec[AVIF_CODEC_PLANES_COLOR] = avifCodecCreateInternal(decoder->codecChoice, decoder->data->colorInput);
    if (!decoder->data->codec[AVIF_CODEC_PLANES_COLOR]) {
        return AVIF_RESULT_NO_CODEC_AVAILABLE;
    }
    if (!decoder->data->codec[AVIF_CODEC_PLANES_COLOR]->open(decoder->data->codec[AVIF_CODEC_PLANES_COLOR], decoder->imageIndex + 1)) {
        return AVIF_RESULT_DECODE_COLOR_FAILED;
    }

    if (decoder->data->alphaInput) {
        decoder->data->codec[AVIF_CODEC_PLANES_ALPHA] = avifCodecCreateInternal(decoder->codecChoice, decoder->data->alphaInput);
        if (!decoder->data->codec[AVIF_CODEC_PLANES_ALPHA]) {
            return AVIF_RESULT_NO_CODEC_AVAILABLE;
        }
        if (!decoder->data->codec[AVIF_CODEC_PLANES_ALPHA]->open(decoder->data->codec[AVIF_CODEC_PLANES_ALPHA], decoder->imageIndex + 1)) {
            return AVIF_RESULT_DECODE_ALPHA_FAILED;
        }
    }
    return AVIF_RESULT_OK;
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
            if (!avifSampleTableHasFormat(track->sampleTable, "av01")) {
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
            if (!avifSampleTableHasFormat(track->sampleTable, "av01")) {
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

        decoder->containerWidth = colorTrack->width;
        decoder->containerHeight = colorTrack->height;
        decoder->containerDepth = avifSampleTableGetDepth(colorTrack->sampleTable);
    } else {
        // Create from items

        avifROData colorOBU = AVIF_DATA_EMPTY;
        avifROData alphaOBU = AVIF_DATA_EMPTY;
        avifROData exifData = AVIF_DATA_EMPTY;
        avifROData xmpData = AVIF_DATA_EMPTY;
        avifItem * colorOBUItem = NULL;

        // Find the colorOBU (primary) item
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
            if ((data->primaryItemID > 0) && (item->id != data->primaryItemID)) {
                // a primary item ID was specified, require it
                continue;
            }

            colorOBUItem = item;
            colorOBU.data = avifDataCalcItemPtr(data, item);
            colorOBU.size = item->size;
            break;
        }

        if (colorOBUItem) {
            // Find the alphaOBU item, if any
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
                    alphaOBU.data = avifDataCalcItemPtr(data, item);
                    alphaOBU.size = item->size;
                    break;
                }
            }

            // Find Exif and/or XMP metadata, if any
            for (uint32_t itemIndex = 0; itemIndex < data->items.count; ++itemIndex) {
                avifItem * item = &data->items.item[itemIndex];
                if (!item->id || !item->size) {
                    break;
                }

                if (item->descForID != colorOBUItem->id) {
                    // Not a content description (metadata) for the colorOBU, skip it
                    continue;
                }

                if (!memcmp(item->type, "Exif", 4)) {
                    // Advance past Annex A.2.1's header
                    const uint8_t * boxPtr = avifDataCalcItemPtr(data, item);
                    BEGIN_STREAM(exifBoxStream, boxPtr, item->size);
                    uint32_t exifTiffHeaderOffset;
                    CHECK(avifROStreamReadU32(&exifBoxStream, &exifTiffHeaderOffset)); // unsigned int(32) exif_tiff_header_offset;

                    exifData.data = avifROStreamCurrent(&exifBoxStream);
                    exifData.size = avifROStreamRemainingBytes(&exifBoxStream);
                }

                if (!memcmp(item->type, "mime", 4) && !memcmp(item->contentType.contentType, xmpContentType, xmpContentTypeSize)) {
                    xmpData.data = avifDataCalcItemPtr(data, item);
                    xmpData.size = item->size;
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

        if (exifData.data && exifData.size) {
            avifImageSetMetadataExif(decoder->image, exifData.data, exifData.size);
        }
        if (xmpData.data && xmpData.size) {
            avifImageSetMetadataXMP(decoder->image, xmpData.data, xmpData.size);
        }

        data->colorInput = avifCodecDecodeInputCreate();
        avifSample * colorSample = (avifSample *)avifArrayPushPtr(&data->colorInput->samples);
        memcpy(&colorSample->data, &colorOBU, sizeof(avifROData));
        colorSample->sync = AVIF_TRUE;
        if (alphaOBU.size > 0) {
            data->alphaInput = avifCodecDecodeInputCreate();
            avifSample * alphaSample = (avifSample *)avifArrayPushPtr(&data->alphaInput->samples);
            memcpy(&alphaSample->data, &alphaOBU, sizeof(avifROData));
            alphaSample->sync = AVIF_TRUE;
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

        if (colorOBUItem->ispePresent) {
            decoder->containerWidth = colorOBUItem->ispe.width;
            decoder->containerHeight = colorOBUItem->ispe.height;
        } else {
            decoder->containerWidth = 0;
            decoder->containerHeight = 0;
        }
        if (colorOBUItem->av1CPresent) {
            decoder->containerDepth = avifCodecConfigurationBoxGetDepth(&colorOBUItem->av1C);
        } else {
            decoder->containerDepth = 0;
        }
    }

    return avifDecoderFlush(decoder);
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
        decoder->imageTiming.ptsInTimescales = 0;
        for (int imageIndex = 0; imageIndex < decoder->imageIndex; ++imageIndex) {
            decoder->imageTiming.ptsInTimescales += avifSampleTableGetImageDelta(decoder->data->sourceSampleTable, imageIndex);
        }
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

avifResult avifDecoderNthImage(avifDecoder * decoder, uint32_t frameIndex)
{
    int requestedIndex = (int)frameIndex;
    if (requestedIndex == decoder->imageIndex) {
        // We're here already, nothing to do
        return AVIF_RESULT_OK;
    }

    if (requestedIndex == (decoder->imageIndex + 1)) {
        // it's just the next image, nothing special here
        return avifDecoderNextImage(decoder);
    }

    if (requestedIndex >= decoder->imageCount) {
        // Impossible index
        return AVIF_RESULT_NO_IMAGES_REMAINING;
    }

    // If we get here, a decoder flush is necessary
    decoder->imageIndex = ((int)avifDecoderNearestKeyframe(decoder, frameIndex)) - 1; // prepare to read nearest keyframe
    avifDecoderFlush(decoder);
    for (;;) {
        avifResult result = avifDecoderNextImage(decoder);
        if (result != AVIF_RESULT_OK) {
            return result;
        }

        if (requestedIndex == decoder->imageIndex) {
            break;
        }
    }
    return AVIF_RESULT_OK;
}

avifBool avifDecoderIsKeyframe(avifDecoder * decoder, uint32_t frameIndex)
{
    if (decoder->data->colorInput) {
        if (frameIndex < decoder->data->colorInput->samples.count) {
            return decoder->data->colorInput->samples.sample[frameIndex].sync;
        }
    }
    return AVIF_FALSE;
}

uint32_t avifDecoderNearestKeyframe(avifDecoder * decoder, uint32_t frameIndex)
{
    for (; frameIndex != 0; --frameIndex) {
        if (avifDecoderIsKeyframe(decoder, frameIndex)) {
            break;
        }
    }
    return frameIndex;
}

avifResult avifDecoderRead(avifDecoder * decoder, avifImage * image, avifROData * input)
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
