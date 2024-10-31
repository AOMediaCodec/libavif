// Copyright 2023 Google LLC
// SPDX-License-Identifier: BSD-2-Clause

#include <algorithm>
#include <cstdint>
#include <limits>
#include <numeric>
#include <vector>

#include "avif/internal.h"
#include "aviftest_helpers.h"
#include "gtest/gtest.h"

namespace avif {
namespace {

//------------------------------------------------------------------------------

TEST(StreamTest, Roundtrip) {
  // Write some fields.
  testutil::AvifRwData rw_data;
  avifRWStream rw_stream;
  avifRWStreamStart(&rw_stream, &rw_data);
  EXPECT_EQ(avifRWStreamOffset(&rw_stream), size_t{0});

  const uint8_t rw_somedata[] = {3, 1, 4};
  EXPECT_EQ(avifRWStreamWrite(&rw_stream, rw_somedata, sizeof(rw_somedata)),
            AVIF_RESULT_OK);

  const char rw_somechars[] = "somechars";
  EXPECT_EQ(
      avifRWStreamWriteChars(&rw_stream, rw_somechars, sizeof(rw_somechars)),
      AVIF_RESULT_OK);

  const char rw_box_type[] = "type";
  avifBoxMarker rw_box_marker;
  EXPECT_EQ(avifRWStreamWriteBox(&rw_stream, rw_box_type, /*contentSize=*/0,
                                 &rw_box_marker),
            AVIF_RESULT_OK);

  const uint8_t rw_someu8 = 0xAA;
  EXPECT_EQ(avifRWStreamWriteU8(&rw_stream, rw_someu8), AVIF_RESULT_OK);

  const int rw_full_box_version = 7;
  const uint32_t rw_full_box_flags = 0x555;
  avifBoxMarker rw_full_box_marker;
  EXPECT_EQ(avifRWStreamWriteFullBox(&rw_stream, rw_box_type, /*contentSize=*/0,
                                     rw_full_box_version, rw_full_box_flags,
                                     &rw_full_box_marker),
            AVIF_RESULT_OK);

  const uint16_t rw_someu16 = 0xAABB;
  EXPECT_EQ(avifRWStreamWriteU16(&rw_stream, rw_someu16), AVIF_RESULT_OK);

  avifRWStreamFinishBox(&rw_stream, rw_full_box_marker);

  avifRWStreamFinishBox(&rw_stream, rw_box_marker);

  const uint32_t rw_someu32 = 0xAABBCCDD;
  EXPECT_EQ(avifRWStreamWriteU32(&rw_stream, rw_someu32), AVIF_RESULT_OK);

  const uint64_t rw_someu64 = 0xAABBCCDDEEFF0011;
  EXPECT_EQ(avifRWStreamWriteU64(&rw_stream, rw_someu64), AVIF_RESULT_OK);

  const size_t rw_somebitcount = 6;
  const uint32_t rw_somebits = (1 << rw_somebitcount) - 2;
  EXPECT_EQ(avifRWStreamWriteBits(&rw_stream, rw_somebits, rw_somebitcount),
            AVIF_RESULT_OK);
  const size_t rw_maxbitcount = sizeof(uint32_t) * 8;
  const uint32_t rw_maxbits = std::numeric_limits<uint32_t>::max();
  EXPECT_EQ(avifRWStreamWriteBits(&rw_stream, rw_maxbits, rw_maxbitcount),
            AVIF_RESULT_OK);

  const uint32_t skipbitcount = 23;
  EXPECT_EQ(avifRWStreamWriteBits(&rw_stream, 0, skipbitcount), AVIF_RESULT_OK);

  const uint32_t rw_somebit = 1;
  EXPECT_EQ(avifRWStreamWriteBits(&rw_stream, rw_somebit, /*bitCount=*/1),
            AVIF_RESULT_OK);

  // Pad till byte alignment.
  EXPECT_NE(rw_stream.numUsedBitsInPartialByte, 0);
  EXPECT_EQ(avifRWStreamWriteBits(&rw_stream, 0,
                                  8 - rw_stream.numUsedBitsInPartialByte),
            AVIF_RESULT_OK);

  const size_t num_zeros = 10000;
  EXPECT_EQ(avifRWStreamWriteZeros(&rw_stream, /*byteCount=*/num_zeros),
            AVIF_RESULT_OK);

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

  std::vector<char> ro_somechars(sizeof(rw_somechars));
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

  uint32_t ro_somebits;
  EXPECT_TRUE(
      avifROStreamReadBitsU32(&ro_stream, &ro_somebits, rw_somebitcount));
  EXPECT_EQ(rw_somebits, ro_somebits);
  uint32_t ro_maxbits;
  EXPECT_TRUE(avifROStreamReadBitsU32(&ro_stream, &ro_maxbits, rw_maxbitcount));
  EXPECT_EQ(rw_maxbits, ro_maxbits);

  EXPECT_TRUE(avifROStreamSkipBits(&ro_stream, skipbitcount));

  uint8_t ro_somebit;
  EXPECT_TRUE(avifROStreamReadBitsU8(&ro_stream, &ro_somebit, /*bitCount=*/1));
  EXPECT_EQ(rw_somebit, ro_somebit);

  // Pad till byte alignment.
  EXPECT_TRUE(
      avifROStreamSkipBits(&ro_stream, 8 - ro_stream.numUsedBitsInPartialByte));

  EXPECT_TRUE(avifROStreamSkip(&ro_stream, /*byteCount=*/num_zeros));
  EXPECT_FALSE(avifROStreamSkip(&ro_stream, /*byteCount=*/1));
}

TEST(StreamTest, SkipBits) {
  const uint8_t data[40] = {};
  avifROData ro_data = {data, sizeof(data)};
  avifDiagnostics diag;
  avifDiagnosticsClearError(&diag);
  avifROStream ro_stream;
  avifROStreamStart(&ro_stream, &ro_data, &diag, "diagContext");

  EXPECT_TRUE(avifROStreamSkip(&ro_stream, 32));
  EXPECT_EQ(avifROStreamOffset(&ro_stream), 32);
  EXPECT_EQ(ro_stream.numUsedBitsInPartialByte, 0);

  uint32_t unused;
  EXPECT_TRUE(avifROStreamReadBitsU32(&ro_stream, &unused, 5));
  EXPECT_EQ(avifROStreamOffset(&ro_stream), 33);
  EXPECT_EQ(ro_stream.numUsedBitsInPartialByte, 5);

  EXPECT_TRUE(avifROStreamSkipBits(&ro_stream, 1));
  EXPECT_EQ(avifROStreamOffset(&ro_stream), 33);
  EXPECT_EQ(ro_stream.numUsedBitsInPartialByte, 6);

  EXPECT_TRUE(avifROStreamSkipBits(&ro_stream, 2));
  EXPECT_EQ(avifROStreamOffset(&ro_stream), 33);
  EXPECT_EQ(ro_stream.numUsedBitsInPartialByte, 0);
}

TEST(StreamTest, WriteBitsLimit) {
  testutil::AvifRwData rw_data;
  avifRWStream rw_stream;
  avifRWStreamStart(&rw_stream, &rw_data);
  EXPECT_EQ(avifRWStreamWriteBits(&rw_stream, 7, 3), AVIF_RESULT_OK);
  EXPECT_EQ(avifRWStreamWriteBits(&rw_stream, 8, 3),
            AVIF_RESULT_INVALID_ARGUMENT);
}

//------------------------------------------------------------------------------

}  // namespace
}  // namespace avif
