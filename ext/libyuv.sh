# If you want to use a local build of libyuv, you must clone the libyuv repo in this directory first,
# then enable CMake's AVIF_LOCAL_LIBYUV option.
# cmake and ninja must be in your PATH.

git clone --single-branch https://chromium.googlesource.com/libyuv/libyuv

cd libyuv
git checkout 2871589

mkdir build
cd build

cmake -G Ninja -DBUILD_SHARED_LIBS=0 -DCMAKE_BUILD_TYPE=Release ..
ninja yuv
cd ../..
