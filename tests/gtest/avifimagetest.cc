// Copyright 2023 Google LLC
// SPDX-License-Identifier: BSD-2-Clause

#include <array>
#include <cstdint>
#include <limits>

#include "avif/avif.h"
#include "aviftest_helpers.h"
#include "gtest/gtest.h"

namespace avif {
namespace {

TEST(AvifImageTest, CreateEmpty) {
  ImagePtr empty(avifImageCreateEmpty());
  EXPECT_NE(empty, nullptr);
}

bool IsValidAvifImageCreate(uint32_t width, uint32_t height, uint32_t depth,
                            avifPixelFormat format) {
  ImagePtr image(avifImageCreate(width, height, depth, format));
  return image != nullptr;
}

TEST(AvifImageTest, Create) {
  EXPECT_TRUE(IsValidAvifImageCreate(0, 0, 0, AVIF_PIXEL_FORMAT_NONE));
  EXPECT_TRUE(
      IsValidAvifImageCreate(1, 1, /*depth=*/1, AVIF_PIXEL_FORMAT_NONE));
  EXPECT_TRUE(
      IsValidAvifImageCreate(64, 64, /*depth=*/8, AVIF_PIXEL_FORMAT_NONE));
  EXPECT_TRUE(IsValidAvifImageCreate(std::numeric_limits<uint32_t>::max(),
                                     std::numeric_limits<uint32_t>::max(),
                                     /*depth=*/16, AVIF_PIXEL_FORMAT_NONE));
}

TEST(AvifImageTest, Invalid) {
  EXPECT_FALSE(IsValidAvifImageCreate(0, 0, 0, AVIF_PIXEL_FORMAT_COUNT));
  EXPECT_FALSE(
      IsValidAvifImageCreate(0, 0, /*depth=*/17, AVIF_PIXEL_FORMAT_YUV400));
}

TEST(AvifImageTest, WriteImage) {
  ImagePtr image =
      testutil::CreateImage(/*width=*/12, /*height=*/34, /*depth=*/10,
                            AVIF_PIXEL_FORMAT_YUV444, AVIF_PLANES_ALL);
  ASSERT_NE(image, nullptr);
  testutil::FillImageGradient(image.get());
  ASSERT_TRUE(testutil::WriteImage(
      image.get(), (testing::TempDir() + "/avifimagetest.png").c_str()));
}

TEST(AvifImageTest, CopyViewIntoOwner) {
  ImagePtr image =
      testutil::CreateImage(/*width=*/16, /*height=*/16, /*depth=*/8,
                            AVIF_PIXEL_FORMAT_YUV444, AVIF_PLANES_ALL);
  ASSERT_NE(image, nullptr);
  testutil::FillImageGradient(image.get());

  ImagePtr view(avifImageCreateEmpty());
  ASSERT_NE(view, nullptr);
  const avifCropRect rect = {/*x=*/4, /*y=*/4, /*width=*/8, /*height=*/8};
  ASSERT_EQ(avifImageSetViewRect(view.get(), image.get(), &rect),
            AVIF_RESULT_OK);

  std::array<std::array<uint8_t, 64>, 4> expected;
  for (int channel = AVIF_CHAN_Y; channel <= AVIF_CHAN_A; ++channel) {
    const uint8_t* row = avifImagePlane(view.get(), channel);
    ASSERT_NE(row, nullptr);
    for (uint32_t y = 0; y < rect.height; ++y) {
      for (uint32_t x = 0; x < rect.width; ++x) {
        expected[channel][y * rect.width + x] = row[x];
      }
      row += avifImagePlaneRowBytes(view.get(), channel);
    }
  }

  ASSERT_EQ(avifImageCopy(image.get(), view.get(), AVIF_PLANES_ALL),
            AVIF_RESULT_OK);
  EXPECT_EQ(image->width, rect.width);
  EXPECT_EQ(image->height, rect.height);
  EXPECT_TRUE(image->imageOwnsYUVPlanes);
  EXPECT_TRUE(image->imageOwnsAlphaPlane);
  for (int channel = AVIF_CHAN_Y; channel <= AVIF_CHAN_A; ++channel) {
    const uint8_t* row = avifImagePlane(image.get(), channel);
    ASSERT_NE(row, nullptr);
    for (uint32_t y = 0; y < rect.height; ++y) {
      for (uint32_t x = 0; x < rect.width; ++x) {
        EXPECT_EQ(row[x], expected[channel][y * rect.width + x]);
      }
      row += avifImagePlaneRowBytes(image.get(), channel);
    }
  }
}

TEST(AvifImageTest, SetViewRectRejectsViewIntoDestination) {
  ImagePtr owner(avifImageCreate(/*width=*/16, /*height=*/16, /*depth=*/8,
                                 AVIF_PIXEL_FORMAT_YUV444));
  ImagePtr view(avifImageCreateEmpty());
  ASSERT_NE(owner, nullptr);
  ASSERT_NE(view, nullptr);
  ASSERT_EQ(avifImageAllocatePlanes(owner.get(), AVIF_PLANES_ALL),
            AVIF_RESULT_OK);

  const avifCropRect outer_rect = {/*x=*/4, /*y=*/4, /*width=*/8,
                                   /*height=*/8};
  ASSERT_EQ(avifImageSetViewRect(view.get(), owner.get(), &outer_rect),
            AVIF_RESULT_OK);
  uint8_t* const owner_y = owner->yuvPlanes[AVIF_CHAN_Y];
  uint8_t* const owner_alpha = owner->alphaPlane;
  uint8_t* const view_y = view->yuvPlanes[AVIF_CHAN_Y];
  uint8_t* const view_alpha = view->alphaPlane;

  const avifCropRect inner_rect = {/*x=*/0, /*y=*/0, /*width=*/4,
                                   /*height=*/4};
  EXPECT_EQ(avifImageSetViewRect(owner.get(), view.get(), &inner_rect),
            AVIF_RESULT_INVALID_ARGUMENT);
  EXPECT_EQ(owner->yuvPlanes[AVIF_CHAN_Y], owner_y);
  EXPECT_EQ(owner->alphaPlane, owner_alpha);
  EXPECT_EQ(view->yuvPlanes[AVIF_CHAN_Y], view_y);
  EXPECT_EQ(view->alphaPlane, view_alpha);
  EXPECT_TRUE(owner->imageOwnsYUVPlanes);
  EXPECT_TRUE(owner->imageOwnsAlphaPlane);
  EXPECT_FALSE(view->imageOwnsYUVPlanes);
  EXPECT_FALSE(view->imageOwnsAlphaPlane);
}

TEST(AvifImageTest, SetViewRectRejectsAlphaViewIntoDestination) {
  ImagePtr owner(avifImageCreate(/*width=*/16, /*height=*/16, /*depth=*/8,
                                 AVIF_PIXEL_FORMAT_NONE));
  ImagePtr view(avifImageCreateEmpty());
  ASSERT_NE(owner, nullptr);
  ASSERT_NE(view, nullptr);
  ASSERT_EQ(avifImageAllocatePlanes(owner.get(), AVIF_PLANES_A),
            AVIF_RESULT_OK);

  const avifCropRect outer_rect = {/*x=*/4, /*y=*/4, /*width=*/8,
                                   /*height=*/8};
  ASSERT_EQ(avifImageSetViewRect(view.get(), owner.get(), &outer_rect),
            AVIF_RESULT_OK);
  uint8_t* const owner_alpha = owner->alphaPlane;
  uint8_t* const view_alpha = view->alphaPlane;

  const avifCropRect inner_rect = {/*x=*/0, /*y=*/0, /*width=*/4,
                                   /*height=*/4};
  EXPECT_EQ(avifImageSetViewRect(owner.get(), view.get(), &inner_rect),
            AVIF_RESULT_INVALID_ARGUMENT);
  EXPECT_EQ(owner->alphaPlane, owner_alpha);
  EXPECT_EQ(view->alphaPlane, view_alpha);
  EXPECT_TRUE(owner->imageOwnsAlphaPlane);
  EXPECT_FALSE(view->imageOwnsAlphaPlane);
}

TEST(AvifImageTest, SetViewRectAllowsUnrelatedOwningDestination) {
  ImagePtr source(avifImageCreate(/*width=*/16, /*height=*/16, /*depth=*/8,
                                  AVIF_PIXEL_FORMAT_YUV444));
  ImagePtr destination(avifImageCreate(/*width=*/16, /*height=*/16,
                                       /*depth=*/8, AVIF_PIXEL_FORMAT_YUV444));
  ImagePtr view(avifImageCreateEmpty());
  ASSERT_NE(source, nullptr);
  ASSERT_NE(destination, nullptr);
  ASSERT_NE(view, nullptr);
  ASSERT_EQ(avifImageAllocatePlanes(source.get(), AVIF_PLANES_YUV),
            AVIF_RESULT_OK);
  ASSERT_EQ(avifImageAllocatePlanes(destination.get(), AVIF_PLANES_YUV),
            AVIF_RESULT_OK);

  const avifCropRect outer_rect = {/*x=*/4, /*y=*/4, /*width=*/8,
                                   /*height=*/8};
  ASSERT_EQ(avifImageSetViewRect(view.get(), source.get(), &outer_rect),
            AVIF_RESULT_OK);
  const avifCropRect inner_rect = {/*x=*/2, /*y=*/2, /*width=*/4,
                                   /*height=*/4};
  ASSERT_EQ(avifImageSetViewRect(destination.get(), view.get(), &inner_rect),
            AVIF_RESULT_OK);
  EXPECT_EQ(destination->yuvPlanes[AVIF_CHAN_Y],
            view->yuvPlanes[AVIF_CHAN_Y] +
                inner_rect.y * view->yuvRowBytes[AVIF_CHAN_Y] + inner_rect.x);
  EXPECT_FALSE(destination->imageOwnsYUVPlanes);
}

}  // namespace
}  // namespace avif
