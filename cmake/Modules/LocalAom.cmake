set(AVIF_LOCAL_AOM_GIT_TAG v3.9.1)
set(AVIF_LOCAL_AVM_GIT_TAG research-v7.0.1)

if(AVIF_CODEC_AVM)
    # Building the avm repository generates files such as "libaom.a" because it is a fork of aom,
    # so its build can be treated the same as aom
    set(AOM_PACKAGE_NAME avm)
    set(AOM_MESSAGE_PREFIX "libavif(AVIF_CODEC_AVM=LOCAL)")
else()
    set(AOM_PACKAGE_NAME aom)
    set(AOM_MESSAGE_PREFIX "libavif(AVIF_CODEC_AOM=LOCAL)")
endif()

set(AOM_EXT_SOURCE_DIR "${AVIF_SOURCE_DIR}/ext/${AOM_PACKAGE_NAME}")
set(LIB_FILENAME "${AVIF_SOURCE_DIR}/ext/aom/build.libavif/${CMAKE_STATIC_LIBRARY_PREFIX}aom${CMAKE_STATIC_LIBRARY_SUFFIX}")

if(EXISTS "${LIB_FILENAME}")
    message(STATUS "${AOM_MESSAGE_PREFIX}: compiled library found at ${LIB_FILENAME}")
    add_library(aom STATIC IMPORTED GLOBAL)
    set_target_properties(aom PROPERTIES IMPORTED_LOCATION "${LIB_FILENAME}" AVIF_LOCAL ON)
    target_include_directories(aom INTERFACE "${AOM_EXT_SOURCE_DIR}")
    if(AVIF_CODEC_AVM)
        # ext/avm/aom/aom_encoder.h includes config/aom_config.h which is generated by the local build of avm.
        target_include_directories(aom INTERFACE "${AOM_EXT_SOURCE_DIR}/build.libavif")
    endif()

    # Add link dependency flags from the aom.pc file in ext/aom or ext/avm
    # by prepending the build directory to PKG_CONFIG_PATH and then calling
    # pkg_check_modules
    if(MSVC)
        set(ENV{PKG_CONFIG_PATH} "${AOM_EXT_SOURCE_DIR}/build.libavif;$ENV{PKG_CONFIG_PATH}")
    else()
        set(ENV{PKG_CONFIG_PATH} "${AOM_EXT_SOURCE_DIR}/build.libavif:$ENV{PKG_CONFIG_PATH}")
    endif()

    pkg_check_modules(_AOM QUIET aom)

    set(_AOM_PC_LIBRARIES ${_AOM_STATIC_LIBRARIES})
    # remove "aom" so we only have library dependencies
    list(REMOVE_ITEM _AOM_PC_LIBRARIES "aom")

    # Add absolute paths to libraries
    foreach(_lib ${_AOM_PC_LIBRARIES})
      find_library(_aom_dep_lib_${_lib} ${_lib} HINTS ${_AOM_STATIC_LIBRARY_DIRS})
      target_link_libraries(aom INTERFACE ${_aom_dep_lib_${_lib}})
    endforeach()
