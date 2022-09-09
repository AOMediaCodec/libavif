// Copyright 2022 Google LLC. All rights reserved.
// SPDX-License-Identifier: BSD-2-Clause

#include "avif/internal.h"

#include <stdint.h>
#include <string.h>

avifResult avifExtractExifTiffHeaderOffset(const avifRWData * exif, uint32_t * exifTiffHeaderOffset)
{
    *exifTiffHeaderOffset = 0;
    if (exif->size < 4) {
        // Can't even fit the TIFF header, something is wrong
        return AVIF_RESULT_INVALID_EXIF_PAYLOAD;
    }

    const uint8_t tiffHeaderBE[4] = { 'M', 'M', 0, 42 };
    const uint8_t tiffHeaderLE[4] = { 'I', 'I', 42, 0 };
    for (; *exifTiffHeaderOffset < (exif->size - 4); ++*exifTiffHeaderOffset) {
        if (!memcmp(&exif->data[*exifTiffHeaderOffset], tiffHeaderBE, sizeof(tiffHeaderBE))) {
            break;
        }
        if (!memcmp(&exif->data[*exifTiffHeaderOffset], tiffHeaderLE, sizeof(tiffHeaderLE))) {
            break;
        }
    }

    if (*exifTiffHeaderOffset >= exif->size - 4) {
        // Couldn't find the TIFF header
        return AVIF_RESULT_INVALID_EXIF_PAYLOAD;
    }
    return AVIF_RESULT_OK;
}

avifResult avifExtractExifOrientation(const avifRWData * exif, avifTransformFlags * flags, avifImageRotation * irot, avifImageMirror * imir)
{
    uint32_t exifTiffHeaderOffset;
    const avifResult result = avifExtractExifTiffHeaderOffset(exif, &exifTiffHeaderOffset);
    if (result != AVIF_RESULT_OK) {
        // Couldn't find the TIFF header
        return result;
    }

    avifROData raw = { exif->data + exifTiffHeaderOffset, exif->size - exifTiffHeaderOffset };
    const avifBool isLittleEndian = (raw.data[0] == 'I');
    avifROStream stream;
    avifROStreamStart(&stream, &raw, NULL, NULL);

    // TIFF Header
    uint32_t offsetToNextIfd;
    if (!avifROStreamSkip(&stream, 2 + 2) ||                                         // Byte Order ("II" or "MM" then "42")
        !avifROStreamReadU32Endianness(&stream, &offsetToNextIfd, isLittleEndian) || // Offset to 0th IFD
        offsetToNextIfd < 2 + 2 + 4) {
        return AVIF_RESULT_INVALID_EXIF_PAYLOAD;
    }

    while (offsetToNextIfd) { // for each IFD
        avifROStreamSetOffset(&stream, offsetToNextIfd);
        uint16_t fieldCount;
        if (!avifROStreamReadU16Endianness(&stream, &fieldCount, isLittleEndian)) {
            return AVIF_RESULT_INVALID_EXIF_PAYLOAD;
        }
        for (uint16_t field = 0; field < fieldCount; ++fieldCount) { // for each interoperability array
            uint16_t tag;
            uint16_t type;
            uint32_t count;
            uint16_t firstHalfOfValueOffset;
            uint16_t secondHalfOfValueOffset;
            if (!avifROStreamReadU16Endianness(&stream, &tag, isLittleEndian) ||
                !avifROStreamReadU16Endianness(&stream, &type, isLittleEndian) ||
                !avifROStreamReadU32Endianness(&stream, &count, isLittleEndian) ||
                !avifROStreamReadU16Endianness(&stream, &firstHalfOfValueOffset, isLittleEndian) ||
                !avifROStreamReadU16Endianness(&stream, &secondHalfOfValueOffset, isLittleEndian)) {
                return AVIF_RESULT_INVALID_EXIF_PAYLOAD;
            }
            // Orientation attribute according to JEITA CP-3451C section 4.6.4 (TIFF Rev. 6.0 Attribute Information):
            if (tag == 0x0112 && type == /*SHORT=*/0x03 && count == 0x01) { // Exif IFD
                // Mapping from Exif orientation as defined in JEITA CP-3451C section 4.6.4.A Orientation
                // to irot and imir boxes as defined in HEIF ISO/IEC 28002-12:2021 sections 6.5.10 and 6.5.12.
                switch (firstHalfOfValueOffset) {
                    case 1: // The 0th row is at the visual top of the image, and the 0th column is the visual left-hand side.
                        *flags = AVIF_TRANSFORM_NONE;
                        irot->angle = 0; // ignored
                        imir->mode = 0;  // ignored
                        return AVIF_RESULT_OK;
                    case 2: // The 0th row is at the visual top of the image, and the 0th column is the visual right-hand side.
                        *flags = AVIF_TRANSFORM_IMIR;
                        irot->angle = 0; // ignored
                        imir->mode = 1;
                        return AVIF_RESULT_OK;
                    case 3: // The 0th row is at the visual bottom of the image, and the 0th column is the visual right-hand side.
                        *flags = AVIF_TRANSFORM_IROT;
                        irot->angle = 2;
                        imir->mode = 0; // ignored
                        return AVIF_RESULT_OK;
                    case 4: // The 0th row is at the visual bottom of the image, and the 0th column is the visual left-hand side.
                        *flags = AVIF_TRANSFORM_IMIR;
                        irot->angle = 0; // ignored
                        imir->mode = 0;
                        return AVIF_RESULT_OK;
                    case 5: // The 0th row is the visual left-hand side of the image, and the 0th column is the visual top.
                        *flags = AVIF_TRANSFORM_IROT | AVIF_TRANSFORM_IMIR;
                        irot->angle = 1; // applied before imir according to Chrome's rendering behavior
                        imir->mode = 0;
                        return AVIF_RESULT_OK;
                    case 6: // The 0th row is the visual right-hand side of the image, and the 0th column is the visual top.
                        *flags = AVIF_TRANSFORM_IROT;
                        irot->angle = 3;
                        imir->mode = 0; // ignored
                        return AVIF_RESULT_OK;
                    case 7: // The 0th row is the visual right-hand side of the image, and the 0th column is the visual bottom.
                        *flags = AVIF_TRANSFORM_IROT | AVIF_TRANSFORM_IMIR;
                        irot->angle = 3; // applied before imir according to Chrome's rendering behavior
                        imir->mode = 0;
                        return AVIF_RESULT_OK;
                    case 8: // The 0th row is the visual left-hand side of the image, and the 0th column is the visual bottom.
                        *flags = AVIF_TRANSFORM_IROT;
                        irot->angle = 1;
                        imir->mode = 0; // ignored
                        return AVIF_RESULT_OK;
                    default: // reserved
                        break;
                }
            }
            // Exif IFD according to JEITA CP-3451C is (tag == 0x8769 && type == /*LONG=*/0x04 && count == 0x01) with
            // valueOffset being a "pointer" (meaning offset) to the Exif IFD but there is little to do with that information.
        }
        if (!avifROStreamReadU32(&stream, &offsetToNextIfd)) {
            return AVIF_RESULT_INVALID_EXIF_PAYLOAD;
        }
    }
    return AVIF_RESULT_NO_CONTENT;
}
