// Copyright 2023 Google LLC
// SPDX-License-Identifier: BSD-2-Clause

#include "avif/avif.h"
#include "avif/internal.h"
#include "aviftest_helpers.h"
#include "gtest/gtest.h"

namespace libavif {
namespace {

TEST(QualityTest, ToQuantizer) {
  int previous_quantizer = AVIF_QUANTIZER_WORST_QUALITY;
  for (int quality = AVIF_QUALITY_WORST; quality <= AVIF_QUALITY_BEST;
       ++quality) {
    const int quantizer = avifQualityToQuantizer(
        quality, /*minQuantizer=*/AVIF_QUANTIZER_BEST_QUALITY,
        /*maxQuantizer=*/AVIF_QUANTIZER_BEST_QUALITY);
    EXPECT_GE(quantizer, AVIF_QUANTIZER_BEST_QUALITY);
    EXPECT_LE(quantizer, AVIF_QUANTIZER_WORST_QUALITY);

    // Roundtrip. There are more quality values than quantizers so some
    // collisions are expected.
    EXPECT_NEAR(avifQuantizerToQuality(quantizer), quality, 1.0);

    // minQuantizer and maxQuantizer have no impact with an explicit quality.
    EXPECT_EQ(quantizer,
              avifQualityToQuantizer(quality, AVIF_QUANTIZER_WORST_QUALITY,
                                     AVIF_QUANTIZER_WORST_QUALITY));

    EXPECT_LE(quantizer, previous_quantizer);
    previous_quantizer = quantizer;
  }
}

TEST(QualityTest, DefaultToQuantizer) {
  for (int min_quantizer = AVIF_QUANTIZER_BEST_QUALITY;
       min_quantizer <= AVIF_QUANTIZER_WORST_QUALITY; ++min_quantizer) {
    for (int max_quantizer = AVIF_QUANTIZER_BEST_QUALITY;
         max_quantizer <= AVIF_QUANTIZER_WORST_QUALITY; ++max_quantizer) {
      const int quantizer = avifQualityToQuantizer(
          AVIF_QUALITY_DEFAULT, min_quantizer, max_quantizer);
      EXPECT_GE(quantizer, AVIF_QUANTIZER_BEST_QUALITY);
      EXPECT_LE(quantizer, AVIF_QUANTIZER_WORST_QUALITY);
    }
  }
}

TEST(QualityTest, FromQuantizer) {
  for (int quantizer = AVIF_QUANTIZER_BEST_QUALITY;
       quantizer <= AVIF_QUANTIZER_WORST_QUALITY; ++quantizer) {
    const int quality = avifQuantizerToQuality(quantizer);
    EXPECT_GE(quantizer, AVIF_QUALITY_WORST);
    EXPECT_LE(quantizer, AVIF_QUALITY_BEST);

    // Roundtrip.
    EXPECT_EQ(quantizer,
              avifQualityToQuantizer(quality, AVIF_QUANTIZER_WORST_QUALITY,
                                     AVIF_QUANTIZER_BEST_QUALITY));
  }
}

TEST(QualityTest, WorstBest) {
  EXPECT_EQ(
      avifQualityToQuantizer(AVIF_QUALITY_WORST, AVIF_QUANTIZER_WORST_QUALITY,
                             AVIF_QUANTIZER_WORST_QUALITY),
      AVIF_QUANTIZER_WORST_QUALITY);
  EXPECT_EQ(
      avifQualityToQuantizer(AVIF_QUALITY_BEST, AVIF_QUANTIZER_BEST_QUALITY,
                             AVIF_QUANTIZER_BEST_QUALITY),
      AVIF_QUANTIZER_BEST_QUALITY);

  EXPECT_EQ(avifQuantizerToQuality(AVIF_QUANTIZER_WORST_QUALITY),
            AVIF_QUALITY_WORST);
  EXPECT_EQ(avifQuantizerToQuality(AVIF_QUANTIZER_BEST_QUALITY),
            AVIF_QUALITY_BEST);
}

TEST(QualityTest, DefaultWorstBest) {
  EXPECT_EQ(
      avifQualityToQuantizer(AVIF_QUALITY_DEFAULT, AVIF_QUANTIZER_WORST_QUALITY,
                             AVIF_QUANTIZER_WORST_QUALITY),
      AVIF_QUANTIZER_WORST_QUALITY);
  EXPECT_EQ(
      avifQualityToQuantizer(AVIF_QUALITY_DEFAULT, AVIF_QUANTIZER_BEST_QUALITY,
                             AVIF_QUANTIZER_BEST_QUALITY),
      AVIF_QUANTIZER_BEST_QUALITY);
}

}  // namespace
}  // namespace libavif
