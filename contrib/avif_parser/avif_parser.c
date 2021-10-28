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

#include "./avif_parser.h"

#include <stdio.h>
#include <string.h>

// Avoid warnings, no matter how this file is compiled and/or copy-pasted.
#ifdef __cplusplus
#define AVIF_PARSER_NULL nullptr
#else
#define AVIF_PARSER_NULL NULL
#endif

//------------------------------------------------------------------------------

// Status returned when reading the content of a box (or file).
enum AvifParserInternalStatus {
  kFound,     // Input correctly parsed and information retrieved.
  kNotFound,  // Input correctly parsed but information is missing or elsewhere.
  kTruncated,  // Input correctly parsed until missing bytes to continue.
  kAborted,  // Input correctly parsed until stopped to avoid timeout or crash.
  kInvalid,  // Input incorrectly parsed.
};

// What is searched within the content of a box (or file).
enum AvifParserInternalTarget {
  // Target and associated meaning of 'target_id' in AvifParserInternalParse():
  kIsAvifOrAvis,                     // Ignored
  kPrimaryItemId,                    // Ignored
  kWidthHeightOfItem,                // Primary item ID
  kWidthHeightOfProperty,            // Property index of primary item
  kBitDepthNumChannelsOfItem,        // Primary (or tile) item ID
  kBitDepthNumChannelsOfProperty,    // Property index of primary (or tile) item
  kBitDepthNumChannelsOfTileOfItem,  // Item ID of parent of tile
  kHasAlphaChannel                   // Ignored
};

// Reads an unsigner integer from 'input' with most significant bits first.
// 'input' must be at least 'num_bytes'-long.
static uint32_t AvifParserInternalReadBigEndian(const uint8_t* input,
                                                uint32_t num_bytes) {
  uint32_t value = 0;
  for (uint32_t i = 0; i < num_bytes; ++i) {
    value = (value << 8) | input[i];
  }
  return value;
}

// Extracted features and variables global to the whole parsing duration.
struct AvifParserInternalContext {
  struct AvifParserFeatures features;
  uint32_t num_parsed_boxes;
};

//------------------------------------------------------------------------------
// Convenience macros.

#if defined(AVIF_PARSER_LOG_ERROR)  // Toggle to log encountered issues.
static inline void AvifParserInternalLogError(
    const char* file, int line, const char* reason,
    enum AvifParserInternalStatus status) {
  const char* kStr[] = {"Found", "NotFound", "Truncated", "Invalid", "Aborted"};
  printf("  %s:%d: %s because \"%s\"\n", file, line, kStr[status], reason);
  // Set a breakpoint here to catch the first detected issue.
}
#define AVIF_PARSER_CHECK(check_condition, check_status)                   \
  do {                                                                     \
    if (!(check_condition)) {                                              \
      const enum AvifParserInternalStatus status_checked = (check_status); \
      if (status_checked != kFound && status_checked != kNotFound) {       \
        AvifParserInternalLogError(__FILE__, __LINE__, #check_condition,   \
                                   status_checked);                        \
      }                                                                    \
      return status_checked;                                               \
    }                                                                      \
  } while (0)
#else
#define AVIF_PARSER_CHECK(check_condition, check_status) \
  do {                                                   \
    if (!(check_condition)) return (check_status);       \
  } while (0)
#endif

#define AVIF_PARSER_CHECK_STATUS_IS(check_status, expected_status)            \
  do {                                                                        \
    const enum AvifParserInternalStatus status_returned = (check_status);     \
    AVIF_PARSER_CHECK(status_returned == (expected_status), status_returned); \
  } while (0)
#define AVIF_PARSER_CHECK_FOUND(check_status) \
  AVIF_PARSER_CHECK_STATUS_IS((check_status), kFound)
#define AVIF_PARSER_CHECK_NOT_FOUND(check_status) \
  AVIF_PARSER_CHECK_STATUS_IS((check_status), kNotFound)

//------------------------------------------------------------------------------

