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
ENCODED_FILE=/tmp/avif_test_cmd_encoded.avif
ENCODED_FILE_NO_METADATA=/tmp/avif_test_cmd_encoded_no_metadata.avif
ENCODED_FILE_WITH_DASH=-avif_test_cmd_encoded.avif
DECODED_FILE=/tmp/avif_test_cmd_decoded.png
PNG_FILE=/tmp/avif_test_cmd_kodim03.png

# Prepare some extra data.
set -x
echo "Generating a color PNG"
"${AVIFENC}" -s 10 "${TESTDATA_DIR}/kodim03_yuv420_8bpc.y4m" -o "${ENCODED_FILE}" > /dev/null
"${AVIFDEC}" "${ENCODED_FILE}" "${PNG_FILE}"  > /dev/null
set -x

# Basic calls.
"${AVIFENC}" --version
"${AVIFDEC}" --version

# Lossless test.
echo "Testing basic lossless"
"${AVIFENC}" -s 10 -l "${PNG_FILE}" -o "${ENCODED_FILE}"
"${AVIFDEC}" "${ENCODED_FILE}" "${DECODED_FILE}"
"${ARE_IMAGES_EQUAL}" "${PNG_FILE}" "${DECODED_FILE}" 0

# Metadata test.
echo "Testing metadata enc/dec"
"${AVIFENC}" "${TESTDATA_DIR}/paris_exif_xmp_icc.png" -o "${ENCODED_FILE}"
"${AVIFENC}" "${TESTDATA_DIR}/paris_exif_xmp_icc.png" -o "${ENCODED_FILE_NO_METADATA}" --ignore-exif
cmp "${ENCODED_FILE}" "${ENCODED_FILE_NO_METADATA}" && exit 1

# Argument parsing test with filenames starting with a dash.
"${AVIFENC}" -s 10 "${PNG_FILE}" -- "${ENCODED_FILE_WITH_DASH}"
"${AVIFDEC}" --info  -- "${ENCODED_FILE_WITH_DASH}"
# Passing a filename starting with a dash without using -- should fail.
set +e
"${AVIFENC}" -s 10 "${PNG_FILE}" "${ENCODED_FILE_WITH_DASH}"
if [[ $? -ne 1 ]]; then
  echo "Argument parsing should fail for avifenc"
  exit 1
fi
"${AVIFDEC}" --info "${ENCODED_FILE_WITH_DASH}"
if [[ $? -ne 1 ]]; then
  echo "Argument parsing should fail for avifdec"
  exit 1
fi
set -e
rm -- "${ENCODED_FILE_WITH_DASH}"

# Test code that should fail.
set +e
"${ARE_IMAGES_EQUAL}" "${TESTDATA_DIR}/kodim23_yuv420_8bpc.y4m" "${DECODED_FILE}" 0
if [[ $? -ne 1 ]]; then
  echo "Image should be different"
  exit 1
fi

echo "TEST OK"

# Cleanup
rm "${ENCODED_FILE}" "${ENCODED_FILE_NO_METADATA}" "${DECODED_FILE}" "${PNG_FILE}"

exit 0
