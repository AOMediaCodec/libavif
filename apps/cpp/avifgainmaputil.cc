// Copyright 2023 Google LLC
// SPDX-License-Identifier: BSD-2-Clause

#include <iomanip>
#include <string>
#include <vector>

#include "avif/avif.h"
#include "avifjpeg.h"
#include "avifpng.h"
#include "avifutil.h"
#include "popl.hpp"
#include "y4m.h"

#if defined(_WIN32)
#include <locale.h>
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

namespace {

// A command that can be invoked by name (similar to how 'git' has commands like
// 'commit', 'checkout', etc.)
class ProgramCommand {
 public:
  ProgramCommand() : options_("Options") {}
  virtual ~ProgramCommand() = default;

  // Parses command line arguments. Should be called before Run().
  avifResult ParseArgs(int argc, const char* const argv[]) {
    options_.parse(argc, argv);
    // Unknown options are not allowed.
    const std::vector<std::string>& unknown_options =
        options_.unknown_options();
    if (!unknown_options.empty()) {
      std::cerr << "Unknown option" << (unknown_options.size() > 1 ? "s" : "")
                << " " << unknown_options[0];
      for (size_t i = 1; i < unknown_options.size(); ++i) {
        std::cerr << ", " << unknown_options[i];
      }
      std::cerr << "\n";
      return AVIF_RESULT_INVALID_ARGUMENT;
    }
    return AVIF_RESULT_OK;
  }
  // Runs the command.
  virtual avifResult Run() = 0;

  // Returns the name of the command that should be used to invoke it on the
  // command line.
  virtual std::string GetName() = 0;
  // Returns a one line description of what the command does.
  virtual std::string GetDescription() = 0;
  // Returns example parameters to invoke this command.
  virtual std::string GetUsageParams() = 0;
  // Returns a full description of how to use this command.
  std::string GetUsage() {
    return "Usage: avifgainmaputil " + GetName() + " " + GetUsageParams() +
           "\n\n" + options_.help();
  }

 protected:
  popl::OptionParser options_;
};

class HelpCommand : public ProgramCommand {
 public:
  std::string GetName() override { return "help"; }
  std::string GetDescription() override { return "Prints a command's usage"; }
  std::string GetUsageParams() override { return "<command>"; }
  avifResult Run() override {
    // Actual implementation is in the main function because it needs access to
    // the list of commands.
    return AVIF_RESULT_OK;
  }
};

template <typename T>
inline T Clamp(T x, T low, T high) {  // Only exists in C++17.
  return (x < low) ? low : (high < x) ? high : x;
}

// Writes an image in any of the supported formats based on the file extension.
avifResult WriteImage(const avifImage* image, std::string output_filename,
                      int quality, int speed) {
  quality = Clamp(quality, 0, 100);
  speed = Clamp(quality, 0, 10);
  const avifAppFileFormat output_format =
      avifGuessFileFormat(output_filename.c_str());
  if (output_format == AVIF_APP_FILE_FORMAT_UNKNOWN) {
    std::cerr << "Cannot determine output file extension: " << output_filename
              << "\n";
    return AVIF_RESULT_INVALID_ARGUMENT;
  } else if (output_format == AVIF_APP_FILE_FORMAT_Y4M) {
    if (!y4mWrite(output_filename.c_str(), image)) {
      return AVIF_RESULT_UNKNOWN_ERROR;
    }
  } else if (output_format == AVIF_APP_FILE_FORMAT_JPEG) {
    if (!avifJPEGWrite(output_filename.c_str(), image, quality,
                       AVIF_CHROMA_UPSAMPLING_AUTOMATIC)) {
      return AVIF_RESULT_UNKNOWN_ERROR;
    }
  } else if (output_format == AVIF_APP_FILE_FORMAT_PNG) {
    const int compresion_level = Clamp(10 - speed, 0, 9);
    if (!avifPNGWrite(output_filename.c_str(), image, image->depth,
                      AVIF_CHROMA_UPSAMPLING_AUTOMATIC, compresion_level)) {
      return AVIF_RESULT_UNKNOWN_ERROR;
    }
  } else if (output_format == AVIF_APP_FILE_FORMAT_AVIF) {
    std::unique_ptr<avifEncoder, decltype(&avifEncoderDestroy)> encoder(
        avifEncoderCreate(), avifEncoderDestroy);
    if (encoder == nullptr) {
      return AVIF_RESULT_OUT_OF_MEMORY;
    }
    encoder->quality = quality;
    encoder->speed = speed;
    avifRWData encoded = AVIF_DATA_EMPTY;
    std::cout << "Encoding AVIF...\n";
    avifResult result = avifEncoderWrite(encoder.get(), image, &encoded);
    if (result != AVIF_RESULT_OK) {
      std::cerr << "Failed to encode image: " << avifResultToString(result)
                << " (" << encoder->diag.error << ")\n";
      return result;
    }
    std::ofstream f(output_filename, std::ios::binary);
    f.write(reinterpret_cast<char*>(encoded.data), encoded.size);
    if (f.bad()) {
      std::cerr << "Failed to write image " << output_filename << "\n";
      return AVIF_RESULT_UNKNOWN_ERROR;
    }
    std::cout << "Wrote AVIF: " << output_filename << "\n";
  } else {
    std::cerr << "Unsupported output file extension: " << output_filename
              << "\n";
    return AVIF_RESULT_INVALID_ARGUMENT;
  }
  return AVIF_RESULT_OK;
}

class ExtractGainMapCommand : public ProgramCommand {
 public:
  ExtractGainMapCommand() {
    option_quality_ = options_.add<popl::Value<int>>(
        "q", "quality",
        "Image quality (0-100, worst-best) if saving as jpg or avif", 90);
    option_speed_ = options_.add<popl::Value<int>>(
        "s", "speed", "Encoder speed (0-10, slowest-fastest) for avif or png",
        6);
  }

