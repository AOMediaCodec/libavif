// Copyright 2022 Google LLC
// SPDX-License-Identifier: BSD-2-Clause

#include "avif/avif.h"
#include "aviftest_helpers.h"
#include "gtest/gtest.h"

namespace libavif {
namespace {

//------------------------------------------------------------------------------

TEST(AvifReducedHeaderTest, SimpleOpaque) {
  testutil::AvifImagePtr image =
      testutil::CreateImage(/*width=*/12, /*height=*/34, /*depth=*/10,
                            AVIF_PIXEL_FORMAT_YUV444, AVIF_PLANES_YUV);
  ASSERT_NE(image, nullptr);
  testutil::FillImageGradient(image.get());  // The pixels do not matter.

  // Encode.
  testutil::AvifRwData encoded_avif_reduced_header;
  testutil::AvifEncoderPtr encoder(avifEncoderCreate(), avifEncoderDestroy);
  ASSERT_NE(encoder, nullptr);
  encoder->headerStrategy = AVIF_ENCODER_MINIMIZE_HEADER;
  ASSERT_EQ(avifEncoderWrite(encoder.get(), image.get(),
                             &encoded_avif_reduced_header),
            AVIF_RESULT_OK);

  // Decode.
  const testutil::AvifImagePtr decoded_reduced_header = testutil::Decode(
      encoded_avif_reduced_header.data, encoded_avif_reduced_header.size);
  ASSERT_NE(decoded_reduced_header, nullptr);

  // Compare.
  testutil::AvifRwData encoded_avif_full_header = testutil::Encode(image.get());
  ASSERT_NE(encoded_avif_full_header.data, nullptr);
  EXPECT_LT(encoded_avif_reduced_header.size, encoded_avif_full_header.size);

  const testutil::AvifImagePtr decoded_full_header = testutil::Decode(
      encoded_avif_full_header.data, encoded_avif_full_header.size);
  ASSERT_NE(decoded_full_header, nullptr);

  // Only the container changed, the pixels and features should be identical.
  EXPECT_TRUE(testutil::AreImagesEqual(*decoded_full_header.get(),
                                       *decoded_reduced_header.get()));
}

// TODO(yguyon): Alpha test

// TODO(yguyon): Grid test

//------------------------------------------------------------------------------

}  // namespace
}  // namespace libavif
