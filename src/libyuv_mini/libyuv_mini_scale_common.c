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

#include "libyuv_mini_scale_row.h"
#include "libyuv_mini_row.h"


#define STATIC_CAST(type, expr) (type)(expr)


// TODO(fbarchard): make clamp255 preserve negative values.
static __inline int32_t clamp255(int32_t v) {
  return (-(v >= 255) | v) & 255;
}

// Use scale to convert lsb formats to msb, depending how many bits there are:
// 32768 = 9 bits
// 16384 = 10 bits
// 4096 = 12 bits
// 256 = 16 bits
// TODO(fbarchard): change scale to bits
#define C16TO8(v, scale) clamp255(((v) * (scale)) >> 16)

static __inline int Abs(int v) {
  return v >= 0 ? v : -v;
}


// Sample position: (O is src sample position, X is dst sample position)
//
//      v dst_ptr at here           v stop at here
//  X O X   X O X   X O X   X O X   X O X
//    ^ src_ptr at here
void ScaleRowUp2_Linear_C(const uint8_t* src_ptr,
                          uint8_t* dst_ptr,
                          int dst_width) {
  int src_width = dst_width >> 1;
  int x;
  assert((dst_width % 2 == 0) && (dst_width >= 0));
  for (x = 0; x < src_width; ++x) {
    dst_ptr[2 * x + 0] = (src_ptr[x + 0] * 3 + src_ptr[x + 1] * 1 + 2) >> 2;
    dst_ptr[2 * x + 1] = (src_ptr[x + 0] * 1 + src_ptr[x + 1] * 3 + 2) >> 2;
  }
}

// Sample position: (O is src sample position, X is dst sample position)
//
//    src_ptr at here
//  X v X   X   X   X   X   X   X   X   X
//    O       O       O       O       O
//  X   X   X   X   X   X   X   X   X   X
//      ^ dst_ptr at here           ^ stop at here
//  X   X   X   X   X   X   X   X   X   X
//    O       O       O       O       O
//  X   X   X   X   X   X   X   X   X   X
void ScaleRowUp2_Bilinear_C(const uint8_t* src_ptr,
                            ptrdiff_t src_stride,
                            uint8_t* dst_ptr,
                            ptrdiff_t dst_stride,
                            int dst_width) {
  const uint8_t* s = src_ptr;
  const uint8_t* t = src_ptr + src_stride;
  uint8_t* d = dst_ptr;
  uint8_t* e = dst_ptr + dst_stride;
  int src_width = dst_width >> 1;
  int x;
  assert((dst_width % 2 == 0) && (dst_width >= 0));
  for (x = 0; x < src_width; ++x) {
    d[2 * x + 0] =
        (s[x + 0] * 9 + s[x + 1] * 3 + t[x + 0] * 3 + t[x + 1] * 1 + 8) >> 4;
    d[2 * x + 1] =
        (s[x + 0] * 3 + s[x + 1] * 9 + t[x + 0] * 1 + t[x + 1] * 3 + 8) >> 4;
    e[2 * x + 0] =
        (s[x + 0] * 3 + s[x + 1] * 1 + t[x + 0] * 9 + t[x + 1] * 3 + 8) >> 4;
    e[2 * x + 1] =
        (s[x + 0] * 1 + s[x + 1] * 3 + t[x + 0] * 3 + t[x + 1] * 9 + 8) >> 4;
  }
}

// Only suitable for at most 14 bit range.
void ScaleRowUp2_Linear_16_C(const uint16_t* src_ptr,
                             uint16_t* dst_ptr,
                             int dst_width) {
  int src_width = dst_width >> 1;
  int x;
  assert((dst_width % 2 == 0) && (dst_width >= 0));
  for (x = 0; x < src_width; ++x) {
    dst_ptr[2 * x + 0] = (src_ptr[x + 0] * 3 + src_ptr[x + 1] * 1 + 2) >> 2;
    dst_ptr[2 * x + 1] = (src_ptr[x + 0] * 1 + src_ptr[x + 1] * 3 + 2) >> 2;
  }
}

