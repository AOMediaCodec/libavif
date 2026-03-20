#!/bin/bash
# Copyright 2023 Google LLC
# SPDX-License-Identifier: BSD-2-Clause
#
# tests for command lines (avifgainmaputil tool)

source $(dirname "$0")/cmd_test_common.sh || exit

if [[ ! -z "$CONFIG" ]]; then
  AVIFGAINMAPUTIL="${BINARY_DIR}/${CONFIG}/avifgainmaputil"
else
  AVIFGAINMAPUTIL="${BINARY_DIR}/avifgainmaputil"
fi

# Input file paths.
INPUT_AVIF_GAINMAP_SDR="${TESTDATA_DIR}/seine_sdr_gainmap_srgb.avif"
INPUT_AVIF_GAINMAP_HDR="${TESTDATA_DIR}/seine_hdr_gainmap_srgb.avif"
INPUT_AVIF_HDR2020="${TESTDATA_DIR}/seine_hdr_rec2020.avif"
INPUT_JPEG_GAINMAP_SDR="${TESTDATA_DIR}/seine_sdr_gainmap_srgb.jpg"
INPUT_AVIF_GAINMAP_SDR_WITH_ICC="${TESTDATA_DIR}/seine_sdr_gainmap_srgb_icc.avif"
# Output file names.
AVIF_OUTPUT="avif_test_cmd_avifgainmaputil_output.avif"
JPEG_OUTPUT="avif_test_cmd_avifgainmaputil_output.jpg"
PNG_OUTPUT="avif_test_cmd_avifgainmaputil_output.png"

# Cleanup
cleanup() {
  rm -f -r "${TMP_DIR}"
}
trap cleanup EXIT

