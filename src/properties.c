// Copyright 2024 Brad Hards. All rights reserved.
// SPDX-License-Identifier: BSD-2-Clause

#include "avif/internal.h"
#include <string.h>

struct avifKnownProperty
{
    const uint8_t fourcc[4];
};

static const struct avifKnownProperty avifKnownProperties[] = {
    { "ftyp" }, { "uuid" }, { "meta" }, { "hdlr" }, { "pitm" }, { "dinf" }, { "dref" }, { "idat" }, { "iloc" },
    { "iinf" }, { "infe" }, { "iprp" }, { "ipco" }, { "av1C" }, { "av2C" }, { "ispe" }, { "pixi" }, { "pasp" },
    { "colr" }, { "auxC" }, { "clap" }, { "irot" }, { "imir" }, { "clli" }, { "cclv" }, { "mdcv" }, { "amve" },
    { "reve" }, { "ndwt" }, { "a1op" }, { "lsel" }, { "a1lx" }, { "cmin" }, { "cmex" }, { "ipma" }, { "iref" },
    { "auxl" }, { "thmb" }, { "dimg" }, { "prem" }, { "cdsc" }, { "grpl" }, { "altr" }, { "ster" }, { "mdat" },
};

static const size_t numKnownProperties = sizeof(avifKnownProperties) / sizeof(avifKnownProperties[0]);

static const size_t FOURCC_BYTES = 4;
static const size_t UUID_BYTES = 16;

static const uint8_t ISO_UUID_SUFFIX[12] = { 0x00, 0x01, 0x00, 0x10, 0x80, 0x00, 0x00, 0xAA, 0x00, 0x38, 0x9b, 0x71 };

avifBool avifIsKnownPropertyType(const uint8_t boxtype[4])
{
    for (size_t i = 0; i < numKnownProperties; i++) {
        if (memcmp(avifKnownProperties[i].fourcc, boxtype, FOURCC_BYTES) == 0) {
            return AVIF_TRUE;
        }
    }
    return AVIF_FALSE;
}

avifBool avifIsValidUUID(const uint8_t uuid[16])
{
    for (size_t i = 0; i < numKnownProperties; i++) {
        if ((memcmp(avifKnownProperties[i].fourcc, uuid, FOURCC_BYTES) == 0) &&
            (memcmp(ISO_UUID_SUFFIX, uuid + FOURCC_BYTES, UUID_BYTES - FOURCC_BYTES) == 0)) {
            return AVIF_FALSE;
        }
    }
    uint8_t variant = uuid[8] >> 4;
    if ((variant < 0x08) || (variant > 0x0b)) {
        return AVIF_FALSE;
    }
    uint8_t version = uuid[6] >> 4;
    if ((version < 1) || (version > 8)) {
        return AVIF_FALSE;
    }
    return AVIF_TRUE;
}