// Only suitable for at most 12bit range.
void ScaleRowUp2_Bilinear_16_C(const uint16_t* src_ptr,
                               ptrdiff_t src_stride,
                               uint16_t* dst_ptr,
                               ptrdiff_t dst_stride,
                               int dst_width) {
  const uint16_t* s = src_ptr;
  const uint16_t* t = src_ptr + src_stride;
  uint16_t* d = dst_ptr;
  uint16_t* e = dst_ptr + dst_stride;
  int src_width = dst_width >> 1;
  int x;
  assert((dst_width % 2 == 0) && (dst_width >= 0));
  for (x = 0; x < src_width; ++x) {
    d[2 * x + 0] =
        (s[x + 0] * 9 + s[x + 1] * 3 + t[x + 0] * 3 + t[x + 1] * 1 + 8) >> 4;
    d[2 * x + 1] =
        (s[x + 0] * 3 + s[x + 1] * 9 + t[x + 0] * 1 + t[x + 1] * 3 + 8) >> 4;
    e[2 * x + 0] =
        (s[x + 0] * 3 + s[x + 1] * 1 + t[x + 0] * 9 + t[x + 1] * 3 + 8) >> 4;
    e[2 * x + 1] =
        (s[x + 0] * 1 + s[x + 1] * 3 + t[x + 0] * 3 + t[x + 1] * 9 + 8) >> 4;
  }
}

// Scales a single row of pixels using point sampling.
void ScaleCols_C(uint8_t* dst_ptr,
                 const uint8_t* src_ptr,
                 int dst_width,
                 int x,
                 int dx) {
  int j;
  for (j = 0; j < dst_width - 1; j += 2) {
    dst_ptr[0] = src_ptr[x >> 16];
    x += dx;
    dst_ptr[1] = src_ptr[x >> 16];
    x += dx;
    dst_ptr += 2;
  }
  if (dst_width & 1) {
    dst_ptr[0] = src_ptr[x >> 16];
  }
}

void ScaleCols_16_C(uint16_t* dst_ptr,
                    const uint16_t* src_ptr,
                    int dst_width,
                    int x,
                    int dx) {
  int j;
  for (j = 0; j < dst_width - 1; j += 2) {
    dst_ptr[0] = src_ptr[x >> 16];
    x += dx;
    dst_ptr[1] = src_ptr[x >> 16];
    x += dx;
    dst_ptr += 2;
  }
  if (dst_width & 1) {
    dst_ptr[0] = src_ptr[x >> 16];
  }
}

// Scales a single row of pixels up by 2x using point sampling.
void ScaleColsUp2_C(uint8_t* dst_ptr,
                    const uint8_t* src_ptr,
                    int dst_width,
                    int x,
                    int dx) {
  int j;
  (void)x;
  (void)dx;
  for (j = 0; j < dst_width - 1; j += 2) {
    dst_ptr[1] = dst_ptr[0] = src_ptr[0];
    src_ptr += 1;
    dst_ptr += 2;
  }
  if (dst_width & 1) {
    dst_ptr[0] = src_ptr[0];
  }
}

void ScaleColsUp2_16_C(uint16_t* dst_ptr,
                       const uint16_t* src_ptr,
                       int dst_width,
                       int x,
                       int dx) {
  int j;
  (void)x;
  (void)dx;
  for (j = 0; j < dst_width - 1; j += 2) {
    dst_ptr[1] = dst_ptr[0] = src_ptr[0];
    src_ptr += 1;
    dst_ptr += 2;
  }
  if (dst_width & 1) {
    dst_ptr[0] = src_ptr[0];
  }
}

// (1-f)a + fb can be replaced with a + f(b-a)
#if defined(__arm__) || defined(__aarch64__)
#define BLENDER(a, b, f) \
  (uint8_t)((int)(a) + ((((int)((f)) * ((int)(b) - (int)(a))) + 0x8000) >> 16))
#else
// Intel uses 7 bit math with rounding.
#define BLENDER(a, b, f) \
  (uint8_t)((int)(a) + (((int)((f) >> 9) * ((int)(b) - (int)(a)) + 0x40) >> 7))
#endif

void ScaleFilterCols_C(uint8_t* dst_ptr,
                       const uint8_t* src_ptr,
                       int dst_width,
                       int x,
                       int dx) {
  int j;
  for (j = 0; j < dst_width - 1; j += 2) {
    int xi = x >> 16;
    int a = src_ptr[xi];
    int b = src_ptr[xi + 1];
    dst_ptr[0] = BLENDER(a, b, x & 0xffff);
    x += dx;
    xi = x >> 16;
    a = src_ptr[xi];
    b = src_ptr[xi + 1];
    dst_ptr[1] = BLENDER(a, b, x & 0xffff);
    x += dx;
    dst_ptr += 2;
  }
  if (dst_width & 1) {
    int xi = x >> 16;
    int a = src_ptr[xi];
    int b = src_ptr[xi + 1];
    dst_ptr[0] = BLENDER(a, b, x & 0xffff);
  }
}

