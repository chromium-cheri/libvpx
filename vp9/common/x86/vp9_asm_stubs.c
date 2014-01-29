/*
 *  Copyright (c) 2014 The WebM project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include <assert.h>

#include "./vpx_config.h"
#include "./vp9_rtcd.h"
#include "vpx_ports/mem.h"

typedef void filter8_1dfunction (
  const unsigned char *src_ptr,
  const unsigned int src_pitch,
  unsigned char *output_ptr,
  unsigned int out_pitch,
  unsigned int output_height,
  const short *filter
);

#define FUN_CONV_1D(name, step_q4, filter, dir, src_start, avg, opt) \
void vp9_convolve8_##name##_##opt(const uint8_t *src, ptrdiff_t src_stride, \
                                  uint8_t *dst, ptrdiff_t dst_stride, \
                                  const int16_t *filter_x, int x_step_q4, \
                                  const int16_t *filter_y, int y_step_q4, \
                                  int w, int h) { \
  if (step_q4 == 16 && filter[3] != 128) { \
    while (w >= 16) { \
      vp9_filter_block1d16_##dir##8_##avg##opt(src_start, src_stride, \
                                               dst, dst_stride, \
                                               h, filter); \
      src += 16; \
      dst += 16; \
      w -= 16; \
    } \
    while (w >= 8) { \
      vp9_filter_block1d8_##dir##8_##avg##opt(src_start, src_stride, \
                                              dst, dst_stride, \
                                              h, filter); \
      src += 8; \
      dst += 8; \
      w -= 8; \
    } \
    while (w >= 4) { \
      vp9_filter_block1d4_##dir##8_##avg##opt(src_start, src_stride, \
                                              dst, dst_stride,\
                                              h, filter);\
      src += 4; \
      dst += 4; \
      w -= 4; \
    } \
  } \
  if (w) { \
    vp9_convolve8_##name##_c(src, src_stride, dst, dst_stride, \
                             filter_x, x_step_q4, filter_y, y_step_q4, \
                             w, h); \
  } \
}

#define FUN_CONV_2D(avg, opt) \
void vp9_convolve8_##avg##opt(const uint8_t *src, ptrdiff_t src_stride, \
                              uint8_t *dst, ptrdiff_t dst_stride, \
                              const int16_t *filter_x, int x_step_q4, \
                              const int16_t *filter_y, int y_step_q4, \
                              int w, int h) { \
  DECLARE_ALIGNED_ARRAY(16, unsigned char, fdata2, 64 * 71); \
  \
  assert(w <= 64); \
  assert(h <= 64); \
  if (x_step_q4 == 16 && y_step_q4 == 16) { \
    vp9_convolve8_horiz_##opt(src - 3 * src_stride, src_stride, fdata2, 64, \
                              filter_x, x_step_q4, filter_y, y_step_q4, \
                              w, h + 7); \
    vp9_convolve8_##avg##vert_##opt(fdata2 + 3 * 64, 64, dst, dst_stride, \
                                    filter_x, x_step_q4, filter_y, y_step_q4, \
                                    w, h); \
  } else { \
    vp9_convolve8_##avg##c(src, src_stride, dst, dst_stride, \
                           filter_x, x_step_q4, filter_y, y_step_q4, w, h); \
  } \
}

#if HAVE_SSSE3
filter8_1dfunction vp9_filter_block1d16_v8_ssse3;
filter8_1dfunction vp9_filter_block1d16_h8_ssse3;
filter8_1dfunction vp9_filter_block1d8_v8_ssse3;
filter8_1dfunction vp9_filter_block1d8_h8_ssse3;
filter8_1dfunction vp9_filter_block1d4_v8_ssse3;
filter8_1dfunction vp9_filter_block1d4_h8_ssse3;
filter8_1dfunction vp9_filter_block1d16_v8_avg_ssse3;
filter8_1dfunction vp9_filter_block1d16_h8_avg_ssse3;
filter8_1dfunction vp9_filter_block1d8_v8_avg_ssse3;
filter8_1dfunction vp9_filter_block1d8_h8_avg_ssse3;
filter8_1dfunction vp9_filter_block1d4_v8_avg_ssse3;
filter8_1dfunction vp9_filter_block1d4_h8_avg_ssse3;

// void vp9_convolve8_horiz_ssse3(const uint8_t *src, ptrdiff_t src_stride,
//                                uint8_t *dst, ptrdiff_t dst_stride,
//                                const int16_t *filter_x, int x_step_q4,
//                                const int16_t *filter_y, int y_step_q4,
//                                int w, int h);
// void vp9_convolve8_vert_ssse3(const uint8_t *src, ptrdiff_t src_stride,
//                               uint8_t *dst, ptrdiff_t dst_stride,
//                               const int16_t *filter_x, int x_step_q4,
//                               const int16_t *filter_y, int y_step_q4,
//                               int w, int h);
// void vp9_convolve8_avg_horiz_ssse3(const uint8_t *src, ptrdiff_t src_stride,
//                                    uint8_t *dst, ptrdiff_t dst_stride,
//                                    const int16_t *filter_x, int x_step_q4,
//                                    const int16_t *filter_y, int y_step_q4,
//                                    int w, int h);
// void vp9_convolve8_avg_vert_ssse3(const uint8_t *src, ptrdiff_t src_stride,
//                                   uint8_t *dst, ptrdiff_t dst_stride,
//                                   const int16_t *filter_x, int x_step_q4,
//                                   const int16_t *filter_y, int y_step_q4,
//                                   int w, int h);
FUN_CONV_1D(horiz, x_step_q4, filter_x, h, src, , ssse3);
FUN_CONV_1D(vert, y_step_q4, filter_y, v, src - src_stride * 3, , ssse3);
FUN_CONV_1D(avg_horiz, x_step_q4, filter_x, h, src, avg_, ssse3);
FUN_CONV_1D(avg_vert, y_step_q4, filter_y, v, src - src_stride * 3, avg_,
            ssse3);

// void vp9_convolve8_ssse3(const uint8_t *src, ptrdiff_t src_stride,
//                          uint8_t *dst, ptrdiff_t dst_stride,
//                          const int16_t *filter_x, int x_step_q4,
//                          const int16_t *filter_y, int y_step_q4,
//                          int w, int h);
// void vp9_convolve8_avg_ssse3(const uint8_t *src, ptrdiff_t src_stride,
//                              uint8_t *dst, ptrdiff_t dst_stride,
//                              const int16_t *filter_x, int x_step_q4,
//                              const int16_t *filter_y, int y_step_q4,
//                              int w, int h);
FUN_CONV_2D( , ssse3)
FUN_CONV_2D(avg_ , ssse3)
#endif

#if HAVE_SSE2
filter8_1dfunction vp9_filter_block1d16_v8_sse2;
filter8_1dfunction vp9_filter_block1d16_h8_sse2;
filter8_1dfunction vp9_filter_block1d8_v8_sse2;
filter8_1dfunction vp9_filter_block1d8_h8_sse2;
filter8_1dfunction vp9_filter_block1d4_v8_sse2;
filter8_1dfunction vp9_filter_block1d4_h8_sse2;
filter8_1dfunction vp9_filter_block1d16_v8_avg_sse2;
filter8_1dfunction vp9_filter_block1d16_h8_avg_sse2;
filter8_1dfunction vp9_filter_block1d8_v8_avg_sse2;
filter8_1dfunction vp9_filter_block1d8_h8_avg_sse2;
filter8_1dfunction vp9_filter_block1d4_v8_avg_sse2;
filter8_1dfunction vp9_filter_block1d4_h8_avg_sse2;

// void vp9_convolve8_horiz_sse2(const uint8_t *src, ptrdiff_t src_stride,
//                               uint8_t *dst, ptrdiff_t dst_stride,
//                               const int16_t *filter_x, int x_step_q4,
//                               const int16_t *filter_y, int y_step_q4,
//                               int w, int h);
// void vp9_convolve8_vert_sse2(const uint8_t *src, ptrdiff_t src_stride,
//                              uint8_t *dst, ptrdiff_t dst_stride,
//                              const int16_t *filter_x, int x_step_q4,
//                              const int16_t *filter_y, int y_step_q4,
//                              int w, int h);
// void vp9_convolve8_avg_horiz_sse2(const uint8_t *src, ptrdiff_t src_stride,
//                                   uint8_t *dst, ptrdiff_t dst_stride,
//                                   const int16_t *filter_x, int x_step_q4,
//                                   const int16_t *filter_y, int y_step_q4,
//                                   int w, int h);
// void vp9_convolve8_avg_vert_sse2(const uint8_t *src, ptrdiff_t src_stride,
//                                  uint8_t *dst, ptrdiff_t dst_stride,
//                                  const int16_t *filter_x, int x_step_q4,
//                                  const int16_t *filter_y, int y_step_q4,
//                                  int w, int h);
FUN_CONV_1D(horiz, x_step_q4, filter_x, h, src, , sse2);
FUN_CONV_1D(vert, y_step_q4, filter_y, v, src - src_stride * 3, , sse2);
FUN_CONV_1D(avg_horiz, x_step_q4, filter_x, h, src, avg_, sse2);
FUN_CONV_1D(avg_vert, y_step_q4, filter_y, v, src - src_stride * 3, avg_, sse2);

// void vp9_convolve8_sse2(const uint8_t *src, ptrdiff_t src_stride,
//                         uint8_t *dst, ptrdiff_t dst_stride,
//                         const int16_t *filter_x, int x_step_q4,
//                         const int16_t *filter_y, int y_step_q4,
//                         int w, int h);
// void vp9_convolve8_avg_sse2(const uint8_t *src, ptrdiff_t src_stride,
//                             uint8_t *dst, ptrdiff_t dst_stride,
//                             const int16_t *filter_x, int x_step_q4,
//                             const int16_t *filter_y, int y_step_q4,
//                             int w, int h);
FUN_CONV_2D( , sse2)
FUN_CONV_2D(avg_ , sse2)
#endif
