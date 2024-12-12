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

namespace avif {
namespace testutil {
namespace {

void CheckGainMapMetadataMatches(const avifGainMap& actual,
                                 const avifGainMap& expected) {
  EXPECT_EQ(actual.baseHdrHeadroom.n, expected.baseHdrHeadroom.n);
  EXPECT_EQ(actual.baseHdrHeadroom.d, expected.baseHdrHeadroom.d);
  EXPECT_EQ(actual.alternateHdrHeadroom.n, expected.alternateHdrHeadroom.n);
  EXPECT_EQ(actual.alternateHdrHeadroom.d, expected.alternateHdrHeadroom.d);
  for (int c = 0; c < 3; ++c) {
    SCOPED_TRACE(c);
    EXPECT_EQ(actual.baseOffset[c].n, expected.baseOffset[c].n);
    EXPECT_EQ(actual.baseOffset[c].d, expected.baseOffset[c].d);
    EXPECT_EQ(actual.alternateOffset[c].n, expected.alternateOffset[c].n);
    EXPECT_EQ(actual.alternateOffset[c].d, expected.alternateOffset[c].d);
    EXPECT_EQ(actual.gainMapGamma[c].n, expected.gainMapGamma[c].n);
    EXPECT_EQ(actual.gainMapGamma[c].d, expected.gainMapGamma[c].d);
    EXPECT_EQ(actual.gainMapMin[c].n, expected.gainMapMin[c].n);
    EXPECT_EQ(actual.gainMapMin[c].d, expected.gainMapMin[c].d);
    EXPECT_EQ(actual.gainMapMax[c].n, expected.gainMapMax[c].n);
    EXPECT_EQ(actual.gainMapMax[c].d, expected.gainMapMax[c].d);
  }
}

void EncodeDecodeValid(ImagePtr image, EncoderPtr encoder, DecoderPtr decoder) {
  ImagePtr decoded_image(avifImageCreateEmpty());
  ASSERT_NE(image.get(), nullptr);
  ASSERT_NE(encoder.get(), nullptr);
  ASSERT_NE(decoder.get(), nullptr);
  ASSERT_NE(decoded_image.get(), nullptr);

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

  EXPECT_EQ(decoded_image->gainMap != nullptr, image->gainMap != nullptr);
  if (decoded_image->gainMap != nullptr &&
      (decoder->imageContentToDecode & AVIF_IMAGE_CONTENT_GAIN_MAP)) {
    ASSERT_NE(decoded_image->gainMap->image, nullptr);
    EXPECT_EQ(decoded_image->gainMap->image->width,
              image->gainMap->image->width);
    EXPECT_EQ(decoded_image->gainMap->image->height,
              image->gainMap->image->height);
    EXPECT_EQ(decoded_image->gainMap->image->depth,
              image->gainMap->image->depth);
    EXPECT_EQ(decoded_image->gainMap->image->yuvFormat,
              image->gainMap->image->yuvFormat);
    EXPECT_EQ(image->gainMap->image->gainMap, nullptr);
    EXPECT_EQ(decoded_image->gainMap->image->alphaPlane, nullptr);

    CheckGainMapMetadataMatches(*decoded_image->gainMap, *image->gainMap);
  }

  // Verify that an opaque input leads to an opaque output.
  if (avifImageIsOpaque(image.get())) {
    EXPECT_TRUE(avifImageIsOpaque(decoded_image.get()));
  }
  // A transparent image may be heavily compressed to an opaque image. This is
  // hard to verify so do not check it.
}

FUZZ_TEST(EncodeDecodeAvifFuzzTest, EncodeDecodeValid)
    .WithDomains(fuzztest::OneOf(ArbitraryAvifImage(),
                                 ArbitraryAvifImageWithGainMap()),
                 ArbitraryAvifEncoder(), ArbitraryAvifDecoder());

}  // namespace
}  // namespace testutil
}  // namespace avif
