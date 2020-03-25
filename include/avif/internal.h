// Copyright 2019 Joe Drago. All rights reserved.
// SPDX-License-Identifier: BSD-2-Clause

#ifndef AVIF_INTERNAL_H
#define AVIF_INTERNAL_H

#include "avif/avif.h"

#ifdef __cplusplus
extern "C" {
#endif

// Yes, clamp macros are nasty. Do not use them.
#define AVIF_CLAMP(x, low, high) (((x) < (low))) ? (low) : (((high) < (x)) ? (high) : (x))
#define AVIF_MIN(a, b) (((a) < (b)) ? (a) : (b))

// Used by stream related things.
#define CHECK(A)               \
    do {                       \
        if (!(A))              \
            return AVIF_FALSE; \
    } while (0)

// ---------------------------------------------------------------------------
// URNs and Content-Types

#define URN_ALPHA0 "urn:mpeg:mpegB:cicp:systems:auxiliary:alpha"
#define URN_ALPHA1 "urn:mpeg:hevc:2015:auxid:1"

#define CONTENT_TYPE_XMP "application/rdf+xml"

// ---------------------------------------------------------------------------
// Utils

float avifRoundf(float v);

uint16_t avifHTONS(uint16_t s);
uint16_t avifNTOHS(uint16_t s);
uint32_t avifHTONL(uint32_t l);
uint32_t avifNTOHL(uint32_t l);
uint64_t avifHTON64(uint64_t l);
uint64_t avifNTOH64(uint64_t l);

void avifCalcYUVCoefficients(avifImage * image, float * outR, float * outG, float * outB);

#define AVIF_ARRAY_DECLARE(TYPENAME, ITEMSTYPE, ITEMSNAME) \
    typedef struct TYPENAME                                \
    {                                                      \
        ITEMSTYPE * ITEMSNAME;                             \
        uint32_t elementSize;                              \
        uint32_t count;                                    \
        uint32_t capacity;                                 \
    } TYPENAME
void avifArrayCreate(void * arrayStruct, uint32_t elementSize, uint32_t initialCapacity);
uint32_t avifArrayPushIndex(void * arrayStruct);
void * avifArrayPushPtr(void * arrayStruct);
void avifArrayPush(void * arrayStruct, void * element);
void avifArrayDestroy(void * arrayStruct);

AVIF_ARRAY_DECLARE(avifRODataArray, avifROData, raw);
AVIF_ARRAY_DECLARE(avifRWDataArray, avifRWData, raw);

typedef struct avifAlphaParams
{
    uint32_t width;
    uint32_t height;

    uint32_t srcDepth;
    avifRange srcRange;
    uint8_t * srcPlane;
    uint32_t srcRowBytes;
    uint32_t srcOffsetBytes;
    uint32_t srcPixelBytes;

    uint32_t dstDepth;
    avifRange dstRange;
    uint8_t * dstPlane;
    uint32_t dstRowBytes;
    uint32_t dstOffsetBytes;
    uint32_t dstPixelBytes;

} avifAlphaParams;

avifBool avifFillAlpha(const avifAlphaParams * const params);
avifBool avifReformatAlpha(const avifAlphaParams * const params);

// ---------------------------------------------------------------------------
// avifCodecDecodeInput

typedef struct avifSample
{
    avifROData data;
    avifBool sync; // is sync sample (keyframe)
} avifSample;
AVIF_ARRAY_DECLARE(avifSampleArray, avifSample, sample);

typedef struct avifCodecDecodeInput
{
    avifSampleArray samples;
    avifBool alpha; // if true, this is decoding an alpha plane
} avifCodecDecodeInput;

avifCodecDecodeInput * avifCodecDecodeInputCreate(void);
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

struct avifCodec;
struct avifCodecInternal;

typedef avifBool (*avifCodecOpenFunc)(struct avifCodec * codec, uint32_t firstSampleIndex);
typedef avifBool (*avifCodecGetNextImageFunc)(struct avifCodec * codec, avifImage * image);
// avifCodecEncodeImageFunc: if either OBU* is null, skip its encode. alpha should always be lossless
typedef avifBool (*avifCodecEncodeImageFunc)(struct avifCodec * codec, avifImage * image, avifEncoder * encoder, avifRWData * obu, avifBool alpha);
typedef void (*avifCodecDestroyInternalFunc)(struct avifCodec * codec);

typedef struct avifCodec
{
    avifCodecDecodeInput * decodeInput;
    avifCodecConfigurationBox configBox; // Pre-populated by avifEncoderWrite(), available and overridable by codec impls
    struct avifCodecInternal * internal; // up to each codec to use how it wants

    avifCodecOpenFunc open;
    avifCodecGetNextImageFunc getNextImage;
    avifCodecEncodeImageFunc encodeImage;
    avifCodecDestroyInternalFunc destroyInternal;
} avifCodec;

avifCodec * avifCodecCreate(avifCodecChoice choice, uint32_t requiredFlags);
void avifCodecDestroy(avifCodec * codec);

avifCodec * avifCodecCreateAOM(void);     // requires AVIF_CODEC_AOM (codec_aom.c)
const char * avifCodecVersionAOM(void);   // requires AVIF_CODEC_AOM (codec_aom.c)
avifCodec * avifCodecCreateDav1d(void);   // requires AVIF_CODEC_DAV1D (codec_dav1d.c)
const char * avifCodecVersionDav1d(void); // requires AVIF_CODEC_DAV1D (codec_dav1d.c)
avifCodec * avifCodecCreateGav1(void);    // requires AVIF_CODEC_LIBGAV1 (codec_libgav1.c)
const char * avifCodecVersionGav1(void);  // requires AVIF_CODEC_LIBGAV1 (codec_libgav1.c)
avifCodec * avifCodecCreateRav1e(void);   // requires AVIF_CODEC_RAV1E (codec_rav1e.c)
const char * avifCodecVersionRav1e(void); // requires AVIF_CODEC_RAV1E (codec_rav1e.c)

// ---------------------------------------------------------------------------
// avifStream

typedef size_t avifBoxMarker;

typedef struct avifBoxHeader
{
    size_t size;
    uint8_t type[4];
} avifBoxHeader;

typedef struct avifROStream
{
    avifROData * raw;
    size_t offset;
} avifROStream;

const uint8_t * avifROStreamCurrent(avifROStream * stream);
void avifROStreamStart(avifROStream * stream, avifROData * raw);
size_t avifROStreamOffset(avifROStream * stream);
void avifROStreamSetOffset(avifROStream * stream, size_t offset);

avifBool avifROStreamHasBytesLeft(avifROStream * stream, size_t byteCount);
size_t avifROStreamRemainingBytes(avifROStream * stream);
avifBool avifROStreamSkip(avifROStream * stream, size_t byteCount);
avifBool avifROStreamRead(avifROStream * stream, uint8_t * data, size_t size);
avifBool avifROStreamReadU16(avifROStream * stream, uint16_t * v);
avifBool avifROStreamReadU32(avifROStream * stream, uint32_t * v);
avifBool avifROStreamReadUX8(avifROStream * stream, uint64_t * v, uint64_t factor); // Reads a factor*8 sized uint, saves in v
avifBool avifROStreamReadU64(avifROStream * stream, uint64_t * v);
avifBool avifROStreamReadString(avifROStream * stream, char * output, size_t outputSize);
avifBool avifROStreamReadBoxHeader(avifROStream * stream, avifBoxHeader * header);
avifBool avifROStreamReadVersionAndFlags(avifROStream * stream, uint8_t * version, uint8_t * flags); // flags is an optional uint8_t[3]
avifBool avifROStreamReadAndEnforceVersion(avifROStream * stream, uint8_t enforcedVersion);          // currently discards flags

typedef struct avifRWStream
{
    avifRWData * raw;
    size_t offset;
} avifRWStream;

uint8_t * avifRWStreamCurrent(avifRWStream * stream);
void avifRWStreamStart(avifRWStream * stream, avifRWData * raw);
size_t avifRWStreamOffset(avifRWStream * stream);
void avifRWStreamSetOffset(avifRWStream * stream, size_t offset);

void avifRWStreamFinishWrite(avifRWStream * stream);
void avifRWStreamWrite(avifRWStream * stream, const uint8_t * data, size_t size);
void avifRWStreamWriteChars(avifRWStream * stream, const char * chars, size_t size);
avifBoxMarker avifRWStreamWriteBox(avifRWStream * stream, const char * type, int version /* -1 for "not a FullBox" */, size_t contentSize);
void avifRWStreamFinishBox(avifRWStream * stream, avifBoxMarker marker);
void avifRWStreamWriteU8(avifRWStream * stream, uint8_t v);
void avifRWStreamWriteU16(avifRWStream * stream, uint16_t v);
void avifRWStreamWriteU32(avifRWStream * stream, uint32_t v);
void avifRWStreamWriteZeros(avifRWStream * stream, size_t byteCount);

#ifdef __cplusplus
} // extern "C"
#endif

#endif // ifndef AVIF_INTERNAL_H
