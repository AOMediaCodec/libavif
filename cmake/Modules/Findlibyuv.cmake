# - Try to find libyuv
# Once done this will define
#
#  LIBYUV_FOUND - system has libyuv
#  LIBYUV_INCLUDE_DIR - the libyuv include directory
#  LIBYUV_LIBRARIES - Link these to use libyuv
#
#=============================================================================
#  Copyright (c) 2020 Andreas Schneider <asn@cryptomilk.org>
#
#  Distributed under the OSI-approved BSD License (the "License");
#  see accompanying file Copyright.txt for details.
#
#  This software is distributed WITHOUT ANY WARRANTY; without even the
#  implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
#  See the License for more information.
#=============================================================================
#

find_package(PkgConfig QUIET)
if(PKG_CONFIG_FOUND)
    pkg_check_modules(_LIBYUV libyuv)
    if(_LIBYUV_FOUND)
        if(BUILD_SHARED_LIBS)
            set(_PC_TYPE)
        else()
            set(_PC_TYPE _STATIC)
        endif()
        set(LIBYUV_LIBRARY_DIRS ${_LIBYUV${_PC_TYPE}_LIBRARY_DIRS})
        set(LIBYUV_LIBRARIES ${_LIBYUV${_PC_TYPE}_LIBRARIES})
    endif()
endif(PKG_CONFIG_FOUND)

if(NOT LIBYUV_INCLUDE_DIR)
    find_path(LIBYUV_INCLUDE_DIR NAMES libyuv.h PATHS ${_LIBYUV_INCLUDE_DIRS})
endif()

if(LIBYUV_INCLUDE_DIR AND NOT LIBYUV_VERSION)
    set(LIBYUV_VERSION_H "${LIBYUV_INCLUDE_DIR}/libyuv/version.h")
    if(EXISTS ${LIBYUV_VERSION_H})
        # message(STATUS "Reading: ${LIBYUV_VERSION_H}")
        file(READ ${LIBYUV_VERSION_H} LIBYUV_VERSION_H_CONTENTS)
        string(REGEX MATCH "#define LIBYUV_VERSION ([0-9]+)" _ ${LIBYUV_VERSION_H_CONTENTS})
        set(LIBYUV_VERSION ${CMAKE_MATCH_1})
        # message(STATUS "libyuv version detected: ${LIBYUV_VERSION}")
    endif()
    if(NOT LIBYUV_VERSION)
        message(STATUS "libyuv version detection failed.")
    endif()
endif()

if(NOT LIBYUV_LIBRARY)
    find_library(LIBYUV_LIBRARY NAMES yuv PATHS ${_LIBYUV_LIBRARY_DIRS})
endif()

# Remove -lyuv since it will be replaced with full libyuv library path
if(LIBYUV_LIBRARIES)
    list(REMOVE_ITEM LIBYUV_LIBRARIES "yuv")
endif()
set(LIBYUV_LIBRARIES ${LIBYUV_LIBRARY} ${LIBYUV_LIBRARIES})

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(
    libyuv
    FOUND_VAR LIBYUV_FOUND
    REQUIRED_VARS LIBYUV_LIBRARY LIBYUV_LIBRARIES LIBYUV_INCLUDE_DIR
    VERSION_VAR _LIBYUV_VERSION
)

# show the LIBYUV_INCLUDE_DIR, LIBYUV_LIBRARY and LIBYUV_LIBRARIES variables only
# in the advanced view
mark_as_advanced(LIBYUV_INCLUDE_DIR LIBYUV_LIBRARY LIBYUV_LIBRARIES)
