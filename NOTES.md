# Google Patch Rewards — libavif local draft notes

**Date:** 2026-09-09 (America/Chicago)  
**Do not claim yet:** need upstream merge + ≥30 days, then https://bughunters.google.com/report/patch_rewards

## Chosen target + why

- **Project:** libavif (AOMediaCodec/libavif, branch `main`)
- **Upstream:** https://github.com/AOMediaCodec/libavif (GitHub PR)
- **Local clone:** `/workspace/google-patch-libavif` @ `6666395`
- **Branch:** `local/rwdata-fbounds-safety`
- **Why this target:**
  1. Tier-1 **Core infrastructure data parsers** (3× memory-safety multiplier through end-2026).
  2. Clear, mergeable first-CL scope: **one** public buffer+size pair (`avifRWData.data` / `size`) that sits on the untrusted IO/parse path (idat, mergedExtents, file-reader buffer, property payloads, encode/decode samples via `avifRWData`).
  3. Same pattern as the local libpng draft and libwebp demux: **inert macros** when the flag is off; experimental Clang `-fbounds-safety` only when explicitly enabled.
  4. Field order left unchanged (public ABI); only assignment order at update sites is capacity-first.
  5. Narrower than open upstream exploration in https://github.com/AOMediaCodec/libavif/pull/3272 (which also annotated `avifROData` / `properties` and discussed whole-TU `__ptrcheck_abi_assume_unsafe_indexable()`). This draft is intentionally the libpng-sized first step: macros + one annotation + capacity-first + CMake OFF.

## Security benefit

`avifRWData` owns (or aliases) byte buffers used throughout BMFF/AVIF parsing and IO. Callers already track capacity in `size`, but the compiler cannot see that relationship.

This draft:

1. Introduces `include/avif/avif_bounds_safety.h` with `AVIF_COUNTED_BY` / `AVIF_COUNTED_BY_OR_NULL` (empty by default).
2. Annotates `avifRWData.data` with `AVIF_COUNTED_BY_OR_NULL(size)` (nullable; size may be 0).
3. Makes update sites **capacity-then-pointer** (`avifRWDataRealloc`; non-owning `mergedExtents` alias in `read.c`). Free path already nulls the pointer before zeroing size.
4. Wires optional CMake `AVIF_ENABLE_FBOUNDS_SAFETY` (OFF by default) → `-DAVIF_SUPPORT_FBOUNDS_SAFETY` + `-fbounds-safety` (Clang only).

**Default builds are unchanged:** macros expand to nothing; no new runtime checks without the experimental flag.

**Note:** A *real* `-fbounds-safety` enforcing build is a whole-TU contract (every pointer needs a bounds attribute or an unsafe-indexable baseline). That migration is a follow-up; this CL only plants the inert annotation and keeps assignment order correct for when enforcement is enabled later.

## Files changed

| File | Change |
|------|--------|
| `include/avif/avif_bounds_safety.h` | **New** — inert / Clang bounds macros |
| `include/avif/avif.h` | Include macros; annotate `avifRWData.data` |
| `src/rawdata.c` | Capacity-first assign in `avifRWDataRealloc` |
| `src/read.c` | Capacity-first for non-owning `mergedExtents` alias |
| `CMakeLists.txt` | `AVIF_ENABLE_FBOUNDS_SAFETY` option; install new header |
| `NOTES.md` | This file |

## How to build / test

Default (macros inert — must stay green), library only with system libaom:

```sh
cmake -S . -B build-draft \
  -DCMAKE_C_COMPILER=gcc \
  -DBUILD_SHARED_LIBS=OFF \
  -DAVIF_CODEC_AOM=SYSTEM \
  -DAVIF_CODEC_DAV1D=OFF \
  -DAVIF_CODEC_LIBGAV1=OFF \
  -DAVIF_CODEC_RAV1E=OFF \
  -DAVIF_CODEC_SVT=OFF \
  -DAVIF_CODEC_AVM=OFF \
  -DAVIF_LIBYUV=OFF \
  -DAVIF_BUILD_APPS=OFF \
  -DAVIF_BUILD_TESTS=OFF
cmake --build build-draft -j
```

With experimental bounds-safety toolchain (maintainers / CI; **not** available on this box — no Clang/`ptrcheck.h`):

```sh
cmake -S . -B build-fbs -DAVIF_ENABLE_FBOUNDS_SAFETY=ON \
  -DCMAKE_C_COMPILER=<clang-with-fbounds-safety> \
  -DBUILD_SHARED_LIBS=OFF -DAVIF_CODEC_AOM=SYSTEM
cmake --build build-fbs -j
```

## Upstream submit plan

1. Open a focused GitHub PR against `AOMediaCodec/libavif` branch `main` (parent agent as LaptopsPlural; do not push from this draft machine).
2. Proposed title: `rwdata: add optional -fbounds-safety annotation for avifRWData`
3. Frame as secure-by-design / Safe Buffers-style systematization of an existing size+pointer pair; cite libwebp prior art and Google Patch Rewards Tier-1 parser goals.
4. Emphasize: default build behavior unchanged; flag OFF; no PoC / no CVE claim; narrower than #3272.
5. Coordinate with maintainers re: #3272 / whole-TU baseline follow-up.
6. Do **not** claim on https://bughunters.google.com/report/patch_rewards until **merge + ≥30 days**.

## Follow-ups (separate CLs)

- `avifROData.data` / `size` (read-only views on the parse path)
- `avifImage.properties` / `numProperties`
- Whole-TU `__ptrcheck_abi_assume_unsafe_indexable()` baseline so `AVIF_ENABLE_FBOUNDS_SAFETY=ON` actually compiles under the swiftlang/Clang preview toolchain

## Status

**LOCAL DRAFT COMPLETE — no GitHub upload, no claim.**
