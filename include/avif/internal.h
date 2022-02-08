// Copyright 2019 Joe Drago. All rights reserved.
// SPDX-License-Identifier: BSD-2-Clause

#ifndef AVIF_INTERNAL_H
#define AVIF_INTERNAL_H

#include "avif/avif.h"

#ifdef __cplusplus
extern "C" {
#endif

// Yes, clamp macros are nasty. Do not use them.
#define AVIF_CLAMP(x, low, high) (((x) < (low)) ? (low) : (((high) < (x)) ? (high) : (x)))
#define AVIF_MIN(a, b) (((a) < (b)) ? (a) : (b))
#define AVIF_MAX(a, b) (((a) > (b)) ? (a) : (b))

// Used by stream related things.
#define CHECK(A)               \
    do {                       \
        if (!(A))              \
            return AVIF_FALSE; \
    } while (0)

// Used instead of CHECK if needing to return a specific error on failure, instead of AVIF_FALSE
#define CHECKERR(A, ERR) \
    do {                 \
        if (!(A))        \
            return ERR;  \
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

void avifCalcYUVCoefficients(const avifImage * image, float * outR, float * outG, float * outB);

#define AVIF_ARRAY_DECLARE(TYPENAME, ITEMSTYPE, ITEMSNAME) \
    typedef struct TYPENAME                                \
    {                                                      \
        ITEMSTYPE * ITEMSNAME;                             \
        uint32_t elementSize;                              \
        uint32_t count;                                    \
        uint32_t capacity;                                 \
    } TYPENAME
avifBool avifArrayCreate(void * arrayStruct, uint32_t elementSize, uint32_t initialCapacity);
uint32_t avifArrayPushIndex(void * arrayStruct);
void * avifArrayPushPtr(void * arrayStruct);
void avifArrayPush(void * arrayStruct, void * element);
void avifArrayPop(void * arrayStruct);
void avifArrayDestroy(void * arrayStruct);

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

typedef enum avifReformatMode
{
    AVIF_REFORMAT_MODE_YUV_COEFFICIENTS = 0, // Normal YUV conversion using coefficients
    AVIF_REFORMAT_MODE_IDENTITY,             // Pack GBR directly into YUV planes (AVIF_MATRIX_COEFFICIENTS_IDENTITY)
    AVIF_REFORMAT_MODE_YCGCO                 // YUV conversion using AVIF_MATRIX_COEFFICIENTS_YCGCO
} avifReformatMode;

typedef enum avifAlphaMultiplyMode
{
    AVIF_ALPHA_MULTIPLY_MODE_NO_OP = 0,
    AVIF_ALPHA_MULTIPLY_MODE_MULTIPLY,
    AVIF_ALPHA_MULTIPLY_MODE_UNMULTIPLY
} avifAlphaMultiplyMode;

typedef struct avifReformatState
{
    // YUV coefficients
    float kr;
    float kg;
    float kb;

    uint32_t yuvChannelBytes;
    uint32_t rgbChannelBytes;
    uint32_t rgbChannelCount;
    uint32_t rgbPixelBytes;
    uint32_t rgbOffsetBytesR;
    uint32_t rgbOffsetBytesG;
    uint32_t rgbOffsetBytesB;
    uint32_t rgbOffsetBytesA;

    uint32_t yuvDepth;
    avifRange yuvRange;
    int yuvMaxChannel;
    int rgbMaxChannel;
    float rgbMaxChannelF;
    float biasY;   // minimum Y value
    float biasUV;  // the value of 0.5 for the appropriate bit depth [128, 512, 2048]
    float biasA;   // minimum A value
    float rangeY;  // difference between max and min Y
    float rangeUV; // difference between max and min UV
    float rangeA;  // difference between max and min A

    avifPixelFormatInfo formatInfo;

    // LUTs for going from YUV limited/full unorm -> full range RGB FP32
    float unormFloatTableY[1 << 12];
    float unormFloatTableUV[1 << 12];

    avifReformatMode mode;
    // Used by avifImageYUVToRGB() only. avifImageRGBToYUV() uses a local variable (alphaMode) instead.
    avifAlphaMultiplyMode toRGBAlphaMode;
} avifReformatState;

// Returns:
// * AVIF_RESULT_OK              - Converted successfully with libyuv
// * AVIF_RESULT_NOT_IMPLEMENTED - The fast path for this combination is not implemented with libyuv, use built-in YUV conversion
// * [any other error]           - Return error to caller
avifResult avifImageYUVToRGBLibYUV(const avifImage * image, avifRGBImage * rgb);

// Returns:
// * AVIF_RESULT_OK               - Converted successfully with libyuv.
// * AVIF_RESULT_NOT_IMPLEMENTED  - The fast path for this conversion is not implemented with libyuv, use built-in conversion.
// * AVIF_RESULT_INVALID_ARGUMENT - Return error to caller.
avifResult avifRGBImageToF16LibYUV(avifRGBImage * rgb);

// Returns:
// * AVIF_RESULT_OK              - (Un)Premultiply successfully with libyuv
// * AVIF_RESULT_NOT_IMPLEMENTED - The fast path for this combination is not implemented with libyuv, use built-in (Un)Premultiply
// * [any other error]           - Return error to caller
avifResult avifRGBImagePremultiplyAlphaLibYUV(avifRGBImage * rgb);
avifResult avifRGBImageUnpremultiplyAlphaLibYUV(avifRGBImage * rgb);

// ---------------------------------------------------------------------------
// Scaling

// This scales the YUV/A planes in-place.
avifBool avifImageScale(avifImage * image, uint32_t dstWidth, uint32_t dstHeight, uint32_t imageSizeLimit, avifDiagnostics * diag);

// ---------------------------------------------------------------------------
// Grid AVIF images

// Returns false if the tiles in a grid image violate any standards.
// The image contains imageW*imageH pixels. The tiles are of tileW*tileH pixels each.
AVIF_API avifBool avifAreGridDimensionsValid(avifPixelFormat yuvFormat,
                                             uint32_t imageW,
                                             uint32_t imageH,
                                             uint32_t tileW,
                                             uint32_t tileH,
                                             avifDiagnostics * diag);

// ---------------------------------------------------------------------------
// avifCodecDecodeInput

// Legal spatial_id values are [0,1,2,3], so this serves as a sentinel value for "do not filter by spatial_id"
#define AVIF_SPATIAL_ID_UNSET 0xff

typedef struct avifDecodeSample
{
    avifROData data;
    avifBool ownsData;
    avifBool partialData; // if true, data exists but doesn't have all of the sample in it

    uint32_t itemID;   // if non-zero, data comes from a mergedExtents buffer in an avifDecoderItem, not a file offset
    uint64_t offset;   // additional offset into data. Can be used to offset into an itemID's payload as well.
    size_t size;       //
    uint8_t spatialID; // If set to a value other than AVIF_SPATIAL_ID_UNSET, output frames from this sample should be
                       // skipped until the output frame's spatial_id matches this ID.
    avifBool sync;     // is sync sample (keyframe)
} avifDecodeSample;
AVIF_ARRAY_DECLARE(avifDecodeSampleArray, avifDecodeSample, sample);

typedef struct avifCodecDecodeInput
{
    avifDecodeSampleArray samples;
    avifBool allLayers; // if true, the underlying codec must decode all layers, not just the best layer
    avifBool alpha;     // if true, this is decoding an alpha plane
} avifCodecDecodeInput;

avifCodecDecodeInput * avifCodecDecodeInputCreate(void);
void avifCodecDecodeInputDestroy(avifCodecDecodeInput * decodeInput);

// ---------------------------------------------------------------------------
// avifCodecEncodeOutput

typedef struct avifEncodeSample
{
    avifRWData data;
    avifBool sync; // is sync sample (keyframe)
} avifEncodeSample;
AVIF_ARRAY_DECLARE(avifEncodeSampleArray, avifEncodeSample, sample);

typedef struct avifCodecEncodeOutput
{
    avifEncodeSampleArray samples;
} avifCodecEncodeOutput;

avifCodecEncodeOutput * avifCodecEncodeOutputCreate(void);
void avifCodecEncodeOutputAddSample(avifCodecEncodeOutput * encodeOutput, const uint8_t * data, size_t len, avifBool sync);
void avifCodecEncodeOutputDestroy(avifCodecEncodeOutput * encodeOutput);

// ---------------------------------------------------------------------------
// avifCodecSpecificOptions (key/value string pairs for advanced tuning)

typedef struct avifCodecSpecificOption
{
    char * key;   // Must be a simple lowercase alphanumeric string
    char * value; // Free-form string to be interpreted by the codec
} avifCodecSpecificOption;
AVIF_ARRAY_DECLARE(avifCodecSpecificOptions, avifCodecSpecificOption, entries);
avifCodecSpecificOptions * avifCodecSpecificOptionsCreate(void);
void avifCodecSpecificOptionsDestroy(avifCodecSpecificOptions * csOptions);
void avifCodecSpecificOptionsSet(avifCodecSpecificOptions * csOptions, const char * key, const char * value); // if(value==NULL), key is deleted

// ---------------------------------------------------------------------------
// avifCodec (abstraction layer to use different AV1 implementations)

struct avifCodec;
struct avifCodecInternal;

typedef avifBool (*avifCodecGetNextImageFunc)(struct avifCodec * codec,
                                              struct avifDecoder * decoder,
                                              const avifDecodeSample * sample,
                                              avifBool alpha,
                                              avifImage * image);
// EncodeImage and EncodeFinish are not required to always emit a sample, but when all images are
// encoded and EncodeFinish is called, the number of samples emitted must match the number of submitted frames.
// avifCodecEncodeImageFunc may return AVIF_RESULT_UNKNOWN_ERROR to automatically emit the appropriate
// AVIF_RESULT_ENCODE_COLOR_FAILED or AVIF_RESULT_ENCODE_ALPHA_FAILED depending on the alpha argument.
typedef avifResult (*avifCodecEncodeImageFunc)(struct avifCodec * codec,
                                               avifEncoder * encoder,
                                               const avifImage * image,
                                               avifBool alpha,
                                               avifAddImageFlags addImageFlags,
                                               avifCodecEncodeOutput * output);
typedef avifBool (*avifCodecEncodeFinishFunc)(struct avifCodec * codec, avifCodecEncodeOutput * output);
typedef void (*avifCodecDestroyInternalFunc)(struct avifCodec * codec);

typedef struct avifCodec
{
    avifCodecSpecificOptions * csOptions; // Contains codec-specific key/value pairs for advanced tuning.
                                          // If a codec uses a value, it must mark it as used.
                                          // This array is NOT owned by avifCodec.
    struct avifCodecInternal * internal;  // up to each codec to use how it wants
                                          //
    avifDiagnostics * diag;               // Shallow copy; owned by avifEncoder or avifDecoder
                                          //
    uint8_t operatingPoint;               // Operating point, defaults to 0.
    avifBool allLayers;                   // if true, the underlying codec must decode all layers, not just the best layer

    avifCodecGetNextImageFunc getNextImage;
    avifCodecEncodeImageFunc encodeImage;
    avifCodecEncodeFinishFunc encodeFinish;
    avifCodecDestroyInternalFunc destroyInternal;
} avifCodec;

avifCodec * avifCodecCreate(avifCodecChoice choice, avifCodecFlags requiredFlags);
void avifCodecDestroy(avifCodec * codec);

avifCodec * avifCodecCreateAOM(void);     // requires AVIF_CODEC_AOM (codec_aom.c)
const char * avifCodecVersionAOM(void);   // requires AVIF_CODEC_AOM (codec_aom.c)
avifCodec * avifCodecCreateDav1d(void);   // requires AVIF_CODEC_DAV1D (codec_dav1d.c)
const char * avifCodecVersionDav1d(void); // requires AVIF_CODEC_DAV1D (codec_dav1d.c)
avifCodec * avifCodecCreateGav1(void);    // requires AVIF_CODEC_LIBGAV1 (codec_libgav1.c)
const char * avifCodecVersionGav1(void);  // requires AVIF_CODEC_LIBGAV1 (codec_libgav1.c)
avifCodec * avifCodecCreateRav1e(void);   // requires AVIF_CODEC_RAV1E (codec_rav1e.c)
const char * avifCodecVersionRav1e(void); // requires AVIF_CODEC_RAV1E (codec_rav1e.c)
avifCodec * avifCodecCreateSvt(void);     // requires AVIF_CODEC_SVT (codec_svt.c)
const char * avifCodecVersionSvt(void);   // requires AVIF_CODEC_SVT (codec_svt.c)

// ---------------------------------------------------------------------------
// avifDiagnostics

#ifdef __clang__
__attribute__((__format__(__printf__, 2, 3)))
#endif
void avifDiagnosticsPrintf(avifDiagnostics * diag, const char * format, ...);

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
    avifDiagnostics * diag;
    const char * diagContext;
} avifROStream;

const uint8_t * avifROStreamCurrent(avifROStream * stream);
void avifROStreamStart(avifROStream * stream, avifROData * raw, avifDiagnostics * diag, const char * diagContext);
size_t avifROStreamOffset(const avifROStream * stream);
void avifROStreamSetOffset(avifROStream * stream, size_t offset);

avifBool avifROStreamHasBytesLeft(const avifROStream * stream, size_t byteCount);
size_t avifROStreamRemainingBytes(const avifROStream * stream);
avifBool avifROStreamSkip(avifROStream * stream, size_t byteCount);
avifBool avifROStreamRead(avifROStream * stream, uint8_t * data, size_t size);
avifBool avifROStreamReadU16(avifROStream * stream, uint16_t * v);
avifBool avifROStreamReadU32(avifROStream * stream, uint32_t * v);
avifBool avifROStreamReadUX8(avifROStream * stream, uint64_t * v, uint64_t factor); // Reads a factor*8 sized uint, saves in v
avifBool avifROStreamReadU64(avifROStream * stream, uint64_t * v);
avifBool avifROStreamReadString(avifROStream * stream, char * output, size_t outputSize);
avifBool avifROStreamReadBoxHeader(avifROStream * stream, avifBoxHeader * header); // This fails if the size reported by the header cannot fit in the stream
avifBool avifROStreamReadBoxHeaderPartial(avifROStream * stream, avifBoxHeader * header); // This doesn't require that the full box can fit in the stream
avifBool avifROStreamReadVersionAndFlags(avifROStream * stream, uint8_t * version, uint32_t * flags); // version and flags ptrs are both optional
avifBool avifROStreamReadAndEnforceVersion(avifROStream * stream, uint8_t enforcedVersion); // currently discards flags

typedef struct avifRWStream
{
    avifRWData * raw;
    size_t offset;
} avifRWStream;

uint8_t * avifRWStreamCurrent(avifRWStream * stream);
void avifRWStreamStart(avifRWStream * stream, avifRWData * raw);
size_t avifRWStreamOffset(const avifRWStream * stream);
void avifRWStreamSetOffset(avifRWStream * stream, size_t offset);

void avifRWStreamFinishWrite(avifRWStream * stream);
void avifRWStreamWrite(avifRWStream * stream, const void * data, size_t size);
void avifRWStreamWriteChars(avifRWStream * stream, const char * chars, size_t size);
avifBoxMarker avifRWStreamWriteBox(avifRWStream * stream, const char * type, size_t contentSize);
avifBoxMarker avifRWStreamWriteFullBox(avifRWStream * stream, const char * type, size_t contentSize, int version, uint32_t flags);
void avifRWStreamFinishBox(avifRWStream * stream, avifBoxMarker marker);
void avifRWStreamWriteU8(avifRWStream * stream, uint8_t v);
void avifRWStreamWriteU16(avifRWStream * stream, uint16_t v);
void avifRWStreamWriteU32(avifRWStream * stream, uint32_t v);
void avifRWStreamWriteU64(avifRWStream * stream, uint64_t v);
void avifRWStreamWriteZeros(avifRWStream * stream, size_t byteCount);

// This is to make it clear that the box size is currently unknown, and will be determined later (with a call to avifRWStreamFinishBox)
#define AVIF_BOX_SIZE_TBD 0

typedef struct avifSequenceHeader
{
    uint32_t maxWidth;
    uint32_t maxHeight;
    uint32_t bitDepth;
    avifPixelFormat yuvFormat;
    avifChromaSamplePosition chromaSamplePosition;
    avifColorPrimaries colorPrimaries;
    avifTransferCharacteristics transferCharacteristics;
    avifMatrixCoefficients matrixCoefficients;
    avifRange range;
    avifCodecConfigurationBox av1C;
} avifSequenceHeader;
avifBool avifSequenceHeaderParse(avifSequenceHeader * header, const avifROData * sample);

#ifdef __cplusplus
} // extern "C"
#endif

#endif // ifndef AVIF_INTERNAL_H
