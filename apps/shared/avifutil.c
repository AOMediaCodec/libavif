// Copyright 2019 Joe Drago. All rights reserved.
// SPDX-License-Identifier: BSD-2-Clause

#include "avifutil.h"

#include <assert.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "avifjpeg.h"
#include "avifpng.h"
#include "y4m.h"

char * avifFileFormatToString(avifAppFileFormat format)
{
    switch (format) {
        case AVIF_APP_FILE_FORMAT_UNKNOWN:
            return "unknown";
        case AVIF_APP_FILE_FORMAT_AVIF:
            return "AVIF";
        case AVIF_APP_FILE_FORMAT_JPEG:
            return "JPEG";
        case AVIF_APP_FILE_FORMAT_PNG:
            return "PNG";
        case AVIF_APP_FILE_FORMAT_Y4M:
            return "Y4M";
    }
    return "unknown";
}

// |a| and |b| hold int32_t values. The int64_t type is used so that we can negate INT32_MIN without
// overflowing int32_t.
static int64_t calcGCD(int64_t a, int64_t b)
{
    if (a < 0) {
        a *= -1;
    }
    if (b < 0) {
        b *= -1;
    }
    while (b != 0) {
        int64_t r = a % b;
        a = b;
        b = r;
    }
    return a;
}

static void printClapFraction(const char * name, int32_t n, int32_t d)
{
    printf("%s: %d/%d", name, n, d);
    if (d != 0) {
        int64_t gcd = calcGCD(n, d);
        if (gcd > 1) {
            int32_t rn = (int32_t)(n / gcd);
            int32_t rd = (int32_t)(d / gcd);
            printf(" (%d/%d)", rn, rd);
        }
    }
}

