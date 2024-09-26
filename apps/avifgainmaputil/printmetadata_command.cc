// Copyright 2023 Google LLC
// SPDX-License-Identifier: BSD-2-Clause

#include "printmetadata_command.h"

#include <cassert>
#include <iomanip>

#include "avif/avif_cxx.h"

namespace avif {

namespace {
template <typename T>
std::string FormatFraction(T numerator, uint32_t denominator) {
  std::stringstream stream;
  stream << (denominator != 0 ? (double)numerator / denominator : 0)
         << " (as fraction: " << numerator << "/" << denominator << ")";
  return stream.str();
}

template <typename T>
std::string FormatFractions(const T fractions[3]) {
  std::stringstream stream;
  const int w = 40;
  stream << "R " << std::left << std::setw(w)
         << FormatFraction(fractions[0].n, fractions[0].d) << " G " << std::left
         << std::setw(w) << FormatFraction(fractions[1].n, fractions[1].d)
         << " B " << std::left << std::setw(w)
         << FormatFraction(fractions[2].n, fractions[2].d);
  return stream.str();
}
}  // namespace

PrintMetadataCommand::PrintMetadataCommand()
    : ProgramCommand("printmetadata",
                     "Prints the metadata of the gain map of an avif file") {
  argparse_.add_argument(arg_input_filename_, "input_filename");
}

avifResult PrintMetadataCommand::Run() {
  DecoderPtr decoder(avifDecoderCreate());
  if (decoder == NULL) {
    return AVIF_RESULT_OUT_OF_MEMORY;
  }
  decoder->enableParsingGainMapMetadata = true;

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
  if (!decoder->gainMapPresent) {
    std::cerr << "Input image " << arg_input_filename_
              << " does not contain a gain map\n";
    return AVIF_RESULT_INVALID_ARGUMENT;
  }
  assert(decoder->image->gainMap);

  const avifGainMap& gainMap = *decoder->image->gainMap;
  const int w = 20;
  std::cout << " * " << std::left << std::setw(w) << "Base headroom: "
            << FormatFraction(gainMap.baseHdrHeadroom.n,
                              gainMap.baseHdrHeadroom.d)
            << "\n";
  std::cout << " * " << std::left << std::setw(w) << "Alternate headroom: "
            << FormatFraction(gainMap.alternateHdrHeadroom.n,
                              gainMap.alternateHdrHeadroom.d)
            << "\n";
  std::cout << " * " << std::left << std::setw(w)
            << "Gain Map Min: " << FormatFractions(gainMap.gainMapMin) << "\n";
  std::cout << " * " << std::left << std::setw(w)
            << "Gain Map Max: " << FormatFractions(gainMap.gainMapMax) << "\n";
  std::cout << " * " << std::left << std::setw(w)
            << "Base Offset: " << FormatFractions(gainMap.baseOffset) << "\n";
  std::cout << " * " << std::left << std::setw(w)
            << "Alternate Offset: " << FormatFractions(gainMap.alternateOffset)
            << "\n";
  std::cout << " * " << std::left << std::setw(w)
            << "Gain Map Gamma: " << FormatFractions(gainMap.gainMapGamma)
            << "\n";
  std::cout << " * " << std::left << std::setw(w) << "Use Base Color Space: "
            << (gainMap.useBaseColorSpace ? "True" : "False") << "\n";

  return AVIF_RESULT_OK;
}

}  // namespace avif
