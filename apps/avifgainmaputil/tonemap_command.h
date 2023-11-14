// Copyright 2023 Google LLC
// SPDX-License-Identifier: BSD-2-Clause

#ifndef LIBAVIF_APPS_AVIFGAINMAPUTIL_TONEMAP_COMMAND_H_
#define LIBAVIF_APPS_AVIFGAINMAPUTIL_TONEMAP_COMMAND_H_

#include "avif/avif.h"
#include "program_command.h"

namespace avif {

class TonemapCommand : public ProgramCommand {
 public:
  TonemapCommand();
  avifResult Run() override;

 private:
  argparse::ArgValue<std::string> arg_input_filename_;
  argparse::ArgValue<std::string> arg_output_filename_;
  argparse::ArgValue<float> arg_headroom_;
  argparse::ArgValue<std::string> arg_clli_str_;
  argparse::ArgValue<CicpValues> arg_input_cicp_;
  argparse::ArgValue<CicpValues> arg_output_cicp_;
  ImageReadArgs arg_image_read_;
  BasicImageEncodeArgs arg_image_encode_;
};

}  // namespace avif

#endif  // LIBAVIF_APPS_AVIFGAINMAPUTIL_TONEMAP_COMMAND_H_
