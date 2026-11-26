// Copyright 2023 Google LLC
// SPDX-License-Identifier: BSD-2-Clause

#include <cmath>
#include <cstddef>
#include <cstdint>

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

//------------------------------------------------------------------------------
// Checked arithmetic

TEST(CheckedArithmeticTest, MulU32) {
  uint32_t result = 123;
  EXPECT_TRUE(avifCheckedMulU32(0, 0, &result));
  EXPECT_EQ(result, 0u);
  EXPECT_TRUE(avifCheckedMulU32(0, UINT32_MAX, &result));
  EXPECT_EQ(result, 0u);
  EXPECT_TRUE(avifCheckedMulU32(UINT32_MAX, 0, &result));
  EXPECT_EQ(result, 0u);
  EXPECT_TRUE(avifCheckedMulU32(UINT32_MAX, 1, &result));
  EXPECT_EQ(result, UINT32_MAX);
  // 65535 * 65537 = 2^32 - 1 just fits.
  EXPECT_TRUE(avifCheckedMulU32(65535, 65537, &result));
  EXPECT_EQ(result, UINT32_MAX);
  EXPECT_TRUE(avifCheckedMulU32(UINT32_MAX / 2, 2, &result));
  EXPECT_EQ(result, UINT32_MAX - 1);

  // The result is left untouched on overflow.
  result = 123;
  // 65536 * 65536 = 2^32 just overflows.
  EXPECT_FALSE(avifCheckedMulU32(65536, 65536, &result));
  EXPECT_FALSE(avifCheckedMulU32(UINT32_MAX / 2 + 1, 2, &result));
  EXPECT_FALSE(avifCheckedMulU32(2, UINT32_MAX / 2 + 1, &result));
  EXPECT_FALSE(avifCheckedMulU32(UINT32_MAX, UINT32_MAX, &result));
  EXPECT_EQ(result, 123u);
}

TEST(CheckedArithmeticTest, AddU64) {
  uint64_t result = 123;
  EXPECT_TRUE(avifCheckedAddU64(0, 0, &result));
  EXPECT_EQ(result, 0u);
  EXPECT_TRUE(avifCheckedAddU64(UINT64_MAX, 0, &result));
  EXPECT_EQ(result, UINT64_MAX);
  EXPECT_TRUE(avifCheckedAddU64(0, UINT64_MAX, &result));
  EXPECT_EQ(result, UINT64_MAX);
  EXPECT_TRUE(avifCheckedAddU64(UINT64_MAX - 1, 1, &result));
  EXPECT_EQ(result, UINT64_MAX);
  EXPECT_TRUE(avifCheckedAddU64(UINT64_MAX / 2, UINT64_MAX / 2 + 1, &result));
  EXPECT_EQ(result, UINT64_MAX);

  result = 123;
  EXPECT_FALSE(avifCheckedAddU64(UINT64_MAX, 1, &result));
  EXPECT_FALSE(avifCheckedAddU64(1, UINT64_MAX, &result));
  EXPECT_FALSE(
      avifCheckedAddU64(UINT64_MAX / 2 + 1, UINT64_MAX / 2 + 1, &result));
  EXPECT_FALSE(avifCheckedAddU64(UINT64_MAX, UINT64_MAX, &result));
  EXPECT_EQ(result, 123u);
}

TEST(CheckedArithmeticTest, MulSize) {
  size_t result = 123;
  EXPECT_TRUE(avifCheckedMulSize(0, 0, &result));
  EXPECT_EQ(result, 0u);
  EXPECT_TRUE(avifCheckedMulSize(0, SIZE_MAX, &result));
  EXPECT_EQ(result, 0u);
  EXPECT_TRUE(avifCheckedMulSize(SIZE_MAX, 0, &result));
  EXPECT_EQ(result, 0u);
  EXPECT_TRUE(avifCheckedMulSize(SIZE_MAX, 1, &result));
  EXPECT_EQ(result, SIZE_MAX);
  EXPECT_TRUE(avifCheckedMulSize(SIZE_MAX / 2, 2, &result));
  EXPECT_EQ(result, SIZE_MAX - 1);
  EXPECT_TRUE(avifCheckedMulSize(SIZE_MAX / 4, 4, &result));
  EXPECT_EQ(result, SIZE_MAX - 3);

  result = 123;
  EXPECT_FALSE(avifCheckedMulSize(SIZE_MAX / 2 + 1, 2, &result));
  EXPECT_FALSE(avifCheckedMulSize(2, SIZE_MAX / 2 + 1, &result));
  EXPECT_FALSE(avifCheckedMulSize(SIZE_MAX / 4 + 1, 4, &result));
  EXPECT_FALSE(avifCheckedMulSize(SIZE_MAX, SIZE_MAX, &result));
  EXPECT_EQ(result, 123u);
}

TEST(CheckedArithmeticTest, AddSize) {
  size_t result = 123;
  EXPECT_TRUE(avifCheckedAddSize(0, 0, &result));
  EXPECT_EQ(result, 0u);
  EXPECT_TRUE(avifCheckedAddSize(SIZE_MAX, 0, &result));
  EXPECT_EQ(result, SIZE_MAX);
  EXPECT_TRUE(avifCheckedAddSize(0, SIZE_MAX, &result));
  EXPECT_EQ(result, SIZE_MAX);
  EXPECT_TRUE(avifCheckedAddSize(SIZE_MAX - 4, 4, &result));
  EXPECT_EQ(result, SIZE_MAX);

  result = 123;
  EXPECT_FALSE(avifCheckedAddSize(SIZE_MAX, 1, &result));
  EXPECT_FALSE(avifCheckedAddSize(1, SIZE_MAX, &result));
  EXPECT_FALSE(avifCheckedAddSize(SIZE_MAX - 3, 4, &result));
  EXPECT_FALSE(avifCheckedAddSize(SIZE_MAX, SIZE_MAX, &result));
  EXPECT_EQ(result, 123u);
}

}  // namespace
}  // namespace avif
