// Copyright 2026 the libavif contributors
// SPDX-License-Identifier: BSD-2-Clause

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

#include "avif/avif.h"
#include "avif/avif_cxx.h"
#include "aviftest_helpers.h"
#include "gtest/gtest.h"

namespace avif {
namespace {

// Used to pass the data folder path to the GoogleTest suites.
const char* data_path = nullptr;

//------------------------------------------------------------------------------

// colors-animated-8bpc.avif contains the top-level boxes 'ftyp', 'meta', 'moov'
// and 'mdat', in this order. 'moov' holds one 'trak' whose 'stbl' box contains
// 'stsd', 'stts', 'stsc', 'stsz', 'stco' and 'stss', in this order. The
// constants below are byte offsets in that file.
constexpr const char* kFileName = "colors-animated-8bpc.avif";

struct Box {
  size_t offset;
  const char* type;
  size_t size;  // 0 if not checked.
};
// 'stbl' and its ancestors.
constexpr Box kStblAndAncestors[] = {{286, "moov", 0},
                                     {414, "trak", 0},
                                     {570, "mdia", 0},
                                     {662, "minf", 0},
                                     {726, "stbl", 0}};
// The children of 'stbl'.
constexpr Box kStsd = {734, "stsd", 149};
constexpr Box kStts = {883, "stts", 24};
constexpr Box kStsc = {907, "stsc", 28};
constexpr Box kStsz = {935, "stsz", 40};
constexpr Box kStco = {975, "stco", 20};
constexpr Box kStss = {995, "stss", 20};
constexpr Box kMdat = {1015, "mdat", 0};

// The extent_offset of the only item in 'iloc' and the chunk_offset of the
// only entry in 'stco'. Both point to the first byte of the 'mdat' payload.
constexpr size_t kOffsetsToMdatPayload[] = {132, kStco.offset + 16};
// The sample_description_index of the only entry in 'stsc'.
constexpr size_t kStscSampleDescriptionIndex = kStsc.offset + 24;

uint32_t ReadBE32(const uint8_t* bytes) {
  return (static_cast<uint32_t>(bytes[0]) << 24) |
         (static_cast<uint32_t>(bytes[1]) << 16) |
         (static_cast<uint32_t>(bytes[2]) << 8) |
         static_cast<uint32_t>(bytes[3]);
}

void WriteBE32(uint32_t value, uint8_t* bytes) {
  bytes[0] = static_cast<uint8_t>(value >> 24);
  bytes[1] = static_cast<uint8_t>(value >> 16);
  bytes[2] = static_cast<uint8_t>(value >> 8);
  bytes[3] = static_cast<uint8_t>(value);
}

// Sanity check: the fixture is the file this test was authored against.
void CheckBoxAt(const std::vector<uint8_t>& file, const Box& box) {
  ASSERT_GE(file.size(), box.offset + 8);
  ASSERT_EQ(std::memcmp(file.data() + box.offset + 4, box.type, 4), 0)
      << box.type;
  if (box.size != 0) {
    ASSERT_EQ(ReadBE32(file.data() + box.offset), box.size) << box.type;
  }
}

std::vector<uint8_t> ReadFixture() {
  const testutil::AvifRwData encoded =
      testutil::ReadFile(std::string(data_path) + kFileName);
  return std::vector<uint8_t>(encoded.data, encoded.data + encoded.size);
}

// Returns a copy of box, taken from file.
std::vector<uint8_t> CopyBox(const std::vector<uint8_t>& file, const Box& box) {
  return std::vector<uint8_t>(file.begin() + box.offset,
                              file.begin() + box.offset + box.size);
}

// Inserts box into 'stbl', right after 'stts'. The sizes of 'stbl' and of its
// ancestors grow by the size of box, and so do the offsets to the 'mdat'
// payload because 'mdat' comes after 'moov'. Nothing else is touched.
void InsertBoxAfterStts(const std::vector<uint8_t>& box,
                        std::vector<uint8_t>* file) {
  ASSERT_NO_FATAL_FAILURE(CheckBoxAt(*file, kStts));
  ASSERT_NO_FATAL_FAILURE(CheckBoxAt(*file, kMdat));
  const uint32_t delta = static_cast<uint32_t>(box.size());
  for (const Box& ancestor : kStblAndAncestors) {
    ASSERT_NO_FATAL_FAILURE(CheckBoxAt(*file, ancestor));
    uint8_t* size = file->data() + ancestor.offset;
    WriteBE32(ReadBE32(size) + delta, size);
  }
  for (size_t offset : kOffsetsToMdatPayload) {
    uint8_t* mdat_payload_offset = file->data() + offset;
    ASSERT_EQ(ReadBE32(mdat_payload_offset), kMdat.offset + 8);
    WriteBE32(ReadBE32(mdat_payload_offset) + delta, mdat_payload_offset);
  }
  file->insert(file->begin() + kStts.offset + kStts.size, box.begin(),
               box.end());
}

avifResult Parse(const std::vector<uint8_t>& file) {
  DecoderPtr decoder(avifDecoderCreate());
  if (decoder == nullptr) return AVIF_RESULT_OUT_OF_MEMORY;
  const avifResult result =
      avifDecoderSetIOMemory(decoder.get(), file.data(), file.size());
  if (result != AVIF_RESULT_OK) return result;
  return avifDecoderParse(decoder.get());
}

//------------------------------------------------------------------------------

// Control for InsertBoxAfterStts(): the file stays valid after inserting a
// 'free' box, so a failure in DuplicateBox can only come from the duplicate.
TEST(SampleTableBoxTest, InsertedFreeBoxDecodes) {
  if (!testutil::Av1DecoderAvailable()) {
    GTEST_SKIP() << "AV1 Codec unavailable, skip test.";
  }
  std::vector<uint8_t> file = ReadFixture();
  ASSERT_NO_FATAL_FAILURE(CheckBoxAt(file, kStts));
  std::vector<uint8_t> free_box = CopyBox(file, kStts);
  std::memcpy(free_box.data() + 4, "free", 4);
  ASSERT_NO_FATAL_FAILURE(InsertBoxAfterStts(free_box, &file));
  for (const avifDecoderSource source :
       {AVIF_DECODER_SOURCE_TRACKS, AVIF_DECODER_SOURCE_PRIMARY_ITEM}) {
    SCOPED_TRACE(source);
    DecoderPtr decoder(avifDecoderCreate());
    ASSERT_NE(decoder, nullptr);
    ASSERT_EQ(avifDecoderSetIOMemory(decoder.get(), file.data(), file.size()),
              AVIF_RESULT_OK);
    ASSERT_EQ(avifDecoderSetSource(decoder.get(), source), AVIF_RESULT_OK);
    ASSERT_EQ(avifDecoderParse(decoder.get()), AVIF_RESULT_OK)
        << decoder->diag.error;
    const int expected_image_count =
        source == AVIF_DECODER_SOURCE_TRACKS ? 5 : 1;
    ASSERT_EQ(decoder->imageCount, expected_image_count);
    for (int i = 0; i < expected_image_count; ++i) {
      ASSERT_EQ(avifDecoderNextImage(decoder.get()), AVIF_RESULT_OK)
          << decoder->diag.error;
    }
  }
}

// ISO/IEC 14496-12: at most one of each 'stbl' child, 'stco' and 'co64'
// sharing one slot. Each box is inserted after 'stts', so that 'stbl' holds
// two boxes of that kind. Each of these files was accepted before.
TEST(SampleTableBoxTest, DuplicateBox) {
  const std::vector<uint8_t> original = ReadFixture();
  for (const Box& box : {kStsd, kStts, kStsc, kStsz, kStss}) {
    ASSERT_NO_FATAL_FAILURE(CheckBoxAt(original, box));
  }
  // A chunk offset box with version, flags and entry_count all 0: a copy of
  // 'stco' would add a chunk, which is already rejected for other reasons.
  const auto empty_box = [](const char* type) {
    std::vector<uint8_t> box = {0, 0, 0, 16, 0, 0, 0, 0,
                                0, 0, 0, 0,  0, 0, 0, 0};
    std::memcpy(box.data() + 4, type, 4);
    return box;
  };
  const std::vector<uint8_t> boxes[] = {
      CopyBox(original, kStsd), CopyBox(original, kStts),
      CopyBox(original, kStsc), CopyBox(original, kStsz),
      empty_box("stco"),        empty_box("co64"),
      CopyBox(original, kStss)};
  for (const std::vector<uint8_t>& box : boxes) {
    SCOPED_TRACE(std::string(box.begin() + 4, box.begin() + 8));
    std::vector<uint8_t> file = original;
    ASSERT_NO_FATAL_FAILURE(InsertBoxAfterStts(box, &file));
    EXPECT_EQ(Parse(file), AVIF_RESULT_BMFF_PARSE_FAILED);
  }
}

// ISO/IEC 14496-12 Section 8.7.4.3: The index ranges from 1 to the number of
// sample entries in the Sample Description Box.
TEST(SampleTableBoxTest, SampleDescriptionIndexZero) {
  std::vector<uint8_t> file = ReadFixture();
  ASSERT_NO_FATAL_FAILURE(CheckBoxAt(file, kStsc));
  ASSERT_EQ(ReadBE32(file.data() + kStscSampleDescriptionIndex), 1u);
  WriteBE32(0, file.data() + kStscSampleDescriptionIndex);
  EXPECT_EQ(Parse(file), AVIF_RESULT_BMFF_PARSE_FAILED);
}

//------------------------------------------------------------------------------

}  // namespace
}  // namespace avif

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  if (argc < 2) {
    std::cerr
        << "The path to the test data folder must be provided as an argument"
        << std::endl;
    return 1;
  }
  avif::data_path = argv[1];
  return RUN_ALL_TESTS();
}
