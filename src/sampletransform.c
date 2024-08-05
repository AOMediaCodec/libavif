// Copyright 2024 Google LLC
// SPDX-License-Identifier: BSD-2-Clause

#include "avif/internal.h"

#include <assert.h>
#include <stdint.h>
#include <string.h>

//------------------------------------------------------------------------------
// Convenience functions

avifBool avifSampleTransformExpressionIsValid(const avifSampleTransformExpression * tokens, uint32_t numInputImageItems)
{
    uint32_t stackSize = 0;
    for (uint32_t t = 0; t < tokens->count; ++t) {
        const avifSampleTransformToken * token = &tokens->tokens[t];
        AVIF_CHECK(token->type < AVIF_SAMPLE_TRANSFORM_RESERVED);
        if (token->type == AVIF_SAMPLE_TRANSFORM_INPUT_IMAGE_ITEM_INDEX) {
            // inputImageItemIndex is 1-based.
            AVIF_CHECK(token->inputImageItemIndex != 0);
            AVIF_CHECK(token->inputImageItemIndex <= numInputImageItems);
        }
        if (token->type == AVIF_SAMPLE_TRANSFORM_CONSTANT || token->type == AVIF_SAMPLE_TRANSFORM_INPUT_IMAGE_ITEM_INDEX) {
            ++stackSize;
        } else if (token->type == AVIF_SAMPLE_TRANSFORM_NEGATE || token->type == AVIF_SAMPLE_TRANSFORM_ABSOLUTE ||
                   token->type == AVIF_SAMPLE_TRANSFORM_NOT || token->type == AVIF_SAMPLE_TRANSFORM_MSB) {
            AVIF_CHECK(stackSize >= 1);
            // Pop one and push one.
        } else {
            AVIF_CHECK(stackSize >= 2);
            --stackSize; // Pop two and push one.
        }
    }
    AVIF_CHECK(stackSize == 1);
    return AVIF_TRUE;
}

avifBool avifSampleTransformExpressionIsEquivalentTo(const avifSampleTransformExpression * a, const avifSampleTransformExpression * b)
{
    if (a->count != b->count) {
        return AVIF_FALSE;
    }

    for (uint32_t t = 0; t < a->count; ++t) {
        const avifSampleTransformToken * aToken = &a->tokens[t];
        const avifSampleTransformToken * bToken = &b->tokens[t];
        if (aToken->type != bToken->type || (aToken->type == AVIF_SAMPLE_TRANSFORM_CONSTANT && aToken->constant != bToken->constant)) {
            return AVIF_FALSE;
        }
        // For AVIF_SAMPLE_TRANSFORM_INPUT_IMAGE_ITEM_INDEX, no need to compare inputImageItemIndex
        // because these are variables in the expression.
    }
    return AVIF_TRUE;
}

//------------------------------------------------------------------------------
// Recipe to expression

static avifBool avifPushConstant(avifSampleTransformExpression * expression, int32_t constant)
{
    avifSampleTransformToken * token = (avifSampleTransformToken *)avifArrayPush(expression);
    if (token == NULL) {
        return AVIF_FALSE;
    }
    token->type = AVIF_SAMPLE_TRANSFORM_CONSTANT;
    token->constant = constant;
    return AVIF_TRUE;
}
static avifBool avifPushInputImageItem(avifSampleTransformExpression * expression, uint8_t inputImageItemIndex)
{
    avifSampleTransformToken * token = (avifSampleTransformToken *)avifArrayPush(expression);
    if (token == NULL) {
        return AVIF_FALSE;
    }
    token->type = AVIF_SAMPLE_TRANSFORM_INPUT_IMAGE_ITEM_INDEX;
    token->inputImageItemIndex = inputImageItemIndex;
    return AVIF_TRUE;
}
static avifBool avifPushOperator(avifSampleTransformExpression * expression, avifSampleTransformTokenType operator)
{
    avifSampleTransformToken * token = (avifSampleTransformToken *)avifArrayPush(expression);
    if (token == NULL) {
        return AVIF_FALSE;
    }
    token->type = (uint8_t) operator;
    return AVIF_TRUE;
}

