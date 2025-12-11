set(AVIF_ZLIB_GIT_TAG v1.3.1.2)
# v1.6.51 and v1.6.53 confuse CMake by giving Unix flags with MSVC on ci-windows.yml
set(AVIF_LIBPNG_GIT_TAG v1.6.51)

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

FetchContent_Declare(
    zlib
    GIT_REPOSITORY "https://github.com/madler/zlib.git"
    SOURCE_DIR "${ZLIB_SOURCE_DIR}" BINARY_DIR "${ZLIB_BINARY_DIR}"
    GIT_TAG "${AVIF_ZLIB_GIT_TAG}"
    GIT_SHALLOW ON
    UPDATE_COMMAND ""
)

set(ZLIB_BUILD_TESTING OFF CACHE BOOL "")
set(ZLIB_BUILD_SHARED OFF CACHE BOOL "")
set(ZLIB_BUILD_STATIC ON CACHE BOOL "")

if(NOT zlib_POPULATED)
    avif_fetchcontent_populate_cmake(zlib)
endif()

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

set_target_properties(png_static zlibstatic PROPERTIES AVIF_LOCAL ON)
add_library(PNG::PNG ALIAS png_static)

message(CHECK_PASS "complete")
