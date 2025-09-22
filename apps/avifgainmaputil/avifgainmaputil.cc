// Copyright 2023 Google LLC
// SPDX-License-Identifier: BSD-2-Clause

#include <iomanip>
#include <string>
#include <vector>

#include "argparse.hpp"
#include "avif/avif.h"
#include "avifutil.h"
#include "combine_command.h"
#include "convert_command.h"
#include "extractgainmap_command.h"
#include "printmetadata_command.h"
#include "program_command.h"
#include "swapbase_command.h"
#include "tonemap_command.h"

namespace avif {
namespace {

class HelpCommand : public ProgramCommand {
 public:
  HelpCommand() : ProgramCommand("help", "Print a command's usage") {}
  avifResult Run() override {
    // Actual implementation is in the main function because it needs access
    // to the list of commands.
    return AVIF_RESULT_OK;
  }
};

void PrintUsage(const std::vector<std::unique_ptr<ProgramCommand>>& commands) {
  std::cout << "\nTool to manipulate AVIF images with HDR gain maps.\n\n";
  std::cout << "usage: avifgainmaputil <command> [options] [arguments...]\n\n";
  std::cout << "Available commands:\n";
  int longest_command_size = 0;
  for (const std::unique_ptr<ProgramCommand>& command : commands) {
    longest_command_size = std::max(longest_command_size,
                                    static_cast<int>(command->name().size()));
  }
  for (const std::unique_ptr<ProgramCommand>& command : commands) {
    std::cout << "  " << std::left << std::setw(longest_command_size)
              << command->name() << "  " << command->short_description()
              << "\n";
  }
  std::cout << "\n";

  std::cout << R"(General concepts:
  Gain maps allow creating HDR (High Dynamic Range) images that look good on any display,
  including SDR (Standard Dynamic Range) displays. Images with gain maps are also backward
  compatible with viewers that do not support gain maps.

  An image with a gain map consists of a "base image", and a "gain map image". The gain map
  image contains information used to "tone map" the base image, in order to adapt it to displays
  with different HDR capabilities. Fully applying the gain map results in a different image
  called the "alternate image". The gain map can also be applied partially, giving a result in
  between the base image and the alternate image.

  Typically, either the base image or the alternate image is SDR, and the other one is HDR.
  Both images have a target "HDR headroom" that they are meant to be displayed on.
  The HDR headroom is the ratio between the maximum brightness of white that the display can
  produce, and the standard SDR white brightness. This value is usually expressed in log2.
  An SDR display has an HDR headroom of 0. An HDR display with a headroom of 1 can produce white
  that is twice as bright as SDR white.

  Viewers that support gain maps will show the base image, or the alternate image, or something
  in between, depending on the display's current HDR headroom and the target headroom of the base
  image and alternate image. Viewers that do not support gain maps will always show the base image.

)";

  avifPrintVersions();
}

}  // namespace
}  // namespace avif

int main(int argc, char** argv) {
  std::vector<std::unique_ptr<avif::ProgramCommand>> commands;
  commands.emplace_back(std::make_unique<avif::HelpCommand>());
  commands.emplace_back(std::make_unique<avif::CombineCommand>());
  commands.emplace_back(std::make_unique<avif::ConvertCommand>());
  commands.emplace_back(std::make_unique<avif::TonemapCommand>());
  commands.emplace_back(std::make_unique<avif::SwapBaseCommand>());
  commands.emplace_back(std::make_unique<avif::ExtractGainMapCommand>());
  commands.emplace_back(std::make_unique<avif::PrintMetadataCommand>());

  if (argc < 2) {
    std::cerr << "Command name missing\n";
    avif::PrintUsage(commands);
    return 1;
  }

  const std::string command_name(argv[1]);
  if (command_name == "help") {
    if (argc >= 3) {
      const std::string sub_command_name(argv[2]);
      for (const auto& command : commands) {
        if (command->name() == sub_command_name) {
          command->PrintUsage();
          return 0;
        }
      }
      std::cerr << "Unknown command " << sub_command_name << "\n";
      avif::PrintUsage(commands);
      return 1;
    } else {
      avif::PrintUsage(commands);
      return 0;
    }
  }

  for (const auto& command : commands) {
    if (command->name() == command_name) {
      try {
        avifResult result = command->ParseArgs(argc - 1, argv + 1);
        if (result == AVIF_RESULT_OK) {
          result = command->Run();
        }

        if (result == AVIF_RESULT_INVALID_ARGUMENT) {
          command->PrintUsage();
        }
        return (result == AVIF_RESULT_OK) ? 0 : 1;
      } catch (const std::invalid_argument& e) {
        std::cerr << e.what() << "\n\n";
        command->PrintUsage();
        return 1;
      }
    }
  }

  std::cerr << "Unknown command " << command_name << "\n";
  avif::PrintUsage(commands);
  return 1;
}
