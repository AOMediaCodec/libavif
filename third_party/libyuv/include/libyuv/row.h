/*
 *  Copyright 2011 The LibYuv Project Authors. All rights reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS. All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#ifndef INCLUDE_LIBYUV_ROW_H_
#define INCLUDE_LIBYUV_ROW_H_

#include <stdint.h>
#include <stdlib.h>

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

#endif // INCLUDE_LIBYUV_ROW_H_
