// Copyright 2023 Google LLC
// SPDX-License-Identifier: BSD-2-Clause

#include "avif/avif.h"
#include "aviftest_helpers.h"
#include "gtest/gtest.h"

namespace avif {
namespace {

TEST(BasicTest, EncodeDecode) {
  ImagePtr image =
      testutil::CreateImage(/*width=*/12, /*height=*/34, /*depth=*/8,
                            AVIF_PIXEL_FORMAT_YUV420, AVIF_PLANES_ALL);
  ASSERT_NE(image, nullptr);
  testutil::FillImageGradient(image.get());

  EncoderPtr encoder(avifEncoderCreate());
  ASSERT_NE(encoder, nullptr);
  testutil::AvifRwData encoded;
  avifResult result = avifEncoderWrite(encoder.get(), image.get(), &encoded);
  ASSERT_EQ(result, AVIF_RESULT_OK) << avifResultToString(result);

  ImagePtr decoded(avifImageCreateEmpty());
  ASSERT_NE(decoded, nullptr);
  DecoderPtr decoder(avifDecoderCreate());
  ASSERT_NE(decoder, nullptr);
  result = avifDecoderReadMemory(decoder.get(), decoded.get(), encoded.data,
                                 encoded.size);
  ASSERT_EQ(result, AVIF_RESULT_OK) << avifResultToString(result);
  EXPECT_EQ(decoder->imageSequenceTrackPresent, AVIF_FALSE);

  // Verify that the input and decoded images are close.
  ASSERT_GT(testutil::GetPsnr(*image, *decoded), 40.0);

  // Uncomment the following to save the encoded image as an AVIF file.
  //  std::ofstream("/tmp/avifbasictest.avif", std::ios::binary)
  //      .write(reinterpret_cast<char*>(encoded.data), encoded.size);

  // Uncomment the following to save the decoded image as a PNG file.
  //  testutil::WriteImage(decoded.get(), "/tmp/avifbasictest.png");
}

TEST(BasicTest, RGBImageCleanup) {
  // Make sure the following does not create sanitizer bugs.
  ImagePtr image =
      testutil::CreateImage(/*width=*/12, /*height=*/34, /*depth=*/8,
                            AVIF_PIXEL_FORMAT_YUV420, AVIF_PLANES_ALL);

  avifRGBImage rgb;
  avifRGBImageSetDefaults(&rgb, image.get());
  RGBImageCleanup cleanup(&rgb);
  ASSERT_EQ(avifRGBImageAllocatePixels(&rgb), AVIF_RESULT_OK);
}

TEST(BasicTest, NullPointerGuards) {
  avifGetPixelFormatInfo(AVIF_PIXEL_FORMAT_YUV420, nullptr);
  avifImageSetDefaults(nullptr);
  avifImageDestroy(nullptr);
  avifImageFreePlanes(nullptr, AVIF_PLANES_ALL);
  avifImageStealPlanes(nullptr, nullptr, AVIF_PLANES_ALL);
  EXPECT_EQ(avifImageUsesU16(nullptr), AVIF_FALSE);
  EXPECT_EQ(avifImageIsOpaque(nullptr), AVIF_TRUE);
  EXPECT_EQ(avifImagePlane(nullptr, AVIF_CHAN_Y), nullptr);
  EXPECT_EQ(avifImagePlaneRowBytes(nullptr, AVIF_CHAN_Y), 0u);
  EXPECT_EQ(avifImagePlaneWidth(nullptr, AVIF_CHAN_Y), 0u);
  EXPECT_EQ(avifImagePlaneHeight(nullptr, AVIF_CHAN_Y), 0u);
  EXPECT_EQ(avifImageCopy(nullptr, nullptr, AVIF_PLANES_ALL),
            AVIF_RESULT_INVALID_ARGUMENT);
  avifImageCopyNoAlloc(nullptr, nullptr);
  avifImageCopySamples(nullptr, nullptr, AVIF_PLANES_ALL);
  EXPECT_EQ(avifImageSetViewRect(nullptr, nullptr, nullptr),
            AVIF_RESULT_INVALID_ARGUMENT);
  EXPECT_EQ(avifImageAllocatePlanes(nullptr, AVIF_PLANES_ALL),
            AVIF_RESULT_INVALID_ARGUMENT);
  EXPECT_EQ(avifImageSetProfileICC(nullptr, nullptr, 0),
            AVIF_RESULT_INVALID_ARGUMENT);
  EXPECT_EQ(avifImageSetMetadataXMP(nullptr, nullptr, 0),
            AVIF_RESULT_INVALID_ARGUMENT);

  avifRGBImageSetDefaults(nullptr, nullptr);
  EXPECT_EQ(avifRGBImagePixelSize(nullptr), 0u);
  EXPECT_EQ(avifRGBImageAllocatePixels(nullptr), AVIF_RESULT_INVALID_ARGUMENT);
  avifRGBImageFreePixels(nullptr);

  avifGainMapSetDefaults(nullptr);
  avifGainMapDestroy(nullptr);
  EXPECT_EQ(avifGainMapValidateMetadata(nullptr), AVIF_FALSE);
  EXPECT_EQ(avifSameGainMapMetadata(nullptr, nullptr), AVIF_TRUE);

  avifEncoderDestroy(nullptr);
  avifEncoderSetCodecSpecificOption(nullptr, "key", "val");
  EXPECT_EQ(avifEncoderAddImage(nullptr, nullptr, 1, 0),
            AVIF_RESULT_INVALID_ARGUMENT);
  EXPECT_EQ(avifEncoderAddImageGrid(nullptr, nullptr, 1, 1, nullptr, 0),
            AVIF_RESULT_INVALID_ARGUMENT);
  EXPECT_EQ(avifEncoderFinish(nullptr, nullptr), AVIF_RESULT_INVALID_ARGUMENT);
  EXPECT_EQ(avifEncoderWrite(nullptr, nullptr, nullptr),
            AVIF_RESULT_INVALID_ARGUMENT);

  avifDecoderDestroy(nullptr);
  EXPECT_EQ(avifDecoderSetSource(nullptr, AVIF_DECODER_SOURCE_AUTO),
            AVIF_RESULT_INVALID_ARGUMENT);
  avifDecoderSetIO(nullptr, nullptr);
  EXPECT_EQ(avifDecoderSetIOMemory(nullptr, nullptr, 0),
            AVIF_RESULT_INVALID_ARGUMENT);
  EXPECT_EQ(avifDecoderSetIOFile(nullptr, nullptr),
            AVIF_RESULT_INVALID_ARGUMENT);
  EXPECT_EQ(avifDecoderParse(nullptr), AVIF_RESULT_INVALID_ARGUMENT);
  EXPECT_EQ(avifDecoderReset(nullptr), AVIF_RESULT_INVALID_ARGUMENT);
  EXPECT_EQ(avifDecoderNextImage(nullptr), AVIF_RESULT_INVALID_ARGUMENT);
  EXPECT_EQ(avifDecoderNthImage(nullptr, 0), AVIF_RESULT_INVALID_ARGUMENT);
  EXPECT_EQ(avifDecoderNthImageTiming(nullptr, 0, nullptr),
            AVIF_RESULT_INVALID_ARGUMENT);
  EXPECT_EQ(avifDecoderNthImageMaxExtent(nullptr, 0, nullptr),
            AVIF_RESULT_INVALID_ARGUMENT);
  EXPECT_EQ(avifDecoderIsKeyframe(nullptr, 0), AVIF_FALSE);
  EXPECT_EQ(avifDecoderNearestKeyframe(nullptr, 0), 0u);
  EXPECT_EQ(avifDecoderDecodedRowCount(nullptr), 0u);
  EXPECT_EQ(avifDecoderRead(nullptr, nullptr), AVIF_RESULT_INVALID_ARGUMENT);
  EXPECT_EQ(avifDecoderReadMemory(nullptr, nullptr, nullptr, 0),
            AVIF_RESULT_INVALID_ARGUMENT);
  EXPECT_EQ(avifDecoderReadFile(nullptr, nullptr, nullptr),
            AVIF_RESULT_INVALID_ARGUMENT);

  EXPECT_EQ(avifRWDataRealloc(nullptr, 0), AVIF_RESULT_INVALID_ARGUMENT);
  EXPECT_EQ(avifRWDataSet(nullptr, nullptr, 0), AVIF_RESULT_INVALID_ARGUMENT);
  avifRWDataFree(nullptr);

  avifCodecSpecificOptionsClear(nullptr);
  avifCodecSpecificOptionsDestroy(nullptr);
  EXPECT_EQ(avifCodecSpecificOptionsSet(nullptr, nullptr, nullptr),
            AVIF_RESULT_INVALID_ARGUMENT);
}

}  // namespace
}  // namespace avif
