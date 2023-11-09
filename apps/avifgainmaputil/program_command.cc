#include "program_command.h"

namespace avif {

ProgramCommand::ProgramCommand(const std::string& name,
                               const std::string& description)
    : argparse_(
          argparse::ArgumentParser("avifgainmaputil " + name, description)),
      name_(name),
      description_(description) {}

// Parses command line arguments. Should be called before Run().
avifResult ProgramCommand::ParseArgs(int argc, const char* const argv[]) {
  argparse_.parse_args(argc, argv);
  return AVIF_RESULT_OK;
}

// Prints this command's help on stdout.
void ProgramCommand::PrintUsage() { argparse_.print_help(); }

argparse::ConvertedValue<int> PixelFormatConverter::from_str(std::string str) {
  argparse::ConvertedValue<int> converted_value;

  if (str == "444") {
    converted_value.set_value(AVIF_PIXEL_FORMAT_YUV444);
  } else if (str == "422") {
    converted_value.set_value(AVIF_PIXEL_FORMAT_YUV422);
  } else if (str == "420") {
    converted_value.set_value(AVIF_PIXEL_FORMAT_YUV420);
  } else if (str == "400") {
    converted_value.set_value(AVIF_PIXEL_FORMAT_YUV400);
  } else {
    converted_value.set_error("Invalid argument value");
  }
  return converted_value;
}

std::vector<std::string> PixelFormatConverter::default_choices() {
  return {"444", "422", "420", "400"};
}

}  // namespace avif
