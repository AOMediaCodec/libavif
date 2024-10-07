// Copyright 2023 Google LLC
// SPDX-License-Identifier: BSD-2-Clause
// Decodes an arbitrary sequence of bytes.

#include <cstdint>

#include "avif/avif.h"
#include "avif_fuzztest_helpers.h"
#include "aviftest_helpers.h"
#include "fuzztest/fuzztest.h"
#include "gtest/gtest.h"

using ::fuzztest::Arbitrary;

namespace avif {
namespace testutil {
namespace {

//------------------------------------------------------------------------------

void Decode(const std::string& arbitrary_bytes, bool is_persistent,
            DecoderPtr decoder) {
  ASSERT_FALSE(GetSeedDataDirs().empty());  // Make sure seeds are available.

  ImagePtr decoded(avifImageCreateEmpty());
  ASSERT_NE(decoded, nullptr);

  avifIO* const io = avifIOCreateMemoryReader(
      reinterpret_cast<const uint8_t*>(arbitrary_bytes.data()),
      arbitrary_bytes.size());
  if (io == nullptr) return;
  // The Chrome's avifIO object is not persistent.
  io->persistent = is_persistent;
  avifDecoderSetIO(decoder.get(), io);

  if (avifDecoderParse(decoder.get()) != AVIF_RESULT_OK) return;
  while (avifDecoderNextImage(decoder.get()) == AVIF_RESULT_OK) {
    EXPECT_GT(decoder->image->width, 0u);
    EXPECT_GT(decoder->image->height, 0u);
  }

  // Loop once.
  if (avifDecoderReset(decoder.get()) != AVIF_RESULT_OK) return;
  while (avifDecoderNextImage(decoder.get()) == AVIF_RESULT_OK) {
  }
}

FUZZ_TEST(DecodeAvifTest, Decode)
    .WithDomains(ArbitraryImageWithSeeds({AVIF_APP_FILE_FORMAT_AVIF}),
                 Arbitrary<bool>(), ArbitraryAvifDecoder());

//------------------------------------------------------------------------------

}  // namespace
}  // namespace testutil
}  // namespace avif
