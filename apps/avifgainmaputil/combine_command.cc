// Copyright 2023 Google LLC
// SPDX-License-Identifier: BSD-2-Clause

#include "combine_command.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <iostream>
#include <string>

#include "avif/avif_cxx.h"
#include "imageio.h"

namespace avif {

CombineCommand::CombineCommand()
    : ProgramCommand("combine",
                     "Create an AVIF image with a gain map from a base image "
                     "and an alternate image") {
  argparse_.add_argument("base_image")
      .help(
          "The base image, that will be shown by viewers that don't support "
          "gain maps");
  argparse_.add_argument("alternate_image")
      .help("The alternate image, the result of fully applying the gain map");
  argparse_.add_argument("output_image.avif");
  argparse_.add_argument("--downscaling")
      .help("Downscaling factor for the gain map")
      .scan<'d', int>()
      .default_value(1)
      .nargs(1);
  argparse_.add_argument("--qgain-map")
      .help("Quality for the gain map (0-100, where 100 is lossless)")
      .scan<'d', int>()
      .default_value(60)
      .nargs(1);
  argparse_.add_argument("--depth-gain-map")
      .choices(8, 10, 12)
      .help("Output depth for the gain map")
      .scan<'d', int>()
      .default_value(8)
      .nargs(1);
  argparse_.add_argument("--yuv-gain-map")
      .choices("444", "422", "420", "400")
      .help("Output format for the gain map")
      .default_value(std::string("444"))
      .nargs(1);
  argparse_.add_argument("--matrix-gain-map")
      .help(
          "Matrix coefficients for the gain map (e.g. 0 for identity, 6 for "
          "BT.601).")
      .scan<'d', int>()
      .default_value(6)
      .nargs(1);
  argparse_.add_argument("--max-headroom")
      .help(
          "Maximum value for the base image HDR headroom and alternate "
          "image HDR headroom. Overrides the default headroom values computed "
          "from the image's content if they are larger than this maximum. Use "
          "0 for no maximum. "
          "E.g. assuming one of the two images is SDR and the "
          "other is HDR, the full HDR image (i.e. without tone mapping to SDR "
          "using the gain map) will be shown for displays with at least this "
          "amount of HDR headroom.")
      .scan<'g', float>()
      .default_value(4.0f)
      .nargs(1);
  argparse_.add_argument("--cicp-base")
      .help(
          "Set or override the CICP values for the base image, expressed as "
          "P/T/M where P = color primaries, T = transfer characteristics, "
          "M = matrix coefficients.")
      .action(ParseCicp);
  argparse_.add_argument("--cicp-alternate")
      .help(
          "Set or override the CICP values for the alternate image, expressed "
          "as P/T/M  where P = color primaries, T = transfer characteristics, "
          "M = matrix coefficients.")
      .action(ParseCicp);
  argparse_.add_argument("--clli-base")
      .help(
          "Override content light level information of the base image, "
          "expressed as:  MaxCLL,MaxPALL.")
      .action(ParseClli);
  argparse_.add_argument("--clli-alternate")
      .help(
          "Override content light level information of the alternate image, "
          "expressed as:  MaxCLL,MaxPALL.")
      .action(ParseClli);
  arg_image_encode_.Init(argparse_, /*can_have_alpha=*/true);
  arg_image_read_.Init(argparse_);
  arg_jobs_.Init(argparse_);
}

avifResult CombineCommand::Run() {
  const std::string base_filename = argparse_.get<std::string>("base_image");
  const std::string alternate_filename =
      argparse_.get<std::string>("alternate_image");
  const std::string output_filename =
      argparse_.get<std::string>("output_image.avif");
  const avifPixelFormat pixel_format = arg_image_read_.pixel_format();
  const avifPixelFormat gain_map_pixel_format =
      ParsePixelFormat(argparse_.get<std::string>("--yuv-gain-map"));

  ImagePtr base_image(avifImageCreateEmpty());
  ImagePtr alternate_image(avifImageCreateEmpty());
  if (base_image == nullptr || alternate_image == nullptr) {
    return AVIF_RESULT_OUT_OF_MEMORY;
  }
  if (const auto base_cicp = argparse_.present<CicpValues>("--cicp-base")) {
    base_image->colorPrimaries = base_cicp->color_primaries;
    base_image->transferCharacteristics = base_cicp->transfer_characteristics;
    base_image->matrixCoefficients = base_cicp->matrix_coefficients;
  }
  avifResult result = ReadImage(
      base_image.get(), base_filename, pixel_format, arg_image_read_.depth(),
      arg_image_read_.ignore_profile(), arg_image_read_.ignore_exif(),
      arg_image_read_.ignore_xmp(), arg_image_read_.ignore_alpha(),
      /*ignore_gain_map=*/true, arg_jobs_.jobs());
  if (result != AVIF_RESULT_OK) {
    std::cout << "Failed to read base image: " << avifResultToString(result)
              << "\n";
    return result;
  }

  if (const auto alternate_cicp =
          argparse_.present<CicpValues>("--cicp-alternate")) {
    alternate_image->colorPrimaries = alternate_cicp->color_primaries;
    alternate_image->transferCharacteristics =
        alternate_cicp->transfer_characteristics;
    alternate_image->matrixCoefficients = alternate_cicp->matrix_coefficients;
  }
  result =
      ReadImage(alternate_image.get(), alternate_filename, pixel_format,
                arg_image_read_.depth(), arg_image_read_.ignore_profile(),
                arg_image_read_.ignore_exif(), arg_image_read_.ignore_xmp(),
                arg_image_read_.ignore_alpha(), /*ignore_gain_map=*/true,
                arg_jobs_.jobs());
  if (result != AVIF_RESULT_OK) {
    std::cout << "Failed to read alternate image: "
              << avifResultToString(result) << "\n";
    return result;
  }

  const uint32_t downscaling =
      std::max<int>(1, argparse_.get<int>("--downscaling"));
  const uint32_t rounding = downscaling / 2;
  const uint32_t gain_map_width =
      std::max((base_image->width + rounding) / downscaling, 1u);
  const uint32_t gain_map_height =
      std::max((base_image->height + rounding) / downscaling, 1u);
  std::cout << "Creating a gain map of size " << gain_map_width << " x "
            << gain_map_height << "\n";

  // Because base_image is read with ignore_gain_map=true, there is no
  // preexisting gain map but there may be preexisting gain map metadata.
  if (base_image->gainMap) {
    assert(base_image->gainMap->image == nullptr);
    avifGainMapDestroy(base_image->gainMap);
    base_image->gainMap = nullptr;
  }

  avifMatrixCoefficients gain_map_matrix_coefficients =
      static_cast<avifMatrixCoefficients>(
          argparse_.get<int>("--matrix-gain-map"));
  if (gain_map_matrix_coefficients == AVIF_MATRIX_COEFFICIENTS_IDENTITY &&
      gain_map_pixel_format != AVIF_PIXEL_FORMAT_YUV444) {
    std::cout << "Matrix coefficients may not be set to identity (0) when "
                 "gain map is subsampled (yuv-gain-map must be 444).\n";
    return AVIF_RESULT_INVALID_ARGUMENT;
  }

  base_image->gainMap = avifGainMapCreate();
  base_image->gainMap->image = avifImageCreate(
      gain_map_width, gain_map_height, argparse_.get<int>("--depth-gain-map"),
      gain_map_pixel_format);
  if (base_image->gainMap->image == nullptr) {
    return AVIF_RESULT_OUT_OF_MEMORY;
  }
  base_image->gainMap->image->matrixCoefficients = gain_map_matrix_coefficients;
  avifDiagnostics diag;
  result = avifImageComputeGainMap(base_image.get(), alternate_image.get(),
                                   base_image->gainMap, &diag);
  if (result != AVIF_RESULT_OK) {
    std::cout << "Failed to compute gain map: " << avifResultToString(result)
              << " (" << diag.error << ")\n";
    return result;
  }

  const float max_headroom = argparse_.get<float>("--max-headroom");
  if (max_headroom > 0) {
    if (max_headroom * base_image->gainMap->baseHdrHeadroom.d <
        base_image->gainMap->baseHdrHeadroom.n) {
      if (!avifDoubleToUnsignedFraction(
              max_headroom, &base_image->gainMap->baseHdrHeadroom)) {
        std::cout << "Unable to express " << max_headroom << " as a fraction";
        return AVIF_RESULT_INVALID_ARGUMENT;
      }
    }
    if (max_headroom * base_image->gainMap->alternateHdrHeadroom.d <
        base_image->gainMap->alternateHdrHeadroom.n) {
      if (!avifDoubleToUnsignedFraction(
              max_headroom, &base_image->gainMap->alternateHdrHeadroom)) {
        std::cout << "Unable to express " << max_headroom << " as a fraction";
        return AVIF_RESULT_INVALID_ARGUMENT;
      }
    }
  }

  if (const auto base_clli =
          argparse_.present<avifContentLightLevelInformationBox>(
              "--clli-base")) {
    base_image->clli = *base_clli;
  }
  if (const auto alternate_clli =
          argparse_.present<avifContentLightLevelInformationBox>(
              "--clli-alternate")) {
    base_image->gainMap->altCLLI = *alternate_clli;
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
  result = WriteAvifGrid(base_image.get(), grid.grid_cols, grid.grid_rows,
                         encoder.get(), output_filename);
  if (result != AVIF_RESULT_OK) {
    std::cout << "Failed to encode image: " << avifResultToString(result)
              << " (" << encoder->diag.error << ")\n";
    return result;
  }

  return AVIF_RESULT_OK;
}

}  // namespace avif