// Parses the input 'bytes' and iterates over boxes until the 'target' is found.
// 'bytes' can be a file, or the content of a box whose header ends at 'bytes'.
// 'num_bytes' is the number of available 'bytes'.
// 'max_num_bytes' is the size defined by the parent (such as the file size, or
// the box size minus header size of the box whose header ends at 'bytes').
// 'parent_bytes' and 'parent_max_num_bytes' point to the content of the box
// (or file) containing the box whose header ends at 'bytes'.
// 'target_id' is item ID, property idx or none. See AvifParserInternalTarget.
// 'context' contains the extracted features. See AvifParserInternalContext.
// Returns kFound or an error. See AvifParserInternalStatus.
static enum AvifParserInternalStatus
AvifParserInternalParse(  // NOLINT (readability-function-size)
    const uint8_t* bytes, uint32_t num_bytes, uint32_t max_num_bytes,
    const uint8_t* parent_bytes, uint32_t parent_max_num_bytes,
    uint32_t call_depth, enum AvifParserInternalTarget target,
    uint32_t target_id, struct AvifParserInternalContext* context) {
  // "ftyp">"meta">"pitm">"iprp">"ipco">"...." should be the maximum depth,
  // except with a primary item of type grid, possibly recursive.
  AVIF_PARSER_CHECK(call_depth < 6 * 3, kNotFound);  // Recurse 3 times maximum.

  uint32_t position = 0;   // Within 'bytes'.
  uint32_t box_index = 1;  // 1-based index. Used for iterating over properties.
  do {
    // See ISO/IEC 14496-12:2012(E) 4.2
    AVIF_PARSER_CHECK(position + 8 <= max_num_bytes, kInvalid);  // size+fourcc
    AVIF_PARSER_CHECK(position + 4 <= num_bytes, kTruncated);    // 32b size
    const uint32_t box_size =
        AvifParserInternalReadBigEndian(bytes + position, sizeof(uint32_t));
    // Note: 'box_size==1' means 64b size should be read.
    //       'box_size==0' means this box extends to all remaining bytes.
    //       These two use cases are not handled here for simplicity.
    AVIF_PARSER_CHECK(box_size >= 2, kAborted);
    AVIF_PARSER_CHECK(box_size >= 8, kInvalid);  // 32b size + 32b fourcc
    AVIF_PARSER_CHECK(box_size <= 4294967295u - position, kAborted);
    AVIF_PARSER_CHECK(position + box_size <= max_num_bytes, kInvalid);
    AVIF_PARSER_CHECK(position + 8 <= num_bytes, kTruncated);
    const char* const fourcc = (const char*)(bytes + position + 4);

    const int has_fullbox_header =
        !strncmp(fourcc, "meta", 4) || !strncmp(fourcc, "pitm", 4) ||
        !strncmp(fourcc, "ipma", 4) || !strncmp(fourcc, "ispe", 4) ||
        !strncmp(fourcc, "pixi", 4) || !strncmp(fourcc, "iref", 4) ||
        !strncmp(fourcc, "auxC", 4);
    const uint32_t box_header_size = (has_fullbox_header ? 12 : 8);
    AVIF_PARSER_CHECK(box_size >= box_header_size, kInvalid);
    const uint32_t content_position = position + box_header_size;
    AVIF_PARSER_CHECK(content_position <= num_bytes, kTruncated);
    const uint32_t content_size = box_size - box_header_size;
    const uint8_t* const content = bytes + content_position;
    // Avoid timeouts. The maximum number of parsed boxes is arbitrary.
    AVIF_PARSER_CHECK(++context->num_parsed_boxes < 4096, kAborted);

    uint32_t version = 0, flags = 0, skip_box = 0;
    if (has_fullbox_header) {
      version = AvifParserInternalReadBigEndian(bytes + position + 8, 1);
      flags = AvifParserInternalReadBigEndian(bytes + position + 9, 3);
      // See AV1 Image File Format (AVIF) 8.1
      // at https://aomediacodec.github.io/av1-avif/#avif-boxes (available when
      // https://github.com/AOMediaCodec/av1-avif/pull/170 is merged).
      if (!strncmp(fourcc, "meta", 4)) skip_box = (version > 0);
      if (!strncmp(fourcc, "pitm", 4)) skip_box = (version > 1);
      if (!strncmp(fourcc, "ipma", 4)) skip_box = (version > 1);
      if (!strncmp(fourcc, "ispe", 4)) skip_box = (version > 0);
      if (!strncmp(fourcc, "pixi", 4)) skip_box = (version > 0);
      if (!strncmp(fourcc, "iref", 4)) skip_box = (version > 1);
      if (!strncmp(fourcc, "auxC", 4)) skip_box = (version > 0);
    }

    if (skip_box) {
      // Instead of considering this file as invalid, skip unparsable boxes.
    } else if (target == kIsAvifOrAvis && !strncmp(fourcc, "ftyp", 4)) {
      // See ISO/IEC 14496-12:2012(E) 4.3.1
      AVIF_PARSER_CHECK(content_size >= 4, kInvalid);
      for (uint32_t brand = 0; brand < content_size; brand += 4) {
        AVIF_PARSER_CHECK(content_position + brand + 4 <= num_bytes,
                          kTruncated);
        if (!strncmp((const char*)(content + brand), "avif", 4) ||
            !strncmp((const char*)(content + brand), "avis", 4)) {
          // 'data' seems to be an AVIF bitstream.
          // Find the primary ID and its associated 'features' (or tiles).
          AVIF_PARSER_CHECK_FOUND(AvifParserInternalParse(
              bytes, num_bytes, max_num_bytes, parent_bytes,
              parent_max_num_bytes, call_depth + 1, kPrimaryItemId,
              /*target_id=*/0, context));
          // 'features' have been found. Check if there is an alpha layer.
          return AvifParserInternalParse(bytes, num_bytes, max_num_bytes,
                                         parent_bytes, parent_max_num_bytes,
                                         call_depth + 1, kHasAlphaChannel,
                                         /*target_id=*/0, context);
        }
      }
      AVIF_PARSER_CHECK_FOUND(kInvalid);  // Only one "ftyp" allowed per file.
    } else if (((target == kPrimaryItemId || target == kWidthHeightOfItem ||
                 target == kBitDepthNumChannelsOfItem ||
                 target == kBitDepthNumChannelsOfTileOfItem ||
                 target == kHasAlphaChannel) &&
                !strncmp(fourcc, "meta", 4)) ||
               (target == kBitDepthNumChannelsOfTileOfItem &&
                !strncmp(fourcc, "iref", 4)) ||
               ((target == kWidthHeightOfItem ||
                 target == kBitDepthNumChannelsOfItem ||
                 target == kHasAlphaChannel) &&
                !strncmp(fourcc, "iprp", 4)) ||
               ((target == kWidthHeightOfProperty ||
                 target == kBitDepthNumChannelsOfProperty ||
                 target == kHasAlphaChannel) &&
                !strncmp(fourcc, "ipco", 4))) {
      // Recurse into child box.
      const enum AvifParserInternalStatus status = AvifParserInternalParse(
          content, /*num_bytes=*/num_bytes - content_position,
          /*max_num_bytes=*/content_size, /*parent_bytes=*/bytes,
          /*parent_max_num_bytes=*/max_num_bytes, call_depth + 1, target,
          target_id, context);
      // Return any definitive success or failure now. Otherwise continue.
      if (status != kNotFound) return status;

      // According to ISO/IEC 14496-12:2012(E) 8.11.1.1, there is at most one
      // "meta" per file. No "pitm" or "iref" until now means never.
      if (target == kPrimaryItemId ||
          target == kBitDepthNumChannelsOfTileOfItem) {
        AVIF_PARSER_CHECK(!!strncmp(fourcc, "meta", 4), kInvalid);
      }

      // According to ISO/IEC 14496-12:2012(E) 8.11.1.1, there is at most one
      // "meta" per file. According to ISO/IEC 23008-12:2017(E) 9.3.1, there is
      // exactly one "ipco" per "iprp" and at most one "iprp" per "meta".
      // So if no alpha "auxC" was seen until now, there shall be none.
      if (target == kHasAlphaChannel && !strncmp(fourcc, "ipco", 4)) {
        return kFound;  // We found that there is no alpha layer.
      }
    } else if (target == kPrimaryItemId && !strncmp(fourcc, "pitm", 4)) {
      // See ISO/IEC 14496-12:2012(E) 8.11.4.2
      AVIF_PARSER_CHECK(content_size >= 2, kInvalid);
      AVIF_PARSER_CHECK(content_position + 2 <= num_bytes, kTruncated);
      const uint32_t primary_item_id =
          AvifParserInternalReadBigEndian(content + 0, 2);

      // The ID of the primary item was found. Only the primary item is allowed
      // to have image dimensions, so they must be found now.
      // 'bytes' should be the content of the "meta" box so call
      // AvifParserInternalParse() as is.
      AVIF_PARSER_CHECK_FOUND(AvifParserInternalParse(
          bytes, num_bytes, max_num_bytes, parent_bytes, parent_max_num_bytes,
          call_depth + 1, kWidthHeightOfItem, primary_item_id, context));

      // Find the bit depth per pixel and the number of channels of the primary
      // item. Tiles are allowed to have these features too. Return now if they
      // are found for the primary item, otherwise carry on.
      AVIF_PARSER_CHECK_NOT_FOUND(AvifParserInternalParse(
          bytes, num_bytes, max_num_bytes, parent_bytes, parent_max_num_bytes,
          call_depth + 1, kBitDepthNumChannelsOfItem, primary_item_id,
          context));
      // Missing properties for the primary item so look into tiles.
      return AvifParserInternalParse(
          bytes, num_bytes, max_num_bytes, parent_bytes, parent_max_num_bytes,
          call_depth + 1, kBitDepthNumChannelsOfTileOfItem, primary_item_id,
          context);
    } else if ((target == kWidthHeightOfItem ||
                target == kBitDepthNumChannelsOfItem) &&
               !strncmp(fourcc, "ipma", 4)) {
      // See ISO/IEC 23008-12:2017(E) 9.3.2
      AVIF_PARSER_CHECK(content_size >= 4, kInvalid);
      AVIF_PARSER_CHECK(content_position + 4 <= num_bytes, kTruncated);
      const uint32_t entry_count =
          AvifParserInternalReadBigEndian(content + 0, 4);
      uint32_t offset = 4;
      const uint32_t num_bytes_per_id = (version < 1) ? 2 : 4;
      const uint32_t num_bytes_per_index = (flags & 1) ? 2 : 1;
      const uint32_t essential_bit_mask = (flags & 1) ? 0x8000 : 0x80;

      for (uint32_t entry = 0; entry < entry_count; ++entry) {
        AVIF_PARSER_CHECK(content_size >= offset + num_bytes_per_id + 1,
                          kInvalid);
        AVIF_PARSER_CHECK(
            content_position + offset + num_bytes_per_id + 1 <= num_bytes,
            kTruncated);
        const uint32_t item_id =
            AvifParserInternalReadBigEndian(content + offset, num_bytes_per_id);

        offset += num_bytes_per_id;
        const uint32_t association_count =
            AvifParserInternalReadBigEndian(content + offset, 1);
        offset += 1;

        for (uint32_t property = 0; property < association_count; ++property) {
          AVIF_PARSER_CHECK(content_size >= offset + num_bytes_per_index,
                            kInvalid);
          AVIF_PARSER_CHECK(
              content_position + offset + num_bytes_per_index <= num_bytes,
              kTruncated);
          const uint32_t value = AvifParserInternalReadBigEndian(
              content + offset, num_bytes_per_index);
          offset += num_bytes_per_index;

          if (item_id == target_id) {
            // const int essential = (value & essential_bit_mask);  // Unused.
            const uint32_t property_index = (value & ~essential_bit_mask);

            // Call it again at the same "iprp" level to find the associated
            // "ipco", then the "ispe", "pixi" or "av1C" within.
            const enum AvifParserInternalTarget sub_target =
                (target == kWidthHeightOfItem) ? kWidthHeightOfProperty
                                               : kBitDepthNumChannelsOfProperty;
            AVIF_PARSER_CHECK_NOT_FOUND(AvifParserInternalParse(
                bytes, num_bytes, max_num_bytes, parent_bytes,
                parent_max_num_bytes, call_depth + 1, sub_target,
                /*target_id=*/property_index, context));
          }
        }
      }

      // According to ISO/IEC 14496-12:2012(E) 8.11.1.1, there is at most one
      // "meta" per file. According to ISO/IEC 23008-12:2017(E) 9.3.1, there is
      // exactly one "ipma" per "iprp" and at most one "iprp" per "meta".
      // The primary properties shall have been found now.
      if (target == kBitDepthNumChannelsOfItem) {
        // Exception: The bit depth and number of channels may be referenced
        //            in a tile and not in the primary item of type "grid".
        return kNotFound;  // Continue the search at a higher level.
      }
      AVIF_PARSER_CHECK_FOUND(kInvalid);  // Log.
    } else if (target == kWidthHeightOfProperty && box_index == target_id &&
               !strncmp(fourcc, "ispe", 4)) {
      // See ISO/IEC 23008-12:2017(E) 6.5.3.2
      AVIF_PARSER_CHECK(content_size >= 8, kInvalid);
      AVIF_PARSER_CHECK(content_position + 8 <= num_bytes, kTruncated);
      context->features.width = AvifParserInternalReadBigEndian(content + 0, 4);
      context->features.height =
          AvifParserInternalReadBigEndian(content + 4, 4);
      return kFound;
    } else if (target == kBitDepthNumChannelsOfProperty &&
               box_index == target_id && !strncmp(fourcc, "pixi", 4)) {
      // See ISO/IEC 23008-12:2017(E) 6.5.6.2
      AVIF_PARSER_CHECK(content_size >= 1, kInvalid);
      AVIF_PARSER_CHECK(content_position + 1 <= num_bytes, kTruncated);
      context->features.num_channels =
          AvifParserInternalReadBigEndian(content + 0, 1);
      AVIF_PARSER_CHECK(context->features.num_channels >= 1, kInvalid);
      AVIF_PARSER_CHECK(content_size >= 1 + context->features.num_channels,
                        kInvalid);
      AVIF_PARSER_CHECK(
          content_position + 1 + context->features.num_channels <= num_bytes,
          kTruncated);
      context->features.bit_depth =
          AvifParserInternalReadBigEndian(content + 1, 1);
      for (uint32_t i = 1; i < context->features.num_channels; ++i) {
        AVIF_PARSER_CHECK(
            AvifParserInternalReadBigEndian(content + 1 + i, 1) ==
                context->features.bit_depth,
            kInvalid);  // Bit depth should be the same for all channels.
      }
      return kFound;
    } else if (target == kBitDepthNumChannelsOfProperty &&
               box_index == target_id && !strncmp(fourcc, "av1C", 4)) {
      // See AV1 Codec ISO Media File Format Binding 2.3.1
      // at https://aomediacodec.github.io/av1-isobmff/#av1c
      // Only parse the necessary third byte. Assume that the others are valid.
      AVIF_PARSER_CHECK(content_size >= 3, kInvalid);
      AVIF_PARSER_CHECK(content_position + 3 <= num_bytes, kTruncated);
      const uint32_t fields = AvifParserInternalReadBigEndian(content + 2, 1);
      const int high_bitdepth = (fields & 0x40) != 0;
      const int twelve_bit = (fields & 0x20) != 0;
      const int monochrome = (fields & 0x10) != 0;
      AVIF_PARSER_CHECK(twelve_bit || !high_bitdepth, kInvalid);
      context->features.bit_depth = high_bitdepth ? twelve_bit ? 12 : 10 : 8;
      context->features.num_channels = monochrome ? 1 : 3;
      return kFound;
    } else if (target == kBitDepthNumChannelsOfTileOfItem &&
               !strncmp(fourcc, "dimg", 4)) {
      // See ISO/IEC 14496-12:2012(E) 8.11.12.2
      AVIF_PARSER_CHECK(content_size >= 4, kInvalid);
      AVIF_PARSER_CHECK(content_position + 4 <= num_bytes, kTruncated);
      const uint32_t from_item_id =
          AvifParserInternalReadBigEndian(content + 0, 2);
      if (from_item_id == target_id) {
        const uint32_t reference_count =
            AvifParserInternalReadBigEndian(content + 2, 2);
        for (uint32_t i = 0; i < reference_count; ++i) {
          AVIF_PARSER_CHECK(content_size >= 4 + (i + 1) * 2, kInvalid);
          AVIF_PARSER_CHECK(content_position + 4 + (i + 1) * 2 <= num_bytes,
                            kTruncated);
          const uint32_t to_item_id =
              AvifParserInternalReadBigEndian(content + 4 + i * 2, 2);
          AVIF_PARSER_CHECK(parent_bytes != AVIF_PARSER_NULL, kInvalid);
          AVIF_PARSER_CHECK(parent_bytes < bytes, kInvalid);
          AVIF_PARSER_CHECK(parent_max_num_bytes > 0, kInvalid);
          // Go up one level: from "dimg" among "iref" to boxes among "meta".
          // Note: A bad file may contain two reciprocal "dimg". The infinite
          //       loop is prevented thanks to the 'call_depth' check.
          AVIF_PARSER_CHECK_NOT_FOUND(AvifParserInternalParse(
              /*bytes=*/parent_bytes,
              /*num_bytes=*/num_bytes + (bytes - parent_bytes),
              /*max_num_bytes=*/parent_max_num_bytes,
              /*parent_bytes=*/AVIF_PARSER_NULL, /*parent_max_num_bytes=*/0,
              call_depth + 1, kBitDepthNumChannelsOfItem, to_item_id, context));
          // Trying the first tile should be enough. Check others just in case.
        }
      }
    } else if (target == kHasAlphaChannel && !strncmp(fourcc, "auxC", 4)) {
      // See AV1 Image File Format (AVIF) 4
      // at https://aomediacodec.github.io/av1-avif/#auxiliary-images
      const char* kAlphaStr = "urn:mpeg:mpegB:cicp:systems:auxiliary:alpha";
      const uint32_t kAlphaStrLength = 44;  // Includes terminating character.
      if (content_size >= kAlphaStrLength) {
        AVIF_PARSER_CHECK(content_position + kAlphaStrLength <= num_bytes,
                          kTruncated);
        const char* const aux_type = (const char*)content;
        if (strcmp(aux_type, kAlphaStr) == 0) {
          context->features.num_channels += 1;
          return kFound;
        }
      }
    }

    ++box_index;
    position += box_size;
    // File is valid only if the end of the last box is at the same position as
    // the end of the container. Oddities are caught when parsing further.
  } while (position != max_num_bytes);
  AVIF_PARSER_CHECK_FOUND(kNotFound);
}

