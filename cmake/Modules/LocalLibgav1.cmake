set(AVIF_LOCAL_LIBGAV1_GIT_TAG "v0.19.0")

set(AVIF_LIBGAV1_BUILD_DIR "${AVIF_SOURCE_DIR}/ext/libgav1/build")
# If ${ANDROID_ABI} is set, look for the library under that subdirectory.
if(DEFINED ANDROID_ABI)
    set(AVIF_LIBGAV1_BUILD_DIR "${AVIF_LIBGAV1_BUILD_DIR}/${ANDROID_ABI}")
endif()
set(LIB_FILENAME "${AVIF_LIBGAV1_BUILD_DIR}/libgav1${CMAKE_STATIC_LIBRARY_SUFFIX}")

if(EXISTS "${LIB_FILENAME}")
    message(STATUS "libavif(AVIF_CODEC_LIBGAV1=LOCAL): compiled library found at ${LIB_FILENAME}")
    add_library(libgav1_static STATIC IMPORTED GLOBAL)
    set_target_properties(libgav1_static PROPERTIES IMPORTED_LOCATION "${LIB_FILENAME}")
    target_include_directories(libgav1_static INTERFACE "${AVIF_SOURCE_DIR}/ext/libgav1/src")
else()
    message(STATUS "libavif(AVIF_CODEC_LIBGAV1=LOCAL): compiled library not found at ${LIB_FILENAME}; using FetchContent")

    if(EXISTS "${AVIF_SOURCE_DIR}/ext/libgav1")
        message(STATUS "libavif(AVIF_CODEC_LIBGAV1=LOCAL): ext/libgav1 found; using as FetchContent SOURCE_DIR")
        set(FETCHCONTENT_SOURCE_DIR_LIBGAV1 "${AVIF_SOURCE_DIR}/ext/libgav1")
        message(CHECK_START "libavif(AVIF_CODEC_LIBGAV1=LOCAL): configuring libgav1")
    else()
        message(CHECK_START "libavif(AVIF_CODEC_LIBGAV1=LOCAL): fetching and configuring libgav1")
    endif()

    set(LIBGAV1_BINARY_DIR "${FETCHCONTENT_BASE_DIR}/libgav1-build")
    if(ANDROID_ABI)
        set(LIBGAV1_BINARY_DIR "${LIBGAV1_BINARY_DIR}/${ANDROID_ABI}")
    endif()

    set(LIBGAV1_THREADPOOL_USE_STD_MUTEX 1 CACHE INTERNAL "")
    set(LIBGAV1_ENABLE_EXAMPLES OFF CACHE INTERNAL "")
    set(LIBGAV1_ENABLE_TESTS OFF CACHE INTERNAL "")
    set(LIBGAV1_MAX_BITDEPTH 12 CACHE INTERNAL "")

    FetchContent_Declare(
        libgav1
        GIT_REPOSITORY "https://chromium.googlesource.com/codecs/libgav1"
        BINARY_DIR "${LIBGAV1_BINARY_DIR}"
        GIT_TAG "${AVIF_LOCAL_LIBGAV1_GIT_TAG}"
        GIT_SHALLOW ON
        UPDATE_COMMAND ""
    )

    avif_fetchcontent_populate_cmake(libgav1)
    message(CHECK_PASS "complete")
endif()

set_property(TARGET libgav1_static PROPERTY AVIF_LOCAL ON)
add_library(libgav1::libgav1 ALIAS libgav1_static)

if(EXISTS "${AVIF_SOURCE_DIR}/ext/libgav1")
    set_property(TARGET libgav1_static PROPERTY FOLDER "ext/libgav1")
endif()
