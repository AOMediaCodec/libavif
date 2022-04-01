# Copyright 2022 Joe Drago. All rights reserved.
# SPDX-License-Identifier: BSD-2-Clause

# With testing enabled, all targets referenced by add_test() can be run
# at once with CMake's ctest command line tool from the build folder.
enable_testing()
# Using a CMake FIXTURES_SETUP/FIXTURES_CLEANUP to create and delete a
# subdirectory would be cleaner but this is way simpler.
set(AVIF_TEST_TMP_DIR ${PROJECT_BINARY_DIR})

add_executable(aviftest aviftest.c)
if(AVIF_LOCAL_LIBGAV1)
    set_target_properties(aviftest PROPERTIES LINKER_LANGUAGE "CXX")
endif()
target_link_libraries(aviftest avif ${AVIF_PLATFORM_LIBRARIES})

add_executable(avifgridapitest avifgridapitest.c)
if(AVIF_LOCAL_LIBGAV1)
    set_target_properties(avifgridapitest PROPERTIES LINKER_LANGUAGE "CXX")
endif()
target_link_libraries(avifgridapitest avif ${AVIF_PLATFORM_LIBRARIES})
add_test(NAME avifgridapitest COMMAND avifgridapitest)

add_executable(avifincrtest avifincrtest.c)
if(AVIF_LOCAL_LIBGAV1)
    set_target_properties(avifincrtest PROPERTIES LINKER_LANGUAGE "CXX")
endif()
target_link_libraries(avifincrtest avif ${AVIF_PLATFORM_LIBRARIES})
add_test(NAME avifincrtest COMMAND avifincrtest ${CMAKE_CURRENT_SOURCE_DIR}/data/sofa_grid1x5_420.avif)

add_executable(avify4mtest avify4mtest.c)
if(AVIF_LOCAL_LIBGAV1)
    set_target_properties(avify4mtest PROPERTIES LINKER_LANGUAGE "CXX")
endif()
target_link_libraries(avify4mtest avif avif_apps ${AVIF_PLATFORM_LIBRARIES})
add_test(NAME avify4mtest COMMAND avify4mtest AVIF_TEST_TMP_DIR)
set_tests_properties(avify4mtest PROPERTIES ENVIRONMENT "AVIF_TEST_TMP_DIR=${AVIF_TEST_TMP_DIR}")

add_executable(avifyuv avifyuv.c)
if(AVIF_LOCAL_LIBGAV1)
    set_target_properties(avifyuv PROPERTIES LINKER_LANGUAGE "CXX")
endif()
target_link_libraries(avifyuv avif ${AVIF_PLATFORM_LIBRARIES})

add_custom_target(avif_test_all
    COMMAND $<TARGET_FILE:aviftest> ${CMAKE_CURRENT_SOURCE_DIR}/data
    DEPENDS aviftest
)

if(AVIF_ENABLE_COVERAGE)
    add_custom_target(avif_coverage
        COMMAND ${CMAKE_COMMAND} -E env "LLVM_PROFILE_FILE=${CMAKE_CURRENT_BINARY_DIR}/aviftest.profraw" $<TARGET_FILE:aviftest> ${CMAKE_CURRENT_SOURCE_DIR}/data
        COMMAND ${XCRUN} llvm-profdata merge -sparse ${CMAKE_CURRENT_BINARY_DIR}/aviftest.profraw -o ${CMAKE_CURRENT_BINARY_DIR}/aviftest.profdata
        COMMAND cmake -E make_directory ${CMAKE_CURRENT_BINARY_DIR}/coverage
        COMMAND ${XCRUN} llvm-cov show $<TARGET_FILE:aviftest> -instr-profile=${CMAKE_CURRENT_BINARY_DIR}/aviftest.profdata -project-title=libavif --format html -output-dir=${CMAKE_CURRENT_BINARY_DIR}/coverage
        COMMAND echo Coverage report here: ${CMAKE_CURRENT_BINARY_DIR}/coverage/index.html
        DEPENDS aviftest
    )
endif()
