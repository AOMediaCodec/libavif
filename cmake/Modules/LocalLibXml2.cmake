set(AVIF_LIBXML_GIT_TAG "v2.15.1")

# First, whether the library exists.
set(PREFIXES lib ${AVIF_LIBRARY_PREFIX})
set(SUFFIXES "s" "sd" " ")
foreach(PREFIX IN LISTS PREFIXES)
    foreach(SUFFIX IN LISTS SUFFIXES)
        set(LIB_FILENAME
            "${AVIF_SOURCE_DIR}/ext/libxml2/install.libavif/lib/${PREFIX}xml2${SUFFIX}${CMAKE_STATIC_LIBRARY_SUFFIX}"
        )
        if(EXISTS "${LIB_FILENAME}")
            message(INFO ${CMAKE_LINK_LIBRARY_FLAG})
            message(STATUS "libavif(AVIF_LIBXML2=LOCAL): compiled library found at ${LIB_FILENAME}")
            add_library(LibXml2 STATIC IMPORTED GLOBAL)
            set_target_properties(LibXml2 PROPERTIES IMPORTED_LOCATION "${LIB_FILENAME}" AVIF_LOCAL ON)
            if(WIN32)
                target_link_libraries(LibXml2 INTERFACE bcrypt.lib)
            endif()
            target_compile_definitions(LibXml2 INTERFACE LIBXML_STATIC)
            target_include_directories(LibXml2 INTERFACE "${AVIF_SOURCE_DIR}/ext/libxml2/install.libavif/include/libxml2")
            add_library(LibXml2::LibXml2 ALIAS LibXml2)
        endif()
    endforeach()
endforeach()

if(TARGET LibXml2::LibXml2)
    return()
endif()

message(STATUS "libavif(AVIF_LIBXML2=LOCAL): compiled library not found at ${LIB_FILENAME}; using FetchContent")
if(EXISTS "${AVIF_SOURCE_DIR}/ext/libxml2")
    message(STATUS "libavif(AVIF_LIBXML2=LOCAL): ext/libxml2 found; using as FetchContent SOURCE_DIR")
    set(FETCHCONTENT_SOURCE_DIR_LIBXML2 "${AVIF_SOURCE_DIR}/ext/libxml2")
    message(CHECK_START "libavif(AVIF_LIBXML2=LOCAL): configuring libxml2")
else()
    message(CHECK_START "libavif(AVIF_LIBXML2=LOCAL): fetching and configuring libxml2")
endif()

set(LIBXML2_WITH_ICONV OFF CACHE INTERNAL "-")
set(LIBXML2_WITH_PROGRAMS OFF CACHE INTERNAL "-")
set(LIBXML2_WITH_PYTHON OFF CACHE INTERNAL "-")
set(LIBXML2_WITH_TESTS OFF CACHE INTERNAL "-")
set(LIBXML2_WITH_ZLIB OFF CACHE INTERNAL "-")

set(LIBXML2_BINARY_DIR "${FETCHCONTENT_BASE_DIR}/libxml2-build")
if(ANDROID_ABI)
    set(LIBXML2_BINARY_DIR "${LIBXML2_BINARY_DIR}/${ANDROID_ABI}")
endif()

FetchContent_Declare(
    libxml2
    GIT_REPOSITORY "https://github.com/GNOME/libxml2.git"
    BINARY_DIR "${LIBXML2_BINARY_DIR}"
    GIT_TAG "${AVIF_LIBXML_GIT_TAG}"
    GIT_SHALLOW ON
    UPDATE_COMMAND ""
)

avif_fetchcontent_populate_cmake(libxml2)

set_property(TARGET LibXml2 PROPERTY AVIF_LOCAL ON)
get_target_property(VAR1 LibXml2 LINKER_LANGUAGE)
get_target_property(VAR2 LibXml2 LINK_LIBRARIES)
get_target_property(VAR3 LibXml2 LINK_DIRECTORIES)
get_target_property(VAR4 LibXml2 LINKER_TYPE)
get_target_property(VAR5 LibXml2 LINK_FLAGS)
get_target_property(VAR6 LibXml2 LINK_OPTIONS)

message(CHECK_PASS "complete")
