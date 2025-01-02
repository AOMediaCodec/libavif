set(AVIF_LOCAL_DAV1D_TAG "1.5.0")

function(avif_build_local_dav1d)
    set(download_step_args)
    if(EXISTS "${AVIF_SOURCE_DIR}/ext/dav1d")
        message(STATUS "libavif(AVIF_CODEC_DAV1D=LOCAL): ext/dav1d found, using as SOURCE_DIR")
        set(source_dir "${AVIF_SOURCE_DIR}/ext/dav1d")
    else()
        message(STATUS "libavif(AVIF_CODEC_DAV1D=LOCAL): ext/dav1d not found, fetching")
        set(source_dir "${FETCHCONTENT_BASE_DIR}/dav1d-src")
        list(APPEND download_step_args GIT_REPOSITORY https://code.videolan.org/videolan/dav1d.git GIT_TAG
             ${AVIF_LOCAL_DAV1D_TAG} GIT_SHALLOW ON
        )
    endif()

    find_program(NINJA_EXECUTABLE NAMES ninja ninja-build REQUIRED)
    find_program(MESON_EXECUTABLE meson REQUIRED)

    set(PATH $ENV{PATH})
    if(WIN32)
        string(REPLACE ";" "\$<SEMICOLON>" PATH "${PATH}")
    endif()
    if(ANDROID_TOOLCHAIN_ROOT)
        set(PATH "${ANDROID_TOOLCHAIN_ROOT}/bin$<IF:$<BOOL:${WIN32}>,$<SEMICOLON>,:>${PATH}")
    endif()

    if(ANDROID)
        list(APPEND CMAKE_PROGRAM_PATH "${ANDROID_TOOLCHAIN_ROOT}/bin")

        if(CMAKE_SYSTEM_PROCESSOR STREQUAL "armv7-a")
            set(android_arch "arm")
        elseif(CMAKE_SYSTEM_PROCESSOR STREQUAL "aarch64")
            set(android_arch "aarch64")
        elseif(CMAKE_SYSTEM_PROCESSOR STREQUAL "x86_64")
            set(android_arch "x86_64")
        else()
            set(android_arch "x86")
        endif()

        set(CROSS_FILE "${source_dir}/package/crossfiles/${android_arch}-android.meson")
    elseif(APPLE)
        # If we are cross compiling generate the corresponding file to use with meson
        if(NOT CMAKE_SYSTEM_PROCESSOR STREQUAL CMAKE_HOST_SYSTEM_PROCESSOR)
            string(TOLOWER "${CMAKE_SYSTEM_NAME}" cross_system_name)
            if(CMAKE_C_BYTE_ORDER STREQUAL "BIG_ENDIAN")
                set(cross_system_endian "big")
            else()
                set(cross_system_endian "little")
            endif()
            if(CMAKE_SYSTEM_PROCESSOR STREQUAL "arm64")
                set(cross_system_processor "aarch64")
            else()
                set(cross_system_processor "${CMAKE_SYSTEM_PROCESSOR}")
            endif()
            if(CMAKE_OSX_DEPLOYMENT_TARGET)
                set(cross_osx_deployment_target "-mmacosx-version-min=${CMAKE_OSX_DEPLOYMENT_TARGET}")
            endif()

            set(CROSS_FILE "${PROJECT_BINARY_DIR}/crossfile-apple.meson")
            configure_file("cmake/Meson/crossfile-apple.meson.in" "${CROSS_FILE}")
        endif()
    endif()

    if(CROSS_FILE)
        set(EXTRA_ARGS "--cross-file=${CROSS_FILE}")
    endif()

    set(build_dir "${FETCHCONTENT_BASE_DIR}/dav1d-build")
    set(install_dir "${FETCHCONTENT_BASE_DIR}/dav1d-install")

    if(ANDROID_ABI)
        set(build_dir "${build_dir}/${ANDROID_ABI}")
        set(install_dir "${install_dir}/${ANDROID_ABI}")
    endif()
    file(MAKE_DIRECTORY ${install_dir}/include)

    ExternalProject_Add(
        dav1d
        ${download_step_args}
        DOWNLOAD_DIR "${source_dir}"
        LOG_DIR "${build_dir}"
        STAMP_DIR "${build_dir}"
        TMP_DIR "${build_dir}"
        SOURCE_DIR "${source_dir}"
        BINARY_DIR "${build_dir}"
        INSTALL_DIR "${install_dir}"
        LIST_SEPARATOR |
        UPDATE_COMMAND ""
        CONFIGURE_COMMAND
            ${CMAKE_COMMAND} -E env "PATH=${PATH}" ${MESON_EXECUTABLE} setup --buildtype=release --default-library=static
            --prefix=<INSTALL_DIR> --libdir=lib -Denable_asm=true -Denable_tools=false -Denable_examples=false
            -Denable_tests=false ${EXTRA_ARGS} <SOURCE_DIR>
        BUILD_COMMAND ${CMAKE_COMMAND} -E env "PATH=${PATH}" ${NINJA_EXECUTABLE} -C <BINARY_DIR>
        INSTALL_COMMAND ${CMAKE_COMMAND} -E env "PATH=${PATH}" ${NINJA_EXECUTABLE} -C <BINARY_DIR> install
        BUILD_BYPRODUCTS <INSTALL_DIR>/lib/libdav1d.a
    )

    add_library(dav1d::dav1d STATIC IMPORTED)
    set_target_properties(dav1d::dav1d PROPERTIES IMPORTED_LOCATION ${install_dir}/lib/libdav1d.a AVIF_LOCAL ON)
    target_include_directories(dav1d::dav1d INTERFACE "${install_dir}/include")
    target_link_directories(dav1d::dav1d INTERFACE ${install_dir}/lib)
    add_dependencies(dav1d::dav1d dav1d)
