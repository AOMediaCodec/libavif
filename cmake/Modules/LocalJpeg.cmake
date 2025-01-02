set(AVIF_LOCAL_JPEG_TAG "3.0.4")

add_library(JPEG::JPEG STATIC IMPORTED GLOBAL)

set(LIB_DIR "${AVIF_SOURCE_DIR}/ext/libjpeg-turbo/build.libavif")
if(MSVC)
    set(LIB_FILENAME "${LIB_DIR}/${CMAKE_STATIC_LIBRARY_PREFIX}jpeg-static${CMAKE_STATIC_LIBRARY_SUFFIX}")
else()
    set(LIB_FILENAME "${LIB_DIR}/${CMAKE_STATIC_LIBRARY_PREFIX}jpeg${CMAKE_STATIC_LIBRARY_SUFFIX}")
endif()
if(EXISTS "${LIB_FILENAME}")
    message(STATUS "libavif(AVIF_JPEG=LOCAL): ${LIB_FILENAME} found, using for local JPEG")
    set(JPEG_INCLUDE_DIR "${AVIF_SOURCE_DIR}/ext/libjpeg-turbo")
else()
    message(STATUS "libavif(AVIF_JPEG=LOCAL): ${LIB_FILENAME} not found, fetching")
    set(LIB_DIR "${CMAKE_CURRENT_BINARY_DIR}/libjpeg/src/libjpeg-build")
    if(MSVC)
        set(LIB_FILENAME "${LIB_DIR}/${CMAKE_STATIC_LIBRARY_PREFIX}jpeg-static${CMAKE_STATIC_LIBRARY_SUFFIX}")
    else()
        set(LIB_FILENAME "${LIB_DIR}/${CMAKE_STATIC_LIBRARY_PREFIX}jpeg${CMAKE_STATIC_LIBRARY_SUFFIX}")
    endif()

    set(JPEG_INSTALL_DIR "${prefix}/libjpeg-install")

    # Set WITH_CRT_DLL to ON to compile libjpeg-turbo with /MD (use the DLL
    # version of the run-time library) instead of /MT (use the static version
    # of the run-time library) on Windows. On non-Windows platform, this causes
    # a CMake warning, which is safe to ignore:
    #   Manually-specified variables were not used by the project:
    #
    #     WITH_CRT_DLL
    ExternalProject_Add(
        libjpeg
        PREFIX ${CMAKE_CURRENT_BINARY_DIR}/libjpeg
        GIT_REPOSITORY "https://github.com/libjpeg-turbo/libjpeg-turbo.git"
        GIT_TAG "${AVIF_LOCAL_JPEG_TAG}"
        LIST_SEPARATOR |
        BUILD_COMMAND ${CMAKE_COMMAND} --build <BINARY_DIR> --config $<CONFIG> --target jpeg-static
        CMAKE_ARGS -DCMAKE_C_COMPILER=${CMAKE_C_COMPILER}
                   -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}
                   -DCMAKE_C_FLAGS=${CMAKE_C_FLAGS}
                   -DCMAKE_C_FLAGS_DEBUG=${CMAKE_C_FLAGS_DEBUG}
                   -DCMAKE_C_FLAGS_RELEASE=${CMAKE_C_FLAGS_RELEASE}
                   -DCMAKE_C_FLAGS_MINSIZEREL=${CMAKE_C_FLAGS_MINSIZEREL}
                   -DCMAKE_C_FLAGS_RELWITHDEBINFO=${CMAKE_C_FLAGS_RELWITHDEBINFO}
                   -DENABLE_SHARED=OFF
                   -DENABLE_STATIC=ON
                   -DCMAKE_BUILD_TYPE=Release
                   -DWITH_TURBOJPEG=OFF
                   -DWITH_CRT_DLL=ON
        BUILD_BYPRODUCTS "${LIB_FILENAME}"
        INSTALL_COMMAND ""
    )
    add_dependencies(JPEG::JPEG libjpeg)
    set(JPEG_INCLUDE_DIR ${CMAKE_CURRENT_BINARY_DIR}/libjpeg/src/libjpeg)
endif()

set_target_properties(JPEG::JPEG PROPERTIES IMPORTED_LOCATION "${LIB_FILENAME}" AVIF_LOCAL ON)
target_include_directories(JPEG::JPEG INTERFACE "${JPEG_INCLUDE_DIR}")

# Also add the build directory path because it contains jconfig.h,
# which is included by jpeglib.h.
target_include_directories(JPEG::JPEG INTERFACE "${LIB_DIR}")
