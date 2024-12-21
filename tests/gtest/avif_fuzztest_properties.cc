// Copyright 2024 Google LLC
// SPDX-License-Identifier: BSD-2-Clause

#include <array>
#include <cstdint>
#include <cstring>
#include <memory>
#include <vector>

#include "avif/avif.h"
#include "avif/internal.h"
#include "avif_fuzztest_helpers.h"
#include "aviftest_helpers.h"
#include "fuzztest/fuzztest.h"
#include "gtest/gtest.h"

namespace avif {
namespace testutil {
namespace {

struct TestProp {
  std::vector<uint8_t> fourcc;
  std::vector<uint8_t> uuid;
  std::vector<uint8_t> body;
};

static std::vector<uint8_t> UUID_4CC = {'u', 'u', 'i', 'd'};

void PropsValid(ImagePtr image, EncoderPtr encoder, DecoderPtr decoder,
                std::vector<TestProp> testProps) {
  ImagePtr decoded_image(avifImageCreateEmpty());
  ASSERT_NE(image.get(), nullptr);
  ASSERT_NE(encoder.get(), nullptr);
  ASSERT_NE(decoder.get(), nullptr);
  ASSERT_NE(decoded_image.get(), nullptr);

  for (TestProp testProp : testProps) {
    if (testProp.fourcc == UUID_4CC) {
      ASSERT_EQ(
          avifImageAddUUIDProperty(image.get(), testProp.uuid.data(),
                                   testProp.body.data(), testProp.body.size()),
          AVIF_RESULT_OK);
    } else {
      ASSERT_EQ(avifImageAddOpaqueProperty(image.get(), testProp.fourcc.data(),
                                           testProp.body.data(),
                                           testProp.body.size()),
                AVIF_RESULT_OK);
    }
  }

  AvifRwData encoded_data;
  const avifResult encoder_result =
      avifEncoderWrite(encoder.get(), image.get(), &encoded_data);
  ASSERT_EQ(encoder_result, AVIF_RESULT_OK)
      << avifResultToString(encoder_result);

  const avifResult decoder_result = avifDecoderReadMemory(
      decoder.get(), decoded_image.get(), encoded_data.data, encoded_data.size);
  ASSERT_EQ(decoder_result, AVIF_RESULT_OK)
      << avifResultToString(decoder_result);

  ASSERT_EQ(decoder->image->numProperties, testProps.size());
  for (size_t i = 0; i < testProps.size(); i++) {
    TestProp testProp = testProps[i];
    const avifImageItemProperty& decodeProp = decoder->image->properties[i];
    EXPECT_EQ(std::string(decodeProp.boxtype, decodeProp.boxtype + 4),
              std::string(testProp.fourcc.data(), testProp.fourcc.data() + 4));
    EXPECT_EQ(std::vector<uint8_t>(
                  decodeProp.boxPayload.data,
                  decodeProp.boxPayload.data + decodeProp.boxPayload.size),
              testProp.body);
  }
}

inline auto ArbitraryProp() {
  auto fourcc = fuzztest::Arbitrary<std::vector<uint8_t>>().WithSize(4);
  auto uuid =
      fuzztest::Arbitrary<std::vector<uint8_t>>().WithSize(16);  // ignored
  auto body = fuzztest::Arbitrary<std::vector<uint8_t>>();
  // Don't return known properties.
  return fuzztest::Filter(
      [](TestProp prop) {
        return !avifIsKnownPropertyType(prop.fourcc.data());
      },
      fuzztest::StructOf<TestProp>(fourcc, uuid, body));
}

inline auto ArbitraryUUIDProp() {
  auto fourcc = fuzztest::Just(UUID_4CC);
  auto uuid = fuzztest::Arbitrary<std::vector<uint8_t>>().WithSize(16);
  auto body = fuzztest::Arbitrary<std::vector<uint8_t>>();
  // Don't use invalid UUIDs
  return fuzztest::Filter(
      [](TestProp prop) { return avifIsValidUUID(prop.uuid.data()); },
      fuzztest::StructOf<TestProp>(fourcc, uuid, body));
}

inline auto ArbitraryProps() {
  return fuzztest::VectorOf(
      fuzztest::OneOf(ArbitraryProp(), ArbitraryUUIDProp()));
}

FUZZ_TEST(PropertiesAvifFuzzTest, PropsValid)
    .WithDomains(ArbitraryAvifImage(), ArbitraryAvifEncoder(),
                 ArbitraryAvifDecoder(), ArbitraryProps());

}  // namespace
}  // namespace testutil
}  // namespace avif
