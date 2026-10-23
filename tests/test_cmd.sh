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

source $(dirname "$0")/cmd_test_common.sh || exit

# Basic calls.
"${AVIFENC}" --version
"${AVIFDEC}" --version

# Input file paths.
INPUT_Y4M="${TESTDATA_DIR}/kodim03_yuv420_8bpc.y4m"
INPUT_PNG="${TESTDATA_DIR}/circle-trns-after-plte.png"
INPUT_UTF8_Y4M="🐾.y4m"
# Output file names.
ENCODED_FILE="avif_test_cmd_encoded.avif"
ENCODED_UTF8_FILE="🐾.avif"
ENCODED_FILE_REFERENCE="avif_test_cmd_encoded_ref.avif"
ENCODED_FILE_WITH_DASH="-avif_test_cmd_encoded.avif"
DECODED_FILE="avif_test_cmd_decoded.png"
DECODED_UTF8_FILE="😀.png"
OUT_MSG="avif_test_cmd_out_msg.txt"

# Cleanup
cleanup() {
  pushd ${TMP_DIR}
    rm -f -- "${ENCODED_FILE}" "${ENCODED_UTF8_FILE}" "${ENCODED_FILE_REFERENCE}" \
             "${ENCODED_FILE_WITH_DASH}" "${DECODED_FILE}" "${DECODED_UTF8_FILE}" \
             "${OUT_MSG}"
  popd
}
trap cleanup EXIT

