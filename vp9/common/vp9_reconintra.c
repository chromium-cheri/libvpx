/*
 *  Copyright (c) 2010 The WebM project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "./vpx_config.h"
#include "./vp9_rtcd.h"

#include "vpx_mem/vpx_mem.h"

#include "vp9/common/vp9_reconintra.h"
#include "vp9/common/vp9_onyxc_int.h"

const TX_TYPE intra_mode_to_tx_type_lookup[INTRA_MODES] = {
  DCT_DCT,    // DC
  ADST_DCT,   // V
  DCT_ADST,   // H
  DCT_DCT,    // D45
  ADST_ADST,  // D135
  ADST_DCT,   // D117
  DCT_ADST,   // D153
  DCT_ADST,   // D207
  ADST_DCT,   // D63
  ADST_ADST,  // TM
};

// This serves as a wrapper function, so that all the prediction functions
// can be unified and accessed as a pointer array. Note that the boundary
// above and left are not necessarily used all the time.
#define intra_pred_sized(type, size) \
  void vp9_##type##_predictor_##size##x##size##_c(uint8_t *dst, \
                                                  ptrdiff_t stride, \
                                                  const uint8_t *above, \
                                                  const uint8_t *left) { \
    type##_predictor(dst, stride, size, above, left); \
  }

#if CONFIG_VP9_HIGHBITDEPTH
#define intra_pred_highbd_sized(type, size) \
  void vp9_highbd_##type##_predictor_##size##x##size##_c( \
      uint16_t *dst, ptrdiff_t stride, const uint16_t *above, \
      const uint16_t *left, int bd) { \
    highbd_##type##_predictor(dst, stride, size, above, left, bd); \
  }
#endif  // CONFIG_VP9_HIGHBITDEPTH

#if CONFIG_TX64X64

#if CONFIG_VP9_HIGHBITDEPTH
#define intra_pred_allsizes(type) \
  intra_pred_sized(type, 4) \
  intra_pred_sized(type, 8) \
  intra_pred_sized(type, 16) \
  intra_pred_sized(type, 32) \
  intra_pred_sized(type, 64) \
  intra_pred_highbd_sized(type, 4) \
  intra_pred_highbd_sized(type, 8) \
  intra_pred_highbd_sized(type, 16) \
  intra_pred_highbd_sized(type, 32) \
  intra_pred_highbd_sized(type, 64)
#else
#define intra_pred_allsizes(type) \
  intra_pred_sized(type, 4) \
  intra_pred_sized(type, 8) \
  intra_pred_sized(type, 16) \
  intra_pred_sized(type, 32) \
  intra_pred_sized(type, 64)
#endif  // CONFIG_VP9_HIGHBITDEPTH

#else   // CONFIG_TX64X64

#if CONFIG_VP9_HIGHBITDEPTH
#define intra_pred_allsizes(type) \
  intra_pred_sized(type, 4) \
  intra_pred_sized(type, 8) \
  intra_pred_sized(type, 16) \
  intra_pred_sized(type, 32) \
  intra_pred_highbd_sized(type, 4) \
  intra_pred_highbd_sized(type, 8) \
  intra_pred_highbd_sized(type, 16) \
  intra_pred_highbd_sized(type, 32)
#else
#define intra_pred_allsizes(type) \
  intra_pred_sized(type, 4) \
  intra_pred_sized(type, 8) \
  intra_pred_sized(type, 16) \
  intra_pred_sized(type, 32)
#endif  // CONFIG_VP9_HIGHBITDEPTH

#endif  // CONFIG_TX64X64

#if CONFIG_VP9_HIGHBITDEPTH
static INLINE void highbd_d207_predictor(uint16_t *dst, ptrdiff_t stride,
                                         int bs, const uint16_t *above,
                                         const uint16_t *left, int bd) {
  int r, c;
  (void) above;
  (void) bd;

  // First column.
  for (r = 0; r < bs - 1; ++r) {
    dst[r * stride] = ROUND_POWER_OF_TWO(left[r] + left[r + 1], 1);
  }
  dst[(bs - 1) * stride] = left[bs - 1];
  dst++;

  // Second column.
  for (r = 0; r < bs - 2; ++r) {
    dst[r * stride] = ROUND_POWER_OF_TWO(left[r] + left[r + 1] * 2 +
                                         left[r + 2], 2);
  }
  dst[(bs - 2) * stride] = ROUND_POWER_OF_TWO(left[bs - 2] +
                                              left[bs - 1] * 3, 2);
  dst[(bs - 1) * stride] = left[bs - 1];
  dst++;

  // Rest of last row.
  for (c = 0; c < bs - 2; ++c)
    dst[(bs - 1) * stride + c] = left[bs - 1];

  for (r = bs - 2; r >= 0; --r) {
    for (c = 0; c < bs - 2; ++c)
      dst[r * stride + c] = dst[(r + 1) * stride + c - 2];
  }
}

static INLINE void highbd_d63_predictor(uint16_t *dst, ptrdiff_t stride,
                                        int bs, const uint16_t *above,
                                        const uint16_t *left, int bd) {
  int r, c;
  (void) left;
  (void) bd;
  for (r = 0; r < bs; ++r) {
    for (c = 0; c < bs; ++c) {
      dst[c] = r & 1 ? ROUND_POWER_OF_TWO(above[r/2 + c] +
                                          above[r/2 + c + 1] * 2 +
                                          above[r/2 + c + 2], 2)
                     : ROUND_POWER_OF_TWO(above[r/2 + c] +
                                          above[r/2 + c + 1], 1);
    }
    dst += stride;
  }
}

static INLINE void highbd_d45_predictor(uint16_t *dst, ptrdiff_t stride, int bs,
                                        const uint16_t *above,
                                        const uint16_t *left, int bd) {
  int r, c;
  (void) left;
  (void) bd;
  for (r = 0; r < bs; ++r) {
    for (c = 0; c < bs; ++c) {
      dst[c] = r + c + 2 < bs * 2 ?  ROUND_POWER_OF_TWO(above[r + c] +
                                                        above[r + c + 1] * 2 +
                                                        above[r + c + 2], 2)
                                  : above[bs * 2 - 1];
    }
    dst += stride;
  }
}

static INLINE void highbd_d117_predictor(uint16_t *dst, ptrdiff_t stride,
                                         int bs, const uint16_t *above,
                                         const uint16_t *left, int bd) {
  int r, c;
  (void) bd;

  // first row
  for (c = 0; c < bs; c++)
    dst[c] = ROUND_POWER_OF_TWO(above[c - 1] + above[c], 1);
  dst += stride;

  // second row
  dst[0] = ROUND_POWER_OF_TWO(left[0] + above[-1] * 2 + above[0], 2);
  for (c = 1; c < bs; c++)
    dst[c] = ROUND_POWER_OF_TWO(above[c - 2] + above[c - 1] * 2 + above[c], 2);
  dst += stride;

  // the rest of first col
  dst[0] = ROUND_POWER_OF_TWO(above[-1] + left[0] * 2 + left[1], 2);
  for (r = 3; r < bs; ++r)
    dst[(r - 2) * stride] = ROUND_POWER_OF_TWO(left[r - 3] + left[r - 2] * 2 +
                                               left[r - 1], 2);

  // the rest of the block
  for (r = 2; r < bs; ++r) {
    for (c = 1; c < bs; c++)
      dst[c] = dst[-2 * stride + c - 1];
    dst += stride;
  }
}

static INLINE void highbd_d135_predictor(uint16_t *dst, ptrdiff_t stride,
                                         int bs, const uint16_t *above,
                                         const uint16_t *left, int bd) {
  int r, c;
  (void) bd;
  dst[0] = ROUND_POWER_OF_TWO(left[0] + above[-1] * 2 + above[0], 2);
  for (c = 1; c < bs; c++)
    dst[c] = ROUND_POWER_OF_TWO(above[c - 2] + above[c - 1] * 2 + above[c], 2);

  dst[stride] = ROUND_POWER_OF_TWO(above[-1] + left[0] * 2 + left[1], 2);
  for (r = 2; r < bs; ++r)
    dst[r * stride] = ROUND_POWER_OF_TWO(left[r - 2] + left[r - 1] * 2 +
                                         left[r], 2);

  dst += stride;
  for (r = 1; r < bs; ++r) {
    for (c = 1; c < bs; c++)
      dst[c] = dst[-stride + c - 1];
    dst += stride;
  }
}

static INLINE void highbd_d153_predictor(uint16_t *dst, ptrdiff_t stride,
                                         int bs, const uint16_t *above,
                                         const uint16_t *left, int bd) {
  int r, c;
  (void) bd;
  dst[0] = ROUND_POWER_OF_TWO(above[-1] + left[0], 1);
  for (r = 1; r < bs; r++)
    dst[r * stride] = ROUND_POWER_OF_TWO(left[r - 1] + left[r], 1);
  dst++;

  dst[0] = ROUND_POWER_OF_TWO(left[0] + above[-1] * 2 + above[0], 2);
  dst[stride] = ROUND_POWER_OF_TWO(above[-1] + left[0] * 2 + left[1], 2);
  for (r = 2; r < bs; r++)
    dst[r * stride] = ROUND_POWER_OF_TWO(left[r - 2] + left[r - 1] * 2 +
                                         left[r], 2);
  dst++;

  for (c = 0; c < bs - 2; c++)
    dst[c] = ROUND_POWER_OF_TWO(above[c - 1] + above[c] * 2 + above[c + 1], 2);
  dst += stride;

  for (r = 1; r < bs; ++r) {
    for (c = 0; c < bs - 2; c++)
      dst[c] = dst[-stride + c - 2];
    dst += stride;
  }
}

static INLINE void highbd_v_predictor(uint16_t *dst, ptrdiff_t stride,
                                      int bs, const uint16_t *above,
                                      const uint16_t *left, int bd) {
  int r;
  (void) left;
  (void) bd;
  for (r = 0; r < bs; r++) {
    vpx_memcpy(dst, above, bs * sizeof(uint16_t));
    dst += stride;
  }
}

static INLINE void highbd_h_predictor(uint16_t *dst, ptrdiff_t stride,
                                      int bs, const uint16_t *above,
                                      const uint16_t *left, int bd) {
  int r;
  (void) above;
  (void) bd;
  for (r = 0; r < bs; r++) {
    vpx_memset16(dst, left[r], bs);
    dst += stride;
  }
}

static INLINE void highbd_tm_predictor(uint16_t *dst, ptrdiff_t stride,
                                       int bs, const uint16_t *above,
                                       const uint16_t *left, int bd) {
  int r, c;
  int ytop_left = above[-1];
  (void) bd;

  for (r = 0; r < bs; r++) {
    for (c = 0; c < bs; c++)
      dst[c] = clip_pixel_highbd(left[r] + above[c] - ytop_left, bd);
    dst += stride;
  }
}

static INLINE void highbd_dc_128_predictor(uint16_t *dst, ptrdiff_t stride,
                                           int bs, const uint16_t *above,
                                           const uint16_t *left, int bd) {
  int r;
  (void) above;
  (void) left;

  for (r = 0; r < bs; r++) {
    vpx_memset16(dst, 128 << (bd - 8), bs);
    dst += stride;
  }
}

static INLINE void highbd_dc_left_predictor(uint16_t *dst, ptrdiff_t stride,
                                            int bs, const uint16_t *above,
                                            const uint16_t *left, int bd) {
  int i, r, expected_dc, sum = 0;
  (void) above;
  (void) bd;

  for (i = 0; i < bs; i++)
    sum += left[i];
  expected_dc = (sum + (bs >> 1)) / bs;

  for (r = 0; r < bs; r++) {
    vpx_memset16(dst, expected_dc, bs);
    dst += stride;
  }
}

static INLINE void highbd_dc_top_predictor(uint16_t *dst, ptrdiff_t stride,
                                           int bs, const uint16_t *above,
                                           const uint16_t *left, int bd) {
  int i, r, expected_dc, sum = 0;
  (void) left;
  (void) bd;

  for (i = 0; i < bs; i++)
    sum += above[i];
  expected_dc = (sum + (bs >> 1)) / bs;

  for (r = 0; r < bs; r++) {
    vpx_memset16(dst, expected_dc, bs);
    dst += stride;
  }
}

static INLINE void highbd_dc_predictor(uint16_t *dst, ptrdiff_t stride,
                                       int bs, const uint16_t *above,
                                       const uint16_t *left, int bd) {
  int i, r, expected_dc, sum = 0;
  const int count = 2 * bs;
  (void) bd;

  for (i = 0; i < bs; i++) {
    sum += above[i];
    sum += left[i];
  }

  expected_dc = (sum + (count >> 1)) / count;

  for (r = 0; r < bs; r++) {
    vpx_memset16(dst, expected_dc, bs);
    dst += stride;
  }
}
#endif  // CONFIG_VP9_HIGHBITDEPTH

static INLINE void d207_predictor(uint8_t *dst, ptrdiff_t stride, int bs,
                                  const uint8_t *above, const uint8_t *left) {
  int r, c;
  (void) above;
  // first column
  for (r = 0; r < bs - 1; ++r)
    dst[r * stride] = ROUND_POWER_OF_TWO(left[r] + left[r + 1], 1);
  dst[(bs - 1) * stride] = left[bs - 1];
  dst++;

  // second column
  for (r = 0; r < bs - 2; ++r)
    dst[r * stride] = ROUND_POWER_OF_TWO(left[r] + left[r + 1] * 2 +
                                         left[r + 2], 2);
  dst[(bs - 2) * stride] = ROUND_POWER_OF_TWO(left[bs - 2] +
                                              left[bs - 1] * 3, 2);
  dst[(bs - 1) * stride] = left[bs - 1];
  dst++;

  // rest of last row
  for (c = 0; c < bs - 2; ++c)
    dst[(bs - 1) * stride + c] = left[bs - 1];

  for (r = bs - 2; r >= 0; --r)
    for (c = 0; c < bs - 2; ++c)
      dst[r * stride + c] = dst[(r + 1) * stride + c - 2];
}
intra_pred_allsizes(d207)

static INLINE void d63_predictor(uint8_t *dst, ptrdiff_t stride, int bs,
                                 const uint8_t *above, const uint8_t *left) {
  int r, c;
  (void) left;
  for (r = 0; r < bs; ++r) {
    for (c = 0; c < bs; ++c)
      dst[c] = r & 1 ? ROUND_POWER_OF_TWO(above[r/2 + c] +
                                          above[r/2 + c + 1] * 2 +
                                          above[r/2 + c + 2], 2)
                     : ROUND_POWER_OF_TWO(above[r/2 + c] +
                                          above[r/2 + c + 1], 1);
    dst += stride;
  }
}
intra_pred_allsizes(d63)

static INLINE void d45_predictor(uint8_t *dst, ptrdiff_t stride, int bs,
                                 const uint8_t *above, const uint8_t *left) {
  int r, c;
  (void) left;
  for (r = 0; r < bs; ++r) {
    for (c = 0; c < bs; ++c)
      dst[c] = r + c + 2 < bs * 2 ?  ROUND_POWER_OF_TWO(above[r + c] +
                                                        above[r + c + 1] * 2 +
                                                        above[r + c + 2], 2)
                                  : above[bs * 2 - 1];
    dst += stride;
  }
}
intra_pred_allsizes(d45)

static INLINE void d117_predictor(uint8_t *dst, ptrdiff_t stride, int bs,
                                  const uint8_t *above, const uint8_t *left) {
  int r, c;

  // first row
  for (c = 0; c < bs; c++)
    dst[c] = ROUND_POWER_OF_TWO(above[c - 1] + above[c], 1);
  dst += stride;

  // second row
  dst[0] = ROUND_POWER_OF_TWO(left[0] + above[-1] * 2 + above[0], 2);
  for (c = 1; c < bs; c++)
    dst[c] = ROUND_POWER_OF_TWO(above[c - 2] + above[c - 1] * 2 + above[c], 2);
  dst += stride;

  // the rest of first col
  dst[0] = ROUND_POWER_OF_TWO(above[-1] + left[0] * 2 + left[1], 2);
  for (r = 3; r < bs; ++r)
    dst[(r - 2) * stride] = ROUND_POWER_OF_TWO(left[r - 3] + left[r - 2] * 2 +
                                               left[r - 1], 2);

  // the rest of the block
  for (r = 2; r < bs; ++r) {
    for (c = 1; c < bs; c++)
      dst[c] = dst[-2 * stride + c - 1];
    dst += stride;
  }
}
intra_pred_allsizes(d117)

static INLINE void d135_predictor(uint8_t *dst, ptrdiff_t stride, int bs,
                                  const uint8_t *above, const uint8_t *left) {
  int r, c;
  dst[0] = ROUND_POWER_OF_TWO(left[0] + above[-1] * 2 + above[0], 2);
  for (c = 1; c < bs; c++)
    dst[c] = ROUND_POWER_OF_TWO(above[c - 2] + above[c - 1] * 2 + above[c], 2);

  dst[stride] = ROUND_POWER_OF_TWO(above[-1] + left[0] * 2 + left[1], 2);
  for (r = 2; r < bs; ++r)
    dst[r * stride] = ROUND_POWER_OF_TWO(left[r - 2] + left[r - 1] * 2 +
                                         left[r], 2);

  dst += stride;
  for (r = 1; r < bs; ++r) {
    for (c = 1; c < bs; c++)
      dst[c] = dst[-stride + c - 1];
    dst += stride;
  }
}
intra_pred_allsizes(d135)

static INLINE void d153_predictor(uint8_t *dst, ptrdiff_t stride, int bs,
                                  const uint8_t *above, const uint8_t *left) {
  int r, c;
  dst[0] = ROUND_POWER_OF_TWO(above[-1] + left[0], 1);
  for (r = 1; r < bs; r++)
    dst[r * stride] = ROUND_POWER_OF_TWO(left[r - 1] + left[r], 1);
  dst++;

  dst[0] = ROUND_POWER_OF_TWO(left[0] + above[-1] * 2 + above[0], 2);
  dst[stride] = ROUND_POWER_OF_TWO(above[-1] + left[0] * 2 + left[1], 2);
  for (r = 2; r < bs; r++)
    dst[r * stride] = ROUND_POWER_OF_TWO(left[r - 2] + left[r - 1] * 2 +
                                         left[r], 2);
  dst++;

  for (c = 0; c < bs - 2; c++)
    dst[c] = ROUND_POWER_OF_TWO(above[c - 1] + above[c] * 2 + above[c + 1], 2);
  dst += stride;

  for (r = 1; r < bs; ++r) {
    for (c = 0; c < bs - 2; c++)
      dst[c] = dst[-stride + c - 2];
    dst += stride;
  }
}
intra_pred_allsizes(d153)

static INLINE void v_predictor(uint8_t *dst, ptrdiff_t stride, int bs,
                               const uint8_t *above, const uint8_t *left) {
  int r;
  (void) left;

  for (r = 0; r < bs; r++) {
    vpx_memcpy(dst, above, bs);
    dst += stride;
  }
}
intra_pred_allsizes(v)

static INLINE void h_predictor(uint8_t *dst, ptrdiff_t stride, int bs,
                               const uint8_t *above, const uint8_t *left) {
  int r;
  (void) above;

  for (r = 0; r < bs; r++) {
    vpx_memset(dst, left[r], bs);
    dst += stride;
  }
}
intra_pred_allsizes(h)

static INLINE void tm_predictor(uint8_t *dst, ptrdiff_t stride, int bs,
                                const uint8_t *above, const uint8_t *left) {
  int r, c;
  int ytop_left = above[-1];

  for (r = 0; r < bs; r++) {
    for (c = 0; c < bs; c++)
      dst[c] = clip_pixel(left[r] + above[c] - ytop_left);
    dst += stride;
  }
}
intra_pred_allsizes(tm)

static INLINE void dc_128_predictor(uint8_t *dst, ptrdiff_t stride, int bs,
                                    const uint8_t *above, const uint8_t *left) {
  int r;
  (void) above;
  (void) left;

  for (r = 0; r < bs; r++) {
    vpx_memset(dst, 128, bs);
    dst += stride;
  }
}
intra_pred_allsizes(dc_128)

static INLINE void dc_left_predictor(uint8_t *dst, ptrdiff_t stride, int bs,
                                     const uint8_t *above,
                                     const uint8_t *left) {
  int i, r, expected_dc, sum = 0;
  (void) above;

  for (i = 0; i < bs; i++)
    sum += left[i];
  expected_dc = (sum + (bs >> 1)) / bs;

  for (r = 0; r < bs; r++) {
    vpx_memset(dst, expected_dc, bs);
    dst += stride;
  }
}
intra_pred_allsizes(dc_left)

static INLINE void dc_top_predictor(uint8_t *dst, ptrdiff_t stride, int bs,
                                    const uint8_t *above, const uint8_t *left) {
  int i, r, expected_dc, sum = 0;
  (void) left;

  for (i = 0; i < bs; i++)
    sum += above[i];
  expected_dc = (sum + (bs >> 1)) / bs;

  for (r = 0; r < bs; r++) {
    vpx_memset(dst, expected_dc, bs);
    dst += stride;
  }
}
intra_pred_allsizes(dc_top)

static INLINE void dc_predictor(uint8_t *dst, ptrdiff_t stride, int bs,
                                const uint8_t *above, const uint8_t *left) {
  int i, r, expected_dc, sum = 0;
  const int count = 2 * bs;

  for (i = 0; i < bs; i++) {
    sum += above[i];
    sum += left[i];
  }

  expected_dc = (sum + (count >> 1)) / count;

  for (r = 0; r < bs; r++) {
    vpx_memset(dst, expected_dc, bs);
    dst += stride;
  }
}
intra_pred_allsizes(dc)
#undef intra_pred_allsizes

typedef void (*intra_pred_fn)(uint8_t *dst, ptrdiff_t stride,
                              const uint8_t *above, const uint8_t *left);

static intra_pred_fn pred[INTRA_MODES][TX_SIZES];
static intra_pred_fn dc_pred[2][2][TX_SIZES];

#if CONFIG_VP9_HIGHBITDEPTH
typedef void (*intra_high_pred_fn)(uint16_t *dst, ptrdiff_t stride,
                                   const uint16_t *above, const uint16_t *left,
                                   int bd);
static intra_high_pred_fn pred_high[INTRA_MODES][TX_SIZES];
static intra_high_pred_fn dc_pred_high[2][2][TX_SIZES];
#endif  // CONFIG_VP9_HIGHBITDEPTH

void vp9_init_intra_predictors() {
#if CONFIG_TX64X64
#define INIT_ALL_SIZES(p, type) \
  p[TX_4X4] = vp9_##type##_predictor_4x4; \
  p[TX_8X8] = vp9_##type##_predictor_8x8; \
  p[TX_16X16] = vp9_##type##_predictor_16x16; \
  p[TX_32X32] = vp9_##type##_predictor_32x32; \
  p[TX_64X64] = vp9_##type##_predictor_64x64
#else
#define INIT_ALL_SIZES(p, type) \
  p[TX_4X4] = vp9_##type##_predictor_4x4; \
  p[TX_8X8] = vp9_##type##_predictor_8x8; \
  p[TX_16X16] = vp9_##type##_predictor_16x16; \
  p[TX_32X32] = vp9_##type##_predictor_32x32
#endif

  INIT_ALL_SIZES(pred[V_PRED], v);
  INIT_ALL_SIZES(pred[H_PRED], h);
  INIT_ALL_SIZES(pred[D207_PRED], d207);
  INIT_ALL_SIZES(pred[D45_PRED], d45);
  INIT_ALL_SIZES(pred[D63_PRED], d63);
  INIT_ALL_SIZES(pred[D117_PRED], d117);
  INIT_ALL_SIZES(pred[D135_PRED], d135);
  INIT_ALL_SIZES(pred[D153_PRED], d153);
  INIT_ALL_SIZES(pred[TM_PRED], tm);

  INIT_ALL_SIZES(dc_pred[0][0], dc_128);
  INIT_ALL_SIZES(dc_pred[0][1], dc_top);
  INIT_ALL_SIZES(dc_pred[1][0], dc_left);
  INIT_ALL_SIZES(dc_pred[1][1], dc);

#if CONFIG_VP9_HIGHBITDEPTH
  INIT_ALL_SIZES(pred_high[V_PRED], highbd_v);
  INIT_ALL_SIZES(pred_high[H_PRED], highbd_h);
  INIT_ALL_SIZES(pred_high[D207_PRED], highbd_d207);
  INIT_ALL_SIZES(pred_high[D45_PRED], highbd_d45);
  INIT_ALL_SIZES(pred_high[D63_PRED], highbd_d63);
  INIT_ALL_SIZES(pred_high[D117_PRED], highbd_d117);
  INIT_ALL_SIZES(pred_high[D135_PRED], highbd_d135);
  INIT_ALL_SIZES(pred_high[D153_PRED], highbd_d153);
  INIT_ALL_SIZES(pred_high[TM_PRED], highbd_tm);

  INIT_ALL_SIZES(dc_pred_high[0][0], highbd_dc_128);
  INIT_ALL_SIZES(dc_pred_high[0][1], highbd_dc_top);
  INIT_ALL_SIZES(dc_pred_high[1][0], highbd_dc_left);
  INIT_ALL_SIZES(dc_pred_high[1][1], highbd_dc);
#endif  // CONFIG_VP9_HIGHBITDEPTH

#undef intra_pred_allsizes
}

#if CONFIG_VP9_HIGHBITDEPTH
static void build_intra_predictors_high(const MACROBLOCKD *xd,
                                        const uint8_t *ref8,
                                        int ref_stride,
                                        uint8_t *dst8,
                                        int dst_stride,
                                        PREDICTION_MODE mode,
                                        TX_SIZE tx_size,
                                        int up_available,
                                        int left_available,
                                        int right_available,
                                        int x, int y,
                                        int plane, int bd) {
  int i;
  uint16_t *dst = CONVERT_TO_SHORTPTR(dst8);
  uint16_t *ref = CONVERT_TO_SHORTPTR(ref8);
  DECLARE_ALIGNED_ARRAY(16, uint16_t, left_col, 64);
#if CONFIG_TX64X64
  DECLARE_ALIGNED_ARRAY(16, uint16_t, above_data, 256 + 16);
#else
  DECLARE_ALIGNED_ARRAY(16, uint16_t, above_data, 128 + 16);
#endif
  uint16_t *above_row = above_data + 16;
  const uint16_t *const_above_row = above_row;
  const int bs = 4 << tx_size;
  int frame_width, frame_height;
  int x0, y0;
  const struct macroblockd_plane *const pd = &xd->plane[plane];
  //  int base=128;
  int base = 128 << (bd - 8);
  // 127 127 127 .. 127 127 127 127 127 127
  // 129  A   B  ..  Y   Z
  // 129  C   D  ..  W   X
  // 129  E   F  ..  U   V
  // 129  G   H  ..  S   T   T   T   T   T

  // Get current frame pointer, width and height.
  if (plane == 0) {
    frame_width = xd->cur_buf->y_width;
    frame_height = xd->cur_buf->y_height;
  } else {
    frame_width = xd->cur_buf->uv_width;
    frame_height = xd->cur_buf->uv_height;
  }

  // Get block position in current frame.
  x0 = (-xd->mb_to_left_edge >> (3 + pd->subsampling_x)) + x;
  y0 = (-xd->mb_to_top_edge >> (3 + pd->subsampling_y)) + y;

  // left
  if (left_available) {
    if (xd->mb_to_bottom_edge < 0) {
      /* slower path if the block needs border extension */
      if (y0 + bs <= frame_height) {
        for (i = 0; i < bs; ++i)
          left_col[i] = ref[i * ref_stride - 1];
      } else {
        const int extend_bottom = frame_height - y0;
        for (i = 0; i < extend_bottom; ++i)
          left_col[i] = ref[i * ref_stride - 1];
        for (; i < bs; ++i)
          left_col[i] = ref[(extend_bottom - 1) * ref_stride - 1];
      }
    } else {
      /* faster path if the block does not need extension */
      for (i = 0; i < bs; ++i)
        left_col[i] = ref[i * ref_stride - 1];
    }
  } else {
    // TODO(Peter): this value should probably change for high bitdepth
    vpx_memset16(left_col, base + 1, bs);
  }

  // TODO(hkuang) do not extend 2*bs pixels for all modes.
  // above
  if (up_available) {
    const uint16_t *above_ref = ref - ref_stride;
    if (xd->mb_to_right_edge < 0) {
      /* slower path if the block needs border extension */
      if (x0 + 2 * bs <= frame_width) {
        if (right_available && bs == 4) {
          vpx_memcpy(above_row, above_ref, 2 * bs * sizeof(uint16_t));
        } else {
          vpx_memcpy(above_row, above_ref, bs * sizeof(uint16_t));
          vpx_memset16(above_row + bs, above_row[bs - 1], bs);
        }
      } else if (x0 + bs <= frame_width) {
        const int r = frame_width - x0;
        if (right_available && bs == 4) {
          vpx_memcpy(above_row, above_ref, r * sizeof(uint16_t));
          vpx_memset16(above_row + r, above_row[r - 1],
                       x0 + 2 * bs - frame_width);
        } else {
          vpx_memcpy(above_row, above_ref, bs * sizeof(uint16_t));
          vpx_memset16(above_row + bs, above_row[bs - 1], bs);
        }
      } else if (x0 <= frame_width) {
        const int r = frame_width - x0;
        if (right_available && bs == 4) {
          vpx_memcpy(above_row, above_ref, r * sizeof(uint16_t));
          vpx_memset16(above_row + r, above_row[r - 1],
                       x0 + 2 * bs - frame_width);
        } else {
          vpx_memcpy(above_row, above_ref, r * sizeof(uint16_t));
          vpx_memset16(above_row + r, above_row[r - 1],
                       x0 + 2 * bs - frame_width);
        }
      }
      // TODO(Peter) this value should probably change for high bitdepth
      above_row[-1] = left_available ? above_ref[-1] : (base+1);
    } else {
      /* faster path if the block does not need extension */
      if (bs == 4 && right_available && left_available) {
        const_above_row = above_ref;
      } else {
        vpx_memcpy(above_row, above_ref, bs * sizeof(uint16_t));
        if (bs == 4 && right_available)
          vpx_memcpy(above_row + bs, above_ref + bs, bs * sizeof(uint16_t));
        else
          vpx_memset16(above_row + bs, above_row[bs - 1], bs);
        // TODO(Peter): this value should probably change for high bitdepth
        above_row[-1] = left_available ? above_ref[-1] : (base+1);
      }
    }
  } else {
    vpx_memset16(above_row, base - 1, bs * 2);
    // TODO(Peter): this value should probably change for high bitdepth
    above_row[-1] = base - 1;
  }

  // predict
  if (mode == DC_PRED) {
    dc_pred_high[left_available][up_available][tx_size](dst, dst_stride,
                                                        const_above_row,
                                                        left_col, xd->bd);
  } else {
    pred_high[mode][tx_size](dst, dst_stride, const_above_row, left_col,
                             xd->bd);
  }
}
#endif  // CONFIG_VP9_HIGHBITDEPTH

