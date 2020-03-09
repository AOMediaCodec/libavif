// Copyright 2020 Joe Drago. All rights reserved.
// SPDX-License-Identifier: BSD-2-Clause

// #define WIN32_MEMORY_LEAK_DETECTION
#ifdef WIN32_MEMORY_LEAK_DETECTION
#define _CRTDBG_MAP_ALLOC
#include <crtdbg.h>
#endif

#include "avif/avif.h"

#include "testcase.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)

#include <windows.h>

typedef struct NextFilenameData
{
    int didFirstFile;
    HANDLE handle;
    WIN32_FIND_DATA wfd;
} NextFilenameData;

static const char * nextFilename(const char * parentDir, const char * extension, NextFilenameData * nfd)
{
    for (;;) {
        if (nfd->didFirstFile) {
            if (FindNextFile(nfd->handle, &nfd->wfd) == 0) {
                // No more files
                break;
            }
        } else {
            char filenameBuffer[2048];
            snprintf(filenameBuffer, sizeof(filenameBuffer), "%s\\*", parentDir);
            filenameBuffer[sizeof(filenameBuffer) - 1] = 0;
            nfd->handle = FindFirstFile(filenameBuffer, &nfd->wfd);
            if (nfd->handle == INVALID_HANDLE_VALUE) {
                return NULL;
            }
            nfd->didFirstFile = 1;
        }

        // If we get here, we should have a valid wfd
        const char * dot = strrchr(nfd->wfd.cFileName, '.');
        if (dot) {
            ++dot;
            if (!strcmp(dot, extension)) {
                return nfd->wfd.cFileName;
            }
        }
    }

    FindClose(nfd->handle);
    nfd->handle = INVALID_HANDLE_VALUE;
    nfd->didFirstFile = 0;
    return NULL;
}

#else
#include <dirent.h>
typedef struct NextFilenameData
{
    DIR * dir;
} NextFilenameData;

static const char * nextFilename(const char * parentDir, const char * extension, NextFilenameData * nfd)
{
    if (!nfd->dir) {
        nfd->dir = opendir(parentDir);
        if (!nfd->dir) {
            return NULL;
        }
    }

    struct dirent * entry;
    while ((entry = readdir(nfd->dir)) != NULL) {
        const char * dot = strrchr(entry->d_name, '.');
        if (dot) {
            ++dot;
            if (!strcmp(dot, extension)) {
                return entry->d_name;
            }
        }
    }

    closedir(nfd->dir);
    nfd->dir = NULL;
    return NULL;
}
#endif

static int generateTests(const char * dataDir)
{
    printf("AVIF Test Suite: Generating Tests...\n");

    int retCode = 0;
    cJSON * tests = cJSON_CreateArray();

    struct QuantizerPairs
    {
        int minQP;
        int maxQP;
    } quantizerPairs[] = {
        { 0, 0 },  // lossless
        { 4, 40 }, // Q60
        { 24, 60 } // Q40
    };
    const int quantizerPairsCount = sizeof(quantizerPairs) / sizeof(quantizerPairs[0]);

    NextFilenameData nfd;
    memset(&nfd, 0, sizeof(nfd));
    const char * filename = nextFilename(dataDir, "y4m", &nfd);
    for (; filename != NULL; filename = nextFilename(dataDir, "y4m", &nfd)) {
        avifCodecChoice encodeChoices[] = { AVIF_CODEC_CHOICE_AOM, AVIF_CODEC_CHOICE_RAV1E };
        const int encodeChoiceCount = sizeof(encodeChoices) / sizeof(encodeChoices[0]);
        for (int encodeChoiceIndex = 0; encodeChoiceIndex < encodeChoiceCount; ++encodeChoiceIndex) {
            avifCodecChoice decodeChoices[] = { AVIF_CODEC_CHOICE_AOM, AVIF_CODEC_CHOICE_DAV1D, AVIF_CODEC_CHOICE_LIBGAV1 };
            const int decodeChoiceCount = sizeof(decodeChoices) / sizeof(decodeChoices[0]);
            for (int decodeChoiceIndex = 0; decodeChoiceIndex < decodeChoiceCount; ++decodeChoiceIndex) {
                for (int qpIndex = 0; qpIndex < quantizerPairsCount; ++qpIndex) {
                    int speeds[] = { AVIF_SPEED_DEFAULT, 10 };
                    int speedCount = sizeof(speeds) / sizeof(speeds[0]);
                    for (int speedIndex = 0; speedIndex < speedCount; ++speedIndex) {
                        TestCase * tc = testCaseCreate();
                        testCaseSetInputFilename(tc, filename);
                        tc->encodeChoice = encodeChoices[encodeChoiceIndex];
                        tc->decodeChoice = decodeChoices[decodeChoiceIndex];
                        tc->active = AVIF_TRUE;
                        tc->speed = speeds[speedIndex];
                        tc->minQuantizer = quantizerPairs[qpIndex].minQP;
                        tc->maxQuantizer = quantizerPairs[qpIndex].maxQP;
                        testCaseGenerateName(tc);

                        if (!testCaseRun(tc, dataDir, AVIF_TRUE)) {
                            printf("ERROR: Failed to run test case: %s\n", tc->name);
                            goto cleanup;
                        }

                        cJSON_AddItemToArray(tests, testCaseToJSON(tc));
                        testCaseDestroy(tc);
                    }
                }
            }
        }
    }

    char * jsonString = cJSON_PrintUnformatted(tests);

    char testJSONFilename[2048];
    snprintf(testJSONFilename, sizeof(testJSONFilename), "%s/tests.json", dataDir);
    testJSONFilename[sizeof(testJSONFilename) - 1] = 0;
    FILE * f = fopen(testJSONFilename, "wb");
    if (f) {
        fprintf(f, "%s", jsonString);
        fclose(f);

        printf("Wrote: %s\n", testJSONFilename);
    } else {
        printf("Failed to write: %s\n", testJSONFilename);
        retCode = 1;
    }
    free(jsonString);

cleanup:
    cJSON_Delete(tests);
    return retCode;
}

