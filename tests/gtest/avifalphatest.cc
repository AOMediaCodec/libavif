// Copyright 2026 Jeongkeun Kim. All rights reserved.
// SPDX-License-Identifier: BSD-2-Clause

#include <cstdint>
#include <cstring>
#include <vector>

#include "avif/internal.h"
#include "aviftest_helpers.h"
#include "gtest/gtest.h"

namespace avif {
namespace {

constexpr size_t kGuardBytes = 32;

// Deterministic, position-dependent (not constant) fill pattern: a bug that
// reads a byte back unchanged, or swaps two neighboring bytes, produces a value
// that still differs from what this function expects at that exact position --
// unlike a constant sentinel, which such bugs could leave looking "correct" by
// accident.
uint8_t PatternByte(size_t absoluteIndex) {
  return (uint8_t)((absoluteIndex * 37u + 11u) & 0xFFu);
}

// A byte buffer pre-filled with PatternByte(), with kGuardBytes of the same
// pattern before and after the region handed to libavif, so any write outside
// the intended region (in either direction) is caught by
// ExpectUnchanged()/GuardsIntact().
class PatternBuffer {
 public:
  explicit PatternBuffer(size_t size) : storage_(size + 2 * kGuardBytes) {
    for (size_t i = 0; i < storage_.size(); ++i) {
      storage_[i] = PatternByte(i);
    }
  }
  uint8_t* data() { return storage_.data() + kGuardBytes; }
  const uint8_t* data() const { return storage_.data() + kGuardBytes; }

  // `offset` is relative to data(), as returned by (ptr - data()).
  uint8_t ExpectedAt(size_t offset) const {
    return PatternByte(kGuardBytes + offset);
  }

  bool GuardsIntact() const {
    for (size_t i = 0; i < kGuardBytes; ++i) {
      if (storage_[i] != PatternByte(i)) return false;
      const size_t tail = storage_.size() - 1 - i;
      if (storage_[tail] != PatternByte(tail)) return false;
    }
    return true;
  }

