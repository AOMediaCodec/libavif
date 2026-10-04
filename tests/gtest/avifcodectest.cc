// Copyright 2023 Google LLC
// SPDX-License-Identifier: BSD-2-Clause

#include <cassert>
#include <cstring>
#include <vector>

#include "avif/avif.h"
#include "aviftest_helpers.h"
#include "gtest/gtest.h"

namespace avif {
namespace {

class CodecTest : public testing::TestWithParam<
                      std::tuple</*encoding_codec=*/avifCodecChoice,
                                 /*decoding_codec=*/avifCodecChoice>> {};

TEST_P(CodecTest, EncodeDecode) {
  const avifCodecChoice encoding_codec = std::get<0>(GetParam());
  const avifCodecChoice decoding_codec = std::get<1>(GetParam());

  if (avifCodecName(encoding_codec, AVIF_CODEC_FLAG_CAN_ENCODE) == nullptr ||
      avifCodecName(decoding_codec, AVIF_CODEC_FLAG_CAN_DECODE) == nullptr) {
    GTEST_SKIP() << "Codec unavailable, skip test.";
  }

  // AVIF_CODEC_CHOICE_SVT requires dimensions to be at least 64 pixels.
  ImagePtr image =
      testutil::CreateImage(/*width=*/64, /*height=*/64, /*depth=*/8,
                            AVIF_PIXEL_FORMAT_YUV420, AVIF_PLANES_ALL);
  ASSERT_NE(image, nullptr);
  testutil::FillImageGradient(image.get());

  EncoderPtr encoder(avifEncoderCreate());
  ASSERT_NE(encoder, nullptr);
  encoder->codecChoice = encoding_codec;
  encoder->quality = encoder->qualityAlpha = 90;  // Small loss.
  testutil::AvifRwData encoded;
  ASSERT_EQ(avifEncoderWrite(encoder.get(), image.get(), &encoded),
            AVIF_RESULT_OK);

  DecoderPtr decoder(avifDecoderCreate());
  ASSERT_NE(decoder, nullptr);
  decoder->codecChoice = decoding_codec;
  ImagePtr decoded(avifImageCreateEmpty());
  ASSERT_NE(decoded, nullptr);
  ASSERT_EQ(avifDecoderReadMemory(decoder.get(), decoded.get(), encoded.data,
                                  encoded.size),
            AVIF_RESULT_OK);

  ASSERT_GT(testutil::GetPsnr(*image, *decoded), 32.0);
}

INSTANTIATE_TEST_SUITE_P(
    All, CodecTest,
    testing::Combine(/*encoding_codec=*/testing::Values(AVIF_CODEC_CHOICE_AOM,
                                                        AVIF_CODEC_CHOICE_RAV1E,
                                                        AVIF_CODEC_CHOICE_SVT),
                     /*decoding_codec=*/testing::Values(
                         AVIF_CODEC_CHOICE_AOM, AVIF_CODEC_CHOICE_DAV1D,
                         AVIF_CODEC_CHOICE_LIBGAV1)));

//------------------------------------------------------------------------------

// A minimal AVIF file whose only item payload is a sequence header OBU
// declaring a maximum frame size of 65536x65536, way above the default
// avifDecoder limits. The ispe property advertises a small image so that the
// file parses fine; the oversized dimensions are only visible in the AV1
// bitstream, where the codec is expected to reject them.

class BitWriter {
 public:
  void put(uint32_t value, int count) {
    for (int i = count - 1; i >= 0; --i) {
      bits_.push_back((value >> i) & 1);
    }
  }
  std::vector<uint8_t> bytes() const {
    std::vector<uint8_t> out((bits_.size() + 7) / 8, 0);
    for (size_t i = 0; i < bits_.size(); ++i) {
      out[i / 8] |= uint8_t(bits_[i] << (7 - (i % 8)));
    }
    return out;
  }