static void build_intra_predictors(const MACROBLOCKD *xd, const uint8_t *ref,
                                   int ref_stride, uint8_t *dst, int dst_stride,
                                   PREDICTION_MODE mode, TX_SIZE tx_size,
                                   int up_available, int left_available,
                                   int right_available, int x, int y,
                                   int plane) {
  int i;
  DECLARE_ALIGNED_ARRAY(16, uint8_t, left_col, 64);
#if CONFIG_TX64X64
  DECLARE_ALIGNED_ARRAY(16, uint8_t, above_data, 256 + 16);
#else
  DECLARE_ALIGNED_ARRAY(16, uint8_t, above_data, 128 + 16);
#endif
  uint8_t *above_row = above_data + 16;
  const uint8_t *const_above_row = above_row;
  const int bs = 4 << tx_size;
  int frame_width, frame_height;
  int x0, y0;
  const struct macroblockd_plane *const pd = &xd->plane[plane];

  // 127 127 127 .. 127 127 127 127 127 127
  // 129  A   B  ..  Y   Z
  // 129  C   D  ..  W   X
  // 129  E   F  ..  U   V
  // 129  G   H  ..  S   T   T   T   T   T
  // ..

  // Get current frame pointer, width and height.
  if (plane == 0) {
    frame_width = xd->cur_buf->y_width;
    frame_height = xd->cur_buf->y_height;
  } else {
    frame_width = xd->cur_buf->uv_width;
    frame_height = xd->cur_buf->uv_height;
  }

  // Get block position in current frame.
  x0 = (-xd->mb_to_left_edge >> (3 + pd->subsampling_x)) + x;
  y0 = (-xd->mb_to_top_edge >> (3 + pd->subsampling_y)) + y;

  vpx_memset(left_col, 129, 64);

  // left
  if (left_available) {
    if (xd->mb_to_bottom_edge < 0) {
      /* slower path if the block needs border extension */
      if (y0 + bs <= frame_height) {
        for (i = 0; i < bs; ++i)
          left_col[i] = ref[i * ref_stride - 1];
      } else {
        const int extend_bottom = frame_height - y0;
        for (i = 0; i < extend_bottom; ++i)
          left_col[i] = ref[i * ref_stride - 1];
        for (; i < bs; ++i)
          left_col[i] = ref[(extend_bottom - 1) * ref_stride - 1];
      }
    } else {
      /* faster path if the block does not need extension */
      for (i = 0; i < bs; ++i)
        left_col[i] = ref[i * ref_stride - 1];
    }
  }

  // TODO(hkuang) do not extend 2*bs pixels for all modes.
  // above
  if (up_available) {
    const uint8_t *above_ref = ref - ref_stride;
    if (xd->mb_to_right_edge < 0) {
      /* slower path if the block needs border extension */
      if (x0 + 2 * bs <= frame_width) {
        if (right_available && bs == 4) {
          vpx_memcpy(above_row, above_ref, 2 * bs);
        } else {
          vpx_memcpy(above_row, above_ref, bs);
          vpx_memset(above_row + bs, above_row[bs - 1], bs);
        }
      } else if (x0 + bs <= frame_width) {
        const int r = frame_width - x0;
        if (right_available && bs == 4) {
          vpx_memcpy(above_row, above_ref, r);
          vpx_memset(above_row + r, above_row[r - 1],
                     x0 + 2 * bs - frame_width);
        } else {
          vpx_memcpy(above_row, above_ref, bs);
          vpx_memset(above_row + bs, above_row[bs - 1], bs);
        }
      } else if (x0 <= frame_width) {
        const int r = frame_width - x0;
        if (right_available && bs == 4) {
          vpx_memcpy(above_row, above_ref, r);
          vpx_memset(above_row + r, above_row[r - 1],
                     x0 + 2 * bs - frame_width);
        } else {
          vpx_memcpy(above_row, above_ref, r);
          vpx_memset(above_row + r, above_row[r - 1],
                     x0 + 2 * bs - frame_width);
        }
      }
      above_row[-1] = left_available ? above_ref[-1] : 129;
    } else {
      /* faster path if the block does not need extension */
      if (bs == 4 && right_available && left_available) {
        const_above_row = above_ref;
      } else {
        vpx_memcpy(above_row, above_ref, bs);
        if (bs == 4 && right_available)
          vpx_memcpy(above_row + bs, above_ref + bs, bs);
        else
          vpx_memset(above_row + bs, above_row[bs - 1], bs);
        above_row[-1] = left_available ? above_ref[-1] : 129;
      }
    }
  } else {
    vpx_memset(above_row, 127, bs * 2);
    above_row[-1] = 127;
  }

  // predict
  if (mode == DC_PRED) {
    dc_pred[left_available][up_available][tx_size](dst, dst_stride,
                                                   const_above_row, left_col);
  } else {
    pred[mode][tx_size](dst, dst_stride, const_above_row, left_col);
  }
}
#if CONFIG_FILTERINTRA
static void filter_intra_predictors_4tap(uint8_t *ypred_ptr, int y_stride,
                                         int bs,
                                         const uint8_t *yabove_row,
                                         const uint8_t *yleft_col,
                                         int mode) {
  static const int prec_bits = 10;
  static const int round_val = 511;

  int k, r, c;
  int pred[33][33];
  int mean, ipred;

  int taps4_4[10][4] = {
      {735, 881, -537, -54},
      {1005, 519, -488, -11},
      {383, 990, -343, -6},
      {442, 805, -542, 319},
      {658, 616, -133, -116},
      {875, 442, -141, -151},
      {386, 741, -23, -80},
      {390, 1027, -446, 51},
      {679, 606, -523, 262},
      {903, 922, -778, -23}
  };
  int taps4_8[10][4] = {
      {648, 803, -444, 16},
      {972, 620, -576, 7},
      {561, 967, -499, -5},
      {585, 762, -468, 144},
      {596, 619, -182, -9},
      {895, 459, -176, -153},
      {557, 722, -126, -129},
      {601, 839, -523, 105},
      {562, 709, -499, 251},
      {803, 872, -695, 43}
  };
  int taps4_16[10][4] = {
      {423, 728, -347, 111},
      {963, 685, -665, 23},
      {281, 1024, -480, 216},
      {640, 596, -437, 78},
      {429, 669, -259, 99},
      {740, 646, -415, 23},
      {568, 771, -346, 40},
      {404, 833, -486, 209},
      {398, 712, -423, 307},
      {939, 935, -887, 17}
  };
  int taps4_32[10][4] = {
      {477, 737, -393, 150},
      {881, 630, -546, 67},
      {506, 984, -443, -20},
      {114, 459, -270, 528},
      {433, 528, 14, 3},
      {837, 470, -301, -30},
      {181, 777, 89, -107},
      {-29, 716, -232, 259},
      {589, 646, -495, 255},
      {740, 884, -728, 77}
  };

  const int c1 = (bs >= 32) ? taps4_32[mode][0] : ((bs >= 16) ?
      taps4_16[mode][0] : ((bs >= 8) ? taps4_8[mode][0] : taps4_4[mode][0]));
  const int c2 = (bs >= 32) ? taps4_32[mode][1] : ((bs >= 16) ?
      taps4_16[mode][1] : ((bs >= 8) ? taps4_8[mode][1] : taps4_4[mode][1]));
  const int c3 = (bs >= 32) ? taps4_32[mode][2] : ((bs >= 16) ?
      taps4_16[mode][2] : ((bs >= 8) ? taps4_8[mode][2] : taps4_4[mode][2]));
  const int c4 = (bs >= 32) ? taps4_32[mode][3] : ((bs >= 16) ?
      taps4_16[mode][3] : ((bs >= 8) ? taps4_8[mode][3] : taps4_4[mode][3]));

  k = 0;
  mean = 0;
  while (k < bs) {
    mean = mean + (int)yleft_col[k];
    mean = mean + (int)yabove_row[k];
    k++;
  }
  mean = (mean + bs) / (2 * bs);

  for (r = 0; r < bs; r++)
    pred[r + 1][0] = (int)yleft_col[r] - mean;

  for (c = 0; c < 2 * bs + 1; c++)
    pred[0][c] = (int)yabove_row[c - 1] - mean;

  for (r = 1; r < bs + 1; r++)
    for (c = 1; c < 2 * bs + 1 - r; c++) {
      ipred = c1 * pred[r - 1][c] + c2 * pred[r][c - 1]
                    + c3 * pred[r - 1][c - 1] + c4 * pred[r - 1][c + 1];
      pred[r][c] = ipred < 0 ? -((-ipred + round_val) >> prec_bits) :
                               ((ipred + round_val) >> prec_bits);
    }

  for (r = 0; r < bs; r++) {
    for (c = 0; c < bs; c++) {
      ipred = pred[r + 1][c + 1] + mean;
      ypred_ptr[c] = clip_pixel(ipred);
    }
    ypred_ptr += y_stride;
  }
}

