// Copyright 2019 Joe Drago. All rights reserved.
// SPDX-License-Identifier: BSD-2-Clause

#include "avif/internal.h"

#include <assert.h>
#include <string.h>
#include <time.h>

#define MAX_ASSOCIATIONS 16
struct ipmaArray
{
    uint8_t associations[MAX_ASSOCIATIONS];
    avifBool essential[MAX_ASSOCIATIONS];
    uint8_t count;
};

// Used to store offsets in meta boxes which need to point at mdat offsets that
// aren't known yet. When an item's mdat payload is written, all registered fixups
// will have this now-known offset "fixed up".
typedef struct avifOffsetFixup
{
    size_t offset;
} avifOffsetFixup;
AVIF_ARRAY_DECLARE(avifOffsetFixupArray, avifOffsetFixup, fixup);

static const char alphaURN[] = AVIF_URN_ALPHA0;
static const size_t alphaURNSize = sizeof(alphaURN);

static const char xmpContentType[] = AVIF_CONTENT_TYPE_XMP;
static const size_t xmpContentTypeSize = sizeof(xmpContentType);

static avifResult writeConfigBox(avifRWStream * s, const avifCodecConfigurationBox * cfg, const char * configPropName);

// ---------------------------------------------------------------------------
// avifSetTileConfiguration

static int floorLog2(uint32_t n)
{
    assert(n > 0);
    int count = 0;
    while (n != 0) {
        ++count;
        n >>= 1;
    }
    return count - 1;
}

// Splits tilesLog2 into *tileDim1Log2 and *tileDim2Log2, considering the ratio of dim1 to dim2.
//
// Precondition:
//     dim1 >= dim2
// Postcondition:
//     tilesLog2 == *tileDim1Log2 + *tileDim2Log2
//     *tileDim1Log2 >= *tileDim2Log2
static void splitTilesLog2(uint32_t dim1, uint32_t dim2, int tilesLog2, int * tileDim1Log2, int * tileDim2Log2)
{
    assert(dim1 >= dim2);
    uint32_t ratio = dim1 / dim2;
    int diffLog2 = floorLog2(ratio);
    int subtract = tilesLog2 - diffLog2;
    if (subtract < 0) {
        subtract = 0;
    }
    *tileDim2Log2 = subtract / 2;
    *tileDim1Log2 = tilesLog2 - *tileDim2Log2;
    assert(*tileDim1Log2 >= *tileDim2Log2);
}

// Set the tile configuration: the number of tiles and the tile size.
//
// Tiles improve encoding and decoding speeds when multiple threads are available. However, for
// image coding, the total tile boundary length affects the compression efficiency because intra
// prediction can't go across tile boundaries. So the more tiles there are in an image, the worse
// the compression ratio is. For a given number of tiles, making the tile size close to a square
// tends to reduce the total tile boundary length inside the image. Use more tiles along the longer
// dimension of the image to make the tile size closer to a square.
void avifSetTileConfiguration(int threads, uint32_t width, uint32_t height, int * tileRowsLog2, int * tileColsLog2)
{
    *tileRowsLog2 = 0;
    *tileColsLog2 = 0;
    if (threads > 1) {
        // Avoid small tiles because they are particularly bad for image coding.
        //
        // Use no more tiles than the number of threads. Aim for one tile per thread. Using more
        // than one thread inside one tile could be less efficient. Using more tiles than the
        // number of threads would result in a compression penalty without much benefit.
        const uint32_t kMinTileArea = 512 * 512;
        const uint32_t kMaxTiles = 32;
        uint32_t imageArea = width * height;
        uint32_t tiles = (imageArea + kMinTileArea - 1) / kMinTileArea;
        if (tiles > kMaxTiles) {
            tiles = kMaxTiles;
        }
        if (tiles > (uint32_t)threads) {
            tiles = threads;
        }
        int tilesLog2 = floorLog2(tiles);
        // If the image's width is greater than the height, use more tile columns than tile rows.
        if (width >= height) {
            splitTilesLog2(width, height, tilesLog2, tileColsLog2, tileRowsLog2);
        } else {
            splitTilesLog2(height, width, tilesLog2, tileRowsLog2, tileColsLog2);
        }
    }
}

// ---------------------------------------------------------------------------
// avifCodecEncodeOutput

avifCodecEncodeOutput * avifCodecEncodeOutputCreate(void)
{
    avifCodecEncodeOutput * encodeOutput = (avifCodecEncodeOutput *)avifAlloc(sizeof(avifCodecEncodeOutput));
    memset(encodeOutput, 0, sizeof(avifCodecEncodeOutput));
    if (!avifArrayCreate(&encodeOutput->samples, sizeof(avifEncodeSample), 1)) {
        goto error;
    }
    return encodeOutput;

error:
    avifCodecEncodeOutputDestroy(encodeOutput);
    return NULL;
}

avifResult avifCodecEncodeOutputAddSample(avifCodecEncodeOutput * encodeOutput, const uint8_t * data, size_t len, avifBool sync)
{
    avifEncodeSample * sample = (avifEncodeSample *)avifArrayPushPtr(&encodeOutput->samples);
    AVIF_CHECKERR(sample, AVIF_RESULT_OUT_OF_MEMORY);
    const avifResult result = avifRWDataSet(&sample->data, data, len);
    if (result != AVIF_RESULT_OK) {
        avifArrayPop(&encodeOutput->samples);
        return result;
    }
    sample->sync = sync;
    return AVIF_RESULT_OK;
}

void avifCodecEncodeOutputDestroy(avifCodecEncodeOutput * encodeOutput)
{
    for (uint32_t sampleIndex = 0; sampleIndex < encodeOutput->samples.count; ++sampleIndex) {
        avifRWDataFree(&encodeOutput->samples.sample[sampleIndex].data);
    }
    avifArrayDestroy(&encodeOutput->samples);
    avifFree(encodeOutput);
}

// ---------------------------------------------------------------------------
// avifEncoderItem

// one "item" worth for encoder
typedef struct avifEncoderItem
{
    uint16_t id;
    uint8_t type[4];                      // 4-character 'item_type' field in the 'infe' (item info) box
    avifCodec * codec;                    // only present on image items
    avifCodecEncodeOutput * encodeOutput; // AV1 sample data
    avifRWData metadataPayload;           // Exif/XMP data
    avifCodecConfigurationBox av1C;       // Harvested in avifEncoderFinish(), if encodeOutput has samples
                                          // TODO(yguyon): Rename or add av2C
    uint32_t cellIndex;                   // Which row-major cell index corresponds to this item. only present on image items
    avifItemCategory itemCategory;        // Category of item being encoded
    avifBool hiddenImage;                 // A hidden image item has (flags & 1) equal to 1 in its ItemInfoEntry.

    const char * infeName;
    size_t infeNameSize;
    const char * infeContentType;
    size_t infeContentTypeSize;
    avifOffsetFixupArray mdatFixups;

    uint16_t irefToID; // if non-zero, make an iref from this id -> irefToID
    const char * irefType;

    uint32_t gridCols; // if non-zero (legal range [1-256]), this is a grid item
    uint32_t gridRows; // if non-zero (legal range [1-256]), this is a grid item

    // the reconstructed image of a grid item will be trimmed to these dimensions (only present on grid items)
    uint32_t gridWidth;
    uint32_t gridHeight;

    uint32_t extraLayerCount; // if non-zero (legal range [1-(AVIF_MAX_AV1_LAYER_COUNT-1)]), this is a layered AV1 image

    uint16_t dimgFromID; // if non-zero, make an iref from dimgFromID -> this id

    struct ipmaArray ipma;
} avifEncoderItem;
AVIF_ARRAY_DECLARE(avifEncoderItemArray, avifEncoderItem, item);

// ---------------------------------------------------------------------------
// avifEncoderItemReference

// pointer to one "item" interested in
typedef avifEncoderItem * avifEncoderItemReference;
AVIF_ARRAY_DECLARE(avifEncoderItemReferenceArray, avifEncoderItemReference, ref);

// ---------------------------------------------------------------------------
// avifEncoderFrame

typedef struct avifEncoderFrame
{
    uint64_t durationInTimescales;
} avifEncoderFrame;
AVIF_ARRAY_DECLARE(avifEncoderFrameArray, avifEncoderFrame, frame);

// ---------------------------------------------------------------------------
// avifEncoderData

typedef struct avifEncoderData
{
    avifEncoderItemArray items;
    avifEncoderFrameArray frames;
    // Map the encoder settings quality and qualityAlpha to quantizer and quantizerAlpha
    int quantizer;
    int quantizerAlpha;
    // tileRowsLog2 and tileColsLog2 are the actual tiling values after automatic tiling is handled
    int tileRowsLog2;
    int tileColsLog2;
    avifEncoder lastEncoder;
    // lastQuantizer and lastQuantizerAlpha are the quantizer and quantizerAlpha values used last
    // time
    int lastQuantizer;
    int lastQuantizerAlpha;
    // lastTileRowsLog2 and lastTileColsLog2 are the actual tiling values used last time
    int lastTileRowsLog2;
    int lastTileColsLog2;
    avifImage * imageMetadata;
    uint16_t lastItemID;
    uint16_t primaryItemID;
    avifBool singleImage; // if true, the AVIF_ADD_IMAGE_FLAG_SINGLE flag was set on the first call to avifEncoderAddImage()
    avifBool alphaPresent;
    // Fields specific to AV1/AV2
    const char * imageItemType;  // "av01" for AV1 ("av02" for AV2 if AVIF_CODEC_AVM)
    const char * configPropName; // "av1C" for AV1 ("av2C" for AV2 if AVIF_CODEC_AVM)
} avifEncoderData;

static void avifEncoderDataDestroy(avifEncoderData * data);

// Returns NULL if a memory allocation failed.
static avifEncoderData * avifEncoderDataCreate()
{
    avifEncoderData * data = (avifEncoderData *)avifAlloc(sizeof(avifEncoderData));
    if (!data) {
        return NULL;
    }
    memset(data, 0, sizeof(avifEncoderData));
    data->imageMetadata = avifImageCreateEmpty();
    if (!data->imageMetadata) {
        goto error;
    }
    if (!avifArrayCreate(&data->items, sizeof(avifEncoderItem), 8)) {
        goto error;
    }
    if (!avifArrayCreate(&data->frames, sizeof(avifEncoderFrame), 1)) {
        goto error;
    }
    return data;

error:
    avifEncoderDataDestroy(data);
    return NULL;
}

static avifEncoderItem * avifEncoderDataCreateItem(avifEncoderData * data, const char * type, const char * infeName, size_t infeNameSize, uint32_t cellIndex)
{
    avifEncoderItem * item = (avifEncoderItem *)avifArrayPushPtr(&data->items);
    ++data->lastItemID;
    item->id = data->lastItemID;
    memcpy(item->type, type, sizeof(item->type));
    item->infeName = infeName;
    item->infeNameSize = infeNameSize;
    item->encodeOutput = avifCodecEncodeOutputCreate();
    item->cellIndex = cellIndex;
    if (!avifArrayCreate(&item->mdatFixups, sizeof(avifOffsetFixup), 4)) {
        goto error;
    }
    return item;

error:
    avifCodecEncodeOutputDestroy(item->encodeOutput);
    --data->lastItemID;
    avifArrayPop(&data->items);
    return NULL;
}

static avifEncoderItem * avifEncoderDataFindItemByID(avifEncoderData * data, uint16_t id)
{
    for (uint32_t itemIndex = 0; itemIndex < data->items.count; ++itemIndex) {
        avifEncoderItem * item = &data->items.item[itemIndex];
        if (item->id == id) {
            return item;
        }
    }
    return NULL;
}

static void avifEncoderDataDestroy(avifEncoderData * data)
{
    for (uint32_t i = 0; i < data->items.count; ++i) {
        avifEncoderItem * item = &data->items.item[i];
        if (item->codec) {
            avifCodecDestroy(item->codec);
        }
        avifCodecEncodeOutputDestroy(item->encodeOutput);
        avifRWDataFree(&item->metadataPayload);
        avifArrayDestroy(&item->mdatFixups);
    }
    if (data->imageMetadata) {
        avifImageDestroy(data->imageMetadata);
    }
    avifArrayDestroy(&data->items);
    avifArrayDestroy(&data->frames);
    avifFree(data);
}

static void avifEncoderItemAddMdatFixup(avifEncoderItem * item, const avifRWStream * s)
{
    avifOffsetFixup * fixup = (avifOffsetFixup *)avifArrayPushPtr(&item->mdatFixups);
    fixup->offset = avifRWStreamOffset(s);
}

// ---------------------------------------------------------------------------
// avifItemPropertyDedup - Provides ipco deduplication

typedef struct avifItemProperty
{
    uint8_t index;
    size_t offset;
    size_t size;
} avifItemProperty;
AVIF_ARRAY_DECLARE(avifItemPropertyArray, avifItemProperty, property);

typedef struct avifItemPropertyDedup
{
    avifItemPropertyArray properties;
    avifRWStream s;    // Temporary stream for each new property, checked against already-written boxes for deduplications
    avifRWData buffer; // Temporary storage for 's'
    uint8_t nextIndex; // 1-indexed, incremented every time another unique property is finished
} avifItemPropertyDedup;

static void avifItemPropertyDedupDestroy(avifItemPropertyDedup * dedup);

static avifItemPropertyDedup * avifItemPropertyDedupCreate(void)
{
    avifItemPropertyDedup * dedup = (avifItemPropertyDedup *)avifAlloc(sizeof(avifItemPropertyDedup));
    memset(dedup, 0, sizeof(avifItemPropertyDedup));
    if (!avifArrayCreate(&dedup->properties, sizeof(avifItemProperty), 8)) {
        goto error;
    }
    if (avifRWDataRealloc(&dedup->buffer, 2048) != AVIF_RESULT_OK) {
        goto error;
    }
    return dedup;

error:
    avifItemPropertyDedupDestroy(dedup);
    return NULL;
}

static void avifItemPropertyDedupDestroy(avifItemPropertyDedup * dedup)
{
    avifArrayDestroy(&dedup->properties);
    avifRWDataFree(&dedup->buffer);
    avifFree(dedup);
}

// Resets the dedup's temporary write stream in preparation for a single item property's worth of writing
static void avifItemPropertyDedupStart(avifItemPropertyDedup * dedup)
{
    avifRWStreamStart(&dedup->s, &dedup->buffer);
}

// This compares the newly written item property (in the dedup's temporary storage buffer) to
// already-written properties (whose offsets/sizes in outputStream are recorded in the dedup). If a
// match is found, the previous property's index is used. If this new property is unique, it is
// assigned the next available property index, written to the output stream, and its offset/size in
// the output stream is recorded in the dedup for future comparisons.
//
// On success, this function adds to the given ipma box a property association linking the reused
// or newly created property with the item.
static avifResult avifItemPropertyDedupFinish(avifItemPropertyDedup * dedup, avifRWStream * outputStream, struct ipmaArray * ipma, avifBool essential)
{
    uint8_t propertyIndex = 0;
    const size_t newPropertySize = avifRWStreamOffset(&dedup->s);

    for (size_t i = 0; i < dedup->properties.count; ++i) {
        avifItemProperty * property = &dedup->properties.property[i];
        if ((property->size == newPropertySize) &&
            !memcmp(&outputStream->raw->data[property->offset], dedup->buffer.data, newPropertySize)) {
            // We've already written this exact property, reuse it
            propertyIndex = property->index;
            assert(propertyIndex != 0);
            break;
        }
    }

    if (propertyIndex == 0) {
        // Write a new property, and remember its location in the output stream for future deduplication
        avifItemProperty * property = (avifItemProperty *)avifArrayPushPtr(&dedup->properties);
        property->index = ++dedup->nextIndex; // preincrement so the first new index is 1 (as ipma is 1-indexed)
        property->size = newPropertySize;
        property->offset = avifRWStreamOffset(outputStream);
        AVIF_CHECKRES(avifRWStreamWrite(outputStream, dedup->buffer.data, newPropertySize));
        propertyIndex = property->index;
    }

    AVIF_CHECKERR(ipma->count < MAX_ASSOCIATIONS, AVIF_RESULT_UNKNOWN_ERROR);
    ipma->associations[ipma->count] = propertyIndex;
    ipma->essential[ipma->count] = essential;
    ++ipma->count;
    return AVIF_RESULT_OK;
}

