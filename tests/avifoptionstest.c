// SPDX-License-Identifier: BSD-2-Clause

#include "avif/internal.h"

#include <stdio.h>
#include <string.h>

static int failures;

#define CHECK(condition)                                                    \
    do {                                                                    \
        if (!(condition)) {                                                 \
            fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #condition); \
            ++failures;                                                     \
            return;                                                         \
        }                                                                   \
    } while (0)

static void checkOption(const avifCodecSpecificOptions * options, uint32_t index, const char * key, const char * value)
{
    CHECK(index < options->count);
    CHECK(!strcmp(options->entries[index].key, key));
    CHECK(!strcmp(options->entries[index].value, value));
}

static void testNormalOperations(void)
{
    avifCodecSpecificOptions * options = avifCodecSpecificOptionsCreate();
    CHECK(options != NULL);
    const uint32_t initialCapacity = options->capacity;
    CHECK(initialCapacity == 4);

    CHECK(avifCodecSpecificOptionsSet(options, "a", "one") == AVIF_RESULT_OK);
    CHECK(avifCodecSpecificOptionsSet(options, "b", "two") == AVIF_RESULT_OK);
    CHECK(options->count == 2);
    checkOption(options, 0, "a", "one");
    checkOption(options, 1, "b", "two");

    CHECK(avifCodecSpecificOptionsSet(options, "a", "updated") == AVIF_RESULT_OK);
    CHECK(options->count == 2);
    checkOption(options, 0, "a", "updated");

    CHECK(avifCodecSpecificOptionsSet(options, "b", NULL) == AVIF_RESULT_OK);
    CHECK(options->count == 1);
    checkOption(options, 0, "a", "updated");

    CHECK(avifCodecSpecificOptionsSet(options, "c", "three") == AVIF_RESULT_OK);
    CHECK(avifCodecSpecificOptionsSet(options, "d", "four") == AVIF_RESULT_OK);
    CHECK(avifCodecSpecificOptionsSet(options, "e", "five") == AVIF_RESULT_OK);
    CHECK(options->count == initialCapacity);
    CHECK(avifCodecSpecificOptionsSet(options, "f", "six") == AVIF_RESULT_OK);
    CHECK(options->count == initialCapacity + 1);
    CHECK(options->capacity == initialCapacity * 2);
    checkOption(options, 4, "f", "six");

    avifCodecSpecificOptionsClear(options);
    CHECK(options->count == 0);
    CHECK(options->capacity == initialCapacity * 2);
    avifCodecSpecificOptionsDestroy(options);
}

#if defined(AVIF_OPTIONSTEST_WRAP_MALLOC)

extern void * __real_malloc(size_t size);

// A nonnegative value permits that many allocations before the next one fails.
static int mallocFailuresAfter = -1;

void * __wrap_malloc(size_t size)
{
    if (mallocFailuresAfter == 0) {
        return NULL;
    }
    if (mallocFailuresAfter > 0) {
        --mallocFailuresAfter;
    }
    return __real_malloc(size);
}

typedef struct optionState
{
    avifCodecSpecificOption * entries;
    uint32_t count;
    uint32_t capacity;
    char * keys[4];
    char * values[4];
} optionState;

static optionState saveOptionState(const avifCodecSpecificOptions * options)
{
    optionState state;
    memset(&state, 0, sizeof(state));
    state.entries = options->entries;
    state.count = options->count;
    state.capacity = options->capacity;
    if (state.count > 4) {
        fprintf(stderr, "%s:%d: state.count <= 4\n", __FILE__, __LINE__);
        ++failures;
        state.count = 0;
        return state;
    }
    for (uint32_t i = 0; i < state.count; ++i) {
        state.keys[i] = options->entries[i].key;
        state.values[i] = options->entries[i].value;
    }
    return state;
}

static void checkOptionState(const avifCodecSpecificOptions * options, const optionState * state)
{
    CHECK(options->entries == state->entries);
    CHECK(options->count == state->count);
    CHECK(options->capacity == state->capacity);
    for (uint32_t i = 0; i < state->count; ++i) {
        CHECK(options->entries[i].key == state->keys[i]);
        CHECK(options->entries[i].value == state->values[i]);
    }
}

static void testReplaceAllocationFailure(void)
{
    avifCodecSpecificOptions * options = avifCodecSpecificOptionsCreate();
    CHECK(options != NULL);
    CHECK(avifCodecSpecificOptionsSet(options, "key", "old") == AVIF_RESULT_OK);
    const optionState state = saveOptionState(options);

    mallocFailuresAfter = 0;
    const avifResult result = avifCodecSpecificOptionsSet(options, "key", "new");
    mallocFailuresAfter = -1;
    CHECK(result == AVIF_RESULT_OUT_OF_MEMORY);
    checkOptionState(options, &state);
    checkOption(options, 0, "key", "old");
    avifCodecSpecificOptionsDestroy(options);
}

static void testAddKeyAndValueAllocationFailures(void)
{
    avifCodecSpecificOptions * options = avifCodecSpecificOptionsCreate();
    CHECK(options != NULL);
    CHECK(avifCodecSpecificOptionsSet(options, "base", "value") == AVIF_RESULT_OK);
    const optionState state = saveOptionState(options);

    mallocFailuresAfter = 0;
    avifResult result = avifCodecSpecificOptionsSet(options, "new", "value");
    mallocFailuresAfter = -1;
    CHECK(result == AVIF_RESULT_OUT_OF_MEMORY);
    checkOptionState(options, &state);

    mallocFailuresAfter = 1;
    result = avifCodecSpecificOptionsSet(options, "new", "value");
    mallocFailuresAfter = -1;
    CHECK(result == AVIF_RESULT_OUT_OF_MEMORY);
    checkOptionState(options, &state);
    checkOption(options, 0, "base", "value");
    avifCodecSpecificOptionsDestroy(options);
}

static void testGrowthAllocationFailure(void)
{
    avifCodecSpecificOptions * options = avifCodecSpecificOptionsCreate();
    CHECK(options != NULL);
    CHECK(options->capacity == 4);
    CHECK(avifCodecSpecificOptionsSet(options, "a", "1") == AVIF_RESULT_OK);
    CHECK(avifCodecSpecificOptionsSet(options, "b", "2") == AVIF_RESULT_OK);
    CHECK(avifCodecSpecificOptionsSet(options, "c", "3") == AVIF_RESULT_OK);
    CHECK(avifCodecSpecificOptionsSet(options, "d", "4") == AVIF_RESULT_OK);
    const optionState state = saveOptionState(options);

    mallocFailuresAfter = 2;
    const avifResult result = avifCodecSpecificOptionsSet(options, "e", "5");
    mallocFailuresAfter = -1;
    CHECK(result == AVIF_RESULT_OUT_OF_MEMORY);
    checkOptionState(options, &state);
    checkOption(options, 0, "a", "1");
    checkOption(options, 3, "d", "4");
    avifCodecSpecificOptionsDestroy(options);
}

#endif

int main(void)
{
    testNormalOperations();
#if defined(AVIF_OPTIONSTEST_WRAP_MALLOC)
    testReplaceAllocationFailure();
    testAddKeyAndValueAllocationFailures();
    testGrowthAllocationFailure();
#endif
    return failures != 0;
}
