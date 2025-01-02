// Copyright 2024 Google LLC
// SPDX-License-Identifier: BSD-2-Clause

#include <algorithm>

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
  const testutil::AvifRwData avif = testutil::Encode(image.get());
  ASSERT_NE(avif.data, nullptr);

  // Make sure the HandlerBox is as small as possible, meaning its name field is
  // empty.
  const uint8_t kHdlr[] = {'h', 'd', 'l', 'r'};
  const uint8_t* hdlr_position =
      std::search(avif.data, avif.data + avif.size, kHdlr, kHdlr + 4);
  ASSERT_NE(hdlr_position, avif.data + avif.size);
  // The previous four bytes represent the size of the box as a big endian
  // unsigned integer.
  constexpr uint8_t kExpectedHdlrSize =
      /*size field*/ 4 + /*"hdlr"*/ 4 + /*version*/ 3 + /*flags*/ 1 +
      /*pre_defined*/ 4 + /*handler_type*/ 4 + /*reserved*/ 4 * 3 + /*name*/ 1;
  ASSERT_EQ(hdlr_position[-4], 0);
  ASSERT_EQ(hdlr_position[-3], 0);
  ASSERT_EQ(hdlr_position[-2], 0);
  ASSERT_EQ(hdlr_position[-1], kExpectedHdlrSize);
}

}  // namespace
}  // namespace avif
