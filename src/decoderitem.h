// Copyright 2019 Joe Drago. All rights reserved.
// SPDX-License-Identifier: BSD-2-Clause

#ifndef SRC_DECODERITEM_H_
#define SRC_DECODERITEM_H_

#include "avif/internal.h"
#include "sample.h"

#include <assert.h>
#include <inttypes.h>
#include <limits.h>
#include <string.h>

// ---------------------------------------------------------------------------
// Top-level structures

struct avifMeta;

AVIF_ARRAY_DECLARE(avifExtentArray, avifExtent, extent);

// one "item" worth for decoding (all iref, iloc, iprp, etc refer to one of these)
typedef struct avifDecoderItem
{
    uint32_t id;
    struct avifMeta * meta; // Unowned; A back-pointer for convenience
    uint8_t type[4];
    size_t size;
    avifBool idatStored; // If true, offset is relative to the associated meta box's idat box (iloc construction_method==1)
    uint32_t width;      // Set from this item's ispe property, if present
    uint32_t height;     // Set from this item's ispe property, if present
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
    avifBool ipmaSeen;    // if true, this item already received a property association
    avifBool progressive; // if true, this item has progressive layers (a1lx), but does not select a specific layer (lsel)
} avifDecoderItem;
AVIF_ARRAY_DECLARE(avifDecoderItemArray, avifDecoderItem, item);

uint32_t avifCodecConfigurationBoxGetDepth(const avifCodecConfigurationBox * av1C);

// This returns the max extent that has to be read in order to decode this item. If
// the item is stored in an idat, the data has already been read during Parse() and
// this function will return AVIF_RESULT_OK with a 0-byte extent.
avifResult avifDecoderItemMaxExtent(const avifDecoderItem * item, const avifDecodeSample * sample, avifExtent * outExtent);

uint8_t avifDecoderItemOperatingPoint(const avifDecoderItem * item);

avifResult avifDecoderItemValidateAV1(const avifDecoderItem * item, avifDiagnostics * diag, const avifStrictFlags strictFlags);

avifResult avifDecoderItemRead(avifDecoderItem * item,
                               avifIO * io,
                               avifROData * outData,
                               size_t offset,
                               size_t partialByteCount,
                               avifDiagnostics * diag);

#endif  // SRC_DECODERITEM_H_
