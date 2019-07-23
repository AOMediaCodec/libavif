# Copyright 2019 Joe Drago. All rights reserved.
# SPDX-License-Identifier: BSD-2-Clause

cmake_minimum_required(VERSION 3.5)
project(libavif)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU")
  add_definitions(-std=c99) # Enforce C99 for gcc
endif()

option(AVIF_BUILD_AOM "Build the AOM codec by providing your own copy of the repo in ext/aom" OFF)

option(AVIF_CODEC_AOM "Use the AOM codec for encoding (and decoding if no other decoder is present)" ON)
option(AVIF_CODEC_DAV1D "Use the dav1d codec for decoding (overrides AOM decoding if also enabled)" OFF)

add_subdirectory(ext)

include_directories(include)

# Enable all warnings
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  MESSAGE(STATUS "libavif: Enabling warnings for Clang")
  add_definitions(
    -Weverything
    -Werror
    -Wno-bad-function-cast
    -Wno-c11-extensions
    -Wno-cast-align
    -Wno-cast-qual
    -Wno-covered-switch-default
    -Wno-deprecated-declarations
    -Wno-disabled-macro-expansion
    -Wno-documentation
    -Wno-documentation-unknown-command
    -Wno-double-promotion
    -Wno-format-nonliteral
    -Wno-language-extension-token
    -Wno-missing-noreturn
    -Wno-padded
    -Wno-reserved-id-macro
    -Wno-sign-conversion
    -Wno-switch-enum
    -Wno-undef
  )
elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU")
  add_definitions(-std=c99) # Enforce C99 for gcc
  MESSAGE(STATUS "libavif: Enabling warnings for GCC")
  add_definitions(-Werror -Wall -Wextra)
elseif(CMAKE_CXX_COMPILER_ID MATCHES "MSVC")
  MESSAGE(STATUS "libavif: Enabling warnings for MS CL")
  add_definitions(
    /Wall   # All warnings
    /WX     # Warnings as errors
    /wd4255 # Disable: no function prototype given
    /wd4324 # Disable: structure was padded due to alignment specifier
    /wd4668 # Disable: is not defined as a preprocessor macro, replacing with '0'
    /wd4710 # Disable: function not inlined
    /wd4711 # Disable: function selected for inline expansion
    /wd4738 # Disable: storing 32-bit float result in memory, possible loss of performance
    /wd4820 # Disable: bytes padding added after data member
    /wd4996 # Disable: potentially unsafe stdlib methods
    /wd5045 # Disable: Compiler will insert Spectre mitigation for memory load if /Qspectre switch specified
  )
else()
  MESSAGE(FATAL_ERROR "libavif: Unknown compiler, bailing out")
endif()

set(AVIF_SRCS
    include/avif/avif.h
    include/avif/internal.h
    src/avif.c
    src/colr.c
    src/mem.c
    src/rawdata.c
    src/read.c
    src/reformat.c
    src/stream.c
    src/utils.c
    src/write.c
)

set(AVIF_CODEC_LIBARIES)
if(AVIF_CODEC_AOM)
    message(STATUS "libavif: Encoding library: aom")
    if(NOT AVIF_CODEC_DAV1D)
        message(STATUS "libavif: Decoding library: aom")
    endif()
    add_definitions(-DAVIF_CODEC_AOM=1)
    set(AVIF_SRCS ${AVIF_SRCS}
        src/codec_aom.c
    )
    if(AVIF_BUILD_AOM)
        include_directories(
            "${CMAKE_CURRENT_SOURCE_DIR}/ext/aom"
            "${AOM_BINARY_DIR}"
        )
        message(STATUS "LIBAOM_INCLUDE_PATH: ${CMAKE_CURRENT_SOURCE_DIR}/ext/aom")
    else()
        # Check to see if aom is independently being built by the outer CMake project
        if(NOT TARGET aom)
            find_library(LIBAOM_LIBRARY_PATH NAMES aom)
            if(NOT LIBAOM_LIBRARY_PATH)
              message(FATAL_ERROR "libavif: aom library not found")
            endif()
            find_path (LIBAOM_INCLUDE_PATH aom/aom.h)
            if(NOT LIBAOM_INCLUDE_PATH)
              message(FATAL_ERROR "libavif: aom library includes not found")
            endif()
            message(STATUS "LIBAOM_INCLUDE_PATH: ${LIBAOM_INCLUDE_PATH}")
            include_directories(${LIBAOM_INCLUDE_PATH})
        endif()
    endif()
    set(AVIF_CODEC_LIBARIES ${AVIF_CODEC_LIBARIES} aom)
