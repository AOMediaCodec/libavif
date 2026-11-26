// Copyright 2023 Google LLC
// SPDX-License-Identifier: BSD-2-Clause

#include "program_command.h"

#include <iostream>
#include <stdexcept>
#include <vector>

#include "avifutil.h"

namespace avif {

ProgramCommand::ProgramCommand(const std::string& name,
                               const std::string& short_description,
                               const std::string& long_description)
    : argparse_("avifgainmaputil " + name, "",
                argparse::default_arguments::help),
      name_(name),
      short_description_(short_description) {
  argparse_.add_description(long_description.empty()
                                ? short_description + "."
                                : short_description + ".\n" + long_description);
}

// Parses command line arguments. Should be called before Run().
avifResult ProgramCommand::ParseArgs(int argc, const char* const argv[]) {
  argparse_.parse_args(argc, argv);
  return AVIF_RESULT_OK;
}

// Prints this command's help on stdout.
void ProgramCommand::PrintUsage() { std::cout << argparse_; }

avifPixelFormat ParsePixelFormat(const std::string& str) {
  if (str == "444") {
    return AVIF_PIXEL_FORMAT_YUV444;
  } else if (str == "422") {
    return AVIF_PIXEL_FORMAT_YUV422;
  } else if (str == "420") {
    return AVIF_PIXEL_FORMAT_YUV420;
  } else if (str == "400") {
    return AVIF_PIXEL_FORMAT_YUV400;
  }
  throw std::invalid_argument("Invalid YUV format: " + str);
}

CicpValues ParseCicp(const std::string& str) {
  std::vector<uint32_t> cicp_values;
  if (!ParseList(str, '/', 3, &cicp_values)) {
    throw std::invalid_argument(
        "Invalid CICP values, expected format: P/T/M where each "
        "value is a positive integer, got: " +
        str);
  }

  CicpValues cicp = {};
  cicp.color_primaries = (avifColorPrimaries)cicp_values[0];
  cicp.transfer_characteristics = (avifTransferCharacteristics)cicp_values[1];
  cicp.matrix_coefficients = (avifMatrixCoefficients)cicp_values[2];
  return cicp;
}

avifContentLightLevelInformationBox ParseClli(const std::string& str) {
  std::vector<uint16_t> clli;
  if (!ParseList(str, ',', 2, &clli)) {
    throw std::invalid_argument(
        "Invalid CLLI values, expected format: maxCLL,maxPALL where "
        "both maxCLL and maxPALL are positive integers, got: " +
        str);
  }
  avifContentLightLevelInformationBox clli_box = {};
  clli_box.maxCLL = clli[0];
  clli_box.maxPALL = clli[1];
  return clli_box;
}

GridOptions ParseGridOptions(const std::string& str) {
  std::vector<int> grid_dims;
  if (!ParseList(str, 'x', 2, &grid_dims) || grid_dims[0] <= 0 ||
      grid_dims[1] <= 0) {
    throw std::invalid_argument("Invalid grid dimensions: " + str);
  }
  return GridOptions{grid_dims[0], grid_dims[1]};
}

int ParseJobs(const std::string& str) {
  if (str == "all") {
    return avifQueryCPUCount();
  }
  int jobs;
  std::size_t pos = 0;
  try {
    jobs = std::stoi(str, &pos);
  } catch (...) {
    throw std::invalid_argument("Invalid jobs value: " + str);
  }
  if (pos != str.size()) {
    throw std::invalid_argument("Invalid jobs value: " + str);
  }
  if (jobs < 1) {
    jobs = 1;
  }
  return jobs;
}

void BasicImageEncodeArgs::Init(argparse::ArgumentParser& argparse,
                                bool can_have_alpha) {
  argparse_ = &argparse;
  can_have_alpha_ = can_have_alpha;
  argparse.add_argument("-s", "--speed")
      .help("Encoder speed (0-10, slowest-fastest)")
      .scan<'d', int>()
      .default_value(6)
      .nargs(1);
  argparse.add_argument("-q", "--qcolor")
      .help(can_have_alpha ? "Quality for color (0-100, where 100 is lossless)"
                           : "Quality (0-100, where 100 is lossless)")
      .scan<'d', int>()
      .default_value(60)
      .nargs(1);
  if (can_have_alpha) {
    argparse.add_argument("--qalpha")
        .help("Quality for alpha (0-100, where 100 is lossless)")
        .scan<'d', int>()
        .default_value(100)
        .nargs(1);
  }
  argparse.add_argument("--grid")
      .help("Encode a grid AVIF with M cols and N rows, expressed as MxN")
      .action(ParseGridOptions)
      .default_value(GridOptions{1, 1})
      .nargs(1);
}

int BasicImageEncodeArgs::speed() const {
  return argparse_->get<int>("--speed");
}

int BasicImageEncodeArgs::quality() const {
  return argparse_->get<int>("--qcolor");
}

int BasicImageEncodeArgs::quality_alpha() const {
  return can_have_alpha_ ? argparse_->get<int>("--qalpha") : 100;
}

GridOptions BasicImageEncodeArgs::grid() const {
  return argparse_->get<GridOptions>("--grid");
}

void ImageReadArgs::Init(argparse::ArgumentParser& argparse) {
  argparse_ = &argparse;
  argparse.add_argument("-y", "--yuv")
      .help("Output YUV format for avif (default = automatic)")
      .choices("444", "422", "420", "400");
  argparse.add_argument("-d", "--depth")
      .choices(0, 8, 10, 12)
      .help("Output depth (0 = automatic)")
      .scan<'d', int>()
      .default_value(0)
      .nargs(1);
  argparse.add_argument("--ignore-profile")
      .help(
          "If the input file contains an embedded color profile, ignore it "
          "(no-op if absent)")
      .default_value(false)
      .implicit_value(true);
  argparse.add_argument("--ignore-alpha")
      .help(
          "If the input file contains an alpha channel, ignore it "
          "(no-op if absent)")
      .default_value(false)
      .implicit_value(true);
  argparse.add_argument("--ignore-exif")
      .help(
          "If the input file contains embedded Exif metadata, ignore it "
          "(no-op if absent)")
      .default_value(false)
      .implicit_value(true);
  argparse.add_argument("--ignore-xmp")
      .help(
          "If the input file contains embedded XMP metadata, ignore it "
          "(no-op if absent)")
      .default_value(false)
      .implicit_value(true);
}

int ImageReadArgs::depth() const { return argparse_->get<int>("--depth"); }

avifPixelFormat ImageReadArgs::pixel_format() const {
  if (const auto yuv = argparse_->present<std::string>("--yuv")) {
    return ParsePixelFormat(*yuv);
  }
  return AVIF_PIXEL_FORMAT_NONE;
}

bool ImageReadArgs::ignore_profile() const {
  return argparse_->get<bool>("--ignore-profile");
}

bool ImageReadArgs::ignore_alpha() const {
  return argparse_->get<bool>("--ignore-alpha");
}

bool ImageReadArgs::ignore_exif() const {
  return argparse_->get<bool>("--ignore-exif");
}

bool ImageReadArgs::ignore_xmp() const {
  return argparse_->get<bool>("--ignore-xmp");
}

void JobsArgs::Init(argparse::ArgumentParser& argparse) {
  argparse_ = &argparse;
  argparse.add_argument("-j", "--jobs")
      .help(
          "Number of jobs (worker threads), or 'all' to potentially use as "
          "many cores as possible (default: all).")
      .action(ParseJobs);
}

int JobsArgs::jobs() const {
  return argparse_->present<int>("--jobs").value_or(avifQueryCPUCount());
}

}  // namespace avif