static void avifImageDumpInternal(const avifImage * avif, uint32_t gridCols, uint32_t gridRows, avifBool alphaPresent, avifProgressiveState progressiveState)
{
    uint32_t width = avif->width;
    uint32_t height = avif->height;
    if (gridCols && gridRows) {
        width *= gridCols;
        height *= gridRows;
    }
    printf(" * Resolution     : %ux%u\n", width, height);
    printf(" * Bit Depth      : %u\n", avif->depth);
    printf(" * Format         : %s\n", avifPixelFormatToString(avif->yuvFormat));
    if (avif->yuvFormat == AVIF_PIXEL_FORMAT_YUV420) {
        printf(" * Chroma Sam. Pos: %u\n", avif->yuvChromaSamplePosition);
    }
    printf(" * Alpha          : %s\n", alphaPresent ? (avif->alphaPremultiplied ? "Premultiplied" : "Not premultiplied") : "Absent");
    printf(" * Range          : %s\n", (avif->yuvRange == AVIF_RANGE_FULL) ? "Full" : "Limited");

    printf(" * Color Primaries: %u\n", avif->colorPrimaries);
    printf(" * Transfer Char. : %u\n", avif->transferCharacteristics);
    printf(" * Matrix Coeffs. : %u\n", avif->matrixCoefficients);

    if (avif->icc.size != 0) {
        printf(" * ICC Profile    : Present (%" AVIF_FMT_ZU " bytes)\n", avif->icc.size);
    } else {
        printf(" * ICC Profile    : Absent\n");
    }
    if (avif->xmp.size != 0) {
        printf(" * XMP Metadata   : Present (%" AVIF_FMT_ZU " bytes)\n", avif->xmp.size);
    } else {
        printf(" * XMP Metadata   : Absent\n");
    }
    if (avif->exif.size != 0) {
        printf(" * Exif Metadata  : Present (%" AVIF_FMT_ZU " bytes)\n", avif->exif.size);
    } else {
        printf(" * Exif Metadata  : Absent\n");
    }

    if (avif->transformFlags == AVIF_TRANSFORM_NONE) {
        printf(" * Transformations: None\n");
    } else {
        printf(" * Transformations:\n");

        if (avif->transformFlags & AVIF_TRANSFORM_PASP) {
            printf("    * pasp (Aspect Ratio)  : %d/%d\n", (int)avif->pasp.hSpacing, (int)avif->pasp.vSpacing);
        }
        if (avif->transformFlags & AVIF_TRANSFORM_CLAP) {
            printf("    * clap (Clean Aperture): ");
            printClapFraction("W", (int32_t)avif->clap.widthN, (int32_t)avif->clap.widthD);
            printf(", ");
            printClapFraction("H", (int32_t)avif->clap.heightN, (int32_t)avif->clap.heightD);
            printf(", ");
            printClapFraction("hOff", (int32_t)avif->clap.horizOffN, (int32_t)avif->clap.horizOffD);
            printf(", ");
            printClapFraction("vOff", (int32_t)avif->clap.vertOffN, (int32_t)avif->clap.vertOffD);
            printf("\n");

            avifCropRect cropRect;
            avifDiagnostics diag;
            avifDiagnosticsClearError(&diag);
            avifBool validClap = avifCropRectFromCleanApertureBox(&cropRect, &avif->clap, avif->width, avif->height, &diag);
            if (validClap) {
                printf("      * Valid, derived crop rect: X: %d, Y: %d, W: %d, H: %d%s\n",
                       cropRect.x,
                       cropRect.y,
                       cropRect.width,
                       cropRect.height,
                       avifCropRectRequiresUpsampling(&cropRect, avif->yuvFormat) ? " (upsample before cropping)" : "");
            } else {
                printf("      * Invalid: %s\n", diag.error);
            }
        }
        if (avif->transformFlags & AVIF_TRANSFORM_IROT) {
            printf("    * irot (Rotation)      : %u\n", avif->irot.angle);
        }
        if (avif->transformFlags & AVIF_TRANSFORM_IMIR) {
            printf("    * imir (Mirror)        : %u (%s)\n", avif->imir.axis, (avif->imir.axis == 0) ? "top-to-bottom" : "left-to-right");
        }
    }
    printf(" * Progressive    : %s\n", avifProgressiveStateToString(progressiveState));
    if (avif->clli.maxCLL > 0 || avif->clli.maxPALL > 0) {
        printf(" * CLLI           : %hu, %hu\n", avif->clli.maxCLL, avif->clli.maxPALL);
    }

    printf(" * Gain map       : ");
    avifImage * gainMapImage = avif->gainMap ? avif->gainMap->image : NULL;
    if (gainMapImage != NULL) {
        printf("%ux%u pixels, %u bit, %s, %s Range, Matrix Coeffs. %u, Base Headroom %.2f (%s), Alternate Headroom %.2f (%s)\n",
               gainMapImage->width,
               gainMapImage->height,
               gainMapImage->depth,
               avifPixelFormatToString(gainMapImage->yuvFormat),
               (gainMapImage->yuvRange == AVIF_RANGE_FULL) ? "Full" : "Limited",
               gainMapImage->matrixCoefficients,
               avif->gainMap->baseHdrHeadroom.d == 0 ? 0
                                                     : (double)avif->gainMap->baseHdrHeadroom.n / avif->gainMap->baseHdrHeadroom.d,
               (avif->gainMap->baseHdrHeadroom.n == 0) ? "SDR" : "HDR",
               avif->gainMap->alternateHdrHeadroom.d == 0
                   ? 0
                   : (double)avif->gainMap->alternateHdrHeadroom.n / avif->gainMap->alternateHdrHeadroom.d,
               (avif->gainMap->alternateHdrHeadroom.n == 0) ? "SDR" : "HDR");
        printf(" * Alternate image:\n");
        printf("    * Color Primaries: %u\n", avif->gainMap->altColorPrimaries);
        printf("    * Transfer Char. : %u\n", avif->gainMap->altTransferCharacteristics);
        printf("    * Matrix Coeffs. : %u\n", avif->gainMap->altMatrixCoefficients);
        if (avif->gainMap->altICC.size != 0) {
            printf("    * ICC Profile    : Present (%" AVIF_FMT_ZU " bytes)\n", avif->gainMap->altICC.size);
        } else {
            printf("    * ICC Profile    : Absent\n");
        }
        if (avif->gainMap->altDepth) {
            printf("    * Bit Depth      : %u\n", avif->gainMap->altDepth);
        }
        if (avif->gainMap->altPlaneCount) {
            printf("    * Planes         : %u\n", avif->gainMap->altPlaneCount);
        }
        if (avif->gainMap->altCLLI.maxCLL > 0 || avif->gainMap->altCLLI.maxPALL > 0) {
            printf("    * CLLI           : %hu, %hu\n", avif->gainMap->altCLLI.maxCLL, avif->gainMap->altCLLI.maxPALL);
        }
        printf("\n");
    } else if (avif->gainMap != NULL) {
        printf("Present (but ignored)\n");
    } else {
        printf("Absent\n");
    }
}

void avifImageDump(const avifImage * avif, uint32_t gridCols, uint32_t gridRows, avifProgressiveState progressiveState)
{
    const avifBool alphaPresent = avif->alphaPlane && (avif->alphaRowBytes > 0);
    avifImageDumpInternal(avif, gridCols, gridRows, alphaPresent, progressiveState);
}

