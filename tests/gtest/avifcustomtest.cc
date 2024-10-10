// Copyright 2024 Google LLC
// SPDX-License-Identifier: BSD-2-Clause

#include <algorithm>

#include "avif/avif.h"
#include "aviftest_helpers.h"
#include "gtest/gtest.h"

namespace avif {
namespace {

avifResult CustomEncodeImageFunc(avifEncoder* encoder, const avifImage*,
                                 const avifEncoderCustomEncodeImageItem* item,
                                 const avifEncoderCustomEncodeImageArgs*) {
  if (item->type != AVIF_ENCODER_CUSTOM_ENCODE_ITEM_COLOR ||
      item->gridRow != 0 || item->gridColumn != 0) {
    // Unexpected item.
    return AVIF_RESULT_INTERNAL_ERROR;
  }

  if (encoder->customEncodeData != NULL) {
    return AVIF_RESULT_OK;  // Overrides the AV1 codec encoding pipeline.
  } else {
    return AVIF_RESULT_NO_CONTENT;  // Lets libavif encode the image item.
  }
}

avifResult CustomEncodeFinishFunc(avifEncoder* encoder,
                                  const avifEncoderCustomEncodeImageItem* item,
                                  avifROData* sample) {
  if (item->type != AVIF_ENCODER_CUSTOM_ENCODE_ITEM_COLOR ||
      item->gridRow != 0 || item->gridColumn != 0) {
    // Unexpected item.
    return AVIF_RESULT_INTERNAL_ERROR;
  }

  avifROData* av1_payload =
      reinterpret_cast<avifROData*>(encoder->customEncodeData);
  if (av1_payload->size != 0) {
    *sample = *av1_payload;
    *av1_payload = AVIF_DATA_EMPTY;
    return AVIF_RESULT_OK;  // Outputs a sample.
  } else {
    return AVIF_RESULT_NO_IMAGES_REMAINING;  // Done.
  }
}

TEST(BasicTest, EncodeDecode) {
  ImagePtr image = testutil::CreateImage(12, 34, 8, AVIF_PIXEL_FORMAT_YUV420,
                                         AVIF_PLANES_YUV);
  ASSERT_NE(image, nullptr);
  testutil::FillImageGradient(image.get());

  EncoderPtr encoder(avifEncoderCreate());
  ASSERT_NE(encoder, nullptr);
  testutil::AvifRwData encoded;
  ASSERT_EQ(avifEncoderWrite(encoder.get(), image.get(), &encoded),
            AVIF_RESULT_OK);

  const uint8_t* kMdat = reinterpret_cast<const uint8_t*>("mdat");
  const uint8_t* mdat_position =
      std::search(encoded.data, encoded.data + encoded.size, kMdat, kMdat + 4);
  ASSERT_NE(mdat_position, encoded.data + encoded.size);
  avifROData av1_payload{
      mdat_position + 4,
      static_cast<size_t>((encoded.data + encoded.size) - (mdat_position + 4))};

  EncoderPtr encoder_custom(avifEncoderCreate());
  ASSERT_NE(encoder_custom, nullptr);
  encoder_custom->customEncodeData = reinterpret_cast<void*>(&av1_payload);
  encoder_custom->customEncodeImageFunc = CustomEncodeImageFunc;
  encoder_custom->customEncodeFinishFunc = CustomEncodeFinishFunc;
  testutil::AvifRwData encoded_custom;
  ASSERT_EQ(
      avifEncoderWrite(encoder_custom.get(), image.get(), &encoded_custom),
      AVIF_RESULT_OK);

  ASSERT_EQ(encoded.size, encoded_custom.size);
  EXPECT_TRUE(std::equal(encoded.data, encoded.data + encoded.size,
                         encoded_custom.data));
}

}  // namespace
}  // namespace avif
