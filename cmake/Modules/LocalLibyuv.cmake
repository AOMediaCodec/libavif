set(AVIF_LOCAL_LIBYUV_TAG "a6a2ec654b1be1166b376476a7555c89eca0c275")

set(AVIF_LIBYUV_BUILD_DIR "${AVIF_SOURCE_DIR}/ext/libyuv/build")
# If ${ANDROID_ABI} is set, look for the library under that subdirectory.
if(DEFINED ANDROID_ABI)
    set(AVIF_LIBYUV_BUILD_DIR "${AVIF_LIBYUV_BUILD_DIR}/${ANDROID_ABI}")
endif()
set(LIB_FILENAME "${AVIF_LIBYUV_BUILD_DIR}/${AVIF_LIBRARY_PREFIX}yuv${CMAKE_STATIC_LIBRARY_SUFFIX}")

if(EXISTS "${LIB_FILENAME}")
    message(STATUS "libavif(AVIF_LIBYUV=LOCAL): compiled library found at ${LIB_FILENAME}")
    set(LIBYUV_INCLUDE_DIR "${AVIF_SOURCE_DIR}/ext/libyuv/include")

    add_library(yuv::yuv STATIC IMPORTED GLOBAL)
    set_target_properties(yuv::yuv PROPERTIES IMPORTED_LOCATION "${LIB_FILENAME}" AVIF_LOCAL ON)
    target_include_directories(yuv::yuv INTERFACE "${LIBYUV_INCLUDE_DIR}")
    set_target_properties(yuv::yuv PROPERTIES FOLDER "ext/libyuv")
else()
    message(STATUS "libavif(AVIF_LIBYUV=LOCAL): compiled library not found at ${LIB_FILENAME}; using FetchContent")
    if(EXISTS "${AVIF_SOURCE_DIR}/ext/libyuv")
        message(STATUS "libavif(AVIF_LIBYUV=LOCAL): ext/libyuv found; using as FetchContent SOURCE_DIR")
        set(FETCHCONTENT_SOURCE_DIR_LIBYUV "${AVIF_SOURCE_DIR}/ext/libyuv")
        message(CHECK_START "libavif(AVIF_LIBYUV=LOCAL): configuring libyuv")
    else()
        message(CHECK_START "libavif(AVIF_LIBYUV=LOCAL): fetching and configuring libyuv")
    endif()

    set(LIBYUV_BINARY_DIR "${FETCHCONTENT_BASE_DIR}/libyuv-build")
    if(ANDROID_ABI)
        set(LIBYUV_BINARY_DIR "${LIBYUV_BINARY_DIR}/${ANDROID_ABI}")
    endif()
    FetchContent_Declare(
        libyuv
        GIT_REPOSITORY "https://chromium.googlesource.com/libyuv/libyuv"
        BINARY_DIR "${LIBYUV_BINARY_DIR}"
        GIT_TAG "${AVIF_LOCAL_LIBYUV_TAG}"
        UPDATE_COMMAND ""
    )

    avif_fetchcontent_populate_cmake(libyuv)

    set_target_properties(yuv PROPERTIES AVIF_LOCAL ON POSITION_INDEPENDENT_CODE ON)

    add_library(yuv::yuv ALIAS yuv)

    set(LIBYUV_INCLUDE_DIR "${libyuv_SOURCE_DIR}/include")

    target_include_directories(yuv INTERFACE ${LIBYUV_INCLUDE_DIR})

    if(EXISTS "${AVIF_SOURCE_DIR}/ext/libyuv")
        set_target_properties(yuv PROPERTIES FOLDER "ext/libyuv")
    endif()

    message(CHECK_PASS "complete")
endif()

set(libyuv_FOUND ON)

set(LIBYUV_VERSION_H "${LIBYUV_INCLUDE_DIR}/libyuv/version.h")
if(EXISTS ${LIBYUV_VERSION_H})
    # message(STATUS "Reading: ${LIBYUV_VERSION_H}")
    file(READ ${LIBYUV_VERSION_H} LIBYUV_VERSION_H_CONTENTS)
    string(REGEX MATCH "#define LIBYUV_VERSION ([0-9]+)" _ ${LIBYUV_VERSION_H_CONTENTS})
    set(LIBYUV_VERSION ${CMAKE_MATCH_1})
    # message(STATUS "libyuv version detected: ${LIBYUV_VERSION}")
endif()
if(NOT LIBYUV_VERSION)
    message(STATUS "libyuv version detection failed.")
endif()
