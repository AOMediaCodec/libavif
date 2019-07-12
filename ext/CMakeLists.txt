# Copyright 2019 Joe Drago. All rights reserved.
# SPDX-License-Identifier: BSD-2-Clause

if(AVIF_CODEC_AOM)
    set(ENABLE_DOCS 0)
    set(ENABLE_EXAMPLES 0)
    set(ENABLE_TESTDATA 0)
    set(ENABLE_TESTS 0)
    set(ENABLE_TOOLS 0)
    add_subdirectory(aom EXCLUDE_FROM_ALL)
endif()

if(NOT AVIF_EXTERNAL_GB)
include_directories(gb)
add_library(gb
    gb/gb_math.c
    gb/gb_math.h
)
endif()
