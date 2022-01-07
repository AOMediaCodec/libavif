// Copyright 2019 Joe Drago. All rights reserved.
// SPDX-License-Identifier: BSD-2-Clause

#include "avif/internal.h"
#include "decodeinput.h"
#include "layout.h"

#include <assert.h>
#include <inttypes.h>
#include <limits.h>
#include <string.h>

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

avifBool avifCodecDecodeInputFillFromSampleTable(avifCodecDecodeInput * decodeInput,
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
            sample->spatialID = AVIF_SPATIAL_ID_UNSET; // Not filtering by spatial_id
            sample->sync = AVIF_FALSE;                 // to potentially be set to true following the outer loop

            if (sampleSize > UINT64_MAX - sampleOffset) {
                avifDiagnosticsPrintf(diag,
                                      "Sample table contains an offset/size pair which overflows: [%" PRIu64 " / %u]",
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

avifBool avifCodecDecodeInputFillFromDecoderItem(avifCodecDecodeInput * decodeInput,
                                                 avifDecoderItem * item,
                                                 avifBool allowProgressive,
                                                 const uint32_t imageCountLimit,
                                                 const uint64_t sizeHint,
                                                 avifDiagnostics * diag)
{
    if (sizeHint && (item->size > sizeHint)) {
        avifDiagnosticsPrintf(diag, "Exceeded avifIO's sizeHint, possibly truncated data");
        return AVIF_FALSE;
    }

    uint8_t layerCount = 0;
    size_t layerSizes[4] = { 0 };
    const avifProperty * a1lxProp = avifPropertyArrayFind(&item->properties, "a1lx");
    if (a1lxProp) {
        // Calculate layer count and all layer sizes from the a1lx box, and then validate

        size_t remainingSize = item->size;
        for (int i = 0; i < 3; ++i) {
            ++layerCount;

            const size_t layerSize = (size_t)a1lxProp->u.a1lx.layerSize[i];
            if (layerSize) {
                if (layerSize >= remainingSize) { // >= instead of > because there must be room for the last layer
                    avifDiagnosticsPrintf(diag, "a1lx layer index [%d] does not fit in item size", i);
                    return AVIF_FALSE;
                }
                layerSizes[i] = layerSize;
                remainingSize -= layerSize;
            } else {
                layerSizes[i] = remainingSize;
                remainingSize = 0;
                break;
            }
        }
        if (remainingSize > 0) {
            assert(layerCount == 3);
            ++layerCount;
            layerSizes[3] = remainingSize;
        }
    }

    const avifProperty * lselProp = avifPropertyArrayFind(&item->properties, "lsel");
    item->progressive = (a1lxProp && !lselProp); // Progressive images offer layers via the a1lxProp, but don't specify a layer selection with lsel.
    if (lselProp) {
        // Layer selection. This requires that the underlying AV1 codec decodes all layers,
        // and then only returns the requested layer as a single frame. To the user of libavif,
        // this appears to be a single frame.

        decodeInput->allLayers = AVIF_TRUE;

        size_t sampleSize = 0;
        if (layerCount > 0) {
            // Optimization: If we're selecting a layer that doesn't require the entire image's payload (hinted via the a1lx box)

            if (lselProp->u.lsel.layerID >= layerCount) {
                avifDiagnosticsPrintf(diag,
                                      "lsel property requests layer index [%u] which isn't present in a1lx property ([%u] layers)",
                                      lselProp->u.lsel.layerID,
                                      layerCount);
                return AVIF_FALSE;
            }

            for (uint8_t i = 0; i <= lselProp->u.lsel.layerID; ++i) {
                sampleSize += layerSizes[i];
            }
        } else {
            // This layer's payload subsection is unknown, just use the whole payload
            sampleSize = item->size;
        }

        avifDecodeSample * sample = (avifDecodeSample *)avifArrayPushPtr(&decodeInput->samples);
        sample->itemID = item->id;
        sample->offset = 0;
        sample->size = sampleSize;
        assert(lselProp->u.lsel.layerID < MAX_AV1_LAYER_COUNT);
        sample->spatialID = (uint8_t)lselProp->u.lsel.layerID;
        sample->sync = AVIF_TRUE;
    } else if (allowProgressive && item->progressive) {
        // Progressive image. Decode all layers and expose them all to the user.

        if (imageCountLimit && (layerCount > imageCountLimit)) {
            avifDiagnosticsPrintf(diag, "Exceeded avifDecoder's imageCountLimit (progressive)");
            return AVIF_FALSE;
        }

        decodeInput->allLayers = AVIF_TRUE;

        size_t offset = 0;
        for (int i = 0; i < layerCount; ++i) {
            avifDecodeSample * sample = (avifDecodeSample *)avifArrayPushPtr(&decodeInput->samples);
            sample->itemID = item->id;
            sample->offset = offset;
            sample->size = layerSizes[i];
            sample->spatialID = AVIF_SPATIAL_ID_UNSET;
            sample->sync = (i == 0); // Assume all layers depend on the first layer

            offset += layerSizes[i];
        }
    } else {
        // Typical case: Use the entire item's payload for a single frame output

        avifDecodeSample * sample = (avifDecodeSample *)avifArrayPushPtr(&decodeInput->samples);
        sample->itemID = item->id;
        sample->offset = 0;
        sample->size = item->size;
        sample->spatialID = AVIF_SPATIAL_ID_UNSET;
        sample->sync = AVIF_TRUE;
    }
    return AVIF_TRUE;
}
