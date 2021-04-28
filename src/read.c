// Copyright 2019 Joe Drago. All rights reserved.
// SPDX-License-Identifier: BSD-2-Clause

#include "avif/internal.h"

#include <assert.h>
#include <inttypes.h>
#include <limits.h>
#include <string.h>

#define AUXTYPE_SIZE 64
#define CONTENTTYPE_SIZE 64

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

// The only supported ipma box values for both version and flags are [0,1], so there technically
// can't be more than 4 unique tuples right now.
#define MAX_IPMA_VERSION_AND_FLAGS_SEEN 4

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

// ---------------------------------------------------------------------------
// Top-level structures

struct avifMeta;

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
    } u;
} avifProperty;
AVIF_ARRAY_DECLARE(avifPropertyArray, avifProperty, prop);

static const avifProperty * avifPropertyArrayFind(const avifPropertyArray * properties, const char * type)
{
    for (uint32_t propertyIndex = 0; propertyIndex < properties->count; ++propertyIndex) {
        avifProperty * prop = &properties->prop[propertyIndex];
        if (!memcmp(prop->type, type, 4)) {
            return prop;
        }
    }
    return NULL;
}

AVIF_ARRAY_DECLARE(avifExtentArray, avifExtent, extent);

// one "item" worth for decoding (all iref, iloc, iprp, etc refer to one of these)
typedef struct avifDecoderItem
{
    uint32_t id;
    struct avifMeta * meta; // Unowned; A back-pointer for convenience
    uint8_t type[4];
    size_t size;
    uint32_t idatID; // If non-zero, offset is relative to this idat box (iloc construction_method==1)
    avifContentType contentType;
    avifPropertyArray properties;
    avifExtentArray extents;       // All extent offsets/sizes
    avifRWData mergedExtents;      // if set, is a single contiguous block of this item's extents (unused when extents.count == 1)
    avifBool ownsMergedExtents;    // if true, mergedExtents must be freed when this item is destroyed
    avifBool partialMergedExtents; // If true, mergedExtents doesn't have all of the item data yet
    uint32_t thumbnailForID;       // if non-zero, this item is a thumbnail for Item #{thumbnailForID}
    uint32_t auxForID;             // if non-zero, this item is an auxC plane for Item #{auxForID}
    uint32_t descForID;            // if non-zero, this item is a content description for Item #{descForID}
    uint32_t dimgForID;            // if non-zero, this item is a derived image for Item #{dimgForID}
    uint32_t premByID;             // if non-zero, this item is premultiplied by Item #{premByID}
    avifBool hasUnsupportedEssentialProperty; // If true, this item cites a property flagged as 'essential' that libavif doesn't support (yet). Ignore the item, if so.
    avifBool ipmaSeen; // if true, this item already received a property association
} avifDecoderItem;
AVIF_ARRAY_DECLARE(avifDecoderItemArray, avifDecoderItem, item);

// idat storage
typedef struct avifDecoderItemData
{
    uint32_t id;
    avifRWData data;
} avifDecoderItemData;
AVIF_ARRAY_DECLARE(avifDecoderItemDataArray, avifDecoderItemData, idat);

// grid storage
typedef struct avifImageGrid
{
    uint32_t rows;    // Legal range: [1-256]
    uint32_t columns; // Legal range: [1-256]
    uint32_t outputWidth;
    uint32_t outputHeight;
} avifImageGrid;

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
    avifPropertyArray properties;
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
    uint32_t allSamplesSize; // If this is non-zero, sampleSizes will be empty and all samples will be this size
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
    for (uint32_t i = 0; i < sampleTable->sampleDescriptions.count; ++i) {
        avifSampleDescription * description = &sampleTable->sampleDescriptions.description[i];
        avifArrayDestroy(&description->properties);
    }
    avifArrayDestroy(&sampleTable->sampleDescriptions);
    avifArrayDestroy(&sampleTable->sampleToChunks);
    avifArrayDestroy(&sampleTable->sampleSizes);
    avifArrayDestroy(&sampleTable->timeToSamples);
    avifArrayDestroy(&sampleTable->syncSamples);
    avifFree(sampleTable);
}

static uint32_t avifSampleTableGetImageDelta(const avifSampleTable * sampleTable, int imageIndex)
{
    int maxSampleIndex = 0;
    for (uint32_t i = 0; i < sampleTable->timeToSamples.count; ++i) {
        const avifSampleTableTimeToSample * timeToSample = &sampleTable->timeToSamples.timeToSample[i];
        maxSampleIndex += timeToSample->sampleCount;
        if ((imageIndex < maxSampleIndex) || (i == (sampleTable->timeToSamples.count - 1))) {
            return timeToSample->sampleDelta;
        }
    }

    // TODO: fail here?
    return 1;
}

static avifBool avifSampleTableHasFormat(const avifSampleTable * sampleTable, const char * format)
{
    for (uint32_t i = 0; i < sampleTable->sampleDescriptions.count; ++i) {
        if (!memcmp(sampleTable->sampleDescriptions.description[i].format, format, 4)) {
            return AVIF_TRUE;
        }
    }
    return AVIF_FALSE;
}

static uint32_t avifCodecConfigurationBoxGetDepth(const avifCodecConfigurationBox * av1C)
{
    if (av1C->twelveBit) {
        return 12;
    } else if (av1C->highBitdepth) {
        return 10;
    }
    return 8;
}

static const avifPropertyArray * avifSampleTableGetProperties(const avifSampleTable * sampleTable)
{
    for (uint32_t i = 0; i < sampleTable->sampleDescriptions.count; ++i) {
        const avifSampleDescription * description = &sampleTable->sampleDescriptions.description[i];
        if (!memcmp(description->format, "av01", 4)) {
            return &description->properties;
        }
    }
    return NULL;
}

// one video track ("trak" contents)
typedef struct avifTrack
{
    uint32_t id;
    uint32_t auxForID; // if non-zero, this track is an auxC plane for Track #{auxForID}
    uint32_t premByID; // if non-zero, this track is premultiplied by Track #{premByID}
    uint32_t mediaTimescale;
    uint64_t mediaDuration;
    uint32_t width;
    uint32_t height;
    avifSampleTable * sampleTable;
    struct avifMeta * meta;
} avifTrack;
AVIF_ARRAY_DECLARE(avifTrackArray, avifTrack, track);

// ---------------------------------------------------------------------------
// avifCodecDecodeInput

avifCodecDecodeInput * avifCodecDecodeInputCreate(void)
{
    avifCodecDecodeInput * decodeInput = (avifCodecDecodeInput *)avifAlloc(sizeof(avifCodecDecodeInput));
    memset(decodeInput, 0, sizeof(avifCodecDecodeInput));
    avifArrayCreate(&decodeInput->samples, sizeof(avifDecodeSample), 1);
    return decodeInput;
}

void avifCodecDecodeInputDestroy(avifCodecDecodeInput * decodeInput)
{
    for (uint32_t sampleIndex = 0; sampleIndex < decodeInput->samples.count; ++sampleIndex) {
        avifDecodeSample * sample = &decodeInput->samples.sample[sampleIndex];
        if (sample->ownsData) {
            avifRWDataFree((avifRWData *)&sample->data);
        }
    }
    avifArrayDestroy(&decodeInput->samples);
    avifFree(decodeInput);
}

// Returns how many samples are in the chunk.
static uint32_t avifGetSampleCountOfChunk(const avifSampleTableSampleToChunkArray * sampleToChunks, uint32_t chunkIndex)
{
    uint32_t sampleCount = 0;
    for (int sampleToChunkIndex = sampleToChunks->count - 1; sampleToChunkIndex >= 0; --sampleToChunkIndex) {
        const avifSampleTableSampleToChunk * sampleToChunk = &sampleToChunks->sampleToChunk[sampleToChunkIndex];
        if (sampleToChunk->firstChunk <= (chunkIndex + 1)) {
            sampleCount = sampleToChunk->samplesPerChunk;
            break;
        }
    }
    return sampleCount;
}