void avifContainerDump(const avifDecoder * decoder)
{
    avifImageDumpInternal(decoder->image, 0, 0, decoder->alphaPresent, decoder->progressiveState);
    if (decoder->imageSequenceTrackPresent) {
        if (decoder->repetitionCount == AVIF_REPETITION_COUNT_INFINITE) {
            printf(" * Repeat Count   : Infinite\n");
        } else if (decoder->repetitionCount == AVIF_REPETITION_COUNT_UNKNOWN) {
            printf(" * Repeat Count   : Unknown\n");
        } else {
            printf(" * Repeat Count   : %d\n", decoder->repetitionCount);
        }
    }
}

void avifPrintVersions(void)
{
    char codecVersions[256];
    avifCodecVersions(codecVersions);
    printf("Version: %s (%s)\n", avifVersion(), codecVersions);

    unsigned int libyuvVersion = avifLibYUVVersion();
    if (libyuvVersion == 0) {
        printf("libyuv : unavailable\n");
    } else {
        printf("libyuv : available (%u)\n", libyuvVersion);
    }

    printf("\n");
}

avifAppFileFormat avifGuessFileFormat(const char * filename)
{
    // Guess from the file header
    FILE * f = fopen(filename, "rb");
    if (f) {
        uint8_t headerBuffer[144];
        size_t bytesRead = fread(headerBuffer, 1, sizeof(headerBuffer), f);
        fclose(f);

        if (bytesRead > 0) {
            // If the file could be read, use the first bytes to guess the file format.
            return avifGuessBufferFileFormat(headerBuffer, bytesRead);
        }
    }

    // If we get here, the file header couldn't be read for some reason. Guess from the extension.

    const char * fileExt = strrchr(filename, '.');
    if (!fileExt) {
        return AVIF_APP_FILE_FORMAT_UNKNOWN;
    }
    ++fileExt; // skip past the dot

    char lowercaseExt[8]; // This only needs to fit up to "jpeg", so this is plenty
    const size_t fileExtLen = strlen(fileExt);
    if (fileExtLen >= sizeof(lowercaseExt)) { // >= accounts for NULL terminator
        return AVIF_APP_FILE_FORMAT_UNKNOWN;
    }

    for (size_t i = 0; i < fileExtLen; ++i) {
        lowercaseExt[i] = (char)tolower((unsigned char)fileExt[i]);
    }
    lowercaseExt[fileExtLen] = 0;

    if (!strcmp(lowercaseExt, "avif")) {
        return AVIF_APP_FILE_FORMAT_AVIF;
    } else if (!strcmp(lowercaseExt, "y4m")) {
        return AVIF_APP_FILE_FORMAT_Y4M;
    } else if (!strcmp(lowercaseExt, "jpg") || !strcmp(lowercaseExt, "jpeg")) {
        return AVIF_APP_FILE_FORMAT_JPEG;
    } else if (!strcmp(lowercaseExt, "png")) {
        return AVIF_APP_FILE_FORMAT_PNG;
    }
    return AVIF_APP_FILE_FORMAT_UNKNOWN;
}

avifAppFileFormat avifGuessBufferFileFormat(const uint8_t * data, size_t size)
{
    if (size == 0) {
        return AVIF_APP_FILE_FORMAT_UNKNOWN;
    }

    avifROData header;
    header.data = data;
    header.size = size;

    if (avifPeekCompatibleFileType(&header)) {
        return AVIF_APP_FILE_FORMAT_AVIF;
    }

    static const uint8_t signatureJPEG[2] = { 0xFF, 0xD8 };
    static const uint8_t signaturePNG[8] = { 0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A };
    static const uint8_t signatureY4M[9] = { 0x59, 0x55, 0x56, 0x34, 0x4D, 0x50, 0x45, 0x47, 0x32 }; // "YUV4MPEG2"
    struct avifHeaderSignature
    {
        avifAppFileFormat format;
        const uint8_t * magic;
        size_t magicSize;
    } signatures[] = { { AVIF_APP_FILE_FORMAT_JPEG, signatureJPEG, sizeof(signatureJPEG) },
                       { AVIF_APP_FILE_FORMAT_PNG, signaturePNG, sizeof(signaturePNG) },
                       { AVIF_APP_FILE_FORMAT_Y4M, signatureY4M, sizeof(signatureY4M) } };
    const size_t signaturesCount = sizeof(signatures) / sizeof(signatures[0]);

    for (size_t signatureIndex = 0; signatureIndex < signaturesCount; ++signatureIndex) {
        const struct avifHeaderSignature * const signature = &signatures[signatureIndex];
        if (header.size < signature->magicSize) {
            continue;
        }
        if (!memcmp(header.data, signature->magic, signature->magicSize)) {
            return signature->format;
        }
    }

    return AVIF_APP_FILE_FORMAT_UNKNOWN;
}