// ---------------------------------------------------------------------------

static const avifScalingMode noScaling = { { 1, 1 }, { 1, 1 } };

avifEncoder * avifEncoderCreate(void)
{
    avifEncoder * encoder = (avifEncoder *)avifAlloc(sizeof(avifEncoder));
    if (!encoder) {
        return NULL;
    }
    memset(encoder, 0, sizeof(avifEncoder));
    encoder->codecChoice = AVIF_CODEC_CHOICE_AUTO;
    encoder->maxThreads = 1;
    encoder->speed = AVIF_SPEED_DEFAULT;
    encoder->keyframeInterval = 0;
    encoder->timescale = 1;
    encoder->repetitionCount = AVIF_REPETITION_COUNT_INFINITE;
    encoder->quality = AVIF_QUALITY_DEFAULT;
    encoder->qualityAlpha = AVIF_QUALITY_DEFAULT;
    encoder->minQuantizer = AVIF_QUANTIZER_BEST_QUALITY;
    encoder->maxQuantizer = AVIF_QUANTIZER_WORST_QUALITY;
    encoder->minQuantizerAlpha = AVIF_QUANTIZER_BEST_QUALITY;
    encoder->maxQuantizerAlpha = AVIF_QUANTIZER_WORST_QUALITY;
    encoder->tileRowsLog2 = 0;
    encoder->tileColsLog2 = 0;
    encoder->autoTiling = AVIF_FALSE;
    encoder->scalingMode = noScaling;
    encoder->data = avifEncoderDataCreate();
    encoder->csOptions = avifCodecSpecificOptionsCreate();
    if (!encoder->data || !encoder->csOptions) {
        avifEncoderDestroy(encoder);
        return NULL;
    }
    return encoder;
}

void avifEncoderDestroy(avifEncoder * encoder)
{
    if (encoder->csOptions) {
        avifCodecSpecificOptionsDestroy(encoder->csOptions);
    }
    if (encoder->data) {
        avifEncoderDataDestroy(encoder->data);
    }
    avifFree(encoder);
}

avifResult avifEncoderSetCodecSpecificOption(avifEncoder * encoder, const char * key, const char * value)
{
    return avifCodecSpecificOptionsSet(encoder->csOptions, key, value);
}

static void avifEncoderBackupSettings(avifEncoder * encoder)
{
    avifEncoder * lastEncoder = &encoder->data->lastEncoder;

    // lastEncoder->data is only used to mark that lastEncoder is initialized. lastEncoder->data
    // must not be dereferenced.
    lastEncoder->data = encoder->data;
    lastEncoder->codecChoice = encoder->codecChoice;
    lastEncoder->maxThreads = encoder->maxThreads;
    lastEncoder->speed = encoder->speed;
    lastEncoder->keyframeInterval = encoder->keyframeInterval;
    lastEncoder->timescale = encoder->timescale;
    lastEncoder->repetitionCount = encoder->repetitionCount;
    lastEncoder->extraLayerCount = encoder->extraLayerCount;
    lastEncoder->minQuantizer = encoder->minQuantizer;
    lastEncoder->maxQuantizer = encoder->maxQuantizer;
    lastEncoder->minQuantizerAlpha = encoder->minQuantizerAlpha;
    lastEncoder->maxQuantizerAlpha = encoder->maxQuantizerAlpha;
    encoder->data->lastQuantizer = encoder->data->quantizer;
    encoder->data->lastQuantizerAlpha = encoder->data->quantizerAlpha;
    encoder->data->lastTileRowsLog2 = encoder->data->tileRowsLog2;
    encoder->data->lastTileColsLog2 = encoder->data->tileColsLog2;
    lastEncoder->scalingMode = encoder->scalingMode;
}

// This function detects changes made on avifEncoder. It returns true on success (i.e., if every
// change is valid), or false on failure (i.e., if any setting that can't change was changed). It
// reports a bitwise-OR of detected changes in encoderChanges.
static avifBool avifEncoderDetectChanges(const avifEncoder * encoder, avifEncoderChanges * encoderChanges)
{
    const avifEncoder * lastEncoder = &encoder->data->lastEncoder;
    *encoderChanges = 0;

    if (!lastEncoder->data) {
        // lastEncoder is not initialized.
        return AVIF_TRUE;
    }

    if ((lastEncoder->codecChoice != encoder->codecChoice) || (lastEncoder->maxThreads != encoder->maxThreads) ||
        (lastEncoder->speed != encoder->speed) || (lastEncoder->keyframeInterval != encoder->keyframeInterval) ||
        (lastEncoder->timescale != encoder->timescale) || (lastEncoder->repetitionCount != encoder->repetitionCount) ||
        (lastEncoder->extraLayerCount != encoder->extraLayerCount)) {
        return AVIF_FALSE;
    }

    if (encoder->data->lastQuantizer != encoder->data->quantizer) {
        *encoderChanges |= AVIF_ENCODER_CHANGE_QUANTIZER;
    }
    if (encoder->data->lastQuantizerAlpha != encoder->data->quantizerAlpha) {
        *encoderChanges |= AVIF_ENCODER_CHANGE_QUANTIZER_ALPHA;
    }
    if (lastEncoder->minQuantizer != encoder->minQuantizer) {
        *encoderChanges |= AVIF_ENCODER_CHANGE_MIN_QUANTIZER;
    }
    if (lastEncoder->maxQuantizer != encoder->maxQuantizer) {
        *encoderChanges |= AVIF_ENCODER_CHANGE_MAX_QUANTIZER;
    }
    if (lastEncoder->minQuantizerAlpha != encoder->minQuantizerAlpha) {
        *encoderChanges |= AVIF_ENCODER_CHANGE_MIN_QUANTIZER_ALPHA;
    }
    if (lastEncoder->maxQuantizerAlpha != encoder->maxQuantizerAlpha) {
        *encoderChanges |= AVIF_ENCODER_CHANGE_MAX_QUANTIZER_ALPHA;
    }
    if (encoder->data->lastTileRowsLog2 != encoder->data->tileRowsLog2) {
        *encoderChanges |= AVIF_ENCODER_CHANGE_TILE_ROWS_LOG2;
    }
    if (encoder->data->lastTileColsLog2 != encoder->data->tileColsLog2) {
        *encoderChanges |= AVIF_ENCODER_CHANGE_TILE_COLS_LOG2;
    }
    if (memcmp(&lastEncoder->scalingMode, &encoder->scalingMode, sizeof(avifScalingMode)) != 0) {
        *encoderChanges |= AVIF_ENCODER_CHANGE_SCALING_MODE;
    }
    if (encoder->csOptions->count > 0) {
        *encoderChanges |= AVIF_ENCODER_CHANGE_CODEC_SPECIFIC;
    }

    return AVIF_TRUE;
}

// Subset of avifEncoderWriteColorProperties() for the properties clli, pasp, clap, irot, imir.
static avifResult avifEncoderWriteExtendedColorProperties(avifRWStream * dedupStream,
                                                          avifRWStream * outputStream,
                                                          const avifImage * imageMetadata,
                                                          struct ipmaArray * ipma,
                                                          avifItemPropertyDedup * dedup);

// This function is used in two codepaths:
// * writing color *item* properties
// * writing color *track* properties
//
// Item properties must have property associations with them and can be deduplicated (by reusing
// these associations), so this function leverages the ipma and dedup arguments to do this.
//
// Track properties, however, are implicitly associated by the track in which they are contained, so
// there is no need to build a property association box (ipma), and no way to deduplicate/reuse a
// property. In this case, the ipma and dedup properties should/will be set to NULL, and this
// function will avoid using them.
static avifResult avifEncoderWriteColorProperties(avifRWStream * outputStream,
                                                  const avifImage * imageMetadata,
                                                  struct ipmaArray * ipma,
                                                  avifItemPropertyDedup * dedup)
{
    // outputStream is the final bitstream that will be output by the libavif encoder API.
    // dedupStream is either equal to outputStream or to &dedup->s which is a temporary stream used
    // to store parts of the final bitstream; these parts may be discarded if they are a duplicate
    // of an already stored property.
    avifRWStream * dedupStream = outputStream;
    if (dedup) {
        assert(ipma);

        // Use the dedup's temporary stream for box writes.
        dedupStream = &dedup->s;
    }

    if (imageMetadata->icc.size > 0) {
        if (dedup) {
            avifItemPropertyDedupStart(dedup);
        }
        avifBoxMarker colr;
        AVIF_CHECKRES(avifRWStreamWriteBox(dedupStream, "colr", AVIF_BOX_SIZE_TBD, &colr));
        AVIF_CHECKRES(avifRWStreamWriteChars(dedupStream, "prof", 4)); // unsigned int(32) colour_type;
        AVIF_CHECKRES(avifRWStreamWrite(dedupStream, imageMetadata->icc.data, imageMetadata->icc.size));
        avifRWStreamFinishBox(dedupStream, colr);
        if (dedup) {
            AVIF_CHECKRES(avifItemPropertyDedupFinish(dedup, outputStream, ipma, AVIF_FALSE));
        }
    }

    // HEIF 6.5.5.1, from Amendment 3 allows multiple colr boxes: "at most one for a given value of colour type"
    // Therefore, *always* writing an nclx box, even if an a prof box was already written above.
    if (dedup) {
        avifItemPropertyDedupStart(dedup);
    }
    avifBoxMarker colr;
    AVIF_CHECKRES(avifRWStreamWriteBox(dedupStream, "colr", AVIF_BOX_SIZE_TBD, &colr));
    AVIF_CHECKRES(avifRWStreamWriteChars(dedupStream, "nclx", 4));                   // unsigned int(32) colour_type;
    AVIF_CHECKRES(avifRWStreamWriteU16(dedupStream, imageMetadata->colorPrimaries)); // unsigned int(16) colour_primaries;
    AVIF_CHECKRES(avifRWStreamWriteU16(dedupStream, imageMetadata->transferCharacteristics)); // unsigned int(16) transfer_characteristics;
    AVIF_CHECKRES(avifRWStreamWriteU16(dedupStream, imageMetadata->matrixCoefficients)); // unsigned int(16) matrix_coefficients;
    AVIF_CHECKRES(avifRWStreamWriteBits(dedupStream, (imageMetadata->yuvRange == AVIF_RANGE_FULL) ? 1 : 0, /*bitCount=*/1)); // unsigned int(1) full_range_flag;
    AVIF_CHECKRES(avifRWStreamWriteBits(dedupStream, 0, /*bitCount=*/7)); // unsigned int(7) reserved = 0;
    avifRWStreamFinishBox(dedupStream, colr);
    if (dedup) {
        AVIF_CHECKRES(avifItemPropertyDedupFinish(dedup, outputStream, ipma, AVIF_FALSE));
    }

    return avifEncoderWriteExtendedColorProperties(dedupStream, outputStream, imageMetadata, ipma, dedup);
}

static avifResult avifEncoderWriteExtendedColorProperties(avifRWStream * dedupStream,
                                                          avifRWStream * outputStream,
                                                          const avifImage * imageMetadata,
                                                          struct ipmaArray * ipma,
                                                          avifItemPropertyDedup * dedup)
{
    // Write Content Light Level Information, if present
    if (imageMetadata->clli.maxCLL || imageMetadata->clli.maxPALL) {
        if (dedup) {
            avifItemPropertyDedupStart(dedup);
        }
        avifBoxMarker clli;
        AVIF_CHECKRES(avifRWStreamWriteBox(dedupStream, "clli", AVIF_BOX_SIZE_TBD, &clli));
        AVIF_CHECKRES(avifRWStreamWriteU16(dedupStream, imageMetadata->clli.maxCLL)); // unsigned int(16) max_content_light_level;
        AVIF_CHECKRES(avifRWStreamWriteU16(dedupStream, imageMetadata->clli.maxPALL)); // unsigned int(16) max_pic_average_light_level;
        avifRWStreamFinishBox(dedupStream, clli);
        if (dedup) {
            AVIF_CHECKRES(avifItemPropertyDedupFinish(dedup, outputStream, ipma, AVIF_FALSE));
        }
    }

    // Write (Optional) Transformations
    if (imageMetadata->transformFlags & AVIF_TRANSFORM_PASP) {
        if (dedup) {
            avifItemPropertyDedupStart(dedup);
        }
        avifBoxMarker pasp;
        AVIF_CHECKRES(avifRWStreamWriteBox(dedupStream, "pasp", AVIF_BOX_SIZE_TBD, &pasp));
        AVIF_CHECKRES(avifRWStreamWriteU32(dedupStream, imageMetadata->pasp.hSpacing)); // unsigned int(32) hSpacing;
        AVIF_CHECKRES(avifRWStreamWriteU32(dedupStream, imageMetadata->pasp.vSpacing)); // unsigned int(32) vSpacing;
        avifRWStreamFinishBox(dedupStream, pasp);
        if (dedup) {
            AVIF_CHECKRES(avifItemPropertyDedupFinish(dedup, outputStream, ipma, AVIF_FALSE));
        }
    }
    if (imageMetadata->transformFlags & AVIF_TRANSFORM_CLAP) {
        if (dedup) {
            avifItemPropertyDedupStart(dedup);
        }
        avifBoxMarker clap;
        AVIF_CHECKRES(avifRWStreamWriteBox(dedupStream, "clap", AVIF_BOX_SIZE_TBD, &clap));
        AVIF_CHECKRES(avifRWStreamWriteU32(dedupStream, imageMetadata->clap.widthN));    // unsigned int(32) cleanApertureWidthN;
        AVIF_CHECKRES(avifRWStreamWriteU32(dedupStream, imageMetadata->clap.widthD));    // unsigned int(32) cleanApertureWidthD;
        AVIF_CHECKRES(avifRWStreamWriteU32(dedupStream, imageMetadata->clap.heightN));   // unsigned int(32) cleanApertureHeightN;
        AVIF_CHECKRES(avifRWStreamWriteU32(dedupStream, imageMetadata->clap.heightD));   // unsigned int(32) cleanApertureHeightD;
        AVIF_CHECKRES(avifRWStreamWriteU32(dedupStream, imageMetadata->clap.horizOffN)); // unsigned int(32) horizOffN;
        AVIF_CHECKRES(avifRWStreamWriteU32(dedupStream, imageMetadata->clap.horizOffD)); // unsigned int(32) horizOffD;
        AVIF_CHECKRES(avifRWStreamWriteU32(dedupStream, imageMetadata->clap.vertOffN));  // unsigned int(32) vertOffN;
        AVIF_CHECKRES(avifRWStreamWriteU32(dedupStream, imageMetadata->clap.vertOffD));  // unsigned int(32) vertOffD;
        avifRWStreamFinishBox(dedupStream, clap);
        if (dedup) {
            AVIF_CHECKRES(avifItemPropertyDedupFinish(dedup, outputStream, ipma, AVIF_TRUE));
        }
    }
    if (imageMetadata->transformFlags & AVIF_TRANSFORM_IROT) {
        if (dedup) {
            avifItemPropertyDedupStart(dedup);
        }
        avifBoxMarker irot;
        AVIF_CHECKRES(avifRWStreamWriteBox(dedupStream, "irot", AVIF_BOX_SIZE_TBD, &irot));
        AVIF_CHECKRES(avifRWStreamWriteBits(dedupStream, 0, /*bitCount=*/6)); // unsigned int (6) reserved = 0;
        AVIF_CHECKRES(avifRWStreamWriteBits(dedupStream, imageMetadata->irot.angle & 0x3, /*bitCount=*/2)); // unsigned int (2) angle;
        avifRWStreamFinishBox(dedupStream, irot);
        if (dedup) {
            AVIF_CHECKRES(avifItemPropertyDedupFinish(dedup, outputStream, ipma, AVIF_TRUE));
        }
    }
    if (imageMetadata->transformFlags & AVIF_TRANSFORM_IMIR) {
        if (dedup) {
            avifItemPropertyDedupStart(dedup);
        }
        avifBoxMarker imir;
        AVIF_CHECKRES(avifRWStreamWriteBox(dedupStream, "imir", AVIF_BOX_SIZE_TBD, &imir));
        AVIF_CHECKRES(avifRWStreamWriteBits(dedupStream, 0, /*bitCount=*/7)); // unsigned int(7) reserved = 0;
        AVIF_CHECKRES(avifRWStreamWriteBits(dedupStream, imageMetadata->imir.axis ? 1 : 0, /*bitCount=*/1)); // unsigned int(1) axis;
        avifRWStreamFinishBox(dedupStream, imir);
        if (dedup) {
            AVIF_CHECKRES(avifItemPropertyDedupFinish(dedup, outputStream, ipma, AVIF_TRUE));
        }
    }
    return AVIF_RESULT_OK;
}

