include(ExternalProject)

if(CMAKE_BUILD_TYPE MATCHES "[Dd][Ee][Bb][Uu][Gg]")
    set(_dav1d_build_type debug)
elseif(CMAKE_BUILD_TYPE MATCHES "[Rr][Ee][Ll][Ee][Aa][Ss][Ee]")
    set(_dav1d_build_type release)
elseif(CMAKE_BUILD_TYPE MATCHES "[Rr][Ee][Ll][Ww][Ii][Tt][Hh][Dd][Ee][Bb][Ii][Nn][Ff][Oo]")
    set(_dav1d_build_type debugoptimized)
elseif(CMAKE_BUILD_TYPE MATCHES "[Mm][Ii][Nn][Ss][Ii][Zz][Ee][Rr][Ee][Ll]")
    set(_dav1d_build_type minsize)
else()
    set(_dav1d_build_type plain)
endif()

if(BUILD_SHARED_LIBS)
    set(_dav1d_default_library both)
else()
    set(_dav1d_default_library static)
endif()

ExternalProject_Add(dav1d
    PREFIX dav1d
    SOURCE_DIR ${PROJECT_SOURCE_DIR}/ext/dav1d
    #GIT_REPOSITORY "https://code.videolan.org/videolan/dav1d.git"
    #GIT_TAG 0.6.0
    URL "https://code.videolan.org/videolan/dav1d/-/archive/0.6.0/dav1d-0.6.0.tar.gz"
    URL_HASH SHA256=66c3e831a93f074290a72aad5da907e3763ecb092325f0250a841927b3d30ce3
    INSTALL_COMMAND ""
    CONFIGURE_COMMAND meson setup
        "--default-library=${_dav1d_default_library}"
        --buildtype "${_dav1d_build_type}"
        ${PROJECT_SOURCE_DIR}/ext/dav1d/build
        ${PROJECT_SOURCE_DIR}/ext/dav1d
    BUILD_COMMAND ninja -C ${PROJECT_SOURCE_DIR}/ext/dav1d/build)

set(DAV1D_INCLUDE_DIR "${PROJECT_SOURCE_DIR}/ext/dav1d/include/" "${PROJECT_SOURCE_DIR}/ext/dav1d/build/include/dav1d/")
set(DAV1D_LIBRARY "${PROJECT_SOURCE_DIR}/ext/dav1d/build/src/${CMAKE_STATIC_LIBRARY_PREFIX}dav1d${CMAKE_STATIC_LIBRARY_SUFFIX}")
set(DAV1D_LIBRARIES ${DAV1D_LIBRARY})

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(dav1d
                                  FOUND_VAR DAV1D_FOUND
                                  REQUIRED_VARS DAV1D_LIBRARY DAV1D_LIBRARIES DAV1D_INCLUDE_DIR
                                  VERSION_VAR _DAV1D_VERSION)

# show the DAV1D_INCLUDE_DIR, DAV1D_LIBRARY and DAV1D_LIBRARIES variables only
# in the advanced view
mark_as_advanced(DAV1D_INCLUDE_DIR DAV1D_LIBRARY DAV1D_LIBRARIES)
