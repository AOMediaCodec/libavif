# Copyright 2021 Google LLC.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#      https://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

cmake_minimum_required(VERSION 3.5)
option(AVIF_BUILD_PARSER "Build a lightweight AVIF parser" OFF)

# Library

if(AVIF_BUILD_PARSER)
  add_library(avif_parser avif_parser.c)
endif()

# Tests

if(AVIF_BUILD_PARSER)
  find_package(Threads)  # Needed by GoogleTest
  find_package(GTest)
  if(GTEST_FOUND)
    enable_testing()
    add_executable(avif_parser_test avif_parser_test.cc)
    target_include_directories(avif_parser_test PRIVATE ${GTEST_INCLUDE_DIRS})
    target_link_libraries(avif_parser_test PRIVATE ${GTEST_BOTH_LIBRARIES} avif_parser)
    add_test(
      NAME avif_parser_test
      COMMAND ${CMAKE_CURRENT_BINARY_DIR}/avif_parser_test
      WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
  else()
    message(WARNING "Did not find GoogleTest. Tests are disabled.")
  endif()
endif()
