/*
 *  Copyright (c) 2014 The WebM project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include <arm_neon.h>

#include "./vpx_config.h"
#include "./vpx_dsp_rtcd.h"

static INLINE void load_u8_8x8(const uint8_t *s, const ptrdiff_t p,
                               uint8x8_t *const s0, uint8x8_t *const s1,
                               uint8x8_t *const s2, uint8x8_t *const s3,
                               uint8x8_t *const s4, uint8x8_t *const s5,
                               uint8x8_t *const s6, uint8x8_t *const s7) {
  *s0 = vld1_u8(s);
  s += p;
  *s1 = vld1_u8(s);
  s += p;
  *s2 = vld1_u8(s);
  s += p;
  *s3 = vld1_u8(s);
  s += p;
  *s4 = vld1_u8(s);
  s += p;
  *s5 = vld1_u8(s);
  s += p;
  *s6 = vld1_u8(s);
  s += p;
  *s7 = vld1_u8(s);
}

static INLINE uint8x8_t convolve8_8(const int16x8_t s0, const int16x8_t s1,
                                    const int16x8_t s2, const int16x8_t s3,
                                    const int16x8_t s4, const int16x8_t s5,
                                    const int16x8_t s6, const int16x8_t s7,
                                    const int16x8_t filters,
                                    const int16x8_t filter3,
                                    const int16x8_t filter4) {
  const int16x4_t filters_lo = vget_low_s16(filters);
  const int16x4_t filters_hi = vget_high_s16(filters);
  int16x8_t sum;

  sum = vmulq_lane_s16(s0, filters_lo, 0);
  sum = vmlaq_lane_s16(sum, s1, filters_lo, 1);
  sum = vmlaq_lane_s16(sum, s2, filters_lo, 2);
  sum = vmlaq_lane_s16(sum, s5, filters_hi, 1);
  sum = vmlaq_lane_s16(sum, s6, filters_hi, 2);
  sum = vmlaq_lane_s16(sum, s7, filters_hi, 3);
  sum = vqaddq_s16(sum, vmulq_s16(s3, filter3));
  sum = vqaddq_s16(sum, vmulq_s16(s4, filter4));
  return vqrshrun_n_s16(sum, 7);
}
