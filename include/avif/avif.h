// Copyright 2019 Joe Drago. All rights reserved.
// SPDX-License-Identifier: BSD-2-Clause

#ifndef AVIF_AVIF_H
#define AVIF_AVIF_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// ---------------------------------------------------------------------------
// Constants

#define AVIF_VERSION_MAJOR 0
#define AVIF_VERSION_MINOR 8
#define AVIF_VERSION_PATCH 1
#define AVIF_VERSION (AVIF_VERSION_MAJOR * 10000) + (AVIF_VERSION_MINOR * 100) + AVIF_VERSION_PATCH

typedef int avifBool;
#define AVIF_TRUE 1
#define AVIF_FALSE 0

#define AVIF_QUANTIZER_LOSSLESS 0
#define AVIF_QUANTIZER_BEST_QUALITY 0
#define AVIF_QUANTIZER_WORST_QUALITY 63

#define AVIF_PLANE_COUNT_YUV 3

#define AVIF_SPEED_DEFAULT -1
#define AVIF_SPEED_SLOWEST 0
#define AVIF_SPEED_FASTEST 10

enum avifPlanesFlags
{
    AVIF_PLANES_YUV = (1 << 0),
    AVIF_PLANES_A = (1 << 1),

    AVIF_PLANES_ALL = 0xff
};

enum avifChannelIndex
{
    // rgbPlanes
    AVIF_CHAN_R = 0,
    AVIF_CHAN_G = 1,
    AVIF_CHAN_B = 2,

    // yuvPlanes
    AVIF_CHAN_Y = 0,
    AVIF_CHAN_U = 1,
    AVIF_CHAN_V = 2
};

// ---------------------------------------------------------------------------
// Version

const char * avifVersion(void);
void avifCodecVersions(char outBuffer[256]);

// ---------------------------------------------------------------------------
// Memory management

void * avifAlloc(size_t size);
void avifFree(void * p);

// ---------------------------------------------------------------------------
// avifResult

typedef enum avifResult
{
    AVIF_RESULT_OK = 0,
    AVIF_RESULT_UNKNOWN_ERROR,
    AVIF_RESULT_INVALID_FTYP,
    AVIF_RESULT_NO_CONTENT,
    AVIF_RESULT_NO_YUV_FORMAT_SELECTED,
    AVIF_RESULT_REFORMAT_FAILED,
    AVIF_RESULT_UNSUPPORTED_DEPTH,
    AVIF_RESULT_ENCODE_COLOR_FAILED,
    AVIF_RESULT_ENCODE_ALPHA_FAILED,
    AVIF_RESULT_BMFF_PARSE_FAILED,
    AVIF_RESULT_NO_AV1_ITEMS_FOUND,
    AVIF_RESULT_DECODE_COLOR_FAILED,
    AVIF_RESULT_DECODE_ALPHA_FAILED,
    AVIF_RESULT_COLOR_ALPHA_SIZE_MISMATCH,
    AVIF_RESULT_ISPE_SIZE_MISMATCH,
    AVIF_RESULT_NO_CODEC_AVAILABLE,
    AVIF_RESULT_NO_IMAGES_REMAINING,
    AVIF_RESULT_INVALID_EXIF_PAYLOAD,
    AVIF_RESULT_INVALID_IMAGE_GRID,
    AVIF_RESULT_INVALID_CODEC_SPECIFIC_OPTION
} avifResult;

const char * avifResultToString(avifResult result);

// ---------------------------------------------------------------------------
// avifROData/avifRWData: Generic raw memory storage

typedef struct avifROData
{
    const uint8_t * data;
    size_t size;
} avifROData;

// Note: Use avifRWDataFree() if any avif*() function populates one of these.

typedef struct avifRWData
{
    uint8_t * data;
    size_t size;
} avifRWData;

// clang-format off
// Initialize avifROData/avifRWData on the stack with this
#define AVIF_DATA_EMPTY { NULL, 0 }
// clang-format on

