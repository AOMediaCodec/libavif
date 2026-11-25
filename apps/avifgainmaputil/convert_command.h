// Copyright 2023 Google LLC
// SPDX-License-Identifier: BSD-2-Clause

#ifndef LIBAVIF_APPS_AVIFGAINMAPUTIL_CONVERT_COMMAND_H_
#define LIBAVIF_APPS_AVIFGAINMAPUTIL_CONVERT_COMMAND_H_

#include "avif/avif.h"
#include "program_command.h"

namespace avif {

class ConvertCommand : public ProgramCommand {
 public:
  ConvertCommand();
  avifResult Run() override;

 private:
  BasicImageEncodeArgs arg_image_encode_;
  ImageReadArgs arg_image_read_;
  JobsArgs arg_jobs_;
};

}  // namespace avif

#endif  // LIBAVIF_APPS_AVIFGAINMAPUTIL_CONVERT_COMMAND_H_