avifAppFileFormat avifReadImage(const char * filename,
                                avifAppFileFormat inputFormat,
                                avifPixelFormat requestedFormat,
                                int requestedDepth,
                                avifChromaDownsampling chromaDownsampling,
                                avifBool ignoreColorProfile,
                                avifBool ignoreExif,
                                avifBool ignoreXMP,
                                avifBool ignoreGainMap,
                                uint32_t imageSizeLimit,
                                avifImage * image,
                                uint32_t * outDepth,
                                avifAppSourceTiming * sourceTiming,
                                struct y4mFrameIterator ** frameIter)
{
    if (inputFormat == AVIF_APP_FILE_FORMAT_UNKNOWN) {
        inputFormat = avifGuessFileFormat(filename);
    }

    if (inputFormat == AVIF_APP_FILE_FORMAT_Y4M) {
        if (!y4mRead(filename, imageSizeLimit, image, sourceTiming, frameIter)) {
            return AVIF_APP_FILE_FORMAT_UNKNOWN;
        }
        if (outDepth) {
            *outDepth = image->depth;
        }
    } else if (inputFormat == AVIF_APP_FILE_FORMAT_JPEG) {
        // imageSizeLimit is also used to limit Exif and XMP metadata here.
        if (!avifJPEGRead(filename, image, requestedFormat, requestedDepth, chromaDownsampling, ignoreColorProfile, ignoreExif, ignoreXMP, ignoreGainMap, imageSizeLimit)) {
            return AVIF_APP_FILE_FORMAT_UNKNOWN;
        }
        if (outDepth) {
            *outDepth = 8;
        }
    } else if (inputFormat == AVIF_APP_FILE_FORMAT_PNG) {
        if (!avifPNGRead(filename, image, requestedFormat, requestedDepth, chromaDownsampling, ignoreColorProfile, ignoreExif, ignoreXMP, imageSizeLimit, outDepth)) {
            return AVIF_APP_FILE_FORMAT_UNKNOWN;
        }
    } else if (inputFormat == AVIF_APP_FILE_FORMAT_UNKNOWN) {
        fprintf(stderr, "Unrecognized file format for input file: %s\n", filename);
        return AVIF_APP_FILE_FORMAT_UNKNOWN;
    } else {
        fprintf(stderr, "Unsupported file format %s for input file: %s\n", avifFileFormatToString(inputFormat), filename);
        return AVIF_APP_FILE_FORMAT_UNKNOWN;
    }
    return inputFormat;
}

avifBool avifReadEntireFile(const char * filename, avifRWData * raw)
{
    FILE * f = fopen(filename, "rb");
    if (!f) {
        return AVIF_FALSE;
    }

    fseek(f, 0, SEEK_END);
    long pos = ftell(f);
    if (pos <= 0) {
        fclose(f);
        return AVIF_FALSE;
    }
    size_t fileSize = (size_t)pos;
    fseek(f, 0, SEEK_SET);

    if (avifRWDataRealloc(raw, fileSize) != AVIF_RESULT_OK) {
        fclose(f);
        return AVIF_FALSE;
    }
    size_t bytesRead = fread(raw->data, 1, fileSize, f);
    fclose(f);

    if (bytesRead != fileSize) {
        avifRWDataFree(raw);
        return AVIF_FALSE;
    }
    return AVIF_TRUE;
}

void avifImageFixXMP(avifImage * image)
{
    // Zero bytes are forbidden in UTF-8 XML: https://en.wikipedia.org/wiki/Valid_characters_in_XML
    // Keeping zero bytes in XMP may lead to issues at encoding or decoding.
    // For example, the PNG specification forbids null characters in XMP. See avifPNGWrite().
    // The XMP Specification Part 3 says "When XMP is encoded as UTF-8,
    // there are no zero bytes in the XMP packet" for GIF.

    // Consider a single trailing null character following a non-null character
    // as a programming error. Leave other null characters as is.
    // See the discussion at https://github.com/AOMediaCodec/libavif/issues/1333.
    if (image->xmp.size >= 2 && image->xmp.data[image->xmp.size - 1] == '\0' && image->xmp.data[image->xmp.size - 2] != '\0') {
        --image->xmp.size;
    }
}

void avifDumpDiagnostics(const avifDiagnostics * diag)
{
    if (!*diag->error) {
        return;
    }

    printf("Diagnostics:\n");
    printf(" * %s\n", diag->error);
}

// ---------------------------------------------------------------------------
// avifQueryCPUCount (separated into OS implementations)

#if defined(_WIN32)

// Windows

#include <windows.h>

int avifQueryCPUCount(void)
{
    int numCPU;
    SYSTEM_INFO sysinfo;
    GetSystemInfo(&sysinfo);
    numCPU = sysinfo.dwNumberOfProcessors;
    return numCPU;
}

