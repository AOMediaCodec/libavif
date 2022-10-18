#!/bin/bash
# Copyright 2022 Google LLC
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
# tests for command lines

# Very verbose but useful for debugging.
set -ex

if [[ "$#" -ge 1 ]]; then
  # eval so that the passed in directory can contain variables.
  BINARY_DIR="$(eval echo "$1")"
else
  # Assume "tests" is the current directory.
  BINARY_DIR="$(pwd)/.."
fi
if [[ "$#" -ge 2 ]]; then
  TESTDATA_DIR="$(eval echo "$2")"
else
  TESTDATA_DIR="$(pwd)/data"
fi
if [[ "$#" -ge 3 ]]; then
  TMP_DIR="$(eval echo "$3")"
else
  TMP_DIR="$(mktemp -d)"
fi

AVIFENC="${BINARY_DIR}/avifenc"
AVIFDEC="${BINARY_DIR}/avifdec"
ARE_IMAGES_EQUAL="${BINARY_DIR}/tests/are_images_equal"

# Basic calls.
"${AVIFENC}" --version
"${AVIFDEC}" --version

# Input file paths.
INPUT_Y4M="${TESTDATA_DIR}/kodim03_yuv420_8bpc.y4m"
# Output file names.
ENCODED_FILE="avif_test_cmd_encoded.avif"
ENCODED_FILE_WITH_DASH="-avif_test_cmd_encoded.avif"
DECODED_FILE="avif_test_cmd_decoded.png"

# Cleanup
cleanup() {
  pushd ${TMP_DIR}
    rm -- "${ENCODED_FILE}" "${ENCODED_FILE_WITH_DASH}" "${DECODED_FILE}"
  popd
}
trap cleanup EXIT

pushd ${TMP_DIR}
  # Lossy test. The decoded pixels should be different from the original image.
  echo "Testing basic lossy"
  "${AVIFENC}" -s 8 "${INPUT_Y4M}" -o "${ENCODED_FILE}"
  "${AVIFDEC}" "${ENCODED_FILE}" "${DECODED_FILE}"
  "${ARE_IMAGES_EQUAL}" "${INPUT_Y4M}" "${DECODED_FILE}" 0 && exit 1

  # Argument parsing test with filenames starting with a dash.
  echo "Testing arguments"
  "${AVIFENC}" -s 10 "${INPUT_Y4M}" -- "${ENCODED_FILE_WITH_DASH}"
  "${AVIFDEC}" --info  -- "${ENCODED_FILE_WITH_DASH}"
  # Passing a filename starting with a dash without using -- should fail.
  "${AVIFENC}" -s 10 "${INPUT_Y4M}" "${ENCODED_FILE_WITH_DASH}" && exit 1
  "${AVIFDEC}" --info "${ENCODED_FILE_WITH_DASH}" && exit 1
popd

exit 0
