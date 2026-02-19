#!/bin/bash
# Copyright 2026 Google LLC
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
# tests for command lines using image transforms (clap/irot/imir...)

source $(dirname "$0")/cmd_test_common.sh || exit

# Input file paths.
INPUT_PNG="${TESTDATA_DIR}/paris_exif_xmp_icc.jpg"
# Output file names.
ENCODED_FILE="encoded.avif"
ENCODED_FILE_CLAP="encoded_clap.avif"

# Cleanup
cleanup() {
  rm -f -r "${TMP_DIR}"
}
trap cleanup EXIT

pushd ${TMP_DIR}
  # Encode/decode uncropped image
  # Some image magick versions may drop EXIF. Remove it preemptively to avoid
  # ARE_IMAGES_EQUAL failing because of EXIF mismatch.
  "${AVIFENC}" --ignore-exif -s 10 "${INPUT_PNG}" -o "${ENCODED_FILE}"
  "${AVIFDEC}" "${ENCODED_FILE}" "${ENCODED_FILE}.png"

  # Encode with crop
  "${AVIFENC}" --ignore-exif -s 10 "${INPUT_PNG}" --crop 10,50,100,25 -o "${ENCODED_FILE_CLAP}"
  # Decode to PNG
  "${AVIFDEC}" "${ENCODED_FILE_CLAP}" "${ENCODED_FILE_CLAP}.png"
  # Decode to JPEG
  "${AVIFDEC}" "${ENCODED_FILE_CLAP}" -q 100 "${ENCODED_FILE_CLAP}.jpg"

  if command -v magick &> /dev/null
  then
      IMAGEMAGICK="magick"
  elif command -v convert &> /dev/null
  then
      IMAGEMAGICK="convert"
  else
      echo Missing ImageMagick, test skipped
      popd
      exit 0
  fi
  "${IMAGEMAGICK}" --version

  "${IMAGEMAGICK}" "${ENCODED_FILE}.png" -crop 100x25+10+50 "${ENCODED_FILE}_cropped.png"
  "${ARE_IMAGES_EQUAL}" "${ENCODED_FILE}_cropped.png" "${ENCODED_FILE_CLAP}.png" 0
  "${ARE_IMAGES_EQUAL}" "${ENCODED_FILE}_cropped.png" "${ENCODED_FILE_CLAP}.jpg" 0 49
popd

exit 0
