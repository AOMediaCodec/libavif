// Copyright 2020 Joe Drago. All rights reserved.
// SPDX-License-Identifier: BSD-2-Clause

// #define WIN32_MEMORY_LEAK_DETECTION
#ifdef WIN32_MEMORY_LEAK_DETECTION
#define _CRTDBG_MAP_ALLOC
#include <crtdbg.h>
#endif

#include "avif/avif.h"

#include "testcase.h"

#include <inttypes.h>
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

static int generateEncodeDecodeTests(const char * dataDir)
{
    printf("AVIF Test Suite: Generating Encode/Decode Tests...\n");

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

static int runEncodeDecodeTests(const char * dataDir, const char * testFilter)
{
    printf("AVIF Test Suite: Running Encode/Decode Tests...\n");

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

typedef struct avifIOTestReader
{
    avifIO io;
    avifROData rodata;
    size_t availableBytes;
} avifIOTestReader;

static avifResult avifIOTestReaderRead(struct avifIO * io, uint32_t readFlags, uint64_t offset, size_t size, avifROData * out)
{
    // printf("avifIOTestReaderRead offset %" PRIu64 " size %zu\n", offset, size);

    if (readFlags != 0) {
        // Unsupported readFlags
        return AVIF_RESULT_IO_ERROR;
    }

    avifIOTestReader * reader = (avifIOTestReader *)io;

    // Sanitize/clamp incoming request
    if (offset > reader->rodata.size) {
        // The offset is past the end of the buffer.
        return AVIF_RESULT_IO_ERROR;
    }
    uint64_t availableSize = reader->rodata.size - offset;
    if (size > availableSize) {
        size = availableSize;
    }

    if (offset > reader->availableBytes) {
        return AVIF_RESULT_WAITING_ON_IO;
    }
    if (size > (reader->availableBytes - offset)) {
        return AVIF_RESULT_WAITING_ON_IO;
    }

    out->data = reader->rodata.data + offset;
    out->size = size;
    return AVIF_RESULT_OK;
}

static void avifIOTestReaderDestroy(struct avifIO * io)
{
    avifFree(io);
}

static avifIOTestReader * avifIOCreateTestReader(const uint8_t * data, size_t size)
{
    avifIOTestReader * reader = avifAlloc(sizeof(avifIOTestReader));
    memset(reader, 0, sizeof(avifIOTestReader));
    reader->io.destroy = avifIOTestReaderDestroy;
    reader->io.read = avifIOTestReaderRead;
    reader->io.sizeHint = size;
    reader->io.persistent = AVIF_TRUE;
    reader->rodata.data = data;
    reader->rodata.size = size;
    return reader;
}

#define FILENAME_MAX_LENGTH 2047

static int runIOTests(const char * dataDir)
{
    printf("AVIF Test Suite: Running IO Tests...\n");

    static const char * ioSuffix = "/io/";

    char ioDir[FILENAME_MAX_LENGTH + 1];
    size_t dataDirLen = strlen(dataDir);
    size_t ioSuffixLen = strlen(ioSuffix);

    if ((dataDirLen + ioSuffixLen) > FILENAME_MAX_LENGTH) {
        printf("Path too long: %s\n", dataDir);
        return 1;
    }
    strcpy(ioDir, dataDir);
    strcat(ioDir, ioSuffix);
    size_t ioDirLen = strlen(ioDir);

    int retCode = 0;

    NextFilenameData nfd;
    memset(&nfd, 0, sizeof(nfd));
    avifRWData fileBuffer = AVIF_DATA_EMPTY;
    const char * filename = nextFilename(ioDir, "avif", &nfd);
    for (; filename != NULL; filename = nextFilename(ioDir, "avif", &nfd)) {
        char fullFilename[FILENAME_MAX_LENGTH + 1];
        size_t filenameLen = strlen(filename);
        if ((ioDirLen + filenameLen) > FILENAME_MAX_LENGTH) {
            printf("Path too long: %s\n", filename);
            return 1;
        }
        strcpy(fullFilename, ioDir);
        strcat(fullFilename, filename);

        FILE * f = fopen(fullFilename, "rb");
        if (!f) {
            printf("Can't open for read: %s\n", filename);
            return 1;
        }
        fseek(f, 0, SEEK_END);
        size_t fileSize = ftell(f);
        fseek(f, 0, SEEK_SET);
        avifRWDataRealloc(&fileBuffer, fileSize);
        if (fread(fileBuffer.data, 1, fileSize, f) != fileSize) {
            printf("Can't read entire file: %s\n", filename);
            fclose(f);
            return 1;
        }
        fclose(f);

        avifDecoder * decoder = avifDecoderCreate();
        avifIOTestReader * io = avifIOCreateTestReader(fileBuffer.data, fileBuffer.size);
        avifDecoderSetIO(decoder, (avifIO *)io);

        for (int pass = 0; pass < 4; ++pass) {
            io->io.persistent = ((pass % 2) == 0);
            decoder->ignoreExif = decoder->ignoreXMP = (pass < 2);

            // Slowly pretend to have streamed-in / downloaded more and more bytes
            avifResult parseResult = AVIF_RESULT_UNKNOWN_ERROR;
            for (io->availableBytes = 0; io->availableBytes <= io->io.sizeHint; ++io->availableBytes) {
                parseResult = avifDecoderParse(decoder);
                if (parseResult == AVIF_RESULT_WAITING_ON_IO) {
                    continue;
                }
                if (parseResult != AVIF_RESULT_OK) {
                    retCode = 1;
                }

                printf("File: [%s @ %zu / %" PRIu64 " bytes, %s, %s] parse returned: %s\n",
                       filename,
                       io->availableBytes,
                       io->io.sizeHint,
                       io->io.persistent ? "Persistent" : "NonPersistent",
                       decoder->ignoreExif ? "IgnoreMetadata" : "Metadata",
                       avifResultToString(parseResult));
                break;
            }

            if (parseResult == AVIF_RESULT_OK) {
                for (; io->availableBytes <= io->io.sizeHint; ++io->availableBytes) {
                    avifResult nextImageResult = avifDecoderNextImage(decoder);
                    if (nextImageResult == AVIF_RESULT_WAITING_ON_IO) {
                        continue;
                    }
                    if (nextImageResult != AVIF_RESULT_OK) {
                        retCode = 1;
                    }

                    printf("File: [%s @ %zu / %" PRIu64 " bytes, %s, %s] nextImage returned: %s\n",
                           filename,
                           io->availableBytes,
                           io->io.sizeHint,
                           io->io.persistent ? "Persistent" : "NonPersistent",
                           decoder->ignoreExif ? "IgnoreMetadata" : "Metadata",
                           avifResultToString(nextImageResult));
                    break;
                }
            }
        }

        avifDecoderDestroy(decoder);
    }

    avifRWDataFree(&fileBuffer);
    return retCode;
}

static void syntax(void)
{
    fprintf(stderr,
            "Syntax: aviftest [options] dataDir [testFilter]\n"
            "Options:\n"
            "    -g : Generate Encode/Decode tests\n"
            "    --io-only : Run IO tests only\n");
}

int main(int argc, char * argv[])
{
    const char * dataDir = NULL;
    const char * testFilter = NULL;
    avifBool generate = AVIF_FALSE;
    avifBool ioOnly = AVIF_FALSE;

#ifdef WIN32_MEMORY_LEAK_DETECTION
    _CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF);
    // _CrtSetBreakAlloc(2906);
#endif

    // Parse cmdline
    for (int i = 1; i < argc; ++i) {
        char * arg = argv[i];
        if (!strcmp(arg, "-g")) {
            generate = AVIF_TRUE;
        } else if (!strcmp(arg, "--io-only")) {
            ioOnly = AVIF_TRUE;
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
        retCode = generateEncodeDecodeTests(dataDir);
    } else {
        retCode = runIOTests(dataDir);
        if ((retCode == 0) && !ioOnly) {
            retCode = runEncodeDecodeTests(dataDir, testFilter);
        }
    }

    if (retCode == 0) {
        printf("AVIF Test Suite: Complete.\n");
    } else {
        printf("AVIF Test Suite: Failed.\n");
    }
    return retCode;
}
