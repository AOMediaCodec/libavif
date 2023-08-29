/*
 * Copyright 2013 The LibYuv Project Authors. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice, this
 *    list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *
 * 3. Neither the name of Google nor the names of its contributors may
 *    be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR
 * ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <assert.h>
#include <string.h>
#include <stdint.h>

#include "avif_scale_internal.h"

#ifdef __cplusplus
namespace avif_yuvscale {
extern "C" {
#endif

static __inline int Abs(int v) {
  return v >= 0 ? v : -v;
}

#define SUBSAMPLE(v, a, s) (v < 0) ? (-((-v + a) >> s)) : ((v + a) >> s)
#define CENTERSTART(dx, s) (dx < 0) ? -((-dx >> 1) + s) : ((dx >> 1) + s)

// Scale plane, 1/2
// This is an optimized version for scaling down a plane to 1/2 of
// its original size.

static void ScalePlaneDown2(int src_width,
                            int src_height,
                            int dst_width,
                            int dst_height,
                            int src_stride,
                            int dst_stride,
                            const uint8_t* src_ptr,
                            uint8_t* dst_ptr,
                            enum FilterMode filtering) {
  int y;
  void (*ScaleRowDown2)(const uint8_t* src_ptr, ptrdiff_t src_stride,
                        uint8_t* dst_ptr, int dst_width) =
      filtering == kFilterNone
          ? ScaleRowDown2_C
          : (filtering == kFilterLinear ? ScaleRowDown2Linear_C
                                        : ScaleRowDown2Box_C);
  int row_stride = src_stride * 2;
  (void)src_width;
  (void)src_height;
  if (!filtering) {
    src_ptr += src_stride;  // Point to odd rows.
    src_stride = 0;
  }

  if (filtering == kFilterLinear) {
    src_stride = 0;
  }
  // TODO(fbarchard): Loop through source height to allow odd height.
  for (y = 0; y < dst_height; ++y) {
    ScaleRowDown2(src_ptr, src_stride, dst_ptr, dst_width);
    src_ptr += row_stride;
    dst_ptr += dst_stride;
  }
}

static void ScalePlaneDown2_16(int src_width,
                               int src_height,
                               int dst_width,
                               int dst_height,
                               int src_stride,
                               int dst_stride,
                               const uint16_t* src_ptr,
                               uint16_t* dst_ptr,
                               enum FilterMode filtering) {
  int y;
  void (*ScaleRowDown2)(const uint16_t* src_ptr, ptrdiff_t src_stride,
                        uint16_t* dst_ptr, int dst_width) =
      filtering == kFilterNone
          ? ScaleRowDown2_16_C
          : (filtering == kFilterLinear ? ScaleRowDown2Linear_16_C
                                        : ScaleRowDown2Box_16_C);
  int row_stride = src_stride * 2;
  (void)src_width;
  (void)src_height;
  if (!filtering) {
    src_ptr += src_stride;  // Point to odd rows.
    src_stride = 0;
  }

  if (filtering == kFilterLinear) {
    src_stride = 0;
  }
  // TODO(fbarchard): Loop through source height to allow odd height.
  for (y = 0; y < dst_height; ++y) {
    ScaleRowDown2(src_ptr, src_stride, dst_ptr, dst_width);
    src_ptr += row_stride;
    dst_ptr += dst_stride;
  }
}

void ScalePlaneDown2_16To8(int src_width,
                           int src_height,
                           int dst_width,
                           int dst_height,
                           int src_stride,
                           int dst_stride,
                           const uint16_t* src_ptr,
                           uint8_t* dst_ptr,
                           int scale,
                           enum FilterMode filtering) {
  int y;
  void (*ScaleRowDown2)(const uint16_t* src_ptr, ptrdiff_t src_stride,
                        uint8_t* dst_ptr, int dst_width, int scale) =
      (src_width & 1)
          ? (filtering == kFilterNone
                 ? ScaleRowDown2_16To8_Odd_C
                 : (filtering == kFilterLinear ? ScaleRowDown2Linear_16To8_Odd_C
                                               : ScaleRowDown2Box_16To8_Odd_C))
          : (filtering == kFilterNone
                 ? ScaleRowDown2_16To8_C
                 : (filtering == kFilterLinear ? ScaleRowDown2Linear_16To8_C
                                               : ScaleRowDown2Box_16To8_C));
  int row_stride = src_stride * 2;
  (void)dst_height;
  if (!filtering) {
    src_ptr += src_stride;  // Point to odd rows.
    src_stride = 0;
  }

  if (filtering == kFilterLinear) {
    src_stride = 0;
  }
  for (y = 0; y < src_height / 2; ++y) {
    ScaleRowDown2(src_ptr, src_stride, dst_ptr, dst_width, scale);
    src_ptr += row_stride;
    dst_ptr += dst_stride;
  }
  if (src_height & 1) {
    if (!filtering) {
      src_ptr -= src_stride;  // Point to last row.
    }
    ScaleRowDown2(src_ptr, 0, dst_ptr, dst_width, scale);
  }
}

// Scale plane, 1/4
// This is an optimized version for scaling down a plane to 1/4 of
// its original size.

static void ScalePlaneDown4(int src_width,
                            int src_height,
                            int dst_width,
                            int dst_height,
                            int src_stride,
                            int dst_stride,
                            const uint8_t* src_ptr,
                            uint8_t* dst_ptr,
                            enum FilterMode filtering) {
  int y;
  void (*ScaleRowDown4)(const uint8_t* src_ptr, ptrdiff_t src_stride,
                        uint8_t* dst_ptr, int dst_width) =
      filtering ? ScaleRowDown4Box_C : ScaleRowDown4_C;
  int row_stride = src_stride * 4;
  (void)src_width;
  (void)src_height;
  if (!filtering) {
    src_ptr += src_stride * 2;  // Point to row 2.
    src_stride = 0;
  }

  if (filtering == kFilterLinear) {
    src_stride = 0;
  }
  for (y = 0; y < dst_height; ++y) {
    ScaleRowDown4(src_ptr, src_stride, dst_ptr, dst_width);
    src_ptr += row_stride;
    dst_ptr += dst_stride;
  }
}

static void ScalePlaneDown4_16(int src_width,
                               int src_height,
                               int dst_width,
                               int dst_height,
                               int src_stride,
                               int dst_stride,
                               const uint16_t* src_ptr,
                               uint16_t* dst_ptr,
                               enum FilterMode filtering) {
  int y;
  void (*ScaleRowDown4)(const uint16_t* src_ptr, ptrdiff_t src_stride,
                        uint16_t* dst_ptr, int dst_width) =
      filtering ? ScaleRowDown4Box_16_C : ScaleRowDown4_16_C;
  int row_stride = src_stride * 4;
  (void)src_width;
  (void)src_height;
  if (!filtering) {
    src_ptr += src_stride * 2;  // Point to row 2.
    src_stride = 0;
  }

  if (filtering == kFilterLinear) {
    src_stride = 0;
  }
  for (y = 0; y < dst_height; ++y) {
    ScaleRowDown4(src_ptr, src_stride, dst_ptr, dst_width);
    src_ptr += row_stride;
    dst_ptr += dst_stride;
  }
}

// Scale plane down, 3/4
static void ScalePlaneDown34(int src_width,
                             int src_height,
                             int dst_width,
                             int dst_height,
                             int src_stride,
                             int dst_stride,
                             const uint8_t* src_ptr,
                             uint8_t* dst_ptr,
                             enum FilterMode filtering) {
  int y;
  void (*ScaleRowDown34_0)(const uint8_t* src_ptr, ptrdiff_t src_stride,
                           uint8_t* dst_ptr, int dst_width);
  void (*ScaleRowDown34_1)(const uint8_t* src_ptr, ptrdiff_t src_stride,
                           uint8_t* dst_ptr, int dst_width);
  const int filter_stride = (filtering == kFilterLinear) ? 0 : src_stride;
  (void)src_width;
  (void)src_height;
  assert(dst_width % 3 == 0);
  if (!filtering) {
    ScaleRowDown34_0 = ScaleRowDown34_C;
    ScaleRowDown34_1 = ScaleRowDown34_C;
  } else {
    ScaleRowDown34_0 = ScaleRowDown34_0_Box_C;
    ScaleRowDown34_1 = ScaleRowDown34_1_Box_C;
  }

  for (y = 0; y < dst_height - 2; y += 3) {
    ScaleRowDown34_0(src_ptr, filter_stride, dst_ptr, dst_width);
    src_ptr += src_stride;
    dst_ptr += dst_stride;
    ScaleRowDown34_1(src_ptr, filter_stride, dst_ptr, dst_width);
    src_ptr += src_stride;
    dst_ptr += dst_stride;
    ScaleRowDown34_0(src_ptr + src_stride, -filter_stride, dst_ptr, dst_width);
    src_ptr += src_stride * 2;
    dst_ptr += dst_stride;
  }

  // Remainder 1 or 2 rows with last row vertically unfiltered
  if ((dst_height % 3) == 2) {
    ScaleRowDown34_0(src_ptr, filter_stride, dst_ptr, dst_width);
    src_ptr += src_stride;
    dst_ptr += dst_stride;
    ScaleRowDown34_1(src_ptr, 0, dst_ptr, dst_width);
  } else if ((dst_height % 3) == 1) {
    ScaleRowDown34_0(src_ptr, 0, dst_ptr, dst_width);
  }
}

static void ScalePlaneDown34_16(int src_width,
                                int src_height,
                                int dst_width,
                                int dst_height,
                                int src_stride,
                                int dst_stride,
                                const uint16_t* src_ptr,
                                uint16_t* dst_ptr,
                                enum FilterMode filtering) {
  int y;
  void (*ScaleRowDown34_0)(const uint16_t* src_ptr, ptrdiff_t src_stride,
                           uint16_t* dst_ptr, int dst_width);
  void (*ScaleRowDown34_1)(const uint16_t* src_ptr, ptrdiff_t src_stride,
                           uint16_t* dst_ptr, int dst_width);
  const int filter_stride = (filtering == kFilterLinear) ? 0 : src_stride;
  (void)src_width;
  (void)src_height;
  assert(dst_width % 3 == 0);
  if (!filtering) {
    ScaleRowDown34_0 = ScaleRowDown34_16_C;
    ScaleRowDown34_1 = ScaleRowDown34_16_C;
  } else {
    ScaleRowDown34_0 = ScaleRowDown34_0_Box_16_C;
    ScaleRowDown34_1 = ScaleRowDown34_1_Box_16_C;
  }

  for (y = 0; y < dst_height - 2; y += 3) {
    ScaleRowDown34_0(src_ptr, filter_stride, dst_ptr, dst_width);
    src_ptr += src_stride;
    dst_ptr += dst_stride;
    ScaleRowDown34_1(src_ptr, filter_stride, dst_ptr, dst_width);
    src_ptr += src_stride;
    dst_ptr += dst_stride;
    ScaleRowDown34_0(src_ptr + src_stride, -filter_stride, dst_ptr, dst_width);
    src_ptr += src_stride * 2;
    dst_ptr += dst_stride;
  }

  // Remainder 1 or 2 rows with last row vertically unfiltered
  if ((dst_height % 3) == 2) {
    ScaleRowDown34_0(src_ptr, filter_stride, dst_ptr, dst_width);
    src_ptr += src_stride;
    dst_ptr += dst_stride;
    ScaleRowDown34_1(src_ptr, 0, dst_ptr, dst_width);
  } else if ((dst_height % 3) == 1) {
    ScaleRowDown34_0(src_ptr, 0, dst_ptr, dst_width);
  }
}

// Scale plane, 3/8
// This is an optimized version for scaling down a plane to 3/8
// of its original size.
//
// Uses box filter arranges like this
// aaabbbcc -> abc
// aaabbbcc    def
// aaabbbcc    ghi
// dddeeeff
// dddeeeff
// dddeeeff
// ggghhhii
// ggghhhii
// Boxes are 3x3, 2x3, 3x2 and 2x2

static void ScalePlaneDown38(int src_width,
                             int src_height,
                             int dst_width,
                             int dst_height,
                             int src_stride,
                             int dst_stride,
                             const uint8_t* src_ptr,
                             uint8_t* dst_ptr,
                             enum FilterMode filtering) {
  int y;
  void (*ScaleRowDown38_3)(const uint8_t* src_ptr, ptrdiff_t src_stride,
                           uint8_t* dst_ptr, int dst_width);
  void (*ScaleRowDown38_2)(const uint8_t* src_ptr, ptrdiff_t src_stride,
                           uint8_t* dst_ptr, int dst_width);
  const int filter_stride = (filtering == kFilterLinear) ? 0 : src_stride;
  assert(dst_width % 3 == 0);
  (void)src_width;
  (void)src_height;
  if (!filtering) {
    ScaleRowDown38_3 = ScaleRowDown38_C;
    ScaleRowDown38_2 = ScaleRowDown38_C;
  } else {
    ScaleRowDown38_3 = ScaleRowDown38_3_Box_C;
    ScaleRowDown38_2 = ScaleRowDown38_2_Box_C;
  }

  for (y = 0; y < dst_height - 2; y += 3) {
    ScaleRowDown38_3(src_ptr, filter_stride, dst_ptr, dst_width);
    src_ptr += src_stride * 3;
    dst_ptr += dst_stride;
    ScaleRowDown38_3(src_ptr, filter_stride, dst_ptr, dst_width);
    src_ptr += src_stride * 3;
    dst_ptr += dst_stride;
    ScaleRowDown38_2(src_ptr, filter_stride, dst_ptr, dst_width);
    src_ptr += src_stride * 2;
    dst_ptr += dst_stride;
  }

  // Remainder 1 or 2 rows with last row vertically unfiltered
  if ((dst_height % 3) == 2) {
    ScaleRowDown38_3(src_ptr, filter_stride, dst_ptr, dst_width);
    src_ptr += src_stride * 3;
    dst_ptr += dst_stride;
    ScaleRowDown38_3(src_ptr, 0, dst_ptr, dst_width);
  } else if ((dst_height % 3) == 1) {
    ScaleRowDown38_3(src_ptr, 0, dst_ptr, dst_width);
  }
}

static void ScalePlaneDown38_16(int src_width,
                                int src_height,
                                int dst_width,
                                int dst_height,
                                int src_stride,
                                int dst_stride,
                                const uint16_t* src_ptr,
                                uint16_t* dst_ptr,
                                enum FilterMode filtering) {
  int y;
  void (*ScaleRowDown38_3)(const uint16_t* src_ptr, ptrdiff_t src_stride,
                           uint16_t* dst_ptr, int dst_width);
  void (*ScaleRowDown38_2)(const uint16_t* src_ptr, ptrdiff_t src_stride,
                           uint16_t* dst_ptr, int dst_width);
  const int filter_stride = (filtering == kFilterLinear) ? 0 : src_stride;
  (void)src_width;
  (void)src_height;
  assert(dst_width % 3 == 0);
  if (!filtering) {
    ScaleRowDown38_3 = ScaleRowDown38_16_C;
    ScaleRowDown38_2 = ScaleRowDown38_16_C;
  } else {
    ScaleRowDown38_3 = ScaleRowDown38_3_Box_16_C;
    ScaleRowDown38_2 = ScaleRowDown38_2_Box_16_C;
  }

  for (y = 0; y < dst_height - 2; y += 3) {
    ScaleRowDown38_3(src_ptr, filter_stride, dst_ptr, dst_width);
    src_ptr += src_stride * 3;
    dst_ptr += dst_stride;
    ScaleRowDown38_3(src_ptr, filter_stride, dst_ptr, dst_width);
    src_ptr += src_stride * 3;
    dst_ptr += dst_stride;
    ScaleRowDown38_2(src_ptr, filter_stride, dst_ptr, dst_width);
    src_ptr += src_stride * 2;
    dst_ptr += dst_stride;
  }

  // Remainder 1 or 2 rows with last row vertically unfiltered
  if ((dst_height % 3) == 2) {
    ScaleRowDown38_3(src_ptr, filter_stride, dst_ptr, dst_width);
    src_ptr += src_stride * 3;
    dst_ptr += dst_stride;
    ScaleRowDown38_3(src_ptr, 0, dst_ptr, dst_width);
  } else if ((dst_height % 3) == 1) {
    ScaleRowDown38_3(src_ptr, 0, dst_ptr, dst_width);
  }
}

#define MIN1(x) ((x) < 1 ? 1 : (x))

static __inline uint32_t SumPixels(int iboxwidth, const uint16_t* src_ptr) {
  uint32_t sum = 0u;
  int x;
  assert(iboxwidth > 0);
  for (x = 0; x < iboxwidth; ++x) {
    sum += src_ptr[x];
  }
  return sum;
}

static __inline uint32_t SumPixels_16(int iboxwidth, const uint32_t* src_ptr) {
  uint32_t sum = 0u;
  int x;
  assert(iboxwidth > 0);
  for (x = 0; x < iboxwidth; ++x) {
    sum += src_ptr[x];
  }
  return sum;
}

static void ScaleAddCols2_C(int dst_width,
                            int boxheight,
                            int x,
                            int dx,
                            const uint16_t* src_ptr,
                            uint8_t* dst_ptr) {
  int i;
  int scaletbl[2];
  int minboxwidth = dx >> 16;
  int boxwidth;
  scaletbl[0] = 65536 / (MIN1(minboxwidth) * boxheight);
  scaletbl[1] = 65536 / (MIN1(minboxwidth + 1) * boxheight);
  for (i = 0; i < dst_width; ++i) {
    int ix = x >> 16;
    x += dx;
    boxwidth = MIN1((x >> 16) - ix);
    int scaletbl_index = boxwidth - minboxwidth;
    assert((scaletbl_index == 0) || (scaletbl_index == 1));
    *dst_ptr++ = (uint8_t)(SumPixels(boxwidth, src_ptr + ix) *
                               scaletbl[scaletbl_index] >>
                           16);
  }
}

static void ScaleAddCols2_16_C(int dst_width,
                               int boxheight,
                               int x,
                               int dx,
                               const uint32_t* src_ptr,
                               uint16_t* dst_ptr) {
  int i;
  int scaletbl[2];
  int minboxwidth = dx >> 16;
  int boxwidth;
  scaletbl[0] = 65536 / (MIN1(minboxwidth) * boxheight);
  scaletbl[1] = 65536 / (MIN1(minboxwidth + 1) * boxheight);
  for (i = 0; i < dst_width; ++i) {
    int ix = x >> 16;
    x += dx;
    boxwidth = MIN1((x >> 16) - ix);
    int scaletbl_index = boxwidth - minboxwidth;
    assert((scaletbl_index == 0) || (scaletbl_index == 1));
    *dst_ptr++ =
        SumPixels_16(boxwidth, src_ptr + ix) * scaletbl[scaletbl_index] >> 16;
  }
}

static void ScaleAddCols0_C(int dst_width,
                            int boxheight,
                            int x,
                            int dx,
                            const uint16_t* src_ptr,
                            uint8_t* dst_ptr) {
  int scaleval = 65536 / boxheight;
  int i;
  (void)dx;
  src_ptr += (x >> 16);
  for (i = 0; i < dst_width; ++i) {
    *dst_ptr++ = (uint8_t)(src_ptr[i] * scaleval >> 16);
  }
}

static void ScaleAddCols1_C(int dst_width,
                            int boxheight,
                            int x,
                            int dx,
                            const uint16_t* src_ptr,
                            uint8_t* dst_ptr) {
  int boxwidth = MIN1(dx >> 16);
  int scaleval = 65536 / (boxwidth * boxheight);
  int i;
  x >>= 16;
  for (i = 0; i < dst_width; ++i) {
    *dst_ptr++ = (uint8_t)(SumPixels(boxwidth, src_ptr + x) * scaleval >> 16);
    x += boxwidth;
  }
}

static void ScaleAddCols1_16_C(int dst_width,
                               int boxheight,
                               int x,
                               int dx,
                               const uint32_t* src_ptr,
                               uint16_t* dst_ptr) {
  int boxwidth = MIN1(dx >> 16);
  int scaleval = 65536 / (boxwidth * boxheight);
  int i;
  for (i = 0; i < dst_width; ++i) {
    *dst_ptr++ = SumPixels_16(boxwidth, src_ptr + x) * scaleval >> 16;
    x += boxwidth;
  }
}

// Scale plane down to any dimensions, with interpolation.
// (boxfilter).
//
// Same method as SimpleScale, which is fixed point, outputting
// one pixel of destination using fixed point (16.16) to step
// through source, sampling a box of pixel with simple
// averaging.
static void ScalePlaneBox(int src_width,
                          int src_height,
                          int dst_width,
                          int dst_height,
                          int src_stride,
                          int dst_stride,
                          const uint8_t* src_ptr,
                          uint8_t* dst_ptr) {
  int j, k;
  // Initial source x/y coordinate and step values as 16.16 fixed point.
  int x = 0;
  int y = 0;
  int dx = 0;
  int dy = 0;
  const int max_y = (src_height << 16);
  ScaleSlope(src_width, src_height, dst_width, dst_height, kFilterBox, &x, &y,
             &dx, &dy);
  src_width = Abs(src_width);
  {
    // Allocate a row buffer of uint16_t.
    align_buffer_64(row16, src_width * 2);
    void (*ScaleAddCols)(int dst_width, int boxheight, int x, int dx,
                         const uint16_t* src_ptr, uint8_t* dst_ptr) =
        (dx & 0xffff) ? ScaleAddCols2_C
                      : ((dx != 0x10000) ? ScaleAddCols1_C : ScaleAddCols0_C);
    void (*ScaleAddRow)(const uint8_t* src_ptr, uint16_t* dst_ptr,
                        int src_width) = ScaleAddRow_C;

    for (j = 0; j < dst_height; ++j) {
      int boxheight;
      int iy = y >> 16;
      const uint8_t* src = src_ptr + iy * (int64_t)src_stride;
      y += dy;
      if (y > max_y) {
        y = max_y;
      }
      boxheight = MIN1((y >> 16) - iy);
      memset(row16, 0, src_width * 2);
      for (k = 0; k < boxheight; ++k) {
        ScaleAddRow(src, (uint16_t*)(row16), src_width);
        src += src_stride;
      }
      ScaleAddCols(dst_width, boxheight, x, dx, (uint16_t*)(row16), dst_ptr);
      dst_ptr += dst_stride;
    }
    free_aligned_buffer_64(row16);
  }
}

static void ScalePlaneBox_16(int src_width,
                             int src_height,
                             int dst_width,
                             int dst_height,
                             int src_stride,
                             int dst_stride,
                             const uint16_t* src_ptr,
                             uint16_t* dst_ptr) {
  int j, k;
  // Initial source x/y coordinate and step values as 16.16 fixed point.
  int x = 0;
  int y = 0;
  int dx = 0;
  int dy = 0;
  const int max_y = (src_height << 16);
  ScaleSlope(src_width, src_height, dst_width, dst_height, kFilterBox, &x, &y,
             &dx, &dy);
  src_width = Abs(src_width);
  {
    // Allocate a row buffer of uint32_t.
    align_buffer_64(row32, src_width * 4);
    void (*ScaleAddCols)(int dst_width, int boxheight, int x, int dx,
                         const uint32_t* src_ptr, uint16_t* dst_ptr) =
        (dx & 0xffff) ? ScaleAddCols2_16_C : ScaleAddCols1_16_C;
    void (*ScaleAddRow)(const uint16_t* src_ptr, uint32_t* dst_ptr,
                        int src_width) = ScaleAddRow_16_C;

#if defined(HAS_SCALEADDROW_16_SSE2)
    if (TestCpuFlag(kCpuHasSSE2) && IS_ALIGNED(src_width, 16)) {
      ScaleAddRow = ScaleAddRow_16_SSE2;
    }
#endif

    for (j = 0; j < dst_height; ++j) {
      int boxheight;
      int iy = y >> 16;
      const uint16_t* src = src_ptr + iy * (int64_t)src_stride;
      y += dy;
      if (y > max_y) {
        y = max_y;
      }
      boxheight = MIN1((y >> 16) - iy);
      memset(row32, 0, src_width * 4);
      for (k = 0; k < boxheight; ++k) {
        ScaleAddRow(src, (uint32_t*)(row32), src_width);
        src += src_stride;
      }
      ScaleAddCols(dst_width, boxheight, x, dx, (uint32_t*)(row32), dst_ptr);
      dst_ptr += dst_stride;
    }
    free_aligned_buffer_64(row32);
  }
}

// Scale plane down with bilinear interpolation.
static void ScalePlaneBilinearDown(int src_width,
                                   int src_height,
                                   int dst_width,
                                   int dst_height,
                                   int src_stride,
                                   int dst_stride,
                                   const uint8_t* src_ptr,
                                   uint8_t* dst_ptr,
                                   enum FilterMode filtering) {
  // Initial source x/y coordinate and step values as 16.16 fixed point.
  int x = 0;
  int y = 0;
  int dx = 0;
  int dy = 0;
  // TODO(fbarchard): Consider not allocating row buffer for kFilterLinear.
  // Allocate a row buffer.
  align_buffer_64(row, src_width);

  const int max_y = (src_height - 1) << 16;
  int j;
  void (*ScaleFilterCols)(uint8_t* dst_ptr, const uint8_t* src_ptr,
                          int dst_width, int x, int dx) =
      (src_width >= 32768) ? ScaleFilterCols64_C : ScaleFilterCols_C;
  void (*InterpolateRow)(uint8_t* dst_ptr, const uint8_t* src_ptr,
                         ptrdiff_t src_stride, int dst_width,
                         int source_y_fraction) = InterpolateRow_C;
  ScaleSlope(src_width, src_height, dst_width, dst_height, filtering, &x, &y,
             &dx, &dy);
  src_width = Abs(src_width);

  if (y > max_y) {
    y = max_y;
  }

  for (j = 0; j < dst_height; ++j) {
    int yi = y >> 16;
    const uint8_t* src = src_ptr + yi * (int64_t)src_stride;
    if (filtering == kFilterLinear) {
      ScaleFilterCols(dst_ptr, src, dst_width, x, dx);
    } else {
      int yf = (y >> 8) & 255;
      InterpolateRow(row, src, src_stride, src_width, yf);
      ScaleFilterCols(dst_ptr, row, dst_width, x, dx);
    }
    dst_ptr += dst_stride;
    y += dy;
    if (y > max_y) {
      y = max_y;
    }
  }
  free_aligned_buffer_64(row);
}

static void ScalePlaneBilinearDown_16(int src_width,
                                      int src_height,
                                      int dst_width,
                                      int dst_height,
                                      int src_stride,
                                      int dst_stride,
                                      const uint16_t* src_ptr,
                                      uint16_t* dst_ptr,
                                      enum FilterMode filtering) {
  // Initial source x/y coordinate and step values as 16.16 fixed point.
  int x = 0;
  int y = 0;
  int dx = 0;
  int dy = 0;
  // TODO(fbarchard): Consider not allocating row buffer for kFilterLinear.
  // Allocate a row buffer.
  align_buffer_64(row, src_width * 2);

  const int max_y = (src_height - 1) << 16;
  int j;
  void (*ScaleFilterCols)(uint16_t* dst_ptr, const uint16_t* src_ptr,
                          int dst_width, int x, int dx) =
      (src_width >= 32768) ? ScaleFilterCols64_16_C : ScaleFilterCols_16_C;
  void (*InterpolateRow)(uint16_t* dst_ptr, const uint16_t* src_ptr,
                         ptrdiff_t src_stride, int dst_width,
                         int source_y_fraction) = InterpolateRow_16_C;
  ScaleSlope(src_width, src_height, dst_width, dst_height, filtering, &x, &y,
             &dx, &dy);
  src_width = Abs(src_width);

  if (y > max_y) {
    y = max_y;
  }

  for (j = 0; j < dst_height; ++j) {
    int yi = y >> 16;
    const uint16_t* src = src_ptr + yi * (int64_t)src_stride;
    if (filtering == kFilterLinear) {
      ScaleFilterCols(dst_ptr, src, dst_width, x, dx);
    } else {
      int yf = (y >> 8) & 255;
      InterpolateRow((uint16_t*)row, src, src_stride, src_width, yf);
      ScaleFilterCols(dst_ptr, (uint16_t*)row, dst_width, x, dx);
    }
    dst_ptr += dst_stride;
    y += dy;
    if (y > max_y) {
      y = max_y;
    }
  }
  free_aligned_buffer_64(row);
}

// Scale up down with bilinear interpolation.
static void ScalePlaneBilinearUp(int src_width,
                                 int src_height,
                                 int dst_width,
                                 int dst_height,
                                 int src_stride,
                                 int dst_stride,
                                 const uint8_t* src_ptr,
                                 uint8_t* dst_ptr,
                                 enum FilterMode filtering) {
  int j;
  // Initial source x/y coordinate and step values as 16.16 fixed point.
  int x = 0;
  int y = 0;
  int dx = 0;
  int dy = 0;
  const int max_y = (src_height - 1) << 16;
  void (*InterpolateRow)(uint8_t* dst_ptr, const uint8_t* src_ptr,
                         ptrdiff_t src_stride, int dst_width,
                         int source_y_fraction) = InterpolateRow_C;
  void (*ScaleFilterCols)(uint8_t* dst_ptr, const uint8_t* src_ptr,
                          int dst_width, int x, int dx) =
      filtering ? ScaleFilterCols_C : ScaleCols_C;
  ScaleSlope(src_width, src_height, dst_width, dst_height, filtering, &x, &y,
             &dx, &dy);
  src_width = Abs(src_width);

  if (filtering && src_width >= 32768) {
    ScaleFilterCols = ScaleFilterCols64_C;
  }
  if (!filtering && src_width * 2 == dst_width && x < 0x8000) {
    ScaleFilterCols = ScaleColsUp2_C;
  }

  if (y > max_y) {
    y = max_y;
  }
  {
    int yi = y >> 16;
    const uint8_t* src = src_ptr + yi * (int64_t)src_stride;

    // Allocate 2 row buffers.
    const int row_size = (dst_width + 31) & ~31;
    align_buffer_64(row, row_size * 2);

    uint8_t* rowptr = row;
    int rowstride = row_size;
    int lasty = yi;

    ScaleFilterCols(rowptr, src, dst_width, x, dx);
    if (src_height > 1) {
      src += src_stride;
    }
    ScaleFilterCols(rowptr + rowstride, src, dst_width, x, dx);
    if (src_height > 2) {
      src += src_stride;
    }

    for (j = 0; j < dst_height; ++j) {
      yi = y >> 16;
      if (yi != lasty) {
        if (y > max_y) {
          y = max_y;
          yi = y >> 16;
          src = src_ptr + yi * (int64_t)src_stride;
        }
        if (yi != lasty) {
          ScaleFilterCols(rowptr, src, dst_width, x, dx);
          rowptr += rowstride;
          rowstride = -rowstride;
          lasty = yi;
          if ((y + 65536) < max_y) {
            src += src_stride;
          }
        }
      }
      if (filtering == kFilterLinear) {
        InterpolateRow(dst_ptr, rowptr, 0, dst_width, 0);
      } else {
        int yf = (y >> 8) & 255;
        InterpolateRow(dst_ptr, rowptr, rowstride, dst_width, yf);
      }
      dst_ptr += dst_stride;
      y += dy;
    }
    free_aligned_buffer_64(row);
  }
}


// Scale up horizontally 2 times using linear filter.
#define SUH2LANY(NAME, SIMD, C, MASK, PTYPE)                       \
  void NAME(const PTYPE* src_ptr, PTYPE* dst_ptr, int dst_width) { \
    int work_width = (dst_width - 1) & ~1;                         \
    int r = work_width & MASK;                                     \
    int n = work_width & ~MASK;                                    \
    dst_ptr[0] = src_ptr[0];                                       \
    if (work_width > 0) {                                          \
      if (n != 0) {                                                \
        SIMD(src_ptr, dst_ptr + 1, n);                             \
      }                                                            \
      C(src_ptr + (n / 2), dst_ptr + n + 1, r);                    \
    }                                                              \
    dst_ptr[dst_width - 1] = src_ptr[(dst_width - 1) / 2];         \
  }

// Even the C versions need to be wrapped, because boundary pixels have to
// be handled differently

SUH2LANY(ScaleRowUp2_Linear_Any_C,
         ScaleRowUp2_Linear_C,
         ScaleRowUp2_Linear_C,
         0,
         uint8_t)

SUH2LANY(ScaleRowUp2_Linear_16_Any_C,
         ScaleRowUp2_Linear_16_C,
         ScaleRowUp2_Linear_16_C,
         0,
         uint16_t)

// Scale plane, horizontally up by 2 times.
// Uses linear filter horizontally, nearest vertically.
// This is an optimized version for scaling up a plane to 2 times of
// its original width, using linear interpolation.
// This is used to scale U and V planes of I422 to I444.
static void ScalePlaneUp2_Linear(int src_width,
                                 int src_height,
                                 int dst_width,
                                 int dst_height,
                                 int src_stride,
                                 int dst_stride,
                                 const uint8_t* src_ptr,
                                 uint8_t* dst_ptr) {
  void (*ScaleRowUp)(const uint8_t* src_ptr, uint8_t* dst_ptr, int dst_width) =
      ScaleRowUp2_Linear_Any_C;
  int i;
  int y;
  int dy;

  // This function can only scale up by 2 times horizontally.
  assert(src_width == ((dst_width + 1) / 2));

  if (dst_height == 1) {
    ScaleRowUp(src_ptr + ((src_height - 1) / 2) * (int64_t)src_stride, dst_ptr,
               dst_width);
  } else {
    dy = FixedDiv(src_height - 1, dst_height - 1);
    y = (1 << 15) - 1;
    for (i = 0; i < dst_height; ++i) {
      ScaleRowUp(src_ptr + (y >> 16) * (int64_t)src_stride, dst_ptr, dst_width);
      dst_ptr += dst_stride;
      y += dy;
    }
  }
}


// Scale up 2 times using bilinear filter.
// This function produces 2 rows at a time.
#define SU2BLANY(NAME, SIMD, C, MASK, PTYPE)                              \
  void NAME(const PTYPE* src_ptr, ptrdiff_t src_stride, PTYPE* dst_ptr,   \
            ptrdiff_t dst_stride, int dst_width) {                        \
    int work_width = (dst_width - 1) & ~1;                                \
    int r = work_width & MASK;                                            \
    int n = work_width & ~MASK;                                           \
    const PTYPE* sa = src_ptr;                                            \
    const PTYPE* sb = src_ptr + src_stride;                               \
    PTYPE* da = dst_ptr;                                                  \
    PTYPE* db = dst_ptr + dst_stride;                                     \
    da[0] = (3 * sa[0] + sb[0] + 2) >> 2;                                 \
    db[0] = (sa[0] + 3 * sb[0] + 2) >> 2;                                 \
    if (work_width > 0) {                                                 \
      if (n != 0) {                                                       \
        SIMD(sa, sb - sa, da + 1, db - da, n);                            \
      }                                                                   \
      C(sa + (n / 2), sb - sa, da + n + 1, db - da, r);                   \
    }                                                                     \
    da[dst_width - 1] =                                                   \
        (3 * sa[(dst_width - 1) / 2] + sb[(dst_width - 1) / 2] + 2) >> 2; \
    db[dst_width - 1] =                                                   \
        (sa[(dst_width - 1) / 2] + 3 * sb[(dst_width - 1) / 2] + 2) >> 2; \
  }

SU2BLANY(ScaleRowUp2_Bilinear_Any_C,
         ScaleRowUp2_Bilinear_C,
         ScaleRowUp2_Bilinear_C,
         0,
         uint8_t)

SU2BLANY(ScaleRowUp2_Bilinear_16_Any_C,
         ScaleRowUp2_Bilinear_16_C,
         ScaleRowUp2_Bilinear_16_C,
         0,
         uint16_t)

// Scale plane, up by 2 times.
// This is an optimized version for scaling up a plane to 2 times of
// its original size, using bilinear interpolation.
// This is used to scale U and V planes of I420 to I444.
static void ScalePlaneUp2_Bilinear(int src_width,
                                   int src_height,
                                   int dst_width,
                                   int dst_height,
                                   int src_stride,
                                   int dst_stride,
                                   const uint8_t* src_ptr,
                                   uint8_t* dst_ptr) {
  void (*Scale2RowUp)(const uint8_t* src_ptr, ptrdiff_t src_stride,
                      uint8_t* dst_ptr, ptrdiff_t dst_stride, int dst_width) =
      ScaleRowUp2_Bilinear_Any_C;
  int x;

  // This function can only scale up by 2 times.
  assert(src_width == ((dst_width + 1) / 2));
  assert(src_height == ((dst_height + 1) / 2));

  Scale2RowUp(src_ptr, 0, dst_ptr, 0, dst_width);
  dst_ptr += dst_stride;
  for (x = 0; x < src_height - 1; ++x) {
    Scale2RowUp(src_ptr, src_stride, dst_ptr, dst_stride, dst_width);
    src_ptr += src_stride;
    // TODO(fbarchard): Test performance of writing one row of destination at a
    // time.
    dst_ptr += 2 * dst_stride;
  }
  if (!(dst_height & 1)) {
    Scale2RowUp(src_ptr, 0, dst_ptr, 0, dst_width);
  }
}

// Scale at most 14 bit plane, horizontally up by 2 times.
// This is an optimized version for scaling up a plane to 2 times of
// its original width, using linear interpolation.
// stride is in count of uint16_t.
// This is used to scale U and V planes of I210 to I410 and I212 to I412.
static void ScalePlaneUp2_12_Linear(int src_width,
                                    int src_height,
                                    int dst_width,
                                    int dst_height,
                                    int src_stride,
                                    int dst_stride,
                                    const uint16_t* src_ptr,
                                    uint16_t* dst_ptr) {
  void (*ScaleRowUp)(const uint16_t* src_ptr, uint16_t* dst_ptr,
                     int dst_width) = ScaleRowUp2_Linear_16_Any_C;
  int i;
  int y;
  int dy;

  // This function can only scale up by 2 times horizontally.
  assert(src_width == ((dst_width + 1) / 2));

  if (dst_height == 1) {
    ScaleRowUp(src_ptr + ((src_height - 1) / 2) * (int64_t)src_stride, dst_ptr,
               dst_width);
  } else {
    dy = FixedDiv(src_height - 1, dst_height - 1);
    y = (1 << 15) - 1;
    for (i = 0; i < dst_height; ++i) {
      ScaleRowUp(src_ptr + (y >> 16) * (int64_t)src_stride, dst_ptr, dst_width);
      dst_ptr += dst_stride;
      y += dy;
    }
  }
}

// Scale at most 12 bit plane, up by 2 times.
// This is an optimized version for scaling up a plane to 2 times of
// its original size, using bilinear interpolation.
// stride is in count of uint16_t.
// This is used to scale U and V planes of I010 to I410 and I012 to I412.
static void ScalePlaneUp2_12_Bilinear(int src_width,
                                      int src_height,
                                      int dst_width,
                                      int dst_height,
                                      int src_stride,
                                      int dst_stride,
                                      const uint16_t* src_ptr,
                                      uint16_t* dst_ptr) {
  void (*Scale2RowUp)(const uint16_t* src_ptr, ptrdiff_t src_stride,
                      uint16_t* dst_ptr, ptrdiff_t dst_stride, int dst_width) =
      ScaleRowUp2_Bilinear_16_Any_C;
  int x;

  // This function can only scale up by 2 times.
  assert(src_width == ((dst_width + 1) / 2));
  assert(src_height == ((dst_height + 1) / 2));

  Scale2RowUp(src_ptr, 0, dst_ptr, 0, dst_width);
  dst_ptr += dst_stride;
  for (x = 0; x < src_height - 1; ++x) {
    Scale2RowUp(src_ptr, src_stride, dst_ptr, dst_stride, dst_width);
    src_ptr += src_stride;
    dst_ptr += 2 * dst_stride;
  }
  if (!(dst_height & 1)) {
    Scale2RowUp(src_ptr, 0, dst_ptr, 0, dst_width);
  }
}

static void ScalePlaneUp2_16_Linear(int src_width,
                                    int src_height,
                                    int dst_width,
                                    int dst_height,
                                    int src_stride,
                                    int dst_stride,
                                    const uint16_t* src_ptr,
                                    uint16_t* dst_ptr) {
  void (*ScaleRowUp)(const uint16_t* src_ptr, uint16_t* dst_ptr,
                     int dst_width) = ScaleRowUp2_Linear_16_Any_C;
  int i;
  int y;
  int dy;

  // This function can only scale up by 2 times horizontally.
  assert(src_width == ((dst_width + 1) / 2));

  if (dst_height == 1) {
    ScaleRowUp(src_ptr + ((src_height - 1) / 2) * (int64_t)src_stride, dst_ptr,
               dst_width);
  } else {
    dy = FixedDiv(src_height - 1, dst_height - 1);
    y = (1 << 15) - 1;
    for (i = 0; i < dst_height; ++i) {
      ScaleRowUp(src_ptr + (y >> 16) * (int64_t)src_stride, dst_ptr, dst_width);
      dst_ptr += dst_stride;
      y += dy;
    }
  }
}

static void ScalePlaneUp2_16_Bilinear(int src_width,
                                      int src_height,
                                      int dst_width,
                                      int dst_height,
                                      int src_stride,
                                      int dst_stride,
                                      const uint16_t* src_ptr,
                                      uint16_t* dst_ptr) {
  void (*Scale2RowUp)(const uint16_t* src_ptr, ptrdiff_t src_stride,
                      uint16_t* dst_ptr, ptrdiff_t dst_stride, int dst_width) =
      ScaleRowUp2_Bilinear_16_Any_C;
  int x;

  // This function can only scale up by 2 times.
  assert(src_width == ((dst_width + 1) / 2));
  assert(src_height == ((dst_height + 1) / 2));

  Scale2RowUp(src_ptr, 0, dst_ptr, 0, dst_width);
  dst_ptr += dst_stride;
  for (x = 0; x < src_height - 1; ++x) {
    Scale2RowUp(src_ptr, src_stride, dst_ptr, dst_stride, dst_width);
    src_ptr += src_stride;
    dst_ptr += 2 * dst_stride;
  }
  if (!(dst_height & 1)) {
    Scale2RowUp(src_ptr, 0, dst_ptr, 0, dst_width);
  }
}

static void ScalePlaneBilinearUp_16(int src_width,
                                    int src_height,
                                    int dst_width,
                                    int dst_height,
                                    int src_stride,
                                    int dst_stride,
                                    const uint16_t* src_ptr,
                                    uint16_t* dst_ptr,
                                    enum FilterMode filtering) {
  int j;
  // Initial source x/y coordinate and step values as 16.16 fixed point.
  int x = 0;
  int y = 0;
  int dx = 0;
  int dy = 0;
  const int max_y = (src_height - 1) << 16;
  void (*InterpolateRow)(uint16_t* dst_ptr, const uint16_t* src_ptr,
                         ptrdiff_t src_stride, int dst_width,
                         int source_y_fraction) = InterpolateRow_16_C;
  void (*ScaleFilterCols)(uint16_t* dst_ptr, const uint16_t* src_ptr,
                          int dst_width, int x, int dx) =
      filtering ? ScaleFilterCols_16_C : ScaleCols_16_C;
  ScaleSlope(src_width, src_height, dst_width, dst_height, filtering, &x, &y,
             &dx, &dy);
  src_width = Abs(src_width);

  if (filtering && src_width >= 32768) {
    ScaleFilterCols = ScaleFilterCols64_16_C;
  }
  if (!filtering && src_width * 2 == dst_width && x < 0x8000) {
    ScaleFilterCols = ScaleColsUp2_16_C;
  }
  if (y > max_y) {
    y = max_y;
  }
  {
    int yi = y >> 16;
    const uint16_t* src = src_ptr + yi * (int64_t)src_stride;

    // Allocate 2 row buffers.
    const int row_size = (dst_width + 31) & ~31;
    align_buffer_64(row, row_size * 4);

    uint16_t* rowptr = (uint16_t*)row;
    int rowstride = row_size;
    int lasty = yi;

    ScaleFilterCols(rowptr, src, dst_width, x, dx);
    if (src_height > 1) {
      src += src_stride;
    }
    ScaleFilterCols(rowptr + rowstride, src, dst_width, x, dx);
    if (src_height > 2) {
      src += src_stride;
    }

    for (j = 0; j < dst_height; ++j) {
      yi = y >> 16;
      if (yi != lasty) {
        if (y > max_y) {
          y = max_y;
          yi = y >> 16;
          src = src_ptr + yi * (int64_t)src_stride;
        }
        if (yi != lasty) {
          ScaleFilterCols(rowptr, src, dst_width, x, dx);
          rowptr += rowstride;
          rowstride = -rowstride;
          lasty = yi;
          if ((y + 65536) < max_y) {
            src += src_stride;
          }
        }
      }
      if (filtering == kFilterLinear) {
        InterpolateRow(dst_ptr, rowptr, 0, dst_width, 0);
      } else {
        int yf = (y >> 8) & 255;
        InterpolateRow(dst_ptr, rowptr, rowstride, dst_width, yf);
      }
      dst_ptr += dst_stride;
      y += dy;
    }
    free_aligned_buffer_64(row);
  }
}

// Scale Plane to/from any dimensions, without interpolation.
// Fixed point math is used for performance: The upper 16 bits
// of x and dx is the integer part of the source position and
// the lower 16 bits are the fixed decimal part.

static void ScalePlaneSimple(int src_width,
                             int src_height,
                             int dst_width,
                             int dst_height,
                             int src_stride,
                             int dst_stride,
                             const uint8_t* src_ptr,
                             uint8_t* dst_ptr) {
  int i;
  void (*ScaleCols)(uint8_t* dst_ptr, const uint8_t* src_ptr, int dst_width,
                    int x, int dx) = ScaleCols_C;
  // Initial source x/y coordinate and step values as 16.16 fixed point.
  int x = 0;
  int y = 0;
  int dx = 0;
  int dy = 0;
  ScaleSlope(src_width, src_height, dst_width, dst_height, kFilterNone, &x, &y,
             &dx, &dy);
  src_width = Abs(src_width);

  if (src_width * 2 == dst_width && x < 0x8000) {
    ScaleCols = ScaleColsUp2_C;
  }

  for (i = 0; i < dst_height; ++i) {
    ScaleCols(dst_ptr, src_ptr + (y >> 16) * (int64_t)src_stride, dst_width, x,
              dx);
    dst_ptr += dst_stride;
    y += dy;
  }
}

static void ScalePlaneSimple_16(int src_width,
                                int src_height,
                                int dst_width,
                                int dst_height,
                                int src_stride,
                                int dst_stride,
                                const uint16_t* src_ptr,
                                uint16_t* dst_ptr) {
  int i;
  void (*ScaleCols)(uint16_t* dst_ptr, const uint16_t* src_ptr, int dst_width,
                    int x, int dx) = ScaleCols_16_C;
  // Initial source x/y coordinate and step values as 16.16 fixed point.
  int x = 0;
  int y = 0;
  int dx = 0;
  int dy = 0;
  ScaleSlope(src_width, src_height, dst_width, dst_height, kFilterNone, &x, &y,
             &dx, &dy);
  src_width = Abs(src_width);

  if (src_width * 2 == dst_width && x < 0x8000) {
    ScaleCols = ScaleColsUp2_16_C;
  }

  for (i = 0; i < dst_height; ++i) {
    ScaleCols(dst_ptr, src_ptr + (y >> 16) * (int64_t)src_stride, dst_width, x,
              dx);
    dst_ptr += dst_stride;
    y += dy;
  }
}

// Scale a plane.
// This function dispatches to a specialized scaler based on scale factor.
void avifScalePlane(const uint8_t* src,
                int src_stride,
                int src_width,
                int src_height,
                uint8_t* dst,
                int dst_stride,
                int dst_width,
                int dst_height,
                enum FilterMode filtering) {
  // Simplify filtering when possible.
  filtering = ScaleFilterReduce(src_width, src_height, dst_width, dst_height,
                                filtering);

  // Negative height means invert the image.
  if (src_height < 0) {
    src_height = -src_height;
    src = src + (src_height - 1) * (int64_t)src_stride;
    src_stride = -src_stride;
  }
  // Use specialized scales to improve performance for common resolutions.
  // For example, all the 1/2 scalings will use ScalePlaneDown2()
  if (dst_width == src_width && dst_height == src_height) {
    // Straight copy.
    CopyPlane(src, src_stride, dst, dst_stride, dst_width, dst_height);
    return;
  }
  if (dst_width == src_width && filtering != kFilterBox) {
    int dy = 0;
    int y = 0;
    // When scaling down, use the center 2 rows to filter.
    // When scaling up, last row of destination uses the last 2 source rows.
    if (dst_height <= src_height) {
      dy = FixedDiv(src_height, dst_height);
      y = CENTERSTART(dy, -32768);  // Subtract 0.5 (32768) to center filter.
    } else if (src_height > 1 && dst_height > 1) {
      dy = FixedDiv1(src_height, dst_height);
    }
    // Arbitrary scale vertically, but unscaled horizontally.
    ScalePlaneVertical(src_height, dst_width, dst_height, src_stride,
                       dst_stride, src, dst, 0, y, dy, /*bpp=*/1, filtering);
    return;
  }
  if (dst_width <= Abs(src_width) && dst_height <= src_height) {
    // Scale down.
    if (4 * dst_width == 3 * src_width && 4 * dst_height == 3 * src_height) {
      // optimized, 3/4
      ScalePlaneDown34(src_width, src_height, dst_width, dst_height, src_stride,
                       dst_stride, src, dst, filtering);
      return;
    }
    if (2 * dst_width == src_width && 2 * dst_height == src_height) {
      // optimized, 1/2
      ScalePlaneDown2(src_width, src_height, dst_width, dst_height, src_stride,
                      dst_stride, src, dst, filtering);
      return;
    }
    // 3/8 rounded up for odd sized chroma height.
    if (8 * dst_width == 3 * src_width && 8 * dst_height == 3 * src_height) {
      // optimized, 3/8
      ScalePlaneDown38(src_width, src_height, dst_width, dst_height, src_stride,
                       dst_stride, src, dst, filtering);
      return;
    }
    if (4 * dst_width == src_width && 4 * dst_height == src_height &&
        (filtering == kFilterBox || filtering == kFilterNone)) {
      // optimized, 1/4
      ScalePlaneDown4(src_width, src_height, dst_width, dst_height, src_stride,
                      dst_stride, src, dst, filtering);
      return;
    }
  }
  if (filtering == kFilterBox && dst_height * 2 < src_height) {
    ScalePlaneBox(src_width, src_height, dst_width, dst_height, src_stride,
                  dst_stride, src, dst);
    return;
  }
  if ((dst_width + 1) / 2 == src_width && filtering == kFilterLinear) {
    ScalePlaneUp2_Linear(src_width, src_height, dst_width, dst_height,
                         src_stride, dst_stride, src, dst);
    return;
  }
  if ((dst_height + 1) / 2 == src_height && (dst_width + 1) / 2 == src_width &&
      (filtering == kFilterBilinear || filtering == kFilterBox)) {
    ScalePlaneUp2_Bilinear(src_width, src_height, dst_width, dst_height,
                           src_stride, dst_stride, src, dst);
    return;
  }
  if (filtering && dst_height > src_height) {
    ScalePlaneBilinearUp(src_width, src_height, dst_width, dst_height,
                         src_stride, dst_stride, src, dst, filtering);
    return;
  }
  if (filtering) {
    ScalePlaneBilinearDown(src_width, src_height, dst_width, dst_height,
                           src_stride, dst_stride, src, dst, filtering);
    return;
  }
  ScalePlaneSimple(src_width, src_height, dst_width, dst_height, src_stride,
                   dst_stride, src, dst);
}

