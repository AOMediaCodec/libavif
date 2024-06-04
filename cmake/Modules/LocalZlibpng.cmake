# To upgrade libpng to > v1.6.40, you need zlib containing f1f503da85d52e56aae11557b4d79a42bcaa2b86
# hence a version > v1.3.1 .
set(AVIF_LOCAL_ZLIB_GIT_TAG v1.3.1)
set(AVIF_LOCAL_LIBPNG_GIT_TAG v1.6.40)

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
    GIT_TAG "${AVIF_LOCAL_ZLIB_GIT_TAG}"
    GIT_SHALLOW ON
    UPDATE_COMMAND ""
)

# Put the value of ZLIB_INCLUDE_DIR in the cache. This works around cmake behavior that has been updated by
# cmake policy CMP0102 in cmake 3.17. Remove the CACHE workaround when we require cmake 3.17 or later. See
# https://gitlab.kitware.com/cmake/cmake/-/issues/21343.
set(ZLIB_INCLUDE_DIR "${ZLIB_SOURCE_DIR}" CACHE PATH "zlib include dir")
# This include_directories() call must be before add_subdirectory(ext/zlib) to work around the
# zlib/CMakeLists.txt bug fixed by https://github.com/madler/zlib/pull/818.
include_directories(SYSTEM $<BUILD_INTERFACE:${ZLIB_INCLUDE_DIR}>)

if(NOT zlib_POPULATED)
    avif_fetchcontent_populate_cmake(zlib)

    # Re-enable example and example64 targets, as these are used by tests
    if(AVIF_BUILD_TESTS)
        set_property(TARGET example PROPERTY EXCLUDE_FROM_ALL FALSE)
        if(TARGET example64)
            set_property(TARGET example64 PROPERTY EXCLUDE_FROM_ALL FALSE)
        endif()
    endif()
endif()

target_include_directories(zlibstatic INTERFACE $<BUILD_INTERFACE:${ZLIB_INCLUDE_DIR}>)

# This include_directories() call and the previous include_directories() call provide the zlib
# include directories for add_subdirectory(ext/libpng). Because we set PNG_BUILD_ZLIB,
# libpng/CMakeLists.txt won't call find_package(ZLIB REQUIRED) and will see an empty
# ${ZLIB_INCLUDE_DIR}.
include_directories("${zlib_BINARY_DIR}")
set(CMAKE_DEBUG_POSTFIX "")

add_library(ZLIB::ZLIB ALIAS zlibstatic)

# TODO Uncomment for zlib > v1.3
# install(TARGETS zlib zlibstatic EXPORT zlib)
# install(EXPORT zlib
#         DESTINATION ${CMAKE_INSTALL_LIBDIR}/zlib)

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
set(PNG_BUILD_ZLIB "${zlib_SOURCE_DIR}" CACHE STRING "" FORCE)
set(PNG_SHARED OFF CACHE BOOL "")
set(PNG_TESTS OFF CACHE BOOL "")
set(PNG_EXECUTABLES OFF CACHE BOOL "")

set(LIBPNG_BINARY_DIR "${FETCHCONTENT_BASE_DIR}/libpng")
if(ANDROID_ABI)
    set(LIBPNG_BINARY_DIR "${LIBPNG_BINARY_DIR}/${ANDROID_ABI}")
endif()

FetchContent_Declare(
    libpng
    GIT_REPOSITORY "https://github.com/glennrp/libpng.git"
    BINARY_DIR "${LIBPNG_BINARY_DIR}"
    GIT_TAG "${AVIF_LOCAL_LIBPNG_GIT_TAG}"
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