static avifBool avifCodecDecodeInputGetSamples(avifCodecDecodeInput * decodeInput,
                                               avifSampleTable * sampleTable,
                                               const uint32_t imageCountLimit,
                                               const uint64_t sizeHint,
                                               avifDiagnostics * diag)
{
    if (imageCountLimit) {
        // Verify that the we're not about to exceed the frame count limit.

        uint32_t imageCountLeft = imageCountLimit;
        for (uint32_t chunkIndex = 0; chunkIndex < sampleTable->chunks.count; ++chunkIndex) {
            // First, figure out how many samples are in this chunk
            uint32_t sampleCount = avifGetSampleCountOfChunk(&sampleTable->sampleToChunks, chunkIndex);
            if (sampleCount == 0) {
                // chunks with 0 samples are invalid
                avifDiagnosticsPrintf(diag, "Sample table contains a chunk with 0 samples");
                return AVIF_FALSE;
            }

            if (sampleCount > imageCountLeft) {
                // This file exceeds the imageCountLimit, bail out
                avifDiagnosticsPrintf(diag, "Exceeded avifDecoder's imageCountLimit");
                return AVIF_FALSE;
            }
            imageCountLeft -= sampleCount;
        }
    }

    uint32_t sampleSizeIndex = 0;
    for (uint32_t chunkIndex = 0; chunkIndex < sampleTable->chunks.count; ++chunkIndex) {
        avifSampleTableChunk * chunk = &sampleTable->chunks.chunk[chunkIndex];

        // First, figure out how many samples are in this chunk
        uint32_t sampleCount = avifGetSampleCountOfChunk(&sampleTable->sampleToChunks, chunkIndex);
        if (sampleCount == 0) {
            // chunks with 0 samples are invalid
            avifDiagnosticsPrintf(diag, "Sample table contains a chunk with 0 samples");
            return AVIF_FALSE;
        }

        uint64_t sampleOffset = chunk->offset;
        for (uint32_t sampleIndex = 0; sampleIndex < sampleCount; ++sampleIndex) {
            uint32_t sampleSize = sampleTable->allSamplesSize;
            if (sampleSize == 0) {
                if (sampleSizeIndex >= sampleTable->sampleSizes.count) {
                    // We've run out of samples to sum
                    avifDiagnosticsPrintf(diag, "Truncated sample table");
                    return AVIF_FALSE;
                }
                avifSampleTableSampleSize * sampleSizePtr = &sampleTable->sampleSizes.sampleSize[sampleSizeIndex];
                sampleSize = sampleSizePtr->size;
            }

            avifDecodeSample * sample = (avifDecodeSample *)avifArrayPushPtr(&decodeInput->samples);
            sample->offset = sampleOffset;
            sample->size = sampleSize;
            sample->sync = AVIF_FALSE; // to potentially be set to true following the outer loop

            if (sampleSize > UINT64_MAX - sampleOffset) {
                avifDiagnosticsPrintf(diag,
                                      "Sample table contains an offset/size pair which overflows: [%" PRIu64 " / %" PRIu64 "]",
                                      sampleOffset,
                                      sampleSize);
                return AVIF_FALSE;
            }
            if (sizeHint && ((sampleOffset + sampleSize) > sizeHint)) {
                avifDiagnosticsPrintf(diag, "Exceeded avifIO's sizeHint, possibly truncated data");
                return AVIF_FALSE;
            }

            sampleOffset += sampleSize;
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
// Helper macros / functions

#define BEGIN_STREAM(VARNAME, PTR, SIZE) \
    avifROStream VARNAME;                \
    avifROData VARNAME##_roData;         \
    VARNAME##_roData.data = PTR;         \
    VARNAME##_roData.size = SIZE;        \
    avifROStreamStart(&VARNAME, &VARNAME##_roData)

// Use this to keep track of whether or not a child box that must be unique (0 or 1 present) has
// been seen yet, when parsing a parent box. If the "seen" bit is already set for a given box when
// it is encountered during parse, an error is thrown. Which bit corresponds to which box is
// dictated entirely by the calling function.
static avifBool uniqueBoxSeen(uint32_t * uniqueBoxFlags, uint32_t whichFlag, const char * parentBoxType, const char * boxType, avifDiagnostics * diagnostics)
{
    const uint32_t flag = 1 << whichFlag;
    if (*uniqueBoxFlags & flag) {
        // This box has already been seen. Error!
        avifDiagnosticsPrintf(diagnostics, "Box type '%s' contains a duplicate unique box of type '%s'", parentBoxType, boxType);
        return AVIF_FALSE;
    }

    // Mark this box as seen.
    *uniqueBoxFlags |= flag;
    return AVIF_TRUE;
}

// ---------------------------------------------------------------------------
// avifDecoderData

typedef struct avifTile
{
    avifCodecDecodeInput * input;
    struct avifCodec * codec;
    avifImage * image;
} avifTile;
AVIF_ARRAY_DECLARE(avifTileArray, avifTile, tile);

// This holds one "meta" box (from the BMFF and HEIF standards) worth of relevant-to-AVIF information.
// * If a meta box is parsed from the root level of the BMFF, it can contain the information about
//   "items" which might be color planes, alpha planes, or EXIF or XMP metadata.
// * If a meta box is parsed from inside of a track ("trak") box, any metadata (EXIF/XMP) items inside
//   of that box are implicitly associated with that track.
typedef struct avifMeta
{
    // Items (from HEIF) are the generic storage for any data that does not require timed processing
    // (single image color planes, alpha planes, EXIF, XMP, etc). Each item has a unique integer ID >1,
    // and is defined by a series of child boxes in a meta box:
    //  * iloc - location:     byte offset to item data, item size in bytes
    //  * iinf - information:  type of item (color planes, alpha plane, EXIF, XMP)
    //  * ipco - properties:   dimensions, aspect ratio, image transformations, references to other items
    //  * ipma - associations: Attaches an item in the properties list to a given item
    //
    // Items are lazily created in this array when any of the above boxes refer to one by a new (unseen) ID,
    // and are then further modified/updated as new information for an item's ID is parsed.
    avifDecoderItemArray items;

    // Any ipco boxes explained above are populated into this array as a staging area, which are
    // then duplicated into the appropriate items upon encountering an item property association
    // (ipma) box.
    avifPropertyArray properties;

    // Filled with the contents of "idat" boxes, which are raw data that an item can directly refer to in its
    // item location box (iloc) instead of just giving an offset into the overall file. If all items' iloc boxes
    // simply point at an offset/length in the file itself, this array will likely be empty.
    avifDecoderItemDataArray idats;

    // Ever-incrementing ID for uniquely identifying which 'meta' box contains an idat (when
    // multiple meta boxes exist as BMFF siblings). Each time avifParseMetaBox() is called on an
    // avifMeta struct, this value is incremented. Any time an additional meta box is detected at
    // the same "level" (root level, trak level, etc), this ID helps distinguish which meta box's
    // "idat" is which, as items implicitly reference idat boxes that exist in the same meta
    // box.
    uint32_t idatID;

    // Contents of a pitm box, which signal which of the items in this file is the main image. For
    // AVIF, this should point at an av01 type item containing color planes, and all other items
    // are ignored unless they refer to this item in some way (alpha plane, EXIF/XMP metadata).
    uint32_t primaryItemID;
} avifMeta;

static avifMeta * avifMetaCreate()
{
    avifMeta * meta = (avifMeta *)avifAlloc(sizeof(avifMeta));
    memset(meta, 0, sizeof(avifMeta));
    avifArrayCreate(&meta->items, sizeof(avifDecoderItem), 8);
    avifArrayCreate(&meta->properties, sizeof(avifProperty), 16);
    avifArrayCreate(&meta->idats, sizeof(avifDecoderItemData), 1);
    return meta;
}

static void avifMetaDestroy(avifMeta * meta)
{
    for (uint32_t i = 0; i < meta->items.count; ++i) {
        avifDecoderItem * item = &meta->items.item[i];
        avifArrayDestroy(&item->properties);
        avifArrayDestroy(&item->extents);
        if (item->ownsMergedExtents) {
            avifRWDataFree(&item->mergedExtents);
        }
    }
    avifArrayDestroy(&meta->items);
    avifArrayDestroy(&meta->properties);
    for (uint32_t i = 0; i < meta->idats.count; ++i) {
        avifDecoderItemData * idat = &meta->idats.idat[i];
        avifRWDataFree(&idat->data);
    }
    avifArrayDestroy(&meta->idats);
    avifFree(meta);
}

static avifDecoderItem * avifMetaFindItem(avifMeta * meta, uint32_t itemID)
{
    if (itemID == 0) {
        return NULL;
    }

    for (uint32_t i = 0; i < meta->items.count; ++i) {
        if (meta->items.item[i].id == itemID) {
            return &meta->items.item[i];
        }
    }

    avifDecoderItem * item = (avifDecoderItem *)avifArrayPushPtr(&meta->items);
    avifArrayCreate(&item->properties, sizeof(avifProperty), 16);
    avifArrayCreate(&item->extents, sizeof(avifExtent), 1);
    item->id = itemID;
    item->meta = meta;
    return item;
}

typedef struct avifDecoderData
{
    avifMeta * meta; // The root-level meta box
    avifTrackArray tracks;
    avifTileArray tiles;
    unsigned int colorTileCount;
    unsigned int alphaTileCount;
    avifImageGrid colorGrid;
    avifImageGrid alphaGrid;
    avifDecoderSource source;
    avifDiagnostics * diag;                    // Shallow copy; owned by avifDecoder
    const avifSampleTable * sourceSampleTable; // NULL unless (source == AVIF_DECODER_SOURCE_TRACKS), owned by an avifTrack
    avifBool cicpSet;                          // True if avifDecoder's image has had its CICP set correctly yet.
                                               // This allows nclx colr boxes to override AV1 CICP, as specified in the MIAF
                                               // standard (ISO/IEC 23000-22:2019), section 7.3.6.4:
                                               //
    // "The colour information property takes precedence over any colour information in the image
    // bitstream, i.e. if the property is present, colour information in the bitstream shall be ignored."
} avifDecoderData;

static avifDecoderData * avifDecoderDataCreate()
{
    avifDecoderData * data = (avifDecoderData *)avifAlloc(sizeof(avifDecoderData));
    memset(data, 0, sizeof(avifDecoderData));
    data->meta = avifMetaCreate();
    avifArrayCreate(&data->tracks, sizeof(avifTrack), 2);
    avifArrayCreate(&data->tiles, sizeof(avifTile), 8);
    return data;
}

static void avifDecoderDataResetCodec(avifDecoderData * data)
{
    for (unsigned int i = 0; i < data->tiles.count; ++i) {
        avifTile * tile = &data->tiles.tile[i];
        if (tile->image) {
            avifImageFreePlanes(tile->image, AVIF_PLANES_ALL); // forget any pointers into codec image buffers
        }
        if (tile->codec) {
            avifCodecDestroy(tile->codec);
            tile->codec = NULL;
        }
    }
}

static avifTile * avifDecoderDataCreateTile(avifDecoderData * data)
{
    avifTile * tile = (avifTile *)avifArrayPushPtr(&data->tiles);
    tile->image = avifImageCreateEmpty();
    tile->input = avifCodecDecodeInputCreate();
    return tile;
}

static avifTrack * avifDecoderDataCreateTrack(avifDecoderData * data)
{
    avifTrack * track = (avifTrack *)avifArrayPushPtr(&data->tracks);
    track->meta = avifMetaCreate();
    return track;
}

static void avifDecoderDataClearTiles(avifDecoderData * data)
{
    for (unsigned int i = 0; i < data->tiles.count; ++i) {
        avifTile * tile = &data->tiles.tile[i];
        if (tile->input) {
            avifCodecDecodeInputDestroy(tile->input);
            tile->input = NULL;
        }
        if (tile->codec) {
            avifCodecDestroy(tile->codec);
            tile->codec = NULL;
        }
        if (tile->image) {
            avifImageDestroy(tile->image);
            tile->image = NULL;
        }
    }
    data->tiles.count = 0;
    data->colorTileCount = 0;
    data->alphaTileCount = 0;
}

static void avifDecoderDataDestroy(avifDecoderData * data)
{
    avifMetaDestroy(data->meta);
    for (uint32_t i = 0; i < data->tracks.count; ++i) {
        avifTrack * track = &data->tracks.track[i];
        if (track->sampleTable) {
            avifSampleTableDestroy(track->sampleTable);
        }
        if (track->meta) {
            avifMetaDestroy(track->meta);
        }
    }
    avifArrayDestroy(&data->tracks);
    avifDecoderDataClearTiles(data);
    avifArrayDestroy(&data->tiles);
    avifFree(data);
}

// This returns the max extent that has to be read in order to decode this item. If
// the item is stored in an idat, the data has already been read during Parse() and
// this function will return AVIF_RESULT_OK with a 0-byte extent.
static avifResult avifDecoderItemMaxExtent(const avifDecoderItem * item, avifExtent * outExtent)
{
    if (item->extents.count == 0) {
        return AVIF_RESULT_TRUNCATED_DATA;
    }

    if (item->idatID != 0) {
        // construction_method: idat(1)

        // Find associated idat box
        for (uint32_t i = 0; i < item->meta->idats.count; ++i) {
            if (item->meta->idats.idat[i].id == item->idatID) {
                // Already read from a meta box during Parse()
                memset(outExtent, 0, sizeof(avifExtent));
                return AVIF_RESULT_OK;
            }
        }

        // no associated idat box was found in the meta box, bail out
        return AVIF_RESULT_NO_CONTENT;
    }

    // construction_method: file(0)

    // Assert that the for loop below will execute at least one iteration.
    assert(item->extents.count != 0);
    uint64_t minOffset = UINT64_MAX;
    uint64_t maxOffset = 0;
    for (uint32_t extentIter = 0; extentIter < item->extents.count; ++extentIter) {
        avifExtent * extent = &item->extents.extent[extentIter];

        if (extent->size > UINT64_MAX - extent->offset) {
            return AVIF_RESULT_BMFF_PARSE_FAILED;
        }
        const uint64_t endOffset = extent->offset + extent->size;

        if (minOffset > extent->offset) {
            minOffset = extent->offset;
        }
        if (maxOffset < endOffset) {
            maxOffset = endOffset;
        }
    }

    outExtent->offset = minOffset;
    const uint64_t extentLength = maxOffset - minOffset;
    if (extentLength > SIZE_MAX) {
        return AVIF_RESULT_BMFF_PARSE_FAILED;
    }
    outExtent->size = (size_t)extentLength;
    return AVIF_RESULT_OK;
}

static avifResult avifDecoderItemValidateAV1(const avifDecoderItem * item)
{
    const avifProperty * av1CProp = avifPropertyArrayFind(&item->properties, "av1C");
    if (!av1CProp) {
        // An av1C box is mandatory in all valid AVIF configurations. Bail out.
        return AVIF_RESULT_BMFF_PARSE_FAILED;
    }
    const uint32_t av1CDepth = avifCodecConfigurationBoxGetDepth(&av1CProp->u.av1C);

    const avifProperty * pixiProp = avifPropertyArrayFind(&item->properties, "pixi");
    if (!pixiProp) {
        // A pixi box is mandatory in all valid AVIF configurations. Bail out.
        return AVIF_RESULT_BMFF_PARSE_FAILED;
    }

    for (uint8_t i = 0; i < pixiProp->u.pixi.planeCount; ++i) {
        if (pixiProp->u.pixi.planeDepths[i] != av1CDepth) {
            // pixi depth must match av1C depth
            return AVIF_RESULT_BMFF_PARSE_FAILED;
        }
    }
    return AVIF_RESULT_OK;
}

static avifResult avifDecoderItemRead(avifDecoderItem * item, avifIO * io, avifROData * outData, size_t partialByteCount)
{
    if (item->mergedExtents.data && !item->partialMergedExtents) {
        // Multiple extents have already been concatenated for this item, just return it
        memcpy(outData, &item->mergedExtents, sizeof(avifROData));
        return AVIF_RESULT_OK;
    }

    if (item->extents.count == 0) {
        return AVIF_RESULT_TRUNCATED_DATA;
    }

    // Find this item's source of all extents' data, based on the construction method
    const avifRWData * idatBuffer = NULL;
    if (item->idatID != 0) {
        // construction_method: idat(1)

        // Find associated idat box
        for (uint32_t i = 0; i < item->meta->idats.count; ++i) {
            if (item->meta->idats.idat[i].id == item->idatID) {
                idatBuffer = &item->meta->idats.idat[i].data;
                break;
            }
        }

        if (idatBuffer == NULL) {
            // no associated idat box was found in the meta box, bail out
            return AVIF_RESULT_NO_CONTENT;
        }
    }

    // Merge extents into a single contiguous buffer
    if ((io->sizeHint > 0) && (item->size > io->sizeHint)) {
        // Sanity check: somehow the sum of extents for this item exceeds the entire file or idat
        // size!
        return AVIF_RESULT_TRUNCATED_DATA;
    }

    size_t totalBytesToRead = item->size;
    if (partialByteCount && (totalBytesToRead > partialByteCount)) {
        totalBytesToRead = partialByteCount;
    }

    // If there is a single extent for this item and the source of the read buffer is going to be
    // persistent for the lifetime of the avifDecoder (whether it comes from its own internal
    // idatBuffer or from a known-persistent IO), we can avoid buffer duplication and just use the
    // preexisting buffer.
    avifBool singlePersistentBuffer = ((item->extents.count == 1) && (idatBuffer || io->persistent));
    if (!singlePersistentBuffer) {
        avifRWDataRealloc(&item->mergedExtents, totalBytesToRead);
        item->ownsMergedExtents = AVIF_TRUE;
    }

    // Set this until we manage to fill the entire mergedExtents buffer
    item->partialMergedExtents = AVIF_TRUE;

    uint8_t * front = item->mergedExtents.data;
    size_t remainingBytes = totalBytesToRead;
    for (uint32_t extentIter = 0; extentIter < item->extents.count; ++extentIter) {
        avifExtent * extent = &item->extents.extent[extentIter];

        size_t bytesToRead = extent->size;
        if (bytesToRead > remainingBytes) {
            bytesToRead = remainingBytes;
        }

        avifROData offsetBuffer;
        if (idatBuffer) {
            if (extent->offset > idatBuffer->size) {
                return AVIF_RESULT_BMFF_PARSE_FAILED;
            }
            if (extent->size > idatBuffer->size - extent->offset) {
                return AVIF_RESULT_BMFF_PARSE_FAILED;
            }
            offsetBuffer.data = idatBuffer->data + extent->offset;
            offsetBuffer.size = idatBuffer->size - extent->offset;
        } else {
            // construction_method: file(0)

            if ((io->sizeHint > 0) && (extent->offset > io->sizeHint)) {
                return AVIF_RESULT_BMFF_PARSE_FAILED;
            }
            avifResult readResult = io->read(io, 0, extent->offset, bytesToRead, &offsetBuffer);
            if (readResult != AVIF_RESULT_OK) {
                return readResult;
            }
            if (bytesToRead != offsetBuffer.size) {
                return AVIF_RESULT_TRUNCATED_DATA;
            }
        }

        if (singlePersistentBuffer) {
            memcpy(&item->mergedExtents, &offsetBuffer, sizeof(avifRWData));
            item->mergedExtents.size = bytesToRead;
        } else {
            assert(item->ownsMergedExtents);
            assert(front);
            memcpy(front, offsetBuffer.data, bytesToRead);
            front += bytesToRead;
        }

        remainingBytes -= bytesToRead;
        if (remainingBytes == 0) {
            // This happens when partialByteCount is set
            break;
        }
    }
    if (remainingBytes != 0) {
        // This should be impossible?
        return AVIF_RESULT_TRUNCATED_DATA;
    }

    outData->data = item->mergedExtents.data;
    outData->size = totalBytesToRead;
    item->partialMergedExtents = (item->size != totalBytesToRead);
    return AVIF_RESULT_OK;
}

static avifBool avifDecoderDataGenerateImageGridTiles(avifDecoderData * data, avifImageGrid * grid, avifDecoderItem * gridItem, avifBool alpha)
{
    unsigned int tilesRequested = grid->rows * grid->columns;

    // Count number of dimg for this item, bail out if it doesn't match perfectly
    unsigned int tilesAvailable = 0;
    for (uint32_t i = 0; i < gridItem->meta->items.count; ++i) {
        avifDecoderItem * item = &gridItem->meta->items.item[i];
        if (item->dimgForID == gridItem->id) {
            if (memcmp(item->type, "av01", 4)) {
                continue;
            }
            if (item->hasUnsupportedEssentialProperty) {
                // An essential property isn't supported by libavif; can't
                // decode a grid image if any tile in the grid isn't supported.
                avifDiagnosticsPrintf(data->diag, "Grid image contains tile with an unsupported property marked as essential");
                return AVIF_FALSE;
            }

            ++tilesAvailable;
        }
    }

    if (tilesRequested != tilesAvailable) {
        avifDiagnosticsPrintf(data->diag,
                              "Grid image of dimensions %u/%u requires %u tiles, and only %u were found",
                              grid->columns,
                              grid->rows,
                              tilesRequested,
                              tilesAvailable);
        return AVIF_FALSE;
    }

    avifBool firstTile = AVIF_TRUE;
    for (uint32_t i = 0; i < gridItem->meta->items.count; ++i) {
        avifDecoderItem * item = &gridItem->meta->items.item[i];
        if (item->dimgForID == gridItem->id) {
            if (memcmp(item->type, "av01", 4)) {
                continue;
            }

            avifTile * tile = avifDecoderDataCreateTile(data);
            avifDecodeSample * sample = (avifDecodeSample *)avifArrayPushPtr(&tile->input->samples);
            sample->itemID = item->id;
            sample->offset = 0;
            sample->size = item->size;
            sample->sync = AVIF_TRUE;
            tile->input->alpha = alpha;

            if (firstTile) {
                firstTile = AVIF_FALSE;

                // Adopt the av1C property of the first av01 tile, so that it can be queried from
                // the top-level color/alpha item during avifDecoderReset().
                const avifProperty * srcProp = avifPropertyArrayFind(&item->properties, "av1C");
                if (!srcProp) {
                    avifDiagnosticsPrintf(data->diag, "Grid image's first tile is missing an av1C property");
                    return AVIF_FALSE;
                }
                avifProperty * dstProp = (avifProperty *)avifArrayPushPtr(&gridItem->properties);
                memcpy(dstProp, srcProp, sizeof(avifProperty));
            }
        }
    }
    return AVIF_TRUE;
}

static avifBool avifDecoderDataFillImageGrid(avifDecoderData * data,
                                             avifImageGrid * grid,
                                             avifImage * dstImage,
                                             unsigned int firstTileIndex,
                                             unsigned int tileCount,
                                             avifBool alpha)
{
    if (tileCount == 0) {
        avifDiagnosticsPrintf(data->diag, "Cannot fill grid image, no tiles");
        return AVIF_FALSE;
    }

    avifTile * firstTile = &data->tiles.tile[firstTileIndex];
    avifBool firstTileUVPresent = (firstTile->image->yuvPlanes[AVIF_CHAN_U] && firstTile->image->yuvPlanes[AVIF_CHAN_V]);

    // Check for tile consistency: All tiles in a grid image should match in the properties checked below.
    for (unsigned int i = 1; i < tileCount; ++i) {
        avifTile * tile = &data->tiles.tile[firstTileIndex + i];
        avifBool uvPresent = (tile->image->yuvPlanes[AVIF_CHAN_U] && tile->image->yuvPlanes[AVIF_CHAN_V]);
        if ((tile->image->width != firstTile->image->width) || (tile->image->height != firstTile->image->height) ||
            (tile->image->depth != firstTile->image->depth) || (tile->image->yuvFormat != firstTile->image->yuvFormat) ||
            (tile->image->yuvRange != firstTile->image->yuvRange) || (uvPresent != firstTileUVPresent) ||
            (tile->image->colorPrimaries != firstTile->image->colorPrimaries) ||
            (tile->image->transferCharacteristics != firstTile->image->transferCharacteristics) ||
            (tile->image->matrixCoefficients != firstTile->image->matrixCoefficients) ||
            (tile->image->alphaRange != firstTile->image->alphaRange)) {
            avifDiagnosticsPrintf(data->diag, "Grid image contains mismatched tiles");
            return AVIF_FALSE;
        }
    }

    // Validate grid image size and tile size.
    //
    // HEIF (ISO/IEC 23008-12:2017), Section 6.6.2.3.1:
    //   The tiled input images shall completely "cover" the reconstructed image grid canvas, ...
    if (((firstTile->image->width * grid->columns) < grid->outputWidth) ||
        ((firstTile->image->height * grid->rows) < grid->outputHeight)) {
        avifDiagnosticsPrintf(data->diag,
                              "Grid image tiles do not completely cover the image (HEIF (ISO/IEC 23008-12:2017), Section 6.6.2.3.1)");
        return AVIF_FALSE;
    }
    // Tiles in the rightmost column and bottommost row must overlap the reconstructed image grid canvas. See MIAF (ISO/IEC 23000-22:2019), Section 7.3.11.4.2, Figure 2.
    if (((firstTile->image->width * (grid->columns - 1)) >= grid->outputWidth) ||
        ((firstTile->image->height * (grid->rows - 1)) >= grid->outputHeight)) {
        avifDiagnosticsPrintf(data->diag,
                              "Grid image tiles in the rightmost column and bottommost row do not overlap the reconstructed image grid canvas. See MIAF (ISO/IEC 23000-22:2019), Section 7.3.11.4.2, Figure 2");
        return AVIF_FALSE;
    }
    // Check the restrictions in MIAF (ISO/IEC 23000-22:2019), Section 7.3.11.4.2.
    //
    // The tile_width shall be greater than or equal to 64, and the tile_height shall be greater than or equal to 64.
    if ((firstTile->image->width < 64) || (firstTile->image->height < 64)) {
        avifDiagnosticsPrintf(data->diag,
                              "Grid image tiles are smaller than 64x64 (%u/%u). See MIAF (ISO/IEC 23000-22:2019), Section 7.3.11.4.2",
                              firstTile->image->width,
                              firstTile->image->height);
        return AVIF_FALSE;
    }
    if (!alpha) {
        if ((firstTile->image->yuvFormat == AVIF_PIXEL_FORMAT_YUV422) || (firstTile->image->yuvFormat == AVIF_PIXEL_FORMAT_YUV420)) {
            // The horizontal tile offsets and widths, and the output width, shall be even numbers.
            if (((firstTile->image->width & 1) != 0) || ((grid->outputWidth & 1) != 0)) {
                avifDiagnosticsPrintf(data->diag,
                                      "Grid image horizontal tile offsets and widths [%u], and the output width [%u], shall be even numbers.",
                                      firstTile->image->width,
                                      grid->outputWidth);
                return AVIF_FALSE;
            }
        }
        if (firstTile->image->yuvFormat == AVIF_PIXEL_FORMAT_YUV420) {
            // The vertical tile offsets and heights, and the output height, shall be even numbers.
            if (((firstTile->image->height & 1) != 0) || ((grid->outputHeight & 1) != 0)) {
                avifDiagnosticsPrintf(data->diag,
                                      "Grid image vertical tile offsets and heights [%u], and the output height [%u], shall be even numbers.",
                                      firstTile->image->height,
                                      grid->outputHeight);
                return AVIF_FALSE;
            }
        }
    }

    // Lazily populate dstImage with the new frame's properties. If we're decoding alpha,
    // these values must already match.
    if ((dstImage->width != grid->outputWidth) || (dstImage->height != grid->outputHeight) ||
        (dstImage->depth != firstTile->image->depth) || (!alpha && (dstImage->yuvFormat != firstTile->image->yuvFormat))) {
        if (alpha) {
            // Alpha doesn't match size, just bail out
            avifDiagnosticsPrintf(data->diag, "Alpha plane dimensions do not match color plane dimensions");
            return AVIF_FALSE;
        }

        avifImageFreePlanes(dstImage, AVIF_PLANES_ALL);
        dstImage->width = grid->outputWidth;
        dstImage->height = grid->outputHeight;
        dstImage->depth = firstTile->image->depth;
        dstImage->yuvFormat = firstTile->image->yuvFormat;
        dstImage->yuvRange = firstTile->image->yuvRange;
        if (!data->cicpSet) {
            data->cicpSet = AVIF_TRUE;
            dstImage->colorPrimaries = firstTile->image->colorPrimaries;
            dstImage->transferCharacteristics = firstTile->image->transferCharacteristics;
            dstImage->matrixCoefficients = firstTile->image->matrixCoefficients;
        }
    }
    if (alpha) {
        dstImage->alphaRange = firstTile->image->alphaRange;
    }

    avifImageAllocatePlanes(dstImage, alpha ? AVIF_PLANES_A : AVIF_PLANES_YUV);

    avifPixelFormatInfo formatInfo;
    avifGetPixelFormatInfo(firstTile->image->yuvFormat, &formatInfo);

    unsigned int tileIndex = firstTileIndex;
    size_t pixelBytes = avifImageUsesU16(dstImage) ? 2 : 1;
    for (unsigned int rowIndex = 0; rowIndex < grid->rows; ++rowIndex) {
        for (unsigned int colIndex = 0; colIndex < grid->columns; ++colIndex, ++tileIndex) {
            avifTile * tile = &data->tiles.tile[tileIndex];

            unsigned int widthToCopy = firstTile->image->width;
            unsigned int maxX = firstTile->image->width * (colIndex + 1);
            if (maxX > grid->outputWidth) {
                widthToCopy -= maxX - grid->outputWidth;
            }

            unsigned int heightToCopy = firstTile->image->height;
            unsigned int maxY = firstTile->image->height * (rowIndex + 1);
            if (maxY > grid->outputHeight) {
                heightToCopy -= maxY - grid->outputHeight;
            }

            // Y and A channels
            size_t yaColOffset = (size_t)colIndex * firstTile->image->width;
            size_t yaRowOffset = (size_t)rowIndex * firstTile->image->height;
            size_t yaRowBytes = widthToCopy * pixelBytes;

            if (alpha) {
                // A
                for (unsigned int j = 0; j < heightToCopy; ++j) {
                    uint8_t * src = &tile->image->alphaPlane[j * tile->image->alphaRowBytes];
                    uint8_t * dst = &dstImage->alphaPlane[(yaColOffset * pixelBytes) + ((yaRowOffset + j) * dstImage->alphaRowBytes)];
                    memcpy(dst, src, yaRowBytes);
                }
            } else {
                // Y
                for (unsigned int j = 0; j < heightToCopy; ++j) {
                    uint8_t * src = &tile->image->yuvPlanes[AVIF_CHAN_Y][j * tile->image->yuvRowBytes[AVIF_CHAN_Y]];
                    uint8_t * dst =
                        &dstImage->yuvPlanes[AVIF_CHAN_Y][(yaColOffset * pixelBytes) + ((yaRowOffset + j) * dstImage->yuvRowBytes[AVIF_CHAN_Y])];
                    memcpy(dst, src, yaRowBytes);
                }

                if (!firstTileUVPresent) {
                    continue;
                }

                // UV
                heightToCopy >>= formatInfo.chromaShiftY;
                size_t uvColOffset = yaColOffset >> formatInfo.chromaShiftX;
                size_t uvRowOffset = yaRowOffset >> formatInfo.chromaShiftY;
                size_t uvRowBytes = yaRowBytes >> formatInfo.chromaShiftX;
                for (unsigned int j = 0; j < heightToCopy; ++j) {
                    uint8_t * srcU = &tile->image->yuvPlanes[AVIF_CHAN_U][j * tile->image->yuvRowBytes[AVIF_CHAN_U]];
                    uint8_t * dstU =
                        &dstImage->yuvPlanes[AVIF_CHAN_U][(uvColOffset * pixelBytes) + ((uvRowOffset + j) * dstImage->yuvRowBytes[AVIF_CHAN_U])];
                    memcpy(dstU, srcU, uvRowBytes);

                    uint8_t * srcV = &tile->image->yuvPlanes[AVIF_CHAN_V][j * tile->image->yuvRowBytes[AVIF_CHAN_V]];
                    uint8_t * dstV =
                        &dstImage->yuvPlanes[AVIF_CHAN_V][(uvColOffset * pixelBytes) + ((uvRowOffset + j) * dstImage->yuvRowBytes[AVIF_CHAN_V])];
                    memcpy(dstV, srcV, uvRowBytes);
                }
            }
        }
    }

    return AVIF_TRUE;
}

// If colorId == 0 (a sentinel value as item IDs must be nonzero), accept any found EXIF/XMP metadata. Passing in 0
// is used when finding metadata in a meta box embedded in a trak box, as any items inside of a meta box that is
// inside of a trak box are implicitly associated to the track.
static avifResult avifDecoderFindMetadata(avifDecoder * decoder, avifMeta * meta, avifImage * image, uint32_t colorId)
{
    if (decoder->ignoreExif && decoder->ignoreXMP) {
        // Nothing to do!
        return AVIF_RESULT_OK;
    }

    for (uint32_t itemIndex = 0; itemIndex < meta->items.count; ++itemIndex) {
        avifDecoderItem * item = &meta->items.item[itemIndex];
        if (!item->size) {
            continue;
        }
        if (item->hasUnsupportedEssentialProperty) {
            // An essential property isn't supported by libavif; ignore the item.
            continue;
        }

        if ((colorId > 0) && (item->descForID != colorId)) {
            // Not a content description (metadata) for the colorOBU, skip it
            continue;
        }

        if (!decoder->ignoreExif && !memcmp(item->type, "Exif", 4)) {
            avifROData exifContents;
            avifResult readResult = avifDecoderItemRead(item, decoder->io, &exifContents, 0);
            if (readResult != AVIF_RESULT_OK) {
                return readResult;
            }

            // Advance past Annex A.2.1's header
            BEGIN_STREAM(exifBoxStream, exifContents.data, exifContents.size);
            uint32_t exifTiffHeaderOffset;
            CHECKERR(avifROStreamReadU32(&exifBoxStream, &exifTiffHeaderOffset), AVIF_RESULT_BMFF_PARSE_FAILED); // unsigned int(32) exif_tiff_header_offset;

            avifImageSetMetadataExif(image, avifROStreamCurrent(&exifBoxStream), avifROStreamRemainingBytes(&exifBoxStream));
        } else if (!decoder->ignoreXMP && !memcmp(item->type, "mime", 4) &&
                   !memcmp(item->contentType.contentType, xmpContentType, xmpContentTypeSize)) {
            avifROData xmpContents;
            avifResult readResult = avifDecoderItemRead(item, decoder->io, &xmpContents, 0);
            if (readResult != AVIF_RESULT_OK) {
                return readResult;
            }

            avifImageSetMetadataXMP(image, xmpContents.data, xmpContents.size);
        }
    }
    return AVIF_RESULT_OK;
}

// ---------------------------------------------------------------------------
// URN

static avifBool isAlphaURN(const char * urn)
{
    return !strcmp(urn, URN_ALPHA0) || !strcmp(urn, URN_ALPHA1);
}

// ---------------------------------------------------------------------------
// BMFF Parsing

static avifBool avifParseHandlerBox(const uint8_t * raw, size_t rawLen, avifDiagnostics * diag)
{
    BEGIN_STREAM(s, raw, rawLen);

    CHECK(avifROStreamReadAndEnforceVersion(&s, 0));

    uint32_t predefined;
    CHECK(avifROStreamReadU32(&s, &predefined)); // unsigned int(32) pre_defined = 0;
    if (predefined != 0) {
        avifDiagnosticsPrintf(diag, "Box type 'hdlr' contains a pre_defined value that is nonzero");
        return AVIF_FALSE;
    }

    uint8_t handlerType[4];
    CHECK(avifROStreamRead(&s, handlerType, 4)); // unsigned int(32) handler_type;
    if (memcmp(handlerType, "pict", 4) != 0) {
        avifDiagnosticsPrintf(diag, "Box type 'hdlr' handler_type is not 'pict'");
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
    BEGIN_STREAM(s, raw, rawLen);

    uint8_t version;
    CHECK(avifROStreamReadVersionAndFlags(&s, &version, NULL));
    if (version > 2) {
        avifDiagnosticsPrintf(diag, "Box type 'iloc' has an unsupported version [%u]", version);
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
            avifDiagnosticsPrintf(diag, "Box type 'iloc' has an unsupported extent_index");
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
                avifDiagnosticsPrintf(diag, "Box type 'iloc' has an unsupported construction method [%u]", constructionMethod);
                return AVIF_FALSE;
            }
            if (constructionMethod == 1) {
                idatID = meta->idatID;
            }
        }

        avifDecoderItem * item = avifMetaFindItem(meta, itemID);
        if (!item) {
            avifDiagnosticsPrintf(diag, "Box type 'iloc' has an invalid item ID [%u]", itemID);
            return AVIF_FALSE;
        }
        if (item->extents.count > 0) {
            // This item has already been given extents via this iloc box. This is invalid.
            avifDiagnosticsPrintf(diag, "Item ID [%u] contains duplicate sets of extents", itemID);
            return AVIF_FALSE;
        }
        item->idatID = idatID;

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
                                      "Item ID [%u] contains an extent length which overflows the item size: [%" PRIu64 ", %" PRIu64 "]",
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

static avifBool avifParseImageGridBox(avifImageGrid * grid, const uint8_t * raw, size_t rawLen, avifDiagnostics * diag)
{
    BEGIN_STREAM(s, raw, rawLen);

    uint8_t version, flags;
    CHECK(avifROStreamRead(&s, &version, 1)); // unsigned int(8) version = 0;
    if (version != 0) {
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
    if ((grid->outputWidth == 0) || (grid->outputHeight == 0) || (grid->outputWidth > (AVIF_MAX_IMAGE_SIZE / grid->outputHeight))) {
        avifDiagnosticsPrintf(diag, "Grid box contains illegal dimensions: [%u x %u]", grid->outputWidth, grid->outputHeight);
        return AVIF_FALSE;
    }
    return avifROStreamRemainingBytes(&s) == 0;
}

static avifBool avifParseImageSpatialExtentsProperty(avifProperty * prop, const uint8_t * raw, size_t rawLen)
{
    BEGIN_STREAM(s, raw, rawLen);
    CHECK(avifROStreamReadAndEnforceVersion(&s, 0));

    avifImageSpatialExtents * ispe = &prop->u.ispe;
    CHECK(avifROStreamReadU32(&s, &ispe->width));
    CHECK(avifROStreamReadU32(&s, &ispe->height));
    return AVIF_TRUE;
}

static avifBool avifParseAuxiliaryTypeProperty(avifProperty * prop, const uint8_t * raw, size_t rawLen)
{
    BEGIN_STREAM(s, raw, rawLen);
    CHECK(avifROStreamReadAndEnforceVersion(&s, 0));

    CHECK(avifROStreamReadString(&s, prop->u.auxC.auxType, AUXTYPE_SIZE));
    return AVIF_TRUE;
}

static avifBool avifParseColourInformationBox(avifProperty * prop, const uint8_t * raw, size_t rawLen)
{
    BEGIN_STREAM(s, raw, rawLen);

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
    BEGIN_STREAM(s, raw, rawLen);

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

static avifBool avifParsePixelAspectRatioBoxProperty(avifProperty * prop, const uint8_t * raw, size_t rawLen)
{
    BEGIN_STREAM(s, raw, rawLen);

    avifPixelAspectRatioBox * pasp = &prop->u.pasp;
    CHECK(avifROStreamReadU32(&s, &pasp->hSpacing)); // unsigned int(32) hSpacing;
    CHECK(avifROStreamReadU32(&s, &pasp->vSpacing)); // unsigned int(32) vSpacing;
    return AVIF_TRUE;
}

static avifBool avifParseCleanApertureBoxProperty(avifProperty * prop, const uint8_t * raw, size_t rawLen)
{
    BEGIN_STREAM(s, raw, rawLen);

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
    BEGIN_STREAM(s, raw, rawLen);

    avifImageRotation * irot = &prop->u.irot;
    CHECK(avifROStreamRead(&s, &irot->angle, 1)); // unsigned int (6) reserved = 0; unsigned int (2) angle;
    if ((irot->angle & 0xfc) != 0) {
        // reserved bits must be 0
        avifDiagnosticsPrintf(diag, "Box type 'irot' contains nonzero reserved bits [%u]", irot->angle);
        return AVIF_FALSE;
    }
    return AVIF_TRUE;
}

static avifBool avifParseImageMirrorProperty(avifProperty * prop, const uint8_t * raw, size_t rawLen, avifDiagnostics * diag)
{
    BEGIN_STREAM(s, raw, rawLen);

    avifImageMirror * imir = &prop->u.imir;
    CHECK(avifROStreamRead(&s, &imir->axis, 1)); // unsigned int (7) reserved = 0; unsigned int (1) axis;
    if ((imir->axis & 0xfe) != 0) {
        // reserved bits must be 0
        avifDiagnosticsPrintf(diag, "Box type 'imir' contains nonzero reserved bits [%u]", imir->axis);
        return AVIF_FALSE;
    }
    return AVIF_TRUE;
}

static avifBool avifParsePixelInformationProperty(avifProperty * prop, const uint8_t * raw, size_t rawLen, avifDiagnostics * diag)
{
    BEGIN_STREAM(s, raw, rawLen);
    CHECK(avifROStreamReadAndEnforceVersion(&s, 0));

    avifPixelInformationProperty * pixi = &prop->u.pixi;
    CHECK(avifROStreamRead(&s, &pixi->planeCount, 1)); // unsigned int (8) num_channels;
    if (pixi->planeCount > MAX_PIXI_PLANE_DEPTHS) {
        avifDiagnosticsPrintf(diag, "Box type 'pixi' contains unsupported plane count [%u]", pixi->planeCount);
        return AVIF_FALSE;
    }
    for (uint8_t i = 0; i < pixi->planeCount; ++i) {
        CHECK(avifROStreamRead(&s, &pixi->planeDepths[i], 1)); // unsigned int (8) bits_per_channel;
    }
    return AVIF_TRUE;
}

static avifBool avifParseItemPropertyContainerBox(avifPropertyArray * properties, const uint8_t * raw, size_t rawLen, avifDiagnostics * diag)
{
    BEGIN_STREAM(s, raw, rawLen);

    while (avifROStreamHasBytesLeft(&s, 1)) {
        avifBoxHeader header;
        CHECK(avifROStreamReadBoxHeader(&s, &header));

        int propertyIndex = avifArrayPushIndex(properties);
        avifProperty * prop = &properties->prop[propertyIndex];
        memcpy(prop->type, header.type, 4);
        if (!memcmp(header.type, "ispe", 4)) {
            CHECK(avifParseImageSpatialExtentsProperty(prop, avifROStreamCurrent(&s), header.size));
        } else if (!memcmp(header.type, "auxC", 4)) {
            CHECK(avifParseAuxiliaryTypeProperty(prop, avifROStreamCurrent(&s), header.size));
        } else if (!memcmp(header.type, "colr", 4)) {
            CHECK(avifParseColourInformationBox(prop, avifROStreamCurrent(&s), header.size));
        } else if (!memcmp(header.type, "av1C", 4)) {
            CHECK(avifParseAV1CodecConfigurationBoxProperty(prop, avifROStreamCurrent(&s), header.size, diag));
        } else if (!memcmp(header.type, "pasp", 4)) {
            CHECK(avifParsePixelAspectRatioBoxProperty(prop, avifROStreamCurrent(&s), header.size));
        } else if (!memcmp(header.type, "clap", 4)) {
            CHECK(avifParseCleanApertureBoxProperty(prop, avifROStreamCurrent(&s), header.size));
        } else if (!memcmp(header.type, "irot", 4)) {
            CHECK(avifParseImageRotationProperty(prop, avifROStreamCurrent(&s), header.size, diag));
        } else if (!memcmp(header.type, "imir", 4)) {
            CHECK(avifParseImageMirrorProperty(prop, avifROStreamCurrent(&s), header.size, diag));
        } else if (!memcmp(header.type, "pixi", 4)) {
            CHECK(avifParsePixelInformationProperty(prop, avifROStreamCurrent(&s), header.size, diag));
        }

        CHECK(avifROStreamSkip(&s, header.size));
    }
    return AVIF_TRUE;
}

static avifBool avifParseItemPropertyAssociation(avifMeta * meta, const uint8_t * raw, size_t rawLen, avifDiagnostics * diag, uint32_t * outVersionAndFlags)
{
    // NOTE: If this function ever adds support for versions other than [0,1] or flags other than
    //       [0,1], please increase the value of MAX_IPMA_VERSION_AND_FLAGS_SEEN accordingly.

    BEGIN_STREAM(s, raw, rawLen);

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
            avifDiagnosticsPrintf(diag, "Box type 'ipma' item IDs are not ordered by increasing ID");
            return AVIF_FALSE;
        }
        prevItemID = itemID;

        avifDecoderItem * item = avifMetaFindItem(meta, itemID);
        if (!item) {
            avifDiagnosticsPrintf(diag, "Box type 'ipma' has an invalid item ID [%u]", itemID);
            return AVIF_FALSE;
        }
        if (item->ipmaSeen) {
            avifDiagnosticsPrintf(diag, "Duplicate box type 'ipma' for item ID [%u]", itemID);
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
                                      "Box type 'ipma' for item ID [%u] contains an illegal property index [%u] (out of [%u] properties)",
                                      itemID,
                                      propertyIndex,
                                      meta->properties.count);
                return AVIF_FALSE;
            }

            // Copy property to item
            avifProperty * srcProp = &meta->properties.prop[propertyIndex];

            static const char * supportedTypes[] = { "ispe", "auxC", "colr", "av1C", "pasp", "clap", "irot", "imir", "pixi" };
            size_t supportedTypesCount = sizeof(supportedTypes) / sizeof(supportedTypes[0]);
            avifBool supportedType = AVIF_FALSE;
            for (size_t i = 0; i < supportedTypesCount; ++i) {
                if (!memcmp(srcProp->type, supportedTypes[i], 4)) {
                    supportedType = AVIF_TRUE;
                    break;
                }
            }
            if (supportedType) {
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
        avifDiagnosticsPrintf(diag, "Multiple boxes of unique box type 'pitm' found");
        return AVIF_FALSE;
    }

    BEGIN_STREAM(s, raw, rawLen);

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
    for (uint32_t i = 0; i < meta->idats.count; ++i) {
        if (meta->idats.idat[i].id == meta->idatID) {
            avifDiagnosticsPrintf(diag, "Meta box contains multiple idat boxes");
            return AVIF_FALSE;
        }
    }

    int index = avifArrayPushIndex(&meta->idats);
    avifDecoderItemData * idat = &meta->idats.idat[index];
    idat->id = meta->idatID;
    avifRWDataSet(&idat->data, raw, rawLen);
    return AVIF_TRUE;
}

static avifBool avifParseItemPropertiesBox(avifMeta * meta, const uint8_t * raw, size_t rawLen, avifDiagnostics * diag)
{
    BEGIN_STREAM(s, raw, rawLen);

    avifBoxHeader ipcoHeader;
    CHECK(avifROStreamReadBoxHeader(&s, &ipcoHeader));
    if (memcmp(ipcoHeader.type, "ipco", 4)) {
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
                    avifDiagnosticsPrintf(diag, "Multiple box type 'ipma' with a given pair of values of version and flags. See HEIF (ISO 23008-12:2017) 9.3.1");
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
            avifDiagnosticsPrintf(diag, "Box type 'iprp' contains a box that isn't type 'ipma'");
            return AVIF_FALSE;
        }

        CHECK(avifROStreamSkip(&s, ipmaHeader.size));
    }
    return AVIF_TRUE;
}

static avifBool avifParseItemInfoEntry(avifMeta * meta, const uint8_t * raw, size_t rawLen, avifDiagnostics * diag)
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

    avifDecoderItem * item = avifMetaFindItem(meta, itemID);
    if (!item) {
        avifDiagnosticsPrintf(diag, "Box type 'infe' has an invalid item ID [%u]", itemID);
        return AVIF_FALSE;
    }

    memcpy(item->type, itemType, sizeof(itemType));
    memcpy(&item->contentType, &contentType, sizeof(contentType));
    return AVIF_TRUE;
}

static avifBool avifParseItemInfoBox(avifMeta * meta, const uint8_t * raw, size_t rawLen, avifDiagnostics * diag)
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
            CHECK(avifParseItemInfoEntry(meta, avifROStreamCurrent(&s), infeHeader.size, diag));
        } else {
            // These must all be type ipma
            return AVIF_FALSE;
        }

        CHECK(avifROStreamSkip(&s, infeHeader.size));
    }

    return AVIF_TRUE;
}