// Write unassociated metadata items (EXIF, XMP) to a small meta box inside of a trak box.
// These items are implicitly associated with the track they are contained within.
static avifResult avifEncoderWriteTrackMetaBox(avifEncoder * encoder, avifRWStream * s)
{
    // Count how many non-image items (such as EXIF/XMP) are being written
    uint32_t metadataItemCount = 0;
    for (uint32_t itemIndex = 0; itemIndex < encoder->data->items.count; ++itemIndex) {
        avifEncoderItem * item = &encoder->data->items.item[itemIndex];
        if (memcmp(item->type, encoder->data->imageItemType, 4) != 0) {
            ++metadataItemCount;
        }
    }
    if (metadataItemCount == 0) {
        // Don't even bother writing the trak meta box
        return AVIF_RESULT_OK;
    }

    avifBoxMarker meta;
    AVIF_CHECKRES(avifRWStreamWriteFullBox(s, "meta", AVIF_BOX_SIZE_TBD, 0, 0, &meta));

    avifBoxMarker hdlr;
    AVIF_CHECKRES(avifRWStreamWriteFullBox(s, "hdlr", AVIF_BOX_SIZE_TBD, 0, 0, &hdlr));
    AVIF_CHECKRES(avifRWStreamWriteU32(s, 0));              // unsigned int(32) pre_defined = 0;
    AVIF_CHECKRES(avifRWStreamWriteChars(s, "pict", 4));    // unsigned int(32) handler_type;
    AVIF_CHECKRES(avifRWStreamWriteZeros(s, 12));           // const unsigned int(32)[3] reserved = 0;
    AVIF_CHECKRES(avifRWStreamWriteChars(s, "libavif", 8)); // string name; (writing null terminator)
    avifRWStreamFinishBox(s, hdlr);

    avifBoxMarker iloc;
    AVIF_CHECKRES(avifRWStreamWriteFullBox(s, "iloc", AVIF_BOX_SIZE_TBD, 0, 0, &iloc));
    AVIF_CHECKRES(avifRWStreamWriteBits(s, 4, /*bitCount=*/4));          // unsigned int(4) offset_size;
    AVIF_CHECKRES(avifRWStreamWriteBits(s, 4, /*bitCount=*/4));          // unsigned int(4) length_size;
    AVIF_CHECKRES(avifRWStreamWriteBits(s, 0, /*bitCount=*/4));          // unsigned int(4) base_offset_size;
    AVIF_CHECKRES(avifRWStreamWriteBits(s, 0, /*bitCount=*/4));          // unsigned int(4) reserved;
    AVIF_CHECKRES(avifRWStreamWriteU16(s, (uint16_t)metadataItemCount)); // unsigned int(16) item_count;
    for (uint32_t trakItemIndex = 0; trakItemIndex < encoder->data->items.count; ++trakItemIndex) {
        avifEncoderItem * item = &encoder->data->items.item[trakItemIndex];
        if (memcmp(item->type, encoder->data->imageItemType, 4) == 0) {
            // Skip over all non-metadata items
            continue;
        }

        AVIF_CHECKRES(avifRWStreamWriteU16(s, item->id));          // unsigned int(16) item_ID;
        AVIF_CHECKRES(avifRWStreamWriteU16(s, 0));                 // unsigned int(16) data_reference_index;
        AVIF_CHECKRES(avifRWStreamWriteU16(s, 1));                 // unsigned int(16) extent_count;
        avifEncoderItemAddMdatFixup(item, s);                      //
        AVIF_CHECKRES(avifRWStreamWriteU32(s, 0 /* set later */)); // unsigned int(offset_size*8) extent_offset;
        AVIF_CHECKRES(avifRWStreamWriteU32(s, (uint32_t)item->metadataPayload.size)); // unsigned int(length_size*8) extent_length;
    }
    avifRWStreamFinishBox(s, iloc);

    avifBoxMarker iinf;
    AVIF_CHECKRES(avifRWStreamWriteFullBox(s, "iinf", AVIF_BOX_SIZE_TBD, 0, 0, &iinf));
    AVIF_CHECKRES(avifRWStreamWriteU16(s, (uint16_t)metadataItemCount)); //  unsigned int(16) entry_count;
    for (uint32_t trakItemIndex = 0; trakItemIndex < encoder->data->items.count; ++trakItemIndex) {
        avifEncoderItem * item = &encoder->data->items.item[trakItemIndex];
        if (memcmp(item->type, encoder->data->imageItemType, 4) == 0) {
            continue;
        }

        assert(!item->hiddenImage);
        avifBoxMarker infe;
        AVIF_CHECKRES(avifRWStreamWriteFullBox(s, "infe", AVIF_BOX_SIZE_TBD, 2, 0, &infe));
        AVIF_CHECKRES(avifRWStreamWriteU16(s, item->id));                             // unsigned int(16) item_ID;
        AVIF_CHECKRES(avifRWStreamWriteU16(s, 0));                                    // unsigned int(16) item_protection_index;
        AVIF_CHECKRES(avifRWStreamWrite(s, item->type, 4));                           // unsigned int(32) item_type;
        AVIF_CHECKRES(avifRWStreamWriteChars(s, item->infeName, item->infeNameSize)); // string item_name; (writing null terminator)
        if (item->infeContentType && item->infeContentTypeSize) { // string content_type; (writing null terminator)
            AVIF_CHECKRES(avifRWStreamWriteChars(s, item->infeContentType, item->infeContentTypeSize));
        }
        avifRWStreamFinishBox(s, infe);
    }
    avifRWStreamFinishBox(s, iinf);

    avifRWStreamFinishBox(s, meta);
    return AVIF_RESULT_OK;
}

static avifResult avifWriteGridPayload(avifRWData * data, uint32_t gridCols, uint32_t gridRows, uint32_t gridWidth, uint32_t gridHeight)
{
    // ISO/IEC 23008-12 6.6.2.3.2
    // aligned(8) class ImageGrid {
    //     unsigned int(8) version = 0;
    //     unsigned int(8) flags;
    //     FieldLength = ((flags & 1) + 1) * 16;
    //     unsigned int(8) rows_minus_one;
    //     unsigned int(8) columns_minus_one;
    //     unsigned int(FieldLength) output_width;
    //     unsigned int(FieldLength) output_height;
    // }

    uint8_t gridFlags = ((gridWidth > 65535) || (gridHeight > 65535)) ? 1 : 0;

    avifRWStream s;
    avifRWStreamStart(&s, data);
    AVIF_CHECKRES(avifRWStreamWriteU8(&s, 0));                       // unsigned int(8) version = 0;
    AVIF_CHECKRES(avifRWStreamWriteU8(&s, gridFlags));               // unsigned int(8) flags;
    AVIF_CHECKRES(avifRWStreamWriteU8(&s, (uint8_t)(gridRows - 1))); // unsigned int(8) rows_minus_one;
    AVIF_CHECKRES(avifRWStreamWriteU8(&s, (uint8_t)(gridCols - 1))); // unsigned int(8) columns_minus_one;
    if (gridFlags & 1) {
        AVIF_CHECKRES(avifRWStreamWriteU32(&s, gridWidth));  // unsigned int(FieldLength) output_width;
        AVIF_CHECKRES(avifRWStreamWriteU32(&s, gridHeight)); // unsigned int(FieldLength) output_height;
    } else {
        uint16_t tmpWidth = (uint16_t)gridWidth;
        uint16_t tmpHeight = (uint16_t)gridHeight;
        AVIF_CHECKRES(avifRWStreamWriteU16(&s, tmpWidth));  // unsigned int(FieldLength) output_width;
        AVIF_CHECKRES(avifRWStreamWriteU16(&s, tmpHeight)); // unsigned int(FieldLength) output_height;
    }
    avifRWStreamFinishWrite(&s);
    return AVIF_RESULT_OK;
}

static avifResult avifEncoderDataCreateExifItem(avifEncoderData * data, const avifRWData * exif)
{
    size_t exifTiffHeaderOffset;
    const avifResult result = avifGetExifTiffHeaderOffset(exif->data, exif->size, &exifTiffHeaderOffset);
    if (result != AVIF_RESULT_OK) {
        // Couldn't find the TIFF header
        return result;
    }

    avifEncoderItem * exifItem = avifEncoderDataCreateItem(data, "Exif", "Exif", 5, 0);
    if (!exifItem) {
        return AVIF_RESULT_OUT_OF_MEMORY;
    }
    exifItem->irefToID = data->primaryItemID;
    exifItem->irefType = "cdsc";

    const uint32_t offset32bit = avifHTONL((uint32_t)exifTiffHeaderOffset);
    AVIF_CHECKRES(avifRWDataRealloc(&exifItem->metadataPayload, sizeof(offset32bit) + exif->size));
    memcpy(exifItem->metadataPayload.data, &offset32bit, sizeof(offset32bit));
    memcpy(exifItem->metadataPayload.data + sizeof(offset32bit), exif->data, exif->size);
    return AVIF_RESULT_OK;
}

static avifResult avifEncoderDataCreateXMPItem(avifEncoderData * data, const avifRWData * xmp)
{
    avifEncoderItem * xmpItem = avifEncoderDataCreateItem(data, "mime", "XMP", 4, 0);
    if (!xmpItem) {
        return AVIF_RESULT_OUT_OF_MEMORY;
    }
    xmpItem->irefToID = data->primaryItemID;
    xmpItem->irefType = "cdsc";

    xmpItem->infeContentType = xmpContentType;
    xmpItem->infeContentTypeSize = xmpContentTypeSize;
    AVIF_CHECKRES(avifRWDataSet(&xmpItem->metadataPayload, xmp->data, xmp->size));
    return AVIF_RESULT_OK;
}

// Same as avifImageCopy() but pads the dstImage with border pixel values to reach dstWidth and dstHeight.
// Returns NULL if a memory allocation failed.
static avifImage * avifImageCopyAndPad(const avifImage * srcImage, uint32_t dstWidth, uint32_t dstHeight)
{
    avifImage * dstImage = avifImageCreateEmpty();
    if (!dstImage) {
        return NULL;
    }
    // Copy all fields but do not allocate.
    if (avifImageCopy(dstImage, srcImage, (avifPlanesFlag)0) != AVIF_RESULT_OK) {
        avifImageDestroy(dstImage);
        return NULL;
    }
    assert(dstWidth >= srcImage->width);
    assert(dstHeight >= srcImage->height);
    dstImage->width = dstWidth;
    dstImage->height = dstHeight;

    if (srcImage->yuvPlanes[AVIF_CHAN_Y]) {
        const avifResult allocationResult = avifImageAllocatePlanes(dstImage, AVIF_PLANES_YUV);
        if (allocationResult != AVIF_RESULT_OK) {
            avifImageDestroy(dstImage);
            return NULL;
        }
    }
    if (srcImage->alphaPlane) {
        const avifResult allocationResult = avifImageAllocatePlanes(dstImage, AVIF_PLANES_A);
        if (allocationResult != AVIF_RESULT_OK) {
            avifImageDestroy(dstImage);
            return NULL;
        }
    }
    const avifBool usesU16 = avifImageUsesU16(srcImage);
    for (int plane = AVIF_CHAN_Y; plane <= AVIF_CHAN_A; ++plane) {
        const uint8_t * srcRow = avifImagePlane(srcImage, plane);
        const uint32_t srcRowBytes = avifImagePlaneRowBytes(srcImage, plane);
        const uint32_t srcPlaneWidth = avifImagePlaneWidth(srcImage, plane);
        const uint32_t srcPlaneHeight = avifImagePlaneHeight(srcImage, plane); // 0 for A if no alpha and 0 for UV if 4:0:0.
        const size_t srcPlaneWidthBytes = (size_t)srcPlaneWidth << usesU16;

        uint8_t * dstRow = avifImagePlane(dstImage, plane);
        const uint32_t dstRowBytes = avifImagePlaneRowBytes(dstImage, plane);
        const uint32_t dstPlaneWidth = avifImagePlaneWidth(dstImage, plane);
        const uint32_t dstPlaneHeight = avifImagePlaneHeight(dstImage, plane); // 0 for A if no alpha and 0 for UV if 4:0:0.
        const size_t dstPlaneWidthBytes = (size_t)dstPlaneWidth << usesU16;

        for (uint32_t j = 0; j < srcPlaneHeight; ++j) {
            memcpy(dstRow, srcRow, srcPlaneWidthBytes);

            // Pad columns.
            if (dstPlaneWidth > srcPlaneWidth) {
                if (usesU16) {
                    uint16_t * dstRow16 = (uint16_t *)dstRow;
                    for (uint32_t x = srcPlaneWidth; x < dstPlaneWidth; ++x) {
                        dstRow16[x] = dstRow16[srcPlaneWidth - 1];
                    }
                } else {
                    memset(&dstRow[srcPlaneWidth], dstRow[srcPlaneWidth - 1], dstPlaneWidth - srcPlaneWidth);
                }
            }
            srcRow += srcRowBytes;
            dstRow += dstRowBytes;
        }

        // Pad rows.
        for (uint32_t j = srcPlaneHeight; j < dstPlaneHeight; ++j) {
            memcpy(dstRow, dstRow - dstRowBytes, dstPlaneWidthBytes);
            dstRow += dstRowBytes;
        }
    }
    return dstImage;
}

static int avifQualityToQuantizer(int quality, int minQuantizer, int maxQuantizer)
{
    int quantizer;
    if (quality == AVIF_QUALITY_DEFAULT) {
        // In older libavif releases, avifEncoder didn't have the quality and qualityAlpha fields.
        // Supply a default value for quantizer.
        quantizer = (minQuantizer + maxQuantizer) / 2;
        quantizer = AVIF_CLAMP(quantizer, 0, 63);
    } else {
        quality = AVIF_CLAMP(quality, 0, 100);
        quantizer = ((100 - quality) * 63 + 50) / 100;
    }
    return quantizer;
}

