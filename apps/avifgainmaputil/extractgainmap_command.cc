// Copyright 2023 Google LLC
// SPDX-License-Identifier: BSD-2-Clause

#include "extractgainmap_command.h"

#include <iostream>
#include <string>

#include "avif/avif_cxx.h"
#include "avifutil.h"
#include "imageio.h"

namespace avif {

ExtractGainMapCommand::ExtractGainMapCommand()
    : ProgramCommand("extractgainmap",
                     "Save the gain map of an AVIF file as an image") {
  argparse_.add_argument("input_filename");
  argparse_.add_argument("output_filename");
  arg_image_encode_.Init(argparse_, /*can_have_alpha=*/false);
  arg_jobs_.Init(argparse_);
}

avifResult ExtractGainMapCommand::Run() {
  const std::string input_filename =
      argparse_.get<std::string>("input_filename");
  const std::string output_filename =
      argparse_.get<std::string>("output_filename");

  DecoderPtr decoder(avifDecoderCreate());
  if (decoder == nullptr) {
    return AVIF_RESULT_OUT_OF_MEMORY;
  }
  decoder->maxThreads = arg_jobs_.jobs();
  decoder->imageContentToDecode = AVIF_IMAGE_CONTENT_GAIN_MAP;
  decoder->ignoreICC = true;

  avifResult result = ReadAvif(decoder.get(), input_filename);
  if (result != AVIF_RESULT_OK) {
    return result;
  }

  const avifImage* image = decoder->image;

  if (image->gainMap == nullptr || image->gainMap->image == nullptr) {
    std::cerr << "Input image " << input_filename
              << " does not contain a gain map\n";
    return AVIF_RESULT_INVALID_ARGUMENT;
  }

  const GridOptions grid = arg_image_encode_.grid();
  return WriteImage(image->gainMap->image, grid.grid_cols, grid.grid_rows,
                    output_filename, arg_image_encode_.quality(),
                    arg_image_encode_.speed(), arg_jobs_.jobs());
}

}  // namespace avif