 private:
  std::vector<uint8_t> storage_;
};

uint32_t ElementBytes(uint32_t depth) { return (depth > 8) ? 2 : 1; }

uint16_t ReadChannel(const uint8_t* p, uint32_t elem) {
  if (elem == 1) return *p;
  uint16_t v;
  memcpy(&v, p, 2);
  return v;
}

void WriteChannel(uint8_t* p, uint32_t elem, uint16_t value) {
  if (elem == 1) {
    *p = (uint8_t)value;
  } else {
    memcpy(p, &value, 2);
  }
}

//------------------------------------------------------------------------------
// avifFillAlpha
//
// Every case below is exercised through the *public* avifFillAlpha() entry
// point, so the NEON dispatch and the scalar fallback are both covered by the
// same assertions: a well-formed layout runs through NEON, and a layout the
// NEON helper rejects (zero pixel size, non-power-of-two-ish interleave factor,
// misaligned offset, out-of-range lane, ...) falls back to the always-correct
// scalar body. Either way, the buffer-level invariants checked here must hold.

struct FillShape {
  uint32_t depth;
  uint32_t pixelBytes;   // avifAlphaParams::dstPixelBytes
  uint32_t offsetBytes;  // avifAlphaParams::dstOffsetBytes
};

struct FillCase {
  FillShape shape;
  uint32_t width;
  uint32_t height;
};

void RunFillCase(const FillCase& c) {
  const uint32_t elem = ElementBytes(c.shape.depth);
  const uint32_t rowPadding =
      5 * (c.shape.pixelBytes == 0 ? elem : c.shape.pixelBytes);
  const uint32_t rowBytes = c.width * c.shape.pixelBytes + rowPadding;

  PatternBuffer buf(static_cast<size_t>(rowBytes) * c.height);

  avifAlphaParams params;
  memset(&params, 0, sizeof(params));
  params.width = c.width;
  params.height = c.height;
  params.dstDepth = c.shape.depth;
  params.dstPlane = buf.data();
  params.dstRowBytes = rowBytes;
  params.dstOffsetBytes = c.shape.offsetBytes;
  params.dstPixelBytes = c.shape.pixelBytes;

  avifFillAlpha(&params);

  const uint16_t maxChannel = (uint16_t)((1u << c.shape.depth) - 1);

  for (uint32_t j = 0; j < c.height; ++j) {
    const size_t rowOffset = (size_t)j * rowBytes;
    for (uint32_t i = 0; i < c.width; ++i) {
      const size_t pixelOffset = rowOffset + (size_t)i * c.shape.pixelBytes;
      // Every byte in [pixelOffset, pixelOffset + pixelBytes) other than the
      // target channel [offsetBytes, offsetBytes + elem) must be untouched --
      // this is a superset of "neighboring interleaved channel preserved" and
      // doesn't rely on assuming a specific interleave factor.
      for (uint32_t b = 0; b < c.shape.pixelBytes; ++b) {
        const size_t byteOffset = pixelOffset + b;
        const uint8_t* bytePtr = buf.data() + byteOffset;
        if (b >= c.shape.offsetBytes && b < c.shape.offsetBytes + elem) {
          continue;  // checked below via ReadChannel
        }
        EXPECT_EQ(*bytePtr, buf.ExpectedAt(byteOffset))
            << "unexpected write at i=" << i << " j=" << j << " byte=" << b;
      }
      const uint8_t* channel = buf.data() + pixelOffset + c.shape.offsetBytes;
      EXPECT_EQ(ReadChannel(channel, elem), maxChannel)
          << "i=" << i << " j=" << j;
    }
    for (uint32_t p = 0; p < rowPadding; ++p) {
      const size_t byteOffset =
          rowOffset + (size_t)c.width * c.shape.pixelBytes + p;
      EXPECT_EQ(buf.data()[byteOffset], buf.ExpectedAt(byteOffset))
          << "row padding corrupted at j=" << j << " p=" << p;
    }
  }
  EXPECT_TRUE(buf.GuardsIntact());
}

std::vector<FillShape> ValidFillShapes(uint32_t depth) {
  const uint32_t elem = ElementBytes(depth);
  return {
      {depth, 1 * elem, 0},                                      // contiguous
      {depth, 2 * elem, 0},        {depth, 2 * elem, 1 * elem},  // factor 2
      {depth, 4 * elem, 0},        {depth, 4 * elem, 1 * elem},  // factor 4
      {depth, 4 * elem, 2 * elem}, {depth, 4 * elem, 3 * elem},
  };
}

class FillAlphaValidLayoutTest : public ::testing::TestWithParam<FillCase> {};

TEST_P(FillAlphaValidLayoutTest, FillsAlphaAndPreservesEverythingElse) {
  RunFillCase(GetParam());
}

std::vector<FillCase> BuildValidFillCases() {
  std::vector<FillCase> cases;
  for (uint32_t depth : {8u, 10u}) {
    for (const FillShape& shape : ValidFillShapes(depth)) {
      for (uint32_t width : {0u, 1u, 7u, 8u, 9u, 15u, 16u, 17u, 33u}) {
        for (uint32_t height : {0u, 1u, 3u}) {
          cases.push_back({shape, width, height});
        }
      }
    }
  }
  return cases;
}

INSTANTIATE_TEST_SUITE_P(ValidLayouts, FillAlphaValidLayoutTest,
                         ::testing::ValuesIn(BuildValidFillCases()));

// All supported bit depths, verifying the fill value itself: (1 << depth) - 1.
class FillAlphaDepthTest : public ::testing::TestWithParam<uint32_t> {};

TEST_P(FillAlphaDepthTest, FillsMaxChannelForDepth) {
  const uint32_t depth = GetParam();
  const uint32_t elem = ElementBytes(depth);
  RunFillCase({{depth, elem, 0}, /*width=*/20, /*height=*/2});
}

INSTANTIATE_TEST_SUITE_P(AllDepths, FillAlphaDepthTest,
                         ::testing::Values(8u, 9u, 10u, 12u, 16u));

// Layouts the NEON helper must reject (one malformed dimension at a time) and
// defer to scalar. depth=10 (elem=2) so pixel-size/offset divisibility is a
// meaningful constraint.
class FillAlphaFallbackTest : public ::testing::TestWithParam<FillShape> {};

TEST_P(FillAlphaFallbackTest, FallsBackToScalarAndIsCorrect) {
  RunFillCase({GetParam(), /*width=*/12, /*height=*/2});
}

INSTANTIATE_TEST_SUITE_P(
    MalformedLayouts, FillAlphaFallbackTest,
    ::testing::Values(FillShape{10, 3,
                                0},  // pixel size not divisible by elem (2)
                      FillShape{10, 4, 1},     // offset not divisible by elem
                      FillShape{10, 6, 0},     // factor 3 (unsupported)
                      FillShape{10, 10, 0}));  // factor 5 (unsupported)

// lane >= factor only arises when offsetBytes >= pixelBytes, i.e. the "channel"
// for pixel i is placed at or past where pixel (i+1) starts -- every write
// aliases into the next pixel's window, so RunFillCase's per-pixel/padding
// framing (which assumes offsetBytes < pixelBytes, true for every real caller)
// doesn't apply. Checked narrowly instead: the NEON validation must reject it
// (lane >= factor) and the scalar fallback must still place each write at
// exactly rowBase + offsetBytes + i * pixelBytes, matching its own documented
// pointer walk.
TEST(FillAlphaFallbackTest, LaneAtOrPastFactorFallsBackToScalarAndIsCorrect) {
  const uint32_t depth = 10;
  const uint32_t pixelBytes = 4;
  const uint32_t offsetBytes = 4;  // == pixelBytes, so lane (2) >= factor (2)
  const uint32_t width = 3;
  const uint32_t rowBytes = width * pixelBytes + offsetBytes +
                            2;  // room for the last (aliased) write

  PatternBuffer buf(rowBytes);
  avifAlphaParams params;
  memset(&params, 0, sizeof(params));
  params.width = width;
  params.height = 1;
  params.dstDepth = depth;
  params.dstPlane = buf.data();
  params.dstRowBytes = rowBytes;
  params.dstOffsetBytes = offsetBytes;
  params.dstPixelBytes = pixelBytes;

  avifFillAlpha(&params);

  const uint16_t maxChannel = (uint16_t)((1u << depth) - 1);
  for (uint32_t i = 0; i < width; ++i) {
    const size_t byteOffset = (size_t)i * pixelBytes + offsetBytes;
    EXPECT_EQ(ReadChannel(buf.data() + byteOffset, 2), maxChannel) << "i=" << i;
  }
}

// dstPixelBytes == 0 has no well-defined "one pixel among many" semantics
// (every pixel index maps to the same byte), so it is checked separately for
// "doesn't crash", not via the buffer-layout matrix above.
TEST(FillAlphaFallbackTest, ZeroPixelBytesDoesNotCrash) {
  avifAlphaParams params;
  memset(&params, 0, sizeof(params));
  uint8_t single = 0;
  params.width = 4;
  params.height = 1;
  params.dstDepth = 8;
  params.dstPlane = &single;
  params.dstRowBytes = 0;
  params.dstOffsetBytes = 0;
  params.dstPixelBytes = 0;
  EXPECT_NO_FATAL_FAILURE(avifFillAlpha(&params));
}

//------------------------------------------------------------------------------
// avifReformatAlpha same-depth copy path
//
// Same principle as above: every case runs through the public
// avifReformatAlpha() entry point.

struct CopyShape {
  uint32_t depth;
  uint32_t srcPixelBytes;
  uint32_t srcOffsetBytes;
  uint32_t dstPixelBytes;
  uint32_t dstOffsetBytes;
};

struct CopyCase {
  CopyShape shape;
  uint32_t width;
  uint32_t height;
};

void RunCopyCase(const CopyCase& c) {
  const uint32_t elem = ElementBytes(c.shape.depth);
  const uint32_t srcRowBytes =
      c.width * c.shape.srcPixelBytes +
      3 * (c.shape.srcPixelBytes == 0 ? elem : c.shape.srcPixelBytes);
  const uint32_t dstRowBytes =
      c.width * c.shape.dstPixelBytes +
      3 * (c.shape.dstPixelBytes == 0 ? elem : c.shape.dstPixelBytes);

  PatternBuffer srcBuf(static_cast<size_t>(srcRowBytes) * c.height);
  PatternBuffer dstBuf(static_cast<size_t>(dstRowBytes) * c.height);

  // Give the source alpha channel a distinctive, position-dependent value
  // (distinct from the background PatternByte() sequence) so a copy that reads
  // the wrong byte is caught, not just "any alpha-like value".
  for (uint32_t j = 0; j < c.height; ++j) {
    uint8_t* row = srcBuf.data() + (size_t)j * srcRowBytes;
    for (uint32_t i = 0; i < c.width; ++i) {
      uint8_t* channel =
          row + (size_t)i * c.shape.srcPixelBytes + c.shape.srcOffsetBytes;
      const uint16_t value =
          (uint16_t)((i * 9 + j * 5 + 3) & ((1u << c.shape.depth) - 1));
      WriteChannel(channel, elem, value);
    }
  }

  avifAlphaParams params;
  memset(&params, 0, sizeof(params));
  params.width = c.width;
  params.height = c.height;
  params.srcDepth = c.shape.depth;
  params.srcPlane = srcBuf.data();
  params.srcRowBytes = srcRowBytes;
  params.srcOffsetBytes = c.shape.srcOffsetBytes;
  params.srcPixelBytes = c.shape.srcPixelBytes;
  params.dstDepth = c.shape.depth;
  params.dstPlane = dstBuf.data();
  params.dstRowBytes = dstRowBytes;
  params.dstOffsetBytes = c.shape.dstOffsetBytes;
  params.dstPixelBytes = c.shape.dstPixelBytes;

  avifReformatAlpha(&params);

  for (uint32_t j = 0; j < c.height; ++j) {
    const size_t srcRowOffset = (size_t)j * srcRowBytes;
    const size_t dstRowOffset = (size_t)j * dstRowBytes;
    for (uint32_t i = 0; i < c.width; ++i) {
      const size_t srcPixelOffset =
          srcRowOffset + (size_t)i * c.shape.srcPixelBytes;
      const size_t dstPixelOffset =
          dstRowOffset + (size_t)i * c.shape.dstPixelBytes;
      const uint8_t* srcChannel =
          srcBuf.data() + srcPixelOffset + c.shape.srcOffsetBytes;

      for (uint32_t b = 0; b < c.shape.dstPixelBytes; ++b) {
        const size_t byteOffset = dstPixelOffset + b;
        if (b >= c.shape.dstOffsetBytes && b < c.shape.dstOffsetBytes + elem) {
          continue;  // checked below via ReadChannel
        }
        EXPECT_EQ(dstBuf.data()[byteOffset], dstBuf.ExpectedAt(byteOffset))
            << "unexpected write at i=" << i << " j=" << j << " byte=" << b;
      }
      const uint8_t* dstChannel =
          dstBuf.data() + dstPixelOffset + c.shape.dstOffsetBytes;
      EXPECT_EQ(ReadChannel(dstChannel, elem), ReadChannel(srcChannel, elem))
          << "i=" << i << " j=" << j;
    }
    for (uint32_t p = 0; p < dstRowBytes - c.width * c.shape.dstPixelBytes;
         ++p) {
      const size_t byteOffset =
          dstRowOffset + (size_t)c.width * c.shape.dstPixelBytes + p;
      EXPECT_EQ(dstBuf.data()[byteOffset], dstBuf.ExpectedAt(byteOffset))
          << "dst row padding corrupted at j=" << j << " p=" << p;
    }
  }
  EXPECT_TRUE(srcBuf.GuardsIntact());
  EXPECT_TRUE(dstBuf.GuardsIntact());
}

std::vector<CopyShape> ValidCopyShapes(uint32_t depth) {
  // Curated to hit every (srcFactor, dstFactor) combination in {1,2,4}^2 at
  // least once, and every valid lane for factor 2 and 4 at least once on each
  // side.
  const uint32_t e = ElementBytes(depth);
  return {
      {depth, e, 0, e, 0},
      {depth, e, 0, 2 * e, 0},
      {depth, e, 0, 2 * e, e},
      {depth, e, 0, 4 * e, 0},
      {depth, e, 0, 4 * e, e},
      {depth, e, 0, 4 * e, 2 * e},
      {depth, e, 0, 4 * e, 3 * e},
      {depth, 2 * e, 0, e, 0},
      {depth, 2 * e, e, e, 0},
      {depth, 4 * e, 0, e, 0},
      {depth, 4 * e, e, e, 0},
      {depth, 4 * e, 2 * e, e, 0},
      {depth, 4 * e, 3 * e, e, 0},
      {depth, 2 * e, 0, 2 * e, e},
      {depth, 2 * e, e, 2 * e, 0},
      {depth, 4 * e, 0, 4 * e, 3 * e},
      {depth, 4 * e, 3 * e, 4 * e, 0},
      {depth, 4 * e, e, 2 * e, 0},
      {depth, 2 * e, e, 4 * e, 2 * e},
  };
}

class ReformatAlphaValidLayoutTest : public ::testing::TestWithParam<CopyCase> {
};

TEST_P(ReformatAlphaValidLayoutTest, CopiesAlphaAndPreservesEverythingElse) {
  RunCopyCase(GetParam());
}

std::vector<CopyCase> BuildValidCopyCases() {
  std::vector<CopyCase> cases;
  for (uint32_t depth : {8u, 10u}) {
    for (const CopyShape& shape : ValidCopyShapes(depth)) {
      for (uint32_t width : {0u, 1u, 7u, 8u, 9u, 15u, 16u, 17u, 33u}) {
        for (uint32_t height : {0u, 1u, 3u}) {
          cases.push_back({shape, width, height});
        }
      }
    }
  }
  return cases;
}

INSTANTIATE_TEST_SUITE_P(ValidLayouts, ReformatAlphaValidLayoutTest,
                         ::testing::ValuesIn(BuildValidCopyCases()));

// Depth mismatch is explicitly outside the NEON copy helper's scope in this PR
// (the rescale paths are deferred); it must fall back to the scalar rescale
// implementation.
TEST(ReformatAlphaFallbackTest, DepthMismatchStillRescalesCorrectly) {
  const uint32_t width = 6;
  const uint32_t height = 1;
  const uint8_t src[width] = {0, 1, 128, 254, 255, 255};
  uint16_t dst[width];
  memset(dst, 0xCD, sizeof(dst));

  avifAlphaParams params;
  memset(&params, 0, sizeof(params));
  params.width = width;
  params.height = height;
  params.srcDepth = 8;
  params.srcPlane = src;
  params.srcRowBytes = width;
  params.srcOffsetBytes = 0;
  params.srcPixelBytes = 1;
  params.dstDepth = 10;
  params.dstPlane = reinterpret_cast<uint8_t*>(dst);
  params.dstRowBytes = width * 2;
  params.dstOffsetBytes = 0;
  params.dstPixelBytes = 2;

  avifReformatAlpha(&params);

  const int dstMax = (1 << 10) - 1;
  for (uint32_t i = 0; i < width; ++i) {
    const float alphaF = (float)src[i] / 255.0f;
    int expected = (int)(0.5f + alphaF * (float)dstMax);
    if (expected < 0) expected = 0;
    if (expected > dstMax) expected = dstMax;
    EXPECT_EQ(dst[i], expected) << "i=" << i;
  }
}

// Layouts the NEON copy helper must reject on ONE side at a time (the other
// side held at a simple, valid contiguous shape) and defer to scalar. depth=10
// (elem=2).
class ReformatAlphaFallbackTest2 : public ::testing::TestWithParam<CopyShape> {
};

TEST_P(ReformatAlphaFallbackTest2, FallsBackToScalarAndIsCorrect) {
  RunCopyCase({GetParam(), /*width=*/12, /*height=*/2});
}

INSTANTIATE_TEST_SUITE_P(
    MalformedOnEitherSide, ReformatAlphaFallbackTest2,
    ::testing::Values(
        // Malformed source side, valid contiguous destination. (Source pixel
        // size 0 is included here: unlike the destination-side case below,
        // every output position ends up reading the same single collapsed
        // source byte, which RunCopyCase's per-pixel check already models
        // correctly -- no aliasing ambiguity on the write side.)
        CopyShape{10, /*srcPixelBytes=*/0, 0, /*dstPixelBytes=*/2,
                  0},  // src zero pixel size
        CopyShape{10, /*srcPixelBytes=*/3, 0, /*dstPixelBytes=*/2,
                  0},  // src size not divisible
        CopyShape{10, /*srcPixelBytes=*/4, 1, /*dstPixelBytes=*/2,
                  0},  // src offset not divisible
        CopyShape{10, /*srcPixelBytes=*/6, 0, /*dstPixelBytes=*/2,
                  0},  // src factor 3
        CopyShape{10, /*srcPixelBytes=*/4, 4, /*dstPixelBytes=*/2,
                  0},  // src lane (2) >= factor (2)
        // Valid contiguous source, malformed destination side.
        CopyShape{10, /*srcPixelBytes=*/2, 0, /*dstPixelBytes=*/3,
                  0},  // dst size not divisible
        CopyShape{10, /*srcPixelBytes=*/2, 0, /*dstPixelBytes=*/4,
                  1},  // dst offset not divisible
        CopyShape{10, /*srcPixelBytes=*/2, 0, /*dstPixelBytes=*/6,
                  0}));  // dst factor 3

// dstLane >= dstFactor only arises when dstOffsetBytes >= dstPixelBytes,
// aliasing each output into the next pixel's window -- same reasoning as
// FillAlphaFallbackTest.LaneAtOrPastFactorFalls... above. Checked narrowly: the
// scalar fallback must still place each copied value at exactly dstRowBase +
// dstOffsetBytes + i * dstPixelBytes.
TEST(ReformatAlphaFallbackTest,
     DstLaneAtOrPastFactorFallsBackToScalarAndIsCorrect) {
  const uint32_t depth = 10;
  const uint32_t width = 3;
  const uint32_t srcPixelBytes = 2;
  const uint32_t dstPixelBytes = 4;
  const uint32_t dstOffsetBytes =
      4;  // == dstPixelBytes, so dstLane (2) >= dstFactor (2)
  const uint32_t srcRowBytes = width * srcPixelBytes;
  const uint32_t dstRowBytes = width * dstPixelBytes + dstOffsetBytes + 2;

  PatternBuffer srcBuf(srcRowBytes);
  PatternBuffer dstBuf(dstRowBytes);
  for (uint32_t i = 0; i < width; ++i) {
    WriteChannel(srcBuf.data() + (size_t)i * srcPixelBytes, 2,
                 (uint16_t)(100 + i));
  }

  avifAlphaParams params;
  memset(&params, 0, sizeof(params));
  params.width = width;
  params.height = 1;
  params.srcDepth = depth;
  params.srcPlane = srcBuf.data();
  params.srcRowBytes = srcRowBytes;
  params.srcOffsetBytes = 0;
  params.srcPixelBytes = srcPixelBytes;
  params.dstDepth = depth;
  params.dstPlane = dstBuf.data();
  params.dstRowBytes = dstRowBytes;
  params.dstOffsetBytes = dstOffsetBytes;
  params.dstPixelBytes = dstPixelBytes;

  avifReformatAlpha(&params);

  for (uint32_t i = 0; i < width; ++i) {
    const size_t byteOffset = (size_t)i * dstPixelBytes + dstOffsetBytes;
    EXPECT_EQ(ReadChannel(dstBuf.data() + byteOffset, 2), 100 + i) << "i=" << i;
  }
}

// dstPixelBytes == 0 has no well-defined "one pixel among many" semantics on
// the write side (every output position aliases the same byte, so only the last
// write in a row survives) -- checked separately for "doesn't crash", not via
// the buffer-layout matrix above.
TEST(ReformatAlphaFallbackTest, DstZeroPixelBytesDoesNotCrash) {
  avifAlphaParams params;
  memset(&params, 0, sizeof(params));
  const uint8_t src[4] = {1, 2, 3, 4};
  uint8_t dst = 0;
  params.width = 4;
  params.height = 1;
  params.srcDepth = 8;
  params.srcPlane = src;
  params.srcRowBytes = 4;
  params.srcOffsetBytes = 0;
  params.srcPixelBytes = 1;
  params.dstDepth = 8;
  params.dstPlane = &dst;
  params.dstRowBytes = 0;
  params.dstOffsetBytes = 0;
  params.dstPixelBytes = 0;
  EXPECT_NO_FATAL_FAILURE(avifReformatAlpha(&params));
}

//------------------------------------------------------------------------------

}  // namespace
}  // namespace avif