 private:
  std::vector<uint8_t> bits_;
};

void Put16(std::vector<uint8_t>* b, uint16_t v) {
  b->push_back(static_cast<uint8_t>(v >> 8));
  b->push_back(static_cast<uint8_t>(v));
}

void Put32(std::vector<uint8_t>* b, uint32_t v) {
  b->push_back(static_cast<uint8_t>(v >> 24));
  b->push_back(static_cast<uint8_t>(v >> 16));
  b->push_back(static_cast<uint8_t>(v >> 8));
  b->push_back(static_cast<uint8_t>(v));
}

void PutType(std::vector<uint8_t>* b, const char* type) {
  b->insert(b->end(), type, type + 4);
}

void PutBox(std::vector<uint8_t>* b, const char* type,
            const std::vector<uint8_t>& payload) {
  Put32(b, static_cast<uint32_t>(8 + payload.size()));
  PutType(b, type);
  b->insert(b->end(), payload.begin(), payload.end());
}

void PutFullBox(std::vector<uint8_t>* b, const char* type,
                const std::vector<uint8_t>& payload) {
  Put32(b, static_cast<uint32_t>(12 + payload.size()));
  PutType(b, type);
  Put32(b, 0);  // version and flags.
  b->insert(b->end(), payload.begin(), payload.end());
}

// Sequence header OBU: profile 0, 8-bit 4:2:0, max_frame 65536x65536.
std::vector<uint8_t> CreateOversizedSequenceHeaderOBU() {
  BitWriter w;
  w.put(0, 3);       // seq_profile
  w.put(0, 1);       // still_picture
  w.put(0, 1);       // reduced_still_picture_header
  w.put(0, 1);       // timing_info_present_flag
  w.put(0, 1);       // initial_display_delay_present_flag
  w.put(0, 5);       // operating_points_cnt_minus_1 (one operating point)
  w.put(0, 12);      // operating_point_idc[0]
  w.put(0, 5);       // seq_level_idx[0]
  w.put(15, 4);      // frame_width_bits_minus_1 (16 bits)
  w.put(15, 4);      // frame_height_bits_minus_1 (16 bits)
  w.put(65535, 16);  // max_frame_width_minus_1
  w.put(65535, 16);  // max_frame_height_minus_1
  w.put(0, 1);       // frame_id_numbers_present_flag
  w.put(0, 1);       // use_128x128_superblock
  w.put(0, 2);       // enable_filter_intra, enable_intra_edge_filter
  w.put(0, 4);       // enable_interintra_compound, enable_masked_compound,
                     // enable_warped_motion, enable_dual_filter
  w.put(0, 1);       // enable_order_hint
  w.put(0, 1);       // seq_choose_screen_content_tools
  w.put(0, 1);       // seq_force_screen_content_tools
  w.put(0, 3);       // enable_superres, enable_cdef, enable_restoration
  w.put(0, 1);       // high_bitdepth
  w.put(0, 1);       // mono_chrome
  w.put(0, 1);       // color_description_present_flag
  w.put(0, 1);       // color_range
  w.put(0, 2);       // chroma_sample_position
  w.put(0, 1);       // separate_uv_delta_q
  w.put(0, 1);       // film_grain_params_present

  const std::vector<uint8_t> payload = w.bytes();
  std::vector<uint8_t> obu;
  obu.push_back(
      0x0A);  // obu_type = OBU_SEQUENCE_HEADER, obu_has_size_field = 1.
  obu.push_back(static_cast<uint8_t>(payload.size()));  // leb128 size.
  obu.insert(obu.end(), payload.begin(), payload.end());
  return obu;
}

std::vector<uint8_t> CreateOversizedAvif() {
  const std::vector<uint8_t> obu = CreateOversizedSequenceHeaderOBU();

  std::vector<uint8_t> hdlr;
  Put32(&hdlr, 0);  // pre_defined
  PutType(&hdlr, "pict");
  for (int i = 0; i < 3; ++i) Put32(&hdlr, 0);  // reserved
  hdlr.push_back(0);                            // name

  std::vector<uint8_t> pitm;
  Put16(&pitm, 1);  // item_ID

  std::vector<uint8_t> infe;
  Put16(&infe, 1);  // item_ID
  Put16(&infe, 0);  // item_protection_index
  PutType(&infe, "av01");
  infe.push_back(0);  // item_name

  std::vector<uint8_t> infeBox;
  Put32(&infeBox, static_cast<uint32_t>(12 + infe.size()));
  PutType(&infeBox, "infe");
  Put32(&infeBox, 2u << 24);  // Version 2, no flags.
  infeBox.insert(infeBox.end(), infe.begin(), infe.end());

  std::vector<uint8_t> iinf;
  Put16(&iinf, 1);  // entry_count
  iinf.insert(iinf.end(), infeBox.begin(), infeBox.end());

  std::vector<uint8_t> ispe;
  Put32(&ispe, 64);  // width
  Put32(&ispe, 64);  // height

  const std::vector<uint8_t> av1C = {0x81, 0x00, 0x0C,
                                     0x00};  // 8-bit 4:2:0, level 0.

  const std::vector<uint8_t> pixi = {0x03, 0x08, 0x08,
                                     0x08};  // Three 8-bit channels.

  std::vector<uint8_t> ipco;
  PutFullBox(&ipco, "ispe", ispe);
  PutBox(&ipco, "av1C", av1C);
  PutFullBox(&ipco, "pixi", pixi);

  std::vector<uint8_t> ipma;
  Put32(&ipma, 1);    // entry_count
  Put16(&ipma, 1);    // item_ID
  ipma.push_back(3);  // association_count
  ipma.push_back(1);  // ispe
  ipma.push_back(2);  // av1C
  ipma.push_back(3);  // pixi

  std::vector<uint8_t> iprp;
  PutBox(&iprp, "ipco", ipco);
  PutFullBox(&iprp, "ipma", ipma);

  // Box sizes are independent of the iloc extent_offset value, so the position
  // of the mdat payload can be computed before iloc is written.
  const size_t ilocSize = 1 + 1 + 2 /* item_count */ + 2 /* item_ID */ +
                          2 /* data_reference_index */ + 2 /* extent_count */ +
                          4 /* extent_offset */ + 4 /* extent_length */;
  const size_t metaPayloadSize = (12 + hdlr.size()) + (12 + pitm.size()) +
                                 (12 + iinf.size()) + (8 + iprp.size()) +
                                 (12 + ilocSize);
  const size_t ftypBoxSize =
      8 + 16;  // Major brand, minor version, two compatible brands.
  const uint32_t extentOffset =
      static_cast<uint32_t>(ftypBoxSize + 12 /* meta box header */ +
                            metaPayloadSize + 8 /* mdat box header */);

  std::vector<uint8_t> iloc;
  iloc.push_back(0x44);  // offset_size = 4, length_size = 4.
  iloc.push_back(0x00);  // base_offset_size = 0, reserved.
  Put16(&iloc, 1);       // item_count
  Put16(&iloc, 1);       // item_ID
  Put16(&iloc, 0);       // data_reference_index
  Put16(&iloc, 1);       // extent_count
  Put32(&iloc, extentOffset);
  Put32(&iloc, static_cast<uint32_t>(obu.size()));  // extent_length
  assert(iloc.size() == ilocSize);

  std::vector<uint8_t> meta;
  PutFullBox(&meta, "hdlr", hdlr);
  PutFullBox(&meta, "pitm", pitm);
  PutFullBox(&meta, "iinf", iinf);
  PutBox(&meta, "iprp", iprp);
  PutFullBox(&meta, "iloc", iloc);
  assert(meta.size() == metaPayloadSize);

  std::vector<uint8_t> ftyp;
  PutType(&ftyp, "avif");
  Put32(&ftyp, 0);
  PutType(&ftyp, "avif");
  PutType(&ftyp, "mif1");

  std::vector<uint8_t> file;
  PutBox(&file, "ftyp", ftyp);
  PutFullBox(&file, "meta", meta);
  Put32(&file, static_cast<uint32_t>(8 + obu.size()));
  PutType(&file, "mdat");
  file.insert(file.end(), obu.begin(), obu.end());
  assert(file[extentOffset] == 0x0A);  // The OBU starts at the declared extent.
  return file;
}

TEST(CodecTest, OversizedSequenceHeaderRejected) {
  const std::vector<uint8_t> file = CreateOversizedAvif();
  ASSERT_GT(file.size(), size_t{0});

  for (const avifCodecChoice choice :
       {AVIF_CODEC_CHOICE_AOM, AVIF_CODEC_CHOICE_DAV1D,
        AVIF_CODEC_CHOICE_LIBGAV1}) {
    if (avifCodecName(choice, AVIF_CODEC_FLAG_CAN_DECODE) == nullptr) {
      continue;
    }
    DecoderPtr decoder(avifDecoderCreate());
    ASSERT_NE(decoder, nullptr);
    decoder->codecChoice = choice;
    ASSERT_EQ(avifDecoderSetIOMemory(decoder.get(), file.data(), file.size()),
              AVIF_RESULT_OK);
    // The item ispe is small, so parsing succeeds regardless of the codec.
    const avifResult parseResult = avifDecoderParse(decoder.get());
    ASSERT_EQ(parseResult, AVIF_RESULT_OK)
        << "choice=" << choice << " diag: " << decoder->diag.error;
    // The sequence header declares a 65536x65536 maximum frame size, which no
    // codec may decode.
    EXPECT_NE(avifDecoderNextImage(decoder.get()), AVIF_RESULT_OK);
    if (choice == AVIF_CODEC_CHOICE_LIBGAV1) {
      EXPECT_NE(strstr(decoder->diag.error, "dimensions too large"), nullptr)
          << decoder->diag.error;
    }
  }
}

}  // namespace
}  // namespace avif
