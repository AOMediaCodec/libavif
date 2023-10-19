# - Try to find libxml2
# Once done this will define
#
#  LIBXML2_FOUND - system has libxml2
#  LIBXML2_INCLUDE_DIR - the libxml2 include directory
#  LIBXML2_LIBRARIES - Link these to use libxml2
#
#=============================================================================
#  Copyright (c) 2022 Google LLC
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
    pkg_check_modules(_LIBXML2 libxml-2.0)
    if(_LIBXML2_FOUND)
        if(BUILD_SHARED_LIBS)
            set(_PC_TYPE)
        else()
            set(_PC_TYPE _STATIC)
        endif()
        set(LIBXML2_LIBRARY_DIRS ${_LIBXML2${_PC_TYPE}_LIBRARY_DIRS})
        set(LIBXML2_LIBRARIES ${_LIBXML2${_PC_TYPE}_LIBRARIES})
    endif()
endif(PKG_CONFIG_FOUND)

find_path(
    LIBXML2_INCLUDE_DIR
    NAMES libxml/xpath.h
    HINTS ${_LIBXML_INCLUDEDIR} ${_LIBXML_INCLUDE_DIRS}
    PATH_SUFFIXES libxml2
)

find_library(LIBXML2_LIBRARY NAMES xml2 libxml2 PATHS ${LIBXML2_LIBRARY_DIRS})

# Remove the target lib as it will be replaced with full libxml2 library path,
# but not if it is in the macOS SYSROOT.
if(NOT BUILD_SHARED_LIBS
   AND LIBXML2_LIBRARY
   AND LIBXML2_LIBRARIES
   AND CMAKE_OSX_SYSROOT
)
    list(POP_FRONT LIBXML2_LIBRARIES LIBXML2_PC_STATIC_LIBRARY)
    string(FIND ${LIBXML2_LIBRARY} ${CMAKE_OSX_SYSROOT} MATCH_POS)
    if(${MATCH_POS} EQUAL 0)
        # list(POP_FRONT LIBXML2_LIBRARIES LIBXML2_LIBRARY)
        set(LIBXML2_LIBRARY ${LIBXML2_PC_STATIC_LIBRARY})
        # Check that the new LIBXML2_LIBRARY from the pkg-config static libraries
        # is not also in the sysroot
        string(FIND ${LIBXML2_LIBRARY} ${CMAKE_OSX_SYSROOT} MATCH_POS)
        if(${MATCH_POS} EQUAL 0)
            # Replace it with the first library from the shared pkg-config library type
            list(GET _LIBXML2_LIBRARIES 0 LIBXML2_LIBRARY)
        endif()
    endif()
    set(LIBXML2_LIBRARIES ${LIBXML2_LIBRARY} ${LIBXML2_LIBRARIES})
else()
    list(REMOVE_AT LIBXML2_LIBRARIES 0)
    set(LIBXML2_LIBRARIES ${LIBXML2_LIBRARY} ${LIBXML2_LIBRARIES})
endif()

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(
    LibXml2
    FOUND_VAR LIBXML2_FOUND
    REQUIRED_VARS LIBXML2_LIBRARY LIBXML2_LIBRARIES LIBXML2_INCLUDE_DIR
    VERSION_VAR _LIBXML2_VERSION
)

# show the LIBXML2_INCLUDE_DIR, LIBXML2_LIBRARY and LIBXML2_LIBRARIES variables
# only in the advanced view
mark_as_advanced(LIBXML2_INCLUDE_DIR LIBXML2_LIBRARY LIBXML2_LIBRARIES)
