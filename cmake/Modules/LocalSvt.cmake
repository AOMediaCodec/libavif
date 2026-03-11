set(AVIF_SVT_GIT_TAG "v4.0.1")

set(LIB_FILENAME "${AVIF_SOURCE_DIR}/ext/SVT-AV1/Bin/Release/${AVIF_LIBRARY_PREFIX}SvtAv1Enc${CMAKE_STATIC_LIBRARY_SUFFIX}")

if(EXISTS "${LIB_FILENAME}")
    message(STATUS "libavif(AVIF_CODEC_SVT=LOCAL): compiled library found at ${LIB_FILENAME}")
    add_library(SvtAv1Enc STATIC IMPORTED GLOBAL)
    set_target_properties(SvtAv1Enc PROPERTIES IMPORTED_LOCATION "${LIB_FILENAME}" AVIF_LOCAL ON)
    target_include_directories(SvtAv1Enc INTERFACE "${AVIF_SOURCE_DIR}/ext/SVT-AV1/include")
else()
    message(STATUS "libavif(AVIF_CODEC_SVT=LOCAL): compiled library not found at ${LIB_FILENAME}; using FetchContent")
    if(EXISTS "${AVIF_SOURCE_DIR}/ext/SVT-AV1")
        message(STATUS "libavif(AVIF_CODEC_SVT=LOCAL): ext/SVT-AV1 found; using as FetchContent SOURCE_DIR")
        set(FETCHCONTENT_SOURCE_DIR_SVT "${AVIF_SOURCE_DIR}/ext/SVT-AV1")
        message(CHECK_START "libavif(AVIF_CODEC_SVT=LOCAL): configuring SVT-AV1")
    else()
        message(CHECK_START "libavif(AVIF_CODEC_SVT=LOCAL): fetching and configuring SVT-AV1")
    endif()

    if(CMAKE_SYSTEM_PROCESSOR MATCHES "(x86_64|AMD64|amd64)")
        # NASM is only used on x86.
        if(NOT CMAKE_ASM_NASM_COMPILER)
            include(CheckLanguage)
            check_language(ASM_NASM)
            if(CMAKE_ASM_NASM_COMPILER)
                enable_language(ASM_NASM)
            endif()
        endif()
    elseif(NOT CMAKE_ASM_COMPILER)
        # ASM is only used on ARM.
        include(CheckLanguage)
        check_language(ASM)
        if(CMAKE_ASM_COMPILER)
            enable_language(ASM)
        endif()
    endif()

    FetchContent_Declare(
        svt
        EXCLUDE_FROM_ALL
        GIT_REPOSITORY "https://gitlab.com/AOMediaCodec/SVT-AV1.git"
        GIT_TAG "${AVIF_SVT_GIT_TAG}"
        UPDATE_COMMAND ""
        GIT_SHALLOW ON
    )

    set(BUILD_DEC OFF CACHE BOOL "")
    set(BUILD_APPS OFF CACHE BOOL "")
    set(NATIVE OFF CACHE BOOL "")

    set(CMAKE_BUILD_TYPE_ORIG ${CMAKE_BUILD_TYPE})
    set(CMAKE_BUILD_TYPE Release CACHE INTERNAL "")

    set(CMAKE_OUTPUT_DIRECTORY_ORIG "${CMAKE_OUTPUT_DIRECTORY}")
    set(CMAKE_OUTPUT_DIRECTORY "${svt_BINARY_DIR}" CACHE INTERNAL "")

    if(CMAKE_INTERPROCEDURAL_OPTIMIZATION)
        set(SVT_AV1_LTO ON)
    else()
        set(SVT_AV1_LTO OFF)
    endif()

    avif_fetchcontent_makeavailable_cmake(svt)

    set(CMAKE_BUILD_TYPE ${CMAKE_BUILD_TYPE_ORIG} CACHE STRING "" FORCE)
    set(CMAKE_OUTPUT_DIRECTORY ${CMAKE_OUTPUT_DIRECTORY_ORIG} CACHE STRING "" FORCE)

    set(SVT_INCLUDE_DIR ${svt_BINARY_DIR}/include)
    file(MAKE_DIRECTORY ${SVT_INCLUDE_DIR}/svt-av1)

    file(GLOB _svt_header_files ${svt_SOURCE_DIR}/Source/API/*.h)

    set(_svt_header_byproducts)

    foreach(_svt_header_file ${_svt_header_files})
        get_filename_component(_svt_header_name "${_svt_header_file}" NAME)
        set(_svt_header_output ${SVT_INCLUDE_DIR}/svt-av1/${_svt_header_name})
        add_custom_command(
            OUTPUT ${_svt_header_output}
            COMMAND ${CMAKE_COMMAND} -E copy_if_different ${_svt_header_file} ${_svt_header_output}
            DEPENDS ${_svt_header_file}
            VERBATIM
        )
        list(APPEND _svt_header_byproducts ${_svt_header_output})
    endforeach()

    add_custom_target(_svt_install_headers DEPENDS ${_svt_header_byproducts})
    add_dependencies(SvtAv1Enc _svt_install_headers)
    set_target_properties(SvtAv1Enc PROPERTIES AVIF_LOCAL ON)

    target_include_directories(SvtAv1Enc INTERFACE $<BUILD_INTERFACE:${SVT_INCLUDE_DIR}>)

    message(CHECK_PASS "complete")
endif()

if(EXISTS "${AVIF_SOURCE_DIR}/ext/SVT-AV1")
    set_target_properties(SvtAv1Enc PROPERTIES FOLDER "ext/SVT-AV1")
endif()
