// Copyright 2022 Google LLC
// SPDX-License-Identifier: BSD-2-Clause

#include "avif_fuzztest_helpers.h"

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <utility>
#include <vector>

#include "avif/avif.h"
#include "avifutil.h"
#include "fuzztest/fuzztest.h"
#include "gtest/gtest.h"

namespace avif {
namespace testutil {
namespace {

//------------------------------------------------------------------------------

ImagePtr CreateAvifImage(size_t width, size_t height, int depth,
                         avifPixelFormat pixel_format, bool has_alpha,
                         const uint8_t* samples) {
  ImagePtr image(avifImageCreate(static_cast<uint32_t>(width),
                                 static_cast<uint32_t>(height), depth,
                                 pixel_format));
  if (image.get() == nullptr) {
    return image;
  }
  if (avifImageAllocatePlanes(image.get(),
                              has_alpha ? AVIF_PLANES_ALL : AVIF_PLANES_YUV) !=
      AVIF_RESULT_OK) {
    return nullptr;
  }

  for (avifChannelIndex c :
       {AVIF_CHAN_Y, AVIF_CHAN_U, AVIF_CHAN_V, AVIF_CHAN_A}) {
    const size_t plane_height = avifImagePlaneHeight(image.get(), c);
    uint8_t* row = avifImagePlane(image.get(), c);
    const uint32_t row_bytes = avifImagePlaneRowBytes(image.get(), c);
    assert(row_bytes == avifImagePlaneWidth(image.get(), c) *
                            (avifImageUsesU16(image.get()) ? 2 : 1));
    for (size_t y = 0; y < plane_height; ++y) {
      std::copy(samples, samples + row_bytes, row);
      row += row_bytes;
      samples += row_bytes;
    }
  }
  return image;
}

}  // namespace

ImagePtr CreateAvifImage8b(size_t width, size_t height,
                           avifPixelFormat pixel_format, bool has_alpha,
                           const std::vector<uint8_t>& samples) {
  return CreateAvifImage(width, height, 8, pixel_format, has_alpha,
                         samples.data());
}

ImagePtr CreateAvifImage16b(size_t width, size_t height, int depth,
                            avifPixelFormat pixel_format, bool has_alpha,
                            const std::vector<uint16_t>& samples) {
  return CreateAvifImage(width, height, depth, pixel_format, has_alpha,
                         reinterpret_cast<const uint8_t*>(samples.data()));
}

std::vector<ImagePtr> CreateAvifAnim8b(size_t num_frames, size_t width,
                                       size_t height,
                                       avifPixelFormat pixel_format,
                                       bool has_alpha,
                                       const std::vector<uint8_t>& samples) {
  std::vector<ImagePtr> frames;
  frames.reserve(num_frames);
  const uint8_t* frame_samples = samples.data();
  for (size_t i = 0; i < num_frames; ++i) {
    frames.push_back(CreateAvifImage(width, height, 8, pixel_format, has_alpha,
                                     frame_samples));
    frame_samples +=
        GetNumSamples(/*num_frames=*/1, width, height, pixel_format, has_alpha);
  }
  return frames;
}

std::vector<ImagePtr> CreateAvifAnim16b(size_t num_frames, size_t width,
                                        size_t height, int depth,
                                        avifPixelFormat pixel_format,
                                        bool has_alpha,
                                        const std::vector<uint16_t>& samples) {
  std::vector<ImagePtr> frames;
  frames.reserve(num_frames);
  const uint16_t* frame_samples = samples.data();
  for (size_t i = 0; i < num_frames; ++i) {
    frames.push_back(
        CreateAvifImage(width, height, depth, pixel_format, has_alpha,
                        reinterpret_cast<const uint8_t*>(frame_samples)));
    frame_samples +=
        GetNumSamples(/*num_frames=*/1, width, height, pixel_format, has_alpha);
  }
  return frames;
}

EncoderPtr CreateAvifEncoder(avifCodecChoice codec_choice, int max_threads,
                             int min_quantizer, int max_quantizer,
                             int min_quantizer_alpha, int max_quantizer_alpha,
                             int tile_rows_log2, int tile_cols_log2,
                             int speed) {
  EncoderPtr encoder(avifEncoderCreate());
  if (encoder.get() == nullptr) {
    return encoder;
  }
  encoder->codecChoice = codec_choice;
  encoder->maxThreads = max_threads;
  // minQuantizer must be at most maxQuantizer.
  encoder->minQuantizer = std::min(min_quantizer, max_quantizer);
  encoder->maxQuantizer = std::max(min_quantizer, max_quantizer);
  encoder->minQuantizerAlpha =
      std::min(min_quantizer_alpha, max_quantizer_alpha);
  encoder->maxQuantizerAlpha =
      std::max(min_quantizer_alpha, max_quantizer_alpha);
  encoder->tileRowsLog2 = tile_rows_log2;
  encoder->tileColsLog2 = tile_cols_log2;
  encoder->speed = speed;
  return encoder;
}

DecoderPtr CreateAvifDecoder(avifCodecChoice codec_choice, int max_threads,
                             avifDecoderSource requested_source,
                             bool allow_progressive, bool allow_incremental,
                             bool ignore_exif, bool ignore_xmp,
                             uint32_t image_size_limit,
                             uint32_t image_dimension_limit,
                             uint32_t image_count_limit,
                             avifStrictFlags strict_flags) {
  DecoderPtr decoder(avifDecoderCreate());
  if (decoder.get() == nullptr) {
    return decoder;
  }
  decoder->codecChoice = codec_choice;
  decoder->maxThreads = max_threads;
  decoder->requestedSource = requested_source;
  decoder->allowProgressive = allow_progressive;
  decoder->allowIncremental = allow_incremental;
  decoder->ignoreExif = ignore_exif;
  decoder->ignoreXMP = ignore_xmp;
  decoder->imageSizeLimit = image_size_limit;
  decoder->imageDimensionLimit = image_dimension_limit;
  decoder->imageCountLimit = image_count_limit;
  decoder->strictFlags = strict_flags;
  return decoder;
}

ImagePtr AvifImageToUniquePtr(avifImage* image) { return ImagePtr(image); }

#if defined(AVIF_ENABLE_EXPERIMENTAL_GAIN_MAP)
DecoderPtr AddGainMapOptionsToDecoder(DecoderPtr decoder,
                                      GainMapDecodeMode gain_map_decode_mode) {
  decoder->enableParsingGainMapMetadata =
      (gain_map_decode_mode == GainMapDecodeMode::kMetadataOnly ||
       gain_map_decode_mode == GainMapDecodeMode::kDecode);
  decoder->enableDecodingGainMap =
      (gain_map_decode_mode == GainMapDecodeMode::kDecode);
  // Do not fuzz 'ignoreColorAndAlpha' since most tests assume that if the
  // file/buffer is successfully decoded, then the main image was decoded, which
  // is no longer the case when this option is on.
  return decoder;
}

ImagePtr AddGainMapToImage(
    ImagePtr image, ImagePtr gain_map, int32_t gain_map_min_n0,
    int32_t gain_map_min_n1, int32_t gain_map_min_n2, uint32_t gain_map_min_d0,
    uint32_t gain_map_min_d1, uint32_t gain_map_min_d2, int32_t gain_map_max_n0,
    int32_t gain_map_max_n1, int32_t gain_map_max_n2, uint32_t gain_map_max_d0,
    uint32_t gain_map_max_d1, uint32_t gain_map_max_d2,
    uint32_t gain_map_gamma_n0, uint32_t gain_map_gamma_n1,
    uint32_t gain_map_gamma_n2, uint32_t gain_map_gamma_d0,
    uint32_t gain_map_gamma_d1, uint32_t gain_map_gamma_d2,
    int32_t base_offset_n0, int32_t base_offset_n1, int32_t base_offset_n2,
    uint32_t base_offset_d0, uint32_t base_offset_d1, uint32_t base_offset_d2,
    int32_t alternate_offset_n0, int32_t alternate_offset_n1,
    int32_t alternate_offset_n2, uint32_t alternate_offset_d0,
    uint32_t alternate_offset_d1, uint32_t alternate_offset_d2,
    uint32_t base_hdr_headroom_n, uint32_t base_hdr_headroom_d,
    uint32_t alternate_hdr_headroom_n, uint32_t alternate_hdr_headroom_d,
    bool use_base_color_space) {
  image->gainMap = avifGainMapCreate();
  image->gainMap->image = gain_map.release();

  image->gainMap->gainMapMin[0] = {gain_map_min_n0, gain_map_min_d0};
  image->gainMap->gainMapMin[1] = {gain_map_min_n1, gain_map_min_d1};
  image->gainMap->gainMapMin[2] = {gain_map_min_n2, gain_map_min_d2};

  image->gainMap->gainMapMax[0] = {gain_map_max_n0, gain_map_max_d0};
  image->gainMap->gainMapMax[1] = {gain_map_max_n1, gain_map_max_d1};
  image->gainMap->gainMapMax[2] = {gain_map_max_n2, gain_map_max_d2};

  image->gainMap->gainMapGamma[0] = {gain_map_gamma_n0, gain_map_gamma_d0};
  image->gainMap->gainMapGamma[1] = {gain_map_gamma_n1, gain_map_gamma_d1};
  image->gainMap->gainMapGamma[2] = {gain_map_gamma_n2, gain_map_gamma_d2};

  image->gainMap->baseOffset[0] = {base_offset_n0, base_offset_d0};
  image->gainMap->baseOffset[1] = {base_offset_n1, base_offset_d1};
  image->gainMap->baseOffset[2] = {base_offset_n2, base_offset_d2};

  image->gainMap->alternateOffset[0] = {alternate_offset_n0,
                                        alternate_offset_d0};
  image->gainMap->alternateOffset[1] = {alternate_offset_n1,
                                        alternate_offset_d1};
  image->gainMap->alternateOffset[2] = {alternate_offset_n2,
                                        alternate_offset_d2};

  image->gainMap->baseHdrHeadroom = {base_hdr_headroom_n, base_hdr_headroom_d};
  image->gainMap->alternateHdrHeadroom = {alternate_hdr_headroom_n,
                                          alternate_hdr_headroom_d};
  image->gainMap->useBaseColorSpace = use_base_color_space;

  return image;
}
#endif

//------------------------------------------------------------------------------

size_t GetNumSamples(size_t num_frames, size_t width, size_t height,
                     avifPixelFormat pixel_format, bool has_alpha) {
  const size_t num_luma_samples = width * height;

  avifPixelFormatInfo pixel_format_info;
  avifGetPixelFormatInfo(pixel_format, &pixel_format_info);
  size_t num_chroma_samples = 0;
  if (!pixel_format_info.monochrome) {
    num_chroma_samples = 2 *
                         ((width + pixel_format_info.chromaShiftX) >>
                          pixel_format_info.chromaShiftX) *
                         ((height + pixel_format_info.chromaShiftY) >>
                          pixel_format_info.chromaShiftY);
  }

  size_t num_alpha_samples = 0;
  if (has_alpha) {
    num_alpha_samples = width * height;
  }

  return num_frames *
         (num_luma_samples + num_chroma_samples + num_alpha_samples);
}

//------------------------------------------------------------------------------
// Environment setup

namespace {
class Environment : public ::testing::Environment {
 public:
  Environment(const char* name, const char* value)
      : name_(name), value_(value) {}
  void SetUp() override {
#ifdef _WIN32
    _putenv_s(name_, value_);  // Defined in stdlib.h.
#else
    setenv(name_, value_, /*overwrite=*/1);
#endif
  }

