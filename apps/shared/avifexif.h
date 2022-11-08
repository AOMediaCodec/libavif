// Copyright 2022 Google LLC. All rights reserved.
// SPDX-License-Identifier: BSD-2-Clause

#include "avif/avif.h"

// Converts image->transformFlags, image->irot and image->imir to the equivalent Exif orientation value in [1:8].
uint8_t avifImageGetExifOrientationFromIrotImir(const avifImage * image);

// Attempts to parse the image->exif payload until the Exif orientation is found, then sets it to the given value.
avifResult avifSetExifOrientation(avifRWData * exif, uint8_t orientation);
