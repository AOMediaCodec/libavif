// Copyright 2026 Google LLC
// SPDX-License-Identifier: BSD-2-Clause

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "avif/avif.h"
#include "avif_fuzztest_helpers.h"
#include "aviftest_helpers.h"
#include "fuzztest/fuzztest.h"
#include "gtest/gtest.h"

namespace avif {
namespace testutil {
namespace {

constexpr size_t kLayerCount = kMaxNumLayers;

struct UniformScale {
  int32_t n;
  int32_t d;
};

constexpr UniformScale kNoScale = {1, 1};
constexpr size_t kRandomScalingModeCount = kLayerCount - 1;
static_assert(kRandomScalingModeCount == 3);
constexpr std::array<UniformScale, 6> kSupportedScalingModes = {
    UniformScale{1, 8}, UniformScale{1, 4}, UniformScale{1, 2},
    UniformScale{3, 5}, UniformScale{3, 4}, UniformScale{4, 5}};
constexpr avifScalingMode kNoScalingMode = {{kNoScale.n, kNoScale.d},
                                            {kNoScale.n, kNoScale.d}};
constexpr std::array<avifScalingMode, kLayerCount> kNoScalingModes = {
    kNoScalingMode, kNoScalingMode, kNoScalingMode, kNoScalingMode};

constexpr avifScalingMode MakeScalingMode(UniformScale horizontal,
                                          UniformScale vertical) {
  return avifScalingMode{{horizontal.n, horizontal.d},
                         {vertical.n, vertical.d}};
}

constexpr size_t BinomialCoefficient(size_t n, size_t k) {
  if (k > n) {
    return 0;
  }
  if (k > (n - k)) {
    k = n - k;
  }
  size_t coefficient = 1;
  for (size_t i = 1; i <= k; ++i) {
    coefficient = (coefficient * (n - k + i)) / i;
  }
  return coefficient;
}

constexpr size_t CombinationCountWithRepetition(size_t value_count,
                                                size_t pick_count) {
  return BinomialCoefficient(value_count + pick_count - 1, pick_count);
}

constexpr size_t kScalingModeCombinationCount = CombinationCountWithRepetition(
    kSupportedScalingModes.size(), kRandomScalingModeCount);

std::array<UniformScale, kRandomScalingModeCount> GetScalingModeCombination(
    size_t combination_index) {
  std::array<UniformScale, kRandomScalingModeCount> combination = {};
  size_t remaining_index = combination_index;
  size_t next_candidate = 0;
  for (size_t slot = 0; slot < combination.size(); ++slot) {
    const size_t remaining_slots = combination.size() - slot - 1;
    for (size_t candidate = next_candidate;
         candidate < kSupportedScalingModes.size(); ++candidate) {
      const size_t combinations_with_candidate = CombinationCountWithRepetition(
          kSupportedScalingModes.size() - candidate, remaining_slots);
      if (remaining_index < combinations_with_candidate) {
        combination[slot] = kSupportedScalingModes[candidate];
        next_candidate = candidate;
        break;
      }
      remaining_index -= combinations_with_candidate;
    }
  }
  return combination;
}

std::array<avifScalingMode, kLayerCount> MakeRandomScalingModes(
    int horizontal_combination_index, int vertical_combination_index) {
  const auto horizontal_combination =
      GetScalingModeCombination(horizontal_combination_index);
  const auto vertical_combination =
      GetScalingModeCombination(vertical_combination_index);
  return std::array<avifScalingMode, kLayerCount>{
      MakeScalingMode(horizontal_combination[0], vertical_combination[0]),
      MakeScalingMode(horizontal_combination[1], vertical_combination[1]),
      MakeScalingMode(horizontal_combination[2], vertical_combination[2]),
      kNoScalingMode};
}

inline auto ArbitraryScalingModes() {
  constexpr int kMaxCombinationIndex =
      static_cast<int>(kScalingModeCombinationCount - 1);
  return fuzztest::Map(MakeRandomScalingModes,
                       fuzztest::InRange<int>(0, kMaxCombinationIndex),
                       fuzztest::InRange<int>(0, kMaxCombinationIndex));
}

void EncodeDecodeLayered(
    std::vector<ImagePtr> layers,
    const std::array<avifScalingMode, kLayerCount>& scaling_modes,
    uint32_t expected_width, uint32_t expected_height,
    bool use_rendered_size_override, EncoderPtr encoder, DecoderPtr decoder) {
  ASSERT_EQ(layers.size(), kLayerCount);
  ASSERT_NE(encoder, nullptr);
  ASSERT_NE(decoder, nullptr);

  if (avifCodecName(AVIF_CODEC_CHOICE_AOM, AVIF_CODEC_FLAG_CAN_ENCODE) ==
      nullptr) {
    return;
  }
  if (avifLibYUVVersion() == 0) {
    return;
  }

  const avifImage* const reference = layers.front().get();
  ASSERT_NE(reference, nullptr);

  encoder->codecChoice = AVIF_CODEC_CHOICE_AOM;
  encoder->extraLayerCount = static_cast<uint32_t>(layers.size() - 1);
  if (use_rendered_size_override) {
    encoder->width = expected_width;
    encoder->height = expected_height;
  }

  for (size_t i = 0; i < layers.size(); ++i) {
    ASSERT_NE(layers[i].get(), nullptr);
    encoder->scalingMode = scaling_modes[i];
    const avifResult result = avifEncoderAddImage(
        encoder.get(), layers[i].get(),
        /*durationInTimescales=*/1, AVIF_ADD_IMAGE_FLAG_NONE);
    ASSERT_EQ(result, AVIF_RESULT_OK)
        << " layer " << i << ": " << avifResultToString(result) << ": "
        << encoder->diag.error;
  }

  AvifRwData encoded_data;
  avifResult result = avifEncoderFinish(encoder.get(), &encoded_data);
  ASSERT_EQ(result, AVIF_RESULT_OK)
      << avifResultToString(result) << ": " << encoder->diag.error;

  result = avifDecoderSetIOMemory(decoder.get(), encoded_data.data,
                                  encoded_data.size);
  ASSERT_EQ(result, AVIF_RESULT_OK)
      << avifResultToString(result) << ": " << decoder->diag.error;

  result = avifDecoderParse(decoder.get());
  ASSERT_EQ(result, AVIF_RESULT_OK)
      << avifResultToString(result) << ": " << decoder->diag.error;

  const int num_decodes =
      decoder->allowProgressive ? static_cast<int>(layers.size()) : 1;
  if (decoder->allowProgressive) {
    EXPECT_EQ(decoder->progressiveState, AVIF_PROGRESSIVE_STATE_ACTIVE);
    ASSERT_EQ(decoder->imageCount, static_cast<int>(layers.size()));
  } else {
    EXPECT_EQ(decoder->progressiveState, AVIF_PROGRESSIVE_STATE_AVAILABLE);
    ASSERT_EQ(decoder->imageCount, 1);
  }

  for (int i = 0; i < num_decodes; ++i) {
    result = avifDecoderNextImage(decoder.get());
    ASSERT_EQ(result, AVIF_RESULT_OK)
        << " layer " << i << ": " << avifResultToString(result) << ": "
        << decoder->diag.error;
    EXPECT_EQ(decoder->image->width, expected_width);
    EXPECT_EQ(decoder->image->height, expected_height);
    EXPECT_EQ(decoder->image->depth, reference->depth);
    EXPECT_EQ(decoder->image->yuvFormat, reference->yuvFormat);
  }
  result = avifDecoderNextImage(decoder.get());
  ASSERT_EQ(result, AVIF_RESULT_NO_IMAGES_REMAINING)
      << avifResultToString(result) << ": " << decoder->diag.error;
}

void EncodeDecodeDimensionChange(
    std::vector<ImagePtr> layers,
    const std::array<avifScalingMode, kLayerCount>& scaling_modes,
    EncoderPtr encoder, DecoderPtr decoder) {
  ASSERT_EQ(layers.size(), kLayerCount);
  ASSERT_NE(layers.back().get(), nullptr);
  const uint32_t expected_width = layers.back()->width;
  const uint32_t expected_height = layers.back()->height;
  EncodeDecodeLayered(std::move(layers), scaling_modes,
                      /*expected_width=*/expected_width,
                      /*expected_height=*/expected_height,
                      /*use_rendered_size_override=*/false, std::move(encoder),
                      std::move(decoder));
}

void EncodeDecodeDimensionChangeExternal(std::vector<ImagePtr> layers,
                                         EncoderPtr encoder,
                                         DecoderPtr decoder) {
  ASSERT_EQ(layers.size(), kLayerCount);
  ASSERT_NE(layers.back().get(), nullptr);
  const uint32_t expected_width = layers.back()->width;
  const uint32_t expected_height = layers.back()->height;
  EncodeDecodeLayered(std::move(layers), kNoScalingModes,
                      /*expected_width=*/expected_width,
                      /*expected_height=*/expected_height,
                      /*use_rendered_size_override=*/true, std::move(encoder),
                      std::move(decoder));
}

FUZZ_TEST(LayeredEncodeDecodeAvifFuzzTest, EncodeDecodeDimensionChange)
    .WithDomains(ArbitraryAvifLayered(), ArbitraryScalingModes(),
                 ArbitraryAvifEncoder(), ArbitraryAvifDecoder());

FUZZ_TEST(LayeredEncodeDecodeAvifFuzzTest, EncodeDecodeDimensionChangeExternal)
    .WithDomains(ArbitraryAvifLayeredRandDim(), ArbitraryAvifEncoder(),
                 ArbitraryAvifDecoder());

}  // namespace
}  // namespace testutil
}  // namespace avif
