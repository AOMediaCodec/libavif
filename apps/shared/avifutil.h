// Copyright 2019 Joe Drago. All rights reserved.
// SPDX-License-Identifier: BSD-2-Clause

#ifndef LIBAVIF_APPS_SHARED_AVIFUTIL_H
#define LIBAVIF_APPS_SHARED_AVIFUTIL_H

#include "avif/avif.h"

#ifdef __cplusplus
extern "C" {
#endif

// MAIN(), INIT_ARGV(), FREE_ARGV() for UTF8-aware command line parsing.
#if defined(_WIN32)
#define MAIN() int wmain(int argc, wchar_t * wargv[])
#else
#define MAIN() int main(int argc, char * argv[])
#endif

#if defined(_WIN32)
#ifdef __cplusplus
#define INIT_ARGV()                                                                                           \
    if (setlocale(LC_ALL, ".UTF8") == NULL) {                                                                 \
        fprintf(stderr, "setlocale failed\n");                                                                \
        return 1;                                                                                             \
    }                                                                                                         \
    std::vector<char> argvAllVector(1024 * argc);                                                             \
    std::vector<char *> argvVector(argc);                                                                     \
    char ** argv = argvVector.data();                                                                         \
    for (int i = 0; i < argc; ++i) {                                                                          \
        argvVector[i] = &argvAllVector[1024 * i];                                                             \
        int rc = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, wargv[i], -1, argv[i], 1024, NULL, NULL); \
        if (rc == 0) {                                                                                        \
            fprintf(stderr, "WideCharToMultiByte() failed\n");                                                \
            return 1;                                                                                         \
        }                                                                                                     \
    }
#else
#define INIT_ARGV()                                                                                           \
    char * argvAll = NULL;                                                                                    \
    char ** argv = NULL;                                                                                      \
    if (setlocale(LC_ALL, ".UTF8") == NULL) {                                                                 \
        fprintf(stderr, "setlocale failed\n");                                                                \
        return 1;                                                                                             \
    }                                                                                                         \
    argvAll = (char *)malloc(1024 * argc * sizeof(*argvAll));                                                 \
    argv = (char **)malloc(argc * sizeof(*argv));                                                             \
    if (argv == NULL || argvAll == NULL) {                                                                    \
        FREE_ARGV()                                                                                           \
        return 1;                                                                                             \
    }                                                                                                         \
    for (int i = 0; i < argc; ++i) {                                                                          \
        argv[i] = argvAll + 1024 * i;                                                                         \
        int rc = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, wargv[i], -1, argv[i], 1024, NULL, NULL); \
        if (rc == 0) {                                                                                        \
            fprintf(stderr, "WideCharToMultiByte() failed\n");                                                \
            FREE_ARGV()                                                                                       \
            return 1;                                                                                         \
        }                                                                                                     \
    }
#endif // __cplusplus
#else
#define INIT_ARGV()
#endif
#if defined(_WIN32) && !defined(__cplusplus)
#define FREE_ARGV() \
    free(argv);     \
    free(argvAll);
#else
#define FREE_ARGV()
#endif

// The %z format specifier is not available in the old Windows CRT msvcrt,
// hence the %I format specifier must be used instead to print out `size_t`.
// The new Windows CRT UCRT, which is used by Visual Studio 2015 or later,
// supports the %z specifier properly.
//
// Additionally, with c99 set as the standard mingw-w64 toolchains built with
// the commit mentioned can patch format functions to support the %z specifier,
// even if it's using the old msvcrt, and this can be detected by
// the `__USE_MINGW_ANSI_STDIO` macro.
//
// Related mingw-w64 commit: bfd33f6c0ec5e652cc9911857dd1492ece8d8383

#if !defined(_UCRT) && (defined(__USE_MINGW_ANSI_STDIO) && __USE_MINGW_ANSI_STDIO == 0)
#define AVIF_FMT_ZU "Iu"
#else
#define AVIF_FMT_ZU "zu"
#endif

void avifImageDump(const avifImage * avif, uint32_t gridCols, uint32_t gridRows, avifBool gainMapPresent, avifProgressiveState progressiveState);
void avifContainerDump(const avifDecoder * decoder);
void avifPrintVersions(void);
void avifDumpDiagnostics(const avifDiagnostics * diag);
int avifQueryCPUCount(void); // Returns 1 if it cannot query or fails to query

typedef enum avifAppFileFormat
{
    AVIF_APP_FILE_FORMAT_UNKNOWN = 0,

    AVIF_APP_FILE_FORMAT_AVIF,
    AVIF_APP_FILE_FORMAT_JPEG,
    AVIF_APP_FILE_FORMAT_PNG,
    AVIF_APP_FILE_FORMAT_Y4M
} avifAppFileFormat;

// Guesses the format of a file by looking at the first bytes, or at the extension if the file
// can't be read or is empty.
avifAppFileFormat avifGuessFileFormat(const char * filename);
// Guesses the format of a buffer by looking at the first bytes.
avifAppFileFormat avifGuessBufferFileFormat(const uint8_t * data, size_t size);

// This structure holds any timing data coming from source (typically non-AVIF) inputs being fed
// into avifenc. If either or both values are 0, the timing is "invalid" / sentinel and the values
// should be ignored. This structure is used to override the timing defaults in avifenc when the
// enduser doesn't provide timing on the commandline and the source content provides a framerate.
typedef struct avifAppSourceTiming
{
    uint64_t duration;  // duration in time units (based on the timescale below)
    uint64_t timescale; // timescale of the media (Hz)
} avifAppSourceTiming;

struct y4mFrameIterator;
// Reads an image from a file with the requested format and depth.
// In case of a y4m file, sourceTiming and frameIter can be set.
// Returns AVIF_APP_FILE_FORMAT_UNKNOWN in case of error.
// 'ignoreGainMap' is only relevant for jpeg files that have a gain map
// and only if AVIF_ENABLE_EXPERIMENTAL_JPEG_GAIN_MAP_CONVERSION is ON
// (requires AVIF_ENABLE_EXPERIMENTAL_GAIN_MAP and libxml2). Otherwise
// it has no effect.
avifAppFileFormat avifReadImage(const char * filename,
                                avifPixelFormat requestedFormat,
                                int requestedDepth,
                                avifChromaDownsampling chromaDownsampling,
                                avifBool ignoreColorProfile,
                                avifBool ignoreExif,
                                avifBool ignoreXMP,
                                avifBool allowChangingCicp,
                                avifBool ignoreGainMap,
                                avifImage * image,
                                uint32_t * outDepth,
                                avifAppSourceTiming * sourceTiming,
                                struct y4mFrameIterator ** frameIter);

// Removes a single trailing null character from the image->xmp, if there is exactly one.
void avifImageFixXMP(avifImage * image);

// Used by image decoders when the user doesn't explicitly choose a format with --yuv
// This must match the cited fallback for "--yuv auto" in avifenc.c's syntax() function.
#define AVIF_APP_DEFAULT_PIXEL_FORMAT AVIF_PIXEL_FORMAT_YUV444

#ifdef __cplusplus
} // extern "C"
#endif

#endif // ifndef LIBAVIF_APPS_SHARED_AVIFUTIL_H
