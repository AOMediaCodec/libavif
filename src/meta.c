// Copyright 2019 Joe Drago. All rights reserved.
// SPDX-License-Identifier: BSD-2-Clause

#include "avif/internal.h"
#include "meta.h"

#include <assert.h>
#include <inttypes.h>
#include <limits.h>
#include <string.h>

static const char xmpContentType[] = CONTENT_TYPE_XMP;
static const size_t xmpContentTypeSize = sizeof(xmpContentType);

avifMeta * avifMetaCreate()
{
    avifMeta * meta = (avifMeta *)avifAlloc(sizeof(avifMeta));
    memset(meta, 0, sizeof(avifMeta));
    avifArrayCreate(&meta->items, sizeof(avifDecoderItem), 8);
    avifArrayCreate(&meta->properties, sizeof(avifProperty), 16);
    return meta;
}

void avifMetaDestroy(avifMeta * meta)
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
    avifRWDataFree(&meta->idat);
    avifFree(meta);
}

avifDecoderItem * avifMetaFindItem(avifMeta * meta, uint32_t itemID)
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

avifResult avifDecoderFindMetadata(avifDecoder * decoder, avifMeta * meta, avifImage * image, uint32_t colorId)
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
            avifResult readResult = avifDecoderItemRead(item, decoder->io, &exifContents, 0, 0, &decoder->diag);
            if (readResult != AVIF_RESULT_OK) {
                return readResult;
            }

            // Advance past Annex A.2.1's header
            BEGIN_STREAM(exifBoxStream, exifContents.data, exifContents.size, &decoder->diag, "Exif header");
            uint32_t exifTiffHeaderOffset;
            CHECKERR(avifROStreamReadU32(&exifBoxStream, &exifTiffHeaderOffset), AVIF_RESULT_BMFF_PARSE_FAILED); // unsigned int(32) exif_tiff_header_offset;

            avifImageSetMetadataExif(image, avifROStreamCurrent(&exifBoxStream), avifROStreamRemainingBytes(&exifBoxStream));
        } else if (!decoder->ignoreXMP && !memcmp(item->type, "mime", 4) &&
                   !memcmp(item->contentType.contentType, xmpContentType, xmpContentTypeSize)) {
            avifROData xmpContents;
            avifResult readResult = avifDecoderItemRead(item, decoder->io, &xmpContents, 0, 0, &decoder->diag);
            if (readResult != AVIF_RESULT_OK) {
                return readResult;
            }

            avifImageSetMetadataXMP(image, xmpContents.data, xmpContents.size);
        }
    }
    return AVIF_RESULT_OK;
}