  std::string GetName() override { return "extractgainmap"; }
  std::string GetDescription() override {
    return "Saves the gain map of an avif file as an image";
  }
  std::string GetUsageParams() override {
    return "[options] <input.avif> <output_gainmap.png/jpg/avif>";
  }

  avifResult Run() override {
    const std::vector<std::string>& positional_args =
        options_.non_option_args();
    if (positional_args.size() != 2) {
      std::cerr << "Expected 2 arguments, avif input path, and output image "
                   "path, got "
                << positional_args.size() << " arguments\n";
      return AVIF_RESULT_INVALID_ARGUMENT;
    }

    const std::string& input = positional_args[0];
    const std::string& output = positional_args[1];

    std::unique_ptr<avifDecoder, decltype(&avifDecoderDestroy)> decoder(
        avifDecoderCreate(), avifDecoderDestroy);
    if (decoder == NULL) {
      return AVIF_RESULT_OUT_OF_MEMORY;
    }
    decoder->enableDecodingGainMap = true;

    avifResult result = avifDecoderSetIOFile(decoder.get(), input.c_str());
    if (result != AVIF_RESULT_OK) {
      std::cerr << "Cannot open file for read: " << input << "\n";
      return result;
    }
    result = avifDecoderParse(decoder.get());
    if (result != AVIF_RESULT_OK) {
      std::cerr << "Failed to parse image: " << avifResultToString(result)
                << " (" << decoder->diag.error << ")\n";
      return result;
    }
    result = avifDecoderNextImage(decoder.get());
    if (result != AVIF_RESULT_OK) {
      std::cerr << "Failed to decode image: " << avifResultToString(result)
                << " (" << decoder->diag.error << ")\n";
      return result;
    }

    if (decoder->image->gainMap.image == nullptr) {
      std::cerr << "Input image " << input << " does not contain a gain map\n";
      return AVIF_RESULT_INVALID_ARGUMENT;
    }

    return WriteImage(decoder->image->gainMap.image, output,
                      option_quality_->value(), option_speed_->value());
  }

 private:
  std::shared_ptr<popl::Value<int>> option_quality_;
  std::shared_ptr<popl::Value<int>> option_speed_;
};

template <typename T>
std::string FormatFraction(T numerator, uint32_t denominator) {
  std::stringstream stream;
  stream << (denominator != 0 ? (double)numerator / denominator : 0)
         << " (as fraction: " << numerator << "/" << denominator << ")";
  return stream.str();
}

template <typename T>
std::string FormatFractions(const T numerator[3],
                            const uint32_t denominator[3]) {
  std::stringstream stream;
  const int w = 40;
  stream << "R " << std::left << std::setw(w)
         << FormatFraction(numerator[0], denominator[0]) << " G " << std::left
         << std::setw(w) << FormatFraction(numerator[1], denominator[1])
         << " B " << std::left << std::setw(w)
         << FormatFraction(numerator[1], denominator[1]);
  return stream.str();
}

class PrintMetadataCommand : public ProgramCommand {
 public:
  std::string GetName() override { return "printmetadata"; }
  std::string GetDescription() override {
    return "Prints the metadata of the gain map of an avif file";
  }
  std::string GetUsageParams() override { return "<input.avif>"; }

