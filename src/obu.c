/*
 * Copyright © 2018, VideoLAN and dav1d authors
 * Copyright © 2018, Two Orioles, LLC
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice, this
 *    list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR
 * ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

// OBU parsing and bit magic all originally from dav1d's obu.c and getbits.c,
// but heavily modified/reduced down to simply find the Sequence Header OBU
// and pull a few interesting pieces from it.
//
// Any other code in here is under this license:
//
// Copyright 2020 Joe Drago. All rights reserved.
// SPDX-License-Identifier: BSD-2-Clause

#include "avif/internal.h"

#include <stdint.h>
#include <string.h>

#if defined(AVIF_CODEC_AVM)
// TODO: b/398931194 - Remove specific codec includes once AV2 is released.
#include "avm/avm_codec.h"
#include "config/avm_config.h"
#endif

// ---------------------------------------------------------------------------
// avifBits - Originally dav1d's GetBits struct (see dav1d's getbits.c)

typedef struct avifBits
{
    int error, eof;
    uint64_t state;
    uint32_t bitsLeft;
    const uint8_t *ptr, *start, *end;
} avifBits;

static inline uint32_t avifBitsReadPos(const avifBits * bits)
{
    return (uint32_t)(bits->ptr - bits->start) * 8 - bits->bitsLeft;
}

static void avifBitsInit(avifBits * const bits, const uint8_t * const data, const size_t size)
{
    bits->ptr = bits->start = data;
    bits->end = &bits->start[size];
    bits->bitsLeft = 0;
    bits->state = 0;
    bits->error = 0;
    bits->eof = (size == 0);
}

static void avifBitsRefill(avifBits * const bits, const uint32_t n)
{
    uint64_t state = 0;
    do {
        state <<= 8;
        bits->bitsLeft += 8;
        if (!bits->eof)
            state |= *bits->ptr++;
        if (bits->ptr >= bits->end) {
            bits->error = bits->eof;
            bits->eof = 1;
        }
    } while (n > bits->bitsLeft);
    bits->state |= state << (64 - bits->bitsLeft);
}

static uint32_t avifBitsRead(avifBits * const bits, const uint32_t n)
{
    if (n > bits->bitsLeft)
        avifBitsRefill(bits, n);

    const uint64_t state = bits->state;
    bits->bitsLeft -= n;
    bits->state <<= n;

    return (uint32_t)(state >> (64 - n));
}

static uint32_t avifBitsReadUleb128(avifBits * bits)
{
    uint64_t val = 0;
    uint32_t more;
    uint32_t i = 0;

    do {
        const uint32_t v = avifBitsRead(bits, 8);
        more = v & 0x80;
        val |= ((uint64_t)(v & 0x7F)) << i;
        i += 7;
    } while (more && i < 56);

    if (val > UINT32_MAX || more) {
        bits->error = 1;
        return 0;
    }

    return (uint32_t)val;
}

static uint32_t avifBitsReadVLC(avifBits * const bits)
{
    int numBits = 0;
    while (!avifBitsRead(bits, 1))
        if (++numBits == 32)
            return 0xFFFFFFFFU;
    return numBits ? ((1U << numBits) - 1) + avifBitsRead(bits, numBits) : 0;
}

#if defined(AVIF_CODEC_AVM)
// Rice-Golomb coding with parameter n.
static uint32_t avifBitsReadRG(avifBits * const bits, const uint32_t n)
{
    for (uint32_t q = 0; q < 32; q++) {
        const uint32_t rgBit = avifBitsRead(bits, 1);
        if (rgBit == 0) {
            const uint32_t remainder = avifBitsRead(bits, n);
            return (q << n) + remainder;
        }
    }
    return 0xFFFFFFFFU;
}
#endif // defined(AVIF_CODEC_AVM)

// ---------------------------------------------------------------------------
// Variables in here use snake_case to self-document from the AV1 spec and the draft AV2 spec:
//
// https://aomediacodec.github.io/av1-spec/av1-spec.pdf
//
// Originally dav1d's parse_seq_hdr() function (heavily modified and split)

static avifBool parseSequenceHeaderProfile(avifBits * bits, avifSequenceHeader * header)
{
    uint32_t seq_profile = avifBitsRead(bits, 3);
    if (seq_profile > 2) {
        return AVIF_FALSE;
    }
    header->av1C.seqProfile = (uint8_t)seq_profile;
    return !bits->error;
}

static avifBool parseSequenceHeaderLevelIdxAndTier(avifBits * bits, avifSequenceHeader * header)
{
    uint32_t still_picture = avifBitsRead(bits, 1);
    header->reduced_still_picture_header = (uint8_t)avifBitsRead(bits, 1);
    if (header->reduced_still_picture_header && !still_picture) {
        return AVIF_FALSE;
    }

    if (header->reduced_still_picture_header) {
        header->av1C.seqLevelIdx0 = (uint8_t)avifBitsRead(bits, 5);
        header->av1C.seqTier0 = 0;
    } else {
        uint32_t timing_info_present_flag = avifBitsRead(bits, 1);
        uint32_t decoder_model_info_present_flag = 0;
        uint32_t buffer_delay_length = 0;
        if (timing_info_present_flag) { // timing_info()
            avifBitsRead(bits, 32);     // num_units_in_display_tick
            avifBitsRead(bits, 32);     // time_scale
            uint32_t equal_picture_interval = avifBitsRead(bits, 1);
            if (equal_picture_interval) {
                uint32_t num_ticks_per_picture_minus_1 = avifBitsReadVLC(bits);
                if (num_ticks_per_picture_minus_1 == 0xFFFFFFFFU)
                    return AVIF_FALSE;
            }

            decoder_model_info_present_flag = avifBitsRead(bits, 1);
            if (decoder_model_info_present_flag) { // decoder_model_info()
                buffer_delay_length = avifBitsRead(bits, 5) + 1;
                avifBitsRead(bits, 32); // num_units_in_decoding_tick
                avifBitsRead(bits, 10); // buffer_removal_time_length_minus_1, frame_presentation_time_length_minus_1
            }
        }

        uint32_t initial_display_delay_present_flag = avifBitsRead(bits, 1);
        uint32_t operating_points_cnt = avifBitsRead(bits, 5) + 1;
        for (uint32_t i = 0; i < operating_points_cnt; i++) {
            avifBitsRead(bits, 12); // operating_point_idc
            uint32_t seq_level_idx = avifBitsRead(bits, 5);
            if (i == 0) {
                header->av1C.seqLevelIdx0 = (uint8_t)seq_level_idx;
                header->av1C.seqTier0 = 0;
            }
            if (seq_level_idx > 7) {
                uint32_t seq_tier = avifBitsRead(bits, 1);
                if (i == 0) {
                    header->av1C.seqTier0 = (uint8_t)seq_tier;
                }
            }
            if (decoder_model_info_present_flag) {
                uint32_t decoder_model_present_for_this_op = avifBitsRead(bits, 1);
                if (decoder_model_present_for_this_op) {     // operating_parameters_info()
                    avifBitsRead(bits, buffer_delay_length); // decoder_buffer_delay
                    avifBitsRead(bits, buffer_delay_length); // encoder_buffer_delay
                    avifBitsRead(bits, 1);                   // low_delay_mode_flag
                }
            }
            if (initial_display_delay_present_flag) {
                uint32_t initial_display_delay_present_for_this_op = avifBitsRead(bits, 1);
                if (initial_display_delay_present_for_this_op) {
                    avifBitsRead(bits, 4); // initial_display_delay_minus_1
                }
            }
        }
    }
    return !bits->error;
}

static avifBool parseSequenceHeaderFrameMaxDimensions(avifBits * bits, avifSequenceHeader * header)
{
    uint32_t frame_width_bits = avifBitsRead(bits, 4) + 1;
    uint32_t frame_height_bits = avifBitsRead(bits, 4) + 1;
    header->maxWidth = avifBitsRead(bits, frame_width_bits) + 1;   // max_frame_width
    header->maxHeight = avifBitsRead(bits, frame_height_bits) + 1; // max_frame_height
    uint32_t frame_id_numbers_present_flag = 0;
    if (!header->reduced_still_picture_header) {
        frame_id_numbers_present_flag = avifBitsRead(bits, 1);
    }
    if (frame_id_numbers_present_flag) {
        avifBitsRead(bits, 7); // delta_frame_id_length_minus_2, additional_frame_id_length_minus_1
    }
    return !bits->error;
}

static avifBool parseSequenceHeaderEnabledFeatures(avifBits * bits, avifSequenceHeader * header)
{
    avifBitsRead(bits, 2); // enable_filter_intra, enable_intra_edge_filter

    if (!header->reduced_still_picture_header) {
        avifBitsRead(bits, 4); // enable_interintra_compound, enable_masked_compound, enable_warped_motion, enable_dual_filter
        uint32_t enable_order_hint = avifBitsRead(bits, 1);
        if (enable_order_hint) {
            avifBitsRead(bits, 2); // enable_jnt_comp, enable_ref_frame_mvs
        }

        uint32_t seq_force_screen_content_tools = 0;
        uint32_t seq_choose_screen_content_tools = avifBitsRead(bits, 1);
        if (seq_choose_screen_content_tools) {
            seq_force_screen_content_tools = 2;
        } else {
            seq_force_screen_content_tools = avifBitsRead(bits, 1);
        }
        if (seq_force_screen_content_tools > 0) {
            uint32_t seq_choose_integer_mv = avifBitsRead(bits, 1);
            if (!seq_choose_integer_mv) {
                avifBitsRead(bits, 1); // seq_force_integer_mv
            }
        }
        if (enable_order_hint) {
            avifBitsRead(bits, 3); // order_hint_bits_minus_1
        }
    }

    return !bits->error;
}

// Note: Does not parse separate_uv_delta_q.
static avifBool parseAV1SequenceHeaderColorConfig(avifBits * bits, avifSequenceHeader * header)
{
    header->bitDepth = 8;
    header->chromaSamplePosition = AVIF_CHROMA_SAMPLE_POSITION_UNKNOWN;
    header->av1C.chromaSamplePosition = (uint8_t)header->chromaSamplePosition;
    uint32_t high_bitdepth = avifBitsRead(bits, 1);
    header->av1C.highBitdepth = (uint8_t)high_bitdepth;
    if ((header->av1C.seqProfile == 2) && high_bitdepth) {
        uint32_t twelve_bit = avifBitsRead(bits, 1);
        header->bitDepth = twelve_bit ? 12 : 10;
        header->av1C.twelveBit = (uint8_t)twelve_bit;
    } else /* if (seq_profile <= 2) */ {
        header->bitDepth = high_bitdepth ? 10 : 8;
        header->av1C.twelveBit = 0;
    }
    uint32_t mono_chrome = 0;
    if (header->av1C.seqProfile != 1) {
        mono_chrome = avifBitsRead(bits, 1);
    }
    header->av1C.monochrome = (uint8_t)mono_chrome;
    uint32_t color_description_present_flag = avifBitsRead(bits, 1);
    if (color_description_present_flag) {
        header->colorPrimaries = (avifColorPrimaries)avifBitsRead(bits, 8);                   // color_primaries
        header->transferCharacteristics = (avifTransferCharacteristics)avifBitsRead(bits, 8); // transfer_characteristics
        header->matrixCoefficients = (avifMatrixCoefficients)avifBitsRead(bits, 8);           // matrix_coefficients
    } else {
        header->colorPrimaries = AVIF_COLOR_PRIMARIES_UNSPECIFIED;
        header->transferCharacteristics = AVIF_TRANSFER_CHARACTERISTICS_UNSPECIFIED;
        header->matrixCoefficients = AVIF_MATRIX_COEFFICIENTS_UNSPECIFIED;
    }
    if (mono_chrome) {
        header->range = avifBitsRead(bits, 1) ? AVIF_RANGE_FULL : AVIF_RANGE_LIMITED; // color_range
        header->av1C.chromaSubsamplingX = 1;
        header->av1C.chromaSubsamplingY = 1;
        header->yuvFormat = AVIF_PIXEL_FORMAT_YUV400;
    } else if (header->colorPrimaries == AVIF_COLOR_PRIMARIES_BT709 &&
               header->transferCharacteristics == AVIF_TRANSFER_CHARACTERISTICS_SRGB &&
               header->matrixCoefficients == AVIF_MATRIX_COEFFICIENTS_IDENTITY) {
        header->range = AVIF_RANGE_FULL;
        header->av1C.chromaSubsamplingX = 0;
        header->av1C.chromaSubsamplingY = 0;
        header->yuvFormat = AVIF_PIXEL_FORMAT_YUV444;
    } else {
        uint32_t subsampling_x = 0;
        uint32_t subsampling_y = 0;
        header->range = avifBitsRead(bits, 1) ? AVIF_RANGE_FULL : AVIF_RANGE_LIMITED; // color_range
        switch (header->av1C.seqProfile) {
            case 0:
                subsampling_x = 1;
                subsampling_y = 1;
                header->yuvFormat = AVIF_PIXEL_FORMAT_YUV420;
                break;
            case 1:
                subsampling_x = 0;
                subsampling_y = 0;
                header->yuvFormat = AVIF_PIXEL_FORMAT_YUV444;
                break;
            case 2:
                if (header->bitDepth == 12) {
                    subsampling_x = avifBitsRead(bits, 1);
                    if (subsampling_x) {
                        subsampling_y = avifBitsRead(bits, 1);
                    }
                } else {
                    subsampling_x = 1;
                    subsampling_y = 0;
                }
                if (subsampling_x) {
                    header->yuvFormat = subsampling_y ? AVIF_PIXEL_FORMAT_YUV420 : AVIF_PIXEL_FORMAT_YUV422;
                } else {
                    header->yuvFormat = AVIF_PIXEL_FORMAT_YUV444;
                }
                break;
            default:
                return AVIF_FALSE;
        }

        if (subsampling_x && subsampling_y) {
            header->chromaSamplePosition = (avifChromaSamplePosition)avifBitsRead(bits, 2); // chroma_sample_position
            header->av1C.chromaSamplePosition = (uint8_t)header->chromaSamplePosition;
        }
        header->av1C.chromaSubsamplingX = (uint8_t)subsampling_x;
        header->av1C.chromaSubsamplingY = (uint8_t)subsampling_y;
    }

    return !bits->error;
}

