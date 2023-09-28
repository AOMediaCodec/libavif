// Copyright 2022 Google LLC
// SPDX-License-Identifier: BSD-2-Clause

#include <array>
#include <cstdint>
#include <cstring>
#include <memory>
#include <vector>

#include "avif/avif.h"
#include "avif_fuzztest_helpers.h"
#include "aviftest_helpers.h"
#include "fuzztest/fuzztest.h"
#include "gtest/gtest.h"

namespace libavif {
namespace testutil {
namespace {

::testing::Environment* const stack_limit_env =
    ::testing::AddGlobalTestEnvironment(
        new FuzztestStackLimitEnvironment("524288"));  // 512 * 1024

void CheckGainMapMetadataMatches(const avifGainMapMetadata& actual,
                                 const avifGainMapMetadata& expected) {
  // 'expecteed' is the source struct which has arbitrary data and booleans
  // values can contain any value, but the decoded ('actual') struct should
  // be 0 or 1.
  EXPECT_EQ(actual.baseRenditionIsHDR, expected.baseRenditionIsHDR ? 1 : 0);
  EXPECT_EQ(actual.hdrCapacityMinN, expected.hdrCapacityMinN);
  EXPECT_EQ(actual.hdrCapacityMinD, expected.hdrCapacityMinD);
  EXPECT_EQ(actual.hdrCapacityMaxN, expected.hdrCapacityMaxN);
  EXPECT_EQ(actual.hdrCapacityMaxD, expected.hdrCapacityMaxD);
  for (int c = 0; c < 3; ++c) {
    SCOPED_TRACE(c);
    EXPECT_EQ(actual.offsetSdrN[c], expected.offsetSdrN[c]);
    EXPECT_EQ(actual.offsetSdrD[c], expected.offsetSdrD[c]);
    EXPECT_EQ(actual.offsetHdrN[c], expected.offsetHdrN[c]);
    EXPECT_EQ(actual.offsetHdrD[c], expected.offsetHdrD[c]);
    EXPECT_EQ(actual.gainMapGammaN[c], expected.gainMapGammaN[c]);
    EXPECT_EQ(actual.gainMapGammaD[c], expected.gainMapGammaD[c]);
    EXPECT_EQ(actual.gainMapMinN[c], expected.gainMapMinN[c]);
    EXPECT_EQ(actual.gainMapMinD[c], expected.gainMapMinD[c]);
    EXPECT_EQ(actual.gainMapMaxN[c], expected.gainMapMaxN[c]);
    EXPECT_EQ(actual.gainMapMaxD[c], expected.gainMapMaxD[c]);
  }
}

void EncodeDecodeValid(AvifImagePtr image, AvifEncoderPtr encoder,
                       AvifDecoderPtr decoder) {
  AvifImagePtr decoded_image(avifImageCreateEmpty(), avifImageDestroy);
  ASSERT_NE(image.get(), nullptr);
  ASSERT_NE(encoder.get(), nullptr);
  ASSERT_NE(decoder.get(), nullptr);
  ASSERT_NE(decoded_image.get(), nullptr);

  // TODO(maryla): fuzz with different settings.
  decoder->enableDecodingGainMap = true;
  decoder->enableParsingGainMapMetadata = true;

  AvifRwData encoded_data;
  const avifResult encoder_result =
      avifEncoderWrite(encoder.get(), image.get(), &encoded_data);
  ASSERT_EQ(encoder_result, AVIF_RESULT_OK)
      << avifResultToString(encoder_result);

  const avifResult decoder_result = avifDecoderReadMemory(
      decoder.get(), decoded_image.get(), encoded_data.data, encoded_data.size);
  ASSERT_EQ(decoder_result, AVIF_RESULT_OK)
      << avifResultToString(decoder_result);

  EXPECT_EQ(decoded_image->width, image->width);
  EXPECT_EQ(decoded_image->height, image->height);
  EXPECT_EQ(decoded_image->depth, image->depth);
  EXPECT_EQ(decoded_image->yuvFormat, image->yuvFormat);

  EXPECT_EQ(image->gainMap.image != nullptr,
            decoded_image->gainMap.image != nullptr);
  if (image->gainMap.image != nullptr) {
    EXPECT_EQ(decoded_image->gainMap.image->width, image->gainMap.image->width);
    EXPECT_EQ(decoded_image->gainMap.image->height,
              image->gainMap.image->height);
    EXPECT_EQ(decoded_image->gainMap.image->depth, image->gainMap.image->depth);
    EXPECT_EQ(decoded_image->gainMap.image->yuvFormat,
              image->gainMap.image->yuvFormat);
    EXPECT_EQ(image->gainMap.image->gainMap.image, nullptr);
    EXPECT_EQ(decoded_image->gainMap.image->alphaPlane, nullptr);

    CheckGainMapMetadataMatches(decoded_image->gainMap.metadata,
                                image->gainMap.metadata);
  }

  // Verify that an opaque input leads to an opaque output.
  if (avifImageIsOpaque(image.get())) {
    EXPECT_TRUE(avifImageIsOpaque(decoded_image.get()));
  }
  // A transparent image may be heavily compressed to an opaque image. This is
  // hard to verify so do not check it.
}

// Note that avifGainMapMetadata is passed as a byte array
// because the C array fields in the struct seem to prevent fuzztest from
// handling it natively.
AvifImagePtr AddGainMapToImage(
    AvifImagePtr image, AvifImagePtr gainMap,
    const std::array<uint8_t, sizeof(avifGainMapMetadata)>& metadata) {
  image->gainMap.image = gainMap.release();
  std::memcpy(&image->gainMap.metadata, metadata.data(), metadata.size());
  return image;
}

inline auto ArbitraryAvifImageWithGainMap() {
  return fuzztest::Map(
      AddGainMapToImage, ArbitraryAvifImage(), ArbitraryAvifImage(),
      fuzztest::Arbitrary<std::array<uint8_t, sizeof(avifGainMapMetadata)>>());
}

FUZZ_TEST(EncodeDecodeAvifFuzzTest, EncodeDecodeValid)
    .WithDomains(fuzztest::OneOf(ArbitraryAvifImage(),
                                 ArbitraryAvifImageWithGainMap()),
                 ArbitraryAvifEncoder(),
                 ArbitraryAvifDecoder({AVIF_CODEC_CHOICE_AUTO}));

}  // namespace
}  // namespace testutil
}  // namespace libavif