#elif defined(__APPLE__)

// Apple

#include <sys/sysctl.h>

int avifQueryCPUCount(void)
{
    int mib[4];
    int numCPU;
    size_t len = sizeof(numCPU);

    /* set the mib for hw.ncpu */
    mib[0] = CTL_HW;
    mib[1] = HW_AVAILCPU; // alternatively, try HW_NCPU;

    /* get the number of CPUs from the system */
    sysctl(mib, 2, &numCPU, &len, NULL, 0);

    if (numCPU < 1) {
        mib[1] = HW_NCPU;
        sysctl(mib, 2, &numCPU, &len, NULL, 0);
        if (numCPU < 1)
            numCPU = 1;
    }
    return numCPU;
}

#elif defined(__EMSCRIPTEN__)

// Emscripten

int avifQueryCPUCount(void)
{
    return 1;
}

#else

// POSIX

#include <unistd.h>

int avifQueryCPUCount(void)
{
    int numCPU = (int)sysconf(_SC_NPROCESSORS_ONLN);
    return (numCPU > 0) ? numCPU : 1;
}

#endif

// Returns the best cell size for a given horizontal or vertical dimension.
avifBool avifGetBestCellSize(const char * dimensionStr, uint32_t numPixels, uint32_t numCells, avifBool isSubsampled, uint32_t * cellSize)
{
    assert(numPixels);
    assert(numCells);

    // ISO/IEC 23008-12:2017, Section 6.6.2.3.1:
    //   The reconstructed image is formed by tiling the input images into a grid with a column width
    //   (potentially excluding the right-most column) equal to tile_width and a row height (potentially
    //   excluding the bottom-most row) equal to tile_height, without gap or overlap, and then
    //   trimming on the right and the bottom to the indicated output_width and output_height.
    // The priority could be to use a cell size that is a multiple of 64, but there is not always a valid one,
    // even though it is recommended by MIAF. Just use ceil(numPixels/numCells) for simplicity and to avoid
    // as much padding in the right-most and bottom-most cells as possible.
    // Use uint64_t computation to avoid a potential uint32_t overflow.
    *cellSize = (uint32_t)(((uint64_t)numPixels + numCells - 1) / numCells);

    // ISO/IEC 23000-22:2019, Section 7.3.11.4.2:
    //   - the tile_width shall be greater than or equal to 64, and should be a multiple of 64
    //   - the tile_height shall be greater than or equal to 64, and should be a multiple of 64
    if (*cellSize < 64) {
        *cellSize = 64;
        if ((uint64_t)(numCells - 1) * *cellSize >= (uint64_t)numPixels) {
            // Some cells would be entirely off-canvas.
            fprintf(stderr, "ERROR: There are too many cells %s (%u) to have at least 64 pixels per cell.\n", dimensionStr, numCells);
            return AVIF_FALSE;
        }
    }

    // The maximum AV1 frame size is 65536 pixels inclusive.
    if (*cellSize > 65536) {
        fprintf(stderr, "ERROR: Cell size %u is bigger %s than the maximum frame size 65536.\n", *cellSize, dimensionStr);
        return AVIF_FALSE;
    }

    // ISO/IEC 23000-22:2019, Section 7.3.11.4.2:
    //   - when the images are in the 4:2:2 chroma sampling format the horizontal tile offsets and widths,
    //     and the output width, shall be even numbers;
    //   - when the images are in the 4:2:0 chroma sampling format both the horizontal and vertical tile
    //     offsets and widths, and the output width and height, shall be even numbers.
    if (isSubsampled && (*cellSize & 1)) {
        ++*cellSize;
        if ((uint64_t)(numCells - 1) * *cellSize >= (uint64_t)numPixels) {
            // Some cells would be entirely off-canvas.
            fprintf(stderr, "ERROR: Odd cell size %u is forbidden on a %s subsampled image.\n", *cellSize - 1, dimensionStr);
            return AVIF_FALSE;
        }
    }

    // Each pixel is covered by exactly one cell, and each cell contains at least one pixel.
    assert(((uint64_t)(numCells - 1) * *cellSize < (uint64_t)numPixels) && ((uint64_t)numCells * *cellSize >= (uint64_t)numPixels));
    return AVIF_TRUE;
}

