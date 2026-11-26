set(AVIF_ARGPARSE_GIT_TAG v3.2)

set(ARGPARSE_INCLUDE_DIR "${AVIF_SOURCE_DIR}/ext/argparse/include")
if(EXISTS "${ARGPARSE_INCLUDE_DIR}/argparse/argparse.hpp")
    message(STATUS "libavif(AVIF_ARGPARSE=LOCAL): header found in ${ARGPARSE_INCLUDE_DIR}")
    add_library(argparse INTERFACE)
    add_library(argparse::argparse ALIAS argparse)
    set_target_properties(argparse PROPERTIES AVIF_LOCAL ON)
    target_include_directories(argparse SYSTEM INTERFACE "${ARGPARSE_INCLUDE_DIR}")
else()
    message(STATUS "libavif(AVIF_ARGPARSE=LOCAL): header not found in ${ARGPARSE_INCLUDE_DIR}; using FetchContent")
    if(EXISTS "${AVIF_SOURCE_DIR}/ext/argparse")
        message(STATUS "libavif(AVIF_ARGPARSE=LOCAL): ext/argparse found; using as FetchContent SOURCE_DIR")
        set(FETCHCONTENT_SOURCE_DIR_ARGPARSE "${AVIF_SOURCE_DIR}/ext/argparse")
        message(CHECK_START "libavif(AVIF_ARGPARSE=LOCAL): configuring argparse")
    else()
        message(CHECK_START "libavif(AVIF_ARGPARSE=LOCAL): fetching and configuring argparse")
    endif()

    set(ARGPARSE_INSTALL OFF CACHE INTERNAL "")
    set(ARGPARSE_BUILD_TESTS OFF CACHE INTERNAL "")
    set(ARGPARSE_BUILD_SAMPLES OFF CACHE INTERNAL "")

    FetchContent_Declare(
        argparse
        EXCLUDE_FROM_ALL
        GIT_REPOSITORY "https://github.com/p-ranav/argparse.git"
        GIT_TAG ${AVIF_ARGPARSE_GIT_TAG}
        GIT_SHALLOW ON
        UPDATE_COMMAND ""
    )
    avif_fetchcontent_makeavailable_cmake(argparse)

    set_target_properties(argparse PROPERTIES AVIF_LOCAL ON)

    message(CHECK_PASS "complete")
endif()
