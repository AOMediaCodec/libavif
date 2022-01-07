// Copyright 2019 Joe Drago. All rights reserved.
// SPDX-License-Identifier: BSD-2-Clause

#include "avif/internal.h"
#include "decodeinput.h"
#include "decoderdata.h"
#include "layout.h"
#include "meta.h"
#include "sample.h"

#include <assert.h>
#include <inttypes.h>
#include <limits.h>
#include <string.h>

avifBool avifDecoderGenerateImageGridTiles(avifDecoder * decoder, avifImageGrid * grid, avifDecoderItem * gridItem, avifBool alpha)
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
                avifDiagnosticsPrintf(&decoder->diag, "Grid image contains tile with an unsupported property marked as essential");
                return AVIF_FALSE;
            }

            ++tilesAvailable;
        }
    }

    if (tilesRequested != tilesAvailable) {
        avifDiagnosticsPrintf(&decoder->diag,
                              "Grid image of dimensions %ux%u requires %u tiles, and only %u were found",
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

            avifTile * tile = avifDecoderDataCreateTile(decoder->data, item->width, item->height, avifDecoderItemOperatingPoint(item));
            if (!avifCodecDecodeInputFillFromDecoderItem(tile->input,
                                                         item,
                                                         decoder->allowProgressive,
                                                         decoder->imageCountLimit,
                                                         decoder->io->sizeHint,
                                                         &decoder->diag)) {
                return AVIF_FALSE;
            }
            tile->input->alpha = alpha;

            if (firstTile) {
                firstTile = AVIF_FALSE;

                // Adopt the av1C property of the first av01 tile, so that it can be queried from
                // the top-level color/alpha item during avifDecoderReset().
                const avifProperty * srcProp = avifPropertyArrayFind(&item->properties, "av1C");
                if (!srcProp) {
                    avifDiagnosticsPrintf(&decoder->diag, "Grid image's first tile is missing an av1C property");
                    return AVIF_FALSE;
                }
                avifProperty * dstProp = (avifProperty *)avifArrayPushPtr(&gridItem->properties);
                memcpy(dstProp, srcProp, sizeof(avifProperty));

                if (!alpha && item->progressive) {
                    decoder->progressiveState = AVIF_PROGRESSIVE_STATE_AVAILABLE;
                    if (tile->input->samples.count > 1) {
                        decoder->progressiveState = AVIF_PROGRESSIVE_STATE_ACTIVE;
                        decoder->imageCount = tile->input->samples.count;
                    }
                }
            }
        }
    }
    return AVIF_TRUE;
}

avifBool avifDecoderDataFillImageGrid(avifDecoderData * data,
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
                              "Grid image tiles are smaller than 64x64 (%ux%u). See MIAF (ISO/IEC 23000-22:2019), Section 7.3.11.4.2",
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
