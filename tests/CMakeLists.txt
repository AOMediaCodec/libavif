# Copyright 2022 Joe Drago. All rights reserved.
# SPDX-License-Identifier: BSD-2-Clause

# With testing enabled, all targets referenced by add_test() can be run
# at once with CMake's ctest command line tool from the build folder.
enable_testing()

################################################################################
# C tests and tools

add_executable(aviftest aviftest.c)
if(AVIF_LOCAL_LIBGAV1)
    set_target_properties(aviftest PROPERTIES LINKER_LANGUAGE "CXX")
endif()
target_link_libraries(aviftest avif ${AVIF_PLATFORM_LIBRARIES})
add_test(NAME aviftest COMMAND aviftest ${CMAKE_CURRENT_SOURCE_DIR}/data)

if(AVIF_ENABLE_COVERAGE)
    add_custom_target(
        avif_coverage
        COMMAND ${CMAKE_COMMAND} -E env "LLVM_PROFILE_FILE=${CMAKE_CURRENT_BINARY_DIR}/aviftest.profraw" $<TARGET_FILE:aviftest>
                ${CMAKE_CURRENT_SOURCE_DIR}/data
        COMMAND ${XCRUN} llvm-profdata merge -sparse ${CMAKE_CURRENT_BINARY_DIR}/aviftest.profraw -o
                ${CMAKE_CURRENT_BINARY_DIR}/aviftest.profdata
        COMMAND cmake -E make_directory ${CMAKE_CURRENT_BINARY_DIR}/coverage
        COMMAND ${XCRUN} llvm-cov show $<TARGET_FILE:aviftest> -instr-profile=${CMAKE_CURRENT_BINARY_DIR}/aviftest.profdata
                -project-title=libavif --format html -output-dir=${CMAKE_CURRENT_BINARY_DIR}/coverage
        COMMAND echo Coverage report here: ${CMAKE_CURRENT_BINARY_DIR}/coverage/index.html
        DEPENDS aviftest
    )
endif()

add_executable(avifyuv avifyuv.c)
if(AVIF_LOCAL_LIBGAV1)
    set_target_properties(avifyuv PROPERTIES LINKER_LANGUAGE "CXX")
endif()
target_link_libraries(avifyuv avif ${AVIF_PLATFORM_LIBRARIES})
foreach(AVIFYUV_MODE limited rgb) # Modes drift and premultiply take more than 2 minutes each so they are disabled.
    add_test(NAME avifyuv_${AVIFYUV_MODE} COMMAND avifyuv -m ${AVIFYUV_MODE})
endforeach()

if(AVIF_ENABLE_GTEST OR AVIF_BUILD_APPS)
    enable_language(CXX)
    set(CMAKE_CXX_STANDARD 14)
    add_library(aviftest_helpers OBJECT gtest/aviftest_helpers.cc)
    target_link_libraries(aviftest_helpers avif_apps)
endif()

################################################################################
# GoogleTest

