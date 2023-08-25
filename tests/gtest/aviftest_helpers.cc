// Copyright 2022 Google LLC
// SPDX-License-Identifier: BSD-2-Clause

#include "aviftest_helpers.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "avif/avif.h"
#include "avifpng.h"
#include "avifutil.h"

namespace libavif {
namespace testutil {

//------------------------------------------------------------------------------

AvifRgbImage::AvifRgbImage(const avifImage* yuv, int rgbDepth,
                           avifRGBFormat rgbFormat) {
  avifRGBImageSetDefaults(this, yuv);
  depth = rgbDepth;
  format = rgbFormat;
  if (avifRGBImageAllocatePixels(this) != AVIF_RESULT_OK) {
    std::abort();
  }
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
  for (avifChannelIndex c :
       {AVIF_CHAN_Y, AVIF_CHAN_U, AVIF_CHAN_V, AVIF_CHAN_A}) {
    const uint32_t limitedRangeMin =
        c == AVIF_CHAN_Y ? 16 << (image->depth - 8) : 0;
    const uint32_t limitedRangeMax = (c == AVIF_CHAN_Y ? 219 : 224)
                                     << (image->depth - 8);

    const uint32_t plane_width = avifImagePlaneWidth(image, c);
    // 0 for A if no alpha and 0 for UV if 4:0:0.
    const uint32_t plane_height = avifImagePlaneHeight(image, c);
    uint8_t* row = avifImagePlane(image, c);
    const uint32_t row_bytes = avifImagePlaneRowBytes(image, c);
    for (uint32_t y = 0; y < plane_height; ++y) {
      for (uint32_t x = 0; x < plane_width; ++x) {
        uint32_t value;
        if (image->yuvRange == AVIF_RANGE_FULL || c == AVIF_CHAN_A) {
          value = (x + y) * ((1u << image->depth) - 1u) /
                  std::max(1u, plane_width + plane_height - 2);
        } else {
          value = limitedRangeMin +
                  (x + y) * (limitedRangeMax - limitedRangeMin) /
                      std::max(1u, plane_width + plane_height - 2);
        }
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

  if (image1.clli.maxCLL != image2.clli.maxCLL ||
      image1.clli.maxPALL != image2.clli.maxPALL) {
    return false;
  }
  if (image1.transformFlags != image2.transformFlags ||
      ((image1.transformFlags & AVIF_TRANSFORM_PASP) &&
       std::memcmp(&image1.pasp, &image2.pasp, sizeof(image1.pasp))) ||
      ((image1.transformFlags & AVIF_TRANSFORM_CLAP) &&
       std::memcmp(&image1.clap, &image2.clap, sizeof(image1.clap))) ||
      ((image1.transformFlags & AVIF_TRANSFORM_IROT) &&
       std::memcmp(&image1.irot, &image2.irot, sizeof(image1.irot))) ||
      ((image1.transformFlags & AVIF_TRANSFORM_IMIR) &&
       std::memcmp(&image1.imir, &image2.imir, sizeof(image1.imir)))) {
    return false;
  }

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

namespace {

template <typename Sample>
uint64_t SquaredDiffSum(const Sample* samples1, const Sample* samples2,
                        uint32_t num_samples) {
  uint64_t sum = 0;
  for (uint32_t i = 0; i < num_samples; ++i) {
    const int32_t diff = static_cast<int32_t>(samples1[i]) - samples2[i];
    sum += diff * diff;
  }
  return sum;
}

}  // namespace

double GetPsnr(const avifImage& image1, const avifImage& image2,
               bool ignore_alpha) {
  if (image1.width != image2.width || image1.height != image2.height ||
      image1.depth != image2.depth || image1.yuvFormat != image2.yuvFormat ||
      image1.yuvRange != image2.yuvRange) {
    return -1;
  }
  assert(image1.width * image1.height > 0);

  uint64_t squared_diff_sum = 0;
  uint32_t num_samples = 0;
  const uint32_t max_sample_value = (1 << image1.depth) - 1;
  for (avifChannelIndex c :
       {AVIF_CHAN_Y, AVIF_CHAN_U, AVIF_CHAN_V, AVIF_CHAN_A}) {
    if (ignore_alpha && c == AVIF_CHAN_A) continue;

    const uint32_t plane_width = std::max(avifImagePlaneWidth(&image1, c),
                                          avifImagePlaneWidth(&image2, c));
    const uint32_t plane_height = std::max(avifImagePlaneHeight(&image1, c),
                                           avifImagePlaneHeight(&image2, c));
    if (plane_width == 0 || plane_height == 0) continue;

    const uint8_t* row1 = avifImagePlane(&image1, c);
    const uint8_t* row2 = avifImagePlane(&image2, c);
    if (!row1 != !row2 && c != AVIF_CHAN_A) {
      return -1;
    }
    uint32_t row_bytes1 = avifImagePlaneRowBytes(&image1, c);
    uint32_t row_bytes2 = avifImagePlaneRowBytes(&image2, c);

    // Consider missing alpha planes as samples set to the maximum value.
    std::vector<uint8_t> opaque_alpha_samples;
    if (!row1 != !row2) {
      opaque_alpha_samples.resize(std::max(row_bytes1, row_bytes2));
      if (avifImageUsesU16(&image1)) {
        uint16_t* opaque_alpha_samples_16b =
            reinterpret_cast<uint16_t*>(opaque_alpha_samples.data());
        std::fill(opaque_alpha_samples_16b,
                  opaque_alpha_samples_16b + plane_width,
                  static_cast<int16_t>(max_sample_value));
      } else {
        std::fill(opaque_alpha_samples.begin(), opaque_alpha_samples.end(),
                  uint8_t{255});
      }
      if (!row1) {
        row1 = opaque_alpha_samples.data();
        row_bytes1 = 0;
      } else {
        row2 = opaque_alpha_samples.data();
        row_bytes2 = 0;
      }
    }

    for (uint32_t y = 0; y < plane_height; ++y) {
      if (avifImageUsesU16(&image1)) {
        squared_diff_sum += SquaredDiffSum(
            reinterpret_cast<const uint16_t*>(row1),
            reinterpret_cast<const uint16_t*>(row2), plane_width);
      } else {
        squared_diff_sum += SquaredDiffSum(row1, row2, plane_width);
      }
      row1 += row_bytes1;
      row2 += row_bytes2;
      num_samples += plane_width;
    }
  }

  if (squared_diff_sum == 0) {
    return 99.0;
  }
  const double normalized_error =
      squared_diff_sum /
      (static_cast<double>(num_samples) * max_sample_value * max_sample_value);
  if (normalized_error <= std::numeric_limits<double>::epsilon()) {
    return 98.99;  // Very small distortion but not lossless.
  }
  return std::min(-10 * std::log10(normalized_error), 98.99);
}

bool AreImagesEqual(const avifRGBImage& image1, const avifRGBImage& image2) {
  if (image1.width != image2.width || image1.height != image2.height ||
      image1.depth != image2.depth || image1.format != image2.format ||
      image1.alphaPremultiplied != image2.alphaPremultiplied ||
      image1.isFloat != image2.isFloat) {
    return false;
  }
  const uint8_t* row1 = image1.pixels;
  const uint8_t* row2 = image2.pixels;
  const unsigned int row_width = image1.width * avifRGBImagePixelSize(&image1);
  for (unsigned int y = 0; y < image1.height; ++y) {
    if (!std::equal(row1, row1 + row_width, row2)) {
      return false;
    }
    row1 += image1.rowBytes;
    row2 += image2.rowBytes;
  }
  return true;
}

//------------------------------------------------------------------------------

AvifImagePtr ReadImage(const char* folder_path, const char* file_name,
                       avifPixelFormat requested_format, int requested_depth,
                       avifChromaDownsampling chroma_downsampling,
                       avifBool ignore_icc, avifBool ignore_exif,
                       avifBool ignore_xmp, avifBool allow_changing_cicp) {
  testutil::AvifImagePtr image(avifImageCreateEmpty(), avifImageDestroy);
  if (!image ||
      avifReadImage((std::string(folder_path) + file_name).c_str(),
                    requested_format, requested_depth, chroma_downsampling,
                    ignore_icc, ignore_exif, ignore_xmp, allow_changing_cicp,
                    image.get(),
                    /*outDepth=*/nullptr, /*sourceTiming=*/nullptr,
                    /*frameIter=*/nullptr) == AVIF_APP_FILE_FORMAT_UNKNOWN) {
    return {nullptr, nullptr};
  }
  return image;
}

bool WriteImage(const avifImage* image, const char* file_path) {
  if (!image || !file_path) return false;
  const size_t str_len = std::strlen(file_path);
  if (str_len >= 4 && !std::strncmp(file_path + str_len - 4, ".png", 4)) {
    return avifPNGWrite(file_path, image, /*requestedDepth=*/0,
                        AVIF_CHROMA_UPSAMPLING_BEST_QUALITY,
                        /*compressionLevel=*/0);
  }
  // Other formats are not supported.
  return false;
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

bool Av1EncoderAvailable() {
  const char* encoding_codec =
      avifCodecName(AVIF_CODEC_CHOICE_AUTO, AVIF_CODEC_FLAG_CAN_ENCODE);
  return encoding_codec != nullptr && std::string(encoding_codec) != "avm";
}

bool Av1DecoderAvailable() {
  const char* decoding_codec =
      avifCodecName(AVIF_CODEC_CHOICE_AUTO, AVIF_CODEC_FLAG_CAN_DECODE);
  return decoding_codec != nullptr && std::string(decoding_codec) != "avm";
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