// Adds the items for a single cell or a grid of cells. Outputs the topLevelItemID which is
// the only item if there is exactly one cell, or the grid item for multiple cells.
// Note: The topLevelItemID output argument has the type uint16_t* instead of avifEncoderItem** because
//       the avifEncoderItem pointer may be invalidated by a call to avifEncoderDataCreateItem().
static avifResult avifEncoderAddImageItems(avifEncoder * encoder,
                                           uint32_t gridCols,
                                           uint32_t gridRows,
                                           uint32_t gridWidth,
                                           uint32_t gridHeight,
                                           avifItemCategory itemCategory,
                                           uint16_t * topLevelItemID)
{
    const uint32_t cellCount = gridCols * gridRows;
    const char * infeName = (itemCategory == AVIF_ITEM_ALPHA) ? "Alpha" : "Color";
    const size_t infeNameSize = 6;

    if (cellCount > 1) {
        avifEncoderItem * gridItem = avifEncoderDataCreateItem(encoder->data, "grid", infeName, infeNameSize, 0);
        AVIF_CHECKRES(avifWriteGridPayload(&gridItem->metadataPayload, gridCols, gridRows, gridWidth, gridHeight));
        gridItem->itemCategory = itemCategory;
        gridItem->gridCols = gridCols;
        gridItem->gridRows = gridRows;
        gridItem->gridWidth = gridWidth;
        gridItem->gridHeight = gridHeight;
        *topLevelItemID = gridItem->id;
    }

    for (uint32_t cellIndex = 0; cellIndex < cellCount; ++cellIndex) {
        avifEncoderItem * item =
            avifEncoderDataCreateItem(encoder->data, encoder->data->imageItemType, infeName, infeNameSize, cellIndex);
        AVIF_CHECKERR(item, AVIF_RESULT_OUT_OF_MEMORY);
        item->codec = avifCodecCreate(encoder->codecChoice, AVIF_CODEC_FLAG_CAN_ENCODE);
        AVIF_CHECKERR(item->codec, AVIF_RESULT_NO_CODEC_AVAILABLE);
        item->codec->csOptions = encoder->csOptions;
        item->codec->diag = &encoder->diag;
        item->itemCategory = itemCategory;
        item->extraLayerCount = encoder->extraLayerCount;

        if (cellCount > 1) {
            item->dimgFromID = *topLevelItemID;
            item->hiddenImage = AVIF_TRUE;
        } else {
            *topLevelItemID = item->id;
        }
    }
    return AVIF_RESULT_OK;
}

static avifCodecType avifEncoderGetCodecType(const avifEncoder * encoder)
{
    // TODO(yguyon): Rework when AVIF_CODEC_CHOICE_AUTO can be AVM
    assert((encoder->codecChoice != AVIF_CODEC_CHOICE_AUTO) ||
           (strcmp(avifCodecName(encoder->codecChoice, AVIF_CODEC_FLAG_CAN_ENCODE), "avm") != 0));
    return avifCodecTypeFromChoice(encoder->codecChoice, AVIF_CODEC_FLAG_CAN_ENCODE);
}

// This function is called after every color frame is encoded. It returns AVIF_TRUE if a keyframe needs to be forced for the next
// alpha frame to be encoded, AVIF_FALSE otherwise.
static avifBool avifEncoderDataShouldForceKeyframeForAlpha(const avifEncoderData * data,
                                                           const avifEncoderItem * colorItem,
                                                           avifAddImageFlags addImageFlags)
{
    if (!data->alphaPresent) {
        // There is no alpha plane.
        return AVIF_FALSE;
    }
    if (addImageFlags & AVIF_ADD_IMAGE_FLAG_SINGLE) {
        // Not an animated image.
        return AVIF_FALSE;
    }
    if (data->frames.count == 0) {
        // data->frames.count is the number of frames that have been encoded so far by previous calls to avifEncoderAddImage. If
        // this is the first frame, there is no need to force keyframe.
        return AVIF_FALSE;
    }
    const uint32_t colorFramesOutputSoFar = colorItem->encodeOutput->samples.count;
    const avifBool isLaggedOutput = (data->frames.count + 1) != colorFramesOutputSoFar;
    if (isLaggedOutput) {
        // If the encoder is operating with lag, then there is no way to determine if the last encoded frame was a keyframe until
        // the encoder outputs it (after the lag). So do not force keyframe for alpha channel in this case.
        return AVIF_FALSE;
    }
    return colorItem->encodeOutput->samples.sample[colorFramesOutputSoFar - 1].sync;
}

static avifResult avifGetErrorForItemCategory(avifItemCategory itemCategory)
{
    return (itemCategory == AVIF_ITEM_ALPHA) ? AVIF_RESULT_ENCODE_ALPHA_FAILED : AVIF_RESULT_ENCODE_COLOR_FAILED;
}

static avifResult avifEncoderAddImageInternal(avifEncoder * encoder,
                                              uint32_t gridCols,
                                              uint32_t gridRows,
                                              const avifImage * const * cellImages,
                                              uint64_t durationInTimescales,
                                              avifAddImageFlags addImageFlags)
{
    // -----------------------------------------------------------------------
    // Verify encoding is possible

    if (!avifCodecName(encoder->codecChoice, AVIF_CODEC_FLAG_CAN_ENCODE)) {
        return AVIF_RESULT_NO_CODEC_AVAILABLE;
    }

    if (encoder->extraLayerCount >= AVIF_MAX_AV1_LAYER_COUNT) {
        avifDiagnosticsPrintf(&encoder->diag, "extraLayerCount [%u] must be less than %d", encoder->extraLayerCount, AVIF_MAX_AV1_LAYER_COUNT);
        return AVIF_RESULT_INVALID_ARGUMENT;
    }

    // -----------------------------------------------------------------------
    // Validate images

    const uint32_t cellCount = gridCols * gridRows;
    if (cellCount == 0) {
        return AVIF_RESULT_INVALID_ARGUMENT;
    }

    const avifImage * firstCell = cellImages[0];
    const avifImage * bottomRightCell = cellImages[cellCount - 1];
    if ((firstCell->depth != 8) && (firstCell->depth != 10) && (firstCell->depth != 12)) {
        return AVIF_RESULT_UNSUPPORTED_DEPTH;
    }
    if (!firstCell->width || !firstCell->height || !bottomRightCell->width || !bottomRightCell->height) {
        return AVIF_RESULT_NO_CONTENT;
    }

    // HEIF (ISO 23008-12:2017), Section 6.6.2.3.1:
    //   All input images shall have exactly the same width and height; call those tile_width and tile_height.
    // HEIF (ISO 23008-12:2017), Section 6.6.2.3.1:
    //   The reconstructed image is formed by tiling the input images into a grid with a column width
    //   (potentially excluding the right-most column) equal to tile_width and a row height (potentially
    //   excluding the bottom-most row) equal to tile_height, without gap or overlap, and then
    //   trimming on the right and the bottom to the indicated output_width and output_height.
    // Consider the combined input cellImages as the user's final output intent.
    // Right and bottom cells may be padded below so that all tiles are tileWidth by tileHeight,
    // and the output cropped to gridWidth by gridHeight.
    const uint32_t tileWidth = firstCell->width;
    const uint32_t tileHeight = firstCell->height;
    const uint32_t gridWidth = (gridCols - 1) * tileWidth + bottomRightCell->width;
    const uint32_t gridHeight = (gridRows - 1) * tileHeight + bottomRightCell->height;
    for (uint32_t cellIndex = 0; cellIndex < cellCount; ++cellIndex) {
        const avifImage * cellImage = cellImages[cellIndex];
        const uint32_t expectedCellWidth = ((cellIndex + 1) % gridCols) ? tileWidth : bottomRightCell->width;
        const uint32_t expectedCellHeight = (cellIndex < (cellCount - gridCols)) ? tileHeight : bottomRightCell->height;
        if ((cellImage->width != expectedCellWidth) || (cellImage->height != expectedCellHeight)) {
            return AVIF_RESULT_INVALID_IMAGE_GRID;
        }
    }

    if ((bottomRightCell->width > tileWidth) || (bottomRightCell->height > tileHeight)) {
        return AVIF_RESULT_INVALID_IMAGE_GRID;
    }
    if ((cellCount > 1) &&
        !avifAreGridDimensionsValid(firstCell->yuvFormat, gridWidth, gridHeight, tileWidth, tileHeight, &encoder->diag)) {
        return AVIF_RESULT_INVALID_IMAGE_GRID;
    }

    for (uint32_t cellIndex = 0; cellIndex < cellCount; ++cellIndex) {
        const avifImage * cellImage = cellImages[cellIndex];
        // MIAF (ISO 23000-22:2019), Section 7.3.11.4.1:
        //   All input images of a grid image item shall use the same coding format, chroma sampling format, and the
        //   same decoder configuration (see 7.3.6.2).
        if ((cellImage->depth != firstCell->depth) || (cellImage->yuvFormat != firstCell->yuvFormat) ||
            (cellImage->yuvRange != firstCell->yuvRange) || (cellImage->colorPrimaries != firstCell->colorPrimaries) ||
            (cellImage->transferCharacteristics != firstCell->transferCharacteristics) ||
            (cellImage->matrixCoefficients != firstCell->matrixCoefficients) || (!!cellImage->alphaPlane != !!firstCell->alphaPlane) ||
            (cellImage->alphaPremultiplied != firstCell->alphaPremultiplied)) {
            return AVIF_RESULT_INVALID_IMAGE_GRID;
        }

        if (!cellImage->yuvPlanes[AVIF_CHAN_Y]) {
            return AVIF_RESULT_NO_CONTENT;
        }

        if (cellImage->yuvFormat == AVIF_PIXEL_FORMAT_NONE) {
            return AVIF_RESULT_NO_YUV_FORMAT_SELECTED;
        }
    }

    // -----------------------------------------------------------------------
    // Validate flags

    if (encoder->data->singleImage) {
        // The previous call to avifEncoderAddImage() set AVIF_ADD_IMAGE_FLAG_SINGLE.
        // avifEncoderAddImage() cannot be called again for this encode.
        return AVIF_RESULT_ENCODE_COLOR_FAILED;
    }

    if (addImageFlags & AVIF_ADD_IMAGE_FLAG_SINGLE) {
        encoder->data->singleImage = AVIF_TRUE;

        if (encoder->extraLayerCount > 0) {
            // AVIF_ADD_IMAGE_FLAG_SINGLE may not be set for layered image.
            return AVIF_RESULT_INVALID_ARGUMENT;
        }

        if (encoder->data->items.count > 0) {
            // AVIF_ADD_IMAGE_FLAG_SINGLE may only be set on the first and only image.
            return AVIF_RESULT_INVALID_ARGUMENT;
        }
    }

    // -----------------------------------------------------------------------
    // Choose AV1 or AV2

    const avifCodecType codecType = avifEncoderGetCodecType(encoder);
    switch (codecType) {
        case AVIF_CODEC_TYPE_AV1:
            encoder->data->imageItemType = "av01";
            encoder->data->configPropName = "av1C";
            break;
#if defined(AVIF_CODEC_AVM)
        case AVIF_CODEC_TYPE_AV2:
            encoder->data->imageItemType = "av02";
            encoder->data->configPropName = "av2C";
            break;
#endif
        default:
            return AVIF_RESULT_NO_CODEC_AVAILABLE;
    }

    // -----------------------------------------------------------------------
    // Map quality and qualityAlpha to quantizer and quantizerAlpha
    encoder->data->quantizer = avifQualityToQuantizer(encoder->quality, encoder->minQuantizer, encoder->maxQuantizer);
    encoder->data->quantizerAlpha = avifQualityToQuantizer(encoder->qualityAlpha, encoder->minQuantizerAlpha, encoder->maxQuantizerAlpha);

    // -----------------------------------------------------------------------
    // Handle automatic tiling

    encoder->data->tileRowsLog2 = AVIF_CLAMP(encoder->tileRowsLog2, 0, 6);
    encoder->data->tileColsLog2 = AVIF_CLAMP(encoder->tileColsLog2, 0, 6);
    if (encoder->autoTiling) {
        // Use as many tiles as allowed by the minimum tile area requirement and impose a maximum
        // of 8 tiles.
        const int threads = 8;
        avifSetTileConfiguration(threads, tileWidth, tileHeight, &encoder->data->tileRowsLog2, &encoder->data->tileColsLog2);
    }

    // -----------------------------------------------------------------------
    // All encoder settings are known now. Detect changes.

    avifEncoderChanges encoderChanges;
    if (!avifEncoderDetectChanges(encoder, &encoderChanges)) {
        return AVIF_RESULT_CANNOT_CHANGE_SETTING;
    }
    avifEncoderBackupSettings(encoder);

    // -----------------------------------------------------------------------

    if (durationInTimescales == 0) {
        durationInTimescales = 1;
    }

    if (encoder->data->items.count == 0) {
        // Make a copy of the first image's metadata (sans pixels) for future writing/validation
        const avifResult copyResult = avifImageCopy(encoder->data->imageMetadata, firstCell, 0);
        if (copyResult != AVIF_RESULT_OK) {
            return copyResult;
        }

        // Prepare all AV1 items
        uint16_t colorItemID;
        AVIF_CHECKRES(avifEncoderAddImageItems(encoder, gridCols, gridRows, gridWidth, gridHeight, AVIF_ITEM_COLOR, &colorItemID));
        encoder->data->primaryItemID = colorItemID;

        encoder->data->alphaPresent = (firstCell->alphaPlane != NULL);
        if (encoder->data->alphaPresent && (addImageFlags & AVIF_ADD_IMAGE_FLAG_SINGLE)) {
            // If encoding a single image in which the alpha plane exists but is entirely opaque,
            // simply skip writing an alpha AV1 payload entirely, as it'll be interpreted as opaque
            // and is less bytes.
            //
            // However, if encoding an image sequence, the first frame's alpha plane being entirely
            // opaque could be a false positive for removing the alpha AV1 payload, as it might simply
            // be a fade out later in the sequence. This is why avifImageIsOpaque() is only called
            // when encoding a single image.

            encoder->data->alphaPresent = AVIF_FALSE;
            for (uint32_t cellIndex = 0; cellIndex < cellCount; ++cellIndex) {
                const avifImage * cellImage = cellImages[cellIndex];
                if (!avifImageIsOpaque(cellImage)) {
                    encoder->data->alphaPresent = AVIF_TRUE;
                    break;
                }
            }
        }

        if (encoder->data->alphaPresent) {
            uint16_t alphaItemID;
            AVIF_CHECKRES(avifEncoderAddImageItems(encoder, gridCols, gridRows, gridWidth, gridHeight, AVIF_ITEM_ALPHA, &alphaItemID));
            avifEncoderItem * alphaItem = avifEncoderDataFindItemByID(encoder->data, alphaItemID);
            assert(alphaItem);
            alphaItem->irefType = "auxl";
            alphaItem->irefToID = colorItemID;
            if (encoder->data->imageMetadata->alphaPremultiplied) {
                avifEncoderItem * colorItem = avifEncoderDataFindItemByID(encoder->data, colorItemID);
                assert(colorItem);
                colorItem->irefType = "prem";
                colorItem->irefToID = alphaItemID;
            }
        }

        // -----------------------------------------------------------------------
        // Create metadata items (Exif, XMP)

        if (firstCell->exif.size > 0) {
            const avifResult result = avifEncoderDataCreateExifItem(encoder->data, &firstCell->exif);
            if (result != AVIF_RESULT_OK) {
                return result;
            }
        }

        if (firstCell->xmp.size > 0) {
            const avifResult result = avifEncoderDataCreateXMPItem(encoder->data, &firstCell->xmp);
            if (result != AVIF_RESULT_OK) {
                return result;
            }
        }
    } else {
        // Another frame in an image sequence, or layer in a layered image

        const avifImage * imageMetadata = encoder->data->imageMetadata;
        // Image metadata that are copied to the configuration property and nclx boxes are not allowed to change.
        // If the first image in the sequence had an alpha plane (even if fully opaque), all
        // subsequent images must have alpha as well.
        if ((imageMetadata->depth != firstCell->depth) || (imageMetadata->yuvFormat != firstCell->yuvFormat) ||
            (imageMetadata->yuvRange != firstCell->yuvRange) ||
            (imageMetadata->yuvChromaSamplePosition != firstCell->yuvChromaSamplePosition) ||
            (imageMetadata->colorPrimaries != firstCell->colorPrimaries) ||
            (imageMetadata->transferCharacteristics != firstCell->transferCharacteristics) ||
            (imageMetadata->matrixCoefficients != firstCell->matrixCoefficients) ||
            (imageMetadata->alphaPremultiplied != firstCell->alphaPremultiplied) ||
            (encoder->data->alphaPresent && !firstCell->alphaPlane)) {
            return AVIF_RESULT_INCOMPATIBLE_IMAGE;
        }
    }

    if (encoder->data->frames.count == 1) {
        // We will be writing an image sequence. When writing the AV1SampleEntry (derived from
        // VisualSampleEntry) in the stsd box, we need to cast imageMetadata->width and
        // imageMetadata->height to uint16_t:
        //     class VisualSampleEntry(codingname) extends SampleEntry (codingname){
        //        ...
        //        unsigned int(16) width;
        //        unsigned int(16) height;
        //        ...
        //     }
        // Check whether it is safe to cast width and height to uint16_t. The maximum width and
        // height of an AV1 frame are 65536, which just exceeds uint16_t.
        assert(encoder->data->items.count > 0);
        const avifImage * imageMetadata = encoder->data->imageMetadata;
        if ((imageMetadata->width > 65535) || (imageMetadata->height > 65535)) {
            return AVIF_RESULT_INVALID_ARGUMENT;
        }
    }

    // -----------------------------------------------------------------------
    // Encode AV1 OBUs

    for (uint32_t itemIndex = 0; itemIndex < encoder->data->items.count; ++itemIndex) {
        avifEncoderItem * item = &encoder->data->items.item[itemIndex];
        if (item->codec) {
            const avifImage * cellImage = cellImages[item->cellIndex];
            avifImage * paddedCellImage = NULL;
            if ((cellImage->width != tileWidth) || (cellImage->height != tileHeight)) {
                paddedCellImage = avifImageCopyAndPad(cellImage, tileWidth, tileHeight);
                if (!paddedCellImage) {
                    return AVIF_RESULT_OUT_OF_MEMORY;
                }
                cellImage = paddedCellImage;
            }
            const int quantizer = (item->itemCategory == AVIF_ITEM_ALPHA) ? encoder->data->quantizerAlpha : encoder->data->quantizer;
            // If alpha channel is present, set disableLaggedOutput to AVIF_TRUE. If the encoder supports it, this enables
            // avifEncoderDataShouldForceKeyframeForAlpha to force a keyframe in the alpha channel whenever a keyframe has been
            // encoded in the color channel for animated images.
            avifResult encodeResult = item->codec->encodeImage(item->codec,
                                                               encoder,
                                                               cellImage,
                                                               item->itemCategory == AVIF_ITEM_ALPHA,
                                                               encoder->data->tileRowsLog2,
                                                               encoder->data->tileColsLog2,
                                                               quantizer,
                                                               encoderChanges,
                                                               /*disableLaggedOutput=*/encoder->data->alphaPresent,
                                                               addImageFlags,
                                                               item->encodeOutput);
            if (paddedCellImage) {
                avifImageDestroy(paddedCellImage);
            }
            if (encodeResult == AVIF_RESULT_UNKNOWN_ERROR) {
                encodeResult = avifGetErrorForItemCategory(item->itemCategory);
            }
            if (encodeResult != AVIF_RESULT_OK) {
                return encodeResult;
            }
            if (itemIndex == 0 && avifEncoderDataShouldForceKeyframeForAlpha(encoder->data, item, addImageFlags)) {
                addImageFlags |= AVIF_ADD_IMAGE_FLAG_FORCE_KEYFRAME;
            }
        }
    }

    avifCodecSpecificOptionsClear(encoder->csOptions);
    avifEncoderFrame * frame = (avifEncoderFrame *)avifArrayPushPtr(&encoder->data->frames);
    frame->durationInTimescales = durationInTimescales;
    return AVIF_RESULT_OK;
}

