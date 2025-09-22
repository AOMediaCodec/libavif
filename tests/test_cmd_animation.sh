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

source $(dirname "$0")/cmd_test_common.sh || exit

# Input file paths.
INPUT_Y4M_0="${TESTDATA_DIR}/kodim03_yuv420_8bpc.y4m"
INPUT_Y4M_1="${TESTDATA_DIR}/kodim23_yuv420_8bpc.y4m"
INPUT_BAD_DIMENSIONS="${TESTDATA_DIR}/paris_exif_xmp_icc.jpg"
# Output file names.
ENCODED_FILE="avif_test_cmd_animation_encoded.avif"
DECODED_FILE="avif_test_cmd_animation_decoded.y4m"
ERROR_MSG="avif_test_cmd_animation_error_msg.txt"

cleanup() {
  pushd ${TMP_DIR}
    rm -f -- "${ENCODED_FILE}" "${DECODED_FILE}" "${ERROR_MSG}"
  popd
}
trap cleanup EXIT

# Looks for a string (NOT a regex) in a string.
# findstr needle haystack
findstr() {
  if ! echo "${2}" | grep -F "${1}"; then
    echo "ERROR: String '${1}' not found"
    return 1
  fi
}

pushd ${TMP_DIR}
  # Lossy test.
  "${AVIFENC}" -s 8 "${INPUT_Y4M_0}" "${INPUT_Y4M_1}" -o "${ENCODED_FILE}"
  "${AVIFDEC}" "${ENCODED_FILE}" "${DECODED_FILE}"
  # The are_images_equal binary only reads the first frame of each input file.
  "${ARE_IMAGES_EQUAL}" "${INPUT_Y4M_0}" "${DECODED_FILE}" 0 && exit 1

  # Lossless test.
  "${AVIFENC}" -s 8 "${INPUT_Y4M_0}" "${INPUT_Y4M_1}" -q 100 -o "${ENCODED_FILE}"
  "${AVIFDEC}" "${ENCODED_FILE}" "${DECODED_FILE}"
  "${ARE_IMAGES_EQUAL}" "${INPUT_Y4M_0}" "${DECODED_FILE}" 0

  # All input frames must have the same size.
  "${AVIFENC}" "${INPUT_Y4M_0}" "${INPUT_BAD_DIMENSIONS}" -o "${ENCODED_FILE}" \
    2> "${ERROR_MSG}" && exit 1
  grep "dimensions mismatch" "${ERROR_MSG}"

  # Output should be larger if second frame is set to higher quality.
  "${AVIFENC}" -s 8 -q 60 "${INPUT_Y4M_0}" "${INPUT_Y4M_1}" -o "${ENCODED_FILE}"
  "${AVIFDEC}" "${ENCODED_FILE}" "${DECODED_FILE}"
  Q60_FILE_SIZE=$(wc -c < "${ENCODED_FILE}")
  out=$("${AVIFENC}" -s 8 -q 60 "${INPUT_Y4M_0}" -q:u 100 "${INPUT_Y4M_1}" -o "${ENCODED_FILE}")
  findstr "Encoding frame 0 [1/25 ts] color quality [60 (Medium)], alpha quality [60 (Medium)]" "${out}"
  findstr "Encoding frame 1 [1/25 ts] color quality [100 (Lossless)], alpha quality [100 (Lossless)]" "${out}"
  "${AVIFDEC}" "${ENCODED_FILE}" "${DECODED_FILE}"
  Q60_Q100_FILE_SIZE=$(wc -c < "${ENCODED_FILE}")
  [[ ${Q60_FILE_SIZE} -lt ${Q60_Q100_FILE_SIZE} ]] || exit 1

  # Test updating of q and qalpha. Alpha quality defaults to the same as color quality
  # until the first time it's explicitly set.
  out=$("${AVIFENC}" -s 8 -q 70 "${INPUT_Y4M_0}" -q:u 20 "${INPUT_Y4M_1}" --qalpha:u 10 "${INPUT_Y4M_0}" -q:u 30 "${INPUT_Y4M_1}" -o "${ENCODED_FILE}")
  findstr "Encoding frame 0 [1/25 ts] color quality [70 (Medium)], alpha quality [70 (Medium)]" "${out}"
  findstr "Encoding frame 1 [1/25 ts] color quality [20 (Low)], alpha quality [20 (Low)]" "${out}"
  findstr "Encoding frame 2 [1/25 ts] color quality [20 (Low)], alpha quality [10 (Low)]" "${out}"
  findstr "Encoding frame 3 [1/25 ts] color quality [30 (Low)], alpha quality [10 (Low)]" "${out}"
  out=$("${AVIFENC}" -s 8 --qalpha 50 -q 70 "${INPUT_Y4M_0}" -q:u 20 "${INPUT_Y4M_1}" -o "${ENCODED_FILE}")
  findstr "Encoding frame 0 [1/25 ts] color quality [70 (Medium)], alpha quality [50 (Medium)]" "${out}"
  findstr "Encoding frame 1 [1/25 ts] color quality [20 (Low)], alpha quality [50 (Medium)]" "${out}"
  out=$("${AVIFENC}" -s 8 "${INPUT_Y4M_0}" "${INPUT_Y4M_1}" -o "${ENCODED_FILE}")
  findstr "Encoding frame 0 [1/25 ts] color quality [60 (Medium)], alpha quality [60 (Medium)]" "${out}"
  findstr "Encoding frame 1 [1/25 ts] color quality [60 (Medium)], alpha quality [60 (Medium)]" "${out}"

  # Test updating of tiling options.
  out=$("${AVIFENC}" -s 10 "${INPUT_Y4M_0}" --tilerowslog2:u 1 "${INPUT_Y4M_1}" --tilecolslog2:u 2 "${INPUT_Y4M_0}" --autotiling:u "${INPUT_Y4M_1}" --tilecolslog2:u 1 "${INPUT_Y4M_0}" -o "${ENCODED_FILE}")
  findstr "Encoding frame 0 [1/25 ts] color quality [60 (Medium)], alpha quality [60 (Medium)], automatic tiling" "${out}"
  findstr "Encoding frame 1 [1/25 ts] color quality [60 (Medium)], alpha quality [60 (Medium)], tileRowsLog2 [1], tileColsLog2 [0]" "${out}"
  findstr "Encoding frame 2 [1/25 ts] color quality [60 (Medium)], alpha quality [60 (Medium)], tileRowsLog2 [1], tileColsLog2 [2]" "${out}"
  findstr "Encoding frame 3 [1/25 ts] color quality [60 (Medium)], alpha quality [60 (Medium)], automatic tiling" "${out}"
  findstr "Encoding frame 4 [1/25 ts] color quality [60 (Medium)], alpha quality [60 (Medium)], tileRowsLog2 [0], tileColsLog2 [1]" "${out}"
  # --autotiling:u and --tilerowslog2:u R --tilecolslog2:u C are mutually exclusive.
  "${AVIFENC}" -s 10 "${INPUT_Y4M_0}" --autotiling:u --tilerowslog2:u 1 "${INPUT_Y4M_1}" -o "${ENCODED_FILE}" && exit 1
  "${AVIFENC}" -s 10 "${INPUT_Y4M_0}" --autotiling:u --tilecolslog2:u 2 "${INPUT_Y4M_1}" -o "${ENCODED_FILE}" && exit 1
  "${AVIFENC}" -s 10 "${INPUT_Y4M_0}" --autotiling:u --tilerowslog2:u 1 --tilecolslog2:u 2 "${INPUT_Y4M_1}" -o "${ENCODED_FILE}" && exit 1
popd

exit 0
