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
