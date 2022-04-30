// Copyright 2022 Google LLC. All rights reserved.
// SPDX-License-Identifier: BSD-2-Clause

#include "avifincrtest_helpers.h"
#include "avif/avif.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

//------------------------------------------------------------------------------

// Returns true if the first (top) rowCount rows of image1 and image2 are identical.
static avifBool comparePartialYUVA(const avifImage * image1, const avifImage * image2, uint32_t rowCount)
{
    if (rowCount == 0) {
        return AVIF_TRUE;
    }
    if (!image1 || !image2 || (image1->width != image2->width) || (image1->depth != image2->depth) ||
        (image1->yuvFormat != image2->yuvFormat) || (image1->yuvRange != image2->yuvRange)) {
        printf("ERROR: input mismatch\n");
        return AVIF_FALSE;
    }
    if ((image1->height < rowCount) || (image2->height < rowCount)) {
        printf("ERROR: not enough rows to compare\n");
        return AVIF_FALSE;
    }

    avifPixelFormatInfo info;
    avifGetPixelFormatInfo(image1->yuvFormat, &info);
    const uint32_t uvWidth = (image1->width + info.chromaShiftX) >> info.chromaShiftX;
    const uint32_t uvHeight = (rowCount + info.chromaShiftY) >> info.chromaShiftY;
    const uint32_t pixelByteCount = (image1->depth > 8) ? sizeof(uint16_t) : sizeof(uint8_t);

    for (uint32_t plane = 0; plane < (info.monochrome ? 1 : AVIF_PLANE_COUNT_YUV); ++plane) {
        const uint32_t width = (plane == AVIF_CHAN_Y) ? image1->width : uvWidth;
        const uint32_t widthByteCount = width * pixelByteCount;
        const uint32_t height = (plane == AVIF_CHAN_Y) ? rowCount : uvHeight;
        const uint8_t * data1 = image1->yuvPlanes[plane];
        const uint8_t * data2 = image2->yuvPlanes[plane];
        for (uint32_t y = 0; y < height; ++y) {
            if (memcmp(data1, data2, widthByteCount)) {
                printf("ERROR: different px at row %" PRIu32 ", channel %" PRIu32 "\n", y, plane);
                return AVIF_FALSE;
            }
            data1 += image1->yuvRowBytes[plane];
            data2 += image2->yuvRowBytes[plane];
        }
    }

    if (image1->alphaPlane) {
        if (!image2->alphaPlane || (image1->alphaPremultiplied != image2->alphaPremultiplied)) {
            printf("ERROR: input mismatch\n");
            return AVIF_FALSE;
        }
        const uint32_t widthByteCount = image1->width * pixelByteCount;
        const uint8_t * data1 = image1->alphaPlane;
        const uint8_t * data2 = image2->alphaPlane;
        for (uint32_t y = 0; y < rowCount; ++y) {
            if (memcmp(data1, data2, widthByteCount)) {
                printf("ERROR: different px at row %" PRIu32 ", alpha\n", y);
                return AVIF_FALSE;
            }
            data1 += image1->alphaRowBytes;
            data2 += image2->alphaRowBytes;
        }
    }
    return AVIF_TRUE;
}

// Returns the expected number of decoded rows when availableByteCount out of byteCount were
// given to the decoder, for an image of height rows, split into cells of cellHeight rows.
static uint32_t getMinDecodedRowCount(uint32_t height, uint32_t cellHeight, avifBool hasAlpha, size_t availableByteCount, size_t byteCount)
{
    // The whole image should be available when the full input is.
    if (availableByteCount >= byteCount) {
        return height;
    }
    // All but one cell should be decoded if at most 10 bytes are missing.
    if ((availableByteCount + 10) >= byteCount) {
        return height - cellHeight;
    }

    // Subtract the header because decoding it does not output any pixel.
    // Most AVIF headers are below 500 bytes.
    if (availableByteCount <= 500) {
        return 0;
    }
    availableByteCount -= 500;
    byteCount -= 500;
    // Alpha, if any, is assumed to be located before the other planes and to
    // represent at most 50% of the payload.
    if (hasAlpha) {
        if (availableByteCount <= (byteCount / 2)) {
            return 0;
        }
        availableByteCount -= byteCount / 2;
        byteCount -= byteCount / 2;
    }
    // Linearly map the input availability ratio to the decoded row ratio.
    const uint32_t minDecodedCellRowCount = (height / cellHeight) * availableByteCount / byteCount;
    const uint32_t minDecodedPxRowCount = minDecodedCellRowCount * cellHeight;
    // One cell is the incremental decoding granularity.
    // It is unlikely that bytes are evenly distributed among cells. Offset two of them.
    if (minDecodedPxRowCount <= (2 * cellHeight)) {
        return 0;
    }
    return minDecodedPxRowCount - 2 * cellHeight;
}

