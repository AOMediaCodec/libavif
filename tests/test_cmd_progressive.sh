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

# Basic calls.
"${AVIFENC}" --version
"${AVIFDEC}" --version

# Input file paths.
INPUT_Y4M="${TESTDATA_DIR}/kodim03_yuv420_8bpc.y4m"
# Output file names.
ENCODED_FILE="avif_test_cmd_encoded.avif"
DECODED_FILE="avif_test_cmd_decoded.png"

# Cleanup
cleanup() {
  pushd ${TMP_DIR}
    rm -- "${ENCODED_FILE}" "${DECODED_FILE}"
  popd
}
trap cleanup EXIT

pushd ${TMP_DIR}
  echo "Testing basic progressive"
  "${AVIFENC}" --auto-progressive -s 8 "${INPUT_Y4M}" -o "${ENCODED_FILE}"
  "${AVIFDEC}" "${ENCODED_FILE}" "${DECODED_FILE}"
  "${AVIFDEC}" --progressive "${ENCODED_FILE}" "${DECODED_FILE}"

  echo "Testing manual progressive"
  "${AVIFENC}" -s 8 --layer 2 -q 2 "${INPUT_Y4M}" -q 60 "${INPUT_Y4M}" -o "${ENCODED_FILE}"
  "${AVIFDEC}" "${ENCODED_FILE}" "${DECODED_FILE}"
  "${AVIFDEC}" --progressive "${ENCODED_FILE}" "${DECODED_FILE}"

  # libavif relies on libyuv to do scaling
  echo "Testing progressive with frame scaling"
  if avifenc -V | grep -o "libyuv : available" --quiet; then
    for SCALE in 1/2 1/2,1/2; do
      "${AVIFENC}" -s 8 --layer 2 --scaling-mode ${SCALE} "${INPUT_Y4M}" --scaling-mode 1 "${INPUT_Y4M}" -o "${ENCODED_FILE}"
      "${AVIFDEC}" "${ENCODED_FILE}" "${DECODED_FILE}"
      "${AVIFDEC}" --progressive "${ENCODED_FILE}" "${DECODED_FILE}"
    done
  fi

  echo "Testing no enough input"
  "${AVIFENC}" -s 8 --layer 3 -q 2 "${INPUT_Y4M}" -q 60 "${INPUT_Y4M}" -o "${ENCODED_FILE}" && exit 1
popd

exit 0
