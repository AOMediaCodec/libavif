// Copyright 2021 Google LLC
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "./avif_parser.h"

#include <algorithm>
#include <fstream>
#include <vector>

#include "gtest/gtest.h"

namespace {

using Data = std::vector<uint8_t>;

Data LoadFile(const char file_path[]) {
  std::ifstream file(file_path, std::ios::binary | std::ios::ate);
  if (!file) return Data();
  const auto file_size = file.tellg();
  Data bytes(file_size * sizeof(char));
  file.seekg(0);  // Rewind.
  return file.read(reinterpret_cast<char*>(bytes.data()), file_size) ? bytes
                                                                     : Data();
}

//------------------------------------------------------------------------------
// Positive tests

TEST(AvifParserGetFeaturesTest, WithoutFileSize) {
  const Data input = LoadFile("avif_parser_test_1x1.avif");
  ASSERT_FALSE(input.empty());

  AvifParserFeatures features;
  EXPECT_EQ(AvifParserGetFeatures(input.data(), input.size(), &features),
            kAvifParserOk);
  EXPECT_EQ(features.width, 1u);
  EXPECT_EQ(features.height, 1u);
  EXPECT_EQ(features.bit_depth, 8u);
  EXPECT_EQ(features.num_channels, 3u);
}

TEST(AvifParserGetFeaturesTest, WithFileSize) {
  const Data input = LoadFile("avif_parser_test_1x1.avif");
  ASSERT_FALSE(input.empty());

  AvifParserFeatures features;
  EXPECT_EQ(
      AvifParserGetFeaturesWithSize(input.data(), /*data_size=*/input.size(),
                                    &features, /*file_size=*/input.size()),
      kAvifParserOk);
  EXPECT_EQ(features.width, 1u);
  EXPECT_EQ(features.height, 1u);
  EXPECT_EQ(features.bit_depth, 8u);
  EXPECT_EQ(features.num_channels, 3u);
}

TEST(AvifParserGetFeaturesTest, WithShorterSize) {
  const Data input = LoadFile("avif_parser_test_1x1.avif");
  ASSERT_FALSE(input.empty());

  AvifParserFeatures features;
  // No more than 'file_size' bytes should be read, even if more are passed.
  EXPECT_EQ(AvifParserGetFeaturesWithSize(
                input.data(), /*data_size=*/input.size() * 10, &features,
                /*file_size=*/input.size()),
            kAvifParserOk);
  EXPECT_EQ(features.width, 1u);
  EXPECT_EQ(features.height, 1u);
  EXPECT_EQ(features.bit_depth, 8u);
  EXPECT_EQ(features.num_channels, 3u);
}

TEST(AvifParserGetFeaturesTest, EnoughBytes) {
  Data input = LoadFile("avif_parser_test_1x1.avif");
  ASSERT_FALSE(input.empty());
  // Truncate 'input' just after the required information (discard AV1 box).
  const uint8_t kMdatTag[] = {'m', 'd', 'a', 't'};
  input.resize(std::search(input.begin(), input.end(), kMdatTag, kMdatTag + 4) -
               input.begin());

  AvifParserFeatures features;
  EXPECT_EQ(AvifParserGetFeatures(input.data(), input.size(), &features),
            kAvifParserOk);
  EXPECT_EQ(features.width, 1u);
  EXPECT_EQ(features.height, 1u);
  EXPECT_EQ(features.bit_depth, 8u);
  EXPECT_EQ(features.num_channels, 3u);
}

TEST(AvifParserGetFeaturesTest, Null) {
  const Data input = LoadFile("avif_parser_test_1x1.avif");
  ASSERT_FALSE(input.empty());

  EXPECT_EQ(AvifParserGetFeatures(input.data(), input.size(), nullptr),
            kAvifParserOk);
}

//------------------------------------------------------------------------------
// Negative tests

TEST(AvifParserGetFeaturesTest, Empty) {
  AvifParserFeatures features;
  EXPECT_EQ(AvifParserGetFeatures(nullptr, 0, &features),
            kAvifParserNotEnoughData);
  EXPECT_EQ(features.width, 0u);
  EXPECT_EQ(features.height, 0u);
  EXPECT_EQ(features.bit_depth, 0u);
  EXPECT_EQ(features.num_channels, 0u);
}

TEST(AvifParserGetFeaturesTest, NotEnoughBytes) {
  Data input = LoadFile("avif_parser_test_1x1.avif");
  ASSERT_FALSE(input.empty());
  // Truncate 'input' before having all the required information.
  const uint8_t kIpmaTag[] = {'i', 'p', 'm', 'a'};
  input.resize(std::search(input.begin(), input.end(), kIpmaTag, kIpmaTag + 4) -
               input.begin());

  AvifParserFeatures features;
  EXPECT_EQ(AvifParserGetFeatures(input.data(), input.size(), &features),
            kAvifParserNotEnoughData);
}

TEST(AvifParserGetFeaturesTest, Broken) {
  Data input = LoadFile("avif_parser_test_1x1.avif");
  ASSERT_FALSE(input.empty());
  // Change "ispe" to "aspe".
  const uint8_t kIspeTag[] = {'i', 's', 'p', 'e'};
  std::search(input.begin(), input.end(), kIspeTag, kIspeTag + 4)[0] = 'a';

  AvifParserFeatures features;
  EXPECT_EQ(AvifParserGetFeatures(input.data(), input.size(), &features),
            kAvifParserInvalidFile);
  EXPECT_EQ(features.width, 0u);
  EXPECT_EQ(features.height, 0u);
  EXPECT_EQ(features.bit_depth, 0u);
  EXPECT_EQ(features.num_channels, 0u);
}

TEST(AvifParserGetFeaturesTest, MetaBoxIsTooBig) {
  Data input = LoadFile("avif_parser_test_1x1.avif");
  ASSERT_FALSE(input.empty());
  // Change "meta" box size to the maximum size 2^32-1.
  const uint8_t kMetaTag[] = {'m', 'e', 't', 'a'};
  auto meta_tag =
      std::search(input.begin(), input.end(), kMetaTag, kMetaTag + 4);
  meta_tag[-4] = meta_tag[-3] = meta_tag[-2] = meta_tag[-1] = 255;

  AvifParserFeatures features;
  EXPECT_EQ(AvifParserGetFeatures(input.data(), input.size(), &features),
            kAvifParserTooComplex);
  EXPECT_EQ(features.width, 0u);
  EXPECT_EQ(features.height, 0u);
  EXPECT_EQ(features.bit_depth, 0u);
  EXPECT_EQ(features.num_channels, 0u);
}

TEST(AvifParserGetFeaturesTest, TooManyBoxes) {
  // Create a valid-ish input with too many boxes to parse.
  Data input = {0, 0, 0, 12, 'f', 't', 'y', 'p', 'a', 'v', 'i', 'f'};
  const uint32_t kNumBoxes = 12345;
  input.reserve(input.size() + kNumBoxes * 8);
  for (uint32_t i = 0; i < kNumBoxes; ++i) {
    const uint8_t kBox[] = {0, 0, 0, 8, 'a', 'b', 'c', 'd'};
    input.insert(input.end(), kBox, kBox + kBox[3]);
  }

  AvifParserFeatures features;
  EXPECT_EQ(AvifParserGetFeatures(reinterpret_cast<uint8_t*>(input.data()),
                                  input.size() * 4, &features),
            kAvifParserTooComplex);
}

//------------------------------------------------------------------------------

}  // namespace

