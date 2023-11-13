#!/bin/bash
# Copyright 2023 Google LLC
# SPDX-License-Identifier: BSD-2-Clause
#
# tests for command lines (avifgainmaputil tool)

# Very verbose but useful for debugging.
set -ex

if [[ "$#" -ge 1 ]]; then
  # eval so that the passed in directory can contain variables.
  BINARY_DIR="$(eval echo "$1")"
else
  # Assume "tests" is the current directory.
  BINARY_DIR="$(pwd)/.."
fi
if [[ "$#" -ge 2 ]]; then
  TESTDATA_DIR="$(eval echo "$2")"
else
  TESTDATA_DIR="$(pwd)/data"
fi
if [[ "$#" -ge 3 ]]; then
  TMP_DIR="$(eval echo "$3")"
else
  TMP_DIR="$(mktemp -d)"
fi

AVIFGAINMAPUTIL="${BINARY_DIR}/avifgainmaputil"

# Input file paths.
INPUT_AVIF_GAINMAP_SDR="${TESTDATA_DIR}/seine_sdr_gainmap_srgb.avif"
INPUT_AVIF_GAINMAP_HDR="${TESTDATA_DIR}/seine_hdr_gainmap_srgb.avif"
JPEG_AVIF_GAINMAP_SDR="${TESTDATA_DIR}/seine_sdr_gainmap_srgb.jpg"
# Output file names.
AVIF_OUTPUT="avif_test_cmd_avifgainmaputil_output.avif"
JPEG_OUTPUT="avif_test_cmd_avifgainmaputil_output.jpg"
PNG_OUTPUT="avif_test_cmd_avifgainmaputil_output.png"

# Cleanup
cleanup() {
  pushd ${TMP_DIR}
    rm -- "${AVIF_OUTPUT}" "${JPEG_OUTPUT}" "${PNG_OUTPUT}"
  popd
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
  "${AVIFGAINMAPUTIL}" combine "${JPEG_AVIF_GAINMAP_SDR}" "${INPUT_AVIF_GAINMAP_HDR}" "${AVIF_OUTPUT}" \
      -q 50 --qgain-map 90 && exit 1 # should fail because of icc profiles are not supported
  "${AVIFGAINMAPUTIL}" combine "${JPEG_AVIF_GAINMAP_SDR}" "${INPUT_AVIF_GAINMAP_HDR}" "${AVIF_OUTPUT}" \
      -q 50 --qgain-map 90 --ignore-profile
popd

exit 0
