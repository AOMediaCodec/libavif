// Copyright 2020 Joe Drago. All rights reserved.
// SPDX-License-Identifier: BSD-2-Clause

#include "testcase.h"

#include "compare.h"
#include "y4m.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char * choiceToString(avifCodecChoice choice)
{
    switch (choice) {
        case AVIF_CODEC_CHOICE_AUTO:
            return "auto";
        case AVIF_CODEC_CHOICE_AOM:
            return "aom";
        case AVIF_CODEC_CHOICE_DAV1D:
            return "dav1d";
        case AVIF_CODEC_CHOICE_LIBGAV1:
            return "libgav1";
        case AVIF_CODEC_CHOICE_RAV1E:
            return "rav1e";
        case AVIF_CODEC_CHOICE_SVT:
            return "svt";
    }
    return "unknown";
}

static avifCodecChoice stringToChoice(const char * str)
{
    if (!strcmp(str, "aom")) {
        return AVIF_CODEC_CHOICE_AOM;
    } else if (!strcmp(str, "dav1d")) {
        return AVIF_CODEC_CHOICE_DAV1D;
    } else if (!strcmp(str, "libgav1")) {
        return AVIF_CODEC_CHOICE_LIBGAV1;
    } else if (!strcmp(str, "rav1e")) {
        return AVIF_CODEC_CHOICE_RAV1E;
    } else if (!strcmp(str, "svt")) {
        return AVIF_CODEC_CHOICE_SVT;
    }
    return AVIF_CODEC_CHOICE_AUTO;
}

TestCase * testCaseCreate(void)
{
    TestCase * tc = malloc(sizeof(TestCase));
    memset(tc, 0, sizeof(TestCase));
    return tc;
}

void testCaseDestroy(TestCase * tc)
{
    if (tc->name) {
        free(tc->name);
    }
    if (tc->inputFilename) {
        free(tc->inputFilename);
    }
    free(tc);
}

void testCaseSetInputFilename(TestCase * tc, const char * inputFilename)
{
    if (tc->inputFilename) {
        free(tc->inputFilename);
    }
    tc->inputFilename = strdup(inputFilename);
}

void testCaseGenerateName(TestCase * tc)
{
    char basenameBuffer[1024];
    if (tc->inputFilename) {
        strcpy(basenameBuffer, tc->inputFilename);
        char * dotLoc = strrchr(basenameBuffer, '.');
        if (dotLoc) {
            *dotLoc = 0;
        }
    } else {
        basenameBuffer[0] = 0;
    }

    char nameBuffer[1024];
    if (snprintf(nameBuffer,
                 sizeof(nameBuffer),
                 "%s_%s_to_%s_qp%d_%d_speed%d",
                 basenameBuffer,
                 choiceToString(tc->encodeChoice),
                 choiceToString(tc->decodeChoice),
                 tc->minQuantizer,
                 tc->maxQuantizer,
                 tc->speed) < 0) {
        nameBuffer[0] = 0;
    }
    nameBuffer[sizeof(nameBuffer) - 1] = 0;
    if (tc->name) {
        free(tc->name);
    }
    tc->name = strdup(nameBuffer);
}

static const char * jsonGetString(cJSON * parent, const char * key, const char * def)
{
    if (!parent || !cJSON_IsObject(parent)) {
        return def;
    }

    cJSON * childItem = cJSON_GetObjectItem(parent, key);
    if (!childItem || !cJSON_IsString(childItem)) {
        return def;
    }
    return childItem->valuestring;
}

static int jsonGetInt(cJSON * parent, const char * key, int def)
{
    if (!parent || !cJSON_IsObject(parent)) {
        return def;
    }

    cJSON * childItem = cJSON_GetObjectItem(parent, key);
    if (!childItem || !cJSON_IsNumber(childItem)) {
        return def;
    }
    return childItem->valueint;
}

static float jsonGetFloat(cJSON * parent, const char * key, float def)
{
    if (!parent || !cJSON_IsObject(parent)) {
        return def;
    }

    cJSON * childItem = cJSON_GetObjectItem(parent, key);
    if (!childItem || !cJSON_IsNumber(childItem)) {
        return def;
    }
    return (float)childItem->valuedouble;
}

static avifBool jsonGetBool(cJSON * parent, const char * key, avifBool def)
{
    if (!parent || !cJSON_IsObject(parent)) {
        return def;
    }

    cJSON * childItem = cJSON_GetObjectItem(parent, key);
    if (!childItem || !cJSON_IsBool(childItem)) {
        return def;
    }
    return (childItem->type == cJSON_True);
}

TestCase * testCaseFromJSON(cJSON * json)
{
    TestCase * tc = testCaseCreate();
    tc->name = strdup(jsonGetString(json, "name", "unknown"));
    tc->inputFilename = strdup(jsonGetString(json, "input", "unknown"));

    tc->encodeChoice = stringToChoice(jsonGetString(json, "enc", "aom"));
    tc->decodeChoice = stringToChoice(jsonGetString(json, "dec", "aom"));
    tc->minQuantizer = jsonGetInt(json, "minQP", 0);
    tc->maxQuantizer = jsonGetInt(json, "maxQP", 0);
    tc->speed = jsonGetInt(json, "speed", 0);
    tc->active = jsonGetBool(json, "active", AVIF_FALSE);

    tc->maxThreshold = jsonGetInt(json, "max", 0);
    tc->avgThreshold = jsonGetFloat(json, "avg", 0);
    return tc;
}

