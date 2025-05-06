set(AVIF_LIBARGPARSE_GIT_TAG ee74d1b53bd680748af14e737378de57e2a0a954)

set(LIBARGPARSE_FILENAME
    "${AVIF_SOURCE_DIR}/ext/libargparse/build/${CMAKE_STATIC_LIBRARY_PREFIX}argparse${CMAKE_STATIC_LIBRARY_SUFFIX}"
)

if(EXISTS "${LIBARGPARSE_FILENAME}")
    message(STATUS "libavif(libargparse): compiled library found at ${LIBARGPARSE_FILENAME}")
    add_library(libargparse STATIC IMPORTED GLOBAL)
    set_target_properties(libargparse PROPERTIES IMPORTED_LOCATION "${LIBARGPARSE_FILENAME}" AVIF_LOCAL ON)
    target_include_directories(libargparse INTERFACE "${AVIF_SOURCE_DIR}/ext/libargparse/src")
else()
    message(STATUS "libavif(libargparse): compiled library not found at ${LIBARGPARSE_FILENAME}; using FetchContent")
    if(EXISTS "${AVIF_SOURCE_DIR}/ext/libargparse")
        message(STATUS "libavif(libargparse): ext/libargparse found; using as FetchContent SOURCE_DIR")
        set(FETCHCONTENT_SOURCE_DIR_LIBARGPARSE "${AVIF_SOURCE_DIR}/ext/libargparse")
        message(CHECK_START "libavif(libargparse): configuring libargparse")
    else()
        message(CHECK_START "libavif(libargparse): fetching and configuring libargparse")
    endif()

    FetchContent_Declare(
        libargparse
        GIT_REPOSITORY "https://github.com/kmurray/libargparse.git"
        GIT_TAG ${AVIF_LIBARGPARSE_GIT_TAG}
        # TODO(vrabaud) remove once CMake 3.13 is not supported anymore.
        PATCH_COMMAND git apply --ignore-whitespace "${AVIF_SOURCE_DIR}/ext/libargparse.patch"
        UPDATE_COMMAND ""
    )
    avif_fetchcontent_populate_cmake(libargparse)

    message(CHECK_PASS "complete")
endif()

if(EXISTS "${AVIF_SOURCE_DIR}/ext/libargparse")
    set_target_properties(libargparse PROPERTIES FOLDER "ext/libargparse")
endif()
