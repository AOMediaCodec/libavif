// Copyright 2023 Google LLC
// SPDX-License-Identifier: BSD-2-Clause

#include <cmath>
#include <cstdint>

#include "avif/avif_cxx.h"
#include "avif/internal.h"
#include "gtest/gtest.h"

namespace avif {
namespace {

// Converts a double value to a fraction, and checks that the difference
// between fraction.n/fraction.d and v is below relative_tolerance.
void TestRoundTrip(double v, double relative_tolerance) {
  // Unsigned.
  if (v >= 0) {
    avifUnsignedFraction fraction;
    ASSERT_TRUE(avifDoubleToUnsignedFraction(v, &fraction)) << v;
    const double reconstructed = (double)fraction.n / fraction.d;
    const double tolerance = v * relative_tolerance;
    EXPECT_NEAR(reconstructed, v, tolerance)
        << "fraction.n " << (double)fraction.n << " fraction.d "
        << (double)fraction.d;
  }

  // Signed.
  if (v <= INT32_MAX) {
    for (double multiplier : {1.0, -1.0}) {
      double v2 = v * multiplier;
      avifSignedFraction fraction;

      ASSERT_TRUE(avifDoubleToSignedFraction(v2, &fraction)) << v2;
      const double reconstructed = (double)fraction.n / fraction.d;
      const double tolerance = v * relative_tolerance;
      EXPECT_NEAR(reconstructed, v2, tolerance)
          << "fraction.n " << (double)fraction.n << " fraction.d "
          << (double)fraction.d;
    }
  }
}

constexpr double kLotsOfDecimals = 0.14159265358979323846;

TEST(ToFractionTest, RoundTrip) {
  // Whole numbers and simple fractions should match perfectly.
  constexpr double kPerfectTolerance = 0.0;
  TestRoundTrip(0.0, kPerfectTolerance);
  TestRoundTrip(1.0, kPerfectTolerance);
  TestRoundTrip(42.0, kPerfectTolerance);
  TestRoundTrip(102356.0, kPerfectTolerance);
  TestRoundTrip(102356456.0f, kPerfectTolerance);
  TestRoundTrip(UINT32_MAX / 2.0, kPerfectTolerance);
  TestRoundTrip((double)UINT32_MAX - 1.0, kPerfectTolerance);
  TestRoundTrip((double)UINT32_MAX, kPerfectTolerance);
  TestRoundTrip(0.123, kPerfectTolerance);
  TestRoundTrip(1.0 / 3.0, kPerfectTolerance);
  TestRoundTrip(1.0 / 4.0, kPerfectTolerance);
  TestRoundTrip(3.0 / 23.0, kPerfectTolerance);
  TestRoundTrip(1253456.456, kPerfectTolerance);
  TestRoundTrip(8598533.9, kPerfectTolerance);

  // Numbers with a lot of decimals or very large/small can show a small
  // error.
  constexpr double kSmallTolerance = 1e-9;
  TestRoundTrip(0.0123456, kSmallTolerance);
  TestRoundTrip(3 + kLotsOfDecimals, kSmallTolerance);
  TestRoundTrip(sqrt(2.0), kSmallTolerance);
  TestRoundTrip(exp(1.0), kSmallTolerance);
  TestRoundTrip(exp(10.0), kSmallTolerance);
  TestRoundTrip(exp(15.0), kSmallTolerance);
  // The golden ratio, the irrational number that is the "most difficult" to
  // approximate rationally according to Wikipedia.
  const double kGoldenRatio = (1.0 + std::sqrt(5.0)) / 2.0;
  TestRoundTrip(kGoldenRatio, kSmallTolerance);  // Golden ratio.
  TestRoundTrip(((double)UINT32_MAX) - 0.5, kSmallTolerance);
  // Note that values smaller than this might have a larger relative error
  // (e.g. 1.0e-10).
  TestRoundTrip(4.2e-10, kSmallTolerance);
}

// Tests the max difference between the fraction-ified value and the original
// value, for a subset of values between 0.0 and UINT32_MAX.
TEST(ToFractionTest, MaxDifference) {
  double max_error = 0;
  double max_error_v = 0;
  double max_relative_error = 0;
  double max_relative_error_v = 0;
  for (uint64_t i = 0; i < UINT32_MAX; i += 1000) {
    const double v = i + kLotsOfDecimals;
    avifUnsignedFraction fraction;
    ASSERT_TRUE(avifDoubleToUnsignedFraction(v, &fraction)) << v;
    const double reconstructed = (double)fraction.n / fraction.d;
    const double error = abs(reconstructed - v);
    const double relative_error = error / v;
    if (error > max_error) {
      max_error = error;
      max_error_v = v;
    }
    if (relative_error > max_relative_error) {
      max_relative_error = relative_error;
      max_relative_error_v = v;
    }
  }
  EXPECT_LE(max_error, 0.5f) << max_error_v;
  EXPECT_LT(max_relative_error, 1e-9) << max_relative_error_v;
}

// Tests the max difference between the fraction-ified value and the original
// value, for a subset of values between 0 and 1.0/UINT32_MAX.
TEST(ToFractionTest, MaxDifferenceSmall) {
  double max_error = 0;
  double max_error_v = 0;
  double max_relative_error = 0;
  double max_relative_error_v = 0;
  for (uint64_t i = 1; i < UINT32_MAX; i += 1000) {
    const double v = 1.0 / (i + kLotsOfDecimals);
    avifUnsignedFraction fraction;
    ASSERT_TRUE(avifDoubleToUnsignedFraction(v, &fraction)) << v;
    const double reconstructed = (double)fraction.n / fraction.d;
    const double error = abs(reconstructed - v);
    const double relative_error = error / v;
    if (error > max_error) {
      max_error = error;
      max_error_v = v;
    }
    if (relative_error > max_relative_error) {
      max_relative_error = relative_error;
      max_relative_error_v = v;
    }
  }
  EXPECT_LE(max_error, 1e-10) << max_error_v;
  EXPECT_LT(max_relative_error, 1e-5) << max_relative_error_v;
}

TEST(ToFractionTest, BadValues) {
  avifUnsignedFraction fraction;
  // Negative value.
  EXPECT_FALSE(avifDoubleToUnsignedFraction(-0.1, &fraction));
  // Too large.
  EXPECT_FALSE(
      avifDoubleToUnsignedFraction(((double)UINT32_MAX) + 1.0, &fraction));
}

TEST(ExifTest, TiffHeaderOffsetShortAndBoundaryPayloads) {
  size_t offset = 99u;
  EXPECT_EQ(avifGetExifTiffHeaderOffset(nullptr, 0, &offset),
            AVIF_RESULT_INVALID_EXIF_PAYLOAD);
  EXPECT_EQ(offset, 0u);

  const uint8_t kPartialBigEndian[3] = {'M', 'M', 0};
  for (size_t len = 0; len <= sizeof(kPartialBigEndian); ++len) {
    offset = 99u;
    EXPECT_EQ(avifGetExifTiffHeaderOffset(kPartialBigEndian, len, &offset),
              AVIF_RESULT_INVALID_EXIF_PAYLOAD);
    EXPECT_EQ(offset, 0u);
  }

  const uint8_t kExactBigEndian[4] = {'M', 'M', 0, 42};
  ASSERT_EQ(avifGetExifTiffHeaderOffset(kExactBigEndian,
                                        sizeof(kExactBigEndian), &offset),
            AVIF_RESULT_OK);
  EXPECT_EQ(offset, 0u);

  const uint8_t kExactLittleEndian[4] = {'I', 'I', 42, 0};
  ASSERT_EQ(avifGetExifTiffHeaderOffset(kExactLittleEndian,
                                        sizeof(kExactLittleEndian), &offset),
            AVIF_RESULT_OK);
  EXPECT_EQ(offset, 0u);

  const uint8_t kOffsetOneBigEndian[5] = {0x00, 'M', 'M', 0, 42};
  ASSERT_EQ(avifGetExifTiffHeaderOffset(kOffsetOneBigEndian,
                                        sizeof(kOffsetOneBigEndian), &offset),
            AVIF_RESULT_OK);
  EXPECT_EQ(offset, 1u);

  const uint8_t kTrailingPartialMagic[6] = {0, 0, 0, 'M', 'M', 0};
  EXPECT_EQ(avifGetExifTiffHeaderOffset(kTrailingPartialMagic,
                                        sizeof(kTrailingPartialMagic), &offset),
            AVIF_RESULT_INVALID_EXIF_PAYLOAD);
}

TEST(ExifTest, OrientationOffsetRejectsHeaderOverlap) {
  // Valid minimal big-endian payload with 0th IFD immediately after the 8-byte
  // TIFF header (offsetTo0thIfd == 8) and orientation == 6.
  const uint8_t kValidBigEndian[] = {
      'M',  'M',  0, 42,  // TIFF magic
      0,    0,    0, 8,   // offsetTo0thIfd == 8
      0,    1,            // fieldCount == 1
      0x01, 0x12,         // tag == Orientation (0x0112)
      0,    3,            // type == SHORT (3)
      0,    0,    0, 1,   // count == 1
      0,    6,    0, 0    // value == 6
  };
  size_t offset = 0;
  ASSERT_EQ(avifGetExifOrientationOffset(kValidBigEndian,
                                         sizeof(kValidBigEndian), &offset),
            AVIF_RESULT_OK);
  ASSERT_LT(offset, sizeof(kValidBigEndian));
  EXPECT_EQ(kValidBigEndian[offset], 6u);

  // Valid minimal little-endian payload with 4-byte leading preamble and
  // offsetTo0thIfd == 8, orientation == 3.
  const uint8_t kValidLittleEndian[] = {
      0,    0,    0,  0,  // Leading Exif preamble
      'I',  'I',  42, 0,  // TIFF magic
      8,    0,    0,  0,  // offsetTo0thIfd == 8
      1,    0,            // fieldCount == 1
      0x12, 0x01,         // tag == Orientation (0x0112)
      3,    0,            // type == SHORT (3)
      1,    0,    0,  0,  // count == 1
      3,    0,    0,  0   // value == 3
  };
  ASSERT_EQ(avifGetExifOrientationOffset(kValidLittleEndian,
                                         sizeof(kValidLittleEndian), &offset),
            AVIF_RESULT_OK);
  ASSERT_LT(offset, sizeof(kValidLittleEndian));
  EXPECT_EQ(kValidLittleEndian[offset], 3u);

  // Any 0th IFD offset < 8 overlaps the 8-byte TIFF header itself and must be
  // rejected rather than rewinding into the header bytes.
  for (uint8_t bad_ifd_offset = 0; bad_ifd_offset < 8; ++bad_ifd_offset) {
    const uint8_t kHeaderOnlyBigEndian[8] = {
        'M', 'M', 0, 42, 0, 0, 0, bad_ifd_offset,
    };
    EXPECT_EQ(avifGetExifOrientationOffset(
                  kHeaderOnlyBigEndian, sizeof(kHeaderOnlyBigEndian), &offset),
              AVIF_RESULT_INVALID_EXIF_PAYLOAD)
        << static_cast<int>(bad_ifd_offset);
  }

  // Crafted little-endian payload whose 0th IFD offset (4) rewinds into the
  // TIFF header's own IFD-offset field (fieldCount == 4) and places a forged
  // Orientation tag (value == 6) in the second IFD entry.
  const uint8_t kOverlappingIfdWithOrientation[30] = {
      'I',  'I',  42, 0,        // TIFF magic
      4,    0,    0,  0,        // Invalid offsetTo0thIfd == 4 (< 8)
      0,    0,    0,  0, 0, 0,  // Remainder of overlapped entry 0
      0,    0,    0,  0,        // Trailing bytes of entry 0
      0x12, 0x01,               // Entry 1: tag == 0x0112
      3,    0,                  // type == SHORT (3)
      1,    0,    0,  0,        // count == 1
      6,    0,    0,  0         // forged orientation == 6
  };
  EXPECT_EQ(avifGetExifOrientationOffset(kOverlappingIfdWithOrientation,
                                         sizeof(kOverlappingIfdWithOrientation),
                                         &offset),
            AVIF_RESULT_INVALID_EXIF_PAYLOAD);

  const ImagePtr image(avifImageCreateEmpty());
  ASSERT_NE(image, nullptr);
  ASSERT_EQ(
      avifImageSetMetadataExif(image.get(), kOverlappingIfdWithOrientation,
                               sizeof(kOverlappingIfdWithOrientation)),
      AVIF_RESULT_OK);
  EXPECT_EQ(image->transformFlags, AVIF_TRANSFORM_NONE);
}

AVIF_ARRAY_DECLARE(avifTestU32Array, uint32_t, elements);

TEST(ArrayTest, ZeroInitialCapacityAndGrowth) {
  // Positive control with non-zero initial capacity.
  avifTestU32Array control = {};
  ASSERT_TRUE(avifArrayCreate(&control, sizeof(uint32_t), 2));
  for (uint32_t i = 0; i < 5; ++i) {
    uint32_t* slot = static_cast<uint32_t*>(avifArrayPush(&control));
    ASSERT_NE(slot, nullptr);
    *slot = 100u + i;
  }
  EXPECT_EQ(control.count, 5u);
  EXPECT_EQ(control.capacity, 8u);
  for (uint32_t i = 0; i < 5; ++i) {
    EXPECT_EQ(control.elements[i], 100u + i);
  }
  avifArrayPop(&control);
  EXPECT_EQ(control.count, 4u);
  avifArrayDestroy(&control);
  EXPECT_EQ(control.elements, nullptr);
  EXPECT_EQ(control.count, 0u);
  EXPECT_EQ(control.capacity, 0u);

  // Zero initial capacity must succeed without calling avifAlloc(0) and grow
  // cleanly across 0 -> 1 -> 2 -> 4 -> 8 pushes.
  avifTestU32Array arr = {};
  ASSERT_TRUE(avifArrayCreate(&arr, sizeof(uint32_t), 0));
  EXPECT_EQ(arr.elements, nullptr);
  EXPECT_EQ(arr.count, 0u);
  EXPECT_EQ(arr.capacity, 0u);

  const uint32_t kExpectedCapacities[5] = {1u, 2u, 4u, 4u, 8u};
  for (uint32_t i = 0; i < 5; ++i) {
    uint32_t* slot = static_cast<uint32_t*>(avifArrayPush(&arr));
    ASSERT_NE(slot, nullptr);
    *slot = 42u + i;
    EXPECT_EQ(arr.count, i + 1);
    EXPECT_EQ(arr.capacity, kExpectedCapacities[i]);
  }
  for (uint32_t i = 0; i < 5; ++i) {
    EXPECT_EQ(arr.elements[i], 42u + i);
  }
  avifArrayDestroy(&arr);

  // Invalid zero elementSize and uninitialized array structs must be rejected.
  avifTestU32Array invalid = {};
  EXPECT_FALSE(avifArrayCreate(&invalid, 0, 4));
  EXPECT_EQ(invalid.elements, nullptr);
  EXPECT_EQ(invalid.count, 0u);
  EXPECT_EQ(invalid.capacity, 0u);

  avifTestU32Array uninitialized = {};
  EXPECT_EQ(avifArrayPush(&uninitialized), nullptr);
  EXPECT_EQ(uninitialized.count, 0u);
}

TEST(FractionTest, RejectsZeroDenominator) {
  avifFraction sum = {0, 0};
  ASSERT_TRUE(avifFractionAdd(avifFraction{1, 6}, avifFraction{1, 3}, &sum));
  EXPECT_EQ(sum.n, 1);
  EXPECT_EQ(sum.d, 2);

  avifFraction diff = {0, 0};
  ASSERT_TRUE(avifFractionSub(avifFraction{1, 2}, avifFraction{1, 3}, &diff));
  EXPECT_EQ(diff.n, 1);
  EXPECT_EQ(diff.d, 6);

  avifFraction zero_d = {1, 0};
  avifFraction valid = {1, 2};
  EXPECT_FALSE(avifFractionCD(&zero_d, &valid));
  EXPECT_EQ(valid.n, 1);
  EXPECT_EQ(valid.d, 2);
  EXPECT_FALSE(avifFractionCD(&valid, &zero_d));

  avifFraction out = {7, 9};
  EXPECT_FALSE(avifFractionAdd(zero_d, valid, &out));
  EXPECT_FALSE(avifFractionAdd(valid, zero_d, &out));
  EXPECT_FALSE(avifFractionSub(zero_d, valid, &out));
  EXPECT_FALSE(avifFractionSub(valid, zero_d, &out));
  EXPECT_EQ(out.n, 7);
  EXPECT_EQ(out.d, 9);
}

TEST(ToFractionTest, SignedSubUnitAndRoundingBounds) {
  // Sub-unit magnitudes in (INT32_MAX / UINT32_MAX, 1.0] must still cap the
  // numerator to INT32_MAX rather than allowing denominators up to UINT32_MAX
  // that overflow int32_t.
  constexpr double kTolerance = 1e-9;
  const double kSubUnitBoundary = std::nextafter(
      static_cast<double>(INT32_MAX) / static_cast<double>(UINT32_MAX), 1.0);
  for (double v : {kSubUnitBoundary, -kSubUnitBoundary,
                   2147483648.0 / 2147483649.0, -2147483648.0 / 2147483649.0,
                   3000000000.0 / 3000000001.0, -3000000000.0 / 3000000001.0,
                   4294967294.0 / 4294967295.0, -4294967294.0 / 4294967295.0}) {
    avifSignedFraction sfraction = {0, 0};
    ASSERT_TRUE(avifDoubleToSignedFraction(v, &sfraction)) << v;
    ASSERT_GE(sfraction.d, 1u);
    if (v >= 0.0) {
      EXPECT_GE(sfraction.n, 0) << v;
    } else {
      EXPECT_LE(sfraction.n, 0) << v;
    }
    const double reconstructed = static_cast<double>(sfraction.n) / sfraction.d;
    EXPECT_NEAR(reconstructed, v, std::abs(v) * kTolerance) << v;
  }

  // Values where IEEE-754 maxNumerator / v rounds up to an exact integer k even
  // though k * v > maxNumerator must not exceed maxNumerator during continued
  // fraction expansion.
  avifSignedFraction signed_boundary = {0, 0};
  const double kSignedBoundary = 429496729.40000004;
  ASSERT_TRUE(avifDoubleToSignedFraction(kSignedBoundary, &signed_boundary));
  ASSERT_GE(signed_boundary.d, 1u);
  EXPECT_GT(signed_boundary.n, 0);
  EXPECT_NEAR(static_cast<double>(signed_boundary.n) / signed_boundary.d,
              kSignedBoundary, kSignedBoundary * kTolerance);

  avifUnsignedFraction unsigned_boundary = {0, 0};
  const double kUnsignedBoundary = 116080197.16216217;
  ASSERT_TRUE(
      avifDoubleToUnsignedFraction(kUnsignedBoundary, &unsigned_boundary));
  ASSERT_GE(unsigned_boundary.d, 1u);
  EXPECT_NEAR(static_cast<double>(unsigned_boundary.n) / unsigned_boundary.d,
              kUnsignedBoundary, kUnsignedBoundary * kTolerance);
}

}  // namespace
}  // namespace avif
