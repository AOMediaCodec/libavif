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

  # Crop
  "${AVIFENC}" --ignore-exif -s 10 "${INPUT_PNG}" --crop 10,50,100,25 -o "encoded_clap.avif"
  "${AVIFDEC}" "encoded_clap.avif" "encoded_clap.png"
  "${AVIFDEC}" "encoded_clap.avif" -q 100 "encoded_clap.jpg"

  # Rotation (0 degrees)
  "${AVIFENC}" --ignore-exif -s 10 "${INPUT_PNG}" --irot 0 -o "encoded_irot0.avif"
  "${AVIFDEC}" "encoded_irot0.avif" "encoded_irot0.png"
  "${AVIFDEC}" "encoded_irot0.avif" -q 100 "encoded_irot0.jpg"

  # Rotation (90 degrees anti-clockwise)
  "${AVIFENC}" --ignore-exif -s 10 "${INPUT_PNG}" --irot 1 -o "encoded_irot1.avif"
  "${AVIFDEC}" "encoded_irot1.avif" "encoded_irot1.png"
  "${AVIFDEC}" "encoded_irot1.avif" -q 100 "encoded_irot1.jpg"

  # Rotation (180 degrees)
  "${AVIFENC}" --ignore-exif -s 10 "${INPUT_PNG}" --irot 2 -o "encoded_irot2.avif"
  "${AVIFDEC}" "encoded_irot2.avif" "encoded_irot2.png"
  "${AVIFDEC}" "encoded_irot2.avif" -q 100 "encoded_irot2.jpg"

  # Rotation (90 clockwise)
  "${AVIFENC}" --ignore-exif -s 10 "${INPUT_PNG}" --irot 3 -o "encoded_irot3.avif"
  "${AVIFDEC}" "encoded_irot3.avif" "encoded_irot3.png"
  "${AVIFDEC}" "encoded_irot3.avif" -q 100 "encoded_irot3.jpg"

  # Mirror (top-to-bottom)
  "${AVIFENC}" --ignore-exif -s 10 "${INPUT_PNG}" --imir 0 -o "encoded_imir0.avif"
  "${AVIFDEC}" "encoded_imir0.avif" "encoded_imir0.png"
  "${AVIFDEC}" "encoded_imir0.avif" -q 100 "encoded_imir0.jpg"

  # Mirror (left-to-right)
  "${AVIFENC}" --ignore-exif -s 10 "${INPUT_PNG}" --imir 1 -o "encoded_imir1.avif"
  "${AVIFDEC}" "encoded_imir1.avif" "encoded_imir1.png"
  "${AVIFDEC}" "encoded_imir1.avif" -q 100 "encoded_imir1.jpg"

  # Combined transforms: crop + mirror (no rotation)
  "${AVIFENC}" --ignore-exif -s 10 "${INPUT_PNG}" --crop 10,50,100,25 --imir 0 -o "encoded_crop_imir0.avif"
  "${AVIFDEC}" "encoded_crop_imir0.avif" "encoded_crop_imir0.png"
  "${AVIFDEC}" "encoded_crop_imir0.avif" -q 100 "encoded_crop_imir0.jpg"

  # Combined transforms: crop + rotation + mirror
  # MIAF (ISO/IEC 23000-22) specifies the application order: 1. clap, 2. irot, 3. imir.
  "${AVIFENC}" --ignore-exif -s 10 "${INPUT_PNG}" --crop 10,50,100,25 --irot 1 --imir 1 -o "encoded_combined.avif"
  "${AVIFDEC}" "encoded_combined.avif" "encoded_combined.png"
  "${AVIFDEC}" "encoded_combined.avif" -q 100 "encoded_combined.jpg"

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

  JPEG_PSNR_THRESHOLD=48

  # Crop
  "${IMAGEMAGICK}" "${ENCODED_FILE}.png" -crop 100x25+10+50 "encoded_clap_ref.png"
  "${ARE_IMAGES_EQUAL}" "encoded_clap_ref.png" "encoded_clap.png" 0
  "${ARE_IMAGES_EQUAL}" "encoded_clap_ref.png" "encoded_clap.jpg" 0 ${JPEG_PSNR_THRESHOLD}

  # Rotation (0 degrees)
  "${IMAGEMAGICK}" "${ENCODED_FILE}.png" -rotate 0 "encoded_irot0_ref.png"
  "${ARE_IMAGES_EQUAL}" "encoded_irot0_ref.png" "encoded_irot0.png" 0
  "${ARE_IMAGES_EQUAL}" "encoded_irot0_ref.png" "encoded_irot0.jpg" 0 ${JPEG_PSNR_THRESHOLD}

  # Rotation (90 degrees anti-clockwise)
  "${IMAGEMAGICK}" "${ENCODED_FILE}.png" -rotate -90 "encoded_irot1_ref.png"
  "${ARE_IMAGES_EQUAL}" "encoded_irot1_ref.png" "encoded_irot1.png" 0
  "${ARE_IMAGES_EQUAL}" "encoded_irot1_ref.png" "encoded_irot1.jpg" 0 ${JPEG_PSNR_THRESHOLD}

  # Rotation (180 degrees)
  "${IMAGEMAGICK}" "${ENCODED_FILE}.png" -rotate 180 "encoded_irot2_ref.png"
  "${ARE_IMAGES_EQUAL}" "encoded_irot2_ref.png" "encoded_irot2.png" 0
  "${ARE_IMAGES_EQUAL}" "encoded_irot2_ref.png" "encoded_irot2.jpg" 0 ${JPEG_PSNR_THRESHOLD}

  # Rotation (90 clockwise)
  "${IMAGEMAGICK}" "${ENCODED_FILE}.png" -rotate 90 "encoded_irot3_ref.png"
  "${ARE_IMAGES_EQUAL}" "encoded_irot3_ref.png" "encoded_irot3.png" 0
  "${ARE_IMAGES_EQUAL}" "encoded_irot3_ref.png" "encoded_irot3.jpg" 0 ${JPEG_PSNR_THRESHOLD}

  # Mirror (top-to-bottom)
  "${IMAGEMAGICK}" "${ENCODED_FILE}.png" -flip "encoded_imir0_ref.png"
  "${ARE_IMAGES_EQUAL}" "encoded_imir0_ref.png" "encoded_imir0.png" 0
  "${ARE_IMAGES_EQUAL}" "encoded_imir0_ref.png" "encoded_imir0.jpg" 0 ${JPEG_PSNR_THRESHOLD}

  # Mirror (left-to-right)
  "${IMAGEMAGICK}" "${ENCODED_FILE}.png" -flop "encoded_imir1_ref.png"
  "${ARE_IMAGES_EQUAL}" "encoded_imir1_ref.png" "encoded_imir1.png" 0
  "${ARE_IMAGES_EQUAL}" "encoded_imir1_ref.png" "encoded_imir1.jpg" 0 ${JPEG_PSNR_THRESHOLD}

  # Combined transforms: crop + mirror (no rotation)
  "${IMAGEMAGICK}" "${ENCODED_FILE}.png" -crop 100x25+10+50 -flip "encoded_crop_imir0_ref.png"
  "${ARE_IMAGES_EQUAL}" "encoded_crop_imir0_ref.png" "encoded_crop_imir0.png" 0
  "${ARE_IMAGES_EQUAL}" "encoded_crop_imir0_ref.png" "encoded_crop_imir0.jpg" 0 ${JPEG_PSNR_THRESHOLD}

  # Combined transforms: crop + rotation + mirror
  # MIAF (ISO/IEC 23000-22) specifies the application order: 1. clap, 2. irot, 3. imir.
  "${IMAGEMAGICK}" "${ENCODED_FILE}.png" -crop 100x25+10+50 -rotate -90 -flop "encoded_combined_ref.png"
  "${ARE_IMAGES_EQUAL}" "encoded_combined_ref.png" "encoded_combined.png" 0
  "${ARE_IMAGES_EQUAL}" "encoded_combined_ref.png" "encoded_combined.jpg" 0 ${JPEG_PSNR_THRESHOLD}
popd

exit 0
