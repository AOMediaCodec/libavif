// Copyright 2019 Joe Drago. All rights reserved.
// SPDX-License-Identifier: BSD-2-Clause

#ifndef SRC_LAYOUT_H_
#define SRC_LAYOUT_H_

#include "avif/internal.h"
#include "decoderitem.h"
#include "meta.h"
#include "sample.h"

#include <assert.h>
#include <inttypes.h>
#include <limits.h>
#include <string.h>

#define MAX_AV1_LAYER_COUNT 4

// grid storage
typedef struct avifImageGrid
{
    uint32_t rows;    // Legal range: [1-256]
    uint32_t columns; // Legal range: [1-256]
    uint32_t outputWidth;
    uint32_t outputHeight;
} avifImageGrid;

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

typedef struct avifTile
{
    avifCodecDecodeInput * input;
    struct avifCodec * codec;
    avifImage * image;
    uint32_t width;  // Either avifTrack.width or avifDecoderItem.width
    uint32_t height; // Either avifTrack.height or avifDecoderItem.height
    uint8_t operatingPoint;
} avifTile;
AVIF_ARRAY_DECLARE(avifTileArray, avifTile, tile);

avifBool avifDecoderGenerateImageGridTiles(avifDecoder * decoder, avifImageGrid * grid, avifDecoderItem * gridItem, avifBool alpha);

struct avifDecoderData;

avifBool avifDecoderDataFillImageGrid(struct avifDecoderData * data,
                                      avifImageGrid * grid,
                                      avifImage * dstImage,
                                      unsigned int firstTileIndex,
                                      unsigned int tileCount,
                                      avifBool alpha);

avifBool avifParseImageGridBox(avifImageGrid * grid, const uint8_t * raw, size_t rawLen, uint32_t imageSizeLimit, avifDiagnostics * diag);

#endif  // SRC_LAYOUT_H_