static int runTests(const char * dataDir, const char * testFilter)
{
    (void)testFilter;
    printf("AVIF Test Suite: Running Tests...\n");

    char testJSONFilename[2048];
    snprintf(testJSONFilename, sizeof(testJSONFilename), "%s/tests.json", dataDir);
    testJSONFilename[sizeof(testJSONFilename) - 1] = 0;
    FILE * f = fopen(testJSONFilename, "rb");
    if (!f) {
        printf("ERROR: Failed to read: %s\n", testJSONFilename);
        return 1;
    }

    fseek(f, 0, SEEK_END);
    size_t fileSize = ftell(f);
    fseek(f, 0, SEEK_SET);

    char * rawJSON = malloc(fileSize + 1);
    if (fread(rawJSON, 1, fileSize, f) != fileSize) {
        printf("ERROR: Failed to read: %s\n", testJSONFilename);
        free(rawJSON);
        fclose(f);
        return 1;
    }
    rawJSON[fileSize] = 0;
    fclose(f);

    cJSON * tests = cJSON_Parse(rawJSON);
    if (!tests || !cJSON_IsArray(tests)) {
        if (tests) {
            cJSON_Delete(tests);
        }
        printf("ERROR: Invalid JSON: %s\n", testJSONFilename);
        return 1;
    }
    free(rawJSON);

    int totalCount = 0;
    int skippedCount = 0;
    int failedCount = 0;
    for (cJSON * t = tests->child; t != NULL; t = t->next) {
        if (!cJSON_IsObject(t)) {
            ++skippedCount;
            continue;
        }

        TestCase * tc = testCaseFromJSON(t);
        if (!tc || !tc->active) {
            ++skippedCount;
            testCaseDestroy(tc);
            continue;
        }

        if (testFilter) {
            if (strstr(tc->name, testFilter) == NULL) {
                ++skippedCount;
                testCaseDestroy(tc);
                continue;
            }
        }

        // Skip the test if the requested encoder or decoder is not available.
        if (!avifCodecName(tc->encodeChoice, AVIF_CODEC_FLAG_CAN_ENCODE) || !avifCodecName(tc->decodeChoice, AVIF_CODEC_FLAG_CAN_DECODE)) {
            ++skippedCount;
            testCaseDestroy(tc);
            continue;
        }

        if (!testCaseRun(tc, dataDir, AVIF_FALSE)) {
            ++failedCount;
        }
        ++totalCount;

        testCaseDestroy(tc);
    }

    printf("Complete. %d tests ran, %d skipped, %d failed.\n", totalCount, skippedCount, failedCount);

    cJSON_Delete(tests);
    return (failedCount == 0) ? 0 : 1;
}

static void syntax(void)
{
    fprintf(stderr,
            "Syntax: aviftest [options] dataDir [testFilter]\n"
            "Options:\n"
            "    -g : Generate tests\n");
}

int main(int argc, char * argv[])
{
    const char * dataDir = NULL;
    const char * testFilter = NULL;
    avifBool generate = AVIF_FALSE;

#ifdef WIN32_MEMORY_LEAK_DETECTION
    _CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF);
    // _CrtSetBreakAlloc(2906);
#endif

    // Parse cmdline
    for (int i = 1; i < argc; ++i) {
        char * arg = argv[i];
        if (!strcmp(arg, "-g")) {
            generate = AVIF_TRUE;
        } else if (dataDir == NULL) {
            dataDir = arg;
        } else if (testFilter == NULL) {
            testFilter = arg;
        } else {
            fprintf(stderr, "Too many positional arguments: %s\n", arg);
            syntax();
            return 1;
        }
    }

    // Verify all required args were set
    if (dataDir == NULL) {
        fprintf(stderr, "dataDir is required, bailing out.\n");
        syntax();
        return 1;
    }

    setbuf(stdout, NULL);

    char codecVersions[256];
    avifCodecVersions(codecVersions);
    printf("Codec Versions: %s\n", codecVersions);
    printf("Test Data Dir : %s\n", dataDir);

    int retCode = 1;
    if (generate) {
        retCode = generateTests(dataDir);
    } else {
        retCode = runTests(dataDir, testFilter);
    }

    if (retCode == 0) {
        printf("AVIF Test Suite: Complete.\n");
    } else {
        printf("AVIF Test Suite: Failed.\n");
    }
    return retCode;
}
