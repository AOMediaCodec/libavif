// Copyright 2019 Joe Drago. All rights reserved.
// SPDX-License-Identifier: BSD-2-Clause

#ifndef AVIF_INTERNAL_H
#define AVIF_INTERNAL_H

#include "avif/avif.h"

#ifdef __cplusplus
extern "C" {
#endif

// Yes, clamp macros are nasty. Do not use them.
#define AVIF_CLAMP(x, low, high) (((x) > (high)) ? (high) : (((x) < (low)) ? (low) : (x)))

// Used by stream related things.
#define CHECK(A) \
    if (!(A))    \
        return AVIF_FALSE;

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

#define AVIF_ARRAY_DECLARE(TYPENAME, ITEMSTYPE, ITEMSNAME) \
    typedef struct TYPENAME                                \
    {                                                      \
        ITEMSTYPE * ITEMSNAME;                             \
        uint32_t elementSize;                              \
        uint32_t count;                                    \
        uint32_t capacity;                                 \
    } TYPENAME;
void avifArrayCreate(void * arrayStruct, uint32_t elementSize, uint32_t initialCapacity);
uint32_t avifArrayPushIndex(void * arrayStruct);
void * avifArrayPushPtr(void * arrayStruct);
void avifArrayPush(void * arrayStruct, void * element);
void avifArrayDestroy(void * arrayStruct);

AVIF_ARRAY_DECLARE(avifRawDataArray, avifRawData, raw);

// Used internally by avifDecoderNextImage() when there is limited range alpha
void avifImageCopyDecoderAlpha(avifImage * image);

// ---------------------------------------------------------------------------
// Memory management

void * avifAlloc(size_t size);
void avifFree(void * p);

// ---------------------------------------------------------------------------
// avifCodecDecodeInput

typedef struct avifCodecDecodeInput
{
    avifRawDataArray samples;
    avifBool alpha; // if true, this is decoding an alpha plane
} avifCodecDecodeInput;

avifCodecDecodeInput * avifCodecDecodeInputCreate();
void avifCodecDecodeInputDestroy(avifCodecDecodeInput * decodeInput);

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

struct avifCodec;
struct avifCodecInternal;

typedef avifBool (*avifCodecDecodeFunc)(struct avifCodec * codec);
// avifCodecAlphaLimitedRangeFunc: returns AVIF_TRUE if an alpha plane exists and was encoded with limited range
typedef avifBool (*avifCodecAlphaLimitedRangeFunc)(struct avifCodec * codec);
typedef avifBool (*avifCodecGetNextImageFunc)(struct avifCodec * codec, avifImage * image);
// avifCodecEncodeImageFunc: if either OBU* is null, skip its encode. alpha should always be lossless
typedef avifBool (*avifCodecEncodeImageFunc)(struct avifCodec * codec, avifImage * image, avifEncoder * encoder, avifRawData * obu, avifBool alpha);
typedef void (*avifCodecGetConfigurationBoxFunc)(struct avifCodec * codec, avifCodecConfigurationBox * outConfig);
typedef void (*avifCodecDestroyInternalFunc)(struct avifCodec * codec);

typedef struct avifCodec
{
    avifCodecDecodeInput * decodeInput;
    struct avifCodecInternal * internal; // up to each codec to use how it wants

    avifCodecDecodeFunc decode;
    avifCodecAlphaLimitedRangeFunc alphaLimitedRange;
    avifCodecGetNextImageFunc getNextImage;
    avifCodecEncodeImageFunc encodeImage;
    avifCodecGetConfigurationBoxFunc getConfigurationBox;
    avifCodecDestroyInternalFunc destroyInternal;
} avifCodec;

avifCodec * avifCodecCreateAOM();   // requires AVIF_CODEC_AOM
avifCodec * avifCodecCreateDav1d(); // requires AVIF_CODEC_DAV1D
void avifCodecDestroy(avifCodec * codec);

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