 private:
  const char* name_;
  const char* value_;
};
}  // namespace

::testing::Environment* SetEnv(const char* name, const char* value) {
  return ::testing::AddGlobalTestEnvironment(new Environment(name, value));
}

//------------------------------------------------------------------------------

std::vector<std::string> GetSeedDataDirs() {
  const char* var = std::getenv("TEST_DATA_DIRS");
  std::vector<std::string> res;
  if (var == nullptr || *var == 0) return res;
  const char* var_start = var;
  while (true) {
    if (*var == 0 || *var == ';') {
      res.push_back(std::string(var_start, var - var_start));
      if (*var == 0) break;
      var_start = var + 1;
    }
    ++var;
  }
  return res;
}

std::vector<std::string> GetTestImagesContents(
    size_t max_file_size, const std::vector<avifAppFileFormat>& image_formats) {
  // Use an environment variable to get the test data directory because
  // fuzztest seeds are created before the main() function is called, so the
  // test has no chance to parse command line arguments.
  const std::vector<std::string> test_data_dirs = GetSeedDataDirs();
  if (test_data_dirs.empty()) {
    // Only a warning because this can happen when running the binary with
    // --list_fuzz_tests (such as with gtest_discover_tests() in cmake).
    std::cerr << "WARNING: TEST_DATA_DIRS env variable not set, unable to read "
                 "seed files\n";
    return {};
  }

  std::vector<std::string> seeds;
  for (const std::string& test_data_dir : test_data_dirs) {
    std::cout << "Reading seeds from " << test_data_dir
              << " (non recursively)\n";
    auto tuple_vector = fuzztest::ReadFilesFromDirectory(test_data_dir);
    seeds.reserve(tuple_vector.size());
    for (auto& [file_content] : tuple_vector) {
      if (file_content.size() > max_file_size) continue;
      if (!image_formats.empty()) {
        const avifAppFileFormat format = avifGuessBufferFileFormat(
            reinterpret_cast<const uint8_t*>(file_content.data()),
            file_content.size());
        if (std::find(image_formats.begin(), image_formats.end(), format) ==
            image_formats.end()) {
          continue;
        }
      }

      seeds.push_back(std::move(file_content));
    }
  }
  if (seeds.empty()) {
    std::cerr << "ERROR: no files found that match the given file size and "
                 "format criteria\n";
    std::abort();
  }
  std::cout << "Returning " << seeds.size() << " seed images\n";
  return seeds;
}

//------------------------------------------------------------------------------

}  // namespace testutil
}  // namespace avif