#if defined(AVIF_CODEC_AVM)
enum
{
    AV2_CHROMA_FORMAT_420 = 0,
    AV2_CHROMA_FORMAT_400 = 1,
    AV2_CHROMA_FORMAT_444 = 2,
    AV2_CHROMA_FORMAT_422 = 3,
};

static avifChromaSamplePosition av2ChromaSamplePositionToAv1ChromaSamplePosition(uint32_t av2ChromaSamplePosition)
{
    if (av2ChromaSamplePosition == 0) {
        // AVM_CSP_LEFT: Horizontal offset 0, vertical offset 0.5
        return AVIF_CHROMA_SAMPLE_POSITION_VERTICAL;
    } else if (av2ChromaSamplePosition == 1) {
        // AVM_CSP_CENTER: Horizontal offset 0.5, vertical offset 0.5
        return AVIF_CHROMA_SAMPLE_POSITION_UNKNOWN;
    } else if (av2ChromaSamplePosition == 2) {
        // AVM_CSP_TOPLEFT: Horizontal offset 0, vertical offset 0
        return AVIF_CHROMA_SAMPLE_POSITION_COLOCATED;
    } else {
        return AVIF_CHROMA_SAMPLE_POSITION_UNKNOWN;
    }
}

static avifBool parseAV2ChromaFormatBitdepth(avifBits * bits, avifSequenceHeader * header)
{
    const uint32_t chromaFormatIdc = avifBitsReadVLC(bits);

    const uint32_t bitdepthIdx = avifBitsReadVLC(bits);
    if (bitdepthIdx == 0) {
        header->bitDepth = 10;
    } else if (bitdepthIdx == 1) {
        header->bitDepth = 8;
    } else if (bitdepthIdx == 2) {
        header->bitDepth = 12;
    } else {
        return AVIF_FALSE;
    }
    header->av1C.highBitdepth = header->bitDepth > 8;
    header->av1C.twelveBit = header->bitDepth == 12;
    header->av1C.monochrome = chromaFormatIdc == AV2_CHROMA_FORMAT_400;

    if (header->av1C.monochrome) {
        header->av1C.chromaSubsamplingX = 1;
        header->av1C.chromaSubsamplingY = 1;
        header->yuvFormat = AVIF_PIXEL_FORMAT_YUV400;
    } else if (chromaFormatIdc == AV2_CHROMA_FORMAT_420) {
        header->av1C.chromaSubsamplingX = 1;
        header->av1C.chromaSubsamplingY = 1;
        header->yuvFormat = AVIF_PIXEL_FORMAT_YUV420;
    } else if (chromaFormatIdc == AV2_CHROMA_FORMAT_444) {
        header->av1C.chromaSubsamplingX = 0;
        header->av1C.chromaSubsamplingY = 0;
        header->yuvFormat = AVIF_PIXEL_FORMAT_YUV444;
    } else if (chromaFormatIdc == AV2_CHROMA_FORMAT_422) {
        header->av1C.chromaSubsamplingX = 1;
        header->av1C.chromaSubsamplingY = 0;
        header->yuvFormat = AVIF_PIXEL_FORMAT_YUV422;
    } else {
        return AVIF_FALSE;
    }

    return !bits->error;
}
#endif // defined(AVIF_CODEC_AVM)

