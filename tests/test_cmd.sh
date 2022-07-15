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

set -ex

if [[ "$#" -ge 1 ]]; then
  # eval so that the passed in directory can contain variables.
  BINARY_DIR="$(eval echo "$1")"
else
  # assume "tests" is the current directory
  BINARY_DIR=..
fi
if [[ "$#" -ge 2 ]]; then
  TESTDATA_DIR="$(eval echo "$2")"
else
  TESTDATA_DIR=./data
fi

AVIFENC="${BINARY_DIR}/avifenc"
AVIFDEC="${BINARY_DIR}/avifdec"
ARE_IMAGES_EQUAL="${BINARY_DIR}/tests/are_images_equal"
TMP_ENCODED_FILE=/tmp/encoded.avif
DECODED_FILE=/tmp/decoded.png
PNG_FILE=/tmp/kodim03.png

# Prepare some extra data.
set +x
echo "Generating a color PNG"
"${AVIFENC}" -s 10 "${TESTDATA_DIR}"/kodim03_yuv420_8bpc.y4m -o "${TMP_ENCODED_FILE}" &> /dev/null
"${AVIFDEC}" "${TMP_ENCODED_FILE}" "${PNG_FILE}"  &> /dev/null
set -x

# Basic calls.
"${AVIFENC}" --version
"${AVIFDEC}" --version

# Lossless test.
echo "Testing basic lossless"
"${AVIFENC}" -s 10 -l "${PNG_FILE}" -o "${TMP_ENCODED_FILE}"
"${AVIFDEC}" "${TMP_ENCODED_FILE}" "${DECODED_FILE}"
"${ARE_IMAGES_EQUAL}" "${PNG_FILE}" "${DECODED_FILE}" 0

# Test code that should fail.
set +e
"${ARE_IMAGES_EQUAL}" "${TESTDATA_DIR}"/kodim23_yuv420_8bpc.y4m "${DECODED_FILE}" 0
if [[ $? -ne 1 ]]; then
  echo "Image should be different"
  exit 1
fi

echo "TEST OK"
exit 0
