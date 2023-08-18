// Copyright 2023 Google LLC
// SPDX-License-Identifier: BSD-2-Clause

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <vector>

#include "avif/internal.h"
#include "aviftest_helpers.h"
#include "gtest/gtest.h"

namespace libavif {
namespace {

//------------------------------------------------------------------------------

TEST(StreamTest, Roundtrip) {
  // TODO(yguyon): Check return values once pull request #1498 is merged.

  // Write some fields.
  testutil::AvifRwData rw_data;
  avifRWStream rw_stream;
  avifRWStreamStart(&rw_stream, &rw_data);
  EXPECT_EQ(avifRWStreamOffset(&rw_stream), size_t{0});

  const uint8_t rw_somedata[] = {3, 1, 4};
  avifRWStreamWrite(&rw_stream, rw_somedata, sizeof(rw_somedata));

  const char rw_somechars[] = "somechars";
  avifRWStreamWriteChars(&rw_stream, rw_somechars,
                         std::strlen(rw_somechars) + 1);

  const char rw_box_type[] = "type";
  avifBoxMarker rw_box_marker =
      avifRWStreamWriteBox(&rw_stream, rw_box_type, /*contentSize=*/0);

  const uint8_t rw_someu8 = 0xA;
  avifRWStreamWriteU8(&rw_stream, rw_someu8);

  const int rw_full_box_version = 7;
  const uint32_t rw_full_box_flags = 0x555;
  avifBoxMarker rw_full_box_marker =
      avifRWStreamWriteFullBox(&rw_stream, rw_box_type, /*contentSize=*/0,
                               rw_full_box_version, rw_full_box_flags);

  const uint16_t rw_someu16 = 0xAB;
  avifRWStreamWriteU16(&rw_stream, rw_someu16);

  avifRWStreamFinishBox(&rw_stream, rw_full_box_marker);

  avifRWStreamFinishBox(&rw_stream, rw_box_marker);

  const uint32_t rw_someu32 = 0xABCD;
  avifRWStreamWriteU32(&rw_stream, rw_someu32);

  const uint64_t rw_someu64 = 0xABCDEF01;
  avifRWStreamWriteU64(&rw_stream, rw_someu64);

  const size_t num_zeros = 10000;
  avifRWStreamWriteZeros(&rw_stream, /*byteCount=*/num_zeros);

  avifRWStreamFinishWrite(&rw_stream);

  // Read and compare the fields.
  avifDiagnostics diag;
  avifDiagnosticsClearError(&diag);
  avifROData ro_data = {rw_data.data, rw_data.size};
  avifROStream ro_stream;
  avifROStreamStart(&ro_stream, &ro_data, &diag, "diagContext");
  EXPECT_EQ(avifROStreamCurrent(&ro_stream), ro_data.data);
  EXPECT_EQ(avifROStreamOffset(&ro_stream), size_t{0});
  EXPECT_TRUE(avifROStreamHasBytesLeft(&ro_stream, rw_data.size));
  EXPECT_FALSE(avifROStreamHasBytesLeft(&ro_stream, rw_data.size + 1));
  EXPECT_EQ(avifROStreamRemainingBytes(&ro_stream), rw_data.size);

  std::vector<uint8_t> ro_somedata(sizeof(rw_somedata));
  EXPECT_TRUE(
      avifROStreamRead(&ro_stream, ro_somedata.data(), ro_somedata.size()));
  EXPECT_TRUE(std::equal(rw_somedata, rw_somedata + sizeof(rw_somedata),
                         ro_somedata.data()));

  std::vector<char> ro_somechars(sizeof(rw_somechars) + 1);
  EXPECT_TRUE(avifROStreamReadString(&ro_stream, ro_somechars.data(),
                                     ro_somechars.size()));
  EXPECT_TRUE(std::equal(rw_somechars, rw_somechars + sizeof(rw_somechars),
                         ro_somechars.data()));

  avifBoxHeader ro_box_header;
  EXPECT_TRUE(avifROStreamReadBoxHeader(&ro_stream, &ro_box_header));
  EXPECT_TRUE(std::equal(rw_box_type, rw_box_type + 4, ro_box_header.type));

  uint8_t ro_someu8;
  EXPECT_TRUE(avifROStreamRead(&ro_stream, &ro_someu8, /*size=*/1));
  EXPECT_EQ(rw_someu8, ro_someu8);

  avifBoxHeader ro_full_box_header;
  EXPECT_TRUE(avifROStreamReadBoxHeader(&ro_stream, &ro_full_box_header));
  EXPECT_TRUE(
      std::equal(rw_box_type, rw_box_type + 4, ro_full_box_header.type));
  uint8_t ro_full_box_version;
  uint32_t ro_full_box_flags;
  EXPECT_TRUE(avifROStreamReadVersionAndFlags(&ro_stream, &ro_full_box_version,
                                              &ro_full_box_flags));
  EXPECT_EQ(rw_full_box_version, ro_full_box_version);
  EXPECT_EQ(rw_full_box_flags, ro_full_box_flags);

  uint16_t ro_someu16;
  EXPECT_TRUE(avifROStreamReadU16(&ro_stream, &ro_someu16));
  EXPECT_EQ(rw_someu16, ro_someu16);

  uint32_t ro_someu32;
  EXPECT_TRUE(avifROStreamReadU32(&ro_stream, &ro_someu32));
  EXPECT_EQ(rw_someu32, ro_someu32);

  uint64_t ro_someu64;
  EXPECT_TRUE(avifROStreamReadU64(&ro_stream, &ro_someu64));
  EXPECT_EQ(rw_someu64, ro_someu64);

  EXPECT_TRUE(avifROStreamSkip(&ro_stream, /*byteCount=*/num_zeros));
  EXPECT_FALSE(avifROStreamSkip(&ro_stream, /*byteCount=*/1));
}

//------------------------------------------------------------------------------

}  // namespace
}  // namespace libavif
