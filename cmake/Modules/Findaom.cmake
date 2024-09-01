# - Try to find aom
# Once done this will define
#
#  AOM_FOUND - system has aom
#  AOM_INCLUDE_DIR - the aom include directory
#  AOM_LIBRARIES - Link these to use aom
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
    pkg_check_modules(_AOM aom)
endif(PKG_CONFIG_FOUND)

find_path(AOM_INCLUDE_DIR NAMES aom/aom.h PATHS ${_AOM_INCLUDEDIR})

find_library(AOM_LIBRARY NAMES aom PATHS ${_AOM_LIBDIR})

set(AOM_LIBRARIES ${AOM_LIBRARIES} ${AOM_LIBRARY})

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(aom REQUIRED_VARS AOM_LIBRARY AOM_LIBRARIES AOM_INCLUDE_DIR VERSION_VAR _AOM_VERSION)

# show the AOM_INCLUDE_DIR, AOM_LIBRARY and AOM_LIBRARIES variables only
# in the advanced view
mark_as_advanced(AOM_INCLUDE_DIR AOM_LIBRARY AOM_LIBRARIES)

if(AOM_LIBRARY)
    if("${AOM_LIBRARY}" MATCHES "\\${CMAKE_STATIC_LIBRARY_SUFFIX}$")
        add_library(aom STATIC IMPORTED GLOBAL)

        set(_AOM_PC_LIBRARIES "${_AOM_STATIC_LIBRARIES}")
        # remove "aom" so we only have library dependencies
        list(REMOVE_ITEM _AOM_PC_LIBRARIES "aom")

        # Add absolute paths to libraries
        foreach(_lib ${_AOM_PC_LIBRARIES})
            find_library(_aom_dep_lib_${_lib} ${_lib} HINTS ${_AOM_STATIC_LIBRARY_DIRS})
            target_link_libraries(aom INTERFACE ${_aom_dep_lib_${_lib}})
        endforeach()
    else()
        add_library(aom SHARED IMPORTED GLOBAL)
    endif()
    set_target_properties(aom PROPERTIES IMPORTED_LOCATION "${AOM_LIBRARY}" IMPORTED_IMPLIB "${AOM_LIBRARY}")
    target_include_directories(aom INTERFACE ${AOM_INCLUDE_DIR})
endif()
