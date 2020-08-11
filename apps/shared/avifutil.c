// Copyright 2019 Joe Drago. All rights reserved.
// SPDX-License-Identifier: BSD-2-Clause

#include "avifutil.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

void avifImageDump(avifImage * avif)
{
    printf(" * Resolution     : %dx%d\n", avif->width, avif->height);
    printf(" * Bit Depth      : %d\n", avif->depth);
    printf(" * Format         : %s\n", avifPixelFormatToString(avif->yuvFormat));
    printf(" * Alpha          : %s\n", (avif->alphaPlane && (avif->alphaRowBytes > 0)) ? "Present" : "Absent");
    printf(" * Range          : %s\n", (avif->yuvRange == AVIF_RANGE_FULL) ? "Full" : "Limited");

    printf(" * Color Primaries: %d\n", avif->colorPrimaries);
    printf(" * Transfer Char. : %d\n", avif->transferCharacteristics);
    printf(" * Matrix Coeffs. : %d\n", avif->matrixCoefficients);

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
            printf("    * clap (Clean Aperture): W: %d/%d, H: %d/%d, hOff: %d/%d, vOff: %d/%d\n",
                   (int)avif->clap.widthN,
                   (int)avif->clap.widthD,
                   (int)avif->clap.heightN,
                   (int)avif->clap.heightD,
                   (int)avif->clap.horizOffN,
                   (int)avif->clap.horizOffD,
                   (int)avif->clap.vertOffN,
                   (int)avif->clap.vertOffD);
        }
        if (avif->transformFlags & AVIF_TRANSFORM_IROT) {
            printf("    * irot (Rotation)      : %u\n", avif->irot.angle);
        }
        if (avif->transformFlags & AVIF_TRANSFORM_IMIR) {
            printf("    * imir (Mirror)        : %u (%s)\n", avif->imir.axis, (avif->imir.axis == 0) ? "Vertical" : "Horizontal");
        }
    }
}

void avifPrintVersions(void)
{
    char codecVersions[256];
    avifCodecVersions(codecVersions);
    printf("Version: %s (%s)\n\n", avifVersion(), codecVersions);
}

avifAppFileFormat avifGuessFileFormat(const char * filename)
{
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
