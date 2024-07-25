#!/bin/bash
# Copyright 2020 Google Inc.
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
#
################################################################################

# This script is meant to be run by the oss-fuzz infrastructure from the script
# https://github.com/google/oss-fuzz/blob/master/projects/libavif/build.sh
# It builds the different fuzz targets.

# To test changes to this file:
# - make changes and commit to your REPO
# - run:
#     git clone --depth=1 git@github.com:google/oss-fuzz.git
#     cd oss-fuzz
# - modify projects/libavif/Dockerfile to point to your REPO
# - run:
#     python3 infra/helper.py build_image libavif
#     # enter 'y' and wait for everything to be downloaded
# - run:
#     python3 infra/helper.py build_fuzzers --sanitizer address libavif
#     # wait for the tests to be built
# And then run the fuzzer locally, for example:
#     python3 infra/helper.py run_fuzzer libavif \
#     avif_fuzztest_enc_dec_incr@EncodeDecodeAvifFuzzTest.EncodeDecodeGridValid \
#     --sanitizer address

set -eu

# Build dav1d with sanitizer flags.
# Adds extra flags: -Db_sanitize=$SANITIZER -Db_lundef=false, and -Denable_asm=false for msan
DAV1D_EXTRA_FLAGS=""
if [[ "$SANITIZER" != "coverage" && "$SANITIZER" != "introspector" ]]; then
  DAV1D_EXTRA_FLAGS="${DAV1D_EXTRA_FLAGS} -Db_sanitize=$SANITIZER -Db_lundef=false"
fi
if [[ "$SANITIZER" == "memory" ]]; then
  DAV1D_EXTRA_FLAGS="${DAV1D_EXTRA_FLAGS} -Denable_asm=false"
fi
sed -i "s/meson setup \(.*\) \.\./meson setup \1${DAV1D_EXTRA_FLAGS} ../g" ./ext/dav1d.cmd

# Build libaom with sanitizer flags.
# Adds extra flags: -DAOM_TARGET_CPU=generic for msan.
if [[ "$SANITIZER" == "memory" ]]; then
  sed -i 's/cmake \(.*\) \.\./cmake \1 -DAOM_TARGET_CPU=generic ../g' ./ext/aom.cmd
fi

# Build libjpeg-turbo with sanitizer flags. Add extra flag -DWITH_SIMD=0 for msan.
# See https://github.com/libjpeg-turbo/libjpeg-turbo/blob/main/README.md#memory-debugger-pitfalls
if [[ "$SANITIZER" == "memory" ]]; then
  sed -i 's/cmake -S libjpeg-turbo \(.*\)/cmake -S libjpeg-turbo \1 -DWITH_SIMD=0/g' ./ext/libjpeg.cmd
fi

# Prepare dependencies.
cd ext && bash aom.cmd && bash dav1d.cmd && bash fuzztest.cmd && bash libjpeg.cmd &&
      bash libsharpyuv.cmd && bash libyuv.cmd && bash zlibpng.cmd && cd ..

# build libavif
mkdir build
cd build
EXTRA_CMAKE_FLAGS=""
if [[ "$FUZZING_ENGINE" == "libfuzzer" ]]; then
  CXXFLAGS="${CXXFLAGS} -DFUZZTEST_COMPATIBILITY_MODE"
  EXTRA_CMAKE_FLAGS="${EXTRA_CMAKE_FLAGS} -DAVIF_ENABLE_FUZZTEST=ON -DFUZZTEST_COMPATIBILITY_MODE=libfuzzer"
fi
cmake .. -G Ninja -DBUILD_SHARED_LIBS=OFF -DAVIF_CODEC_AOM=LOCAL -DAVIF_CODEC_DAV1D=LOCAL \
      -DAVIF_CODEC_AOM_DECODE=ON -DAVIF_CODEC_AOM_ENCODE=ON \
      -DAVIF_LOCAL_FUZZTEST=ON \
      -DAVIF_JPEG=LOCAL -DAVIF_LIBSHARPYUV=LOCAL \
      -DAVIF_LIBYUV=LOCAL -DAVIF_ZLIBPNG=LOCAL \
      -DAVIF_BUILD_TESTS=ON -DAVIF_ENABLE_GTEST=OFF -DAVIF_ENABLE_WERROR=ON \
      ${EXTRA_CMAKE_FLAGS}

ninja

# Restrict fuzztest tests to the only compatible fuzz engine: libfuzzer.
if [[ "$FUZZING_ENGINE" == "libfuzzer" ]]; then
  # build fuzztests
  # The following is taken from https://github.com/google/oss-fuzz/blob/31ac7244748ea7390015455fb034b1f4eda039d9/infra/base-images/base-builder/compile_fuzztests.sh#L59
  # Iterate the fuzz binaries and list each fuzz entrypoint in the binary. For
  # each entrypoint create a wrapper script that calls into the binaries the
  # given entrypoint as argument.
  # The scripts will be named:
  # {binary_name}@{fuzztest_entrypoint}
  FUZZ_TEST_BINARIES_OUT_PATHS=$(ls ./tests/avif_fuzztest_*)
  echo "Fuzz binaries: $FUZZ_TEST_BINARIES_OUT_PATHS"
  for fuzz_main_file in $FUZZ_TEST_BINARIES_OUT_PATHS; do
    FUZZ_TESTS=$($fuzz_main_file --list_fuzz_tests | cut -d ' ' -f 4)
    cp -f ${fuzz_main_file} $OUT/
    fuzz_basename=$(basename $fuzz_main_file)
    chmod -x $OUT/$fuzz_basename
    for fuzz_entrypoint in $FUZZ_TESTS; do
      TARGET_FUZZER="${fuzz_basename}@$fuzz_entrypoint"
      # Write executer script
      echo "#!/bin/sh
# LLVMFuzzerTestOneInput for fuzzer detection.
this_dir=\$(dirname \"\$0\")
export TEST_DATA_DIRS=\$this_dir/corpus
chmod +x \$this_dir/$fuzz_basename
\$this_dir/$fuzz_basename --fuzz=$fuzz_entrypoint -- \$@
chmod -x \$this_dir/$fuzz_basename" > $OUT/$TARGET_FUZZER
      chmod +x $OUT/$TARGET_FUZZER
    done
  done
fi

# create a bigger seed corpus for avif_decode_fuzzer
cp $SRC/avif_decode_seed_corpus.zip $OUT/avif_decode_fuzzer_seed_corpus.zip
zip -j $OUT/avif_decode_fuzzer_seed_corpus.zip \
  $(find $SRC/libavif/tests/data -maxdepth 1 -type f)
# copy seed corpus for fuzztest tests
unzip $OUT/avif_decode_fuzzer_seed_corpus.zip -d $OUT/corpus