avifResult avifEncoderAddImage(avifEncoder * encoder, const avifImage * image, uint64_t durationInTimescales, avifAddImageFlags addImageFlags)
{
    avifDiagnosticsClearError(&encoder->diag);
    return avifEncoderAddImageInternal(encoder, 1, 1, &image, durationInTimescales, addImageFlags);
}

avifResult avifEncoderAddImageGrid(avifEncoder * encoder,
                                   uint32_t gridCols,
                                   uint32_t gridRows,
                                   const avifImage * const * cellImages,
                                   avifAddImageFlags addImageFlags)
{
    avifDiagnosticsClearError(&encoder->diag);
    if ((gridCols == 0) || (gridCols > 256) || (gridRows == 0) || (gridRows > 256)) {
        return AVIF_RESULT_INVALID_IMAGE_GRID;
    }
    if (encoder->extraLayerCount == 0) {
        addImageFlags |= AVIF_ADD_IMAGE_FLAG_SINGLE; // image grids cannot be image sequences
    }
    return avifEncoderAddImageInternal(encoder, gridCols, gridRows, cellImages, 1, addImageFlags);
}

static size_t avifEncoderFindExistingChunk(avifRWStream * s, size_t mdatStartOffset, const uint8_t * data, size_t size)
{
    const size_t mdatCurrentOffset = avifRWStreamOffset(s);
    const size_t mdatSearchSize = mdatCurrentOffset - mdatStartOffset;
    if (mdatSearchSize < size) {
        return 0;
    }
    const size_t mdatEndSearchOffset = mdatCurrentOffset - size;
    for (size_t searchOffset = mdatStartOffset; searchOffset <= mdatEndSearchOffset; ++searchOffset) {
        if (!memcmp(data, &s->raw->data[searchOffset], size)) {
            return searchOffset;
        }
    }
    return 0;
}

static avifResult avifEncoderWriteMediaDataBox(avifEncoder * encoder,
                                               avifRWStream * s,
                                               avifEncoderItemReferenceArray * layeredColorItems,
                                               avifEncoderItemReferenceArray * layeredAlphaItems)
{
    encoder->ioStats.colorOBUSize = 0;
    encoder->ioStats.alphaOBUSize = 0;

    avifBoxMarker mdat;
    AVIF_CHECKRES(avifRWStreamWriteBox(s, "mdat", AVIF_BOX_SIZE_TBD, &mdat));
    const size_t mdatStartOffset = avifRWStreamOffset(s);
    for (uint32_t itemPasses = 0; itemPasses < 3; ++itemPasses) {
        // Use multiple passes to pack in the following order:
        //   * Pass 0: metadata (Exif/XMP)
        //   * Pass 1: alpha (AV1)
        //   * Pass 2: all other item data (AV1 color)
        //
        // See here for the discussion on alpha coming before color:
        // https://github.com/AOMediaCodec/libavif/issues/287
        //
        // Exif and XMP are packed first as they're required to be fully available
        // by avifDecoderParse() before it returns AVIF_RESULT_OK, unless ignoreXMP
        // and ignoreExif are enabled.
        //
        const avifBool metadataPass = (itemPasses == 0);
        const avifBool alphaPass = (itemPasses == 1);

        for (uint32_t itemIndex = 0; itemIndex < encoder->data->items.count; ++itemIndex) {
            avifEncoderItem * item = &encoder->data->items.item[itemIndex];
            const avifBool isGrid = (item->gridCols > 0); // Grids store their payload in metadataPayload, so use this to distinguish grid payloads from XMP/Exif
            if ((item->metadataPayload.size == 0) && (item->encodeOutput->samples.count == 0)) {
                // this item has nothing for the mdat box
                continue;
            }
            if (!isGrid && (metadataPass != (item->metadataPayload.size > 0))) {
                // only process metadata (XMP/Exif) payloads when metadataPass is true
                continue;
            }
            if (alphaPass != (item->itemCategory == AVIF_ITEM_ALPHA)) {
                // only process alpha payloads when alphaPass is true
                continue;
            }

            if ((encoder->extraLayerCount > 0) && (item->encodeOutput->samples.count > 0)) {
                // Interleave - Pick out AV1 items and interleave them later.
                // We always interleave all AV1 items for layered images.
                assert(item->encodeOutput->samples.count == item->mdatFixups.count);

                avifEncoderItemReference * ref = (item->itemCategory == AVIF_ITEM_ALPHA) ? avifArrayPushPtr(layeredAlphaItems)
                                                                                         : avifArrayPushPtr(layeredColorItems);
                *ref = item;
                continue;
            }

            size_t chunkOffset = 0;

            // Deduplication - See if an identical chunk to this has already been written
            // Doing it when item->encodeOutput->samples.count > 0 would require contiguous memory.
            if (item->encodeOutput->samples.count == 1) {
                avifEncodeSample * sample = &item->encodeOutput->samples.sample[0];
                chunkOffset = avifEncoderFindExistingChunk(s, mdatStartOffset, sample->data.data, sample->data.size);
            } else if (item->encodeOutput->samples.count == 0) {
                chunkOffset = avifEncoderFindExistingChunk(s, mdatStartOffset, item->metadataPayload.data, item->metadataPayload.size);
            }

            if (!chunkOffset) {
                // We've never seen this chunk before; write it out
                chunkOffset = avifRWStreamOffset(s);
                if (item->encodeOutput->samples.count > 0) {
                    for (uint32_t sampleIndex = 0; sampleIndex < item->encodeOutput->samples.count; ++sampleIndex) {
                        avifEncodeSample * sample = &item->encodeOutput->samples.sample[sampleIndex];
                        AVIF_CHECKRES(avifRWStreamWrite(s, sample->data.data, sample->data.size));

                        if (item->itemCategory == AVIF_ITEM_ALPHA) {
                            encoder->ioStats.alphaOBUSize += sample->data.size;
                        } else {
                            encoder->ioStats.colorOBUSize += sample->data.size;
                        }
                    }
                } else {
                    AVIF_CHECKRES(avifRWStreamWrite(s, item->metadataPayload.data, item->metadataPayload.size));
                }
            }

            for (uint32_t fixupIndex = 0; fixupIndex < item->mdatFixups.count; ++fixupIndex) {
                avifOffsetFixup * fixup = &item->mdatFixups.fixup[fixupIndex];
                size_t prevOffset = avifRWStreamOffset(s);
                avifRWStreamSetOffset(s, fixup->offset);
                AVIF_CHECKRES(avifRWStreamWriteU32(s, (uint32_t)chunkOffset));
                avifRWStreamSetOffset(s, prevOffset);
            }
        }
    }

    uint32_t layeredItemCount = AVIF_MAX(layeredColorItems->count, layeredAlphaItems->count);
    if (layeredItemCount > 0) {
        // Interleave samples of all AV1 items.
        // We first write the first layer of all items,
        // in which we write first layer of each cell,
        // in which we write alpha first and then color.
        avifBool hasMoreSample;
        uint32_t layerIndex = 0;
        do {
            hasMoreSample = AVIF_FALSE;
            for (uint32_t itemIndex = 0; itemIndex < layeredItemCount; ++itemIndex) {
                for (int samplePass = 0; samplePass < 2; ++samplePass) {
                    // Alpha coming before color
                    avifEncoderItemReferenceArray * currentItems = (samplePass == 0) ? layeredAlphaItems : layeredColorItems;
                    if (itemIndex >= currentItems->count) {
                        continue;
                    }

                    // TODO: Offer the ability for a user to specify which grid cell should be written first.
                    avifEncoderItem * item = currentItems->ref[itemIndex];
                    if (item->encodeOutput->samples.count <= layerIndex) {
                        // We've already written all samples of this item
                        continue;
                    } else if (item->encodeOutput->samples.count > layerIndex + 1) {
                        hasMoreSample = AVIF_TRUE;
                    }
                    avifRWData * data = &item->encodeOutput->samples.sample[layerIndex].data;
                    size_t chunkOffset = avifEncoderFindExistingChunk(s, mdatStartOffset, data->data, data->size);
                    if (!chunkOffset) {
                        // We've never seen this chunk before; write it out
                        chunkOffset = avifRWStreamOffset(s);
                        AVIF_CHECKRES(avifRWStreamWrite(s, data->data, data->size));
                        if (samplePass == 0) {
                            encoder->ioStats.alphaOBUSize += data->size;
                        } else {
                            encoder->ioStats.colorOBUSize += data->size;
                        }
                    }

                    size_t prevOffset = avifRWStreamOffset(s);
                    avifRWStreamSetOffset(s, item->mdatFixups.fixup[layerIndex].offset);
                    AVIF_CHECKRES(avifRWStreamWriteU32(s, (uint32_t)chunkOffset));
                    avifRWStreamSetOffset(s, prevOffset);
                }
            }
            ++layerIndex;
        } while (hasMoreSample);

        assert(layerIndex <= AVIF_MAX_AV1_LAYER_COUNT);
    }
    avifRWStreamFinishBox(s, mdat);
    return AVIF_RESULT_OK;
}

