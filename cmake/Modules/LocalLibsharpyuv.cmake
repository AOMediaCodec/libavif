set(AVIF_LOCAL_LIBSHARPYUV_GIT_TAG v1.4.0)

set(LIB_FILENAME "${CMAKE_CURRENT_SOURCE_DIR}/ext/libwebp/build/libsharpyuv${CMAKE_STATIC_LIBRARY_SUFFIX}")
if(EXISTS "${LIB_FILENAME}")
    message(STATUS "libavif(AVIF_LIBSHARPYUV=LOCAL): compiled library found at ${LIB_FILENAME}")
    add_library(sharpyuv::sharpyuv STATIC IMPORTED GLOBAL)
    set_target_properties(sharpyuv::sharpyuv PROPERTIES IMPORTED_LOCATION "${LIB_FILENAME}" AVIF_LOCAL ON FOLDER "ext/libwebp")
    target_include_directories(sharpyuv::sharpyuv INTERFACE "${AVIF_SOURCE_DIR}/ext/libwebp")

    set(libsharpyuv_FOUND ON)
else()
    message(STATUS "libavif(AVIF_LIBSHARPYUV=LOCAL): compiled library not found at ${LIB_FILENAME}; using FetchContent")

    if(EXISTS "${AVIF_SOURCE_DIR}/ext/libwebp")
        message(STATUS "libavif(AVIF_LIBSHARPYUV=LOCAL): ext/libwebp found; using as FetchContent SOURCE_DIR")
        set(FETCHCONTENT_SOURCE_DIR_LIBWEBP "${AVIF_SOURCE_DIR}/ext/libwebp")
        message(CHECK_START "libavif(AVIF_LIBSHARPYUV=LOCAL): configuring libwebp")
    else()
        message(CHECK_START "libavif(AVIF_LIBSHARPYUV=LOCAL): fetching and configuring libwebp")
    endif()

    set(WEBP_BUILD_ANIM_UTILS OFF CACHE BOOL "")
    set(WEBP_BUILD_CWEBP OFF CACHE BOOL "")
    set(WEBP_BUILD_DWEBP OFF CACHE BOOL "")
    set(WEBP_BUILD_GIF2WEBP OFF CACHE BOOL "")
    set(WEBP_BUILD_IMG2WEBP OFF CACHE BOOL "")
    set(WEBP_BUILD_VWEBP OFF CACHE BOOL "")
    set(WEBP_BUILD_WEBPINFO OFF CACHE BOOL "")
    set(WEBP_BUILD_LIBWEBPMUX OFF CACHE BOOL "")
    set(WEBP_BUILD_WEBPMUX OFF CACHE BOOL "")
    set(WEBP_BUILD_EXTRAS OFF CACHE BOOL "")

    set(LIBSHARPYUV_BINARY_DIR "${FETCHCONTENT_BASE_DIR}/libwebp")
    if(ANDROID_ABI)
        set(LIBSHARPYUV_BINARY_DIR "${LIBSHARPYUV_BINARY_DIR}/${ANDROID_ABI}")
    endif()

    FetchContent_Declare(
        libwebp
        GIT_REPOSITORY "https://chromium.googlesource.com/webm/libwebp"
        BINARY_DIR "${LIBSHARPYUV_BINARY_DIR}"
        GIT_TAG "${AVIF_LOCAL_LIBSHARPYUV_GIT_TAG}"
        GIT_SHALLOW ON
        UPDATE_COMMAND ""
    )

    avif_fetchcontent_populate_cmake(libwebp)

    set_property(TARGET sharpyuv PROPERTY POSITION_INDEPENDENT_CODE ON)
    set_property(TARGET sharpyuv PROPERTY AVIF_LOCAL ON)

    target_include_directories(
        sharpyuv INTERFACE $<BUILD_INTERFACE:${libwebp_SOURCE_DIR}> $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDE_DIR}>
    )

    add_library(sharpyuv::sharpyuv ALIAS sharpyuv)

    if(EXISTS "${AVIF_SOURCE_DIR}/ext/libwebp")
        set_property(TARGET sharpyuv PROPERTY FOLDER "ext/libwebp")
    endif()
    message(CHECK_PASS "complete")
endif()
