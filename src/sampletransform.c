// Copyright 2023 Google LLC
// SPDX-License-Identifier: BSD-2-Clause

#include "avif/internal.h"

#include <assert.h>
#include <stdint.h>
#include <string.h>

avifResult avifImageTransformImageAndImageSamples(avifImage * result,
                                                  avifSampleTransformIntermediateBitDepth intermediateBitDepth,
                                                  const avifImage * leftOperand,
                                                  avifSampleTransformOperation operation,
                                                  const avifImage * rightOperand,
                                                  avifPlanesFlags planes)
{
    if (!(planes & AVIF_PLANES_YUV) && !(planes & AVIF_PLANES_A)) {
        // Early exit.
        return AVIF_RESULT_OK;
    }

    // TODO(yguyon): Implement other avifSampleTransformIntermediateBitDepth values.
    AVIF_CHECKERR(intermediateBitDepth == AVIF_SAMPLE_TRANSFORM_INTERMEDIATE_BIT_DEPTH_32, AVIF_RESULT_NOT_IMPLEMENTED);
    AVIF_CHECKERR(avifImageUsesU16(leftOperand), AVIF_RESULT_NOT_IMPLEMENTED); // libavif only uses 16-bit leftOperand.
    AVIF_CHECKERR(avifImageUsesU16(result), AVIF_RESULT_NOT_IMPLEMENTED);      // libavif only uses 16-bit result.

    const avifBool skipColor = !(planes & AVIF_PLANES_YUV);
    const avifBool skipAlpha = !(planes & AVIF_PLANES_A);
    for (int c = AVIF_CHAN_Y; c <= AVIF_CHAN_A; ++c) {
        const avifBool alpha = c == AVIF_CHAN_A;
        if ((skipColor && !alpha) || (skipAlpha && alpha)) {
            continue;
        }

        const uint32_t planeWidth = avifImagePlaneWidth(leftOperand, c);
        const uint32_t planeHeight = avifImagePlaneHeight(leftOperand, c);
        const uint8_t * leftOperandRow = avifImagePlane(leftOperand, c);
        const uint8_t * rightOperandRow = avifImagePlane(rightOperand, c);
        uint8_t * resultRow = avifImagePlane(result, c);
        const uint32_t leftOperandRowBytes = avifImagePlaneRowBytes(leftOperand, c);
        const uint32_t rightOperandRowBytes = avifImagePlaneRowBytes(rightOperand, c);
        const uint32_t resultRowBytes = avifImagePlaneRowBytes(result, c);
        AVIF_CHECKERR(!leftOperandRow == !resultRow, AVIF_RESULT_INVALID_ARGUMENT);
        AVIF_CHECKERR(!rightOperandRow == !resultRow, AVIF_RESULT_INVALID_ARGUMENT);
        if (!leftOperandRow) {
            continue;
        }
        AVIF_CHECKERR(planeWidth == avifImagePlaneWidth(rightOperand, c), AVIF_RESULT_INVALID_ARGUMENT);
        AVIF_CHECKERR(planeHeight == avifImagePlaneHeight(rightOperand, c), AVIF_RESULT_INVALID_ARGUMENT);
        AVIF_CHECKERR(planeWidth == avifImagePlaneWidth(result, c), AVIF_RESULT_INVALID_ARGUMENT);
        AVIF_CHECKERR(planeHeight == avifImagePlaneHeight(result, c), AVIF_RESULT_INVALID_ARGUMENT);

        if (operation == AVIF_SAMPLE_TRANSFORM_OR) {
            // Reminder of the above checks.
            assert(avifImageUsesU16(leftOperand));
            assert(avifImageUsesU16(result));

            if (avifImageUsesU16(rightOperand)) {
                for (uint32_t y = 0; y < planeHeight; ++y) {
                    const uint16_t * leftOperandRow16 = (const uint16_t *)leftOperandRow;
                    const uint16_t * rightOperandRow16 = (const uint16_t *)rightOperandRow;
                    uint16_t * resultRow16 = (uint16_t *)resultRow;
                    for (uint32_t x = 0; x < planeWidth; ++x) {
                        resultRow16[x] = (uint16_t)(leftOperandRow16[x] | (uint16_t)rightOperandRow16[x]);
                    }
                    leftOperandRow += leftOperandRowBytes;
                    rightOperandRow += rightOperandRowBytes;
                    resultRow += resultRowBytes;
                }
            } else {
                for (uint32_t y = 0; y < planeHeight; ++y) {
                    const uint16_t * leftOperandRow16 = (const uint16_t *)leftOperandRow;
                    uint16_t * resultRow16 = (uint16_t *)resultRow;
                    for (uint32_t x = 0; x < planeWidth; ++x) {
                        resultRow16[x] = (uint16_t)(leftOperandRow16[x] | (uint16_t)rightOperandRow[x]);
                    }
                    leftOperandRow += leftOperandRowBytes;
                    rightOperandRow += rightOperandRowBytes;
                    resultRow += resultRowBytes;
                }
            }
            continue;
        }

        // The remaining operations are not used in libavif for now.
        return AVIF_RESULT_NOT_IMPLEMENTED;
    }
    return AVIF_RESULT_OK;
}