static avifBool avifParseItemReferenceBox(avifMeta * meta, const uint8_t * raw, size_t rawLen, avifDiagnostics * diag)
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
                avifDecoderItem * item = avifMetaFindItem(meta, fromID);
                if (!item) {
                    avifDiagnosticsPrintf(diag, "Box type 'iref' has an invalid item ID [%u]", fromID);
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
                        avifDiagnosticsPrintf(diag, "Box type 'iref' has an invalid item ID dimg ref [%u]", toID);
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
    BEGIN_STREAM(s, raw, rawLen);

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
        return AVIF_FALSE;
    }
    return AVIF_TRUE;
}

static avifBool avifParseTrackHeaderBox(avifTrack * track, const uint8_t * raw, size_t rawLen, avifDiagnostics * diag)
{
    BEGIN_STREAM(s, raw, rawLen);

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
        avifDiagnosticsPrintf(diag, "Box type 'tkhd' has an unsupported version [%u]", version);
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

static avifBool avifParseMediaHeaderBox(avifTrack * track, const uint8_t * raw, size_t rawLen, avifDiagnostics * diag)
{
    BEGIN_STREAM(s, raw, rawLen);

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
        avifDiagnosticsPrintf(diag, "Box type 'mdhd' has an unsupported version [%u]", version);
        return AVIF_FALSE;
    }

    track->mediaTimescale = mediaTimescale;
    return AVIF_TRUE;
}

