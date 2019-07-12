# libavif [![AppVeyor Build Status](https://ci.appveyor.com/api/projects/status/github/joedrago/avif?branch=master&svg=true)](https://ci.appveyor.com/project/joedrago/avif) [![Travis Build Status](https://travis-ci.com/joedrago/avif.svg?branch=master)](https://travis-ci.com/joedrago/avif)

This library aims to be a friendly, portable C implementation of the AV1 Image File Format, as described here:

https://aomediacodec.github.io/av1-avif/

It is a work-in-progress, but can already encode and decode all AOM supported YUV formats and bit depths (with alpha).

For now, it is recommended that you checkout/use [tagged releases](https://github.com/AOMediaCodec/libavif) instead of just using the master branch. I will regularly create new versions as bugfixes and features are added.

# Build Notes

Building libavif requires [NASM](https://nasm.us/) and [CMake](https://cmake.org/).

Make sure nasm is available and in your PATH on your machine, then use CMake to do a basic build (Debug or Release).

# Prebuilt Library (Windows)

If you're building on Windows with VS2017 and want to try out libavif without going through the build process, static library builds for both Debug and Release are available on [Appveyor](https://ci.appveyor.com/project/joedrago/avif).

---

# License

Released under the BSD License.

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
