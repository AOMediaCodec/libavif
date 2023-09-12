#!/bin/bash
# Copyright 2023 Google LLC
# SPDX-License-Identifier: BSD-2-Clause
#
# Common code for tests that compares the structure (boxes) of AVIF files
# metadata, dumped as xml, with golden files stored in tests/data/goldens/.
# Depends on the command line tool MP4Box.
#
# To use this, export a function called 'encode_test_files', then
# 'source' this file.
# The paths to avifenc and to the testdata dir will be passed as arguments.
# See 'test_cmd_enc_boxes_golden.sh' for an example.

set -e

if [[ "$#" -ge 1 ]]; then
  # eval so that the passed in directory can contain variables.
  ENCODER_DIR="$(eval echo "$1")"
else
  # Assume "tests" is the current directory.
  ENCODER_DIR="$(pwd)/.."
fi
if [[ "$#" -ge 2 ]]; then
  # eval so that the passed in directory can contain variables.
  MP4BOX_DIR="$(eval echo "$2")"
else
  # Assume "tests" is the current directory.
  MP4BOX_DIR="$(pwd)/../ext/gpac/bin/gcc"
fi
if [[ "$#" -ge 3 ]]; then
  TESTDATA_DIR="$(eval echo "$3")"
else
  TESTDATA_DIR="$(pwd)/data"
fi
if [[ "$#" -ge 4 && ! -z "$4" ]]; then
  OUTPUT_DIR="$(eval echo "$4")/test_cmd_enc_boxes_golden"
else
  OUTPUT_DIR="$(mktemp -d)"
fi

GOLDEN_DIR="${TESTDATA_DIR}/goldens"
AVIFENC="${ENCODER_DIR}/avifenc"
MP4BOX="${MP4BOX_DIR}/MP4Box"

if [[ ! -x "$AVIFENC" ]]; then
    echo "'$AVIFENC' does not exist or is not executable"
    exit 1
fi
if [[ ! -x "$MP4BOX" ]]; then
    echo "'$MP4BOX' does not exist or is not executable, build it by running ext/mp4box.sh"
    exit 1
fi

mkdir -p "$OUTPUT_DIR"
echo "Outputting to $OUTPUT_DIR"

has_errors=false

# Cleanup
cleanup() {
    # Only delete temp files if the test succeeded, to allow debugging in case or error.
    if ! $has_errors; then
        find ${OUTPUT_DIR} -type f \( -name '*.avif' -o -name '*.xml' -o -name '*.xml.diff' \) -delete
    fi
}
trap cleanup EXIT

# Replaces parts of the xml that are brittle with 'REDACTED', typically because they depend
# on the size of the encoded pixels.
redact_xml() {
  local f="$1"

  # Remove item data offsets and size so that the xml is stable even if the encoded size changes.
  # The first 2 regexes are for the iloc box, the next 2 are for the mdat box.
  # Note that "-i.bak" works on both Linux and Mac https://stackoverflow.com/a/22084103
  sed -i.bak -e 's/extent_offset="[0-9]*"/extent_offset="REDACTED"/g' \
             -e 's/extent_length="[0-9]*"/extent_length="REDACTED"/g' \
             -e 's/dataSize="[0-9]*"/dataSize="REDACTED"/g' \
             -e 's/<MediaDataBox\(.*\) Size="[0-9]*"/<MediaDataBox\1 Size="REDACTED"/g' \
             "$f"
  # For animations.
  sed -i.bak -e 's/CreationTime="[0-9]*"/CreationTime="REDACTED"/g' \
             -e 's/ModificationTime="[0-9]*"/ModificationTime="REDACTED"/g' \
             -e 's/<Sample\(.*\) size="[0-9]*"/<Sample\1 size="REDACTED"/g' \
             -e 's/<SampleSizeEntry\(.*\) Size="[0-9]*"/<SampleSizeEntry\1 Size="REDACTED"/g' \
             -e 's/<OBU\(.*\) size="[0-9]*"/<OBU\1 size="REDACTED"/g' \
             -e 's/<Tile\(.*\) size="[0-9]*"/<Tile\1 size="REDACTED"/g' \
            "$f"
  rm "$f.bak"
}

diff_with_goldens() {
    echo "==="

    for f in $(find . -name '*.avif'); do
        echo "Testing $f"
        xml="$(basename "$f").xml" # Remove leading ./ for prettier paths.
        # Dump the file structure as XML
        "${MP4BOX}" -dxml -out "$xml"  "$f"
        redact_xml "$xml"

        # Compare with golden.
        golden="$GOLDEN_DIR/$xml"
        if [[ -f "$golden" ]]; then
            golden_differs=false
            diff -u "$golden" "$xml" > "$xml.diff" || golden_differs=true
            if $golden_differs; then
                has_errors=true
                echo "FAILED: Differences found for file $xml, see details in $OUTPUT_DIR/$xml.diff" >&2
                echo "Expected file: $golden" >&2
                echo "Actual file: $OUTPUT_DIR/$xml" >&2
                cp "$golden" "$xml.golden" # Copy golden to output directory for easier debugging.
            else
                rm "$xml.diff"  # Delete empty diff files.
                echo "Passed"
            fi
        else
            echo "FAILED:  Missing golden file $golden" >&2
            echo "Actual file: $OUTPUT_DIR/$xml" >&2
            has_errors=true
        fi

        echo "---"

    done
    echo "==="
}

pushd "${OUTPUT_DIR}"
    set -x # Print the encoding command lines.
    # Expect this function to be exported by the calling script.
    encode_test_files "${AVIFENC}" "${TESTDATA_DIR}"
    set +x

    diff_with_goldens
popd > /dev/null

if $has_errors; then
  cat >&2 << EOF
Test failed.

# IF RUNNING ON GITHUB
  Check the workflow step called "How to fix failing tests" for instructions on how to debug/fix this test.

# IF RUNNING LOCALLY
  Look at the .xml.diff files in $OUTPUT_DIR

  If the diffs are expected, update the golden files with:
  cp "$OUTPUT_DIR"/*.xml "$GOLDEN_DIR"
EOF

    cat > "$OUTPUT_DIR/README.txt" << EOF
To debug test failures, look at the .xml.diff files.

If the diffs are expected, update the golden files by copying the .xml files, e.g.:
cp *.xml $HOME/git/libavif/tests/data/goldens

You can also debug the test locally by setting AVIF_ENABLE_GOLDEN_TESTS=ON in cmake.
EOF

    exit 1
fi

exit 0