void avifRWDataRealloc(avifRWData * raw, size_t newSize);
void avifRWDataSet(avifRWData * raw, const uint8_t * data, size_t len);
void avifRWDataFree(avifRWData * raw);

// ---------------------------------------------------------------------------
// avifPixelFormat

typedef enum avifPixelFormat
{
    // No pixels are present
    AVIF_PIXEL_FORMAT_NONE = 0,

    AVIF_PIXEL_FORMAT_YUV444,
    AVIF_PIXEL_FORMAT_YUV422,
    AVIF_PIXEL_FORMAT_YUV420,
    AVIF_PIXEL_FORMAT_YUV400
} avifPixelFormat;
const char * avifPixelFormatToString(avifPixelFormat format);

typedef struct avifPixelFormatInfo
{
    avifBool monochrome;
    int chromaShiftX;
    int chromaShiftY;
} avifPixelFormatInfo;

void avifGetPixelFormatInfo(avifPixelFormat format, avifPixelFormatInfo * info);

// ---------------------------------------------------------------------------
// avifChromaSamplePosition

typedef enum avifChromaSamplePosition
{
    AVIF_CHROMA_SAMPLE_POSITION_UNKNOWN = 0,
    AVIF_CHROMA_SAMPLE_POSITION_VERTICAL = 1,
    AVIF_CHROMA_SAMPLE_POSITION_COLOCATED = 2
} avifChromaSamplePosition;

// ---------------------------------------------------------------------------
// avifRange

typedef enum avifRange
{
    AVIF_RANGE_LIMITED = 0,
    AVIF_RANGE_FULL = 1
} avifRange;

// ---------------------------------------------------------------------------
// CICP enums - https://www.itu.int/rec/T-REC-H.273-201612-I/en

typedef enum avifColorPrimaries
{
    // This is actually reserved, but libavif uses it as a sentinel value.
    AVIF_COLOR_PRIMARIES_UNKNOWN = 0,

    AVIF_COLOR_PRIMARIES_BT709 = 1,
    AVIF_COLOR_PRIMARIES_IEC61966_2_4 = 1,
    AVIF_COLOR_PRIMARIES_UNSPECIFIED = 2,
    AVIF_COLOR_PRIMARIES_BT470M = 4,
    AVIF_COLOR_PRIMARIES_BT470BG = 5,
    AVIF_COLOR_PRIMARIES_BT601 = 6,
    AVIF_COLOR_PRIMARIES_SMPTE240 = 7,
    AVIF_COLOR_PRIMARIES_GENERIC_FILM = 8,
    AVIF_COLOR_PRIMARIES_BT2020 = 9,
    AVIF_COLOR_PRIMARIES_XYZ = 10,
    AVIF_COLOR_PRIMARIES_SMPTE431 = 11,
    AVIF_COLOR_PRIMARIES_SMPTE432 = 12, // DCI P3
    AVIF_COLOR_PRIMARIES_EBU3213 = 22
} avifColorPrimaries;

// outPrimaries: rX, rY, gX, gY, bX, bY, wX, wY
void avifColorPrimariesGetValues(avifColorPrimaries acp, float outPrimaries[8]);
avifColorPrimaries avifColorPrimariesFind(const float inPrimaries[8], const char ** outName);

