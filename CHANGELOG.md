# Changelog
All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

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

[Unreleased]: https://github.com/joedrago/avif/compare/v0.1.4...HEAD
[0.1.4]: https://github.com/joedrago/avif/compare/v0.1.3...v0.1.4
[0.1.3]: https://github.com/joedrago/avif/compare/v0.1.2...v0.1.3
[0.1.2]: https://github.com/joedrago/avif/compare/v0.1.1...v0.1.2
[0.1.1]: https://github.com/joedrago/avif/compare/v0.1.0...v0.1.1
[0.1.0]: https://github.com/joedrago/avif/releases/tag/v0.1.0
