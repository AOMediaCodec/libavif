: # If you want to use a local build of libgav1, you must clone the libgav1 repo in this directory first, then enable CMake's AVIF_CODEC_LIBGAV1 and AVIF_LOCAL_LIBGAV1 options.
: # The git SHA below is known to work, and will occasionally be updated. Feel free to use a more recent commit.

: # The odd choice of comment style in this file is to try to share this script between *nix and win32.

: # cmake and ninja must be in your PATH.

: # If you're running this on Windows, be sure you've already run this (from your VC2017 install dir):
: #     "C:\Program Files (x86)\Microsoft Visual Studio\2017\Professional\VC\Auxiliary\Build\vcvars64.bat"

git clone --single-branch https://chromium.googlesource.com/codecs/libgav1

cd libgav1
git checkout 45a1d76
git clone https://github.com/abseil/abseil-cpp.git third_party/abseil-cpp
mkdir build
cd build

cmake -G Ninja -DCMAKE_BUILD_TYPE=Release -DLIBGAV1_THREADPOOL_USE_STD_MUTEX=1 ..
ninja
cd ../..
