// Copyright 2019 Joe Drago. All rights reserved.
// SPDX-License-Identifier: BSD-2-Clause

#ifndef AVIF_INTERNAL_H
#define AVIF_INTERNAL_H

#include "avif/avif.h"

// Yes, clamp macros are nasty. Do not use them.
#define AVIF_CLAMP(x, low, high)  (((x) > (high)) ? (high) : (((x) < (low)) ? (low) : (x)))

// Used by stream related things.
#define CHECK(A) if (!(A)) return AVIF_FALSE;

// ---------------------------------------------------------------------------
// URNs

#define URN_ALPHA0 "urn:mpeg:mpegB:cicp:systems:auxiliary:alpha"
#define URN_ALPHA1 "urn:mpeg:hevc:2015:auxid:1"

// ---------------------------------------------------------------------------
// Utils

float avifRoundf(float v);

uint16_t avifHTONS(uint16_t s);
uint16_t avifNTOHS(uint16_t s);
uint32_t avifHTONL(uint32_t l);
uint32_t avifNTOHL(uint32_t l);
uint64_t avifHTON64(uint64_t l);
uint64_t avifNTOH64(uint64_t l);

int avifFullToLimited(int depth, int v);
int avifLimitedToFull(int depth, int v);

// ---------------------------------------------------------------------------
// Memory management

void * avifAlloc(size_t size);
void avifFree(void * p);

// ---------------------------------------------------------------------------
// avifStream

typedef size_t avifBoxMarker;

typedef struct avifStream
{
    avifRawData * raw;
    size_t offset;
} avifStream;

typedef struct avifBoxHeader
{
    size_t size;
    uint8_t type[4];
} avifBoxHeader;

uint8_t * avifStreamCurrent(avifStream * stream);

void avifStreamStart(avifStream * stream, avifRawData * raw);

// Read
avifBool avifStreamHasBytesLeft(avifStream * stream, size_t byteCount);
size_t avifStreamRemainingBytes(avifStream * stream);
avifBool avifStreamSkip(avifStream * stream, size_t byteCount);
avifBool avifStreamRead(avifStream * stream, uint8_t * data, size_t size);
avifBool avifStreamReadU16(avifStream * stream, uint16_t * v);
avifBool avifStreamReadU32(avifStream * stream, uint32_t * v);
avifBool avifStreamReadUX8(avifStream * stream, uint64_t * v, uint64_t factor); // Reads a factor*8 sized uint, saves in v
avifBool avifStreamReadU64(avifStream * stream, uint64_t * v);
avifBool avifStreamReadString(avifStream * stream, char * output, size_t outputSize);
avifBool avifStreamReadBoxHeader(avifStream * stream, avifBoxHeader * header);
avifBool avifStreamReadVersionAndFlags(avifStream * stream, uint8_t * version, uint8_t * flags); // flags is an optional uint8_t[3]
avifBool avifStreamReadAndEnforceVersion(avifStream * stream, uint8_t enforcedVersion);          // currently discards flags

// Write
void avifStreamFinishWrite(avifStream * stream);
void avifStreamWrite(avifStream * stream, const uint8_t * data, size_t size);
avifBoxMarker avifStreamWriteBox(avifStream * stream, const char * type, int version /* -1 for "not a FullBox" */, size_t contentSize);
void avifStreamFinishBox(avifStream * stream, avifBoxMarker marker);
void avifStreamWriteU8(avifStream * stream, uint8_t v);
void avifStreamWriteU16(avifStream * stream, uint16_t v);
void avifStreamWriteU32(avifStream * stream, uint32_t v);
void avifStreamWriteZeros(avifStream * stream, size_t byteCount);

#endif // ifndef AVIF_INTERNAL_H
