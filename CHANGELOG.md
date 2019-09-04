# Changelog
All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

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

[Unreleased]: https://github.com/AOMediaCodec/libavif/compare/v0.3.8...HEAD
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
