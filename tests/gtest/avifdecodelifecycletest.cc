// Copyright 2026 Google LLC
// SPDX-License-Identifier: BSD-2-Clause

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <limits>
#include <string>
#include <vector>

#include "avif/avif.h"
#include "aviftest_helpers.h"
#include "gtest/gtest.h"

namespace {

constexpr size_t kAllocHeaderAlign = 16;
constexpr size_t kAllocMagic = static_cast<size_t>(0xA71F5AFEULL);

size_t g_live_alloc_count = 0;
size_t g_live_alloc_bytes = 0;
size_t g_fail_alloc_countdown = 0;

}  // namespace

extern "C" {

void* avifAlloc(size_t size) {
  if (size == 0) {
    return nullptr;
  }
  if (g_fail_alloc_countdown > 0 && --g_fail_alloc_countdown == 0) {
    return nullptr;
  }
  if (size > std::numeric_limits<size_t>::max() - kAllocHeaderAlign) {
    return nullptr;
  }
  auto* raw = static_cast<uint8_t*>(std::malloc(kAllocHeaderAlign + size));
  if (raw == nullptr) {
    return nullptr;
  }
  auto* hdr = reinterpret_cast<size_t*>(raw);
  hdr[0] = size;
  hdr[1] = kAllocMagic;
  ++g_live_alloc_count;
  g_live_alloc_bytes += size;
  return raw + kAllocHeaderAlign;
}

void* avifCalloc(size_t count, size_t size) {
  if (count == 0 || size == 0) {
    return nullptr;
  }
  if (g_fail_alloc_countdown > 0 && --g_fail_alloc_countdown == 0) {
    return nullptr;
  }
  if (count > (std::numeric_limits<size_t>::max() - kAllocHeaderAlign) / size) {
    return nullptr;
  }
  const size_t total = count * size;
  auto* raw = static_cast<uint8_t*>(std::calloc(1, kAllocHeaderAlign + total));
  if (raw == nullptr) {
    return nullptr;
  }
  auto* hdr = reinterpret_cast<size_t*>(raw);
  hdr[0] = total;
  hdr[1] = kAllocMagic;
  ++g_live_alloc_count;
  g_live_alloc_bytes += total;
  return raw + kAllocHeaderAlign;
}

void avifFree(void* p) {
  if (p == nullptr) {
    return;
  }
  auto* raw = static_cast<uint8_t*>(p) - kAllocHeaderAlign;
  auto* hdr = reinterpret_cast<size_t*>(raw);
  if (hdr[1] == kAllocMagic) {
    --g_live_alloc_count;
    g_live_alloc_bytes -= hdr[0];
    hdr[1] = 0;
  }
  std::free(raw);
}

}  // extern "C"

