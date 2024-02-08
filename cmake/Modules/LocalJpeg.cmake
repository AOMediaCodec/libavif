set(LIB_DIR "${AVIF_SOURCE_DIR}/ext/libjpeg-turbo/build.libavif")
if(MSVC)
    set(LIB_FILENAME "${LIB_DIR}/${CMAKE_STATIC_LIBRARY_PREFIX}jpeg-static${CMAKE_STATIC_LIBRARY_SUFFIX}")
else()
    set(LIB_FILENAME "${LIB_DIR}/${CMAKE_STATIC_LIBRARY_PREFIX}jpeg${CMAKE_STATIC_LIBRARY_SUFFIX}")
endif()
if(NOT EXISTS "${LIB_FILENAME}")
    message(FATAL_ERROR "libavif: ${LIB_FILENAME} is missing, bailing out")
endif()

add_library(JPEG::JPEG STATIC IMPORTED GLOBAL)
set_target_properties(JPEG::JPEG PROPERTIES IMPORTED_LOCATION "${LIB_FILENAME}" AVIF_LOCAL ON)
set(JPEG_INCLUDE_DIR "${AVIF_SOURCE_DIR}/ext/libjpeg-turbo")
target_include_directories(JPEG::JPEG INTERFACE "${JPEG_INCLUDE_DIR}")

# Also add the build directory path because it contains jconfig.h,
# which is included by jpeglib.h.
target_include_directories(JPEG::JPEG INTERFACE "${LIB_DIR}")
