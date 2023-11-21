// Copyright 2023 Google LLC
// SPDX-License-Identifier: BSD-2-Clause

#include "swapbase_command.h"

#include "avif/avif_cxx.h"
#include "imageio.h"

namespace avif {

SwapBaseCommand::SwapBaseCommand()
    : ProgramCommand(
          "swapbase",
          "Swaps the base and alternate images (e.g. if the base image is SDR "
          "and the alternate is HDR, makes the base HDR). The alternate image "
          "is the result ot fully applying the gain map.") {
  argparse_.add_argument(arg_input_filename_, "input_filename");
  argparse_.add_argument(arg_output_filename_, "output_filename");
  arg_image_read_.Init(argparse_);
  arg_image_encode_.Init(argparse_, /*can_have_alpha=*/true);
  argparse_.add_argument(arg_gain_map_quality_, "--qgain-map")
      .help("Quality for the gain map (0-100, where 100 is lossless)")
      .default_value("60");
}

avifResult SwapBaseCommand::Run() {
  DecoderPtr decoder(avifDecoderCreate());
  if (decoder == NULL) {
    return AVIF_RESULT_OUT_OF_MEMORY;
  }
  decoder->enableParsingGainMapMetadata = true;
  decoder->enableDecodingGainMap = true;
  avifResult result = ReadAvif(decoder.get(), arg_input_filename_,
                               arg_image_read_.ignore_profile);
  if (result != AVIF_RESULT_OK) {
    return result;
  }

  if (decoder->image->gainMap.image == nullptr) {
    std::cerr << "Input image " << arg_output_filename_
              << " does not contain a gain map\n";
    return AVIF_RESULT_INVALID_ARGUMENT;
  }

  int depth = arg_image_read_.depth;
  const float headroom =
      static_cast<double>(
          decoder->image->gainMap.metadata.alternateHdrHeadroomN) /
      decoder->image->gainMap.metadata.alternateHdrHeadroomD;
  const bool tone_mapping_to_sdr = (headroom == 0.0f);
  if (depth == 0) {
    depth = tone_mapping_to_sdr
                ? 8
                : std::max(decoder->image->depth,
                           decoder->image->gainMap.image->depth);
  }
  avifPixelFormat pixel_format =
      (avifPixelFormat)arg_image_read_.pixel_format.value();
  if (pixel_format == AVIF_PIXEL_FORMAT_NONE) {
    pixel_format = AVIF_PIXEL_FORMAT_YUV444;
  }
  ImagePtr new_base(avifImageCreate(
      decoder->image->width, decoder->image->height, depth, pixel_format));
  // The gain map's cicp values are those of the 'tmap' item and describe the
  // image obtained by fully applying the gain map. See ISO/IEC JTC 1/SC 29/WG 3
  // m64379v1 4.1.1: A tmap derived item shall be associated with a colr item
  // property. This property describes the colour properties of the
  // reconstructed image if the gain map input item is fully applied according
  // to ISO/AWI 214961.
  if (decoder->image->gainMap.image->transferCharacteristics !=
          AVIF_TRANSFER_CHARACTERISTICS_UNSPECIFIED ||
      decoder->image->gainMap.image->colorPrimaries !=
          AVIF_COLOR_PRIMARIES_UNSPECIFIED ||
      decoder->image->gainMap.image->matrixCoefficients !=
          AVIF_MATRIX_COEFFICIENTS_UNSPECIFIED) {
    new_base->colorPrimaries = decoder->image->gainMap.image->colorPrimaries;
    new_base->transferCharacteristics =
        decoder->image->gainMap.image->transferCharacteristics;
    new_base->matrixCoefficients =
        decoder->image->gainMap.image->matrixCoefficients;
  } else {
    // If there is no cicp on the gain map, use the same values as the old base
    // image, except for the transfer characteristics.
    new_base->colorPrimaries = decoder->image->colorPrimaries;
    const avifTransferCharacteristics transfer_characteristics =
        tone_mapping_to_sdr ? AVIF_TRANSFER_CHARACTERISTICS_SRGB
                            : AVIF_TRANSFER_CHARACTERISTICS_SMPTE2084;
    new_base->transferCharacteristics = transfer_characteristics;
    new_base->matrixCoefficients = decoder->image->matrixCoefficients;
  }

  avifRGBImage new_base_rgb;
  avifRGBImageSetDefaults(&new_base_rgb, new_base.get());

  avifContentLightLevelInformationBox clli =
      decoder->image->gainMap.image->clli;
  const bool compute_clli =
      !tone_mapping_to_sdr && clli.maxCLL == 0 && clli.maxPALL == 0;

  avifDiagnostics diag;
  result =
      avifImageApplyGainMap(decoder->image, &decoder->image->gainMap, headroom,
                            new_base->transferCharacteristics, &new_base_rgb,
                            (compute_clli ? &clli : nullptr), &diag);
  if (result != AVIF_RESULT_OK) {
    std::cout << "Failed to tone map image: " << avifResultToString(result)
              << " (" << diag.error << ")\n";
    return result;
  }
  result = avifImageRGBToYUV(new_base.get(), &new_base_rgb);
  if (result != AVIF_RESULT_OK) {
    std::cerr << "Failed to convert to YUV: " << avifResultToString(result)
              << "\n";
    return result;
  }

  new_base->clli = clli;
  new_base->gainMap = decoder->image->gainMap;
  // new_base has taken ownership of the gain map image, so remove ownership
  // from the old image to prevent a double free.
  decoder->image->gainMap.image = nullptr;
  // Set the gain map's cicp values and clli to the old base image's.
  new_base->gainMap.image->clli = decoder->image->clli;
  new_base->gainMap.image->colorPrimaries = decoder->image->colorPrimaries;
  new_base->gainMap.image->transferCharacteristics =
      decoder->image->transferCharacteristics;
  new_base->gainMap.image->matrixCoefficients =
      decoder->image->matrixCoefficients;

  // Swap base and alternate in the gain map metadata.
  avifGainMapMetadata& metadata = new_base->gainMap.metadata;
  metadata.backwardDirection = !metadata.backwardDirection;
  metadata.useBaseColorSpace = !metadata.useBaseColorSpace;
  std::swap(metadata.baseHdrHeadroomN, metadata.alternateHdrHeadroomN);
  std::swap(metadata.baseHdrHeadroomD, metadata.alternateHdrHeadroomD);
  for (int c = 0; c < 3; ++c) {
    std::swap(metadata.baseOffsetN, metadata.alternateOffsetN);
    std::swap(metadata.baseOffsetD, metadata.alternateOffsetD);
  }

  // Steal metadata.
  std::swap(new_base->xmp, decoder->image->xmp);
  std::swap(new_base->exif, decoder->image->exif);

  EncoderPtr encoder(avifEncoderCreate());
  if (encoder == nullptr) {
    return AVIF_RESULT_OUT_OF_MEMORY;
  }
  encoder->quality = arg_image_encode_.quality;
  encoder->qualityAlpha = arg_image_encode_.quality_alpha;
  encoder->qualityGainMap = arg_gain_map_quality_;
  encoder->speed = arg_image_encode_.speed;
  result = WriteAvif(new_base.get(), encoder.get(), arg_output_filename_);
  if (result != AVIF_RESULT_OK) {
    std::cout << "Failed to encode image: " << avifResultToString(result)
              << " (" << encoder->diag.error << ")\n";
    return result;
  }

  return AVIF_RESULT_OK;
}

}  // namespace avif
