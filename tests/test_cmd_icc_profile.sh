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
# tests for command lines (icc profile)

source $(dirname "$0")/cmd_test_common.sh || exit

# Input file paths.
INPUT_COLOR_PNG="${TESTDATA_DIR}/ArcTriomphe-cHRM-red-green-swap.png"
REFERENCE_COLOR_PNG="${TESTDATA_DIR}/ArcTriomphe-cHRM-red-green-swap-reference.png"
INPUT_GRAY_PNG="${TESTDATA_DIR}/kodim03_grayscale_gamma1.6.png"
REFERENCE_GRAY_PNG="${TESTDATA_DIR}/kodim03_grayscale_gamma1.6-reference.png"
SRGB_ICC="${TESTDATA_DIR}/sRGB2014.icc"
INPUT_ICC_PNG="${TESTDATA_DIR}/paris_icc_exif_xmp.png"
INPUT_ICC_JPG="${TESTDATA_DIR}/paris_exif_xmp_icc.jpg"
# Output file names.
ENCODED_FILE="avif_test_cmd_icc_profile_encoded.avif"
DECODED_FILE="avif_test_cmd_icc_profile_decoded.png"
CORRECTED_FILE="avif_test_cmd_icc_profile_corrected.png"

# Cleanup
cleanup() {
  pushd ${TMP_DIR}
    rm -f -- "${ENCODED_FILE}" "${DECODED_FILE}" "${CORRECTED_FILE}"
  popd
}
trap cleanup EXIT

pushd ${TMP_DIR}
  # Check --cicp flag works
  "${AVIFENC}" --cicp 9/12/8 -s 8 "${INPUT_COLOR_PNG}" -o "${ENCODED_FILE}"
  "${AVIFDEC}" "${ENCODED_FILE}" "${DECODED_FILE}" | grep "Color Primaries.* 9$"
  "${AVIFDEC}" "${ENCODED_FILE}" "${DECODED_FILE}" | grep "Transfer Char.* 12$"
  "${AVIFDEC}" "${ENCODED_FILE}" "${DECODED_FILE}" | grep "Matrix Coeffs.* 8$"

  # RGB ICC cannot be kept when asking for gray.
  "${AVIFENC}" -s 10 "${INPUT_ICC_JPG}" --yuv 400 -o "${ENCODED_FILE}" && exit 1
  "${AVIFENC}" -s 10 "${INPUT_ICC_PNG}" --yuv 400 -o "${ENCODED_FILE}" && exit 1
  "${AVIFENC}" -s 10 "${INPUT_ICC_JPG}" --yuv 400 --ignore-icc -o "${ENCODED_FILE}"
  "${AVIFENC}" -s 10 "${INPUT_ICC_PNG}" --yuv 400 --ignore-icc -o "${ENCODED_FILE}"

  # We use third-party tool (ImageMagick) to independently check
  # our generated ICC profile is valid and correct.
  if command -v magick &> /dev/null
  then
    IMAGEMAGICK="magick"
  elif command -v convert &> /dev/null
  then
    IMAGEMAGICK="convert"
  else
    echo Missing ImageMagick, test skipped
    touch "${ENCODED_FILE}"
    touch "${DECODED_FILE}"
    touch "${CORRECTED_FILE}"
    popd
    exit 0
  fi

  "${AVIFENC}" -s 8 -l "${INPUT_COLOR_PNG}" -o "${ENCODED_FILE}"
  # Old version of ImageMagick may not support reading ICC from AVIF.
  # Decode to PNG using avifdec first.
  "${AVIFDEC}" "${ENCODED_FILE}" "${DECODED_FILE}"
  "${IMAGEMAGICK}" "${DECODED_FILE}" -profile "${SRGB_ICC}" "${CORRECTED_FILE}"

  # PSNR test. Different Color Management Modules (CMMs) resulted in slightly different outputs.
  "${ARE_IMAGES_EQUAL}" "${REFERENCE_COLOR_PNG}" "${CORRECTED_FILE}" 0 50

  "${AVIFENC}" -s 8 -l "${INPUT_GRAY_PNG}" -o "${ENCODED_FILE}"
  "${AVIFDEC}" "${ENCODED_FILE}" "${DECODED_FILE}"
  "${IMAGEMAGICK}" "${DECODED_FILE}" -define png:color-type=2 -profile "${SRGB_ICC}" "${CORRECTED_FILE}"
  "${ARE_IMAGES_EQUAL}" "${REFERENCE_GRAY_PNG}" "${CORRECTED_FILE}" 0 45
popd

exit 0
