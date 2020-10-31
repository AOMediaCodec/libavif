# libavif [![AppVeyor Build Status](https://ci.appveyor.com/api/projects/status/github/louquillio/libavif?branch=master&svg=true)](https://ci.appveyor.com/project/louquillio/libavif) [![Travis Build Status](https://travis-ci.com/AOMediaCodec/libavif.svg?branch=master)](https://travis-ci.com/AOMediaCodec/libavif)

This library aims to be a friendly, portable C implementation of the AV1 Image File Format, as described here:

<https://aomediacodec.github.io/av1-avif/>

It is a work-in-progress, but can already encode and decode all AOM supported YUV formats and bit depths (with alpha).

For now, it is recommended that you checkout/use [tagged releases](https://github.com/AOMediaCodec/libavif/releases) instead of just using the master branch. I will regularly create new versions as bugfixes and features are added.

## Usage

Please see the examples in the examples directory. If you're already building `libavif`, enable the CMake option `AVIF_BUILD_EXAMPLES` in order to build and run the examples too.

## Build Notes

Building libavif requires [CMake](https://cmake.org/).

No AV1 codecs are enabled by default. Enable them by enabling any of the
following CMake options:

* `AVIF_CODEC_AOM` - requires CMake, NASM
* `AVIF_CODEC_DAV1D` - requires Meson, Ninja, NASM
* `AVIF_CODEC_LIBGAV1` - requires CMake, Ninja
* `AVIF_CODEC_RAV1E` - requires cargo (Rust), NASM

These libraries (in their C API form) must be externally available
(discoverable via CMake's `FIND_LIBRARY`) to use them, or if libavif is
a child CMake project, the appropriate CMake target must already exist
by the time libavif's CMake scripts are executed.

## Local / Static Builds

The `ext/` subdirectory contains a handful of basic scripts which each pull
down a known-good copy of an AV1 codec and make a local static library build.
If you want to statically link any codec into your local (static) build of
libavif, building using one of these scripts and then enabling the associated
`AVIF_LOCAL_*` is a convenient method, but you must make sure to disable
`BUILD_SHARED_LIBS` in CMake to instruct it to make a static libavif library.

If you want to build/install shared libraries for AV1 codecs, you can still
peek inside of each script to see where the current known-good SHA is for each
codec.

## Prebuilt Library (Windows)

If you're building on Windows with Visual Studio 2019 and want to try out libavif without going through the build process, static library builds for both Debug and Release are available on [AppVeyor](https://ci.appveyor.com/project/louquillio/libavif).

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
