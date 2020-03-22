include(ExternalProject)

if(CMAKE_BUILD_TYPE MATCHES "[Rr][Ee][Ll][Ee][Aa][Ss][Ee]")
    set(_rav1e_build_type --release)
elseif(CMAKE_BUILD_TYPE MATCHES "[Rr][Ee][Ll][Ww][Ii][Tt][Hh][Dd][Ee][Bb][Ii][Nn][Ff][Oo]")
    set(_rav1e_build_type --release -C debuginfo=3)
elseif(CMAKE_BUILD_TYPE MATCHES "[Mm][Ii][Nn][Ss][Ii][Zz][Ee][Rr][Ee][Ll]")
    set(_rav1e_build_type --release -C opt-level=s)
else()
    set(_rav1e_build_type)
endif()

ExternalProject_Add(rav1e
    PREFIX rav1e
    SOURCE_DIR ${PROJECT_SOURCE_DIR}/ext/rav1e
    #GIT_REPOSITORY "https://github.com/xiph/rav1e.git"
    #GIT_TAG v0.3.1
    URL "https://github.com/xiph/rav1e/tarball/v0.3.1"
    URL_HASH SHA256=9f51517164d1b25d353f355d4a289c45e3dabe878e899e1a0b7b7a1d81dc4375
    BUILD_IN_SOURCE true
    INSTALL_COMMAND ""
    CONFIGURE_COMMAND cargo install cbindgen
    COMMAND cbindgen
        -c cbindgen.toml
        -l C
        -o target/release/include/rav1e/rav1e.h
        --crate rav1e .
    BUILD_COMMAND cargo build
        --lib
        ${_rav1e_build_type}
        --features capi)

set(RAV1E_INCLUDE_DIR "${PROJECT_SOURCE_DIR}/ext/rav1e/target/release/include/")
set(RAV1E_LIBRARY "${PROJECT_SOURCE_DIR}/ext/rav1e/target/debug/${CMAKE_STATIC_LIBRARY_PREFIX}rav1e${CMAKE_STATIC_LIBRARY_SUFFIX}")
set(RAV1E_LIBRARIES ${RAV1E_LIBRARY})

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(rav1e
                                  FOUND_VAR RAV1E_FOUND
                                  REQUIRED_VARS RAV1E_LIBRARY RAV1E_LIBRARIES RAV1E_INCLUDE_DIR
                                  VERSION_VAR _RAV1E_VERSION)

# show the RAV1E_INCLUDE_DIR, RAV1E_LIBRARY and RAV1E_LIBRARIES variables only
# in the advanced view
mark_as_advanced(RAV1E_INCLUDE_DIR RAV1E_LIBRARY RAV1E_LIBRARIES)