avifBool avifImageSplitGrid(const avifImage * gridSplitImage, uint32_t gridCols, uint32_t gridRows, avifImage ** gridCells)
{
    uint32_t cellWidth, cellHeight;
    avifPixelFormatInfo formatInfo;
    avifGetPixelFormatInfo(gridSplitImage->yuvFormat, &formatInfo);
    const avifBool isSubsampledX = !formatInfo.monochrome && formatInfo.chromaShiftX;
    const avifBool isSubsampledY = !formatInfo.monochrome && formatInfo.chromaShiftY;
    if (!avifGetBestCellSize("horizontally", gridSplitImage->width, gridCols, isSubsampledX, &cellWidth) ||
        !avifGetBestCellSize("vertically", gridSplitImage->height, gridRows, isSubsampledY, &cellHeight)) {
        return AVIF_FALSE;
    }
    const avifBool hasGainMap = gridSplitImage->gainMap && gridSplitImage->gainMap->image;

    for (uint32_t gridY = 0; gridY < gridRows; ++gridY) {
        for (uint32_t gridX = 0; gridX < gridCols; ++gridX) {
            uint32_t gridIndex = gridX + (gridY * gridCols);
            avifImage * cellImage = avifImageCreateEmpty();
            if (!cellImage) {
                fprintf(stderr, "ERROR: Cell creation failed: out of memory\n");
                return AVIF_FALSE;
            }
            gridCells[gridIndex] = cellImage;

            avifCropRect cellRect = { gridX * cellWidth, gridY * cellHeight, cellWidth, cellHeight };
            if (cellRect.x + cellRect.width > gridSplitImage->width) {
                cellRect.width = gridSplitImage->width - cellRect.x;
            }
            if (cellRect.y + cellRect.height > gridSplitImage->height) {
                cellRect.height = gridSplitImage->height - cellRect.y;
            }
            const avifResult copyResult = avifImageSetViewRect(cellImage, gridSplitImage, &cellRect);
            if (copyResult != AVIF_RESULT_OK) {
                fprintf(stderr, "ERROR: Cell creation failed: %s\n", avifResultToString(copyResult));
                return AVIF_FALSE;
            }

            if (hasGainMap) {
                cellImage->gainMap = avifGainMapCreate();
                if (!cellImage->gainMap) {
                    fprintf(stderr, "ERROR: Gain map creation failed: out of memory\n");
                    return AVIF_FALSE;
                }
                // Copy gain map metadata.
                memcpy(cellImage->gainMap, gridSplitImage->gainMap, sizeof(avifGainMap));
                cellImage->gainMap->altICC.data = NULL; // Copied later in this function.
                cellImage->gainMap->altICC.size = 0;
                cellImage->gainMap->image = NULL; // Set later in this function.
            }
        }
    }

    if (hasGainMap) {
        avifImage ** gainMapGridCells = NULL;
        gainMapGridCells = (avifImage **)calloc(gridCols * gridRows, sizeof(avifImage *));
        if (!gainMapGridCells) {
            fprintf(stderr, "ERROR: Memory allocation failed for gain map grid cells\n");
            return AVIF_FALSE;
        }
        if (!avifImageSplitGrid(gridSplitImage->gainMap->image, gridCols, gridRows, gainMapGridCells)) {
            for (uint32_t i = 0; i < gridCols * gridRows; ++i) {
                if (gainMapGridCells[i]) {
                    avifImageDestroy(gainMapGridCells[i]);
                }
            }
            free(gainMapGridCells);
            return AVIF_FALSE;
        }

        for (uint32_t gridIndex = 0; gridIndex < gridCols * gridRows; ++gridIndex) {
            // Ownership of the gain map cell is transferred.
            gridCells[gridIndex]->gainMap->image = gainMapGridCells[gridIndex];
        }
        free(gainMapGridCells);
    }

    // Copy over metadata blobs to the first cell since avifImageSetViewRect() does not copy any
    // properties that require an allocation.
    avifImage * firstCell = gridCells[0];
    if (gridSplitImage->icc.size > 0) {
        const avifResult result = avifImageSetProfileICC(firstCell, gridSplitImage->icc.data, gridSplitImage->icc.size);
        if (result != AVIF_RESULT_OK) {
            fprintf(stderr, "ERROR: Failed to set ICC profile on grid cell: %s\n", avifResultToString(result));
            return AVIF_FALSE;
        }
    }
    if (gridSplitImage->exif.size > 0) {
        const avifResult result = avifImageSetMetadataExif(firstCell, gridSplitImage->exif.data, gridSplitImage->exif.size);
        if (result != AVIF_RESULT_OK) {
            fprintf(stderr, "ERROR: Failed to set Exif metadata on grid cell: %s\n", avifResultToString(result));
            return AVIF_FALSE;
        }
    }
    if (gridSplitImage->xmp.size > 0) {
        const avifResult result = avifImageSetMetadataXMP(firstCell, gridSplitImage->xmp.data, gridSplitImage->xmp.size);
        if (result != AVIF_RESULT_OK) {
            fprintf(stderr, "ERROR: Failed to set XMP metadata on grid cell: %s\n", avifResultToString(result));
            return AVIF_FALSE;
        }
    }
    if (gridSplitImage->gainMap && gridSplitImage->gainMap->image && gridSplitImage->gainMap->altICC.size > 0) {
        for (uint32_t i = 0; i < gridCols * gridRows; ++i) {
            avifImage * cellImage = gridCells[i];
            const avifResult result =
                avifRWDataSet(&cellImage->gainMap->altICC, gridSplitImage->gainMap->altICC.data, gridSplitImage->gainMap->altICC.size);
            if (result != AVIF_RESULT_OK) {
                fprintf(stderr, "ERROR: Failed to set ICC profile on gain map grid cell: %s\n", avifResultToString(result));
                return AVIF_FALSE;
            }
        }
    }

    return AVIF_TRUE;
}

