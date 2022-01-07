// Copyright 2019 Joe Drago. All rights reserved.
// SPDX-License-Identifier: BSD-2-Clause

#ifndef SRC_DECODERDATA_H_
#define SRC_DECODERDATA_H_

#include "avif/avif.h"
#include "layout.h"
#include "meta.h"
#include "sample.h"

typedef struct avifDecoderData
{
    avifMeta * meta; // The root-level meta box
    avifTrackArray tracks;
    avifTileArray tiles;
    unsigned int colorTileCount;
    unsigned int alphaTileCount;
    avifImageGrid colorGrid;
    avifImageGrid alphaGrid;
    avifDecoderSource source;
    uint8_t majorBrand[4];                     // From the file's ftyp, used by AVIF_DECODER_SOURCE_AUTO
    avifDiagnostics * diag;                    // Shallow copy; owned by avifDecoder
    const avifSampleTable * sourceSampleTable; // NULL unless (source == AVIF_DECODER_SOURCE_TRACKS), owned by an avifTrack
    avifBool cicpSet;                          // True if avifDecoder's image has had its CICP set correctly yet.
                                               // This allows nclx colr boxes to override AV1 CICP, as specified in the MIAF
                                               // standard (ISO/IEC 23000-22:2019), section 7.3.6.4:
                                               //
    // "The colour information property takes precedence over any colour information in the image
    // bitstream, i.e. if the property is present, colour information in the bitstream shall be ignored."
} avifDecoderData;

avifDecoderData * avifDecoderDataCreate(void);
void avifDecoderDataResetCodec(avifDecoderData * data);
avifTile * avifDecoderDataCreateTile(avifDecoderData * data, uint32_t width, uint32_t height, uint8_t operatingPoint);
avifTrack * avifDecoderDataCreateTrack(avifDecoderData * data);
void avifDecoderDataClearTiles(avifDecoderData * data);
void avifDecoderDataDestroy(avifDecoderData * data);

void avifDecoderCleanup(avifDecoder * decoder);

#endif  // SRC_DECODERDATA_H_