if(AVIF_ENABLE_GTEST)
    if(AVIF_LOCAL_GTEST)
        set(GTEST_INCLUDE_DIRS ${CMAKE_SOURCE_DIR}/ext/googletest/googletest/include)
        set(GTEST_LIBRARIES ${CMAKE_SOURCE_DIR}/ext/googletest/build/lib/${AVIF_LIBRARY_PREFIX}gtest${AVIF_LIBRARY_SUFFIX})
        set(GTEST_MAIN_LIBRARIES
            ${CMAKE_SOURCE_DIR}/ext/googletest/build/lib/${AVIF_LIBRARY_PREFIX}gtest_main${AVIF_LIBRARY_SUFFIX}
        )
        set(GTEST_BOTH_LIBRARIES ${GTEST_LIBRARIES} ${GTEST_MAIN_LIBRARIES})
        if(NOT EXISTS ${GTEST_INCLUDE_DIRS}/gtest/gtest.h)
            message(FATAL_ERROR "googletest(AVIF_LOCAL_GTEST): ${GTEST_INCLUDE_DIRS}/gtest/gtest.h is missing, bailing out")
        elseif(NOT EXISTS ${GTEST_LIBRARIES})
            message(FATAL_ERROR "googletest(AVIF_LOCAL_GTEST): ${GTEST_LIBRARIES} is missing, bailing out")
        elseif(NOT EXISTS ${GTEST_MAIN_LIBRARIES})
            message(FATAL_ERROR "googletest(AVIF_LOCAL_GTEST): ${GTEST_MAIN_LIBRARIES} is missing, bailing out")
        else()
            message(STATUS "Found local ext/googletest")
        endif()
    else()
        find_package(GTest REQUIRED)
    endif()

    add_executable(avifallocationtest gtest/avifallocationtest.cc)
    target_link_libraries(avifallocationtest aviftest_helpers ${GTEST_BOTH_LIBRARIES})
    target_include_directories(avifallocationtest PRIVATE ${GTEST_INCLUDE_DIRS})
    add_test(NAME avifallocationtest COMMAND avifallocationtest)

    add_executable(avifalphapremtest gtest/avifalphapremtest.cc)
    target_link_libraries(avifalphapremtest aviftest_helpers ${GTEST_BOTH_LIBRARIES})
    target_include_directories(avifalphapremtest PRIVATE ${GTEST_INCLUDE_DIRS})
    add_test(NAME avifalphapremtest COMMAND avifalphapremtest)

    add_executable(avifbasictest gtest/avifbasictest.cc)
    target_link_libraries(avifbasictest aviftest_helpers ${GTEST_BOTH_LIBRARIES})
    target_include_directories(avifbasictest PRIVATE ${GTEST_INCLUDE_DIRS})
    add_test(NAME avifbasictest COMMAND avifbasictest)

    add_executable(avifchangesettingtest gtest/avifchangesettingtest.cc)
    target_link_libraries(avifchangesettingtest aviftest_helpers ${GTEST_BOTH_LIBRARIES})
    target_include_directories(avifchangesettingtest PRIVATE ${GTEST_INCLUDE_DIRS})
    add_test(NAME avifchangesettingtest COMMAND avifchangesettingtest)

    add_executable(avifclaptest gtest/avifclaptest.cc)
    target_link_libraries(avifclaptest avif ${GTEST_BOTH_LIBRARIES})
    target_include_directories(avifclaptest PRIVATE ${GTEST_INCLUDE_DIRS})
    add_test(NAME avifclaptest COMMAND avifclaptest)

    add_executable(avifcllitest gtest/avifcllitest.cc)
    target_link_libraries(avifcllitest aviftest_helpers ${GTEST_BOTH_LIBRARIES})
    target_include_directories(avifcllitest PRIVATE ${GTEST_INCLUDE_DIRS})
    add_test(NAME avifcllitest COMMAND avifcllitest)

    add_executable(avifcodectest gtest/avifcodectest.cc)
    target_link_libraries(avifcodectest aviftest_helpers ${GTEST_BOTH_LIBRARIES})
    target_include_directories(avifcodectest PRIVATE ${GTEST_INCLUDE_DIRS})
    add_test(NAME avifcodectest COMMAND avifcodectest)

    add_executable(avifdecodetest gtest/avifdecodetest.cc)
    target_link_libraries(avifdecodetest aviftest_helpers ${GTEST_LIBRARIES})
    target_include_directories(avifdecodetest PRIVATE ${GTEST_INCLUDE_DIRS})
    add_test(NAME avifdecodetest COMMAND avifdecodetest ${CMAKE_CURRENT_SOURCE_DIR}/data/)

    add_executable(avifgridapitest gtest/avifgridapitest.cc)
    target_link_libraries(avifgridapitest avif_internal aviftest_helpers ${GTEST_BOTH_LIBRARIES})
    target_include_directories(avifgridapitest PRIVATE ${GTEST_INCLUDE_DIRS})
    add_test(NAME avifgridapitest COMMAND avifgridapitest)

    add_executable(avifimagetest gtest/avifimagetest.cc)
    target_link_libraries(avifimagetest aviftest_helpers ${GTEST_BOTH_LIBRARIES})
    target_include_directories(avifimagetest PRIVATE ${GTEST_INCLUDE_DIRS})
    add_test(NAME avifimagetest COMMAND avifimagetest)

    add_library(avifincrtest_helpers OBJECT gtest/avifincrtest_helpers.cc)
    target_link_libraries(avifincrtest_helpers avif ${AVIF_PLATFORM_LIBRARIES} ${GTEST_LIBRARIES})
    target_include_directories(avifincrtest_helpers PUBLIC ${GTEST_INCLUDE_DIRS})

    add_executable(avifincrtest gtest/avifincrtest.cc)
    target_link_libraries(avifincrtest aviftest_helpers avifincrtest_helpers)
    add_test(NAME avifincrtest COMMAND avifincrtest ${CMAKE_CURRENT_SOURCE_DIR}/data/)

    add_executable(avifiostatstest gtest/avifiostatstest.cc)
    target_link_libraries(avifiostatstest avif_internal aviftest_helpers ${GTEST_LIBRARIES})
    target_include_directories(avifiostatstest PRIVATE ${GTEST_INCLUDE_DIRS})
    add_test(NAME avifiostatstest COMMAND avifiostatstest ${CMAKE_CURRENT_SOURCE_DIR}/data/)

    add_executable(aviflosslesstest gtest/aviflosslesstest.cc)
    target_link_libraries(aviflosslesstest aviftest_helpers ${GTEST_BOTH_LIBRARIES})
    target_include_directories(aviflosslesstest PRIVATE ${GTEST_INCLUDE_DIRS})
    add_test(NAME aviflosslesstest COMMAND aviflosslesstest ${CMAKE_CURRENT_SOURCE_DIR}/data/)

    add_executable(avifmetadatatest gtest/avifmetadatatest.cc)
    target_link_libraries(avifmetadatatest aviftest_helpers ${GTEST_LIBRARIES})
    target_include_directories(avifmetadatatest PRIVATE ${GTEST_INCLUDE_DIRS})
    add_test(NAME avifmetadatatest COMMAND avifmetadatatest ${CMAKE_CURRENT_SOURCE_DIR}/data/)

    add_executable(avifopaquetest gtest/avifopaquetest.cc)
    target_link_libraries(avifopaquetest aviftest_helpers ${GTEST_BOTH_LIBRARIES})
    target_include_directories(avifopaquetest PRIVATE ${GTEST_INCLUDE_DIRS})
    add_test(NAME avifopaquetest COMMAND avifopaquetest)

    add_executable(avifpng16bittest gtest/avifpng16bittest.cc)
    target_link_libraries(avifpng16bittest aviftest_helpers ${GTEST_LIBRARIES})
    target_include_directories(avifpng16bittest PRIVATE ${GTEST_INCLUDE_DIRS})
    add_test(NAME avifpng16bittest COMMAND avifpng16bittest ${CMAKE_CURRENT_SOURCE_DIR}/data/)

    add_executable(avifprogressivetest gtest/avifprogressivetest.cc)
    target_link_libraries(avifprogressivetest aviftest_helpers ${GTEST_BOTH_LIBRARIES})
    target_include_directories(avifprogressivetest PRIVATE ${GTEST_INCLUDE_DIRS})
    add_test(NAME avifprogressivetest COMMAND avifprogressivetest)

    add_executable(avifreadimagetest gtest/avifreadimagetest.cc)
    target_link_libraries(avifreadimagetest aviftest_helpers ${GTEST_LIBRARIES})
    target_include_directories(avifreadimagetest PRIVATE ${GTEST_INCLUDE_DIRS})
    add_test(NAME avifreadimagetest COMMAND avifreadimagetest ${CMAKE_CURRENT_SOURCE_DIR}/data/)

    add_executable(avifrgbtoyuvtest gtest/avifrgbtoyuvtest.cc)
    target_link_libraries(avifrgbtoyuvtest aviftest_helpers ${GTEST_BOTH_LIBRARIES})
    target_include_directories(avifrgbtoyuvtest PRIVATE ${GTEST_INCLUDE_DIRS})
    add_test(NAME avifrgbtoyuvtest COMMAND avifrgbtoyuvtest)

    add_executable(avifscaletest gtest/avifscaletest.cc)
    target_link_libraries(avifscaletest avif_internal aviftest_helpers ${GTEST_LIBRARIES})
    target_include_directories(avifscaletest PRIVATE ${GTEST_INCLUDE_DIRS})
    add_test(NAME avifscaletest COMMAND avifscaletest ${CMAKE_CURRENT_SOURCE_DIR}/data/)

    add_executable(avifstreamtest gtest/avifstreamtest.cc)
    target_link_libraries(avifstreamtest avif_internal aviftest_helpers ${GTEST_BOTH_LIBRARIES})
    target_include_directories(avifstreamtest PRIVATE ${GTEST_INCLUDE_DIRS})
    add_test(NAME avifstreamtest COMMAND avifstreamtest ${CMAKE_CURRENT_SOURCE_DIR}/data/)

    add_executable(aviftilingtest gtest/aviftilingtest.cc)
    target_link_libraries(aviftilingtest avif_internal ${GTEST_BOTH_LIBRARIES})
    target_include_directories(aviftilingtest PRIVATE ${GTEST_INCLUDE_DIRS})
    add_test(NAME aviftilingtest COMMAND aviftilingtest)

    add_executable(avify4mtest gtest/avify4mtest.cc)
    target_link_libraries(avify4mtest aviftest_helpers ${GTEST_BOTH_LIBRARIES})
    target_include_directories(avify4mtest PRIVATE ${GTEST_INCLUDE_DIRS})
    add_test(NAME avify4mtest COMMAND avify4mtest)

    if(NOT AVIF_CODEC_AOM OR NOT AVIF_CODEC_AOM_ENCODE OR NOT AVIF_CODEC_AOM_DECODE)
        # These tests are supported with aom being the encoder and decoder. If aom is unavailable,
        # these tests are disabled because other codecs may not implement all the necessary features.
        # For example, SVT-AV1 requires 4:2:0 images with even dimensions of at least 64x64 px.
        set_tests_properties(
            avifallocationtest avifgridapitest avifincrtest aviflosslesstest avifmetadatatest PROPERTIES DISABLED True
        )

        message(STATUS "Some tests are disabled because aom is unavailable for encoding or decoding.")
    endif()

    if(NOT libsharpyuv_FOUND)
        message(STATUS "Some tests are skipped because libsharpyuv is unavailable.")
    endif()