cJSON * testCaseToJSON(TestCase * tc)
{
    cJSON * json = cJSON_CreateObject();

    if (tc->name) {
        cJSON_AddStringToObject(json, "name", tc->name);
    }
    if (tc->inputFilename) {
        cJSON_AddStringToObject(json, "input", tc->inputFilename);
    }

    cJSON_AddStringToObject(json, "enc", choiceToString(tc->encodeChoice));
    cJSON_AddStringToObject(json, "dec", choiceToString(tc->decodeChoice));
    cJSON_AddNumberToObject(json, "minQP", tc->minQuantizer);
    cJSON_AddNumberToObject(json, "maxQP", tc->maxQuantizer);
    cJSON_AddNumberToObject(json, "speed", tc->speed);
    cJSON_AddBoolToObject(json, "active", tc->active);

    cJSON_AddNumberToObject(json, "max", tc->maxThreshold);
    cJSON_AddNumberToObject(json, "avg", tc->avgThreshold);
    return json;
}

int testCaseRun(TestCase * tc, const char * dataDir, avifBool generating)
{
    if (!tc->name || !tc->inputFilename) {
        return AVIF_FALSE;
    }

    char y4mFilename[2048];
    snprintf(y4mFilename, sizeof(y4mFilename), "%s/%s", dataDir, tc->inputFilename);
    y4mFilename[sizeof(y4mFilename) - 1] = 0;

    avifImage * image = avifImageCreateEmpty();
    if (!y4mRead(y4mFilename, image, NULL, NULL)) {
        avifImageDestroy(image);
        printf("ERROR[%s]: Can't read y4m: %s\n", tc->name, y4mFilename);
        return AVIF_FALSE;
    }

    avifBool result = AVIF_TRUE;
    avifRWData encodedData = AVIF_DATA_EMPTY;
    avifEncoder * encoder = NULL;
    avifDecoder * decoder = NULL;

    encoder = avifEncoderCreate();
    encoder->codecChoice = tc->encodeChoice;
    encoder->maxThreads = 4; // TODO: pick something better here
    if (avifEncoderWrite(encoder, image, &encodedData) != AVIF_RESULT_OK) {
        printf("ERROR[%s]: Encode failed\n", tc->name);
        result = AVIF_FALSE;
        goto cleanup;
    }

    decoder = avifDecoderCreate();
    decoder->codecChoice = tc->decodeChoice;
    avifDecoderSetIOMemory(decoder, encodedData.data, encodedData.size);
    avifResult decodeResult = avifDecoderParse(decoder);
    if (decodeResult != AVIF_RESULT_OK) {
        printf("ERROR[%s]: Decode failed\n", tc->name);
        result = AVIF_FALSE;
        goto cleanup;
    }

    avifResult nextImageResult = avifDecoderNextImage(decoder);
    if (nextImageResult != AVIF_RESULT_OK) {
        printf("ERROR[%s]: NextImage failed\n", tc->name);
        result = AVIF_FALSE;
        goto cleanup;
    }

    ImageComparison ic;
    if (!compareYUVA(&ic, image, decoder->image)) {
        printf("ERROR[%s]: compare bailed out\n", tc->name);
        result = AVIF_FALSE;
        goto cleanup;
    }

    if (generating) {
        int maxThreshold = ic.maxDiff;
        if (maxThreshold > 0) {
            // Not lossless, give one more codepoint of wiggle room
            ++maxThreshold;
        }
        float avgThreshold = ic.avgDiff + 0.25f;

        tc->maxThreshold = maxThreshold;
        tc->avgThreshold = avgThreshold;
        printf("Generated[%s]: Thresholds - Max %d, Avg %f\n", tc->name, tc->maxThreshold, tc->avgThreshold);
    } else {
        if (ic.maxDiff > tc->maxThreshold) {
            printf("ERROR[%s]: max diff threshold exceeded: %d > %d\n", tc->name, ic.maxDiff, tc->maxThreshold);
            result = AVIF_FALSE;
            goto cleanup;
        }
        if (ic.avgDiff > tc->avgThreshold) {
            printf("FAILED[%s]: avg diff threshold exceeded: %f > %f\n", tc->name, ic.avgDiff, tc->avgThreshold);
            result = AVIF_FALSE;
            goto cleanup;
        }
        printf("OK[%s]\n", tc->name);
    }

cleanup:
    if (decoder) {
        avifDecoderDestroy(decoder);
    }
    avifRWDataFree(&encodedData);
    if (encoder) {
        avifEncoderDestroy(encoder);
    }
    avifImageDestroy(image);
    (void)generating;
    return result;
}
