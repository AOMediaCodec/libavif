// Copyright 2022 Google LLC
// SPDX-License-Identifier: BSD-2-Clause

#include <algorithm>
#include <cmath>
#include <memory>
#include <climits>
#include <cstdint>
#include <cstring>
#include <tuple>
#include <vector>

#if (defined(__linux__) || defined(__APPLE__)) && UINTPTR_MAX > UINT32_MAX
#include <sys/mman.h>
#include <unistd.h>
#endif

#include "aviftest_helpers.h"
#include "gtest/gtest.h"

using ::testing::Bool;
using ::testing::Combine;
using ::testing::Range;
using ::testing::Values;

namespace avif {
namespace {

// Converts YUV pixels to RGB using one thread and multiple threads and checks
// whether the results of both are identical.
class YUVToRGBThreadingTest
    : public testing::TestWithParam<std::tuple<
          /*rgb_depth=*/int, /*yuv_depth=*/int,
          /*width=*/int, /*height=*/int, avifRGBFormat, avifPixelFormat,
          /*threads=*/int, /*avoidLibYUV=*/bool, avifChromaUpsampling,
          /*has_alpha=*/bool>> {};

TEST_P(YUVToRGBThreadingTest, TestIdentical) {
  const int rgb_depth = std::get<0>(GetParam());
  const int yuv_depth = std::get<1>(GetParam());
  const int width = std::get<2>(GetParam());
  const int height = std::get<3>(GetParam());
  const avifRGBFormat rgb_format = std::get<4>(GetParam());
  const avifPixelFormat yuv_format = std::get<5>(GetParam());
  const int maxThreads = std::get<6>(GetParam());
  const bool avoidLibYUV = std::get<7>(GetParam());
  const avifChromaUpsampling chromaUpsampling = std::get<8>(GetParam());
  const bool has_alpha = std::get<9>(GetParam());

  if (rgb_depth > 8 && rgb_format == AVIF_RGB_FORMAT_RGB_565) {
    return;
  }

  ImagePtr yuv(avifImageCreate(width, height, yuv_depth, yuv_format));
  ASSERT_NE(yuv, nullptr);
  yuv->matrixCoefficients = AVIF_MATRIX_COEFFICIENTS_BT601;
  yuv->yuvRange = AVIF_RANGE_FULL;

  // Fill YUVA planes with random values.
  srand(0xAABBCCDD);
  const int yuv_max = (1 << yuv_depth);
  ASSERT_EQ(avifImageAllocatePlanes(
                yuv.get(), has_alpha ? AVIF_PLANES_ALL : AVIF_PLANES_YUV),
            AVIF_RESULT_OK);
  for (int plane = AVIF_CHAN_Y; plane <= AVIF_CHAN_A; ++plane) {
    const uint32_t plane_width = avifImagePlaneWidth(yuv.get(), plane);
    if (plane_width == 0) continue;
    const uint32_t plane_height = avifImagePlaneHeight(yuv.get(), plane);
    const uint32_t rowBytes = avifImagePlaneRowBytes(yuv.get(), plane);
    uint8_t* row = avifImagePlane(yuv.get(), plane);
    for (uint32_t y = 0; y < plane_height; ++y, row += rowBytes) {
      for (uint32_t x = 0; x < plane_width; ++x) {
        if (yuv_depth == 8) {
          row[x] = (uint8_t)(rand() % yuv_max);
        } else {
          ((uint16_t*)row)[x] = (uint16_t)(rand() % yuv_max);
        }
      }
    }
  }

  // Convert to RGB with 1 thread.
  testutil::AvifRgbImage rgb(yuv.get(), rgb_depth, rgb_format);
  rgb.avoidLibYUV = avoidLibYUV;
  rgb.chromaUpsampling = chromaUpsampling;
  ASSERT_EQ(avifImageYUVToRGB(yuv.get(), &rgb), AVIF_RESULT_OK);

  // Convert to RGB with multiple threads.
  testutil::AvifRgbImage rgb_threaded(yuv.get(), rgb_depth, rgb_format);
  rgb_threaded.avoidLibYUV = avoidLibYUV;
  rgb_threaded.chromaUpsampling = chromaUpsampling;
  rgb_threaded.maxThreads = maxThreads;
  ASSERT_EQ(avifImageYUVToRGB(yuv.get(), &rgb_threaded), AVIF_RESULT_OK);

  EXPECT_TRUE(testutil::AreImagesEqual(rgb, rgb_threaded));
}

INSTANTIATE_TEST_SUITE_P(
    YUVToRGBThreadingTestInstance, YUVToRGBThreadingTest,
    Combine(/*rgb_depth=*/Values(8, 16),
            /*yuv_depth=*/Values(8, 10),
            /*width=*/Values(1, 2, 127, 200),
            /*height=*/Values(1, 2, 127, 200),
            Values(AVIF_RGB_FORMAT_RGB, AVIF_RGB_FORMAT_RGBA),
            Range(AVIF_PIXEL_FORMAT_YUV444, AVIF_PIXEL_FORMAT_COUNT),
            // Test an odd and even number for threads. Not adding all possible
            // thread values to keep the number of test instances low.
            /*threads=*/Values(2, 7),
            /*avoidLibYUV=*/Bool(),
            Values(AVIF_CHROMA_UPSAMPLING_FASTEST,
                   AVIF_CHROMA_UPSAMPLING_BILINEAR),
            /*has_alpha=*/Bool()));

// This will generate a large number of test instances and hence it is disabled
// by default. It can be run manually if necessary.
INSTANTIATE_TEST_SUITE_P(
    DISABLED_ExhaustiveYUVToRGBThreadingTestInstance, YUVToRGBThreadingTest,
    Combine(/*rgb_depth=*/Values(8, 10, 12, 16),
            /*yuv_depth=*/Values(8, 10, 12),
            /*width=*/Values(1, 2, 127, 200),
            /*height=*/Values(1, 2, 127, 200),
            Range(AVIF_RGB_FORMAT_RGB, AVIF_RGB_FORMAT_COUNT),
            Range(AVIF_PIXEL_FORMAT_YUV444, AVIF_PIXEL_FORMAT_COUNT),
            /*threads=*/Range(0, 9),
            /*avoidLibYUV=*/Bool(),
            Values(AVIF_CHROMA_UPSAMPLING_AUTOMATIC,
                   AVIF_CHROMA_UPSAMPLING_FASTEST,
                   AVIF_CHROMA_UPSAMPLING_NEAREST,
                   AVIF_CHROMA_UPSAMPLING_BILINEAR),
            /*has_alpha=*/Bool()));

#if (defined(__linux__) || defined(__APPLE__)) && UINTPTR_MAX > UINT32_MAX
// Regression tests for signed int stride overflow when plane rowBytes > INT_MAX.
class ScopedHugeStrideTwoRowPlane {
 public:
  static constexpr uint32_t kStride = 0x80000000u;

