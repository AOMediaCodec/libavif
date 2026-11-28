#!/bin/bash
# Copyright 2026 Yuan Tong. All rights reserved.
# SPDX-License-Identifier: BSD-2-Clause
# ------------------------------------------------------------------------------
#
# Tests loading a custom codec shared library in avifenc and avifdec.
# AVIF_CUSTOM_CODEC_STUB must be set to the path of the avif_custom_codec_stub library.

source $(dirname "$0")/cmd_test_common.sh || exit

# Input file paths. This image is the only input accepted by the stub encoder,
# and the only output of the stub decoder.
INPUT_Y4M="${TESTDATA_DIR}/custom_codec_stub_8x8.y4m"
# Output file names.
ENCODED_FILE="avif_test_cmd_custom_codec_encoded.avif"
DECODED_FILE="avif_test_cmd_custom_codec_decoded.y4m"
OUT_MSG="avif_test_cmd_custom_codec_out_msg.txt"

# Cleanup
cleanup() {
  pushd ${TMP_DIR}
    rm -f -- "${ENCODED_FILE}" "${DECODED_FILE}" "${OUT_MSG}"
  popd
}
trap cleanup EXIT

pushd ${TMP_DIR}
  echo "Testing encoding with a custom codec"
  "${AVIFENC}" --custom-codec "${AVIF_CUSTOM_CODEC_STUB}" -c stub "${INPUT_Y4M}" -o "${ENCODED_FILE}" > "${OUT_MSG}"
  grep -F "Custom codec stub: encoded 1 frame(s), decoded 0 frame(s)" "${OUT_MSG}"

  echo "Testing decoding with a custom codec"
  "${AVIFDEC}" --custom-codec "${AVIF_CUSTOM_CODEC_STUB}" -c stub "${ENCODED_FILE}" "${DECODED_FILE}" > "${OUT_MSG}"
  grep -F "Custom codec stub: encoded 0 frame(s), decoded 1 frame(s)" "${OUT_MSG}"
  "${ARE_IMAGES_EQUAL}" "${INPUT_Y4M}" "${DECODED_FILE}" 0
popd

exit 0
