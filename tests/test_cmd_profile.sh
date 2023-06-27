#!/bin/bash
# Copyright 2023 Google LLC
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
# tests for command lines (lossless)

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

# Input file paths.
INPUT_PNG="${TESTDATA_DIR}/ArcTriomphe-cHRM-red-green-swap.png"
REFERENCE_PNG="${TESTDATA_DIR}/ArcTriomphe-cHRM-red-green-swap-reference.png"
# Output file names.
ENCODED_FILE="avif_test_cmd_profile_encoded.avif"
DECODED_FILE="avif_test_cmd_profile_decoded_sRGB.png"

# Cleanup
cleanup() {
  pushd ${TMP_DIR}
    rm -- "${ENCODED_FILE}" "${DECODED_FILE}"
  popd
}
trap cleanup EXIT

pushd ${TMP_DIR}
  "${AVIFENC}" -s 8 -l "${INPUT_PNG}" -o "${ENCODED_FILE}"

  # We use third-party tool (libvips) to independently check the validity
  # of our generated ICC profile
  if ! command -v vips &> /dev/null
  then
    touch "${DECODED_FILE}"
    exit 0
  fi

  vips icc_transform "${ENCODED_FILE}" "${DECODED_FILE}" sRGB

  # PSNR test. Different CMMs resulted in slightly different outputs.
  "${ARE_IMAGES_EQUAL}" "${REFERENCE_PNG}" "${DECODED_FILE}" 0 50
popd

exit 0
