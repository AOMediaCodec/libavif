# Copyright 2019 Joe Drago. All rights reserved.
# SPDX-License-Identifier: BSD-2-Clause

set(AOM_MINIMAL_BUILD TRUE)
add_subdirectory(aom)

if(NOT AVIF_EXTERNAL_GB)
include_directories(gb)
add_library(gb
    gb/gb_math.c
    gb/gb_math.h
)
endif()
