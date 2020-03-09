# Changelog
All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]
### Added
- `avifImageYUVToInterleaved*()` functions for converting directly into a pre-existing buffer, skipping temp rgbPlanes creation
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

[Unreleased]: https://github.com/AOMediaCodec/libavif/compare/v0.5.7...HEAD
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