avifResult avifSampleTransformRecipeToExpression(avifSampleTransformRecipe recipe, avifSampleTransformExpression * expression)
{
    // Postfix (or Reverse Polish) notation. Brackets to highlight sub-expressions.

    if (recipe == AVIF_SAMPLE_TRANSFORM_BIT_DEPTH_EXTENSION_8B_8B) {
        // reference_count is two: two 8-bit input images.
        //   (base_sample << 8) | hidden_sample
        // Note: base_sample is encoded losslessly. hidden_sample is encoded lossily or losslessly.
        AVIF_CHECKERR(avifArrayCreate(expression, sizeof(avifSampleTransformToken), 5), AVIF_RESULT_OUT_OF_MEMORY);

        {
            // The base image represents the 8 most significant bits of the reconstructed, bit-depth-extended output image.
            // Left shift the base image (which is also the primary item, or the auxiliary alpha item of the primary item)
            // by 8 bits. This is equivalent to multiplying by 2^8.
            AVIF_ASSERT_OR_RETURN(avifPushConstant(expression, 256));
            AVIF_ASSERT_OR_RETURN(avifPushInputImageItem(expression, 1));
            AVIF_ASSERT_OR_RETURN(avifPushOperator(expression, AVIF_SAMPLE_TRANSFORM_PRODUCT));
        }
        {
            // The second image represents the 8 least significant bits of the reconstructed, bit-depth-extended output image.
            AVIF_ASSERT_OR_RETURN(avifPushInputImageItem(expression, 2));
        }
        AVIF_ASSERT_OR_RETURN(avifPushOperator(expression, AVIF_SAMPLE_TRANSFORM_OR));
        return AVIF_RESULT_OK;
    }

    if (recipe == AVIF_SAMPLE_TRANSFORM_BIT_DEPTH_EXTENSION_12B_4B) {
        // reference_count is two: one 12-bit input image and one 8-bit input image (because AV1 does not support 4-bit samples).
        //   (base_sample << 4) | (hidden_sample >> 4)
        // Note: base_sample is encoded losslessly. hidden_sample is encoded lossily or losslessly.
        AVIF_CHECKERR(avifArrayCreate(expression, sizeof(avifSampleTransformToken), 7), AVIF_RESULT_OUT_OF_MEMORY);

        {
            // The base image represents the 12 most significant bits of the reconstructed, bit-depth-extended output image.
            // Left shift the base image (which is also the primary item, or the auxiliary alpha item of the primary item)
            // by 4 bits. This is equivalent to multiplying by 2^4.
            AVIF_ASSERT_OR_RETURN(avifPushConstant(expression, 16));
            AVIF_ASSERT_OR_RETURN(avifPushInputImageItem(expression, 1));
            AVIF_ASSERT_OR_RETURN(avifPushOperator(expression, AVIF_SAMPLE_TRANSFORM_PRODUCT));
        }
        {
            // The second image represents the 4 least significant bits of the reconstructed, bit-depth-extended output image.
            AVIF_ASSERT_OR_RETURN(avifPushInputImageItem(expression, 2));
            AVIF_ASSERT_OR_RETURN(avifPushConstant(expression, 16));
            AVIF_ASSERT_OR_RETURN(avifPushOperator(expression, AVIF_SAMPLE_TRANSFORM_DIVIDE));
        }
        AVIF_ASSERT_OR_RETURN(avifPushOperator(expression, AVIF_SAMPLE_TRANSFORM_SUM));
        return AVIF_RESULT_OK;
    }

    if (recipe == AVIF_SAMPLE_TRANSFORM_BIT_DEPTH_EXTENSION_12B_8B_OVERLAP_4B) {
        // reference_count is two: one 12-bit input image and one 8-bit input image.
        //   (base_sample << 4) + hidden_sample
        // Note: Both base_sample and hidden_sample are encoded lossily or losslessly. hidden_sample overlaps
        //       with base_sample by 4 bits to alleviate the loss caused by the quantization of base_sample.
        AVIF_CHECKERR(avifArrayCreate(expression, sizeof(avifSampleTransformToken), 7), AVIF_RESULT_OUT_OF_MEMORY);

        // The base image represents the 12 most significant bits of the reconstructed, bit-depth-extended output image.
        // Left shift the base image (which is also the primary item, or the auxiliary alpha item of the primary item)
        // by 4 bits. This is equivalent to multiplying by 2^4.
        AVIF_ASSERT_OR_RETURN(avifPushConstant(expression, 16));
        AVIF_ASSERT_OR_RETURN(avifPushInputImageItem(expression, 1));
        AVIF_ASSERT_OR_RETURN(avifPushOperator(expression, AVIF_SAMPLE_TRANSFORM_PRODUCT));

        // The second image represents the offset to apply to the shifted base image to retrieve
        // the original image, with some loss due to quantization.
        AVIF_ASSERT_OR_RETURN(avifPushInputImageItem(expression, 2));
        AVIF_ASSERT_OR_RETURN(avifPushOperator(expression, AVIF_SAMPLE_TRANSFORM_SUM));

        // The second image is offset by 128 to have unsigned values to encode.
        // Correct that last to always work with unsigned values in the operations above.
        AVIF_ASSERT_OR_RETURN(avifPushConstant(expression, 128));
        AVIF_ASSERT_OR_RETURN(avifPushOperator(expression, AVIF_SAMPLE_TRANSFORM_DIFFERENCE));
        // Sample values are clamped to [0:1<<depth[ at that point.
        return AVIF_RESULT_OK;
    }

    return AVIF_RESULT_INVALID_ARGUMENT;
}