static avifBool parseAV1SequenceHeader(avifBits * bits, avifSequenceHeader * header)
{
    AVIF_CHECK(parseSequenceHeaderProfile(bits, header));
    AVIF_CHECK(parseSequenceHeaderLevelIdxAndTier(bits, header));

    AVIF_CHECK(parseSequenceHeaderFrameMaxDimensions(bits, header));
    avifBitsRead(bits, 1); // use_128x128_superblock
    AVIF_CHECK(parseSequenceHeaderEnabledFeatures(bits, header));

    avifBitsRead(bits, 3); // enable_superres, enable_cdef, enable_restoration

    AVIF_CHECK(parseAV1SequenceHeaderColorConfig(bits, header));
    if (!header->av1C.monochrome) {
        avifBitsRead(bits, 1); // separate_uv_delta_q
    }

    avifBitsRead(bits, 1); // film_grain_params_present
    return !bits->error;
}

#if defined(AVIF_CODEC_AVM)
// See read_sequence_header_obu() in av2/decoder/obu.c.
static avifBool parseAV2SequenceHeader(avifBits * bits, avifSequenceHeader * header)
{
    uint32_t seqHeaderId = avifBitsReadVLC(bits);
    if (seqHeaderId >= 16) {
        return AVIF_FALSE;
    }

    AVIF_CHECK(parseSequenceHeaderProfile(bits, header));
    header->reduced_still_picture_header = (uint8_t)avifBitsRead(bits, 1); // single_picture_header_flag
    if (!header->reduced_still_picture_header) {
        avifBitsRead(bits, 3); // seq_lcr_id
        avifBitsRead(bits, 1); // still_picture
        return AVIF_FALSE;
    }
    header->av1C.seqLevelIdx0 = (uint8_t)avifBitsRead(bits, 5);
    if (header->av1C.seqLevelIdx0 > 7 && !header->reduced_still_picture_header) {
        avifBitsRead(bits, 1); // single_tier_0
    }
    header->av1C.seqTier0 = 0;

    uint32_t frame_width_bits = avifBitsRead(bits, 4) + 1;
    uint32_t frame_height_bits = avifBitsRead(bits, 4) + 1;
    header->maxWidth = avifBitsRead(bits, frame_width_bits) + 1;   // max_frame_width
    header->maxHeight = avifBitsRead(bits, frame_height_bits) + 1; // max_frame_height

    if (avifBitsRead(bits, 1)) { // conf_window_flag
        avifBitsReadVLC(bits);   // conf_win_left_offset
        avifBitsReadVLC(bits);   // conf_win_right_offset
        avifBitsReadVLC(bits);   // conf_win_top_offset
        avifBitsReadVLC(bits);   // conf_win_bottom_offset
    }

    AVIF_CHECK(parseAV2ChromaFormatBitdepth(bits, header));

    header->colorPrimaries = AVIF_COLOR_PRIMARIES_UNSPECIFIED;
    header->transferCharacteristics = AVIF_TRANSFER_CHARACTERISTICS_UNSPECIFIED;
    header->matrixCoefficients = AVIF_MATRIX_COEFFICIENTS_UNSPECIFIED;
    header->range = AVIF_RANGE_LIMITED;

    header->chromaSamplePosition = AVIF_CHROMA_SAMPLE_POSITION_UNKNOWN;
    header->av1C.chromaSamplePosition = (uint8_t)header->chromaSamplePosition;

    // Other ignored fields.
    return !bits->error;
}

