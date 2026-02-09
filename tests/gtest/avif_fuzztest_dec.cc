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
using ::fuzztest::BitFlagCombinationOf;
using ::fuzztest::ElementOf;

namespace avif {
namespace testutil {
namespace {

//------------------------------------------------------------------------------

void Parse(const std::string& arbitrary_bytes, bool is_persistent,
           DecoderPtr decoder, avifImageContentTypeFlags content_to_decode) {
  ASSERT_FALSE(GetSeedDataDirs().empty());  // Make sure seeds are available.

  const uint8_t* data =
      reinterpret_cast<const uint8_t*>(arbitrary_bytes.data());
  avifIO* const io = avifIOCreateMemoryReader(data, arbitrary_bytes.size());
  if (io == nullptr) return;
  io->persistent = is_persistent;
  avifDecoderSetIO(decoder.get(), io);
  // This can lead to AVIF_RESULT_NO_CONTENT or AVIF_RESULT_NOT_IMPLEMENTED.
  decoder->imageContentToDecode = content_to_decode;
  // No need to worry about decoding taking too much time or memory because
  // this test only exercizes parsing.
  decoder->imageSizeLimit = AVIF_DEFAULT_IMAGE_SIZE_LIMIT;
  decoder->imageDimensionLimit = std::numeric_limits<uint32_t>::max();
  decoder->imageCountLimit = 0;

  // AVIF_RESULT_INTERNAL_ERROR means a broken invariant and should not happen.
  EXPECT_NE(avifDecoderParse(decoder.get()), AVIF_RESULT_INTERNAL_ERROR);
}

FUZZ_TEST(ParseAvifTest, Parse)
    .WithDomains(ArbitraryImageWithSeeds({AVIF_APP_FILE_FORMAT_AVIF}),
                 /*is_persistent=*/Arbitrary<bool>(), ArbitraryAvifDecoder(),
                 BitFlagCombinationOf<avifImageContentTypeFlags>(
                     {AVIF_IMAGE_CONTENT_COLOR_AND_ALPHA,
                      AVIF_IMAGE_CONTENT_GAIN_MAP}));

//------------------------------------------------------------------------------

void Decode(const std::string& arbitrary_bytes, bool is_persistent,
            DecoderPtr decoder, avifImageContentTypeFlags content_to_decode) {
  ASSERT_FALSE(GetSeedDataDirs().empty());  // Make sure seeds are available.

  const uint8_t* data =
      reinterpret_cast<const uint8_t*>(arbitrary_bytes.data());
  avifIO* const io = avifIOCreateMemoryReader(data, arbitrary_bytes.size());
  if (io == nullptr) return;
  // The Chrome's avifIO object is not persistent.
  io->persistent = is_persistent;
  avifDecoderSetIO(decoder.get(), io);
  // This can lead to AVIF_RESULT_NO_CONTENT.
  decoder->imageContentToDecode = content_to_decode;

  avifResult result = avifDecoderParse(decoder.get());
  // AVIF_RESULT_INTERNAL_ERROR means a broken invariant and should not happen.
  EXPECT_NE(result, AVIF_RESULT_INTERNAL_ERROR);
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
  EXPECT_NE(result, AVIF_RESULT_INTERNAL_ERROR);

  // Loop once.
  result = avifDecoderReset(decoder.get());
  EXPECT_NE(result, AVIF_RESULT_INTERNAL_ERROR);
  if (result != AVIF_RESULT_OK) return;
  while ((result = avifDecoderNextImage(decoder.get())) == AVIF_RESULT_OK) {
  }
  EXPECT_NE(result, AVIF_RESULT_INTERNAL_ERROR);
}

FUZZ_TEST(DecodeAvifTest, Decode)
    .WithDomains(
        ArbitraryImageWithSeeds({AVIF_APP_FILE_FORMAT_AVIF}),
        /*is_persistent=*/Arbitrary<bool>(), ArbitraryAvifDecoder(),
        ElementOf({AVIF_IMAGE_CONTENT_NONE, AVIF_IMAGE_CONTENT_COLOR_AND_ALPHA,
                   AVIF_IMAGE_CONTENT_GAIN_MAP, AVIF_IMAGE_CONTENT_ALL}));

//------------------------------------------------------------------------------

}  // namespace
}  // namespace testutil
}  // namespace avif