avifResult avifSampleTransformExpressionToRecipe(const avifSampleTransformExpression * expression, avifSampleTransformRecipe * recipe)
{
    *recipe = AVIF_SAMPLE_TRANSFORM_NONE;
    const avifSampleTransformRecipe kAllRecipes[] = { AVIF_SAMPLE_TRANSFORM_BIT_DEPTH_EXTENSION_8B_8B,
                                                      AVIF_SAMPLE_TRANSFORM_BIT_DEPTH_EXTENSION_12B_4B,
                                                      AVIF_SAMPLE_TRANSFORM_BIT_DEPTH_EXTENSION_12B_8B_OVERLAP_4B };
    for (size_t i = 0; i < sizeof(kAllRecipes) / sizeof(kAllRecipes[0]); ++i) {
        avifSampleTransformRecipe candidateRecipe = kAllRecipes[i];
        avifSampleTransformExpression candidateExpression = { 0 };
        AVIF_CHECKRES(avifSampleTransformRecipeToExpression(candidateRecipe, &candidateExpression));
        const avifBool equivalence = avifSampleTransformExpressionIsEquivalentTo(expression, &candidateExpression);
        avifArrayDestroy(&candidateExpression);
        if (equivalence) {
            *recipe = candidateRecipe;
            return AVIF_RESULT_OK;
        }
    }
    return AVIF_RESULT_OK;
}

//------------------------------------------------------------------------------
// Operators

static int32_t avifSampleTransformClamp32b(int64_t value)
{
    return value <= INT32_MIN ? INT32_MIN : value >= INT32_MAX ? INT32_MAX : (int32_t)value;
}

static int32_t avifSampleTransformOperation32bOneOperand(int32_t operand, uint8_t operator)
{
    switch (operator) {
        case AVIF_SAMPLE_TRANSFORM_NEGATE:
            return avifSampleTransformClamp32b(-(int64_t)operand);
        case AVIF_SAMPLE_TRANSFORM_ABSOLUTE:
            return operand >= 0 ? operand : avifSampleTransformClamp32b(-(int64_t)operand);
        case AVIF_SAMPLE_TRANSFORM_NOT:
            return ~operand;
        case AVIF_SAMPLE_TRANSFORM_MSB: {
            if (operand <= 0) {
                return 0;
            }
            int32_t log2 = 0;
            operand >>= 1;
            for (; operand != 0; ++log2) {
                operand >>= 1;
            }
            return log2;
        }
        default:
            assert(AVIF_FALSE);
    }
    return 0;
}

