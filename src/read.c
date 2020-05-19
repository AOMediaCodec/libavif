// Copyright 2019 Joe Drago. All rights reserved.
// SPDX-License-Identifier: BSD-2-Clause

#include "avif/internal.h"

#include <string.h>

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

// one "item" worth for decoding (all iref, iloc, iprp, etc refer to one of these)
typedef struct avifDecoderItem
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
    avifBool paspPresent;
    avifPixelAspectRatioBox pasp;
    avifBool clapPresent;
    avifCleanApertureBox clap;
    avifBool irotPresent;
    avifImageRotation irot;
    avifBool imirPresent;
    avifImageMirror imir;
    avifBool pixiPresent;
    avifPixelInformationProperty pixi;
    uint32_t thumbnailForID; // if non-zero, this item is a thumbnail for Item #{thumbnailForID}
    uint32_t auxForID;       // if non-zero, this item is an auxC plane for Item #{auxForID}
    uint32_t descForID;      // if non-zero, this item is a content description for Item #{descForID}
    uint32_t dimgForID;      // if non-zero, this item is a derived image for Item #{dimgForID}
    avifBool hasUnsupportedEssentialProperty; // If true, this file cites a property flagged as 'essential' that libavif doesn't support (yet). Ignore the item, if so.
} avifDecoderItem;
AVIF_ARRAY_DECLARE(avifDecoderItemArray, avifDecoderItem, item);

// Temporary storage for ipco contents until they can be associated and memcpy'd to an avifDecoderItem
typedef struct avifProperty
{
    uint8_t type[4];
    avifImageSpatialExtents ispe;
    avifAuxiliaryType auxC;
    avifColourInformationBox colr;
    avifCodecConfigurationBox av1C;
    avifPixelAspectRatioBox pasp;
    avifCleanApertureBox clap;
    avifImageRotation irot;
    avifImageMirror imir;
    avifPixelInformationProperty pixi;
} avifProperty;
AVIF_ARRAY_DECLARE(avifPropertyArray, avifProperty, prop);

// idat storage
typedef struct avifDecoderItemData
{
    uint32_t id;
    avifROData data;
} avifDecoderItemData;
AVIF_ARRAY_DECLARE(avifDecoderItemDataArray, avifDecoderItemData, idat);

