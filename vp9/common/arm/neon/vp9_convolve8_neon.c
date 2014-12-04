/*
 *  Copyright (c) 2014 The WebM project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include <stddef.h>
#include <arm_neon.h>

#include "vpx_ports/mem.h"

void vp9_convolve8_horiz_c(const uint8_t *src, ptrdiff_t src_stride,
                           uint8_t *dst, ptrdiff_t dst_stride,
                           const int16_t *filter_x, int x_step_q4,
                           const int16_t *filter_y, int y_step_q4,
                           int w, int h);
void vp9_convolve8_vert_c(const uint8_t *src, ptrdiff_t src_stride,
                          uint8_t *dst, ptrdiff_t dst_stride,
                          const int16_t *filter_x, int x_step_q4,
                          const int16_t *filter_y, int y_step_q4,
                          int w, int h);

/*
 * Multiply and accumulate by filter
 */
static inline int32x4_t multiply_by_filter(int16x4_t src0,
                                           int16x4_t src1,
                                           int16x4_t src2,
                                           int16x4_t src3,
                                           int16x4_t src4,
                                           int16x4_t src5,
                                           int16x4_t src6,
                                           int16x4_t src7,
                                           int16x8_t filter) {
  int32x4_t dst;
  int16x4_t f_low, f_high;

  f_low = vget_low_s16(filter);
  f_high = vget_high_s16(filter);

  dst = vmull_lane_s16(src0, f_low, 0);
  dst = vmlal_lane_s16(dst, src1, f_low, 1);
  dst = vmlal_lane_s16(dst, src2, f_low, 2);
  dst = vmlal_lane_s16(dst, src3, f_low, 3);
  dst = vmlal_lane_s16(dst, src4, f_high, 0);
  dst = vmlal_lane_s16(dst, src5, f_high, 1);
  dst = vmlal_lane_s16(dst, src6, f_high, 2);
  dst = vmlal_lane_s16(dst, src7, f_high, 3);

  return dst;
}

/*
 * This function is only valid when:
 * x_step_q4 == 16
 * w%4 == 0
 * h%4 == 0
 * taps == 8
 * VP9_FILTER_WEIGHT == 128
 * VP9_FILTER_SHIFT == 7
 */
