// Copyright 2023 Google LLC
// SPDX-License-Identifier: BSD-2-Clause

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

struct DecoderLimitsInput {
  const char* name;
  int cell_width;
  int cell_height;
  uint32_t grid_cols;
  uint32_t grid_rows;
};

class DecoderLimitsTest : public testing::TestWithParam<DecoderLimitsInput> {};

TEST_P(DecoderLimitsTest, ConfiguredImageLimits) {
  if (avifCodecName(AVIF_CODEC_CHOICE_AOM, AVIF_CODEC_FLAG_CAN_ENCODE) ==
          nullptr ||
      !testutil::Av1DecoderAvailable()) {
    GTEST_SKIP() << "AOM encoder or AV1 decoder unavailable, skip test.";
  }
  const DecoderLimitsInput& input = GetParam();
  ImagePtr cell = testutil::CreateImage(input.cell_width, input.cell_height,
                                        /*depth=*/8, AVIF_PIXEL_FORMAT_YUV444,
                                        AVIF_PLANES_YUV);
  ASSERT_NE(cell, nullptr);
  testutil::FillImageGradient(cell.get());

  EncoderPtr encoder(avifEncoderCreate());
  ASSERT_NE(encoder, nullptr);
  encoder->codecChoice = AVIF_CODEC_CHOICE_AOM;
  encoder->speed = AVIF_SPEED_FASTEST;
  testutil::AvifRwData encoded;
  if (input.grid_cols == 1 && input.grid_rows == 1) {
    ASSERT_EQ(avifEncoderWrite(encoder.get(), cell.get(), &encoded),
              AVIF_RESULT_OK);
  } else {
    // Multi-cell grids require cells of at least 64x64 pixels. Each cell is
    // also within every accepted limit below, independently of the grid size.
    const std::vector<const avifImage*> cells(input.grid_cols * input.grid_rows,
                                              cell.get());
    ASSERT_EQ(
        avifEncoderAddImageGrid(encoder.get(), input.grid_cols, input.grid_rows,
                                cells.data(), AVIF_ADD_IMAGE_FLAG_SINGLE),
        AVIF_RESULT_OK);
    ASSERT_EQ(avifEncoderFinish(encoder.get(), &encoded), AVIF_RESULT_OK);
  }
  const uint32_t width = cell->width * input.grid_cols;
  const uint32_t height = cell->height * input.grid_rows;
  const uint32_t pixel_count = width * height;
  const uint32_t max_dimension = (width > height) ? width : height;
  struct LimitCase {
    const char* name;
    uint32_t size_limit;
    uint32_t dimension_limit;
    bool accepted;
  };
  const LimitCase cases[] = {
      {"InclusivePixelLimit", pixel_count, 0, true},
      {"InclusiveDimensionLimit", 0, max_dimension, true},
      {"BothInclusiveLimits", pixel_count, max_dimension, true},
      {"PixelLimitExceeded", pixel_count - 1, max_dimension, false},
      {"DimensionLimitExceeded", pixel_count, max_dimension - 1, false},
      {"SizeDisabledDimensionEnforced", 0, max_dimension - 1, false},
      {"DimensionDisabledSizeEnforced", pixel_count - 1, 0, false},
      {"BothDisabled", 0, 0, true},
  };
  for (const LimitCase& limit : cases) {
    SCOPED_TRACE(limit.name);
    DecoderPtr decoder(avifDecoderCreate());
    ASSERT_NE(decoder, nullptr);
    decoder->imageSizeLimit = limit.size_limit;
    decoder->imageDimensionLimit = limit.dimension_limit;
    ASSERT_EQ(avifDecoderSetIOMemory(decoder.get(), encoded.data, encoded.size),
              AVIF_RESULT_OK);
    const avifResult result = avifDecoderParse(decoder.get());
    if (!limit.accepted) {
      EXPECT_NE(result, AVIF_RESULT_OK) << decoder->diag.error;
    } else {
      ASSERT_EQ(result, AVIF_RESULT_OK) << decoder->diag.error;
      ASSERT_NE(decoder->image, nullptr);
      EXPECT_EQ(decoder->image->width, width);
      EXPECT_EQ(decoder->image->height, height);
    }
    // Parsing must not allocate decoded pixels, even when it rejects the
    // configured limit. Only accepted inputs proceed to the AV1 decoder.
    if (decoder->image != nullptr) {
      for (const uint8_t* plane : decoder->image->yuvPlanes) {
        EXPECT_EQ(plane, nullptr);
      }
      EXPECT_EQ(decoder->image->alphaPlane, nullptr);
    }
    if (limit.accepted) {
      ASSERT_EQ(avifDecoderNextImage(decoder.get()), AVIF_RESULT_OK)
          << decoder->diag.error;
      EXPECT_EQ(decoder->image->width, width);
      EXPECT_EQ(decoder->image->height, height);
      EXPECT_NE(decoder->image->yuvPlanes[0], nullptr);
    }
  }
}