static int32_t avifSampleTransformOperation32bTwoOperands(int32_t leftOperand, int32_t rightOperand, uint8_t operator)
{
    switch (operator) {
        case AVIF_SAMPLE_TRANSFORM_SUM:
            return avifSampleTransformClamp32b(leftOperand + rightOperand);
        case AVIF_SAMPLE_TRANSFORM_DIFFERENCE:
            return avifSampleTransformClamp32b(leftOperand - rightOperand);
        case AVIF_SAMPLE_TRANSFORM_PRODUCT:
            return avifSampleTransformClamp32b(leftOperand * rightOperand);
        case AVIF_SAMPLE_TRANSFORM_DIVIDE:
            return rightOperand == 0 ? leftOperand : leftOperand / rightOperand;
        case AVIF_SAMPLE_TRANSFORM_AND:
            return leftOperand & rightOperand;
        case AVIF_SAMPLE_TRANSFORM_OR:
            return leftOperand | rightOperand;
        case AVIF_SAMPLE_TRANSFORM_XOR:
            return leftOperand ^ rightOperand;
        case AVIF_SAMPLE_TRANSFORM_POW: {
            if (leftOperand == 0 || leftOperand == 1) {
                return leftOperand;
            }
            const uint32_t exponent = rightOperand > 0 ? (uint32_t)rightOperand : (uint32_t) - (int64_t)rightOperand;
            if (exponent == 0) {
                return 1;
            }
            if (exponent == 1) {
                return leftOperand;
            }
            if (leftOperand == -1) {
                return (exponent % 2 == 0) ? 1 : -1;
            }

            int64_t result = leftOperand;
            for (uint32_t i = 1; i < exponent; ++i) {
                result *= leftOperand;
                if (result <= INT32_MIN) {
                    return INT32_MIN;
                } else if (result >= INT32_MAX) {
                    return INT32_MAX;
                }
            }
            return (int32_t)result;
        }
        case AVIF_SAMPLE_TRANSFORM_MIN:
            return leftOperand <= rightOperand ? leftOperand : rightOperand;
        case AVIF_SAMPLE_TRANSFORM_MAX:
            return leftOperand <= rightOperand ? rightOperand : leftOperand;
        default:
            assert(AVIF_FALSE);
    }
    return 0;
}

//------------------------------------------------------------------------------
// Expression

AVIF_ARRAY_DECLARE(avifSampleTransformStack32b, int32_t, elements);

static avifResult avifImageApplyExpression32b(avifImage * dstImage,
                                              const avifSampleTransformExpression * expression,
                                              const avifImage * inputImageItems[],
                                              avifPlanesFlags planes,
                                              int32_t * stack,
                                              uint32_t stackCapacity)
{
    // This slow path could be avoided by recognizing the recipe thanks to avifSampleTransformExpressionToRecipe()
    // and having a dedicated optimized implementation for each recipe.

    const int32_t minValue = 0;
    const int32_t maxValue = (1 << dstImage->depth) - 1;

    const avifBool skipColor = !(planes & AVIF_PLANES_YUV);
    const avifBool skipAlpha = !(planes & AVIF_PLANES_A);
    for (int c = AVIF_CHAN_Y; c <= AVIF_CHAN_A; ++c) {
        const avifBool alpha = c == AVIF_CHAN_A;
        if ((skipColor && !alpha) || (skipAlpha && alpha)) {
            continue;
        }

        const uint32_t planeWidth = avifImagePlaneWidth(dstImage, c);
        const uint32_t planeHeight = avifImagePlaneHeight(dstImage, c);
        for (uint32_t y = 0; y < planeHeight; ++y) {
            for (uint32_t x = 0; x < planeWidth; ++x) {
                uint32_t stackSize = 0;
                for (uint32_t t = 0; t < expression->count; ++t) {
                    const avifSampleTransformToken * token = &expression->tokens[t];
                    if (token->type == AVIF_SAMPLE_TRANSFORM_CONSTANT) {
                        AVIF_ASSERT_OR_RETURN(stackSize < stackCapacity);
                        stack[stackSize++] = token->constant;
                    } else if (token->type == AVIF_SAMPLE_TRANSFORM_INPUT_IMAGE_ITEM_INDEX) {
                        const avifImage * image = inputImageItems[token->inputImageItemIndex - 1]; // 1-based
                        const uint8_t * row = avifImagePlane(image, c) + avifImagePlaneRowBytes(image, c) * y;
                        AVIF_ASSERT_OR_RETURN(stackSize < stackCapacity);
                        stack[stackSize++] = avifImageUsesU16(image) ? ((const uint16_t *)row)[x] : row[x];
                    } else if (token->type == AVIF_SAMPLE_TRANSFORM_NEGATE || token->type == AVIF_SAMPLE_TRANSFORM_ABSOLUTE ||
                               token->type == AVIF_SAMPLE_TRANSFORM_NOT || token->type == AVIF_SAMPLE_TRANSFORM_MSB) {
                        AVIF_ASSERT_OR_RETURN(stackSize >= 1);
                        stack[stackSize - 1] = avifSampleTransformOperation32bOneOperand(stack[stackSize - 1], token->type);
                        // Pop one and push one.
                    } else {
                        AVIF_ASSERT_OR_RETURN(stackSize >= 2);
                        stack[stackSize - 2] =
                            avifSampleTransformOperation32bTwoOperands(stack[stackSize - 2], stack[stackSize - 1], token->type);
                        stackSize--; // Pop two and push one.
                    }
                }
                AVIF_ASSERT_OR_RETURN(stackSize == 1);
                // Fit to 'pixi'-defined range. TODO(yguyon): Take avifRange into account.
                stack[0] = AVIF_CLAMP(stack[0], minValue, maxValue);

                uint8_t * row = avifImagePlane(dstImage, c) + avifImagePlaneRowBytes(dstImage, c) * y;
                if (avifImageUsesU16(dstImage)) {
                    ((uint16_t *)row)[x] = (uint16_t)stack[0];
                } else {
                    row[x] = (uint8_t)stack[0];
                }
            }
        }
    }
    return AVIF_RESULT_OK;
}