void vp9_convolve8_horiz_neon(const uint8_t *src, ptrdiff_t src_stride,
                              uint8_t *dst, ptrdiff_t dst_stride,
                              const int16_t *filter_x, int x_step_q4,
                              const int16_t *filter_y, int y_step_q4,
                              int w, int h) {
  const uint8_t *psrc = NULL;
  uint8_t *pdst = NULL;
  int i, j;

  // filter vector
  int16x8_t f_q;

  // intermediate vectors
  uint8x8_t v_u8x8_0, v_u8x8_1, v_u8x8_2, v_u8x8_3;
  uint32x2_t v_u32x2_0, v_u32x2_1, v_u32x2_2, v_u32x2_3;
  uint8x16_t v_u8x16_0, v_u8x16_1;

  // intermediate matrices
  uint8x8x2_t m_u8x8x2_0, m_u8x8x2_1;
  uint16x4x2_t m_u16x4x2_0, m_u16x4x2_1;
  uint16x8x2_t m_u16x8x2;
  uint32x4x2_t m_u32x4x2;

  // convolution input vectors
  uint16x8_t ci_q_0, ci_q_1, ci_q_2;
  uint16x8_t ci_q_3, ci_q_4, ci_q_5;
  int16x4_t ci_d_0, ci_d_1, ci_d_2, ci_d_3;
  int16x4_t ci_d_4, ci_d_6, ci_d_7, ci_d_8;
  int16x4_t ci_d_9, ci_d_10, ci_d_11;

  // convolution output vectors
  uint16x4_t co_u16x4_0, co_u16x4_1, co_u16x4_2, co_u16x4_3;
  uint8x8_t co_u8x8_0, co_u8x8_1;
  uint32x2_t co_u32x2_0, co_u32x2_1;
  int32x4_t co_s32x4_0, co_s32x4_1, co_s32x4_2, co_s32x4_3;
  uint16x4x2_t co_u16x4x2;
  uint32x2x2_t co_u32x2x2;
  uint8x8x2_t co_u8x8x2;

  if (x_step_q4 != 16) {
    vp9_convolve8_horiz_c(src, src_stride,
                          dst, dst_stride,
                          filter_x, x_step_q4,
                          filter_y, y_step_q4,
                          w, h);
    return;
  }

  // adjust for taps
  src -= 3;

  // load filter vector from memory
  f_q = vld1q_s16(filter_x);

  for (i = h; i > 0; i -= 4) {
    /*
     * vertical direction loop
     *
     * v_u8x8_0: 00 01 02 03 04 05 06 07
     * v_u8x8_1: 10 11 12 13 14 15 16 17
     * v_u8x8_2: 20 21 22 23 24 25 26 27
     * v_u8x8_3: 30 31 32 33 34 35 36 37
     */
    v_u8x8_0 = vld1_u8(src);
    v_u8x8_1 = vld1_u8(src + src_stride);
    v_u8x8_2 = vld1_u8(src + src_stride * 2);
    v_u8x8_3 = vld1_u8(src + src_stride * 3);

    /*
     * mirror and rotate matrix
     *
     * before:
     * v_u8x8_0: 00 01 02 03 04 05 06 07
     * v_u8x8_1: 10 11 12 13 14 15 16 17
     * v_u8x8_2: 20 21 22 23 24 25 26 27
     * v_u8x8_3: 30 31 32 33 34 35 36 37
     *
     * after:
     * v_u8x8_0: 33 23 13 03 37 27 17 07
     * v_u8x8_1: 32 22 12 02 36 26 16 06
     * v_u8x8_2: 31 21 11 01 35 25 15 05
     * v_u8x8_3: 30 20 10 00 34 24 14 04
     */
    m_u16x8x2 = vtrnq_u16(vreinterpretq_u16_u8(vcombine_u8(v_u8x8_0,
                                                           v_u8x8_1)),
                          vreinterpretq_u16_u8(vcombine_u8(v_u8x8_2,
                                                           v_u8x8_3)));

    v_u8x8_0 = vreinterpret_u8_u16(vget_low_u16(m_u16x8x2.val[0]));
    v_u8x8_1 = vreinterpret_u8_u16(vget_high_u16(m_u16x8x2.val[0]));
    v_u8x8_2 = vreinterpret_u8_u16(vget_low_u16(m_u16x8x2.val[1]));
    v_u8x8_3 = vreinterpret_u8_u16(vget_high_u16(m_u16x8x2.val[1]));

    m_u8x8x2_0 = vtrn_u8(v_u8x8_0, v_u8x8_1);
    m_u8x8x2_1 = vtrn_u8(v_u8x8_2, v_u8x8_3);

    // preload data and instruction
    __builtin_prefetch(src + src_stride * 4);

    ci_q_0 = vmovl_u8(v_u8x8_0);
    ci_q_1 = vmovl_u8(v_u8x8_1);
    ci_q_2 = vmovl_u8(v_u8x8_2);
    ci_q_3 = vmovl_u8(v_u8x8_3);

    /*
     * exchange the contents of two vectors, i.e.,
     * ci_q_0's high u16x4 and ci_q_1's low u16x4.
     *
     * copy the contents of vector of ci_q_2's high u16x4 to
     * vector of ci_q_3's high u16x4.
     *
     * before:
     * ci_q_0: 33 23 13 03 37 27 17 07
     * ci_q_1: 32 22 12 02 36 26 16 06
     * ci_q_2: 31 21 11 01 35 25 15 05
     * ci_q_3: 30 20 10 00 34 24 14 04
     *
     * after:
     * ci_q_0: 33 23 13 03 32 22 12 02
     * ci_q_1: 37 27 17 07 36 26 16 06
     * ci_q_2: 31 21 11 01 35 25 15 05
     * ci_q_3: 30 20 10 00 35 25 15 05
     */
    ci_q_0 = vcombine_u16(vget_low_u16(ci_q_0),
                          vget_low_u16(ci_q_1));
    ci_q_1 = vcombine_u16(vget_high_u16(ci_q_0),
                          vget_high_u16(ci_q_1));
    ci_q_3 = vcombine_u16(vget_low_u16(ci_q_3),
                          vget_high_u16(ci_q_2));

    psrc = src + 7;
    pdst = dst;

    for (j = w; j > 0; j -= 4) {
      /*
       * horizontal direction loop
       *
       * v_u32x2_0: 010 011 012 013 014 015 016 017
       * v_u32x2_1: 110 111 112 113 114 115 116 117
       * v_u32x2_2: 210 211 212 213 214 215 216 217
       * v_u32x2_3: 310 311 312 313 314 315 316 317
       */
      v_u32x2_0 = vld1_dup_u32((const uint32_t *)psrc);
      v_u32x2_1 = vld1_dup_u32((const uint32_t *)
                               ((const uint8_t *)psrc + src_stride));
      v_u32x2_2 = vld1_dup_u32((const uint32_t *)
                               ((const uint8_t *)psrc + src_stride * 2));
      v_u32x2_3 = vld1_dup_u32((const uint32_t *)
                               ((const uint8_t *)psrc + src_stride * 3));

      // preload data and instruction
      __builtin_prefetch(psrc + 64);

      /*
       * mirror and rotate matrix
       *
       * before:
       * v_u32x2_0: 010 011 012 013 014 015 016 017
       * v_u32x2_1: 110 111 112 113 114 115 116 117
       * v_u32x2_2: 210 211 212 213 214 215 216 217
       * v_u32x2_3: 310 311 312 313 314 315 316 317
       *
       * after:
       * v_u32x2_0: 313 213 113 013 317 217 117 017
       * v_u32x2_1: 312 212 112 012 316 216 116 016
       * v_u32x2_2: 311 211 111 011 315 215 115 015
       * v_u32x2_3: 310 210 110 010 314 214 114 014
       */
      m_u16x4x2_0 = vtrn_u16(vreinterpret_u16_u32(v_u32x2_0),
                             vreinterpret_u16_u32(v_u32x2_2));
      m_u16x4x2_1 = vtrn_u16(vreinterpret_u16_u32(v_u32x2_1),
                             vreinterpret_u16_u32(v_u32x2_3));
      m_u8x8x2_0 = vtrn_u8(vreinterpret_u8_u16(m_u16x4x2_0.val[0]),
                           vreinterpret_u8_u16(m_u16x4x2_1.val[0]));
      m_u8x8x2_1 = vtrn_u8(vreinterpret_u8_u16(m_u16x4x2_0.val[1]),
                           vreinterpret_u8_u16(m_u16x4x2_1.val[1]));

      // preload data and instruction
      __builtin_prefetch(psrc + 64 + src_stride);

      /*
       * mirror and rotate matrix
       *
       * before:
       * v_u32x2_0: 313 213 113 013 317 217 117 017
       * v_u32x2_1: 312 212 112 012 316 216 116 016
       * v_u32x2_2: 311 211 111 011 315 215 115 015
       * v_u32x2_3: 310 210 110 010 314 214 114 014
       *
       * after:
       * v_u32x2_0: 314 214 114 014 317 217 117 017
       * v_u32x2_1: 315 215 115 015 316 216 116 016
       * v_u32x2_2: 311 211 111 011 312 212 112 012
       * v_u32x2_3: 310 210 110 010 313 213 113 013
       */
      v_u8x16_0 = vcombine_u8(m_u8x8x2_0.val[0],
                              m_u8x8x2_0.val[1]);
      v_u8x16_1 = vcombine_u8(m_u8x8x2_1.val[1],
                              m_u8x8x2_1.val[0]);
      m_u32x4x2 = vtrnq_u32(vreinterpretq_u32_u8(v_u8x16_0),
                            vreinterpretq_u32_u8(v_u8x16_1));

      ci_q_4 =
        vmovl_u8(vreinterpret_u8_u32(vget_low_u32(m_u32x4x2.val[0])));
      ci_q_5 =
        vmovl_u8(vreinterpret_u8_u32(vget_high_u32(m_u32x4x2.val[0])));

      // preload data and instruction
      __builtin_prefetch(psrc + 64 + src_stride * 2);

      // src[] * filter_x
      ci_d_0 = vreinterpret_s16_u16(vget_low_u16(ci_q_0));
      ci_d_1 = vreinterpret_s16_u16(vget_high_u16(ci_q_0));
      ci_d_2 = vreinterpret_s16_u16(vget_low_u16(ci_q_1));
      ci_d_3 = vreinterpret_s16_u16(vget_high_u16(ci_q_1));
      ci_d_4 = vreinterpret_s16_u16(vget_low_u16(ci_q_2));
      ci_d_6 = vreinterpret_s16_u16(vget_low_u16(ci_q_3));
      ci_d_7 = vreinterpret_s16_u16(vget_high_u16(ci_q_3));
      ci_d_8 = vreinterpret_s16_u16(vget_low_u16(ci_q_4));
      ci_d_9 = vreinterpret_s16_u16(vget_high_u16(ci_q_4));
      ci_d_10 = vreinterpret_s16_u16(vget_low_u16(ci_q_5));
      ci_d_11 = vreinterpret_s16_u16(vget_high_u16(ci_q_5));

      co_s32x4_0 = multiply_by_filter(ci_d_0, ci_d_1,
                                      ci_d_4, ci_d_6,
                                      ci_d_2, ci_d_3,
                                      ci_d_7, ci_d_8,
                                      f_q);
      co_s32x4_1 = multiply_by_filter(ci_d_1, ci_d_4,
                                      ci_d_6, ci_d_2,
                                      ci_d_3, ci_d_7,
                                      ci_d_8, ci_d_10,
                                      f_q);
      co_s32x4_2 = multiply_by_filter(ci_d_4, ci_d_6,
                                      ci_d_2, ci_d_3,
                                      ci_d_7, ci_d_8,
                                      ci_d_10, ci_d_11,
                                      f_q);
      co_s32x4_3 = multiply_by_filter(ci_d_6, ci_d_2,
                                      ci_d_3, ci_d_7,
                                      ci_d_8, ci_d_10,
                                      ci_d_11, ci_d_9,
                                      f_q);

      // preload data and instruction
      __builtin_prefetch(psrc + 64 + src_stride * 3 - 4);

      // right shift each element in a quadword vector
      co_u16x4_0 = vqrshrun_n_s32(co_s32x4_0, 7);
      co_u16x4_1 = vqrshrun_n_s32(co_s32x4_1, 7);
      co_u16x4_2 = vqrshrun_n_s32(co_s32x4_2, 7);
      co_u16x4_3 = vqrshrun_n_s32(co_s32x4_3, 7);

      // saturate
      co_u8x8_0 = vqmovn_u16(vcombine_u16(co_u16x4_0, co_u16x4_1));
      co_u8x8_1 = vqmovn_u16(vcombine_u16(co_u16x4_2, co_u16x4_3));

      // transpose
      co_u16x4x2 = vtrn_u16(vreinterpret_u16_u8(co_u8x8_0),
                           vreinterpret_u16_u8(co_u8x8_1));
      co_u32x2x2 = vtrn_u32(vreinterpret_u32_u16(co_u16x4x2.val[0]),
                           vreinterpret_u32_u16(co_u16x4x2.val[1]));
      co_u8x8x2 = vtrn_u8(vreinterpret_u8_u32(co_u32x2x2.val[0]),
                         vreinterpret_u8_u32(co_u32x2x2.val[1]));

      co_u32x2_0 = vreinterpret_u32_u8(co_u8x8x2.val[0]);
      co_u32x2_1 = vreinterpret_u32_u8(co_u8x8x2.val[1]);

      /*
       * Clang in iOS will cause issue by adding alignment directives automatically.
       *
       * Therefoer, use '(uint32_t *)((uint8_t *)pdst + dst_stride * 4)',
       * instead of '(uint32_t *)pdst + dst_stride', to avoid this issue.
       */
      vst1_lane_u32((uint32_t *)pdst, co_u32x2_0, 0);
      vst1_lane_u32((uint32_t *)
                    ((uint8_t *)pdst + dst_stride * 4), co_u32x2_1, 0);
      vst1_lane_u32((uint32_t *)
                    ((uint8_t *)pdst + dst_stride * 8), co_u32x2_0, 1);
      vst1_lane_u32((uint32_t *)
                    ((uint8_t *)pdst + dst_stride * 12), co_u32x2_1, 1);

      ci_q_0 = ci_q_1;
      ci_q_2 = vcombine_u16(vget_high_u16(ci_q_3),
                            vget_high_u16(ci_q_2));
      ci_q_3 = ci_q_4;
      ci_q_1 = ci_q_5;

      psrc += 4;
      pdst += 4;
    }

    src += src_stride * 4;
    dst += dst_stride * 4;
  }
}