avifResult avifImageTransformConstantAndImageSamples(avifImage * result,
                                                     avifSampleTransformIntermediateBitDepth intermediateBitDepth,
                                                     int32_t leftOperand,
                                                     avifSampleTransformOperation operation,
                                                     const avifImage * rightOperand,
                                                     avifPlanesFlags planes)
{
    if (!(planes & AVIF_PLANES_YUV) && !(planes & AVIF_PLANES_A)) {
        // Early exit.
        return AVIF_RESULT_OK;
    }

    // Is calling this function with leftOperand and operation equivalent to copying rightOperand samples to result?
    const avifBool noop = (leftOperand == 0 && operation == AVIF_SAMPLE_TRANSFORM_SUM) ||
                          (leftOperand == 1 && operation == AVIF_SAMPLE_TRANSFORM_PRODUCT) ||
                          (operation == AVIF_SAMPLE_TRANSFORM_DIVIDE_REVERSED && leftOperand == 1) ||
                          (operation == AVIF_SAMPLE_TRANSFORM_POW_REVERSED && leftOperand == 1) ||
                          (operation == AVIF_SAMPLE_TRANSFORM_LOG_REVERSED && leftOperand == 1) ||
                          (leftOperand == INT32_MAX && operation == AVIF_SAMPLE_TRANSFORM_AND) || // rightOperand is positive
                          (leftOperand == 0 && operation == AVIF_SAMPLE_TRANSFORM_OR) ||
                          (leftOperand == INT32_MAX && operation == AVIF_SAMPLE_TRANSFORM_MIN) ||
                          (leftOperand == 0 && operation == AVIF_SAMPLE_TRANSFORM_MAX);
    if (noop && result == rightOperand) {
        // Early exit.
        return AVIF_RESULT_OK;
    }
    const avifBool leftOperandIsPowerOfTwo = (leftOperand > 0) && ((leftOperand & (leftOperand - 1)) == 0);
    uint32_t leftOperandLog2 = 0;
    if (leftOperandIsPowerOfTwo) {
        while ((1 << leftOperandLog2) < leftOperand) {
            ++leftOperandLog2;
        }
    }

    // Is calling this function with leftOperand and operation equivalent to setting all result samples to zero?
    const avifBool clear = (leftOperand == 0 && operation == AVIF_SAMPLE_TRANSFORM_PRODUCT) ||
                           (operation == AVIF_SAMPLE_TRANSFORM_DIVIDE_REVERSED && leftOperand > (1 << rightOperand->depth)) ||
                           (leftOperand == 0 && operation == AVIF_SAMPLE_TRANSFORM_AND) ||
                           (leftOperand == 0 && operation == AVIF_SAMPLE_TRANSFORM_MIN); // rightOperand is positive

    // TODO(yguyon): Implement other avifSampleTransformIntermediateBitDepth values.
    AVIF_CHECKERR(intermediateBitDepth == AVIF_SAMPLE_TRANSFORM_INTERMEDIATE_BIT_DEPTH_32, AVIF_RESULT_NOT_IMPLEMENTED);

    const avifBool skipColor = !(planes & AVIF_PLANES_YUV);
    const avifBool skipAlpha = !(planes & AVIF_PLANES_A);
    for (int c = AVIF_CHAN_Y; c <= AVIF_CHAN_A; ++c) {
        const avifBool alpha = c == AVIF_CHAN_A;
        if ((skipColor && !alpha) || (skipAlpha && alpha)) {
            continue;
        }

        const uint32_t planeWidth = avifImagePlaneWidth(rightOperand, c);
        const uint32_t planeHeight = avifImagePlaneHeight(rightOperand, c);
        const uint8_t * rightOperandRow = avifImagePlane(rightOperand, c);
        uint8_t * resultRow = avifImagePlane(result, c);
        const uint32_t rightOperandRowBytes = avifImagePlaneRowBytes(rightOperand, c);
        const uint32_t resultRowBytes = avifImagePlaneRowBytes(result, c);
        AVIF_CHECKERR(!rightOperandRow == !resultRow, AVIF_RESULT_INVALID_ARGUMENT);
        if (!rightOperandRow) {
            continue;
        }
        AVIF_CHECKERR(planeWidth == avifImagePlaneWidth(result, c), AVIF_RESULT_INVALID_ARGUMENT);
        AVIF_CHECKERR(planeHeight == avifImagePlaneHeight(result, c), AVIF_RESULT_INVALID_ARGUMENT);

        if (noop) {
            // Just copy the rightOperand samples to result.
            if (avifImageUsesU16(rightOperand) == avifImageUsesU16(result)) {
                // The raw bytes can be copied.
                const size_t planeWidthBytes = planeWidth * (avifImageUsesU16(rightOperand) ? 2 : 1);
                for (uint32_t y = 0; y < planeHeight; ++y) {
                    memcpy(resultRow, rightOperandRow, planeWidthBytes);
                    rightOperandRow += rightOperandRowBytes;
                    resultRow += resultRowBytes;
                }
            } else {
                AVIF_CHECKERR(avifImageUsesU16(result), AVIF_RESULT_INVALID_ARGUMENT); // Cannot fit 16-bit samples into 8 bits.
                for (uint32_t y = 0; y < planeHeight; ++y) {
                    uint16_t * resultRow16 = (uint16_t *)resultRow;
                    for (uint32_t x = 0; x < planeWidth; ++x) {
                        resultRow16[x] = rightOperandRow[x]; // 8-bit to 16-bit
                    }
                    rightOperandRow += rightOperandRowBytes;
                    resultRow += resultRowBytes;
                }
            }
            continue;
        }

        if (clear) {
            // Just set the result samples to zero.
            const size_t planeWidthBytes = planeWidth * (avifImageUsesU16(result) ? 2 : 1);
            for (uint32_t y = 0; y < planeHeight; ++y) {
                memset(resultRow, 0, planeWidthBytes);
                resultRow += resultRowBytes;
            }
            continue;
        }

        if (operation == AVIF_SAMPLE_TRANSFORM_SUM) {
            if (avifImageUsesU16(rightOperand)) {
                AVIF_CHECKERR(avifImageUsesU16(result), AVIF_RESULT_INVALID_ARGUMENT); // Cannot fit 16-bit samples into 8 bits.
                for (uint32_t y = 0; y < planeHeight; ++y) {
                    const uint16_t * rightOperandRow16 = (const uint16_t *)rightOperandRow;
                    uint16_t * resultRow16 = (uint16_t *)resultRow;
                    for (uint32_t x = 0; x < planeWidth; ++x) {
                        resultRow16[x] = (uint16_t)(leftOperand + rightOperandRow16[x]);
                    }
                    rightOperandRow += rightOperandRowBytes;
                    resultRow += resultRowBytes;
                }
            } else {
                if (avifImageUsesU16(result)) {
                    for (uint32_t y = 0; y < planeHeight; ++y) {
                        uint16_t * resultRow16 = (uint16_t *)resultRow;
                        for (uint32_t x = 0; x < planeWidth; ++x) {
                            resultRow16[x] = (uint16_t)(leftOperand + rightOperandRow[x]);
                        }
                        rightOperandRow += rightOperandRowBytes;
                        resultRow += resultRowBytes;
                    }
                } else {
                    for (uint32_t y = 0; y < planeHeight; ++y) {
                        for (uint32_t x = 0; x < planeWidth; ++x) {
                            resultRow[x] = (uint8_t)(leftOperand + rightOperandRow[x]);
                        }
                        rightOperandRow += rightOperandRowBytes;
                        resultRow += resultRowBytes;
                    }
                }
            }
            continue;
        }

        if (operation == AVIF_SAMPLE_TRANSFORM_PRODUCT && leftOperandIsPowerOfTwo) {
            if (avifImageUsesU16(rightOperand)) {
                AVIF_CHECKERR(avifImageUsesU16(result), AVIF_RESULT_INVALID_ARGUMENT); // Cannot fit 16-bit samples into 8 bits.
                for (uint32_t y = 0; y < planeHeight; ++y) {
                    const uint16_t * rightOperandRow16 = (const uint16_t *)rightOperandRow;
                    uint16_t * resultRow16 = (uint16_t *)resultRow;
                    for (uint32_t x = 0; x < planeWidth; ++x) {
                        resultRow16[x] = (uint16_t)(rightOperandRow16[x] << leftOperandLog2);
                    }
                    rightOperandRow += rightOperandRowBytes;
                    resultRow += resultRowBytes;
                }
            } else {
                if (avifImageUsesU16(result)) {
                    for (uint32_t y = 0; y < planeHeight; ++y) {
                        uint16_t * resultRow16 = (uint16_t *)resultRow;
                        for (uint32_t x = 0; x < planeWidth; ++x) {
                            resultRow16[x] = (uint16_t)(rightOperandRow[x] << leftOperandLog2);
                        }
                        rightOperandRow += rightOperandRowBytes;
                        resultRow += resultRowBytes;
                    }
                } else {
                    for (uint32_t y = 0; y < planeHeight; ++y) {
                        for (uint32_t x = 0; x < planeWidth; ++x) {
                            resultRow[x] = (uint8_t)(rightOperandRow[x] << leftOperandLog2);
                        }
                        rightOperandRow += rightOperandRowBytes;
                        resultRow += resultRowBytes;
                    }
                }
            }
            continue;
        }

        if (operation == AVIF_SAMPLE_TRANSFORM_DIVIDE_REVERSED && leftOperandIsPowerOfTwo) {
            if (avifImageUsesU16(rightOperand)) {
                if (avifImageUsesU16(result)) {
                    for (uint32_t y = 0; y < planeHeight; ++y) {
                        const uint16_t * rightOperandRow16 = (const uint16_t *)rightOperandRow;
                        uint16_t * resultRow16 = (uint16_t *)resultRow;
                        for (uint32_t x = 0; x < planeWidth; ++x) {
                            resultRow16[x] = (uint16_t)(rightOperandRow16[x] >> leftOperandLog2);
                        }
                        rightOperandRow += rightOperandRowBytes;
                        resultRow += resultRowBytes;
                    }
                } else {
                    for (uint32_t y = 0; y < planeHeight; ++y) {
                        const uint16_t * rightOperandRow16 = (const uint16_t *)rightOperandRow;
                        for (uint32_t x = 0; x < planeWidth; ++x) {
                            resultRow[x] = (uint8_t)(rightOperandRow16[x] >> leftOperandLog2);
                        }
                        rightOperandRow += rightOperandRowBytes;
                        resultRow += resultRowBytes;
                    }
                }
            } else {
                if (avifImageUsesU16(result)) {
                    for (uint32_t y = 0; y < planeHeight; ++y) {
                        uint16_t * resultRow16 = (uint16_t *)resultRow;
                        for (uint32_t x = 0; x < planeWidth; ++x) {
                            resultRow16[x] = (uint16_t)(rightOperandRow[x] >> leftOperandLog2);
                        }
                        rightOperandRow += rightOperandRowBytes;
                        resultRow += resultRowBytes;
                    }
                } else {
                    for (uint32_t y = 0; y < planeHeight; ++y) {
                        for (uint32_t x = 0; x < planeWidth; ++x) {
                            resultRow[x] = (uint8_t)(rightOperandRow[x] >> leftOperandLog2);
                        }
                        rightOperandRow += rightOperandRowBytes;
                        resultRow += resultRowBytes;
                    }
                }
            }
            continue;
        }

        if (operation == AVIF_SAMPLE_TRANSFORM_AND) {
            // Instead of caring about signed bitwise AND, just make sure it does not happen. Unused in libavif anyway.
            assert(leftOperand >= 0);

            if (avifImageUsesU16(rightOperand)) {
                if (avifImageUsesU16(result)) {
                    for (uint32_t y = 0; y < planeHeight; ++y) {
                        const uint16_t * rightOperandRow16 = (const uint16_t *)rightOperandRow;
                        uint16_t * resultRow16 = (uint16_t *)resultRow;
                        for (uint32_t x = 0; x < planeWidth; ++x) {
                            resultRow16[x] = (uint16_t)(leftOperand & rightOperandRow16[x]);
                        }
                        rightOperandRow += rightOperandRowBytes;
                        resultRow += resultRowBytes;
                    }
                } else {
                    // Cannot fit 16-bit samples into 8 bits, so make sure the mask guarantees 8-bit samples.
                    AVIF_CHECKERR(leftOperand < (1 << 8), AVIF_RESULT_INVALID_ARGUMENT);
                    for (uint32_t y = 0; y < planeHeight; ++y) {
                        const uint16_t * rightOperandRow16 = (const uint16_t *)rightOperandRow;
                        for (uint32_t x = 0; x < planeWidth; ++x) {
                            resultRow[x] = (uint8_t)(leftOperand & rightOperandRow16[x]);
                        }
                        rightOperandRow += rightOperandRowBytes;
                        resultRow += resultRowBytes;
                    }
                }
            } else {
                if (avifImageUsesU16(result)) {
                    for (uint32_t y = 0; y < planeHeight; ++y) {
                        uint16_t * resultRow16 = (uint16_t *)resultRow;
                        for (uint32_t x = 0; x < planeWidth; ++x) {
                            resultRow16[x] = (uint16_t)(leftOperand & rightOperandRow[x]);
                        }
                        rightOperandRow += rightOperandRowBytes;
                        resultRow += resultRowBytes;
                    }
                } else {
                    for (uint32_t y = 0; y < planeHeight; ++y) {
                        for (uint32_t x = 0; x < planeWidth; ++x) {
                            resultRow[x] = (uint8_t)(leftOperand & rightOperandRow[x]);
                        }
                        rightOperandRow += rightOperandRowBytes;
                        resultRow += resultRowBytes;
                    }
                }
            }
            continue;
        }

        // The remaining operations are not used in libavif for now.
        return AVIF_RESULT_NOT_IMPLEMENTED;
    }
    return AVIF_RESULT_OK;
}