avifResult avifImageApplyExpression(avifImage * dstImage,
                                    avifSampleTransformBitDepth bitDepth,
                                    const avifSampleTransformExpression * expression,
                                    uint8_t numInputImageItems,
                                    const avifImage * inputImageItems[],
                                    avifPlanesFlags planes)
{
    // Check that the expression is valid.
    AVIF_ASSERT_OR_RETURN(avifSampleTransformExpressionIsValid(expression, numInputImageItems));
    const avifBool skipColor = !(planes & AVIF_PLANES_YUV);
    const avifBool skipAlpha = !(planes & AVIF_PLANES_A);
    for (int c = AVIF_CHAN_Y; c <= AVIF_CHAN_A; ++c) {
        const avifBool alpha = c == AVIF_CHAN_A;
        if ((skipColor && !alpha) || (skipAlpha && alpha)) {
            continue;
        }

        const uint32_t planeWidth = avifImagePlaneWidth(dstImage, c);
        const uint32_t planeHeight = avifImagePlaneHeight(dstImage, c);
        for (uint32_t i = 0; i < numInputImageItems; ++i) {
            AVIF_CHECKERR(avifImagePlaneWidth(inputImageItems[i], c) == planeWidth, AVIF_RESULT_BMFF_PARSE_FAILED);
            AVIF_CHECKERR(avifImagePlaneHeight(inputImageItems[i], c) == planeHeight, AVIF_RESULT_BMFF_PARSE_FAILED);
        }
    }

    // Then apply it. This part should not fail except for memory shortage reasons.
    if (bitDepth == AVIF_SAMPLE_TRANSFORM_BIT_DEPTH_32) {
        uint32_t stackCapacity = expression->count / 2 + 1;
        int32_t * stack = avifAlloc(stackCapacity * sizeof(int32_t));
        AVIF_CHECKERR(stack != NULL, AVIF_RESULT_OUT_OF_MEMORY);
        const avifResult result = avifImageApplyExpression32b(dstImage, expression, inputImageItems, planes, stack, stackCapacity);
        avifFree(stack);
        return result;
    }
    return AVIF_RESULT_NOT_IMPLEMENTED;
}

avifResult avifImageApplyOperations(avifImage * dstImage,
                                    avifSampleTransformBitDepth bitDepth,
                                    uint32_t numTokens,
                                    const avifSampleTransformToken tokens[],
                                    uint8_t numInputImageItems,
                                    const avifImage * inputImageItems[],
                                    avifPlanesFlags planes)
{
    avifSampleTransformExpression expression = { 0 };
    AVIF_CHECKERR(avifArrayCreate(&expression, sizeof(avifSampleTransformToken), numTokens), AVIF_RESULT_OUT_OF_MEMORY);
    for (uint32_t t = 0; t < numTokens; ++t) {
        avifSampleTransformToken * token = (avifSampleTransformToken *)avifArrayPush(&expression);
        AVIF_ASSERT_OR_RETURN(token != NULL);
        *token = tokens[t];
    }
    const avifResult result = avifImageApplyExpression(dstImage, bitDepth, &expression, numInputImageItems, inputImageItems, planes);
    avifArrayDestroy(&expression);
    return result;
}
