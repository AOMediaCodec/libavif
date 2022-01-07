// Copyright 2019 Joe Drago. All rights reserved.
// SPDX-License-Identifier: BSD-2-Clause

#include "avif/internal.h"
#include "decoderitem.h"
#include "meta.h"
#include "sample.h"

#include <assert.h>
#include <inttypes.h>
#include <limits.h>
#include <string.h>

uint32_t avifCodecConfigurationBoxGetDepth(const avifCodecConfigurationBox * av1C)
{
    if (av1C->twelveBit) {
        return 12;
    } else if (av1C->highBitdepth) {
        return 10;
    }
    return 8;
}

// This is used as a hint to validating the clap box in avifDecoderItemValidateAV1.
static avifPixelFormat avifCodecConfigurationBoxGetFormat(const avifCodecConfigurationBox * av1C)
{
    if (av1C->monochrome) {
        return AVIF_PIXEL_FORMAT_YUV400;
    } else if (av1C->chromaSubsamplingY == 1) {
        return AVIF_PIXEL_FORMAT_YUV420;
    } else if (av1C->chromaSubsamplingX == 1) {
        return AVIF_PIXEL_FORMAT_YUV422;
    }
    return AVIF_PIXEL_FORMAT_YUV444;
}

avifResult avifDecoderItemMaxExtent(const avifDecoderItem * item, const avifDecodeSample * sample, avifExtent * outExtent)
{
    if (item->extents.count == 0) {
        return AVIF_RESULT_TRUNCATED_DATA;
    }

    if (item->idatStored) {
        // construction_method: idat(1)

        if (item->meta->idat.size > 0) {
            // Already read from a meta box during Parse()
            memset(outExtent, 0, sizeof(avifExtent));
            return AVIF_RESULT_OK;
        }

        // no associated idat box was found in the meta box, bail out
        return AVIF_RESULT_NO_CONTENT;
    }

    // construction_method: file(0)

    if (sample->size == 0) {
        return AVIF_RESULT_TRUNCATED_DATA;
    }
    uint64_t remainingOffset = sample->offset;
    size_t remainingBytes = sample->size; // This may be smaller than item->size if the item is progressive

    // Assert that the for loop below will execute at least one iteration.
    assert(item->extents.count != 0);
    uint64_t minOffset = UINT64_MAX;
    uint64_t maxOffset = 0;
    for (uint32_t extentIter = 0; extentIter < item->extents.count; ++extentIter) {
        avifExtent * extent = &item->extents.extent[extentIter];

        // Make local copies of extent->offset and extent->size as they might need to be adjusted
        // due to the sample's offset.
        uint64_t startOffset = extent->offset;
        size_t extentSize = extent->size;
        if (remainingOffset) {
            if (remainingOffset >= extentSize) {
                remainingOffset -= extentSize;
                continue;
            } else {
                if (remainingOffset > UINT64_MAX - startOffset) {
                    return AVIF_RESULT_BMFF_PARSE_FAILED;
                }
                startOffset += remainingOffset;
                extentSize -= remainingOffset;
                remainingOffset = 0;
            }
        }

        const size_t usedExtentSize = (extentSize < remainingBytes) ? extentSize : remainingBytes;

        if (usedExtentSize > UINT64_MAX - startOffset) {
            return AVIF_RESULT_BMFF_PARSE_FAILED;
        }
        const uint64_t endOffset = startOffset + usedExtentSize;

        if (minOffset > startOffset) {
            minOffset = startOffset;
        }
        if (maxOffset < endOffset) {
            maxOffset = endOffset;
        }

        remainingBytes -= usedExtentSize;
        if (remainingBytes == 0) {
            // We've got enough bytes for this sample.
            break;
        }
    }

    if (remainingBytes != 0) {
        return AVIF_RESULT_TRUNCATED_DATA;
    }

    outExtent->offset = minOffset;
    const uint64_t extentLength = maxOffset - minOffset;
    if (extentLength > SIZE_MAX) {
        return AVIF_RESULT_BMFF_PARSE_FAILED;
    }
    outExtent->size = (size_t)extentLength;
    return AVIF_RESULT_OK;
}