namespace avif {
namespace {

const char* data_path = nullptr;

// Positive valid control: normal still and animated decode + avifDecoderReset
// round-trip produces pixel-identical images with zero leaked allocations.
TEST(AvifDecodeLifecycleTest, ValidDecodeAndResetSucceeds) {
  if (!testutil::Av1DecoderAvailable()) {
    GTEST_SKIP() << "AV1 Codec unavailable, skip test.";
  }
  const size_t start_alloc_count = g_live_alloc_count;
  const size_t start_alloc_bytes = g_live_alloc_bytes;

  for (const char* file_name :
       {"sofa_grid1x5_420.avif", "colors-animated-8bpc.avif"}) {
    SCOPED_TRACE(file_name);
    const std::string path = std::string(data_path) + file_name;

    DecoderPtr ref_decoder(avifDecoderCreate());
    ASSERT_NE(ref_decoder, nullptr);
    ASSERT_EQ(avifDecoderSetIOFile(ref_decoder.get(), path.c_str()),
              AVIF_RESULT_OK);
    ASSERT_EQ(avifDecoderParse(ref_decoder.get()), AVIF_RESULT_OK);
    ASSERT_EQ(avifDecoderNextImage(ref_decoder.get()), AVIF_RESULT_OK);

    DecoderPtr decoder(avifDecoderCreate());
    ASSERT_NE(decoder, nullptr);
    ASSERT_EQ(avifDecoderSetIOFile(decoder.get(), path.c_str()),
              AVIF_RESULT_OK);
    ASSERT_EQ(avifDecoderParse(decoder.get()), AVIF_RESULT_OK);
    ASSERT_EQ(avifDecoderNextImage(decoder.get()), AVIF_RESULT_OK);
    EXPECT_TRUE(testutil::AreImagesEqual(*ref_decoder->image, *decoder->image));

    ASSERT_EQ(avifDecoderReset(decoder.get()), AVIF_RESULT_OK);
    ASSERT_EQ(avifDecoderNextImage(decoder.get()), AVIF_RESULT_OK);
    EXPECT_TRUE(testutil::AreImagesEqual(*ref_decoder->image, *decoder->image));
  }

  EXPECT_EQ(g_live_alloc_count, start_alloc_count);
  EXPECT_EQ(g_live_alloc_bytes, start_alloc_bytes);
}

TEST(AvifDecodeLifecycleTest, StaleTrackStateClearedAcrossSetSourceAndReparse) {
  if (!testutil::Av1DecoderAvailable()) {
    GTEST_SKIP() << "AV1 Codec unavailable, skip test.";
  }
  DecoderPtr decoder(avifDecoderCreate());
  ASSERT_NE(decoder, nullptr);

  // 1. Parse animated AVIF with infinite repetitionCount and primary item.
  const std::string anim_path =
      std::string(data_path) + "colors-animated-8bpc-alpha-exif-xmp.avif";
  ASSERT_EQ(avifDecoderSetIOFile(decoder.get(), anim_path.c_str()),
            AVIF_RESULT_OK);
  ASSERT_EQ(avifDecoderParse(decoder.get()), AVIF_RESULT_OK);
  EXPECT_EQ(decoder->imageSequenceTrackPresent, AVIF_TRUE);
  EXPECT_EQ(decoder->imageCount, 5);
  EXPECT_EQ(decoder->repetitionCount, AVIF_REPETITION_COUNT_INFINITE);

  // 2. Switch source to PRIMARY_ITEM on the same parsed file: repetitionCount
  //    must reset to 0 alongside imageCount=1 while preserving track presence.
  ASSERT_EQ(
      avifDecoderSetSource(decoder.get(), AVIF_DECODER_SOURCE_PRIMARY_ITEM),
      AVIF_RESULT_OK);
  EXPECT_EQ(decoder->imageSequenceTrackPresent, AVIF_TRUE);
  EXPECT_EQ(decoder->imageCount, 1);
  EXPECT_EQ(decoder->repetitionCount, 0);
  EXPECT_EQ(avifDecoderNextImage(decoder.get()), AVIF_RESULT_OK);

  // 3. Switch back to TRACKS: track repetitionCount is restored.
  ASSERT_EQ(avifDecoderSetSource(decoder.get(), AVIF_DECODER_SOURCE_TRACKS),
            AVIF_RESULT_OK);
  EXPECT_EQ(decoder->repetitionCount, AVIF_REPETITION_COUNT_INFINITE);

  // 4. Reuse the SAME avifDecoder* to parse a still grid image: neither
  //    imageSequenceTrackPresent nor repetitionCount may leak across parses.
  const std::string still_path =
      std::string(data_path) + "sofa_grid1x5_420.avif";
  ASSERT_EQ(avifDecoderSetIOFile(decoder.get(), still_path.c_str()),
            AVIF_RESULT_OK);
  ASSERT_EQ(avifDecoderSetSource(decoder.get(), AVIF_DECODER_SOURCE_AUTO),
            AVIF_RESULT_OK);
  ASSERT_EQ(avifDecoderParse(decoder.get()), AVIF_RESULT_OK);
  EXPECT_EQ(decoder->imageSequenceTrackPresent, AVIF_FALSE);
  EXPECT_EQ(decoder->imageCount, 1);
  EXPECT_EQ(decoder->repetitionCount, 0);
  EXPECT_EQ(avifDecoderNextImage(decoder.get()), AVIF_RESULT_OK);
}

// Crafted multi-codec partial failure inside avifDecoderCreateCodecs() on
// non-grid inputs (both AVIF_DECODER_SOURCE_TRACKS color+alpha and
// AVIF_DECODER_SOURCE_PRIMARY_ITEM single-tile color+alpha) followed by retry.
TEST(AvifDecodeLifecycleTest, PartialCreateCodecsFailureRetryDoesNotNullDeref) {
  if (!testutil::Av1DecoderAvailable()) {
    GTEST_SKIP() << "AV1 Codec unavailable, skip test.";
  }
  for (const char* file_name : {"colors-animated-8bpc-alpha-exif-xmp.avif",
                                "abc_color_irot_alpha_irot.avif"}) {
    SCOPED_TRACE(file_name);
    const std::string path = std::string(data_path) + file_name;

    bool saw_oom = false;
    for (size_t fail_at = 1; fail_at <= 32; ++fail_at) {
      DecoderPtr decoder(avifDecoderCreate());
      ASSERT_NE(decoder, nullptr);
      ASSERT_EQ(avifDecoderSetIOFile(decoder.get(), path.c_str()),
                AVIF_RESULT_OK);
      ASSERT_EQ(avifDecoderParse(decoder.get()), AVIF_RESULT_OK);

      g_fail_alloc_countdown = fail_at;
      const avifResult first_res = avifDecoderNextImage(decoder.get());
      g_fail_alloc_countdown = 0;

      if (first_res == AVIF_RESULT_OK) {
        break;
      }
      saw_oom = true;
      EXPECT_EQ(first_res, AVIF_RESULT_OUT_OF_MEMORY);

      // Retrying avifDecoderNextImage() on the same decoder after transient OOM
      // must cleanly recreate all codecs and decode frame 0 without null-deref.
      EXPECT_EQ(avifDecoderNextImage(decoder.get()), AVIF_RESULT_OK)
          << "fail_at=" << fail_at << " diag=" << decoder->diag.error;
    }
    EXPECT_TRUE(saw_oom);
  }
}

struct OwnedPersistentMemoryIO {
  avifIO io;
  uint8_t* buffer;
  size_t size;
};

avifResult OwnedPersistentMemoryIORead(struct avifIO* io, uint32_t read_flags,
                                       uint64_t offset, size_t size,
                                       avifROData* out) {
  if (read_flags != 0) {
    return AVIF_RESULT_IO_ERROR;
  }
  const auto* reader = reinterpret_cast<const OwnedPersistentMemoryIO*>(io);
  if (offset > reader->size) {
    return AVIF_RESULT_IO_ERROR;
  }
  const size_t available = reader->size - static_cast<size_t>(offset);
  if (size > available) {
    size = available;
  }
  out->data = offset ? reader->buffer + offset : reader->buffer;
  out->size = size;
  return AVIF_RESULT_OK;
}

void OwnedPersistentMemoryIODestroy(struct avifIO* io) {
  auto* reader = reinterpret_cast<OwnedPersistentMemoryIO*>(io);
  avifFree(reader->buffer);
  avifFree(reader);
}

avifIO* CreateOwnedPersistentMemoryIO(const std::string& path) {
  std::ifstream f(path, std::ios::binary);
  if (!f.good()) {
    return nullptr;
  }
  const std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(f)),
                                   std::istreambuf_iterator<char>());
  auto* reader = static_cast<OwnedPersistentMemoryIO*>(
      avifCalloc(1, sizeof(OwnedPersistentMemoryIO)));
  if (reader == nullptr) {
    return nullptr;
  }
  reader->buffer = static_cast<uint8_t*>(avifAlloc(bytes.size()));
  if (reader->buffer == nullptr) {
    avifFree(reader);
    return nullptr;
  }
  std::memcpy(reader->buffer, bytes.data(), bytes.size());
  reader->size = bytes.size();
  reader->io.destroy = OwnedPersistentMemoryIODestroy;
  reader->io.read = OwnedPersistentMemoryIORead;
  reader->io.sizeHint = bytes.size();
  // Per include/avif/avif.h:1120-1129, persistent=AVIF_TRUE guarantees memory
  // regions exist for the lifetime of the avifIO object and are destroyed by
  // io->destroy(io).
  reader->io.persistent = AVIF_TRUE;
  return reinterpret_cast<avifIO*>(reader);
}

