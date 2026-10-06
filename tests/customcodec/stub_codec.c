// Copyright 2026 Yuan Tong. All rights reserved.
// SPDX-License-Identifier: BSD-2-Clause

#include "stub_codec.h"

#include <string.h>

// clang-format off
static const uint8_t stubBitstream[] = {
    // OBU_SEQUENCE_HEADER, has_size_field, size 5:
    //   seq_profile 0, reduced_still_picture_header, seq_level_idx 0, max_frame_width/height 8,
    //   8-bit, not monochrome, color_range 0, chroma_sample_position 0, followed by trailing bits.
    0x0A, 0x05, 0x18, 0x08, 0xBF, 0x00, 0x02,
    // OBU_PADDING, has_size_field, size 16: magic payload.
    0x7A, 0x10, 'a', 'v', 'i', 'f', ' ', 'c', 'u', 's', 't', 'o', 'm', ' ', 's', 't', 'u', 'b',
};
// clang-format on

static avifStubCodecStats stats;

uint8_t avifStubCodecPixel(int plane, uint32_t x, uint32_t y)
{
    switch (plane) {
        case AVIF_CHAN_Y:
            return (uint8_t)(16 + 8 * y + x);
        case AVIF_CHAN_U:
            return (uint8_t)(96 + 4 * y + x);
        default:
            return (uint8_t)(160 + 4 * y + x);
    }
}

static avifBool stubImageMatches(const avifImage * image)
{
    if (image->width != AVIF_STUB_CODEC_WIDTH || image->height != AVIF_STUB_CODEC_HEIGHT ||
        image->depth != AVIF_STUB_CODEC_DEPTH || image->yuvFormat != AVIF_STUB_CODEC_FORMAT) {
        return AVIF_FALSE;
    }
    for (int plane = AVIF_CHAN_Y; plane <= AVIF_CHAN_V; ++plane) {
        const uint32_t width = avifImagePlaneWidth(image, plane);
        const uint32_t height = avifImagePlaneHeight(image, plane);
        const uint32_t rowBytes = avifImagePlaneRowBytes(image, plane);
        const uint8_t * pixels = avifImagePlane(image, plane);
        for (uint32_t y = 0; y < height; ++y) {
            for (uint32_t x = 0; x < width; ++x) {
                if (pixels[y * rowBytes + x] != avifStubCodecPixel(plane, x, y)) {
                    return AVIF_FALSE;
                }
            }
        }
    }
    return AVIF_TRUE;
}

static avifResult stubCodecEncodeImage(avifCodec * codec,
                                       avifEncoder * encoder,
                                       const avifImage * image,
                                       avifBool alpha,
                                       int tileRowsLog2,
                                       int tileColsLog2,
                                       int quality,
                                       avifEncoderChanges encoderChanges,
                                       avifBool disableLaggedOutput,
                                       avifAddImageFlags addImageFlags,
                                       avifCodecEncodeOutput * output)
{
    (void)codec;
    (void)encoder;
    (void)tileRowsLog2;
    (void)tileColsLog2;
    (void)quality;
    (void)encoderChanges;
    (void)disableLaggedOutput;
    (void)addImageFlags;

    if (alpha || !stubImageMatches(image)) {
        return AVIF_RESULT_UNKNOWN_ERROR;
    }
    const avifResult result = avifCodecEncodeOutputAddSample(output, stubBitstream, sizeof(stubBitstream), AVIF_TRUE);
    if (result == AVIF_RESULT_OK) {
        ++stats.encodedFrames;
    }
    return result;
}

static avifBool stubCodecEncodeFinish(avifCodec * codec, avifCodecEncodeOutput * output)
{
    (void)codec;
    (void)output;
    return AVIF_TRUE;
}

static avifBool stubCodecGetNextImage(avifCodec * codec,
                                      const avifDecodeSample * sample,
                                      avifBool alpha,
                                      avifBool * isLimitedRangeAlpha,
                                      avifImage * image)
{
    (void)codec;
    (void)isLimitedRangeAlpha;

    if (alpha || !sample || sample->data.size != sizeof(stubBitstream) ||
        memcmp(sample->data.data, stubBitstream, sizeof(stubBitstream)) != 0) {
        return AVIF_FALSE;
    }

    avifImageFreePlanes(image, AVIF_PLANES_YUV);
    image->width = AVIF_STUB_CODEC_WIDTH;
    image->height = AVIF_STUB_CODEC_HEIGHT;
    image->depth = AVIF_STUB_CODEC_DEPTH;
    image->yuvFormat = AVIF_STUB_CODEC_FORMAT;
    if (avifImageAllocatePlanes(image, AVIF_PLANES_YUV) != AVIF_RESULT_OK) {
        return AVIF_FALSE;
    }
    for (int plane = AVIF_CHAN_Y; plane <= AVIF_CHAN_V; ++plane) {
        const uint32_t width = avifImagePlaneWidth(image, plane);
        const uint32_t height = avifImagePlaneHeight(image, plane);
        const uint32_t rowBytes = avifImagePlaneRowBytes(image, plane);
        uint8_t * pixels = avifImagePlane(image, plane);
        for (uint32_t y = 0; y < height; ++y) {
            for (uint32_t x = 0; x < width; ++x) {
                pixels[y * rowBytes + x] = avifStubCodecPixel(plane, x, y);
            }
        }
    }
    ++stats.decodedFrames;
    return AVIF_TRUE;
}

static const char * stubCodecVersion(void)
{
    return "1.0";
}

static avifCodec * stubCodecCreate(void)
{
    // libavif releases the codec with avifFree(), so it must be allocated with avifAlloc().
    avifCodec * codec = (avifCodec *)avifAlloc(sizeof(avifCodec));
    if (!codec) {
        return NULL;
    }
    memset(codec, 0, sizeof(*codec));
    codec->getNextImage = stubCodecGetNextImage;
    codec->encodeImage = stubCodecEncodeImage;
    codec->encodeFinish = stubCodecEncodeFinish;
    return codec;
}

avifResult avifStubCodecRegister(avifCodecChoice * choice)
{
    const avifCodecChoice existingChoice = avifCodecChoiceFromName(AVIF_STUB_CODEC_NAME);
    if (existingChoice != AVIF_CODEC_CHOICE_AUTO) {
        *choice = existingChoice;
        return AVIF_RESULT_OK;
    }

    avifCodecInformation info = { 0 };
    info.type = AVIF_CODEC_TYPE_AV1;
    info.name = AVIF_STUB_CODEC_NAME;
    info.version = stubCodecVersion;
    info.create = stubCodecCreate;
    info.flags = AVIF_CODEC_FLAG_CAN_DECODE | AVIF_CODEC_FLAG_CAN_ENCODE;
    const avifResult result = avifRegisterCustomCodec(&info);
    if (result == AVIF_RESULT_OK) {
        *choice = info.choice;
    }
    return result;
}

avifStubCodecStats avifStubCodecGetStats(void)
{
    return stats;
}