//------------------------------------------------------------------------------

// Implementation of avifIOReadFunc simulating a stream from an array.
// The full array size must be in io->sizeHint and io->data must be the available avifROData.
static avifResult avifIOPartialRead(struct avifIO * io, uint32_t readFlags, uint64_t offset, size_t size, avifROData * out)
{
    const uint64_t allDataSize = io->sizeHint;
    const avifROData * availableData = (avifROData *)io->data;

    // The behavior below is described in the comment above avifIOReadFunc's declaration.
    if (readFlags != 0) {
        return AVIF_RESULT_IO_ERROR;
    }
    if (!availableData) {
        return AVIF_RESULT_IO_ERROR;
    }
    if (allDataSize < offset) {
        return AVIF_RESULT_IO_ERROR;
    }
    if (allDataSize == offset) {
        out->data = availableData->data;
        out->size = 0;
        return AVIF_RESULT_OK;
    }

    if (allDataSize < (offset + size)) {
        size = allDataSize - offset;
    }
    if (availableData->size < (offset + size)) {
        return AVIF_RESULT_WAITING_ON_IO;
    }
    out->data = availableData->data + offset;
    out->size = size;
    return AVIF_RESULT_OK;
}

//------------------------------------------------------------------------------

// Encodes the image as a grid of at most gridCols*gridRows cells. Returns AVIF_FALSE in case of error.
// The cell count is reduced to fit libavif or AVIF format constraints. If impossible, AVIF_TRUE is returned with no encoded output.
// The final cellWidth and cellHeight are output.
static avifBool encodeAsGrid(const avifImage * image, uint32_t gridCols, uint32_t gridRows, avifRWData * output, uint32_t * cellWidth, uint32_t * cellHeight)
{
    avifBool success = AVIF_FALSE;
    avifEncoder * encoder = NULL;
    avifImage ** cellImages = NULL;
    // Chroma subsampling requires even dimensions. See ISO 23000-22 - 7.3.11.4.2
    const avifBool needEvenWidths = ((image->yuvFormat == AVIF_PIXEL_FORMAT_YUV420) || (image->yuvFormat == AVIF_PIXEL_FORMAT_YUV422));
    const avifBool needEvenHeights = (image->yuvFormat == AVIF_PIXEL_FORMAT_YUV420);

    if ((gridCols == 0) || (gridRows == 0)) {
        printf("ERROR: Bad grid dimensions\n");
        return AVIF_FALSE;
    }

    *cellWidth = image->width / gridCols;
    *cellHeight = image->height / gridRows;

    // avifEncoderAddImageGrid() only accepts grids that evenly split the image
    // into cells at least 64 pixels wide and tall.
    while ((gridCols > 1) &&
           (((*cellWidth * gridCols) != image->width) || (*cellWidth < 64) || (needEvenWidths && ((*cellWidth & 1) != 0)))) {
        --gridCols;
        *cellWidth = image->width / gridCols;
    }
    while ((gridRows > 1) &&
           (((*cellHeight * gridRows) != image->height) || (*cellHeight < 64) || (needEvenHeights && ((*cellHeight & 1) != 0)))) {
        --gridRows;
        *cellHeight = image->height / gridRows;
    }

    cellImages = avifAlloc(sizeof(avifImage *) * gridCols * gridRows);
    memset(cellImages, 0, sizeof(avifImage *) * gridCols * gridRows);
    for (uint32_t row = 0, iCell = 0; row < gridRows; ++row) {
        for (uint32_t col = 0; col < gridCols; ++col, ++iCell) {
            avifCropRect cell;
            cell.x = col * *cellWidth;
            cell.y = row * *cellHeight;
            cell.width = ((cell.x + *cellWidth) <= image->width) ? *cellWidth : (image->width - cell.x);
            cell.height = ((cell.y + *cellHeight) <= image->height) ? *cellHeight : (image->height - cell.y);
            cellImages[iCell] = avifImageCreateEmpty();
            if (!cellImages[iCell] || (avifImageSetViewRect(cellImages[iCell], image, &cell) != AVIF_RESULT_OK)) {
                printf("ERROR: avifImageCreateEmpty() failed\n");
                goto cleanup;
            }
        }
    }

    encoder = avifEncoderCreate();
    if (!encoder) {
        printf("ERROR: avifEncoderCreate() failed\n");
        goto cleanup;
    }
    encoder->speed = AVIF_SPEED_FASTEST;
    if (avifEncoderAddImageGrid(encoder, gridCols, gridRows, (const avifImage * const *)cellImages, AVIF_ADD_IMAGE_FLAG_SINGLE) !=
        AVIF_RESULT_OK) {
        printf("ERROR: avifEncoderAddImageGrid() failed\n");
        goto cleanup;
    }
    if (avifEncoderFinish(encoder, output) != AVIF_RESULT_OK) {
        printf("ERROR: avifEncoderFinish() failed\n");
        goto cleanup;
    }

    success = AVIF_TRUE;
cleanup:
    if (encoder) {
        avifEncoderDestroy(encoder);
    }
    if (cellImages) {
        for (uint32_t i = 0; i < (gridCols * gridRows); ++i) {
            if (cellImages[i]) {
                avifImageDestroy(cellImages[i]);
            }
        }
        avifFree(cellImages);
    }
    return success;
}

