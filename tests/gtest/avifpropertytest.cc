// Copyright 2024 Google LLC
// SPDX-License-Identifier: BSD-2-Clause

#include <cstring>
#include <iostream>
#include <string>
#include <vector>

#include "avif/avif.h"
#include "aviftest_helpers.h"
#include "gtest/gtest.h"

namespace avif {
namespace {

// Used to pass the data folder path to the GoogleTest suites.
const char* data_path = nullptr;

//------------------------------------------------------------------------------

TEST(AvifPropertyTest, Parse) {
  const std::string path =
      std::string(data_path) + "circle_custom_properties.avif";
  DecoderPtr decoder(avifDecoderCreate());
  ASSERT_NE(decoder, nullptr);
  ASSERT_EQ(avifDecoderSetIOFile(decoder.get(), path.c_str()), AVIF_RESULT_OK);
  ASSERT_EQ(avifDecoderParse(decoder.get()), AVIF_RESULT_OK);
  ASSERT_EQ(decoder->image->numProperties, 3u);

  const avifImageItemProperty& p1234 = decoder->image->properties[0];
  EXPECT_EQ(std::string(p1234.boxtype, p1234.boxtype + 4), "1234");
  EXPECT_EQ(std::vector<uint8_t>(p1234.boxPayload.data,
                                 p1234.boxPayload.data + p1234.boxPayload.size),
            std::vector<uint8_t>({/*version*/ 0, /*flags*/ 0, 0, 0,
                                  /*FullBoxPayload*/ 1, 2, 3, 4}));

  const avifImageItemProperty& abcd = decoder->image->properties[1];
  EXPECT_EQ(std::string(abcd.boxtype, abcd.boxtype + 4), "abcd");
  EXPECT_EQ(std::string(reinterpret_cast<const char*>(abcd.boxPayload.data)),
            "abcd");

  const avifImageItemProperty& uuid = decoder->image->properties[2];
  EXPECT_EQ(std::string(uuid.boxtype, uuid.boxtype + 4), "uuid");
  EXPECT_EQ(std::string(uuid.usertype, uuid.usertype + 16), "extended_type 16");
  EXPECT_EQ(uuid.boxPayload.size, 0);
}

TEST(AvifPropertyTest, Serialise) {
  ImagePtr image = testutil::CreateImage(128, 30, 8, AVIF_PIXEL_FORMAT_YUV420,
                                         AVIF_PLANES_ALL);
  ASSERT_NE(image, nullptr);
  testutil::FillImageGradient(image.get());
  std::vector<uint8_t> abcd_data({0, 0, 0, 1, 'a', 'b', 'c'});
  std::vector<uint8_t> efgh_data({'e', 'h'});
  uint8_t uuid[16] = {0x95, 0x96, 0xf1, 0xad, 0xb8, 0xab, 0x4a, 0xfc,
                      0x9e, 0xfc, 0x83, 0x87, 0xac, 0x79, 0x37, 0xda};
  std::vector<uint8_t> uuid_data({'x', 'y', 'z'});
  ASSERT_EQ(avifImageAddOpaqueProperty(image.get(), (uint8_t*)"abcd",
                                       abcd_data.data(), abcd_data.size()),
            AVIF_RESULT_OK);
  ASSERT_EQ(avifImageAddOpaqueProperty(image.get(), (uint8_t*)"efgh",
                                       efgh_data.data(), efgh_data.size()),
            AVIF_RESULT_OK);
  // Should not be added
  ASSERT_EQ(avifImageAddOpaqueProperty(image.get(), (uint8_t*)"mdat",
                                       efgh_data.data(), efgh_data.size()),
            AVIF_RESULT_INVALID_ARGUMENT);
  ASSERT_EQ(avifImageAddUUIDProperty(image.get(), uuid, uuid_data.data(),
                                     uuid_data.size()),
            AVIF_RESULT_OK);

  EncoderPtr encoder(avifEncoderCreate());
  ASSERT_NE(encoder, nullptr);
  testutil::AvifRwData encoded;
  avifResult result = avifEncoderWrite(encoder.get(), image.get(), &encoded);
  ASSERT_EQ(result, AVIF_RESULT_OK) << avifResultToString(result);

  DecoderPtr decoder(avifDecoderCreate());
  ASSERT_NE(decoder, nullptr);
  ASSERT_EQ(avifDecoderSetIOMemory(decoder.get(), encoded.data, encoded.size),
            AVIF_RESULT_OK);
  ASSERT_EQ(avifDecoderParse(decoder.get()), AVIF_RESULT_OK);
  ASSERT_EQ(decoder->image->numProperties, 3u);

  const avifImageItemProperty& abcd = decoder->image->properties[0];
  EXPECT_EQ(std::string(abcd.boxtype, abcd.boxtype + 4), "abcd");
  EXPECT_EQ(std::vector<uint8_t>(abcd.boxPayload.data,
                                 abcd.boxPayload.data + abcd.boxPayload.size),
            abcd_data);

  const avifImageItemProperty& efgh = decoder->image->properties[1];
  EXPECT_EQ(std::string(efgh.boxtype, efgh.boxtype + 4), "efgh");
  EXPECT_EQ(std::vector<uint8_t>(efgh.boxPayload.data,
                                 efgh.boxPayload.data + efgh.boxPayload.size),
            efgh_data);

  const avifImageItemProperty& uuidProp = decoder->image->properties[2];
  EXPECT_EQ(std::string(uuidProp.boxtype, uuidProp.boxtype + 4), "uuid");
  EXPECT_EQ(std::vector<uint8_t>(uuidProp.usertype, uuidProp.usertype + 16),
            std::vector<uint8_t>(uuid, uuid + 16));
  EXPECT_EQ(
      std::vector<uint8_t>(uuidProp.boxPayload.data,
                           uuidProp.boxPayload.data + uuidProp.boxPayload.size),
      uuid_data);
}

TEST(AvifPropertyTest, TooManyUniqueProperties) {
  ImagePtr image = testutil::CreateImage(128, 30, 8, AVIF_PIXEL_FORMAT_YUV420,
                                         AVIF_PLANES_ALL);
  ASSERT_NE(image, nullptr);
  testutil::FillImageGradient(image.get());
  for (uint8_t i = 0; i < 128; ++i) {
    ASSERT_EQ(avifImageAddOpaqueProperty(image.get(), (uint8_t*)"abcd", &i, 1),
              AVIF_RESULT_OK);
  }

  EncoderPtr encoder(avifEncoderCreate());
  ASSERT_NE(encoder, nullptr);
  testutil::AvifRwData encoded;
  ASSERT_EQ(avifEncoderWrite(encoder.get(), image.get(), &encoded),
            AVIF_RESULT_INVALID_ARGUMENT);
}

TEST(AvifPropertyTest, ManyTimesTheSameProperty) {
  ImagePtr image = testutil::CreateImage(128, 30, 8, AVIF_PIXEL_FORMAT_YUV420,
                                         AVIF_PLANES_ALL);
  ASSERT_NE(image, nullptr);
  testutil::FillImageGradient(image.get());
  for (uint8_t i = 0; i < 128; ++i) {
    const uint8_t sameData = 42;
    ASSERT_EQ(
        avifImageAddOpaqueProperty(image.get(), (uint8_t*)"abcd", &sameData, 1),
        AVIF_RESULT_OK);
  }

  EncoderPtr encoder(avifEncoderCreate());
  ASSERT_NE(encoder, nullptr);
  testutil::AvifRwData encoded;
  ASSERT_EQ(avifEncoderWrite(encoder.get(), image.get(), &encoded),
            AVIF_RESULT_OK);
}

//------------------------------------------------------------------------------

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
