// Copyright 2020 Joe Drago. All rights reserved.
// SPDX-License-Identifier: BSD-2-Clause

#include "avif/internal.h"

#include <assert.h>
#include <string.h>

// Scope: this targets compilers that define __aarch64__ (Clang and GCC on AArch64 macOS/Linux/
// Android). AArch64 makes the Advanced SIMD (NEON) baseline mandatory, so no runtime feature
// detection is needed here. MSVC on Windows ARM64 defines _M_ARM64, not __aarch64__, so it is
// intentionally left on the scalar path below in this first pass -- it has not been built or
// tested against these intrinsics.
#if defined(__aarch64__)
#include <arm_neon.h>

// Fills the alpha channel described by 'params' using NEON. 'params->dstOffsetBytes' is the byte
// offset of the alpha channel within a pixel (0 for a contiguous alpha plane, or the interleave
// lane offset for a packed RGBA-like buffer). Returns AVIF_FALSE if the pixel layout isn't one
// this routine handles (dstPixelBytes not a multiple of the channel size, or an interleave factor
// other than 1, 2 or 4), in which case the caller must fall back to the scalar implementation
// below. Never partially fills a plane before returning AVIF_FALSE: all validation happens before
// any store.
static avifBool avifFillAlphaNEON(const avifAlphaParams * params)
{
    const uint32_t elem = (params->dstDepth > 8) ? 2 : 1;
    if ((params->dstPixelBytes == 0) || ((params->dstPixelBytes % elem) != 0) || ((params->dstOffsetBytes % elem) != 0)) {
        return AVIF_FALSE;
    }
    const uint32_t factor = params->dstPixelBytes / elem;
    const uint32_t lane = params->dstOffsetBytes / elem;
    if ((lane >= factor) || ((factor != 1) && (factor != 2) && (factor != 4))) {
        return AVIF_FALSE;
    }

    const uint32_t lanesPerVec = (elem == 1) ? 16 : 8;
    const uint32_t vecWidth = params->width - (params->width % lanesPerVec);
    const uint16_t maxChannel16 = (uint16_t)((1u << params->dstDepth) - 1);
    const uint8x16_t fillU8 = vdupq_n_u8(255);
    const uint16x8_t fillU16 = vdupq_n_u16(maxChannel16);

    uint8_t * dstRow = params->dstPlane;
    for (uint32_t j = 0; j < params->height; ++j) {
        // pixelGroupBase always points at the start of a pixel (never at a lane offset), so
        // vld2q/vld4q see a properly aligned interleaved group.
        uint8_t * pixelGroupBase = dstRow;
        uint32_t i = 0;
        for (; i < vecWidth; i += lanesPerVec) {
            if (elem == 1) {
                if (factor == 1) {
                    vst1q_u8(pixelGroupBase, fillU8);
                } else if (factor == 2) {
                    uint8x16x2_t g = vld2q_u8(pixelGroupBase);
                    g.val[lane] = fillU8;
                    vst2q_u8(pixelGroupBase, g);
                } else {
                    uint8x16x4_t g = vld4q_u8(pixelGroupBase);
                    g.val[lane] = fillU8;
                    vst4q_u8(pixelGroupBase, g);
                }
            } else {
                uint16_t * base16 = (uint16_t *)pixelGroupBase;
                if (factor == 1) {
                    vst1q_u16(base16, fillU16);
                } else if (factor == 2) {
                    uint16x8x2_t g = vld2q_u16(base16);
                    g.val[lane] = fillU16;
                    vst2q_u16(base16, g);
                } else {
                    uint16x8x4_t g = vld4q_u16(base16);
                    g.val[lane] = fillU16;
                    vst4q_u16(base16, g);
                }
            }
            pixelGroupBase += (size_t)lanesPerVec * params->dstPixelBytes;
        }
        // Tail: identical to the scalar reference expression below, so remainder pixels (and any
        // row narrower than one vector) are bit-exact with the non-NEON path by construction.
        uint8_t * dstPixel = pixelGroupBase + params->dstOffsetBytes;
        for (; i < params->width; ++i) {
            if (elem == 1) {
                *dstPixel = 255;
            } else {
                *((uint16_t *)dstPixel) = maxChannel16;
            }
            dstPixel += params->dstPixelBytes;
        }
        dstRow += params->dstRowBytes;
    }
    return AVIF_TRUE;
}