static void build_filter_intra_predictors(const MACROBLOCKD *xd,
                                          const uint8_t *ref, int ref_stride,
                                          uint8_t *dst, int dst_stride,
                                          PREDICTION_MODE mode, TX_SIZE tx_size,
                                          int up_available, int left_available,
                                          int right_available, int x, int y,
                                          int plane) {
  int i;
  DECLARE_ALIGNED_ARRAY(16, uint8_t, left_col, 64);
  DECLARE_ALIGNED_ARRAY(16, uint8_t, above_data, 128 + 16);
  uint8_t *above_row = above_data + 16;
  const uint8_t *const_above_row = above_row;
  const int bs = 4 << tx_size;
  int frame_width, frame_height;
  int x0, y0;
  const struct macroblockd_plane *const pd = &xd->plane[plane];

  // Get current frame pointer, width and height.
  if (plane == 0) {
    frame_width = xd->cur_buf->y_width;
    frame_height = xd->cur_buf->y_height;
  } else {
    frame_width = xd->cur_buf->uv_width;
    frame_height = xd->cur_buf->uv_height;
  }

  // Get block position in current frame.
  x0 = (-xd->mb_to_left_edge >> (3 + pd->subsampling_x)) + x;
  y0 = (-xd->mb_to_top_edge >> (3 + pd->subsampling_y)) + y;

  vpx_memset(left_col, 129, 64);

  // left
  if (left_available) {
    if (xd->mb_to_bottom_edge < 0) {
      /* slower path if the block needs border extension */
      if (y0 + bs <= frame_height) {
        for (i = 0; i < bs; ++i)
          left_col[i] = ref[i * ref_stride - 1];
      } else {
        const int extend_bottom = frame_height - y0;
        for (i = 0; i < extend_bottom; ++i)
          left_col[i] = ref[i * ref_stride - 1];
        for (; i < bs; ++i)
          left_col[i] = ref[(extend_bottom - 1) * ref_stride - 1];
      }
    } else {
      /* faster path if the block does not need extension */
      for (i = 0; i < bs; ++i)
        left_col[i] = ref[i * ref_stride - 1];
    }
  }

  // TODO(hkuang) do not extend 2*bs pixels for all modes.
  // above
  if (up_available) {
    const uint8_t *above_ref = ref - ref_stride;
    if (xd->mb_to_right_edge < 0) {
      /* slower path if the block needs border extension */
      if (x0 + 2 * bs <= frame_width) {
        if (right_available && bs == 4) {
          vpx_memcpy(above_row, above_ref, 2 * bs);
        } else {
          vpx_memcpy(above_row, above_ref, bs);
          vpx_memset(above_row + bs, above_row[bs - 1], bs);
        }
      } else if (x0 + bs <= frame_width) {
        const int r = frame_width - x0;
        if (right_available && bs == 4) {
          vpx_memcpy(above_row, above_ref, r);
          vpx_memset(above_row + r, above_row[r - 1],
                     x0 + 2 * bs - frame_width);
        } else {
          vpx_memcpy(above_row, above_ref, bs);
          vpx_memset(above_row + bs, above_row[bs - 1], bs);
        }
      } else if (x0 <= frame_width) {
        const int r = frame_width - x0;
        if (right_available && bs == 4) {
          vpx_memcpy(above_row, above_ref, r);
          vpx_memset(above_row + r, above_row[r - 1],
                     x0 + 2 * bs - frame_width);
        } else {
          vpx_memcpy(above_row, above_ref, r);
          vpx_memset(above_row + r, above_row[r - 1],
                     x0 + 2 * bs - frame_width);
        }
      }
      above_row[-1] = left_available ? above_ref[-1] : 129;
    } else {
      /* faster path if the block does not need extension */
      if (bs == 4 && right_available && left_available) {
        const_above_row = above_ref;
      } else {
        vpx_memcpy(above_row, above_ref, bs);
        if (bs == 4 && right_available)
          vpx_memcpy(above_row + bs, above_ref + bs, bs);
        else
          vpx_memset(above_row + bs, above_row[bs - 1], bs);
        above_row[-1] = left_available ? above_ref[-1] : 129;
      }
    }
  } else {
    vpx_memset(above_row, 127, bs * 2);
    above_row[-1] = 127;
  }

  // predict
  filter_intra_predictors_4tap(dst, dst_stride, bs, const_above_row, left_col,
                               mode);
}
#endif