avifResult avifEncoderFinish(avifEncoder * encoder, avifRWData * output)
{
    avifDiagnosticsClearError(&encoder->diag);
    if (encoder->data->items.count == 0) {
        return AVIF_RESULT_NO_CONTENT;
    }

    const avifCodecType codecType = avifEncoderGetCodecType(encoder);
    if (codecType == AVIF_CODEC_TYPE_UNKNOWN) {
        return AVIF_RESULT_NO_CODEC_AVAILABLE;
    }

    // -----------------------------------------------------------------------
    // Finish up encoding

    for (uint32_t itemIndex = 0; itemIndex < encoder->data->items.count; ++itemIndex) {
        avifEncoderItem * item = &encoder->data->items.item[itemIndex];
        if (item->codec) {
            if (!item->codec->encodeFinish(item->codec, item->encodeOutput)) {
                return avifGetErrorForItemCategory(item->itemCategory);
            }

            if (item->encodeOutput->samples.count != encoder->data->frames.count) {
                return avifGetErrorForItemCategory(item->itemCategory);
            }

            if ((item->extraLayerCount > 0) && (item->encodeOutput->samples.count != item->extraLayerCount + 1)) {
                // Check whether user has sent enough frames to encoder.
                avifDiagnosticsPrintf(&encoder->diag,
                                      "Expected %u frames given to avifEncoderAddImage() to encode this layered image according to extraLayerCount, but got %u frames.",
                                      item->extraLayerCount + 1,
                                      item->encodeOutput->samples.count);
                return AVIF_RESULT_INVALID_ARGUMENT;
            }
        }
    }

    // -----------------------------------------------------------------------
    // Harvest configuration properties from sequence headers

    for (uint32_t itemIndex = 0; itemIndex < encoder->data->items.count; ++itemIndex) {
        avifEncoderItem * item = &encoder->data->items.item[itemIndex];
        if (item->encodeOutput->samples.count > 0) {
            const avifEncodeSample * firstSample = &item->encodeOutput->samples.sample[0];
            avifSequenceHeader sequenceHeader;
            AVIF_CHECKERR(avifSequenceHeaderParse(&sequenceHeader, (const avifROData *)&firstSample->data, codecType),
                          avifGetErrorForItemCategory(item->itemCategory));
            item->av1C = sequenceHeader.av1C;
        }
    }

    // -----------------------------------------------------------------------
    // Begin write stream

    const avifImage * imageMetadata = encoder->data->imageMetadata;
    // The epoch for creation_time and modification_time is midnight, Jan. 1,
    // 1904, in UTC time. Add the number of seconds between that epoch and the
    // Unix epoch.
    uint64_t now = (uint64_t)time(NULL) + 2082844800;

    avifRWStream s;
    avifRWStreamStart(&s, output);

    // -----------------------------------------------------------------------
    // Write ftyp

    // Layered sequence is not supported for now.
    const avifBool isSequence = (encoder->extraLayerCount == 0) && (encoder->data->frames.count > 1);

    const char * majorBrand = "avif";
    if (isSequence) {
        majorBrand = "avis";
    }

    uint32_t minorVersion = 0;
#if defined(AVIF_CODEC_AVM)
    if (codecType == AVIF_CODEC_TYPE_AV2) {
        // TODO(yguyon): Experimental AV2-AVIF is AVIF version 2 for now (change once it is ratified).
        minorVersion = 2;
    }
#endif

    avifBoxMarker ftyp;
    AVIF_CHECKRES(avifRWStreamWriteBox(&s, "ftyp", AVIF_BOX_SIZE_TBD, &ftyp));
    AVIF_CHECKRES(avifRWStreamWriteChars(&s, majorBrand, 4));              // unsigned int(32) major_brand;
    AVIF_CHECKRES(avifRWStreamWriteU32(&s, minorVersion));                 // unsigned int(32) minor_version;
    AVIF_CHECKRES(avifRWStreamWriteChars(&s, "avif", 4));                  // unsigned int(32) compatible_brands[];
    if (isSequence) {                                                      //
        AVIF_CHECKRES(avifRWStreamWriteChars(&s, "avis", 4));              // ... compatible_brands[]
        AVIF_CHECKRES(avifRWStreamWriteChars(&s, "msf1", 4));              // ... compatible_brands[]
        AVIF_CHECKRES(avifRWStreamWriteChars(&s, "iso8", 4));              // ... compatible_brands[]
    }                                                                      //
    AVIF_CHECKRES(avifRWStreamWriteChars(&s, "mif1", 4));                  // ... compatible_brands[]
    AVIF_CHECKRES(avifRWStreamWriteChars(&s, "miaf", 4));                  // ... compatible_brands[]
    if ((imageMetadata->depth == 8) || (imageMetadata->depth == 10)) {     //
        if (imageMetadata->yuvFormat == AVIF_PIXEL_FORMAT_YUV420) {        //
            AVIF_CHECKRES(avifRWStreamWriteChars(&s, "MA1B", 4));          // ... compatible_brands[]
        } else if (imageMetadata->yuvFormat == AVIF_PIXEL_FORMAT_YUV444) { //
            AVIF_CHECKRES(avifRWStreamWriteChars(&s, "MA1A", 4));          // ... compatible_brands[]
        }
    }
    avifRWStreamFinishBox(&s, ftyp);

    // -----------------------------------------------------------------------
    // Start meta

    avifBoxMarker meta;
    AVIF_CHECKRES(avifRWStreamWriteFullBox(&s, "meta", AVIF_BOX_SIZE_TBD, 0, 0, &meta));

    // -----------------------------------------------------------------------
    // Write hdlr

    avifBoxMarker hdlr;
    AVIF_CHECKRES(avifRWStreamWriteFullBox(&s, "hdlr", AVIF_BOX_SIZE_TBD, 0, 0, &hdlr));
    AVIF_CHECKRES(avifRWStreamWriteU32(&s, 0));              // unsigned int(32) pre_defined = 0;
    AVIF_CHECKRES(avifRWStreamWriteChars(&s, "pict", 4));    // unsigned int(32) handler_type;
    AVIF_CHECKRES(avifRWStreamWriteZeros(&s, 12));           // const unsigned int(32)[3] reserved = 0;
    AVIF_CHECKRES(avifRWStreamWriteChars(&s, "libavif", 8)); // string name; (writing null terminator)
    avifRWStreamFinishBox(&s, hdlr);

    // -----------------------------------------------------------------------
    // Write pitm

    if (encoder->data->primaryItemID != 0) {
        AVIF_CHECKRES(avifRWStreamWriteFullBox(&s, "pitm", sizeof(uint16_t), 0, 0, /*marker=*/NULL));
        AVIF_CHECKRES(avifRWStreamWriteU16(&s, encoder->data->primaryItemID)); //  unsigned int(16) item_ID;
    }

    // -----------------------------------------------------------------------
    // Write iloc

    avifBoxMarker iloc;
    AVIF_CHECKRES(avifRWStreamWriteFullBox(&s, "iloc", AVIF_BOX_SIZE_TBD, 0, 0, &iloc));
    AVIF_CHECKRES(avifRWStreamWriteBits(&s, 4, /*bitCount=*/4));    // unsigned int(4) offset_size;
    AVIF_CHECKRES(avifRWStreamWriteBits(&s, 4, /*bitCount=*/4));    // unsigned int(4) length_size;
    AVIF_CHECKRES(avifRWStreamWriteBits(&s, 0, /*bitCount=*/4));    // unsigned int(4) base_offset_size;
    AVIF_CHECKRES(avifRWStreamWriteBits(&s, 0, /*bitCount=*/4));    // unsigned int(4) reserved;
    avifRWStreamWriteU16(&s, (uint16_t)encoder->data->items.count); // unsigned int(16) item_count;

    for (uint32_t itemIndex = 0; itemIndex < encoder->data->items.count; ++itemIndex) {
        avifEncoderItem * item = &encoder->data->items.item[itemIndex];
        AVIF_CHECKRES(avifRWStreamWriteU16(&s, item->id)); // unsigned int(16) item_ID;
        AVIF_CHECKRES(avifRWStreamWriteU16(&s, 0));        // unsigned int(16) data_reference_index;

        // Layered Image, write location for all samples
        if (item->extraLayerCount > 0) {
            uint32_t layerCount = item->extraLayerCount + 1;
            AVIF_CHECKRES(avifRWStreamWriteU16(&s, (uint16_t)layerCount)); // unsigned int(16) extent_count;
            for (uint32_t i = 0; i < layerCount; ++i) {
                avifEncoderItemAddMdatFixup(item, &s);
                AVIF_CHECKRES(avifRWStreamWriteU32(&s, 0 /* set later */)); // unsigned int(offset_size*8) extent_offset;
                AVIF_CHECKRES(avifRWStreamWriteU32(&s, (uint32_t)item->encodeOutput->samples.sample[i].data.size)); // unsigned int(length_size*8) extent_length;
            }
            continue;
        }

        uint32_t contentSize = (uint32_t)item->metadataPayload.size;
        if (item->encodeOutput->samples.count > 0) {
            // This is choosing sample 0's size as there are two cases here:
            // * This is a single image, in which case this is correct
            // * This is an image sequence, but this file should still be a valid single-image avif,
            //   so there must still be a primary item pointing at a sync sample. Since the first
            //   frame of the image sequence is guaranteed to be a sync sample, it is chosen here.
            //
            // TODO: Offer the ability for a user to specify which frame in the sequence should
            //       become the primary item's image, and force that frame to be a keyframe.
            contentSize = (uint32_t)item->encodeOutput->samples.sample[0].data.size;
        }

        AVIF_CHECKRES(avifRWStreamWriteU16(&s, 1));                     // unsigned int(16) extent_count;
        avifEncoderItemAddMdatFixup(item, &s);                          //
        AVIF_CHECKRES(avifRWStreamWriteU32(&s, 0 /* set later */));     // unsigned int(offset_size*8) extent_offset;
        AVIF_CHECKRES(avifRWStreamWriteU32(&s, (uint32_t)contentSize)); // unsigned int(length_size*8) extent_length;
    }

    avifRWStreamFinishBox(&s, iloc);

    // -----------------------------------------------------------------------
    // Write iinf

    avifBoxMarker iinf;
    AVIF_CHECKRES(avifRWStreamWriteFullBox(&s, "iinf", AVIF_BOX_SIZE_TBD, 0, 0, &iinf));
    AVIF_CHECKRES(avifRWStreamWriteU16(&s, (uint16_t)encoder->data->items.count)); //  unsigned int(16) entry_count;

    for (uint32_t itemIndex = 0; itemIndex < encoder->data->items.count; ++itemIndex) {
        avifEncoderItem * item = &encoder->data->items.item[itemIndex];

        uint32_t flags = item->hiddenImage ? 1 : 0;
        avifBoxMarker infe;
        AVIF_CHECKRES(avifRWStreamWriteFullBox(&s, "infe", AVIF_BOX_SIZE_TBD, 2, flags, &infe));
        AVIF_CHECKRES(avifRWStreamWriteU16(&s, item->id));                             // unsigned int(16) item_ID;
        AVIF_CHECKRES(avifRWStreamWriteU16(&s, 0));                                    // unsigned int(16) item_protection_index;
        AVIF_CHECKRES(avifRWStreamWrite(&s, item->type, 4));                           // unsigned int(32) item_type;
        AVIF_CHECKRES(avifRWStreamWriteChars(&s, item->infeName, item->infeNameSize)); // string item_name; (writing null terminator)
        if (item->infeContentType && item->infeContentTypeSize) { // string content_type; (writing null terminator)
            AVIF_CHECKRES(avifRWStreamWriteChars(&s, item->infeContentType, item->infeContentTypeSize));
        }
        avifRWStreamFinishBox(&s, infe);
    }

    avifRWStreamFinishBox(&s, iinf);

    // -----------------------------------------------------------------------
    // Write iref boxes

    avifBoxMarker iref = 0;
    for (uint32_t itemIndex = 0; itemIndex < encoder->data->items.count; ++itemIndex) {
        avifEncoderItem * item = &encoder->data->items.item[itemIndex];

        // Count how many other items refer to this item with dimgFromID
        uint16_t dimgCount = 0;
        for (uint32_t dimgIndex = 0; dimgIndex < encoder->data->items.count; ++dimgIndex) {
            avifEncoderItem * dimgItem = &encoder->data->items.item[dimgIndex];
            if (dimgItem->dimgFromID == item->id) {
                ++dimgCount;
            }
        }

        if (dimgCount > 0) {
            if (!iref) {
                AVIF_CHECKRES(avifRWStreamWriteFullBox(&s, "iref", AVIF_BOX_SIZE_TBD, 0, 0, &iref));
            }
            avifBoxMarker refType;
            AVIF_CHECKRES(avifRWStreamWriteBox(&s, "dimg", AVIF_BOX_SIZE_TBD, &refType));
            AVIF_CHECKRES(avifRWStreamWriteU16(&s, item->id));  // unsigned int(16) from_item_ID;
            AVIF_CHECKRES(avifRWStreamWriteU16(&s, dimgCount)); // unsigned int(16) reference_count;
            for (uint32_t dimgIndex = 0; dimgIndex < encoder->data->items.count; ++dimgIndex) {
                avifEncoderItem * dimgItem = &encoder->data->items.item[dimgIndex];
                if (dimgItem->dimgFromID == item->id) {
                    AVIF_CHECKRES(avifRWStreamWriteU16(&s, dimgItem->id)); // unsigned int(16) to_item_ID;
                }
            }
            avifRWStreamFinishBox(&s, refType);
        }

        if (item->irefToID != 0) {
            if (!iref) {
                AVIF_CHECKRES(avifRWStreamWriteFullBox(&s, "iref", AVIF_BOX_SIZE_TBD, 0, 0, &iref));
            }
            avifBoxMarker refType;
            AVIF_CHECKRES(avifRWStreamWriteBox(&s, item->irefType, AVIF_BOX_SIZE_TBD, &refType));
            AVIF_CHECKRES(avifRWStreamWriteU16(&s, item->id));       // unsigned int(16) from_item_ID;
            AVIF_CHECKRES(avifRWStreamWriteU16(&s, 1));              // unsigned int(16) reference_count;
            AVIF_CHECKRES(avifRWStreamWriteU16(&s, item->irefToID)); // unsigned int(16) to_item_ID;
            avifRWStreamFinishBox(&s, refType);
        }
    }
    if (iref) {
        avifRWStreamFinishBox(&s, iref);
    }

    // -----------------------------------------------------------------------
    // Write iprp -> ipco/ipma

    avifBoxMarker iprp;
    AVIF_CHECKRES(avifRWStreamWriteBox(&s, "iprp", AVIF_BOX_SIZE_TBD, &iprp));

    avifItemPropertyDedup * dedup = avifItemPropertyDedupCreate();
    avifBoxMarker ipco;
    AVIF_CHECKRES(avifRWStreamWriteBox(&s, "ipco", AVIF_BOX_SIZE_TBD, &ipco));
    for (uint32_t itemIndex = 0; itemIndex < encoder->data->items.count; ++itemIndex) {
        avifEncoderItem * item = &encoder->data->items.item[itemIndex];
        const avifBool isGrid = (item->gridCols > 0);
        memset(&item->ipma, 0, sizeof(item->ipma));
        if (!item->codec && !isGrid) {
            // No ipma to write for this item
            continue;
        }

        if (item->dimgFromID && (item->extraLayerCount == 0)) {
            // All image cells from a grid should share the exact same properties unless they are
            // layered image which have different al1x, so see if we've already written properties
            // out for another cell in this grid, and if so, just steal their ipma and move on.
            // This is a sneaky way to provide iprp deduplication.

            avifBool foundPreviousCell = AVIF_FALSE;
            for (uint32_t dedupIndex = 0; dedupIndex < itemIndex; ++dedupIndex) {
                avifEncoderItem * dedupItem = &encoder->data->items.item[dedupIndex];
                if ((item->dimgFromID == dedupItem->dimgFromID) && (dedupItem->extraLayerCount == 0)) {
                    // We've already written dedup's items out. Steal their ipma indices and move on!
                    item->ipma = dedupItem->ipma;
                    foundPreviousCell = AVIF_TRUE;
                    break;
                }
            }
            if (foundPreviousCell) {
                continue;
            }
        }

        uint32_t imageWidth = imageMetadata->width;
        uint32_t imageHeight = imageMetadata->height;
        if (isGrid) {
            imageWidth = item->gridWidth;
            imageHeight = item->gridHeight;
        }

        // Properties all image items need

        avifItemPropertyDedupStart(dedup);
        avifBoxMarker ispe;
        AVIF_CHECKRES(avifRWStreamWriteFullBox(&dedup->s, "ispe", AVIF_BOX_SIZE_TBD, 0, 0, &ispe));
        AVIF_CHECKRES(avifRWStreamWriteU32(&dedup->s, imageWidth));  // unsigned int(32) image_width;
        AVIF_CHECKRES(avifRWStreamWriteU32(&dedup->s, imageHeight)); // unsigned int(32) image_height;
        avifRWStreamFinishBox(&dedup->s, ispe);
        AVIF_CHECKRES(avifItemPropertyDedupFinish(dedup, &s, &item->ipma, AVIF_FALSE));

        avifItemPropertyDedupStart(dedup);
        uint8_t channelCount =
            ((item->itemCategory == AVIF_ITEM_ALPHA) || (imageMetadata->yuvFormat == AVIF_PIXEL_FORMAT_YUV400)) ? 1 : 3;
        avifBoxMarker pixi;
        AVIF_CHECKRES(avifRWStreamWriteFullBox(&dedup->s, "pixi", AVIF_BOX_SIZE_TBD, 0, 0, &pixi));
        AVIF_CHECKRES(avifRWStreamWriteU8(&dedup->s, channelCount)); // unsigned int (8) num_channels;
        for (uint8_t chan = 0; chan < channelCount; ++chan) {
            AVIF_CHECKRES(avifRWStreamWriteU8(&dedup->s, (uint8_t)imageMetadata->depth)); // unsigned int (8) bits_per_channel;
        }
        avifRWStreamFinishBox(&dedup->s, pixi);
        AVIF_CHECKRES(avifItemPropertyDedupFinish(dedup, &s, &item->ipma, AVIF_FALSE));

        if (item->codec) {
            avifItemPropertyDedupStart(dedup);
            AVIF_CHECKRES(writeConfigBox(&dedup->s, &item->av1C, encoder->data->configPropName));
            AVIF_CHECKRES(avifItemPropertyDedupFinish(dedup, &s, &item->ipma, AVIF_TRUE));
        }

        if (item->itemCategory == AVIF_ITEM_ALPHA) {
            // Alpha specific properties

            avifItemPropertyDedupStart(dedup);
            avifBoxMarker auxC;
            AVIF_CHECKRES(avifRWStreamWriteFullBox(&dedup->s, "auxC", AVIF_BOX_SIZE_TBD, 0, 0, &auxC));
            AVIF_CHECKRES(avifRWStreamWriteChars(&dedup->s, alphaURN, alphaURNSize)); //  string aux_type;
            avifRWStreamFinishBox(&dedup->s, auxC);
            AVIF_CHECKRES(avifItemPropertyDedupFinish(dedup, &s, &item->ipma, AVIF_FALSE));
        } else {
            // Color specific properties

            AVIF_CHECKRES(avifEncoderWriteColorProperties(&s, imageMetadata, &item->ipma, dedup));
        }

        if (item->extraLayerCount > 0) {
            // Layered Image Indexing Property

            avifItemPropertyDedupStart(dedup);
            avifBoxMarker a1lx;
            AVIF_CHECKRES(avifRWStreamWriteBox(&dedup->s, "a1lx", AVIF_BOX_SIZE_TBD, &a1lx));
            uint32_t layerSize[AVIF_MAX_AV1_LAYER_COUNT - 1] = { 0 };
            avifBool largeSize = AVIF_FALSE;

            for (uint32_t validLayer = 0; validLayer < item->extraLayerCount; ++validLayer) {
                uint32_t size = (uint32_t)item->encodeOutput->samples.sample[validLayer].data.size;
                layerSize[validLayer] = size;
                if (size > 0xffff) {
                    largeSize = AVIF_TRUE;
                }
            }

            AVIF_CHECKRES(avifRWStreamWriteBits(&dedup->s, 0, /*bitCount=*/7));                 // unsigned int(7) reserved = 0;
            AVIF_CHECKRES(avifRWStreamWriteBits(&dedup->s, largeSize ? 1 : 0, /*bitCount=*/1)); // unsigned int(1) large_size;

            // FieldLength = (large_size + 1) * 16;
            // unsigned int(FieldLength) layer_size[3];
            for (uint32_t layer = 0; layer < AVIF_MAX_AV1_LAYER_COUNT - 1; ++layer) {
                if (largeSize) {
                    AVIF_CHECKRES(avifRWStreamWriteU32(&dedup->s, layerSize[layer]));
                } else {
                    AVIF_CHECKRES(avifRWStreamWriteU16(&dedup->s, (uint16_t)layerSize[layer]));
                }
            }
            avifRWStreamFinishBox(&dedup->s, a1lx);
            AVIF_CHECKRES(avifItemPropertyDedupFinish(dedup, &s, &item->ipma, AVIF_FALSE));
        }
    }
    avifRWStreamFinishBox(&s, ipco);
    avifItemPropertyDedupDestroy(dedup);
    dedup = NULL;

    avifBoxMarker ipma;
    AVIF_CHECKRES(avifRWStreamWriteFullBox(&s, "ipma", AVIF_BOX_SIZE_TBD, 0, 0, &ipma));
    {
        int ipmaCount = 0;
        for (uint32_t itemIndex = 0; itemIndex < encoder->data->items.count; ++itemIndex) {
            avifEncoderItem * item = &encoder->data->items.item[itemIndex];
            if (item->ipma.count > 0) {
                ++ipmaCount;
            }
        }
        AVIF_CHECKRES(avifRWStreamWriteU32(&s, ipmaCount)); // unsigned int(32) entry_count;

        for (uint32_t itemIndex = 0; itemIndex < encoder->data->items.count; ++itemIndex) {
            avifEncoderItem * item = &encoder->data->items.item[itemIndex];
            if (item->ipma.count == 0) {
                continue;
            }

            AVIF_CHECKRES(avifRWStreamWriteU16(&s, item->id));        // unsigned int(16) item_ID;
            AVIF_CHECKRES(avifRWStreamWriteU8(&s, item->ipma.count)); // unsigned int(8) association_count;
            for (int i = 0; i < item->ipma.count; ++i) {
                AVIF_CHECKRES(avifRWStreamWriteBits(&s, item->ipma.essential[i] ? 1 : 0, /*bitCount=*/1)); // bit(1) essential;
                AVIF_CHECKRES(avifRWStreamWriteBits(&s, item->ipma.associations[i], /*bitCount=*/7)); // unsigned int(7) property_index;
            }
        }
    }
    avifRWStreamFinishBox(&s, ipma);

    avifRWStreamFinishBox(&s, iprp);

    // -----------------------------------------------------------------------
    // Finish meta box

    avifRWStreamFinishBox(&s, meta);

    // -----------------------------------------------------------------------
    // Write tracks (if an image sequence)

    if (isSequence) {
        static const uint8_t unityMatrix[9][4] = {
            /* clang-format off */
            { 0x00, 0x01, 0x00, 0x00 },
            { 0 },
            { 0 },
            { 0 },
            { 0x00, 0x01, 0x00, 0x00 },
            { 0 },
            { 0 },
            { 0 },
            { 0x40, 0x00, 0x00, 0x00 }
            /* clang-format on */
        };

        if (encoder->repetitionCount < 0 && encoder->repetitionCount != AVIF_REPETITION_COUNT_INFINITE) {
            return AVIF_RESULT_INVALID_ARGUMENT;
        }

        uint64_t framesDurationInTimescales = 0;
        for (uint32_t frameIndex = 0; frameIndex < encoder->data->frames.count; ++frameIndex) {
            const avifEncoderFrame * frame = &encoder->data->frames.frame[frameIndex];
            framesDurationInTimescales += frame->durationInTimescales;
        }
        uint64_t durationInTimescales;
        if (encoder->repetitionCount == AVIF_REPETITION_COUNT_INFINITE) {
            durationInTimescales = AVIF_INDEFINITE_DURATION64;
        } else {
            uint64_t loopCount = encoder->repetitionCount + 1;
            assert(framesDurationInTimescales != 0);
            if (loopCount > UINT64_MAX / framesDurationInTimescales) {
                // The multiplication will overflow uint64_t.
                return AVIF_RESULT_INVALID_ARGUMENT;
            }
            durationInTimescales = framesDurationInTimescales * loopCount;
        }

        // -------------------------------------------------------------------
        // Start moov

        avifBoxMarker moov;
        AVIF_CHECKRES(avifRWStreamWriteBox(&s, "moov", AVIF_BOX_SIZE_TBD, &moov));

        avifBoxMarker mvhd;
        AVIF_CHECKRES(avifRWStreamWriteFullBox(&s, "mvhd", AVIF_BOX_SIZE_TBD, 1, 0, &mvhd));
        AVIF_CHECKRES(avifRWStreamWriteU64(&s, now));                          // unsigned int(64) creation_time;
        AVIF_CHECKRES(avifRWStreamWriteU64(&s, now));                          // unsigned int(64) modification_time;
        AVIF_CHECKRES(avifRWStreamWriteU32(&s, (uint32_t)encoder->timescale)); // unsigned int(32) timescale;
        AVIF_CHECKRES(avifRWStreamWriteU64(&s, durationInTimescales));         // unsigned int(64) duration;
        AVIF_CHECKRES(avifRWStreamWriteU32(&s, 0x00010000)); // template int(32) rate = 0x00010000; // typically 1.0
        AVIF_CHECKRES(avifRWStreamWriteU16(&s, 0x0100));     // template int(16) volume = 0x0100; // typically, full volume
        AVIF_CHECKRES(avifRWStreamWriteU16(&s, 0));          // const bit(16) reserved = 0;
        AVIF_CHECKRES(avifRWStreamWriteZeros(&s, 8));        // const unsigned int(32)[2] reserved = 0;
        AVIF_CHECKRES(avifRWStreamWrite(&s, unityMatrix, sizeof(unityMatrix)));
        AVIF_CHECKRES(avifRWStreamWriteZeros(&s, 24));                       // bit(32)[6] pre_defined = 0;
        AVIF_CHECKRES(avifRWStreamWriteU32(&s, encoder->data->items.count)); // unsigned int(32) next_track_ID;
        avifRWStreamFinishBox(&s, mvhd);

        // -------------------------------------------------------------------
        // Write tracks

        for (uint32_t itemIndex = 0; itemIndex < encoder->data->items.count; ++itemIndex) {
            avifEncoderItem * item = &encoder->data->items.item[itemIndex];
            if (item->encodeOutput->samples.count == 0) {
                continue;
            }

            uint32_t syncSamplesCount = 0;
            for (uint32_t sampleIndex = 0; sampleIndex < item->encodeOutput->samples.count; ++sampleIndex) {
                avifEncodeSample * sample = &item->encodeOutput->samples.sample[sampleIndex];
                if (sample->sync) {
                    ++syncSamplesCount;
                }
            }

            avifBoxMarker trak;
            AVIF_CHECKRES(avifRWStreamWriteBox(&s, "trak", AVIF_BOX_SIZE_TBD, &trak));

            avifBoxMarker tkhd;
            AVIF_CHECKRES(avifRWStreamWriteFullBox(&s, "tkhd", AVIF_BOX_SIZE_TBD, 1, 1, &tkhd));
            AVIF_CHECKRES(avifRWStreamWriteU64(&s, now));                    // unsigned int(64) creation_time;
            AVIF_CHECKRES(avifRWStreamWriteU64(&s, now));                    // unsigned int(64) modification_time;
            AVIF_CHECKRES(avifRWStreamWriteU32(&s, itemIndex + 1));          // unsigned int(32) track_ID;
            AVIF_CHECKRES(avifRWStreamWriteU32(&s, 0));                      // const unsigned int(32) reserved = 0;
            AVIF_CHECKRES(avifRWStreamWriteU64(&s, durationInTimescales));   // unsigned int(64) duration;
            AVIF_CHECKRES(avifRWStreamWriteZeros(&s, sizeof(uint32_t) * 2)); // const unsigned int(32)[2] reserved = 0;
            AVIF_CHECKRES(avifRWStreamWriteU16(&s, 0));                      // template int(16) layer = 0;
            AVIF_CHECKRES(avifRWStreamWriteU16(&s, 0));                      // template int(16) alternate_group = 0;
            AVIF_CHECKRES(avifRWStreamWriteU16(&s, 0)); // template int(16) volume = {if track_is_audio 0x0100 else 0};
            AVIF_CHECKRES(avifRWStreamWriteU16(&s, 0)); // const unsigned int(16) reserved = 0;
            AVIF_CHECKRES(avifRWStreamWrite(&s, unityMatrix, sizeof(unityMatrix))); // template int(32)[9] matrix= // { 0x00010000,0,0,0,0x00010000,0,0,0,0x40000000 };
            AVIF_CHECKRES(avifRWStreamWriteU32(&s, imageMetadata->width << 16));  // unsigned int(32) width;
            AVIF_CHECKRES(avifRWStreamWriteU32(&s, imageMetadata->height << 16)); // unsigned int(32) height;
            avifRWStreamFinishBox(&s, tkhd);

            if (item->irefToID != 0) {
                avifBoxMarker tref;
                AVIF_CHECKRES(avifRWStreamWriteBox(&s, "tref", AVIF_BOX_SIZE_TBD, &tref));
                avifBoxMarker refType;
                AVIF_CHECKRES(avifRWStreamWriteBox(&s, item->irefType, AVIF_BOX_SIZE_TBD, &refType));
                AVIF_CHECKRES(avifRWStreamWriteU32(&s, (uint32_t)item->irefToID));
                avifRWStreamFinishBox(&s, refType);
                avifRWStreamFinishBox(&s, tref);
            }

            avifBoxMarker edts;
            AVIF_CHECKRES(avifRWStreamWriteBox(&s, "edts", AVIF_BOX_SIZE_TBD, &edts));
            uint32_t elstFlags = (encoder->repetitionCount != 0);
            avifBoxMarker elst;
            AVIF_CHECKRES(avifRWStreamWriteFullBox(&s, "elst", AVIF_BOX_SIZE_TBD, 1, elstFlags, &elst));
            AVIF_CHECKRES(avifRWStreamWriteU32(&s, 1));                          // unsigned int(32) entry_count;
            AVIF_CHECKRES(avifRWStreamWriteU64(&s, framesDurationInTimescales)); // unsigned int(64) segment_duration;
            AVIF_CHECKRES(avifRWStreamWriteU64(&s, 0));                          // int(64) media_time;
            AVIF_CHECKRES(avifRWStreamWriteU16(&s, 1));                          // int(16) media_rate_integer;
            AVIF_CHECKRES(avifRWStreamWriteU16(&s, 0));                          // int(16) media_rate_fraction = 0;
            avifRWStreamFinishBox(&s, elst);
            avifRWStreamFinishBox(&s, edts);

            if (item->itemCategory != AVIF_ITEM_ALPHA) {
                AVIF_CHECKRES(avifEncoderWriteTrackMetaBox(encoder, &s));
            }

            avifBoxMarker mdia;
            AVIF_CHECKRES(avifRWStreamWriteBox(&s, "mdia", AVIF_BOX_SIZE_TBD, &mdia));

            avifBoxMarker mdhd;
            AVIF_CHECKRES(avifRWStreamWriteFullBox(&s, "mdhd", AVIF_BOX_SIZE_TBD, 1, 0, &mdhd));
            AVIF_CHECKRES(avifRWStreamWriteU64(&s, now));                          // unsigned int(64) creation_time;
            AVIF_CHECKRES(avifRWStreamWriteU64(&s, now));                          // unsigned int(64) modification_time;
            AVIF_CHECKRES(avifRWStreamWriteU32(&s, (uint32_t)encoder->timescale)); // unsigned int(32) timescale;
            AVIF_CHECKRES(avifRWStreamWriteU64(&s, framesDurationInTimescales));   // unsigned int(64) duration;
            AVIF_CHECKRES(avifRWStreamWriteU16(&s, 21956)); // bit(1) pad = 0; unsigned int(5)[3] language; ("und")
            AVIF_CHECKRES(avifRWStreamWriteU16(&s, 0));     // unsigned int(16) pre_defined = 0;
            avifRWStreamFinishBox(&s, mdhd);

            avifBoxMarker hdlrTrak;
            AVIF_CHECKRES(avifRWStreamWriteFullBox(&s, "hdlr", AVIF_BOX_SIZE_TBD, 0, 0, &hdlrTrak));
            AVIF_CHECKRES(avifRWStreamWriteU32(&s, 0)); // unsigned int(32) pre_defined = 0;
            AVIF_CHECKRES(avifRWStreamWriteChars(&s, (item->itemCategory == AVIF_ITEM_ALPHA) ? "auxv" : "pict", 4)); // unsigned int(32) handler_type;
            AVIF_CHECKRES(avifRWStreamWriteZeros(&s, 12));           // const unsigned int(32)[3] reserved = 0;
            AVIF_CHECKRES(avifRWStreamWriteChars(&s, "libavif", 8)); // string name; (writing null terminator)
            avifRWStreamFinishBox(&s, hdlrTrak);

            avifBoxMarker minf;
            AVIF_CHECKRES(avifRWStreamWriteBox(&s, "minf", AVIF_BOX_SIZE_TBD, &minf));

            avifBoxMarker vmhd;
            AVIF_CHECKRES(avifRWStreamWriteFullBox(&s, "vmhd", AVIF_BOX_SIZE_TBD, 0, 1, &vmhd));
            AVIF_CHECKRES(avifRWStreamWriteU16(&s, 0)); // template unsigned int(16) graphicsmode = 0; (copy over the existing image)
            AVIF_CHECKRES(avifRWStreamWriteZeros(&s, 6)); // template unsigned int(16)[3] opcolor = {0, 0, 0};
            avifRWStreamFinishBox(&s, vmhd);

            avifBoxMarker dinf;
            AVIF_CHECKRES(avifRWStreamWriteBox(&s, "dinf", AVIF_BOX_SIZE_TBD, &dinf));
            avifBoxMarker dref;
            AVIF_CHECKRES(avifRWStreamWriteFullBox(&s, "dref", AVIF_BOX_SIZE_TBD, 0, 0, &dref));
            AVIF_CHECKRES(avifRWStreamWriteU32(&s, 1)); // unsigned int(32) entry_count;
            AVIF_CHECKRES(avifRWStreamWriteFullBox(&s, "url ", /*contentSize=*/0, 0, 1, /*marker=*/NULL)); // flags:1 means data is in this file
            avifRWStreamFinishBox(&s, dref);
            avifRWStreamFinishBox(&s, dinf);

            // The boxes within the "stbl" box are ordered using the following recommendation in ISO/IEC 14496-12, Section 6.2.3:
            // 4) It is recommended that the boxes within the Sample Table Box be in the following order: Sample Description
            // (stsd), Time to Sample (stts), Sample to Chunk (stsc), Sample Size (stsz), Chunk Offset (stco).
            //
            // Any boxes not listed in the above line are placed in the end (after the "stco" box).
            avifBoxMarker stbl;
            AVIF_CHECKRES(avifRWStreamWriteBox(&s, "stbl", AVIF_BOX_SIZE_TBD, &stbl));

            avifBoxMarker stsd;
            AVIF_CHECKRES(avifRWStreamWriteFullBox(&s, "stsd", AVIF_BOX_SIZE_TBD, 0, 0, &stsd));
            AVIF_CHECKRES(avifRWStreamWriteU32(&s, 1)); // unsigned int(32) entry_count;
            avifBoxMarker imageItem;
            AVIF_CHECKRES(avifRWStreamWriteBox(&s, encoder->data->imageItemType, AVIF_BOX_SIZE_TBD, &imageItem));
            AVIF_CHECKRES(avifRWStreamWriteZeros(&s, 6));                             // const unsigned int(8)[6] reserved = 0;
            AVIF_CHECKRES(avifRWStreamWriteU16(&s, 1));                               // unsigned int(16) data_reference_index;
            AVIF_CHECKRES(avifRWStreamWriteU16(&s, 0));                               // unsigned int(16) pre_defined = 0;
            AVIF_CHECKRES(avifRWStreamWriteU16(&s, 0));                               // const unsigned int(16) reserved = 0;
            AVIF_CHECKRES(avifRWStreamWriteZeros(&s, sizeof(uint32_t) * 3));          // unsigned int(32)[3] pre_defined = 0;
            AVIF_CHECKRES(avifRWStreamWriteU16(&s, (uint16_t)imageMetadata->width));  // unsigned int(16) width;
            AVIF_CHECKRES(avifRWStreamWriteU16(&s, (uint16_t)imageMetadata->height)); // unsigned int(16) height;
            AVIF_CHECKRES(avifRWStreamWriteU32(&s, 0x00480000));                      // template unsigned int(32) horizresolution
            AVIF_CHECKRES(avifRWStreamWriteU32(&s, 0x00480000));                      // template unsigned int(32) vertresolution
            AVIF_CHECKRES(avifRWStreamWriteU32(&s, 0));                               // const unsigned int(32) reserved = 0;
            AVIF_CHECKRES(avifRWStreamWriteU16(&s, 1));                      // template unsigned int(16) frame_count = 1;
            AVIF_CHECKRES(avifRWStreamWriteChars(&s, "\012AOM Coding", 11)); // string[32] compressorname;
            AVIF_CHECKRES(avifRWStreamWriteZeros(&s, 32 - 11));              //
            AVIF_CHECKRES(avifRWStreamWriteU16(&s, 0x0018));                 // template unsigned int(16) depth = 0x0018;
            AVIF_CHECKRES(avifRWStreamWriteU16(&s, (uint16_t)0xffff));       // int(16) pre_defined = -1;
            AVIF_CHECKRES(writeConfigBox(&s, &item->av1C, encoder->data->configPropName));
            if (item->itemCategory == AVIF_ITEM_COLOR) {
                AVIF_CHECKRES(avifEncoderWriteColorProperties(&s, imageMetadata, NULL, NULL));
            }

            avifBoxMarker ccst;
            AVIF_CHECKRES(avifRWStreamWriteFullBox(&s, "ccst", AVIF_BOX_SIZE_TBD, 0, 0, &ccst));
            AVIF_CHECKRES(avifRWStreamWriteBits(&s, 0, /*bitCount=*/1));  // unsigned int(1) all_ref_pics_intra;
            AVIF_CHECKRES(avifRWStreamWriteBits(&s, 1, /*bitCount=*/1));  // unsigned int(1) intra_pred_used;
            AVIF_CHECKRES(avifRWStreamWriteBits(&s, 15, /*bitCount=*/4)); // unsigned int(4) max_ref_per_pic;
            AVIF_CHECKRES(avifRWStreamWriteBits(&s, 0, /*bitCount=*/26)); // unsigned int(26) reserved;
            avifRWStreamFinishBox(&s, ccst);

            if (item->itemCategory == AVIF_ITEM_ALPHA) {
                avifBoxMarker auxi;
                AVIF_CHECKRES(avifRWStreamWriteFullBox(&s, "auxi", AVIF_BOX_SIZE_TBD, 0, 0, &auxi));
                AVIF_CHECKRES(avifRWStreamWriteChars(&s, alphaURN, alphaURNSize)); //  string aux_track_type;
                avifRWStreamFinishBox(&s, auxi);
            }

            avifRWStreamFinishBox(&s, imageItem);
            avifRWStreamFinishBox(&s, stsd);

            avifBoxMarker stts;
            AVIF_CHECKRES(avifRWStreamWriteFullBox(&s, "stts", AVIF_BOX_SIZE_TBD, 0, 0, &stts));
            size_t sttsEntryCountOffset = avifRWStreamOffset(&s);
            uint32_t sttsEntryCount = 0;
            AVIF_CHECKRES(avifRWStreamWriteU32(&s, 0)); // unsigned int(32) entry_count;
            for (uint32_t sampleCount = 0, frameIndex = 0; frameIndex < encoder->data->frames.count; ++frameIndex) {
                avifEncoderFrame * frame = &encoder->data->frames.frame[frameIndex];
                ++sampleCount;
                if (frameIndex < (encoder->data->frames.count - 1)) {
                    avifEncoderFrame * nextFrame = &encoder->data->frames.frame[frameIndex + 1];
                    if (frame->durationInTimescales == nextFrame->durationInTimescales) {
                        continue;
                    }
                }
                AVIF_CHECKRES(avifRWStreamWriteU32(&s, sampleCount));                           // unsigned int(32) sample_count;
                AVIF_CHECKRES(avifRWStreamWriteU32(&s, (uint32_t)frame->durationInTimescales)); // unsigned int(32) sample_delta;
                sampleCount = 0;
                ++sttsEntryCount;
            }
            size_t prevOffset = avifRWStreamOffset(&s);
            avifRWStreamSetOffset(&s, sttsEntryCountOffset);
            AVIF_CHECKRES(avifRWStreamWriteU32(&s, sttsEntryCount));
            avifRWStreamSetOffset(&s, prevOffset);
            avifRWStreamFinishBox(&s, stts);

            avifBoxMarker stsc;
            AVIF_CHECKRES(avifRWStreamWriteFullBox(&s, "stsc", AVIF_BOX_SIZE_TBD, 0, 0, &stsc));
            AVIF_CHECKRES(avifRWStreamWriteU32(&s, 1));                                 // unsigned int(32) entry_count;
            AVIF_CHECKRES(avifRWStreamWriteU32(&s, 1));                                 // unsigned int(32) first_chunk;
            AVIF_CHECKRES(avifRWStreamWriteU32(&s, item->encodeOutput->samples.count)); // unsigned int(32) samples_per_chunk;
            AVIF_CHECKRES(avifRWStreamWriteU32(&s, 1)); // unsigned int(32) sample_description_index;
            avifRWStreamFinishBox(&s, stsc);

            avifBoxMarker stsz;
            AVIF_CHECKRES(avifRWStreamWriteFullBox(&s, "stsz", AVIF_BOX_SIZE_TBD, 0, 0, &stsz));
            AVIF_CHECKRES(avifRWStreamWriteU32(&s, 0));                                 // unsigned int(32) sample_size;
            AVIF_CHECKRES(avifRWStreamWriteU32(&s, item->encodeOutput->samples.count)); // unsigned int(32) sample_count;
            for (uint32_t sampleIndex = 0; sampleIndex < item->encodeOutput->samples.count; ++sampleIndex) {
                avifEncodeSample * sample = &item->encodeOutput->samples.sample[sampleIndex];
                AVIF_CHECKRES(avifRWStreamWriteU32(&s, (uint32_t)sample->data.size)); // unsigned int(32) entry_size;
            }
            avifRWStreamFinishBox(&s, stsz);

            avifBoxMarker stco;
            AVIF_CHECKRES(avifRWStreamWriteFullBox(&s, "stco", AVIF_BOX_SIZE_TBD, 0, 0, &stco));
            AVIF_CHECKRES(avifRWStreamWriteU32(&s, 1)); // unsigned int(32) entry_count;
            avifEncoderItemAddMdatFixup(item, &s);      //
            AVIF_CHECKRES(avifRWStreamWriteU32(&s, 1)); // unsigned int(32) chunk_offset; (set later)
            avifRWStreamFinishBox(&s, stco);

            avifBoxMarker stss;
            AVIF_CHECKRES(avifRWStreamWriteFullBox(&s, "stss", AVIF_BOX_SIZE_TBD, 0, 0, &stss));
            AVIF_CHECKRES(avifRWStreamWriteU32(&s, syncSamplesCount)); // unsigned int(32) entry_count;
            for (uint32_t sampleIndex = 0; sampleIndex < item->encodeOutput->samples.count; ++sampleIndex) {
                avifEncodeSample * sample = &item->encodeOutput->samples.sample[sampleIndex];
                if (sample->sync) {
                    AVIF_CHECKRES(avifRWStreamWriteU32(&s, sampleIndex + 1)); // unsigned int(32) sample_number;
                }
            }
            avifRWStreamFinishBox(&s, stss);

            avifRWStreamFinishBox(&s, stbl);

            avifRWStreamFinishBox(&s, minf);
            avifRWStreamFinishBox(&s, mdia);
            avifRWStreamFinishBox(&s, trak);
        }

        // -------------------------------------------------------------------
        // Finish moov box

        avifRWStreamFinishBox(&s, moov);
    }

    // -----------------------------------------------------------------------
    // Write mdat

    avifEncoderItemReferenceArray layeredColorItems;
    avifEncoderItemReferenceArray layeredAlphaItems;
    avifResult result = AVIF_RESULT_OK;
    if (!avifArrayCreate(&layeredColorItems, sizeof(avifEncoderItemReference), 1)) {
        result = AVIF_RESULT_OUT_OF_MEMORY;
    }
    if (!avifArrayCreate(&layeredAlphaItems, sizeof(avifEncoderItemReference), 1)) {
        result = AVIF_RESULT_OUT_OF_MEMORY;
    }
    if (result == AVIF_RESULT_OK) {
        result = avifEncoderWriteMediaDataBox(encoder, &s, &layeredColorItems, &layeredAlphaItems);
    }
    avifArrayDestroy(&layeredColorItems);
    avifArrayDestroy(&layeredAlphaItems);
    AVIF_CHECKRES(result);

    // -----------------------------------------------------------------------
    // Finish up stream

    avifRWStreamFinishWrite(&s);

    return AVIF_RESULT_OK;
}

