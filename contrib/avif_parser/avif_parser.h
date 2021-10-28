// Copyright 2021 Google LLC
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     https://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#ifndef AVIF_PARSER_H_  // NOLINT (header_guard)
#define AVIF_PARSER_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

//------------------------------------------------------------------------------

enum AvifParserStatus {
  kAvifParserOk,             // The file was correctly parsed and the requested
                             // information was extracted. It is not guaranteed
                             // that the input bitstream is a valid complete
                             // AVIF file.
  kAvifParserNotEnoughData,  // The input bitstream was correctly parsed until
                             // now but bytes are missing. The request should be
                             // repeated with more input bytes.
  kAvifParserTooComplex,     // The input bitstream was correctly parsed until
                             // now but it is too complex. The parsing was
                             // stopped to avoid any timeout or crash.
  kAvifParserInvalidFile,    // The input bitstream is not a valid AVIF file,
                             // truncated or not.
};

struct AvifParserFeatures {
  uint32_t width, height;  // In number of pixels. Ignores mirror and rotation.
  uint32_t bit_depth;      // Likely 8, 10 or 12 bits per channel per pixel.
  uint32_t num_channels;   // Likely 1 (monochrome), 3 (colored) or 4 (alpha).
};

// Parses the AVIF 'data' and extracts its 'features'.
// 'data' can be partial but must point to the beginning of the AVIF file.
// The 'features' can be parsed in the first 450 bytes of most AVIF files.
// 'features' are set to 0 unless kAvifParserOk is returned.
enum AvifParserStatus AvifParserGetFeatures(
    const uint8_t* data, uint32_t data_size,
    struct AvifParserFeatures* features);

// Same as above with an extra argument 'file_size'. If the latter is known,
// please use this version for extra bitstream validation.
enum AvifParserStatus AvifParserGetFeaturesWithSize(
    const uint8_t* data, uint32_t data_size,
    struct AvifParserFeatures* features, uint32_t file_size);

//------------------------------------------------------------------------------

// If needed, avif_parser.h and avif_parser.c can be merged into a single file:
//   1. Replace this block comment by the content of avif_parser.c
//   2. Discard #include "./avif_parser.h" and move other includes to the top
//   3. Mark AvifParserGetFeatures*() declarations and definitions as static
// This procedure can be useful when only one translation unit uses avif_parser,
// whether it includes the merged .h or the merged code is inserted into a file.

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // AVIF_PARSER_H_  // NOLINT (header_guard)
