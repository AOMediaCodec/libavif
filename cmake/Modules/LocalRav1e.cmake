set(AVIF_LOCAL_RAV1E_GIT_TAG v0.7.1)
set(AVIF_LOCAL_CORROSION_GIT_TAG v0.5.0)
set(AVIF_LOCAL_CARGOC_GIT_TAG v0.10.2)

set(RAV1E_LIB_FILENAME
    "${AVIF_SOURCE_DIR}/ext/rav1e/build.libavif/usr/lib/${AVIF_LIBRARY_PREFIX}rav1e${CMAKE_STATIC_LIBRARY_SUFFIX}"
)

if(EXISTS "${RAV1E_LIB_FILENAME}")
    message(STATUS "libavif(AVIF_CODEC_RAV1E=LOCAL): compiled rav1e library found at ${RAV1E_LIB_FILENAME}")
    add_library(rav1e::rav1e STATIC IMPORTED)
    set_target_properties(rav1e::rav1e PROPERTIES IMPORTED_LOCATION "${RAV1E_LIB_FILENAME}" IMPORTED_SONAME rav1e AVIF_LOCAL ON)
    target_include_directories(rav1e::rav1e INTERFACE "${AVIF_SOURCE_DIR}/ext/rav1e/build.libavif/usr/include/rav1e")
else()
    message(
        STATUS "libavif(AVIF_CODEC_RAV1E=LOCAL): compiled rav1e library not found at ${RAV1E_LIB_FILENAME}; using FetchContent"
    )
    if(EXISTS "${AVIF_SOURCE_DIR}/ext/rav1e")
        message(STATUS "libavif(AVIF_CODEC_RAV1E=LOCAL): ext/rav1e found; using as FetchContent SOURCE_DIR")
        set(FETCHCONTENT_SOURCE_DIR_RAV1E "${AVIF_SOURCE_DIR}/ext/rav1e")
        message(CHECK_START "libavif(AVIF_CODEC_RAV1E=LOCAL): configuring rav1e")
    else()
        message(CHECK_START "libavif(AVIF_CODEC_RAV1E=LOCAL): fetching and configuring rav1e")
    endif()

    FetchContent_Declare(
        Corrosion
        GIT_REPOSITORY https://github.com/corrosion-rs/corrosion.git
        GIT_TAG ${AVIF_LOCAL_CORROSION_GIT_TAG}
        GIT_SHALLOW ON
    )

    if(APPLE)
        if(CMAKE_OSX_ARCHITECTURES STREQUAL "arm64" OR CMAKE_SYSTEM_PROCESSOR STREQUAL "arm64")
            set(Rust_CARGO_TARGET "aarch64-apple-darwin")
        endif()
    endif()

    FetchContent_MakeAvailable(Corrosion)

    find_program(CARGO_CINSTALL cargo-cinstall HINTS "$ENV{HOME}/.cargo/bin")

    if(CARGO_CINSTALL)
        add_executable(cargo-cinstall IMPORTED GLOBAL)
        set_property(TARGET cargo-cinstall PROPERTY IMPORTED_LOCATION ${CARGO_CINSTALL})
    endif()

    if(NOT TARGET cargo-cinstall)
        FetchContent_Declare(
            cargoc
            GIT_REPOSITORY https://github.com/lu-zero/cargo-c.git
            GIT_TAG "${AVIF_LOCAL_CARGOC_GIT_TAG}"
            GIT_SHALLOW ON
        )
        FetchContent_MakeAvailable(cargoc)

        corrosion_import_crate(
            MANIFEST_PATH ${cargoc_SOURCE_DIR}/Cargo.toml PROFILE release IMPORTED_CRATES MYVAR_IMPORTED_CRATES FEATURES
            vendored-openssl
        )

        set(CARGO_CINSTALL $<TARGET_FILE:cargo-cinstall>)
    endif()

    FetchContent_Declare(
        rav1e
        GIT_REPOSITORY https://github.com/xiph/rav1e.git
        GIT_TAG "${AVIF_LOCAL_RAV1E_GIT_TAG}"
        GIT_SHALLOW ON
    )
    FetchContent_MakeAvailable(rav1e)

    set(RAV1E_LIB_FILENAME
        ${CMAKE_CURRENT_BINARY_DIR}/ext/rav1e/usr/lib/${CMAKE_STATIC_LIBRARY_PREFIX}rav1e${CMAKE_STATIC_LIBRARY_SUFFIX}
    )
    set(RAV1E_ENVVARS)
    if(CMAKE_C_IMPLICIT_LINK_DIRECTORIES MATCHES "alpine-linux-musl")
        list(APPEND RAV1E_ENVVARS "RUSTFLAGS=-C link-args=-Wl,-z,stack-size=2097152 -C target-feature=-crt-static")
    endif()
    if(CMAKE_HOST_SYSTEM_NAME STREQUAL "Darwin" AND CMAKE_OSX_SYSROOT)
        list(APPEND RAV1E_ENVVARS "SDKROOT=${CMAKE_OSX_SYSROOT}")
    endif()
    if(CMAKE_HOST_SYSTEM_NAME STREQUAL "Darwin" AND CMAKE_OSX_DEPLOYMENT_TARGET)
        list(APPEND RAV1E_ENVVARS "MACOSX_DEPLOYMENT_TARGET=${CMAKE_OSX_DEPLOYMENT_TARGET}")
    endif()

    add_custom_target(
        rav1e
        COMMAND ${CMAKE_COMMAND} -E env ${RAV1E_ENVVARS} ${CARGO_CINSTALL} cinstall -v --release --library-type=staticlib
                --prefix=/usr --target ${Rust_CARGO_TARGET_CACHED} --destdir ${CMAKE_CURRENT_BINARY_DIR}/ext/rav1e
        DEPENDS cargo-cinstall
        BYPRODUCTS ${RAV1E_LIB_FILENAME}
        USES_TERMINAL
        WORKING_DIRECTORY ${rav1e_SOURCE_DIR}
    )
    set(RAV1E_INCLUDE_DIR "${CMAKE_CURRENT_BINARY_DIR}/ext/rav1e/usr/include/rav1e")
    file(MAKE_DIRECTORY ${RAV1E_INCLUDE_DIR})
    set(RAV1E_FOUND ON)

    add_library(rav1e::rav1e STATIC IMPORTED)
    add_dependencies(rav1e::rav1e rav1e)
    target_link_libraries(rav1e::rav1e INTERFACE "${Rust_CARGO_TARGET_LINK_NATIVE_LIBS}")
    target_link_options(rav1e::rav1e INTERFACE "${Rust_CARGO_TARGET_LINK_OPTIONS}")
    set_target_properties(rav1e::rav1e PROPERTIES IMPORTED_LOCATION "${RAV1E_LIB_FILENAME}" AVIF_LOCAL ON FOLDER "ext/rav1e")
    target_include_directories(rav1e::rav1e INTERFACE "${RAV1E_INCLUDE_DIR}")

    message(CHECK_PASS "complete")
endif()
