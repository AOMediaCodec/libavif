# libavif

This library aims to be a friendly, portable C implementation of the AV1 Image
File Format, as described here:

<https://aomediacodec.github.io/av1-avif/>

It can encode and decode all AV1 supported YUV formats and bit depths (with
alpha).

It is recommended that you check out/use
[tagged releases](https://github.com/AOMediaCodec/libavif/releases) instead of
just using the main branch. We will regularly create new versions as bug fixes
and features are added.

## Usage

Please see the examples in the "examples" directory. If you're already building
`libavif`, enable the CMake option `AVIF_BUILD_EXAMPLES` in order to build and
run the examples too.

## Installation

`libavif` is a package in most major OSs.

### Windows

```sh
vcpkg install libavif
```
You can also download the official windows binaries on the
[release](https://github.com/AOMediaCodec/libavif/releases) page.

### macOS

Homebrew:
```sh
brew install libavif
```
MacPorts:
```sh
sudo port install libavif
```

### Linux

Debian-based distributions:
```sh
sudo apt install libavif-dev
```
Red Hat-based distributions:
```sh
sudo yum -y install libavif
```

### MinGW

For the "default" MSYS2 UCRT64 environment:
```sh
pacman -S mingw-w64-ucrt-x86_64-libavif
```

## Build Notes

Building libavif requires [CMake](https://cmake.org/).

No AV1 codecs are enabled by default. Enable them by setting any of the
following CMake options to `LOCAL` or `SYSTEM` whether you want to use a
locally built or a system installed version (e.g. `-DAVIF_CODEC_AOM=LOCAL`):

* `AVIF_CODEC_AOM` for [libaom](https://aomedia.googlesource.com/aom/) (encoder
  and decoder)
* `AVIF_CODEC_DAV1D` for [dav1d](https://code.videolan.org/videolan/dav1d)
  (decoder)
* `AVIF_CODEC_LIBGAV1` for
  [libgav1](https://chromium.googlesource.com/codecs/libgav1/) (decoder)
* `AVIF_CODEC_RAV1E` for [rav1e](https://github.com/xiph/rav1e) (encoder)
* `AVIF_CODEC_SVT` for [SVT-AV1](https://gitlab.com/AOMediaCodec/SVT-AV1)
  (encoder)

When set to `SYSTEM`, these libraries (in their C API form) must be externally
available (discoverable via CMake's `FIND_LIBRARY`) to use them, or if libavif
is a child CMake project, the appropriate CMake target must already exist
by the time libavif's CMake scripts are executed.

### Static Builds

When set to `LOCAL`, these libraries and the other dependencies will be pulled
locally by CMake to known-good versions.

To override a local dependency version or to use a custom build of a dependency,
first run the associated script in the `ext/` subdirectory.

### Tests

A few tests written in C can be built by enabling the `AVIF_BUILD_TESTS` CMake
option.

The remaining tests can be built by enabling the `AVIF_BUILD_TESTS` and
`AVIF_ENABLE_GTEST` CMake options. They require GoogleTest
(`-DAVIF_GTEST=SYSTEM` or `-DAVIF_GTEST=LOCAL`).

### Command Lines

The following instructions can be used to build the libavif library and the
`avifenc` and `avifdec` tools.

#### Build using installed dependencies

To link against the already installed `aom`, `libjpeg` and `libpng` dependency
libraries (recommended):

```sh
git clone -b v1.1.1 https://github.com/AOMediaCodec/libavif.git
cmake -S libavif -B libavif/build -DAVIF_CODEC_AOM=SYSTEM -DAVIF_BUILD_APPS=ON
cmake --build libavif/build --parallel
```

#### Build everything from scratch

For development and debugging purposes, or to generate fully static binaries:

```sh
git clone -b v1.1.1 https://github.com/AOMediaCodec/libavif.git
cmake -S libavif -B libavif/build -DBUILD_SHARED_LIBS=OFF -DAVIF_CODEC_AOM=LOCAL -DAVIF_LIBYUV=LOCAL -DAVIF_LIBSHARPYUV=LOCAL -DAVIF_JPEG=LOCAL -DAVIF_ZLIBPNG=LOCAL -DAVIF_BUILD_APPS=ON -DCMAKE_C_FLAGS_RELEASE="-static" -DCMAKE_EXE_LINKER_FLAGS="-static"
cmake --build libavif/build --parallel
```

## Prebuilt Binaries (Windows)

Statically-linked `avifenc.exe` and `avifdec.exe` can be downloaded from the
[Releases](https://github.com/AOMediaCodec/libavif/releases) page.

## Development Notes

Please check the [wiki](https://github.com/AOMediaCodec/libavif/wiki) for extra
resources on libavif, such as the Release Checklist.

The libavif library is written in C99. Most of the tests are written in C++14.

### Formatting

Use [clang-format](https://clang.llvm.org/docs/ClangFormat.html) to format the
sources from the top-level folder (`clang-format-16` preferred):

```sh
clang-format -style=file -i \
  apps/*.c apps/*/*.c apps/*/*.cc apps/*/*.h examples/*.c \
  include/avif/*.h src/*.c src/*.cc \
  tests/*.c tests/*/*.cc tests/*/*.h
```

Use [cmake-format](https://github.com/cheshirekow/cmake_format) to format the
CMakeLists.txt files from the top-level folder:

```sh
cmake-format -i \
  CMakeLists.txt \
  tests/CMakeLists.txt \
  cmake/Modules/*.cmake \
  contrib/CMakeLists.txt \
  contrib/gdk-pixbuf/CMakeLists.txt \
  android_jni/avifandroidjni/src/main/jni/CMakeLists.txt
```

---

## License

Released under the BSD License.

```markdown
Copyright 2019 Joe Drago. All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:

1. Redistributions of source code must retain the above copyright notice, this
list of conditions and the following disclaimer.

2. Redistributions in binary form must reproduce the above copyright notice,
this list of conditions and the following disclaimer in the documentation
and/or other materials provided with the distribution.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
```
