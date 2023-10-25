# - Try to find libpng
# Once done this will define
#
#  PNG_FOUND - system has libpng
#  PNG_INCLUDE_DIR - the libpng include directory
#  PNG_LIBRARIES - Link these to use libpng
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
    pkg_check_modules(_PNG libpng)
    if(_PNG_FOUND)
        if(BUILD_SHARED_LIBS)
            set(_PC_TYPE)
        else()
            set(_PC_TYPE _STATIC)
        endif()
        set(PNG_INCLUDE_DIR ${_PNG_INCLUDE_DIR})
        set(PNG_LIBRARY_DIRS ${_PNG${_PC_TYPE}_LIBRARY_DIRS})
        set(PNG_LIBRARIES ${_PNG${_PC_TYPE}_LIBRARIES})
        set(PNG_VERSION ${_PNG_VERSION})
    endif()
endif(PKG_CONFIG_FOUND)

if(NOT PNG_INCLUDE_DIR)
    find_path(PNG_INCLUDE_DIR NAMES png.h PATHS ${_PNG_INCLUDE_DIRS})
endif()

if(PNG_INCLUDE_DIR AND NOT PNG_VERSION)
    set(PNG_PNG_H "${PNG_INCLUDE_DIR}/png.h")
    if(EXISTS ${PNG_PNG_H})
        message(STATUS "Reading: ${PNG_PNG_H}")
        file(READ ${PNG_PNG_H} PNG_PNG_H_CONTENTS)
        string(REGEX MATCH "#define PNG_LIBPNG_VER_STRING \"([0-9.]+)\"" _ ${PNG_PNG_H_CONTENTS})
        set(PNG_VERSION ${CMAKE_MATCH_1})
        message(STATUS "libpng version detected: ${PNG_VERSION}")
    endif()
    if(NOT PNG_VERSION)
        message(STATUS "libpng version detection failed.")
    endif()
endif()

if(NOT PNG_LIBRARY)
    find_library(PNG_LIBRARY NAMES png png16 libpng libpng16 PATHS ${_PNG_LIBRARY_DIRS})
endif()

# Remove -lyuv since it will be replaced with full libpng library path
if(PNG_LIBRARIES)
    list(REMOVE_ITEM PNG_LIBRARIES png png12 png16 libpng libpng12 libpng16)
endif()
set(PNG_LIBRARIES ${PNG_LIBRARY} ${PNG_LIBRARIES})

if(ZLIB_LIBRARY)
    list(TRANSFORM LIBXML2_LIBRARIES REPLACE "^z$" ${ZLIB_LIBRARY})
endif()

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(
    PNG
    FOUND_VAR PNG_FOUND
    REQUIRED_VARS PNG_LIBRARY PNG_LIBRARIES PNG_INCLUDE_DIR
    VERSION_VAR _PNG_VERSION
)

# show the PNG_INCLUDE_DIR, PNG_LIBRARY and PNG_LIBRARIES variables only
# in the advanced view
mark_as_advanced(PNG_INCLUDE_DIR PNG_LIBRARY PNG_LIBRARIES)