// Copies the alpha channel from src to dst using NEON, for the case where src and dst share the
// same bit depth (no rescale). Returns AVIF_FALSE (before performing any store) if the depths
// differ, or if either side's pixel layout isn't a plain plane or a 2/4-way interleave, in which
// case the caller must fall back to the scalar implementation below.
static avifBool avifReformatAlphaCopyNEON(const avifAlphaParams * params)
{
    if (params->srcDepth != params->dstDepth) {
        return AVIF_FALSE;
    }
    const uint32_t elem = (params->srcDepth > 8) ? 2 : 1;
    if ((params->srcPixelBytes == 0) || (params->dstPixelBytes == 0) || ((params->srcPixelBytes % elem) != 0) ||
        ((params->dstPixelBytes % elem) != 0) || ((params->srcOffsetBytes % elem) != 0) || ((params->dstOffsetBytes % elem) != 0)) {
        return AVIF_FALSE;
    }
    const uint32_t srcFactor = params->srcPixelBytes / elem;
    const uint32_t dstFactor = params->dstPixelBytes / elem;
    const uint32_t srcLane = params->srcOffsetBytes / elem;
    const uint32_t dstLane = params->dstOffsetBytes / elem;
    if ((srcLane >= srcFactor) || (dstLane >= dstFactor) || ((srcFactor != 1) && (srcFactor != 2) && (srcFactor != 4)) ||
        ((dstFactor != 1) && (dstFactor != 2) && (dstFactor != 4))) {
        return AVIF_FALSE;
    }

    const uint32_t lanesPerVec = (elem == 1) ? 16 : 8;
    const uint32_t vecWidth = params->width - (params->width % lanesPerVec);

    const uint8_t * srcRow = params->srcPlane;
    uint8_t * dstRow = params->dstPlane;
    for (uint32_t j = 0; j < params->height; ++j) {
        const uint8_t * srcGroupBase = srcRow;
        uint8_t * dstGroupBase = dstRow;
        uint32_t i = 0;
        for (; i < vecWidth; i += lanesPerVec) {
            if (elem == 1) {
                uint8x16_t v;
                if (srcFactor == 1) {
                    v = vld1q_u8(srcGroupBase);
                } else if (srcFactor == 2) {
                    v = vld2q_u8(srcGroupBase).val[srcLane];
                } else {
                    v = vld4q_u8(srcGroupBase).val[srcLane];
                }
                if (dstFactor == 1) {
                    vst1q_u8(dstGroupBase, v);
                } else if (dstFactor == 2) {
                    uint8x16x2_t g = vld2q_u8(dstGroupBase);
                    g.val[dstLane] = v;
                    vst2q_u8(dstGroupBase, g);
                } else {
                    uint8x16x4_t g = vld4q_u8(dstGroupBase);
                    g.val[dstLane] = v;
                    vst4q_u8(dstGroupBase, g);
                }
            } else {
                const uint16_t * srcBase16 = (const uint16_t *)srcGroupBase;
                uint16_t * dstBase16 = (uint16_t *)dstGroupBase;
                uint16x8_t v;
                if (srcFactor == 1) {
                    v = vld1q_u16(srcBase16);
                } else if (srcFactor == 2) {
                    v = vld2q_u16(srcBase16).val[srcLane];
                } else {
                    v = vld4q_u16(srcBase16).val[srcLane];
                }
                if (dstFactor == 1) {
                    vst1q_u16(dstBase16, v);
                } else if (dstFactor == 2) {
                    uint16x8x2_t g = vld2q_u16(dstBase16);
                    g.val[dstLane] = v;
                    vst2q_u16(dstBase16, g);
                } else {
                    uint16x8x4_t g = vld4q_u16(dstBase16);
                    g.val[dstLane] = v;
                    vst4q_u16(dstBase16, g);
                }
            }
            srcGroupBase += (size_t)lanesPerVec * params->srcPixelBytes;
            dstGroupBase += (size_t)lanesPerVec * params->dstPixelBytes;
        }
        // Tail: identical to the scalar reference expression below.
        const uint8_t * srcPixel = srcGroupBase + params->srcOffsetBytes;
        uint8_t * dstPixel = dstGroupBase + params->dstOffsetBytes;
        for (; i < params->width; ++i) {
            if (elem == 1) {
                *dstPixel = *srcPixel;
            } else {
                *((uint16_t *)dstPixel) = *((const uint16_t *)srcPixel);
            }
            srcPixel += params->srcPixelBytes;
            dstPixel += params->dstPixelBytes;
        }
        srcRow += params->srcRowBytes;
        dstRow += params->dstRowBytes;
    }
    return AVIF_TRUE;
}
#endif // defined(__aarch64__)

