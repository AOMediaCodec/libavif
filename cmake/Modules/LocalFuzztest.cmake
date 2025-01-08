set(AVIF_FUZZTEST_TAG "8fdc787e8c64066619b69f3ee9d168c00aeddab2")

set(FUZZTEST_SOURCE_DIR "${AVIF_SOURCE_DIR}/ext/fuzztest")

if(EXISTS "${FUZZTEST_SOURCE_DIR}")
    message(STATUS "libavif(AVIF_FUZZTEST=LOCAL): folder found at ${FUZZTEST_SOURCE_DIR}")
    set(FUZZTEST_BINARY_DIR "${FUZZTEST_SOURCE_DIR}/build.libavif")
    add_subdirectory(${FUZZTEST_SOURCE_DIR} ${FUZZTEST_BINARY_DIR} EXCLUDE_FROM_ALL)
else()
    message(STATUS "libavif(AVIF_FUZZTEST=LOCAL): compiled library not found at ${LIB_FILENAME}; using FetchContent")

    message(CHECK_START "libavif(AVIF_FUZZTEST=LOCAL): configuring fuzztest")

    set(FUZZTEST_SOURCE_DIR "${FETCHCONTENT_BASE_DIR}/fuzztest-src")
    set(FUZZTEST_BINARY_DIR "${FETCHCONTENT_BASE_DIR}/fuzztest-build")
    FetchContent_Declare(
        fuzztest
        GIT_REPOSITORY "https://github.com/google/fuzztest.git"
        BINARY_DIR "${FUZZTEST_BINARY_DIR}"
        GIT_TAG "${AVIF_FUZZTEST_TAG}"
        # Fixes for https://github.com/google/fuzztest/issues/1124
        PATCH_COMMAND
            sed -i.bak -e "s/-fsanitize=address//g" cmake/FuzzTestFlagSetup.cmake && sed -i.bak -e "s/-DADDRESS_SANITIZER//g"
            cmake/FuzzTestFlagSetup.cmake &&
            # Fixes for https://github.com/google/fuzztest/issues/1125
            sed -i.bak -e "s/if (IsEnginePlaceholderInput(data))/if (data.size() == 0)/" fuzztest/internal/compatibility_mode.cc
            && sed -i.bak -e "s/set(GTEST_HAS_ABSL ON)/set(GTEST_HAS_ABSL OFF)/" cmake/BuildDependencies.cmake
    )

    avif_fetchcontent_populate_cmake(fuzztest)

    message(CHECK_PASS "complete")
endif()
