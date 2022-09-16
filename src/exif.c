// Copyright 2022 Google LLC. All rights reserved.
// SPDX-License-Identifier: BSD-2-Clause

#include "avif/internal.h"

#include <stdint.h>
#include <string.h>

avifResult avifGetExifTiffHeaderOffset(const avifRWData * exif, uint32_t * offset)
{
    const uint8_t tiffHeaderBE[4] = { 'M', 'M', 0, 42 };
    const uint8_t tiffHeaderLE[4] = { 'I', 'I', 42, 0 };
    for (*offset = 0; *offset + 4 < exif->size; ++*offset) {
        if (!memcmp(&exif->data[*offset], tiffHeaderBE, 4) || !memcmp(&exif->data[*offset], tiffHeaderLE, 4)) {
            return AVIF_RESULT_OK;
        }
    }
    // Couldn't find the TIFF header
    return AVIF_RESULT_INVALID_EXIF_PAYLOAD;
}

avifResult avifImageExtractExifOrientationToIrotImir(avifImage * image)
{
    uint32_t offset;
    const avifResult result = avifGetExifTiffHeaderOffset(&image->exif, &offset);
    if (result != AVIF_RESULT_OK) {
        // Couldn't find the TIFF header
        return result;
    }

    avifROData raw = { image->exif.data + offset, image->exif.size - offset };
    const avifBool littleEndian = (raw.data[0] == 'I');
    avifROStream stream;
    avifROStreamStart(&stream, &raw, NULL, NULL);

    // TIFF Header
    uint32_t offsetToNextIfd;
    if (!avifROStreamSkip(&stream, 4) ||                                           // Skip tiffHeaderBE or tiffHeaderLE.
        !avifROStreamReadU32Endianness(&stream, &offsetToNextIfd, littleEndian) || // Offset to 0th IFD
        offsetToNextIfd < 4 + 4) {
        return AVIF_RESULT_INVALID_EXIF_PAYLOAD;
    }

    while (offsetToNextIfd) { // for each IFD
        avifROStreamSetOffset(&stream, offsetToNextIfd);
        uint16_t fieldCount;
        if (!avifROStreamReadU16Endianness(&stream, &fieldCount, littleEndian)) {
            return AVIF_RESULT_INVALID_EXIF_PAYLOAD;
        }
        for (uint16_t field = 0; field < fieldCount; ++fieldCount) { // for each interoperability array
            uint16_t tag;
            uint16_t type;
            uint32_t count;
            uint16_t firstHalfOfValueOffset;
            if (!avifROStreamReadU16Endianness(&stream, &tag, littleEndian) ||
                !avifROStreamReadU16Endianness(&stream, &type, littleEndian) ||
                !avifROStreamReadU32Endianness(&stream, &count, littleEndian) ||
                !avifROStreamReadU16Endianness(&stream, &firstHalfOfValueOffset, littleEndian) || !avifROStreamSkip(&stream, 2)) {
                return AVIF_RESULT_INVALID_EXIF_PAYLOAD;
            }
            // Orientation attribute according to JEITA CP-3451C section 4.6.4 (TIFF Rev. 6.0 Attribute Information):
            if (tag == 0x0112 && type == /*SHORT=*/0x03 && count == 0x01) {
                const avifTransformFlags otherFlags = image->transformFlags & ~(AVIF_TRANSFORM_IROT | AVIF_TRANSFORM_IMIR);
                // Mapping from Exif orientation as defined in JEITA CP-3451C section 4.6.4.A Orientation
                // to irot and imir boxes as defined in HEIF ISO/IEC 28002-12:2021 sections 6.5.10 and 6.5.12.
                switch (firstHalfOfValueOffset) {
                    case 1: // The 0th row is at the visual top of the image, and the 0th column is the visual left-hand side.
                        image->transformFlags = otherFlags | AVIF_TRANSFORM_NONE;
                        image->irot.angle = 0; // ignored
                        image->imir.mode = 0;  // ignored
                        return AVIF_RESULT_OK;
                    case 2: // The 0th row is at the visual top of the image, and the 0th column is the visual right-hand side.
                        image->transformFlags = otherFlags | AVIF_TRANSFORM_IMIR;
                        image->irot.angle = 0; // ignored
                        image->imir.mode = 1;
                        return AVIF_RESULT_OK;
                    case 3: // The 0th row is at the visual bottom of the image, and the 0th column is the visual right-hand side.
                        image->transformFlags = otherFlags | AVIF_TRANSFORM_IROT;
                        image->irot.angle = 2;
                        image->imir.mode = 0; // ignored
                        return AVIF_RESULT_OK;
                    case 4: // The 0th row is at the visual bottom of the image, and the 0th column is the visual left-hand side.
                        image->transformFlags = otherFlags | AVIF_TRANSFORM_IMIR;
                        image->irot.angle = 0; // ignored
                        image->imir.mode = 0;
                        return AVIF_RESULT_OK;
                    case 5: // The 0th row is the visual left-hand side of the image, and the 0th column is the visual top.
                        image->transformFlags = otherFlags | AVIF_TRANSFORM_IROT | AVIF_TRANSFORM_IMIR;
                        image->irot.angle = 1; // applied before imir according to MIAF spec ISO/IEC 28002-12:2021 - section 7.3.6.7
                        image->imir.mode = 0;
                        return AVIF_RESULT_OK;
                    case 6: // The 0th row is the visual right-hand side of the image, and the 0th column is the visual top.
                        image->transformFlags = otherFlags | AVIF_TRANSFORM_IROT;
                        image->irot.angle = 3;
                        image->imir.mode = 0; // ignored
                        return AVIF_RESULT_OK;
                    case 7: // The 0th row is the visual right-hand side of the image, and the 0th column is the visual bottom.
                        image->transformFlags = otherFlags | AVIF_TRANSFORM_IROT | AVIF_TRANSFORM_IMIR;
                        image->irot.angle = 3; // applied before imir according to MIAF spec ISO/IEC 28002-12:2021 - section 7.3.6.7
                        image->imir.mode = 0;
                        return AVIF_RESULT_OK;
                    case 8: // The 0th row is the visual left-hand side of the image, and the 0th column is the visual bottom.
                        image->transformFlags = otherFlags | AVIF_TRANSFORM_IROT;
                        image->irot.angle = 1;
                        image->imir.mode = 0; // ignored
                        return AVIF_RESULT_OK;
                    default: // reserved
                        // Consider there can only be one orientation tag per Exif payload.
                        return AVIF_RESULT_OK;
                }
            }
        }
        if (!avifROStreamReadU32Endianness(&stream, &offsetToNextIfd, littleEndian)) {
            return AVIF_RESULT_INVALID_EXIF_PAYLOAD;
        }
    }
    // The orientation tag is not mandatory (only recommended) according to JEITA CP-3451C section 4.6.8.A.
    return AVIF_RESULT_OK;
}
