// Copyright 2023 Google LLC
// SPDX-License-Identifier: BSD-2-Clause

#include "imageio.h"

#include <fstream>
#include <iostream>
#include <memory>

#include "avif/avif_cxx.h"
#include "avifjpeg.h"
#include "avifpng.h"
#include "avifutil.h"
#include "y4m.h"

namespace avif {

template <typename T>
inline T Clamp(T x, T low, T high) {  // Only exists in C++17.
  return (x < low) ? low : (high < x) ? high : x;
}

avifResult WriteImage(const avifImage* image,
                      const std::string& output_filename, int quality,
                      int speed) {
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
    if (!avifPNGWrite(output_filename.c_str(), image, image->depth,
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
    avifRWData encoded = AVIF_DATA_EMPTY;
    std::cout << "Encoding AVIF at quality " << encoder->quality << " speed "
              << encoder->speed << ", please wait...\n";
    avifResult result = avifEncoderWrite(encoder.get(), image, &encoded);
    if (result != AVIF_RESULT_OK) {
      std::cerr << "Failed to encode image: " << avifResultToString(result)
                << " (" << encoder->diag.error << ")\n";
      return result;
    }
    std::ofstream f(output_filename, std::ios::binary);
    f.write(reinterpret_cast<char*>(encoded.data), encoded.size);
    if (f.bad()) {
      std::cerr << "Failed to write image " << output_filename << "\n";
      return AVIF_RESULT_IO_ERROR;
    }
    std::cout << "Wrote AVIF: " << output_filename << "\n";
  } else {
    std::cerr << "Unsupported output file extension: " << output_filename
              << "\n";
    return AVIF_RESULT_INVALID_ARGUMENT;
  }
  return AVIF_RESULT_OK;
}

}  // namespace avif
