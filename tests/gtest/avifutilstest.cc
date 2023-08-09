// Copyright 2023 Google LLC
// SPDX-License-Identifier: BSD-2-Clause

#include "avif/avif.h"
#include "avif/internal.h"
#include "aviftest_helpers.h"
#include "gtest/gtest.h"
namespace libavif {
namespace {

void TestRoundTrip(float v, float relative_tolerance) {
  avifFractionU fraction;
  ASSERT_TRUE(avifToFractionU(v, &fraction)) << v;
  const float reconstructed = (float)fraction.n / fraction.d;
  const float tolerance = v * relative_tolerance;
  EXPECT_NEAR(reconstructed, v, tolerance)
      << "numerator " << (float)fraction.n << " denominator "
      << (float)fraction.d;
}

TEST(ToFractionUTest, RoundTrip) {
  const float perfect_tolerance = 0.0f;
  TestRoundTrip(0.0f, perfect_tolerance);
  TestRoundTrip(1.0f, perfect_tolerance);
  TestRoundTrip(42.0f, perfect_tolerance);
  TestRoundTrip(102356.0f, perfect_tolerance);
  TestRoundTrip(102356456.0f, perfect_tolerance);
  TestRoundTrip(UINT32_MAX / 2.0f, perfect_tolerance);
  TestRoundTrip((float)UINT32_MAX, perfect_tolerance);

  const float small_tolerance = 0.001f;  // 0.1%
  TestRoundTrip(0.00000123456f, small_tolerance);
  TestRoundTrip(0.123f, small_tolerance);
  TestRoundTrip(1.0f / 3.0f, small_tolerance);
  TestRoundTrip(1253456.456f, small_tolerance);
}

TEST(ToFractionUTest, BadValues) {
  avifFractionU fraction;
  // Negative value.
  EXPECT_FALSE(avifToFractionU(-0.1f, &fraction));
  // Too large.
  EXPECT_FALSE(avifToFractionU((float)UINT32_MAX * 2.0f, &fraction));
}

}  // namespace
}  // namespace libavif