// grid storage
typedef struct avifImageGrid
{
    uint8_t rows;
    uint8_t columns;
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

static uint32_t avifSampleTableGetDepth(const avifSampleTable * sampleTable)
{
    for (uint32_t i = 0; i < sampleTable->sampleDescriptions.count; ++i) {
        const avifSampleDescription * description = &sampleTable->sampleDescriptions.description[i];
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
            uint32_t sampleSize = sampleTable->allSamplesSize;
            if (sampleSize == 0) {
                if (sampleSizeIndex >= sampleTable->sampleSizes.count) {
                    // We've run out of samples to sum
                    return AVIF_FALSE;
                }
                avifSampleTableSampleSize * sampleSizePtr = &sampleTable->sampleSizes.sampleSize[sampleSizeIndex];
                sampleSize = sampleSizePtr->size;
            }

            avifSample * sample = (avifSample *)avifArrayPushPtr(&decodeInput->samples);
            sample->data.data = rawInput->data + sampleOffset;
            sample->data.size = sampleSize;
            sample->sync = AVIF_FALSE; // to potentially be set to true following the outer loop

            if ((sampleOffset + sampleSize) > (uint64_t)rawInput->size) {
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
// avifDecoderData

typedef struct avifTile
{
    avifCodecDecodeInput * input;
    struct avifCodec * codec;
    avifImage * image;
} avifTile;
AVIF_ARRAY_DECLARE(avifTileArray, avifTile, tile);

typedef struct avifDecoderData
{
    avifFileType ftyp;
    avifDecoderItemArray items;
    avifPropertyArray properties;
    avifDecoderItemDataArray idats;
    avifTrackArray tracks;
    avifROData rawInput;
    avifTileArray tiles;
    unsigned int colorTileCount;
    unsigned int alphaTileCount;
    avifImageGrid colorGrid;
    avifImageGrid alphaGrid;
    avifDecoderSource source;
    const avifSampleTable * sourceSampleTable; // NULL unless (source == AVIF_DECODER_SOURCE_TRACKS), owned by an avifTrack
    uint32_t primaryItemID;
    uint32_t metaBoxID; // Ever-incrementing ID for uniquely identifying which 'meta' box contains an idat
    avifBool cicpSet;   // True if avifDecoder's image has had its CICP set correctly yet.
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
    avifArrayCreate(&data->items, sizeof(avifDecoderItem), 8);
    avifArrayCreate(&data->properties, sizeof(avifProperty), 16);
    avifArrayCreate(&data->idats, sizeof(avifDecoderItemData), 1);
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
    avifArrayDestroy(&data->items);
    avifArrayDestroy(&data->properties);
    avifArrayDestroy(&data->idats);
    for (uint32_t i = 0; i < data->tracks.count; ++i) {
        if (data->tracks.track[i].sampleTable) {
            avifSampleTableDestroy(data->tracks.track[i].sampleTable);
        }
    }
    avifArrayDestroy(&data->tracks);
    avifDecoderDataClearTiles(data);
    avifArrayDestroy(&data->tiles);
    avifFree(data);
}

static avifDecoderItem * avifDecoderDataFindItem(avifDecoderData * data, uint32_t itemID)
{
    if (itemID == 0) {
        return NULL;
    }

    for (uint32_t i = 0; i < data->items.count; ++i) {
        if (data->items.item[i].id == itemID) {
            return &data->items.item[i];
        }
    }

    avifDecoderItem * item = (avifDecoderItem *)avifArrayPushPtr(&data->items);
    item->id = itemID;
    return item;
}

static const uint8_t * avifDecoderDataCalcItemPtr(avifDecoderData * data, avifDecoderItem * item)
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

static avifBool avifDecoderDataGenerateImageGridTiles(avifDecoderData * data, avifImageGrid * grid, avifDecoderItem * gridItem, avifBool alpha)
{
    unsigned int tilesRequested = (unsigned int)grid->rows * (unsigned int)grid->columns;

    // Count number of dimg for this item, bail out if it doesn't match perfectly
    unsigned int tilesAvailable = 0;
    for (uint32_t i = 0; i < data->items.count; ++i) {
        avifDecoderItem * item = &data->items.item[i];
        if (item->dimgForID == gridItem->id) {
            if (memcmp(item->type, "av01", 4)) {
                continue;
            }
            if (item->hasUnsupportedEssentialProperty) {
                // An essential property isn't supported by libavif; ignore the item.
                continue;
            }

            ++tilesAvailable;
        }
    }

    if (tilesRequested != tilesAvailable) {
        return AVIF_FALSE;
    }

    for (uint32_t i = 0; i < data->items.count; ++i) {
        avifDecoderItem * item = &data->items.item[i];
        if (item->dimgForID == gridItem->id) {
            if (memcmp(item->type, "av01", 4)) {
                continue;
            }
            if (item->hasUnsupportedEssentialProperty) {
                // An essential property isn't supported by libavif; ignore the item.
                continue;
            }

            avifTile * tile = avifDecoderDataCreateTile(data);
            avifSample * sample = (avifSample *)avifArrayPushPtr(&tile->input->samples);
            sample->data.data = avifDecoderDataCalcItemPtr(data, item);
            sample->data.size = item->size;
            sample->sync = AVIF_TRUE;
            tile->input->alpha = alpha;
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
            ((tile->image->colorPrimaries != firstTile->image->colorPrimaries) ||
             (tile->image->transferCharacteristics != firstTile->image->transferCharacteristics) ||
             (tile->image->matrixCoefficients != firstTile->image->matrixCoefficients))) {
            return AVIF_FALSE;
        }
    }

    // Lazily populate dstImage with the new frame's properties. If we're decoding alpha,
    // these values must already match.
    if ((dstImage->width != grid->outputWidth) || (dstImage->height != grid->outputHeight) ||
        (dstImage->depth != firstTile->image->depth) || (dstImage->yuvFormat != firstTile->image->yuvFormat)) {
        if (alpha) {
            // Alpha doesn't match size, just bail out
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
            size_t yaColOffset = colIndex * firstTile->image->width;
            size_t yaRowOffset = rowIndex * firstTile->image->height;
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

// ---------------------------------------------------------------------------
// URN

static avifBool isAlphaURN(char * urn)
{
    return !strcmp(urn, URN_ALPHA0) || !strcmp(urn, URN_ALPHA1);
}

// ---------------------------------------------------------------------------
// BMFF Parsing

#define BEGIN_STREAM(VARNAME, PTR, SIZE) \
    avifROStream VARNAME;                \
    avifROData VARNAME##_roData;         \
    VARNAME##_roData.data = PTR;         \
    VARNAME##_roData.size = SIZE;        \
    avifROStreamStart(&VARNAME, &VARNAME##_roData)

static avifBool avifParseItemLocationBox(avifDecoderData * data, const uint8_t * raw, size_t rawLen)
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

            avifDecoderItem * item = avifDecoderDataFindItem(data, itemID);
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

static avifBool avifParseImageGridBox(avifImageGrid * grid, const uint8_t * raw, size_t rawLen)
{
    BEGIN_STREAM(s, raw, rawLen);

    uint8_t version, flags;
    CHECK(avifROStreamRead(&s, &version, 1)); // unsigned int(8) version = 0;
    if (version != 0) {
        return AVIF_FALSE;
    }
    CHECK(avifROStreamRead(&s, &flags, 1));         // unsigned int(8) flags;
    CHECK(avifROStreamRead(&s, &grid->rows, 1));    // unsigned int(8) rows_minus_one;
    CHECK(avifROStreamRead(&s, &grid->columns, 1)); // unsigned int(8) columns_minus_one;
    ++grid->rows;
    ++grid->columns;

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
            return AVIF_FALSE;
        }
        CHECK(avifROStreamReadU32(&s, &grid->outputWidth));  // unsigned int(FieldLength) output_width;
        CHECK(avifROStreamReadU32(&s, &grid->outputHeight)); // unsigned int(FieldLength) output_height;
    }
    return AVIF_TRUE;
}

static avifBool avifParseImageSpatialExtentsProperty(avifDecoderData * data, const uint8_t * raw, size_t rawLen, int propertyIndex)
{
    BEGIN_STREAM(s, raw, rawLen);
    CHECK(avifROStreamReadAndEnforceVersion(&s, 0));

    CHECK(avifROStreamReadU32(&s, &data->properties.prop[propertyIndex].ispe.width));
    CHECK(avifROStreamReadU32(&s, &data->properties.prop[propertyIndex].ispe.height));
    return AVIF_TRUE;
}

static avifBool avifParseAuxiliaryTypeProperty(avifDecoderData * data, const uint8_t * raw, size_t rawLen, int propertyIndex)
{
    BEGIN_STREAM(s, raw, rawLen);
    CHECK(avifROStreamReadAndEnforceVersion(&s, 0));

    CHECK(avifROStreamReadString(&s, data->properties.prop[propertyIndex].auxC.auxType, AUXTYPE_SIZE));
    return AVIF_TRUE;
}

static avifBool avifParseColourInformationBox(avifDecoderData * data, const uint8_t * raw, size_t rawLen, int propertyIndex)
{
    BEGIN_STREAM(s, raw, rawLen);

    data->properties.prop[propertyIndex].colr.hasICC = AVIF_FALSE;
    data->properties.prop[propertyIndex].colr.hasNCLX = AVIF_FALSE;

    uint8_t colorType[4]; // unsigned int(32) colour_type;
    CHECK(avifROStreamRead(&s, colorType, 4));
    if (!memcmp(colorType, "rICC", 4) || !memcmp(colorType, "prof", 4)) {
        data->properties.prop[propertyIndex].colr.hasICC = AVIF_TRUE;
        data->properties.prop[propertyIndex].colr.icc = avifROStreamCurrent(&s);
        data->properties.prop[propertyIndex].colr.iccSize = avifROStreamRemainingBytes(&s);
    } else if (!memcmp(colorType, "nclx", 4)) {
        uint16_t tmp16;
        // unsigned int(16) colour_primaries;
        CHECK(avifROStreamReadU16(&s, &tmp16));
        data->properties.prop[propertyIndex].colr.colorPrimaries = (avifColorPrimaries)tmp16;
        // unsigned int(16) transfer_characteristics;
        CHECK(avifROStreamReadU16(&s, &tmp16));
        data->properties.prop[propertyIndex].colr.transferCharacteristics = (avifTransferCharacteristics)tmp16;
        // unsigned int(16) matrix_coefficients;
        CHECK(avifROStreamReadU16(&s, &tmp16));
        data->properties.prop[propertyIndex].colr.matrixCoefficients = (avifMatrixCoefficients)tmp16;
        // unsigned int(1) full_range_flag;
        // unsigned int(7) reserved = 0;
        uint8_t tmp8;
        CHECK(avifROStreamRead(&s, &tmp8, 1));
        data->properties.prop[propertyIndex].colr.range = (tmp8 & 0x80) ? AVIF_RANGE_FULL : AVIF_RANGE_LIMITED;
        data->properties.prop[propertyIndex].colr.hasNCLX = AVIF_TRUE;
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

static avifBool avifParseAV1CodecConfigurationBoxProperty(avifDecoderData * data, const uint8_t * raw, size_t rawLen, int propertyIndex)
{
    return avifParseAV1CodecConfigurationBox(raw, rawLen, &data->properties.prop[propertyIndex].av1C);
}

static avifBool avifParsePixelAspectRatioBoxProperty(avifDecoderData * data, const uint8_t * raw, size_t rawLen, int propertyIndex)
{
    BEGIN_STREAM(s, raw, rawLen);

    avifPixelAspectRatioBox * pasp = &data->properties.prop[propertyIndex].pasp;
    CHECK(avifROStreamReadU32(&s, &pasp->hSpacing)); // unsigned int(32) hSpacing;
    CHECK(avifROStreamReadU32(&s, &pasp->vSpacing)); // unsigned int(32) vSpacing;
    return AVIF_TRUE;
}

static avifBool avifParseCleanApertureBoxProperty(avifDecoderData * data, const uint8_t * raw, size_t rawLen, int propertyIndex)
{
    BEGIN_STREAM(s, raw, rawLen);

    avifCleanApertureBox * clap = &data->properties.prop[propertyIndex].clap;
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

static avifBool avifParseImageRotationProperty(avifDecoderData * data, const uint8_t * raw, size_t rawLen, int propertyIndex)
{
    BEGIN_STREAM(s, raw, rawLen);

    avifImageRotation * irot = &data->properties.prop[propertyIndex].irot;
    CHECK(avifROStreamRead(&s, &irot->angle, 1)); // unsigned int (6) reserved = 0; unsigned int (2) angle;
    if ((irot->angle & 0xfc) != 0) {
        // reserved bits must be 0
        return AVIF_FALSE;
    }
    return AVIF_TRUE;
}

static avifBool avifParseImageMirrorProperty(avifDecoderData * data, const uint8_t * raw, size_t rawLen, int propertyIndex)
{
    BEGIN_STREAM(s, raw, rawLen);

    avifImageMirror * imir = &data->properties.prop[propertyIndex].imir;
    CHECK(avifROStreamRead(&s, &imir->axis, 1)); // unsigned int (7) reserved = 0; unsigned int (1) axis;
    if ((imir->axis & 0xfe) != 0) {
        // reserved bits must be 0
        return AVIF_FALSE;
    }
    return AVIF_TRUE;
}

static avifBool avifParsePixelInformationProperty(avifDecoderData * data, const uint8_t * raw, size_t rawLen, int propertyIndex)
{
    BEGIN_STREAM(s, raw, rawLen);
    CHECK(avifROStreamReadAndEnforceVersion(&s, 0));

    avifPixelInformationProperty * pixi = &data->properties.prop[propertyIndex].pixi;
    CHECK(avifROStreamRead(&s, &pixi->planeCount, 1)); // unsigned int (8) num_channels;
    if (pixi->planeCount > MAX_PIXI_PLANE_DEPTHS) {
        return AVIF_FALSE;
    }
    for (uint8_t i = 0; i < pixi->planeCount; ++i) {
        CHECK(avifROStreamRead(&s, &pixi->planeDepths[i], 1)); // unsigned int (8) bits_per_channel;
    }
    return AVIF_TRUE;
}

static avifBool avifParseItemPropertyContainerBox(avifDecoderData * data, const uint8_t * raw, size_t rawLen)
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
        if (!memcmp(header.type, "pasp", 4)) {
            CHECK(avifParsePixelAspectRatioBoxProperty(data, avifROStreamCurrent(&s), header.size, propertyIndex));
        }
        if (!memcmp(header.type, "clap", 4)) {
            CHECK(avifParseCleanApertureBoxProperty(data, avifROStreamCurrent(&s), header.size, propertyIndex));
        }
        if (!memcmp(header.type, "irot", 4)) {
            CHECK(avifParseImageRotationProperty(data, avifROStreamCurrent(&s), header.size, propertyIndex));
        }
        if (!memcmp(header.type, "imir", 4)) {
            CHECK(avifParseImageMirrorProperty(data, avifROStreamCurrent(&s), header.size, propertyIndex));
        }
        if (!memcmp(header.type, "pixi", 4)) {
            CHECK(avifParsePixelInformationProperty(data, avifROStreamCurrent(&s), header.size, propertyIndex));
        }

        CHECK(avifROStreamSkip(&s, header.size));
    }
    return AVIF_TRUE;
}

static avifBool avifParseItemPropertyAssociation(avifDecoderData * data, const uint8_t * raw, size_t rawLen)
{
    BEGIN_STREAM(s, raw, rawLen);

    uint8_t version;
    uint8_t flags[3];
    CHECK(avifROStreamReadVersionAndFlags(&s, &version, flags));
    avifBool propertyIndexIsU16 = ((flags[2] & 0x1) != 0);

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

            if (propertyIndex >= data->properties.count) {
                return AVIF_FALSE;
            }

            avifDecoderItem * item = avifDecoderDataFindItem(data, itemID);
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
            } else if (!memcmp(prop->type, "pasp", 4)) {
                item->paspPresent = AVIF_TRUE;
                memcpy(&item->pasp, &prop->pasp, sizeof(avifPixelAspectRatioBox));
            } else if (!memcmp(prop->type, "clap", 4)) {
                item->clapPresent = AVIF_TRUE;
                memcpy(&item->clap, &prop->clap, sizeof(avifCleanApertureBox));
            } else if (!memcmp(prop->type, "irot", 4)) {
                item->irotPresent = AVIF_TRUE;
                memcpy(&item->irot, &prop->irot, sizeof(avifImageRotation));
            } else if (!memcmp(prop->type, "imir", 4)) {
                item->imirPresent = AVIF_TRUE;
                memcpy(&item->imir, &prop->imir, sizeof(avifImageMirror));
            } else if (!memcmp(prop->type, "pixi", 4)) {
                item->pixiPresent = AVIF_TRUE;
                memcpy(&item->pixi, &prop->pixi, sizeof(avifPixelInformationProperty));
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

static avifBool avifParsePrimaryItemBox(avifDecoderData * data, const uint8_t * raw, size_t rawLen)
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

static avifBool avifParseItemDataBox(avifDecoderData * data, const uint8_t * raw, size_t rawLen)
{
    uint32_t idatID = data->metaBoxID;

    // Check to see if we've already seen an idat box for this meta box. If so, bail out
    for (uint32_t i = 0; i < data->idats.count; ++i) {
        if (data->idats.idat[i].id == idatID) {
            return AVIF_FALSE;
        }
    }

    int index = avifArrayPushIndex(&data->idats);
    avifDecoderItemData * idat = &data->idats.idat[index];
    idat->id = idatID;
    idat->data.data = raw;
    idat->data.size = rawLen;
    return AVIF_TRUE;
}

static avifBool avifParseItemPropertiesBox(avifDecoderData * data, const uint8_t * raw, size_t rawLen)
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

static avifBool avifParseItemInfoEntry(avifDecoderData * data, const uint8_t * raw, size_t rawLen)
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

    avifDecoderItem * item = avifDecoderDataFindItem(data, itemID);
    if (!item) {
        return AVIF_FALSE;
    }

    memcpy(item->type, itemType, sizeof(itemType));
    memcpy(&item->contentType, &contentType, sizeof(contentType));
    return AVIF_TRUE;
}

static avifBool avifParseItemInfoBox(avifDecoderData * data, const uint8_t * raw, size_t rawLen)
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

static avifBool avifParseItemReferenceBox(avifDecoderData * data, const uint8_t * raw, size_t rawLen)
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
                avifDecoderItem * item = avifDecoderDataFindItem(data, fromID);
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
                if (!memcmp(irefHeader.type, "dimg", 4)) {
                    // derived images refer in the opposite direction
                    avifDecoderItem * dimg = avifDecoderDataFindItem(data, toID);
                    if (!dimg) {
                        return AVIF_FALSE;
                    }

                    dimg->dimgForID = fromID;
                }
            }
        }
    }

    return AVIF_TRUE;
}

static avifBool avifParseMetaBox(avifDecoderData * data, const uint8_t * raw, size_t rawLen)
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

static avifBool avifParseTrackHeaderBox(avifDecoderData * data, avifTrack * track, const uint8_t * raw, size_t rawLen)
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

static avifBool avifParseMediaHeaderBox(avifDecoderData * data, avifTrack * track, const uint8_t * raw, size_t rawLen)
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

static avifBool avifParseChunkOffsetBox(avifDecoderData * data, avifSampleTable * sampleTable, avifBool largeOffsets, const uint8_t * raw, size_t rawLen)
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

static avifBool avifParseSampleToChunkBox(avifDecoderData * data, avifSampleTable * sampleTable, const uint8_t * raw, size_t rawLen)
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

static avifBool avifParseSampleSizeBox(avifDecoderData * data, avifSampleTable * sampleTable, const uint8_t * raw, size_t rawLen)
{
    BEGIN_STREAM(s, raw, rawLen);
    (void)data;

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

static avifBool avifParseSyncSampleBox(avifDecoderData * data, avifSampleTable * sampleTable, const uint8_t * raw, size_t rawLen)
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

static avifBool avifParseTimeToSampleBox(avifDecoderData * data, avifSampleTable * sampleTable, const uint8_t * raw, size_t rawLen)
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

static avifBool avifParseSampleDescriptionBox(avifDecoderData * data, avifSampleTable * sampleTable, const uint8_t * raw, size_t rawLen)
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

static avifBool avifParseSampleTableBox(avifDecoderData * data, avifTrack * track, const uint8_t * raw, size_t rawLen)
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

static avifBool avifParseMediaInformationBox(avifDecoderData * data, avifTrack * track, const uint8_t * raw, size_t rawLen)
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

static avifBool avifParseMediaBox(avifDecoderData * data, avifTrack * track, const uint8_t * raw, size_t rawLen)
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

static avifBool avifTrackReferenceBox(avifDecoderData * data, avifTrack * track, const uint8_t * raw, size_t rawLen)
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

static avifBool avifParseTrackBox(avifDecoderData * data, const uint8_t * raw, size_t rawLen)
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

static avifBool avifParse(avifDecoderData * data, const uint8_t * raw, size_t rawLen)
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
    avifBool avifCompatible = (memcmp(ftyp->majorBrand, "avif", 4) == 0);
    if (!avifCompatible) {
        avifCompatible = (memcmp(ftyp->majorBrand, "avis", 4) == 0);
    }
    if (!avifCompatible) {
        avifCompatible = (memcmp(ftyp->majorBrand, "av01", 4) == 0);
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

avifBool avifPeekCompatibleFileType(const avifROData * input)
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
        avifDecoderDataDestroy(decoder->data);
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

avifResult avifDecoderParse(avifDecoder * decoder, const avifROData * rawInput)
{
    // Cleanup anything lingering in the decoder
    avifDecoderCleanup(decoder);

    // -----------------------------------------------------------------------
    // Parse BMFF boxes

    decoder->data = avifDecoderDataCreate();

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
        avifDecoderItem * item = &decoder->data->items.item[itemIndex];
        if (item->hasUnsupportedEssentialProperty) {
            // An essential property isn't supported by libavif; ignore the item.
            continue;
        }
        const uint8_t * p = avifDecoderDataCalcItemPtr(decoder->data, item);
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
    avifDecoderDataResetCodec(decoder->data);

    for (unsigned int i = 0; i < decoder->data->tiles.count; ++i) {
        avifTile * tile = &decoder->data->tiles.tile[i];
        tile->codec = avifCodecCreateInternal(decoder->codecChoice, tile->input);
        if (!tile->codec) {
            return AVIF_RESULT_NO_CODEC_AVAILABLE;
        }
        if (!tile->codec->open(tile->codec, decoder->imageIndex + 1)) {
            return AVIF_RESULT_DECODE_COLOR_FAILED;
        }
    }
    return AVIF_RESULT_OK;
}

avifResult avifDecoderReset(avifDecoder * decoder)
{
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

        avifTile * colorTile = avifDecoderDataCreateTile(decoder->data);
        if (!avifCodecDecodeInputGetSamples(colorTile->input, colorTrack->sampleTable, &decoder->data->rawInput)) {
            return AVIF_RESULT_BMFF_PARSE_FAILED;
        }
        decoder->data->colorTileCount = 1;

        avifTile * alphaTile = NULL;
        if (alphaTrack) {
            alphaTile = avifDecoderDataCreateTile(decoder->data);
            if (!avifCodecDecodeInputGetSamples(alphaTile->input, alphaTrack->sampleTable, &decoder->data->rawInput)) {
                return AVIF_RESULT_BMFF_PARSE_FAILED;
            }
            alphaTile->input->alpha = AVIF_TRUE;
            decoder->data->alphaTileCount = 1;
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

        decoder->containerWidth = colorTrack->width;
        decoder->containerHeight = colorTrack->height;
        decoder->containerDepth = avifSampleTableGetDepth(colorTrack->sampleTable);
    } else {
        // Create from items

        avifROData colorOBU = AVIF_DATA_EMPTY;
        avifROData alphaOBU = AVIF_DATA_EMPTY;
        avifROData exifData = AVIF_DATA_EMPTY;
        avifROData xmpData = AVIF_DATA_EMPTY;
        avifDecoderItem * colorOBUItem = NULL;
        avifDecoderItem * alphaOBUItem = NULL;

        // Find the colorOBU (primary) item
        for (uint32_t itemIndex = 0; itemIndex < data->items.count; ++itemIndex) {
            avifDecoderItem * item = &data->items.item[itemIndex];
            if (!item->id || !item->size) {
                break;
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
            if ((data->primaryItemID > 0) && (item->id != data->primaryItemID)) {
                // a primary item ID was specified, require it
                continue;
            }

            if (isGrid) {
                const uint8_t * itemPtr = avifDecoderDataCalcItemPtr(data, item);
                if (itemPtr == NULL) {
                    return AVIF_RESULT_BMFF_PARSE_FAILED;
                }
                if (!avifParseImageGridBox(&data->colorGrid, itemPtr, item->size)) {
                    return AVIF_RESULT_INVALID_IMAGE_GRID;
                }
            } else {
                colorOBU.data = avifDecoderDataCalcItemPtr(data, item);
                colorOBU.size = item->size;
            }

            colorOBUItem = item;
            break;
        }

        if (!colorOBUItem) {
            return AVIF_RESULT_NO_AV1_ITEMS_FOUND;
        }

        // Find the alphaOBU item, if any
        for (uint32_t itemIndex = 0; itemIndex < data->items.count; ++itemIndex) {
            avifDecoderItem * item = &data->items.item[itemIndex];
            if (!item->id || !item->size) {
                break;
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

            if (isAlphaURN(item->auxC.auxType) && (item->auxForID == colorOBUItem->id)) {
                if (isGrid) {
                    const uint8_t * itemPtr = avifDecoderDataCalcItemPtr(data, item);
                    if (itemPtr == NULL) {
                        return AVIF_RESULT_BMFF_PARSE_FAILED;
                    }
                    if (!avifParseImageGridBox(&data->alphaGrid, itemPtr, item->size)) {
                        return AVIF_RESULT_INVALID_IMAGE_GRID;
                    }
                } else {
                    alphaOBU.data = avifDecoderDataCalcItemPtr(data, item);
                    alphaOBU.size = item->size;
                }

                alphaOBUItem = item;
                break;
            }
        }

        // Find Exif and/or XMP metadata, if any
        for (uint32_t itemIndex = 0; itemIndex < data->items.count; ++itemIndex) {
            avifDecoderItem * item = &data->items.item[itemIndex];
            if (!item->id || !item->size) {
                break;
            }
            if (item->hasUnsupportedEssentialProperty) {
                // An essential property isn't supported by libavif; ignore the item.
                continue;
            }

            if (item->descForID != colorOBUItem->id) {
                // Not a content description (metadata) for the colorOBU, skip it
                continue;
            }

            if (!memcmp(item->type, "Exif", 4)) {
                // Advance past Annex A.2.1's header
                const uint8_t * boxPtr = avifDecoderDataCalcItemPtr(data, item);
                BEGIN_STREAM(exifBoxStream, boxPtr, item->size);
                uint32_t exifTiffHeaderOffset;
                CHECK(avifROStreamReadU32(&exifBoxStream, &exifTiffHeaderOffset)); // unsigned int(32) exif_tiff_header_offset;

                exifData.data = avifROStreamCurrent(&exifBoxStream);
                exifData.size = avifROStreamRemainingBytes(&exifBoxStream);
            }

            if (!memcmp(item->type, "mime", 4) && !memcmp(item->contentType.contentType, xmpContentType, xmpContentTypeSize)) {
                xmpData.data = avifDecoderDataCalcItemPtr(data, item);
                xmpData.size = item->size;
            }
        }

        if ((data->colorGrid.rows > 0) && (data->colorGrid.columns > 0)) {
            if (!avifDecoderDataGenerateImageGridTiles(data, &data->colorGrid, colorOBUItem, AVIF_FALSE)) {
                return AVIF_RESULT_INVALID_IMAGE_GRID;
            }
            data->colorTileCount = data->tiles.count;
        } else {
            if (colorOBU.size == 0) {
                return AVIF_RESULT_NO_AV1_ITEMS_FOUND;
            }

            avifTile * colorTile = avifDecoderDataCreateTile(decoder->data);
            avifSample * colorSample = (avifSample *)avifArrayPushPtr(&colorTile->input->samples);
            memcpy(&colorSample->data, &colorOBU, sizeof(avifROData));
            colorSample->sync = AVIF_TRUE;
            decoder->data->colorTileCount = 1;
        }

        if ((data->alphaGrid.rows > 0) && (data->alphaGrid.columns > 0) && alphaOBUItem) {
            if (!avifDecoderDataGenerateImageGridTiles(data, &data->alphaGrid, alphaOBUItem, AVIF_FALSE)) {
                return AVIF_RESULT_INVALID_IMAGE_GRID;
            }
            data->alphaTileCount = data->tiles.count - data->colorTileCount;
        } else {
            avifTile * alphaTile = NULL;
            if (alphaOBU.size > 0) {
                alphaTile = avifDecoderDataCreateTile(decoder->data);

                avifSample * alphaSample = (avifSample *)avifArrayPushPtr(&alphaTile->input->samples);
                memcpy(&alphaSample->data, &alphaOBU, sizeof(avifROData));
                alphaSample->sync = AVIF_TRUE;
                alphaTile->input->alpha = AVIF_TRUE;
                decoder->data->alphaTileCount = 1;
            }
        }

        if (colorOBUItem->colrPresent) {
            if (colorOBUItem->colr.hasICC) {
                avifImageSetProfileICC(decoder->image, colorOBUItem->colr.icc, colorOBUItem->colr.iccSize);
            } else if (colorOBUItem->colr.hasNCLX) {
                data->cicpSet = AVIF_TRUE;
                decoder->image->colorPrimaries = colorOBUItem->colr.colorPrimaries;
                decoder->image->transferCharacteristics = colorOBUItem->colr.transferCharacteristics;
                decoder->image->matrixCoefficients = colorOBUItem->colr.matrixCoefficients;
                decoder->image->yuvRange = colorOBUItem->colr.range;
            }
        }

        // Transformations
        if (colorOBUItem->paspPresent) {
            decoder->image->transformFlags |= AVIF_TRANSFORM_PASP;
            memcpy(&decoder->image->pasp, &colorOBUItem->pasp, sizeof(avifPixelAspectRatioBox));
        }
        if (colorOBUItem->clapPresent) {
            decoder->image->transformFlags |= AVIF_TRANSFORM_CLAP;
            memcpy(&decoder->image->clap, &colorOBUItem->clap, sizeof(avifCleanApertureBox));
        }
        if (colorOBUItem->irotPresent) {
            decoder->image->transformFlags |= AVIF_TRANSFORM_IROT;
            memcpy(&decoder->image->irot, &colorOBUItem->irot, sizeof(avifImageRotation));
        }
        if (colorOBUItem->imirPresent) {
            decoder->image->transformFlags |= AVIF_TRANSFORM_IMIR;
            memcpy(&decoder->image->imir, &colorOBUItem->imir, sizeof(avifImageMirror));
        }

        if (exifData.data && exifData.size) {
            avifImageSetMetadataExif(decoder->image, exifData.data, exifData.size);
        }
        if (xmpData.data && xmpData.size) {
            avifImageSetMetadataXMP(decoder->image, xmpData.data, xmpData.size);
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
    for (unsigned int tileIndex = 0; tileIndex < decoder->data->tiles.count; ++tileIndex) {
        avifTile * tile = &decoder->data->tiles.tile[tileIndex];

        if (!tile->codec->getNextImage(tile->codec, tile->image)) {
            if (tile->input->alpha) {
                return AVIF_RESULT_DECODE_ALPHA_FAILED;
            } else {
                if (tile->image->width) {
                    // We've sent at least one image, but we've run out now.
                    return AVIF_RESULT_NO_IMAGES_REMAINING;
                }
                return AVIF_RESULT_DECODE_COLOR_FAILED;
            }
        }
    }

    if (decoder->data->tiles.count != (decoder->data->colorTileCount + decoder->data->alphaTileCount)) {
        // TODO: assert here? This should be impossible.
        return AVIF_RESULT_UNKNOWN_ERROR;
    }

    if ((decoder->data->colorGrid.rows > 0) || (decoder->data->colorGrid.columns > 0)) {
        if (!avifDecoderDataFillImageGrid(
                decoder->data, &decoder->data->colorGrid, decoder->image, 0, decoder->data->colorTileCount, AVIF_FALSE)) {
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

            if (!decoder->data->cicpSet) {
                decoder->data->cicpSet = AVIF_TRUE;
                decoder->image->colorPrimaries = srcColor->colorPrimaries;
                decoder->image->transferCharacteristics = srcColor->transferCharacteristics;
                decoder->image->matrixCoefficients = srcColor->matrixCoefficients;
            }
        }

        avifImageStealPlanes(decoder->image, srcColor, AVIF_PLANES_YUV);
    }

    if ((decoder->data->alphaGrid.rows > 0) || (decoder->data->alphaGrid.columns > 0)) {
        if (!avifDecoderDataFillImageGrid(
                decoder->data, &decoder->data->alphaGrid, decoder->image, decoder->data->colorTileCount, decoder->data->alphaTileCount, AVIF_TRUE)) {
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
        }
    }

    ++decoder->imageIndex;
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

    if ((int)frameIndex >= decoder->imageCount) {
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

avifBool avifDecoderIsKeyframe(const avifDecoder * decoder, uint32_t frameIndex)
{
    if ((decoder->data->tiles.count > 0) && decoder->data->tiles.tile[0].input) {
        if (frameIndex < decoder->data->tiles.tile[0].input->samples.count) {
            return decoder->data->tiles.tile[0].input->samples.sample[frameIndex].sync;
        }
    }
    return AVIF_FALSE;
}

uint32_t avifDecoderNearestKeyframe(const avifDecoder * decoder, uint32_t frameIndex)
{
    for (; frameIndex != 0; --frameIndex) {
        if (avifDecoderIsKeyframe(decoder, frameIndex)) {
            break;
        }
    }
    return frameIndex;
}

avifResult avifDecoderRead(avifDecoder * decoder, avifImage * image, const avifROData * input)
{
    avifResult result = avifDecoderParse(decoder, input);
    if (result != AVIF_RESULT_OK) {
        return result;
    }
    result = avifDecoderNextImage(decoder);
    if (result != AVIF_RESULT_OK) {
        return result;
    }
    avifImageCopy(image, decoder->image);
    return AVIF_RESULT_OK;
}
