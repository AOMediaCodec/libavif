// Copyright 2024 Google LLC
// SPDX-License-Identifier: BSD-2-Clause

#include "avif/avif.h"
#include "avif/avif_cxx.h"
#include "avif/internal.h"
#include "aviftest_helpers.h"
#include "gtest/gtest.h"

namespace avif {
namespace {

//------------------------------------------------------------------------------

class AvifExpression : public avifSampleTransformExpression {
 public:
  AvifExpression() : avifSampleTransformExpression{} {
    if (!avifArrayCreate(this, sizeof(avifSampleTransformToken), 1)) {
      abort();
    }
  }
  void AddConstant(int32_t constant) {
    avifSampleTransformToken& token = AddToken();
    token.value = AVIF_SAMPLE_TRANSFORM_CONSTANT;
    token.constant = constant;
  }
  void AddImage(uint8_t inputImageItemIndex) {
    avifSampleTransformToken& token = AddToken();
    token.value = AVIF_SAMPLE_TRANSFORM_INPUT_IMAGE_ITEM_INDEX;
    token.inputImageItemIndex = inputImageItemIndex;
  }
  void AddOperator(uint8_t op) {
    avifSampleTransformToken& token = AddToken();
    token.value = op;
  }
  ~AvifExpression() { avifArrayDestroy(this); }