  bool Init(uint8_t fill_value) {
    const long raw_page = sysconf(_SC_PAGESIZE);
    if (raw_page <= 0) return false;
    page_size_ = static_cast<size_t>(raw_page);
    total_size_ = static_cast<size_t>(kStride) * 2 + page_size_ * 2;
    mapping_ = mmap(nullptr, total_size_, PROT_NONE,
                    MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (mapping_ == MAP_FAILED) {
      mapping_ = nullptr;
      return false;
    }
    row0_ = static_cast<uint8_t*>(mapping_) + kStride;
    row1_ = row0_ + kStride;
    if (mprotect(row0_, page_size_, PROT_READ | PROT_WRITE) != 0 ||
        mprotect(row1_, page_size_, PROT_READ | PROT_WRITE) != 0) {
      munmap(mapping_, total_size_);
      mapping_ = nullptr;
      row0_ = nullptr;
      row1_ = nullptr;
      return false;
    }
    std::memset(row0_, fill_value, 16);
    std::memset(row1_, fill_value, 16);
    return true;
  }

  ~ScopedHugeStrideTwoRowPlane() {
    if (mapping_ != nullptr) {
      munmap(mapping_, total_size_);
    }
  }

  uint8_t* row0() const { return row0_; }
  uint8_t* row1() const { return row1_; }

 private:
  void* mapping_ = nullptr;
  uint8_t* row0_ = nullptr;
  uint8_t* row1_ = nullptr;
  size_t page_size_ = 0;
  size_t total_size_ = 0;
};

void DetachBorrowedYUVPlanes(avifImage* image) {
  image->imageOwnsYUVPlanes = AVIF_FALSE;
  image->yuvPlanes[AVIF_CHAN_Y] = nullptr;
  image->yuvPlanes[AVIF_CHAN_U] = nullptr;
  image->yuvPlanes[AVIF_CHAN_V] = nullptr;
}

TEST(LibYUVPlaneStrideTest, RGBToYUVOversizedChromaStridesFallBackSafely) {
  ScopedHugeStrideTwoRowPlane huge_u;
  ScopedHugeStrideTwoRowPlane huge_v;
  if (!huge_u.Init(0) || !huge_v.Init(0)) {
    GTEST_SKIP() << "Virtual address space reservation unavailable";
  }

  for (int huge_chan : {AVIF_CHAN_U, AVIF_CHAN_V}) {
    ImagePtr image(avifImageCreate(2, 2, 8, AVIF_PIXEL_FORMAT_YUV422));
    ASSERT_NE(image, nullptr);
    image->yuvRange = AVIF_RANGE_LIMITED;
    image->matrixCoefficients = AVIF_MATRIX_COEFFICIENTS_BT601;

    std::vector<uint8_t> y_buf(16, 0);
    std::vector<uint8_t> normal_chroma(16, 0);
    image->yuvPlanes[AVIF_CHAN_Y] = y_buf.data();
    image->yuvRowBytes[AVIF_CHAN_Y] = 2;
    if (huge_chan == AVIF_CHAN_U) {
      image->yuvPlanes[AVIF_CHAN_U] = huge_u.row0();
      image->yuvRowBytes[AVIF_CHAN_U] = ScopedHugeStrideTwoRowPlane::kStride;
      image->yuvPlanes[AVIF_CHAN_V] = normal_chroma.data();
      image->yuvRowBytes[AVIF_CHAN_V] = 1;
    } else {
      image->yuvPlanes[AVIF_CHAN_U] = normal_chroma.data();
      image->yuvRowBytes[AVIF_CHAN_U] = 1;
      image->yuvPlanes[AVIF_CHAN_V] = huge_v.row0();
      image->yuvRowBytes[AVIF_CHAN_V] = ScopedHugeStrideTwoRowPlane::kStride;
    }

    testutil::AvifRgbImage rgb(image.get(), 8, AVIF_RGB_FORMAT_BGRA);
    rgb.avoidLibYUV = AVIF_FALSE;
    std::memset(rgb.pixels, 96, rgb.rowBytes * rgb.height);

    const avifResult res = avifImageRGBToYUV(image.get(), &rgb);
    DetachBorrowedYUVPlanes(image.get());
    ASSERT_EQ(res, AVIF_RESULT_OK);
  }

  // Positive control: monochrome YUV400 ignores unused chroma rowBytes fields.
  ImagePtr mono(avifImageCreate(2, 2, 8, AVIF_PIXEL_FORMAT_YUV400));
  ASSERT_NE(mono, nullptr);
  mono->yuvRange = AVIF_RANGE_LIMITED;
  mono->matrixCoefficients = AVIF_MATRIX_COEFFICIENTS_BT601;
  ASSERT_EQ(avifImageAllocatePlanes(mono.get(), AVIF_PLANES_YUV),
            AVIF_RESULT_OK);
  mono->yuvRowBytes[AVIF_CHAN_U] = ScopedHugeStrideTwoRowPlane::kStride;
  mono->yuvRowBytes[AVIF_CHAN_V] = ScopedHugeStrideTwoRowPlane::kStride;
  testutil::AvifRgbImage mono_rgb(mono.get(), 8, AVIF_RGB_FORMAT_BGRA);
  mono_rgb.avoidLibYUV = AVIF_FALSE;
  std::memset(mono_rgb.pixels, 96, mono_rgb.rowBytes * mono_rgb.height);
  EXPECT_EQ(avifImageRGBToYUV(mono.get(), &mono_rgb), AVIF_RESULT_OK);
}

TEST(LibYUVPlaneStrideTest, YUVToRGBOversizedChromaStridesFallBackSafely) {
  ScopedHugeStrideTwoRowPlane huge_plane;
  if (!huge_plane.Init(128)) {
    GTEST_SKIP() << "Virtual address space reservation unavailable";
  }

  for (int huge_chan : {AVIF_CHAN_U, AVIF_CHAN_V}) {
    ImagePtr image(avifImageCreate(2, 2, 8, AVIF_PIXEL_FORMAT_YUV422));
    ASSERT_NE(image, nullptr);
    image->yuvRange = AVIF_RANGE_LIMITED;
    image->matrixCoefficients = AVIF_MATRIX_COEFFICIENTS_BT601;

    std::vector<uint8_t> y_buf(16, 128);
    std::vector<uint8_t> u_buf(16, 128);
    std::vector<uint8_t> v_buf(16, 128);
    image->yuvPlanes[AVIF_CHAN_Y] = y_buf.data();
    image->yuvRowBytes[AVIF_CHAN_Y] = 2;
    image->yuvPlanes[AVIF_CHAN_U] = u_buf.data();
    image->yuvRowBytes[AVIF_CHAN_U] = 1;
    image->yuvPlanes[AVIF_CHAN_V] = v_buf.data();
    image->yuvRowBytes[AVIF_CHAN_V] = 1;

    if (huge_chan == AVIF_CHAN_U) {
      image->yuvPlanes[AVIF_CHAN_U] = huge_plane.row0();
      image->yuvRowBytes[AVIF_CHAN_U] = ScopedHugeStrideTwoRowPlane::kStride;
    } else {
      image->yuvPlanes[AVIF_CHAN_V] = huge_plane.row0();
      image->yuvRowBytes[AVIF_CHAN_V] = ScopedHugeStrideTwoRowPlane::kStride;
    }

    testutil::AvifRgbImage rgb(image.get(), 8, AVIF_RGB_FORMAT_RGBA);
    rgb.avoidLibYUV = AVIF_FALSE;
    rgb.chromaUpsampling = AVIF_CHROMA_UPSAMPLING_FASTEST;

    const avifResult res = avifImageYUVToRGB(image.get(), &rgb);
    DetachBorrowedYUVPlanes(image.get());
    ASSERT_EQ(res, AVIF_RESULT_OK);
  }
}

TEST(LibYUVPlaneStrideTest, YUVToRGBOversized8BitAlphaStrideFallsBackSafely) {
  ScopedHugeStrideTwoRowPlane huge_alpha;
  if (!huge_alpha.Init(192)) {
    GTEST_SKIP() << "Virtual address space reservation unavailable";
  }

  ImagePtr image(avifImageCreate(2, 2, 8, AVIF_PIXEL_FORMAT_YUV420));
  ASSERT_NE(image, nullptr);
  image->yuvRange = AVIF_RANGE_LIMITED;
  image->matrixCoefficients = AVIF_MATRIX_COEFFICIENTS_BT601;
  ASSERT_EQ(avifImageAllocatePlanes(image.get(), AVIF_PLANES_YUV),
            AVIF_RESULT_OK);
  std::memset(image->yuvPlanes[AVIF_CHAN_Y], 128,
              image->yuvRowBytes[AVIF_CHAN_Y] * image->height);
  std::memset(image->yuvPlanes[AVIF_CHAN_U], 128,
              image->yuvRowBytes[AVIF_CHAN_U]);
  std::memset(image->yuvPlanes[AVIF_CHAN_V], 128,
              image->yuvRowBytes[AVIF_CHAN_V]);
  image->alphaPlane = huge_alpha.row0();
  image->alphaRowBytes = ScopedHugeStrideTwoRowPlane::kStride;
  image->imageOwnsAlphaPlane = AVIF_FALSE;

  testutil::AvifRgbImage rgb(image.get(), 8, AVIF_RGB_FORMAT_RGBA);
  rgb.avoidLibYUV = AVIF_FALSE;
  rgb.chromaUpsampling = AVIF_CHROMA_UPSAMPLING_FASTEST;

  const avifResult res = avifImageYUVToRGB(image.get(), &rgb);
  image->alphaPlane = nullptr;
  ASSERT_EQ(res, AVIF_RESULT_OK);
}
#endif

}  // namespace
}  // namespace avif
