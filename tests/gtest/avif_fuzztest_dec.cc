// Copyright 2023 Google LLC
// SPDX-License-Identifier: BSD-2-Clause
// Decodes an arbitrary sequence of bytes.

#include <cstdint>

#include "avif/avif.h"
#include "avif_fuzztest_helpers.h"
#include "fuzztest/fuzztest.h"
#include "gtest/gtest.h"

using ::fuzztest::Arbitrary;

namespace libavif {
namespace testutil {
namespace {

::testing::Environment* const stack_limit_env =
    ::testing::AddGlobalTestEnvironment(
        new FuzztestStackLimitEnvironment("524288"));  // 512 * 1024

//------------------------------------------------------------------------------

void Decode(const std::string& arbitrary_bytes, AvifDecoderPtr decoder) {
  testutil::AvifImagePtr decoded(avifImageCreateEmpty(), avifImageDestroy);
  ASSERT_NE(decoded, nullptr);
  const avifResult result = avifDecoderReadMemory(
      decoder.get(), decoded.get(),
      reinterpret_cast<const uint8_t*>(arbitrary_bytes.data()),
      arbitrary_bytes.size());
  if (result == AVIF_RESULT_OK) {
    EXPECT_GT(decoded->width, 0u);
    EXPECT_GT(decoded->height, 0u);
  }
}

constexpr uint32_t kMaxFileSize = 1024 * 1024;  // 1MB.

FUZZ_TEST(DecodeAvifTest, Decode)
    .WithDomains(Arbitrary<std::string>()
                     .WithMaxSize(kMaxFileSize)
                     .WithSeeds(GetTestImagesContents(
                         kMaxFileSize, {AVIF_APP_FILE_FORMAT_AVIF})),
                 ArbitraryAvifDecoder({AVIF_CODEC_CHOICE_AUTO}));

//------------------------------------------------------------------------------

}  // namespace
}  // namespace testutil
}  // namespace libavif