else()
    message(STATUS "Most tests are disabled because AVIF_ENABLE_GTEST is OFF.")
endif()

################################################################################
# Bash tests

if(AVIF_BUILD_APPS)
    # When building apps, test the avifenc/avifdec.
    # 'are_images_equal' is used to make sure inputs/outputs are unchanged.
    add_executable(are_images_equal gtest/are_images_equal.cc)
    target_link_libraries(are_images_equal aviftest_helpers)
    add_test(NAME test_cmd COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/test_cmd.sh ${CMAKE_BINARY_DIR}
                                   ${CMAKE_CURRENT_SOURCE_DIR}/data
    )
    add_test(NAME test_cmd_animation COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/test_cmd_animation.sh ${CMAKE_BINARY_DIR}
                                             ${CMAKE_CURRENT_SOURCE_DIR}/data
    )
    add_test(NAME test_cmd_grid COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/test_cmd_grid.sh ${CMAKE_BINARY_DIR}
                                        ${CMAKE_CURRENT_SOURCE_DIR}/data
    )
    add_test(NAME test_cmd_icc_profile COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/test_cmd_icc_profile.sh ${CMAKE_BINARY_DIR}
                                               ${CMAKE_CURRENT_SOURCE_DIR}/data
    )
    add_test(NAME test_cmd_lossless COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/test_cmd_lossless.sh ${CMAKE_BINARY_DIR}
                                            ${CMAKE_CURRENT_SOURCE_DIR}/data
    )
    add_test(NAME test_cmd_metadata COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/test_cmd_metadata.sh ${CMAKE_BINARY_DIR}
                                            ${CMAKE_CURRENT_SOURCE_DIR}/data
    )
    add_test(NAME test_cmd_progressive COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/test_cmd_progressive.sh ${CMAKE_BINARY_DIR}
                                               ${CMAKE_CURRENT_SOURCE_DIR}/data
    )
    add_test(NAME test_cmd_targetsize COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/test_cmd_targetsize.sh ${CMAKE_BINARY_DIR}
                                              ${CMAKE_CURRENT_SOURCE_DIR}/data
    )

    if(NOT AVIF_CODEC_AOM OR NOT AVIF_CODEC_AOM_ENCODE)
        # Only aom encoder supports AV1 lossless encoding.
        set_property(TEST test_cmd_animation PROPERTY DISABLED True)
        set_property(TEST test_cmd_icc_profile PROPERTY DISABLED True)
        set_property(TEST test_cmd_lossless PROPERTY DISABLED True)

        # SVT-AV1 does not support the images with odd dimensions that are used in this test.
        if(NOT AVIF_CODEC_RAV1E)
            set_property(TEST test_cmd_metadata PROPERTY DISABLED True)
        endif()

        # Only aom encoder supports encoding AV1 spatial layers (used to implement
        # AVIF layered images that can be progressively decoded).
        set_property(TEST test_cmd_progressive PROPERTY DISABLED True)

        message(STATUS "Some tests are disabled because aom is unavailable for encoding.")
    endif()
