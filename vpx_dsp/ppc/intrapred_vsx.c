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

  const uint8x16_t v0_0  = vec_splat(d0, 0);
  const uint8x16_t v1_0  = vec_splat(d0, 1);
  const uint8x16_t v2_0  = vec_splat(d0, 2);
  const uint8x16_t v3_0  = vec_splat(d0, 3);
  const uint8x16_t v4_0  = vec_splat(d0, 4);
  const uint8x16_t v5_0  = vec_splat(d0, 5);
  const uint8x16_t v6_0  = vec_splat(d0, 6);
  const uint8x16_t v7_0  = vec_splat(d0, 7);
  const uint8x16_t v8_0  = vec_splat(d0, 8);
  const uint8x16_t v9_0  = vec_splat(d0, 9);
  const uint8x16_t v10_0 = vec_splat(d0, 10);
  const uint8x16_t v11_0 = vec_splat(d0, 11);
  const uint8x16_t v12_0 = vec_splat(d0, 12);
  const uint8x16_t v13_0 = vec_splat(d0, 13);
  const uint8x16_t v14_0 = vec_splat(d0, 14);
  const uint8x16_t v15_0 = vec_splat(d0, 15);

  const uint8x16_t v0_1  = vec_splat(d1, 0);
  const uint8x16_t v1_1  = vec_splat(d1, 1);
  const uint8x16_t v2_1  = vec_splat(d1, 2);
  const uint8x16_t v3_1  = vec_splat(d1, 3);
  const uint8x16_t v4_1  = vec_splat(d1, 4);
  const uint8x16_t v5_1  = vec_splat(d1, 5);
  const uint8x16_t v6_1  = vec_splat(d1, 6);
  const uint8x16_t v7_1  = vec_splat(d1, 7);
  const uint8x16_t v8_1  = vec_splat(d1, 8);
  const uint8x16_t v9_1  = vec_splat(d1, 9);
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

void vpx_tm_predictor_8x8_vsx(uint8_t *dst, ptrdiff_t stride,
                              const uint8_t *above, const uint8_t *left) {
  int16x8_t tl = unpack_to_s16_h(vec_splat(vec_vsx_ld(-1, above), 0));
  int16x8_t l = unpack_to_s16_h(vec_vsx_ld(0, left));
  int16x8_t a = unpack_to_s16_h(vec_vsx_ld(0, above));
  int16x8_t tmp, val;

  tmp = unpack_to_s16_l(vec_vsx_ld(0, dst));
  val = vec_sub(vec_add(vec_splat(l, 0), a), tl);
  vec_vsx_st(vec_packsu(val, tmp), 0, dst);
  dst += stride;

  tmp = unpack_to_s16_l(vec_vsx_ld(0, dst));
  val = vec_sub(vec_add(vec_splat(l, 1), a), tl);
  vec_vsx_st(vec_packsu(val, tmp), 0, dst);
  dst += stride;

  tmp = unpack_to_s16_l(vec_vsx_ld(0, dst));
  val = vec_sub(vec_add(vec_splat(l, 2), a), tl);
  vec_vsx_st(vec_packsu(val, tmp), 0, dst);
  dst += stride;

  tmp = unpack_to_s16_l(vec_vsx_ld(0, dst));
  val = vec_sub(vec_add(vec_splat(l, 3), a), tl);
  vec_vsx_st(vec_packsu(val, tmp), 0, dst);
  dst += stride;

  tmp = unpack_to_s16_l(vec_vsx_ld(0, dst));
  val = vec_sub(vec_add(vec_splat(l, 4), a), tl);
  vec_vsx_st(vec_packsu(val, tmp), 0, dst);
  dst += stride;

  tmp = unpack_to_s16_l(vec_vsx_ld(0, dst));
  val = vec_sub(vec_add(vec_splat(l, 5), a), tl);
  vec_vsx_st(vec_packsu(val, tmp), 0, dst);
  dst += stride;

  tmp = unpack_to_s16_l(vec_vsx_ld(0, dst));
  val = vec_sub(vec_add(vec_splat(l, 6), a), tl);
  vec_vsx_st(vec_packsu(val, tmp), 0, dst);
  dst += stride;

  tmp = unpack_to_s16_l(vec_vsx_ld(0, dst));
  val = vec_sub(vec_add(vec_splat(l, 7), a), tl);
  vec_vsx_st(vec_packsu(val, tmp), 0, dst);
}

