/*
 *  Copyright (c) 2017 The WebM project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "./vpx_dsp_rtcd.h"
#include "vpx_dsp/ppc/types_vsx.h"

void vpx_v_predictor_16x16_vsx(uint8_t *dst, ptrdiff_t stride,
                               const uint8_t *above, const uint8_t *left) {
  const uint8x16_t d = vec_vsx_ld(0, above);
  int i;
  (void)left;

  for (i = 0; i < 16; i++, dst += stride) {
    vec_vsx_st(d, 0, dst);
  }
}

void vpx_v_predictor_32x32_vsx(uint8_t *dst, ptrdiff_t stride,
                               const uint8_t *above, const uint8_t *left) {
  const uint8x16_t d0 = vec_vsx_ld(0, above);
  const uint8x16_t d1 = vec_vsx_ld(16, above);
  int i;
  (void)left;

  for (i = 0; i < 32; i++, dst += stride) {
    vec_vsx_st(d0, 0, dst);
    vec_vsx_st(d1, 16, dst);
  }
}

void vpx_h_predictor_16x16_vsx(uint8_t *dst, ptrdiff_t stride,
                               const uint8_t *above, const uint8_t *left) {
  const uint8x16_t d = vec_vsx_ld(0, left);
  const uint8x16_t v0 = vec_splat(d, 0);
  const uint8x16_t v1 = vec_splat(d, 1);
  const uint8x16_t v2 = vec_splat(d, 2);
  const uint8x16_t v3 = vec_splat(d, 3);

  const uint8x16_t v4 = vec_splat(d, 4);
  const uint8x16_t v5 = vec_splat(d, 5);
  const uint8x16_t v6 = vec_splat(d, 6);
  const uint8x16_t v7 = vec_splat(d, 7);

  const uint8x16_t v8 = vec_splat(d, 8);
  const uint8x16_t v9 = vec_splat(d, 9);
  const uint8x16_t v10 = vec_splat(d, 10);
  const uint8x16_t v11 = vec_splat(d, 11);

  const uint8x16_t v12 = vec_splat(d, 12);
  const uint8x16_t v13 = vec_splat(d, 13);
  const uint8x16_t v14 = vec_splat(d, 14);
  const uint8x16_t v15 = vec_splat(d, 15);

  (void)above;

  vec_vsx_st(v0, 0, dst);
  dst += stride;
  vec_vsx_st(v1, 0, dst);
  dst += stride;
  vec_vsx_st(v2, 0, dst);
  dst += stride;
  vec_vsx_st(v3, 0, dst);
  dst += stride;
  vec_vsx_st(v4, 0, dst);
  dst += stride;
  vec_vsx_st(v5, 0, dst);
  dst += stride;
  vec_vsx_st(v6, 0, dst);
  dst += stride;
  vec_vsx_st(v7, 0, dst);
  dst += stride;
  vec_vsx_st(v8, 0, dst);
  dst += stride;
  vec_vsx_st(v9, 0, dst);
  dst += stride;
  vec_vsx_st(v10, 0, dst);
  dst += stride;
  vec_vsx_st(v11, 0, dst);
  dst += stride;
  vec_vsx_st(v12, 0, dst);
  dst += stride;
  vec_vsx_st(v13, 0, dst);
  dst += stride;
  vec_vsx_st(v14, 0, dst);
  dst += stride;
  vec_vsx_st(v15, 0, dst);
}

#define H_PREDICTOR_32(v) \
  vec_vsx_st(v, 0, dst);  \
  vec_vsx_st(v, 16, dst); \
  dst += stride

void vpx_h_predictor_32x32_vsx(uint8_t *dst, ptrdiff_t stride,
                               const uint8_t *above, const uint8_t *left) {
  const uint8x16_t d0 = vec_vsx_ld(0, left);
  const uint8x16_t d1 = vec_vsx_ld(16, left);

  const uint8x16_t v0_0 = vec_splat(d0, 0);
  const uint8x16_t v1_0 = vec_splat(d0, 1);
  const uint8x16_t v2_0 = vec_splat(d0, 2);
  const uint8x16_t v3_0 = vec_splat(d0, 3);
  const uint8x16_t v4_0 = vec_splat(d0, 4);
  const uint8x16_t v5_0 = vec_splat(d0, 5);
  const uint8x16_t v6_0 = vec_splat(d0, 6);
  const uint8x16_t v7_0 = vec_splat(d0, 7);
  const uint8x16_t v8_0 = vec_splat(d0, 8);
  const uint8x16_t v9_0 = vec_splat(d0, 9);
  const uint8x16_t v10_0 = vec_splat(d0, 10);
  const uint8x16_t v11_0 = vec_splat(d0, 11);
  const uint8x16_t v12_0 = vec_splat(d0, 12);
  const uint8x16_t v13_0 = vec_splat(d0, 13);
  const uint8x16_t v14_0 = vec_splat(d0, 14);
  const uint8x16_t v15_0 = vec_splat(d0, 15);

  const uint8x16_t v0_1 = vec_splat(d1, 0);
  const uint8x16_t v1_1 = vec_splat(d1, 1);
  const uint8x16_t v2_1 = vec_splat(d1, 2);
  const uint8x16_t v3_1 = vec_splat(d1, 3);
  const uint8x16_t v4_1 = vec_splat(d1, 4);
  const uint8x16_t v5_1 = vec_splat(d1, 5);
  const uint8x16_t v6_1 = vec_splat(d1, 6);
  const uint8x16_t v7_1 = vec_splat(d1, 7);
  const uint8x16_t v8_1 = vec_splat(d1, 8);
  const uint8x16_t v9_1 = vec_splat(d1, 9);
  const uint8x16_t v10_1 = vec_splat(d1, 10);
  const uint8x16_t v11_1 = vec_splat(d1, 11);
  const uint8x16_t v12_1 = vec_splat(d1, 12);
  const uint8x16_t v13_1 = vec_splat(d1, 13);
  const uint8x16_t v14_1 = vec_splat(d1, 14);
  const uint8x16_t v15_1 = vec_splat(d1, 15);

  (void)above;

  H_PREDICTOR_32(v0_0);
  H_PREDICTOR_32(v1_0);
  H_PREDICTOR_32(v2_0);
  H_PREDICTOR_32(v3_0);

  H_PREDICTOR_32(v4_0);
  H_PREDICTOR_32(v5_0);
  H_PREDICTOR_32(v6_0);
  H_PREDICTOR_32(v7_0);

  H_PREDICTOR_32(v8_0);
  H_PREDICTOR_32(v9_0);
  H_PREDICTOR_32(v10_0);
  H_PREDICTOR_32(v11_0);

  H_PREDICTOR_32(v12_0);
  H_PREDICTOR_32(v13_0);
  H_PREDICTOR_32(v14_0);
  H_PREDICTOR_32(v15_0);

  H_PREDICTOR_32(v0_1);
  H_PREDICTOR_32(v1_1);
  H_PREDICTOR_32(v2_1);
  H_PREDICTOR_32(v3_1);

  H_PREDICTOR_32(v4_1);
  H_PREDICTOR_32(v5_1);
  H_PREDICTOR_32(v6_1);
  H_PREDICTOR_32(v7_1);

  H_PREDICTOR_32(v8_1);
  H_PREDICTOR_32(v9_1);
  H_PREDICTOR_32(v10_1);
  H_PREDICTOR_32(v11_1);

  H_PREDICTOR_32(v12_1);
  H_PREDICTOR_32(v13_1);
  H_PREDICTOR_32(v14_1);
  H_PREDICTOR_32(v15_1);
}