void avifFillAlpha(const avifAlphaParams * params)
{
#if defined(__aarch64__)
    if (avifFillAlphaNEON(params)) {
        return;
    }
#endif
    if (params->dstDepth > 8) {
        const uint16_t maxChannel = (uint16_t)((1 << params->dstDepth) - 1);
        uint8_t * dstRow = &params->dstPlane[params->dstOffsetBytes];
        for (uint32_t j = 0; j < params->height; ++j) {
            uint8_t * dstPixel = dstRow;
            for (uint32_t i = 0; i < params->width; ++i) {
                *((uint16_t *)dstPixel) = maxChannel;
                dstPixel += params->dstPixelBytes;
            }
            dstRow += params->dstRowBytes;
        }
    } else {
        // In this case, (1 << params->dstDepth) - 1 is always equal to 255.
        const uint8_t maxChannel = 255;
        uint8_t * dstRow = &params->dstPlane[params->dstOffsetBytes];
        for (uint32_t j = 0; j < params->height; ++j) {
            uint8_t * dstPixel = dstRow;
            for (uint32_t i = 0; i < params->width; ++i) {
                *dstPixel = maxChannel;
                dstPixel += params->dstPixelBytes;
            }
            dstRow += params->dstRowBytes;
        }
    }
}

void avifReformatAlpha(const avifAlphaParams * params)
{
#if defined(__aarch64__)
    if (avifReformatAlphaCopyNEON(params)) {
        return;
    }
#endif
    const int srcMaxChannel = (1 << params->srcDepth) - 1;
    const int dstMaxChannel = (1 << params->dstDepth) - 1;
    const float srcMaxChannelF = (float)srcMaxChannel;
    const float dstMaxChannelF = (float)dstMaxChannel;

    if (params->srcDepth == params->dstDepth) {
        // no depth rescale

        if (params->srcDepth > 8) {
            // no depth rescale, uint16_t -> uint16_t

            const uint8_t * srcRow = &params->srcPlane[params->srcOffsetBytes];
            uint8_t * dstRow = &params->dstPlane[params->dstOffsetBytes];
            for (uint32_t j = 0; j < params->height; ++j) {
                const uint8_t * srcPixel = srcRow;
                uint8_t * dstPixel = dstRow;
                for (uint32_t i = 0; i < params->width; ++i) {
                    *((uint16_t *)dstPixel) = *((const uint16_t *)srcPixel);
                    srcPixel += params->srcPixelBytes;
                    dstPixel += params->dstPixelBytes;
                }
                srcRow += params->srcRowBytes;
                dstRow += params->dstRowBytes;
            }
        } else {
            // no depth rescale, uint8_t -> uint8_t

            const uint8_t * srcRow = &params->srcPlane[params->srcOffsetBytes];
            uint8_t * dstRow = &params->dstPlane[params->dstOffsetBytes];
            for (uint32_t j = 0; j < params->height; ++j) {
                const uint8_t * srcPixel = srcRow;
                uint8_t * dstPixel = dstRow;
                for (uint32_t i = 0; i < params->width; ++i) {
                    *dstPixel = *srcPixel;
                    srcPixel += params->srcPixelBytes;
                    dstPixel += params->dstPixelBytes;
                }
                srcRow += params->srcRowBytes;
                dstRow += params->dstRowBytes;
            }
        }
    } else {
        // depth rescale

        if (params->srcDepth > 8) {
            if (params->dstDepth > 8) {
                // depth rescale, uint16_t -> uint16_t

                const uint8_t * srcRow = &params->srcPlane[params->srcOffsetBytes];
                uint8_t * dstRow = &params->dstPlane[params->dstOffsetBytes];
                for (uint32_t j = 0; j < params->height; ++j) {
                    const uint8_t * srcPixel = srcRow;
                    uint8_t * dstPixel = dstRow;
                    for (uint32_t i = 0; i < params->width; ++i) {
                        int srcAlpha = *((const uint16_t *)srcPixel);
                        float alphaF = (float)srcAlpha / srcMaxChannelF;
                        int dstAlpha = (int)(0.5f + (alphaF * dstMaxChannelF));
                        dstAlpha = AVIF_CLAMP(dstAlpha, 0, dstMaxChannel);
                        *((uint16_t *)dstPixel) = (uint16_t)dstAlpha;
                        srcPixel += params->srcPixelBytes;
                        dstPixel += params->dstPixelBytes;
                    }
                    srcRow += params->srcRowBytes;
                    dstRow += params->dstRowBytes;
                }
            } else {
                // depth rescale, uint16_t -> uint8_t

                const uint8_t * srcRow = &params->srcPlane[params->srcOffsetBytes];
                uint8_t * dstRow = &params->dstPlane[params->dstOffsetBytes];
                for (uint32_t j = 0; j < params->height; ++j) {
                    const uint8_t * srcPixel = srcRow;
                    uint8_t * dstPixel = dstRow;
                    for (uint32_t i = 0; i < params->width; ++i) {
                        int srcAlpha = *((const uint16_t *)srcPixel);
                        float alphaF = (float)srcAlpha / srcMaxChannelF;
                        int dstAlpha = (int)(0.5f + (alphaF * dstMaxChannelF));
                        dstAlpha = AVIF_CLAMP(dstAlpha, 0, dstMaxChannel);
                        *dstPixel = (uint8_t)dstAlpha;
                        srcPixel += params->srcPixelBytes;
                        dstPixel += params->dstPixelBytes;
                    }
                    srcRow += params->srcRowBytes;
                    dstRow += params->dstRowBytes;
                }
            }
        } else {
            // If (srcDepth == 8), dstDepth must be >8 otherwise we'd be in the (params->srcDepth == params->dstDepth) block above.
            assert(params->dstDepth > 8);

            // depth rescale, uint8_t -> uint16_t
            const uint8_t * srcRow = &params->srcPlane[params->srcOffsetBytes];
            uint8_t * dstRow = &params->dstPlane[params->dstOffsetBytes];
            for (uint32_t j = 0; j < params->height; ++j) {
                const uint8_t * srcPixel = srcRow;
                uint8_t * dstPixel = dstRow;
                for (uint32_t i = 0; i < params->width; ++i) {
                    int srcAlpha = *srcPixel;
                    float alphaF = (float)srcAlpha / srcMaxChannelF;
                    int dstAlpha = (int)(0.5f + (alphaF * dstMaxChannelF));
                    dstAlpha = AVIF_CLAMP(dstAlpha, 0, dstMaxChannel);
                    *((uint16_t *)dstPixel) = (uint16_t)dstAlpha;
                    srcPixel += params->srcPixelBytes;
                    dstPixel += params->dstPixelBytes;
                }
                srcRow += params->srcRowBytes;
                dstRow += params->dstRowBytes;
            }
        }
    }
}

