// Copyright 2023 Google LLC
// SPDX-License-Identifier: BSD-2-Clause

#include <cstdint>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

#include "avif/avif.h"
#include "aviftest_helpers.h"
#include "gtest/gtest.h"

namespace avif {
namespace {

// Used to pass the data folder path to the GoogleTest suites.
const char* data_path = nullptr;

TEST(AvifDecodeTest, ColorGridAlphaNoGrid) {
  if (!testutil::Av1DecoderAvailable()) {
    GTEST_SKIP() << "AV1 Codec unavailable, skip test.";
  }
  // Test case from https://github.com/AOMediaCodec/libavif/issues/1203.
  const char* file_name = "color_grid_alpha_nogrid.avif";
  DecoderPtr decoder(avifDecoderCreate());
  ASSERT_NE(decoder, nullptr);
  ASSERT_EQ(avifDecoderSetIOFile(decoder.get(),
                                 (std::string(data_path) + file_name).c_str()),
            AVIF_RESULT_OK);
  ASSERT_EQ(avifDecoderParse(decoder.get()), AVIF_RESULT_OK);
  EXPECT_EQ(decoder->alphaPresent, AVIF_TRUE);
  EXPECT_EQ(decoder->imageSequenceTrackPresent, AVIF_FALSE);
  EXPECT_EQ(avifDecoderNextImage(decoder.get()), AVIF_RESULT_OK);
  EXPECT_NE(decoder->image->alphaPlane, nullptr);
  EXPECT_GT(decoder->image->alphaRowBytes, 0u);
}

TEST(AvifDecodeTest, ImageContentToDecodeNone) {
  if (!testutil::Av1DecoderAvailable()) {
    GTEST_SKIP() << "AV1 Codec unavailable, skip test.";
  }
  for (const std::string file_name :
       {"paris_icc_exif_xmp.avif", "draw_points_idat.avif",
        "sofa_grid1x5_420.avif", "color_grid_alpha_nogrid.avif",
        "seine_sdr_gainmap_srgb.avif", "draw_points_idat_progressive.avif",
        "weld_sato_12B_8B_q0.avif"}) {
    for (
        avifImageContentTypeFlag image_content_to_decode : {
            AVIF_IMAGE_CONTENT_NONE,
            AVIF_IMAGE_CONTENT_SAMPLE_TRANSFORMS  // Equivalent to NONE without
                                                  // AVIF_IMAGE_CONTENT_COLOR_AND_ALPHA.
        }) {
      SCOPED_TRACE(file_name);
      DecoderPtr decoder(avifDecoderCreate());
      ASSERT_NE(decoder, nullptr);
      // Do not decode anything.
      decoder->imageContentToDecode = image_content_to_decode;
      ASSERT_EQ(
          avifDecoderSetIOFile(decoder.get(),
                               (std::string(data_path) + file_name).c_str()),
          AVIF_RESULT_OK);
      ASSERT_EQ(avifDecoderParse(decoder.get()), AVIF_RESULT_OK)
          << decoder->diag.error;
      EXPECT_EQ(decoder->imageSequenceTrackPresent, AVIF_FALSE);
      EXPECT_EQ(avifDecoderNextImage(decoder.get()), AVIF_RESULT_NO_CONTENT);
    }
  }
}

TEST(AvifDecodeTest, ParseEmptyData) {
  DecoderPtr decoder(avifDecoderCreate());
  ASSERT_NE(decoder, nullptr);
  ASSERT_EQ(avifDecoderSetIOMemory(decoder.get(), nullptr, 0), AVIF_RESULT_OK);
  // No ftyp box was seen.
  ASSERT_EQ(avifDecoderParse(decoder.get()), AVIF_RESULT_INVALID_FTYP);
}

TEST(AvifDecodeTest, ImageContentToDecodeAlphaOnly) {
  DecoderPtr decoder(avifDecoderCreate());
  ASSERT_NE(decoder, nullptr);
  ASSERT_EQ(avifDecoderSetIOMemory(decoder.get(), nullptr, 0), AVIF_RESULT_OK);
  decoder->imageContentToDecode = AVIF_IMAGE_CONTENT_ALPHA;
  ASSERT_EQ(avifDecoderParse(decoder.get()), AVIF_RESULT_NOT_IMPLEMENTED);
}

TEST(AvifDecodeTest, Idat) {
  if (!testutil::Av1DecoderAvailable()) {
    GTEST_SKIP() << "AV1 Codec unavailable, skip test.";
  }

  const ImagePtr original = testutil::ReadImage(data_path, "draw_points.png");

  for (const std::string file_name :
       {"draw_points_idat.avif", "draw_points_idat_metasize0.avif",
        "draw_points_idat_progressive.avif",
        "draw_points_idat_progressive_metasize0.avif"}) {
    SCOPED_TRACE(file_name);
    DecoderPtr decoder(avifDecoderCreate());
    ASSERT_NE(decoder, nullptr);
    ASSERT_EQ(avifDecoderSetIOFile(
                  decoder.get(), (std::string(data_path) + file_name).c_str()),
              AVIF_RESULT_OK);
    ASSERT_EQ(avifDecoderParse(decoder.get()), AVIF_RESULT_OK);
    EXPECT_EQ(decoder->alphaPresent, AVIF_TRUE);
    EXPECT_EQ(decoder->imageSequenceTrackPresent, AVIF_FALSE);
    ASSERT_EQ(avifDecoderNextImage(decoder.get()), AVIF_RESULT_OK);
    EXPECT_NE(decoder->image->alphaPlane, nullptr);
    EXPECT_GT(decoder->image->alphaRowBytes, 0u);

    EXPECT_EQ(testutil::GetPsnr(*original, *decoder->image), 99.0);
  }
}

// From https://crbug.com/334281983.
TEST(AvifDecodeTest, PeekCompatibleFileTypeBad1) {
  constexpr uint8_t kData[] = {0x00, 0x00, 0x00, 0x1c, 0x66,
                               0x74, 0x79, 0x70, 0x84, 0xca};
  avifROData input = {kData, sizeof(kData)};
  EXPECT_FALSE(avifPeekCompatibleFileType(&input));
}

// From https://crbug.com/334682511.
TEST(AvifDecodeTest, PeekCompatibleFileTypeBad2) {
  constexpr uint8_t kData[] = {0x00, 0x00, 0x00, 0x20, 0x66, 0x74, 0x79,
                               0x70, 0x61, 0x73, 0x31, 0x6d, 0x00, 0x00,
                               0x08, 0x00, 0xd7, 0x89, 0xdb, 0x7f};
  avifROData input = {kData, sizeof(kData)};
  EXPECT_FALSE(avifPeekCompatibleFileType(&input));
}

struct NonPersistentIO {
  std::vector<uint8_t> data;
  avifROData ro_data;
};

// Every read call will explicitly invalidate the pointer returned by the
// previous read call to ensure non-persistent IO.
avifResult NonPersistentRead(struct avifIO* io, uint32_t flags, uint64_t offset,
                             size_t size, avifROData* out) {
  NonPersistentIO* io_data = reinterpret_cast<NonPersistentIO*>(io->data);
  if (flags != 0 || offset > io_data->ro_data.size) {
    return AVIF_RESULT_IO_ERROR;
  }
  uint64_t available_size = io_data->ro_data.size - offset;
  if (size > available_size) {
    size = static_cast<size_t>(available_size);
  }
  // Clear the existing vector.
  std::vector<uint8_t>().swap(io_data->data);
  // Copy new data into the vector.
  io_data->data.reserve(size);
  io_data->data.assign(io_data->ro_data.data + offset,
                       io_data->ro_data.data + offset + size);
  // Set the output.
  out->data = io_data->data.data();
  out->size = size;
  return AVIF_RESULT_OK;
}

TEST(AvifDecodeTest, NonPersistentIOBug506387278) {
  const testutil::AvifRwData avif =
      testutil::ReadFile(std::string(data_path) + "poc_b_506387278.avif");
  NonPersistentIO io_data;
  io_data.ro_data = {/*.data=*/avif.data, /*.size=*/avif.size};
  avifIO io = {/*.destroy=*/nullptr,
               /*.read=*/NonPersistentRead,
               /*.write=*/nullptr,
               /*.sizeHint=*/avif.size,
               /*.persistent=*/false,
               /*.data=*/&io_data};
  // |io| must outlive the decoder.
  DecoderPtr decoder(avifDecoderCreate());
  ASSERT_NE(decoder, nullptr);
  avifDecoderSetIO(decoder.get(), &io);
  ASSERT_EQ(avifDecoderParse(decoder.get()), AVIF_RESULT_OK);
  EXPECT_EQ(decoder->imageSequenceTrackPresent, AVIF_TRUE);
  EXPECT_EQ(decoder->imageCount, 2);
  if (testutil::Av1DecoderAvailable()) {
    avifResult result = avifDecoderNextImage(decoder.get());
    if (result != AVIF_RESULT_OK) {
      EXPECT_EQ(result, AVIF_RESULT_DECODE_COLOR_FAILED);
      return;
    }
    result = avifDecoderNextImage(decoder.get());
    if (result != AVIF_RESULT_OK) {
      EXPECT_EQ(result, AVIF_RESULT_DECODE_COLOR_FAILED);
      return;
    }
    EXPECT_EQ(avifDecoderNextImage(decoder.get()),
              AVIF_RESULT_NO_IMAGES_REMAINING);
  }
}

//------------------------------------------------------------------------------

uint32_t ReadBE32(const uint8_t* bytes) {
  return (static_cast<uint32_t>(bytes[0]) << 24) |
         (static_cast<uint32_t>(bytes[1]) << 16) |
         (static_cast<uint32_t>(bytes[2]) << 8) |
         static_cast<uint32_t>(bytes[3]);
}

uint16_t ReadBE16(const uint8_t* bytes) {
  return static_cast<uint16_t>((static_cast<uint16_t>(bytes[0]) << 8) |
                               static_cast<uint16_t>(bytes[1]));
}

void WriteBE32(uint32_t value, uint8_t* bytes) {
  bytes[0] = static_cast<uint8_t>(value >> 24);
  bytes[1] = static_cast<uint8_t>(value >> 16);
  bytes[2] = static_cast<uint8_t>(value >> 8);
  bytes[3] = static_cast<uint8_t>(value);
}

// Returns the byte offset of the child box of the given type within the
// [start, start + parent_size) range, or std::string::npos if there is none.
// A full box header (4 extra bytes of version and flags) is skipped if
// full_box is true.
size_t FindBox(const uint8_t* data, size_t start, size_t parent_end,
               const char* type, bool full_box) {
  size_t pos = start + (full_box ? 4 : 0);
  while (pos + 8 <= parent_end) {
    const uint32_t size = ReadBE32(data + pos);
    if (size < 8 || pos + size > parent_end) {
      return std::string::npos;
    }
    if (std::memcmp(data + pos + 4, type, 4) == 0) {
      return pos;
    }
    pos += size;
  }
  return std::string::npos;
}

// The Sample Transform derived image item of weld_sato_12B_8B_q0.avif has two
// 'dimg' input items. This test replaces them by 259 input item IDs (each one
// creates an empty item), which exceeds the 32 input items allowed by the
// format. The input item count used to be stored as a uint8_t, so the count
// 259 wrapped around to 3 and bypassed the "at most 32" validation, leading to
// a misleading error. It is now stored as a uint32_t so that the file is
// cleanly rejected for the right reason.
TEST(AvifDecodeTest, SampleTransformTooManyInputItems) {
  testutil::AvifRwData encoded =
      testutil::ReadFile(std::string(data_path) + "weld_sato_12B_8B_q0.avif");
  ASSERT_NE(encoded.size, size_t{0});

  const size_t meta_offset = FindBox(encoded.data, 0, encoded.size, "meta",
                                     /*full_box=*/false);
  ASSERT_NE(meta_offset, std::string::npos);
  const size_t meta_end = meta_offset + ReadBE32(encoded.data + meta_offset);
  const size_t iref_offset = FindBox(encoded.data, meta_offset + 8, meta_end,
                                     "iref", /*full_box=*/true);
  ASSERT_NE(iref_offset, std::string::npos);
  const size_t iref_end = iref_offset + ReadBE32(encoded.data + iref_offset);
  const size_t dimg_offset = FindBox(encoded.data, iref_offset + 8, iref_end,
                                     "dimg", /*full_box=*/true);
  ASSERT_NE(dimg_offset, std::string::npos);
  ASSERT_EQ(ReadBE32(encoded.data + dimg_offset), size_t{16});
  ASSERT_EQ(ReadBE16(encoded.data + dimg_offset + 8),
            uint16_t{2});  // from_item_ID
  ASSERT_EQ(ReadBE16(encoded.data + dimg_offset + 10),
            uint16_t{2});  // reference_count

  constexpr uint16_t kReferenceCount = 259;
  const uint16_t firstNewItemID = 4;  // Existing item IDs are 1, 2 and 3.
  constexpr size_t kExtraBytes = 2 * (kReferenceCount - 2);

  std::vector<uint8_t> crafted;
  crafted.reserve(encoded.size + kExtraBytes);
  // All bytes before the to_item_ID array, i.e. the 'dimg' box header,
  // from_item_ID and reference_count, with the sizes of the 'dimg', 'iref' and
  // 'meta' boxes grown by the extra reference bytes.
  crafted.insert(crafted.end(), encoded.data, encoded.data + dimg_offset + 12);
  WriteBE32(ReadBE32(crafted.data() + meta_offset) + kExtraBytes,
            crafted.data() + meta_offset);
  WriteBE32(ReadBE32(crafted.data() + iref_offset) + kExtraBytes,
            crafted.data() + iref_offset);
  WriteBE32(16 + kExtraBytes, crafted.data() + dimg_offset);
  crafted[dimg_offset + 10] = static_cast<uint8_t>(kReferenceCount >> 8);
  crafted[dimg_offset + 11] = static_cast<uint8_t>(kReferenceCount & 0xff);
  // 259 distinct to_item_IDs.
  for (uint16_t i = 0; i < kReferenceCount; ++i) {
    const uint16_t itemID = firstNewItemID + i;
    crafted.push_back(static_cast<uint8_t>(itemID >> 8));
    crafted.push_back(static_cast<uint8_t>(itemID & 0xff));
  }
  // All boxes after the 'dimg' box ('iprp', 'grpl', 'mdat').
  crafted.insert(crafted.end(), encoded.data + dimg_offset + 16,
                 encoded.data + encoded.size);

  DecoderPtr decoder(avifDecoderCreate());
  ASSERT_NE(decoder, nullptr);
  decoder->imageContentToDecode =
      AVIF_IMAGE_CONTENT_COLOR_AND_ALPHA | AVIF_IMAGE_CONTENT_SAMPLE_TRANSFORMS;
  ASSERT_EQ(
      avifDecoderSetIOMemory(decoder.get(), crafted.data(), crafted.size()),
      AVIF_RESULT_OK);
  EXPECT_EQ(avifDecoderParse(decoder.get()), AVIF_RESULT_BMFF_PARSE_FAILED);
  EXPECT_NE(std::strstr(decoder->diag.error, "too many input items"), nullptr);
  EXPECT_NE(std::strstr(decoder->diag.error, "got 259"), nullptr);
}

}  // namespace
}  // namespace avif

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  if (argc != 2) {
    std::cerr << "There must be exactly one argument containing the path to "
                 "the test data folder"
              << std::endl;
    return 1;
  }
  avif::data_path = argv[1];
  return RUN_ALL_TESTS();
}