 private:
  avifSampleTransformToken& AddToken() {
    avifSampleTransformToken* token =
        reinterpret_cast<avifSampleTransformToken*>(avifArrayPush(this));
    if (token == nullptr) abort();
    return *token;
  }
};

//------------------------------------------------------------------------------

TEST(SampleTransformTest, NoExpression) {
  AvifExpression empty;
  ASSERT_EQ(
      avifSampleTransformRecipeToExpression(AVIF_SAMPLE_TRANSFORM_NONE, &empty),
      AVIF_RESULT_INVALID_ARGUMENT);
  EXPECT_TRUE(avifSampleTransformExpressionIsEquivalentTo(&empty, &empty));
}

TEST(SampleTransformTest, NoRecipe) {
  AvifExpression empty;
  avifSampleTransformRecipe recipe;
  ASSERT_EQ(avifSampleTransformExpressionToRecipe(&empty, &recipe),
            AVIF_RESULT_OK);
  EXPECT_EQ(recipe, AVIF_SAMPLE_TRANSFORM_NONE);
}

TEST(SampleTransformTest, RecipeToExpression) {
  for (avifSampleTransformRecipe recipe :
       {AVIF_SAMPLE_TRANSFORM_BIT_DEPTH_EXTENSION_8B_8B,
        AVIF_SAMPLE_TRANSFORM_BIT_DEPTH_EXTENSION_12B_4B}) {
    AvifExpression expression;
    ASSERT_EQ(avifSampleTransformRecipeToExpression(recipe, &expression),
              AVIF_RESULT_OK);
    avifSampleTransformRecipe result;
    ASSERT_EQ(avifSampleTransformExpressionToRecipe(&expression, &result),
              AVIF_RESULT_OK);
    EXPECT_EQ(recipe, result);

    EXPECT_FALSE(avifSampleTransformExpressionIsValid(
        &expression, /*numInputImageItems=*/1));
    EXPECT_TRUE(avifSampleTransformExpressionIsValid(&expression,
                                                     /*numInputImageItems=*/2));
    EXPECT_TRUE(avifSampleTransformExpressionIsValid(&expression,
                                                     /*numInputImageItems=*/3));

    EXPECT_TRUE(
        avifSampleTransformExpressionIsEquivalentTo(&expression, &expression));
  }
}

TEST(SampleTransformTest, NotEquivalent) {
  AvifExpression a;
  ASSERT_EQ(avifSampleTransformRecipeToExpression(
                AVIF_SAMPLE_TRANSFORM_BIT_DEPTH_EXTENSION_8B_8B, &a),
            AVIF_RESULT_OK);

  AvifExpression b;
  ASSERT_EQ(avifSampleTransformRecipeToExpression(
                AVIF_SAMPLE_TRANSFORM_BIT_DEPTH_EXTENSION_12B_4B, &a),
            AVIF_RESULT_OK);

  EXPECT_FALSE(avifSampleTransformExpressionIsEquivalentTo(&a, &b));
}

//------------------------------------------------------------------------------

struct Op {
  int32_t left_operand;
  avifSampleTransformTokenType op;
  int32_t right_operand;
  uint32_t expected_result;
};

class SampleTransformOperationTest : public testing::TestWithParam<Op> {};

ImagePtr OneByOne(uint32_t depth) {
  ImagePtr image(avifImageCreate(/*width=*/1, /*height=*/1, depth,
                                 AVIF_PIXEL_FORMAT_YUV444));
  if (image.get() != nullptr &&
      avifImageAllocatePlanes(image.get(), AVIF_PLANES_YUV) == AVIF_RESULT_OK) {
    return image;
  }
  return nullptr;
}

TEST_P(SampleTransformOperationTest, Apply) {
  AvifExpression expression;
  // Postfix notation.
  expression.AddConstant(GetParam().left_operand);
  expression.AddConstant(GetParam().right_operand);
  expression.AddOperator(GetParam().op);

  ImagePtr result(avifImageCreate(/*width=*/1, /*height=*/1, /*depth=*/8,
                                  AVIF_PIXEL_FORMAT_YUV444));
  ASSERT_NE(result.get(), nullptr);
  ASSERT_EQ(avifImageAllocatePlanes(result.get(), AVIF_PLANES_YUV),
            AVIF_RESULT_OK);

  ASSERT_EQ(avifImageApplyExpression(
                result.get(), AVIF_SAMPLE_TRANSFORM_BIT_DEPTH_32, &expression,
                /*numInputImageItems=*/0, nullptr, AVIF_PLANES_YUV),
            AVIF_RESULT_OK);
  EXPECT_EQ(result->yuvPlanes[0][0], GetParam().expected_result);
}

INSTANTIATE_TEST_SUITE_P(
    Operations, SampleTransformOperationTest,
    testing::Values(Op{1, AVIF_SAMPLE_TRANSFORM_SUM, 1, 2},
                    Op{255, AVIF_SAMPLE_TRANSFORM_SUM, 255, 255},
                    Op{1, AVIF_SAMPLE_TRANSFORM_DIFFERENCE, 1, 0},
                    Op{255, AVIF_SAMPLE_TRANSFORM_DIFFERENCE, 255, 0},
                    Op{255, AVIF_SAMPLE_TRANSFORM_DIFFERENCE, 0, 255},
                    Op{0, AVIF_SAMPLE_TRANSFORM_DIFFERENCE, 255, 0},
                    Op{1, AVIF_SAMPLE_TRANSFORM_DIFFERENCE, -1, 2},
                    Op{-1, AVIF_SAMPLE_TRANSFORM_DIFFERENCE, 1, 0},
                    Op{1, AVIF_SAMPLE_TRANSFORM_PRODUCT, 1, 1},
                    Op{2, AVIF_SAMPLE_TRANSFORM_PRODUCT, 3, 6},
                    Op{1, AVIF_SAMPLE_TRANSFORM_DIVIDE, 1, 1},
                    Op{2, AVIF_SAMPLE_TRANSFORM_DIVIDE, 3, 0},
                    Op{1, AVIF_SAMPLE_TRANSFORM_AND, 1, 1},
                    Op{1, AVIF_SAMPLE_TRANSFORM_AND, 2, 0},
                    Op{7, AVIF_SAMPLE_TRANSFORM_AND, 15, 7},
                    Op{1, AVIF_SAMPLE_TRANSFORM_OR, 1, 1},
                    Op{1, AVIF_SAMPLE_TRANSFORM_OR, 2, 3},
                    Op{1, AVIF_SAMPLE_TRANSFORM_XOR, 3, 2},
                    Op{254, AVIF_SAMPLE_TRANSFORM_NOR, 1, 0},
                    Op{0, AVIF_SAMPLE_TRANSFORM_MSB, 123, 123},
                    Op{61, AVIF_SAMPLE_TRANSFORM_MSB, 123, 5},
                    Op{2, AVIF_SAMPLE_TRANSFORM_POW, 4, 16},
                    Op{4, AVIF_SAMPLE_TRANSFORM_POW, 2, 16},
                    Op{123, AVIF_SAMPLE_TRANSFORM_POW, 123, 255},
                    Op{123, AVIF_SAMPLE_TRANSFORM_MIN, 124, 123},
                    Op{123, AVIF_SAMPLE_TRANSFORM_MAX, 124, 124}));

//------------------------------------------------------------------------------

}  // namespace
}  // namespace avif