uint8_t avifDecoderItemOperatingPoint(const avifDecoderItem * item)
{
    const avifProperty * a1opProp = avifPropertyArrayFind(&item->properties, "a1op");
    if (a1opProp) {
        return a1opProp->u.a1op.opIndex;
    }
    return 0; // default
}

avifResult avifDecoderItemValidateAV1(const avifDecoderItem * item, avifDiagnostics * diag, const avifStrictFlags strictFlags)
{
    const avifProperty * av1CProp = avifPropertyArrayFind(&item->properties, "av1C");
    if (!av1CProp) {
        // An av1C box is mandatory in all valid AVIF configurations. Bail out.
        avifDiagnosticsPrintf(diag, "Item ID %u of type '%.4s' is missing mandatory av1C property", item->id, (const char *)item->type);
        return AVIF_RESULT_BMFF_PARSE_FAILED;
    }

    const avifProperty * pixiProp = avifPropertyArrayFind(&item->properties, "pixi");
    if (!pixiProp && (strictFlags & AVIF_STRICT_PIXI_REQUIRED)) {
        // A pixi box is mandatory in all valid AVIF configurations. Bail out.
        avifDiagnosticsPrintf(diag,
                              "[Strict] Item ID %u of type '%.4s' is missing mandatory pixi property",
                              item->id,
                              (const char *)item->type);
        return AVIF_RESULT_BMFF_PARSE_FAILED;
    }

    if (pixiProp) {
        const uint32_t av1CDepth = avifCodecConfigurationBoxGetDepth(&av1CProp->u.av1C);
        for (uint8_t i = 0; i < pixiProp->u.pixi.planeCount; ++i) {
            if (pixiProp->u.pixi.planeDepths[i] != av1CDepth) {
                // pixi depth must match av1C depth
                avifDiagnosticsPrintf(diag,
                                      "Item ID %u depth specified by pixi property [%u] does not match av1C property depth [%u]",
                                      item->id,
                                      pixiProp->u.pixi.planeDepths[i],
                                      av1CDepth);
                return AVIF_RESULT_BMFF_PARSE_FAILED;
            }
        }
    }

    if (strictFlags & AVIF_STRICT_CLAP_VALID) {
        const avifProperty * clapProp = avifPropertyArrayFind(&item->properties, "clap");
        if (clapProp) {
            const avifProperty * ispeProp = avifPropertyArrayFind(&item->properties, "ispe");
            if (!ispeProp) {
                avifDiagnosticsPrintf(diag,
                                      "[Strict] Item ID %u is missing an ispe property, so its clap property cannot be validated",
                                      item->id);
                return AVIF_RESULT_BMFF_PARSE_FAILED;
            }

            avifCropRect cropRect;
            const uint32_t imageW = ispeProp->u.ispe.width;
            const uint32_t imageH = ispeProp->u.ispe.height;
            const avifPixelFormat av1CFormat = avifCodecConfigurationBoxGetFormat(&av1CProp->u.av1C);
            avifBool validClap = avifCropRectConvertCleanApertureBox(&cropRect, &clapProp->u.clap, imageW, imageH, av1CFormat, diag);
            if (!validClap) {
                return AVIF_RESULT_BMFF_PARSE_FAILED;
            }
        }
    }
    return AVIF_RESULT_OK;
}

