// Copyright 2024 Google LLC
// SPDX-License-Identifier: BSD-2-Clause

#include "avif/avif.h"
#include "aviftest_helpers.h"
#include "gtest/gtest.h"

namespace avif {
namespace {

// Used to pass the data folder path to the GoogleTest suites.
const char* data_path = nullptr;

//------------------------------------------------------------------------------

TEST(IlocTest, TwoExtents) {
  if (!testutil::Av1DecoderAvailable()) {
    GTEST_SKIP() << "AV1 Codec unavailable, skip test.";
  }

  const ImagePtr source =
      testutil::ReadImage(data_path, "ArcTriomphe-cHRM-orig.png");
  const ImagePtr decoded =
      testutil::DecodeFile(std::string(data_path) +
                           "arc_triomphe_extent1000_nullbyte_extent1310.avif");
  ASSERT_NE(source, nullptr);
  ASSERT_NE(decoded, nullptr);
  const double psnr = testutil::GetPsnr(*source, *decoded);
  EXPECT_GT(psnr, 30.0);
  EXPECT_LT(psnr, 45.0);
}

//------------------------------------------------------------------------------

}  // namespace
}  // namespace avif

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  if (argc != 2) {
    std::cerr << "There must be exactly one argument containing the path to "
                 "the test data folder"
              << std::endl;
    return 1;
  }
  avif::data_path = argv[1];
  return RUN_ALL_TESTS();
}
