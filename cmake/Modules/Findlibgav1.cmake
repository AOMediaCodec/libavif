# - Try to find libgav1
# Once done this will define
#
#  LIBGAV1_FOUND - system has libgav1
#  LIBGAV1_INCLUDE_DIR - the libgav1 include directory
#  LIBGAV1_LIBRARIES - Link these to use libgav1
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
    pkg_check_modules(_LIBGAV1 libgav1)

    if(_LIBGAV1_FOUND)
        if(BUILD_SHARED_LIBS)
            set(_PC_TYPE)
        else()
            set(_PC_TYPE _STATIC)
        endif()
        set(LIBGAV1_LIBRARY_DIRS ${_LIBGAV1${_PC_TYPE}_LIBRARY_DIRS})
        set(LIBGAV1_LIBRARIES ${_LIBGAV1${_PC_TYPE}_LIBRARIES})
    endif()
endif(PKG_CONFIG_FOUND)

find_path(LIBGAV1_INCLUDE_DIR NAMES gav1/decoder.h PATHS ${_LIBGAV1_INCLUDE_DIRS})

find_library(LIBGAV1_LIBRARY NAMES gav1 PATHS ${_LIBGAV1_LIBRARY_DIRS})

# Remove -lgav1 since it will be replaced with full libgav1 library path
if(LIBGAV1_LIBRARIES)
    list(REMOVE_ITEM LIBGAV1_LIBRARIES "gav1")
endif()
set(LIBGAV1_LIBRARIES ${LIBGAV1_LIBRARY} ${LIBGAV1_LIBRARIES})

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(
    libgav1
    FOUND_VAR LIBGAV1_FOUND
    REQUIRED_VARS LIBGAV1_LIBRARY LIBGAV1_LIBRARIES LIBGAV1_INCLUDE_DIR
    VERSION_VAR _LIBGAV1_VERSION
)

# show the LIBGAV1_INCLUDE_DIR, LIBGAV1_LIBRARY and LIBGAV1_LIBRARIES variables
# only in the advanced view
mark_as_advanced(LIBGAV1_INCLUDE_DIR LIBGAV1_LIBRARY LIBGAV1_LIBRARIES)
