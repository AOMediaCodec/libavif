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

#ifndef LIBYUV_MINI_ROW_H
#define LIBYUV_MINI_ROW_H

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

#endif // LIBYUV_MINI_ROW_H
