// Copyright 2019 Joe Drago. All rights reserved.
// SPDX-License-Identifier: BSD-2-Clause

#ifndef AVIF_INTERNAL_H
#define AVIF_INTERNAL_H

#include "avif/avif.h"

#ifdef __cplusplus
extern "C" {
#endif

// Yes, clamp macros are nasty. Do not use them.
#define AVIF_CLAMP(x, low, high)  (((x) > (high)) ? (high) : (((x) < (low)) ? (low) : (x)))

// Used by stream related things.
#define CHECK(A) if (!(A)) return AVIF_FALSE;

// ---------------------------------------------------------------------------
// URNs

#define URN_ALPHA0 "urn:mpeg:mpegB:cicp:systems:auxiliary:alpha"
#define URN_ALPHA1 "urn:mpeg:hevc:2015:auxid:1"

// ---------------------------------------------------------------------------
// Utils

float avifRoundf(float v);

uint16_t avifHTONS(uint16_t s);
uint16_t avifNTOHS(uint16_t s);
uint32_t avifHTONL(uint32_t l);
uint32_t avifNTOHL(uint32_t l);
uint64_t avifHTON64(uint64_t l);
uint64_t avifNTOH64(uint64_t l);

int avifFullToLimitedY(int depth, int v);
int avifFullToLimitedUV(int depth, int v);
int avifLimitedToFullY(int depth, int v);
int avifLimitedToFullUV(int depth, int v);

void avifCalcYUVCoefficients(avifImage * image, float * outR, float * outG, float * outB);

// ---------------------------------------------------------------------------
// Memory management

void * avifAlloc(size_t size);
void avifFree(void * p);

// ---------------------------------------------------------------------------
// avifCodec (abstraction layer to use different AV1 implementations)

typedef struct avifCodecConfigurationBox
{
    // [skipped; is constant] unsigned int (1)marker = 1;
    // [skipped; is constant] unsigned int (7)version = 1;

    uint8_t seqProfile;           // unsigned int (3) seq_profile;
    uint8_t seqLevelIdx0;         // unsigned int (5) seq_level_idx_0;
    uint8_t seqTier0;             // unsigned int (1) seq_tier_0;
    uint8_t highBitdepth;         // unsigned int (1) high_bitdepth;
    uint8_t twelveBit;            // unsigned int (1) twelve_bit;
    uint8_t monochrome;           // unsigned int (1) monochrome;
    uint8_t chromaSubsamplingX;   // unsigned int (1) chroma_subsampling_x;
    uint8_t chromaSubsamplingY;   // unsigned int (1) chroma_subsampling_y;
    uint8_t chromaSamplePosition; // unsigned int (2) chroma_sample_position;

    // unsigned int (3)reserved = 0;
    // unsigned int (1)initial_presentation_delay_present;
    // if (initial_presentation_delay_present) {
    //     unsigned int (4)initial_presentation_delay_minus_one;
    // } else {
    //     unsigned int (4)reserved = 0;
    // }
} avifCodecConfigurationBox;

typedef enum avifCodecPlanes
{
    AVIF_CODEC_PLANES_COLOR = 0, // YUV
    AVIF_CODEC_PLANES_ALPHA,

    AVIF_CODEC_PLANES_COUNT
} avifCodecPlanes;

typedef struct avifCodecImageSize
{
    uint32_t width;
    uint32_t height;
} avifCodecImageSize;

struct avifCodecInternal;

typedef struct avifCodec
{
    struct avifCodecInternal * internal; // up to each codec to use how it wants
} avifCodec;

avifCodec * avifCodecCreate();
void avifCodecDestroy(avifCodec * codec);

avifBool avifCodecDecode(avifCodec * codec, avifCodecPlanes planes, avifRawData * obu);
avifCodecImageSize avifCodecGetImageSize(avifCodec * codec, avifCodecPlanes planes); // should return 0s if absent
avifBool avifCodecAlphaLimitedRange(avifCodec * codec);                              // returns AVIF_TRUE if an alpha plane exists and was encoded with limited range
avifResult avifCodecGetDecodedImage(avifCodec * codec, avifImage * image);
avifResult avifCodecEncodeImage(avifCodec * codec, avifImage * image, int numThreads, int colorQuality, avifRawData * colorOBU, avifRawData * alphaOBU); // if either OBU* is null, skip its encode. alpha should always be lossless
void avifCodecGetConfigurationBox(avifCodec * codec, avifCodecPlanes planes, avifCodecConfigurationBox * outConfig);

// ---------------------------------------------------------------------------
// avifStream

typedef size_t avifBoxMarker;

typedef struct avifStream
{
    avifRawData * raw;
    size_t offset;
} avifStream;

typedef struct avifBoxHeader
{
    size_t size;
    uint8_t type[4];
} avifBoxHeader;

uint8_t * avifStreamCurrent(avifStream * stream);

void avifStreamStart(avifStream * stream, avifRawData * raw);

// Read
avifBool avifStreamHasBytesLeft(avifStream * stream, size_t byteCount);
size_t avifStreamRemainingBytes(avifStream * stream);
size_t avifStreamOffset(avifStream * stream);
void avifStreamSetOffset(avifStream * stream, size_t offset);
avifBool avifStreamSkip(avifStream * stream, size_t byteCount);
avifBool avifStreamRead(avifStream * stream, uint8_t * data, size_t size);
avifBool avifStreamReadU16(avifStream * stream, uint16_t * v);
avifBool avifStreamReadU32(avifStream * stream, uint32_t * v);
avifBool avifStreamReadUX8(avifStream * stream, uint64_t * v, uint64_t factor); // Reads a factor*8 sized uint, saves in v
avifBool avifStreamReadU64(avifStream * stream, uint64_t * v);
avifBool avifStreamReadString(avifStream * stream, char * output, size_t outputSize);
avifBool avifStreamReadBoxHeader(avifStream * stream, avifBoxHeader * header);
avifBool avifStreamReadVersionAndFlags(avifStream * stream, uint8_t * version, uint8_t * flags); // flags is an optional uint8_t[3]
avifBool avifStreamReadAndEnforceVersion(avifStream * stream, uint8_t enforcedVersion);          // currently discards flags

// Write
void avifStreamFinishWrite(avifStream * stream);
void avifStreamWrite(avifStream * stream, const uint8_t * data, size_t size);
void avifStreamWriteChars(avifStream * stream, const char * chars, size_t size);
avifBoxMarker avifStreamWriteBox(avifStream * stream, const char * type, int version /* -1 for "not a FullBox" */, size_t contentSize);
void avifStreamFinishBox(avifStream * stream, avifBoxMarker marker);
void avifStreamWriteU8(avifStream * stream, uint8_t v);
void avifStreamWriteU16(avifStream * stream, uint16_t v);
void avifStreamWriteU32(avifStream * stream, uint32_t v);
void avifStreamWriteZeros(avifStream * stream, size_t byteCount);

#ifdef __cplusplus
} // extern "C"
#endif

#endif // ifndef AVIF_INTERNAL_H
