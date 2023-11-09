// Copyright 2023 Google LLC
// SPDX-License-Identifier: BSD-2-Clause

#ifndef LIBAVIF_APPS_AVIFGAINMAPUTIL_PROGRAM_COMMAND_H_
#define LIBAVIF_APPS_AVIFGAINMAPUTIL_PROGRAM_COMMAND_H_

#include <string>

#include "argparse.hpp"
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
  // 'description' should be a one line description of what the command does.
  ProgramCommand(const std::string& name, const std::string& description);

  virtual ~ProgramCommand() = default;

  // Parses command line arguments. Should be called before Run().
  avifResult ParseArgs(int argc, const char* const argv[]);

  // Runs the command.
  virtual avifResult Run() = 0;

  std::string name() const { return name_; }
  std::string description() const { return description_; }

  // Prints this command's help on stdout.
  void PrintUsage();

 protected:
  argparse::ArgumentParser argparse_;

 private:
  std::string name_;
  std::string description_;
};

}  // namespace avif

#endif  // LIBAVIF_APPS_AVIFGAINMAPUTIL_PROGRAM_COMMAND_H_