pushd ${TMP_DIR}
  # Test options.
  echo "Testing options"
  for pair in aom,end-usage,cbr avm,end-usage,cbr rav1e,tiles,1 svt,film-grain,0; do
    IFS=','; set -- $pair
    if "${AVIFENC}" --help | grep $1' \['; then
      "${AVIFENC}" -s 10 -q 85 -c $1 -a foo=1 "${INPUT_Y4M}" "${ENCODED_FILE}" > "${OUT_MSG}" && exit 1
      "${AVIFENC}" -s 10 -q 85 -c $1 -a $2=$3 "${INPUT_Y4M}" "${ENCODED_FILE}" > "${OUT_MSG}"
    fi
  done

  # Lossy test. The decoded pixels should be different from the original image.
  echo "Testing basic lossy"
  "${AVIFENC}" -s 8 "${INPUT_Y4M}" -o "${ENCODED_FILE}"
  "${AVIFDEC}" "${ENCODED_FILE}" "${DECODED_FILE}"
  "${ARE_IMAGES_EQUAL}" "${INPUT_Y4M}" "${DECODED_FILE}" 0 && exit 1
  cp ${INPUT_Y4M} ${INPUT_UTF8_Y4M}
  "${AVIFENC}" -s 8 "${INPUT_UTF8_Y4M}" -o "${ENCODED_UTF8_FILE}"
  "${AVIFDEC}" "${ENCODED_UTF8_FILE}" "${DECODED_UTF8_FILE}"
  RET=0
  "${ARE_IMAGES_EQUAL}" "${INPUT_UTF8_Y4M}" "${DECODED_UTF8_FILE}" 0 || RET=$?
  if [[ ${RET} -ne 1 ]]; then
    exit 1
  fi
  "${AVIFENC}" -s 8 --depth 8 "${INPUT_Y4M}" -o "${ENCODED_FILE}"
  "${AVIFENC}" -s 8 --depth 8,8 "${INPUT_Y4M}" -o "${ENCODED_FILE}" 2> "${OUT_MSG}" && exit 1
  grep "ERROR: --depth 8,8 is not supported for Y4M input" "${OUT_MSG}"
  "${AVIFENC}" -s 8 --depth 10 "${INPUT_Y4M}" -o "${ENCODED_FILE}" 2> "${OUT_MSG}" && exit 1
  grep "ERROR: --depth 10 does not match Y4M bit depth 8" "${OUT_MSG}"

  # Argument parsing test with filenames starting with a dash.
  echo "Testing arguments"
  "${AVIFENC}" -s 10 "${INPUT_Y4M}" -- "${ENCODED_FILE_WITH_DASH}"
  "${AVIFDEC}" --info  -- "${ENCODED_FILE_WITH_DASH}"
  # Passing a filename starting with a dash without using -- should fail.
  "${AVIFENC}" -s 10 "${INPUT_Y4M}" "${ENCODED_FILE_WITH_DASH}" && exit 1
  "${AVIFDEC}" --info "${ENCODED_FILE_WITH_DASH}" && exit 1
  # A negative number is the argument of the preceding option, which validates it.
  "${AVIFENC}" -s 10 --jobs -10 "${INPUT_Y4M}" "${ENCODED_FILE}" 2> "${OUT_MSG}" && exit 1
  grep "ERROR: Invalid number of jobs: -10" "${OUT_MSG}"
  "${AVIFDEC}" --jobs -10 "${ENCODED_FILE}" "${DECODED_FILE}" 2> "${OUT_MSG}" && exit 1
  grep "ERROR: invalid number of jobs: -10" "${OUT_MSG}"
  "${AVIFDEC}" --index -1 "${ENCODED_FILE}" "${DECODED_FILE}" 2> "${OUT_MSG}" && exit 1
  grep "ERROR: invalid index: -1" "${OUT_MSG}"
  "${AVIFDEC}" --size-limit -1 "${ENCODED_FILE}" "${DECODED_FILE}" 2> "${OUT_MSG}" && exit 1
  grep "ERROR: invalid image size limit: -1" "${OUT_MSG}"
  # Negative values must not be converted to large unsigned values.
  "${AVIFENC}" -s 10 --cicp -1/-1/-1 "${INPUT_Y4M}" "${ENCODED_FILE}" 2> "${OUT_MSG}" && exit 1
  grep "ERROR: Invalid CICP value: -1/-1/-1" "${OUT_MSG}"
  "${AVIFENC}" -s 10 --pasp -1,1 "${INPUT_Y4M}" "${ENCODED_FILE}" 2> "${OUT_MSG}" && exit 1
  grep "ERROR: Invalid pasp values: -1,1" "${OUT_MSG}"
  "${AVIFENC}" -s 10 --irot -253 "${INPUT_Y4M}" "${ENCODED_FILE}" 2> "${OUT_MSG}" && exit 1
  grep "ERROR: Invalid irot angle: -253" "${OUT_MSG}"
  "${AVIFENC}" -s 10 --imir -255 "${INPUT_Y4M}" "${ENCODED_FILE}" 2> "${OUT_MSG}" && exit 1
  grep "ERROR: Invalid imir axis: -255" "${OUT_MSG}"
  # The clap offsets are signed.
  "${AVIFENC}" -s 10 --clap 100,1,100,1,-10,1,-20,1 "${INPUT_Y4M}" "${ENCODED_FILE}"
  "${AVIFDEC}" --info "${ENCODED_FILE}" | grep "hOff: -10/1, vOff: -20/1"
  # An option name is not the argument of the preceding option.
  "${AVIFENC}" -s 10 --jobs -o "${ENCODED_FILE}" "${INPUT_Y4M}" 2> "${OUT_MSG}" && exit 1
  grep -- "--jobs requires an argument." "${OUT_MSG}"
  "${AVIFDEC}" --jobs -i "${ENCODED_FILE}" 2> "${OUT_MSG}" && exit 1
  grep -- "--jobs requires an argument." "${OUT_MSG}"

  # Option update handling test
  # Passing non-update option before input should not print warning.
  "${AVIFENC}" -s 10 -q 85 "${INPUT_Y4M}" "${ENCODED_FILE_REFERENCE}" 2> "${OUT_MSG}"
  grep "WARNING: -q" "${OUT_MSG}" && exit 1
  grep "WARNING: Trailing options" "${OUT_MSG}" && exit 1
  # Passing non-update option after input should print warning.
  "${AVIFENC}" -s 10 "${INPUT_Y4M}" "${ENCODED_FILE}" -q 85 2> "${OUT_MSG}"
  grep "WARNING: -q" "${OUT_MSG}"
  cmp -s "${ENCODED_FILE_REFERENCE}" "${ENCODED_FILE}"
  # Passing non-update option after input but before positional output should also print warning.
  "${AVIFENC}" -s 10 "${INPUT_Y4M}" -q 85 "${ENCODED_FILE}" 2> "${OUT_MSG}"
  grep "WARNING: -q" "${OUT_MSG}"
  cmp -s "${ENCODED_FILE_REFERENCE}" "${ENCODED_FILE}"
  # Passing update option after input should print warning, and has no effect.
  "${AVIFENC}" -s 10 "${INPUT_Y4M}" "${ENCODED_FILE}" -q:u 85 2> "${OUT_MSG}"
  grep "WARNING: Trailing options" "${OUT_MSG}"
  cmp -s "${ENCODED_FILE_REFERENCE}" "${ENCODED_FILE}" && exit 1
  # Passing update option after input but before positional output should also print warning, and has no effect.
  "${AVIFENC}" -s 10 "${INPUT_Y4M}" -q:u 85 "${ENCODED_FILE}" 2> "${OUT_MSG}"
  grep "WARNING: Trailing options" "${OUT_MSG}"
  cmp -s "${ENCODED_FILE_REFERENCE}" "${ENCODED_FILE}" && exit 1

  # --min and --max must be both specified.
  "${AVIFENC}" -s 10 --min 24 "${INPUT_Y4M}" "${ENCODED_FILE}" && exit 1
  "${AVIFENC}" -s 10 --max 26 "${INPUT_Y4M}" "${ENCODED_FILE}" && exit 1
  # --minalpha and --maxalpha must be both specified.
  "${AVIFENC}" -s 10 --minalpha 0 "${INPUT_PNG}" "${ENCODED_FILE}" && exit 1
  "${AVIFENC}" -s 10 --maxalpha 0 "${INPUT_PNG}" "${ENCODED_FILE}" && exit 1

  # The default quality is 60. The default alpha quality is 100 (lossless).
  "${AVIFENC}" -s 10 "${INPUT_Y4M}" "${ENCODED_FILE}" > "${OUT_MSG}"
  grep " color quality \[60 " "${OUT_MSG}"
  grep " alpha quality \[60 " "${OUT_MSG}"
  "${AVIFENC}" -s 10 -q 85 "${INPUT_Y4M}" "${ENCODED_FILE}" > "${OUT_MSG}"
  grep " color quality \[85 " "${OUT_MSG}"
  grep " alpha quality \[85 " "${OUT_MSG}"
  # The average of 15 and 25 is 20. Quantizer 20 maps to quality 68.
  "${AVIFENC}" -s 10 --min 15 --max 25 "${INPUT_Y4M}" "${ENCODED_FILE}" > "${OUT_MSG}"
  grep " color quality \[68 " "${OUT_MSG}"
  grep " alpha quality \[68 " "${OUT_MSG}"
  "${AVIFENC}" -s 10 -q 65 --min 15 --max 25 "${INPUT_Y4M}" "${ENCODED_FILE}" > "${OUT_MSG}"
  grep " color quality \[65 " "${OUT_MSG}"
  grep " alpha quality \[65 " "${OUT_MSG}"

  # Test tiling options.
  echo "Testing tiling options"
  "${AVIFENC}" -s 10 "${INPUT_Y4M}" "${ENCODED_FILE}" > "${OUT_MSG}"
  grep " automatic tiling," "${OUT_MSG}"
  "${AVIFENC}" -s 10 --tilerowslog2 1 "${INPUT_Y4M}" "${ENCODED_FILE}" > "${OUT_MSG}"
  grep " tileRowsLog2 \[1\], tileColsLog2 \[0\]," "${OUT_MSG}"
  "${AVIFENC}" -s 10 --tilecolslog2 2 "${INPUT_Y4M}" "${ENCODED_FILE}" > "${OUT_MSG}"
  grep " tileRowsLog2 \[0\], tileColsLog2 \[2\]," "${OUT_MSG}"
  "${AVIFENC}" -s 10 --tilerowslog2 1 --tilecolslog2 2 "${INPUT_Y4M}" "${ENCODED_FILE}" > "${OUT_MSG}"
  grep " tileRowsLog2 \[1\], tileColsLog2 \[2\]," "${OUT_MSG}"
  "${AVIFENC}" -s 10 --autotiling "${INPUT_Y4M}" "${ENCODED_FILE}" > "${OUT_MSG}"
  grep " automatic tiling," "${OUT_MSG}"
  # --autotiling and --tilerowslog2 R --tilecolslog2 C are mutually exclusive.
  "${AVIFENC}" --autotiling --tilerowslog2 1 "${INPUT_Y4M}" "${ENCODED_FILE}" && exit 1
  "${AVIFENC}" --autotiling --tilecolslog2 2 "${INPUT_Y4M}" "${ENCODED_FILE}" && exit 1
  "${AVIFENC}" --autotiling --tilerowslog2 1 --tilecolslog2 2 "${INPUT_Y4M}" "${ENCODED_FILE}" && exit 1

  # Test --ignore-alpha.
  echo "Testing --ignore-alpha"
  "${AVIFENC}" -s 10 "${INPUT_PNG}" "${ENCODED_FILE}" > "${OUT_MSG}"
  grep " Alpha          : Not premultiplied" "${OUT_MSG}"
  "${AVIFENC}" -s 10 --ignore-alpha "${INPUT_PNG}" "${ENCODED_FILE}" > "${OUT_MSG}"
  grep " Alpha          : Absent" "${OUT_MSG}"
  grep " Alpha total size: 0 bytes" "${OUT_MSG}"
popd

exit 0
