// Copyright 2023 Google LLC
// SPDX-License-Identifier: BSD-2-Clause

#ifndef LIBAVIF_APPS_AVIFGAINMAPUTIL_IMAGEIO_H_
#define LIBAVIF_APPS_AVIFGAINMAPUTIL_IMAGEIO_H_

#include <string>

#include "avif/avif.h"

namespace avif {

// Writes an image in any of the supported formats based on the file extension.
avifResult WriteImage(const avifImage* image,
                      const std::string& output_filename, int quality,
                      int speed);

}  // namespace avif

#endif  // LIBAVIF_APPS_AVIFGAINMAPUTIL_IMAGEIO_H_