static void tm_predictor_16x8(uint8_t *dst, const ptrdiff_t stride,
                              int16x8_t l,
                              int16x8_t ah, int16x8_t al,
                              int16x8_t tl)
{
  int16x8_t vh, vl, ls;

  ls = vec_splat(l, 0);
  vh = vec_sub(vec_add(ls, ah), tl);
  vl = vec_sub(vec_add(ls, al), tl);
  vec_vsx_st(vec_packsu(vh, vl), 0, dst);
  dst += stride;

  ls = vec_splat(l, 1);
  vh = vec_sub(vec_add(ls, ah), tl);
  vl = vec_sub(vec_add(ls, al), tl);
  vec_vsx_st(vec_packsu(vh, vl), 0, dst);
  dst += stride;

  ls = vec_splat(l, 2);
  vh = vec_sub(vec_add(ls, ah), tl);
  vl = vec_sub(vec_add(ls, al), tl);
  vec_vsx_st(vec_packsu(vh, vl), 0, dst);
  dst += stride;

  ls = vec_splat(l, 3);
  vh = vec_sub(vec_add(ls, ah), tl);
  vl = vec_sub(vec_add(ls, al), tl);
  vec_vsx_st(vec_packsu(vh, vl), 0, dst);
  dst += stride;

  ls = vec_splat(l, 4);
  vh = vec_sub(vec_add(ls, ah), tl);
  vl = vec_sub(vec_add(ls, al), tl);
  vec_vsx_st(vec_packsu(vh, vl), 0, dst);
  dst += stride;

  ls = vec_splat(l, 5);
  vh = vec_sub(vec_add(ls, ah), tl);
  vl = vec_sub(vec_add(ls, al), tl);
  vec_vsx_st(vec_packsu(vh, vl), 0, dst);
  dst += stride;

  ls = vec_splat(l, 6);
  vh = vec_sub(vec_add(ls, ah), tl);
  vl = vec_sub(vec_add(ls, al), tl);
  vec_vsx_st(vec_packsu(vh, vl), 0, dst);
  dst += stride;

  ls = vec_splat(l, 7);
  vh = vec_sub(vec_add(ls, ah), tl);
  vl = vec_sub(vec_add(ls, al), tl);
  vec_vsx_st(vec_packsu(vh, vl), 0, dst);
}

void vpx_tm_predictor_16x16_vsx(uint8_t *dst, ptrdiff_t stride,
                                const uint8_t *above, const uint8_t *left) {
  int16x8_t tl = unpack_to_s16_h(vec_splat(vec_vsx_ld(-1, above), 0));
  uint8x16_t l = vec_vsx_ld(0, left);
  int16x8_t lh = unpack_to_s16_h(l);
  int16x8_t ll = unpack_to_s16_l(l);
  uint8x16_t a = vec_vsx_ld(0, above);
  int16x8_t ah = unpack_to_s16_h(a);
  int16x8_t al = unpack_to_s16_l(a);

  tm_predictor_16x8(dst, stride, lh, ah, al, tl);

  dst += stride * 8;

  tm_predictor_16x8(dst, stride, ll, ah, al, tl);
}

static INLINE void tm_predictor_32x1(uint8_t *dst,
                                     const int16x8_t ls,
                                     const int16x8_t a0h,
                                     const int16x8_t a0l,
                                     const int16x8_t a1h,
                                     const int16x8_t a1l,
                                     const int16x8_t tl) {
  int16x8_t vh, vl;

  vh = vec_sub(vec_add(ls, a0h), tl);
  vl = vec_sub(vec_add(ls, a0l), tl);
  vec_vsx_st(vec_packsu(vh, vl), 0, dst);
  vh = vec_sub(vec_add(ls, a1h), tl);
  vl = vec_sub(vec_add(ls, a1l), tl);
  vec_vsx_st(vec_packsu(vh, vl), 16, dst);
}

