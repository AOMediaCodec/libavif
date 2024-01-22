: # If you want to use a local build of fuzztest, you must clone the fuzztest repo in this directory.

: # The odd choice of comment style in this file is to try to share this script between *nix and win32.

: # cmake must be in your PATH.

: # If you're running this on Windows, be sure you've already run this (from your VC2019 install dir):
: #     "C:\Program Files (x86)\Microsoft Visual Studio\2019\Professional\VC\Auxiliary\Build\vcvars64.bat"

git clone https://github.com/google/fuzztest.git
cd fuzztest
: # There is no tagged release as of 2024/01/22. Pick the earliest commit that contains a needed bug fix.
git checkout 8f82f46e14cc2484ddecf6d55e46243923f9a036

: # fuzztest is built by the main CMake project through add_subdirectory as recommended at:
: # https://github.com/google/fuzztest/blob/main/doc/quickstart-cmake.md