void ScaleFilterCols64_C(uint8_t* dst_ptr,
                         const uint8_t* src_ptr,
                         int dst_width,
                         int x32,
                         int dx) {
  int64_t x = (int64_t)(x32);
  int j;
  for (j = 0; j < dst_width - 1; j += 2) {
    int64_t xi = x >> 16;
    int a = src_ptr[xi];
    int b = src_ptr[xi + 1];
    dst_ptr[0] = BLENDER(a, b, x & 0xffff);
    x += dx;
    xi = x >> 16;
    a = src_ptr[xi];
    b = src_ptr[xi + 1];
    dst_ptr[1] = BLENDER(a, b, x & 0xffff);
    x += dx;
    dst_ptr += 2;
  }
  if (dst_width & 1) {
    int64_t xi = x >> 16;
    int a = src_ptr[xi];
    int b = src_ptr[xi + 1];
    dst_ptr[0] = BLENDER(a, b, x & 0xffff);
  }
}
#undef BLENDER

// Same as 8 bit arm blender but return is cast to uint16_t
#define BLENDER(a, b, f) \
  (uint16_t)(            \
      (int)(a) +         \
      (int)((((int64_t)((f)) * ((int64_t)(b) - (int)(a))) + 0x8000) >> 16))

void ScaleFilterCols_16_C(uint16_t* dst_ptr,
                          const uint16_t* src_ptr,
                          int dst_width,
                          int x,
                          int dx) {
  int j;
  for (j = 0; j < dst_width - 1; j += 2) {
    int xi = x >> 16;
    int a = src_ptr[xi];
    int b = src_ptr[xi + 1];
    dst_ptr[0] = BLENDER(a, b, x & 0xffff);
    x += dx;
    xi = x >> 16;
    a = src_ptr[xi];
    b = src_ptr[xi + 1];
    dst_ptr[1] = BLENDER(a, b, x & 0xffff);
    x += dx;
    dst_ptr += 2;
  }
  if (dst_width & 1) {
    int xi = x >> 16;
    int a = src_ptr[xi];
    int b = src_ptr[xi + 1];
    dst_ptr[0] = BLENDER(a, b, x & 0xffff);
  }
}

void ScaleFilterCols64_16_C(uint16_t* dst_ptr,
                            const uint16_t* src_ptr,
                            int dst_width,
                            int x32,
                            int dx) {
  int64_t x = (int64_t)(x32);
  int j;
  for (j = 0; j < dst_width - 1; j += 2) {
    int64_t xi = x >> 16;
    int a = src_ptr[xi];
    int b = src_ptr[xi + 1];
    dst_ptr[0] = BLENDER(a, b, x & 0xffff);
    x += dx;
    xi = x >> 16;
    a = src_ptr[xi];
    b = src_ptr[xi + 1];
    dst_ptr[1] = BLENDER(a, b, x & 0xffff);
    x += dx;
    dst_ptr += 2;
  }
  if (dst_width & 1) {
    int64_t xi = x >> 16;
    int a = src_ptr[xi];
    int b = src_ptr[xi + 1];
    dst_ptr[0] = BLENDER(a, b, x & 0xffff);
  }
}
#undef BLENDER


void ScaleAddRow_C(const uint8_t* src_ptr, uint16_t* dst_ptr, int src_width) {
  int x;
  assert(src_width > 0);
  for (x = 0; x < src_width - 1; x += 2) {
    dst_ptr[0] += src_ptr[0];
    dst_ptr[1] += src_ptr[1];
    src_ptr += 2;
    dst_ptr += 2;
  }
  if (src_width & 1) {
    dst_ptr[0] += src_ptr[0];
  }
}

void ScaleAddRow_16_C(const uint16_t* src_ptr,
                      uint32_t* dst_ptr,
                      int src_width) {
  int x;
  assert(src_width > 0);
  for (x = 0; x < src_width - 1; x += 2) {
    dst_ptr[0] += src_ptr[0];
    dst_ptr[1] += src_ptr[1];
    src_ptr += 2;
    dst_ptr += 2;
  }
  if (src_width & 1) {
    dst_ptr[0] += src_ptr[0];
  }
}


