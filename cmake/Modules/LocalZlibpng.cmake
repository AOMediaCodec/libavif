set(AVIF_ZLIB_GIT_TAG v1.3.2)
set(AVIF_LIBPNG_GIT_TAG v1.6.55)

if(EXISTS "${AVIF_SOURCE_DIR}/ext/zlib")
    message(STATUS "libavif(AVIF_ZLIBPNG=LOCAL): ext/zlib found; using as FetchContent SOURCE_DIR")
    set(FETCHCONTENT_SOURCE_DIR_ZLIB "${AVIF_SOURCE_DIR}/ext/zlib")
    message(CHECK_START "libavif(AVIF_ZLIBPNG=LOCAL): configuring zlib")
else()
    message(CHECK_START "libavif(AVIF_ZLIBPNG=LOCAL): fetching and configuring zlib")
endif()

FetchContent_Declare(
    zlib
    EXCLUDE_FROM_ALL
    GIT_REPOSITORY "https://github.com/madler/zlib.git"
    GIT_TAG "${AVIF_ZLIB_GIT_TAG}"
    GIT_SHALLOW ON
    UPDATE_COMMAND ""
)

set(ZLIB_BUILD_TESTING OFF CACHE BOOL "")
set(ZLIB_BUILD_SHARED OFF CACHE BOOL "")
set(ZLIB_BUILD_STATIC ON CACHE BOOL "")

avif_fetchcontent_makeavailable_cmake(zlib)

if(NOT TARGET ZLIB::ZLIB)
    add_library(ZLIB::ZLIB ALIAS zlibstatic)
endif()

set(CMAKE_DEBUG_POSTFIX "")

message(CHECK_PASS "complete")

if(EXISTS "${AVIF_SOURCE_DIR}/ext/libpng")
    message(STATUS "libavif(AVIF_ZLIBPNG=LOCAL): ext/libpng found; using as FetchContent SOURCE_DIR")
    set(FETCHCONTENT_SOURCE_DIR_LIBPNG "${AVIF_SOURCE_DIR}/ext/libpng")
    message(CHECK_START "libavif(AVIF_ZLIBPNG=LOCAL): configuring libpng")
else()
    message(CHECK_START "libavif(AVIF_ZLIBPNG=LOCAL): fetching and configuring libpng")
endif()

# This is the only way I could avoid libpng going crazy if it found awk.exe, seems benign otherwise
set(PREV_ANDROID ${ANDROID})
set(ANDROID TRUE)
set(ZLIB_LIBRARY ZLIB)
set(ZLIB_ROOT "${zlib_SOURCE_DIR}" CACHE STRING "" FORCE)
set(PNG_SHARED OFF CACHE BOOL "")
set(PNG_TESTS OFF CACHE BOOL "")
set(PNG_TOOLS OFF CACHE BOOL "")

FetchContent_Declare(
    libpng
    EXCLUDE_FROM_ALL
    GIT_REPOSITORY "https://github.com/pnggroup/libpng.git"
    GIT_TAG "${AVIF_LIBPNG_GIT_TAG}"
    GIT_SHALLOW ON
    UPDATE_COMMAND ""
)

avif_fetchcontent_makeavailable_cmake(libpng)

set(PNG_PNG_INCLUDE_DIR "${libpng_SOURCE_DIR}")
include_directories("${libpng_BINARY_DIR}")
set(ANDROID ${PREV_ANDROID})

set_target_properties(png_static zlibstatic PROPERTIES AVIF_LOCAL ON)
add_library(PNG::PNG ALIAS png_static)

message(CHECK_PASS "complete")
