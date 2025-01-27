// Copyright 2025 Google LLC
// SPDX-License-Identifier: BSD-2-Clause

#include "avif/avif.h"
#include "aviftest_helpers.h"
#include "gtest/gtest.h"

namespace avif {
namespace {

// Used to pass the data folder path to the GoogleTest suites.
const char* data_path = nullptr;

//------------------------------------------------------------------------------

TEST(TransformTest, ClapIrotImir) {
  if (!testutil::Av1EncoderAvailable() || !testutil::Av1DecoderAvailable()) {
    GTEST_SKIP() << "AV1 codec unavailable, skip test.";
  }

  avifDiagnostics diag{};
  ImagePtr image =
      testutil::CreateImage(/*width=*/12, /*height=*/34, /*depth=*/10,
                            AVIF_PIXEL_FORMAT_YUV444, AVIF_PLANES_ALL);
  ASSERT_NE(image, nullptr);
  testutil::FillImageGradient(image.get());  // The pixels do not matter.

  image->transformFlags |= AVIF_TRANSFORM_CLAP;
  const avifCropRect rect{/*x=*/4, /*y=*/6, /*width=*/8, /*height=*/10};
  ASSERT_TRUE(avifCleanApertureBoxFromCropRect(
      &image->clap, &rect, image->width, image->height, &diag));

  image->transformFlags |= AVIF_TRANSFORM_IROT;
  image->irot.angle = 1;
  image->transformFlags |= AVIF_TRANSFORM_IMIR;
  image->imir.axis = 1;

  // Encode.
  EncoderPtr encoder(avifEncoderCreate());
  ASSERT_NE(encoder, nullptr);
  encoder->speed = AVIF_SPEED_FASTEST;
  testutil::AvifRwData encoded;
  ASSERT_EQ(avifEncoderWrite(encoder.get(), image.get(), &encoded),
            AVIF_RESULT_OK);

  // Decode.
  ImagePtr decoded(avifImageCreateEmpty());
  ASSERT_NE(decoded, nullptr);
  DecoderPtr decoder(avifDecoderCreate());
  ASSERT_NE(decoder, nullptr);
  ASSERT_EQ(avifDecoderReadMemory(decoder.get(), decoded.get(), encoded.data,
                                  encoded.size),
            AVIF_RESULT_OK);

  EXPECT_EQ(decoded->transformFlags, image->transformFlags);
  EXPECT_EQ(decoded->clap.widthN, image->clap.widthN);
  EXPECT_EQ(decoded->clap.widthD, image->clap.widthD);
  EXPECT_EQ(decoded->clap.heightN, image->clap.heightN);
  EXPECT_EQ(decoded->clap.heightD, image->clap.heightD);
  EXPECT_EQ(decoded->clap.horizOffN, image->clap.horizOffN);
  EXPECT_EQ(decoded->clap.horizOffD, image->clap.horizOffD);
  EXPECT_EQ(decoded->clap.vertOffN, image->clap.vertOffN);
  EXPECT_EQ(decoded->clap.vertOffD, image->clap.vertOffD);
  EXPECT_EQ(decoded->irot.angle, image->irot.angle);
  EXPECT_EQ(decoded->imir.axis, image->imir.axis);
}

TEST(TransformTest, ClapIrotImirNonEssential) {
  // Invalid file with non-essential transformative properties.
  const std::string path =
      std::string(data_path) + "clap_irot_imir_non_essential.avif";
  DecoderPtr decoder(avifDecoderCreate());
  ASSERT_NE(decoder, nullptr);
  ASSERT_EQ(avifDecoderSetIOFile(decoder.get(), path.c_str()), AVIF_RESULT_OK);
  ASSERT_EQ(avifDecoderParse(decoder.get()), AVIF_RESULT_BMFF_PARSE_FAILED);
}

TEST(TransformTest, ClopIrotImor) {
  // File with a non-essential unrecognized property 'clop', an essential
  // transformation property 'irot', and a non-essential unrecognized property
  // 'imor'.
  const std::string path = std::string(data_path) + "clop_irot_imor.avif";
  DecoderPtr decoder(avifDecoderCreate());
  ASSERT_NE(decoder, nullptr);
  ASSERT_EQ(avifDecoderSetIOFile(decoder.get(), path.c_str()), AVIF_RESULT_OK);
  ASSERT_EQ(avifDecoderParse(decoder.get()), AVIF_RESULT_OK);

  // 'imor' should be ignored as it is after a transformative property in the
  // 'ipma' association order. libavif still surfaces it because this constraint
  // is relaxed in Amd2 of HEIF ISO/IEC 23008-12.
  // See https://github.com/MPEGGroup/FileFormat/issues/113.
  ASSERT_EQ(decoder->image->numProperties, 2u);
  const avifImageItemProperty& clop = decoder->image->properties[0];
  EXPECT_EQ(std::string(clop.boxtype, clop.boxtype + 4), "clop");
  const avifImageItemProperty& imor = decoder->image->properties[1];
  EXPECT_EQ(std::string(imor.boxtype, imor.boxtype + 4), "imor");
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
