set(AVIF_LOCAL_LIBARGPARSE_GIT_TAG ee74d1b53bd680748af14e737378de57e2a0a954)

set(LIBARGPARSE_FILENAME
    "${AVIF_SOURCE_DIR}/ext/libargparse/build/${CMAKE_STATIC_LIBRARY_PREFIX}argparse${CMAKE_STATIC_LIBRARY_SUFFIX}"
)

if(EXISTS "${LIBARGPARSE_FILENAME}")
    message(STATUS "libavif(AVIF_LOCAL_LIBARGPARSE): compiled library found at ${LIBARGPARSE_FILENAME}")
    add_library(libargparse STATIC IMPORTED GLOBAL)
    set_target_properties(libargparse PROPERTIES IMPORTED_LOCATION "${LIBARGPARSE_FILENAME}" AVIF_LOCAL ON)
    target_include_directories(libargparse INTERFACE "${AVIF_SOURCE_DIR}/ext/libargparse/src")
else()
    message(STATUS "libavif(AVIF_LOCAL_LIBARGPARSE): compiled library not found at ${LIBARGPARSE_FILENAME}; using FetchContent")
    if(EXISTS "${AVIF_SOURCE_DIR}/ext/libargparse")
        message(STATUS "libavif(AVIF_LOCAL_LIBARGPARSE): ext/libargparse found; using as FetchContent SOURCE_DIR")
        set(FETCHCONTENT_SOURCE_DIR_LIBARGPARSE "${AVIF_SOURCE_DIR}/ext/libargparse")
        message(CHECK_START "libavif(AVIF_LOCAL_LIBARGPARSE): configuring libargparse")
    else()
        message(CHECK_START "libavif(AVIF_LOCAL_LIBARGPARSE): fetching and configuring libargparse")
    endif()

    FetchContent_Declare(
        libargparse
        GIT_REPOSITORY "https://github.com/kmurray/libargparse.git"
        GIT_TAG ${AVIF_LOCAL_LIBARGPARSE_GIT_TAG}
        UPDATE_COMMAND ""
    )
    avif_fetchcontent_populate_cmake(libargparse)

    message(CHECK_PASS "complete")
endif()

if(EXISTS "${AVIF_SOURCE_DIR}/ext/libargparse")
    set_target_properties(libargparse PROPERTIES FOLDER "ext/libargparse")
endif()
