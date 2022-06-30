// Copyright 2022 Google LLC. All rights reserved.
// SPDX-License-Identifier: BSD-2-Clause

#include "avifincrtest_helpers.h"

#include <algorithm>
#include <cstring>
#include <vector>

#include "avif/avif.h"
#include "aviftest_helpers.h"
#include "gtest/gtest.h"

namespace libavif {
namespace testutil {
namespace {

//------------------------------------------------------------------------------

// Verifies that the first (top) rowCount rows of image1 and image2 are
// identical.
void comparePartialYUVA(const avifImage &image1, const avifImage &image2,
                        uint32_t rowCount) {
  if (rowCount == 0) {
    return;
  }
  ASSERT_EQ(image1.width, image2.width);
  ASSERT_GE(image1.height, rowCount);
  ASSERT_GE(image2.height, rowCount);
  ASSERT_EQ(image1.depth, image2.depth);
  ASSERT_EQ(image1.yuvFormat, image2.yuvFormat);
  ASSERT_EQ(image1.yuvRange, image2.yuvRange);

  avifPixelFormatInfo info;
  avifGetPixelFormatInfo(image1.yuvFormat, &info);
  const uint32_t uvWidth =
      (image1.width + info.chromaShiftX) >> info.chromaShiftX;
  const uint32_t uvHeight = (rowCount + info.chromaShiftY) >> info.chromaShiftY;
  const uint32_t pixelByteCount =
      (image1.depth > 8) ? sizeof(uint16_t) : sizeof(uint8_t);

  for (uint32_t plane = 0; plane < (info.monochrome ? 1 : AVIF_PLANE_COUNT_YUV);
       ++plane) {
    const uint32_t width = (plane == AVIF_CHAN_Y) ? image1.width : uvWidth;
    const uint32_t widthByteCount = width * pixelByteCount;
    const uint32_t height = (plane == AVIF_CHAN_Y) ? rowCount : uvHeight;
    const uint8_t *data1 = image1.yuvPlanes[plane];
    const uint8_t *data2 = image2.yuvPlanes[plane];
    for (uint32_t y = 0; y < height; ++y) {
      ASSERT_EQ(std::memcmp(data1, data2, widthByteCount), 0);
      data1 += image1.yuvRowBytes[plane];
      data2 += image2.yuvRowBytes[plane];
    }
  }

  if (image1.alphaPlane) {
    ASSERT_NE(image2.alphaPlane, nullptr);
    ASSERT_EQ(image1.alphaPremultiplied, image2.alphaPremultiplied);
    const uint32_t widthByteCount = image1.width * pixelByteCount;
    const uint8_t *data1 = image1.alphaPlane;
    const uint8_t *data2 = image2.alphaPlane;
    for (uint32_t y = 0; y < rowCount; ++y) {
      ASSERT_EQ(std::memcmp(data1, data2, widthByteCount), 0);
      data1 += image1.alphaRowBytes;
      data2 += image2.alphaRowBytes;
    }
  }
}

// Returns the expected number of decoded rows when availableByteCount out of
// byteCount were given to the decoder, for an image of height rows, split into
// cells of cellHeight rows.
uint32_t getMinDecodedRowCount(uint32_t height, uint32_t cellHeight,
                               bool hasAlpha, size_t availableByteCount,
                               size_t byteCount) {
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
  const uint32_t minDecodedCellRowCount =
      (height / cellHeight) * availableByteCount / byteCount;
  const uint32_t minDecodedPxRowCount = minDecodedCellRowCount * cellHeight;
  // One cell is the incremental decoding granularity.
  // It is unlikely that bytes are evenly distributed among cells. Offset two of
  // them.
  if (minDecodedPxRowCount <= (2 * cellHeight)) {
    return 0;
  }
  return minDecodedPxRowCount - 2 * cellHeight;
}

//------------------------------------------------------------------------------

struct avifROPartialData {
  avifROData available;
  size_t fullSize;
};

// Implementation of avifIOReadFunc simulating a stream from an array. See
// avifIOReadFunc documentation. io->data is expected to point to
// avifROPartialData.
avifResult avifIOPartialRead(struct avifIO *io, uint32_t readFlags,
                             uint64_t offset, size_t size, avifROData *out) {
  const avifROPartialData *data = (avifROPartialData *)io->data;
  if ((readFlags != 0) || !data || (data->fullSize < offset)) {
    return AVIF_RESULT_IO_ERROR;
  }
  if (data->fullSize < (offset + size)) {
    size = data->fullSize - offset;
  }
  if (data->available.size < (offset + size)) {
    return AVIF_RESULT_WAITING_ON_IO;
  }
  out->data = data->available.data + offset;
  out->size = size;
  return AVIF_RESULT_OK;
}

//------------------------------------------------------------------------------

// Encodes the image as a grid of at most gridCols*gridRows cells.
// The cell count is reduced to fit libavif or AVIF format constraints. If
// impossible, the encoded output is returned empty. The final cellWidth and
// cellHeight are output.
void encodeAsGrid(const avifImage &image, uint32_t gridCols, uint32_t gridRows,
                  avifRWData *output, uint32_t *cellWidth,
                  uint32_t *cellHeight) {
  // Chroma subsampling requires even dimensions. See ISO 23000-22 - 7.3.11.4.2
  const bool needEvenWidths = ((image.yuvFormat == AVIF_PIXEL_FORMAT_YUV420) ||
                               (image.yuvFormat == AVIF_PIXEL_FORMAT_YUV422));
  const bool needEvenHeights = (image.yuvFormat == AVIF_PIXEL_FORMAT_YUV420);

  ASSERT_GT(gridCols * gridRows, 0u);
  *cellWidth = image.width / gridCols;
  *cellHeight = image.height / gridRows;

  // avifEncoderAddImageGrid() only accepts grids that evenly split the image
  // into cells at least 64 pixels wide and tall.
  while ((gridCols > 1) &&
         (((*cellWidth * gridCols) != image.width) || (*cellWidth < 64) ||
          (needEvenWidths && ((*cellWidth & 1) != 0)))) {
    --gridCols;
    *cellWidth = image.width / gridCols;
  }
  while ((gridRows > 1) &&
         (((*cellHeight * gridRows) != image.height) || (*cellHeight < 64) ||
          (needEvenHeights && ((*cellHeight & 1) != 0)))) {
    --gridRows;
    *cellHeight = image.height / gridRows;
  }

  std::vector<testutil::avifImagePtr> cellImages;
  cellImages.reserve(gridCols * gridRows);
  for (uint32_t row = 0, iCell = 0; row < gridRows; ++row) {
    for (uint32_t col = 0; col < gridCols; ++col, ++iCell) {
      avifCropRect cell;
      cell.x = col * *cellWidth;
      cell.y = row * *cellHeight;
      cell.width = ((cell.x + *cellWidth) <= image.width)
                       ? *cellWidth
                       : (image.width - cell.x);
      cell.height = ((cell.y + *cellHeight) <= image.height)
                        ? *cellHeight
                        : (image.height - cell.y);
      cellImages.emplace_back(avifImageCreateEmpty(), avifImageDestroy);
      ASSERT_NE(cellImages.back(), nullptr);
      ASSERT_EQ(avifImageSetViewRect(cellImages.back().get(), &image, &cell),
                AVIF_RESULT_OK);
    }
  }

  testutil::avifEncoderPtr encoder(avifEncoderCreate(), avifEncoderDestroy);
  ASSERT_NE(encoder, nullptr);
  encoder->speed = AVIF_SPEED_FASTEST;
  // Just here to match libavif API.
  std::vector<avifImage *> cellImagePtrs(cellImages.size());
  for (size_t i = 0; i < cellImages.size(); ++i) {
    cellImagePtrs[i] = cellImages[i].get();
  }
  ASSERT_EQ(
      avifEncoderAddImageGrid(encoder.get(), gridCols, gridRows,
                              cellImagePtrs.data(), AVIF_ADD_IMAGE_FLAG_SINGLE),
      AVIF_RESULT_OK);
  ASSERT_EQ(avifEncoderFinish(encoder.get(), output), AVIF_RESULT_OK);
}

// Encodes the image to be decoded incrementally.
void encodeAsIncremental(const avifImage &image, bool flatCells,
                         avifRWData *output, uint32_t *cellWidth,
                         uint32_t *cellHeight) {
  const uint32_t gridCols = image.width / 64;  // 64px is the min cell width.
  const uint32_t gridRows = flatCells ? 1 : (image.height / 64);
  encodeAsGrid(image, (gridCols > 1) ? gridCols : 1,
               (gridRows > 1) ? gridRows : 1, output, cellWidth, cellHeight);
}

}  // namespace

void encodeRectAsIncremental(const avifImage &image, uint32_t width,
                             uint32_t height, bool createAlphaIfNone,
                             bool flatCells, avifRWData *output,
                             uint32_t *cellWidth, uint32_t *cellHeight) {
  avifImagePtr subImage(avifImageCreateEmpty(), avifImageDestroy);
  ASSERT_NE(subImage, nullptr);
  ASSERT_LE(width, image.width);
  ASSERT_LE(height, image.height);
  avifPixelFormatInfo info;
  avifGetPixelFormatInfo(image.yuvFormat, &info);
  const avifCropRect rect{
      /*x=*/((image.width - width) / 2) & ~info.chromaShiftX,
      /*y=*/((image.height - height) / 2) & ~info.chromaShiftX, width, height};
  ASSERT_EQ(avifImageSetViewRect(subImage.get(), &image, &rect),
            AVIF_RESULT_OK);
  if (createAlphaIfNone && !subImage->alphaPlane) {
    ASSERT_NE(image.yuvPlanes[AVIF_CHAN_Y], nullptr)
        << "No luma plane to simulate an alpha plane";
    subImage->alphaPlane = image.yuvPlanes[AVIF_CHAN_Y];
    subImage->alphaRowBytes = image.yuvRowBytes[AVIF_CHAN_Y];
    subImage->alphaPremultiplied = AVIF_FALSE;
    subImage->imageOwnsAlphaPlane = AVIF_FALSE;
  }
  encodeAsIncremental(*subImage, flatCells, output, cellWidth, cellHeight);
}

//------------------------------------------------------------------------------

void decodeIncrementally(const avifRWData &encodedAvif, bool isPersistent,
                         bool giveSizeHint, bool useNthImageApi,
                         const avifImage &reference, uint32_t cellHeight) {
  // AVIF cells are at least 64 pixels tall.
  if (cellHeight != reference.height) {
    ASSERT_GE(cellHeight, 64u);
  }

  // Emulate a byte-by-byte stream.
  avifROPartialData data = {/*available=*/{encodedAvif.data, 0},
                            /*fullSize=*/encodedAvif.size};
  avifIO io = {
      /*destroy=*/nullptr, avifIOPartialRead,
      /*write=*/nullptr,   giveSizeHint ? encodedAvif.size : 0,
      isPersistent,        &data};

  testutil::avifDecoderPtr decoder(avifDecoderCreate(), avifDecoderDestroy);
  ASSERT_NE(decoder, nullptr);
  avifDecoderSetIO(decoder.get(), &io);
  decoder->allowIncremental = AVIF_TRUE;
  const size_t step = std::max(static_cast<size_t>(1), data.fullSize / 10000);

  // Parsing is not incremental.
  avifResult parseResult = avifDecoderParse(decoder.get());
  while (parseResult == AVIF_RESULT_WAITING_ON_IO) {
    ASSERT_LT(data.available.size, data.fullSize)
        << "avifDecoderParse() returned WAITING_ON_IO instead of OK";
    data.available.size = std::min(data.available.size + step, data.fullSize);
    parseResult = avifDecoderParse(decoder.get());
  }
  ASSERT_EQ(parseResult, AVIF_RESULT_OK);

  // Decoding is incremental.
  uint32_t previouslyDecodedRowCount = 0;
  avifResult nextImageResult = useNthImageApi
                                   ? avifDecoderNthImage(decoder.get(), 0)
                                   : avifDecoderNextImage(decoder.get());
  while (nextImageResult == AVIF_RESULT_WAITING_ON_IO) {
    ASSERT_LT(data.available.size, data.fullSize)
        << (useNthImageApi ? "avifDecoderNthImage(0)"
                           : "avifDecoderNextImage()")
        << " returned WAITING_ON_IO instead of OK";
    const uint32_t decodedRowCount = avifDecoderDecodedRowCount(decoder.get());
    ASSERT_GE(decodedRowCount, previouslyDecodedRowCount);
    const uint32_t minDecodedRowCount = getMinDecodedRowCount(
        reference.height, cellHeight, reference.alphaPlane != nullptr,
        data.available.size, data.fullSize);
    ASSERT_GE(decodedRowCount, minDecodedRowCount);
    comparePartialYUVA(reference, *decoder->image, decodedRowCount);

    previouslyDecodedRowCount = decodedRowCount;
    data.available.size = std::min(data.available.size + step, data.fullSize);
    nextImageResult = useNthImageApi ? avifDecoderNthImage(decoder.get(), 0)
                                     : avifDecoderNextImage(decoder.get());
  }
  ASSERT_EQ(nextImageResult, AVIF_RESULT_OK);
  ASSERT_EQ(data.available.size, data.fullSize);
  ASSERT_EQ(avifDecoderDecodedRowCount(decoder.get()), decoder->image->height);

  comparePartialYUVA(reference, *decoder->image, reference.height);
}

void decodeNonIncrementallyAndIncrementally(const avifRWData &encodedAvif,
                                            bool isPersistent,
                                            bool giveSizeHint,
                                            bool useNthImageApi,
                                            uint32_t cellHeight) {
  avifImagePtr reference(avifImageCreateEmpty(), avifImageDestroy);
  ASSERT_NE(reference, nullptr);
  testutil::avifDecoderPtr decoder(avifDecoderCreate(), avifDecoderDestroy);
  ASSERT_NE(decoder, nullptr);
  ASSERT_EQ(avifDecoderReadMemory(decoder.get(), reference.get(),
                                  encodedAvif.data, encodedAvif.size),
            AVIF_RESULT_OK);

  decodeIncrementally(encodedAvif, isPersistent, giveSizeHint, useNthImageApi,
                      *reference, cellHeight);
}

//------------------------------------------------------------------------------

}  // namespace testutil
}  // namespace libavif