// Encodes the image to be decoded incrementally.
static avifBool encodeAsIncremental(const avifImage * image, avifBool flatCells, avifRWData * output, uint32_t * cellWidth, uint32_t * cellHeight)
{
    const uint32_t gridCols = image->width / 64; // 64px is the min cell width.
    const uint32_t gridRows = flatCells ? 1 : (image->height / 64);
    return encodeAsGrid(image, (gridCols > 1) ? gridCols : 1, (gridRows > 1) ? gridRows : 1, output, cellWidth, cellHeight);
}

avifBool encodeRectAsIncremental(const avifImage * image,
                                 uint32_t width,
                                 uint32_t height,
                                 avifBool createAlphaIfNone,
                                 avifBool flatCells,
                                 avifRWData * output,
                                 uint32_t * cellWidth,
                                 uint32_t * cellHeight)
{
    avifBool success = AVIF_FALSE;
    avifImage * subImage = avifImageCreateEmpty();
    if (!subImage) {
        printf("ERROR: avifImageCreateEmpty() failed\n");
        goto cleanup;
    }
    if ((width > image->width) || (height > image->height)) {
        printf("ERROR: Bad dimensions\n");
        goto cleanup;
    }
    avifPixelFormatInfo info;
    avifGetPixelFormatInfo(image->yuvFormat, &info);
    avifCropRect rect;
    rect.x = ((image->width - width) / 2) & ~info.chromaShiftX;
    rect.y = ((image->height - height) / 2) & ~info.chromaShiftX;
    rect.width = width;
    rect.height = height;
    if (avifImageSetViewRect(subImage, image, &rect) != AVIF_RESULT_OK) {
        printf("ERROR: avifImageSetViewRect() failed\n");
        goto cleanup;
    }
    if (createAlphaIfNone && !subImage->alphaPlane) {
        if (!image->yuvPlanes[AVIF_CHAN_Y]) {
            printf("ERROR: No luma plane to simulate an alpha plane\n");
            goto cleanup;
        }
        subImage->alphaPlane = image->yuvPlanes[AVIF_CHAN_Y];
        subImage->alphaRowBytes = image->yuvRowBytes[AVIF_CHAN_Y];
        subImage->alphaPremultiplied = AVIF_FALSE;
        subImage->imageOwnsAlphaPlane = AVIF_FALSE;
    }
    success = encodeAsIncremental(subImage, flatCells, output, cellWidth, cellHeight);
cleanup:
    if (subImage) {
        avifImageDestroy(subImage);
    }
    return success;
}

//------------------------------------------------------------------------------

avifBool decodeNonIncrementally(const avifRWData * encodedAvif, avifImage * image)
{
    avifBool success = AVIF_FALSE;
    avifDecoder * decoder = avifDecoderCreate();
    if (!decoder || avifDecoderReadMemory(decoder, image, encodedAvif->data, encodedAvif->size) != AVIF_RESULT_OK) {
        printf("ERROR: avifDecoderReadMemory() failed\n");
        goto cleanup;
    }
    success = AVIF_TRUE;
cleanup:
    if (decoder) {
        avifDecoderDestroy(decoder);
    }
    return success;
}

