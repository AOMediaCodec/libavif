// Copyright 2023 Google LLC
// SPDX-License-Identifier: BSD-2-Clause

#include "avif/avif.h"
#include "avif/internal.h"
#include "aviftest_helpers.h"
#include "gtest/gtest.h"
#include "math.h"

namespace libavif {
namespace {

// Converts a double value to a fraction, and checks that the difference
// between numerator/denominator and v is below relative_tolerance.
void TestRoundTrip(double v, double relative_tolerance) {
  uint32_t numerator, denominator;
  ASSERT_TRUE(avifToUnsignedFraction(v, &numerator, &denominator)) << v;
  const double reconstructed = (double)numerator / denominator;
  const double tolerance = v * relative_tolerance;
  EXPECT_NEAR(reconstructed, v, tolerance)
      << "numerator " << (double)numerator << " denominator "
      << (double)denominator;
}

constexpr double kLotsOfDecimals = 0.14159265358979323846;

TEST(ToFractionUTest, RoundTrip) {
  // Whole numbers and simple fractions should match perfectly.
  const double perfect_tolerance = 0.0;
  TestRoundTrip(0.0, perfect_tolerance);
  TestRoundTrip(1.0, perfect_tolerance);
  TestRoundTrip(42.0, perfect_tolerance);
  TestRoundTrip(102356.0, perfect_tolerance);
  TestRoundTrip(102356456.0f, perfect_tolerance);
  TestRoundTrip(UINT32_MAX / 2.0, perfect_tolerance);
  TestRoundTrip((double)UINT32_MAX - 1.0, perfect_tolerance);
  TestRoundTrip((double)UINT32_MAX, perfect_tolerance);
  TestRoundTrip(0.123, perfect_tolerance);
  TestRoundTrip(1.0 / 3.0, perfect_tolerance);
  TestRoundTrip(1.0 / 4.0, perfect_tolerance);
  TestRoundTrip(3.0 / 23.0, perfect_tolerance);
  TestRoundTrip(1253456.456, perfect_tolerance);
  TestRoundTrip(8598533.9, perfect_tolerance);

  // // Numbers with a lot of decimals or very large/small can show a small
  // error.
  const double small_tolerance = 1e-9;
  TestRoundTrip(0.0123456, small_tolerance);
  TestRoundTrip(3 + kLotsOfDecimals, small_tolerance);
  TestRoundTrip(sqrt(2.0), small_tolerance);
  TestRoundTrip(exp(1.0), small_tolerance);
  TestRoundTrip(exp(10.0), small_tolerance);
  TestRoundTrip(exp(15.0), small_tolerance);
  // The golden ratio, the irrational number that is the "most difficult" to
  // approximate rationally according to Wikipedia.
  const double kGoldenRatio = (1.0 + std::sqrt(5.0)) / 2.0;
  TestRoundTrip(kGoldenRatio, small_tolerance);  // Golden ratio.
  TestRoundTrip(((double)UINT32_MAX) - 0.5, small_tolerance);
  // Note that values smaller than this might have a larger relative error
  // (e.g. 1.0e-10).
  TestRoundTrip(4.2e-10, small_tolerance);
}

// Tests the max difference between the fraction-ified value and the original
// value, for a subset of values between 0.0 and UINT32_MAX.
TEST(ToFractionUTest, MaxDifference) {
  double max_error = 0;
  double max_error_v = 0;
  double max_relative_error = 0;
  double max_relative_error_v = 0;
  for (uint64_t i = 0; i < UINT32_MAX; i += 1000) {
    const double v = i + kLotsOfDecimals;
    uint32_t numerator, denominator;
    ASSERT_TRUE(avifToUnsignedFraction(v, &numerator, &denominator)) << v;
    const double reconstructed = (double)numerator / denominator;
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
TEST(ToFractionUTest, MaxDifferenceSmall) {
  double max_error = 0;
  double max_error_v = 0;
  double max_relative_error = 0;
  double max_relative_error_v = 0;
  for (uint64_t i = 1; i < UINT32_MAX; i += 1000) {
    const double v = 1.0 / (i + kLotsOfDecimals);
    uint32_t numerator, denominator;
    ASSERT_TRUE(avifToUnsignedFraction(v, &numerator, &denominator)) << v;
    const double reconstructed = (double)numerator / denominator;
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

TEST(ToFractionUTest, BadValues) {
  uint32_t numerator, denominator;
  // Negative value.
  EXPECT_FALSE(avifToUnsignedFraction(-0.1, &numerator, &denominator));
  // Too large.
  EXPECT_FALSE(avifToUnsignedFraction(((double)UINT32_MAX) + 1.0, &numerator,
                                      &denominator));
}

}  // namespace
}  // namespace libavif