else()
    message(STATUS "libavif: Encoding library: (none)")
endif()

if(AVIF_CODEC_DAV1D)
    message(STATUS "libavif: Decoding library: dav1d")
    add_definitions(-DAVIF_CODEC_DAV1D=1)
    set(AVIF_SRCS ${AVIF_SRCS}
        src/codec_dav1d.c
    )

    # Check to see if dav1d is independently being built by the outer CMake project
    if(NOT TARGET dav1d)
        find_library(LIBDAV1D_LIBRARY_PATH NAMES dav1d)
        if(NOT LIBDAV1D_LIBRARY_PATH)
          message(FATAL_ERROR "libavif: dav1d library not found")
        endif()
        find_path (LIBDAV1D_INCLUDE_PATH dav1d/dav1d.h)
        if(NOT LIBDAV1D_INCLUDE_PATH)
          message(FATAL_ERROR "libavif: dav1d library includes not found")
        endif()
        message(STATUS "LIBDAV1D_INCLUDE_PATH: ${LIBDAV1D_INCLUDE_PATH}")
        include_directories(${LIBDAV1D_INCLUDE_PATH})
    endif()
    set(AVIF_CODEC_LIBARIES ${AVIF_CODEC_LIBARIES} dav1d)
endif()
if(NOT AVIF_CODEC_AOM AND NOT AVIF_CODEC_DAV1D)
    message(FATAL_ERROR "libavif: No decoding library is enabled, bailing out.")
endif()

add_library(avif STATIC ${AVIF_SRCS})
target_link_libraries(avif ${AVIF_CODEC_LIBARIES})
target_include_directories(avif SYSTEM INTERFACE include)

set(AVIF_MATH_LIBRARY)
if(NOT WIN32)
    set(AVIF_MATH_LIBRARY m)
endif()

option(AVIF_BUILD_EXAMPLES "Build avif Examples." OFF)
if(AVIF_BUILD_EXAMPLES)
    add_executable(avif_example1 examples/avif_example1.c)
    target_link_libraries(avif_example1 avif ${AVIF_MATH_LIBRARY})
endif()

option(AVIF_BUILD_APPS "Build avif apps." OFF)
if(AVIF_BUILD_APPS)
    include_directories(apps/shared)
    add_executable(avifenc
        apps/avifenc.c
        apps/shared/y4m.c
        apps/shared/avifutil.c
    )
    target_link_libraries(avifenc avif ${AVIF_MATH_LIBRARY})
    add_executable(avifdec
        apps/avifdec.c
        apps/shared/y4m.c
        apps/shared/avifutil.c
    )
    target_link_libraries(avifdec avif ${AVIF_MATH_LIBRARY})
endif()

option(AVIF_BUILD_TESTS "Build avif tests (fuzzing)." OFF)
if(AVIF_BUILD_TESTS)
    add_executable(aviffuzz
        tests/aviffuzz.c
    )
    target_link_libraries(aviffuzz avif ${AVIF_MATH_LIBRARY})
endif()

macro(avif_set_folder_safe target folder)
    if(TARGET ${target})
        set_target_properties(${target} PROPERTIES FOLDER ${folder})
    endif()
endmacro()