typedef enum avifTransferCharacteristics
{
    // This is actually reserved, but libavif uses it as a sentinel value.
    AVIF_TRANSFER_CHARACTERISTICS_UNKNOWN = 0,

    AVIF_TRANSFER_CHARACTERISTICS_BT709 = 1,
    AVIF_TRANSFER_CHARACTERISTICS_UNSPECIFIED = 2,
    AVIF_TRANSFER_CHARACTERISTICS_BT470M = 4,  // 2.2 gamma
    AVIF_TRANSFER_CHARACTERISTICS_BT470BG = 5, // 2.8 gamma
    AVIF_TRANSFER_CHARACTERISTICS_BT601 = 6,
    AVIF_TRANSFER_CHARACTERISTICS_SMPTE240 = 7,
    AVIF_TRANSFER_CHARACTERISTICS_LINEAR = 8,
    AVIF_TRANSFER_CHARACTERISTICS_LOG100 = 9,
    AVIF_TRANSFER_CHARACTERISTICS_LOG100_SQRT10 = 10,
    AVIF_TRANSFER_CHARACTERISTICS_IEC61966 = 11,
    AVIF_TRANSFER_CHARACTERISTICS_BT1361 = 12,
    AVIF_TRANSFER_CHARACTERISTICS_SRGB = 13,
    AVIF_TRANSFER_CHARACTERISTICS_BT2020_10BIT = 14,
    AVIF_TRANSFER_CHARACTERISTICS_BT2020_12BIT = 15,
    AVIF_TRANSFER_CHARACTERISTICS_SMPTE2084 = 16, // PQ
    AVIF_TRANSFER_CHARACTERISTICS_SMPTE428 = 17,
    AVIF_TRANSFER_CHARACTERISTICS_HLG = 18
} avifTransferCharacteristics;

typedef enum avifMatrixCoefficients
{
    AVIF_MATRIX_COEFFICIENTS_IDENTITY = 0,
    AVIF_MATRIX_COEFFICIENTS_BT709 = 1,
    AVIF_MATRIX_COEFFICIENTS_UNSPECIFIED = 2,
    AVIF_MATRIX_COEFFICIENTS_FCC = 4,
    AVIF_MATRIX_COEFFICIENTS_BT470BG = 5,
    AVIF_MATRIX_COEFFICIENTS_BT601 = 6,
    AVIF_MATRIX_COEFFICIENTS_SMPTE240 = 7,
    AVIF_MATRIX_COEFFICIENTS_YCGCO = 8,
    AVIF_MATRIX_COEFFICIENTS_BT2020_NCL = 9,
    AVIF_MATRIX_COEFFICIENTS_BT2020_CL = 10,
    AVIF_MATRIX_COEFFICIENTS_SMPTE2085 = 11,
    AVIF_MATRIX_COEFFICIENTS_CHROMA_DERIVED_NCL = 12,
    AVIF_MATRIX_COEFFICIENTS_CHROMA_DERIVED_CL = 13,
    AVIF_MATRIX_COEFFICIENTS_ICTCP = 14
} avifMatrixCoefficients;

// ---------------------------------------------------------------------------
// Optional transformation structs

typedef enum avifTransformationFlags
{
    AVIF_TRANSFORM_NONE = 0,

    AVIF_TRANSFORM_PASP = (1 << 0),
    AVIF_TRANSFORM_CLAP = (1 << 1),
    AVIF_TRANSFORM_IROT = (1 << 2),
    AVIF_TRANSFORM_IMIR = (1 << 3)
} avifTransformationFlags;

typedef struct avifPixelAspectRatioBox
{
    // 'pasp' from ISO/IEC 14496-12:2015 12.1.4.3

    // define the relative width and height of a pixel
    uint32_t hSpacing;
    uint32_t vSpacing;
} avifPixelAspectRatioBox;

typedef struct avifCleanApertureBox
{
    // 'clap' from ISO/IEC 14496-12:2015 12.1.4.3

    // a fractional number which defines the exact clean aperture width, in counted pixels, of the video image
    uint32_t widthN;
    uint32_t widthD;

    // a fractional number which defines the exact clean aperture height, in counted pixels, of the video image
    uint32_t heightN;
    uint32_t heightD;

    // a fractional number which defines the horizontal offset of clean aperture centre minus (width‐1)/2. Typically 0.
    uint32_t horizOffN;
    uint32_t horizOffD;

    // a fractional number which defines the vertical offset of clean aperture centre minus (height‐1)/2. Typically 0.
    uint32_t vertOffN;
    uint32_t vertOffD;
} avifCleanApertureBox;

