// Copyright 2026 the libavif contributors
// SPDX-License-Identifier: BSD-2-Clause

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <string>

#include "avif/avif.h"
#include "avif/avif_cxx.h"
#include "aviftest_helpers.h"
#include "gtest/gtest.h"

namespace avif {
namespace {

// Used to pass the data folder path to the GoogleTest suites.
const char* data_path = nullptr;

//------------------------------------------------------------------------------

avifResult Parse(const testutil::AvifRwData& encoded) {
  DecoderPtr decoder(avifDecoderCreate());
  if (decoder == nullptr) return AVIF_RESULT_UNKNOWN_ERROR;
  const avifResult result =
      avifDecoderSetIOMemory(decoder.get(), encoded.data, encoded.size);
  if (result != AVIF_RESULT_OK) return result;
  return avifDecoderParse(decoder.get());
}

//------------------------------------------------------------------------------

// An EntityToGroupBox may carry grouping type specific data after the
// entity_id array. The 'pymd' group of ISO/IEC 23008-12 does, the 'prsl' group
// of ISO/IEC 14496-12 does, and libheif writes the former. Every child of
// 'grpl' must therefore be parsed within its own declared box size, so that
// the trailing data is not read as the beginning of the next
// EntityToGroupBox.
TEST(EntityToGroupTest, PyramidGroupWithGroupingTypeSpecificData) {
  const testutil::AvifRwData encoded =
      testutil::ReadFile(std::string(data_path) + "pyramid_pymd.avif");
  ASSERT_NE(encoded.size, size_t{0});
  EXPECT_EQ(Parse(encoded), AVIF_RESULT_OK);
}

//------------------------------------------------------------------------------

// The 'iref' box of color_grid_alpha_nogrid.avif holds three
// SingleItemTypeReferenceBoxes. The first one is a 'dimg' box of 16 bytes: an
// 8 byte header followed by from_item_ID, reference_count and two to_item_IDs.
constexpr const char* kFileName = "color_grid_alpha_nogrid.avif";
constexpr size_t kReferenceBoxOffset = 340;
constexpr uint32_t kReferenceBoxSize = 16;

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

// Changes the declared size of the first SingleItemTypeReferenceBox by delta
// bytes. Nothing else is touched, so the reference fields and every other box
// keep the values of the valid file.
void SetReferenceBoxSizeDelta(int delta, testutil::AvifRwData* encoded) {
  ASSERT_GE(encoded->size, kReferenceBoxOffset + kReferenceBoxSize);
  uint8_t* box = encoded->data + kReferenceBoxOffset;
  // Sanity check: the fixture is the file this test was authored against.
  ASSERT_EQ(std::memcmp(box + 4, "dimg", 4), 0);
  ASSERT_EQ(ReadBE32(box), kReferenceBoxSize);
  WriteBE32(static_cast<uint32_t>(static_cast<int>(kReferenceBoxSize) + delta),
            box);
}

// Control for the two cases below: the file parses as it is, so a failure
// there can only come from the one byte size change.
TEST(ItemReferenceTest, ValidFileParses) {
  const testutil::AvifRwData encoded =
      testutil::ReadFile(std::string(data_path) + kFileName);
  ASSERT_NE(encoded.size, size_t{0});
  EXPECT_EQ(Parse(encoded), AVIF_RESULT_OK);
}

// The declared size is one byte short of what the reference fields need. The
// missing byte must not be taken from the next child box.
TEST(ItemReferenceTest, ReferenceBoxSizeOneByteTooSmall) {
  testutil::AvifRwData encoded =
      testutil::ReadFile(std::string(data_path) + kFileName);
  ASSERT_NE(encoded.size, size_t{0});
  ASSERT_NO_FATAL_FAILURE(SetReferenceBoxSizeDelta(-1, &encoded));
  EXPECT_EQ(Parse(encoded), AVIF_RESULT_BMFF_PARSE_FAILED);
}

// The declared size is one byte more than the reference fields occupy, so the
// box claims the first byte of the next child box.
TEST(ItemReferenceTest, ReferenceBoxSizeOneByteTooLarge) {
  testutil::AvifRwData encoded =
      testutil::ReadFile(std::string(data_path) + kFileName);
  ASSERT_NE(encoded.size, size_t{0});
  ASSERT_NO_FATAL_FAILURE(SetReferenceBoxSizeDelta(1, &encoded));
  EXPECT_EQ(Parse(encoded), AVIF_RESULT_BMFF_PARSE_FAILED);
}

//------------------------------------------------------------------------------

// The 'edts' box contains an 'elst' (EditListBox) child box.
// In colors-animated-8bpc.avif, 'edts' is at offset 526 (size 44) and 'elst' is
// at offset 534 with version 1 (declared size 36).
// In colors-animated-8bpc-audio.avif, 'edts' is at offset 527 (size 36) and
// 'elst' is at offset 535 with version 0 (declared size 28).
constexpr const char* kEditListBoxV1FileName = "colors-animated-8bpc.avif";
constexpr size_t kEdtsBoxV1Offset = 526;
constexpr size_t kEditListBoxV1Offset = 534;
constexpr uint32_t kEditListBoxV1Size = 36;

constexpr const char* kEditListBoxV0FileName = "colors-animated-8bpc-audio.avif";
constexpr size_t kEdtsBoxV0Offset = 527;
constexpr size_t kEditListBoxV0Offset = 535;
constexpr uint32_t kEditListBoxV0Size = 28;

void SetEditListBoxSizeDelta(size_t edtsOffset, size_t elstOffset,
                             uint32_t elstSize, int delta,
                             testutil::AvifRwData* encoded) {
  ASSERT_GE(encoded->size, elstOffset + elstSize);
  uint8_t* parentBox = encoded->data + edtsOffset;
  uint8_t* box = encoded->data + elstOffset;
  ASSERT_EQ(std::memcmp(parentBox + 4, "edts", 4), 0);
  ASSERT_EQ(std::memcmp(box + 4, "elst", 4), 0);
  ASSERT_EQ(ReadBE32(box), elstSize);
  const uint32_t parentSize = ReadBE32(parentBox);
  WriteBE32(static_cast<uint32_t>(static_cast<int>(parentSize) + delta),
            parentBox);
  WriteBE32(static_cast<uint32_t>(static_cast<int>(elstSize) + delta), box);
}

TEST(EditListBoxTest, ValidFileV1Parses) {
  const testutil::AvifRwData encoded =
      testutil::ReadFile(std::string(data_path) + kEditListBoxV1FileName);
  ASSERT_NE(encoded.size, size_t{0});
  EXPECT_EQ(Parse(encoded), AVIF_RESULT_OK);
}

TEST(EditListBoxTest, EditListBoxV1SizeOneByteTooSmall) {
  testutil::AvifRwData encoded =
      testutil::ReadFile(std::string(data_path) + kEditListBoxV1FileName);
  ASSERT_NE(encoded.size, size_t{0});
  ASSERT_NO_FATAL_FAILURE(SetEditListBoxSizeDelta(
      kEdtsBoxV1Offset, kEditListBoxV1Offset, kEditListBoxV1Size, -1, &encoded));
  EXPECT_EQ(Parse(encoded), AVIF_RESULT_BMFF_PARSE_FAILED);
}

TEST(EditListBoxTest, EditListBoxV1SizeOneByteTooLarge) {
  testutil::AvifRwData encoded =
      testutil::ReadFile(std::string(data_path) + kEditListBoxV1FileName);
  ASSERT_NE(encoded.size, size_t{0});
  ASSERT_NO_FATAL_FAILURE(SetEditListBoxSizeDelta(
      kEdtsBoxV1Offset, kEditListBoxV1Offset, kEditListBoxV1Size, 1, &encoded));
  EXPECT_EQ(Parse(encoded), AVIF_RESULT_BMFF_PARSE_FAILED);
}

TEST(EditListBoxTest, ValidFileV0Parses) {
  const testutil::AvifRwData encoded =
      testutil::ReadFile(std::string(data_path) + kEditListBoxV0FileName);
  ASSERT_NE(encoded.size, size_t{0});
  EXPECT_EQ(Parse(encoded), AVIF_RESULT_OK);
}

TEST(EditListBoxTest, EditListBoxV0SizeOneByteTooSmall) {
  testutil::AvifRwData encoded =
      testutil::ReadFile(std::string(data_path) + kEditListBoxV0FileName);
  ASSERT_NE(encoded.size, size_t{0});
  ASSERT_NO_FATAL_FAILURE(SetEditListBoxSizeDelta(
      kEdtsBoxV0Offset, kEditListBoxV0Offset, kEditListBoxV0Size, -1, &encoded));
  EXPECT_EQ(Parse(encoded), AVIF_RESULT_BMFF_PARSE_FAILED);
}

TEST(EditListBoxTest, EditListBoxV0SizeOneByteTooLarge) {
  testutil::AvifRwData encoded =
      testutil::ReadFile(std::string(data_path) + kEditListBoxV0FileName);
  ASSERT_NE(encoded.size, size_t{0});
  ASSERT_NO_FATAL_FAILURE(SetEditListBoxSizeDelta(
      kEdtsBoxV0Offset, kEditListBoxV0Offset, kEditListBoxV0Size, 1, &encoded));
  EXPECT_EQ(Parse(encoded), AVIF_RESULT_BMFF_PARSE_FAILED);
}

TEST(EditListBoxTest, EditListBoxUnsupportedVersion) {
  testutil::AvifRwData encoded =
      testutil::ReadFile(std::string(data_path) + kEditListBoxV1FileName);
  ASSERT_NE(encoded.size, size_t{0});
  ASSERT_GE(encoded.size, kEditListBoxV1Offset + kEditListBoxV1Size);
  uint8_t* box = encoded.data + kEditListBoxV1Offset;
  ASSERT_EQ(std::memcmp(box + 4, "elst", 4), 0);
  box[8] = 2;  // unsupported version
  EXPECT_EQ(Parse(encoded), AVIF_RESULT_BMFF_PARSE_FAILED);
}

TEST(EditListBoxTest, EditListBoxZeroSegmentDuration) {
  testutil::AvifRwData encoded =
      testutil::ReadFile(std::string(data_path) + kEditListBoxV1FileName);
  ASSERT_NE(encoded.size, size_t{0});
  ASSERT_GE(encoded.size, kEditListBoxV1Offset + kEditListBoxV1Size);
  uint8_t* box = encoded.data + kEditListBoxV1Offset;
  ASSERT_EQ(std::memcmp(box + 4, "elst", 4), 0);
  WriteBE32(0, box + 16);
  WriteBE32(0, box + 20);
  EXPECT_EQ(Parse(encoded), AVIF_RESULT_BMFF_PARSE_FAILED);
}

TEST(EditListBoxTest, EditListBoxInvalidEntryCount) {
  testutil::AvifRwData encoded =
      testutil::ReadFile(std::string(data_path) + kEditListBoxV1FileName);
  ASSERT_NE(encoded.size, size_t{0});
  ASSERT_GE(encoded.size, kEditListBoxV1Offset + kEditListBoxV1Size);
  uint8_t* box = encoded.data + kEditListBoxV1Offset;
  ASSERT_EQ(std::memcmp(box + 4, "elst", 4), 0);
  WriteBE32(0, box + 12);  // entry_count = 0
  EXPECT_EQ(Parse(encoded), AVIF_RESULT_BMFF_PARSE_FAILED);
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
