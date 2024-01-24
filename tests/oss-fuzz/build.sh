#!/bin/bash -eu
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

# build dependencies
cd ext && bash dav1d.cmd && bash libyuv.cmd && cd ..

# build libavif
mkdir build
cd build
cmake -G Ninja -DBUILD_SHARED_LIBS=OFF \
    -DAVIF_CODEC_DAV1D=ON -DAVIF_LOCAL_DAV1D=ON \
    -DAVIF_LOCAL_LIBYUV=ON -DAVIF_ENABLE_WERROR=ON ..
ninja

# build fuzzer
$CXX $CXXFLAGS -std=c++11 -I../include \
    ../tests/oss-fuzz/avif_decode_fuzzer.cc -o $OUT/avif_decode_fuzzer \
    $LIB_FUZZING_ENGINE libavif.a ../ext/dav1d/build/src/libdav1d.a \
    ../ext/libyuv/build/libyuv.a

# copy seed corpus
cp $SRC/avif_decode_seed_corpus.zip $OUT/