// Scale plane vertically with bilinear interpolation.
void ScalePlaneVertical(int src_height,
                        int dst_width,
                        int dst_height,
                        int src_stride,
                        int dst_stride,
                        const uint8_t* src_argb,
                        uint8_t* dst_argb,
                        int x,
                        int y,
                        int dy,
                        int bpp,  // bytes per pixel. 4 for ARGB.
                        enum FilterMode filtering) {
  // TODO(fbarchard): Allow higher bpp.
  int dst_width_bytes = dst_width * bpp;
  void (*InterpolateRow)(uint8_t* dst_argb, const uint8_t* src_argb,
                         ptrdiff_t src_stride, int dst_width,
                         int source_y_fraction) = InterpolateRow_C;
  const int max_y = (src_height > 1) ? ((src_height - 1) << 16) - 1 : 0;
  int j;
  assert(bpp >= 1 && bpp <= 4);
  assert(src_height != 0);
  assert(dst_width > 0);
  assert(dst_height > 0);
  src_argb += (x >> 16) * bpp;

  for (j = 0; j < dst_height; ++j) {
    int yi;
    int yf;
    if (y > max_y) {
      y = max_y;
    }
    yi = y >> 16;
    yf = filtering ? ((y >> 8) & 255) : 0;
    InterpolateRow(dst_argb, src_argb + yi * src_stride, src_stride,
                   dst_width_bytes, yf);
    dst_argb += dst_stride;
    y += dy;
  }
}

void ScalePlaneVertical_16(int src_height,
                           int dst_width,
                           int dst_height,
                           int src_stride,
                           int dst_stride,
                           const uint16_t* src_argb,
                           uint16_t* dst_argb,
                           int x,
                           int y,
                           int dy,
                           int wpp, /* words per pixel. normally 1 */
                           enum FilterMode filtering) {
  // TODO(fbarchard): Allow higher wpp.
  int dst_width_words = dst_width * wpp;
  void (*InterpolateRow)(uint16_t* dst_argb, const uint16_t* src_argb,
                         ptrdiff_t src_stride, int dst_width,
                         int source_y_fraction) = InterpolateRow_16_C;
  const int max_y = (src_height > 1) ? ((src_height - 1) << 16) - 1 : 0;
  int j;
  assert(wpp >= 1 && wpp <= 2);
  assert(src_height != 0);
  assert(dst_width > 0);
  assert(dst_height > 0);
  src_argb += (x >> 16) * wpp;

  for (j = 0; j < dst_height; ++j) {
    int yi;
    int yf;
    if (y > max_y) {
      y = max_y;
    }
    yi = y >> 16;
    yf = filtering ? ((y >> 8) & 255) : 0;
    InterpolateRow(dst_argb, src_argb + yi * src_stride, src_stride,
                   dst_width_words, yf);
    dst_argb += dst_stride;
    y += dy;
  }
}

#define C16TO8(v, scale) clamp255(((v) * (scale)) >> 16)

void Convert16To8Row_C(const uint16_t* src_y,
                       uint8_t* dst_y,
                       int scale,
                       int width) {
  int x;
  assert(scale >= 256);
  assert(scale <= 32768);

  for (x = 0; x < width; ++x) {
    dst_y[x] = STATIC_CAST(uint8_t, C16TO8(src_y[x], scale));
  }
}

// Simplify the filtering based on scale factors.
enum FilterMode ScaleFilterReduce(int src_width,
                                  int src_height,
                                  int dst_width,
                                  int dst_height,
                                  enum FilterMode filtering) {
  if (src_width < 0) {
    src_width = -src_width;
  }
  if (src_height < 0) {
    src_height = -src_height;
  }
  if (filtering == kFilterBox) {
    // If scaling either axis to 0.5 or larger, switch from Box to Bilinear.
    if (dst_width * 2 >= src_width || dst_height * 2 >= src_height) {
      filtering = kFilterBilinear;
    }
  }
  if (filtering == kFilterBilinear) {
    if (src_height == 1) {
      filtering = kFilterLinear;
    }
    // TODO(fbarchard): Detect any odd scale factor and reduce to Linear.
    if (dst_height == src_height || dst_height * 3 == src_height) {
      filtering = kFilterLinear;
    }
    // TODO(fbarchard): Remove 1 pixel wide filter restriction, which is to
    // avoid reading 2 pixels horizontally that causes memory exception.
    if (src_width == 1) {
      filtering = kFilterNone;
    }
  }
  if (filtering == kFilterLinear) {
    if (src_width == 1) {
      filtering = kFilterNone;
    }
    // TODO(fbarchard): Detect any odd scale factor and reduce to None.
    if (dst_width == src_width || dst_width * 3 == src_width) {
      filtering = kFilterNone;
    }
  }
  return filtering;
}

#define CENTERSTART(dx, s) (dx < 0) ? -((-dx >> 1) + s) : ((dx >> 1) + s)

