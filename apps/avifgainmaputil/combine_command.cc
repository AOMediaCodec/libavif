// Copyright 2023 Google LLC
// SPDX-License-Identifier: BSD-2-Clause

#include "combine_command.h"

#include <cmath>

#include "avif/avif_cxx.h"
#include "imageio.h"

namespace avif {

CreateCommand::CreateCommand()
    : ProgramCommand("combine",
                     "Creates an avif image with a gain map from a base image "
                     "and an alternate image.") {
  argparse_.add_argument(arg_base_filename_, "base_image");
  argparse_.add_argument(arg_alternate_filename_, "alternate_image");
  argparse_.add_argument(arg_output_filename_, "output_image.avif");
  argparse_.add_argument(arg_downscaling_, "--downscaling")
      .help("Downscaling factor for the gain map")
      .default_value("1");
  argparse_.add_argument(arg_gain_map_quality_, "--qgain-map")
      .help("Quality for the gain map (0-100, where 100 is lossless)")
      .default_value("60");
  argparse_.add_argument(arg_gain_map_depth_, "--depth-gain-map")
      .choices({"8", "10", "12"})
      .help(
          "Output depth for the gain map")
      .default_value("8");
  argparse_.add_argument<int, PixelFormatConverter>(arg_gain_map_pixel_format_, "--yuv-gain-map")
      .choices({"444", "422", "420", "400"})
      .help("Output format for the gain map")
      .default_value("444");
  arg_image_encode_.Init(argparse_, /*can_have_alpha=*/true);
  arg_image_read_.Init(argparse_);
  // TODO(maryla): allow specifying cicp for inputs and output.
}

avifResult CreateCommand::Run() {
  const avifPixelFormat pixel_format = static_cast<avifPixelFormat>(arg_image_read_.pixel_format.value());
  const avifPixelFormat gain_map_pixel_format = static_cast<avifPixelFormat>(arg_gain_map_pixel_format_.value());

  ImagePtr base_image(avifImageCreateEmpty());
  ImagePtr alternate_image(avifImageCreateEmpty());
  if (base_image == nullptr || alternate_image == nullptr) {
    return AVIF_RESULT_OUT_OF_MEMORY;
  }
  avifResult result = ReadImage(base_image.get(), arg_base_filename_,
                                pixel_format, arg_image_read_.depth, arg_image_read_.ignore_profile);
  if (result != AVIF_RESULT_OK) {
    std::cout << "Failed to read base image: " << avifResultToString(result)
              << "\n";
    return result;
  }
  result = ReadImage(alternate_image.get(), arg_alternate_filename_,
                     pixel_format, arg_image_read_.depth, arg_image_read_.ignore_profile);
  if (result != AVIF_RESULT_OK) {
    std::cout << "Failed to read alternate image: "
              << avifResultToString(result) << "\n";
    return result;
  }

  const int downscaling = std::max<int>(1, arg_downscaling_);
  const uint32_t gain_map_width = std::max<uint32_t>(
      std::round((float)base_image->width / downscaling), 1u);
  const uint32_t gain_map_height = std::max<uint32_t>(
      std::round((float)base_image->height / downscaling), 1u);
  std::cout << "Creating a gain map of size " << gain_map_width << " x "
            << gain_map_height << "\n";

  base_image->gainMap.image =
      avifImageCreate(gain_map_width, gain_map_height, arg_gain_map_depth_,
                      gain_map_pixel_format);
  if (base_image->gainMap.image == nullptr) {
    return AVIF_RESULT_OUT_OF_MEMORY;
  }
  avifDiagnostics diag;
  result = avifComputeGainMap(base_image.get(), alternate_image.get(),
                              &base_image->gainMap, &diag);
  if (result != AVIF_RESULT_OK) {
    std::cout << "Failed to compute gain map: " << avifResultToString(result)
              << " (" << diag.error << ")\n";
    return result;
  }

  EncoderPtr encoder(avifEncoderCreate());
  if (encoder == nullptr) {
    return AVIF_RESULT_OUT_OF_MEMORY;
  }
  encoder->quality = arg_image_encode_.quality;
  encoder->qualityAlpha = arg_image_encode_.quality_alpha;
  encoder->qualityGainMap = arg_gain_map_quality_;
  encoder->speed = arg_image_encode_.speed;
  result = WriteAvif(base_image.get(), encoder.get(), arg_output_filename_);
  if (result != AVIF_RESULT_OK) {
    std::cout << "Failed to encode image: " << avifResultToString(result)
              << " (" << encoder->diag.error << ")\n";
    return result;
  }

  return AVIF_RESULT_OK;
}

}  // namespace avif
