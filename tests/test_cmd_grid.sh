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
# tests for command lines (grid)

source $(dirname "$0")/cmd_test_common.sh || exit

# Input file paths.
INPUT_PNG="${TESTDATA_DIR}/paris_icc_exif_xmp.png" # 403 x 302 px
# Output file names.
ENCODED_FILE="avif_test_cmd_grid_encoded.avif"
DECODED_FILE="avif_test_cmd_grid_decoded.png"
ENCODED_FILE_2x2="avif_test_cmd_grid_2x2_encoded.avif"
DECODED_FILE_2x2="avif_test_cmd_grid_2x2_decoded.png"
ENCODED_FILE_7x5="avif_test_cmd_grid_7x5_encoded.avif"
DECODED_FILE_7x5="avif_test_cmd_grid_7x5_decoded.png"

# Cleanup
cleanup() {
  pushd ${TMP_DIR}
    rm -f -- "${ENCODED_FILE}" "${DECODED_FILE}" \
             "${ENCODED_FILE_2x2}" "${DECODED_FILE_2x2}" \
             "${ENCODED_FILE_7x5}" "${DECODED_FILE_7x5}"
  popd
}
trap cleanup EXIT

pushd ${TMP_DIR}
  echo "Testing basic grid"
  "${AVIFENC}" -s 8 "${INPUT_PNG}" --grid 2x2 -o "${ENCODED_FILE_2x2}"
  "${AVIFDEC}" "${ENCODED_FILE_2x2}" "${DECODED_FILE_2x2}"

  echo "Testing monochrome grid with odd width (403 px)"
  "${AVIFENC}" -s 8 "${INPUT_PNG}" --grid 2x2 --yuv 400 --ignore-icc -o "${ENCODED_FILE_2x2}"
  "${AVIFDEC}" "${ENCODED_FILE_2x2}" "${DECODED_FILE_2x2}"

  echo "Testing max grid"
  "${AVIFENC}" -s 8 "${INPUT_PNG}" --grid 7x5 -o "${ENCODED_FILE_7x5}"
  "${AVIFDEC}" "${ENCODED_FILE_7x5}" "${DECODED_FILE_7x5}"

  # Lossy compression should give different results for different tiles.
  "${ARE_IMAGES_EQUAL}" "${DECODED_FILE_2x2}" "${DECODED_FILE_7x5}" 1 && exit 1

  echo "Testing grid patterns"
  for GRID in 1x1 1x2 1x3 1x4 1x5 2x1 3x1 4x1 5x1 6x1 7x1; do
    "${AVIFENC}" -s 10 "${INPUT_PNG}" --grid ${GRID} -o "${ENCODED_FILE}"
    "${AVIFDEC}" "${ENCODED_FILE}" "${DECODED_FILE}"
  done

  echo "Testing wrong grid arguments"
  "${AVIFENC}" "${INPUT_PNG}" --grid 0x0 -o "${ENCODED_FILE}" && exit 1
  "${AVIFENC}" "${INPUT_PNG}" --grid 8x5 -o "${ENCODED_FILE}" && exit 1
  "${AVIFENC}" "${INPUT_PNG}" --grid 7x6 -o "${ENCODED_FILE}" && exit 1
  "${AVIFENC}" "${INPUT_PNG}" --grid 4294967295x4294967295 -o "${ENCODED_FILE}" && exit 1
  "${AVIFENC}" "${INPUT_PNG}" --grid 2147483647x2147483647 -o "${ENCODED_FILE}" && exit 1

  echo "Testing valid grid arguments but invalid grid image dimensions for subsampled image"
  "${AVIFENC}" "${INPUT_PNG}" --grid 2x2 --yuv 420 -o "${ENCODED_FILE}" && exit 1
  # Even if there is a single tile in the odd dimension, it is forbidden.
  "${AVIFENC}" "${INPUT_PNG}" --grid 1x2 --yuv 422 -o "${ENCODED_FILE}" && exit 1
  # 1x1 is not a real grid.
  "${AVIFENC}" "${INPUT_PNG}" --grid 1x1 --yuv 420 -o "${ENCODED_FILE}"
popd

exit 0
