// Copyright 2023 Google LLC
// SPDX-License-Identifier: BSD-2-Clause

#include "swapbase_command.h"

#include <algorithm>
#include <iostream>
#include <string>

#include "avif/avif_cxx.h"
#include "avifutil.h"
#include "imageio.h"

namespace avif {

avifResult ChangeBase(const avifImage& image, int depth,
                      avifPixelFormat yuvFormat, avifImage* swapped) {
  if (image.gainMap == nullptr || image.gainMap->image == nullptr) {
    return AVIF_RESULT_INVALID_ARGUMENT;
  }

  // Copy all metadata (no planes).
  avifResult result = avifImageCopy(swapped, &image, /*planes=*/0);
  if (result != AVIF_RESULT_OK) {
    return result;
  }
  swapped->depth = depth;
  swapped->yuvFormat = yuvFormat;

  if (image.gainMap->alternateHdrHeadroom.d == 0) {
    return AVIF_RESULT_INVALID_ARGUMENT;
  }
  const float headroom =
      static_cast<float>(image.gainMap->alternateHdrHeadroom.n) /
      image.gainMap->alternateHdrHeadroom.d;
  const bool tone_mapping_to_sdr = (headroom == 0.0f);

  swapped->colorPrimaries = image.gainMap->altColorPrimaries;
  if (swapped->colorPrimaries == AVIF_COLOR_PRIMARIES_UNSPECIFIED) {
    // Default to the same primaries as the base image if unspecified.
    swapped->colorPrimaries = image.colorPrimaries;
  }

  swapped->transferCharacteristics = image.gainMap->altTransferCharacteristics;
  if (swapped->transferCharacteristics ==
      AVIF_TRANSFER_CHARACTERISTICS_UNSPECIFIED) {
    // Default to PQ for HDR and sRGB for SDR if unspecified.
    const avifTransferCharacteristics transfer_characteristics =
        static_cast<avifTransferCharacteristics>(
            tone_mapping_to_sdr ? AVIF_TRANSFER_CHARACTERISTICS_SRGB
                                : AVIF_TRANSFER_CHARACTERISTICS_PQ);
    swapped->transferCharacteristics = transfer_characteristics;
  }

  swapped->matrixCoefficients = image.gainMap->altMatrixCoefficients;
  if (swapped->matrixCoefficients == AVIF_MATRIX_COEFFICIENTS_UNSPECIFIED) {
    // Default to the same matrix as the base image if unspecified.
    swapped->matrixCoefficients = image.matrixCoefficients;
  }

  avifRGBImage swapped_rgb;
  RGBImageCleanup rgb_cleanup(&swapped_rgb);
  avifRGBImageSetDefaults(&swapped_rgb, swapped);
  if (image.alphaPlane == nullptr) {
    swapped_rgb.format = AVIF_RGB_FORMAT_RGB;
  }

  avifContentLightLevelInformationBox clli = image.gainMap->altCLLI;
  const bool compute_clli =
      !tone_mapping_to_sdr && clli.maxCLL == 0 && clli.maxPALL == 0;

  avifDiagnostics diag;
  result = avifImageApplyGainMap(&image, image.gainMap, headroom,
                                 swapped->colorPrimaries,
                                 swapped->transferCharacteristics, &swapped_rgb,
                                 (compute_clli ? &clli : nullptr), &diag);
  if (result != AVIF_RESULT_OK) {
    std::cout << "Failed to tone map image: " << avifResultToString(result)
              << " (" << diag.error << ")\n";
    return result;
  }
  result = avifImageRGBToYUV(swapped, &swapped_rgb);
  if (result != AVIF_RESULT_OK) {
    std::cerr << "Failed to convert to YUV: " << avifResultToString(result)
              << "\n";
    return result;
  }

  swapped->clli = clli;
  // Copy the gain map's planes.
  result = avifImageCopy(swapped->gainMap->image, image.gainMap->image,
                         AVIF_PLANES_YUV);
  if (result != AVIF_RESULT_OK) {
    return result;
  }

  // Fill in the information on the alternate image
  result =
      avifRWDataSet(&swapped->gainMap->altICC, image.icc.data, image.icc.size);
  if (result != AVIF_RESULT_OK) {
    return result;
  }
  swapped->gainMap->altColorPrimaries = image.colorPrimaries;
  swapped->gainMap->altTransferCharacteristics = image.transferCharacteristics;
  swapped->gainMap->altMatrixCoefficients = image.matrixCoefficients;
  swapped->gainMap->altYUVRange = image.yuvRange;
  swapped->gainMap->altDepth = image.depth;
  swapped->gainMap->altPlaneCount =
      (image.yuvFormat == AVIF_PIXEL_FORMAT_YUV400) ? 1 : 3;
  swapped->gainMap->altCLLI = image.clli;

  // Swap base and alternate in the gain map
  avifGainMap* gainMap = swapped->gainMap;
  gainMap->useBaseColorSpace = !gainMap->useBaseColorSpace;
  std::swap(gainMap->baseHdrHeadroom, gainMap->alternateHdrHeadroom);
  for (int c = 0; c < 3; ++c) {
    std::swap(gainMap->baseOffset, gainMap->alternateOffset);
  }

  return AVIF_RESULT_OK;
}

SwapBaseCommand::SwapBaseCommand()
    : ProgramCommand(
          "swapbase",
          "Swap the base and alternate images (e.g. if the base image is SDR "
          "and the alternate is HDR, makes the base HDR)",
          "The alternate image is the result of fully applying the gain map. "
          "Images with ICC profiles are not supported: use --ignore-profile "
          "and optionally set --cicp and/or --alt-cicp if needed.") {
  argparse_.add_argument("input_filename");
  argparse_.add_argument("output_filename");
  arg_image_read_.Init(argparse_);
  arg_image_encode_.Init(argparse_, /*can_have_alpha=*/true);
  argparse_.add_argument("--qgain-map")
      .help("Quality for the gain map (0-100, where 100 is lossless)")
      .scan<'d', int>()
      .default_value(60)
      .nargs(1);
  argparse_.add_argument("--cicp")
      .help(
          "Override the input image's CICP values, expressed as "
          "P/T/M where P = color primaries, T = transfer characteristics, "
          "M = matrix coefficients. This will become the CICP of the alternate "
          "image after swapping.")
      .action(ParseCicp);
  argparse_.add_argument("--alt-cicp")
      .help(
          "Override the CICP values for the alternate image in the input image,"
          " expressed as P/T/M where P = color primaries, T = transfer "
          "characteristics, M = matrix coefficients. This will become the CICP "
          "of the base image after swapping.")
      .action(ParseCicp);
  argparse_.add_argument("--clli")
      .help(
          "Override the input image's content light level information, "
          "expressed as:  MaxCLL,MaxPALL.")
      .action(ParseClli);
  argparse_.add_argument("--alt-clli")
      .help(
          "Override content light level information for the alternate image "
          "in the input image, expressed as:  MaxCLL,MaxPALL.")
      .action(ParseClli);
  arg_jobs_.Init(argparse_);
}

avifResult SwapBaseCommand::Run() {
  const std::string input_filename =
      argparse_.get<std::string>("input_filename");
  const std::string output_filename =
      argparse_.get<std::string>("output_filename");

  DecoderPtr decoder(avifDecoderCreate());
  if (decoder == nullptr) {
    return AVIF_RESULT_OUT_OF_MEMORY;
  }
  decoder->maxThreads = arg_jobs_.jobs();
  if (arg_image_read_.ignore_alpha()) {
    decoder->imageContentToDecode &= ~AVIF_IMAGE_CONTENT_ALPHA;
  }
  decoder->imageContentToDecode |= AVIF_IMAGE_CONTENT_GAIN_MAP;
  decoder->ignoreICC = arg_image_read_.ignore_profile();
  decoder->ignoreExif = arg_image_read_.ignore_exif();
  decoder->ignoreXMP = arg_image_read_.ignore_xmp();
  avifResult result = ReadAvif(decoder.get(), input_filename);
  if (result != AVIF_RESULT_OK) {
    return result;
  }

  ImagePtr image(avifImageCreateEmpty());
  if (!image) {
    return AVIF_RESULT_OUT_OF_MEMORY;
  }
  result = avifImageCreateView(image.get(), decoder->image);
  if (result != AVIF_RESULT_OK) {
    return result;
  }

  if (image->gainMap == nullptr || image->gainMap->image == nullptr) {
    std::cerr << "Input image " << input_filename
              << " does not contain a gain map\n";
    return AVIF_RESULT_INVALID_ARGUMENT;
  }

  if (const auto cicp = argparse_.present<CicpValues>("--cicp")) {
    image->colorPrimaries = cicp->color_primaries;
    image->transferCharacteristics = cicp->transfer_characteristics;
    image->matrixCoefficients = cicp->matrix_coefficients;
  }
  if (const auto alt_cicp = argparse_.present<CicpValues>("--alt-cicp")) {
    image->gainMap->altColorPrimaries = alt_cicp->color_primaries;
    image->gainMap->altTransferCharacteristics =
        alt_cicp->transfer_characteristics;
    image->gainMap->altMatrixCoefficients = alt_cicp->matrix_coefficients;
  }

  if (const auto base_clli =
          argparse_.present<avifContentLightLevelInformationBox>("--clli")) {
    image->clli = *base_clli;
  }
  if (const auto alternate_clli =
          argparse_.present<avifContentLightLevelInformationBox>(
              "--alt-clli")) {
    image->gainMap->altCLLI = *alternate_clli;
  }

  int depth = arg_image_read_.depth();
  if (depth == 0) {
    depth = image->gainMap->altDepth;
  }
  if (depth == 0) {
    // Default to the max depth between the base image and the gain map/
    depth = std::max(image->depth, image->gainMap->image->depth);
  }

  avifPixelFormat pixel_format = arg_image_read_.pixel_format();
  if (pixel_format == AVIF_PIXEL_FORMAT_NONE) {
    pixel_format = (image->gainMap->altPlaneCount == 1)
                       ? AVIF_PIXEL_FORMAT_YUV420
                       : AVIF_PIXEL_FORMAT_YUV444;
  }

  ImagePtr new_base(avifImageCreateEmpty());
  if (new_base == nullptr) {
    return AVIF_RESULT_OUT_OF_MEMORY;
  }
  result = ChangeBase(*image, depth, pixel_format, new_base.get());
  if (result != AVIF_RESULT_OK) {
    return result;
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
  result = WriteAvifGrid(new_base.get(), grid.grid_cols, grid.grid_rows,
                         encoder.get(), output_filename);
  if (result != AVIF_RESULT_OK) {
    std::cout << "Failed to encode image: " << avifResultToString(result)
              << " (" << encoder->diag.error << ")\n";
    return result;
  }

  return AVIF_RESULT_OK;
}

}  // namespace avif