void avifScalePlane_16(const uint16_t* src,
                   int src_stride,
                   int src_width,
                   int src_height,
                   uint16_t* dst,
                   int dst_stride,
                   int dst_width,
                   int dst_height,
                   enum FilterMode filtering) {
  // Simplify filtering when possible.
  filtering = ScaleFilterReduce(src_width, src_height, dst_width, dst_height,
                                filtering);

  // Negative height means invert the image.
  if (src_height < 0) {
    src_height = -src_height;
    src = src + (src_height - 1) * (int64_t)src_stride;
    src_stride = -src_stride;
  }
  // Use specialized scales to improve performance for common resolutions.
  // For example, all the 1/2 scalings will use ScalePlaneDown2()
  if (dst_width == src_width && dst_height == src_height) {
    // Straight copy.
    CopyPlane_16(src, src_stride, dst, dst_stride, dst_width, dst_height);
    return;
  }
  if (dst_width == src_width && filtering != kFilterBox) {
    int dy = 0;
    int y = 0;
    // When scaling down, use the center 2 rows to filter.
    // When scaling up, last row of destination uses the last 2 source rows.
    if (dst_height <= src_height) {
      dy = FixedDiv(src_height, dst_height);
      y = CENTERSTART(dy, -32768);  // Subtract 0.5 (32768) to center filter.
      // When scaling up, ensure the last row of destination uses the last
      // source. Avoid divide by zero for dst_height but will do no scaling
      // later.
    } else if (src_height > 1 && dst_height > 1) {
      dy = FixedDiv1(src_height, dst_height);
    }
    // Arbitrary scale vertically, but unscaled horizontally.
    ScalePlaneVertical_16(src_height, dst_width, dst_height, src_stride,
                          dst_stride, src, dst, 0, y, dy, /*bpp=*/1, filtering);
    return;
  }
  if (dst_width <= Abs(src_width) && dst_height <= src_height) {
    // Scale down.
    if (4 * dst_width == 3 * src_width && 4 * dst_height == 3 * src_height) {
      // optimized, 3/4
      ScalePlaneDown34_16(src_width, src_height, dst_width, dst_height,
                          src_stride, dst_stride, src, dst, filtering);
      return;
    }
    if (2 * dst_width == src_width && 2 * dst_height == src_height) {
      // optimized, 1/2
      ScalePlaneDown2_16(src_width, src_height, dst_width, dst_height,
                         src_stride, dst_stride, src, dst, filtering);
      return;
    }
    // 3/8 rounded up for odd sized chroma height.
    if (8 * dst_width == 3 * src_width && 8 * dst_height == 3 * src_height) {
      // optimized, 3/8
      ScalePlaneDown38_16(src_width, src_height, dst_width, dst_height,
                          src_stride, dst_stride, src, dst, filtering);
      return;
    }
    if (4 * dst_width == src_width && 4 * dst_height == src_height &&
        (filtering == kFilterBox || filtering == kFilterNone)) {
      // optimized, 1/4
      ScalePlaneDown4_16(src_width, src_height, dst_width, dst_height,
                         src_stride, dst_stride, src, dst, filtering);
      return;
    }
  }
  if (filtering == kFilterBox && dst_height * 2 < src_height) {
    ScalePlaneBox_16(src_width, src_height, dst_width, dst_height, src_stride,
                     dst_stride, src, dst);
    return;
  }
  if ((dst_width + 1) / 2 == src_width && filtering == kFilterLinear) {
    ScalePlaneUp2_16_Linear(src_width, src_height, dst_width, dst_height,
                            src_stride, dst_stride, src, dst);
    return;
  }
  if ((dst_height + 1) / 2 == src_height && (dst_width + 1) / 2 == src_width &&
      (filtering == kFilterBilinear || filtering == kFilterBox)) {
    ScalePlaneUp2_16_Bilinear(src_width, src_height, dst_width, dst_height,
                              src_stride, dst_stride, src, dst);
    return;
  }
  if (filtering && dst_height > src_height) {
    ScalePlaneBilinearUp_16(src_width, src_height, dst_width, dst_height,
                            src_stride, dst_stride, src, dst, filtering);
    return;
  }
  if (filtering) {
    ScalePlaneBilinearDown_16(src_width, src_height, dst_width, dst_height,
                              src_stride, dst_stride, src, dst, filtering);
    return;
  }
  ScalePlaneSimple_16(src_width, src_height, dst_width, dst_height, src_stride,
                      dst_stride, src, dst);
}

