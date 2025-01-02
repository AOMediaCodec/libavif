: # If you want to use a local build of fuzztest, you must clone the fuzztest repo in this directory.

: # The odd choice of comment style in this file is to try to share this script between *nix and win32.

: # cmake must be in your PATH.

: # If you're running this on Windows, be sure you've already run this (from your VC2019 install dir):
: #     "C:\Program Files (x86)\Microsoft Visual Studio\2019\Professional\VC\Auxiliary\Build\vcvars64.bat"

git clone https://github.com/google/fuzztest.git
cd fuzztest
: # There is no tagged release as of 2024/06/21. Pick the latest commit that works.
git checkout 078ea0871cc96d3a69bad406577f176a4fa14ae9
: # Fixes for https://github.com/google/fuzztest/issues/1124
sed -i 's/-fsanitize=address//g' ./cmake/FuzzTestFlagSetup.cmake
sed -i 's/-DADDRESS_SANITIZER//g' ./cmake/FuzzTestFlagSetup.cmake
: # Fixes for https://github.com/google/fuzztest/issues/1125
sed -i 's/if (IsEnginePlaceholderInput(data)) return;/if (data.size() == 0) return;/g' ./fuzztest/internal/compatibility_mode.cc
sed -i 's/set(GTEST_HAS_ABSL ON)/set(GTEST_HAS_ABSL OFF)/g' ./cmake/BuildDependencies.cmake

: # fuzztest is built by the main CMake project through add_subdirectory as recommended at:
: # https://github.com/google/fuzztest/blob/main/doc/quickstart-cmake.md