// See av2_read_content_interpretation_obu() in av2/decoder/obu_ci.c.
static avifBool parseAV2ContentInterpretation(avifBits * bits, avifSequenceHeader * header)
{
    avifBitsRead(bits, 2);                                              // ci_scan_type_idc
    const uint32_t colorDescriptionPresent = avifBitsRead(bits, 1);     // ci_color_description_present_flag
    const uint32_t chromaSamplePositionPresent = avifBitsRead(bits, 1); // ci_chroma_sample_position_present_flag
    avifBitsRead(bits, 1);                                              // ci_aspect_ratio_info_present_flag
    avifBitsRead(bits, 1);                                              // ci_timing_info_present_flag
    avifBitsRead(bits, 1);                                              // ci_extension_present_flag
    avifBitsRead(bits, 1);                                              // reserved_bit

    if (colorDescriptionPresent) {
        // Override the default CICP values.
        const uint32_t colorDescriptionIdc = avifBitsReadRG(bits, 2); // color_description_idc
        if (colorDescriptionIdc == 0xFFFFFFFFU) {
            return AVIF_FALSE;
        }
        if (colorDescriptionIdc == 0) {
            // Explicitly signaled
            header->colorPrimaries = (avifColorPrimaries)avifBitsRead(bits, 8);                   // color_primaries
            header->transferCharacteristics = (avifTransferCharacteristics)avifBitsRead(bits, 8); // transfer_characteristics
            header->matrixCoefficients = (avifMatrixCoefficients)avifBitsRead(bits, 8);           // matrix_coefficients
        } else if (colorDescriptionIdc == 1) {
            // BT.709 SDR
            header->colorPrimaries = AVIF_COLOR_PRIMARIES_BT709;                   // 1
            header->transferCharacteristics = AVIF_TRANSFER_CHARACTERISTICS_BT709; // 1
            header->matrixCoefficients = AVIF_MATRIX_COEFFICIENTS_BT470BG;         // 5
        } else if (colorDescriptionIdc == 2) {
            // BT.2100 PQ
            header->colorPrimaries = AVIF_COLOR_PRIMARIES_BT2100;               // 9
            header->transferCharacteristics = AVIF_TRANSFER_CHARACTERISTICS_PQ; // 16
            header->matrixCoefficients = AVIF_MATRIX_COEFFICIENTS_BT2020_NCL;   // 9
        } else if (colorDescriptionIdc == 3) {
            // BT.2100 HLG
            header->colorPrimaries = AVIF_COLOR_PRIMARIES_BT2100;                         // 9
            header->transferCharacteristics = AVIF_TRANSFER_CHARACTERISTICS_BT2020_10BIT; // 14
            header->matrixCoefficients = AVIF_MATRIX_COEFFICIENTS_BT2020_NCL;             // 9
        } else if (colorDescriptionIdc == 4) {
            // sRGB
            header->colorPrimaries = AVIF_COLOR_PRIMARIES_BT709;                  // 1
            header->transferCharacteristics = AVIF_TRANSFER_CHARACTERISTICS_SRGB; // 13
            header->matrixCoefficients = AVIF_MATRIX_COEFFICIENTS_IDENTITY;       // 0
        } else if (colorDescriptionIdc == 5) {
            // sYCC
            header->colorPrimaries = AVIF_COLOR_PRIMARIES_BT709;                  // 1
            header->transferCharacteristics = AVIF_TRANSFER_CHARACTERISTICS_SRGB; // 13
            header->matrixCoefficients = AVIF_MATRIX_COEFFICIENTS_BT470BG;        // 5
        } else {
            // Reserved
            header->colorPrimaries = AVIF_COLOR_PRIMARIES_UNSPECIFIED;                   // 2
            header->transferCharacteristics = AVIF_TRANSFER_CHARACTERISTICS_UNSPECIFIED; // 2
            header->matrixCoefficients = AVIF_MATRIX_COEFFICIENTS_UNSPECIFIED;           // 2
        }
        header->range = avifBitsRead(bits, 1) ? AVIF_RANGE_FULL : AVIF_RANGE_LIMITED; // color_range
    } else {
        // Keep the default CICP values.
    }

    if (chromaSamplePositionPresent) {
        const uint32_t chromaSamplePosition = avifBitsReadVLC(bits); // ci_chroma_sample_position_0
        header->chromaSamplePosition = av2ChromaSamplePositionToAv1ChromaSamplePosition(chromaSamplePosition);
        header->av1C.chromaSamplePosition = (uint8_t)header->chromaSamplePosition;
    }

    // Other ignored fields.
    return !bits->error;
}
#endif // defined(AVIF_CODEC_AVM)

