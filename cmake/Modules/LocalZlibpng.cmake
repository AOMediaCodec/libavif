set(AVIF_ZLIB_GIT_TAG v1.3.1)
set(AVIF_LIBPNG_GIT_TAG v1.6.47)

if(EXISTS "${AVIF_SOURCE_DIR}/ext/zlib")
    message(STATUS "libavif(AVIF_ZLIBPNG=LOCAL): ext/zlib found; using as FetchContent SOURCE_DIR")
    set(FETCHCONTENT_SOURCE_DIR_ZLIB "${AVIF_SOURCE_DIR}/ext/zlib")
    message(CHECK_START "libavif(AVIF_ZLIBPNG=LOCAL): configuring zlib")
    set(ZLIB_SOURCE_DIR "${FETCHCONTENT_SOURCE_DIR_ZLIB}")
else()
    message(CHECK_START "libavif(AVIF_ZLIBPNG=LOCAL): fetching and configuring zlib")
    set(ZLIB_SOURCE_DIR "${FETCHCONTENT_BASE_DIR}/zlib-src")
endif()

set(ZLIB_BINARY_DIR "${FETCHCONTENT_BASE_DIR}/zlib")
if(ANDROID_ABI)
    set(ZLIB_BINARY_DIR "${ZLIB_BINARY_DIR}/${ANDROID_ABI}")
endif()

# The patch is taken from Google's or-tools
# https://github.com/google/or-tools/blob/73bef34d95768a0fc0676023bd68111946deb417/cmake/dependencies/CMakeLists.txt
FetchContent_Declare(
    zlib
    GIT_REPOSITORY "https://github.com/madler/zlib.git"
    SOURCE_DIR "${ZLIB_SOURCE_DIR}" BINARY_DIR "${ZLIB_BINARY_DIR}"
    GIT_TAG "${AVIF_ZLIB_GIT_TAG}"
    GIT_SHALLOW ON
    PATCH_COMMAND git apply --ignore-whitespace "${CMAKE_CURRENT_SOURCE_DIR}/ext/zlib.patch"
    UPDATE_COMMAND ""
)

# Put the value of ZLIB_INCLUDE_DIR in the cache. This works around cmake behavior that has been updated by
# cmake policy CMP0102 in cmake 3.17. Remove the CACHE workaround when we require cmake 3.17 or later. See
# https://gitlab.kitware.com/cmake/cmake/-/issues/21343.
set(ZLIB_INCLUDE_DIR "${ZLIB_SOURCE_DIR}" CACHE PATH "zlib include dir")
set(ZLIB_BUILD_EXAMPLES OFF CACHE BOOL "")

if(NOT zlib_POPULATED)
    avif_fetchcontent_populate_cmake(zlib)
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
set(ZLIB_USE_STATIC_LIBS ON CACHE BOOL "")
set(PNG_SHARED OFF CACHE BOOL "")
set(PNG_TESTS OFF CACHE BOOL "")
set(PNG_TOOLS OFF CACHE BOOL "")

set(LIBPNG_BINARY_DIR "${FETCHCONTENT_BASE_DIR}/libpng")
if(ANDROID_ABI)
    set(LIBPNG_BINARY_DIR "${LIBPNG_BINARY_DIR}/${ANDROID_ABI}")
endif()

FetchContent_Declare(
    libpng
    GIT_REPOSITORY "https://github.com/glennrp/libpng.git"
    BINARY_DIR "${LIBPNG_BINARY_DIR}"
    GIT_TAG "${AVIF_LIBPNG_GIT_TAG}"
    GIT_SHALLOW ON
    UPDATE_COMMAND ""
)

avif_fetchcontent_populate_cmake(libpng)

set(PNG_PNG_INCLUDE_DIR "${libpng_SOURCE_DIR}")
include_directories("${libpng_BINARY_DIR}")
set(ANDROID ${PREV_ANDROID})

set_target_properties(png_static ZLIB PROPERTIES AVIF_LOCAL ON)
add_library(PNG::PNG ALIAS png_static)

message(CHECK_PASS "complete")