INSTANTIATE_TEST_SUITE_P(
    OrdinaryAndGrid, DecoderLimitsTest,
    testing::Values(DecoderLimitsInput{"StillWide", 34, 12, 1, 1},
                    DecoderLimitsInput{"StillTall", 12, 34, 1, 1},
                    DecoderLimitsInput{"GridWide", 64, 64, 2, 1},
                    DecoderLimitsInput{"GridTall", 64, 64, 1, 2}),
    [](const testing::TestParamInfo<DecoderLimitsInput>& info) {
      return info.param.name;
    });

TEST(AvifDecodeTest, ColorGridAlphaNoGrid) {
  if (!testutil::Av1DecoderAvailable()) {
    GTEST_SKIP() << "AV1 Codec unavailable, skip test.";
  }
  // Test case from https://github.com/AOMediaCodec/libavif/issues/1203.
  const char* file_name = "color_grid_alpha_nogrid.avif";
  DecoderPtr decoder(avifDecoderCreate());
  ASSERT_NE(decoder, nullptr);
  ASSERT_EQ(avifDecoderSetIOFile(decoder.get(),
                                 (std::string(data_path) + file_name).c_str()),
            AVIF_RESULT_OK);
  ASSERT_EQ(avifDecoderParse(decoder.get()), AVIF_RESULT_OK);
  EXPECT_EQ(decoder->alphaPresent, AVIF_TRUE);
  EXPECT_EQ(decoder->imageSequenceTrackPresent, AVIF_FALSE);
  EXPECT_EQ(avifDecoderNextImage(decoder.get()), AVIF_RESULT_OK);
  EXPECT_NE(decoder->image->alphaPlane, nullptr);
  EXPECT_GT(decoder->image->alphaRowBytes, 0u);
}

TEST(AvifDecodeTest, ImageContentToDecodeNone) {
  if (!testutil::Av1DecoderAvailable()) {
    GTEST_SKIP() << "AV1 Codec unavailable, skip test.";
  }
  for (const std::string file_name :
       {"paris_icc_exif_xmp.avif", "draw_points_idat.avif",
        "sofa_grid1x5_420.avif", "color_grid_alpha_nogrid.avif",
        "seine_sdr_gainmap_srgb.avif", "draw_points_idat_progressive.avif",
        "weld_sato_12B_8B_q0.avif"}) {
    for (
        avifImageContentTypeFlag image_content_to_decode : {
            AVIF_IMAGE_CONTENT_NONE,
            AVIF_IMAGE_CONTENT_SAMPLE_TRANSFORMS  // Equivalent to NONE without
                                                  // AVIF_IMAGE_CONTENT_COLOR_AND_ALPHA.
        }) {
      SCOPED_TRACE(file_name);
      DecoderPtr decoder(avifDecoderCreate());
      ASSERT_NE(decoder, nullptr);
      // Do not decode anything.
      decoder->imageContentToDecode = image_content_to_decode;
      ASSERT_EQ(
          avifDecoderSetIOFile(decoder.get(),
                               (std::string(data_path) + file_name).c_str()),
          AVIF_RESULT_OK);
      ASSERT_EQ(avifDecoderParse(decoder.get()), AVIF_RESULT_OK)
          << decoder->diag.error;
      EXPECT_EQ(decoder->imageSequenceTrackPresent, AVIF_FALSE);
      EXPECT_EQ(avifDecoderNextImage(decoder.get()), AVIF_RESULT_NO_CONTENT);
    }
  }
}

TEST(AvifDecodeTest, ParseEmptyData) {
  DecoderPtr decoder(avifDecoderCreate());
  ASSERT_NE(decoder, nullptr);
  ASSERT_EQ(avifDecoderSetIOMemory(decoder.get(), nullptr, 0), AVIF_RESULT_OK);
  // No ftyp box was seen.
  ASSERT_EQ(avifDecoderParse(decoder.get()), AVIF_RESULT_INVALID_FTYP);
}

TEST(AvifDecodeTest, ImageContentToDecodeAlphaOnly) {
  DecoderPtr decoder(avifDecoderCreate());
  ASSERT_NE(decoder, nullptr);
  ASSERT_EQ(avifDecoderSetIOMemory(decoder.get(), nullptr, 0), AVIF_RESULT_OK);
  decoder->imageContentToDecode = AVIF_IMAGE_CONTENT_ALPHA;
  ASSERT_EQ(avifDecoderParse(decoder.get()), AVIF_RESULT_NOT_IMPLEMENTED);
}

