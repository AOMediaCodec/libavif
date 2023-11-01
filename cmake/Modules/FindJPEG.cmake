# - Try to find libjpeg
# Once done this will define
#
#  JPEG_FOUND - system has libjpeg
#  JPEG_INCLUDE_DIR - the libjpeg include directory
#  JPEG_LIBRARIES - Link these to use libjpeg
#
#=============================================================================
#  Copyright (c) 2020 Google LLC
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
    pkg_check_modules(_JPEG libjpeg)

    if(_JPEG_FOUND)
        if(BUILD_SHARED_LIBS)
            set(_PC_TYPE)
        else()
            set(_PC_TYPE _STATIC)
        endif()
        set(JPEG_LIBRARY_DIRS ${_JPEG${_PC_TYPE}_LIBRARY_DIRS})
        set(JPEG_LIBRARIES ${_JPEG${_PC_TYPE}_LIBRARIES})
    endif()
endif(PKG_CONFIG_FOUND)

find_path(JPEG_INCLUDE_DIR NAMES jpeglib.h PATHS ${_JPEG_INCLUDE_DIRS})

find_library(JPEG_LIBRARY NAMES jpeg PATHS ${_JPEG_LIBRARY_DIRS})

# Remove -ljpeg since it will be replaced with full jpeg library path
if(JPEG_LIBRARIES)
    list(REMOVE_ITEM JPEG_LIBRARIES "jpeg")
endif()
set(JPEG_LIBRARIES ${JPEG_LIBRARY} ${JPEG_LIBRARIES})

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(
    JPEG
    FOUND_VAR JPEG_FOUND
    REQUIRED_VARS JPEG_LIBRARY JPEG_LIBRARIES JPEG_INCLUDE_DIR
    VERSION_VAR _JPEG_VERSION
)

# show the JPEG_INCLUDE_DIR, JPEG_LIBRARY and JPEG_LIBRARIES variables
# only in the advanced view
mark_as_advanced(JPEG_INCLUDE_DIR JPEG_LIBRARY JPEG_LIBRARIES)
