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

#include "avif_scale.h"

#include <stdint.h>
#include <stdlib.h>

// Divide num by div and return as 16.16 fixed point result.
static int FixedDiv_C(int num, int div) {
  return (int)(((int64_t)(num) << 16) / div);
}

// Divide num - 1 by div - 1 and return as 16.16 fixed point result.
static int FixedDiv1_C(int num, int div) {
  return (int)((((int64_t)(num) << 16) - 0x00010001) / (div - 1));
}

#define FixedDiv FixedDiv_C
#define FixedDiv1 FixedDiv1_C

// Compute slope values for stepping.
void ScaleSlope(int src_width,
                int src_height,
                int dst_width,
                int dst_height,
                enum FilterMode filtering,
                int* x,
                int* y,
                int* dx,
                int* dy);

void ScaleRowDown2_C(const uint8_t* src_ptr,
                     ptrdiff_t src_stride,
                     uint8_t* dst,
                     int dst_width);
void ScaleRowDown2_16_C(const uint16_t* src_ptr,
                        ptrdiff_t src_stride,
                        uint16_t* dst,
                        int dst_width);
void ScaleRowDown2_16To8_C(const uint16_t* src_ptr,
                           ptrdiff_t src_stride,
                           uint8_t* dst,
                           int dst_width,
                           int scale);
void ScaleRowDown2_16To8_Odd_C(const uint16_t* src_ptr,
                               ptrdiff_t src_stride,
                               uint8_t* dst,
                               int dst_width,
                               int scale);
void ScaleRowDown2Linear_C(const uint8_t* src_ptr,
                           ptrdiff_t src_stride,
                           uint8_t* dst,
                           int dst_width);
void ScaleRowDown2Linear_16_C(const uint16_t* src_ptr,
                              ptrdiff_t src_stride,
                              uint16_t* dst,
                              int dst_width);
void ScaleRowDown2Linear_16To8_C(const uint16_t* src_ptr,
                                 ptrdiff_t src_stride,
                                 uint8_t* dst,
                                 int dst_width,
                                 int scale);
void ScaleRowDown2Linear_16To8_Odd_C(const uint16_t* src_ptr,
                                     ptrdiff_t src_stride,
                                     uint8_t* dst,
                                     int dst_width,
                                     int scale);
void ScaleRowDown2Box_C(const uint8_t* src_ptr,
                        ptrdiff_t src_stride,
                        uint8_t* dst,
                        int dst_width);
void ScaleRowDown2Box_Odd_C(const uint8_t* src_ptr,
                            ptrdiff_t src_stride,
                            uint8_t* dst,
                            int dst_width);
void ScaleRowDown2Box_16_C(const uint16_t* src_ptr,
                           ptrdiff_t src_stride,
                           uint16_t* dst,
                           int dst_width);
void ScaleRowDown2Box_16To8_C(const uint16_t* src_ptr,
                              ptrdiff_t src_stride,
                              uint8_t* dst,
                              int dst_width,
                              int scale);
void ScaleRowDown2Box_16To8_Odd_C(const uint16_t* src_ptr,
                                  ptrdiff_t src_stride,
                                  uint8_t* dst,
                                  int dst_width,
                                  int scale);
void ScaleRowDown4_C(const uint8_t* src_ptr,
                     ptrdiff_t src_stride,
                     uint8_t* dst,
                     int dst_width);
void ScaleRowDown4_16_C(const uint16_t* src_ptr,
                        ptrdiff_t src_stride,
                        uint16_t* dst,
                        int dst_width);
void ScaleRowDown4Box_C(const uint8_t* src_ptr,
                        ptrdiff_t src_stride,
                        uint8_t* dst,
                        int dst_width);
void ScaleRowDown4Box_16_C(const uint16_t* src_ptr,
                           ptrdiff_t src_stride,
                           uint16_t* dst,
                           int dst_width);
void ScaleRowDown34_C(const uint8_t* src_ptr,
                      ptrdiff_t src_stride,
                      uint8_t* dst,
                      int dst_width);
void ScaleRowDown34_16_C(const uint16_t* src_ptr,
                         ptrdiff_t src_stride,
                         uint16_t* dst,
                         int dst_width);
void ScaleRowDown34_0_Box_C(const uint8_t* src_ptr,
                            ptrdiff_t src_stride,
                            uint8_t* d,
                            int dst_width);
void ScaleRowDown34_0_Box_16_C(const uint16_t* src_ptr,
                               ptrdiff_t src_stride,
                               uint16_t* d,
                               int dst_width);
void ScaleRowDown34_1_Box_C(const uint8_t* src_ptr,
                            ptrdiff_t src_stride,
                            uint8_t* d,
                            int dst_width);
void ScaleRowDown34_1_Box_16_C(const uint16_t* src_ptr,
                               ptrdiff_t src_stride,
                               uint16_t* d,
                               int dst_width);

void ScaleRowUp2_Linear_C(const uint8_t* src_ptr,
                          uint8_t* dst_ptr,
                          int dst_width);
void ScaleRowUp2_Bilinear_C(const uint8_t* src_ptr,
                            ptrdiff_t src_stride,
                            uint8_t* dst_ptr,
                            ptrdiff_t dst_stride,
                            int dst_width);
void ScaleRowUp2_Linear_16_C(const uint16_t* src_ptr,
                             uint16_t* dst_ptr,
                             int dst_width);
