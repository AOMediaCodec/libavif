REM If you want to use a local build of jpeg, you must clone the repos in this directory first,
REM then set CMake's AVIF_JPEG=LOCAL.
REM The git tag below is known to work, and will occasionally be updated. Feel free to use a more recent commit.

REM The odd choice of comment style in this file is to try to share this script between *nix and win32.

git clone -b 3.1.4.1 --depth 1 https://github.com/libjpeg-turbo/libjpeg-turbo.git

REM Set WITH_CRT_DLL to ON to compile libjpeg-turbo with /MD (use the DLL
REM version of the run-time library) instead of /MT (use the static version
REM of the run-time library) on Windows. On non-Windows platform, this causes
REM a CMake warning, which is safe to ignore:
REM   Manually-specified variables were not used by the project:
REM
REM     WITH_CRT_DLL
cmake -G Ninja -S libjpeg-turbo -B libjpeg-turbo/build.libavif -DENABLE_SHARED=OFF -DENABLE_STATIC=ON -DCMAKE_BUILD_TYPE=Release -DWITH_TURBOJPEG=OFF -DWITH_CRT_DLL=ON
cmake --build libjpeg-turbo/build.libavif --config Release --parallel
