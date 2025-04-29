# Copyright 2022 Joe Drago. All rights reserved.
# SPDX-License-Identifier: BSD-2-Clause

# With testing enabled, all targets referenced by add_test() can be run
# at once with CMake's ctest command line tool from the build folder.
enable_testing()

################################################################################
# C tests and tools

set(COVERAGE_TARGETS)

configure_file(${AVIF_SOURCE_DIR}/tests/CTestCustom.cmake ${CMAKE_BINARY_DIR})

# Macro to register a test for coverage. The first argument is the target name.
# Other arguments, like data path, can be added.
macro(register_test_for_coverage TEST_NAME)
    if(AVIF_ENABLE_COVERAGE)
        add_custom_target(
            ${TEST_NAME}_coverage
            COMMAND ${CMAKE_COMMAND} -E env "LLVM_PROFILE_FILE=${CMAKE_CURRENT_BINARY_DIR}/${TEST_NAME}.profraw"
                    $<TARGET_FILE:${TEST_NAME}> ${ARGN}
        )
        list(APPEND COVERAGE_TARGETS ${TEST_NAME})
    endif()
endmacro()

add_executable(aviftest aviftest.c)
if(AVIF_CODEC_LIBGAV1_ENABLED OR AVIF_LIBYUV_ENABLED)
    set_target_properties(aviftest PROPERTIES LINKER_LANGUAGE "CXX")
endif()
target_link_libraries(aviftest avif avif_enable_warnings)
add_test(NAME aviftest COMMAND aviftest ${CMAKE_CURRENT_SOURCE_DIR}/data)
register_test_for_coverage(aviftest ${CMAKE_CURRENT_SOURCE_DIR}/data/)

add_executable(avifyuv avifyuv.c)
if(AVIF_CODEC_LIBGAV1_ENABLED OR AVIF_LIBYUV_ENABLED)
    set_target_properties(avifyuv PROPERTIES LINKER_LANGUAGE "CXX")
endif()

target_link_libraries(avifyuv avif avif_enable_warnings)
foreach(AVIFYUV_MODE limited rgb) # Modes drift and premultiply take more than 2 minutes each so they are disabled.
    add_test(NAME avifyuv_${AVIFYUV_MODE} COMMAND avifyuv -m ${AVIFYUV_MODE})
endforeach()

if(AVIF_FUZZTEST OR AVIF_GTEST OR AVIF_BUILD_APPS)
    add_library(aviftest_helpers OBJECT gtest/aviftest_helpers.cc)
    target_link_libraries(aviftest_helpers PUBLIC avif_apps_internal avif_internal)
    target_link_libraries(aviftest_helpers PRIVATE avif_enable_warnings)
endif()

################################################################################
# GoogleTest

# Adds a gtest from file TEST_NAME.cc located in the gtest folder. Extra arguments
# are considered as extra linked libraries.
macro(add_avif_gtest TEST_NAME)
    add_executable(${TEST_NAME} gtest/${TEST_NAME}.cc)
    target_link_libraries(${TEST_NAME} PRIVATE aviftest_helpers GTest::GTest GTest::Main ${ARGN} avif_enable_warnings)
    add_test(NAME ${TEST_NAME} COMMAND ${TEST_NAME})
    register_test_for_coverage(${TEST_NAME})
endmacro()
macro(add_avif_gtest_with_data TEST_NAME)
    add_executable(${TEST_NAME} gtest/${TEST_NAME}.cc)
    target_link_libraries(${TEST_NAME} PRIVATE aviftest_helpers GTest::GTest ${ARGN} avif_enable_warnings)
    add_test(NAME ${TEST_NAME} COMMAND ${TEST_NAME} ${CMAKE_CURRENT_SOURCE_DIR}/data/)
    register_test_for_coverage(${TEST_NAME} ${CMAKE_CURRENT_SOURCE_DIR}/data/)
endmacro()

if(AVIF_GTEST)
    check_avif_option(AVIF_GTEST TARGET GTest::GTest PKG_NAME GTest)
    add_library(avifincrtest_helpers OBJECT gtest/avifincrtest_helpers.cc)
    target_link_libraries(avifincrtest_helpers PUBLIC avif_internal)
    target_link_libraries(avifincrtest_helpers PRIVATE GTest::GTest avif_enable_warnings)
