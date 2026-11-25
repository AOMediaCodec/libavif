// Copyright 2023 Google LLC
// SPDX-License-Identifier: BSD-2-Clause

#include "convert_command.h"

#include <iostream>
#include <string>

#include "avif/avif_cxx.h"
#include "avifutil.h"
#include "imageio.h"
#include "swapbase_command.h"

namespace avif {

ConvertCommand::ConvertCommand()
    : ProgramCommand("convert", "Convert a JPEG with a gain map to AVIF",
                     "If features like --swap-base are not needed, avifenc can "
                     "also be used to convert JPEGs to AVIF.") {
  argparse_.add_argument("input_filename.jpg");
  argparse_.add_argument("output_image.avif");
  argparse_.add_argument("--swap-base")
      .help("Make the HDR image the base image")
      .default_value(false)
      .implicit_value(true);
  argparse_.add_argument("--qgain-map")
      .help("Quality for the gain map (0-100, where 100 is lossless)")
      .scan<'d', int>()
      .default_value(60)
      .nargs(1);
  argparse_.add_argument("--cicp")
      .help(
          "Set the CICP values for the input image, expressed as "
          "P/T/M where P = color primaries, T = transfer characteristics, "
          "M = matrix coefficients.")
      .action(ParseCicp);
  argparse_.add_argument("--clli")
      .help(
          "Set the content light level information of the alternate image, "
          "expressed as:  MaxCLL,MaxPALL.")
      .action(ParseClli);
  arg_image_encode_.Init(argparse_, /*can_have_alpha=*/false);
  arg_image_read_.Init(argparse_);
  arg_jobs_.Init(argparse_);
}

avifResult ConvertCommand::Run() {
#if !defined(AVIF_ENABLE_JPEG_GAIN_MAP_CONVERSION)
  std::cout << "JPEG gainmap conversion unavailable because avifgainmaputil "
               "was not built with libxml2.\n";
  return AVIF_RESULT_NOT_IMPLEMENTED;
#else
  const std::string input_filename =
      argparse_.get<std::string>("input_filename.jpg");
  const std::string output_filename =
      argparse_.get<std::string>("output_image.avif");
  const avifPixelFormat pixel_format = arg_image_read_.pixel_format();

  ImagePtr image(avifImageCreateEmpty());
  if (image == nullptr) {
    return AVIF_RESULT_OUT_OF_MEMORY;
  }

  if (const auto cicp = argparse_.present<CicpValues>("--cicp")) {
    image->colorPrimaries = cicp->color_primaries;
    image->transferCharacteristics = cicp->transfer_characteristics;
    image->matrixCoefficients = cicp->matrix_coefficients;
  }

  avifResult result = ReadImage(
      image.get(), input_filename, pixel_format, arg_image_read_.depth(),
      arg_image_read_.ignore_profile(), arg_image_read_.ignore_exif(),
      arg_image_read_.ignore_xmp(), arg_image_read_.ignore_alpha(),
      /*ignore_gain_map=*/false, arg_jobs_.jobs());
  if (result != AVIF_RESULT_OK) {
    std::cout << "Failed to decode image: " << input_filename;
    return result;
  }

  if (image->gainMap && image->gainMap->altICC.size == 0) {
    if (image->gainMap->altColorPrimaries == AVIF_COLOR_PRIMARIES_UNSPECIFIED) {
      // Assume the alternate image has the same primaries as the base image.
      image->gainMap->altColorPrimaries = image->colorPrimaries;
    }
    if (image->gainMap->altTransferCharacteristics ==
        AVIF_TRANSFER_CHARACTERISTICS_UNSPECIFIED) {
      // Assume the alternate image is PQ HDR.
      image->gainMap->altTransferCharacteristics =
          AVIF_TRANSFER_CHARACTERISTICS_PQ;
    }
  }

  if (image->gainMap == nullptr || image->gainMap->image == nullptr) {
    std::cerr << "Input image " << input_filename
              << " does not contain a gain map\n";
    return AVIF_RESULT_INVALID_ARGUMENT;
  }

  if (const auto clli =
          argparse_.present<avifContentLightLevelInformationBox>("--clli")) {
    image->gainMap->altCLLI = *clli;
  }

  if (argparse_.get<bool>("--swap-base")) {
    int depth = arg_image_read_.depth();
    if (depth == 0) {
      depth = image->gainMap->alternateHdrHeadroom.n == 0 ? 8 : 10;
    }
    ImagePtr new_base(avifImageCreateEmpty());
    if (new_base == nullptr) {
      return AVIF_RESULT_OUT_OF_MEMORY;
    }
    result = ChangeBase(*image, depth, image->yuvFormat, new_base.get());
    if (result != AVIF_RESULT_OK) {
      return result;
    }
    std::swap(image, new_base);
  }

  EncoderPtr encoder(avifEncoderCreate());
  if (encoder == nullptr) {
    return AVIF_RESULT_OUT_OF_MEMORY;
  }
  encoder->quality = arg_image_encode_.quality();
  encoder->qualityAlpha = arg_image_encode_.quality_alpha();
  encoder->qualityGainMap = argparse_.get<int>("--qgain-map");
  encoder->speed = arg_image_encode_.speed();
  encoder->maxThreads = arg_jobs_.jobs();
  encoder->autoTiling = true;  // Match avifenc default.
  const GridOptions grid = arg_image_encode_.grid();
  result = WriteAvifGrid(image.get(), grid.grid_cols, grid.grid_rows,
                         encoder.get(), output_filename);
  if (result != AVIF_RESULT_OK) {
    std::cout << "Failed to encode image: " << avifResultToString(result)
              << " (" << encoder->diag.error << ")\n";
    return result;
  }

  return AVIF_RESULT_OK;
#endif  // !defined(AVIF_ENABLE_JPEG_GAIN_MAP_CONVERSION)
}

}  // namespace avif