if(WIN32)
    option(AVIF_BUILD_STATIC "Build static avif library" OFF)
    if(AVIF_BUILD_STATIC)
        add_custom_command(
            OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/${CMAKE_CFG_INTDIR}/avif_static_x64.lib
            COMMAND link.exe /lib /nologo /MACHINE:x64 /OUT:${CMAKE_CURRENT_BINARY_DIR}/${CMAKE_CFG_INTDIR}/avif_static_x64.lib $<TARGET_FILE:avif> $<TARGET_FILE:aom>
            DEPENDS avif ${AVIF_CODEC_LIBARIES}
            COMMENT "Creating static avif library..."
        )

        add_custom_target(avif_static ALL
            DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/${CMAKE_CFG_INTDIR}/avif_static_x64.lib
        )
    endif()

    set_property(GLOBAL PROPERTY USE_FOLDERS ON)

    avif_set_folder_safe(aom "ext/avif")
    avif_set_folder_safe(aom_av1 "ext/avif")
    avif_set_folder_safe(aom_av1_common "ext/avif")
    avif_set_folder_safe(aom_av1_common_avx2_intrinsics "ext/avif")
    avif_set_folder_safe(aom_av1_common_sse2_intrinsics "ext/avif")
    avif_set_folder_safe(aom_av1_common_sse4_intrinsics "ext/avif")
    avif_set_folder_safe(aom_av1_common_ssse3_intrinsics "ext/avif")
    avif_set_folder_safe(aom_av1_decoder "ext/avif")
    avif_set_folder_safe(aom_av1_encoder "ext/avif")
    avif_set_folder_safe(aom_av1_encoder_avx2_intrinsics "ext/avif")
    avif_set_folder_safe(aom_av1_encoder_sse2 "ext/avif")
    avif_set_folder_safe(aom_av1_encoder_sse2_intrinsics "ext/avif")
    avif_set_folder_safe(aom_av1_encoder_sse3_intrinsics "ext/avif")
    avif_set_folder_safe(aom_av1_encoder_sse42_intrinsics "ext/avif")
    avif_set_folder_safe(aom_av1_encoder_sse4_intrinsics "ext/avif")
    avif_set_folder_safe(aom_av1_encoder_ssse3 "ext/avif")
    avif_set_folder_safe(aom_av1_encoder_ssse3_intrinsics "ext/avif")
    avif_set_folder_safe(aom_dsp "ext/avif")
    avif_set_folder_safe(aom_dsp_common "ext/avif")
    avif_set_folder_safe(aom_dsp_common_avx2_intrinsics "ext/avif")
    avif_set_folder_safe(aom_dsp_common_sse2 "ext/avif")
    avif_set_folder_safe(aom_dsp_common_sse2_intrinsics "ext/avif")
    avif_set_folder_safe(aom_dsp_common_sse4_1_intrinsics "ext/avif")
    avif_set_folder_safe(aom_dsp_common_ssse3 "ext/avif")
    avif_set_folder_safe(aom_dsp_common_ssse3_intrinsics "ext/avif")
    avif_set_folder_safe(aom_dsp_decoder "ext/avif")
    avif_set_folder_safe(aom_dsp_encoder "ext/avif")
    avif_set_folder_safe(aom_dsp_encoder_avx "ext/avif")
    avif_set_folder_safe(aom_dsp_encoder_avx2_intrinsics "ext/avif")
    avif_set_folder_safe(aom_dsp_encoder_sse2 "ext/avif")
    avif_set_folder_safe(aom_dsp_encoder_sse2_intrinsics "ext/avif")
    avif_set_folder_safe(aom_dsp_encoder_sse4_1_intrinsics "ext/avif")
    avif_set_folder_safe(aom_dsp_encoder_ssse3 "ext/avif")
    avif_set_folder_safe(aom_dsp_encoder_ssse3_intrinsics "ext/avif")
    avif_set_folder_safe(aom_mem "ext/avif")
    avif_set_folder_safe(aom_ports "ext/avif")
    avif_set_folder_safe(aom_rtcd "ext/avif")
    avif_set_folder_safe(aom_scale "ext/avif")
    avif_set_folder_safe(aom_util "ext/avif")
    avif_set_folder_safe(aom_version "ext/avif")
    avif_set_folder_safe(aom_version_check "ext/avif")
    avif_set_folder_safe(avif "ext/avif")
    avif_set_folder_safe(dist "ext/avif")
endif()