endif()

if(AVIF_GTEST)
    if(AVIF_ENABLE_EXPERIMENTAL_SAMPLE_TRANSFORM)
        add_avif_gtest_with_data(avif16bittest)
        add_avif_gtest(avifsampletransformtest)
    endif()

    add_avif_gtest(avifallocationtest)
    add_avif_gtest_with_data(avifalphanoispetest)
    add_avif_gtest(avifalphapremtest)
    add_avif_gtest_with_data(avifanimationtest)
    add_avif_gtest(avifbasictest)
    add_avif_gtest(avifchangesettingtest)
    add_avif_gtest(avifcicptest)
    add_avif_gtest(avifclaptest)
    add_avif_gtest(avifcllitest)
    add_avif_gtest(avifcodectest)
    add_avif_gtest_with_data(avifcolrconverttest)
    add_avif_gtest(avifcolrtest)
    add_avif_gtest_with_data(avifdecodetest)
    add_avif_gtest_with_data(avifdimgtest avifincrtest_helpers)
    add_avif_gtest_with_data(avifencodetest)
    add_avif_gtest_with_data(avifgainmaptest avifincrtest_helpers)

    if(AVIF_ENABLE_JPEG_GAIN_MAP_CONVERSION)
        add_avif_gtest_with_data(avifjpeggainmaptest)
    endif()

    add_avif_gtest(avifgridapitest)
    add_avif_gtest(avifheadertest)
    add_avif_gtest_with_data(avifilocextenttest)
    add_avif_gtest(avifimagetest)
    add_avif_gtest_with_data(avifincrtest avifincrtest_helpers)
    add_avif_gtest_with_data(avifiostatstest)
    add_avif_gtest_with_data(avifkeyframetest)
    add_avif_gtest_with_data(aviflosslesstest)
    add_avif_gtest_with_data(avifmetadatatest)

    if(AVIF_ENABLE_EXPERIMENTAL_MINI)
        add_avif_gtest(avifminitest)
    endif()

    add_avif_gtest(avifopaquetest)

    add_avif_gtest_with_data(avifpixitest)
    if(AVIF_ENABLE_EXPERIMENTAL_EXTENDED_PIXI)
        target_compile_definitions(avifpixitest PRIVATE AVIF_ENABLE_EXPERIMENTAL_EXTENDED_PIXI)
    endif()

    add_avif_gtest_with_data(avifpng16bittest)
    add_avif_gtest_with_data(avifprogressivetest)
    add_avif_gtest_with_data(avifpropertytest)
    add_avif_gtest(avifpropinternaltest)
    add_avif_gtest(avifrangetest)
    add_avif_gtest_with_data(avifreadimagetest)
    add_avif_gtest(avifrgbtest)
    add_avif_gtest(avifrgbtoyuvtest)
    add_avif_gtest(avifrgbtoyuvthreadingtest)
    add_avif_gtest_with_data(avifscaletest)
    add_avif_gtest_with_data(avifsize0test)
    add_avif_gtest(avifstreamtest)

    if(AVIF_CODEC_SVT_ENABLED)
        add_avif_gtest(avifsvttest)
    endif()

    add_avif_gtest(aviftilingtest)
    add_avif_gtest_with_data(aviftransformtest)
    add_avif_gtest(avifutilstest)
    add_avif_gtest(avify4mtest)

    if(NOT AVIF_CODEC_AOM OR NOT AVIF_CODEC_AOM_ENCODE)
        # These tests are supported with aom being the encoder. If the aom encoder is unavailable,
        # these tests are disabled because other codecs may not implement all the necessary features.
        # For example, SVT-AV1 requires 4:2:0 images with even dimensions of at least 64x64 px.
        set_tests_properties(
            avifallocationtest avifgridapitest avifincrtest aviflosslesstest avifmetadatatest avifpixitest PROPERTIES DISABLED
                                                                                                                      True
        )

        message(STATUS "Some tests are disabled because aom is unavailable for encoding.")
    endif()

    if(NOT AVIF_LIBSHARPYUV_ENABLED)
        message(STATUS "Some tests are skipped because libsharpyuv is unavailable.")
    endif()
