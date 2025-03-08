#!/bin/bash
# Copyright 2024 Google LLC
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
# Try all test configurations, typically before a release. This script should be
# executed from the root of a fresh local clone of the libavif repository, for
# example (Unix only):
#   git clone https://github.com/AOMediaCodec/libavif.git
#   cd libavif
#   tests/test_all_configurations.sh

set -eu

rm -rf build_*

for BUILD_TYPE in Debug Release; do
  for EXPERIMENTAL in ON OFF; do
    mkdir build_${BUILD_TYPE}_exp${EXPERIMENTAL}
    pushd build_${BUILD_TYPE}_exp${EXPERIMENTAL}
      cmake .. \
       -DAVIF_ENABLE_NODISCARD=ON \
       -DAVIF_ENABLE_WERROR=ON \
       -DAVIF_ENABLE_EXPERIMENTAL_MINI=${EXPERIMENTAL} \
       -DAVIF_ENABLE_EXPERIMENTAL_SAMPLE_TRANSFORM=${EXPERIMENTAL} \
       -DAVIF_ENABLE_EXPERIMENTAL_EXTENDED_PIXI=${EXPERIMENTAL} \
       -DAVIF_BUILD_APPS=ON -DAVIF_BUILD_EXAMPLES=ON \
       -DAVIF_BUILD_TESTS=ON -DAVIF_GTEST=LOCAL \
       -DAVIF_CODEC_AOM=LOCAL -DAVIF_CODEC_DAV1D=LOCAL \
       -DAVIF_LIBYUV=LOCAL -DAVIF_LIBSHARPYUV=LOCAL \
       -DCMAKE_BUILD_TYPE=${BUILD_TYPE} -DBUILD_SHARED_LIBS=OFF
      cmake --build . --parallel
      ctest . -j $(nproc)
    popd

    # The memory sanitizer triggers too many use-of-uninitialized-value errors
    # (possibly false positives because standard libraries were not instrumented).
    for SANITIZER in address thread undefined; do
      mkdir build_${BUILD_TYPE}_exp${EXPERIMENTAL}_${SANITIZER}
      pushd build_${BUILD_TYPE}_exp${EXPERIMENTAL}_${SANITIZER}
        # TODO: This does not apply to the dav1d dependency which is built with meson. Fix.
        CC=clang CXX=clang++ CFLAGS=-fsanitize=${SANITIZER} CXXFLAGS=-fsanitize=${SANITIZER} LDFLAGS=-fsanitize=${SANITIZER} \
        cmake .. \
         -DAVIF_ENABLE_NODISCARD=ON \
         -DAVIF_ENABLE_WERROR=ON \
         -DAVIF_ENABLE_EXPERIMENTAL_MINI=${EXPERIMENTAL} \
         -DAVIF_ENABLE_EXPERIMENTAL_SAMPLE_TRANSFORM=${EXPERIMENTAL} \
         -DAVIF_ENABLE_EXPERIMENTAL_EXTENDED_PIXI=${EXPERIMENTAL} \
         -DAVIF_BUILD_APPS=ON -DAVIF_BUILD_EXAMPLES=ON \
         -DAVIF_BUILD_TESTS=ON -DAVIF_GTEST=LOCAL \
         -DAVIF_CODEC_AOM=LOCAL -DAVIF_CODEC_DAV1D=LOCAL \
         -DAVIF_LIBYUV=LOCAL -DAVIF_LIBSHARPYUV=LOCAL \
         -DCMAKE_BUILD_TYPE=${BUILD_TYPE} -DBUILD_SHARED_LIBS=OFF
        cmake --build . --parallel
        ASAN_OPTIONS=allocator_may_return_null=1:detect_odr_violation=0 \
        TSAN_OPTIONS=allocator_may_return_null=1 \
        ctest . -j $(nproc)
      popd
    done
  done
done

# TODO: Run disabled tests with CMAKE_BUILD_TYPE=Release only (too slow otherwise).
# GTEST_ALSO_RUN_DISABLED_TESTS=1 ctest . -j $(nproc)