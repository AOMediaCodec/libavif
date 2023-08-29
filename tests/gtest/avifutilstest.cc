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

TEST(ToFractionUTest, RoundTrip) {
  const double perfect_tolerance = 0.0;
  TestRoundTrip(0.0, perfect_tolerance);
  TestRoundTrip(1.0, perfect_tolerance);
  TestRoundTrip(42.0, perfect_tolerance);
  TestRoundTrip(102356.0, perfect_tolerance);
  TestRoundTrip(102356456.0f, perfect_tolerance);
  TestRoundTrip(UINT32_MAX / 2.0, perfect_tolerance);
  TestRoundTrip((double)UINT32_MAX, perfect_tolerance);

  const double small_tolerance = 0.001f;  // 0.1%
  TestRoundTrip(0.00000123456, small_tolerance);
  TestRoundTrip(0.123, small_tolerance);
  TestRoundTrip(1.0 / 3.0, small_tolerance);
  TestRoundTrip(1253456.456, small_tolerance);
  TestRoundTrip(8598533.9, small_tolerance);
  TestRoundTrip(((double)UINT32_MAX) - 1.0, small_tolerance);
  TestRoundTrip(((double)UINT32_MAX) - 0.5, small_tolerance);
  // Note that values smaller than this might have a larger relative error
  // (e.g. 1.0e-7).
  TestRoundTrip(4.2e-7, small_tolerance);
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
