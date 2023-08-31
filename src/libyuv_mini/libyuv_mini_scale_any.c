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