// Crafted persistent-IO replacement sequence verifying invalid dangling
// zero-copy item extents are cleared before destroying the old avifIO buffer.
TEST(AvifDecodeLifecycleTest,
     SetIODestroysPersistentIOBufferWithoutDanglingDataUAF) {
  DecoderPtr decoder(avifDecoderCreate());
  ASSERT_NE(decoder, nullptr);
  decoder->imageContentToDecode = AVIF_IMAGE_CONTENT_ALL;

  avifIO* io1 = CreateOwnedPersistentMemoryIO(
      std::string(data_path) + "color_nogrid_alpha_nogrid_gainmap_grid.avif");
  ASSERT_NE(io1, nullptr);
  avifDecoderSetIO(decoder.get(), io1);
  ASSERT_EQ(avifDecoderParse(decoder.get()), AVIF_RESULT_OK);

  avifIO* io2 = CreateOwnedPersistentMemoryIO(
      std::string(data_path) + "colors-animated-8bpc.avif");
  ASSERT_NE(io2, nullptr);
  // Replacing IO destroys io1 (freeing io1->buffer). Configuring source before
  // parsing io2 must not read stale zero-copy pointers into io1->buffer.
  avifDecoderSetIO(decoder.get(), io2);
  EXPECT_EQ(avifDecoderSetSource(decoder.get(), AVIF_DECODER_SOURCE_AUTO),
            AVIF_RESULT_OK);
  ASSERT_EQ(avifDecoderParse(decoder.get()), AVIF_RESULT_OK);
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
