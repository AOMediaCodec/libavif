// Copyright 2026 the libavif contributors
// SPDX-License-Identifier: BSD-2-Clause

#include <cstdint>
#include <cstring>
#include <vector>

#include "avif/avif.h"
#include "aviftest_helpers.h"
#include "gtest/gtest.h"

namespace avif {
namespace {

//------------------------------------------------------------------------------
// Hand-crafted minimal AVIF sequence ('avis') files.

void Put16(std::vector<uint8_t>* b, uint16_t v) {
  b->push_back(static_cast<uint8_t>(v >> 8));
  b->push_back(static_cast<uint8_t>(v));
}

void Put32(std::vector<uint8_t>* b, uint32_t v) {
  b->push_back(static_cast<uint8_t>(v >> 24));
  b->push_back(static_cast<uint8_t>(v >> 16));
  b->push_back(static_cast<uint8_t>(v >> 8));
  b->push_back(static_cast<uint8_t>(v));
}

void PutType(std::vector<uint8_t>* b, const char* type) {
  b->insert(b->end(), type, type + 4);
}

void PutBox(std::vector<uint8_t>* b, const char* type,
            const std::vector<uint8_t>& payload) {
  Put32(b, static_cast<uint32_t>(8 + payload.size()));
  PutType(b, type);
  b->insert(b->end(), payload.begin(), payload.end());
}

void PutFullBox(std::vector<uint8_t>* b, const char* type,
                const std::vector<uint8_t>& payload) {
  Put32(b, static_cast<uint32_t>(12 + payload.size()));
  PutType(b, type);
  Put32(b, 0);  // version and flags.
  b->insert(b->end(), payload.begin(), payload.end());
}

struct SequenceConfig {
  uint32_t samplesPerChunk;  // Declared by the single 'stsc' entry.
  uint32_t allSamplesSize;   // Fixed sample size declared by 'stsz'.
  uint32_t chunkOffset;      // Offset of every 'stco' chunk.
  uint32_t numChunks;        // Number of 'stco' entries, all at chunkOffset.
};

// ftyp(avis) + moov(trak(tkhd, mdia(mdhd, hdlr(pict), minf(stbl(stsd(av01),
// stsc, stsz, stco))))) + mdat. The samples may be overlapping garbage since
// only parsing is exercised, not decoding.
std::vector<uint8_t> CreateAvifSequenceFile(const SequenceConfig& config) {
  std::vector<uint8_t> av1C;
  PutBox(&av1C, "av1C", {0x81, 0x04, 0x00, 0x00});

  std::vector<uint8_t> visualSampleEntry;
  // reserved[6], data_reference_index, pre_defined, reserved, pre_defined[3].
  for (int i = 0; i < 6; ++i) visualSampleEntry.push_back(0);
  Put16(&visualSampleEntry, 1);
  Put16(&visualSampleEntry, 0);
  Put16(&visualSampleEntry, 0);
  for (int i = 0; i < 6; ++i) Put16(&visualSampleEntry, 0);
  Put16(&visualSampleEntry, 64);          // width
  Put16(&visualSampleEntry, 64);          // height
  Put32(&visualSampleEntry, 0x00480000);  // horizresolution
  Put32(&visualSampleEntry, 0x00480000);  // vertresolution
  Put32(&visualSampleEntry, 0);           // reserved
  Put16(&visualSampleEntry, 1);           // frame_count
  for (int i = 0; i < 32; ++i)
    visualSampleEntry.push_back(0);   // compressorname
  Put16(&visualSampleEntry, 0x0018);  // depth
  Put16(&visualSampleEntry, 0xffff);  // pre_defined
  visualSampleEntry.insert(visualSampleEntry.end(), av1C.begin(), av1C.end());

  std::vector<uint8_t> stsd;
  Put32(&stsd, 1);  // entry_count
  PutBox(&stsd, "av01", visualSampleEntry);

  std::vector<uint8_t> stsc;
  Put32(&stsc, 1);  // entry_count
  Put32(&stsc, 1);  // first_chunk
  Put32(&stsc, config.samplesPerChunk);
  Put32(&stsc, 1);  // sample_description_index

  std::vector<uint8_t> stsz;
  Put32(&stsz, config.allSamplesSize);  // sample_size (0 if per-sample table)
  Put32(&stsz, 0);  // sample_count when all samples have the same size

  std::vector<uint8_t> stco;
  Put32(&stco, config.numChunks);
  for (uint32_t i = 0; i < config.numChunks; ++i)
    Put32(&stco, config.chunkOffset);

  std::vector<uint8_t> stbl;
  PutFullBox(&stbl, "stsd", stsd);
  PutFullBox(&stbl, "stsc", stsc);
  PutFullBox(&stbl, "stsz", stsz);
  PutFullBox(&stbl, "stco", stco);

  std::vector<uint8_t> minf;
  PutBox(&minf, "stbl", stbl);

  std::vector<uint8_t> mdhd;
  Put32(&mdhd, 0);       // creation_time
  Put32(&mdhd, 0);       // modification_time
  Put32(&mdhd, 1000);    // timescale
  Put32(&mdhd, 500);     // duration
  Put16(&mdhd, 0x55C4);  // language 'und'
  Put16(&mdhd, 0);       // pre_defined

  std::vector<uint8_t> hdlr;
  Put32(&hdlr, 0);  // pre_defined
  PutType(&hdlr, "pict");
  for (int i = 0; i < 3; ++i) Put32(&hdlr, 0);  // reserved
  hdlr.push_back(0);                            // name

  std::vector<uint8_t> mdia;
  PutFullBox(&mdia, "mdhd", mdhd);
  PutFullBox(&mdia, "hdlr", hdlr);
  PutBox(&mdia, "minf", minf);

  std::vector<uint8_t> tkhd;
  Put32(&tkhd, 0);                              // creation_time
  Put32(&tkhd, 0);                              // modification_time
  Put32(&tkhd, 1);                              // track_ID
  Put32(&tkhd, 0);                              // reserved
  Put32(&tkhd, 500);                            // duration
  for (int i = 0; i < 2; ++i) Put32(&tkhd, 0);  // reserved
  Put16(&tkhd, 0);                              // layer
  Put16(&tkhd, 0);                              // alternate_group
  Put16(&tkhd, 0x0100);                         // volume
  Put16(&tkhd, 0);                              // reserved
  for (int i = 0; i < 9; ++i)
    Put32(&tkhd, (i == 0 || i == 4 || i == 8) ? 0x00010000u : 0u);  // matrix
  Put32(&tkhd, 64u << 16);                                          // width
  Put32(&tkhd, 64u << 16);                                          // height

  std::vector<uint8_t> trak;
  PutFullBox(&trak, "tkhd", tkhd);
  PutBox(&trak, "mdia", mdia);

  std::vector<uint8_t> moov;
  PutBox(&moov, "trak", trak);

  std::vector<uint8_t> ftyp;
  PutType(&ftyp, "avis");
  Put32(&ftyp, 0);
  PutType(&ftyp, "avis");

  std::vector<uint8_t> file;
  PutBox(&file, "ftyp", ftyp);
  PutBox(&file, "moov", moov);
  Put32(&file, 12);
  PutType(&file, "mdat");
  Put32(&file, 0);
  Put32(&file, 0);
  Put32(&file, 0);
  return file;
}

avifResult ParseSequence(const std::vector<uint8_t>& file,
                         uint32_t* imageCount) {
  DecoderPtr decoder(avifDecoderCreate());
  if (decoder == nullptr) return AVIF_RESULT_UNKNOWN_ERROR;
  if (imageCount != nullptr) *imageCount = 0;
  const avifResult result =
      avifDecoderSetIOMemory(decoder.get(), file.data(), file.size());
  if (result != AVIF_RESULT_OK) return result;
  const avifResult parseResult = avifDecoderParse(decoder.get());
  if (imageCount != nullptr) *imageCount = decoder->imageCount;
  return parseResult;
}

//------------------------------------------------------------------------------

// Control: a well-formed sequence parses fine, and stays fine after the
// resource bounds below are added.
TEST(SampleTableTest, DisjointChunksParse) {
  if (!testutil::Av1DecoderAvailable()) {
    GTEST_SKIP() << "AV1 Codec unavailable, skip test.";
  }
  const std::vector<uint8_t> file =
      CreateAvifSequenceFile({.samplesPerChunk = 1,
                              .allSamplesSize = 1,
                              .chunkOffset = 0,
                              .numChunks = 2});
  uint32_t imageCount = 0;
  EXPECT_EQ(ParseSequence(file, &imageCount), AVIF_RESULT_OK);
  EXPECT_EQ(imageCount, 2u);
}

// All chunks overlap at offset 0, so a 4 KiB file can declare the whole
// imageCountLimit budget of samples. The sum of the sample sizes must be
// rejected as exceeding what the file can hold, instead of materializing
// ~125 MiB of samples (about 2 592 000 of them with the default
// imageCountLimit) from a tiny crafted file.
TEST(SampleTableTest, OverlappingChunksExceedTotalSizeBudget) {
  if (!testutil::Av1DecoderAvailable()) {
    GTEST_SKIP() << "AV1 Codec unavailable, skip test.";
  }
  const std::vector<uint8_t> file =
      CreateAvifSequenceFile({.samplesPerChunk = 2592,
                              .allSamplesSize = 1,
                              .chunkOffset = 0,
                              .numChunks = 1000});
  EXPECT_LT(file.size(), size_t{16 * 1024});
  EXPECT_EQ(ParseSequence(file, nullptr), AVIF_RESULT_NOT_IMPLEMENTED);
}

//------------------------------------------------------------------------------

}  // namespace
}  // namespace avif
