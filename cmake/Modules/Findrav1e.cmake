# - Try to find rav1e
# Once done this will define
#
#  RAV1E_FOUND - system has rav1e
#  RAV1E_INCLUDE_DIR - the rav1e include directory
#  RAV1E_LIBRARIES - Link these to use rav1e
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
    pkg_check_modules(RAV1E_PC rav1e)
    if(RAV1E_PC_FOUND)
        if(BUILD_SHARED_LIBS)
            set(_PC_TYPE)
        else()
            set(_PC_TYPE _STATIC)
        endif()
        set(RAV1E_LIBRARY_DIRS ${RAV1E_PC${_PC_TYPE}_LIBRARY_DIRS})
        set(RAV1E_LIBRARIES ${RAV1E_PC${_PC_TYPE}_LIBRARIES})
    endif()
endif(PKG_CONFIG_FOUND)

if(NOT RAV1E_INCLUDE_DIR)
    find_path(
        RAV1E_INCLUDE_DIR
        NAMES rav1e.h
        PATHS ${RAV1E_PC_INCLUDE_DIRS}
        PATH_SUFFIXES rav1e
    )
endif()

find_library(RAV1E_LIBRARY NAMES rav1e PATHS ${RAV1E_PC_LIBRARY_DIRS})

# Remove -lrav1e since it will be replaced with full librav1e library path
if(RAV1E_LIBRARIES)
    list(REMOVE_ITEM RAV1E_LIBRARIES "rav1e")
endif()
set(RAV1E_LIBRARIES ${RAV1E_LIBRARY} ${RAV1E_LIBRARIES})

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(
    rav1e
    FOUND_VAR RAV1E_FOUND
    REQUIRED_VARS RAV1E_LIBRARY RAV1E_LIBRARIES RAV1E_INCLUDE_DIR
    VERSION_VAR RAV1E_PC_VERSION
)

# show the RAV1E_INCLUDE_DIR, RAV1E_LIBRARY, RAV1E_LIBRARIES, and RAV1E_PC_FOUND variables only in the advanced view
mark_as_advanced(RAV1E_INCLUDE_DIR RAV1E_LIBRARY RAV1E_LIBRARIES RAV1E_PC_FOUND)
