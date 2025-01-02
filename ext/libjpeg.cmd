: # If you want to use a local build of jpeg, you must clone the repos in this directory first,
: # then set CMake's AVIF_JPEG=LOCAL.
: # The git tag below is known to work, and will occasionally be updated. Feel free to use a more recent commit.

: # The odd choice of comment style in this file is to try to share this script between *nix and win32.

git clone -b 3.0.4 --depth 1 https://github.com/libjpeg-turbo/libjpeg-turbo.git

# Set WITH_CRT_DLL to ON to compile libjpeg-turbo with /MD (use the DLL
# version of the run-time library) instead of /MT (use the static version
# of the run-time library) on Windows. On non-Windows platform, this causes
# a CMake warning, which is safe to ignore:
#   Manually-specified variables were not used by the project:
#
#     WITH_CRT_DLL
cmake -S libjpeg-turbo -B libjpeg-turbo/build.libavif -G Ninja -DENABLE_SHARED=OFF -DENABLE_STATIC=ON -DCMAKE_BUILD_TYPE=Release -DWITH_TURBOJPEG=OFF -DWITH_CRT_DLL=ON
cmake --build libjpeg-turbo/build.libavif --parallel