avifResult avifRGBImagePremultiplyAlpha(avifRGBImage * rgb)
{
    // no data
    if (!rgb->pixels || !rgb->rowBytes) {
        return AVIF_RESULT_REFORMAT_FAILED;
    }

    // no alpha.
    if (!avifRGBFormatHasAlpha(rgb->format)) {
        return AVIF_RESULT_INVALID_ARGUMENT;
    }

    avifResult libyuvResult = avifRGBImagePremultiplyAlphaLibYUV(rgb);
    if (libyuvResult != AVIF_RESULT_NOT_IMPLEMENTED) {
        return libyuvResult;
    }

    assert(rgb->depth >= 8 && rgb->depth <= 16);

    uint32_t max = (1 << rgb->depth) - 1;
    float maxF = (float)max;

    if (rgb->depth > 8) {
        if (rgb->format == AVIF_RGB_FORMAT_RGBA || rgb->format == AVIF_RGB_FORMAT_BGRA) {
            uint8_t * row = rgb->pixels;
            for (uint32_t j = 0; j < rgb->height; ++j) {
                uint16_t * pixel = (uint16_t *)row;
                for (uint32_t i = 0; i < rgb->width; ++i) {
                    uint16_t a = pixel[3];
                    if (a >= max) {
                        // opaque is no-op
                    } else if (a == 0) {
                        // result must be zero
                        pixel[0] = 0;
                        pixel[1] = 0;
                        pixel[2] = 0;
                    } else {
                        // a < maxF is always true now, so we don't need clamp here
                        pixel[0] = (uint16_t)avifRoundf((float)pixel[0] * (float)a / maxF);
                        pixel[1] = (uint16_t)avifRoundf((float)pixel[1] * (float)a / maxF);
                        pixel[2] = (uint16_t)avifRoundf((float)pixel[2] * (float)a / maxF);
                    }
                    pixel += 4;
                }
                row += rgb->rowBytes;
            }
        } else if (rgb->format == AVIF_RGB_FORMAT_ARGB || rgb->format == AVIF_RGB_FORMAT_ABGR) {
            uint8_t * row = rgb->pixels;
            for (uint32_t j = 0; j < rgb->height; ++j) {
                uint16_t * pixel = (uint16_t *)row;
                for (uint32_t i = 0; i < rgb->width; ++i) {
                    uint16_t a = pixel[0];
                    if (a >= max) {
                    } else if (a == 0) {
                        pixel[1] = 0;
                        pixel[2] = 0;
                        pixel[3] = 0;
                    } else {
                        pixel[1] = (uint16_t)avifRoundf((float)pixel[1] * (float)a / maxF);
                        pixel[2] = (uint16_t)avifRoundf((float)pixel[2] * (float)a / maxF);
                        pixel[3] = (uint16_t)avifRoundf((float)pixel[3] * (float)a / maxF);
                    }
                    pixel += 4;
                }
                row += rgb->rowBytes;
            }
        } else if (rgb->format == AVIF_RGB_FORMAT_GRAYA) {
            uint8_t * row = rgb->pixels;
            for (uint32_t j = 0; j < rgb->height; ++j) {
                uint16_t * pixel = (uint16_t *)row;
                for (uint32_t i = 0; i < rgb->width; ++i) {
                    uint16_t a = pixel[1];
                    if (a >= max) {
                        // opaque is no-op
                    } else if (a == 0) {
                        // result must be zero
                        pixel[0] = 0;
                    } else {
                        // a < maxF is always true now, so we don't need clamp here
                        pixel[0] = (uint16_t)avifRoundf((float)pixel[0] * (float)a / maxF);
                    }
                    pixel += 2;
                }
                row += rgb->rowBytes;
            }
        } else if (rgb->format == AVIF_RGB_FORMAT_AGRAY) {
            uint8_t * row = rgb->pixels;
            for (uint32_t j = 0; j < rgb->height; ++j) {
                uint16_t * pixel = (uint16_t *)row;
                for (uint32_t i = 0; i < rgb->width; ++i) {
                    uint16_t a = pixel[0];
                    if (a >= max) {
                    } else if (a == 0) {
                        pixel[1] = 0;
                    } else {
                        pixel[1] = (uint16_t)avifRoundf((float)pixel[1] * (float)a / maxF);
                    }
                    pixel += 2;
                }
                row += rgb->rowBytes;
            }
        } else {
            return AVIF_RESULT_NOT_IMPLEMENTED;
        }
    } else {
        if (rgb->format == AVIF_RGB_FORMAT_RGBA || rgb->format == AVIF_RGB_FORMAT_BGRA) {
            uint8_t * row = rgb->pixels;
            for (uint32_t j = 0; j < rgb->height; ++j) {
                uint8_t * pixel = row;
                for (uint32_t i = 0; i < rgb->width; ++i) {
                    uint8_t a = pixel[3];
                    // uint8_t can't exceed 255
                    if (a == max) {
                    } else if (a == 0) {
                        pixel[0] = 0;
                        pixel[1] = 0;
                        pixel[2] = 0;
                    } else {
                        pixel[0] = (uint8_t)avifRoundf((float)pixel[0] * (float)a / maxF);
                        pixel[1] = (uint8_t)avifRoundf((float)pixel[1] * (float)a / maxF);
                        pixel[2] = (uint8_t)avifRoundf((float)pixel[2] * (float)a / maxF);
                    }
                    pixel += 4;
                }
                row += rgb->rowBytes;
            }
        } else if (rgb->format == AVIF_RGB_FORMAT_ARGB || rgb->format == AVIF_RGB_FORMAT_ABGR) {
            uint8_t * row = rgb->pixels;
            for (uint32_t j = 0; j < rgb->height; ++j) {
                uint8_t * pixel = row;
                for (uint32_t i = 0; i < rgb->width; ++i) {
                    uint8_t a = pixel[0];
                    if (a == max) {
                    } else if (a == 0) {
                        pixel[1] = 0;
                        pixel[2] = 0;
                        pixel[3] = 0;
                    } else {
                        pixel[1] = (uint8_t)avifRoundf((float)pixel[1] * (float)a / maxF);
                        pixel[2] = (uint8_t)avifRoundf((float)pixel[2] * (float)a / maxF);
                        pixel[3] = (uint8_t)avifRoundf((float)pixel[3] * (float)a / maxF);
                    }
                    pixel += 4;
                }
                row += rgb->rowBytes;
            }
        } else if (rgb->format == AVIF_RGB_FORMAT_GRAYA) {
            uint8_t * row = rgb->pixels;
            for (uint32_t j = 0; j < rgb->height; ++j) {
                uint8_t * pixel = row;
                for (uint32_t i = 0; i < rgb->width; ++i) {
                    uint8_t a = pixel[1];
                    // uint8_t can't exceed 255
                    if (a == max) {
                    } else if (a == 0) {
                        pixel[0] = 0;
                    } else {
                        pixel[0] = (uint8_t)avifRoundf((float)pixel[0] * (float)a / maxF);
                    }
                    pixel += 2;
                }
                row += rgb->rowBytes;
            }
        } else if (rgb->format == AVIF_RGB_FORMAT_AGRAY) {
            uint8_t * row = rgb->pixels;
            for (uint32_t j = 0; j < rgb->height; ++j) {
                uint8_t * pixel = row;
                for (uint32_t i = 0; i < rgb->width; ++i) {
                    uint8_t a = pixel[0];
                    if (a == max) {
                    } else if (a == 0) {
                        pixel[1] = 0;
                    } else {
                        pixel[1] = (uint8_t)avifRoundf((float)pixel[1] * (float)a / maxF);
                    }
                    pixel += 2;
                }
                row += rgb->rowBytes;
            }
        } else {
            return AVIF_RESULT_NOT_IMPLEMENTED;
        }
    }

    return AVIF_RESULT_OK;
}

