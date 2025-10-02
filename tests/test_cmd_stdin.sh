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

source $(dirname "$0")/cmd_test_common.sh || exit

# Input file paths.
INPUT_Y4M_STILL="${TESTDATA_DIR}/kodim03_yuv420_8bpc.y4m"
INPUT_Y4M_ANIMATED="${TESTDATA_DIR}/webp_logo_animated.y4m"
INPUT_PNG="${TESTDATA_DIR}/draw_points.png"
INPUT_JPEG="${TESTDATA_DIR}/paris_extended_xmp.jpg"
# Output file names.
ENCODED_FILE_REGULAR="avif_test_cmd_stdin_encoded.avif"
ENCODED_FILE_STDIN="avif_test_cmd_stdin_encoded_with_stdin.avif"

# Cleanup
cleanup() {
  pushd ${TMP_DIR}
    rm -f -- "${ENCODED_FILE_REGULAR}" "${ENCODED_FILE_STDIN}"
  popd
}
trap cleanup EXIT

test_stdin() {
  INPUT="$1"
  INPUT_FORMAT="$2"
  shift 2
  EXTRA_FLAGS=$@

  # Make sure that --stdin can be replaced with a file path and that it leads to
  # the same encoded bytes.

  "${AVIFENC}" -s 8 -o "${ENCODED_FILE_REGULAR}" ${EXTRA_FLAGS} "${INPUT}"
  "${AVIFENC}" -s 8 -o "${ENCODED_FILE_STDIN}" --stdin --input-format ${INPUT_FORMAT} ${EXTRA_FLAGS} < "${INPUT}"
  cmp --silent "${ENCODED_FILE_REGULAR}" "${ENCODED_FILE_STDIN}"

  "${AVIFENC}" -s 9 "${INPUT}" -o "${ENCODED_FILE_REGULAR}" ${EXTRA_FLAGS}
  "${AVIFENC}" -s 9 --stdin -o "${ENCODED_FILE_STDIN}" --input-format ${INPUT_FORMAT} ${EXTRA_FLAGS} < "${INPUT}"
  cmp --silent "${ENCODED_FILE_REGULAR}" "${ENCODED_FILE_STDIN}"

  "${AVIFENC}" -s 10 ${EXTRA_FLAGS} "${INPUT}" "${ENCODED_FILE_REGULAR}"
  "${AVIFENC}" -s 10 --stdin "${ENCODED_FILE_STDIN}" --input-format ${INPUT_FORMAT} ${EXTRA_FLAGS} < "${INPUT}"
  cmp --silent "${ENCODED_FILE_REGULAR}" "${ENCODED_FILE_STDIN}"
  "${AVIFENC}" -s 10 "${ENCODED_FILE_STDIN}" --stdin --input-format ${INPUT_FORMAT} ${EXTRA_FLAGS} < "${INPUT}"
  cmp --silent "${ENCODED_FILE_REGULAR}" "${ENCODED_FILE_STDIN}"

  "${AVIFENC}" -s 10 "${INPUT}" -q 90 ${EXTRA_FLAGS} "${ENCODED_FILE_REGULAR}"
  "${AVIFENC}" -s 10 --stdin -q 90 "${ENCODED_FILE_STDIN}" --input-format ${INPUT_FORMAT} ${EXTRA_FLAGS} < "${INPUT}"
  cmp --silent "${ENCODED_FILE_REGULAR}" "${ENCODED_FILE_STDIN}"
  "${AVIFENC}" -s 10 "${ENCODED_FILE_STDIN}" -q 90 --stdin --input-format ${INPUT_FORMAT} ${EXTRA_FLAGS} < "${INPUT}"
  cmp --silent "${ENCODED_FILE_REGULAR}" "${ENCODED_FILE_STDIN}"
  "${AVIFENC}" -s 10 --stdin "${ENCODED_FILE_STDIN}" -q 90 --input-format ${INPUT_FORMAT} ${EXTRA_FLAGS} < "${INPUT}"
  cmp --silent "${ENCODED_FILE_REGULAR}" "${ENCODED_FILE_STDIN}"

  # Negative tests.

  # WARNING: Trailing options with update suffix has no effect. Place them before the input you intend to apply to.
  "${AVIFENC}" -s 10 --stdin "${ENCODED_FILE_STDIN}" -q:u 90 --input-format ${INPUT_FORMAT} ${EXTRA_FLAGS} < "${INPUT}"
  cmp --silent "${ENCODED_FILE_REGULAR}" "${ENCODED_FILE_STDIN}" && exit 1

  # ERROR: there cannot be any other input if --stdin is specified
  "${AVIFENC}" --stdin && exit 1
  "${AVIFENC}" --stdin "${INPUT}" "${ENCODED_FILE_STDIN}" && exit 1
  "${AVIFENC}" "${INPUT}" --stdin "${ENCODED_FILE_STDIN}" && exit 1
  "${AVIFENC}" "${INPUT}" "${ENCODED_FILE_STDIN}" --stdin && exit 1

  return 0
}

pushd ${TMP_DIR}
  test_stdin "${INPUT_Y4M_STILL}" y4m
  test_stdin "${INPUT_PNG}" png
  test_stdin "${INPUT_JPEG}" jpeg

  # Make the output of avifenc for animations deterministic by specifying the
  # creation_time and modification_time fields in boxes such as mvhd.
  NOW=$(date +%s)
  test_stdin "${INPUT_Y4M_ANIMATED}" y4m --creation-time ${NOW} --modification-time ${NOW}
popd

exit 0
