// Copyright 2026 the libavif contributors
// SPDX-License-Identifier: BSD-2-Clause

// Regression tests for the resources (memory, CPU) spent by avifDecoderParse()
// on the item related boxes of a 'meta' box. The cost must stay (close to)
// linear in the size of the file:
//  - an 'ipma' entry associating the same opaque property several times to
//    an item must not make that item hold several copies of the property,
//    and the payload of an opaque property associated with many items must
//    not be copied once per item;
//  - the lookup of an item by its ID must not be a linear scan of all items.

#include <chrono>
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

#if defined(__linux__)
#include <sys/resource.h>
#endif

namespace avif {
namespace {

// Used to pass the data folder path to the GoogleTest suites.
const char* data_path = nullptr;

// colors_sdr_srgb.avif: iloc version 0 with 4-byte offsets and lengths, a
// single ipma (version 0, flags 0) with a single entry for item 1, and ipco
// holding 4 properties.
constexpr const char* kFileName = "colors_sdr_srgb.avif";

//------------------------------------------------------------------------------

using Bytes = std::vector<uint8_t>;

void AppendBE16(Bytes* b, uint32_t v) {
  b->push_back(static_cast<uint8_t>(v >> 8));
  b->push_back(static_cast<uint8_t>(v));
}
void AppendBE32(Bytes* b, uint32_t v) {
  AppendBE16(b, v >> 16);
  AppendBE16(b, v & 0xffff);
}
uint32_t ReadBE32(const Bytes& b, size_t offset) {
  return (static_cast<uint32_t>(b[offset]) << 24) |
         (static_cast<uint32_t>(b[offset + 1]) << 16) |
         (static_cast<uint32_t>(b[offset + 2]) << 8) |
         static_cast<uint32_t>(b[offset + 3]);
}
void WriteBE32(Bytes* b, size_t offset, uint32_t v) {
  (*b)[offset] = static_cast<uint8_t>(v >> 24);
  (*b)[offset + 1] = static_cast<uint8_t>(v >> 16);
  (*b)[offset + 2] = static_cast<uint8_t>(v >> 8);
  (*b)[offset + 3] = static_cast<uint8_t>(v);
}

// Returns the offset of the first box of the given type in [begin, end), or 0.
size_t FindBox(const Bytes& b, const char* type, size_t begin, size_t end) {
  for (size_t o = begin; o + 8 <= end;) {
    const uint32_t size = ReadBE32(b, o);
    if (size < 8 || o + size > end) return 0;
    if (std::memcmp(&b[o + 4], type, 4) == 0) return o;
    o += size;
  }
  return 0;
}

// Appends 'num_properties_bytes' of already serialized boxes to ipco and
// 'ipma_entries' (already serialized ipma entries, 'num_ipma_entries' of
// them) to the ipma of kFileName, and fixes the size of the parent boxes and
// the offset in iloc, which is relative to the beginning of the file.
// ipma entries must have item IDs larger than 1 (the primary item) and
// increasing.
void Inject(const Bytes& new_ipco_boxes, const Bytes& new_ipma_entries,
            uint32_t num_ipma_entries, Bytes* file) {
  const size_t meta = FindBox(*file, "meta", 0, file->size());
  ASSERT_NE(meta, size_t{0});
  const size_t meta_end = meta + ReadBE32(*file, meta);
  const size_t iprp = FindBox(*file, "iprp", meta + 12, meta_end);
  ASSERT_NE(iprp, size_t{0});
  const size_t ipco = FindBox(*file, "ipco", iprp + 8, meta_end);
  const size_t ipma = FindBox(*file, "ipma", iprp + 8, meta_end);
  const size_t iloc = FindBox(*file, "iloc", meta + 12, meta_end);
  ASSERT_NE(ipco, size_t{0});
  ASSERT_NE(ipma, size_t{0});
  ASSERT_NE(iloc, size_t{0});
  ASSERT_LT(ipco, ipma);  // So that inserting in ipma does not move ipco.
  const size_t ipco_end = ipco + ReadBE32(*file, ipco);
  ASSERT_EQ(ipco_end, ipma);  // ipma directly follows ipco.
  const size_t ipma_end = ipma + ReadBE32(*file, ipma);
  // Version 0, flags 0, one entry (item 1).
  ASSERT_EQ(ReadBE32(*file, ipma + 8), 0u);
  ASSERT_EQ(ReadBE32(*file, ipma + 12), 1u);
  // iloc version 0, offset_size = length_size = 4, base_offset_size = 0, a
  // single item with a single extent.
  ASSERT_EQ((*file)[iloc + 8], 0);
  ASSERT_EQ((*file)[iloc + 12], 0x44);
  ASSERT_EQ((*file)[iloc + 13], 0x00);
  ASSERT_EQ(ReadBE32(*file, iloc + 14) >> 16, 1u);
  const size_t extent_offset_pos = iloc + 8 + 4 + 2 + 2 + 2 + 2 + 2;
  const uint32_t growth =
      static_cast<uint32_t>(new_ipco_boxes.size() + new_ipma_entries.size());

  Bytes out(file->begin(), file->begin() + ipco_end);
  out.insert(out.end(), new_ipco_boxes.begin(), new_ipco_boxes.end());
  out.insert(out.end(), file->begin() + ipma, file->begin() + ipma_end);
  out.insert(out.end(), new_ipma_entries.begin(), new_ipma_entries.end());
  out.insert(out.end(), file->begin() + ipma_end, file->end());
  WriteBE32(&out, meta, ReadBE32(out, meta) + growth);
  WriteBE32(&out, iprp, ReadBE32(out, iprp) + growth);
  WriteBE32(&out, ipco, ReadBE32(out, ipco) + new_ipco_boxes.size());
  const size_t new_ipma = ipma + new_ipco_boxes.size();
  WriteBE32(&out, new_ipma, ReadBE32(out, new_ipma) + new_ipma_entries.size());
  WriteBE32(&out, new_ipma + 12, 1 + num_ipma_entries);
  WriteBE32(&out, extent_offset_pos, ReadBE32(out, extent_offset_pos) + growth);
  *file = out;
}

Bytes OpaqueProperty(const char* type, size_t payload_size) {
  Bytes b;
  AppendBE32(&b, static_cast<uint32_t>(8 + payload_size));
  b.insert(b.end(), type, type + 4);
  b.insert(b.end(), payload_size, 'A');
  return b;
}

// An ipma (version 0) entry for an item without any other box, associating
// the 1-based property index 'property_index' 'num_associations' times.
void AppendIpmaEntry(uint32_t item_id, uint8_t property_index,
                     uint8_t num_associations, Bytes* entries) {
  AppendBE16(entries, item_id);
  entries->push_back(num_associations);
  entries->insert(entries->end(), num_associations, property_index);
}

DecoderPtr ParseBytes(const Bytes& file, avifResult* result) {
  DecoderPtr decoder(avifDecoderCreate());
  *result = AVIF_RESULT_UNKNOWN_ERROR;
  if (decoder == nullptr) return decoder;
  *result = avifDecoderSetIOMemory(decoder.get(), file.data(), file.size());
  if (*result != AVIF_RESULT_OK) return decoder;
  *result = avifDecoderParse(decoder.get());
  return decoder;
}

Bytes ReadBase() {
  const testutil::AvifRwData encoded =
      testutil::ReadFile(std::string(data_path) + kFileName);
  return Bytes(encoded.data, encoded.data + encoded.size);
}

//------------------------------------------------------------------------------

TEST(MetaResourceTest, InjectionKeepsFileValid) {
  Bytes file = ReadBase();
  ASSERT_FALSE(file.empty());
  Bytes entries;
  AppendIpmaEntry(/*item_id=*/2, /*property_index=*/5, /*num_associations=*/1,
                  &entries);
  ASSERT_NO_FATAL_FAILURE(
      Inject(OpaqueProperty("zzzz", 100), entries, 1, &file));
  avifResult result;
  DecoderPtr decoder = ParseBytes(file, &result);
  ASSERT_EQ(result, AVIF_RESULT_OK);
  EXPECT_EQ(decoder->image->numProperties,
            0u);  // Item 2 is not the primary item.
  ASSERT_EQ(avifDecoderNextImage(decoder.get()), AVIF_RESULT_OK);
}

// The same property associated 252 times to the primary item is exposed once.
TEST(MetaResourceTest, RepeatedAssociationOfOpaquePropertyIsExposedOnce) {
  Bytes file = ReadBase();
  ASSERT_FALSE(file.empty());
  // The primary item (ID 1) is the only entry of the ipma of the file: it
  // has 4 associations (1 byte each, starting at ipma + 19).
  const size_t meta0 = FindBox(file, "meta", 0, file.size());
  const size_t iprp0 = FindBox(file, "iprp", meta0 + 12, file.size());
  const size_t ipma = FindBox(file, "ipma", iprp0 + 8, file.size());
  ASSERT_NE(ipma, size_t{0});
  ASSERT_EQ(file[ipma + 16], 0);
  ASSERT_EQ(file[ipma + 17], 1);
  ASSERT_EQ(file[ipma + 18], 4);
  Bytes tail(251, 5);  // 251 more associations to property index 5 (see below).
  ASSERT_NO_FATAL_FAILURE(
      Inject(OpaqueProperty("zzzz", 1000), Bytes(), 0, &file));
  // Add the new 'zzzz' property (index 5), then extend the entry of item 1 in
  // place: insert 251 associations after the 4 original ones, bump the
  // association count, and fix the sizes and the iloc offset.
  const size_t meta = FindBox(file, "meta", 0, file.size());
  const size_t iprp = FindBox(file, "iprp", meta + 12, file.size());
  const size_t ipma2 = FindBox(file, "ipma", iprp + 8, file.size());
  ASSERT_NE(ipma2, size_t{0});
  const size_t iloc = FindBox(file, "iloc", meta + 12, file.size());
  const size_t insert_at =
      ipma2 + 19 + 4;  // After the 4 original associations.
  ASSERT_EQ(file[ipma2 + 18], 4);
  file.insert(file.begin() + insert_at, tail.begin(), tail.end());
  file[ipma2 + 18] = static_cast<uint8_t>(4 + 251);
  const uint32_t growth = static_cast<uint32_t>(tail.size());
  WriteBE32(&file, meta, ReadBE32(file, meta) + growth);
  WriteBE32(&file, iprp, ReadBE32(file, iprp) + growth);
  WriteBE32(&file, ipma2, ReadBE32(file, ipma2) + growth);
  const size_t extent_offset_pos = iloc + 8 + 4 + 2 + 2 + 2 + 2 + 2;
  WriteBE32(&file, extent_offset_pos,
            ReadBE32(file, extent_offset_pos) + growth);

  avifResult result;
  DecoderPtr decoder = ParseBytes(file, &result);
  ASSERT_EQ(result, AVIF_RESULT_OK) << decoder->diag.error;
  ASSERT_EQ(decoder->image->numProperties, 1u);
  EXPECT_EQ(decoder->image->properties[0].boxPayload.size, size_t{1000});
  ASSERT_EQ(avifDecoderNextImage(decoder.get()), AVIF_RESULT_OK);
}

// Many items associated with the same large opaque property, each 255 times:
// the file is about 100 KB but the memory needed before the fix was about
// 255 * 64 KB * 64 = 1 GB.
TEST(MetaResourceTest, LargeOpaquePropertyAssociatedWithManyItems) {
  Bytes file = ReadBase();
  ASSERT_FALSE(file.empty());
  constexpr size_t kPayloadSize = 64 * 1024;
  constexpr uint32_t kNumItems = 64;
  Bytes entries;
  for (uint32_t i = 0; i < kNumItems; ++i) {
    AppendIpmaEntry(/*item_id=*/2 + i, /*property_index=*/5,
                    /*num_associations=*/255, &entries);
  }
  ASSERT_NO_FATAL_FAILURE(
      Inject(OpaqueProperty("zzzz", kPayloadSize), entries, kNumItems, &file));
#if defined(__linux__)
  struct rusage before, after;
  getrusage(RUSAGE_SELF, &before);
#endif
  avifResult result;
  DecoderPtr decoder = ParseBytes(file, &result);
  ASSERT_EQ(result, AVIF_RESULT_OK);
  ASSERT_EQ(avifDecoderNextImage(decoder.get()), AVIF_RESULT_OK);
#if defined(__linux__)
  getrusage(RUSAGE_SELF, &after);
  // ru_maxrss is in kilobytes. Without the fix the peak grows by about 1 GB.
  EXPECT_LT(after.ru_maxrss - before.ru_maxrss, 256 * 1024);
#endif
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
