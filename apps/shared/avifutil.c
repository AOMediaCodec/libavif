// Copyright 2019 Joe Drago. All rights reserved.
// SPDX-License-Identifier: BSD-2-Clause

#include "avifutil.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

static int32_t calcGCD(int32_t a, int32_t b)
{
    if (a < 0) {
        a *= -1;
    }
    if (b < 0) {
        b *= -1;
    }
    while (a > 0) {
        if (a < b) {
            int32_t t = a;
            a = b;
            b = t;
        }
        a = a - b;
    }
    return b;
}

static void printClapFraction(const char * name, int32_t n, int32_t d)
{
    printf("%s: %d/%d", name, n, d);
    int32_t gcd = calcGCD(n, d);
    if (gcd > 1) {
        int32_t rn = n / gcd;
        int32_t rd = d / gcd;
        printf(" (%d/%d)", rn, rd);
    }
    printf(", ");
}

static void avifImageDumpInternal(const avifImage * avif, uint32_t gridCols, uint32_t gridRows, avifBool alphaPresent)
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
    if (avif->alphaRange == AVIF_RANGE_LIMITED) {
        printf("                    Limited range\n");
        printf("                    WARNING: Limited-range alpha is deprecated. Use full-range alpha instead.\n");
    }
    printf(" * Range          : %s\n", (avif->yuvRange == AVIF_RANGE_FULL) ? "Full" : "Limited");

    printf(" * Color Primaries: %u\n", avif->colorPrimaries);
    printf(" * Transfer Char. : %u\n", avif->transferCharacteristics);
    printf(" * Matrix Coeffs. : %u\n", avif->matrixCoefficients);

    printf(" * ICC Profile    : %s (" AVIF_FMT_ZU " bytes)\n", (avif->icc.size > 0) ? "Present" : "Absent", avif->icc.size);
    printf(" * XMP Metadata   : %s (" AVIF_FMT_ZU " bytes)\n", (avif->xmp.size > 0) ? "Present" : "Absent", avif->xmp.size);
    printf(" * EXIF Metadata  : %s (" AVIF_FMT_ZU " bytes)\n", (avif->exif.size > 0) ? "Present" : "Absent", avif->exif.size);

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
            printClapFraction("H", (int32_t)avif->clap.heightN, (int32_t)avif->clap.heightD);
            printClapFraction("hOff", (int32_t)avif->clap.horizOffN, (int32_t)avif->clap.horizOffD);
            printClapFraction("vOff", (int32_t)avif->clap.vertOffN, (int32_t)avif->clap.vertOffD);
            printf("\n");

            avifCropRect cropRect;
            avifDiagnostics diag;
            avifDiagnosticsClearError(&diag);
            avifBool validClap =
                avifCropRectConvertCleanApertureBox(&cropRect, &avif->clap, avif->width, avif->height, avif->yuvFormat, &diag);
            if (validClap) {
                printf("      * Valid, derived crop rect: X: %d, Y: %d, W: %d, H: %d\n",
                       cropRect.x,
                       cropRect.y,
                       cropRect.width,
                       cropRect.height);
            } else {
                printf("      * Invalid: %s\n", diag.error);
            }
        }
        if (avif->transformFlags & AVIF_TRANSFORM_IROT) {
            printf("    * irot (Rotation)      : %u\n", avif->irot.angle);
        }
        if (avif->transformFlags & AVIF_TRANSFORM_IMIR) {
            printf("    * imir (Mirror)        : Mode %u (%s)\n", avif->imir.mode, (avif->imir.mode == 0) ? "top-to-bottom" : "left-to-right");
        }
    }
}

void avifImageDump(avifImage * avif, uint32_t gridCols, uint32_t gridRows)
{
    const avifBool alphaPresent = avif->alphaPlane && (avif->alphaRowBytes > 0);
    avifImageDumpInternal(avif, gridCols, gridRows, alphaPresent);
}

void avifContainerDump(avifDecoder * decoder)
{
    avifImageDumpInternal(decoder->image, 0, 0, decoder->alphaPresent);
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
            avifROData header;
            header.data = headerBuffer;
            header.size = bytesRead;

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
                struct avifHeaderSignature * signature = &signatures[signatureIndex];
                if (header.size < signature->magicSize) {
                    continue;
                }
                if (!memcmp(header.data, signature->magic, signature->magicSize)) {
                    return signature->format;
                }
            }

            // If none of these signatures match, bail out here. Guessing by extension won't help.
            return AVIF_APP_FILE_FORMAT_UNKNOWN;
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

int avifQueryCPUCount()
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

int avifQueryCPUCount()
{
    return 1;
}

#else

// POSIX

#include <unistd.h>

int avifQueryCPUCount()
{
    int numCPU = (int)sysconf(_SC_NPROCESSORS_ONLN);
    return (numCPU > 0) ? numCPU : 1;
}

#endif