avifResult avifEncoderWrite(avifEncoder * encoder, const avifImage * image, avifRWData * output)
{
    avifResult addImageResult = avifEncoderAddImage(encoder, image, 1, AVIF_ADD_IMAGE_FLAG_SINGLE);
    if (addImageResult != AVIF_RESULT_OK) {
        return addImageResult;
    }
    return avifEncoderFinish(encoder, output);
}

// Implementation of section 2.3.3 of AV1 Codec ISO Media File Format Binding specification v1.2.0.
// See https://aomediacodec.github.io/av1-isobmff/v1.2.0.html#av1codecconfigurationbox-syntax.
static avifResult writeCodecConfig(avifRWStream * s, const avifCodecConfigurationBox * cfg)
{
    AVIF_CHECKRES(avifRWStreamWriteBits(s, 1, /*bitCount=*/1)); // unsigned int (1) marker = 1;
    AVIF_CHECKRES(avifRWStreamWriteBits(s, 1, /*bitCount=*/7)); // unsigned int (7) version = 1;

    AVIF_CHECKRES(avifRWStreamWriteBits(s, cfg->seqProfile, /*bitCount=*/3));   // unsigned int (3) seq_profile;
    AVIF_CHECKRES(avifRWStreamWriteBits(s, cfg->seqLevelIdx0, /*bitCount=*/5)); // unsigned int (5) seq_level_idx_0;

    AVIF_CHECKRES(avifRWStreamWriteBits(s, cfg->seqTier0, /*bitCount=*/1));             // unsigned int (1) seq_tier_0;
    AVIF_CHECKRES(avifRWStreamWriteBits(s, cfg->highBitdepth, /*bitCount=*/1));         // unsigned int (1) high_bitdepth;
    AVIF_CHECKRES(avifRWStreamWriteBits(s, cfg->twelveBit, /*bitCount=*/1));            // unsigned int (1) twelve_bit;
    AVIF_CHECKRES(avifRWStreamWriteBits(s, cfg->monochrome, /*bitCount=*/1));           // unsigned int (1) monochrome;
    AVIF_CHECKRES(avifRWStreamWriteBits(s, cfg->chromaSubsamplingX, /*bitCount=*/1));   // unsigned int (1) chroma_subsampling_x;
    AVIF_CHECKRES(avifRWStreamWriteBits(s, cfg->chromaSubsamplingY, /*bitCount=*/1));   // unsigned int (1) chroma_subsampling_y;
    AVIF_CHECKRES(avifRWStreamWriteBits(s, cfg->chromaSamplePosition, /*bitCount=*/2)); // unsigned int (2) chroma_sample_position;

    AVIF_CHECKRES(avifRWStreamWriteBits(s, 0, /*bitCount=*/3)); // unsigned int (3) reserved = 0;
    AVIF_CHECKRES(avifRWStreamWriteBits(s, 0, /*bitCount=*/1)); // unsigned int (1) initial_presentation_delay_present;
    AVIF_CHECKRES(avifRWStreamWriteBits(s, 0, /*bitCount=*/4)); // unsigned int (4) reserved = 0;

    // According to section 2.2.1 of AV1 Image File Format specification v1.1.0,
    // there is no need to write any OBU here.
    // See https://aomediacodec.github.io/av1-avif/v1.1.0.html#av1-configuration-item-property.
    // unsigned int (8) configOBUs[];
    return AVIF_RESULT_OK;
}

static avifResult writeConfigBox(avifRWStream * s, const avifCodecConfigurationBox * cfg, const char * configPropName)
{
    avifBoxMarker configBox;
    AVIF_CHECKRES(avifRWStreamWriteBox(s, configPropName, AVIF_BOX_SIZE_TBD, &configBox));
    AVIF_CHECKRES(writeCodecConfig(s, cfg));
    avifRWStreamFinishBox(s, configBox);
    return AVIF_RESULT_OK;
}
