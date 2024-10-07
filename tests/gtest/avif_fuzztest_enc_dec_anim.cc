// Copyright 2023 Google LLC
// SPDX-License-Identifier: BSD-2-Clause

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

struct FrameOptions {
  uint64_t duration;
  avifAddImageFlags flags;
};

// Encodes an animation and decodes it.
// For simplicity, there is only one source image, all frames are identical.
void EncodeDecodeAnimation(std::vector<ImagePtr> frames,
                           const std::vector<FrameOptions>& frame_options,
                           EncoderPtr encoder, DecoderPtr decoder) {
  ASSERT_NE(encoder, nullptr);
  ASSERT_NE(decoder, nullptr);
  ImagePtr decoded_image(avifImageCreateEmpty());
  ASSERT_NE(decoded_image, nullptr);

  const size_t num_frames = frames.size();
  int total_duration = 0;

  // Encode.
  for (size_t i = 0; i < num_frames; ++i) {
    total_duration += frame_options[i].duration;
    const avifResult result =
        avifEncoderAddImage(encoder.get(), frames[i].get(),
                            frame_options[i].duration, frame_options[i].flags);
    ASSERT_EQ(result, AVIF_RESULT_OK)
        << avifResultToString(result) << " " << encoder->diag.error;
  }
  AvifRwData encoded_data;
  avifResult result = avifEncoderFinish(encoder.get(), &encoded_data);
  ASSERT_EQ(result, AVIF_RESULT_OK)
      << avifResultToString(result) << " " << encoder->diag.error;

  // Decode.
  result = avifDecoderSetIOMemory(decoder.get(), encoded_data.data,
                                  encoded_data.size);
  ASSERT_EQ(result, AVIF_RESULT_OK)
      << avifResultToString(result) << " " << decoder->diag.error;

  result = avifDecoderParse(decoder.get());
  ASSERT_EQ(result, AVIF_RESULT_OK)
      << avifResultToString(result) << " " << decoder->diag.error;

  if (decoder->requestedSource == AVIF_DECODER_SOURCE_PRIMARY_ITEM ||
      num_frames == 1) {
    ASSERT_EQ(decoder->imageCount, 1);
  } else {
    ASSERT_EQ(decoder->imageCount, num_frames);
    EXPECT_EQ(decoder->durationInTimescales, total_duration);

    for (size_t i = 0; i < num_frames; ++i) {
      result = avifDecoderNextImage(decoder.get());
      ASSERT_EQ(result, AVIF_RESULT_OK)
          << " frame " << i << ": " << avifResultToString(result) << " ("
          << decoder->diag.error << ")";
      EXPECT_EQ(decoder->image->width, frames[i]->width);
      EXPECT_EQ(decoder->image->height, frames[i]->height);
      EXPECT_EQ(decoder->image->depth, frames[i]->depth);
      EXPECT_EQ(decoder->image->yuvFormat, frames[i]->yuvFormat);
    }
    result = avifDecoderNextImage(decoder.get());
    ASSERT_EQ(result, AVIF_RESULT_NO_IMAGES_REMAINING)
        << avifResultToString(result) << " " << decoder->diag.error;
  }
}

FUZZ_TEST(EncodeDecodeAvifFuzzTest, EncodeDecodeAnimation)
    .WithDomains(
        ArbitraryAvifAnim(),
        fuzztest::VectorOf(
            fuzztest::StructOf<FrameOptions>(
                /*duration=*/fuzztest::InRange<uint64_t>(1, 10),
                // BitFlagCombinationOf() requires non zero flags as input.
                // AVIF_ADD_IMAGE_FLAG_NONE (0) is implicitly part of the set.
                fuzztest::BitFlagCombinationOf<avifAddImageFlags>(
                    {AVIF_ADD_IMAGE_FLAG_FORCE_KEYFRAME})))
            // Some FrameOptions may be wasted, but this is simpler.
            .WithSize(kMaxNumFrames),
        ArbitraryAvifEncoder(), ArbitraryAvifDecoder());

}  // namespace
}  // namespace testutil
}  // namespace avif
