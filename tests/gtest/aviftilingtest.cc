// Copyright 2022 Google LLC. All rights reserved.
// SPDX-License-Identifier: BSD-2-Clause

#include "avif/avif.h"
#include "gtest/gtest.h"

namespace {

TEST(TilingTest, SetTileConfiguration) {
  constexpr int kThreads = 8;
  int tile_rows_log2;
  int tile_cols_log2;
  // 144p
  avifSetTileConfiguration(kThreads, 256, 144, &tile_rows_log2,
                           &tile_cols_log2);
  EXPECT_EQ(tile_rows_log2, 0);
  EXPECT_EQ(tile_cols_log2, 0);
  avifSetTileConfiguration(kThreads, 144, 256, &tile_rows_log2,
                           &tile_cols_log2);
  EXPECT_EQ(tile_rows_log2, 0);
  EXPECT_EQ(tile_cols_log2, 0);
  // 240p
  avifSetTileConfiguration(kThreads, 426, 240, &tile_rows_log2,
                           &tile_cols_log2);
  EXPECT_EQ(tile_rows_log2, 0);
  EXPECT_EQ(tile_cols_log2, 0);
  avifSetTileConfiguration(kThreads, 240, 426, &tile_rows_log2,
                           &tile_cols_log2);
  EXPECT_EQ(tile_rows_log2, 0);
  EXPECT_EQ(tile_cols_log2, 0);
  // 360p
  avifSetTileConfiguration(kThreads, 640, 360, &tile_rows_log2,
                           &tile_cols_log2);
  EXPECT_EQ(tile_rows_log2, 0);
  EXPECT_EQ(tile_cols_log2, 0);
  avifSetTileConfiguration(kThreads, 360, 640, &tile_rows_log2,
                           &tile_cols_log2);
  EXPECT_EQ(tile_rows_log2, 0);
  EXPECT_EQ(tile_cols_log2, 0);
  // 480p
  avifSetTileConfiguration(kThreads, 854, 480, &tile_rows_log2,
                           &tile_cols_log2);
  EXPECT_EQ(tile_rows_log2, 0);
  EXPECT_EQ(tile_cols_log2, 1);
  avifSetTileConfiguration(kThreads, 480, 854, &tile_rows_log2,
                           &tile_cols_log2);
  EXPECT_EQ(tile_rows_log2, 1);
  EXPECT_EQ(tile_cols_log2, 0);
  // 720p
  avifSetTileConfiguration(kThreads, 1280, 720, &tile_rows_log2,
                           &tile_cols_log2);
  EXPECT_EQ(tile_rows_log2, 1);
  EXPECT_EQ(tile_cols_log2, 1);
  avifSetTileConfiguration(kThreads, 720, 1280, &tile_rows_log2,
                           &tile_cols_log2);
  EXPECT_EQ(tile_rows_log2, 1);
  EXPECT_EQ(tile_cols_log2, 1);
  // 1080p
  avifSetTileConfiguration(kThreads, 1920, 1080, &tile_rows_log2,
                           &tile_cols_log2);
  EXPECT_EQ(tile_rows_log2, 1);
  EXPECT_EQ(tile_cols_log2, 2);
  avifSetTileConfiguration(kThreads, 1080, 1920, &tile_rows_log2,
                           &tile_cols_log2);
  EXPECT_EQ(tile_rows_log2, 2);
  EXPECT_EQ(tile_cols_log2, 1);
  // 2K
  avifSetTileConfiguration(kThreads, 2560, 1440, &tile_rows_log2,
                           &tile_cols_log2);
  EXPECT_EQ(tile_rows_log2, 1);
  EXPECT_EQ(tile_cols_log2, 2);
  avifSetTileConfiguration(kThreads, 1440, 2560, &tile_rows_log2,
                           &tile_cols_log2);
  EXPECT_EQ(tile_rows_log2, 2);
  EXPECT_EQ(tile_cols_log2, 1);
  // 4K
  avifSetTileConfiguration(32, 3840, 2160, &tile_rows_log2, &tile_cols_log2);
  EXPECT_EQ(tile_rows_log2, 2);
  EXPECT_EQ(tile_cols_log2, 3);
  avifSetTileConfiguration(32, 2160, 3840, &tile_rows_log2, &tile_cols_log2);
  EXPECT_EQ(tile_rows_log2, 3);
  EXPECT_EQ(tile_cols_log2, 2);
  // 8K
  avifSetTileConfiguration(32, 7680, 4320, &tile_rows_log2, &tile_cols_log2);
  EXPECT_EQ(tile_rows_log2, 2);
  EXPECT_EQ(tile_cols_log2, 3);
  avifSetTileConfiguration(32, 4320, 7680, &tile_rows_log2, &tile_cols_log2);
  EXPECT_EQ(tile_rows_log2, 3);
  EXPECT_EQ(tile_cols_log2, 2);

  // Kodak image set: 768x512
  avifSetTileConfiguration(kThreads, 768, 512, &tile_rows_log2,
                           &tile_cols_log2);
  EXPECT_EQ(tile_rows_log2, 0);
  EXPECT_EQ(tile_cols_log2, 1);
  avifSetTileConfiguration(kThreads, 512, 768, &tile_rows_log2,
                           &tile_cols_log2);
  EXPECT_EQ(tile_rows_log2, 1);
  EXPECT_EQ(tile_cols_log2, 0);

  avifSetTileConfiguration(kThreads, 16384, 64, &tile_rows_log2,
                           &tile_cols_log2);
  EXPECT_EQ(tile_rows_log2, 0);
  EXPECT_EQ(tile_cols_log2, 2);
  avifSetTileConfiguration(kThreads, 64, 16384, &tile_rows_log2,
                           &tile_cols_log2);
  EXPECT_EQ(tile_rows_log2, 2);
  EXPECT_EQ(tile_cols_log2, 0);
}

}  // namespace
