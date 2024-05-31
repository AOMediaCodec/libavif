set(AVIF_LOCAL_LIBXML_GIT_TAG "v2.12.7")

set(LIB_FILENAME "${AVIF_SOURCE_DIR}/ext/libxml2/install.libavif/lib/${AVIF_LIBRARY_PREFIX}xml2${CMAKE_STATIC_LIBRARY_SUFFIX}")
if(EXISTS "${LIB_FILENAME}")
    message(STATUS "libavif(AVIF_LIBXML2=LOCAL): compiled library found at ${LIB_FILENAME}")
    add_library(LibXml2 STATIC IMPORTED GLOBAL)
    set_target_properties(LibXml2 PROPERTIES IMPORTED_LOCATION "${LIB_FILENAME}" AVIF_LOCAL ON)
    target_include_directories(LibXml2 INTERFACE "${AVIF_SOURCE_DIR}/ext/libxml2/install.libavif/include/libxml2")
    add_library(LibXml2::LibXml2 ALIAS LibXml2)
else()
    message(STATUS "libavif(AVIF_LIBXML2=LOCAL): compiled library not found at ${LIB_FILENAME}; using FetchContent")
    if(EXISTS "${AVIF_SOURCE_DIR}/ext/libxml2")
        message(STATUS "libavif(AVIF_LIBXML2=LOCAL): ext/libxml2 found; using as FetchContent SOURCE_DIR")
        set(FETCHCONTENT_SOURCE_DIR_LIBXML2 "${AVIF_SOURCE_DIR}/ext/libxml2")
        message(CHECK_START "libavif(AVIF_LOCAL_LIBXML2): configuring libxml2")
    else()
        message(CHECK_START "libavif(AVIF_LOCAL_LIBXML2): fetching and configuring libxml2")
    endif()

    set(LIBXML2_WITH_PYTHON OFF CACHE INTERNAL "-")
    set(LIBXML2_WITH_ZLIB OFF CACHE INTERNAL "-")
    set(LIBXML2_WITH_LZMA OFF CACHE INTERNAL "-")
    set(LIBXML2_WITH_ICONV OFF CACHE INTERNAL "-")
    set(LIBXML2_WITH_TESTS OFF CACHE INTERNAL "-")
    set(LIBXML2_WITH_PROGRAMS OFF CACHE INTERNAL "-")

    set(LIBXML2_BINARY_DIR "${FETCHCONTENT_BASE_DIR}/libxml2-build")
    if(ANDROID_ABI)
        set(LIBXML2_BINARY_DIR "${LIBXML2_BINARY_DIR}/${ANDROID_ABI}")
    endif()

    FetchContent_Declare(
        libxml2
        GIT_REPOSITORY "https://gitlab.gnome.org/GNOME/libxml2.git"
        BINARY_DIR "${LIBXML2_BINARY_DIR}"
        GIT_TAG "${AVIF_LOCAL_LIBXML_GIT_TAG}"
        GIT_SHALLOW ON
        UPDATE_COMMAND ""
    )

    avif_fetchcontent_populate_cmake(libxml2)

    set_property(TARGET LibXml2 PROPERTY AVIF_LOCAL ON)

    message(CHECK_PASS "complete")
endif()
