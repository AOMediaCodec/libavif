# Copyright 2019 Joe Drago. All rights reserved.
# SPDX-License-Identifier: BSD-2-Clause

cmake_minimum_required(VERSION 3.5)

# Specify search path for CMake modules to be loaded by include()
# and find_package()
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake/Modules")

project(libavif LANGUAGES C VERSION 0.8.1)

# Set C99 as the default
set(CMAKE_C_STANDARD 99)

# SOVERSION scheme: MAJOR.MINOR.PATCH
#   If there was an incompatible interface change:
#     Increment MAJOR. Set MINOR and PATCH to 0
#   If there was a compatible interface change:
#     Increment MINOR. Set PATCH to 0
#   If the source code was changed, but there were no interface changes:
#     Increment PATCH.
set(LIBRARY_VERSION_MAJOR 6)
set(LIBRARY_VERSION_MINOR 0)
set(LIBRARY_VERSION_PATCH 0)
set(LIBRARY_VERSION "${LIBRARY_VERSION_MAJOR}.${LIBRARY_VERSION_MINOR}.${LIBRARY_VERSION_PATCH}")
set(LIBRARY_SOVERSION ${LIBRARY_VERSION_MAJOR})

option(BUILD_SHARED_LIBS "Build shared avif library" ON)

option(AVIF_ENABLE_WERROR "Treat all compiler warnings as errors" ON)

option(AVIF_CODEC_AOM "Use the AOM codec for encoding (and decoding if no other decoder is present)" OFF)
option(AVIF_CODEC_DAV1D "Use the dav1d codec for decoding (overrides AOM decoding if also enabled)" OFF)
option(AVIF_CODEC_LIBGAV1 "Use the libgav1 codec for decoding (overrides AOM decoding if also enabled)" OFF)
option(AVIF_CODEC_RAV1E "Use the rav1e codec for encoding (overrides AOM encoding if also enabled)" OFF)

option(AVIF_LOCAL_AOM "Build the AOM codec by providing your own copy of the repo in ext/aom (see Local Builds in README)" OFF)
option(AVIF_LOCAL_DAV1D "Build the dav1d codec by providing your own copy of the repo in ext/dav1d (see Local Builds in README)" OFF)
option(AVIF_LOCAL_LIBGAV1 "Build the libgav1 codec by providing your own copy of the repo in ext/libgav1 (see Local Builds in README)" OFF)
option(AVIF_LOCAL_RAV1E "Build the rav1e codec by providing your own copy of the repo in ext/rav1e (see Local Builds in README)" OFF)

if(AVIF_LOCAL_LIBGAV1)
    enable_language(CXX)
endif()