static avifBool av1SequenceHeaderParse(avifSequenceHeader * header, const avifROData * sample)
{
    avifROData obus = *sample;

    // Find the sequence header OBU
    while (obus.size > 0) {
        avifBits bits;
        avifBitsInit(&bits, obus.data, obus.size);

        // obu_header()
        const uint32_t obu_forbidden_bit = avifBitsRead(&bits, 1);
        if (obu_forbidden_bit != 0) {
            return AVIF_FALSE;
        }
        const uint32_t obu_type = avifBitsRead(&bits, 4);
        const uint32_t obu_extension_flag = avifBitsRead(&bits, 1);
        const uint32_t obu_has_size_field = avifBitsRead(&bits, 1);
        avifBitsRead(&bits, 1); // obu_reserved_1bit

        if (obu_extension_flag) {   // obu_extension_header()
            avifBitsRead(&bits, 8); // temporal_id, spatial_id, extension_header_reserved_3bits
        }

        uint32_t obu_size = 0;
        if (obu_has_size_field)
            obu_size = avifBitsReadUleb128(&bits);
        else
            obu_size = (int)obus.size - 1 - obu_extension_flag;

        if (bits.error) {
            return AVIF_FALSE;
        }

        const uint32_t init_bit_pos = avifBitsReadPos(&bits);
        const uint32_t init_byte_pos = init_bit_pos >> 3;
        if (obu_size > obus.size - init_byte_pos)
            return AVIF_FALSE;

        if (obu_type == 1) { // Sequence Header
            avifBits seqHdrBits;
            avifBitsInit(&seqHdrBits, obus.data + init_byte_pos, obu_size);
            return parseAV1SequenceHeader(&seqHdrBits, header);
        }

        // Skip this OBU
        obus.data += (size_t)obu_size + init_byte_pos;
        obus.size -= (size_t)obu_size + init_byte_pos;
    }
    return AVIF_FALSE;
}

