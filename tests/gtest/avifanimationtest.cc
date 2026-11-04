// Copyright 2023 Google LLC
// SPDX-License-Identifier: BSD-2-Clause

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <utility>
#include <vector>

#include "avif/avif.h"
#include "aviftest_helpers.h"
#include "gtest/gtest.h"

namespace avif {
namespace {

// Used to pass the data folder path to the GoogleTest suites.
const char* data_path = nullptr;

TEST(AvifDecodeTest, AnimatedImage) {
  if (!testutil::Av1DecoderAvailable()) {
    GTEST_SKIP() << "AV1 Codec unavailable, skip test.";
  }
  // Both files should give exactly the same result: the audio is ignored
  // in 'colors-animated-8bpc-audio.avif' but shouldn't prevent the file from
  // decoding.
  for (const char* file_name :
       {"colors-animated-8bpc.avif", "colors-animated-8bpc-audio.avif"}) {
    SCOPED_TRACE(file_name);
    DecoderPtr decoder(avifDecoderCreate());
    ASSERT_NE(decoder, nullptr);
    ASSERT_EQ(avifDecoderSetIOFile(
                  decoder.get(), (std::string(data_path) + file_name).c_str()),
              AVIF_RESULT_OK);
    ASSERT_EQ(avifDecoderParse(decoder.get()), AVIF_RESULT_OK)
        << decoder->diag.error;
    ;
    EXPECT_EQ(decoder->alphaPresent, AVIF_FALSE);
    EXPECT_EQ(decoder->imageSequenceTrackPresent, AVIF_TRUE);
    EXPECT_EQ(decoder->imageCount, 5);
    EXPECT_EQ(decoder->repetitionCount, 0);
    for (int i = 0; i < 5; ++i) {
      EXPECT_EQ(avifDecoderIsKeyframe(decoder.get(), i), i == 0);
      EXPECT_EQ(avifDecoderNearestKeyframe(decoder.get(), i), 0);
    }
    for (int i = 0; i < 5; ++i) {
      EXPECT_EQ(avifDecoderNextImage(decoder.get()), AVIF_RESULT_OK);
    }
  }
}

TEST(AvifDecodeTest, AnimatedImageWithSourceSetToPrimaryItem) {
  if (!testutil::Av1DecoderAvailable()) {
    GTEST_SKIP() << "AV1 Codec unavailable, skip test.";
  }
  const char* file_name = "colors-animated-8bpc.avif";
  DecoderPtr decoder(avifDecoderCreate());
  ASSERT_NE(decoder, nullptr);
  ASSERT_EQ(avifDecoderSetIOFile(decoder.get(),
                                 (std::string(data_path) + file_name).c_str()),
            AVIF_RESULT_OK);
  ASSERT_EQ(
      avifDecoderSetSource(decoder.get(), AVIF_DECODER_SOURCE_PRIMARY_ITEM),
      AVIF_RESULT_OK);
  ASSERT_EQ(avifDecoderParse(decoder.get()), AVIF_RESULT_OK);
  EXPECT_EQ(decoder->alphaPresent, AVIF_FALSE);
  EXPECT_EQ(decoder->imageSequenceTrackPresent, AVIF_TRUE);
  // imageCount is expected to be 1 because we are using primary item as the
  // preferred source.
  EXPECT_EQ(decoder->imageCount, 1);
  EXPECT_EQ(decoder->repetitionCount, 0);
  // Get the first (and only) image.
  EXPECT_EQ(avifDecoderNextImage(decoder.get()), AVIF_RESULT_OK);
  // Subsequent calls should not return AVIF_RESULT_OK since there is only one
  // image in the preferred source.
  EXPECT_NE(avifDecoderNextImage(decoder.get()), AVIF_RESULT_OK);
}

TEST(AvifDecodeTest, AnimatedImageWithAlphaAndMetadata) {
  const char* file_name = "colors-animated-8bpc-alpha-exif-xmp.avif";
  DecoderPtr decoder(avifDecoderCreate());
  ASSERT_NE(decoder, nullptr);
  ASSERT_EQ(avifDecoderSetIOFile(decoder.get(),
                                 (std::string(data_path) + file_name).c_str()),
            AVIF_RESULT_OK);
  ASSERT_EQ(avifDecoderParse(decoder.get()), AVIF_RESULT_OK);
  EXPECT_EQ(decoder->alphaPresent, AVIF_TRUE);
  EXPECT_EQ(decoder->imageSequenceTrackPresent, AVIF_TRUE);
  EXPECT_EQ(decoder->imageCount, 5);
  EXPECT_EQ(decoder->repetitionCount, AVIF_REPETITION_COUNT_INFINITE);
  EXPECT_EQ(decoder->image->exif.size, 1126);
  EXPECT_EQ(decoder->image->xmp.size, 3898);
  if (!testutil::Av1DecoderAvailable()) {
    GTEST_SKIP() << "AV1 Codec unavailable, skip the rest of the test.";
  }
  for (int i = 0; i < 5; ++i) {
    EXPECT_EQ(avifDecoderNextImage(decoder.get()), AVIF_RESULT_OK);
    EXPECT_NE(decoder->image->alphaPlane, nullptr);
    EXPECT_GT(decoder->image->alphaRowBytes, 0);
  }
  EXPECT_EQ(avifDecoderNextImage(decoder.get()),
            AVIF_RESULT_NO_IMAGES_REMAINING);
}

//------------------------------------------------------------------------------
// The alpha track of colors-animated-8bpc-alpha-exif-xmp.avif holds a 20 byte
// 'tref' box at offset 1586, with a single 'auxl' box naming track 1, directly
// followed by a 44 byte 'edts' box. Section 8.3.3.2 of ISO/IEC 14496-12 gives
// TrackReferenceTypeBox an array of track_IDs, and nothing limits a 'tref' to
// one box of a given type. The cases below rewrite those 64 bytes in place and
// pad with a 'free' box, so the file size and every absolute offset in the file
// keep the values of the valid file.

constexpr const char* kAlphaTrackFileName =
    "colors-animated-8bpc-alpha-exif-xmp.avif";
constexpr size_t kTrefOffset = 1586;
constexpr size_t kTrefSize = 20;
constexpr size_t kTrefAndEdtsSize = 64;

void WriteBE32(uint32_t value, uint8_t* bytes) {
  bytes[0] = static_cast<uint8_t>(value >> 24);
  bytes[1] = static_cast<uint8_t>(value >> 16);
  bytes[2] = static_cast<uint8_t>(value >> 8);
  bytes[3] = static_cast<uint8_t>(value);
}

void AppendTrackReferenceTypeBox(const char* type,
                                 const std::vector<uint32_t>& track_ids,
                                 std::vector<uint8_t>* bytes) {
  const size_t size = 8 + 4 * track_ids.size();
  const size_t offset = bytes->size();
  bytes->resize(offset + size);
  WriteBE32(static_cast<uint32_t>(size), bytes->data() + offset);
  std::memcpy(bytes->data() + offset + 4, type, 4);
  for (size_t i = 0; i < track_ids.size(); ++i) {
    WriteBE32(track_ids[i], bytes->data() + offset + 8 + 4 * i);
  }
}

// Overwrites the 'tref' and 'edts' boxes of the alpha track with |boxes| and
// fills the rest of the 64 bytes with a 'free' box.
void ReplaceTrefAndEdts(const std::vector<uint8_t>& boxes,
                        testutil::AvifRwData* encoded) {
  ASSERT_GE(encoded->size, kTrefOffset + kTrefAndEdtsSize);
  uint8_t* p = encoded->data + kTrefOffset;
  // Sanity check: the fixture is the file these tests were authored against.
  ASSERT_EQ(std::memcmp(p + 4, "tref", 4), 0);
  ASSERT_EQ(std::memcmp(p + kTrefSize + 4, "edts", 4), 0);
  ASSERT_LE(boxes.size() + 8, kTrefAndEdtsSize);
  std::copy(boxes.begin(), boxes.end(), p);
  uint8_t* free_box = p + boxes.size();
  const size_t free_size = kTrefAndEdtsSize - boxes.size();
  WriteBE32(static_cast<uint32_t>(free_size), free_box);
  std::memcpy(free_box + 4, "free", 4);
  std::memset(free_box + 8, 0, free_size - 8);
}

void ExpectAlphaTrackIsFound(const testutil::AvifRwData& encoded) {
  DecoderPtr decoder(avifDecoderCreate());
  ASSERT_NE(decoder, nullptr);
  ASSERT_EQ(avifDecoderSetIOMemory(decoder.get(), encoded.data, encoded.size),
            AVIF_RESULT_OK);
  ASSERT_EQ(avifDecoderParse(decoder.get()), AVIF_RESULT_OK);
  EXPECT_EQ(decoder->alphaPresent, AVIF_TRUE);
  EXPECT_EQ(decoder->imageSequenceTrackPresent, AVIF_TRUE);
  EXPECT_EQ(decoder->imageCount, 5);
}

// Control for the two cases below. The alpha track keeps its single reference,
// only its 'edts' box is replaced by a 'free' box. Dropping it leaves that
// track's repetition count unknown, which the decoder takes from the color
// track anyway, so the alpha track still has to be found.
TEST(AvifDecodeTest, AlphaTrackWithoutEdts) {
  testutil::AvifRwData encoded =
      testutil::ReadFile(std::string(data_path) + kAlphaTrackFileName);
  ASSERT_NE(encoded.size, size_t{0});
  std::vector<uint8_t> tref;
  AppendTrackReferenceTypeBox("auxl", {1}, &tref);
  std::vector<uint8_t> boxes;
  AppendTrackReferenceTypeBox("tref", {}, &boxes);
  WriteBE32(static_cast<uint32_t>(8 + tref.size()), boxes.data());
  boxes.insert(boxes.end(), tref.begin(), tref.end());
  ASSERT_NO_FATAL_FAILURE(ReplaceTrefAndEdts(boxes, &encoded));
  ExpectAlphaTrackIsFound(encoded);
}

// The 'tref' box holds two 'auxl' boxes. Check that both are read.
TEST(AvifDecodeTest, AlphaTrackWithTwoAuxlBoxes) {
  testutil::AvifRwData encoded =
      testutil::ReadFile(std::string(data_path) + kAlphaTrackFileName);
  ASSERT_NE(encoded.size, size_t{0});
  std::vector<uint8_t> children;
  AppendTrackReferenceTypeBox("auxl", {1}, &children);
  AppendTrackReferenceTypeBox("auxl", {99}, &children);
  std::vector<uint8_t> boxes;
  AppendTrackReferenceTypeBox("tref", {}, &boxes);
  WriteBE32(static_cast<uint32_t>(8 + children.size()), boxes.data());
  boxes.insert(boxes.end(), children.begin(), children.end());
  ASSERT_NO_FATAL_FAILURE(ReplaceTrefAndEdts(boxes, &encoded));
  ExpectAlphaTrackIsFound(encoded);
}

// One 'auxl' box names two tracks. Check that both are read.
TEST(AvifDecodeTest, AlphaTrackWithTwoTrackIds) {
  testutil::AvifRwData encoded =
      testutil::ReadFile(std::string(data_path) + kAlphaTrackFileName);
  ASSERT_NE(encoded.size, size_t{0});
  std::vector<uint8_t> children;
  AppendTrackReferenceTypeBox("auxl", {99, 1}, &children);
  std::vector<uint8_t> boxes;
  AppendTrackReferenceTypeBox("tref", {}, &boxes);
  WriteBE32(static_cast<uint32_t>(8 + children.size()), boxes.data());
  boxes.insert(boxes.end(), children.begin(), children.end());
  ASSERT_NO_FATAL_FAILURE(ReplaceTrefAndEdts(boxes, &encoded));
  ExpectAlphaTrackIsFound(encoded);
}

//------------------------------------------------------------------------------

TEST(AvifDecodeTest, AnimatedImageWithAlphaAndMetadataIgnoreAlpha) {
  const char* file_name = "colors-animated-8bpc-alpha-exif-xmp.avif";
  DecoderPtr decoder(avifDecoderCreate());
  ASSERT_NE(decoder, nullptr);
  decoder->imageContentToDecode &= ~AVIF_IMAGE_CONTENT_ALPHA;
  ASSERT_EQ(avifDecoderSetIOFile(decoder.get(),
                                 (std::string(data_path) + file_name).c_str()),
            AVIF_RESULT_OK);
  ASSERT_EQ(avifDecoderParse(decoder.get()), AVIF_RESULT_OK);
  EXPECT_EQ(decoder->alphaPresent, AVIF_TRUE);
  EXPECT_EQ(decoder->imageSequenceTrackPresent, AVIF_TRUE);
  EXPECT_EQ(decoder->imageCount, 5);
  EXPECT_EQ(decoder->repetitionCount, AVIF_REPETITION_COUNT_INFINITE);
  EXPECT_EQ(decoder->image->exif.size, 1126);
  EXPECT_EQ(decoder->image->xmp.size, 3898);
  if (!testutil::Av1DecoderAvailable()) {
    GTEST_SKIP() << "AV1 Codec unavailable, skip the rest of the test.";
  }
  for (int i = 0; i < 5; ++i) {
    EXPECT_EQ(avifDecoderNextImage(decoder.get()), AVIF_RESULT_OK);
    EXPECT_EQ(decoder->image->alphaPlane, nullptr);
    EXPECT_EQ(decoder->image->alphaRowBytes, 0);
  }
  EXPECT_EQ(avifDecoderNextImage(decoder.get()),
            AVIF_RESULT_NO_IMAGES_REMAINING);
}

TEST(AvifDecodeTest, AnimatedImageWithAlphaAndMetadataIgnoreAll) {
  const char* file_name = "colors-animated-8bpc-alpha-exif-xmp.avif";
  DecoderPtr decoder(avifDecoderCreate());
  ASSERT_NE(decoder, nullptr);
  decoder->imageContentToDecode = AVIF_IMAGE_CONTENT_NONE;
  ASSERT_EQ(avifDecoderSetIOFile(decoder.get(),
                                 (std::string(data_path) + file_name).c_str()),
            AVIF_RESULT_OK);
  ASSERT_EQ(avifDecoderParse(decoder.get()), AVIF_RESULT_OK);
  EXPECT_EQ(decoder->alphaPresent, AVIF_TRUE);
  EXPECT_EQ(decoder->imageSequenceTrackPresent, AVIF_TRUE);
  EXPECT_EQ(decoder->imageCount, 5);
  EXPECT_EQ(decoder->repetitionCount, AVIF_REPETITION_COUNT_INFINITE);
  EXPECT_EQ(decoder->image->exif.size, 1126);
  EXPECT_EQ(decoder->image->xmp.size, 3898);
  if (!testutil::Av1DecoderAvailable()) {
    GTEST_SKIP() << "AV1 Codec unavailable, skip the rest of the test.";
  }
  EXPECT_EQ(avifDecoderNextImage(decoder.get()), AVIF_RESULT_NO_CONTENT);
}

TEST(AvifDecodeTest, AnimatedImageWithDepthAndMetadata) {
  // Depth is not supported and should be ignored.
  const char* file_name = "colors-animated-8bpc-depth-exif-xmp.avif";
  DecoderPtr decoder(avifDecoderCreate());
  ASSERT_NE(decoder, nullptr);
  ASSERT_EQ(avifDecoderSetIOFile(decoder.get(),
                                 (std::string(data_path) + file_name).c_str()),
            AVIF_RESULT_OK);
  ASSERT_EQ(avifDecoderParse(decoder.get()), AVIF_RESULT_OK);
  EXPECT_EQ(decoder->alphaPresent, AVIF_FALSE);
  EXPECT_EQ(decoder->imageSequenceTrackPresent, AVIF_TRUE);
  EXPECT_EQ(decoder->imageCount, 5);
  EXPECT_EQ(decoder->repetitionCount, AVIF_REPETITION_COUNT_INFINITE);
  EXPECT_EQ(decoder->image->exif.size, 1126);
  EXPECT_EQ(decoder->image->xmp.size, 3898);
}

TEST(AvifDecodeTest,
     AnimatedImageWithDepthAndMetadataWithSourceSetToPrimaryItem) {
  if (!testutil::Av1DecoderAvailable()) {
    GTEST_SKIP() << "AV1 Codec unavailable, skip test.";
  }
  // Depth is not supported and should be ignored.
  const char* file_name = "colors-animated-8bpc-depth-exif-xmp.avif";
  DecoderPtr decoder(avifDecoderCreate());
  ASSERT_NE(decoder, nullptr);
  ASSERT_EQ(avifDecoderSetIOFile(decoder.get(),
                                 (std::string(data_path) + file_name).c_str()),
            AVIF_RESULT_OK);
  ASSERT_EQ(
      avifDecoderSetSource(decoder.get(), AVIF_DECODER_SOURCE_PRIMARY_ITEM),
      AVIF_RESULT_OK);
  ASSERT_EQ(avifDecoderParse(decoder.get()), AVIF_RESULT_OK);
  EXPECT_EQ(decoder->alphaPresent, AVIF_FALSE);
  EXPECT_EQ(decoder->imageSequenceTrackPresent, AVIF_TRUE);
  // imageCount is expected to be 1 because we are using primary item as the
  // preferred source.
  EXPECT_EQ(decoder->imageCount, 1);
  EXPECT_EQ(decoder->repetitionCount, 0);
  EXPECT_EQ(decoder->image->exif.size, 1126);
  EXPECT_EQ(decoder->image->xmp.size, 3898);
  // Get the first (and only) image.
  EXPECT_EQ(avifDecoderNextImage(decoder.get()), AVIF_RESULT_OK);
  // Subsequent calls should not return AVIF_RESULT_OK since there is only one
  // image in the preferred source.
  EXPECT_NE(avifDecoderNextImage(decoder.get()), AVIF_RESULT_OK);
}

uint32_t ReadU32(const uint8_t* data) {
  return (static_cast<uint32_t>(data[0]) << 24) |
         (static_cast<uint32_t>(data[1]) << 16) |
         (static_cast<uint32_t>(data[2]) << 8) | static_cast<uint32_t>(data[3]);
}

// Collects the next_track_ID of the 'mvhd' box and the track_ID of each 'tkhd'
// box found in the 'moov' box of the given file.
void GetTrackIds(const uint8_t* data, size_t size, uint32_t* next_track_id,
                 std::vector<uint32_t>* track_ids) {
  *next_track_id = 0;
  track_ids->clear();
  const uint8_t* const end = data + size;
  std::vector<std::pair<const uint8_t*, const uint8_t*>> containers = {
      {data, end}};
  while (!containers.empty()) {
    const uint8_t* box = containers.back().first;
    const uint8_t* const container_end = containers.back().second;
    containers.pop_back();
    while (container_end - box >= 8) {
      const uint32_t box_size = ReadU32(box);
      ASSERT_GE(box_size, 8u);  // No support for 64-bit or 0 box sizes.
      ASSERT_LE(box_size, static_cast<size_t>(container_end - box));
      const uint8_t* const payload = box + 8;
      const size_t payload_size = box_size - 8;
      if (!memcmp(box + 4, "moov", 4) || !memcmp(box + 4, "trak", 4)) {
        containers.emplace_back(payload, payload + payload_size);
      } else if (!memcmp(box + 4, "mvhd", 4)) {
        ASSERT_EQ(payload[0], 1);  // version
        // Skip version, flags, creation_time, modification_time, timescale,
        // duration, rate, volume, reserved, matrix and pre_defined.
        constexpr size_t kOffset = 4 + 8 + 8 + 4 + 8 + 4 + 2 + 2 + 8 + 36 + 24;
        ASSERT_GE(payload_size, kOffset + 4);
        *next_track_id = ReadU32(payload + kOffset);
      } else if (!memcmp(box + 4, "tkhd", 4)) {
        ASSERT_EQ(payload[0], 1);  // version
        // Skip version, flags, creation_time and modification_time.
        constexpr size_t kOffset = 4 + 8 + 8;
        ASSERT_GE(payload_size, kOffset + 4);
        track_ids->push_back(ReadU32(payload + kOffset));
      }
      box += box_size;
    }
  }
}

// ISO/IEC 14496-12, Section 8.2.2.3:
//   The value of next_track_ID shall be larger than the largest track-ID in
//   use.
TEST(AvifEncodeTest, NextTrackIdIsLargerThanTrackIds) {
  if (!testutil::Av1EncoderAvailable()) {
    GTEST_SKIP() << "AV1 Codec unavailable, skip test.";
  }
  for (avifPlanesFlags planes : {AVIF_PLANES_YUV, AVIF_PLANES_ALL}) {
    SCOPED_TRACE(planes);
    ImagePtr image = testutil::CreateImage(64, 64, 8, AVIF_PIXEL_FORMAT_YUV420,
                                           planes, AVIF_RANGE_FULL);
    ASSERT_NE(image, nullptr);
    testutil::FillImageGradient(image.get());
    EncoderPtr encoder(avifEncoderCreate());
    ASSERT_NE(encoder, nullptr);
    encoder->speed = AVIF_SPEED_FASTEST;
    for (int i = 0; i < 2; ++i) {
      ASSERT_EQ(avifEncoderAddImage(encoder.get(), image.get(),
                                    /*durationInTimescales=*/1,
                                    AVIF_ADD_IMAGE_FLAG_NONE),
                AVIF_RESULT_OK);
    }
    testutil::AvifRwData encoded;
    ASSERT_EQ(avifEncoderFinish(encoder.get(), &encoded), AVIF_RESULT_OK);

    uint32_t next_track_id;
    std::vector<uint32_t> track_ids;
    ASSERT_NO_FATAL_FAILURE(
        GetTrackIds(encoded.data, encoded.size, &next_track_id, &track_ids));
    ASSERT_EQ(track_ids.size(), planes == AVIF_PLANES_ALL ? 2u : 1u);
    EXPECT_GT(next_track_id,
              *std::max_element(track_ids.begin(), track_ids.end()));
  }
}

TEST(AvifDecodeTest, AnimatedImageWithoutTracksShouldFail) {
  testutil::AvifRwData avif =
      testutil::ReadFile(std::string(data_path) + "colors-animated-8bpc.avif");
  // Edit the file to replace 'trak' box with a 'free' box. This way the file
  // will not contain any 'trak' boxes.
  const uint8_t* kTrak = reinterpret_cast<const uint8_t*>("trak");
  uint8_t* trak_position =
      std::search(avif.data, avif.data + avif.size, kTrak, kTrak + 4);
  ASSERT_NE(trak_position, avif.data + avif.size);
  trak_position[0] = static_cast<uint8_t>('f');
  trak_position[1] = static_cast<uint8_t>('r');
  trak_position[2] = static_cast<uint8_t>('e');
  trak_position[3] = static_cast<uint8_t>('e');

  for (auto source :
       {AVIF_DECODER_SOURCE_PRIMARY_ITEM, AVIF_DECODER_SOURCE_TRACKS}) {
    DecoderPtr decoder(avifDecoderCreate());
    ASSERT_NE(decoder, nullptr);
    ASSERT_EQ(avifDecoderSetIOMemory(decoder.get(), avif.data, avif.size),
              AVIF_RESULT_OK);
    ASSERT_EQ(avifDecoderSetSource(decoder.get(), source), AVIF_RESULT_OK);
    ASSERT_EQ(avifDecoderParse(decoder.get()), AVIF_RESULT_BMFF_PARSE_FAILED);
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