static void tm_predictor_32x8(uint8_t *dst, const ptrdiff_t stride,
                              const int16x8_t l,
                              const uint8x16_t a0,
                              const uint8x16_t a1,
                              const int16x8_t tl) {
  const int16x8_t a0h = unpack_to_s16_h(a0);
  const int16x8_t a0l = unpack_to_s16_l(a0);
  const int16x8_t a1h = unpack_to_s16_h(a1);
  const int16x8_t a1l = unpack_to_s16_l(a1);

  tm_predictor_32x1(dst, vec_splat(l, 0), a0h, a0l, a1h, a1l, tl);

  dst += stride;

  tm_predictor_32x1(dst, vec_splat(l, 1), a0h, a0l, a1h, a1l, tl);

  dst += stride;

  tm_predictor_32x1(dst, vec_splat(l, 2), a0h, a0l, a1h, a1l, tl);

  dst += stride;

  tm_predictor_32x1(dst, vec_splat(l, 3), a0h, a0l, a1h, a1l, tl);

  dst += stride;

  tm_predictor_32x1(dst, vec_splat(l, 4), a0h, a0l, a1h, a1l, tl);

  dst += stride;

  tm_predictor_32x1(dst, vec_splat(l, 5), a0h, a0l, a1h, a1l, tl);

  dst += stride;

  tm_predictor_32x1(dst, vec_splat(l, 6), a0h, a0l, a1h, a1l, tl);

  dst += stride;

  tm_predictor_32x1(dst, vec_splat(l, 7), a0h, a0l, a1h, a1l, tl);
}

void vpx_tm_predictor_32x32_vsx(uint8_t *dst, ptrdiff_t stride,
                                const uint8_t *above, const uint8_t *left) {
  int16x8_t tl = unpack_to_s16_h(vec_splat(vec_vsx_ld(-1, above), 0));
  uint8x16_t l0 = vec_vsx_ld(0, left);
  uint8x16_t l1 = vec_vsx_ld(16, left);
  uint8x16_t a0 = vec_vsx_ld(0, above);
  uint8x16_t a1 = vec_vsx_ld(16, above);

  tm_predictor_32x8(dst, stride, unpack_to_s16_h(l0), a0, a1, tl);

  dst += stride * 8;

  tm_predictor_32x8(dst, stride, unpack_to_s16_l(l0), a0, a1, tl);

  dst += stride * 8;

  tm_predictor_32x8(dst, stride, unpack_to_s16_h(l1), a0, a1, tl);

  dst += stride * 8;

  tm_predictor_32x8(dst, stride, unpack_to_s16_l(l1), a0, a1, tl);
}

static INLINE void dc_fill_predictor_16x16(uint8_t *dst, const ptrdiff_t stride,
                                           const uint8x16_t val) {
  int i;

  for (i = 0; i < 16; i++, dst += stride) {
    vec_vsx_st(val, 0, dst);
  }
}

void vpx_dc_128_predictor_16x16_vsx(uint8_t *dst, ptrdiff_t stride,
                                    const uint8_t *above, const uint8_t *left) {
  uint8x16_t v128 = vec_sl(vec_splat_u8(1), vec_splat_u8(7));
  (void)above;
  (void)left;

  dc_fill_predictor_16x16(dst, stride, v128);
}

static INLINE void dc_fill_predictor_32x32(uint8_t *dst, const ptrdiff_t stride,
                                           const uint8x16_t val) {
  int i;

  for (i = 0; i < 32; i++, dst += stride) {
    vec_vsx_st(val, 0, dst);
    vec_vsx_st(val, 16, dst);
  }
}

void vpx_dc_128_predictor_32x32_vsx(uint8_t *dst, ptrdiff_t stride,
                                    const uint8_t *above, const uint8_t *left) {
  uint8x16_t v128 = vec_sl(vec_splat_u8(1), vec_splat_u8(7));
  (void)above;
  (void)left;

  dc_fill_predictor_32x32(dst, stride, v128);
}