else()
    message(STATUS "Most tests are disabled because AVIF_GTEST is OFF.")
endif()

################################################################################
# Experimental FuzzTest support (Linux only)

if(AVIF_FUZZTEST)
    # Adds a fuzztest from file TEST_NAME.cc located in the gtest folder. Extra arguments
    # are considered as extra source files.
    macro(add_avif_fuzztest TEST_NAME)
        add_executable(${TEST_NAME} gtest/${TEST_NAME}.cc ${ARGN})
        # FuzzTest bundles GoogleTest so no need to link to gtest librairies.
        # avif_enable_warnings is not added because it triggers too many warnings in fuzztest.
        target_link_libraries(${TEST_NAME} PRIVATE avif_fuzztest_helpers aviftest_helpers)
        link_fuzztest(${TEST_NAME})
        add_test(NAME ${TEST_NAME} COMMAND ${TEST_NAME} --stack_limit_kb=512)
        set_property(TEST ${TEST_NAME} PROPERTY ENVIRONMENT "TEST_DATA_DIRS=${CMAKE_CURRENT_SOURCE_DIR}/data/")
    endmacro()

    # Recommended top-level CMake options:
    #   -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ -DAVIF_CODEC_DAV1D=ON -DAVIF_ENABLE_WERROR=OFF
    # Reproducing a failure can be done by setting the environment variable
    #   FUZZTEST_REPLAY=/path/to/repro_file.test
    # and running one of the targets below.
    # See https://github.com/google/fuzztest/blob/main/doc/quickstart-cmake.md
    # Note: There are compiler warnings in the FuzzTest headers. Add the
    # ext/fuzztest subdirectory with the SYSTEM directory property set to
    # true so that warnings in its headers are suppressed.
    if(CMAKE_VERSION VERSION_LESS 3.25.0)
        message(FATAL_ERROR "CMake must be at least 3.25 to pass the SYSTEM argument to add_subdirectory(), bailing out")
    endif()
    if(AVIF_FUZZTEST STREQUAL "SYSTEM")
        message(FATAL_ERROR "SYSTEM is not supported for AVIF_FUZZTEST")
    endif()
    # Add the fuzztest project. Note this may add some tests which may not be built because of EXCLUDE_FROM_ALL and will
    # therefore fail. They can be ignored by adding them to CTestCustom.cmake
    # See https://gitlab.kitware.com/cmake/cmake/-/issues/20212
    include(LocalFuzztest)

    fuzztest_setup_fuzzing_flags()

    # Create a library with avif_fuzztest_helpers.cc to compile it only once.
    add_library(avif_fuzztest_helpers OBJECT gtest/avif_fuzztest_helpers.cc)
    target_link_libraries(avif_fuzztest_helpers PUBLIC aviftest_helpers)
    link_fuzztest(avif_fuzztest_helpers)

    add_avif_fuzztest(avif_fuzztest_dec)
    add_avif_fuzztest(avif_fuzztest_dec_incr gtest/avifincrtest_helpers.cc)
    add_avif_fuzztest(avif_fuzztest_enc_dec)
    add_avif_fuzztest(avif_fuzztest_enc_dec_anim)
    add_avif_fuzztest(avif_fuzztest_enc_dec_incr gtest/avifincrtest_helpers.cc)
    add_avif_fuzztest(avif_fuzztest_properties)
    add_avif_fuzztest(avif_fuzztest_read_image)
    add_avif_fuzztest(avif_fuzztest_yuvrgb)
else()
    message(STATUS "FuzzTest targets are disabled because AVIF_FUZZTEST is OFF.")
endif()

################################################################################
# Bash tests

# Macro to add a shell test. It takes multi-config generators into account.
# The first argument is the name of the shell script, without .sh.
# The following arguments are sent to the script.
macro(add_cmd_test SHELL_SCRIPT)
    get_property(IS_MULTI_CONFIG GLOBAL PROPERTY GENERATOR_IS_MULTI_CONFIG)
    if(${IS_MULTI_CONFIG})
        add_test(NAME ${SHELL_SCRIPT} COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/${SHELL_SCRIPT}.sh $<CONFIG> ${CMAKE_BINARY_DIR}
                                              ${ARGN}
        )
    else()
        add_test(NAME ${SHELL_SCRIPT} COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/${SHELL_SCRIPT}.sh "" ${CMAKE_BINARY_DIR} ${ARGN})
    endif()