avifResult avifRGBImageUnpremultiplyAlpha(avifRGBImage * rgb)
{
    // no data
    if (!rgb->pixels || !rgb->rowBytes) {
        return AVIF_RESULT_REFORMAT_FAILED;
    }

    // no alpha.
    if (!avifRGBFormatHasAlpha(rgb->format)) {
        return AVIF_RESULT_REFORMAT_FAILED;
    }

    avifResult libyuvResult = avifRGBImageUnpremultiplyAlphaLibYUV(rgb);
    if (libyuvResult != AVIF_RESULT_NOT_IMPLEMENTED) {
        return libyuvResult;
    }

    assert(rgb->depth >= 8 && rgb->depth <= 16);

    uint32_t max = (1 << rgb->depth) - 1;
    float maxF = (float)max;

    if (rgb->depth > 8) {
        if (rgb->format == AVIF_RGB_FORMAT_RGBA || rgb->format == AVIF_RGB_FORMAT_BGRA) {
            uint8_t * row = rgb->pixels;
            for (uint32_t j = 0; j < rgb->height; ++j) {
                uint16_t * pixel = (uint16_t *)row;
                for (uint32_t i = 0; i < rgb->width; ++i) {
                    uint16_t a = pixel[3];
                    if (a >= max) {
                        // opaque is no-op
                    } else if (a == 0) {
                        // prevent division by zero
                        pixel[0] = 0;
                        pixel[1] = 0;
                        pixel[2] = 0;
                    } else {
                        float c1 = avifRoundf((float)pixel[0] * maxF / (float)a);
                        float c2 = avifRoundf((float)pixel[1] * maxF / (float)a);
                        float c3 = avifRoundf((float)pixel[2] * maxF / (float)a);
                        pixel[0] = (uint16_t)AVIF_MIN(c1, maxF);
                        pixel[1] = (uint16_t)AVIF_MIN(c2, maxF);
                        pixel[2] = (uint16_t)AVIF_MIN(c3, maxF);
                    }
                    pixel += 4;
                }
                row += rgb->rowBytes;
            }
        } else if (rgb->format == AVIF_RGB_FORMAT_ARGB || rgb->format == AVIF_RGB_FORMAT_ABGR) {
            uint8_t * row = rgb->pixels;
            for (uint32_t j = 0; j < rgb->height; ++j) {
                uint16_t * pixel = (uint16_t *)row;
                for (uint32_t i = 0; i < rgb->width; ++i) {
                    uint16_t a = pixel[0];
                    if (a >= max) {
                    } else if (a == 0) {
                        pixel[1] = 0;
                        pixel[2] = 0;
                        pixel[3] = 0;
                    } else {
                        float c1 = avifRoundf((float)pixel[1] * maxF / (float)a);
                        float c2 = avifRoundf((float)pixel[2] * maxF / (float)a);
                        float c3 = avifRoundf((float)pixel[3] * maxF / (float)a);
                        pixel[1] = (uint16_t)AVIF_MIN(c1, maxF);
                        pixel[2] = (uint16_t)AVIF_MIN(c2, maxF);
                        pixel[3] = (uint16_t)AVIF_MIN(c3, maxF);
                    }
                    pixel += 4;
                }
                row += rgb->rowBytes;
            }
        } else if (rgb->format == AVIF_RGB_FORMAT_GRAYA) {
            uint8_t * row = rgb->pixels;
            for (uint32_t j = 0; j < rgb->height; ++j) {
                uint16_t * pixel = (uint16_t *)row;
                for (uint32_t i = 0; i < rgb->width; ++i) {
                    uint16_t a = pixel[1];
                    if (a >= max) {
                        // opaque is no-op
                    } else if (a == 0) {
                        // prevent division by zero
                        pixel[0] = 0;
                    } else {
                        float c1 = avifRoundf((float)pixel[0] * maxF / (float)a);
                        pixel[0] = (uint16_t)AVIF_MIN(c1, maxF);
                    }
                    pixel += 2;
                }
                row += rgb->rowBytes;
            }
        } else if (rgb->format == AVIF_RGB_FORMAT_AGRAY) {
            uint8_t * row = rgb->pixels;
            for (uint32_t j = 0; j < rgb->height; ++j) {
                uint16_t * pixel = (uint16_t *)row;
                for (uint32_t i = 0; i < rgb->width; ++i) {
                    uint16_t a = pixel[0];
                    if (a >= max) {
                    } else if (a == 0) {
                        pixel[1] = 0;
                    } else {
                        float c1 = avifRoundf((float)pixel[1] * maxF / (float)a);
                        pixel[1] = (uint16_t)AVIF_MIN(c1, maxF);
                    }
                    pixel += 2;
                }
                row += rgb->rowBytes;
            }
        } else {
            return AVIF_RESULT_NOT_IMPLEMENTED;
        }
    } else {
        if (rgb->format == AVIF_RGB_FORMAT_RGBA || rgb->format == AVIF_RGB_FORMAT_BGRA) {
            uint8_t * row = rgb->pixels;
            for (uint32_t j = 0; j < rgb->height; ++j) {
                uint8_t * pixel = row;
                for (uint32_t i = 0; i < rgb->width; ++i) {
                    uint8_t a = pixel[3];
                    if (a == max) {
                    } else if (a == 0) {
                        pixel[0] = 0;
                        pixel[1] = 0;
                        pixel[2] = 0;
                    } else {
                        float c1 = avifRoundf((float)pixel[0] * maxF / (float)a);
                        float c2 = avifRoundf((float)pixel[1] * maxF / (float)a);
                        float c3 = avifRoundf((float)pixel[2] * maxF / (float)a);
                        pixel[0] = (uint8_t)AVIF_MIN(c1, maxF);
                        pixel[1] = (uint8_t)AVIF_MIN(c2, maxF);
                        pixel[2] = (uint8_t)AVIF_MIN(c3, maxF);
                    }
                    pixel += 4;
                }
                row += rgb->rowBytes;
            }
        } else if (rgb->format == AVIF_RGB_FORMAT_ARGB || rgb->format == AVIF_RGB_FORMAT_ABGR) {
            uint8_t * row = rgb->pixels;
            for (uint32_t j = 0; j < rgb->height; ++j) {
                uint8_t * pixel = row;
                for (uint32_t i = 0; i < rgb->width; ++i) {
                    uint8_t a = pixel[0];
                    if (a == max) {
                    } else if (a == 0) {
                        pixel[1] = 0;
                        pixel[2] = 0;
                        pixel[3] = 0;
                    } else {
                        float c1 = avifRoundf((float)pixel[1] * maxF / (float)a);
                        float c2 = avifRoundf((float)pixel[2] * maxF / (float)a);
                        float c3 = avifRoundf((float)pixel[3] * maxF / (float)a);
                        pixel[1] = (uint8_t)AVIF_MIN(c1, maxF);
                        pixel[2] = (uint8_t)AVIF_MIN(c2, maxF);
                        pixel[3] = (uint8_t)AVIF_MIN(c3, maxF);
                    }
                    pixel += 4;
                }
                row += rgb->rowBytes;
            }
        } else if (rgb->format == AVIF_RGB_FORMAT_GRAYA) {
            uint8_t * row = rgb->pixels;
            for (uint32_t j = 0; j < rgb->height; ++j) {
                uint8_t * pixel = row;
                for (uint32_t i = 0; i < rgb->width; ++i) {
                    uint8_t a = pixel[1];
                    if (a == max) {
                    } else if (a == 0) {
                        pixel[0] = 0;
                    } else {
                        float c1 = avifRoundf((float)pixel[0] * maxF / (float)a);
                        pixel[0] = (uint8_t)AVIF_MIN(c1, maxF);
                    }
                    pixel += 2;
                }
                row += rgb->rowBytes;
            }
        } else if (rgb->format == AVIF_RGB_FORMAT_AGRAY) {
            uint8_t * row = rgb->pixels;
            for (uint32_t j = 0; j < rgb->height; ++j) {
                uint8_t * pixel = row;
                for (uint32_t i = 0; i < rgb->width; ++i) {
                    uint8_t a = pixel[0];
                    if (a == max) {
                    } else if (a == 0) {
                        pixel[1] = 0;
                    } else {
                        float c1 = avifRoundf((float)pixel[1] * maxF / (float)a);
                        pixel[1] = (uint8_t)AVIF_MIN(c1, maxF);
                    }
                    pixel += 2;
                }
                row += rgb->rowBytes;
            }
        } else {
            return AVIF_RESULT_NOT_IMPLEMENTED;
        }
    }

    return AVIF_RESULT_OK;
}