pushd ${TMP_DIR}
  "${AVIFGAINMAPUTIL}" help

  "${AVIFGAINMAPUTIL}" printmetadata "${INPUT_AVIF_GAINMAP_SDR}"

  "${AVIFGAINMAPUTIL}" extractgainmap "${INPUT_AVIF_GAINMAP_SDR}" "${AVIF_OUTPUT}" -q 50
  "${AVIFGAINMAPUTIL}" extractgainmap "${INPUT_AVIF_GAINMAP_SDR}" "${JPEG_OUTPUT}"
  "${AVIFGAINMAPUTIL}" extractgainmap --speed 9 "${INPUT_AVIF_GAINMAP_SDR}" "${PNG_OUTPUT}"

  "${AVIFGAINMAPUTIL}" combine "${INPUT_AVIF_GAINMAP_SDR}" "${INPUT_AVIF_GAINMAP_HDR}" "${AVIF_OUTPUT}" \
      -q 50 --downscaling 2 --yuv-gain-map 400
  "${AVIFGAINMAPUTIL}" combine "${INPUT_JPEG_GAINMAP_SDR}" "${INPUT_AVIF_GAINMAP_HDR}" "${AVIF_OUTPUT}" \
      -q 50 --qgain-map 90 && exit 1 # should fail because icc profiles are not supported
  "${AVIFGAINMAPUTIL}" combine "${INPUT_JPEG_GAINMAP_SDR}" "${INPUT_AVIF_GAINMAP_HDR}" "${AVIF_OUTPUT}" \
      -q 50 --qgain-map 90 --ignore-profile
  "${AVIFGAINMAPUTIL}" combine "${INPUT_AVIF_GAINMAP_SDR}" "${INPUT_AVIF_HDR2020}" "${AVIF_OUTPUT}" \
      -q 50 --downscaling 2 --yuv-gain-map 400 --grid 2x2

  "${AVIFGAINMAPUTIL}" combine "${INPUT_AVIF_GAINMAP_HDR}" "${INPUT_AVIF_GAINMAP_SDR}" "${AVIF_OUTPUT}" \
      -q 90 --qgain-map 90
  "${AVIFGAINMAPUTIL}" tonemap "${AVIF_OUTPUT}" "${PNG_OUTPUT}" --headroom 0
  "${AVIFGAINMAPUTIL}" tonemap "${INPUT_AVIF_GAINMAP_SDR}" "${PNG_OUTPUT}" --headroom 0 --clli 400,500
  "${ARE_IMAGES_EQUAL}" "${PNG_OUTPUT}" "${INPUT_JPEG_GAINMAP_SDR}" 0 40

  # Test combine with overridden cicp values. Matrix coefficient 0 (identity) makes it obvious if there is an issue.
  "${AVIFGAINMAPUTIL}" combine "${INPUT_JPEG_GAINMAP_SDR}" "${INPUT_AVIF_HDR2020}" "${AVIF_OUTPUT}" \
      -q 100 --qgain-map 100 --cicp-base 1/13/0 --ignore-profile
  # Tone map to SDR and compare with original SDR.
  "${AVIFGAINMAPUTIL}" tonemap "${AVIF_OUTPUT}" "${PNG_OUTPUT}" --headroom 0
  "${ARE_IMAGES_EQUAL}" "${PNG_OUTPUT}" "${INPUT_JPEG_GAINMAP_SDR}" 0 99
  # Tone map to HDR and compare with original HDR.
  "${AVIFGAINMAPUTIL}" tonemap "${AVIF_OUTPUT}" "${AVIF_OUTPUT}.tonemapped.png" --headroom 2
  # are_images_equal doesn't support AVIF so we convert to PNG.
  "${AVIFDEC}" "${INPUT_AVIF_HDR2020}" "input_avif_hdr2020.png"
  "${ARE_IMAGES_EQUAL}" "${AVIF_OUTPUT}.tonemapped.png" "input_avif_hdr2020.png" 0 60 # A bit of loss from going through gainmap

  # Same as above but HDR base.
  "${AVIFGAINMAPUTIL}" combine "${INPUT_AVIF_HDR2020}"  "${INPUT_JPEG_GAINMAP_SDR}" "${AVIF_OUTPUT}" \
      -q 100 --qgain-map 100 --cicp-alternate 1/13/0 --ignore-profile
  "${AVIFGAINMAPUTIL}" tonemap "${AVIF_OUTPUT}" "${PNG_OUTPUT}" --headroom 0
  "${ARE_IMAGES_EQUAL}" "${PNG_OUTPUT}" "${INPUT_JPEG_GAINMAP_SDR}" 0 50 # A bit of loss from going through gainmap
  "${AVIFGAINMAPUTIL}" tonemap "${AVIF_OUTPUT}" "${AVIF_OUTPUT}.tonemapped.png" --headroom 2
  # are_images_equal doesn't support AVIF so we convert to PNG.
  "${AVIFDEC}" "${INPUT_AVIF_HDR2020}" "input_avif_hdr2020.png"
  "${ARE_IMAGES_EQUAL}" "${AVIF_OUTPUT}.tonemapped.png" "input_avif_hdr2020.png" 0 90

  "${AVIFGAINMAPUTIL}" swapbase "${INPUT_AVIF_GAINMAP_SDR}" "${AVIF_OUTPUT}" --qcolor 90 --qgain-map 90
  # should fail because icc profiles are not supported
  "${AVIFGAINMAPUTIL}" swapbase "${INPUT_AVIF_GAINMAP_SDR_WITH_ICC}" "${AVIF_OUTPUT}" --qcolor 90 --qgain-map 90 && exit 1
  "${AVIFGAINMAPUTIL}" swapbase "${INPUT_AVIF_GAINMAP_SDR_WITH_ICC}" "${AVIF_OUTPUT}" --qcolor 90 --qgain-map 90 --ignore-profile

   # Also test the are_images_equal binary itself with some gain maps
  "${ARE_IMAGES_EQUAL}" "${INPUT_JPEG_GAINMAP_SDR}" "${INPUT_JPEG_GAINMAP_SDR}" 0 40 0
  "${ARE_IMAGES_EQUAL}" "${INPUT_JPEG_GAINMAP_SDR}" "${INPUT_JPEG_GAINMAP_SDR}" 0 40 1

  # Check if avifgainmaputil was built with libxml2.
  # If it was not, the 'convert' command will fail with an error message
  # containing "libxml2".
  if "${AVIFGAINMAPUTIL}" convert "${INPUT_JPEG_GAINMAP_SDR}" "${AVIF_OUTPUT}" 2>&1 | grep -q "libxml2"; then
    echo "avifgainmaputil was built without libxml2, skipping convert tests."
    popd
    exit 0
  fi
  "${AVIFGAINMAPUTIL}" convert "${INPUT_JPEG_GAINMAP_SDR}" "${AVIF_OUTPUT}"
   # should fail because icc profiles are not supported
  "${AVIFGAINMAPUTIL}" convert "${INPUT_JPEG_GAINMAP_SDR}" "${AVIF_OUTPUT}" --swap-base && exit 1
  "${AVIFGAINMAPUTIL}" convert "${INPUT_JPEG_GAINMAP_SDR}" "${AVIF_OUTPUT}" --swap-base --ignore-profile \
      --cicp 2/3/4
popd

exit 0
