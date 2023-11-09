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

}  // namespace avif