typedef struct avifImageRotation
{
    // 'irot' from ISO/IEC 23008-12:2017 6.5.10

    // angle * 90 specifies the angle (in anti-clockwise direction) in units of degrees.
    uint8_t angle; // legal values: [0-3]
} avifImageRotation;

typedef struct avifImageMirror
{
    // 'imir' from ISO/IEC 23008-12:2017 6.5.12

    // axis specifies a vertical (axis = 0) or horizontal (axis = 1) axis for the mirroring operation.
    uint8_t axis; // legal values: [0, 1]
} avifImageMirror;

// ---------------------------------------------------------------------------
// avifImage

typedef struct avifImage
{
    // Image information
    uint32_t width;
    uint32_t height;
    uint32_t depth; // all planes must share this depth; if depth>8, all planes are uint16_t internally

    avifPixelFormat yuvFormat;
    avifRange yuvRange;
    avifChromaSamplePosition yuvChromaSamplePosition;
    uint8_t * yuvPlanes[AVIF_PLANE_COUNT_YUV];
    uint32_t yuvRowBytes[AVIF_PLANE_COUNT_YUV];
    avifBool imageOwnsYUVPlanes;

    avifRange alphaRange;
    uint8_t * alphaPlane;
    uint32_t alphaRowBytes;
    avifBool imageOwnsAlphaPlane;

    // ICC Profile
    avifRWData icc;

    // CICP information:
    // These are stored in the AV1 payload and used to signal YUV conversion. Additionally, if an
    // ICC profile is not specified, these will be stored in the AVIF container's `colr` box with
    // a type of `nclx`. If your system supports ICC profiles, be sure to check for the existence
    // of one (avifImage.icc) before relying on the values listed here!
    avifColorPrimaries colorPrimaries;
    avifTransferCharacteristics transferCharacteristics;
    avifMatrixCoefficients matrixCoefficients;

    // Transformations - These metadata values are encoded/decoded when transformFlags are set
    // appropriately, but do not impact/adjust the actual pixel buffers used (images won't be
    // pre-cropped or mirrored upon decode). Basic explanations from the standards are offered in
    // comments above, but for detailed explanations, please refer to the HEIF standard (ISO/IEC
    // 23008-12:2017) and the BMFF standard (ISO/IEC 14496-12:2015).
    //
    // To encode any of these boxes, set the values in the associated box, then enable the flag in
    // transformFlags. On decode, only honor the values in boxes with the associated transform flag set.
    uint32_t transformFlags;
    avifPixelAspectRatioBox pasp;
    avifCleanApertureBox clap;
    avifImageRotation irot;
    avifImageMirror imir;

    // Metadata - set with avifImageSetMetadata*() before write, check .size>0 for existence after read
    avifRWData exif;
    avifRWData xmp;
} avifImage;

avifImage * avifImageCreate(int width, int height, int depth, avifPixelFormat yuvFormat);
avifImage * avifImageCreateEmpty(void); // helper for making an image to decode into
void avifImageCopy(avifImage * dstImage, const avifImage * srcImage, uint32_t planes); // deep copy
void avifImageDestroy(avifImage * image);

void avifImageSetProfileICC(avifImage * image, const uint8_t * icc, size_t iccSize);

// Warning: If the Exif payload is set and invalid, avifEncoderWrite() may return AVIF_RESULT_INVALID_EXIF_PAYLOAD
void avifImageSetMetadataExif(avifImage * image, const uint8_t * exif, size_t exifSize);
void avifImageSetMetadataXMP(avifImage * image, const uint8_t * xmp, size_t xmpSize);

void avifImageAllocatePlanes(avifImage * image, uint32_t planes); // Ignores any pre-existing planes
void avifImageFreePlanes(avifImage * image, uint32_t planes);     // Ignores already-freed planes
void avifImageStealPlanes(avifImage * dstImage, avifImage * srcImage, uint32_t planes);

