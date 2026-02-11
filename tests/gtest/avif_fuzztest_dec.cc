// Copyright 2023 Google LLC
// SPDX-License-Identifier: BSD-2-Clause
// Decodes an arbitrary sequence of bytes.

#include <algorithm>
#include <cstdint>
#include <limits>

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

void Parse(const std::string& arbitrary_bytes, bool is_persistent,
           DecoderPtr decoder) {
  ASSERT_FALSE(GetSeedDataDirs().empty());  // Make sure seeds are available.

  const uint8_t* data =
      reinterpret_cast<const uint8_t*>(arbitrary_bytes.data());
  avifIO* const io = avifIOCreateMemoryReader(data, arbitrary_bytes.size());
  if (io == nullptr) return;
  io->persistent = is_persistent;
  avifDecoderSetIO(decoder.get(), io);
  // No need to worry about decoding taking too much time or memory because
  // this test only exercizes parsing.
  decoder->imageSizeLimit = AVIF_DEFAULT_IMAGE_SIZE_LIMIT;
  decoder->imageDimensionLimit = std::numeric_limits<uint32_t>::max();
  decoder->imageCountLimit = 0;

  // AVIF_RESULT_INTERNAL_ERROR means a broken invariant and should not happen.
  ASSERT_NE(avifDecoderParse(decoder.get()), AVIF_RESULT_INTERNAL_ERROR);
}

FUZZ_TEST(ParseAvifTest, Parse)
    .WithDomains(ArbitraryImageWithSeeds({AVIF_APP_FILE_FORMAT_AVIF}),
                 /*is_persistent=*/Arbitrary<bool>(),
                 ArbitraryAvifDecoderPossiblyNoContent(),
                 /*image_content_color_and_alpha=*/Arbitrary<bool>());

//------------------------------------------------------------------------------

void Decode(const std::string& arbitrary_bytes, bool is_persistent,
            DecoderPtr decoder) {
  ASSERT_FALSE(GetSeedDataDirs().empty());  // Make sure seeds are available.

  const uint8_t* data =
      reinterpret_cast<const uint8_t*>(arbitrary_bytes.data());
  avifIO* const io = avifIOCreateMemoryReader(data, arbitrary_bytes.size());
  if (io == nullptr) return;
  // The Chrome's avifIO object is not persistent.
  io->persistent = is_persistent;
  avifDecoderSetIO(decoder.get(), io);

  avifResult result = avifDecoderParse(decoder.get());
  // AVIF_RESULT_INTERNAL_ERROR means a broken invariant and should not happen.
  ASSERT_NE(result, AVIF_RESULT_INTERNAL_ERROR);
  if (result != AVIF_RESULT_OK) return;

  for (size_t i = 0; i < decoder->image->numProperties; ++i) {
    const avifRWData& box_payload = decoder->image->properties[i].boxPayload;
    // Each custom property should be found as is in the input bitstream.
    EXPECT_NE(std::search(data, data + arbitrary_bytes.size(), box_payload.data,
                          box_payload.data + box_payload.size),
              data + arbitrary_bytes.size());
  }

  while ((result = avifDecoderNextImage(decoder.get())) == AVIF_RESULT_OK) {
    EXPECT_GT(decoder->image->width, 0u);
    EXPECT_GT(decoder->image->height, 0u);
  }
  ASSERT_NE(result, AVIF_RESULT_INTERNAL_ERROR);

  // Loop once.
  result = avifDecoderReset(decoder.get());
  ASSERT_NE(result, AVIF_RESULT_INTERNAL_ERROR);
  if (result != AVIF_RESULT_OK) return;
  while ((result = avifDecoderNextImage(decoder.get())) == AVIF_RESULT_OK) {
  }
  ASSERT_NE(result, AVIF_RESULT_INTERNAL_ERROR);
}

FUZZ_TEST(DecodeAvifTest, Decode)
    .WithDomains(ArbitraryImageWithSeeds({AVIF_APP_FILE_FORMAT_AVIF}),
                 /*is_persistent=*/Arbitrary<bool>(),
                 ArbitraryAvifDecoderPossiblyNoContent());

//------------------------------------------------------------------------------

}  // namespace
}  // namespace testutil
}  // namespace avif