avifBool decodeIncrementally(const avifRWData * encodedAvif, const avifImage * reference, uint32_t cellHeight, avifBool useNthImageApi)
{
    avifBool success = AVIF_FALSE;
    avifDecoder * decoder = NULL;
    // AVIF cells are at least 64 pixels tall.
    if ((cellHeight == 0) || ((cellHeight > reference->height) && (cellHeight != 64))) {
        printf("ERROR: cell height %" PRIu32 " is invalid\n", cellHeight);
        goto cleanup;
    }

    // Emulate a byte-by-byte stream.
    avifROData availableEncodedAvif = { encodedAvif->data, 0 };
    avifIO io = { 0 };
    io.read = avifIOPartialRead;
    io.sizeHint = encodedAvif->size;
    io.persistent = AVIF_TRUE;
    io.data = &availableEncodedAvif;

    decoder = avifDecoderCreate();
    if (!decoder) {
        printf("ERROR: avifDecoderCreate() failed\n");
        goto cleanup;
    }
    avifDecoderSetIO(decoder, &io);
    decoder->allowIncremental = AVIF_TRUE;

    // Parsing is not incremental.
    avifResult parseResult = avifDecoderParse(decoder);
    while (parseResult == AVIF_RESULT_WAITING_ON_IO) {
        if (availableEncodedAvif.size >= encodedAvif->size) {
            printf("ERROR: avifDecoderParse() returned WAITING_ON_IO instead of OK\n");
            goto cleanup;
        }
        availableEncodedAvif.size = availableEncodedAvif.size + 1;
        parseResult = avifDecoderParse(decoder);
    }
    if (parseResult != AVIF_RESULT_OK) {
        printf("ERROR: avifDecoderParse() failed (%s)\n", avifResultToString(parseResult));
        goto cleanup;
    }

    // Decoding is incremental.
    uint32_t previouslyDecodedRowCount = 0;
    avifResult nextImageResult = useNthImageApi ? avifDecoderNthImage(decoder, 0) : avifDecoderNextImage(decoder);
    while (nextImageResult == AVIF_RESULT_WAITING_ON_IO) {
        if (availableEncodedAvif.size >= encodedAvif->size) {
            printf("ERROR: avifDecoderNextImage() or avifDecoderNthImage(0) returned WAITING_ON_IO instead of OK\n");
            goto cleanup;
        }
        const uint32_t decodedRowCount = avifDecoderDecodedRowCount(decoder);
        if (decodedRowCount < previouslyDecodedRowCount) {
            printf("ERROR: %" PRIu32 " decoded rows decreased to %" PRIu32 "\n", previouslyDecodedRowCount, decodedRowCount);
            goto cleanup;
        }
        const uint32_t minDecodedRowCount = getMinDecodedRowCount(reference->height,
                                                                  cellHeight,
                                                                  reference->alphaPlane != NULL,
                                                                  availableEncodedAvif.size,
                                                                  encodedAvif->size);
        if (decodedRowCount < minDecodedRowCount) {
            printf("ERROR: %" PRIu32 " is fewer than %" PRIu32 " decoded rows\n", decodedRowCount, minDecodedRowCount);
            goto cleanup;
        }
        if (!comparePartialYUVA(reference, decoder->image, decodedRowCount)) {
            goto cleanup;
        }

        previouslyDecodedRowCount = decodedRowCount;
        availableEncodedAvif.size = availableEncodedAvif.size + 1;
        nextImageResult = useNthImageApi ? avifDecoderNthImage(decoder, 0) : avifDecoderNextImage(decoder);
    }
    if (nextImageResult != AVIF_RESULT_OK) {
        printf("ERROR: avifDecoderNextImage() or avifDecoderNthImage(0) failed (%s)\n", avifResultToString(nextImageResult));
        goto cleanup;
    }
    if (availableEncodedAvif.size != encodedAvif->size) {
        printf("ERROR: not all bytes were read\n");
        goto cleanup;
    }
    if (avifDecoderDecodedRowCount(decoder) != decoder->image->height) {
        printf("ERROR: avifDecoderDecodedRowCount() should be decoder->image->height after OK\n");
        goto cleanup;
    }

    if (!comparePartialYUVA(reference, decoder->image, reference->height)) {
        goto cleanup;
    }
    success = AVIF_TRUE;
cleanup:
    if (decoder) {
        avifDecoderDestroy(decoder);
    }
    return success;
}

avifBool decodeNonIncrementallyAndIncrementally(const avifRWData * encodedAvif, uint32_t cellHeight, avifBool useNthImageApi)
{
    avifBool success = AVIF_FALSE;
    avifImage * reference = avifImageCreateEmpty();
    if (!reference) {
        goto cleanup;
    }
    if (!decodeNonIncrementally(encodedAvif, reference)) {
        goto cleanup;
    }
    if (!decodeIncrementally(encodedAvif, reference, cellHeight, useNthImageApi)) {
        goto cleanup;
    }
    success = AVIF_TRUE;
cleanup:
    if (reference) {
        avifImageDestroy(reference);
    }
    return success;
}

//------------------------------------------------------------------------------