void ScaleRowUp2_Bilinear_16_C(const uint16_t* src_ptr,
                               ptrdiff_t src_stride,
                               uint16_t* dst_ptr,
                               ptrdiff_t dst_stride,
                               int dst_width);
void ScaleRowUp2_Linear_Any_C(const uint8_t* src_ptr,
                              uint8_t* dst_ptr,
                              int dst_width);
void ScaleRowUp2_Bilinear_Any_C(const uint8_t* src_ptr,
                                ptrdiff_t src_stride,
                                uint8_t* dst_ptr,
                                ptrdiff_t dst_stride,
                                int dst_width);
void ScaleRowUp2_Linear_16_Any_C(const uint16_t* src_ptr,
                                 uint16_t* dst_ptr,
                                 int dst_width);
void ScaleRowUp2_Bilinear_16_Any_C(const uint16_t* src_ptr,
                                   ptrdiff_t src_stride,
                                   uint16_t* dst_ptr,
                                   ptrdiff_t dst_stride,
                                   int dst_width);

void ScaleCols_C(uint8_t* dst_ptr,
                 const uint8_t* src_ptr,
                 int dst_width,
                 int x,
                 int dx);
void ScaleCols_16_C(uint16_t* dst_ptr,
                    const uint16_t* src_ptr,
                    int dst_width,
                    int x,
                    int dx);
void ScaleColsUp2_C(uint8_t* dst_ptr,
                    const uint8_t* src_ptr,
                    int dst_width,
                    int,
                    int);
void ScaleColsUp2_16_C(uint16_t* dst_ptr,
                       const uint16_t* src_ptr,
                       int dst_width,
                       int,
                       int);
void ScaleFilterCols_C(uint8_t* dst_ptr,
                       const uint8_t* src_ptr,
                       int dst_width,
                       int x,
                       int dx);
void ScaleFilterCols_16_C(uint16_t* dst_ptr,
                          const uint16_t* src_ptr,
                          int dst_width,
                          int x,
                          int dx);
void ScaleFilterCols64_C(uint8_t* dst_ptr,
                         const uint8_t* src_ptr,
                         int dst_width,
                         int x32,
                         int dx);
void ScaleFilterCols64_16_C(uint16_t* dst_ptr,
                            const uint16_t* src_ptr,
                            int dst_width,
                            int x32,
                            int dx);
void ScaleRowDown38_C(const uint8_t* src_ptr,
                      ptrdiff_t src_stride,
                      uint8_t* dst,
                      int dst_width);
void ScaleRowDown38_16_C(const uint16_t* src_ptr,
                         ptrdiff_t src_stride,
                         uint16_t* dst,
                         int dst_width);
void ScaleRowDown38_3_Box_C(const uint8_t* src_ptr,
                            ptrdiff_t src_stride,
                            uint8_t* dst_ptr,
                            int dst_width);
void ScaleRowDown38_3_Box_16_C(const uint16_t* src_ptr,
                               ptrdiff_t src_stride,
                               uint16_t* dst_ptr,
                               int dst_width);
void ScaleRowDown38_2_Box_C(const uint8_t* src_ptr,
                            ptrdiff_t src_stride,
                            uint8_t* dst_ptr,
                            int dst_width);
void ScaleRowDown38_2_Box_16_C(const uint16_t* src_ptr,
                               ptrdiff_t src_stride,
                               uint16_t* dst_ptr,
                               int dst_width);
void ScaleAddRow_C(const uint8_t* src_ptr, uint16_t* dst_ptr, int src_width);
void ScaleAddRow_16_C(const uint16_t* src_ptr,
                      uint32_t* dst_ptr,
                      int src_width);

void InterpolateRow_C(uint8_t* dst_ptr,
                      const uint8_t* src_ptr,
                      ptrdiff_t src_stride,
                      int width,
                      int source_y_fraction);

void InterpolateRow_16_C(uint16_t* dst_ptr,
                         const uint16_t* src_ptr,
                         ptrdiff_t src_stride,
                         int width,
                         int source_y_fraction);

enum FilterMode ScaleFilterReduce(int src_width,
                                  int src_height,
                                  int dst_width,
                                  int dst_height,
                                  enum FilterMode filtering);

#define align_buffer_64(var, size)                                         \
  void* var##_mem = malloc((size) + 63);                      /* NOLINT */ \
  uint8_t* var = (uint8_t*)(((intptr_t)var##_mem + 63) & ~63) /* NOLINT */

#define free_aligned_buffer_64(var) \
  free(var##_mem);                  \
  var = NULL

#define align_buffer_64_16(var, size)                                        \
  void* var##_mem = malloc((size)*2 + 63);                      /* NOLINT */ \
  uint16_t* var = (uint16_t*)(((intptr_t)var##_mem + 63) & ~63) /* NOLINT */

#define free_aligned_buffer_64_16(var) \
  free(var##_mem);                     \
  var = NULL

void CopyPlane(const uint8_t* src_y,
               int src_stride_y,
               uint8_t* dst_y,
               int dst_stride_y,
               int width,
               int height);

void CopyPlane_16(const uint16_t* src_y,
                  int src_stride_y,
                  uint16_t* dst_y,
                  int dst_stride_y,
                  int width,
                  int height);

// Scale ARGB vertically with bilinear interpolation.
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
                        int bpp,
                        enum FilterMode filtering);

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
                           int wpp,
                           enum FilterMode filtering);
