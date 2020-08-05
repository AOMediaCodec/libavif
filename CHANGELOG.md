# Changelog
All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

## [0.8.1] - 2020-08-05

### Added
* Add `ignoreAlpha` field to avifRGBImage (linkmauve)
* Save support in gdk-pixbuf component (novomesk)

### Changed
* Only ever create one iref box, filled with multiple cdsc boxes (#247)
* Fix incorrect 16-to-8 monochrome YUV conversion
* Make decoding optional in CMake, like encoding is
* Include avif INTERFACE_INCLUDE_DIRECTORIES first (cryptomilk)
* Set C standard to C99, adjust flags for dav1d (1480c1)
* Minor cleanup/fixes in reformat.c (wantehchang)
* Fix a crash in the gdk-pixbuf loader, removed unnecessary asserts (novomesk)

## [0.8.0] - 2020-07-14

### Added
* Monochrome (YUV400) support **
  * All encoding/decoding and internal memory savings are done/functional
  * libaom has a bug in chroma_check() which crashes when encoding monochrome, to be fixed in a future (>v2.0.0) version
  * rav1e didn't implement CS400 until rav1e v0.4.0
  * libavif safely falls back to YUV420 when these earlier codec versions are detected
    * NOTE: If you want to do heavy monochrome testing, wait for newer versions to libaom/rav1e!
* Image sequence encoding support
  * Required medium-sized refactors in the codec layers
  * Image sequences (tracks) now fully support all metadata properly (Exif/XMP/transforms)
  * avifenc can now encode a series of same-sized images with a consistent framerate, or each with their own custom duration
* Bilinear upsampling support
* avifenc: Add --ignore-icc, which avoids embedding the ICC profile found in the source image
* avifdec: Add --info, which attempts to decode all frames and display their basic info (merge of avifdump)
* avifenc: add --tilerowslog2 and --tilecolslog2 (wantehchang)
* Added `contrib` dir for any unofficially supported code contributions (e.g. gdk-pixbuf)

### Changed
* CICP Refactor (breaking change!)
  * Remove most references to "NCLX", as it is mostly an implementation detail, and the values are really from MPEG-CICP
  * Eliminate avifProfileFormat: having an ICC profile is not mutually exclusive with signaling CICP
  * CICP is now always available in an avifImage, set to unspecified by default
  * Added --cicp as an alias for --nclx (semi-deprecated)
  * Setting CICP via avifenc no longer overrides ICC profiles, they co-exist
  * Simplified avifenc argument parsing / warnings logic
  * avifenc/avifdec/avifdump now all display CICP when dumping AVIF information
  * nclx colr box contents are guaranteed to override AV1 bitstream CICP (as MIAF standard specifies)
  * Added comments explaining various decisions and citing standards
  * Removed ICC inspection code regarding chroma-derived mtxCoeffs; this was overdesigned. Now just honor the assoc. colorPrimaries enum
  * Reworked all examples in the README to reflect the new state of things, and clean out some cruft
  * Harvest CICP from AV1 bitstream as a fallback in avifDecoderParse() if nclx box is absent
* All data other than actual pixel data should be available and valid after a call to avifDecoderParse()
* Refactor avifDecoder internal structures to properly handle meta boxes in trak boxes (see avifMeta)
* Update libaom.cmd to point at the v2.0.0 tag
* Update dav1d.cmd to point at the 0.7.1 tag
* Re-enable cpu-used=7+ in codec_aom when libaom major version > 1
* Memory allocation failures now cause libavif to abort the process (rather than undefined behavior)
* Fix to maintain alpha range when decoding an image grid with alpha
* Improvements to avifyuv to show drift when yuv and rgb depths differ
* Remove any references to (incorrect) "av01" brand (wantehchang)
* Set up libaom to use reduced_still_picture_header (wantehchang)
* Use libaom cpu_used 6 in "good quality" usage mode (wantehchang)
* Update avifBitsReadUleb128 with latest dav1d code (wantehchang)
* Set encoder chroma sample position (wantehchang)

## [0.7.3] - 2020-05-04
### Added
- avifenc: Lossless (--lossless, -l) mode, which sets new defaults and warns when anything would cause the AVIF to not be lossless

### Changed
- Minor cleanup for -Wclobbered warnings
- Minor fixes to README and code (fallout from enum rework)
- Protect against oversized (out of bounds) samples in avif sample tables
- Optimization: avoid AV1 sample copying when feeding data to dav1d

## [0.7.2] - 2020-04-24
### Added
- Recognize extensions with capital letters / capslock
- Proper support for AVIF_NCLX_MATRIX_COEFFICIENTS_IDENTITY

### Changed
- Large nclx enum refactor (breaking change), reworking all 3 enums to better match AV1 codec enums
- Fixes to 'essential' item properties (marking av1C as essential, ignoring any items containing unsupported essential props)
- avifenc - Allow --nclx to override embedded ICC profiles (with a warning), instead of --nclx being ignored
- avifenc - Choose high-quality-but-lossy QP defaults, and a default speed of 8
- avifdump - Fix format specifiers for 32bit
- Now prioritizing libaom over rav1e when both are present
- Remove `-Wclobbered` dodging (volatile) and instead just disable the warning in avifpng/avifjpeg
- avifyuv: extra testing modes
- Cleanup to avifCodecVersions()
- Reorganize iccjpeg code back into its own files for licensing conveniences

## [0.7.1] - 2020-04-16
### Changed
- avifenc: Set nclx/range values in avifImage earlier so proper YUV coefficients are used when converting JPEG/PNG

## [0.7.0] - 2020-04-16
### Added
- avifenc and avifdec JPEG support
- Docker test script to build avifenc + deps in a shared libs (distro-like) env
- Added simple `avifdump` tool for aiding in AVIF debugging
- Added some comments in `avif.h` to clarify `avifDecoderSetSource()` usage

### Changed
- avifRange cleanup/refactor (breaking change)
- avifenc now has `-r` to set YUV range (when using JPEG/PNG), `--nclx` now takes 3 arguments as a result

## [0.6.4] - 2020-04-14
### Added
- Added `avifDecoderNthImageTiming()` for querying frame timing without needing to decode the frame
- Added some comments explaining `avifDecoderSetSource()`

### Changed
- Fix clang warning (switch clamp to min)
- Fix a few clang analyzer issues
- Avoid incorrect YUV range cast
- Call dav1d_data_unref in dav1dCodecDestroyInternal (wantehchang)
- Declare some avifSampleTable * pointers as const (wantehchang)
- Update to cJSON v1.7.13 (wantehchang)
- Minor code cleanup

## [0.6.3] - 2020-03-30
### Changed
- Avoid throwing away const unnecessarily in `avifROStreamReadString()`
- Re-enable a bunch of clang warnings
- Set dav1dSettings.frame_size_limit to avoid OOM (wantehchang)
- Refactor write.c to use a similar Data/Item design as read.c
- YUV to RGB optimizations

## [0.6.2] - 2020-03-11
### Changed
- Fix 16bpc PNG output
- Compile fixes to avoid -Wclobbered in PNG code
- GitHub automatic deployment from AppVeyor (EwoutH)

## [0.6.1] - 2020-03-11
### Added
- PNG support for avifenc/avifdec

### Changed
- Fixed Clang10 build warning
- Fix SOVERSION in cmake (cryptomilk)
- Minor tweaks to avifBool usage (wantehchang)

## [0.6.0] - 2020-03-09
### Added
- `avifRGBImage` structure and associated routines (BREAKING CHANGE)
- avifImage alphaRange support
- Support pasp, clap, irot, imir metadata for encode/decode

### Changed
- Large RGB conversion refactor (BREAKING CHANGE), see README for new examples
- Minor fixes to make Clang 10 happy
- pkg-config fixes
- Lots of minor cleanup in code/CMake (wantehchang)
- Fix to NCLX color profile plumbing (ledyba-z)
- Cleanup unnecessary avifBool ternary expressions
- Update default dav1d version to 0.6.0
- Update default rav1e version to v0.3.1

## [0.5.7] - 2020-03-03
### Added
- libgav1 decode codec support. (wantehchang @Google)
- Expose codec selection to avifdec/avifenc, speed to avifenc
- Image grid support (Summer_in_Tomsk_720p_5x4_grid)
- `minQuantizerAlpha`/`maxQuantizerAlpha` support in avifEncoder, avifenc
- 444alpha support in y4m layer (avifenc, avifdec)
- pkg-config support (cryptomilk)
- Proper support of NCLX matrix coefficients enum (link-u)

### Changed
- AppVeyor builds now compile with dav1d (EwoutH)
- Lots of minor CMake/code cleanup (wantehchang @Google)
- cJSON license note for aviftest (wantehchang @Google)

## [0.5.6] - 2020-02-19
### Added
- Added CMake Find modules for aom, dav1d, rav1e (cryptomilk)

### Changed
- use right-most and bottom-most UV pixels in images with odd-dimensions (ledyba-z)
- avoid libaom crash when encoding >8bpc images at high speed

## [0.5.5] - 2020-02-13
### Added
- Enable still picture mode with rav1e >= 0.3.0 (cryptomilk)
- Basic test suite (aviftest, rough draft)

### Changed
- Explicitly cast unorms to float during YUV conversion, fixing clang warning
- Optimize SampleSizeBox parsing when sample_size>0, fixes OOM oss-fuzz issue #5192805347753984
- Fix memory leak when using avifDecoderReset(), fixes oss-fuzz issue #5770230506979328
- Update default rav1e version from 0.2.1 to 0.3.0
- Remove a null check for codec->internal->image (wantehchang)

## [0.5.4] - 2020-01-21
### Changed
- Fix monochrome inputs on avifImageCopy. Monochrome still isn't really a first-class citizen in libavif, but this should at least honor the incoming data better.
- Updated README's Basic Decoding section reminding of avifDecoderRead's tradeoffs
- build: avoid -ldl if not required or not supported (jbeich)
- apps: convert ADVANCE to an expression (jbeich)

## [0.5.3] - 2019-12-03
### Added
- Honor CMake's builtin `CMAKE_SKIP_INSTALL_RULES`

### Changed
- avifenc - Removed accidental double-delete of avifImage when failing to read a y4m file input
- Round dimensions down when decoding subsampled YUV with odd dimensions

## [0.5.2] - 2019-11-23
### Changed
- Fix incorrect free in 0-case for `avifRWDataSet()`

## [0.5.1] - 2019-11-21
### Changed
- Fix expectations for Exif payload to better match normal usage

## [0.5.0] - 2019-11-21
### Added
- Define version and SO-version for shared library
- Use -DBUILD_SHARED_LIBS=OFF for building a static lib
- avifImage can now hold Exif and XMP metadata (`avifImageSetMetadataExif`, `avifImageSetMetadataXMP`)
- Support for reading/writing Exif and XMP items
- Now tracking idat boxes across meta boxes
- Support for iloc construction_method 1 (idat)

### Changed
- Proper handling of the primary item box (pitm) on read
- avifROStreamReadString() now allows string skipping by passing a NULL output buffer
- Updated README to show Exif/XMP support

## [0.4.8] - 2019-11-19
### Added
- avifEncoder now has a speed setting
- codec_aom only flushes encoder when necessary (avoids lost frame packets)
- shared library compilation (build shared by default, use `-DAVIF_BUILD_STATIC=1` for static lib)
- make install support
- cmake fixes/support for find_package (cryptomilk)

### Changed
- Updated libaom to more recent SHA in aom.cmd
- Tweaked AVIF_LOCAL_AOM settings to play nice with libaom's usage of CMake's option()
- Remove all libaom special cases from libavif's CMakefiles, and have it work the same way dav1d and rav1e do
- Minor cleanup

## [0.4.7] - 2019-11-11
### Changed
- Fix memory leak in rav1e codec (PR20, AurelC2G)
- Bump rav1e version in rav1e.cmd, implement `avifCodecVersionRav1e()`
- Display versions in avifenc and avifdec

## [0.4.6] - 2019-10-30
### Changed
- Fix rav1e build on Linux x64, and eliminate pseudo-dependency on cargo-c

## [0.4.5] - 2019-10-30
### Changed
- Fix rav1e codec's alpha encoding (monochrome asserts, might be unsupported still)

## [0.4.4] - 2019-10-30
### Changed
- Fix QP range for rav1e encodes (rav1e uses [0-255], not [0-63])
- Distribute out and share code populating av01 config box across codecs

## [0.4.3] - 2019-10-28
### Added
- rav1e codec support (encode-only)
- `rav1e.cmd` and `dav1d.cmd` to ext

### Changed
- All codecs can coexist peacefully now, and can be queried for availability or specifically chosen at encode/decode time
- Updated README to indicate changes to CMake which facilitate codec reorg

## [0.4.2] - 2019-10-17
### Changed
- Populate nclx box inside of OBU in addition to AVIF container

## [0.4.1] - 2019-10-17
### Added
- Added `containerDepth` to avifDecoder for surfacing 10bpc/12bpc flags from av1C boxes, if present
- Added `avifCodecVersions()` for getting version strings of internal AV1 codecs

### Changed
- Fixed warning with CHECK macro (additional semicolon)

## [0.4.0] - 2019-10-02
### Added
- exposed util functions: `avifFullToLimitedY`, `avifFullToLimitedUV`, `avifLimitedToFullY`, `avifLimitedToFullUV`, `avifPrepareReformatState`

### Changed
- Renamed ispeWidth/ispeHeight to containerWidth/containerHeight; they now can hold tkhd's width/height
- Split avifImageYUVToRGB into faster internal functions (estimated gain: 3.5x)
- Fixed a few memory leaks, one in the README, one in codec_dav1d (AurelC2G)

## [0.3.11] - 2019-09-26
### Added
- Exposed ispeWidth/ispeHeight to decoder if decoding items with an associated ispe box
- Now parsing/tracking sample description formats to filter non-av01 type tracks
- Allow brand 'av01' to be decoded

### Changed
- Fixed bug in sync sample table element sizing
- Pass through starting sample index to codec when flushing with NthImage

## [0.3.10] - 2019-09-26
### Added
- stss box parsing for keyframe information
- avifBool avifDecoderIsKeyframe(avifDecoder * decoder, uint32_t frameIndex);
- uint32_t avifDecoderNearestKeyframe(avifDecoder * decoder, uint32_t frameIndex);
- avifResult avifDecoderNthImage(avifDecoder * decoder, uint32_t frameIndex);
- aviffuzz prints keyframe information as it repeatedly decodes

### Changed
- internally renamed codec function "decode" to "open", as that's all it does
- dav1d codec's open function no longer does an initial unnecessary feed
- avifCodecDecodeInput now stores an array of avifSample which know if they're keyframes
- moved codec flushing code into avifDecoderFlush() so it is available to avifDecoderNthImage
- ptsInTimescales is now calculated independently of frame decode order

## [0.3.9] - 2019-09-25
### Changed
- Split avifRawData and avifStream into read-only (const) and read/write versions, updated code accordingly
- Fix a few clang/macOS warnings

## [0.3.8] - 2019-09-04
### Changed
- Reverted codec_aom and libaom to use previous SHA (v1.0.0-errata1 is ancient)

## [0.3.7] - 2019-09-04 - *DO NOT USE THIS VERSION*
### Added
- Check for proper width/height/depth when decoding alpha with dav1d, matching libaom's impl

### Changed
- Updated codec_aom and libaom to use v1.0.0-errata1

## [0.3.6] - 2019-07-25
### Added
- Exposed tile encoding to avifEncoder

## [0.3.5] - 2019-07-25
### Changed
- Fixed copypasta bug in libaom encoding quantizer setup

## [0.3.4] - 2019-07-25
### Added
- When the AVIF container does not contain a color profile, fallback to the color OBU's nclx

## [0.3.3] - 2019-07-24
### Added
- new helper function `avifPeekCompatibleFileType()`
- expose ioStats on avifDecoder again (currently only interesting when reading items)

### Changed
- Fixed some warnings (removed unused variables and a bad cast)
- Add a define in dav1d layer for supporting older dav1d codecs
- Enabled tons of warnings, and warnings-as-errors; Fixed associated fallout
- codec_dav1d: disambiguate "needs more data" and "no more frames" in feed data pump

## [0.3.2] - 2019-07-23
### Added
- Added `ext/aom.cmd` to perform a local checkout of the aom codebase, as an alternative to a real submodule. This allows downstream projects to use libavif without recursive submodule issues.
- AppVeyor and Travis scripts now explicitly clone libaom into ext/ as an alternative to a submodule.

### Changed
- Remove `ext/aom` as a submodule. If libavif users want to build aom from ext/, they must enable `AVIF_BUILD_AOM` and supply their own local copy.
- Move the handful of public domain gb_math functions used by colr.c and eliminate the dependence on the gb library
- Detect when libaom or libdav1d is being included by a parent CMake project and allow it
- Offer libavif's include dir alongside the library in CMake (target_include_directories)

## [0.3.1] - 2019-07-22
### Changed
- Moved dependency on libm to avif executables, instead of directly on the library
- Minor changes to README examples

## [0.3.0] - 2019-07-22
### Added
- new CMake option `AVIF_CODEC_AOM` to enable/disable the usage of AOM's codec (default: on)
- new CMake option `AVIF_CODEC_DAV1D` to enable/disable the usage of dav1d's codec (default: off)
- `codec_dav1d.c`, which provides decoding via `libdav1d`
- fuzz.sh which builds with afl-clang and runs afl-fuzz
- aviffuzz tool, used in fuzzing script
- fuzz inputs made with colorist
- `.clang-format` file
- `avifArray*()` functions for basic dynamic arrays when parsing
- `moov` box parsing
- now reads 'avis' brands
- Split avifDecoderRead() into components for image sequences:
  - avifDecoderSetSource()
  - avifDecoderParse()
  - avifDecoderNextImage()
  - avifImageCopy()
  - avifDecoderReset()
- Added decoder and image timings for image sequences

### Changed
- Reorganized internal struct avifCodec to accomodate multiple codecs simultaneously (compile time; not exposed to API)
- Fix some compiler warnings
- Sanity check offsets and sizes in items table before using
- Bail out of box header advertises an impossible size
- Ran clang-format on all of src and include
- Fix copypasta leading to a memory leak in RGB planes
- Switched items and properties during parse to use dynamic arrays
- Refactored codec API to not require each codec to maintain per-plane decoder instances
- avifImage can now "not own" its planes and directly point at decoder planes to avoid copies
- aviffuzz attempts to decode all images in source material twice (using avifDecoderReset())
- Switch decoder->quality to explicit [minQuantizer, maxQuantizer], update assoc. constants
- Add examples to README

## [0.2.0] - 2019-06-12
### Added
- Added `avifEncoder` and `avifDecoder` to match `avifImage`'s pattern and allow for easier future parameterization

### Changed
- Renamed project in cmake to `libavif` to match new official repo naming
- Updated appveyor script to use `libavif`
- Updated examples and apps to use new encoder/decoder pattern

## [0.1.4] - 2019-06-11
### Added
- `avifPixelFormatToString()` convenience function for debugging/printing
- `avifenc` and `avifdec` "apps" which show basic bidirectional conversion to y4m

### Changed
- Make calling `avifImageYUVToRGB()` upon reading an avif optional
- Moved `ext/aom` submodule to use official remote
- Update `ext/aom` submodule to commit [38711e7fe](https://aomedia.googlesource.com/aom/+/38711e7fe1eff68296b0324a9809804aec359fa5)

### Removed
- Remove all calls to `convertXYZToXYY()` as they were all unnecessary

## [0.1.3] - 2019-04-23
### Changed
- `ftyp` - Change `major_brand` to `avif`
- `ftyp` - Reorder `compatible_brands`, add `MA1A` or `MA1B` when appropriate
- Write `meta` box before `mdat` box for streaming friendliness

## [0.1.2] - 2019-04-18
### Added
- `AVIF_NCLX_COLOUR_PRIMARIES_P3` (convenient mirrored value)
- `avifNclxColourPrimariesFind()` - Finds a builtin avifNclxColourPrimaries and name by a set of primaries

### Changed
- Fixed enum name copypasta for `AVIF_NCLX_COLOUR_PRIMARIES_EG432_1`
- Fix UV limited ranges when doing full<->limited range conversion

## [0.1.1] - 2019-04-15
### Added
- Added `appveyor.yml` (exported from Appveyor)
- Move `ext/aom` to a proper submodule
- Update AOM to commit [3e3b9342a](https://aomedia.googlesource.com/aom/+/3e3b9342a20147ec6e4f89aa290e20277c1260ce) with minor CMake changes

### Changed
- Added static library artifact zip to Windows x64 builds (Appveyor)
- Updated README to explain libavif's goals and a little more build info
- Fix clang warning in `avifVersion()` signature

## [0.1.0] - 2019-04-12
### Added
- First version. Plenty of bugfixes and features await!
- `ext/aom` based off AOM commit [3563b12b](https://aomedia.googlesource.com/aom/+/3563b12b766639ba445eb0e62a225a4419594aef) with minor CMake changes
- An interest and willingness to maintain this file.
- Constants `AVIF_VERSION`, `AVIF_VERSION_MAJOR`, `AVIF_VERSION_MINOR`, `AVIF_VERSION_PATCH`
- `avifVersion()` function

[Unreleased]: https://github.com/AOMediaCodec/libavif/compare/v0.8.1...HEAD
[0.8.1]: https://github.com/AOMediaCodec/libavif/compare/v0.8.0...v0.8.1
[0.8.0]: https://github.com/AOMediaCodec/libavif/compare/v0.7.3...v0.8.0
[0.7.3]: https://github.com/AOMediaCodec/libavif/compare/v0.7.2...v0.7.3
[0.7.2]: https://github.com/AOMediaCodec/libavif/compare/v0.7.1...v0.7.2
[0.7.1]: https://github.com/AOMediaCodec/libavif/compare/v0.7.0...v0.7.1
[0.7.0]: https://github.com/AOMediaCodec/libavif/compare/v0.6.4...v0.7.0
[0.6.4]: https://github.com/AOMediaCodec/libavif/compare/v0.6.3...v0.6.4
[0.6.3]: https://github.com/AOMediaCodec/libavif/compare/v0.6.2...v0.6.3
[0.6.2]: https://github.com/AOMediaCodec/libavif/compare/v0.6.1...v0.6.2
[0.6.1]: https://github.com/AOMediaCodec/libavif/compare/v0.6.0...v0.6.1
[0.6.0]: https://github.com/AOMediaCodec/libavif/compare/v0.5.7...v0.6.0
[0.5.7]: https://github.com/AOMediaCodec/libavif/compare/v0.5.6...v0.5.7
[0.5.6]: https://github.com/AOMediaCodec/libavif/compare/v0.5.5...v0.5.6
[0.5.5]: https://github.com/AOMediaCodec/libavif/compare/v0.5.4...v0.5.5
[0.5.4]: https://github.com/AOMediaCodec/libavif/compare/v0.5.3...v0.5.4
[0.5.3]: https://github.com/AOMediaCodec/libavif/compare/v0.5.2...v0.5.3
[0.5.2]: https://github.com/AOMediaCodec/libavif/compare/v0.5.1...v0.5.2
[0.5.1]: https://github.com/AOMediaCodec/libavif/compare/v0.5.0...v0.5.1
[0.5.0]: https://github.com/AOMediaCodec/libavif/compare/v0.4.8...v0.5.0
[0.4.8]: https://github.com/AOMediaCodec/libavif/compare/v0.4.7...v0.4.8
[0.4.7]: https://github.com/AOMediaCodec/libavif/compare/v0.4.6...v0.4.7
[0.4.6]: https://github.com/AOMediaCodec/libavif/compare/v0.4.5...v0.4.6
[0.4.5]: https://github.com/AOMediaCodec/libavif/compare/v0.4.4...v0.4.5
[0.4.4]: https://github.com/AOMediaCodec/libavif/compare/v0.4.3...v0.4.4
[0.4.3]: https://github.com/AOMediaCodec/libavif/compare/v0.4.2...v0.4.3
[0.4.2]: https://github.com/AOMediaCodec/libavif/compare/v0.4.1...v0.4.2
[0.4.1]: https://github.com/AOMediaCodec/libavif/compare/v0.4.0...v0.4.1
[0.4.0]: https://github.com/AOMediaCodec/libavif/compare/v0.3.11...v0.4.0
[0.3.11]: https://github.com/AOMediaCodec/libavif/compare/v0.3.10...v0.3.11
[0.3.10]: https://github.com/AOMediaCodec/libavif/compare/v0.3.9...v0.3.10
[0.3.9]: https://github.com/AOMediaCodec/libavif/compare/v0.3.8...v0.3.9
[0.3.8]: https://github.com/AOMediaCodec/libavif/compare/v0.3.7...v0.3.8
[0.3.7]: https://github.com/AOMediaCodec/libavif/compare/v0.3.6...v0.3.7
[0.3.6]: https://github.com/AOMediaCodec/libavif/compare/v0.3.5...v0.3.6
[0.3.5]: https://github.com/AOMediaCodec/libavif/compare/v0.3.4...v0.3.5
[0.3.4]: https://github.com/AOMediaCodec/libavif/compare/v0.3.3...v0.3.4
[0.3.3]: https://github.com/AOMediaCodec/libavif/compare/v0.3.2...v0.3.3
[0.3.2]: https://github.com/AOMediaCodec/libavif/compare/v0.3.1...v0.3.2
[0.3.1]: https://github.com/AOMediaCodec/libavif/compare/v0.3.0...v0.3.1
[0.3.0]: https://github.com/AOMediaCodec/libavif/compare/v0.2.0...v0.3.0
[0.2.0]: https://github.com/AOMediaCodec/libavif/compare/v0.1.4...v0.2.0
[0.1.4]: https://github.com/AOMediaCodec/libavif/compare/v0.1.3...v0.1.4
[0.1.3]: https://github.com/AOMediaCodec/libavif/compare/v0.1.2...v0.1.3
[0.1.2]: https://github.com/AOMediaCodec/libavif/compare/v0.1.1...v0.1.2
[0.1.1]: https://github.com/AOMediaCodec/libavif/compare/v0.1.0...v0.1.1
[0.1.0]: https://github.com/AOMediaCodec/libavif/releases/tag/v0.1.0
