# Copyright 2019 Joe Drago. All rights reserved.
# SPDX-License-Identifier: BSD-2-Clause

cmake_minimum_required(VERSION 3.5)
project(avif)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU")
  add_definitions(-std=c99) # Enforce C99 for gcc
endif()

add_subdirectory(ext)

include_directories(
    include
    "${CMAKE_CURRENT_SOURCE_DIR}/ext/aom"
    "${AOM_BINARY_DIR}"
)

if(NOT AVIF_EXTERNAL_GB)
    include_directories("${CMAKE_CURRENT_SOURCE_DIR}/ext/gb")
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

# TODO: Support other codec implementations here
set(AVIF_SRCS ${AVIF_SRCS}
    src/codec_aom.c
)

add_library(avif STATIC ${AVIF_SRCS})
target_link_libraries(avif aom gb)

option(AVIF_BUILD_EXAMPLES "Build avif Examples." OFF)
if(AVIF_BUILD_EXAMPLES)
    add_executable(avif_example1 examples/avif_example1.c)
    target_link_libraries(avif_example1 avif)
endif()

macro(avif_set_folder_safe target folder)
    if(TARGET ${target})
        set_target_properties(${target} PROPERTIES FOLDER ${folder})
    endif()
endmacro()

if(WIN32)
    option(AVIF_BUILD_STATIC "Build static avif library (with codec/gb included)" OFF)
    if(AVIF_BUILD_STATIC)
        add_custom_command(
            OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/${CMAKE_CFG_INTDIR}/avif_static_x64.lib
            COMMAND link.exe /lib /nologo /MACHINE:x64 /OUT:${CMAKE_CURRENT_BINARY_DIR}/${CMAKE_CFG_INTDIR}/avif_static_x64.lib $<TARGET_FILE:avif> $<TARGET_FILE:aom> $<TARGET_FILE:gb>
            DEPENDS avif aom
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

    if(NOT AVIF_EXTERNAL_GB)
        avif_set_folder_safe(gb "ext/avif")
    endif()
endif()
