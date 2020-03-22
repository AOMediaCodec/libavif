include(ExternalProject)
ExternalProject_Add(aom
    PREFIX aom
    SOURCE_DIR ${PROJECT_SOURCE_DIR}/ext/aom
    BINARY_DIR ${PROJECT_SOURCE_DIR}/ext/aom/build.libavif
    #GIT_REPOSITORY "https://aomedia.googlesource.com/aom"
    #GIT_TAG v1.0.0-errata1-avif
    URL "https://aomedia.googlesource.com/aom/+archive/refs/tags/v1.0.0-errata1-avif.tar.gz"
    INSTALL_COMMAND ""
    PATCH_COMMAND cmake -E make_directory "${PROJECT_SOURCE_DIR}/ext/aom/build.libavif"
    CMAKE_CACHE_DEFAULT_ARGS
        -DENABLE_DOCS:bool=0
        -DENABLE_EXAMPLES:bool=0
        -DENABLE_TESTDATA:bool=0
        -DENABLE_TESTS:bool=0
        -DENABLE_TOOLS:bool=0)

set(AOM_INCLUDE_DIR "${PROJECT_SOURCE_DIR}/ext/aom")
set(AOM_LIBRARY "${PROJECT_SOURCE_DIR}/ext/aom/build.libavif/${CMAKE_STATIC_LIBRARY_PREFIX}aom${CMAKE_STATIC_LIBRARY_SUFFIX}")
set(AOM_LIBRARIES ${AOM_LIBRARY})

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(aom
                                  FOUND_VAR AOM_FOUND
                                  REQUIRED_VARS AOM_LIBRARY AOM_LIBRARIES AOM_INCLUDE_DIR
                                  VERSION_VAR _AOM_VERSION)

# show the AOM_INCLUDE_DIR, AOM_LIBRARY and AOM_LIBRARIES variables only
# in the advanced view
mark_as_advanced(AOM_INCLUDE_DIR AOM_LIBRARY AOM_LIBRARIES)
