// Copyright 2023 Google LLC
// SPDX-License-Identifier: BSD-2-Clause

#include "tonemap_command.h"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <string>

#include "avif/avif_cxx.h"
#include "avifutil.h"
#include "imageio.h"

namespace avif {

TonemapCommand::TonemapCommand()
    : ProgramCommand(
          "tonemap",
          "Tone map an AVIF image that has a gain map to a given HDR headroom "
          "(how much brighter the display can go compared to SDR white)",
          "Images with ICC  profiles are not supported: use --ignore-profile "
          "and optionally set --cicp-input and/or --cicp-output if needed.") {
  argparse_.add_argument("input_image");
  argparse_.add_argument("output_image");
  argparse_.add_argument("--headroom")
      .help(
          "HDR headroom to tone map to. This is log2 of the ratio of HDR to "
          "SDR luminance. 0 means SDR.")
      .scan<'g', float>()
      .default_value(0.0f)
      .nargs(1);
  argparse_.add_argument("--cicp-input")
      .help(
          "Override input CICP values, expressed as P/T/M "
          "where P = color primaries, T = transfer characteristics, "
          "M = matrix coefficients.")
      .action(ParseCicp);
  argparse_.add_argument("--cicp-output")
      .help(
          "CICP values for the output, expressed as P/T/M "
          "where P = color primaries, T = transfer characteristics, "
          "M = matrix coefficients. P and M are only relevant when saving to "
          "AVIF. "
          "If not specified, 'color primaries' defaults to the base image's "
          "primaries, 'transfer characteristics' defaults to 16 (PQ) if "
          "headroom > 0, or 13 (sRGB) otherwise, 'matrix coefficients' "
          "defaults to 6 (BT601).")
      .action(ParseCicp);
  argparse_.add_argument("--clli")
      .help(
          "Override content light level information expressed as: "
          "MaxCLL,MaxPALL. Only relevant when saving to AVIF.")
      .action(ParseClli);
  arg_image_read_.Init(argparse_);
  arg_image_encode_.Init(argparse_, /*can_have_alpha=*/true);
  arg_jobs_.Init(argparse_);
}

avifResult TonemapCommand::Run() {
  const std::string input_filename = argparse_.get<std::string>("input_image");
  const std::string output_filename =
      argparse_.get<std::string>("output_image");
  const auto clli_opt =
      argparse_.present<avifContentLightLevelInformationBox>("--clli");
  avifContentLightLevelInformationBox clli_box =
      clli_opt.value_or(avifContentLightLevelInformationBox{});
  bool clli_set = clli_opt.has_value();

  const float headroom = argparse_.get<float>("--headroom");
  const bool tone_mapping_to_hdr = (headroom > 0.0f);

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

  if (const auto input_cicp = argparse_.present<CicpValues>("--cicp-input")) {
    image->colorPrimaries = input_cicp->color_primaries;
    image->transferCharacteristics = input_cicp->transfer_characteristics;
    image->matrixCoefficients = input_cicp->matrix_coefficients;
  }

  if (image->gainMap == nullptr || image->gainMap->image == nullptr) {
    std::cerr << "Input image " << input_filename
              << " does not contain a gain map\n";
    return AVIF_RESULT_INVALID_ARGUMENT;
  }
  if (image->gainMap->baseHdrHeadroom.d == 0 ||
      image->gainMap->alternateHdrHeadroom.d == 0) {
    return AVIF_RESULT_INVALID_ARGUMENT;
  }

  const float base_hdr_hreadroom =
      static_cast<float>(image->gainMap->baseHdrHeadroom.n) /
      image->gainMap->baseHdrHeadroom.d;
  const float alternate_hdr_hreadroom =
      static_cast<float>(image->gainMap->alternateHdrHeadroom.n) /
      image->gainMap->alternateHdrHeadroom.d;
  // We are either tone mapping to the base image (i.e. leaving it as is),
  // or tone mapping to the alternate image (i.e. fully applying the gain map),
  // or tone mapping in between (partially applying the gain map).
  const bool tone_mapping_to_base =
      (headroom <= base_hdr_hreadroom &&
       base_hdr_hreadroom <= alternate_hdr_hreadroom) ||
      (headroom >= base_hdr_hreadroom &&
       base_hdr_hreadroom >= alternate_hdr_hreadroom);
  const bool tone_mapping_to_alternate =
      (headroom <= alternate_hdr_hreadroom &&
       alternate_hdr_hreadroom <= base_hdr_hreadroom) ||
      (headroom >= alternate_hdr_hreadroom &&
       alternate_hdr_hreadroom >= base_hdr_hreadroom);
  const bool base_is_hdr = (base_hdr_hreadroom != 0.0f);

  // Determine output CICP.
  CicpValues cicp;
  if (const auto output_cicp = argparse_.present<CicpValues>("--cicp-output")) {
    cicp = *output_cicp;  // User provided values.
  } else if (tone_mapping_to_base || (tone_mapping_to_hdr && base_is_hdr)) {
    cicp = {image->colorPrimaries, image->transferCharacteristics,
            image->matrixCoefficients};
  } else {
    cicp = {image->gainMap->altColorPrimaries,
            image->gainMap->altTransferCharacteristics,
            image->gainMap->altMatrixCoefficients};
  }
  if (cicp.color_primaries == AVIF_COLOR_PRIMARIES_UNSPECIFIED) {
    cicp.color_primaries = image->gainMap->useBaseColorSpace
                               ? image->colorPrimaries
                               : image->gainMap->altColorPrimaries;
  }
  if (cicp.transfer_characteristics ==
      AVIF_TRANSFER_CHARACTERISTICS_UNSPECIFIED) {
    cicp.transfer_characteristics = static_cast<avifTransferCharacteristics>(
        tone_mapping_to_hdr ? AVIF_TRANSFER_CHARACTERISTICS_PQ
                            : AVIF_TRANSFER_CHARACTERISTICS_SRGB);
  }

  // Determine output depth.
  int depth = arg_image_read_.depth();
  if (depth == 0) {
    if (tone_mapping_to_base) {
      depth = image->depth;
    } else if (tone_mapping_to_alternate) {
      depth = image->gainMap->altDepth;
    }
    if (depth == 0) {
      // Default to the max depth between the base image, the gain map and
      // the specified 'altDepth'.
      depth = std::max(std::max(image->depth, image->gainMap->image->depth),
                       image->gainMap->altDepth);
    }
  }

  // Determine output pixel format.
  avifPixelFormat pixel_format = arg_image_read_.pixel_format();
  const avifPixelFormat alt_yuv_format =
      (image->gainMap->altPlaneCount == 1)
          ? AVIF_PIXEL_FORMAT_YUV400
          // Favor the least chroma subsampled format.
          : std::min(image->yuvFormat, image->gainMap->image->yuvFormat);
  if (pixel_format == AVIF_PIXEL_FORMAT_NONE) {
    if (tone_mapping_to_base) {
      pixel_format = image->yuvFormat;
    } else if (tone_mapping_to_alternate) {
      pixel_format = alt_yuv_format;
    }
    if (pixel_format == AVIF_PIXEL_FORMAT_NONE) {
      // Default to the least chroma subsampled format provided.
      pixel_format =
          std::min(std::min(image->yuvFormat, image->gainMap->image->yuvFormat),
                   alt_yuv_format);
    }
  }

  // Use the clli from the base image or the alternate image if the headroom
  // is outside of the (baseHdrHeadroom, alternateHdrHeadroom) range.
  if (!clli_set) {
    if (tone_mapping_to_base) {
      clli_box = image->clli;
    } else if (tone_mapping_to_alternate) {
      clli_box = image->gainMap->image->clli;
    }
  }
  clli_set = (clli_box.maxCLL != 0) || (clli_box.maxPALL != 0);

  ImagePtr tone_mapped(
      avifImageCreate(image->width, image->height, depth, pixel_format));
  if (tone_mapped == nullptr) {
    return AVIF_RESULT_OUT_OF_MEMORY;
  }
  tone_mapped->colorPrimaries = cicp.color_primaries;
  tone_mapped->transferCharacteristics = cicp.transfer_characteristics;
  tone_mapped->matrixCoefficients = cicp.matrix_coefficients;
  tone_mapped->clli = clli_box;

  avifRGBImage tone_mapped_rgb;
  RGBImageCleanup rgb_cleanup(&tone_mapped_rgb);
  avifRGBImageSetDefaults(&tone_mapped_rgb, tone_mapped.get());
  if (image->alphaPlane == nullptr) {
    tone_mapped_rgb.format = AVIF_RGB_FORMAT_RGB;
  }
  avifDiagnostics diag;
  result = avifImageApplyGainMap(
      image.get(), image->gainMap, headroom, cicp.color_primaries,
      cicp.transfer_characteristics, &tone_mapped_rgb,
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
  result = avifImageSetMetadataXMP(tone_mapped.get(), image->xmp.data,
                                   image->xmp.size);
  if (result != AVIF_RESULT_OK) {
    std::cerr << "Failed to copy XMP: " << avifResultToString(result) << "\n";
    return result;
  }
  result = avifImageSetMetadataExif(tone_mapped.get(), image->exif.data,
                                    image->exif.size);
  if (result != AVIF_RESULT_OK) {
    std::cerr << "Failed to copy EXIF: " << avifResultToString(result) << "\n";
    return result;
  }

  const GridOptions grid = arg_image_encode_.grid();
  return WriteImage(tone_mapped.get(), grid.grid_cols, grid.grid_rows,
                    output_filename, arg_image_encode_.quality(),
                    arg_image_encode_.speed(), arg_jobs_.jobs());
}

}  // namespace avif