  avifResult Run() override {
    const std::vector<std::string>& positional_args =
        options_.non_option_args();
    if (positional_args.size() != 1) {
      std::cerr << "Expected 1 argument, the avif input file, got "
                << positional_args.size() << " arguments\n";
      return AVIF_RESULT_INVALID_ARGUMENT;
    }

    const std::string& input = positional_args[0];

    std::unique_ptr<avifDecoder, decltype(&avifDecoderDestroy)> decoder(
        avifDecoderCreate(), avifDecoderDestroy);
    if (decoder == NULL) {
      return AVIF_RESULT_OUT_OF_MEMORY;
    }
    decoder->enableParsingGainMapMetadata = true;

    avifResult result = avifDecoderSetIOFile(decoder.get(), input.c_str());
    if (result != AVIF_RESULT_OK) {
      std::cerr << "Cannot open file for read: " << input << "\n";
      return result;
    }
    result = avifDecoderParse(decoder.get());
    if (result != AVIF_RESULT_OK) {
      std::cerr << "Failed to parse image: " << avifResultToString(result)
                << " (" << decoder->diag.error << ")\n";
      return result;
    }
    if (!decoder->gainMapPresent) {
      std::cerr << "Input image " << input << " does not contain a gain map\n";
      return AVIF_RESULT_INVALID_ARGUMENT;
    }

    const avifGainMapMetadata& metadata = decoder->image->gainMap.metadata;
    const int w = 20;
    std::cout << std::left << std::setw(w) << "Base headroom: "
              << FormatFraction(metadata.baseHdrHeadroomN,
                                metadata.baseHdrHeadroomD)
              << "\n";
    std::cout << std::left << std::setw(w) << "Alternate headroom: "
              << FormatFraction(metadata.alternateHdrHeadroomN,
                                metadata.alternateHdrHeadroomD)
              << "\n";
    std::cout << std::left << std::setw(w) << "Gain Map Min: "
              << FormatFractions(metadata.gainMapMinN, metadata.gainMapMinD)
              << "\n";
    std::cout << std::left << std::setw(w) << "Gain Map Max: "
              << FormatFractions(metadata.gainMapMaxN, metadata.gainMapMaxD)
              << "\n";
    std::cout << std::left << std::setw(w) << "Base Offset: "
              << FormatFractions(metadata.baseOffsetN, metadata.baseOffsetD)
              << "\n";
    std::cout << std::left << std::setw(w) << "Alternate Offset: "
              << FormatFractions(metadata.alternateOffsetN,
                                 metadata.alternateOffsetD)
              << "\n";
    std::cout << std::left << std::setw(w) << "Gain Map Gamma: "
              << FormatFractions(metadata.gainMapGammaN, metadata.gainMapGammaD)
              << "\n";
    std::cout << std::left << std::setw(w) << "Backward Direction: "
              << (metadata.backwardDirection ? "True" : "False") << "\n";

    return AVIF_RESULT_OK;
  }
};

void PrintUsage(const std::vector<std::unique_ptr<ProgramCommand>>& commands) {
  std::cout << "\nExperimental tool to manipulate avif images with HDR gain "
               "maps.\n\n";
  std::cout << "Usage: avifgainmaputil <command> [options] [arguments...]\n\n";
  std::cout << "Available commands:\n";
  int longest_command_size = 0;
  for (const std::unique_ptr<ProgramCommand>& command : commands) {
    longest_command_size =
        std::max(longest_command_size, (int)command->GetName().size());
  }
  for (const std::unique_ptr<ProgramCommand>& command : commands) {
    std::cout << "  " << std::left << std::setw(longest_command_size)
              << command->GetName() << "  " << command->GetDescription()
              << "\n";
  }
  std::cout << "\n";
  avifPrintVersions();
}

}  // namespace

MAIN() {
  std::vector<std::unique_ptr<ProgramCommand>> commands;
  commands.emplace_back(std::make_unique<HelpCommand>());
  commands.emplace_back(std::make_unique<ExtractGainMapCommand>());
  commands.emplace_back(std::make_unique<PrintMetadataCommand>());

  if (argc < 2) {
    std::cerr << "Command name missing\n";
    PrintUsage(commands);
    return 1;
  }

  INIT_ARGV()

  const std::string command_name(argv[1]);
  if (command_name == "help") {
    if (argc >= 3) {
      const std::string sub_command_name(argv[2]);
      for (const std::unique_ptr<ProgramCommand>& command : commands) {
        if (command->GetName() == sub_command_name) {
          std::cout << command->GetDescription() << "\n\n";
          std::cout << command->GetUsage() << "\n";
          return 0;
        }
      }
      std::cerr << "Unknown command " << sub_command_name << "\n";
      PrintUsage(commands);
      return 1;
    } else {
      PrintUsage(commands);
      return 0;
    }
  }

  for (const std::unique_ptr<ProgramCommand>& command : commands) {
    if (command->GetName() == command_name) {
      try {
        avifResult result = command->ParseArgs(argc - 1, argv + 1);
        if (result == AVIF_RESULT_OK) {
          result = command->Run();
        }

        if (result == AVIF_RESULT_INVALID_ARGUMENT) {
          std::cerr << command->GetUsage() << "\n";
        }
        return (result == AVIF_RESULT_OK) ? 0 : 1;
      } catch (const std::invalid_argument& e) {
        std::cerr << e.what() << "\n\n";
        std::cerr << command->GetUsage() << "\n";
        return 1;
      }
    }
  }

  std::cerr << "Unknown command " << command_name << "\n";
  PrintUsage(commands);
  return 1;
}
