// Copyright 2020 Joe Drago. All rights reserved.
// SPDX-License-Identifier: BSD-2-Clause

#ifndef TESTCASE_H
#define TESTCASE_H

#include "avif/avif.h"

#include "cJSON.h"

typedef struct TestCase
{
    char * name;
    char * inputFilename;
    avifCodecChoice encodeChoice;
    avifCodecChoice decodeChoice;
    int minQuantizer;
    int maxQuantizer;
    int speed;
    avifBool active;

    int maxThreshold;
    float avgThreshold;
} TestCase;

TestCase * testCaseCreate(void);
TestCase * testCaseFromJSON(cJSON * json);
void testCaseDestroy(TestCase * tc);
void testCaseSetInputFilename(TestCase * tc, const char * inputFilename);
void testCaseGenerateName(TestCase * tc);
cJSON * testCaseToJSON(TestCase * tc);

int testCaseRun(TestCase * tc, const char * dataDir, avifBool generating); // returns 0 on failure

#endif
