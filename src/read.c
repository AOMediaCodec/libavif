// Copyright 2019 Joe Drago. All rights reserved.
// SPDX-License-Identifier: BSD-2-Clause

#include "avif/internal.h"
#include "box.h"
#include "decodeinput.h"
#include "decoderdata.h"
#include "layout.h"
#include "meta.h"
#include "sample.h"

#include <assert.h>
#include <inttypes.h>
#include <limits.h>
#include <string.h>

avifDecoder * avifDecoderCreate(void)
{
    avifDecoder * decoder = (avifDecoder *)avifAlloc(sizeof(avifDecoder));
    memset(decoder, 0, sizeof(avifDecoder));
    decoder->maxThreads = 1;
    decoder->imageSizeLimit = AVIF_DEFAULT_IMAGE_SIZE_LIMIT;
    decoder->imageCountLimit = AVIF_DEFAULT_IMAGE_COUNT_LIMIT;
    decoder->strictFlags = AVIF_STRICT_ENABLED;
    return decoder;
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
                avifResult maxExtentResult = avifDecoderItemMaxExtent(item, sample, &sampleExtent);
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

// ---------------------------------------------------------------------------

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
        tile->codec->diag = &decoder->diag;
        tile->codec->operatingPoint = tile->operatingPoint;
        tile->codec->allLayers = tile->input->allLayers;
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
    decoder->progressiveState = AVIF_PROGRESSIVE_STATE_UNAVAILABLE;
    data->cicpSet = AVIF_FALSE;

    memset(&decoder->ioStats, 0, sizeof(decoder->ioStats));

    // -----------------------------------------------------------------------
    // Build decode input

    data->sourceSampleTable = NULL; // Reset
    if (decoder->requestedSource == AVIF_DECODER_SOURCE_AUTO) {
        // Honor the major brand (avif or avis) if present, otherwise prefer avis (tracks) if possible.
        if (!memcmp(data->majorBrand, "avis", 4)) {
            data->source = AVIF_DECODER_SOURCE_TRACKS;
        } else if (!memcmp(data->majorBrand, "avif", 4)) {
            data->source = AVIF_DECODER_SOURCE_PRIMARY_ITEM;
        } else if (data->tracks.count > 0) {
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
            avifDiagnosticsPrintf(&decoder->diag, "Failed to find AV1 color track");
            return AVIF_RESULT_NO_CONTENT;
        }
        colorTrack = &data->tracks.track[colorTrackIndex];

        colorProperties = avifSampleTableGetProperties(colorTrack->sampleTable);
        if (!colorProperties) {
            avifDiagnosticsPrintf(&decoder->diag, "Failed to find AV1 color track's color properties");
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

        avifTile * colorTile = avifDecoderDataCreateTile(data, colorTrack->width, colorTrack->height, 0); // No way to set operating point via tracks
        if (!avifCodecDecodeInputFillFromSampleTable(colorTile->input,
                                                     colorTrack->sampleTable,
                                                     decoder->imageCountLimit,
                                                     decoder->io->sizeHint,
                                                     data->diag)) {
            return AVIF_RESULT_BMFF_PARSE_FAILED;
        }
        data->colorTileCount = 1;

        if (alphaTrack) {
            avifTile * alphaTile = avifDecoderDataCreateTile(data, alphaTrack->width, alphaTrack->height, 0); // No way to set operating point via tracks
            if (!avifCodecDecodeInputFillFromSampleTable(alphaTile->input,
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
            avifDiagnosticsPrintf(&decoder->diag, "Primary item not specified");
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
            if (item->id != data->meta->primaryItemID) {
                // This is not the primary item, skip it
                continue;
            }

            if (isGrid) {
                avifROData readData;
                avifResult readResult = avifDecoderItemRead(item, decoder->io, &readData, 0, 0, data->diag);
                if (readResult != AVIF_RESULT_OK) {
                    return readResult;
                }
                if (!avifParseImageGridBox(&data->colorGrid, readData.data, readData.size, decoder->imageSizeLimit, data->diag)) {
                    return AVIF_RESULT_INVALID_IMAGE_GRID;
                }
            }

            colorItem = item;
            break;
        }

        if (!colorItem) {
            avifDiagnosticsPrintf(&decoder->diag, "Primary item not found");
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
                    avifResult readResult = avifDecoderItemRead(item, decoder->io, &readData, 0, 0, data->diag);
                    if (readResult != AVIF_RESULT_OK) {
                        return readResult;
                    }
                    if (!avifParseImageGridBox(&data->alphaGrid, readData.data, readData.size, decoder->imageSizeLimit, data->diag)) {
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

        if ((data->colorGrid.rows > 0) && (data->colorGrid.columns > 0)) {
            if (!avifDecoderGenerateImageGridTiles(decoder, &data->colorGrid, colorItem, AVIF_FALSE)) {
                return AVIF_RESULT_INVALID_IMAGE_GRID;
            }
            data->colorTileCount = data->tiles.count;
        } else {
            if (colorItem->size == 0) {
                return AVIF_RESULT_NO_AV1_ITEMS_FOUND;
            }

            avifTile * colorTile =
                avifDecoderDataCreateTile(data, colorItem->width, colorItem->height, avifDecoderItemOperatingPoint(colorItem));
            if (!avifCodecDecodeInputFillFromDecoderItem(colorTile->input,
                                                         colorItem,
                                                         decoder->allowProgressive,
                                                         decoder->imageCountLimit,
                                                         decoder->io->sizeHint,
                                                         &decoder->diag)) {
                return AVIF_FALSE;
            }
            data->colorTileCount = 1;

            if (colorItem->progressive) {
                decoder->progressiveState = AVIF_PROGRESSIVE_STATE_AVAILABLE;
                if (colorTile->input->samples.count > 1) {
                    decoder->progressiveState = AVIF_PROGRESSIVE_STATE_ACTIVE;
                    decoder->imageCount = colorTile->input->samples.count;
                }
            }
        }

        if (alphaItem) {
            if (!alphaItem->width && !alphaItem->height) {
                // NON-STANDARD: Alpha subimage does not have an ispe property; adopt width/height from color item
                assert(!(decoder->strictFlags & AVIF_STRICT_ALPHA_ISPE_REQUIRED));
                alphaItem->width = colorItem->width;
                alphaItem->height = colorItem->height;
            }

            if ((data->alphaGrid.rows > 0) && (data->alphaGrid.columns > 0)) {
                if (!avifDecoderGenerateImageGridTiles(decoder, &data->alphaGrid, alphaItem, AVIF_TRUE)) {
                    return AVIF_RESULT_INVALID_IMAGE_GRID;
                }
                data->alphaTileCount = data->tiles.count - data->colorTileCount;
            } else {
                if (alphaItem->size == 0) {
                    return AVIF_RESULT_NO_AV1_ITEMS_FOUND;
                }

                avifTile * alphaTile =
                    avifDecoderDataCreateTile(data, alphaItem->width, alphaItem->height, avifDecoderItemOperatingPoint(alphaItem));
                if (!avifCodecDecodeInputFillFromDecoderItem(alphaTile->input,
                                                             alphaItem,
                                                             decoder->allowProgressive,
                                                             decoder->imageCountLimit,
                                                             decoder->io->sizeHint,
                                                             &decoder->diag)) {
                    return AVIF_FALSE;
                }
                alphaTile->input->alpha = AVIF_TRUE;
                data->alphaTileCount = 1;
            }
        }

        decoder->ioStats.colorOBUSize = colorItem->size;
        decoder->ioStats.alphaOBUSize = alphaItem ? alphaItem->size : 0;

        decoder->image->width = colorItem->width;
        decoder->image->height = colorItem->height;
        decoder->alphaPresent = (alphaItem != NULL);
        decoder->image->alphaPremultiplied = decoder->alphaPresent && (colorItem->premByID == alphaItem->id);

        avifResult colorItemValidationResult = avifDecoderItemValidateAV1(colorItem, &decoder->diag, decoder->strictFlags);
        if (colorItemValidationResult != AVIF_RESULT_OK) {
            return colorItemValidationResult;
        }
        if (alphaItem) {
            avifResult alphaItemValidationResult = avifDecoderItemValidateAV1(alphaItem, &decoder->diag, decoder->strictFlags);
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

        if (!tile->codec->getNextImage(tile->codec, decoder, sample, tile->input->alpha, tile->image)) {
            avifDiagnosticsPrintf(&decoder->diag, "tile->codec->getNextImage() failed");
            return tile->input->alpha ? AVIF_RESULT_DECODE_ALPHA_FAILED : AVIF_RESULT_DECODE_COLOR_FAILED;
        }

        // Scale the decoded image so that it corresponds to this tile's output dimensions
        if ((tile->width != tile->image->width) || (tile->height != tile->image->height)) {
            if (!avifImageScale(tile->image, tile->width, tile->height, decoder->imageSizeLimit, &decoder->diag)) {
                avifDiagnosticsPrintf(&decoder->diag, "avifImageScale() failed");
                return tile->input->alpha ? AVIF_RESULT_DECODE_ALPHA_FAILED : AVIF_RESULT_DECODE_COLOR_FAILED;
            }
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
            avifDiagnosticsPrintf(&decoder->diag, "decoder->data->colorTileCount should be 1 but is %u", decoder->data->colorTileCount);
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
                avifDiagnosticsPrintf(&decoder->diag, "decoder->data->alphaTileCount should be 1 but is %u", decoder->data->alphaTileCount);
                return AVIF_RESULT_DECODE_ALPHA_FAILED;
            }

            avifImage * srcAlpha = decoder->data->tiles.tile[decoder->data->colorTileCount].image;
            if ((decoder->image->width != srcAlpha->width) || (decoder->image->height != srcAlpha->height) ||
                (decoder->image->depth != srcAlpha->depth)) {
                avifDiagnosticsPrintf(&decoder->diag, "decoder->image does not match srcAlpha in width, height, or bit depth");
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