void vp9_predict_intra_block(const MACROBLOCKD *xd, int block_idx, int bwl_in,
                             TX_SIZE tx_size, PREDICTION_MODE mode,
#if CONFIG_FILTERINTRA
                             int filterbit,
#endif
                            const uint8_t *ref, int ref_stride,
                             uint8_t *dst, int dst_stride,
                             int aoff, int loff, int plane) {
  const int bwl = bwl_in - tx_size;
  const int wmask = (1 << bwl) - 1;
  const int have_top = (block_idx >> bwl) || xd->up_available;
  const int have_left = (block_idx & wmask) || xd->left_available;
  const int have_right = ((block_idx & wmask) != wmask);
  const int x = aoff * 4;
  const int y = loff * 4;
#if CONFIG_FILTERINTRA
  const int filterflag = is_filter_allowed(mode) && is_filter_enabled(tx_size)
                         && filterbit;
#endif

  assert(bwl >= 0);
#if CONFIG_VP9_HIGHBITDEPTH
  if (xd->cur_buf->flags & YV12_FLAG_HIGHBITDEPTH) {
    build_intra_predictors_high(xd, ref, ref_stride, dst, dst_stride, mode,
                                tx_size, have_top, have_left, have_right,
                                x, y, plane, xd->bd);
    return;
  }
#endif
#if CONFIG_FILTERINTRA
  if (!filterflag) {
#endif
  build_intra_predictors(xd, ref, ref_stride, dst, dst_stride, mode, tx_size,
                         have_top, have_left, have_right, x, y, plane);
#if CONFIG_FILTERINTRA
  } else {
    build_filter_intra_predictors(xd, ref, ref_stride, dst, dst_stride, mode,
                        tx_size, have_top, have_left, have_right, x, y, plane);
  }
#endif
}