TEST(AvifDecodeTest, Idat) {
  if (!testutil::Av1DecoderAvailable()) {
    GTEST_SKIP() << "AV1 Codec unavailable, skip test.";
  }

  const ImagePtr original = testutil::ReadImage(data_path, "draw_points.png");

  for (const std::string file_name :
       {"draw_points_idat.avif", "draw_points_idat_metasize0.avif",
        "draw_points_idat_progressive.avif",
        "draw_points_idat_progressive_metasize0.avif"}) {
    SCOPED_TRACE(file_name);
    DecoderPtr decoder(avifDecoderCreate());
    ASSERT_NE(decoder, nullptr);
    ASSERT_EQ(avifDecoderSetIOFile(
                  decoder.get(), (std::string(data_path) + file_name).c_str()),
              AVIF_RESULT_OK);
    ASSERT_EQ(avifDecoderParse(decoder.get()), AVIF_RESULT_OK);
    EXPECT_EQ(decoder->alphaPresent, AVIF_TRUE);
    EXPECT_EQ(decoder->imageSequenceTrackPresent, AVIF_FALSE);
    ASSERT_EQ(avifDecoderNextImage(decoder.get()), AVIF_RESULT_OK);
    EXPECT_NE(decoder->image->alphaPlane, nullptr);
    EXPECT_GT(decoder->image->alphaRowBytes, 0u);

    EXPECT_EQ(testutil::GetPsnr(*original, *decoder->image), 99.0);
  }
}

// From https://crbug.com/334281983.
TEST(AvifDecodeTest, PeekCompatibleFileTypeBad1) {
  constexpr uint8_t kData[] = {0x00, 0x00, 0x00, 0x1c, 0x66,
                               0x74, 0x79, 0x70, 0x84, 0xca};
  avifROData input = {kData, sizeof(kData)};
  EXPECT_FALSE(avifPeekCompatibleFileType(&input));
}

// From https://crbug.com/334682511.
TEST(AvifDecodeTest, PeekCompatibleFileTypeBad2) {
  constexpr uint8_t kData[] = {0x00, 0x00, 0x00, 0x20, 0x66, 0x74, 0x79,
                               0x70, 0x61, 0x73, 0x31, 0x6d, 0x00, 0x00,
                               0x08, 0x00, 0xd7, 0x89, 0xdb, 0x7f};
  avifROData input = {kData, sizeof(kData)};
  EXPECT_FALSE(avifPeekCompatibleFileType(&input));
}

struct NonPersistentIO {
  std::vector<uint8_t> data;
  avifROData ro_data;
};

// Every read call will explicitly invalidate the pointer returned by the
// previous read call to ensure non-persistent IO.
avifResult NonPersistentRead(struct avifIO* io, uint32_t flags, uint64_t offset,
                             size_t size, avifROData* out) {
  NonPersistentIO* io_data = reinterpret_cast<NonPersistentIO*>(io->data);
  if (flags != 0 || offset > io_data->ro_data.size) {
    return AVIF_RESULT_IO_ERROR;
  }
  uint64_t available_size = io_data->ro_data.size - offset;
  if (size > available_size) {
    size = static_cast<size_t>(available_size);
  }
  // Clear the existing vector.
  std::vector<uint8_t>().swap(io_data->data);
  // Copy new data into the vector.
  io_data->data.reserve(size);
  io_data->data.assign(io_data->ro_data.data + offset,
                       io_data->ro_data.data + offset + size);
  // Set the output.
  out->data = io_data->data.data();
  out->size = size;
  return AVIF_RESULT_OK;
}

TEST(AvifDecodeTest, NonPersistentIOBug506387278) {
  const testutil::AvifRwData avif =
      testutil::ReadFile(std::string(data_path) + "poc_b_506387278.avif");
  NonPersistentIO io_data;
  io_data.ro_data = {/*.data=*/avif.data, /*.size=*/avif.size};
  avifIO io = {/*.destroy=*/nullptr,
               /*.read=*/NonPersistentRead,
               /*.write=*/nullptr,
               /*.sizeHint=*/avif.size,
               /*.persistent=*/false,
               /*.data=*/&io_data};
  // |io| must outlive the decoder.
  DecoderPtr decoder(avifDecoderCreate());
  ASSERT_NE(decoder, nullptr);
  avifDecoderSetIO(decoder.get(), &io);
  ASSERT_EQ(avifDecoderParse(decoder.get()), AVIF_RESULT_OK);
  EXPECT_EQ(decoder->imageSequenceTrackPresent, AVIF_TRUE);
  EXPECT_EQ(decoder->imageCount, 2);
  if (testutil::Av1DecoderAvailable()) {
    avifResult result = avifDecoderNextImage(decoder.get());
    if (result != AVIF_RESULT_OK) {
      EXPECT_EQ(result, AVIF_RESULT_DECODE_COLOR_FAILED);
      return;
    }
    result = avifDecoderNextImage(decoder.get());
    if (result != AVIF_RESULT_OK) {
      EXPECT_EQ(result, AVIF_RESULT_DECODE_COLOR_FAILED);
      return;
    }
    EXPECT_EQ(avifDecoderNextImage(decoder.get()),
              AVIF_RESULT_NO_IMAGES_REMAINING);
  }
}

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