static avifBool avifParseChunkOffsetBox(avifSampleTable * sampleTable, avifBool largeOffsets, const uint8_t * raw, size_t rawLen)
{
    BEGIN_STREAM(s, raw, rawLen);

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
    BEGIN_STREAM(s, raw, rawLen);

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
                avifDiagnosticsPrintf(diag, "Box type 'stsc' does not begin with chunk 1 [%u]", sampleToChunk->firstChunk);
                return AVIF_FALSE;
            }
        } else {
            if (sampleToChunk->firstChunk <= prevFirstChunk) {
                avifDiagnosticsPrintf(diag, "Box type 'stsc' chunks are not strictly increasing");
                return AVIF_FALSE;
            }
        }
        prevFirstChunk = sampleToChunk->firstChunk;
    }
    return AVIF_TRUE;
}

static avifBool avifParseSampleSizeBox(avifSampleTable * sampleTable, const uint8_t * raw, size_t rawLen)
{
    BEGIN_STREAM(s, raw, rawLen);

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

static avifBool avifParseSyncSampleBox(avifSampleTable * sampleTable, const uint8_t * raw, size_t rawLen)
{
    BEGIN_STREAM(s, raw, rawLen);

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

static avifBool avifParseTimeToSampleBox(avifSampleTable * sampleTable, const uint8_t * raw, size_t rawLen)
{
    BEGIN_STREAM(s, raw, rawLen);

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
    BEGIN_STREAM(s, raw, rawLen);

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
        return AVIF_FALSE;
    }
    track->sampleTable = avifSampleTableCreate();

    BEGIN_STREAM(s, raw, rawLen);

    while (avifROStreamHasBytesLeft(&s, 1)) {
        avifBoxHeader header;
        CHECK(avifROStreamReadBoxHeader(&s, &header));

        if (!memcmp(header.type, "stco", 4)) {
            CHECK(avifParseChunkOffsetBox(track->sampleTable, AVIF_FALSE, avifROStreamCurrent(&s), header.size));
        } else if (!memcmp(header.type, "co64", 4)) {
            CHECK(avifParseChunkOffsetBox(track->sampleTable, AVIF_TRUE, avifROStreamCurrent(&s), header.size));
        } else if (!memcmp(header.type, "stsc", 4)) {
            CHECK(avifParseSampleToChunkBox(track->sampleTable, avifROStreamCurrent(&s), header.size, diag));
        } else if (!memcmp(header.type, "stsz", 4)) {
            CHECK(avifParseSampleSizeBox(track->sampleTable, avifROStreamCurrent(&s), header.size));
        } else if (!memcmp(header.type, "stss", 4)) {
            CHECK(avifParseSyncSampleBox(track->sampleTable, avifROStreamCurrent(&s), header.size));
        } else if (!memcmp(header.type, "stts", 4)) {
            CHECK(avifParseTimeToSampleBox(track->sampleTable, avifROStreamCurrent(&s), header.size));
        } else if (!memcmp(header.type, "stsd", 4)) {
            CHECK(avifParseSampleDescriptionBox(track->sampleTable, avifROStreamCurrent(&s), header.size, diag));
        }

        CHECK(avifROStreamSkip(&s, header.size));
    }
    return AVIF_TRUE;
}

static avifBool avifParseMediaInformationBox(avifTrack * track, const uint8_t * raw, size_t rawLen, avifDiagnostics * diag)
{
    BEGIN_STREAM(s, raw, rawLen);

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
    BEGIN_STREAM(s, raw, rawLen);

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

static avifBool avifTrackReferenceBox(avifTrack * track, const uint8_t * raw, size_t rawLen)
{
    BEGIN_STREAM(s, raw, rawLen);

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

static avifBool avifParseTrackBox(avifDecoderData * data, const uint8_t * raw, size_t rawLen)
{
    BEGIN_STREAM(s, raw, rawLen);

    avifTrack * track = avifDecoderDataCreateTrack(data);

    while (avifROStreamHasBytesLeft(&s, 1)) {
        avifBoxHeader header;
        CHECK(avifROStreamReadBoxHeader(&s, &header));

        if (!memcmp(header.type, "tkhd", 4)) {
            CHECK(avifParseTrackHeaderBox(track, avifROStreamCurrent(&s), header.size, data->diag));
        } else if (!memcmp(header.type, "meta", 4)) {
            CHECK(avifParseMetaBox(track->meta, avifROStreamCurrent(&s), header.size, data->diag));
        } else if (!memcmp(header.type, "mdia", 4)) {
            CHECK(avifParseMediaBox(track, avifROStreamCurrent(&s), header.size, data->diag));
        } else if (!memcmp(header.type, "tref", 4)) {
            CHECK(avifTrackReferenceBox(track, avifROStreamCurrent(&s), header.size));
        }

        CHECK(avifROStreamSkip(&s, header.size));
    }
    return AVIF_TRUE;
}

static avifBool avifParseMoovBox(avifDecoderData * data, const uint8_t * raw, size_t rawLen)
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

static avifBool avifParseFileTypeBox(avifFileType * ftyp, const uint8_t * raw, size_t rawLen, avifDiagnostics * diag)
{
    BEGIN_STREAM(s, raw, rawLen);

    CHECK(avifROStreamRead(&s, ftyp->majorBrand, 4));
    CHECK(avifROStreamReadU32(&s, &ftyp->minorVersion));

    size_t compatibleBrandsBytes = avifROStreamRemainingBytes(&s);
    if ((compatibleBrandsBytes % 4) != 0) {
        avifDiagnosticsPrintf(diag, "Box type 'ftyp' contains a compatible brands section that isn't divisible by 4 [%u]", compatibleBrandsBytes);
        return AVIF_FALSE;
    }
    ftyp->compatibleBrands = avifROStreamCurrent(&s);
    CHECK(avifROStreamSkip(&s, compatibleBrandsBytes));
    ftyp->compatibleBrandsCount = (int)compatibleBrandsBytes / 4;

    return AVIF_TRUE;
}

static avifBool avifFileTypeHasBrand(avifFileType * ftyp, const char * brand);
static avifBool avifFileTypeIsCompatible(avifFileType * ftyp);

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
        BEGIN_STREAM(headerStream, headerContents.data, headerContents.size);
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
            needsMeta = avifFileTypeHasBrand(&ftyp, "avif");
            needsMoov = avifFileTypeHasBrand(&ftyp, "avis");
        } else if (!memcmp(header.type, "meta", 4)) {
            CHECKERR(!metaSeen, AVIF_RESULT_BMFF_PARSE_FAILED);
            CHECKERR(avifParseMetaBox(data->meta, boxContents.data, boxContents.size, data->diag), AVIF_RESULT_BMFF_PARSE_FAILED);
            metaSeen = AVIF_TRUE;
        } else if (!memcmp(header.type, "moov", 4)) {
            CHECKERR(!moovSeen, AVIF_RESULT_BMFF_PARSE_FAILED);
            CHECKERR(avifParseMoovBox(data, boxContents.data, boxContents.size), AVIF_RESULT_BMFF_PARSE_FAILED);
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

static avifBool avifFileTypeHasBrand(avifFileType * ftyp, const char * brand)
{
    if (!memcmp(ftyp->majorBrand, brand, 4)) {
        return AVIF_TRUE;
    }

    for (int compatibleBrandIndex = 0; compatibleBrandIndex < ftyp->compatibleBrandsCount; ++compatibleBrandIndex) {
        const uint8_t * compatibleBrand = &ftyp->compatibleBrands[4 * compatibleBrandIndex];
        if (!memcmp(compatibleBrand, brand, 4)) {
            return AVIF_TRUE;
        }
    }
    return AVIF_FALSE;
}

static avifBool avifFileTypeIsCompatible(avifFileType * ftyp)
{
    return avifFileTypeHasBrand(ftyp, "avif") || avifFileTypeHasBrand(ftyp, "avis");
}

avifBool avifPeekCompatibleFileType(const avifROData * input)
{
    BEGIN_STREAM(s, input->data, input->size);

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

avifDecoder * avifDecoderCreate(void)
{
    avifDecoder * decoder = (avifDecoder *)avifAlloc(sizeof(avifDecoder));
    memset(decoder, 0, sizeof(avifDecoder));
    decoder->maxThreads = 1;
    decoder->imageCountLimit = AVIF_DEFAULT_IMAGE_COUNT_LIMIT;
    avifDiagnosticsClearError(&decoder->diag);
    return decoder;
}

static void avifDecoderCleanup(avifDecoder * decoder)
{
    if (decoder->data) {
        avifDecoderDataDestroy(decoder->data);
        decoder->data = NULL;
    }

    if (decoder->image) {
        avifImageDestroy(decoder->image);
        decoder->image = NULL;
    }
    avifDiagnosticsClearError(&decoder->diag);
}

void avifDecoderDestroy(avifDecoder * decoder)
{
    avifDecoderCleanup(decoder);
    avifIODestroy(decoder->io);
    avifFree(decoder);
}

avifResult avifDecoderSetSource(avifDecoder * decoder, avifDecoderSource source)
{
    decoder->requestedSource = source;
    return avifDecoderReset(decoder);
}

void avifDecoderSetIO(avifDecoder * decoder, avifIO * io)
{
    avifIODestroy(decoder->io);
    decoder->io = io;
}

avifResult avifDecoderSetIOMemory(avifDecoder * decoder, const uint8_t * data, size_t size)
{
    avifIO * io = avifIOCreateMemoryReader(data, size);
    assert(io);
    avifDecoderSetIO(decoder, io);
    return AVIF_RESULT_OK;
}

avifResult avifDecoderSetIOFile(avifDecoder * decoder, const char * filename)
{
    avifIO * io = avifIOCreateFileReader(filename);
    if (!io) {
        return AVIF_RESULT_IO_ERROR;
    }
    avifDecoderSetIO(decoder, io);
    return AVIF_RESULT_OK;
}

// 0-byte extents are ignored/overwritten during the merge, as they are the signal from helper
// functions that no extent was necessary for this given sample. If both provided extents are
// >0 bytes, this will set dst to be an extent that bounds both supplied extents.
static avifResult avifExtentMerge(avifExtent * dst, const avifExtent * src)
{
    if (!dst->size) {
        memcpy(dst, src, sizeof(avifExtent));
        return AVIF_RESULT_OK;
    }
    if (!src->size) {
        return AVIF_RESULT_OK;
    }

    const uint64_t minExtent1 = dst->offset;
    const uint64_t maxExtent1 = dst->offset + dst->size;
    const uint64_t minExtent2 = src->offset;
    const uint64_t maxExtent2 = src->offset + src->size;
    dst->offset = AVIF_MIN(minExtent1, minExtent2);
    const uint64_t extentLength = AVIF_MAX(maxExtent1, maxExtent2) - dst->offset;
    if (extentLength > SIZE_MAX) {
        return AVIF_RESULT_BMFF_PARSE_FAILED;
    }
    dst->size = (size_t)extentLength;
    return AVIF_RESULT_OK;
}

avifResult avifDecoderNthImageMaxExtent(const avifDecoder * decoder, uint32_t frameIndex, avifExtent * outExtent)
{
    if (!decoder->data) {
        // Nothing has been parsed yet
        return AVIF_RESULT_NO_CONTENT;
    }

    memset(outExtent, 0, sizeof(avifExtent));

    uint32_t startFrameIndex = avifDecoderNearestKeyframe(decoder, frameIndex);
    uint32_t endFrameIndex = frameIndex;
    for (uint32_t currentFrameIndex = startFrameIndex; currentFrameIndex <= endFrameIndex; ++currentFrameIndex) {
        for (unsigned int tileIndex = 0; tileIndex < decoder->data->tiles.count; ++tileIndex) {
            avifTile * tile = &decoder->data->tiles.tile[tileIndex];
            if (currentFrameIndex >= tile->input->samples.count) {
                return AVIF_RESULT_NO_IMAGES_REMAINING;
            }

            avifDecodeSample * sample = &tile->input->samples.sample[currentFrameIndex];
            avifExtent sampleExtent;
            if (sample->itemID) {
                // The data comes from an item. Let avifDecoderItemMaxExtent() do the heavy lifting.

                avifDecoderItem * item = avifMetaFindItem(decoder->data->meta, sample->itemID);
                avifResult maxExtentResult = avifDecoderItemMaxExtent(item, &sampleExtent);
                if (maxExtentResult != AVIF_RESULT_OK) {
                    return maxExtentResult;
                }
            } else {
                // The data likely comes from a sample table. Use the sample position directly.

                sampleExtent.offset = sample->offset;
                sampleExtent.size = sample->size;
            }

            if (sampleExtent.size > UINT64_MAX - sampleExtent.offset) {
                return AVIF_RESULT_BMFF_PARSE_FAILED;
            }

            avifResult extentMergeResult = avifExtentMerge(outExtent, &sampleExtent);
            if (extentMergeResult != AVIF_RESULT_OK) {
                return extentMergeResult;
            }
        }
    }
    return AVIF_RESULT_OK;
}

static avifResult avifDecoderPrepareSample(avifDecoder * decoder, avifDecodeSample * sample, size_t partialByteCount)
{
    if (!sample->data.size || sample->partialData) {
        // This sample hasn't been read from IO or had its extents fully merged yet.

        if (sample->itemID) {
            // The data comes from an item. Let avifDecoderItemRead() do the heavy lifting.

            avifDecoderItem * item = avifMetaFindItem(decoder->data->meta, sample->itemID);
            avifROData itemContents;
            avifResult readResult = avifDecoderItemRead(item, decoder->io, &itemContents, partialByteCount);
            if (readResult != AVIF_RESULT_OK) {
                return readResult;
            }

            // avifDecoderItemRead is guaranteed to already be persisted by either the underlying IO
            // or by mergedExtents; just reuse the buffer here.
            memcpy(&sample->data, &itemContents, sizeof(avifROData));
            sample->ownsData = AVIF_FALSE;
            sample->partialData = item->partialMergedExtents;
        } else {
            // The data likely comes from a sample table. Pull the sample and make a copy if necessary.

            size_t bytesToRead = sample->size;
            if (partialByteCount && (bytesToRead > partialByteCount)) {
                bytesToRead = partialByteCount;
            }

            avifROData sampleContents;
            if ((decoder->io->sizeHint > 0) && (sample->offset > decoder->io->sizeHint)) {
                return AVIF_RESULT_BMFF_PARSE_FAILED;
            }
            avifResult readResult = decoder->io->read(decoder->io, 0, sample->offset, bytesToRead, &sampleContents);
            if (readResult != AVIF_RESULT_OK) {
                return readResult;
            }
            if (sampleContents.size != bytesToRead) {
                return AVIF_RESULT_TRUNCATED_DATA;
            }

            sample->ownsData = !decoder->io->persistent;
            sample->partialData = (bytesToRead != sample->size);
            if (decoder->io->persistent) {
                memcpy(&sample->data, &sampleContents, sizeof(avifROData));
            } else {
                avifRWDataSet((avifRWData *)&sample->data, sampleContents.data, sampleContents.size);
            }
        }
    }
    return AVIF_RESULT_OK;
}

avifResult avifDecoderParse(avifDecoder * decoder)
{
    avifDiagnosticsClearError(&decoder->diag);

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

    return avifDecoderReset(decoder);
}

static avifCodec * avifCodecCreateInternal(avifCodecChoice choice)
{
    return avifCodecCreate(choice, AVIF_CODEC_FLAG_CAN_DECODE);
}

static avifResult avifDecoderFlush(avifDecoder * decoder)
{
    avifDecoderDataResetCodec(decoder->data);

    for (unsigned int i = 0; i < decoder->data->tiles.count; ++i) {
        avifTile * tile = &decoder->data->tiles.tile[i];
        tile->codec = avifCodecCreateInternal(decoder->codecChoice);
        if (!tile->codec) {
            return AVIF_RESULT_NO_CODEC_AVAILABLE;
        }
        if (!tile->codec->open(tile->codec, decoder)) {
            return AVIF_RESULT_DECODE_COLOR_FAILED;
        }
    }
    return AVIF_RESULT_OK;
}

avifResult avifDecoderReset(avifDecoder * decoder)
{
    avifDiagnosticsClearError(&decoder->diag);

    avifDecoderData * data = decoder->data;
    if (!data) {
        // Nothing to reset.
        return AVIF_RESULT_OK;
    }

    memset(&data->colorGrid, 0, sizeof(data->colorGrid));
    memset(&data->alphaGrid, 0, sizeof(data->alphaGrid));
    avifDecoderDataClearTiles(data);

    // Prepare / cleanup decoded image state
    if (decoder->image) {
        avifImageDestroy(decoder->image);
    }
    decoder->image = avifImageCreateEmpty();
    data->cicpSet = AVIF_FALSE;

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

    const avifPropertyArray * colorProperties = NULL;
    if (data->source == AVIF_DECODER_SOURCE_TRACKS) {
        avifTrack * colorTrack = NULL;
        avifTrack * alphaTrack = NULL;

        // Find primary track - this probably needs some better detection
        uint32_t colorTrackIndex = 0;
        for (; colorTrackIndex < data->tracks.count; ++colorTrackIndex) {
            avifTrack * track = &data->tracks.track[colorTrackIndex];
            if (!track->sampleTable) {
                continue;
            }
            if (!track->id) { // trak box might be missing a tkhd box inside, skip it
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
        if (colorTrackIndex == data->tracks.count) {
            return AVIF_RESULT_NO_CONTENT;
        }
        colorTrack = &data->tracks.track[colorTrackIndex];

        colorProperties = avifSampleTableGetProperties(colorTrack->sampleTable);
        if (!colorProperties) {
            return AVIF_RESULT_BMFF_PARSE_FAILED;
        }

        // Find Exif and/or XMP metadata, if any
        if (colorTrack->meta) {
            // See the comment above avifDecoderFindMetadata() for the explanation of using 0 here
            avifResult findResult = avifDecoderFindMetadata(decoder, colorTrack->meta, decoder->image, 0);
            if (findResult != AVIF_RESULT_OK) {
                return findResult;
            }
        }

        uint32_t alphaTrackIndex = 0;
        for (; alphaTrackIndex < data->tracks.count; ++alphaTrackIndex) {
            avifTrack * track = &data->tracks.track[alphaTrackIndex];
            if (!track->sampleTable) {
                continue;
            }
            if (!track->id) {
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
        if (alphaTrackIndex != data->tracks.count) {
            alphaTrack = &data->tracks.track[alphaTrackIndex];
        }

        avifTile * colorTile = avifDecoderDataCreateTile(data);
        if (!avifCodecDecodeInputGetSamples(colorTile->input, colorTrack->sampleTable, decoder->imageCountLimit, decoder->io->sizeHint, data->diag)) {
            return AVIF_RESULT_BMFF_PARSE_FAILED;
        }
        data->colorTileCount = 1;

        if (alphaTrack) {
            avifTile * alphaTile = avifDecoderDataCreateTile(data);
            if (!avifCodecDecodeInputGetSamples(alphaTile->input,
                                                alphaTrack->sampleTable,
                                                decoder->imageCountLimit,
                                                decoder->io->sizeHint,
                                                data->diag)) {
                return AVIF_RESULT_BMFF_PARSE_FAILED;
            }
            alphaTile->input->alpha = AVIF_TRUE;
            data->alphaTileCount = 1;
        }

        // Stash off sample table for future timing information
        data->sourceSampleTable = colorTrack->sampleTable;

        // Image sequence timing
        decoder->imageIndex = -1;
        decoder->imageCount = colorTile->input->samples.count;
        decoder->timescale = colorTrack->mediaTimescale;
        decoder->durationInTimescales = colorTrack->mediaDuration;
        if (colorTrack->mediaTimescale) {
            decoder->duration = (double)decoder->durationInTimescales / (double)colorTrack->mediaTimescale;
        } else {
            decoder->duration = 0;
        }
        memset(&decoder->imageTiming, 0, sizeof(decoder->imageTiming)); // to be set in avifDecoderNextImage()

        decoder->image->width = colorTrack->width;
        decoder->image->height = colorTrack->height;
        decoder->alphaPresent = (alphaTrack != NULL);
        decoder->image->alphaPremultiplied = decoder->alphaPresent && (colorTrack->premByID == alphaTrack->id);
    } else {
        // Create from items

        avifDecoderItem * colorItem = NULL;
        avifDecoderItem * alphaItem = NULL;

        if (data->meta->primaryItemID == 0) {
            // A primary item is required
            return AVIF_RESULT_NO_AV1_ITEMS_FOUND;
        }

        // Find the colorOBU (primary) item
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
            if (item->thumbnailForID != 0) {
                // It's a thumbnail, skip it
                continue;
            }
            if ((data->meta->primaryItemID > 0) && (item->id != data->meta->primaryItemID)) {
                // a primary item ID was specified, require it
                continue;
            }

            if (isGrid) {
                avifROData readData;
                avifResult readResult = avifDecoderItemRead(item, decoder->io, &readData, 0);
                if (readResult != AVIF_RESULT_OK) {
                    return readResult;
                }
                if (!avifParseImageGridBox(&data->colorGrid, readData.data, readData.size, data->diag)) {
                    return AVIF_RESULT_INVALID_IMAGE_GRID;
                }
            }

            colorItem = item;
            break;
        }

        if (!colorItem) {
            return AVIF_RESULT_NO_AV1_ITEMS_FOUND;
        }
        colorProperties = &colorItem->properties;

        // Find the alphaOBU item, if any
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

            // Is this an alpha auxiliary item of whatever we chose for colorItem?
            const avifProperty * auxCProp = avifPropertyArrayFind(&item->properties, "auxC");
            if (auxCProp && isAlphaURN(auxCProp->u.auxC.auxType) && (item->auxForID == colorItem->id)) {
                if (isGrid) {
                    avifROData readData;
                    avifResult readResult = avifDecoderItemRead(item, decoder->io, &readData, 0);
                    if (readResult != AVIF_RESULT_OK) {
                        return readResult;
                    }
                    if (!avifParseImageGridBox(&data->alphaGrid, readData.data, readData.size, data->diag)) {
                        return AVIF_RESULT_INVALID_IMAGE_GRID;
                    }
                }

                alphaItem = item;
                break;
            }
        }

        // Find Exif and/or XMP metadata, if any
        avifResult findResult = avifDecoderFindMetadata(decoder, data->meta, decoder->image, colorItem->id);
        if (findResult != AVIF_RESULT_OK) {
            return findResult;
        }

        if ((data->colorGrid.rows > 0) && (data->colorGrid.columns > 0)) {
            if (!avifDecoderDataGenerateImageGridTiles(data, &data->colorGrid, colorItem, AVIF_FALSE)) {
                return AVIF_RESULT_INVALID_IMAGE_GRID;
            }
            data->colorTileCount = data->tiles.count;
        } else {
            if (colorItem->size == 0) {
                return AVIF_RESULT_NO_AV1_ITEMS_FOUND;
            }

            avifTile * colorTile = avifDecoderDataCreateTile(data);
            avifDecodeSample * colorSample = (avifDecodeSample *)avifArrayPushPtr(&colorTile->input->samples);
            colorSample->itemID = colorItem->id;
            colorSample->offset = 0;
            colorSample->size = colorItem->size;
            colorSample->sync = AVIF_TRUE;
            data->colorTileCount = 1;
        }

        if (alphaItem) {
            if ((data->alphaGrid.rows > 0) && (data->alphaGrid.columns > 0)) {
                if (!avifDecoderDataGenerateImageGridTiles(data, &data->alphaGrid, alphaItem, AVIF_TRUE)) {
                    return AVIF_RESULT_INVALID_IMAGE_GRID;
                }
                data->alphaTileCount = data->tiles.count - data->colorTileCount;
            } else {
                if (alphaItem->size == 0) {
                    return AVIF_RESULT_NO_AV1_ITEMS_FOUND;
                }

                avifTile * alphaTile = avifDecoderDataCreateTile(data);
                avifDecodeSample * alphaSample = (avifDecodeSample *)avifArrayPushPtr(&alphaTile->input->samples);
                alphaSample->itemID = alphaItem->id;
                alphaSample->offset = 0;
                alphaSample->size = alphaItem->size;
                alphaSample->sync = AVIF_TRUE;
                alphaTile->input->alpha = AVIF_TRUE;
                data->alphaTileCount = 1;
            }
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

        decoder->ioStats.colorOBUSize = colorItem->size;
        decoder->ioStats.alphaOBUSize = alphaItem ? alphaItem->size : 0;

        const avifProperty * ispeProp = avifPropertyArrayFind(colorProperties, "ispe");
        if (ispeProp) {
            decoder->image->width = ispeProp->u.ispe.width;
            decoder->image->height = ispeProp->u.ispe.height;
        } else {
            decoder->image->width = 0;
            decoder->image->height = 0;
        }
        decoder->alphaPresent = (alphaItem != NULL);
        decoder->image->alphaPremultiplied = decoder->alphaPresent && (colorItem->premByID == alphaItem->id);

        avifResult colorItemValidationResult = avifDecoderItemValidateAV1(colorItem);
        if (colorItemValidationResult != AVIF_RESULT_OK) {
            return colorItemValidationResult;
        }
        if (alphaItem) {
            avifResult alphaItemValidationResult = avifDecoderItemValidateAV1(alphaItem);
            if (alphaItemValidationResult != AVIF_RESULT_OK) {
                return alphaItemValidationResult;
            }
        }
    }

    // Sanity check tiles
    for (uint32_t tileIndex = 0; tileIndex < data->tiles.count; ++tileIndex) {
        avifTile * tile = &data->tiles.tile[tileIndex];
        for (uint32_t sampleIndex = 0; sampleIndex < tile->input->samples.count; ++sampleIndex) {
            avifDecodeSample * sample = &tile->input->samples.sample[sampleIndex];
            if (!sample->size) {
                // Every sample must have some data
                return AVIF_RESULT_BMFF_PARSE_FAILED;
            }
        }
    }

    // Find and adopt all colr boxes "at most one for a given value of colour type" (HEIF 6.5.5.1, from Amendment 3)
    // Accept one of each type, and bail out if more than one of a given type is provided.
    avifBool colrICCSeen = AVIF_FALSE;
    avifBool colrNCLXSeen = AVIF_FALSE;
    for (uint32_t propertyIndex = 0; propertyIndex < colorProperties->count; ++propertyIndex) {
        avifProperty * prop = &colorProperties->prop[propertyIndex];

        if (!memcmp(prop->type, "colr", 4)) {
            if (prop->u.colr.hasICC) {
                if (colrICCSeen) {
                    return AVIF_RESULT_BMFF_PARSE_FAILED;
                }
                colrICCSeen = AVIF_TRUE;
                avifImageSetProfileICC(decoder->image, prop->u.colr.icc, prop->u.colr.iccSize);
            }
            if (prop->u.colr.hasNCLX) {
                if (colrNCLXSeen) {
                    return AVIF_RESULT_BMFF_PARSE_FAILED;
                }
                colrNCLXSeen = AVIF_TRUE;
                data->cicpSet = AVIF_TRUE;
                decoder->image->colorPrimaries = prop->u.colr.colorPrimaries;
                decoder->image->transferCharacteristics = prop->u.colr.transferCharacteristics;
                decoder->image->matrixCoefficients = prop->u.colr.matrixCoefficients;
                decoder->image->yuvRange = prop->u.colr.range;
            }
        }
    }

    // Transformations
    const avifProperty * paspProp = avifPropertyArrayFind(colorProperties, "pasp");
    if (paspProp) {
        decoder->image->transformFlags |= AVIF_TRANSFORM_PASP;
        memcpy(&decoder->image->pasp, &paspProp->u.pasp, sizeof(avifPixelAspectRatioBox));
    }
    const avifProperty * clapProp = avifPropertyArrayFind(colorProperties, "clap");
    if (clapProp) {
        decoder->image->transformFlags |= AVIF_TRANSFORM_CLAP;
        memcpy(&decoder->image->clap, &clapProp->u.clap, sizeof(avifCleanApertureBox));
    }
    const avifProperty * irotProp = avifPropertyArrayFind(colorProperties, "irot");
    if (irotProp) {
        decoder->image->transformFlags |= AVIF_TRANSFORM_IROT;
        memcpy(&decoder->image->irot, &irotProp->u.irot, sizeof(avifImageRotation));
    }
    const avifProperty * imirProp = avifPropertyArrayFind(colorProperties, "imir");
    if (imirProp) {
        decoder->image->transformFlags |= AVIF_TRANSFORM_IMIR;
        memcpy(&decoder->image->imir, &imirProp->u.imir, sizeof(avifImageMirror));
    }

    if (!data->cicpSet && (data->tiles.count > 0)) {
        avifTile * firstTile = &data->tiles.tile[0];
        if (firstTile->input->samples.count > 0) {
            avifDecodeSample * sample = &firstTile->input->samples.sample[0];

            // Harvest CICP from the AV1's sequence header, which should be very close to the front
            // of the first sample. Read in successively larger chunks until we successfully parse the sequence.
            static const size_t searchSampleChunkIncrement = 64;
            static const size_t searchSampleSizeMax = 4096;
            size_t searchSampleSize = 0;
            do {
                searchSampleSize += searchSampleChunkIncrement;
                if (searchSampleSize > sample->size) {
                    searchSampleSize = sample->size;
                }

                avifResult prepareResult = avifDecoderPrepareSample(decoder, sample, searchSampleSize);
                if (prepareResult != AVIF_RESULT_OK) {
                    return prepareResult;
                }

                avifSequenceHeader sequenceHeader;
                if (avifSequenceHeaderParse(&sequenceHeader, &sample->data)) {
                    data->cicpSet = AVIF_TRUE;
                    decoder->image->colorPrimaries = sequenceHeader.colorPrimaries;
                    decoder->image->transferCharacteristics = sequenceHeader.transferCharacteristics;
                    decoder->image->matrixCoefficients = sequenceHeader.matrixCoefficients;
                    decoder->image->yuvRange = sequenceHeader.range;
                    break;
                }
            } while (searchSampleSize != sample->size && searchSampleSize < searchSampleSizeMax);
        }
    }

    const avifProperty * av1CProp = avifPropertyArrayFind(colorProperties, "av1C");
    if (av1CProp) {
        decoder->image->depth = avifCodecConfigurationBoxGetDepth(&av1CProp->u.av1C);
        if (av1CProp->u.av1C.monochrome) {
            decoder->image->yuvFormat = AVIF_PIXEL_FORMAT_YUV400;
        } else {
            if (av1CProp->u.av1C.chromaSubsamplingX && av1CProp->u.av1C.chromaSubsamplingY) {
                decoder->image->yuvFormat = AVIF_PIXEL_FORMAT_YUV420;
            } else if (av1CProp->u.av1C.chromaSubsamplingX) {
                decoder->image->yuvFormat = AVIF_PIXEL_FORMAT_YUV422;

            } else {
                decoder->image->yuvFormat = AVIF_PIXEL_FORMAT_YUV444;
            }
        }
        decoder->image->yuvChromaSamplePosition = (avifChromaSamplePosition)av1CProp->u.av1C.chromaSamplePosition;
    } else {
        // An av1C box is mandatory in all valid AVIF configurations. Bail out.
        return AVIF_RESULT_BMFF_PARSE_FAILED;
    }

    return avifDecoderFlush(decoder);
}

avifResult avifDecoderNextImage(avifDecoder * decoder)
{
    avifDiagnosticsClearError(&decoder->diag);

    if (!decoder->data) {
        // Nothing has been parsed yet
        return AVIF_RESULT_NO_CONTENT;
    }

    if (!decoder->io || !decoder->io->read) {
        return AVIF_RESULT_IO_NOT_SET;
    }

    const uint32_t nextImageIndex = (uint32_t)(decoder->imageIndex + 1);

    // Acquire all sample data for the current image first, allowing for any read call to bail out
    // with AVIF_RESULT_WAITING_ON_IO harmlessly / idempotently.
    for (unsigned int tileIndex = 0; tileIndex < decoder->data->tiles.count; ++tileIndex) {
        avifTile * tile = &decoder->data->tiles.tile[tileIndex];
        if (nextImageIndex >= tile->input->samples.count) {
            return AVIF_RESULT_NO_IMAGES_REMAINING;
        }

        avifDecodeSample * sample = &tile->input->samples.sample[nextImageIndex];
        avifResult prepareResult = avifDecoderPrepareSample(decoder, sample, 0);
        if (prepareResult != AVIF_RESULT_OK) {
            return prepareResult;
        }
    }

    // Decode all tiles now that the sample data is ready.
    for (unsigned int tileIndex = 0; tileIndex < decoder->data->tiles.count; ++tileIndex) {
        avifTile * tile = &decoder->data->tiles.tile[tileIndex];

        const avifDecodeSample * sample = &tile->input->samples.sample[nextImageIndex];

        if (!tile->codec->getNextImage(tile->codec, sample, tile->input->alpha, tile->image)) {
            return tile->input->alpha ? AVIF_RESULT_DECODE_ALPHA_FAILED : AVIF_RESULT_DECODE_COLOR_FAILED;
        }
    }

    if (decoder->data->tiles.count != (decoder->data->colorTileCount + decoder->data->alphaTileCount)) {
        // TODO: assert here? This should be impossible.
        return AVIF_RESULT_UNKNOWN_ERROR;
    }

    if ((decoder->data->colorGrid.rows > 0) && (decoder->data->colorGrid.columns > 0)) {
        if (!avifDecoderDataFillImageGrid(decoder->data, &decoder->data->colorGrid, decoder->image, 0, decoder->data->colorTileCount, AVIF_FALSE)) {
            return AVIF_RESULT_INVALID_IMAGE_GRID;
        }
    } else {
        // Normal (most common) non-grid path. Just steal the planes from the only "tile".

        if (decoder->data->colorTileCount != 1) {
            return AVIF_RESULT_DECODE_COLOR_FAILED;
        }

        avifImage * srcColor = decoder->data->tiles.tile[0].image;

        if ((decoder->image->width != srcColor->width) || (decoder->image->height != srcColor->height) ||
            (decoder->image->depth != srcColor->depth)) {
            avifImageFreePlanes(decoder->image, AVIF_PLANES_ALL);

            decoder->image->width = srcColor->width;
            decoder->image->height = srcColor->height;
            decoder->image->depth = srcColor->depth;
        }

#if 0
        // This code is currently unnecessary as the CICP is always set by the end of avifDecoderParse().
        if (!decoder->data->cicpSet) {
            decoder->data->cicpSet = AVIF_TRUE;
            decoder->image->colorPrimaries = srcColor->colorPrimaries;
            decoder->image->transferCharacteristics = srcColor->transferCharacteristics;
            decoder->image->matrixCoefficients = srcColor->matrixCoefficients;
        }
#endif

        avifImageStealPlanes(decoder->image, srcColor, AVIF_PLANES_YUV);
    }

    if ((decoder->data->alphaGrid.rows > 0) && (decoder->data->alphaGrid.columns > 0)) {
        if (!avifDecoderDataFillImageGrid(decoder->data,
                                          &decoder->data->alphaGrid,
                                          decoder->image,
                                          decoder->data->colorTileCount,
                                          decoder->data->alphaTileCount,
                                          AVIF_TRUE)) {
            return AVIF_RESULT_INVALID_IMAGE_GRID;
        }
    } else {
        // Normal (most common) non-grid path. Just steal the planes from the only "tile".

        if (decoder->data->alphaTileCount == 0) {
            avifImageFreePlanes(decoder->image, AVIF_PLANES_A); // no alpha
        } else {
            if (decoder->data->alphaTileCount != 1) {
                return AVIF_RESULT_DECODE_ALPHA_FAILED;
            }

            avifImage * srcAlpha = decoder->data->tiles.tile[decoder->data->colorTileCount].image;
            if ((decoder->image->width != srcAlpha->width) || (decoder->image->height != srcAlpha->height) ||
                (decoder->image->depth != srcAlpha->depth)) {
                return AVIF_RESULT_DECODE_ALPHA_FAILED;
            }

            avifImageStealPlanes(decoder->image, srcAlpha, AVIF_PLANES_A);
            decoder->image->alphaRange = srcAlpha->alphaRange;
        }
    }

    decoder->imageIndex = nextImageIndex;
    if (decoder->data->sourceSampleTable) {
        // Decoding from a track! Provide timing information.

        avifResult timingResult = avifDecoderNthImageTiming(decoder, decoder->imageIndex, &decoder->imageTiming);
        if (timingResult != AVIF_RESULT_OK) {
            return timingResult;
        }
    }
    return AVIF_RESULT_OK;
}

avifResult avifDecoderNthImageTiming(const avifDecoder * decoder, uint32_t frameIndex, avifImageTiming * outTiming)
{
    if (!decoder->data) {
        // Nothing has been parsed yet
        return AVIF_RESULT_NO_CONTENT;
    }

    if ((frameIndex > INT_MAX) || ((int)frameIndex >= decoder->imageCount)) {
        // Impossible index
        return AVIF_RESULT_NO_IMAGES_REMAINING;
    }

    if (!decoder->data->sourceSampleTable) {
        // There isn't any real timing associated with this decode, so
        // just hand back the defaults chosen in avifDecoderReset().
        memcpy(outTiming, &decoder->imageTiming, sizeof(avifImageTiming));
        return AVIF_RESULT_OK;
    }

    outTiming->timescale = decoder->timescale;
    outTiming->ptsInTimescales = 0;
    for (int imageIndex = 0; imageIndex < (int)frameIndex; ++imageIndex) {
        outTiming->ptsInTimescales += avifSampleTableGetImageDelta(decoder->data->sourceSampleTable, imageIndex);
    }
    outTiming->durationInTimescales = avifSampleTableGetImageDelta(decoder->data->sourceSampleTable, frameIndex);

    if (outTiming->timescale > 0) {
        outTiming->pts = (double)outTiming->ptsInTimescales / (double)outTiming->timescale;
        outTiming->duration = (double)outTiming->durationInTimescales / (double)outTiming->timescale;
    } else {
        outTiming->pts = 0.0;
        outTiming->duration = 0.0;
    }
    return AVIF_RESULT_OK;
}

avifResult avifDecoderNthImage(avifDecoder * decoder, uint32_t frameIndex)
{
    avifDiagnosticsClearError(&decoder->diag);

    if (frameIndex > INT_MAX) {
        // Impossible index
        return AVIF_RESULT_NO_IMAGES_REMAINING;
    }

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

    int nearestKeyFrame = (int)avifDecoderNearestKeyframe(decoder, frameIndex);
    if ((nearestKeyFrame > (decoder->imageIndex + 1)) || (requestedIndex < decoder->imageIndex)) {
        // If we get here, a decoder flush is necessary
        decoder->imageIndex = nearestKeyFrame - 1; // prepare to read nearest keyframe
        avifDecoderFlush(decoder);
    }
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

avifBool avifDecoderIsKeyframe(const avifDecoder * decoder, uint32_t frameIndex)
{
    if (!decoder->data) {
        // Nothing has been parsed yet
        return AVIF_FALSE;
    }

    if ((decoder->data->tiles.count > 0) && decoder->data->tiles.tile[0].input) {
        if (frameIndex < decoder->data->tiles.tile[0].input->samples.count) {
            return decoder->data->tiles.tile[0].input->samples.sample[frameIndex].sync;
        }
    }
    return AVIF_FALSE;
}

uint32_t avifDecoderNearestKeyframe(const avifDecoder * decoder, uint32_t frameIndex)
{
    if (!decoder->data) {
        // Nothing has been parsed yet
        return 0;
    }

    for (; frameIndex != 0; --frameIndex) {
        if (avifDecoderIsKeyframe(decoder, frameIndex)) {
            break;
        }
    }
    return frameIndex;
}

avifResult avifDecoderRead(avifDecoder * decoder, avifImage * image)
{
    avifResult result = avifDecoderParse(decoder);
    if (result != AVIF_RESULT_OK) {
        return result;
    }
    result = avifDecoderNextImage(decoder);
    if (result != AVIF_RESULT_OK) {
        return result;
    }
    avifImageCopy(image, decoder->image, AVIF_PLANES_ALL);
    return AVIF_RESULT_OK;
}

avifResult avifDecoderReadMemory(avifDecoder * decoder, avifImage * image, const uint8_t * data, size_t size)
{
    avifDiagnosticsClearError(&decoder->diag);
    avifResult result = avifDecoderSetIOMemory(decoder, data, size);
    if (result != AVIF_RESULT_OK) {
        return result;
    }
    return avifDecoderRead(decoder, image);
}

avifResult avifDecoderReadFile(avifDecoder * decoder, avifImage * image, const char * filename)
{
    avifDiagnosticsClearError(&decoder->diag);
    avifResult result = avifDecoderSetIOFile(decoder, filename);
    if (result != AVIF_RESULT_OK) {
        return result;
    }
    return avifDecoderRead(decoder, image);
}