/*
 * This function is only valid when:
 * x_step_q4 == 16
 * w%4 == 0
 * h%4 == 0
 * taps == 8
 * VP9_FILTER_WEIGHT == 128
 * VP9_FILTER_SHIFT == 7
 */
void vp9_convolve8_vert_neon(const uint8_t *src, ptrdiff_t src_stride,
                             uint8_t *dst, ptrdiff_t dst_stride,
                             const int16_t *filter_x, int x_step_q4,
                             const int16_t *filter_y, int y_step_q4,
                             int w, int h) {
    const uint8_t *psrc = NULL;
    int j;

    // filter vector
    int16x8_t f_q;

    // intermediate vectors
    uint32x2_t v_u32x2_0, v_u32x2_1, v_u32x2_2, v_u32x2_3,
      v_u32x2_4, v_u32x2_5, v_u32x2_6, v_u32x2_7;
    uint16x4_t v_u16x4_0, v_u16x4_1, v_u16x4_2, v_u16x4_3;
    uint16x8_t v_u16x8_0, v_u16x8_1, v_u16x8_2, v_u16x8_3,
      v_u16x8_4, v_u16x8_5, v_u16x8_6, v_u16x8_7;

    // convolution input vectors
    int16x4_t ci_d_0, ci_d_1, ci_d_2, ci_d_3, ci_d_4, ci_d_5,
      ci_d_6, ci_d_7, ci_d_8, ci_d_9, ci_d_10;

    // convolution output vectors
    int32x4_t co_q_0, co_q_1, co_q_2, co_q_3;

    if (y_step_q4 != 16) {
        vp9_convolve8_vert_c(src, src_stride,
                             dst, dst_stride,
                             filter_x, x_step_q4,
                             filter_y, y_step_q4,
                             w, h);
        return;
    }

    // adjust for taps
    src -= src_stride * 3;

    // load filter vector from memory
    f_q = vld1q_s16(filter_y);

    for (; w > 0; w -= 4) {
      /*
       * vertical direction loop
       */
      v_u32x2_0 =
        vld1_lane_u32((const uint32_t *)src, v_u32x2_0, 0);
      v_u32x2_0 =
        vld1_lane_u32((const uint32_t *)((const uint8_t *)src + src_stride),
                      v_u32x2_0, 1);
      v_u32x2_1 =
        vld1_lane_u32((const uint32_t *)((const uint8_t *)src + src_stride * 2),
                      v_u32x2_1, 0);
      v_u32x2_1 =
        vld1_lane_u32((const uint32_t *)((const uint8_t *)src + src_stride * 3),
                      v_u32x2_1, 1);
      v_u32x2_2 =
        vld1_lane_u32((const uint32_t *)((const uint8_t *)src + src_stride * 4),
                      v_u32x2_2, 0);
      v_u32x2_2 =
        vld1_lane_u32((const uint32_t *)((const uint8_t *)src + src_stride * 5),
                      v_u32x2_2, 1);
      v_u32x2_3 =
        vld1_lane_u32((const uint32_t *)((const uint8_t *)src + src_stride * 6),
                      v_u32x2_3, 0);

      v_u16x8_2  = vmovl_u8(vreinterpret_u8_u32(v_u32x2_0));
      v_u16x8_3  = vmovl_u8(vreinterpret_u8_u32(v_u32x2_1));
      v_u16x8_4 = vmovl_u8(vreinterpret_u8_u32(v_u32x2_2));
      v_u16x8_5 = vmovl_u8(vreinterpret_u8_u32(v_u32x2_3));

      ci_d_2 = vreinterpret_s16_u16(vget_low_u16(v_u16x8_3));
      ci_d_3 = vreinterpret_s16_u16(vget_high_u16(v_u16x8_3));
      ci_d_6 = vreinterpret_s16_u16(vget_low_u16(v_u16x8_5));

      psrc = src + src_stride * 7;

      for (j = h; j > 0; j -= 4) {
        /*
         * horizontal direction loop
         */
        v_u32x2_4 = vld1_lane_u32((const uint32_t *)psrc, v_u32x2_4, 0);
        v_u32x2_5 =
          vld1_lane_u32((const uint32_t *)((const uint8_t *)psrc + src_stride),
                        v_u32x2_5, 0);
        v_u32x2_5 =
          vld1_lane_u32((const uint32_t *)
                        ((const uint8_t *)psrc + src_stride * 2), v_u32x2_5, 1);
        v_u32x2_4 =
          vld1_lane_u32((const uint32_t *)
                        ((const uint8_t *)psrc + src_stride * 3), v_u32x2_4, 1);

        v_u16x8_6 = vmovl_u8(vreinterpret_u8_u32(v_u32x2_4));
        v_u16x8_7 = vmovl_u8(vreinterpret_u8_u32(v_u32x2_5));

        ci_d_0 = vreinterpret_s16_u16(vget_low_u16(v_u16x8_2));
        ci_d_1 = vreinterpret_s16_u16(vget_high_u16(v_u16x8_2));
        ci_d_4 = vreinterpret_s16_u16(vget_low_u16(v_u16x8_4));
        ci_d_5 = vreinterpret_s16_u16(vget_high_u16(v_u16x8_4));
        ci_d_7 = vreinterpret_s16_u16(vget_low_u16(v_u16x8_6));
        ci_d_8 = vreinterpret_s16_u16(vget_high_u16(v_u16x8_6));
        ci_d_9 = vreinterpret_s16_u16(vget_low_u16(v_u16x8_7));
        ci_d_10 = vreinterpret_s16_u16(vget_high_u16(v_u16x8_7));

        __builtin_prefetch(dst);
        __builtin_prefetch(dst + dst_stride);

        co_q_0  = multiply_by_filter(ci_d_0, ci_d_1, ci_d_2, ci_d_3,
                                    ci_d_4, ci_d_5, ci_d_6, ci_d_7, f_q);

        __builtin_prefetch(dst + dst_stride * 2);
        __builtin_prefetch(dst + dst_stride * 3);

        co_q_1  = multiply_by_filter(ci_d_1, ci_d_2, ci_d_3, ci_d_4,
                                    ci_d_5, ci_d_6, ci_d_7, ci_d_9, f_q);

        __builtin_prefetch(psrc + src_stride * 4);
        __builtin_prefetch(psrc + src_stride * 5);

        co_q_2 = multiply_by_filter(ci_d_2, ci_d_3, ci_d_4, ci_d_5,
                                    ci_d_6, ci_d_7, ci_d_9, ci_d_10, f_q);

        __builtin_prefetch(psrc + src_stride * 6);
        __builtin_prefetch(psrc + src_stride * 7);

        co_q_3 = multiply_by_filter(ci_d_3, ci_d_4, ci_d_5, ci_d_6,
                                    ci_d_7, ci_d_9, ci_d_10, ci_d_8, f_q);

        v_u16x4_0 = vqrshrun_n_s32(co_q_0, 7);
        v_u16x4_1 = vqrshrun_n_s32(co_q_1, 7);
        v_u16x4_2 = vqrshrun_n_s32(co_q_2, 7);
        v_u16x4_3 = vqrshrun_n_s32(co_q_3, 7);

        v_u16x8_0 = vcombine_u16(v_u16x4_0, v_u16x4_1);
        v_u16x8_1 = vcombine_u16(v_u16x4_2, v_u16x4_3);

        v_u32x2_6 = vreinterpret_u32_u8(vqmovn_u16(v_u16x8_0));
        v_u32x2_7 = vreinterpret_u32_u8(vqmovn_u16(v_u16x8_1));

        vst1_lane_u32((uint32_t *)dst,
                      v_u32x2_6, 0);
        vst1_lane_u32((uint32_t *)((uint8_t *)dst + dst_stride),
                      v_u32x2_6, 1);
        vst1_lane_u32((uint32_t *)((uint8_t *)dst + dst_stride * 2),
                      v_u32x2_7, 0);
        vst1_lane_u32((uint32_t *)((uint8_t *)dst + dst_stride * 3),
                      v_u32x2_7, 1);

        v_u16x8_2 = v_u16x8_4;
        ci_d_2 = ci_d_6;
        ci_d_3 = ci_d_7;
        v_u16x8_4 = v_u16x8_7;
        ci_d_6 = ci_d_8;
      }

      src += 4;
      dst += 4;
    }

    return;
}