#if defined(AVIF_CODEC_AVM)
static avifBool av2SequenceHeaderParse(avifSequenceHeader * header, const avifROData * sample)
{
    avifBool sequenceHeaderFound = AVIF_FALSE;
    avifROData obus = *sample;

    // Find the Sequence Header OBU, and the Content Interpretation OBU if any.
    while (obus.size > 0) {
        avifBits bits;
        avifBitsInit(&bits, obus.data, obus.size);

        const uint32_t obuSize = avifBitsReadUleb128(&bits);

        // obu_header()
        const uint32_t obuHeaderExtensionFlag = avifBitsRead(&bits, 1);
        const uint32_t obuType = avifBitsRead(&bits, 5);
        avifBitsRead(&bits, 2); // obu_tlayer_id

        if (obuHeaderExtensionFlag) {
            avifBitsRead(&bits, 8); // obu_mlayer_id, obu_xlayer_id
        }

        if (bits.error) {
            return AVIF_FALSE;
        }

        const uint32_t obuHeaderSize = 1 + obuHeaderExtensionFlag;
        if (obuSize < obuHeaderSize) {
            return AVIF_FALSE;
        }
        const uint32_t obuPayloadSize = obuSize - obuHeaderSize;
        const uint32_t initBitPos = avifBitsReadPos(&bits);
        const uint32_t initBytePos = initBitPos >> 3;
        if (obuPayloadSize > obus.size - initBytePos) {
            return AVIF_FALSE;
        }

        if (obuType == OBU_SEQUENCE_HEADER) {
            if (sequenceHeaderFound) {
                return AVIF_FALSE;
            }
            avifBits seqHdrBits;
            avifBitsInit(&seqHdrBits, obus.data + initBytePos, obuPayloadSize);
            if (!parseAV2SequenceHeader(&seqHdrBits, header)) {
                return AVIF_FALSE;
            }
            sequenceHeaderFound = AVIF_TRUE;
        } else if (obuType == OBU_CONTENT_INTERPRETATION) { // optional
            if (!sequenceHeaderFound) {
                return AVIF_FALSE;
            }
            avifBits ciBits;
            avifBitsInit(&ciBits, obus.data + initBytePos, obuPayloadSize);
            if (!parseAV2ContentInterpretation(&ciBits, header)) {
                return AVIF_FALSE;
            }
            break;
        }
        obus.data += (size_t)obuPayloadSize + initBytePos;
        obus.size -= (size_t)obuPayloadSize + initBytePos;
    }
    return sequenceHeaderFound;
}
#endif // defined(AVIF_CODEC_AVM)

avifBool avifSequenceHeaderParse(avifSequenceHeader * header, const avifROData * sample, avifCodecType codecType)
{
    switch (codecType) {
        case AVIF_CODEC_TYPE_AV1:
            return av1SequenceHeaderParse(header, sample);
#if defined(AVIF_CODEC_AVM)
        case AVIF_CODEC_TYPE_AV2:
            return av2SequenceHeaderParse(header, sample);
#endif
        default:
            return AVIF_FALSE;
    }
}
