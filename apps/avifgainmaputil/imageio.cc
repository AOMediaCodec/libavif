// Copyright 2023 Google LLC
// SPDX-License-Identifier: BSD-2-Clause

#include "imageio.h"

#include <cassert>
#include <cstring>
#include <fstream>
#include <iostream>
#include <memory>
#include <vector>

#include "avif/avif_cxx.h"
#include "avifjpeg.h"
#include "avifpng.h"
#include "avifutil.h"
#include "y4m.h"

#if defined(AVIF_LIBYUV_ENABLED)
#include <libyuv.h>
#endif

namespace avif {

namespace {

avifResult UpsampleYUV420To444(avifImage* image) {
  if (image->yuvFormat != AVIF_PIXEL_FORMAT_YUV420) {
    return AVIF_RESULT_INVALID_ARGUMENT;
  }
  if (!image->imageOwnsYUVPlanes) {
    // We need to replace the U and V planes with new, owned planes. Y has to
    // be owned as well since there is a single ownership boolean for all
    // three planes. If Y is not already owned, we could copy it, but this is
    // not needed in practice for now so just reject it.
    return AVIF_RESULT_NOT_IMPLEMENTED;
  }

  const uint32_t new_uv_width = image->width;
  const uint32_t new_uv_height = image->height;
  const uint32_t old_uv_width =
      static_cast<uint32_t>((static_cast<uint64_t>(image->width) + 1) / 2);
  const uint32_t old_uv_height =
      static_cast<uint32_t>((static_cast<uint64_t>(image->height) + 1) / 2);
  const uint32_t bytes_per_pixel = (image->depth > 8) ? 2 : 1;

  if (new_uv_width > UINT32_MAX / bytes_per_pixel) {
    return AVIF_RESULT_INVALID_ARGUMENT;
  }
  const uint32_t new_stride_u = new_uv_width * bytes_per_pixel;
  const uint32_t new_stride_v = new_stride_u;

  if (new_stride_u != 0 && new_uv_height > SIZE_MAX / new_stride_u) {
    return AVIF_RESULT_INVALID_ARGUMENT;
  }
  const size_t uv_byte_size = static_cast<size_t>(new_stride_u) * new_uv_height;
  uint8_t* new_u = static_cast<uint8_t*>(avifAlloc(uv_byte_size));
  uint8_t* new_v = static_cast<uint8_t*>(avifAlloc(uv_byte_size));
  if (!new_u || !new_v) {
    avifFree(new_u);
    avifFree(new_v);
    return AVIF_RESULT_OUT_OF_MEMORY;
  }

#if defined(AVIF_LIBYUV_ENABLED)
  if (image->depth == 8) {
    libyuv::ScalePlane(image->yuvPlanes[AVIF_CHAN_U],
                       image->yuvRowBytes[AVIF_CHAN_U], old_uv_width,
                       old_uv_height, new_u, new_stride_u, new_uv_width,
                       new_uv_height, libyuv::kFilterBilinear);
    libyuv::ScalePlane(image->yuvPlanes[AVIF_CHAN_V],
                       image->yuvRowBytes[AVIF_CHAN_V], old_uv_width,
                       old_uv_height, new_v, new_stride_v, new_uv_width,
                       new_uv_height, libyuv::kFilterBilinear);
  } else {
#if LIBYUV_VERSION >= 1774
    libyuv::ScalePlane_12(
        reinterpret_cast<const uint16_t*>(image->yuvPlanes[AVIF_CHAN_U]),
        image->yuvRowBytes[AVIF_CHAN_U] / 2, old_uv_width, old_uv_height,
        reinterpret_cast<uint16_t*>(new_u), new_stride_u / 2, new_uv_width,
        new_uv_height, libyuv::kFilterBilinear);
    libyuv::ScalePlane_12(
        reinterpret_cast<const uint16_t*>(image->yuvPlanes[AVIF_CHAN_V]),
        image->yuvRowBytes[AVIF_CHAN_V] / 2, old_uv_width, old_uv_height,
        reinterpret_cast<uint16_t*>(new_v), new_stride_v / 2, new_uv_width,
        new_uv_height, libyuv::kFilterBilinear);
#else
    libyuv::ScalePlane_16(
        reinterpret_cast<const uint16_t*>(image->yuvPlanes[AVIF_CHAN_U]),
        image->yuvRowBytes[AVIF_CHAN_U] / 2, old_uv_width, old_uv_height,
        reinterpret_cast<uint16_t*>(new_u), new_stride_u / 2, new_uv_width,
        new_uv_height, libyuv::kFilterBilinear);
    libyuv::ScalePlane_16(
        reinterpret_cast<const uint16_t*>(image->yuvPlanes[AVIF_CHAN_V]),
        image->yuvRowBytes[AVIF_CHAN_V] / 2, old_uv_width, old_uv_height,
        reinterpret_cast<uint16_t*>(new_v), new_stride_v / 2, new_uv_width,
        new_uv_height, libyuv::kFilterBilinear);
#endif
  }
#else
  (void)old_uv_width;
  (void)old_uv_height;

  // C Fallback: Nearest Neighbor for simplicity (while libyuv path uses a
  // bilinear filter).
  for (uint32_t y = 0; y < new_uv_height; ++y) {
    for (uint32_t x = 0; x < new_uv_width; ++x) {
      size_t src_y = static_cast<size_t>(y) / 2;
      size_t src_x = static_cast<size_t>(x) / 2;
      if (image->depth == 8) {
        new_u[static_cast<size_t>(y) * new_stride_u + x] =
            image->yuvPlanes[AVIF_CHAN_U]
                            [src_y * image->yuvRowBytes[AVIF_CHAN_U] + src_x];
        new_v[static_cast<size_t>(y) * new_stride_v + x] =
            image->yuvPlanes[AVIF_CHAN_V]
                            [src_y * image->yuvRowBytes[AVIF_CHAN_V] + src_x];
      } else {
        reinterpret_cast<uint16_t*>(
            new_u)[static_cast<size_t>(y) * (new_stride_u / 2) + x] =
            reinterpret_cast<const uint16_t*>(image->yuvPlanes[AVIF_CHAN_U])
                [src_y * (image->yuvRowBytes[AVIF_CHAN_U] / 2) + src_x];
        reinterpret_cast<uint16_t*>(
            new_v)[static_cast<size_t>(y) * (new_stride_v / 2) + x] =
            reinterpret_cast<const uint16_t*>(image->yuvPlanes[AVIF_CHAN_V])
                [src_y * (image->yuvRowBytes[AVIF_CHAN_V] / 2) + src_x];
      }
    }
  }
#endif

  avifFree(image->yuvPlanes[AVIF_CHAN_U]);
  avifFree(image->yuvPlanes[AVIF_CHAN_V]);
  image->yuvPlanes[AVIF_CHAN_U] = new_u;
  image->yuvPlanes[AVIF_CHAN_V] = new_v;
  image->yuvRowBytes[AVIF_CHAN_U] = new_stride_u;
  image->yuvRowBytes[AVIF_CHAN_V] = new_stride_v;
  image->yuvFormat = AVIF_PIXEL_FORMAT_YUV444;

  return AVIF_RESULT_OK;
}

avifResult DownsampleYUV444To420(avifImage* image) {
  if (image->yuvFormat != AVIF_PIXEL_FORMAT_YUV444) {
    return AVIF_RESULT_INVALID_ARGUMENT;
  }
  if (!image->imageOwnsYUVPlanes) {
    // We need to replace the U and V planes with new, owned planes. Y has to
    // be owned as well since there is a single ownership boolean for all
    // three planes. If Y is not already owned, we could copy it, but this is
    // not needed in practice for now so just reject it.
    return AVIF_RESULT_NOT_IMPLEMENTED;
  }

  const uint32_t new_uv_width =
      static_cast<uint32_t>((static_cast<uint64_t>(image->width) + 1) / 2);
  const uint32_t new_uv_height =
      static_cast<uint32_t>((static_cast<uint64_t>(image->height) + 1) / 2);
  const uint32_t bytes_per_pixel = (image->depth > 8) ? 2 : 1;

  const uint32_t new_stride_u = new_uv_width * bytes_per_pixel;
  const uint32_t new_stride_v = new_stride_u;

  const size_t uv_byte_size = static_cast<size_t>(new_stride_u) * new_uv_height;
  uint8_t* new_u = static_cast<uint8_t*>(avifAlloc(uv_byte_size));
  uint8_t* new_v = static_cast<uint8_t*>(avifAlloc(uv_byte_size));
  if (!new_u || !new_v) {
    avifFree(new_u);
    avifFree(new_v);
    return AVIF_RESULT_OUT_OF_MEMORY;
  }

#if defined(AVIF_LIBYUV_ENABLED)
  if (image->depth == 8) {
    libyuv::ScalePlane(image->yuvPlanes[AVIF_CHAN_U],
                       image->yuvRowBytes[AVIF_CHAN_U], image->width,
                       image->height, new_u, new_stride_u, new_uv_width,
                       new_uv_height, libyuv::kFilterBox);
    libyuv::ScalePlane(image->yuvPlanes[AVIF_CHAN_V],
                       image->yuvRowBytes[AVIF_CHAN_V], image->width,
                       image->height, new_v, new_stride_v, new_uv_width,
                       new_uv_height, libyuv::kFilterBox);
  } else {
#if LIBYUV_VERSION >= 1774
    libyuv::ScalePlane_12(
        reinterpret_cast<const uint16_t*>(image->yuvPlanes[AVIF_CHAN_U]),
        image->yuvRowBytes[AVIF_CHAN_U] / 2, image->width, image->height,
        reinterpret_cast<uint16_t*>(new_u), new_stride_u / 2, new_uv_width,
        new_uv_height, libyuv::kFilterBox);
    libyuv::ScalePlane_12(
        reinterpret_cast<const uint16_t*>(image->yuvPlanes[AVIF_CHAN_V]),
        image->yuvRowBytes[AVIF_CHAN_V] / 2, image->width, image->height,
        reinterpret_cast<uint16_t*>(new_v), new_stride_v / 2, new_uv_width,
        new_uv_height, libyuv::kFilterBox);
#else
    libyuv::ScalePlane_16(
        reinterpret_cast<const uint16_t*>(image->yuvPlanes[AVIF_CHAN_U]),
        image->yuvRowBytes[AVIF_CHAN_U] / 2, image->width, image->height,
        reinterpret_cast<uint16_t*>(new_u), new_stride_u / 2, new_uv_width,
        new_uv_height, libyuv::kFilterBox);
    libyuv::ScalePlane_16(
        reinterpret_cast<const uint16_t*>(image->yuvPlanes[AVIF_CHAN_V]),
        image->yuvRowBytes[AVIF_CHAN_V] / 2, image->width, image->height,
        reinterpret_cast<uint16_t*>(new_v), new_stride_v / 2, new_uv_width,
        new_uv_height, libyuv::kFilterBox);
#endif
  }
#else
  // C Fallback: 2x2 Box Filter
  for (uint32_t y = 0; y < new_uv_height; ++y) {
    for (uint32_t x = 0; x < new_uv_width; ++x) {
      uint32_t count = 0;
      uint32_t sum_u = 0;
      uint32_t sum_v = 0;
      for (uint32_t dy = 0; dy < 2; ++dy) {
        for (uint32_t dx = 0; dx < 2; ++dx) {
          size_t src_y = static_cast<size_t>(y) * 2 + dy;
          size_t src_x = static_cast<size_t>(x) * 2 + dx;
          if (src_y < image->height && src_x < image->width) {
            if (image->depth == 8) {
              sum_u +=
                  image->yuvPlanes[AVIF_CHAN_U]
                                  [src_y * image->yuvRowBytes[AVIF_CHAN_U] +
                                   src_x];
              sum_v +=
                  image->yuvPlanes[AVIF_CHAN_V]
                                  [src_y * image->yuvRowBytes[AVIF_CHAN_V] +
                                   src_x];
            } else {
              sum_u += reinterpret_cast<const uint16_t*>(
                  image->yuvPlanes[AVIF_CHAN_U])
                  [src_y * (image->yuvRowBytes[AVIF_CHAN_U] / 2) + src_x];
              sum_v += reinterpret_cast<const uint16_t*>(
                  image->yuvPlanes[AVIF_CHAN_V])
                  [src_y * (image->yuvRowBytes[AVIF_CHAN_V] / 2) + src_x];
            }
            count++;
          }
        }
      }
      if (image->depth == 8) {
        new_u[static_cast<size_t>(y) * new_stride_u + x] =
            (sum_u + count / 2) / count;
        new_v[static_cast<size_t>(y) * new_stride_v + x] =
            (sum_v + count / 2) / count;
      } else {
        reinterpret_cast<uint16_t*>(
            new_u)[static_cast<size_t>(y) * (new_stride_u / 2) + x] =
            (sum_u + count / 2) / count;
        reinterpret_cast<uint16_t*>(
            new_v)[static_cast<size_t>(y) * (new_stride_v / 2) + x] =
            (sum_v + count / 2) / count;
      }
    }
  }
#endif

  avifFree(image->yuvPlanes[AVIF_CHAN_U]);
  avifFree(image->yuvPlanes[AVIF_CHAN_V]);
  image->yuvPlanes[AVIF_CHAN_U] = new_u;
  image->yuvPlanes[AVIF_CHAN_V] = new_v;
  image->yuvRowBytes[AVIF_CHAN_U] = new_stride_u;
  image->yuvRowBytes[AVIF_CHAN_V] = new_stride_v;
  image->yuvFormat = AVIF_PIXEL_FORMAT_YUV420;

  return AVIF_RESULT_OK;
}

}  // namespace

template <typename T>
inline T Clamp(T x, T low, T high) {  // Only exists in C++17.
  return (x < low) ? low : (high < x) ? high : x;
}

avifResult WriteImage(const avifImage* image, int grid_cols, int grid_rows,
                      const std::string& output_filename, int quality,
                      int speed, int jobs) {
  quality = Clamp(quality, 0, 100);
  speed = Clamp(speed, 0, 10);
  const avifAppFileFormat output_format =
      avifGuessFileFormat(output_filename.c_str());
  if (output_format == AVIF_APP_FILE_FORMAT_UNKNOWN) {
    std::cerr << "Cannot determine output file extension: " << output_filename
              << "\n";
    return AVIF_RESULT_INVALID_ARGUMENT;
  } else if (output_format == AVIF_APP_FILE_FORMAT_Y4M) {
    if (!y4mWrite(output_filename.c_str(), image)) {
      return AVIF_RESULT_UNKNOWN_ERROR;
    }
  } else if (output_format == AVIF_APP_FILE_FORMAT_JPEG) {
    if (!avifJPEGWrite(output_filename.c_str(), image, quality,
                       AVIF_CHROMA_UPSAMPLING_AUTOMATIC)) {
      return AVIF_RESULT_UNKNOWN_ERROR;
    }
  } else if (output_format == AVIF_APP_FILE_FORMAT_PNG) {
    const int compression_level = Clamp(10 - speed, 0, 9);
    if (!avifPNGWrite(output_filename.c_str(), image, /*requestedDepth=*/0,
                      AVIF_CHROMA_UPSAMPLING_AUTOMATIC, compression_level)) {
      return AVIF_RESULT_UNKNOWN_ERROR;
    }
  } else if (output_format == AVIF_APP_FILE_FORMAT_AVIF) {
    EncoderPtr encoder(avifEncoderCreate());
    if (encoder == nullptr) {
      return AVIF_RESULT_OUT_OF_MEMORY;
    }
    encoder->quality = quality;
    encoder->speed = speed;
    encoder->maxThreads = jobs;
    return WriteAvifGrid(image, grid_cols, grid_rows, encoder.get(),
                         output_filename);
  } else {
    std::cerr << "Unsupported output file extension: " << output_filename
              << "\n";
    return AVIF_RESULT_INVALID_ARGUMENT;
  }
  return AVIF_RESULT_OK;
}

namespace {
std::string QualityLevelString(int quality) {
  if (quality == AVIF_QUALITY_LOSSLESS) {
    return "Lossless";
  }
  if (quality >= 80) {
    return "High";
  }
  if (quality >= 50) {
    return "Medium";
  }
  if (quality == AVIF_QUALITY_WORST) {
    return "Worst";
  }
  return "Low";
}

// Based on avifenc.c, changes here may be mirrored there if relevant.
void PrintEncodingSettings(const avifEncoder* encoder, bool has_gain_map) {
  std::string gain_map_str;
  if (has_gain_map) {
    gain_map_str = ", gain map quality [" +
                   std::to_string(encoder->qualityGainMap) + " (" +
                   QualityLevelString(encoder->qualityGainMap) + ")]";
  }
  std::string tiling_str = "automatic tiling";
  if (!encoder->autoTiling) {
    tiling_str = "tileRowsLog2 [" + std::to_string(encoder->tileRowsLog2) +
                 "], tileColsLog2 [" + std::to_string(encoder->tileColsLog2) +
                 "]";
  }
  std::cout << "Encoding AVIF with settings: codec '"
            << avifCodecName(encoder->codecChoice, AVIF_CODEC_FLAG_CAN_ENCODE)
            << "' speed ['" << encoder->speed << "'], color quality ['"
            << encoder->quality << "' (" << QualityLevelString(encoder->quality)
            << ")], alpha quality ['" << encoder->qualityAlpha << "' ("
            << QualityLevelString(encoder->qualityAlpha) << ")]" << gain_map_str
            << ", " << tiling_str << ", " << encoder->maxThreads
            << " worker thread(s), please wait...\n";
}
}  // namespace

avifResult WriteAvif(const avifImage* image, avifEncoder* encoder,
                     const std::string& output_filename) {
  avifRWData encoded = AVIF_DATA_EMPTY;
  std::cout << "AVIF to be written:\n";
  avifImageDump(image,
                /*gridCols=*/1,
                /*gridRows=*/1, AVIF_PROGRESSIVE_STATE_UNAVAILABLE);
  PrintEncodingSettings(encoder, image->gainMap != nullptr);
  avifResult result = avifEncoderWrite(encoder, image, &encoded);
  if (result != AVIF_RESULT_OK) {
    std::cerr << "Failed to encode image: " << avifResultToString(result)
              << " (" << encoder->diag.error << ")\n";
    return result;
  }
  std::ofstream f(output_filename, std::ios::binary);
  f.write(reinterpret_cast<char*>(encoded.data), encoded.size);
  avifRWDataFree(&encoded);
  if (f.fail()) {
    std::cerr << "Failed to write image " << output_filename << ": "
              << std::strerror(errno) << "\n";
    return AVIF_RESULT_IO_ERROR;
  }
  std::cout << "Wrote AVIF: " << output_filename << "\n";
  return AVIF_RESULT_OK;
}

avifResult WriteAvifGrid(const avifImage* image, int grid_cols, int grid_rows,
                         avifEncoder* encoder, const std::string& filename) {
  if (grid_cols == 1 && grid_rows == 1) {
    return WriteAvif(image, encoder, filename);
  }

  const uint32_t grid_cell_count = grid_cols * grid_rows;
  std::cout << "Preparing to encode a " << grid_cols << "x" << grid_rows
            << " grid (" << grid_cell_count << " cells)...\n";

  std::vector<avifImage*> grid_cells_ptrs(grid_cell_count);
  if (!avifImageSplitGrid(image, grid_cols, grid_rows,
                          grid_cells_ptrs.data())) {
    return AVIF_RESULT_UNKNOWN_ERROR;
  }
  // Take ownership of the pointers returned by avifImageSplitGrid.
  std::vector<ImagePtr> grid_cells(grid_cell_count);
  for (uint32_t i = 0; i < grid_cell_count; i++) {
    grid_cells[i].reset(grid_cells_ptrs[i]);
  }

  avifRWData encoded = AVIF_DATA_EMPTY;
  std::cout << "AVIF to be written:\n";
  avifImageDump(grid_cells_ptrs[0], grid_cols, grid_rows,
                AVIF_PROGRESSIVE_STATE_UNAVAILABLE);
  PrintEncodingSettings(encoder, image->gainMap != nullptr);
  avifResult result = avifEncoderAddImageGrid(encoder, grid_cols, grid_rows,
                                              grid_cells_ptrs.data(),
                                              AVIF_ADD_IMAGE_FLAG_SINGLE);
  if (result != AVIF_RESULT_OK) {
    std::cerr << "Failed to encode image grid: " << avifResultToString(result)
              << " (" << encoder->diag.error << ")\n";
    return result;
  }
  result = avifEncoderFinish(encoder, &encoded);
  if (result != AVIF_RESULT_OK) {
    std::cerr << "Failed to finish encoding image grid: "
              << avifResultToString(result) << " (" << encoder->diag.error
              << ")\n";
    return result;
  }

  std::ofstream f(filename, std::ios::binary);
  f.write(reinterpret_cast<char*>(encoded.data), encoded.size);
  avifRWDataFree(&encoded);
  if (f.fail()) {
    std::cerr << "Failed to write image " << filename << ": "
              << std::strerror(errno) << "\n";
    return AVIF_RESULT_IO_ERROR;
  }
  std::cout << "Wrote AVIF: " << filename << "\n";
  return AVIF_RESULT_OK;
}

namespace {
std::string PixelFormatToString(avifPixelFormat format) {
  switch (format) {
    case AVIF_PIXEL_FORMAT_YUV444:
      return "4:4:4";
    case AVIF_PIXEL_FORMAT_YUV422:
      return "4:2:2";
    case AVIF_PIXEL_FORMAT_YUV420:
      return "4:2:0";
    case AVIF_PIXEL_FORMAT_YUV400:
      return "4:0:0";
    default:
      return "unknown";
  }
}
}  // namespace

avifResult ReadImage(avifImage* image, const std::string& input_filename,
                     avifPixelFormat requested_format, uint32_t requested_depth,
                     bool ignore_profile, bool ignore_exif, bool ignore_xmp,
                     bool ignore_alpha, bool ignore_gain_map, int jobs) {
  avifAppFileFormat input_format = avifGuessFileFormat(input_filename.c_str());
  if (input_format == AVIF_APP_FILE_FORMAT_UNKNOWN) {
    std::cerr << "Cannot determine input format: " << input_filename;
    return AVIF_RESULT_INVALID_ARGUMENT;
  } else if (input_format == AVIF_APP_FILE_FORMAT_AVIF) {
    DecoderPtr decoder(avifDecoderCreate());
    if (decoder == nullptr) {
      return AVIF_RESULT_OUT_OF_MEMORY;
    }
    decoder->maxThreads = jobs;
    if (ignore_alpha) {
      decoder->imageContentToDecode &= ~AVIF_IMAGE_CONTENT_ALPHA;
    }
    if (!ignore_gain_map) {
      decoder->imageContentToDecode |= AVIF_IMAGE_CONTENT_GAIN_MAP;
    }
    decoder->ignoreICC = ignore_profile;
    decoder->ignoreExif = ignore_exif;
    decoder->ignoreXMP = ignore_xmp;
    avifResult result = ReadAvif(decoder.get(), input_filename);
    if (result != AVIF_RESULT_OK) {
      return result;
    }

    const avifColorPrimaries in_primaries = image->colorPrimaries;
    const avifTransferCharacteristics in_transfer =
        image->transferCharacteristics;
    const avifMatrixCoefficients in_matrix = image->matrixCoefficients;

    result = avifImageCopy(image, decoder->image, AVIF_PLANES_ALL);
    if (result != AVIF_RESULT_OK) {
      return result;
    }

    if (in_primaries != AVIF_COLOR_PRIMARIES_UNSPECIFIED ||
        in_transfer != AVIF_TRANSFER_CHARACTERISTICS_UNSPECIFIED ||
        in_matrix != AVIF_MATRIX_COEFFICIENTS_UNSPECIFIED) {
      image->colorPrimaries = in_primaries;
      image->transferCharacteristics = in_transfer;
      image->matrixCoefficients = in_matrix;
    }

    // Attempt to honor the requested format if it was explicitly asked for and
    // it differs from the source.
    if (requested_format != AVIF_PIXEL_FORMAT_NONE &&
        requested_format != image->yuvFormat) {
      if (image->yuvFormat == AVIF_PIXEL_FORMAT_YUV444 &&
          requested_format == AVIF_PIXEL_FORMAT_YUV420) {
        avifResult scale_res = DownsampleYUV444To420(image);
        if (scale_res != AVIF_RESULT_OK) {
          return scale_res;
        }
      } else if (image->yuvFormat == AVIF_PIXEL_FORMAT_YUV420 &&
                 requested_format == AVIF_PIXEL_FORMAT_YUV444) {
        avifResult scale_res = UpsampleYUV420To444(image);
        if (scale_res != AVIF_RESULT_OK) {
          return scale_res;
        }
      } else {
        std::cerr << "Warning: converting from "
                  << PixelFormatToString(image->yuvFormat) << " to "
                  << PixelFormatToString(requested_format)
                  << " is not supported. Leaving image as "
                  << PixelFormatToString(image->yuvFormat) << "\n";
      }
    }
  } else {
    const avifAppFileFormat file_format = avifReadImage(
        input_filename.c_str(), AVIF_APP_FILE_FORMAT_UNKNOWN /* guess format */,
        requested_format, static_cast<int>(requested_depth),
        AVIF_CHROMA_DOWNSAMPLING_AUTOMATIC, ignore_profile, ignore_exif,
        ignore_xmp, ignore_alpha, ignore_gain_map,
        AVIF_DEFAULT_IMAGE_SIZE_LIMIT, image,
        /*outDepth=*/nullptr,
        /*sourceTiming=*/nullptr, /*frameIter=*/nullptr);
    if (file_format == AVIF_APP_FILE_FORMAT_UNKNOWN) {
      std::cout << "Failed to decode image: " << input_filename;
      return AVIF_RESULT_INVALID_ARGUMENT;
    }
    // Assume sRGB by default.
    if (image->icc.size == 0 &&
        image->colorPrimaries == AVIF_COLOR_PRIMARIES_UNSPECIFIED &&
        image->transferCharacteristics == AVIF_COLOR_PRIMARIES_UNSPECIFIED) {
      image->colorPrimaries = AVIF_COLOR_PRIMARIES_SRGB;
      image->transferCharacteristics = AVIF_TRANSFER_CHARACTERISTICS_SRGB;
    }
    if (image->matrixCoefficients == AVIF_MATRIX_COEFFICIENTS_UNSPECIFIED) {
      // Explicitly set the default matrix coefficient, see
      // avifCalcYUVCoefficients().
      image->matrixCoefficients = AVIF_MATRIX_COEFFICIENTS_BT601;
    }
  }
  return AVIF_RESULT_OK;
}

avifResult ReadAvif(avifDecoder* decoder, const std::string& input_filename) {
  avifResult result = avifDecoderSetIOFile(decoder, input_filename.c_str());
  if (result != AVIF_RESULT_OK) {
    std::cerr << "Cannot open file for read: " << input_filename << "\n";
    return result;
  }
  result = avifDecoderParse(decoder);
  if (result != AVIF_RESULT_OK) {
    std::cerr << "Failed to parse image: " << avifResultToString(result) << " ("
              << decoder->diag.error << ")\n";
    return result;
  }
  result = avifDecoderNextImage(decoder);
  if (result != AVIF_RESULT_OK) {
    std::cerr << "Failed to decode image: " << avifResultToString(result)
              << " (" << decoder->diag.error << ")\n";
    return result;
  }
  if (decoder->ignoreICC) {
    assert(decoder->image->icc.size == 0);
    if (decoder->image->gainMap) {
      assert(decoder->image->gainMap->altICC.size == 0);
    }
  }
  assert((decoder->imageContentToDecode & AVIF_IMAGE_CONTENT_ALPHA) != 0 ||
         (decoder->image->alphaPlane == nullptr &&
          decoder->image->alphaRowBytes == 0));

  return AVIF_RESULT_OK;
}

}  // namespace avif