// ---------------------------------------------------------------------------
// Optional YUV<->RGB support

// To convert to/from RGB, create an avifRGBImage on the stack, call avifRGBImageSetDefaults() on
// it, and then tweak the values inside of it accordingly. At a minimum, you should populate
// ->pixels and ->rowBytes with an appropriately sized pixel buffer, which should be at least
// (->rowBytes * ->height) bytes, where ->rowBytes is at least (->width * avifRGBImagePixelSize()).
// If you don't want to supply your own pixel buffer, you can use the
// avifRGBImageAllocatePixels()/avifRGBImageFreePixels() convenience functions.

// avifImageRGBToYUV() and avifImageYUVToRGB() will perform depth rescaling and limited<->full range
// conversion, if necessary. Pixels in an avifRGBImage buffer are always full range, and conversion
// routines will fail if the width and height don't match the associated avifImage.

typedef enum avifRGBFormat
{
    AVIF_RGB_FORMAT_RGB = 0,
    AVIF_RGB_FORMAT_RGBA,
    AVIF_RGB_FORMAT_ARGB,
    AVIF_RGB_FORMAT_BGR,
    AVIF_RGB_FORMAT_BGRA,
    AVIF_RGB_FORMAT_ABGR
} avifRGBFormat;
uint32_t avifRGBFormatChannelCount(avifRGBFormat format);
avifBool avifRGBFormatHasAlpha(avifRGBFormat format);

typedef enum avifChromaUpsampling
{
    AVIF_CHROMA_UPSAMPLING_BILINEAR = 0, // Slower and prettier (default)
    AVIF_CHROMA_UPSAMPLING_NEAREST = 1   // Faster and uglier
} avifChromaUpsampling;

typedef struct avifRGBImage
{
    uint32_t width;                        // must match associated avifImage
    uint32_t height;                       // must match associated avifImage
    uint32_t depth;                        // legal depths [8, 10, 12, 16]. if depth>8, pixels must be uint16_t internally
    avifRGBFormat format;                  // all channels are always full range
    avifChromaUpsampling chromaUpsampling; // How to upsample non-4:4:4 UV (ignored for 444) when converting to RGB.
                                           // Unused when converting to YUV. avifRGBImageSetDefaults() prefers quality over speed.
    avifBool ignoreAlpha; // Used for XRGB formats, treats formats containing alpha (such as ARGB) as if they were
                          // RGB, treating the alpha bits as if they were all 1.

    uint8_t * pixels;
    uint32_t rowBytes;
} avifRGBImage;

void avifRGBImageSetDefaults(avifRGBImage * rgb, const avifImage * image);
uint32_t avifRGBImagePixelSize(const avifRGBImage * rgb);

// Convenience functions. If you supply your own pixels/rowBytes, you do not need to use these.
void avifRGBImageAllocatePixels(avifRGBImage * rgb);
void avifRGBImageFreePixels(avifRGBImage * rgb);

// The main conversion functions
avifResult avifImageRGBToYUV(avifImage * image, const avifRGBImage * rgb);
avifResult avifImageYUVToRGB(const avifImage * image, avifRGBImage * rgb);

// ---------------------------------------------------------------------------
// YUV Utils

int avifFullToLimitedY(int depth, int v);
int avifFullToLimitedUV(int depth, int v);
int avifLimitedToFullY(int depth, int v);
int avifLimitedToFullUV(int depth, int v);

typedef enum avifReformatMode
{
    AVIF_REFORMAT_MODE_YUV_COEFFICIENTS = 0, // Normal YUV conversion using coefficients
    AVIF_REFORMAT_MODE_IDENTITY              // Pack GBR directly into YUV planes (AVIF_MATRIX_COEFFICIENTS_IDENTITY)
} avifReformatMode;

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
    uint32_t rgbDepth;
    avifRange yuvRange;
    int yuvMaxChannel;
    int rgbMaxChannel;
    float yuvMaxChannelF;
    float rgbMaxChannelF;
    int uvBias; // the integer value of 0.5 for the appropriate bit depth [128, 512, 2048]

    avifPixelFormatInfo formatInfo;

    // LUTs for going from YUV limited/full unorm -> full range RGB FP32
    float unormFloatTableY[1 << 12];
    float unormFloatTableUV[1 << 12];

    avifReformatMode mode;
} avifReformatState;
avifBool avifPrepareReformatState(const avifImage * image, const avifRGBImage * rgb, avifReformatState * state);

