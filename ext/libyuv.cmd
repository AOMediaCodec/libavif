: # If you want to use a local build of libyuv, you must clone the libyuv repo in this directory first, then enable CMake's AVIF_LOCAL_LIBYUV option.

: # The odd choice of comment style in this file is to try to share this script between *nix and win32.

: # cmake and ninja must be in your PATH.

: # If you're running this on Windows, be sure you've already run this (from your VC2019 install dir):
: #     "C:\Program Files (x86)\Microsoft Visual Studio\2019\Professional\VC\Auxiliary\Build\vcvars64.bat"

git clone --single-branch https://chromium.googlesource.com/libyuv/libyuv

cd libyuv
git checkout 2871589

mkdir build
cd build

cmake -G Ninja -DBUILD_SHARED_LIBS=0 -DCMAKE_BUILD_TYPE=Release ..
ninja yuv
cd ../..