void avifScalePlane_12(const uint16_t* src,
                   int src_stride,
                   int src_width,
                   int src_height,
                   uint16_t* dst,
                   int dst_stride,
                   int dst_width,
                   int dst_height,
                   enum FilterMode filtering) {
  // Simplify filtering when possible.
  filtering = ScaleFilterReduce(src_width, src_height, dst_width, dst_height,
                                filtering);

  // Negative height means invert the image.
  if (src_height < 0) {
    src_height = -src_height;
    src = src + (src_height - 1) * (int64_t)src_stride;
    src_stride = -src_stride;
  }

  if ((dst_width + 1) / 2 == src_width && filtering == kFilterLinear) {
    ScalePlaneUp2_12_Linear(src_width, src_height, dst_width, dst_height,
                            src_stride, dst_stride, src, dst);
    return;
  }
  if ((dst_height + 1) / 2 == src_height && (dst_width + 1) / 2 == src_width &&
      (filtering == kFilterBilinear || filtering == kFilterBox)) {
    ScalePlaneUp2_12_Bilinear(src_width, src_height, dst_width, dst_height,
                              src_stride, dst_stride, src, dst);
    return;
  }

  avifScalePlane_16(src, src_stride, src_width, src_height, dst, dst_stride,
                dst_width, dst_height, filtering);
}


#ifdef __cplusplus
}  // extern "C"
}  // namespace avif_yuvscalelib
#endif