//------------------------------------------------------------------------------

enum AvifParserStatus AvifParserGetFeatures(
    const uint8_t* data, uint32_t data_size,
    struct AvifParserFeatures* features) {
  // Consider the file to be of maximum size.
  return AvifParserGetFeaturesWithSize(data, data_size, features,
                                       /*file_size=*/4294967295u);
}

enum AvifParserStatus AvifParserGetFeaturesWithSize(
    const uint8_t* data, uint32_t data_size,
    struct AvifParserFeatures* features, uint32_t file_size) {
  if (features != AVIF_PARSER_NULL) memset(features, 0, sizeof(*features));
  if (data == AVIF_PARSER_NULL) return kAvifParserNotEnoughData;
  if (data_size > file_size) data_size = file_size;

  struct AvifParserInternalContext context;
  context.num_parsed_boxes = 0;
  const enum AvifParserInternalStatus status = AvifParserInternalParse(
      /*bytes=*/data, /*num_bytes=*/data_size, /*max_num_bytes=*/file_size,
      /*parent_bytes=*/AVIF_PARSER_NULL, /*parent_max_num_bytes=*/0,
      /*call_depth=*/0, kIsAvifOrAvis, /*target_id=*/0, &context);
  if (status == kNotFound) {
    return (data_size < file_size)      ? kAvifParserNotEnoughData
           : (file_size >= 4294967295u) ? kAvifParserTooComplex
                                        : kAvifParserInvalidFile;
  }
  if (status == kTruncated) return kAvifParserNotEnoughData;
  if (status == kInvalid) return kAvifParserInvalidFile;
  if (status == kAborted) return kAvifParserTooComplex;
  if (features != AVIF_PARSER_NULL) {
    memcpy(features, &context.features, sizeof(*features));
  }
  return kAvifParserOk;
}

//------------------------------------------------------------------------------

#undef AVIF_PARSER_NULL
#undef AVIF_PARSER_CHECK
#undef AVIF_PARSER_CHECK_STATUS_IS
#undef AVIF_PARSER_CHECK_FOUND
#undef AVIF_PARSER_CHECK_NOT_FOUND