// Compute slope values for stepping.
void ScaleSlope(int src_width,
                int src_height,
                int dst_width,
                int dst_height,
                enum FilterMode filtering,
                int* x,
                int* y,
                int* dx,
                int* dy) {
  assert(x != NULL);
  assert(y != NULL);
  assert(dx != NULL);
  assert(dy != NULL);
  assert(src_width != 0);
  assert(src_height != 0);
  assert(dst_width > 0);
  assert(dst_height > 0);
  // Check for 1 pixel and avoid FixedDiv overflow.
  if (dst_width == 1 && src_width >= 32768) {
    dst_width = src_width;
  }
  if (dst_height == 1 && src_height >= 32768) {
    dst_height = src_height;
  }
  if (filtering == kFilterBox) {
    // Scale step for point sampling duplicates all pixels equally.
    *dx = FixedDiv(Abs(src_width), dst_width);
    *dy = FixedDiv(src_height, dst_height);
    *x = 0;
    *y = 0;
  } else if (filtering == kFilterBilinear) {
    // Scale step for bilinear sampling renders last pixel once for upsample.
    if (dst_width <= Abs(src_width)) {
      *dx = FixedDiv(Abs(src_width), dst_width);
      *x = CENTERSTART(*dx, -32768);  // Subtract 0.5 (32768) to center filter.
    } else if (src_width > 1 && dst_width > 1) {
      *dx = FixedDiv1(Abs(src_width), dst_width);
      *x = 0;
    }
    if (dst_height <= src_height) {
      *dy = FixedDiv(src_height, dst_height);
      *y = CENTERSTART(*dy, -32768);  // Subtract 0.5 (32768) to center filter.
    } else if (src_height > 1 && dst_height > 1) {
      *dy = FixedDiv1(src_height, dst_height);
      *y = 0;
    }
  } else if (filtering == kFilterLinear) {
    // Scale step for bilinear sampling renders last pixel once for upsample.
    if (dst_width <= Abs(src_width)) {
      *dx = FixedDiv(Abs(src_width), dst_width);
      *x = CENTERSTART(*dx, -32768);  // Subtract 0.5 (32768) to center filter.
    } else if (src_width > 1 && dst_width > 1) {
      *dx = FixedDiv1(Abs(src_width), dst_width);
      *x = 0;
    }
    *dy = FixedDiv(src_height, dst_height);
    *y = *dy >> 1;
  } else {
    // Scale step for point sampling duplicates all pixels equally.
    *dx = FixedDiv(Abs(src_width), dst_width);
    *dy = FixedDiv(src_height, dst_height);
    *x = CENTERSTART(*dx, 0);
    *y = CENTERSTART(*dy, 0);
  }
  // Negative src_width means horizontally mirror.
  if (src_width < 0) {
    *x += (dst_width - 1) * *dx;
    *dx = -*dx;
    // src_width = -src_width;   // Caller must do this.
  }
}
#undef CENTERSTART

void CopyRow_C(const uint8_t* src, uint8_t* dst, int count) {
  memcpy(dst, src, count);
}

// Copy a plane of data
void CopyPlane(const uint8_t* src_y,
               int src_stride_y,
               uint8_t* dst_y,
               int dst_stride_y,
               int width,
               int height) {
  int y;
  void (*CopyRow)(const uint8_t* src, uint8_t* dst, int width) = CopyRow_C;
  if (width <= 0 || height == 0) {
    return;
  }
  // Negative height means invert the image.
  if (height < 0) {
    height = -height;
    dst_y = dst_y + (height - 1) * dst_stride_y;
    dst_stride_y = -dst_stride_y;
  }
  // Coalesce rows.
  if (src_stride_y == width && dst_stride_y == width) {
    width *= height;
    height = 1;
    src_stride_y = dst_stride_y = 0;
  }
  // Nothing to do.
  if (src_y == dst_y && src_stride_y == dst_stride_y) {
    return;
  }

  // Copy plane
  for (y = 0; y < height; ++y) {
    CopyRow(src_y, dst_y, width);
    src_y += src_stride_y;
    dst_y += dst_stride_y;
  }
}

void CopyPlane_16(const uint16_t* src_y,
                  int src_stride_y,
                  uint16_t* dst_y,
                  int dst_stride_y,
                  int width,
                  int height) {
  CopyPlane((const uint8_t*)src_y, src_stride_y * 2, (uint8_t*)dst_y,
            dst_stride_y * 2, width * 2, height);
}