endfunction()

set(AVIF_DAV1D_BUILD_DIR "${AVIF_SOURCE_DIR}/ext/dav1d/build")
# If ${ANDROID_ABI} is set, look for the library under that subdirectory.
if(DEFINED ANDROID_ABI)
    set(AVIF_DAV1D_BUILD_DIR "${AVIF_DAV1D_BUILD_DIR}/${ANDROID_ABI}")
endif()
set(LIB_FILENAME "${AVIF_DAV1D_BUILD_DIR}/src/libdav1d${CMAKE_STATIC_LIBRARY_SUFFIX}")
if(NOT EXISTS "${LIB_FILENAME}" AND NOT "${CMAKE_STATIC_LIBRARY_SUFFIX}" STREQUAL ".a")
    # On windows, meson will produce a libdav1d.a instead of the expected libdav1d.dll/.lib.
    # See https://github.com/mesonbuild/meson/issues/8153.
    set(LIB_FILENAME "${AVIF_DAV1D_BUILD_DIR}/src/libdav1d.a")
endif()
if(EXISTS "${LIB_FILENAME}")
    message(STATUS "libavif(AVIF_CODEC_DAV1D=LOCAL): compiled library found at ${LIB_FILENAME}")
    add_library(dav1d::dav1d STATIC IMPORTED)
    set_target_properties(dav1d::dav1d PROPERTIES IMPORTED_LOCATION ${LIB_FILENAME} AVIF_LOCAL ON)
    target_include_directories(
        dav1d::dav1d INTERFACE "${AVIF_DAV1D_BUILD_DIR}" "${AVIF_DAV1D_BUILD_DIR}/include"
                               "${AVIF_DAV1D_BUILD_DIR}/include/dav1d" "${AVIF_SOURCE_DIR}/ext/dav1d/include"
    )
else()
    message(STATUS "libavif(AVIF_CODEC_DAV1D=LOCAL): compiled library not found at ${LIB_FILENAME}; using ExternalProject")

    avif_build_local_dav1d()
endif()

if(EXISTS "${AVIF_SOURCE_DIR}/ext/dav1d")
    set_target_properties(dav1d::dav1d PROPERTIES FOLDER "ext/dav1d")
endif()
