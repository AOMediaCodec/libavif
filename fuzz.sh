#!/bin/bash

command -v afl-clang >/dev/null 2>&1 || { echo >&2 "Please install afl-clang."; exit 1; }
command -v afl-fuzz >/dev/null 2>&1 || { echo >&2 "Please install afl-fuzz."; exit 1; }

mkdir build.fuzz
cd build.fuzz
# Enable the "-DAVIF_BUILD_EXAMPLES=1" to get the target fuzz test program.
#  e.g: avif_example_decode_file or avif_example_decode_streaming
CC=afl-clang cmake -G Ninja .. -DAVIF_BUILD_EXAMPLES=1 -DAVIF_CODEC_AOM=0 -DAVIF_BUILD_AOM=0 -DAVIF_CODEC_DAV1D=1 -DAVIF_LOCAL_DAV1D=1 -DAVIF_BUILD_TESTS=1 || exit 1
ninja || exit 1
AFL_EXIT_WHEN_DONE=1 afl-fuzz -t 200 -i ../tests/inputs -o output.$(date "+%Y.%m.%d-%H.%M.%S") ./aviffuzz @@
