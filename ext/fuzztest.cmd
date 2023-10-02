: # If you want to use a local build of fuzztest, you must clone the fuzztest repo in this directory.

: # The odd choice of comment style in this file is to try to share this script between *nix and win32.

: # cmake must be in your PATH.

: # If you're running this on Windows, be sure you've already run this (from your VC2019 install dir):
: #     "C:\Program Files (x86)\Microsoft Visual Studio\2019\Professional\VC\Auxiliary\Build\vcvars64.bat"

git clone https://github.com/google/fuzztest.git
cd fuzztest
: # There is no tagged release as of 2023/09/18. Pick the last commit.
git checkout b5500afbac873c884e1e021ea019ad943434df7d

mkdir build.libavif
cd build.libavif
cmake -G Ninja -DBUILD_SHARED_LIBS=OFF -DCMAKE_POSITION_INDEPENDENT_CODE=ON -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ ..
ninja
cd ../..
