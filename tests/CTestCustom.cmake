set(CTEST_CUSTOM_TESTS_IGNORE
    # Ignore failing tests brought by `add_subdirectory(${AVIF_SOURCE_DIR}/ext/fuzztest)` when AVIF_ENABLE_FUZZTEST is ON
    antlr4_tests_NOT_BUILT
)
