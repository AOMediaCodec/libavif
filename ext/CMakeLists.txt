# Copyright 2019 Joe Drago. All rights reserved.
# SPDX-License-Identifier: BSD-2-Clause

if(AVIF_CODEC_AOM AND AVIF_LOCAL_AOM)
    if(NOT EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/aom)
        message(FATAL_ERROR "libavif: AVIF_LOCAL_AOM is enabled, but ext/aom dir is missing. See ext/aom.sh for information.")
    endif()

    set(ENABLE_DOCS 0)
    set(ENABLE_EXAMPLES 0)
    set(ENABLE_TESTDATA 0)
    set(ENABLE_TESTS 0)
    set(ENABLE_TOOLS 0)
    add_subdirectory(aom EXCLUDE_FROM_ALL)
endif()