endif()

################################################################################
# AV2 tests

if(AVIF_CODEC_AVM)
    if(AVIF_ENABLE_GTEST)
        add_executable(avifavmtest gtest/avifavmtest.cc)
        target_link_libraries(avifavmtest aviftest_helpers ${GTEST_BOTH_LIBRARIES})
        target_include_directories(avifavmtest PRIVATE ${GTEST_INCLUDE_DIRS})
        add_test(NAME avifavmtest COMMAND avifavmtest)
    endif()

    if(AVIF_BUILD_APPS)
        add_test(NAME test_cmd_avm COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/test_cmd_avm.sh ${CMAKE_BINARY_DIR}
                                           ${CMAKE_CURRENT_SOURCE_DIR}/data
        )
        add_test(NAME test_cmd_avm_lossless COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/test_cmd_avm_lossless.sh ${CMAKE_BINARY_DIR}
                                                    ${CMAKE_CURRENT_SOURCE_DIR}/data
        )
    endif()

    # AV2 support is experimental and only available when avm is explicitly specified as the encoder.
    # This may lead to test failures when there is no available AV1 codec.
    if(((NOT AVIF_CODEC_AOM OR NOT AVIF_CODEC_AOM_ENCODE) AND NOT AVIF_CODEC_RAV1E AND NOT AVIF_CODEC_SVT)
       OR ((NOT AVIF_CODEC_AOM OR NOT AVIF_CODEC_AOM_DECODE) AND NOT AVIF_CODEC_DAV1D AND NOT AVIF_CODEC_LIBGAV1)
    )
        # Disable all tests that use avifEncoder without explicitly setting the codec to avm.
        set_tests_properties(aviftest PROPERTIES DISABLED True)
        if(AVIF_ENABLE_GTEST)
            set_tests_properties(
                avifallocationtest avifbasictest avifchangesettingtest avifcllitest avifgridapitest avifincrtest avifiostatstest
                avifmetadatatest avifprogressivetest avify4mtest PROPERTIES DISABLED True
            )
        endif()

        if(AVIF_BUILD_APPS)
            # Disable all tests that use avifenc without explicitly setting --codec=avm.
            set_tests_properties(test_cmd test_cmd_animation test_cmd_grid test_cmd_targetsize PROPERTIES DISABLED True)
        endif()
    endif()
endif()
