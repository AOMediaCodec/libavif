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
# tests for command lines (bit depth extension)

source $(dirname "$0")/cmd_test_common.sh || exit

# Input file path.
INPUT_PNG="${TESTDATA_DIR}/weld_16bit.png"
# Output file names.
INPUT_CROP_PNG="avif_test_cmd_depth_extension_input.png"
ENCODED_FILE="avif_test_cmd_depth_extension.avif"
DECODED_FILE="avif_test_cmd_depth_extension_decoded.png"

cleanup() {
  pushd ${TMP_DIR}
    rm -f -- "${INPUT_CROP_PNG}" "${ENCODED_FILE}" "${DECODED_FILE}"
  popd
}
trap cleanup EXIT

pushd ${TMP_DIR}
  # Crop the 1024x684 input image to speed up encoding under sanitizers.
  if command -v magick &> /dev/null; then
    magick "${INPUT_PNG}" -crop 128x128+0+0 +repage -depth 16 "${INPUT_CROP_PNG}"
    INPUT_PNG="${INPUT_CROP_PNG}"
  elif command -v convert &> /dev/null; then
    convert "${INPUT_PNG}" -crop 128x128+0+0 +repage -depth 16 "${INPUT_CROP_PNG}"
    INPUT_PNG="${INPUT_CROP_PNG}"
  fi

  echo "Default depth"
  "${AVIFENC}" "${INPUT_PNG}" --speed 9 -o "${ENCODED_FILE}"

  echo "Specified depth"
  "${AVIFENC}" "${INPUT_PNG}" --speed 9 --depth 8 -o "${ENCODED_FILE}"
  "${AVIFENC}" "${INPUT_PNG}" --speed 9 --depth 10 -o "${ENCODED_FILE}"
  "${AVIFENC}" "${INPUT_PNG}" --speed 9 --depth 12 -o "${ENCODED_FILE}"

  echo "Specified depth and depth extension"
  "${AVIFENC}" "${INPUT_PNG}" --speed 9 --depth 8,0 -o "${ENCODED_FILE}"
  "${AVIFDEC}" "${ENCODED_FILE}" "${DECODED_FILE}"
  "${AVIFENC}" "${INPUT_PNG}" --speed 9 --depth 8,8 -o "${ENCODED_FILE}"
  "${AVIFDEC}" "${ENCODED_FILE}" "${DECODED_FILE}"
  "${AVIFENC}" "${INPUT_PNG}" --speed 9 --depth 12,4 -o "${ENCODED_FILE}"
  "${AVIFDEC}" "${ENCODED_FILE}" "${DECODED_FILE}"
  "${AVIFENC}" "${INPUT_PNG}" --speed 9 --depth 12,8 -o "${ENCODED_FILE}"
  "${AVIFDEC}" "${ENCODED_FILE}" "${DECODED_FILE}"

  echo "Unsupported depth"
  "${AVIFENC}" "${INPUT_PNG}" --depth 16 -o "${ENCODED_FILE}" && exit 1
  echo "Unsupported depth extension"
  "${AVIFENC}" "${INPUT_PNG}" --depth 10,6 -o "${ENCODED_FILE}" && exit 1
popd

exit 0