// ---------------------------------------------------------------------------
// Codec selection

typedef enum avifCodecChoice
{
    AVIF_CODEC_CHOICE_AUTO = 0,
    AVIF_CODEC_CHOICE_AOM,
    AVIF_CODEC_CHOICE_DAV1D,   // Decode only
    AVIF_CODEC_CHOICE_LIBGAV1, // Decode only
    AVIF_CODEC_CHOICE_RAV1E    // Encode only
} avifCodecChoice;

typedef enum avifCodecFlags
{
    AVIF_CODEC_FLAG_CAN_DECODE = (1 << 0),
    AVIF_CODEC_FLAG_CAN_ENCODE = (1 << 1)
} avifCodecFlags;

// If this returns NULL, the codec choice/flag combination is unavailable
const char * avifCodecName(avifCodecChoice choice, uint32_t requiredFlags);
avifCodecChoice avifCodecChoiceFromName(const char * name);

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

// ---------------------------------------------------------------------------
// avifDecoder

// Useful stats related to a read/write
typedef struct avifIOStats
{
    size_t colorOBUSize;
    size_t alphaOBUSize;
} avifIOStats;

struct avifDecoderData;

typedef enum avifDecoderSource
{
    // If a moov box is present in the .avif(s), use the tracks in it, otherwise decode the primary item.
    AVIF_DECODER_SOURCE_AUTO = 0,

    // Use the primary item and the aux (alpha) item in the avif(s).
    // This is where single-image avifs store their image.
    AVIF_DECODER_SOURCE_PRIMARY_ITEM,

    // Use the chunks inside primary/aux tracks in the moov block.
    // This is where avifs image sequences store their images.
    AVIF_DECODER_SOURCE_TRACKS,

    // Decode the thumbnail item. Currently unimplemented.
    // AVIF_DECODER_SOURCE_THUMBNAIL_ITEM
} avifDecoderSource;

// Information about the timing of a single image in an image sequence
typedef struct avifImageTiming
{
    uint64_t timescale;            // timescale of the media (Hz)
    double pts;                    // presentation timestamp in seconds (ptsInTimescales / timescale)
    uint64_t ptsInTimescales;      // presentation timestamp in "timescales"
    double duration;               // in seconds (durationInTimescales / timescale)
    uint64_t durationInTimescales; // duration in "timescales"
} avifImageTiming;

