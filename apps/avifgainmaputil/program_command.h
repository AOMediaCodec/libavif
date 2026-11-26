// Copyright 2023 Google LLC
// SPDX-License-Identifier: BSD-2-Clause

#ifndef LIBAVIF_APPS_AVIFGAINMAPUTIL_PROGRAM_COMMAND_H_
#define LIBAVIF_APPS_AVIFGAINMAPUTIL_PROGRAM_COMMAND_H_

#include <argparse/argparse.hpp>
#include <ostream>
#include <sstream>
#include <string>
#include <vector>

#include "avif/avif.h"

namespace avif {

// A command that can be invoked by name (similar to how 'git' has commands like
// 'commit', 'checkout', etc.)
// NOTE: "avifgainmaputil" is currently hardcoded in the implementation (for
// help messages).
class ProgramCommand {
 public:
  // 'name' is the command that should be used to invoke the command on the
  // command line.
  // 'short_description' should be a one line description of what the command
  // does.
  ProgramCommand(const std::string& name, const std::string& short_description,
                 const std::string& long_description = "");

  virtual ~ProgramCommand() = default;

  // Parses command line arguments. Should be called before Run().
  avifResult ParseArgs(int argc, const char* const argv[]);

  // Runs the command.
  virtual avifResult Run() = 0;

  std::string name() const { return name_; }
  std::string short_description() const { return short_description_; }

  // Prints this command's help on stdout.
  void PrintUsage();

 protected:
  argparse::ArgumentParser argparse_;

 private:
  std::string name_;
  std::string short_description_;
};

//------------------------------------------------------------------------------
// Utilities for flag parsing.

// Parses a YUV format string ("444", "422", "420", "400") into avifPixelFormat.
avifPixelFormat ParsePixelFormat(const std::string& str);

struct CicpValues {
  avifColorPrimaries color_primaries;
  avifTransferCharacteristics transfer_characteristics;
  avifMatrixCoefficients matrix_coefficients;
};

// Parses CICP values in the format 'P/T/M'.
CicpValues ParseCicp(const std::string& str);

// Parses content light level information in the format 'maxCLL,maxPALL'.
avifContentLightLevelInformationBox ParseClli(const std::string& str);

struct GridOptions {
  int grid_cols = 1;
  int grid_rows = 1;
};

inline std::ostream& operator<<(std::ostream& os, const GridOptions& grid) {
  return os << grid.grid_cols << "x" << grid.grid_rows;
}

// Parses grid options in the format 'MxN'.
GridOptions ParseGridOptions(const std::string& str);

// Parses a jobs value: an integer or "all".
int ParseJobs(const std::string& str);

// Basic flags for image writing.
struct BasicImageEncodeArgs {
  // can_have_alpha should be true if the image can have alpha and the
  // output format can be avif.
  void Init(argparse::ArgumentParser& argparse, bool can_have_alpha);

  int speed() const;
  int quality() const;
  int quality_alpha() const;
  GridOptions grid() const;

 private:
  const argparse::ArgumentParser* argparse_ = nullptr;
  bool can_have_alpha_ = false;
};

// Flags relevant when reading jpeg/png.
struct ImageReadArgs {
  void Init(argparse::ArgumentParser& argparse);

  int depth() const;
  avifPixelFormat pixel_format() const;
  bool ignore_profile() const;
  bool ignore_alpha() const;
  bool ignore_exif() const;
  bool ignore_xmp() const;

 private:
  const argparse::ArgumentParser* argparse_ = nullptr;
};

// Flags for setting the number of worker threads.
struct JobsArgs {
  void Init(argparse::ArgumentParser& argparse);

  int jobs() const;

 private:
  const argparse::ArgumentParser* argparse_ = nullptr;
};

// Helper to parse flags that contain several delimited values.
template <typename T>
bool ParseList(std::string to_parse, char delim, int expected_num,
               std::vector<T>* out) {
  std::stringstream ss(to_parse);
  std::string part;
  T parsed;
  while (std::getline(ss, part, delim)) {
    std::istringstream is(part);
    is >> parsed;
    if (is.bad()) {
      return false;
    }
    out->push_back(parsed);
  }
  if (expected_num > 0 && (int)out->size() != expected_num) {
    return false;
  }
  return true;
}

}  // namespace avif

#endif  // LIBAVIF_APPS_AVIFGAINMAPUTIL_PROGRAM_COMMAND_H_