void avifRGBImageSetViewRect(avifRGBImage * dstImage, const avifRGBImage * srcImage, const avifCropRect * cropRect)
{
    memset(dstImage, 0, sizeof(avifRGBImage));
    dstImage->width = cropRect->width;
    dstImage->height = cropRect->height;
    dstImage->depth = srcImage->depth;
    dstImage->format = srcImage->format;
    dstImage->alphaPremultiplied = srcImage->alphaPremultiplied;
    dstImage->isFloat = srcImage->isFloat;
    const uint32_t bytesPerPixel = avifRGBImagePixelSize(srcImage);
    // This should not overflow if cropRect is a valid crop of the image.
    const size_t offset = (size_t)cropRect->y * srcImage->rowBytes + (size_t)cropRect->x * bytesPerPixel;
    dstImage->pixels = srcImage->pixels + offset;
    dstImage->rowBytes = srcImage->rowBytes;
}

// NOTE: this saves the rotated pixels to a different image. Rotating an image in place is possible, but can be non trivial depending on the angle.
// A 90° rotation can be implemented as a transposition operation followed by mirroring.
// It's the transposition step that is non trivial for non-square images, see https://en.wikipedia.org/wiki/In-place_matrix_transposition
avifResult avifRGBImageRotate(avifRGBImage * dstImage, const avifRGBImage * srcImage, const avifImageRotation * rotation)
{
    const uint32_t bytesPerPixel = avifRGBImagePixelSize(srcImage);
    const uint8_t angle = rotation->angle;
    const uint32_t newWidth = (angle == 0 || angle == 2) ? srcImage->width : srcImage->height;
    const uint32_t newHeight = (angle == 0 || angle == 2) ? srcImage->height : srcImage->width;
    *dstImage = *srcImage;
    dstImage->width = newWidth;
    dstImage->height = newHeight;
    dstImage->pixels = NULL;
    avifResult result = avifRGBImageAllocatePixels(dstImage);
    if (result != AVIF_RESULT_OK) {
        return result;
    }

    if (rotation->angle == 0) {
        const size_t bytesPerRow = (size_t)bytesPerPixel * srcImage->width;
        // 0 degrees. Just copy the rows as is.
        for (uint32_t j = 0; j < srcImage->height; ++j) {
            memcpy(dstImage->pixels + (j * dstImage->rowBytes), srcImage->pixels + (j * srcImage->rowBytes), bytesPerRow);
        }
    } else if (rotation->angle == 1) {
        // 90 degrees anti-clockwise.
        for (uint32_t j = 0; j < srcImage->height; ++j) {
            for (uint32_t i = 0; i < srcImage->width; ++i) {
                // Source pixel at (i, j) goes to destination pixel at (j, srcImage->width - 1 - i).
                memcpy(dstImage->pixels + ((srcImage->width - 1 - i) * dstImage->rowBytes) + (j * bytesPerPixel),
                       srcImage->pixels + (j * srcImage->rowBytes) + (i * bytesPerPixel),
                       bytesPerPixel);
            }
        }
    } else if (rotation->angle == 2) {
        // 180 degrees.
        for (uint32_t j = 0; j < srcImage->height; ++j) {
            for (uint32_t i = 0; i < srcImage->width; ++i) {
                // Source pixel at (i, j) goes to destination pixel at (srcImage->width - 1 - i, srcImage->height - 1 - j).
                memcpy(dstImage->pixels + ((srcImage->height - 1 - j) * dstImage->rowBytes) + ((srcImage->width - 1 - i) * bytesPerPixel),
                       srcImage->pixels + (j * srcImage->rowBytes) + (i * bytesPerPixel),
                       bytesPerPixel);
            }
        }
    } else if (rotation->angle == 3) {
        // 90 degrees clockwise.
        for (uint32_t j = 0; j < srcImage->height; ++j) {
            for (uint32_t i = 0; i < srcImage->width; ++i) {
                // Source pixel at (i, j) goes to destination pixel at (srcImage->width - 1 - i, j).
                memcpy(dstImage->pixels + (i * dstImage->rowBytes) + ((srcImage->height - 1 - j) * bytesPerPixel),
                       srcImage->pixels + (j * srcImage->rowBytes) + (i * bytesPerPixel),
                       bytesPerPixel);
            }
        }
    } else {
        return AVIF_RESULT_INVALID_ARGUMENT; // Invalid angle.
    }
    return AVIF_RESULT_OK;
}

