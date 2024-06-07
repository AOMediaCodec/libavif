: # If you want to use a local build of jpeg, you must clone the repos in this directory first,
: # then set CMake's AVIF_JPEG=LOCAL.
: # The git tag below is known to work, and will occasionally be updated. Feel free to use a more recent commit.

: # The odd choice of comment style in this file is to try to share this script between *nix and win32.

git clone -b 3.0.3 --depth 1 https://github.com/libjpeg-turbo/libjpeg-turbo.git

cmake -S libjpeg-turbo -B libjpeg-turbo/build.libavif -G Ninja -DENABLE_SHARED=OFF -DENABLE_STATIC=ON -DCMAKE_BUILD_TYPE=Release -DWITH_TURBOJPEG=OFF
cmake --build libjpeg-turbo/build.libavif --parallel
