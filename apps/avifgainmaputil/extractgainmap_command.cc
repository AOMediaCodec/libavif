// Copyright 2023 Google LLC
// SPDX-License-Identifier: BSD-2-Clause

#include "extractgainmap_command.h"

#include "avif/avif_cxx.h"
#include "imageio.h"

namespace avif {

ExtractGainMapCommand::ExtractGainMapCommand()
    : ProgramCommand("extractgainmap",
                     "Saves the gain map of an avif file as an image") {
  argparse_.add_argument(arg_quality_, "--quality", "-q")
      .help("Image quality (0-100, worst-best) if saving as jpg or avif")
      .default_value("90");
  argparse_.add_argument(arg_speed_, "--speed", "-s")
      .help("Encoder speed (0-10, slowest-fastest) for avif or png")
      .default_value("6");
  argparse_.add_argument(arg_input_filename_, "input_filename");
  argparse_.add_argument(arg_output_filename_, "output_filename");
}

avifResult ExtractGainMapCommand::Run() {
  DecoderPtr decoder(avifDecoderCreate());
  if (decoder == NULL) {
    return AVIF_RESULT_OUT_OF_MEMORY;
  }
  decoder->enableDecodingGainMap = true;

  avifResult result =
      avifDecoderSetIOFile(decoder.get(), arg_input_filename_.value().c_str());
  if (result != AVIF_RESULT_OK) {
    std::cerr << "Cannot open file for read: " << arg_input_filename_ << "\n";
    return result;
  }
  result = avifDecoderParse(decoder.get());
  if (result != AVIF_RESULT_OK) {
    std::cerr << "Failed to parse image: " << avifResultToString(result) << " ("
              << decoder->diag.error << ")\n";
    return result;
  }
  result = avifDecoderNextImage(decoder.get());
  if (result != AVIF_RESULT_OK) {
    std::cerr << "Failed to decode image: " << avifResultToString(result)
              << " (" << decoder->diag.error << ")\n";
    return result;
  }

  if (decoder->image->gainMap.image == nullptr) {
    std::cerr << "Input image " << arg_output_filename_
              << " does not contain a gain map\n";
    return AVIF_RESULT_INVALID_ARGUMENT;
  }

  return WriteImage(decoder->image->gainMap.image, arg_output_filename_,
                    arg_quality_, arg_speed_);
}

}  // namespace avif