typedef struct avifDecoder
{
    // Defaults to AVIF_CODEC_CHOICE_AUTO: Preference determined by order in availableCodecs table (avif.c)
    avifCodecChoice codecChoice;

    // avifs can have multiple sets of images in them. This specifies which to decode.
    // Set this via avifDecoderSetSource().
    avifDecoderSource requestedSource;

    // All decoded image data; owned by the decoder. All information in this image is incrementally
    // added and updated as avifDecoder*() functions are called. After a successful call to
    // avifDecoderParse(), all values in decoder->image (other than the planes/rowBytes themselves)
    // will be pre-populated with all information found in the outer AVIF container, prior to any
    // AV1 decoding. If the contents of the inner AV1 payload disagree with the outer container,
    // these values may change after calls to avifDecoderRead(),avifDecoderNextImage(), or
    // avifDecoderNthImage().
    //
    // The YUV and A contents of this image are likely owned by the decoder, so be sure to copy any
    // data inside of this image before advancing to the next image or reusing the decoder. It is
    // legal to call avifImageYUVToRGB() on this in between calls to avifDecoderNextImage(), but use
    // avifImageCopy() if you want to make a complete, permanent copy of this image's YUV content or
    // metadata.
    avifImage * image;

    // Counts and timing for the current image in an image sequence. Uninteresting for single image files.
    int imageIndex;                // 0-based
    int imageCount;                // Always 1 for non-sequences
    avifImageTiming imageTiming;   //
    uint64_t timescale;            // timescale of the media (Hz)
    double duration;               // in seconds (durationInTimescales / timescale)
    uint64_t durationInTimescales; // duration in "timescales"

    // This is true when avifDecoderParse() detects an alpha plane. Use this to find out if alpha is
    // present after a successful call to avifDecoderParse(), but prior to any call to
    // avifDecoderNextImage() or avifDecoderNthImage(), as decoder->image->alphaPlane won't exist yet.
    avifBool alphaPresent;

    // Set this to true to disable support of grid images. If a grid image is encountered, AVIF_RESULT_BMFF_PARSE_FAILED will be returned.
    avifBool disableGridImages;

    // stats from the most recent read, possibly 0s if reading an image sequence
    avifIOStats ioStats;

    // Internals used by the decoder
    struct avifDecoderData * data;
} avifDecoder;

avifDecoder * avifDecoderCreate(void);
void avifDecoderDestroy(avifDecoder * decoder);

// Simple interface to decode a single image, independent of the decoder afterwards (decoder may be deestroyed).
avifResult avifDecoderRead(avifDecoder * decoder, avifImage * image, const avifROData * input);

// Multi-function alternative to avifDecoderRead() for image sequences and gaining direct access
// to the decoder's YUV buffers (for performance's sake). Data passed into avifDecoderParse() is NOT
// copied, so it must continue to exist until the decoder is destroyed.
//
// Usage / function call order is:
// * avifDecoderCreate()
// * avifDecoderSetSource() - optional, the default (AVIF_DECODER_SOURCE_AUTO) is usually sufficient
// * avifDecoderParse()
// * avifDecoderNextImage() - in a loop, using decoder->image after each successful call
// * avifDecoderDestroy()
//
// You can use avifDecoderReset() any time after a successful call to avifDecoderParse()
// to reset the internal decoder back to before the first frame. Calling either
// avifDecoderSetSource() or avifDecoderParse() will automatically Reset the decoder.
//
// avifDecoderSetSource() allows you not only to choose whether to parse tracks or
// items in a file containing both, but switch between sources without having to
// Parse again. Normally AVIF_DECODER_SOURCE_AUTO is enough for the common path.
avifResult avifDecoderSetSource(avifDecoder * decoder, avifDecoderSource source);
avifResult avifDecoderParse(avifDecoder * decoder, const avifROData * input);
avifResult avifDecoderNextImage(avifDecoder * decoder);
avifResult avifDecoderNthImage(avifDecoder * decoder, uint32_t frameIndex);
avifResult avifDecoderReset(avifDecoder * decoder);

// Keyframe information
// frameIndex - 0-based, matching avifDecoder->imageIndex, bound by avifDecoder->imageCount
// "nearest" keyframe means the keyframe prior to this frame index (returns frameIndex if it is a keyframe)
avifBool avifDecoderIsKeyframe(const avifDecoder * decoder, uint32_t frameIndex);
uint32_t avifDecoderNearestKeyframe(const avifDecoder * decoder, uint32_t frameIndex);

// Timing helper - This does not change the current image or invoke the codec (safe to call repeatedly)
// This function may be used after a successful call to avifDecoderParse().
avifResult avifDecoderNthImageTiming(const avifDecoder * decoder, uint32_t frameIndex, avifImageTiming * outTiming);

// ---------------------------------------------------------------------------
// avifEncoder

struct avifEncoderData;
struct avifCodecSpecificOptions;