endmacro()

if(AVIF_BUILD_APPS)
    # When building apps, test the avifenc/avifdec.
    # 'are_images_equal' is used to make sure inputs/outputs are unchanged.
    add_executable(are_images_equal gtest/are_images_equal.cc)
    if(WIN32)
        if(MSVC)
            target_sources(are_images_equal PRIVATE ${CMAKE_SOURCE_DIR}/apps/utf8.manifest)
        elseif(MINGW)
            target_sources(are_images_equal PRIVATE ${CMAKE_SOURCE_DIR}/apps/utf8.rc)
        endif()
    endif()
    target_link_libraries(are_images_equal aviftest_helpers avif_enable_warnings)
    add_cmd_test(test_cmd ${CMAKE_CURRENT_SOURCE_DIR}/data)
    add_cmd_test(test_cmd_animation ${CMAKE_CURRENT_SOURCE_DIR}/data)
    add_cmd_test(test_cmd_grid ${CMAKE_CURRENT_SOURCE_DIR}/data)
    add_cmd_test(test_cmd_icc_profile ${CMAKE_CURRENT_SOURCE_DIR}/data)
    add_cmd_test(test_cmd_lossless ${CMAKE_CURRENT_SOURCE_DIR}/data)
    add_cmd_test(test_cmd_metadata ${CMAKE_CURRENT_SOURCE_DIR}/data)
    add_cmd_test(test_cmd_progressive ${CMAKE_CURRENT_SOURCE_DIR}/data)
    add_cmd_test(test_cmd_stdin ${CMAKE_CURRENT_SOURCE_DIR}/data)
    add_cmd_test(test_cmd_targetsize ${CMAKE_CURRENT_SOURCE_DIR}/data)

    if(AVIF_ENABLE_JPEG_GAIN_MAP_CONVERSION)
        add_cmd_test(test_cmd_avifgainmaputil ${CMAKE_CURRENT_SOURCE_DIR}/data)
    endif()

    if(AVIF_ENABLE_GOLDEN_TESTS AND AVIF_CODEC_AOM_ENCODE)
        # test_cmd_enc_boxes_golden.sh depends on MP4Box
        # Only allow a locally built version to avoid differences with versioning.
        set(MP4BOX_DIR ${AVIF_SOURCE_DIR}/ext/gpac/bin/gcc/)
        if(NOT EXISTS ${MP4BOX_DIR}/MP4Box)
            message(FATAL_ERROR "AVIF_ENABLE_GOLDEN_TESTS is ON but ${MP4BOX_DIR}/MP4Box is missing. Run ext/mp4box.sh")
        endif()

        set(GOLDEN_TESTS_OUTPUT_DIR "" CACHE STRING "Output path for golden tests (will be a temp dir if empty)")

        add_cmd_test(test_cmd_enc_boxes_golden ${MP4BOX_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/data ${GOLDEN_TESTS_OUTPUT_DIR})

        if(AVIF_ENABLE_JPEG_GAIN_MAP_CONVERSION)
            add_cmd_test(test_cmd_gainmap ${CMAKE_CURRENT_SOURCE_DIR}/data)

            add_cmd_test(
                test_cmd_enc_gainmap_boxes_golden ${MP4BOX_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/data ${GOLDEN_TESTS_OUTPUT_DIR}
            )
        endif()
    endif()

    if(NOT AVIF_CODEC_AOM_ENABLED OR NOT AVIF_CODEC_AOM_ENCODE)
        # Only aom encoder supports AV1 lossless encoding.
        set_property(TEST test_cmd_animation PROPERTY DISABLED True)
        set_property(TEST test_cmd_icc_profile PROPERTY DISABLED True)
        set_property(TEST test_cmd_lossless PROPERTY DISABLED True)

        # SVT-AV1 does not support the images with odd dimensions that are used in this test.
        if(NOT AVIF_CODEC_RAV1E_ENABLED)
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

if(AVIF_CODEC_AVM_ENABLED)
    if(AVIF_GTEST)
        add_avif_gtest(avifavmtest)
        if(AVIF_ENABLE_EXPERIMENTAL_MINI)
            add_avif_gtest(avifavmminitest)
        endif()
    endif()

    if(AVIF_BUILD_APPS)
        add_cmd_test(test_cmd_avm ${CMAKE_CURRENT_SOURCE_DIR}/data)
        add_cmd_test(test_cmd_avm_lossless ${CMAKE_CURRENT_SOURCE_DIR}/data)
    endif()

    # AV2 support is experimental and only available when avm is explicitly specified as the encoder.
    # This may lead to test failures when there is no available AV1 codec.
    if(((NOT AVIF_CODEC_AOM_ENABLED OR NOT AVIF_CODEC_AOM_ENCODE) AND NOT AVIF_CODEC_RAV1E_ENABLED AND NOT AVIF_CODEC_SVT_ENABLED)
       OR ((NOT AVIF_CODEC_AOM_ENABLED OR NOT AVIF_CODEC_AOM_DECODE) AND NOT AVIF_CODEC_DAV1D_ENABLED
           AND NOT AVIF_CODEC_LIBGAV1_ENABLED)
    )
        # Disable all tests that use avifEncoder without explicitly setting the codec to avm.
        set_tests_properties(aviftest PROPERTIES DISABLED True)
        if(AVIF_GTEST)
            set_tests_properties(
                avifallocationtest
                avifbasictest
                avifchangesettingtest
                avifcllitest
                avifcolrconverttest
                avifdimgtest
                avifencodetest
                avifgridapitest
                avifheadertest
                avifincrtest
                avifiostatstest
                avifmetadatatest
                avifpixitest
                avifprogressivetest
                avifpropertytest
                avifrangetest
                avify4mtest
                PROPERTIES DISABLED True
            )

            if(AVIF_ENABLE_EXPERIMENTAL_MINI)
                set_tests_properties(avifminitest PROPERTIES DISABLED True)
            endif()
            set_tests_properties(avifgainmaptest PROPERTIES DISABLED True)
            if(AVIF_ENABLE_JPEG_GAIN_MAP_CONVERSION)
                set_tests_properties(avifjpeggainmaptest PROPERTIES DISABLED True)
            endif()
            if(AVIF_ENABLE_EXPERIMENTAL_SAMPLE_TRANSFORM)
                set_tests_properties(avif16bittest PROPERTIES DISABLED True)
            endif()
        endif()

        if(AVIF_BUILD_APPS)
            # Disable all tests that use avifenc without explicitly setting --codec=avm.
            set_tests_properties(
                test_cmd test_cmd_animation test_cmd_grid test_cmd_stdin test_cmd_targetsize PROPERTIES DISABLED True
            )
        endif()
    endif()
endif()

if(AVIF_ENABLE_COVERAGE)
    set(MERGE_COMMAND llvm-profdata merge)
    set(SHOW_COMMAND llvm-cov show --ignore-filename-regex=.*/tests/.* --ignore-filename-regex=.*/third_party/.*)
    foreach(TARGET ${COVERAGE_TARGETS})
        list(APPEND MERGE_COMMAND -sparse ${CMAKE_CURRENT_BINARY_DIR}/${TARGET}.profraw)
        list(APPEND SHOW_COMMAND -object $<TARGET_FILE:${TARGET}>)
    endforeach()
    list(APPEND MERGE_COMMAND -o ${CMAKE_CURRENT_BINARY_DIR}/avif_coverage.profdata)
    list(APPEND SHOW_COMMAND -instr-profile=${CMAKE_CURRENT_BINARY_DIR}/avif_coverage.profdata -project-title=libavif --format
         html -output-dir=${CMAKE_CURRENT_BINARY_DIR}/coverage
    )
    add_custom_target(
        avif_coverage
        COMMAND ${XCRUN} ${MERGE_COMMAND}
        COMMAND cmake -E make_directory ${CMAKE_CURRENT_BINARY_DIR}/coverage
        COMMAND ${XCRUN} ${SHOW_COMMAND}
        COMMAND echo Coverage report here: ${CMAKE_CURRENT_BINARY_DIR}/coverage/index.html
    )
    foreach(TARGET ${COVERAGE_TARGETS})
        add_dependencies(avif_coverage ${TARGET}_coverage)
    endforeach()
endif()
