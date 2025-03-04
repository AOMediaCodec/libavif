: # If you want to use a local build of libsharpyuv, you must clone the libwebp repo in this directory first,
: # then set CMake's AVIF_LIBSHARPYUV to LOCAL.

: # The odd choice of comment style in this file is to try to share this script between *nix and win32.

: # cmake must be in your PATH.

: # If you're running this on Windows, be sure you've already run this (from your VC2019 install dir):
: #     "C:\Program Files (x86)\Microsoft Visual Studio\2019\Professional\VC\Auxiliary\Build\vcvars64.bat"

: # libsharpyuv is part of the libwebp repo.
git clone -b v1.4.0 --depth 1 https://chromium.googlesource.com/webm/libwebp

cmake -S libwebp -B libwebp/build -DBUILD_SHARED_LIBS=OFF -DCMAKE_BUILD_TYPE=Release -DWEBP_BUILD_ANIM_UTILS=OFF -DWEBP_BUILD_CWEBP=OFF -DWEBP_BUILD_DWEBP=OFF -DWEBP_BUILD_GIF2WEBP=OFF -DWEBP_BUILD_IMG2WEBP=OFF -DWEBP_BUILD_VWEBP=OFF -DWEBP_BUILD_WEBPINFO=OFF -DWEBP_BUILD_LIBWEBPMUX=OFF -DWEBP_BUILD_WEBPMUX=OFF -DWEBP_BUILD_EXTRAS=OFF
cmake --build libwebp/build --target sharpyuv --parallel