avifResult avifRGBImageMirror(avifRGBImage * image, const avifImageMirror * mirror)
{
    if (mirror->axis == 0) { // Horizontal axis.
        const uint32_t bytesPerPixel = avifRGBImagePixelSize(image);
        // May be less than image->rowBytes e.g. if image is a cropped view.
        const size_t bytesPerRowToMove = (size_t)bytesPerPixel * image->width;
        // Top-to-bottom
        uint8_t * tempRow = (uint8_t *)avifAlloc(bytesPerRowToMove);
        if (!tempRow) {
            return AVIF_RESULT_OUT_OF_MEMORY;
        }
        for (uint32_t y = 0; y < image->height / 2; ++y) {
            uint8_t * row1 = &image->pixels[(size_t)y * image->rowBytes];
            uint8_t * row2 = &image->pixels[(size_t)(image->height - 1 - y) * image->rowBytes];
            memcpy(tempRow, row1, bytesPerRowToMove);
            memcpy(row1, row2, bytesPerRowToMove);
            memcpy(row2, tempRow, bytesPerRowToMove);
        }
        avifFree(tempRow);
    } else if (mirror->axis == 1) { // Vertical axis.
        const uint32_t pixelSize = avifRGBImagePixelSize(image);
        uint8_t tempPixel[8]; // Max pixel size should be 8 bytes (RGBA 16-bit).
        if (pixelSize > sizeof(tempPixel)) {
            return AVIF_RESULT_INVALID_ARGUMENT;
        }
        for (uint32_t y = 0; y < image->height; ++y) {
            uint8_t * row = &image->pixels[(size_t)y * image->rowBytes];
            for (uint32_t x = 0; x < image->width / 2; ++x) {
                uint8_t * pixel1 = &row[x * pixelSize];
                uint8_t * pixel2 = &row[(image->width - 1 - x) * pixelSize];
                memcpy(tempPixel, pixel1, pixelSize);
                memcpy(pixel1, pixel2, pixelSize);
                memcpy(pixel2, tempPixel, pixelSize);
            }
        }
    } else {
        return AVIF_RESULT_INVALID_ARGUMENT; // Invalid axis value.
    }

    return AVIF_RESULT_OK;
}

avifResult avifApplyTransforms(avifRGBImage * dstView, avifRGBImage * srcImage, const avifImage * avif)
{
    // ISO/IEC 23000-22 (MIAF), Section 7.3.6.7:
    //  These properties, if used, shall be indicated to be applied in the following order:
    //  clean aperture first, then rotation, then mirror.
    *dstView = *srcImage;
    if (avif->transformFlags & AVIF_TRANSFORM_CLAP) {
        avifCropRect cropRect;
        avifDiagnostics diag;
        if (avifCropRectFromCleanApertureBox(&cropRect, &avif->clap, avif->width, avif->height, &diag) &&
            (cropRect.x != 0 || cropRect.y != 0 || cropRect.width != avif->width || cropRect.height != avif->height)) {
            avifRGBImageSetViewRect(dstView, srcImage, &cropRect);
        } else {
            fprintf(stderr, "Invalid clean aperture box\n");
            return AVIF_RESULT_INVALID_ARGUMENT;
        }
    }
    if (avif->transformFlags & AVIF_TRANSFORM_IROT && avif->irot.angle != 0) {
        avifRGBImage tmpRgbImage;
        avifResult result = avifRGBImageRotate(&tmpRgbImage, dstView, &avif->irot);
        if (result != AVIF_RESULT_OK) {
            fprintf(stderr, "Failed to apply rotation\n");
            return result;
        }
        // We assume that srcImage owned its pixels and free them before replacing it with tmpRgbImage.
        avifRGBImageFreePixels(srcImage);
        *srcImage = tmpRgbImage;
        *dstView = *srcImage;
    }
    if (avif->transformFlags & AVIF_TRANSFORM_IMIR) {
        avifResult result = avifRGBImageMirror(dstView, &avif->imir);
        if (result != AVIF_RESULT_OK) {
            fprintf(stderr, "Failed to apply mirror\n");
            return result;
        }
    }
    return AVIF_RESULT_OK;
}