# ---------------------------------------------------------------------------------------
# This insanity is for people embedding libavif or making fully static or Windows builds.
# Any proper unix environment should ignore these entire following blocks.
option(AVIF_LOCAL_ZLIBPNG "Build zlib and libpng by providing your own copy inside the ext subdir." OFF)
if(AVIF_LOCAL_ZLIBPNG)
    add_subdirectory(ext/zlib)
    set(ZLIB_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/ext/zlib")
    include_directories("${CMAKE_CURRENT_BINARY_DIR}/ext/zlib")
    set(CMAKE_DEBUG_POSTFIX "")

    # This is the only way I could avoid libpng going crazy if it found awk.exe, seems benign otherwise
    set(PREV_ANDROID ${ANDROID})
    set(ANDROID TRUE)
    set(PNG_BUILD_ZLIB "${CMAKE_CURRENT_SOURCE_DIR}/ext/zlib" CACHE STRING "" FORCE)
    set(PNG_SHARED OFF CACHE BOOL "")
    set(PNG_TESTS OFF CACHE BOOL "")
    add_subdirectory(ext/libpng)
    set(PNG_PNG_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/ext/libpng")
    set(PNG_LIBRARY png_static)
    include_directories("${CMAKE_CURRENT_BINARY_DIR}/ext/libpng")
    set(ANDROID ${PREV_ANDROID})

    set(ZLIB_LIBRARY zlibstatic)
endif()
option(AVIF_LOCAL_JPEG "Build jpeg by providing your own copy inside the ext subdir." OFF)
if(AVIF_LOCAL_JPEG)
    add_subdirectory(ext/libjpeg)
    if("${CMAKE_SOURCE_DIR}" STREQUAL  "${CMAKE_CURRENT_SOURCE_DIR}")
        set(JPEG_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/ext/libjpeg")
        set(JPEG_LIBRARY jpeg)
    else()
        set(JPEG_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/ext/libjpeg" PARENT_SCOPE)
        set(JPEG_LIBRARY jpeg PARENT_SCOPE)
    endif()
endif()
# ---------------------------------------------------------------------------------------

# Enable all warnings
include(CheckCCompilerFlag)
if(CMAKE_C_COMPILER_ID MATCHES "Clang")
    MESSAGE(STATUS "libavif: Enabling warnings for Clang")
    add_definitions(
        -Weverything
        -Wno-bad-function-cast
        -Wno-cast-align
        -Wno-conversion
        -Wno-covered-switch-default
        -Wno-disabled-macro-expansion
        -Wno-documentation
        -Wno-documentation-unknown-command
        -Wno-double-promotion
        -Wno-float-equal
        -Wno-missing-noreturn
        -Wno-padded
        -Wno-sign-conversion
        -Wno-error=c11-extensions
    )
    # The detection of cross compilation by -Wpoison-system-directories has false positives on macOS because
    # --sysroot is implicitly added. Turn the warning off.
    check_c_compiler_flag(-Wpoison-system-directories HAVE_POISON_SYSTEM_DIRECTORIES_WARNING)
    if(HAVE_POISON_SYSTEM_DIRECTORIES_WARNING)
        add_definitions(-Wno-poison-system-directories)
    endif()
elseif(CMAKE_C_COMPILER_ID MATCHES "GNU")
    MESSAGE(STATUS "libavif: Enabling warnings for GCC")
    add_definitions(-Wall -Wextra)
elseif(CMAKE_C_COMPILER_ID MATCHES "MSVC")
    MESSAGE(STATUS "libavif: Enabling warnings for MS CL")
    add_definitions(
        /Wall   # All warnings
        /wd4255 # Disable: no function prototype given
        /wd4324 # Disable: structure was padded due to alignment specifier
        /wd4668 # Disable: is not defined as a preprocessor macro, replacing with '0'
        /wd4710 # Disable: function not inlined
        /wd4711 # Disable: function selected for inline expansion
        /wd4738 # Disable: storing 32-bit float result in memory, possible loss of performance
        /wd4820 # Disable: bytes padding added after data member
        /wd4996 # Disable: potentially unsafe stdlib methods
        /wd5045 # Disable: Compiler will insert Spectre mitigation for memory load if /Qspectre switch specified
    )
else()
    MESSAGE(FATAL_ERROR "libavif: Unknown compiler, bailing out")
endif()

if(AVIF_ENABLE_WERROR)
    # Warnings as errors
    if(CMAKE_C_COMPILER_ID MATCHES "Clang" OR CMAKE_C_COMPILER_ID MATCHES "GNU")
        add_definitions(-Werror)
    elseif(CMAKE_C_COMPILER_ID MATCHES "MSVC")
        add_definitions(/WX)
    else()
        MESSAGE(FATAL_ERROR "libavif: Unknown compiler, bailing out")
    endif()
endif()

set(AVIF_SRCS
    src/alpha.c
    src/avif.c
    src/colr.c
    src/mem.c
    src/obu.c
    src/rawdata.c
    src/read.c
    src/reformat.c
    src/stream.c
    src/utils.c
    src/write.c
)

set(AVIF_PLATFORM_LIBRARIES)
if(UNIX)
    # Find out if we have threading available
    set(CMAKE_THREAD_PREFER_PTHREADS ON)
    set(THREADS_PREFER_PTHREAD_FLAG ON)
    find_package(Threads)
    set(AVIF_PLATFORM_LIBRARIES m Threads::Threads)
endif()

set(AVIF_CODEC_DEFINITIONS)
set(AVIF_CODEC_INCLUDES)
set(AVIF_CODEC_LIBRARIES)

if(AVIF_CODEC_DAV1D)
    message(STATUS "libavif: Codec enabled: dav1d (decode)")
    set(AVIF_CODEC_DEFINITIONS ${AVIF_CODEC_DEFINITIONS} -DAVIF_CODEC_DAV1D=1)
    set(AVIF_SRCS ${AVIF_SRCS}
        src/codec_dav1d.c
    )

    if(AVIF_LOCAL_DAV1D)
        set(LIB_FILENAME "${CMAKE_CURRENT_SOURCE_DIR}/ext/dav1d/build/src/libdav1d.a")
        if(NOT EXISTS "${LIB_FILENAME}")
            message(FATAL_ERROR "libavif: ${LIB_FILENAME} is missing, bailing out")
        endif()

        set(AVIF_CODEC_INCLUDES ${AVIF_CODEC_INCLUDES}
            "${CMAKE_CURRENT_SOURCE_DIR}/ext/dav1d/build"
            "${CMAKE_CURRENT_SOURCE_DIR}/ext/dav1d/build/include"
            "${CMAKE_CURRENT_SOURCE_DIR}/ext/dav1d/build/include/dav1d"
            "${CMAKE_CURRENT_SOURCE_DIR}/ext/dav1d/include"
        )
        set(AVIF_CODEC_LIBRARIES ${AVIF_CODEC_LIBRARIES} ${LIB_FILENAME})
    else()
        # Check to see if dav1d is independently being built by the outer CMake project
        if(NOT TARGET dav1d)
            find_package(dav1d REQUIRED)
            set(AVIF_CODEC_INCLUDES ${AVIF_CODEC_INCLUDES} ${DAV1D_INCLUDE_DIR})
        endif()
        set(AVIF_CODEC_LIBRARIES ${AVIF_CODEC_LIBRARIES} ${DAV1D_LIBRARY})
    endif()

    if(UNIX AND NOT APPLE)
        set(AVIF_PLATFORM_LIBRARIES ${AVIF_PLATFORM_LIBRARIES} ${CMAKE_DL_LIBS}) # for dlsym
    endif()
endif()

if(AVIF_CODEC_LIBGAV1)
    message(STATUS "libavif: Codec enabled: libgav1 (decode)")
    set(AVIF_CODEC_DEFINITIONS ${AVIF_CODEC_DEFINITIONS} -DAVIF_CODEC_LIBGAV1=1)
    set(AVIF_SRCS ${AVIF_SRCS}
        src/codec_libgav1.c
    )

    if(AVIF_LOCAL_LIBGAV1)
        set(LIB_FILENAME "${CMAKE_CURRENT_SOURCE_DIR}/ext/libgav1/build/libgav1${CMAKE_STATIC_LIBRARY_SUFFIX}")
        if(NOT EXISTS "${LIB_FILENAME}")
            message(FATAL_ERROR "libavif: ${LIB_FILENAME} is missing, bailing out")
        endif()

        set(AVIF_CODEC_INCLUDES ${AVIF_CODEC_INCLUDES}
            "${CMAKE_CURRENT_SOURCE_DIR}/ext/libgav1/src"
        )
        set(AVIF_CODEC_LIBRARIES ${AVIF_CODEC_LIBRARIES} ${LIB_FILENAME})
    else()
        # Check to see if libgav1 is independently being built by the outer CMake project
        if(NOT TARGET libgav1)
            find_package(libgav1 REQUIRED)
            set(AVIF_CODEC_INCLUDES ${AVIF_CODEC_INCLUDES} ${LIBGAV1_INCLUDE_DIR})
        endif()
        set(AVIF_CODEC_LIBRARIES ${AVIF_CODEC_LIBRARIES} ${LIBGAV1_LIBRARY})
    endif()
endif()

if(AVIF_CODEC_RAV1E)
    message(STATUS "libavif: Codec enabled: rav1e (encode)")
    set(AVIF_CODEC_DEFINITIONS ${AVIF_CODEC_DEFINITIONS} -DAVIF_CODEC_RAV1E=1)
    set(AVIF_SRCS ${AVIF_SRCS}
        src/codec_rav1e.c
    )

    if(AVIF_LOCAL_RAV1E)
        set(LIB_FILENAME "${CMAKE_CURRENT_SOURCE_DIR}/ext/rav1e/target/release/${CMAKE_STATIC_LIBRARY_PREFIX}rav1e${CMAKE_STATIC_LIBRARY_SUFFIX}")
        if(NOT EXISTS "${LIB_FILENAME}")
            message(FATAL_ERROR "libavif: compiled rav1e library is missing (in ext/rav1e/target/release), bailing out")
        endif()

        set(AVIF_CODEC_INCLUDES ${AVIF_CODEC_INCLUDES}
            "${CMAKE_CURRENT_SOURCE_DIR}/ext/rav1e/target/release/include"
        )
        set(AVIF_CODEC_LIBRARIES ${AVIF_CODEC_LIBRARIES} ${LIB_FILENAME})
    else()
        # Check to see if rav1e is independently being built by the outer CMake project
        if(NOT TARGET rav1e)
            find_package(rav1e REQUIRED)
            set(AVIF_CODEC_INCLUDES ${AVIF_CODEC_INCLUDES} ${RAV1E_INCLUDE_DIR})
        endif()
        set(AVIF_CODEC_LIBRARIES ${AVIF_CODEC_LIBRARIES} ${RAV1E_LIBRARY})
    endif()

    # Unfortunately, rav1e requires a few more libraries
    if(WIN32)
        set(AVIF_PLATFORM_LIBRARIES ${AVIF_PLATFORM_LIBRARIES} ws2_32.lib userenv.lib)
    elseif(UNIX AND NOT APPLE)
        set(AVIF_PLATFORM_LIBRARIES ${AVIF_PLATFORM_LIBRARIES} ${CMAKE_DL_LIBS}) # for backtrace
    endif()
endif()

if(AVIF_CODEC_AOM)
    message(STATUS "libavif: Codec enabled: aom (encode/decode)")
    set(AVIF_CODEC_DEFINITIONS ${AVIF_CODEC_DEFINITIONS} -DAVIF_CODEC_AOM=1)
    set(AVIF_SRCS ${AVIF_SRCS}
        src/codec_aom.c
    )
    if(AVIF_LOCAL_AOM)
        set(LIB_FILENAME "${CMAKE_CURRENT_SOURCE_DIR}/ext/aom/build.libavif/${CMAKE_STATIC_LIBRARY_PREFIX}aom${CMAKE_STATIC_LIBRARY_SUFFIX}")
        if(NOT EXISTS "${LIB_FILENAME}")
            message(FATAL_ERROR "libavif: ${LIB_FILENAME} is missing, bailing out")
        endif()

        set(AVIF_CODEC_INCLUDES ${AVIF_CODEC_INCLUDES}
            "${CMAKE_CURRENT_SOURCE_DIR}/ext/aom"
            "${CMAKE_CURRENT_SOURCE_DIR}/ext/aom/build.avif"
        )
        set(AVIF_CODEC_LIBRARIES ${AVIF_CODEC_LIBRARIES} ${LIB_FILENAME})
        message(STATUS "LIBAOM_INCLUDE_PATH: ${CMAKE_CURRENT_SOURCE_DIR}/ext/aom")
    else()
        # Check to see if aom is independently being built by the outer CMake project
        if(NOT TARGET aom)
            find_package(aom REQUIRED)
            set(AVIF_CODEC_INCLUDES ${AVIF_CODEC_INCLUDES} ${AOM_INCLUDE_DIR})
        endif()
        set(AVIF_CODEC_LIBRARIES ${AVIF_CODEC_LIBRARIES} ${AOM_LIBRARY})
    endif()
endif()

if(NOT AVIF_CODEC_AOM AND NOT AVIF_CODEC_DAV1D AND NOT AVIF_CODEC_LIBGAV1)
    message(WARNING "libavif: No decoding library is enabled.")
endif()

add_library(avif ${AVIF_SRCS})
set_target_properties(avif
                      PROPERTIES
                          VERSION ${LIBRARY_VERSION}
                          SOVERSION ${LIBRARY_SOVERSION})
target_compile_definitions(avif
                           PRIVATE ${AVIF_CODEC_DEFINITIONS})
target_link_libraries(avif
                      PRIVATE ${AVIF_CODEC_LIBRARIES} ${AVIF_PLATFORM_LIBRARIES})
target_include_directories(avif
                           PUBLIC $<BUILD_INTERFACE:${libavif_SOURCE_DIR}/include>
                                  $<INSTALL_INTERFACE:include>
                           PRIVATE ${AVIF_CODEC_INCLUDES})

option(AVIF_BUILD_EXAMPLES "Build avif Examples." OFF)
if(AVIF_BUILD_EXAMPLES)
    add_executable(avif_example1 examples/avif_example1.c)
    if(AVIF_LOCAL_LIBGAV1)
        set_target_properties(avif_example1 PROPERTIES LINKER_LANGUAGE "CXX")
    endif()
    target_link_libraries(avif_example1 avif ${AVIF_PLATFORM_LIBRARIES})
endif()

if(CMAKE_SKIP_INSTALL_RULES)
    set(SKIP_INSTALL_ALL TRUE)
endif()

if(NOT SKIP_INSTALL_ALL)
    include(GNUInstallDirs)
endif()

option(AVIF_BUILD_APPS "Build avif apps." OFF)
if(AVIF_BUILD_APPS)
    find_package(ZLIB REQUIRED)
    find_package(PNG REQUIRED)
    find_package(JPEG REQUIRED)

    add_executable(avifenc
        apps/avifenc.c

        apps/shared/avifjpeg.c
        apps/shared/iccjpeg.c
        apps/shared/avifpng.c
        apps/shared/avifutil.c
        apps/shared/y4m.c
    )
    if(AVIF_LOCAL_LIBGAV1)
        set_target_properties(avifenc PROPERTIES LINKER_LANGUAGE "CXX")
    endif()
    target_link_libraries(avifenc avif ${AVIF_PLATFORM_LIBRARIES} ${ZLIB_LIBRARY} ${PNG_LIBRARY} ${JPEG_LIBRARY})
    target_include_directories(avifenc
                               PRIVATE
                                   $<TARGET_PROPERTY:avif,INTERFACE_INCLUDE_DIRECTORIES>
                                   ${CMAKE_CURRENT_SOURCE_DIR}/apps/shared
                                   ${ZLIB_INCLUDE_DIR}
                                   ${PNG_PNG_INCLUDE_DIR}
                                   ${JPEG_INCLUDE_DIR})
    add_executable(avifdec
        apps/avifdec.c

        apps/shared/avifjpeg.c
        apps/shared/iccjpeg.c
        apps/shared/avifpng.c
        apps/shared/avifutil.c
        apps/shared/y4m.c
    )
    if(AVIF_LOCAL_LIBGAV1)
        set_target_properties(avifdec PROPERTIES LINKER_LANGUAGE "CXX")
    endif()
    target_link_libraries(avifdec avif ${AVIF_PLATFORM_LIBRARIES} ${ZLIB_LIBRARY} ${PNG_LIBRARY} ${JPEG_LIBRARY})
    target_include_directories(avifdec
                               PRIVATE
                                   $<TARGET_PROPERTY:avif,INTERFACE_INCLUDE_DIRECTORIES>
                                   ${CMAKE_CURRENT_SOURCE_DIR}/apps/shared
                                   ${ZLIB_INCLUDE_DIR}
                                   ${PNG_PNG_INCLUDE_DIR}
                                   ${JPEG_INCLUDE_DIR})

    if(NOT SKIP_INSTALL_APPS AND NOT SKIP_INSTALL_ALL)
        install(TARGETS avifenc avifdec
            RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
            ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
            LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}"
        )
    endif()
endif()

option(AVIF_BUILD_TESTS "Build avif tests." OFF)
if(AVIF_BUILD_TESTS)
    add_executable(aviftest
        apps/shared/y4m.c
        tests/aviftest.c
        tests/cJSON.c
        tests/compare.c
        tests/testcase.c
    )
    if(AVIF_LOCAL_LIBGAV1)
        set_target_properties(aviftest PROPERTIES LINKER_LANGUAGE "CXX")
    endif()
    target_link_libraries(aviftest avif ${AVIF_PLATFORM_LIBRARIES})
    target_include_directories(aviftest PRIVATE apps/shared)

    add_executable(avifyuv
        tests/avifyuv.c
    )
    if(AVIF_LOCAL_LIBGAV1)
        set_target_properties(avifyuv PROPERTIES LINKER_LANGUAGE "CXX")
    endif()
    target_link_libraries(avifyuv avif ${AVIF_PLATFORM_LIBRARIES})

    add_custom_target(avif_test_all
        COMMAND $<TARGET_FILE:aviftest> ${CMAKE_CURRENT_SOURCE_DIR}/tests/data
        DEPENDS aviftest
    )
endif()

configure_file(libavif.pc.cmake ${CMAKE_CURRENT_BINARY_DIR}/libavif.pc @ONLY)

if(NOT SKIP_INSTALL_LIBRARIES AND NOT SKIP_INSTALL_ALL)
    install(TARGETS avif
        EXPORT ${PROJECT_NAME}-config
        RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
        ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
        LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}"
    )

    if (BUILD_SHARED_LIBS)
        install(EXPORT ${PROJECT_NAME}-config
                DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/${PROJECT_NAME})

        include(CMakePackageConfigHelpers)
        write_basic_package_version_file(${PROJECT_NAME}-config-version.cmake
                                         VERSION ${PROJECT_VERSION}
                                         COMPATIBILITY SameMajorVersion)
        install(FILES ${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}-config-version.cmake
                DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/${PROJECT_NAME})
    endif()

    install(FILES ${CMAKE_CURRENT_BINARY_DIR}/libavif.pc
            DESTINATION ${CMAKE_INSTALL_LIBDIR}/pkgconfig)
endif()
if(NOT SKIP_INSTALL_HEADERS AND NOT SKIP_INSTALL_ALL)
    install(FILES include/avif/avif.h
        DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/avif"
    )
endif()

# ---------------------------------------------------------------------------------------
# Win32 (Visual Studio) fixups

macro(avif_set_folder_safe target folder)
    if(TARGET ${target})
        set_target_properties(${target} PROPERTIES FOLDER ${folder})
    endif()
endmacro()

macro(avif_exclude_safe target)
    if(TARGET ${target})
        set_target_properties(${target} PROPERTIES EXCLUDE_FROM_DEFAULT_BUILD True)
    endif()
endmacro()

if(WIN32)
    set_property(GLOBAL PROPERTY USE_FOLDERS ON)

    avif_set_folder_safe(avif "ext/avif")
    if(AVIF_LOCAL_ZLIBPNG)
        avif_set_folder_safe(example "ext/zlibpng")
        avif_set_folder_safe(genfiles "ext/zlibpng")
        avif_set_folder_safe(minigzip "ext/zlibpng")
        avif_set_folder_safe(png_static "ext/zlibpng")
        avif_set_folder_safe(zlib "ext/zlibpng")
        avif_set_folder_safe(zlibstatic "ext/zlibpng")

        # Don't bother building these targets
        avif_exclude_safe(example)
        avif_exclude_safe(genfiles)
        avif_exclude_safe(minigzip)
    endif()
    if(AVIF_LOCAL_JPEG)
        avif_set_folder_safe(jpeg "ext/libjpeg")
    endif()
endif()

add_subdirectory(contrib)
