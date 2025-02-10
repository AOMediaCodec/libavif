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

source $(dirname "$0")/cmd_test_common.sh

# Input file paths.
INPUT_Y4M_STILL="${TESTDATA_DIR}/kodim03_yuv420_8bpc.y4m"
INPUT_Y4M_ANIMATED="${TESTDATA_DIR}/webp_logo_animated.y4m"
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

strip_header_if() {
  FILE="$1"
  STRIP_HEADER="$2"

  if ${STRIP_HEADER}; then
    # The following does not work on all platforms:
    #   grep --text mdat "${FILE}"
    #   awk -b -v RS='mdat' '{print length($0); exit}' "${FILE}"
    # Hence the hardcoded variable below.
    MDAT_OFFSET=1061
    FILE_CONTENTS_AFTER_HEADER=$(tail -c +${MDAT_OFFSET} < "${FILE}")
    echo "${FILE_CONTENTS_AFTER_HEADER}" > "${FILE}"
  fi
}

test_stdin() {
  INPUT_Y4M="$1"
  STRIP_HEADER="$2"

  # Make sure that --stdin can be replaced with a file path and that it leads to
  # the same encoded bytes.

  "${AVIFENC}" -s 8 -o "${ENCODED_FILE_REGULAR}" "${INPUT_Y4M}"
  "${AVIFENC}" -s 8 -o "${ENCODED_FILE_STDIN}" --stdin < "${INPUT_Y4M}"
  strip_header_if "${ENCODED_FILE_REGULAR}" ${STRIP_HEADER}
  strip_header_if "${ENCODED_FILE_STDIN}" ${STRIP_HEADER}
  cmp --silent "${ENCODED_FILE_REGULAR}" "${ENCODED_FILE_STDIN}"

  "${AVIFENC}" -s 9 "${INPUT_Y4M}" -o "${ENCODED_FILE_REGULAR}"
  "${AVIFENC}" -s 9 --stdin -o "${ENCODED_FILE_STDIN}" < "${INPUT_Y4M}"
  strip_header_if "${ENCODED_FILE_REGULAR}" ${STRIP_HEADER}
  strip_header_if "${ENCODED_FILE_STDIN}" ${STRIP_HEADER}
  cmp --silent "${ENCODED_FILE_REGULAR}" "${ENCODED_FILE_STDIN}"

  "${AVIFENC}" -s 10 "${INPUT_Y4M}" "${ENCODED_FILE_REGULAR}"
  "${AVIFENC}" -s 10 --stdin "${ENCODED_FILE_STDIN}" < "${INPUT_Y4M}"
  strip_header_if "${ENCODED_FILE_REGULAR}" ${STRIP_HEADER}
  strip_header_if "${ENCODED_FILE_STDIN}" ${STRIP_HEADER}
  cmp --silent "${ENCODED_FILE_REGULAR}" "${ENCODED_FILE_STDIN}"
  "${AVIFENC}" -s 10 "${ENCODED_FILE_STDIN}" --stdin < "${INPUT_Y4M}"
  strip_header_if "${ENCODED_FILE_STDIN}" ${STRIP_HEADER}
  cmp --silent "${ENCODED_FILE_REGULAR}" "${ENCODED_FILE_STDIN}"

  "${AVIFENC}" -s 10 "${INPUT_Y4M}" -q 90 "${ENCODED_FILE_REGULAR}"
  "${AVIFENC}" -s 10 --stdin -q 90 "${ENCODED_FILE_STDIN}" < "${INPUT_Y4M}"
  strip_header_if "${ENCODED_FILE_REGULAR}" ${STRIP_HEADER}
  strip_header_if "${ENCODED_FILE_STDIN}" ${STRIP_HEADER}
  cmp --silent "${ENCODED_FILE_REGULAR}" "${ENCODED_FILE_STDIN}"
  "${AVIFENC}" -s 10 "${ENCODED_FILE_STDIN}" -q 90 --stdin < "${INPUT_Y4M}"
  strip_header_if "${ENCODED_FILE_STDIN}" ${STRIP_HEADER}
  cmp --silent "${ENCODED_FILE_REGULAR}" "${ENCODED_FILE_STDIN}"
  "${AVIFENC}" -s 10 --stdin "${ENCODED_FILE_STDIN}" -q 90 < "${INPUT_Y4M}"
  strip_header_if "${ENCODED_FILE_STDIN}" ${STRIP_HEADER}
  cmp --silent "${ENCODED_FILE_REGULAR}" "${ENCODED_FILE_STDIN}"

  # Negative tests.

  # WARNING: Trailing options with update suffix has no effect. Place them before the input you intend to apply to.
  "${AVIFENC}" -s 10 --stdin "${ENCODED_FILE_STDIN}" -q:u 90 < "${INPUT_Y4M}"
  cmp --silent "${ENCODED_FILE_REGULAR}" "${ENCODED_FILE_STDIN}" && exit 1

  # ERROR: there cannot be any other input if --stdin is specified
  "${AVIFENC}" --stdin && exit 1
  "${AVIFENC}" --stdin "${INPUT_Y4M}" "${ENCODED_FILE_STDIN}" && exit 1
  "${AVIFENC}" "${INPUT_Y4M}" --stdin "${ENCODED_FILE_STDIN}" && exit 1
  "${AVIFENC}" "${INPUT_Y4M}" "${ENCODED_FILE_STDIN}" --stdin && exit 1

  return 0
}

pushd ${TMP_DIR}
  test_stdin "${INPUT_Y4M_STILL}" false

  # The output of avifenc for animations is not deterministic because of boxes
  # such as mvhd and its field creation_time. Strip the whole header to compare
  # only the encoded samples.
  test_stdin "${INPUT_Y4M_ANIMATED}" true
popd

exit 0