else()
    message(STATUS "${AOM_MESSAGE_PREFIX}: compiled library not found at ${LIB_FILENAME}, using FetchContent")
    if(EXISTS "${AOM_EXT_SOURCE_DIR}")
        message(STATUS "${AOM_MESSAGE_PREFIX}: ext/${AOM_PACKAGE_NAME} found; using as FetchContent SOURCE_DIR")
        set(FETCHCONTENT_SOURCE_DIR_AOM "${AOM_EXT_SOURCE_DIR}")
        message(CHECK_START "${AOM_MESSAGE_PREFIX}: configuring ${AOM_PACKAGE_NAME}")
    else()
        message(CHECK_START "${AOM_MESSAGE_PREFIX}: fetching and configuring ${AOM_PACKAGE_NAME}")
    endif()

    # aom sets its compile options by setting variables like CMAKE_C_FLAGS_RELEASE using
    # CACHE FORCE, which effectively adds those flags to all targets. We stash and restore
    # the original values and call avif_set_aom_compile_options to instead set the flags on all aom
    # targets
    function(avif_set_aom_compile_options target config)
        string(REPLACE " " ";" AOM_C_FLAGS_LIST "${CMAKE_C_FLAGS_${config}}")
        string(REPLACE " " ";" AOM_CXX_FLAGS_LIST "${CMAKE_CXX_FLAGS_${config}}")
        foreach(flag ${AOM_C_FLAGS_LIST})
            target_compile_options(${target} PRIVATE $<$<COMPILE_LANGUAGE:C>:${flag}>)
        endforeach()
        foreach(flag ${AOM_CXX_FLAGS_LIST})
            target_compile_options(${target} PRIVATE $<$<COMPILE_LANGUAGE:CXX>:${flag}>)
        endforeach()

        get_target_property(sources ${target} SOURCES)
        foreach(src ${sources})
            if(src MATCHES "TARGET_OBJECTS:")
                string(REGEX REPLACE "\\$<TARGET_OBJECTS:(.*)>" "\\1" source_target ${src})
                avif_set_aom_compile_options(${source_target} ${config})
            endif()
        endforeach()
    endfunction()

    set(AOM_BINARY_DIR "${FETCHCONTENT_BASE_DIR}/${AOM_PACKAGE_NAME}-build")

    if(ANDROID_ABI)
        set(AOM_BINARY_DIR "${AOM_BINARY_DIR}/${ANDROID_ABI}")
    endif()

    if(AVIF_CODEC_AVM)
        FetchContent_Declare(
            libaom
            GIT_REPOSITORY "https://gitlab.com/AOMediaCodec/avm.git"
            BINARY_DIR "${AOM_BINARY_DIR}"
            GIT_TAG ${AVIF_LOCAL_AVM_GIT_TAG}
            GIT_PROGRESS ON
            GIT_SHALLOW ON
            UPDATE_COMMAND ""
        )
    else()
        FetchContent_Declare(
            libaom URL "https://aomedia.googlesource.com/aom/+archive/${AVIF_LOCAL_AOM_GIT_TAG}.tar.gz" BINARY_DIR
                       "${AOM_BINARY_DIR}" UPDATE_COMMAND ""
        )
    endif()

    set(CONFIG_PIC 1 CACHE INTERNAL "")
    if(libyuv_FOUND)
        set(CONFIG_LIBYUV 0 CACHE INTERNAL "")
    else()
        set(CONFIG_LIBYUV 1 CACHE INTERNAL "")
    endif()
    set(CONFIG_WEBM_IO 0 CACHE INTERNAL "")
    set(ENABLE_DOCS 0 CACHE INTERNAL "")
    set(ENABLE_EXAMPLES 0 CACHE INTERNAL "")
    set(ENABLE_TESTDATA 0 CACHE INTERNAL "")
    set(ENABLE_TESTS 0 CACHE INTERNAL "")
    set(ENABLE_TOOLS 0 CACHE INTERNAL "")
    if(CMAKE_OSX_ARCHITECTURES STREQUAL "arm64")
        set(AOM_TARGET_CPU "arm64")
    endif()

    if(NOT libaom_POPULATED)
        # Guard against the project setting cmake variables that would affect the parent build
        # See comment above for avif_set_aom_compile_options
        foreach(_config_setting CMAKE_C_FLAGS CMAKE_CXX_FLAGS CMAKE_EXE_LINKER_FLAGS)
            foreach(_config_type DEBUG RELEASE MINSIZEREL RELWITHDEBINFO)
                set(${_config_setting}_${_config_type}_ORIG ${${_config_setting}_${_config_type}})
            endforeach()
        endforeach()

        avif_fetchcontent_populate_cmake(libaom)

        set(_aom_config RELEASE)
        if(CMAKE_BUILD_TYPE)
            string(TOUPPER ${CMAKE_BUILD_TYPE} _aom_config)
        endif()
        list(LENGTH CMAKE_CONFIGURATION_TYPES num_configs)
        if(${num_configs} GREATER 0)
            list(GET CMAKE_CONFIGURATION_TYPES 0 _aom_config_type)
            string(TOUPPER ${_aom_config_type} _aom_config)
        endif()
        avif_set_aom_compile_options(aom ${_aom_config})

        foreach(_config_setting CMAKE_C_FLAGS CMAKE_CXX_FLAGS CMAKE_EXE_LINKER_FLAGS)
            foreach(_config_type DEBUG RELEASE MINSIZEREL RELWITHDEBINFO)
                set(${_config_setting}_${_config_type} ${${_config_setting}_${_config_type}_ORIG} CACHE STRING "" FORCE)
                unset(${_config_setting}_${_config_type}_ORIG)
            endforeach()
        endforeach()
        unset(_config_type)
        unset(_config_setting)
    endif()

    # If we have libyuv, we disable CONFIG_LIBYUV so that aom does not include the libyuv
    # sources from its third-party vendor library. But we still want AOM to have libyuv, only
    # linked against this project's target. Here we update the value in aom_config.h and add libyuv
    # to AOM's link libraries
    if(libyuv_FOUND)
        file(READ ${AOM_BINARY_DIR}/config/aom_config.h AOM_CONFIG_H)
        if("${AOM_CONFIG_H}" MATCHES "CONFIG_LIBYUV 0")
            string(REPLACE "CONFIG_LIBYUV 0" "CONFIG_LIBYUV 1" AOM_CONFIG_H "${AOM_CONFIG_H}")
            file(WRITE ${AOM_BINARY_DIR}/config/aom_config.h "${AOM_CONFIG_H}")
        endif()
        target_link_libraries(aom PRIVATE $<TARGET_FILE:yuv::yuv>)
    endif()

    set_property(TARGET aom PROPERTY AVIF_LOCAL ON)
    target_include_directories(aom INTERFACE "${libaom_SOURCE_DIR}" ${AOM_BINARY_DIR})

    message(CHECK_PASS "complete")
endif()
