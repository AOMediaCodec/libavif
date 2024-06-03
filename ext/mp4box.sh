#!/bin/bash
#
# Local build of MP4Box (part of the gpac project) for tests.

set -e

git clone https://github.com/gpac/gpac.git

cd gpac
git checkout 5d70253 # GPAC 2.4.0

./configure --static-bin
make -j
# MP4Box is in ext/gpac/bin/gcc/MP4Box

cd ..