avifResult avifDecoderItemRead(avifDecoderItem * item,
                               avifIO * io,
                               avifROData * outData,
                               size_t offset,
                               size_t partialByteCount,
                               avifDiagnostics * diag)
{
    if (item->mergedExtents.data && !item->partialMergedExtents) {
        // Multiple extents have already been concatenated for this item, just return it
        if (offset >= item->mergedExtents.size) {
            avifDiagnosticsPrintf(diag, "Item ID %u read has overflowing offset", item->id);
            return AVIF_RESULT_TRUNCATED_DATA;
        }
        outData->data = item->mergedExtents.data + offset;
        outData->size = item->mergedExtents.size - offset;
        return AVIF_RESULT_OK;
    }

    if (item->extents.count == 0) {
        avifDiagnosticsPrintf(diag, "Item ID %u has zero extents", item->id);
        return AVIF_RESULT_TRUNCATED_DATA;
    }

    // Find this item's source of all extents' data, based on the construction method
    const avifRWData * idatBuffer = NULL;
    if (item->idatStored) {
        // construction_method: idat(1)

        if (item->meta->idat.size > 0) {
            idatBuffer = &item->meta->idat;
        } else {
            // no associated idat box was found in the meta box, bail out
            avifDiagnosticsPrintf(diag, "Item ID %u is stored in an idat, but no associated idat box was found", item->id);
            return AVIF_RESULT_NO_CONTENT;
        }
    }

    // Merge extents into a single contiguous buffer
    if ((io->sizeHint > 0) && (item->size > io->sizeHint)) {
        // Sanity check: somehow the sum of extents exceeds the entire file or idat size!
        avifDiagnosticsPrintf(diag, "Item ID %u reported size failed size hint sanity check. Truncated data?", item->id);
        return AVIF_RESULT_TRUNCATED_DATA;
    }

    if (offset >= item->size) {
        avifDiagnosticsPrintf(diag, "Item ID %u read has overflowing offset", item->id);
        return AVIF_RESULT_TRUNCATED_DATA;
    }
    const size_t maxOutputSize = item->size - offset;
    const size_t readOutputSize = (partialByteCount && (partialByteCount < maxOutputSize)) ? partialByteCount : maxOutputSize;
    const size_t totalBytesToRead = offset + readOutputSize;

    // If there is a single extent for this item and the source of the read buffer is going to be
    // persistent for the lifetime of the avifDecoder (whether it comes from its own internal
    // idatBuffer or from a known-persistent IO), we can avoid buffer duplication and just use the
    // preexisting buffer.
    avifBool singlePersistentBuffer = ((item->extents.count == 1) && (idatBuffer || io->persistent));
    if (!singlePersistentBuffer) {
        // Always allocate the item's full size here, as progressive image decodes will do partial
        // reads into this buffer and begin feeding the buffer to the underlying AV1 decoder, but
        // will then write more into this buffer without flushing the AV1 decoder (which is still
        // holding the address of the previous allocation of this buffer). This strategy avoids
        // use-after-free issues in the AV1 decoder and unnecessary reallocs as a typical
        // progressive decode use case will eventually decode the final layer anyway.
        avifRWDataRealloc(&item->mergedExtents, item->size);
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
                avifDiagnosticsPrintf(diag, "Item ID %u has impossible extent offset in idat buffer", item->id);
                return AVIF_RESULT_BMFF_PARSE_FAILED;
            }
            // Since extent->offset (a uint64_t) is not bigger than idatBuffer->size (a size_t),
            // it is safe to cast extent->offset to size_t.
            const size_t extentOffset = (size_t)extent->offset;
            if (extent->size > idatBuffer->size - extentOffset) {
                avifDiagnosticsPrintf(diag, "Item ID %u has impossible extent size in idat buffer", item->id);
                return AVIF_RESULT_BMFF_PARSE_FAILED;
            }
            offsetBuffer.data = idatBuffer->data + extentOffset;
            offsetBuffer.size = idatBuffer->size - extentOffset;
        } else {
            // construction_method: file(0)

            if ((io->sizeHint > 0) && (extent->offset > io->sizeHint)) {
                avifDiagnosticsPrintf(diag, "Item ID %u extent offset failed size hint sanity check. Truncated data?", item->id);
                return AVIF_RESULT_BMFF_PARSE_FAILED;
            }
            avifResult readResult = io->read(io, 0, extent->offset, bytesToRead, &offsetBuffer);
            if (readResult != AVIF_RESULT_OK) {
                return readResult;
            }
            if (bytesToRead != offsetBuffer.size) {
                avifDiagnosticsPrintf(diag,
                                      "Item ID %u tried to read %zu bytes, but only received %zu bytes",
                                      item->id,
                                      bytesToRead,
                                      offsetBuffer.size);
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
        avifDiagnosticsPrintf(diag, "Item ID %u has %zu unexpected trailing bytes", item->id, remainingBytes);
        return AVIF_RESULT_TRUNCATED_DATA;
    }

    outData->data = item->mergedExtents.data + offset;
    outData->size = readOutputSize;
    item->partialMergedExtents = (item->size != totalBytesToRead);
    return AVIF_RESULT_OK;
}
