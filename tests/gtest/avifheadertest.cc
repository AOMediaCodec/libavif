// Copyright 2024 Google LLC
// SPDX-License-Identifier: BSD-2-Clause

#include "avif/avif.h"
#include "aviftest_helpers.h"
#include "gtest/gtest.h"

namespace avif {
namespace {

TEST(BasicTest, EncodeDecode) {
  ImagePtr image = testutil::CreateImage(12, 34, 8, AVIF_PIXEL_FORMAT_YUV420,
                                         AVIF_PLANES_ALL);
  ASSERT_NE(image, nullptr);
  testutil::FillImageGradient(image.get());

  EncoderPtr encoder_header_full(avifEncoderCreate());
  ASSERT_NE(encoder_header_full, nullptr);
  encoder_header_full->headerFormat = AVIF_HEADER_FULL;
  testutil::AvifRwData encoded_header_full;
  ASSERT_EQ(avifEncoderWrite(encoder_header_full.get(), image.get(),
                             &encoded_header_full),
            AVIF_RESULT_OK);

  EncoderPtr encoder_header_default(avifEncoderCreate());
  ASSERT_NE(encoder_header_default, nullptr);
  encoder_header_default->headerFormat = AVIF_HEADER_DEFAULT;
  testutil::AvifRwData encoded_header_default;
  ASSERT_EQ(avifEncoderWrite(encoder_header_default.get(), image.get(),
                             &encoded_header_default),
            AVIF_RESULT_OK);

  // AVIF_HEADER_DEFAULT saves 7 bytes by omitting "libavif" as 'hdlr' name.
  EXPECT_EQ(encoded_header_full.size, encoded_header_default.size + 7);
}

}  // namespace
}  // namespace avif
