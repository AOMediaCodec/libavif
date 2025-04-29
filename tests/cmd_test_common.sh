#!/bin/bash
# Copyright 2025 Google LLC
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
# ------------------------------------------------------------------------------
#
# Common code for command line tests. Should be source'd at the beginning of
# the test: source $(dirname "$0")/cmd_test_common.sh

# Very verbose but useful for debugging.
set -eux

# The CMake config must always be passed. It can be empty if there is
# no multi-config generator.
CONFIG="$1"
if [[ "$#" -ge 2 ]]; then
  # eval so that the passed in directory can contain variables.
  BINARY_DIR="$(eval echo "$2")"
else
  # Assume "tests" is the current directory.
  BINARY_DIR="$(pwd)/.."
fi
if [[ "$#" -ge 3 ]]; then
  TESTDATA_DIR="$(eval echo "$3")"
else
  TESTDATA_DIR="$(pwd)/data"
fi
if [[ "$#" -ge 4 ]]; then
  TMP_DIR="$(eval echo "$4")"
else
  TMP_DIR="$(mktemp -d)"
fi

if [[ ! -z "$CONFIG" ]]; then
  AVIFENC="${BINARY_DIR}/${CONFIG}/avifenc"
  AVIFDEC="${BINARY_DIR}/${CONFIG}/avifdec"
  ARE_IMAGES_EQUAL="${BINARY_DIR}/tests/${CONFIG}/are_images_equal"
else
  AVIFENC="${BINARY_DIR}/avifenc"
  AVIFDEC="${BINARY_DIR}/avifdec"
  ARE_IMAGES_EQUAL="${BINARY_DIR}/tests/are_images_equal"
fi
