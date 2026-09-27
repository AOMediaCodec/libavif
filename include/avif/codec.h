// Copyright 2019 Joe Drago. All rights reserved.
// SPDX-License-Identifier: BSD-2-Clause

#ifndef AVIF_CODEC_H
#define AVIF_CODEC_H

#include "avif/avif.h" // IWYU pragma: export

#ifdef __cplusplus
extern "C" {
#endif

// ---------------------------------------------------------------------------
// Utils

#define AVIF_ARRAY_DECLARE(TYPENAME, ITEMSTYPE, ITEMSNAME) \
    typedef struct TYPENAME                                \
    {                                                      \
        ITEMSTYPE * ITEMSNAME;                             \
        uint32_t elementSize;                              \
        uint32_t count;                                    \
        uint32_t capacity;                                 \
    } TYPENAME
AVIF_NODISCARD avifBool avifArrayCreate(void * arrayStruct, uint32_t elementSize, uint32_t initialCapacity);
AVIF_NODISCARD void * avifArrayPush(void * arrayStruct);
void avifArrayPop(void * arrayStruct);
void avifArrayDestroy(void * arrayStruct);

// ---------------------------------------------------------------------------
// avifDecodeSample

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

AVIF_NODISCARD avifCodecEncodeOutput * avifCodecEncodeOutputCreate(void);
avifResult avifCodecEncodeOutputAddSample(avifCodecEncodeOutput * encodeOutput, const uint8_t * data, size_t len, avifBool sync);
void avifCodecEncodeOutputDestroy(avifCodecEncodeOutput * encodeOutput);

// ---------------------------------------------------------------------------
// avifCodecSpecificOptions (key/value string pairs for advanced tuning)

typedef struct avifCodecSpecificOption
{
    char * key;   // Must be a simple lowercase alphanumeric string
    char * value; // Free-form string to be interpreted by the codec
} avifCodecSpecificOption;
AVIF_ARRAY_DECLARE(avifCodecSpecificOptions, avifCodecSpecificOption, entries);

// ---------------------------------------------------------------------------
// avifCodecType (underlying video format)

// Alliance for Open Media video formats that can be used in the AVIF image format.
typedef enum avifCodecType
{
    AVIF_CODEC_TYPE_UNKNOWN,
    AVIF_CODEC_TYPE_AV1,
#if defined(AVIF_CODEC_AVM)
    AVIF_CODEC_TYPE_AV2, // Experimental.
#endif
} avifCodecType;

// ---------------------------------------------------------------------------
// avifCodec (abstraction layer to use different codec implementations)

struct avifCodec;
struct avifCodecInternal;

typedef enum avifEncoderChange
{
    AVIF_ENCODER_CHANGE_MIN_QUANTIZER = (1 << 0),
    AVIF_ENCODER_CHANGE_MAX_QUANTIZER = (1 << 1),
    AVIF_ENCODER_CHANGE_MIN_QUANTIZER_ALPHA = (1 << 2),
    AVIF_ENCODER_CHANGE_MAX_QUANTIZER_ALPHA = (1 << 3),
    AVIF_ENCODER_CHANGE_TILE_ROWS_LOG2 = (1 << 4),
    AVIF_ENCODER_CHANGE_TILE_COLS_LOG2 = (1 << 5),
    AVIF_ENCODER_CHANGE_QUALITY = (1 << 6),
    AVIF_ENCODER_CHANGE_QUALITY_ALPHA = (1 << 7),
    AVIF_ENCODER_CHANGE_SCALING_MODE = (1 << 8),

    AVIF_ENCODER_CHANGE_CODEC_SPECIFIC = (1 << 30)
} avifEncoderChange;
typedef int avifEncoderChanges;

typedef avifBool (*avifCodecGetNextImageFunc)(struct avifCodec * codec,
                                              const avifDecodeSample * sample,
                                              avifBool alpha,
                                              avifBool * isLimitedRangeAlpha,
                                              avifImage * image);
// EncodeImage and EncodeFinish are not required to always emit a sample, but when all images are
// encoded and EncodeFinish is called, the number of samples emitted must match the number of submitted frames.
// avifCodecEncodeImageFunc may return AVIF_RESULT_UNKNOWN_ERROR to automatically emit the appropriate
// AVIF_RESULT_ENCODE_COLOR_FAILED or AVIF_RESULT_ENCODE_ALPHA_FAILED depending on the alpha argument.
// avifCodecEncodeImageFunc should use tileRowsLog2 and tileColsLog2 instead of
// encoder->tileRowsLog2, encoder->tileColsLog2, and encoder->autoTiling. The caller of
// avifCodecEncodeImageFunc is responsible for automatic tiling if encoder->autoTiling is set to
// AVIF_TRUE. The actual tiling values are passed to avifCodecEncodeImageFunc as parameters.
// Similarly, avifCodecEncodeImageFunc should use the quality parameter instead of
// encoder->quality, encoder->qualityAlpha, and encoder->qualityGainMap. If disableLaggedOutput is AVIF_TRUE, then
// the encoder will emit the output frame without any lag (if supported). Note that disableLaggedOutput is only
// used by the first call to this function (which initializes the encoder) and is ignored by the subsequent calls.
//
// Note: The caller of avifCodecEncodeImageFunc always passes encoder->data->tileRowsLog2 and
// encoder->data->tileColsLog2 as the tileRowsLog2 and tileColsLog2 arguments. Because
// encoder->data is of a struct type defined in src/write.c, avifCodecEncodeImageFunc cannot
// dereference encoder->data and has to receive encoder->data->tileRowsLog2 and
// encoder->data->tileColsLog2 via function parameters.
typedef avifResult (*avifCodecEncodeImageFunc)(struct avifCodec * codec,
                                               avifEncoder * encoder,
                                               const avifImage * image,
                                               avifBool alpha,
                                               int tileRowsLog2,
                                               int tileColsLog2,
                                               int quality,
                                               avifEncoderChanges encoderChanges,
                                               avifBool disableLaggedOutput,
                                               avifAddImageFlags addImageFlags,
                                               avifCodecEncodeOutput * output);
typedef avifBool (*avifCodecEncodeFinishFunc)(struct avifCodec * codec, avifCodecEncodeOutput * output);
typedef void (*avifCodecDestroyInternalFunc)(struct avifCodec * codec);

typedef struct avifCodec
{
    const avifCodecSpecificOptions * csOptions; // Contains codec-specific key/value pairs for advanced tuning.
                                                // This array is NOT owned by avifCodec.
    struct avifCodecInternal * internal;        // up to each codec to use how it wants
    avifDiagnostics * diag;                     // Shallow copy; owned by avifEncoder or avifDecoder

    // Decoder options (for getNextImage):
    int maxThreads;               // See avifDecoder::maxThreads.
    uint32_t imageSizeLimit;      // See avifDecoder::imageSizeLimit.
    uint32_t imageDimensionLimit; // See avifDecoder::imageDimensionLimit.
    uint8_t operatingPoint;       // Operating point, defaults to 0.
    avifBool allLayers;           // if true, the underlying codec must decode all layers, not just the best layer

    avifCodecGetNextImageFunc getNextImage;
    avifCodecEncodeImageFunc encodeImage;
    avifCodecEncodeFinishFunc encodeFinish;
    avifCodecDestroyInternalFunc destroyInternal;
} avifCodec;

#ifdef __cplusplus
} // extern "C"
#endif

#endif //AVIF_CODEC_H
