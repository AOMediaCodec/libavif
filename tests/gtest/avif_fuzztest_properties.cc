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
  std::array<uint8_t, 4> fourcc;
  std::array<uint8_t, 16> uuid;
  std::vector<uint8_t> body;
};

void EncodeDecode(ImagePtr image, EncoderPtr encoder, DecoderPtr decoder,
                  const std::vector<TestProp>& testProps) {
  ImagePtr decoded_image(avifImageCreateEmpty());
  ASSERT_NE(image.get(), nullptr);
  ASSERT_NE(encoder.get(), nullptr);
  ASSERT_NE(decoder.get(), nullptr);
  ASSERT_NE(decoded_image.get(), nullptr);

  for (const TestProp& testProp : testProps) {
    if (testProp.fourcc == std::array<uint8_t, 4>{'u', 'u', 'i', 'd'}) {
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
    const TestProp& testProp = testProps[i];
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
  auto fourcc = fuzztest::Arbitrary<std::array<uint8_t, 4>>();
  auto uuid = fuzztest::Arbitrary<std::array<uint8_t, 16>>();  // ignored
  auto body = fuzztest::Arbitrary<std::vector<uint8_t>>();
  // Don't return known properties.
  return fuzztest::Filter(
      [](const TestProp& prop) {
        return !avifIsKnownPropertyType(prop.fourcc.data());
      },
      fuzztest::StructOf<TestProp>(fourcc, uuid, body));
}

inline auto ArbitraryUUIDProp() {
  auto fourcc = fuzztest::Just(std::array<uint8_t, 4>{'u', 'u', 'i', 'd'});
  auto uuid = fuzztest::Arbitrary<std::array<uint8_t, 16>>();
  auto body = fuzztest::Arbitrary<std::vector<uint8_t>>();
  // Don't use invalid UUIDs.
  return fuzztest::Filter(
      [](const TestProp& prop) { return avifIsValidUUID(prop.uuid.data()); },
      fuzztest::StructOf<TestProp>(fourcc, uuid, body));
}

inline auto ArbitraryProps() {
  return fuzztest::VectorOf(
      fuzztest::OneOf(ArbitraryProp(), ArbitraryUUIDProp()));
}

FUZZ_TEST(PropertiesAvifFuzzTest, EncodeDecode)
    .WithDomains(ArbitraryAvifImage(), ArbitraryAvifEncoder(),
                 ArbitraryAvifDecoder(), ArbitraryProps());

}  // namespace
}  // namespace testutil
}  // namespace avif
