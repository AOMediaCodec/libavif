// Copyright 2023 Google LLC
// SPDX-License-Identifier: BSD-2-Clause

#include "tonemap_command.h"

#include <cmath>

#include "avif/avif_cxx.h"
#include "imageio.h"

namespace avif {

TonemapCommand::TonemapCommand()
    : ProgramCommand("tonemap",
                     "Tone maps an avif image that has a gain map to a "
                     "given HDR headroom (how much brighter the display can go "
                     "compared to an SDR display)") {
  argparse_.add_argument(arg_input_filename_, "input_image");
  argparse_.add_argument(arg_output_filename_, "output_image");
  argparse_.add_argument(arg_headroom_, "--headroom")
      .help(
          "HDR headroom to tone map to. This is log2 of the ratio of HDR to "
          "SDR luminance. 0 means SDR.")
      .default_value("0");
  argparse_
      .add_argument<CicpValues, CicpConverter>(arg_input_cicp_, "--input_cicp")
      .help(
          "Override input CICP values, expressed as P/T/M "
          "where P = color primaries, T = transfer characteristics, "
          "M = matrix coefficients.");
  argparse_
      .add_argument<CicpValues, CicpConverter>(arg_output_cicp_,
                                               "--output_cicp")
      .help(
          "CICP values for the output, expressed as P/T/M "
          "where P = color primaries, T = transfer characteristics, "
          "M = matrix coefficients. P and M are only relevant when saving to "
          "AVIF. "
          "If not specified, 'color primaries' defaults to the base image's "
          "primaries, 'transfer characteristics' defaults to 16 (PQ) if "
          "headroom > 0, or 13 (sRGB) otherwise, 'matrix coefficients' "
          "defaults to 6 (BT601).");
  argparse_.add_argument(arg_clli_str_, "--clli")
      .help(
          "Override content light level information expressed as: "
          "MaxCLL,MaxPALL. Only relevant when saving to AVIF.");
  arg_image_read_.Init(argparse_);
  arg_image_encode_.Init(argparse_, /*can_have_alpha=*/true);
}

avifResult TonemapCommand::Run() {
  avifContentLightLevelInformationBox clli_box = {};
  bool clli_set = false;
  if (arg_clli_str_.value().size() > 0) {
    std::vector<uint32_t> clli;
    if (!ParseList(arg_clli_str_, ',', 2, &clli)) {
      std::cerr << "Invalid clli values, expected format: maxCLL,maxPALL where "
                   "both maxCLL and maxPALL are positive integers, got: "
                << arg_clli_str_ << "\n";
      return AVIF_RESULT_INVALID_ARGUMENT;
    }
    clli_box.maxCLL = clli[0];
    clli_box.maxPALL = clli[1];
    clli_set = true;
  }

  const float headroom = arg_headroom_;
  const bool tone_mapping_to_sdr = (headroom == 0.0f);
  CicpValues cicp = {AVIF_COLOR_PRIMARIES_UNKNOWN,
                     tone_mapping_to_sdr
                         ? AVIF_TRANSFER_CHARACTERISTICS_SRGB
                         : AVIF_TRANSFER_CHARACTERISTICS_SMPTE2084,
                     AVIF_MATRIX_COEFFICIENTS_BT601};
  if (arg_output_cicp_.provenance() == argparse::Provenance::SPECIFIED) {
    cicp = arg_output_cicp_;
  }

  DecoderPtr decoder(avifDecoderCreate());
  if (decoder == NULL) {
    return AVIF_RESULT_OUT_OF_MEMORY;
  }
  decoder->enableDecodingGainMap = true;
  decoder->enableParsingGainMapMetadata = true;
  avifResult result = ReadAvif(decoder.get(), arg_input_filename_,
                               arg_image_read_.ignore_profile);
  if (result != AVIF_RESULT_OK) {
    return result;
  }

  if (arg_input_cicp_.provenance() == argparse::Provenance::SPECIFIED) {
    decoder->image->colorPrimaries = arg_input_cicp_.value().color_primaries;
    decoder->image->transferCharacteristics =
        arg_input_cicp_.value().transfer_characteristics;
    decoder->image->matrixCoefficients =
        arg_input_cicp_.value().matrix_coefficients;
  }

  if (decoder->image->gainMap.image == nullptr) {
    std::cerr << "Input image " << arg_input_filename_
              << " does not contain a gain map\n";
    return AVIF_RESULT_INVALID_ARGUMENT;
  }

  avifGainMapMetadataDouble metadata;
  if (!avifGainMapMetadataFractionsToDouble(
          &metadata, &decoder->image->gainMap.metadata)) {
    std::cerr << "Input image " << arg_input_filename_
              << " has invalid gain map metadata\n";
    return AVIF_RESULT_INVALID_ARGUMENT;
  }
  if (!clli_set) {
    // Use the clli from the base image or the alternate image if the headroom
    // is outside of the [baseHdrHeadroom, alternateHdrHeadroom] range.
    if ((headroom <= metadata.baseHdrHeadroom &&
         metadata.baseHdrHeadroom <= metadata.alternateHdrHeadroom) ||
        (headroom >= metadata.baseHdrHeadroom &&
         metadata.baseHdrHeadroom >= metadata.alternateHdrHeadroom)) {
      clli_box = decoder->image->clli;
    } else if ((headroom <= metadata.alternateHdrHeadroom &&
                metadata.alternateHdrHeadroom <= metadata.baseHdrHeadroom) ||
               (headroom >= metadata.alternateHdrHeadroom &&
                metadata.alternateHdrHeadroom >= metadata.baseHdrHeadroom)) {
      clli_box = decoder->image->gainMap.image->clli;
    }
    clli_set = (clli_box.maxCLL != 0) || (clli_box.maxPALL != 0);
  }

  int depth = arg_image_read_.depth;
  if (depth == 0) {
    depth = tone_mapping_to_sdr ? 8 : decoder->image->depth;
  }
  ImagePtr tone_mapped(
      avifImageCreate(decoder->image->width, decoder->image->height, depth,
                      (avifPixelFormat)arg_image_read_.pixel_format.value()));
  if (tone_mapped == nullptr) {
    return AVIF_RESULT_OUT_OF_MEMORY;
  }
  avifRGBImage tone_mapped_rgb;
  avifRGBImageSetDefaults(&tone_mapped_rgb, tone_mapped.get());
  avifDiagnostics diag;
  result = avifImageApplyGainMap(decoder->image, &decoder->image->gainMap,
                                 arg_headroom_, cicp.transfer_characteristics,
                                 &tone_mapped_rgb,
                                 clli_set ? nullptr : &clli_box, &diag);
  if (result != AVIF_RESULT_OK) {
    std::cout << "Failed to tone map image: " << avifResultToString(result)
              << " (" << diag.error << ")\n";
    return result;
  }
  result = avifImageRGBToYUV(tone_mapped.get(), &tone_mapped_rgb);
  if (result != AVIF_RESULT_OK) {
    std::cerr << "Failed to convert to YUV: " << avifResultToString(result)
              << "\n";
    return result;
  }
  if (cicp.color_primaries == AVIF_COLOR_PRIMARIES_UNKNOWN) {
    // TODO(maryla): for now avifImageApplyGainMap always uses the primaries of
    // the base image, but it should take into account the metadata's
    // useBaseColorSpace property.
    cicp.color_primaries = decoder->image->colorPrimaries;
  }
  tone_mapped->clli = clli_box;
  tone_mapped->transferCharacteristics = cicp.transfer_characteristics;
  tone_mapped->colorPrimaries = cicp.color_primaries;
  tone_mapped->matrixCoefficients = cicp.matrix_coefficients;

  return WriteImage(tone_mapped.get(), arg_output_filename_,
                    arg_image_encode_.quality, arg_image_encode_.speed);
}

}  // namespace avif
