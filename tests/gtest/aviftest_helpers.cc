// Copyright 2022 Google LLC
// SPDX-License-Identifier: BSD-2-Clause

#include "aviftest_helpers.h"

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <string>

#include "avif/avif.h"
#include "avifutil.h"

namespace libavif {
namespace testutil {

//------------------------------------------------------------------------------

AvifRgbImage::AvifRgbImage(const avifImage* yuv, int rgbDepth,
                           avifRGBFormat rgbFormat) {
  avifRGBImageSetDefaults(this, yuv);
  depth = rgbDepth;
  format = rgbFormat;
  avifRGBImageAllocatePixels(this);
}

AvifRwData::AvifRwData(AvifRwData&& other) : avifRWData{other} {
  other.data = nullptr;
  other.size = 0;
}

//------------------------------------------------------------------------------

RgbChannelOffsets GetRgbChannelOffsets(avifRGBFormat format) {
  switch (format) {
    case AVIF_RGB_FORMAT_RGB:
      return {/*r=*/0, /*g=*/1, /*b=*/2, /*a=*/0};
    case AVIF_RGB_FORMAT_RGBA:
      return {/*r=*/0, /*g=*/1, /*b=*/2, /*a=*/3};
    case AVIF_RGB_FORMAT_ARGB:
      return {/*r=*/1, /*g=*/2, /*b=*/3, /*a=*/0};
    case AVIF_RGB_FORMAT_BGR:
      return {/*r=*/2, /*g=*/1, /*b=*/0, /*a=*/0};
    case AVIF_RGB_FORMAT_BGRA:
      return {/*r=*/2, /*g=*/1, /*b=*/0, /*a=*/3};
    case AVIF_RGB_FORMAT_ABGR:
      return {/*r=*/3, /*g=*/2, /*b=*/1, /*a=*/0};
    case AVIF_RGB_FORMAT_RGB_565:
    case AVIF_RGB_FORMAT_COUNT:
    default:
      return {/*r=*/0, /*g=*/0, /*b=*/0, /*a=*/0};
  }
}

//------------------------------------------------------------------------------

AvifImagePtr CreateImage(int width, int height, int depth,
                         avifPixelFormat yuv_format, avifPlanesFlags planes,
                         avifRange yuv_range) {
  AvifImagePtr image(avifImageCreate(width, height, depth, yuv_format),
                     avifImageDestroy);
  if (!image) {
    return {nullptr, nullptr};
  }
  image->yuvRange = yuv_range;
  if (avifImageAllocatePlanes(image.get(), planes) != AVIF_RESULT_OK) {
    return {nullptr, nullptr};
  }
  return image;
}

void FillImagePlain(avifImage* image, const uint32_t yuva[4]) {
  for (avifChannelIndex c :
       {AVIF_CHAN_Y, AVIF_CHAN_U, AVIF_CHAN_V, AVIF_CHAN_A}) {
    const uint32_t plane_width = avifImagePlaneWidth(image, c);
    // 0 for A if no alpha and 0 for UV if 4:0:0.
    const uint32_t plane_height = avifImagePlaneHeight(image, c);
    uint8_t* row = avifImagePlane(image, c);
    const uint32_t row_bytes = avifImagePlaneRowBytes(image, c);
    for (uint32_t y = 0; y < plane_height; ++y) {
      if (avifImageUsesU16(image)) {
        std::fill(reinterpret_cast<uint16_t*>(row),
                  reinterpret_cast<uint16_t*>(row) + plane_width,
                  static_cast<uint16_t>(yuva[c]));
      } else {
        std::fill(row, row + plane_width, static_cast<uint8_t>(yuva[c]));
      }
      row += row_bytes;
    }
  }
}

void FillImageGradient(avifImage* image) {
  assert(image->yuvRange == AVIF_RANGE_FULL);
  for (avifChannelIndex c :
       {AVIF_CHAN_Y, AVIF_CHAN_U, AVIF_CHAN_V, AVIF_CHAN_A}) {
    const uint32_t plane_width = avifImagePlaneWidth(image, c);
    // 0 for A if no alpha and 0 for UV if 4:0:0.
    const uint32_t plane_height = avifImagePlaneHeight(image, c);
    uint8_t* row = avifImagePlane(image, c);
    const uint32_t row_bytes = avifImagePlaneRowBytes(image, c);
    for (uint32_t y = 0; y < plane_height; ++y) {
      for (uint32_t x = 0; x < plane_width; ++x) {
        const uint32_t value = (x + y) * ((1u << image->depth) - 1u) /
                               std::max(1u, plane_width + plane_height - 2);
        if (avifImageUsesU16(image)) {
          reinterpret_cast<uint16_t*>(row)[x] = static_cast<uint16_t>(value);
        } else {
          row[x] = static_cast<uint8_t>(value);
        }
      }
      row += row_bytes;
    }
  }
}

namespace {
template <typename PixelType>
void FillImageChannel(avifRGBImage* image, uint32_t channel_offset,
                      uint32_t value) {
  const uint32_t channel_count = avifRGBFormatChannelCount(image->format);
  assert(channel_offset < channel_count);
  for (uint32_t y = 0; y < image->height; ++y) {
    PixelType* pixel =
        reinterpret_cast<PixelType*>(image->pixels + image->rowBytes * y);
    for (uint32_t x = 0; x < image->width; ++x) {
      pixel[channel_offset] = static_cast<PixelType>(value);
      pixel += channel_count;
    }
  }
}
}  // namespace

void FillImageChannel(avifRGBImage* image, uint32_t channel_offset,
                      uint32_t value) {
  (image->depth <= 8)
      ? FillImageChannel<uint8_t>(image, channel_offset, value)
      : FillImageChannel<uint16_t>(image, channel_offset, value);
}

//------------------------------------------------------------------------------

bool AreByteSequencesEqual(const uint8_t data1[], size_t data1_length,
                           const uint8_t data2[], size_t data2_length) {
  if (data1_length != data2_length) return false;
  return data1_length == 0 || std::equal(data1, data1 + data1_length, data2);
}

bool AreByteSequencesEqual(const avifRWData& data1, const avifRWData& data2) {
  return AreByteSequencesEqual(data1.data, data1.size, data2.data, data2.size);
}

// Returns true if image1 and image2 are identical.
bool AreImagesEqual(const avifImage& image1, const avifImage& image2,
                    bool ignore_alpha) {
  if (image1.width != image2.width || image1.height != image2.height ||
      image1.depth != image2.depth || image1.yuvFormat != image2.yuvFormat ||
      image1.yuvRange != image2.yuvRange) {
    return false;
  }
  assert(image1.width * image1.height > 0);

  for (avifChannelIndex c :
       {AVIF_CHAN_Y, AVIF_CHAN_U, AVIF_CHAN_V, AVIF_CHAN_A}) {
    if (ignore_alpha && c == AVIF_CHAN_A) continue;
    const uint8_t* row1 = avifImagePlane(&image1, c);
    const uint8_t* row2 = avifImagePlane(&image2, c);
    if (!row1 != !row2) {
      // Maybe one image contains an opaque alpha channel while the other has no
      // alpha channel, but they should still be considered equal.
      if (c == AVIF_CHAN_A && avifImageIsOpaque(&image1) &&
          avifImageIsOpaque(&image2)) {
        continue;
      }
      return false;
    }
    const uint32_t row_bytes1 = avifImagePlaneRowBytes(&image1, c);
    const uint32_t row_bytes2 = avifImagePlaneRowBytes(&image2, c);
    const uint32_t plane_width = avifImagePlaneWidth(&image1, c);
    // 0 for A if no alpha and 0 for UV if 4:0:0.
    const uint32_t plane_height = avifImagePlaneHeight(&image1, c);
    for (uint32_t y = 0; y < plane_height; ++y) {
      if (avifImageUsesU16(&image1)) {
        if (!std::equal(reinterpret_cast<const uint16_t*>(row1),
                        reinterpret_cast<const uint16_t*>(row1) + plane_width,
                        reinterpret_cast<const uint16_t*>(row2))) {
          return false;
        }
      } else {
        if (!std::equal(row1, row1 + plane_width, row2)) {
          return false;
        }
      }
      row1 += row_bytes1;
      row2 += row_bytes2;
    }
  }
  return AreByteSequencesEqual(image1.icc, image2.icc) &&
         AreByteSequencesEqual(image1.exif, image2.exif) &&
         AreByteSequencesEqual(image1.xmp, image2.xmp);
}

void CopyImageSamples(const avifImage& from, avifImage* to) {
  assert(from.width == to->width);
  assert(from.height == to->height);
  assert(from.depth == to->depth);
  assert(from.yuvFormat == to->yuvFormat);
  assert(from.yuvRange == to->yuvRange);

  for (avifChannelIndex c :
       {AVIF_CHAN_Y, AVIF_CHAN_U, AVIF_CHAN_V, AVIF_CHAN_A}) {
    const uint8_t* from_row = avifImagePlane(&from, c);
    uint8_t* to_row = avifImagePlane(to, c);
    assert(!from_row == !to_row);
    const uint32_t from_row_bytes = avifImagePlaneRowBytes(&from, c);
    const uint32_t to_row_bytes = avifImagePlaneRowBytes(to, c);
    const uint32_t plane_width = avifImagePlaneWidth(&from, c);
    // 0 for A if no alpha and 0 for UV if 4:0:0.
    const uint32_t plane_height = avifImagePlaneHeight(&from, c);
    for (uint32_t y = 0; y < plane_height; ++y) {
      if (avifImageUsesU16(&from)) {
        std::copy(reinterpret_cast<const uint16_t*>(from_row),
                  reinterpret_cast<const uint16_t*>(from_row) + plane_width,
                  reinterpret_cast<uint16_t*>(to_row));
      } else {
        std::copy(from_row, from_row + plane_width, to_row);
      }
      from_row += from_row_bytes;
      to_row += to_row_bytes;
    }
  }
}

//------------------------------------------------------------------------------

AvifImagePtr ReadImage(const char* folder_path, const char* file_name,
                       avifPixelFormat requested_format, int requested_depth,
                       avifChromaDownsampling chroma_downsampling,
                       avifBool ignore_icc, avifBool ignore_exif,
                       avifBool ignore_xmp) {
  testutil::AvifImagePtr image(avifImageCreateEmpty(), avifImageDestroy);
  if (!image ||
      avifReadImage((std::string(folder_path) + file_name).c_str(),
                    requested_format, requested_depth, chroma_downsampling,
                    ignore_icc, ignore_exif, ignore_xmp, image.get(),
                    /*outDepth=*/nullptr, /*sourceTiming=*/nullptr,
                    /*frameIter=*/nullptr) == AVIF_APP_FILE_FORMAT_UNKNOWN) {
    return {nullptr, nullptr};
  }
  return image;
}

AvifRwData Encode(const avifImage* image, int speed) {
  testutil::AvifEncoderPtr encoder(avifEncoderCreate(), avifEncoderDestroy);
  if (!encoder) return {};
  encoder->speed = speed;
  testutil::AvifRwData bytes;
  if (avifEncoderWrite(encoder.get(), image, &bytes) != AVIF_RESULT_OK) {
    return {};
  }
  return bytes;
}

AvifImagePtr Decode(const uint8_t* bytes, size_t num_bytes) {
  testutil::AvifImagePtr decoded(avifImageCreateEmpty(), avifImageDestroy);
  testutil::AvifDecoderPtr decoder(avifDecoderCreate(), avifDecoderDestroy);
  if (!decoded || !decoder ||
      (avifDecoderReadMemory(decoder.get(), decoded.get(), bytes, num_bytes) !=
       AVIF_RESULT_OK)) {
    return {nullptr, nullptr};
  }
  return decoded;
}

//------------------------------------------------------------------------------

static avifResult avifIOLimitedReaderRead(avifIO* io, uint32_t readFlags,
                                          uint64_t offset, size_t size,
                                          avifROData* out) {
  auto reader = reinterpret_cast<AvifIOLimitedReader*>(io);

  if (offset > UINT64_MAX - size) {
    return AVIF_RESULT_IO_ERROR;
  }
  if (offset + size > reader->clamp) {
    return AVIF_RESULT_WAITING_ON_IO;
  }

  return reader->underlyingIO->read(reader->underlyingIO, readFlags, offset,
                                    size, out);
}

static void avifIOLimitedReaderDestroy(avifIO* io) {
  auto reader = reinterpret_cast<AvifIOLimitedReader*>(io);
  reader->underlyingIO->destroy(reader->underlyingIO);
  delete reader;
}

avifIO* AvifIOCreateLimitedReader(avifIO* underlyingIO, uint64_t clamp) {
  return reinterpret_cast<avifIO*>(
      new AvifIOLimitedReader{{
                                  avifIOLimitedReaderDestroy,
                                  avifIOLimitedReaderRead,
                                  nullptr,
                                  underlyingIO->sizeHint,
                                  underlyingIO->persistent,
                                  nullptr,
                              },
                              underlyingIO,
                              clamp});
}

//------------------------------------------------------------------------------

}  // namespace testutil
}  // namespace libavif