// Notes:
// * If avifEncoderWrite() returns AVIF_RESULT_OK, output must be freed with avifRWDataFree()
// * If (maxThreads < 2), multithreading is disabled
// * Quality range: [AVIF_QUANTIZER_BEST_QUALITY - AVIF_QUANTIZER_WORST_QUALITY]
// * To enable tiling, set tileRowsLog2 > 0 and/or tileColsLog2 > 0.
//   Tiling values range [0-6], where the value indicates a request for 2^n tiles in that dimension.
// * Speed range: [AVIF_SPEED_SLOWEST - AVIF_SPEED_FASTEST]. Slower should make for a better quality
//   image in less bytes. AVIF_SPEED_DEFAULT means "Leave the AV1 codec to its default speed settings"./
//   If avifEncoder uses rav1e, the speed value is directly passed through (0-10). If libaom is used,
//   a combination of settings are tweaked to simulate this speed range.
typedef struct avifEncoder
{
    // Defaults to AVIF_CODEC_CHOICE_AUTO: Preference determined by order in availableCodecs table (avif.c)
    avifCodecChoice codecChoice;

    // settings (see Notes above)
    int maxThreads;
    int minQuantizer;
    int maxQuantizer;
    int minQuantizerAlpha;
    int maxQuantizerAlpha;
    int tileRowsLog2;
    int tileColsLog2;
    int speed;
    int keyframeInterval; // How many frames between automatic forced keyframes; 0 to disable (default).
    uint64_t timescale;   // timescale of the media (Hz)

    // stats from the most recent write
    avifIOStats ioStats;

    // Internals used by the encoder
    struct avifEncoderData * data;
    struct avifCodecSpecificOptions * csOptions;
} avifEncoder;

avifEncoder * avifEncoderCreate(void);
avifResult avifEncoderWrite(avifEncoder * encoder, const avifImage * image, avifRWData * output);
void avifEncoderDestroy(avifEncoder * encoder);

enum avifAddImageFlags
{
    AVIF_ADD_IMAGE_FLAG_NONE = 0,

    // Force this frame to be a keyframe (sync frame).
    AVIF_ADD_IMAGE_FLAG_FORCE_KEYFRAME = (1 << 0),

    // Use this flag when encoding a single image. Signals "still_picture" to AV1 encoders, which
    // tweaks various compression rules. This is enabled automatically when using the
    // avifEncoderWrite() single-image encode path.
    AVIF_ADD_IMAGE_FLAG_SINGLE = (1 << 1)
};

// Multi-function alternative to avifEncoderWrite() for image sequences.
//
// Usage / function call order is:
// * avifEncoderCreate()
// * Set encoder->timescale (Hz) correctly
// * avifEncoderAddImage() ... [repeatedly; at least once]
// * avifEncoderFinish()
// * avifEncoderDestroy()
//
avifResult avifEncoderAddImage(avifEncoder * encoder, const avifImage * image, uint64_t durationInTimescales, uint32_t addImageFlags);
avifResult avifEncoderFinish(avifEncoder * encoder, avifRWData * output);

// Codec-specific, optional "advanced" tuning settings, in the form of string key/value pairs. These
// should be set as early as possible, preferably just after creating avifEncoder but before
// performing any other actions.
// key must be non-NULL, but passing a NULL value will delete that key, if it exists.
// Setting an incorrect or unknown option for the current codec will cause errors of type
// AVIF_RESULT_INVALID_CODEC_SPECIFIC_OPTION from avifEncoderWrite() or avifEncoderAddImage().
void avifEncoderSetCodecSpecificOption(avifEncoder * encoder, const char * key, const char * value);

// Helpers
avifBool avifImageUsesU16(const avifImage * image);

// Returns AVIF_TRUE if input begins with a valid FileTypeBox (ftyp) that supports
// either the brand 'avif' or 'avis' (or both), without performing any allocations.
avifBool avifPeekCompatibleFileType(const avifROData * input);

#ifdef __cplusplus
} // extern "C"
#endif

#endif // ifndef AVIF_AVIF_H
