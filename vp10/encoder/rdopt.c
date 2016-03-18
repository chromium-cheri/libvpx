/*
 *  Copyright (c) 2010 The WebM project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include <assert.h>
#include <math.h>

#include "./vp10_rtcd.h"
#include "./vpx_dsp_rtcd.h"

#include "vpx_dsp/vpx_dsp_common.h"
#include "vpx_mem/vpx_mem.h"
#include "vpx_ports/mem.h"
#include "vpx_ports/system_state.h"

#include "vp10/common/common.h"
#include "vp10/common/entropy.h"
#include "vp10/common/entropymode.h"
#include "vp10/common/idct.h"
#include "vp10/common/mvref_common.h"
#include "vp10/common/pred_common.h"
#include "vp10/common/quant_common.h"
#include "vp10/common/reconinter.h"
#include "vp10/common/reconintra.h"
#include "vp10/common/scan.h"
#include "vp10/common/seg_common.h"

#include "vp10/encoder/cost.h"
#include "vp10/encoder/encodemb.h"
#include "vp10/encoder/encodemv.h"
#include "vp10/encoder/encoder.h"
#include "vp10/encoder/hybrid_fwd_txfm.h"
#include "vp10/encoder/mcomp.h"
#include "vp10/encoder/palette.h"
#include "vp10/encoder/quantize.h"
#include "vp10/encoder/ratectrl.h"
#include "vp10/encoder/rd.h"
#include "vp10/encoder/rdopt.h"
#include "vp10/encoder/aq_variance.h"

#if CONFIG_EXT_REFS

#define LAST_FRAME_MODE_MASK    ((1 << GOLDEN_FRAME) | (1 << ALTREF_FRAME) | \
                                 (1 << LAST2_FRAME) | (1 << INTRA_FRAME) | \
                                 (1 << LAST3_FRAME) | (1 << LAST4_FRAME))
#define LAST2_FRAME_MODE_MASK   ((1 << GOLDEN_FRAME) | (1 << ALTREF_FRAME) | \
                                 (1 << LAST_FRAME) | (1 << INTRA_FRAME) | \
                                 (1 << LAST3_FRAME) | (1 << LAST4_FRAME))
#define LAST3_FRAME_MODE_MASK   ((1 << GOLDEN_FRAME) | (1 << ALTREF_FRAME) | \
                                 (1 << LAST_FRAME) | (1 << INTRA_FRAME) | \
                                 (1 << LAST2_FRAME) | (1 << LAST4_FRAME))
#define LAST4_FRAME_MODE_MASK   ((1 << GOLDEN_FRAME) | (1 << ALTREF_FRAME) | \
                                 (1 << LAST_FRAME) | (1 << INTRA_FRAME) | \
                                 (1 << LAST2_FRAME) | (1 << LAST3_FRAME))
#define GOLDEN_FRAME_MODE_MASK  ((1 << LAST_FRAME) | (1 << ALTREF_FRAME) | \
                                 (1 << LAST2_FRAME) | (1 << INTRA_FRAME) | \
                                 (1 << LAST3_FRAME) | (1 << LAST4_FRAME))
#define ALT_REF_MODE_MASK       ((1 << LAST_FRAME) | (1 << GOLDEN_FRAME) | \
                                 (1 << LAST2_FRAME) | (1 << INTRA_FRAME) | \
                                 (1 << LAST3_FRAME) | (1 << LAST4_FRAME))

#else

#define LAST_FRAME_MODE_MASK    ((1 << GOLDEN_FRAME) | (1 << ALTREF_FRAME) | \
                                 (1 << INTRA_FRAME))
#define GOLDEN_FRAME_MODE_MASK  ((1 << LAST_FRAME) | (1 << ALTREF_FRAME) | \
                                 (1 << INTRA_FRAME))
#define ALT_REF_MODE_MASK       ((1 << LAST_FRAME) | (1 << GOLDEN_FRAME) | \
                                 (1 << INTRA_FRAME))

#endif  // CONFIG_EXT_REFS

#define SECOND_REF_FRAME_MASK   ((1 << ALTREF_FRAME) | 0x01)

#define MIN_EARLY_TERM_INDEX    3
#define NEW_MV_DISCOUNT_FACTOR  8

#if CONFIG_EXT_TX
const double ext_tx_th = 0.99;
#else
const double ext_tx_th = 0.99;
#endif

const double ADST_FLIP_SVM[8] = {-7.3283, -3.0450, -3.2450, 3.6403,  // vert
                                 -9.4204, -3.1821, -4.6851, 4.1469};  // horz

typedef struct {
  PREDICTION_MODE mode;
  MV_REFERENCE_FRAME ref_frame[2];
} MODE_DEFINITION;

typedef struct {
  MV_REFERENCE_FRAME ref_frame[2];
} REF_DEFINITION;

struct rdcost_block_args {
  const VP10_COMP *cpi;
  MACROBLOCK *x;
  ENTROPY_CONTEXT t_above[16];
  ENTROPY_CONTEXT t_left[16];
  int this_rate;
  int64_t this_dist;
  int64_t this_sse;
  int64_t this_rd;
  int64_t best_rd;
  int exit_early;
  int use_fast_coef_costing;
  const scan_order *so;
  uint8_t skippable;
};

#define LAST_NEW_MV_INDEX 6
static const MODE_DEFINITION vp10_mode_order[MAX_MODES] = {
  {NEARESTMV, {LAST_FRAME,   NONE}},
#if CONFIG_EXT_REFS
  {NEARESTMV, {LAST2_FRAME,  NONE}},
  {NEARESTMV, {LAST3_FRAME,  NONE}},
  {NEARESTMV, {LAST4_FRAME,  NONE}},
#endif  // CONFIG_EXT_REFS
  {NEARESTMV, {ALTREF_FRAME, NONE}},
  {NEARESTMV, {GOLDEN_FRAME, NONE}},

  {DC_PRED,   {INTRA_FRAME,  NONE}},

  {NEWMV,     {LAST_FRAME,   NONE}},
#if CONFIG_EXT_REFS
  {NEWMV,     {LAST2_FRAME,  NONE}},
  {NEWMV,     {LAST3_FRAME,  NONE}},
  {NEWMV,     {LAST4_FRAME,  NONE}},
#endif  // CONFIG_EXT_REFS
  {NEWMV,     {ALTREF_FRAME, NONE}},
  {NEWMV,     {GOLDEN_FRAME, NONE}},

  {NEARMV,    {LAST_FRAME,   NONE}},
#if CONFIG_EXT_REFS
  {NEARMV,    {LAST2_FRAME,  NONE}},
  {NEARMV,    {LAST3_FRAME,  NONE}},
  {NEARMV,    {LAST4_FRAME,  NONE}},
#endif  // CONFIG_EXT_REFS
  {NEARMV,    {ALTREF_FRAME, NONE}},
  {NEARMV,    {GOLDEN_FRAME, NONE}},

#if CONFIG_EXT_INTER
  {NEWFROMNEARMV,    {LAST_FRAME,   NONE}},
#if CONFIG_EXT_REFS
  {NEWFROMNEARMV,    {LAST2_FRAME,  NONE}},
  {NEWFROMNEARMV,    {LAST3_FRAME,  NONE}},
  {NEWFROMNEARMV,    {LAST4_FRAME,  NONE}},
#endif  // CONFIG_EXT_REFS
  {NEWFROMNEARMV,    {ALTREF_FRAME, NONE}},
  {NEWFROMNEARMV,    {GOLDEN_FRAME, NONE}},
#endif  // CONFIG_EXT_INTER

  {ZEROMV,    {LAST_FRAME,   NONE}},
#if CONFIG_EXT_REFS
  {ZEROMV,    {LAST2_FRAME,  NONE}},
  {ZEROMV,    {LAST3_FRAME,  NONE}},
  {ZEROMV,    {LAST4_FRAME,  NONE}},
#endif  // CONFIG_EXT_REFS
  {ZEROMV,    {GOLDEN_FRAME, NONE}},
  {ZEROMV,    {ALTREF_FRAME, NONE}},

#if CONFIG_EXT_INTER
  {NEAREST_NEARESTMV, {LAST_FRAME,   ALTREF_FRAME}},
#if CONFIG_EXT_REFS
  {NEAREST_NEARESTMV, {LAST2_FRAME,  ALTREF_FRAME}},
  {NEAREST_NEARESTMV, {LAST3_FRAME,  ALTREF_FRAME}},
  {NEAREST_NEARESTMV, {LAST4_FRAME,  ALTREF_FRAME}},
#endif  // CONFIG_EXT_REFS
  {NEAREST_NEARESTMV, {GOLDEN_FRAME, ALTREF_FRAME}},
#else  // CONFIG_EXT_INTER
  {NEARESTMV, {LAST_FRAME,   ALTREF_FRAME}},
#if CONFIG_EXT_REFS
  {NEARESTMV, {LAST2_FRAME,  ALTREF_FRAME}},
  {NEARESTMV, {LAST3_FRAME,  ALTREF_FRAME}},
  {NEARESTMV, {LAST4_FRAME,  ALTREF_FRAME}},
#endif  // CONFIG_EXT_REFS
  {NEARESTMV, {GOLDEN_FRAME, ALTREF_FRAME}},
#endif  // CONFIG_EXT_INTER

  {TM_PRED,   {INTRA_FRAME,  NONE}},

#if CONFIG_EXT_INTER
  {NEAR_NEARESTMV, {LAST_FRAME,   ALTREF_FRAME}},
  {NEAR_NEARESTMV, {GOLDEN_FRAME, ALTREF_FRAME}},
  {NEAREST_NEARMV, {LAST_FRAME,   ALTREF_FRAME}},
  {NEAREST_NEARMV, {GOLDEN_FRAME, ALTREF_FRAME}},
  {NEW_NEARESTMV, {LAST_FRAME,   ALTREF_FRAME}},
  {NEW_NEARESTMV, {GOLDEN_FRAME, ALTREF_FRAME}},
  {NEAREST_NEWMV, {LAST_FRAME,   ALTREF_FRAME}},
  {NEAREST_NEWMV, {GOLDEN_FRAME, ALTREF_FRAME}},
  {NEW_NEARMV, {LAST_FRAME,   ALTREF_FRAME}},
  {NEW_NEARMV, {GOLDEN_FRAME, ALTREF_FRAME}},
  {NEAR_NEWMV, {LAST_FRAME,   ALTREF_FRAME}},
  {NEAR_NEWMV, {GOLDEN_FRAME, ALTREF_FRAME}},
  {NEW_NEWMV,      {LAST_FRAME,   ALTREF_FRAME}},
  {NEW_NEWMV,      {GOLDEN_FRAME, ALTREF_FRAME}},
  {ZERO_ZEROMV,    {LAST_FRAME,   ALTREF_FRAME}},
  {ZERO_ZEROMV,    {GOLDEN_FRAME, ALTREF_FRAME}},
#if CONFIG_EXT_REFS
  {NEAR_NEARESTMV, {LAST2_FRAME,  ALTREF_FRAME}},
  {NEAREST_NEARMV, {LAST2_FRAME,  ALTREF_FRAME}},
  {NEW_NEARESTMV,  {LAST2_FRAME,  ALTREF_FRAME}},
  {NEAREST_NEWMV,  {LAST2_FRAME,  ALTREF_FRAME}},
  {NEW_NEARMV,     {LAST2_FRAME,  ALTREF_FRAME}},
  {NEAR_NEWMV,     {LAST2_FRAME,  ALTREF_FRAME}},
  {NEW_NEWMV,      {LAST2_FRAME,  ALTREF_FRAME}},
  {ZERO_ZEROMV,    {LAST2_FRAME,  ALTREF_FRAME}},

  {NEAR_NEARESTMV, {LAST3_FRAME,  ALTREF_FRAME}},
  {NEAREST_NEARMV, {LAST3_FRAME,  ALTREF_FRAME}},
  {NEW_NEARESTMV,  {LAST3_FRAME,  ALTREF_FRAME}},
  {NEAREST_NEWMV,  {LAST3_FRAME,  ALTREF_FRAME}},
  {NEW_NEARMV,     {LAST3_FRAME,  ALTREF_FRAME}},
  {NEAR_NEWMV,     {LAST3_FRAME,  ALTREF_FRAME}},
  {NEW_NEWMV,      {LAST3_FRAME,  ALTREF_FRAME}},
  {ZERO_ZEROMV,    {LAST3_FRAME,  ALTREF_FRAME}},

  {NEAR_NEARESTMV, {LAST4_FRAME,  ALTREF_FRAME}},
  {NEAREST_NEARMV, {LAST4_FRAME,  ALTREF_FRAME}},
  {NEW_NEARESTMV,  {LAST4_FRAME,  ALTREF_FRAME}},
  {NEAREST_NEWMV,  {LAST4_FRAME,  ALTREF_FRAME}},
  {NEW_NEARMV,     {LAST4_FRAME,  ALTREF_FRAME}},
  {NEAR_NEWMV,     {LAST4_FRAME,  ALTREF_FRAME}},
  {NEW_NEWMV,      {LAST4_FRAME,  ALTREF_FRAME}},
  {ZERO_ZEROMV,    {LAST4_FRAME,  ALTREF_FRAME}},
#endif  // CONFIG_EXT_REFS
#else
  {NEARMV,    {LAST_FRAME,   ALTREF_FRAME}},
  {NEWMV,     {LAST_FRAME,   ALTREF_FRAME}},
#if CONFIG_EXT_REFS
  {NEARMV,    {LAST2_FRAME,  ALTREF_FRAME}},
  {NEWMV,     {LAST2_FRAME,  ALTREF_FRAME}},
  {NEARMV,    {LAST3_FRAME,  ALTREF_FRAME}},
  {NEWMV,     {LAST3_FRAME,  ALTREF_FRAME}},
  {NEARMV,    {LAST4_FRAME,  ALTREF_FRAME}},
  {NEWMV,     {LAST4_FRAME,  ALTREF_FRAME}},
#endif  // CONFIG_EXT_REFS
  {NEARMV,    {GOLDEN_FRAME, ALTREF_FRAME}},
  {NEWMV,     {GOLDEN_FRAME, ALTREF_FRAME}},

  {ZEROMV,    {LAST_FRAME,   ALTREF_FRAME}},
#if CONFIG_EXT_REFS
  {ZEROMV,    {LAST3_FRAME,  ALTREF_FRAME}},
  {ZEROMV,    {LAST2_FRAME,  ALTREF_FRAME}},
  {ZEROMV,    {LAST4_FRAME,  ALTREF_FRAME}},
#endif  // CONFIG_EXT_REFS
  {ZEROMV,    {GOLDEN_FRAME, ALTREF_FRAME}},
#endif  // CONFIG_EXT_INTER

  {H_PRED,    {INTRA_FRAME,  NONE}},
  {V_PRED,    {INTRA_FRAME,  NONE}},
  {D135_PRED, {INTRA_FRAME,  NONE}},
  {D207_PRED, {INTRA_FRAME,  NONE}},
  {D153_PRED, {INTRA_FRAME,  NONE}},
  {D63_PRED,  {INTRA_FRAME,  NONE}},
  {D117_PRED, {INTRA_FRAME,  NONE}},
  {D45_PRED,  {INTRA_FRAME,  NONE}},

#if CONFIG_EXT_INTER
  {ZEROMV,    {LAST_FRAME,   INTRA_FRAME}},
  {NEARESTMV, {LAST_FRAME,   INTRA_FRAME}},
  {NEARMV,    {LAST_FRAME,   INTRA_FRAME}},
  {NEWMV,     {LAST_FRAME,   INTRA_FRAME}},

#if CONFIG_EXT_REFS
  {ZEROMV,    {LAST2_FRAME,  INTRA_FRAME}},
  {NEARESTMV, {LAST2_FRAME,  INTRA_FRAME}},
  {NEARMV,    {LAST2_FRAME,  INTRA_FRAME}},
  {NEWMV,     {LAST2_FRAME,  INTRA_FRAME}},

  {ZEROMV,    {LAST3_FRAME,  INTRA_FRAME}},
  {NEARESTMV, {LAST3_FRAME,  INTRA_FRAME}},
  {NEARMV,    {LAST3_FRAME,  INTRA_FRAME}},
  {NEWMV,     {LAST3_FRAME,  INTRA_FRAME}},

  {ZEROMV,    {LAST4_FRAME,  INTRA_FRAME}},
  {NEARESTMV, {LAST4_FRAME,  INTRA_FRAME}},
  {NEARMV,    {LAST4_FRAME,  INTRA_FRAME}},
  {NEWMV,     {LAST4_FRAME,  INTRA_FRAME}},
#endif  // CONFIG_EXT_REFS

  {ZEROMV,    {GOLDEN_FRAME, INTRA_FRAME}},
  {NEARESTMV, {GOLDEN_FRAME, INTRA_FRAME}},
  {NEARMV,    {GOLDEN_FRAME, INTRA_FRAME}},
  {NEWMV,     {GOLDEN_FRAME, INTRA_FRAME}},

  {ZEROMV,    {ALTREF_FRAME, INTRA_FRAME}},
  {NEARESTMV, {ALTREF_FRAME, INTRA_FRAME}},
  {NEARMV,    {ALTREF_FRAME, INTRA_FRAME}},
  {NEWMV,     {ALTREF_FRAME, INTRA_FRAME}},
#endif  // CONFIG_EXT_INTER
};

static const REF_DEFINITION vp10_ref_order[MAX_REFS] = {
  {{LAST_FRAME,   NONE}},
#if CONFIG_EXT_REFS
  {{LAST2_FRAME,  NONE}},
  {{LAST3_FRAME,  NONE}},
  {{LAST4_FRAME,  NONE}},
#endif  // CONFIG_EXT_REFS
  {{GOLDEN_FRAME, NONE}},
  {{ALTREF_FRAME, NONE}},
  {{LAST_FRAME,   ALTREF_FRAME}},
#if CONFIG_EXT_REFS
  {{LAST2_FRAME,  ALTREF_FRAME}},
  {{LAST3_FRAME,  ALTREF_FRAME}},
  {{LAST4_FRAME,  ALTREF_FRAME}},
#endif  // CONFIG_EXT_REFS
  {{GOLDEN_FRAME, ALTREF_FRAME}},
  {{INTRA_FRAME,  NONE}},
};

static INLINE int write_uniform_cost(int n, int v) {
  int l = get_unsigned_bits(n), m = (1 << l) - n;
  if (l == 0)
    return 0;
  if (v < m)
    return (l - 1) * vp10_cost_bit(128, 0);
  else
    return l * vp10_cost_bit(128, 0);
}

static void swap_block_ptr(MACROBLOCK *x, PICK_MODE_CONTEXT *ctx,
                           int m, int n, int min_plane, int max_plane) {
  int i;

  for (i = min_plane; i < max_plane; ++i) {
    struct macroblock_plane *const p = &x->plane[i];
    struct macroblockd_plane *const pd = &x->e_mbd.plane[i];

    p->coeff    = ctx->coeff_pbuf[i][m];
    p->qcoeff   = ctx->qcoeff_pbuf[i][m];
    pd->dqcoeff = ctx->dqcoeff_pbuf[i][m];
    p->eobs     = ctx->eobs_pbuf[i][m];

    ctx->coeff_pbuf[i][m]   = ctx->coeff_pbuf[i][n];
    ctx->qcoeff_pbuf[i][m]  = ctx->qcoeff_pbuf[i][n];
    ctx->dqcoeff_pbuf[i][m] = ctx->dqcoeff_pbuf[i][n];
    ctx->eobs_pbuf[i][m]    = ctx->eobs_pbuf[i][n];

    ctx->coeff_pbuf[i][n]   = p->coeff;
    ctx->qcoeff_pbuf[i][n]  = p->qcoeff;
    ctx->dqcoeff_pbuf[i][n] = pd->dqcoeff;
    ctx->eobs_pbuf[i][n]    = p->eobs;
  }
}

// constants for prune 1 and prune 2 decision boundaries
#define FAST_EXT_TX_CORR_MID 0.0
#define FAST_EXT_TX_EDST_MID 0.1
#define FAST_EXT_TX_CORR_MARGIN 0.5
#define FAST_EXT_TX_EDST_MARGIN 0.05

typedef enum {
  DCT_1D = 0,
  ADST_1D = 1,
  FLIPADST_1D = 2,
  DST_1D = 3,
  TX_TYPES_1D = 4,
} TX_TYPE_1D;

static void get_energy_distribution_fine(const VP10_COMP *cpi,
                                         BLOCK_SIZE bsize,
                                         uint8_t *src, int src_stride,
                                         uint8_t *dst, int dst_stride,
                                         double *hordist, double *verdist) {
  int bw = 4 << (b_width_log2_lookup[bsize]);
  int bh = 4 << (b_height_log2_lookup[bsize]);
  unsigned int esq[16] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
  unsigned int var[16];
  double total = 0;
  const int f_index = bsize - 6;

  if (f_index < 0) {
    int i, j, index;
    int w_shift = bw == 8 ? 1 : 2;
    int h_shift = bh == 8 ? 1 : 2;
#if CONFIG_VP9_HIGHBITDEPTH
    if (cpi->common.use_highbitdepth) {
      uint16_t *src16 = CONVERT_TO_SHORTPTR(src);
      uint16_t *dst16 = CONVERT_TO_SHORTPTR(dst);
      for (i = 0; i < bh; ++i)
        for (j = 0; j < bw; ++j) {
          index = (j >> w_shift) + ((i >> h_shift) << 2);
          esq[index] += (src16[j + i * src_stride] -
                        dst16[j + i * dst_stride]) *
                        (src16[j + i * src_stride] -
                        dst16[j + i * dst_stride]);
        }
    } else {
#endif  // CONFIG_VP9_HIGHBITDEPTH

      for (i = 0; i < bh; ++i)
        for (j = 0; j < bw; ++j) {
          index = (j >> w_shift) + ((i >> h_shift) << 2);
          esq[index] += (src[j + i * src_stride] - dst[j + i * dst_stride]) *
                        (src[j + i * src_stride] - dst[j + i * dst_stride]);
        }
#if CONFIG_VP9_HIGHBITDEPTH
    }
#endif  // CONFIG_VP9_HIGHBITDEPTH
  } else {
    var[0] = cpi->fn_ptr[f_index].vf(src, src_stride,
                                     dst, dst_stride, &esq[0]);
    var[1] = cpi->fn_ptr[f_index].vf(src + bw / 4, src_stride,
                                     dst + bw / 4, dst_stride, &esq[1]);
    var[2] = cpi->fn_ptr[f_index].vf(src + bw / 2, src_stride,
                                     dst + bw / 2, dst_stride, &esq[2]);
    var[3] = cpi->fn_ptr[f_index].vf(src + 3 * bw / 4, src_stride,
                                     dst + 3 * bw / 4, dst_stride, &esq[3]);
    src += bh / 4 * src_stride;
    dst += bh / 4 * dst_stride;

    var[4] = cpi->fn_ptr[f_index].vf(src, src_stride,
                                     dst, dst_stride, &esq[4]);
    var[5] = cpi->fn_ptr[f_index].vf(src + bw / 4, src_stride,
                                     dst + bw / 4, dst_stride, &esq[5]);
    var[6] = cpi->fn_ptr[f_index].vf(src + bw / 2, src_stride,
                                     dst + bw / 2, dst_stride, &esq[6]);
    var[7] = cpi->fn_ptr[f_index].vf(src + 3 * bw / 4, src_stride,
                                     dst + 3 * bw / 4, dst_stride, &esq[7]);
    src += bh / 4 * src_stride;
    dst += bh / 4 * dst_stride;

    var[8] = cpi->fn_ptr[f_index].vf(src, src_stride,
                                     dst, dst_stride, &esq[8]);
    var[9] = cpi->fn_ptr[f_index].vf(src + bw / 4, src_stride,
                                     dst + bw / 4, dst_stride, &esq[9]);
    var[10] = cpi->fn_ptr[f_index].vf(src + bw / 2, src_stride,
                                      dst + bw / 2, dst_stride, &esq[10]);
    var[11] = cpi->fn_ptr[f_index].vf(src + 3 * bw / 4, src_stride,
                                      dst + 3 * bw / 4, dst_stride, &esq[11]);
    src += bh / 4 * src_stride;
    dst += bh / 4 * dst_stride;

    var[12] = cpi->fn_ptr[f_index].vf(src, src_stride,
                                      dst, dst_stride, &esq[12]);
    var[13] = cpi->fn_ptr[f_index].vf(src + bw / 4, src_stride,
                                      dst + bw / 4, dst_stride, &esq[13]);
    var[14] = cpi->fn_ptr[f_index].vf(src + bw / 2, src_stride,
                                      dst + bw / 2, dst_stride, &esq[14]);
    var[15] = cpi->fn_ptr[f_index].vf(src + 3 * bw / 4, src_stride,
                                      dst + 3 * bw / 4, dst_stride, &esq[15]);
  }

  total = esq[0] + esq[1] + esq[2] + esq[3] +
          esq[4] + esq[5] + esq[6] + esq[7] +
          esq[8] + esq[9] + esq[10] + esq[11] +
          esq[12] + esq[13] + esq[14] + esq[15];
  if (total > 0) {
    const double e_recip = 1.0 / total;
    hordist[0] = ((double)esq[0] + (double)esq[4] + (double)esq[8] +
                  (double)esq[12]) * e_recip;
    hordist[1] = ((double)esq[1] + (double)esq[5] + (double)esq[9] +
                  (double)esq[13]) * e_recip;
    hordist[2] = ((double)esq[2] + (double)esq[6] + (double)esq[10] +
                  (double)esq[14]) * e_recip;
    verdist[0] = ((double)esq[0] + (double)esq[1] + (double)esq[2] +
                  (double)esq[3]) * e_recip;
    verdist[1] = ((double)esq[4] + (double)esq[5] + (double)esq[6] +
                  (double)esq[7]) * e_recip;
    verdist[2] = ((double)esq[8] + (double)esq[9] + (double)esq[10] +
                  (double)esq[11]) * e_recip;
  } else {
    hordist[0] = verdist[0] = 0.25;
    hordist[1] = verdist[1] = 0.25;
    hordist[2] = verdist[2] = 0.25;
  }
  (void) var[0];
  (void) var[1];
  (void) var[2];
  (void) var[3];
  (void) var[4];
  (void) var[5];
  (void) var[6];
  (void) var[7];
  (void) var[8];
  (void) var[9];
  (void) var[10];
  (void) var[11];
  (void) var[12];
  (void) var[13];
  (void) var[14];
  (void) var[15];
}

int adst_vs_flipadst(const VP10_COMP *cpi,
                     BLOCK_SIZE bsize,
                     uint8_t *src, int src_stride,
                     uint8_t *dst, int dst_stride,
                     double *hdist, double *vdist) {
  int prune_bitmask = 0;
  double svm_proj_h = 0, svm_proj_v = 0;
  get_energy_distribution_fine(cpi, bsize, src, src_stride,
                               dst, dst_stride, hdist, vdist);



  svm_proj_v = vdist[0] * ADST_FLIP_SVM[0] +
               vdist[1] * ADST_FLIP_SVM[1] +
               vdist[2] * ADST_FLIP_SVM[2] + ADST_FLIP_SVM[3];
  svm_proj_h = hdist[0] * ADST_FLIP_SVM[4] +
               hdist[1] * ADST_FLIP_SVM[5] +
               hdist[2] * ADST_FLIP_SVM[6] + ADST_FLIP_SVM[7];
  if (svm_proj_v > FAST_EXT_TX_EDST_MID + FAST_EXT_TX_EDST_MARGIN)
    prune_bitmask |= 1 << FLIPADST_1D;
  else if (svm_proj_v < FAST_EXT_TX_EDST_MID - FAST_EXT_TX_EDST_MARGIN)
    prune_bitmask |= 1 << ADST_1D;

  if (svm_proj_h > FAST_EXT_TX_EDST_MID + FAST_EXT_TX_EDST_MARGIN)
    prune_bitmask |= 1 << (FLIPADST_1D + 8);
  else if (svm_proj_h < FAST_EXT_TX_EDST_MID - FAST_EXT_TX_EDST_MARGIN)
    prune_bitmask |= 1 << (ADST_1D + 8);

  return prune_bitmask;
}

#if CONFIG_EXT_TX
static void get_horver_correlation(int16_t *diff, int stride,
                                   int w, int h,
                                   double *hcorr, double *vcorr) {
  // Returns hor/ver correlation coefficient
  const int num = (h - 1) * (w - 1);
  double num_r;
  int i, j;
  int64_t xy_sum = 0, xz_sum = 0;
  int64_t x_sum = 0, y_sum = 0, z_sum = 0;
  int64_t x2_sum = 0, y2_sum = 0, z2_sum = 0;
  double x_var_n, y_var_n, z_var_n, xy_var_n, xz_var_n;
  *hcorr = *vcorr = 1;

  assert(num > 0);
  num_r = 1.0 / num;
  for (i = 1; i < h; ++i) {
    for (j = 1; j < w; ++j) {
      const int16_t x = diff[i * stride + j];
      const int16_t y = diff[i * stride + j - 1];
      const int16_t z = diff[(i - 1) * stride + j];
      xy_sum += x * y;
      xz_sum += x * z;
      x_sum += x;
      y_sum += y;
      z_sum += z;
      x2_sum += x * x;
      y2_sum += y * y;
      z2_sum += z * z;
    }
  }
  x_var_n =  x2_sum - (x_sum * x_sum) * num_r;
  y_var_n =  y2_sum - (y_sum * y_sum) * num_r;
  z_var_n =  z2_sum - (z_sum * z_sum) * num_r;
  xy_var_n = xy_sum - (x_sum * y_sum) * num_r;
  xz_var_n = xz_sum - (x_sum * z_sum) * num_r;
  if (x_var_n > 0 && y_var_n > 0) {
    *hcorr = xy_var_n / sqrt(x_var_n * y_var_n);
    *hcorr = *hcorr < 0 ? 0 : *hcorr;
  }
  if (x_var_n > 0 && z_var_n > 0) {
    *vcorr = xz_var_n / sqrt(x_var_n * z_var_n);
    *vcorr = *vcorr < 0 ? 0 : *vcorr;
  }
}

int dct_vs_dst(int16_t *diff, int stride, int w, int h,
               double *hcorr, double *vcorr) {
  int prune_bitmask = 0;
  get_horver_correlation(diff, stride, w, h, hcorr, vcorr);

  if (*vcorr > FAST_EXT_TX_CORR_MID + FAST_EXT_TX_CORR_MARGIN)
    prune_bitmask |= 1 << DST_1D;
  else if (*vcorr < FAST_EXT_TX_CORR_MID - FAST_EXT_TX_CORR_MARGIN)
    prune_bitmask |= 1 << DCT_1D;

  if (*hcorr > FAST_EXT_TX_CORR_MID + FAST_EXT_TX_CORR_MARGIN)
    prune_bitmask |= 1 << (DST_1D + 8);
  else if (*hcorr < FAST_EXT_TX_CORR_MID - FAST_EXT_TX_CORR_MARGIN)
    prune_bitmask |= 1 << (DCT_1D + 8);
  return prune_bitmask;
}

// Performance drop: 0.5%, Speed improvement: 24%
static int prune_two_for_sby(const VP10_COMP *cpi,
                             BLOCK_SIZE bsize,
                             MACROBLOCK *x,
                             MACROBLOCKD *xd) {
  struct macroblock_plane *const p = &x->plane[0];
  struct macroblockd_plane *const pd = &xd->plane[0];
  const BLOCK_SIZE bs = get_plane_block_size(bsize, pd);
  const int bw = 4 << (b_width_log2_lookup[bs]);
  const int bh = 4 << (b_height_log2_lookup[bs]);
  double hdist[3] = {0, 0, 0}, vdist[3] = {0, 0, 0};
  double hcorr, vcorr;
  vp10_subtract_plane(x, bsize, 0);
  return adst_vs_flipadst(cpi, bsize, p->src.buf, p->src.stride, pd->dst.buf,
                          pd->dst.stride, hdist, vdist) |
         dct_vs_dst(p->src_diff, bw, bw, bh, &hcorr, &vcorr);
}

static int prune_three_for_sby(const VP10_COMP *cpi,
                               BLOCK_SIZE bsize,
                               MACROBLOCK *x,
                               MACROBLOCKD *xd) {
  (void) cpi;
  (void) bsize;
  (void) x;
  (void) xd;
  return 0;
}

#endif  // CONFIG_EXT_TX

// Performance drop: 0.3%, Speed improvement: 5%
static int prune_one_for_sby(const VP10_COMP *cpi,
                             BLOCK_SIZE bsize,
                             MACROBLOCK *x,
                             MACROBLOCKD *xd) {
  struct macroblock_plane *const p = &x->plane[0];
  struct macroblockd_plane *const pd = &xd->plane[0];
  double hdist[3] = {0, 0, 0}, vdist[3] = {0, 0, 0};
  vp10_subtract_plane(x, bsize, 0);
  return adst_vs_flipadst(cpi, bsize, p->src.buf, p->src.stride, pd->dst.buf,
                          pd->dst.stride, hdist, vdist);
}

static int prune_tx_types(const VP10_COMP *cpi,
                          BLOCK_SIZE bsize,
                          MACROBLOCK *x,
                          MACROBLOCKD *xd) {
  switch (cpi->sf.tx_type_search) {
    case NO_PRUNE:
      return 0;
      break;
    case PRUNE_ONE :
      return prune_one_for_sby(cpi, bsize, x, xd);
      break;
  #if CONFIG_EXT_TX
    case PRUNE_TWO :
      return prune_two_for_sby(cpi, bsize, x, xd);
      break;
    case PRUNE_THREE :
      return prune_three_for_sby(cpi, bsize, x, xd);
      break;
  #endif
  }
  assert(0);
  return 0;
}

static int do_tx_type_search(TX_TYPE tx_type,
                             int prune) {
// TODO(sarahparker) implement for non ext tx
#if CONFIG_EXT_TX
  static TX_TYPE_1D vtx_tab[TX_TYPES] = {
    DCT_1D,
    ADST_1D,
    DCT_1D,
    ADST_1D,
    FLIPADST_1D,
    DCT_1D,
    FLIPADST_1D,
    ADST_1D,
    FLIPADST_1D,
    DST_1D,
    DCT_1D,
    DST_1D,
    ADST_1D,
    DST_1D,
    FLIPADST_1D,
    DST_1D,
  };
  static TX_TYPE_1D htx_tab[TX_TYPES] = {
    DCT_1D,
    DCT_1D,
    ADST_1D,
    ADST_1D,
    DCT_1D,
    FLIPADST_1D,
    FLIPADST_1D,
    FLIPADST_1D,
    ADST_1D,
    DCT_1D,
    DST_1D,
    ADST_1D,
    DST_1D,
    FLIPADST_1D,
    DST_1D,
    DST_1D,
  };
  if (tx_type >= IDTX)
    return 1;
  return !(((prune >> vtx_tab[tx_type]) & 1) |
         ((prune >> (htx_tab[tx_type] + 8)) & 1));
#else
  // temporary to avoid compiler warnings
  (void) tx_type;
  (void) prune;
  return 1;
#endif
}

static void model_rd_for_sb(VP10_COMP *cpi, BLOCK_SIZE bsize,
                            MACROBLOCK *x, MACROBLOCKD *xd,
                            int *out_rate_sum, int64_t *out_dist_sum,
                            int *skip_txfm_sb, int64_t *skip_sse_sb) {
  // Note our transform coeffs are 8 times an orthogonal transform.
  // Hence quantizer step is also 8 times. To get effective quantizer
  // we need to divide by 8 before sending to modeling function.
  int i;
  int64_t rate_sum = 0;
  int64_t dist_sum = 0;
  const int ref = xd->mi[0]->mbmi.ref_frame[0];
  unsigned int sse;
  unsigned int var = 0;
  unsigned int sum_sse = 0;
  int64_t total_sse = 0;
  int skip_flag = 1;
  const int shift = 6;
  int rate;
  int64_t dist;
  const int dequant_shift =
#if CONFIG_VP9_HIGHBITDEPTH
      (xd->cur_buf->flags & YV12_FLAG_HIGHBITDEPTH) ?
          xd->bd - 5 :
#endif  // CONFIG_VP9_HIGHBITDEPTH
          3;

  x->pred_sse[ref] = 0;

  for (i = 0; i < MAX_MB_PLANE; ++i) {
    struct macroblock_plane *const p = &x->plane[i];
    struct macroblockd_plane *const pd = &xd->plane[i];
    const BLOCK_SIZE bs = get_plane_block_size(bsize, pd);
    const TX_SIZE max_tx_size = max_txsize_lookup[bs];
    const BLOCK_SIZE unit_size = txsize_to_bsize[max_tx_size];
    const int64_t dc_thr = p->quant_thred[0] >> shift;
    const int64_t ac_thr = p->quant_thred[1] >> shift;
    // The low thresholds are used to measure if the prediction errors are
    // low enough so that we can skip the mode search.
    const int64_t low_dc_thr = VPXMIN(50, dc_thr >> 2);
    const int64_t low_ac_thr = VPXMIN(80, ac_thr >> 2);
    int bw_shift = (b_width_log2_lookup[bs] - b_width_log2_lookup[unit_size]);
    int bh_shift = (b_height_log2_lookup[bs] - b_width_log2_lookup[unit_size]);
    int bw = 1 << bw_shift;
    int bh = 1 << bh_shift;
    int idx, idy;
    int lw = b_width_log2_lookup[unit_size] + 2;
    int lh = b_height_log2_lookup[unit_size] + 2;

    sum_sse = 0;

    for (idy = 0; idy < bh; ++idy) {
      for (idx = 0; idx < bw; ++idx) {
        uint8_t *src = p->src.buf + (idy * p->src.stride << lh) + (idx << lw);
        uint8_t *dst = pd->dst.buf + (idy * pd->dst.stride << lh) + (idx << lh);
        int block_idx = (idy << bw_shift) + idx;
        int low_err_skip = 0;

        var = cpi->fn_ptr[unit_size].vf(src, p->src.stride,
                                        dst, pd->dst.stride, &sse);
        x->bsse[(i << 2) + block_idx] = sse;
        sum_sse += sse;

        x->skip_txfm[(i << 2) + block_idx] = SKIP_TXFM_NONE;
        if (!x->select_tx_size) {
          // Check if all ac coefficients can be quantized to zero.
          if (var < ac_thr || var == 0) {
            x->skip_txfm[(i << 2) + block_idx] = SKIP_TXFM_AC_ONLY;

            // Check if dc coefficient can be quantized to zero.
            if (sse - var < dc_thr || sse == var) {
              x->skip_txfm[(i << 2) + block_idx] = SKIP_TXFM_AC_DC;

              if (!sse || (var < low_ac_thr && sse - var < low_dc_thr))
                low_err_skip = 1;
            }
          }
        }

        if (skip_flag && !low_err_skip)
          skip_flag = 0;

        if (i == 0)
          x->pred_sse[ref] += sse;
      }
    }

    total_sse += sum_sse;

    // Fast approximate the modelling function.
    if (cpi->sf.simple_model_rd_from_var) {
      int64_t rate;
      const int64_t square_error = sum_sse;
      int quantizer = (pd->dequant[1] >> dequant_shift);

      if (quantizer < 120)
        rate = (square_error * (280 - quantizer)) >> (16 - VP9_PROB_COST_SHIFT);
      else
        rate = 0;
      dist = (square_error * quantizer) >> 8;
      rate_sum += rate;
      dist_sum += dist;
    } else {
      vp10_model_rd_from_var_lapndz(sum_sse, num_pels_log2_lookup[bs],
                                   pd->dequant[1] >> dequant_shift,
                                   &rate, &dist);
      rate_sum += rate;
      dist_sum += dist;
    }
  }

  *skip_txfm_sb = skip_flag;
  *skip_sse_sb = total_sse << 4;
  *out_rate_sum = (int)rate_sum;
  *out_dist_sum = dist_sum << 4;
}

int64_t vp10_block_error_c(const tran_low_t *coeff, const tran_low_t *dqcoeff,
                          intptr_t block_size, int64_t *ssz) {
  int i;
  int64_t error = 0, sqcoeff = 0;

  for (i = 0; i < block_size; i++) {
    const int diff = coeff[i] - dqcoeff[i];
    error +=  diff * diff;
    sqcoeff += coeff[i] * coeff[i];
  }

  *ssz = sqcoeff;
  return error;
}

int64_t vp10_block_error_fp_c(const int16_t *coeff, const int16_t *dqcoeff,
                             int block_size) {
  int i;
  int64_t error = 0;

  for (i = 0; i < block_size; i++) {
    const int diff = coeff[i] - dqcoeff[i];
    error +=  diff * diff;
  }

  return error;
}

#if CONFIG_VP9_HIGHBITDEPTH
int64_t vp10_highbd_block_error_c(const tran_low_t *coeff,
                                 const tran_low_t *dqcoeff,
                                 intptr_t block_size,
                                 int64_t *ssz, int bd) {
  int i;
  int64_t error = 0, sqcoeff = 0;
  int shift = 2 * (bd - 8);
  int rounding = shift > 0 ? 1 << (shift - 1) : 0;

  for (i = 0; i < block_size; i++) {
    const int64_t diff = coeff[i] - dqcoeff[i];
    error +=  diff * diff;
    sqcoeff += (int64_t)coeff[i] * (int64_t)coeff[i];
  }
  assert(error >= 0 && sqcoeff >= 0);
  error = (error + rounding) >> shift;
  sqcoeff = (sqcoeff + rounding) >> shift;

  *ssz = sqcoeff;
  return error;
}
#endif  // CONFIG_VP9_HIGHBITDEPTH

/* The trailing '0' is a terminator which is used inside cost_coeffs() to
 * decide whether to include cost of a trailing EOB node or not (i.e. we
 * can skip this if the last coefficient in this transform block, e.g. the
 * 16th coefficient in a 4x4 block or the 64th coefficient in a 8x8 block,
 * were non-zero). */
static const int16_t band_counts[TX_SIZES][8] = {
  { 1, 2, 3, 4,  3,   16 - 13, 0 },
  { 1, 2, 3, 4, 11,   64 - 21, 0 },
  { 1, 2, 3, 4, 11,  256 - 21, 0 },
  { 1, 2, 3, 4, 11, 1024 - 21, 0 },
};
static int cost_coeffs(MACROBLOCK *x,
                       int plane, int block,
#if CONFIG_VAR_TX
                       int coeff_ctx,
#else
                       ENTROPY_CONTEXT *A, ENTROPY_CONTEXT *L,
#endif
                       TX_SIZE tx_size,
                       const int16_t *scan, const int16_t *nb,
                       int use_fast_coef_costing) {
  MACROBLOCKD *const xd = &x->e_mbd;
  MB_MODE_INFO *mbmi = &xd->mi[0]->mbmi;
  const struct macroblock_plane *p = &x->plane[plane];
  const struct macroblockd_plane *pd = &xd->plane[plane];
  const PLANE_TYPE type = pd->plane_type;
  const int16_t *band_count = &band_counts[tx_size][1];
  const int eob = p->eobs[block];
  const tran_low_t *const qcoeff = BLOCK_OFFSET(p->qcoeff, block);
  unsigned int (*token_costs)[2][COEFF_CONTEXTS][ENTROPY_TOKENS] =
                   x->token_costs[tx_size][type][is_inter_block(mbmi)];
  uint8_t token_cache[32 * 32];
#if CONFIG_VAR_TX
  int pt = coeff_ctx;
#else
  int pt = combine_entropy_contexts(*A, *L);
#endif
  int c, cost;
#if CONFIG_VP9_HIGHBITDEPTH
  const int *cat6_high_cost = vp10_get_high_cost_table(xd->bd);
#else
  const int *cat6_high_cost = vp10_get_high_cost_table(8);
#endif

#if !CONFIG_VAR_TX && !CONFIG_SUPERTX
  // Check for consistency of tx_size with mode info
  assert(type == PLANE_TYPE_Y ? mbmi->tx_size == tx_size
                              : get_uv_tx_size(mbmi, pd) == tx_size);
#endif  // !CONFIG_VAR_TX && !CONFIG_SUPERTX

  if (eob == 0) {
    // single eob token
    cost = token_costs[0][0][pt][EOB_TOKEN];
    c = 0;
  } else {
    if (use_fast_coef_costing) {
      int band_left = *band_count++;

      // dc token
      int v = qcoeff[0];
      int16_t prev_t;
      cost = vp10_get_token_cost(v, &prev_t, cat6_high_cost);
      cost += (*token_costs)[0][pt][prev_t];

      token_cache[0] = vp10_pt_energy_class[prev_t];
      ++token_costs;

      // ac tokens
      for (c = 1; c < eob; c++) {
        const int rc = scan[c];
        int16_t t;

        v = qcoeff[rc];
        cost += vp10_get_token_cost(v, &t, cat6_high_cost);
        cost += (*token_costs)[!prev_t][!prev_t][t];
        prev_t = t;
        if (!--band_left) {
          band_left = *band_count++;
          ++token_costs;
        }
      }

      // eob token
      if (band_left)
        cost += (*token_costs)[0][!prev_t][EOB_TOKEN];

    } else {  // !use_fast_coef_costing
      int band_left = *band_count++;

      // dc token
      int v = qcoeff[0];
      int16_t tok;
      unsigned int (*tok_cost_ptr)[COEFF_CONTEXTS][ENTROPY_TOKENS];
      cost = vp10_get_token_cost(v, &tok, cat6_high_cost);
      cost += (*token_costs)[0][pt][tok];

      token_cache[0] = vp10_pt_energy_class[tok];
      ++token_costs;

      tok_cost_ptr = &((*token_costs)[!tok]);

      // ac tokens
      for (c = 1; c < eob; c++) {
        const int rc = scan[c];

        v = qcoeff[rc];
        cost += vp10_get_token_cost(v, &tok, cat6_high_cost);
        pt = get_coef_context(nb, token_cache, c);
        cost += (*tok_cost_ptr)[pt][tok];
        token_cache[rc] = vp10_pt_energy_class[tok];
        if (!--band_left) {
          band_left = *band_count++;
          ++token_costs;
        }
        tok_cost_ptr = &((*token_costs)[!tok]);
      }

      // eob token
      if (band_left) {
        pt = get_coef_context(nb, token_cache, c);
        cost += (*token_costs)[0][pt][EOB_TOKEN];
      }
    }
  }

#if !CONFIG_VAR_TX
  // is eob first coefficient;
  *A = *L = (c > 0);
#endif

  return cost;
}

static void dist_block(const VP10_COMP *cpi, MACROBLOCK *x, int plane,
                       int block, int blk_row, int blk_col, TX_SIZE tx_size,
                       int64_t *out_dist, int64_t *out_sse) {
  if (cpi->sf.use_transform_domain_distortion) {
    // Transform domain distortion computation is more accurate as it does
    // not involve an inverse transform, but it is less accurate.
    const int ss_txfrm_size = tx_size << 1;
    MACROBLOCKD* const xd = &x->e_mbd;
    const struct macroblock_plane *const p = &x->plane[plane];
    const struct macroblockd_plane *const pd = &xd->plane[plane];
    int64_t this_sse;
    int shift = tx_size == TX_32X32 ? 0 : 2;
    tran_low_t *const coeff = BLOCK_OFFSET(p->coeff, block);
    tran_low_t *const dqcoeff = BLOCK_OFFSET(pd->dqcoeff, block);
#if CONFIG_VP9_HIGHBITDEPTH
    const int bd = (xd->cur_buf->flags & YV12_FLAG_HIGHBITDEPTH) ? xd->bd : 8;
    *out_dist = vp10_highbd_block_error(coeff, dqcoeff, 16 << ss_txfrm_size,
                                        &this_sse, bd) >> shift;
#else
    *out_dist = vp10_block_error(coeff, dqcoeff, 16 << ss_txfrm_size,
                                 &this_sse) >> shift;
#endif  // CONFIG_VP9_HIGHBITDEPTH
    *out_sse = this_sse >> shift;
  } else {
    const BLOCK_SIZE tx_bsize = txsize_to_bsize[tx_size];
    const int bs = 4*num_4x4_blocks_wide_lookup[tx_bsize];

    const MACROBLOCKD *xd = &x->e_mbd;
    const struct macroblock_plane *p = &x->plane[plane];
    const struct macroblockd_plane *pd = &xd->plane[plane];

    const int src_stride = x->plane[plane].src.stride;
    const int dst_stride = xd->plane[plane].dst.stride;
    const int src_idx = 4 * (blk_row * src_stride + blk_col);
    const int dst_idx = 4 * (blk_row * dst_stride + blk_col);
    const uint8_t *src = &x->plane[plane].src.buf[src_idx];
    const uint8_t *dst = &xd->plane[plane].dst.buf[dst_idx];
    const tran_low_t *dqcoeff = BLOCK_OFFSET(pd->dqcoeff, block);
    const uint16_t *eob = &p->eobs[block];

    unsigned int tmp;

    assert(cpi != NULL);

    cpi->fn_ptr[tx_bsize].vf(src, src_stride, dst, dst_stride, &tmp);
    *out_sse = (int64_t)tmp * 16;

    if (*eob) {
      const MB_MODE_INFO *mbmi = &xd->mi[0]->mbmi;
#if CONFIG_VP9_HIGHBITDEPTH
      DECLARE_ALIGNED(16, uint16_t, recon16[32 * 32]);  // MAX TX_SIZE**2
      uint8_t *recon = (uint8_t*)recon16;
#else
      DECLARE_ALIGNED(16, uint8_t, recon[32 * 32]);     // MAX TX_SIZE**2
#endif  // CONFIG_VP9_HIGHBITDEPTH

      const PLANE_TYPE plane_type = plane == 0 ? PLANE_TYPE_Y : PLANE_TYPE_UV;

      INV_TXFM_PARAM inv_txfm_param;

      inv_txfm_param.tx_type = get_tx_type(plane_type, xd, block, tx_size);
      inv_txfm_param.tx_size = tx_size;
      inv_txfm_param.eob = *eob;
      inv_txfm_param.lossless = xd->lossless[mbmi->segment_id];

#if CONFIG_VP9_HIGHBITDEPTH
      if (xd->cur_buf->flags & YV12_FLAG_HIGHBITDEPTH) {
        recon = CONVERT_TO_BYTEPTR(recon);
        inv_txfm_param.bd = xd->bd;
        vpx_highbd_convolve_copy(dst, dst_stride, recon, 32,
                                 NULL, 0, NULL, 0, bs, bs, xd->bd);
        highbd_inv_txfm_add(dqcoeff, recon, 32, &inv_txfm_param);
      } else
#endif  // CONFIG_VP9_HIGHBITDEPTH
      {
        vpx_convolve_copy(dst, dst_stride, recon, 32,
                          NULL, 0, NULL, 0, bs, bs);
        inv_txfm_add(dqcoeff, recon, 32, &inv_txfm_param);
      }

      cpi->fn_ptr[tx_bsize].vf(src, src_stride, recon, 32, &tmp);
    }

    *out_dist = (int64_t)tmp * 16;
  }
}

static int rate_block(int plane, int block, int blk_row, int blk_col,
                      TX_SIZE tx_size, struct rdcost_block_args* args) {
#if CONFIG_VAR_TX
  int coeff_ctx = combine_entropy_contexts(*(args->t_above + blk_col),
                                           *(args->t_left + blk_row));
  int coeff_cost = cost_coeffs(args->x, plane, block, coeff_ctx,
                               tx_size, args->so->scan, args->so->neighbors,
                               args->use_fast_coef_costing);
  const struct macroblock_plane *p = &args->x->plane[plane];
  *(args->t_above + blk_col) = !(p->eobs[block] == 0);
  *(args->t_left  + blk_row) = !(p->eobs[block] == 0);
  return coeff_cost;
#else
  return cost_coeffs(args->x, plane, block,
                     args->t_above + blk_col,
                     args->t_left + blk_row,
                     tx_size, args->so->scan, args->so->neighbors,
                     args->use_fast_coef_costing);
#endif  // CONFIG_VAR_TX
}

static void block_rd_txfm(int plane, int block, int blk_row, int blk_col,
                          BLOCK_SIZE plane_bsize,
                          TX_SIZE tx_size, void *arg) {
  struct rdcost_block_args *args = arg;
  MACROBLOCK *const x = args->x;
  MACROBLOCKD *const xd = &x->e_mbd;
  MB_MODE_INFO *const mbmi = &xd->mi[0]->mbmi;
  int64_t rd1, rd2, rd;
  int rate;
  int64_t dist;
  int64_t sse;

  if (args->exit_early)
    return;

  if (!is_inter_block(mbmi)) {
    struct encode_b_args arg = {x, NULL, &mbmi->skip};
    vp10_encode_block_intra(plane, block, blk_row, blk_col,
                            plane_bsize, tx_size, &arg);

    if (args->cpi->sf.use_transform_domain_distortion) {
      dist_block(args->cpi, x, plane, block, blk_row, blk_col,
                 tx_size, &dist, &sse);
    } else {
      // Note that the encode block_intra call above already calls
      // inv_txfm_add, so we can't just call dist_block here.
      const int bs = 4 << tx_size;
      const BLOCK_SIZE tx_bsize = txsize_to_bsize[tx_size];
      const vpx_variance_fn_t variance = args->cpi->fn_ptr[tx_bsize].vf;

      const struct macroblock_plane *const p = &x->plane[plane];
      const struct macroblockd_plane *const pd = &xd->plane[plane];

      const int src_stride = p->src.stride;
      const int dst_stride = pd->dst.stride;
      const int diff_stride = 4 * num_4x4_blocks_wide_lookup[plane_bsize];

      const uint8_t *src = &p->src.buf[4 * (blk_row * src_stride + blk_col)];
      const uint8_t *dst = &pd->dst.buf[4 * (blk_row * dst_stride + blk_col)];
      const int16_t *diff = &p->src_diff[4 * (blk_row * diff_stride + blk_col)];

      unsigned int tmp;

      sse = vpx_sum_squares_2d_i16(diff, diff_stride, bs);
#if CONFIG_VP9_HIGHBITDEPTH
      if (xd->cur_buf->flags & YV12_FLAG_HIGHBITDEPTH)
        sse = ROUNDZ_POWER_OF_TWO(sse, (xd->bd - 8) * 2);
#endif  // CONFIG_VP9_HIGHBITDEPTH
      sse = (int64_t)sse * 16;

      variance(src, src_stride, dst, dst_stride, &tmp);
      dist = (int64_t)tmp * 16;
    }
  } else if (max_txsize_lookup[plane_bsize] == tx_size) {
    if (x->skip_txfm[(plane << 2) + (block >> (tx_size << 1))] ==
        SKIP_TXFM_NONE) {
      // full forward transform and quantization
      vp10_xform_quant(x, plane, block, blk_row, blk_col,
                       plane_bsize, tx_size, VP10_XFORM_QUANT_B);
      dist_block(args->cpi, x, plane, block, blk_row, blk_col,
                 tx_size, &dist, &sse);
    } else if (x->skip_txfm[(plane << 2) + (block >> (tx_size << 1))] ==
               SKIP_TXFM_AC_ONLY) {
      // compute DC coefficient
      tran_low_t *const coeff   = BLOCK_OFFSET(x->plane[plane].coeff, block);
      tran_low_t *const dqcoeff = BLOCK_OFFSET(xd->plane[plane].dqcoeff, block);
      vp10_xform_quant(x, plane, block, blk_row, blk_col,
                          plane_bsize, tx_size, VP10_XFORM_QUANT_DC);
      sse  = x->bsse[(plane << 2) + (block >> (tx_size << 1))] << 4;
      dist = sse;
      if (x->plane[plane].eobs[block]) {
        const int64_t orig_sse = (int64_t)coeff[0] * coeff[0];
        const int64_t resd_sse = coeff[0] - dqcoeff[0];
        int64_t dc_correct = orig_sse - resd_sse * resd_sse;
#if CONFIG_VP9_HIGHBITDEPTH
        dc_correct >>= ((xd->bd - 8) * 2);
#endif
        if (tx_size != TX_32X32)
          dc_correct >>= 2;

        dist = VPXMAX(0, sse - dc_correct);
      }
    } else {
      // SKIP_TXFM_AC_DC
      // skip forward transform
      x->plane[plane].eobs[block] = 0;
      sse  = x->bsse[(plane << 2) + (block >> (tx_size << 1))] << 4;
      dist = sse;
    }
  } else {
    // full forward transform and quantization
    vp10_xform_quant(x, plane, block, blk_row, blk_col, plane_bsize, tx_size,
                     VP10_XFORM_QUANT_B);
    dist_block(args->cpi, x, plane, block, blk_row, blk_col,
               tx_size, &dist, &sse);
  }

  rd = RDCOST(x->rdmult, x->rddiv, 0, dist);
  if (args->this_rd + rd > args->best_rd) {
    args->exit_early = 1;
    return;
  }

  rate = rate_block(plane, block, blk_row, blk_col, tx_size, args);
  rd1 = RDCOST(x->rdmult, x->rddiv, rate, dist);
  rd2 = RDCOST(x->rdmult, x->rddiv, 0, sse);

  // TODO(jingning): temporarily enabled only for luma component
  rd = VPXMIN(rd1, rd2);
  if (plane == 0)
    x->zcoeff_blk[tx_size][block] = !x->plane[plane].eobs[block] ||
        (rd1 > rd2 && !xd->lossless[mbmi->segment_id]);

  args->this_rate += rate;
  args->this_dist += dist;
  args->this_sse += sse;
  args->this_rd += rd;

  if (args->this_rd > args->best_rd) {
    args->exit_early = 1;
    return;
  }

  args->skippable &= !x->plane[plane].eobs[block];
}

static void txfm_rd_in_plane(MACROBLOCK *x,
                             const VP10_COMP *cpi,
                             int *rate, int64_t *distortion,
                             int *skippable, int64_t *sse,
                             int64_t ref_best_rd, int plane,
                             BLOCK_SIZE bsize, TX_SIZE tx_size,
                             int use_fast_coef_casting) {
  MACROBLOCKD *const xd = &x->e_mbd;
  const struct macroblockd_plane *const pd = &xd->plane[plane];
  TX_TYPE tx_type;
  struct rdcost_block_args args;
  vp10_zero(args);
  args.x = x;
  args.cpi = cpi;
  args.best_rd = ref_best_rd;
  args.use_fast_coef_costing = use_fast_coef_casting;
  args.skippable = 1;

  if (plane == 0)
    xd->mi[0]->mbmi.tx_size = tx_size;

  vp10_get_entropy_contexts(bsize, tx_size, pd, args.t_above, args.t_left);

  tx_type = get_tx_type(pd->plane_type, xd, 0, tx_size);
  args.so = get_scan(tx_size, tx_type, is_inter_block(&xd->mi[0]->mbmi));

  vp10_foreach_transformed_block_in_plane(xd, bsize, plane,
                                          block_rd_txfm, &args);
  if (args.exit_early) {
    *rate       = INT_MAX;
    *distortion = INT64_MAX;
    *sse        = INT64_MAX;
    *skippable  = 0;
  } else {
    *distortion = args.this_dist;
    *rate       = args.this_rate;
    *sse        = args.this_sse;
    *skippable  = args.skippable;
  }
}

#if CONFIG_SUPERTX
void vp10_txfm_rd_in_plane_supertx(MACROBLOCK *x,
                                   const VP10_COMP *cpi,
                                   int *rate, int64_t *distortion,
                                   int *skippable, int64_t *sse,
                                   int64_t ref_best_rd, int plane,
                                   BLOCK_SIZE bsize, TX_SIZE tx_size,
                                   int use_fast_coef_casting) {
  MACROBLOCKD *const xd = &x->e_mbd;
  const struct macroblockd_plane *const pd = &xd->plane[plane];
  struct rdcost_block_args args;
  TX_TYPE tx_type;

  vp10_zero(args);
  args.cpi = cpi;
  args.x = x;
  args.best_rd = ref_best_rd;
  args.use_fast_coef_costing = use_fast_coef_casting;

  if (plane == 0)
    xd->mi[0]->mbmi.tx_size = tx_size;

  vp10_get_entropy_contexts(bsize, tx_size, pd, args.t_above, args.t_left);

  tx_type = get_tx_type(pd->plane_type, xd, 0, tx_size);
  args.so = get_scan(tx_size, tx_type, is_inter_block(&xd->mi[0]->mbmi));

  block_rd_txfm(plane, 0, 0, 0, get_plane_block_size(bsize, pd),
                tx_size, &args);

  if (args.exit_early) {
    *rate       = INT_MAX;
    *distortion = INT64_MAX;
    *sse        = INT64_MAX;
    *skippable  = 0;
  } else {
    *distortion = args.this_dist;
    *rate       = args.this_rate;
    *sse        = args.this_sse;
    *skippable  = !x->plane[plane].eobs[0];
  }
}
#endif  // CONFIG_SUPERTX

static void choose_largest_tx_size(VP10_COMP *cpi, MACROBLOCK *x,
                                   int *rate, int64_t *distortion,
                                   int *skip, int64_t *sse,
                                   int64_t ref_best_rd,
                                   BLOCK_SIZE bs) {
  const TX_SIZE max_tx_size = max_txsize_lookup[bs];
  VP10_COMMON *const cm = &cpi->common;
  const TX_SIZE largest_tx_size = tx_mode_to_biggest_tx_size[cm->tx_mode];
  MACROBLOCKD *const xd = &x->e_mbd;
  MB_MODE_INFO *const mbmi = &xd->mi[0]->mbmi;
  TX_TYPE tx_type, best_tx_type = DCT_DCT;
  int r, s;
  int64_t d, psse, this_rd, best_rd = INT64_MAX;
  vpx_prob skip_prob = vp10_get_skip_prob(cm, xd);
  int  s0 = vp10_cost_bit(skip_prob, 0);
  int  s1 = vp10_cost_bit(skip_prob, 1);
  const int is_inter = is_inter_block(mbmi);
  int prune = 0;
#if CONFIG_EXT_TX
  int ext_tx_set;
#endif  // CONFIG_EXT_TX

  if (is_inter && cpi->sf.tx_type_search > 0)
    prune = prune_tx_types(cpi, bs, x, xd);
  mbmi->tx_size = VPXMIN(max_tx_size, largest_tx_size);

#if CONFIG_EXT_TX
  ext_tx_set = get_ext_tx_set(mbmi->tx_size, bs, is_inter);

  if (get_ext_tx_types(mbmi->tx_size, bs, is_inter) > 1 &&
      !xd->lossless[mbmi->segment_id]) {
    for (tx_type = 0; tx_type < TX_TYPES; ++tx_type) {
      if (is_inter) {
        if (!ext_tx_used_inter[ext_tx_set][tx_type])
          continue;
        if (cpi->sf.tx_type_search > 0) {
          if (!do_tx_type_search(tx_type, prune))
            continue;
        } else if (ext_tx_set == 1 &&
                   tx_type >= DST_ADST && tx_type < IDTX &&
                   best_tx_type == DCT_DCT) {
          tx_type = IDTX - 1;
          continue;
        }
      } else {
        if (!ALLOW_INTRA_EXT_TX && bs >= BLOCK_8X8) {
          if (tx_type != intra_mode_to_tx_type_context[mbmi->mode])
            continue;
        }
        if (!ext_tx_used_intra[ext_tx_set][tx_type])
          continue;
        if (ext_tx_set == 1 &&
            tx_type >= DST_ADST && tx_type < IDTX &&
            best_tx_type == DCT_DCT) {
          tx_type = IDTX - 1;
          continue;
        }
      }

      mbmi->tx_type = tx_type;

      txfm_rd_in_plane(x,
                       cpi,
                       &r, &d, &s,
                       &psse, ref_best_rd, 0, bs, mbmi->tx_size,
                       cpi->sf.use_fast_coef_costing);

      if (r == INT_MAX)
        continue;
      if (get_ext_tx_types(mbmi->tx_size, bs, is_inter) > 1) {
        if (is_inter) {
          if (ext_tx_set > 0)
            r += cpi->inter_tx_type_costs[ext_tx_set]
                                         [mbmi->tx_size][mbmi->tx_type];
        } else {
          if (ext_tx_set > 0 && ALLOW_INTRA_EXT_TX)
            r += cpi->intra_tx_type_costs[ext_tx_set][mbmi->tx_size]
                                         [mbmi->mode][mbmi->tx_type];
        }
      }

      if (s)
        this_rd = RDCOST(x->rdmult, x->rddiv, s1, psse);
      else
        this_rd = RDCOST(x->rdmult, x->rddiv, r + s0, d);
      if (is_inter_block(mbmi) && !xd->lossless[mbmi->segment_id] && !s)
        this_rd = VPXMIN(this_rd, RDCOST(x->rdmult, x->rddiv, s1, psse));

      if (this_rd < ((best_tx_type == DCT_DCT) ? ext_tx_th : 1) * best_rd) {
        best_rd = this_rd;
        best_tx_type = mbmi->tx_type;
      }
    }
  }

#else  // CONFIG_EXT_TX
  if (mbmi->tx_size < TX_32X32 &&
      !xd->lossless[mbmi->segment_id]) {
    for (tx_type = 0; tx_type < TX_TYPES; ++tx_type) {
      mbmi->tx_type = tx_type;
      txfm_rd_in_plane(x,
                       cpi,
                       &r, &d, &s,
                       &psse, ref_best_rd, 0, bs, mbmi->tx_size,
                       cpi->sf.use_fast_coef_costing);
      if (r == INT_MAX)
        continue;
      if (is_inter) {
        r += cpi->inter_tx_type_costs[mbmi->tx_size][mbmi->tx_type];
        if (cpi->sf.tx_type_search > 0 && !do_tx_type_search(tx_type, prune))
            continue;
      } else {
        r += cpi->intra_tx_type_costs[mbmi->tx_size]
                                     [intra_mode_to_tx_type_context[mbmi->mode]]
                                     [mbmi->tx_type];
      }
      if (s)
        this_rd = RDCOST(x->rdmult, x->rddiv, s1, psse);
      else
        this_rd = RDCOST(x->rdmult, x->rddiv, r + s0, d);
      if (is_inter && !xd->lossless[mbmi->segment_id] && !s)
        this_rd = VPXMIN(this_rd, RDCOST(x->rdmult, x->rddiv, s1, psse));

      if (this_rd < ((best_tx_type == DCT_DCT) ? ext_tx_th : 1) * best_rd) {
        best_rd = this_rd;
        best_tx_type = mbmi->tx_type;
      }
    }
  }
#endif  // CONFIG_EXT_TX
  mbmi->tx_type = best_tx_type;

  txfm_rd_in_plane(x,
                   cpi,
                   rate, distortion, skip,
                   sse, ref_best_rd, 0, bs,
                   mbmi->tx_size, cpi->sf.use_fast_coef_costing);
}

static void choose_smallest_tx_size(VP10_COMP *cpi, MACROBLOCK *x,
                                    int *rate, int64_t *distortion,
                                    int *skip, int64_t *sse,
                                    int64_t ref_best_rd,
                                    BLOCK_SIZE bs) {
  MACROBLOCKD *const xd = &x->e_mbd;
  MB_MODE_INFO *const mbmi = &xd->mi[0]->mbmi;

  mbmi->tx_size = TX_4X4;
  mbmi->tx_type = DCT_DCT;

  txfm_rd_in_plane(x,
                   cpi,
                   rate, distortion, skip,
                   sse, ref_best_rd, 0, bs,
                   mbmi->tx_size, cpi->sf.use_fast_coef_costing);
}

static void choose_tx_size_from_rd(VP10_COMP *cpi, MACROBLOCK *x,
                                   int *rate,
                                   int64_t *distortion,
                                   int *skip,
                                   int64_t *psse,
                                   int64_t ref_best_rd,
                                   BLOCK_SIZE bs) {
  const TX_SIZE max_tx_size = max_txsize_lookup[bs];
  VP10_COMMON *const cm = &cpi->common;
  MACROBLOCKD *const xd = &x->e_mbd;
  MB_MODE_INFO *const mbmi = &xd->mi[0]->mbmi;
  vpx_prob skip_prob = vp10_get_skip_prob(cm, xd);
  int r, s;
  int64_t d, sse;
  int64_t rd = INT64_MAX;
  int n;
  int s0, s1;
  int64_t best_rd = INT64_MAX, last_rd = INT64_MAX;
  TX_SIZE best_tx = max_tx_size;
  int start_tx, end_tx;
  const int tx_select = cm->tx_mode == TX_MODE_SELECT;
  const int is_inter = is_inter_block(mbmi);
  TX_TYPE tx_type, best_tx_type = DCT_DCT;
  int prune = 0;
#if CONFIG_EXT_TX
  int ext_tx_set;
#endif  // CONFIG_EXT_TX

  if (is_inter && cpi->sf.tx_type_search > 0)
    prune = prune_tx_types(cpi, bs, x, xd);

  assert(skip_prob > 0);
  s0 = vp10_cost_bit(skip_prob, 0);
  s1 = vp10_cost_bit(skip_prob, 1);

  if (tx_select) {
    start_tx = max_tx_size;
    end_tx = 0;
  } else {
    const TX_SIZE chosen_tx_size =
        VPXMIN(max_tx_size, tx_mode_to_biggest_tx_size[cm->tx_mode]);
    start_tx = chosen_tx_size;
    end_tx = chosen_tx_size;
  }

  *distortion = INT64_MAX;
  *rate       = INT_MAX;
  *skip       = 0;
  *psse       = INT64_MAX;

  for (tx_type = DCT_DCT; tx_type < TX_TYPES; ++tx_type) {
    last_rd = INT64_MAX;
    for (n = start_tx; n >= end_tx; --n) {
      const int r_tx_size =
          cpi->tx_size_cost[max_tx_size - TX_8X8][get_tx_size_context(xd)][n];
      if (FIXED_TX_TYPE && tx_type != get_default_tx_type(0, xd, 0, n))
          continue;
#if CONFIG_EXT_TX
      ext_tx_set = get_ext_tx_set(n, bs, is_inter);
      if (is_inter) {
        if (!ext_tx_used_inter[ext_tx_set][tx_type])
          continue;
        if (cpi->sf.tx_type_search > 0) {
          if (!do_tx_type_search(tx_type, prune))
            continue;
        } else if (ext_tx_set == 1 &&
                   tx_type >= DST_ADST && tx_type < IDTX &&
                   best_tx_type == DCT_DCT) {
          tx_type = IDTX - 1;
          continue;
        }
      } else {
        if (!ALLOW_INTRA_EXT_TX && bs >= BLOCK_8X8) {
          if (tx_type != intra_mode_to_tx_type_context[mbmi->mode])
            continue;
        }
        if (!ext_tx_used_intra[ext_tx_set][tx_type])
          continue;
        if (ext_tx_set == 1 &&
            tx_type >= DST_ADST && tx_type < IDTX &&
            best_tx_type == DCT_DCT) {
          tx_type = IDTX - 1;
          break;
        }
      }
      mbmi->tx_type = tx_type;
      txfm_rd_in_plane(x,
                       cpi,
                       &r, &d, &s,
                       &sse, ref_best_rd, 0, bs, n,
                       cpi->sf.use_fast_coef_costing);
      if (get_ext_tx_types(n, bs, is_inter) > 1 &&
          !xd->lossless[xd->mi[0]->mbmi.segment_id] &&
          r != INT_MAX) {
        if (is_inter) {
          if (ext_tx_set > 0)
            r += cpi->inter_tx_type_costs[ext_tx_set]
                                         [mbmi->tx_size][mbmi->tx_type];
        } else {
          if (ext_tx_set > 0 && ALLOW_INTRA_EXT_TX)
            r += cpi->intra_tx_type_costs[ext_tx_set][mbmi->tx_size]
                                         [mbmi->mode][mbmi->tx_type];
        }
      }
#else  // CONFIG_EXT_TX
      if (n >= TX_32X32 && tx_type != DCT_DCT) {
        continue;
      }
      mbmi->tx_type = tx_type;
      txfm_rd_in_plane(x,
                       cpi,
                       &r, &d, &s,
                       &sse, ref_best_rd, 0, bs, n,
                       cpi->sf.use_fast_coef_costing);
      if (n < TX_32X32 &&
          !xd->lossless[xd->mi[0]->mbmi.segment_id] &&
          r != INT_MAX && !FIXED_TX_TYPE) {
        if (is_inter) {
          r += cpi->inter_tx_type_costs[mbmi->tx_size][mbmi->tx_type];
          if (cpi->sf.tx_type_search > 0 && !do_tx_type_search(tx_type, prune))
              continue;
        } else {
          r += cpi->intra_tx_type_costs[mbmi->tx_size]
              [intra_mode_to_tx_type_context[mbmi->mode]]
              [mbmi->tx_type];
        }
      }
#endif  // CONFIG_EXT_TX

      if (r == INT_MAX)
        continue;

      if (s) {
        if (is_inter) {
          rd = RDCOST(x->rdmult, x->rddiv, s1, sse);
        } else {
          rd =  RDCOST(x->rdmult, x->rddiv, s1 + r_tx_size * tx_select, sse);
        }
      } else {
        rd = RDCOST(x->rdmult, x->rddiv, r + s0 + r_tx_size * tx_select, d);
      }

      if (tx_select && !(s && is_inter))
        r += r_tx_size;

      if (is_inter && !xd->lossless[xd->mi[0]->mbmi.segment_id] && !s)
        rd = VPXMIN(rd, RDCOST(x->rdmult, x->rddiv, s1, sse));

      // Early termination in transform size search.
      if (cpi->sf.tx_size_search_breakout &&
          (rd == INT64_MAX ||
           (s == 1 && tx_type != DCT_DCT && n < start_tx) ||
           (n < (int) max_tx_size && rd > last_rd)))
        break;

      last_rd = rd;
      if (rd <
          (is_inter && best_tx_type == DCT_DCT ? ext_tx_th : 1) *
          best_rd) {
        best_tx = n;
        best_rd = rd;
        *distortion = d;
        *rate       = r;
        *skip       = s;
        *psse       = sse;
        best_tx_type = mbmi->tx_type;
      }
    }
  }

  mbmi->tx_size = best_tx;
  mbmi->tx_type = best_tx_type;

#if !CONFIG_EXT_TX
  if (mbmi->tx_size >= TX_32X32)
    assert(mbmi->tx_type == DCT_DCT);
#endif

  txfm_rd_in_plane(x,
                   cpi,
                   &r, &d, &s,
                   &sse, ref_best_rd, 0, bs, best_tx,
                   cpi->sf.use_fast_coef_costing);
}

static void super_block_yrd(VP10_COMP *cpi, MACROBLOCK *x, int *rate,
                            int64_t *distortion, int *skip,
                            int64_t *psse, BLOCK_SIZE bs,
                            int64_t ref_best_rd) {
  MACROBLOCKD *xd = &x->e_mbd;
  int64_t sse;
  int64_t *ret_sse = psse ? psse : &sse;

  assert(bs == xd->mi[0]->mbmi.sb_type);

  if (xd->lossless[xd->mi[0]->mbmi.segment_id]) {
    choose_smallest_tx_size(cpi, x, rate, distortion, skip, ret_sse,
                            ref_best_rd, bs);
  } else if (cpi->sf.tx_size_search_method == USE_LARGESTALL) {
    choose_largest_tx_size(cpi, x, rate, distortion, skip, ret_sse, ref_best_rd,
                           bs);
  } else {
    choose_tx_size_from_rd(cpi, x, rate, distortion, skip, ret_sse,
                           ref_best_rd, bs);
  }
}

static int conditional_skipintra(PREDICTION_MODE mode,
                                 PREDICTION_MODE best_intra_mode) {
  if (mode == D117_PRED &&
      best_intra_mode != V_PRED &&
      best_intra_mode != D135_PRED)
    return 1;
  if (mode == D63_PRED &&
      best_intra_mode != V_PRED &&
      best_intra_mode != D45_PRED)
    return 1;
  if (mode == D207_PRED &&
      best_intra_mode != H_PRED &&
      best_intra_mode != D45_PRED)
    return 1;
  if (mode == D153_PRED &&
      best_intra_mode != H_PRED &&
      best_intra_mode != D135_PRED)
    return 1;
  return 0;
}

static int rd_pick_palette_intra_sby(VP10_COMP *cpi, MACROBLOCK *x,
                                     BLOCK_SIZE bsize,
                                     int palette_ctx, int dc_mode_cost,
                                     PALETTE_MODE_INFO *palette_mode_info,
                                     uint8_t *best_palette_color_map,
                                     TX_SIZE *best_tx,
                                     PREDICTION_MODE *mode_selected,
                                     int64_t *best_rd) {
  MACROBLOCKD *const xd = &x->e_mbd;
  MODE_INFO *const mic = xd->mi[0];
  const int rows = 4 * num_4x4_blocks_high_lookup[bsize];
  const int cols = 4 * num_4x4_blocks_wide_lookup[bsize];
  int this_rate, this_rate_tokenonly, s, colors, n;
  int rate_overhead = 0;
  int64_t this_distortion, this_rd;
  const int src_stride = x->plane[0].src.stride;
  const uint8_t *const src = x->plane[0].src.buf;

#if CONFIG_VP9_HIGHBITDEPTH
  if (cpi->common.use_highbitdepth)
    colors = vp10_count_colors_highbd(src, src_stride, rows, cols,
                                      cpi->common.bit_depth);
  else
#endif  // CONFIG_VP9_HIGHBITDEPTH
    colors = vp10_count_colors(src, src_stride, rows, cols);
  palette_mode_info->palette_size[0] = 0;
#if CONFIG_EXT_INTRA
  mic->mbmi.ext_intra_mode_info.use_ext_intra_mode[0] = 0;
#endif  // CONFIG_EXT_INTRA

  if (colors > 1 && colors <= 64 && cpi->common.allow_screen_content_tools) {
    int r, c, i, j, k;
    const int max_itr = 50;
    int color_ctx, color_idx = 0;
    int color_order[PALETTE_MAX_SIZE];
    double *const data = x->palette_buffer->kmeans_data_buf;
    uint8_t *const indices = x->palette_buffer->kmeans_indices_buf;
    uint8_t *const pre_indices = x->palette_buffer->kmeans_pre_indices_buf;
    double centroids[PALETTE_MAX_SIZE];
    uint8_t *const color_map = xd->plane[0].color_index_map;
    double lb, ub, val;
    MB_MODE_INFO *const mbmi = &mic->mbmi;
    PALETTE_MODE_INFO *const pmi = &mbmi->palette_mode_info;
#if CONFIG_VP9_HIGHBITDEPTH
    uint16_t *src16 = CONVERT_TO_SHORTPTR(src);
    if (cpi->common.use_highbitdepth)
      lb = ub = src16[0];
    else
#endif  // CONFIG_VP9_HIGHBITDEPTH
      lb = ub = src[0];

#if CONFIG_VP9_HIGHBITDEPTH
    if (cpi->common.use_highbitdepth) {
      for (r = 0; r < rows; ++r) {
        for (c = 0; c < cols; ++c) {
          val = src16[r * src_stride + c];
          data[r * cols + c] = val;
          if (val < lb)
            lb = val;
          else if (val > ub)
            ub = val;
        }
      }
    } else {
#endif  // CONFIG_VP9_HIGHBITDEPTH
      for (r = 0; r < rows; ++r) {
        for (c = 0; c < cols; ++c) {
          val = src[r * src_stride + c];
          data[r * cols + c] = val;
          if (val < lb)
            lb = val;
          else if (val > ub)
            ub = val;
        }
      }
#if CONFIG_VP9_HIGHBITDEPTH
    }
#endif  // CONFIG_VP9_HIGHBITDEPTH

    mbmi->mode = DC_PRED;
#if CONFIG_EXT_INTRA
    mbmi->ext_intra_mode_info.use_ext_intra_mode[0] = 0;
#endif  // CONFIG_EXT_INTRA

    for (n = colors > PALETTE_MAX_SIZE ? PALETTE_MAX_SIZE : colors;
        n >= 2; --n) {
      for (i = 0; i < n; ++i)
        centroids[i] = lb + (2 * i + 1) * (ub - lb) / n / 2;
      vp10_k_means(data, centroids, indices, pre_indices, rows * cols,
                   n, 1, max_itr);
      vp10_insertion_sort(centroids, n);
      for (i = 0; i < n; ++i)
        centroids[i] = round(centroids[i]);
      // remove duplicates
      i = 1;
      k = n;
      while (i < k) {
        if (centroids[i] == centroids[i - 1]) {
          j = i;
          while (j < k - 1) {
            centroids[j] = centroids[j + 1];
            ++j;
          }
          --k;
        } else {
          ++i;
        }
      }

#if CONFIG_VP9_HIGHBITDEPTH
      if (cpi->common.use_highbitdepth)
        for (i = 0; i < k; ++i)
          pmi->palette_colors[i] = clip_pixel_highbd((int)round(centroids[i]),
                                                     cpi->common.bit_depth);
      else
#endif  // CONFIG_VP9_HIGHBITDEPTH
        for (i = 0; i < k; ++i)
          pmi->palette_colors[i] = clip_pixel((int)round(centroids[i]));
      pmi->palette_size[0] = k;

      vp10_calc_indices(data, centroids, indices, rows * cols, k, 1);
      for (r = 0; r < rows; ++r)
        for (c = 0; c < cols; ++c)
          color_map[r * cols + c] = indices[r * cols + c];

      super_block_yrd(cpi, x, &this_rate_tokenonly, &this_distortion,
                      &s, NULL, bsize, *best_rd);
      if (this_rate_tokenonly == INT_MAX)
        continue;

      this_rate = this_rate_tokenonly + dc_mode_cost +
          cpi->common.bit_depth * k * vp10_cost_bit(128, 0) +
          cpi->palette_y_size_cost[bsize - BLOCK_8X8][k - 2] +
          write_uniform_cost(k, color_map[0]) +
          vp10_cost_bit(vp10_default_palette_y_mode_prob[bsize - BLOCK_8X8]
                                                        [palette_ctx], 1);
      for (i = 0; i < rows; ++i) {
        for (j = (i == 0 ? 1 : 0); j < cols; ++j) {
          color_ctx = vp10_get_palette_color_context(color_map, cols, i, j,
                                                     k, color_order);
          for (r = 0; r < k; ++r)
            if (color_map[i * cols + j] == color_order[r]) {
              color_idx = r;
              break;
            }
          assert(color_idx >= 0 && color_idx < k);
          this_rate +=
              cpi->palette_y_color_cost[k - 2][color_ctx][color_idx];
        }
      }
      this_rd = RDCOST(x->rdmult, x->rddiv, this_rate, this_distortion);

      if (this_rd < *best_rd) {
        *best_rd = this_rd;
        *palette_mode_info = *pmi;
        memcpy(best_palette_color_map, color_map,
               rows * cols * sizeof(color_map[0]));
        *mode_selected = DC_PRED;
        *best_tx = mbmi->tx_size;
        rate_overhead = this_rate - this_rate_tokenonly;
      }
    }
  }

  return rate_overhead;
}

static int64_t rd_pick_intra4x4block(VP10_COMP *cpi, MACROBLOCK *x,
                                     int row, int col,
                                     PREDICTION_MODE *best_mode,
                                     const int *bmode_costs,
                                     ENTROPY_CONTEXT *a, ENTROPY_CONTEXT *l,
                                     int *bestrate, int *bestratey,
                                     int64_t *bestdistortion,
                                     BLOCK_SIZE bsize, int64_t rd_thresh) {
  PREDICTION_MODE mode;
  MACROBLOCKD *const xd = &x->e_mbd;
  int64_t best_rd = rd_thresh;
  struct macroblock_plane *p = &x->plane[0];
  struct macroblockd_plane *pd = &xd->plane[0];
  const int src_stride = p->src.stride;
  const int dst_stride = pd->dst.stride;
  const uint8_t *src_init = &p->src.buf[row * 4 * src_stride + col * 4];
  uint8_t *dst_init = &pd->dst.buf[row * 4 * src_stride + col * 4];
  ENTROPY_CONTEXT ta[2], tempa[2];
  ENTROPY_CONTEXT tl[2], templ[2];
  const int num_4x4_blocks_wide = num_4x4_blocks_wide_lookup[bsize];
  const int num_4x4_blocks_high = num_4x4_blocks_high_lookup[bsize];
  int idx, idy;
  uint8_t best_dst[8 * 8];
#if CONFIG_VP9_HIGHBITDEPTH
  uint16_t best_dst16[8 * 8];
#endif

  memcpy(ta, a, sizeof(ta));
  memcpy(tl, l, sizeof(tl));
  xd->mi[0]->mbmi.tx_size = TX_4X4;
  xd->mi[0]->mbmi.palette_mode_info.palette_size[0] = 0;

#if CONFIG_VP9_HIGHBITDEPTH
  if (xd->cur_buf->flags & YV12_FLAG_HIGHBITDEPTH) {
    for (mode = DC_PRED; mode <= TM_PRED; ++mode) {
      int64_t this_rd;
      int ratey = 0;
      int64_t distortion = 0;
      int rate = bmode_costs[mode];

      if (!(cpi->sf.intra_y_mode_mask[TX_4X4] & (1 << mode)))
        continue;

      // Only do the oblique modes if the best so far is
      // one of the neighboring directional modes
      if (cpi->sf.mode_search_skip_flags & FLAG_SKIP_INTRA_DIRMISMATCH) {
        if (conditional_skipintra(mode, *best_mode))
          continue;
      }

      memcpy(tempa, ta, sizeof(ta));
      memcpy(templ, tl, sizeof(tl));

      for (idy = 0; idy < num_4x4_blocks_high; ++idy) {
        for (idx = 0; idx < num_4x4_blocks_wide; ++idx) {
          const int block = (row + idy) * 2 + (col + idx);
          const uint8_t *const src = &src_init[idx * 4 + idy * 4 * src_stride];
          uint8_t *const dst = &dst_init[idx * 4 + idy * 4 * dst_stride];
          int16_t *const src_diff = vp10_raster_block_offset_int16(BLOCK_8X8,
                                                                   block,
                                                                   p->src_diff);
          tran_low_t *const coeff = BLOCK_OFFSET(x->plane[0].coeff, block);
          xd->mi[0]->bmi[block].as_mode = mode;
          vp10_predict_intra_block(xd, 1, 1, TX_4X4, mode, dst, dst_stride,
                                  dst, dst_stride,
                                  col + idx, row + idy, 0);
          vpx_highbd_subtract_block(4, 4, src_diff, 8, src, src_stride,
                                    dst, dst_stride, xd->bd);
          if (xd->lossless[xd->mi[0]->mbmi.segment_id]) {
            TX_TYPE tx_type = get_tx_type(PLANE_TYPE_Y, xd, block, TX_4X4);
            const scan_order *so = get_scan(TX_4X4, tx_type, 0);
#if CONFIG_VAR_TX
            const int coeff_ctx = combine_entropy_contexts(*(tempa + idx),
                                                           *(templ + idy));
#endif  // CONFIG_VAR_TX
            vp10_highbd_fwd_txfm_4x4(src_diff, coeff, 8, DCT_DCT, 1);
            vp10_regular_quantize_b_4x4(x, 0, block, so->scan, so->iscan);
#if CONFIG_VAR_TX
            ratey += cost_coeffs(x, 0, block, coeff_ctx, TX_4X4, so->scan,
                                 so->neighbors, cpi->sf.use_fast_coef_costing);
            *(tempa + idx) = !(p->eobs[block] == 0);
            *(templ + idy) = !(p->eobs[block] == 0);
#else
            ratey += cost_coeffs(x, 0, block, tempa + idx, templ + idy,
                                 TX_4X4,
                                 so->scan, so->neighbors,
                                 cpi->sf.use_fast_coef_costing);
#endif  // CONFIG_VAR_TX
            if (RDCOST(x->rdmult, x->rddiv, ratey, distortion) >= best_rd)
              goto next_highbd;
            vp10_highbd_inv_txfm_add_4x4(BLOCK_OFFSET(pd->dqcoeff, block),
                                         dst, dst_stride, p->eobs[block],
                                         xd->bd, DCT_DCT, 1);
          } else {
            int64_t dist;
            unsigned int tmp;
            TX_TYPE tx_type = get_tx_type(PLANE_TYPE_Y, xd, block, TX_4X4);
            const scan_order *so = get_scan(TX_4X4, tx_type, 0);
#if CONFIG_VAR_TX
            const int coeff_ctx = combine_entropy_contexts(*(tempa + idx),
                                                           *(templ + idy));
#endif  // CONFIG_VAR_TX
            vp10_highbd_fwd_txfm_4x4(src_diff, coeff, 8, tx_type, 0);
            vp10_regular_quantize_b_4x4(x, 0, block, so->scan, so->iscan);
#if CONFIG_VAR_TX
            ratey += cost_coeffs(x, 0, block, coeff_ctx, TX_4X4, so->scan,
                                 so->neighbors, cpi->sf.use_fast_coef_costing);
            *(tempa + idx) = !(p->eobs[block] == 0);
            *(templ + idy) = !(p->eobs[block] == 0);
#else
            ratey += cost_coeffs(x, 0, block, tempa + idx, templ + idy,
                                 TX_4X4, so->scan, so->neighbors,
                                 cpi->sf.use_fast_coef_costing);
#endif  // CONFIG_VAR_TX
            vp10_highbd_inv_txfm_add_4x4(BLOCK_OFFSET(pd->dqcoeff, block),
                                         dst, dst_stride, p->eobs[block],
                                         xd->bd, tx_type, 0);
            cpi->fn_ptr[BLOCK_4X4].vf(src, src_stride, dst, dst_stride, &tmp);
            dist = (int64_t)tmp << 4;
            distortion += dist;
            if (RDCOST(x->rdmult, x->rddiv, ratey, distortion) >= best_rd)
              goto next_highbd;
          }
        }
      }

      rate += ratey;
      this_rd = RDCOST(x->rdmult, x->rddiv, rate, distortion);

      if (this_rd < best_rd) {
        *bestrate = rate;
        *bestratey = ratey;
        *bestdistortion = distortion;
        best_rd = this_rd;
        *best_mode = mode;
        memcpy(a, tempa, sizeof(tempa));
        memcpy(l, templ, sizeof(templ));
        for (idy = 0; idy < num_4x4_blocks_high * 4; ++idy) {
          memcpy(best_dst16 + idy * 8,
                 CONVERT_TO_SHORTPTR(dst_init + idy * dst_stride),
                 num_4x4_blocks_wide * 4 * sizeof(uint16_t));
        }
      }
next_highbd:
      {}
    }

    if (best_rd >= rd_thresh)
      return best_rd;

    for (idy = 0; idy < num_4x4_blocks_high * 4; ++idy) {
      memcpy(CONVERT_TO_SHORTPTR(dst_init + idy * dst_stride),
             best_dst16 + idy * 8,
             num_4x4_blocks_wide * 4 * sizeof(uint16_t));
    }

    return best_rd;
  }
#endif  // CONFIG_VP9_HIGHBITDEPTH

  for (mode = DC_PRED; mode <= TM_PRED; ++mode) {
    int64_t this_rd;
    int ratey = 0;
    int64_t distortion = 0;
    int rate = bmode_costs[mode];

    if (!(cpi->sf.intra_y_mode_mask[TX_4X4] & (1 << mode)))
      continue;

    // Only do the oblique modes if the best so far is
    // one of the neighboring directional modes
    if (cpi->sf.mode_search_skip_flags & FLAG_SKIP_INTRA_DIRMISMATCH) {
      if (conditional_skipintra(mode, *best_mode))
        continue;
    }

    memcpy(tempa, ta, sizeof(ta));
    memcpy(templ, tl, sizeof(tl));

    for (idy = 0; idy < num_4x4_blocks_high; ++idy) {
      for (idx = 0; idx < num_4x4_blocks_wide; ++idx) {
        const int block = (row + idy) * 2 + (col + idx);
        const uint8_t *const src = &src_init[idx * 4 + idy * 4 * src_stride];
        uint8_t *const dst = &dst_init[idx * 4 + idy * 4 * dst_stride];
        int16_t *const src_diff =
            vp10_raster_block_offset_int16(BLOCK_8X8, block, p->src_diff);
        tran_low_t *const coeff = BLOCK_OFFSET(x->plane[0].coeff, block);
        xd->mi[0]->bmi[block].as_mode = mode;
        vp10_predict_intra_block(xd, 1, 1, TX_4X4, mode, dst, dst_stride,
                                dst, dst_stride, col + idx, row + idy, 0);
        vpx_subtract_block(4, 4, src_diff, 8, src, src_stride, dst, dst_stride);

        if (xd->lossless[xd->mi[0]->mbmi.segment_id]) {
          TX_TYPE tx_type = get_tx_type(PLANE_TYPE_Y, xd, block, TX_4X4);
          const scan_order *so = get_scan(TX_4X4, tx_type, 0);
#if CONFIG_VAR_TX
          const int coeff_ctx = combine_entropy_contexts(*(tempa + idx),
                                                         *(templ + idy));
#endif
          vp10_fwd_txfm_4x4(src_diff, coeff, 8, DCT_DCT, 1);
          vp10_regular_quantize_b_4x4(x, 0, block, so->scan, so->iscan);
#if CONFIG_VAR_TX
          ratey += cost_coeffs(x, 0, block, coeff_ctx, TX_4X4, so->scan,
                               so->neighbors, cpi->sf.use_fast_coef_costing);
          *(tempa + idx) = !(p->eobs[block] == 0);
          *(templ + idy) = !(p->eobs[block] == 0);
#else
          ratey += cost_coeffs(x, 0, block, tempa + idx, templ + idy,
                               TX_4X4,
                               so->scan, so->neighbors,
                               cpi->sf.use_fast_coef_costing);
#endif
          if (RDCOST(x->rdmult, x->rddiv, ratey, distortion) >= best_rd)
            goto next;
          vp10_inv_txfm_add_4x4(BLOCK_OFFSET(pd->dqcoeff, block),
                                dst, dst_stride, p->eobs[block], DCT_DCT, 1);
        } else {
          int64_t dist;
          unsigned int tmp;
          TX_TYPE tx_type = get_tx_type(PLANE_TYPE_Y, xd, block, TX_4X4);
          const scan_order *so = get_scan(TX_4X4, tx_type, 0);
#if CONFIG_VAR_TX
          const int coeff_ctx = combine_entropy_contexts(*(tempa + idx),
                                                         *(templ + idy));
#endif
          vp10_fwd_txfm_4x4(src_diff, coeff, 8, tx_type, 0);
          vp10_regular_quantize_b_4x4(x, 0, block, so->scan, so->iscan);
#if CONFIG_VAR_TX
          ratey += cost_coeffs(x, 0, block, coeff_ctx, TX_4X4, so->scan,
                               so->neighbors, cpi->sf.use_fast_coef_costing);
          *(tempa + idx) = !(p->eobs[block] == 0);
          *(templ + idy) = !(p->eobs[block] == 0);
#else
          ratey += cost_coeffs(x, 0, block, tempa + idx, templ + idy,
                               TX_4X4, so->scan, so->neighbors,
                               cpi->sf.use_fast_coef_costing);
#endif
          vp10_inv_txfm_add_4x4(BLOCK_OFFSET(pd->dqcoeff, block),
                                dst, dst_stride, p->eobs[block], tx_type, 0);
          cpi->fn_ptr[BLOCK_4X4].vf(src, src_stride, dst, dst_stride, &tmp);
          dist = (int64_t)tmp << 4;
          distortion += dist;
          // To use the pixel domain distortion, the step below needs to be
          // put behind the inv txfm. Compared to calculating the distortion
          // in the frequency domain, the overhead of encoding effort is low.
          if (RDCOST(x->rdmult, x->rddiv, ratey, distortion) >= best_rd)
            goto next;
        }
      }
    }

    rate += ratey;
    this_rd = RDCOST(x->rdmult, x->rddiv, rate, distortion);

    if (this_rd < best_rd) {
      *bestrate = rate;
      *bestratey = ratey;
      *bestdistortion = distortion;
      best_rd = this_rd;
      *best_mode = mode;
      memcpy(a, tempa, sizeof(tempa));
      memcpy(l, templ, sizeof(templ));
      for (idy = 0; idy < num_4x4_blocks_high * 4; ++idy)
        memcpy(best_dst + idy * 8, dst_init + idy * dst_stride,
               num_4x4_blocks_wide * 4);
    }
  next:
    {}
  }

  if (best_rd >= rd_thresh)
    return best_rd;

  for (idy = 0; idy < num_4x4_blocks_high * 4; ++idy)
    memcpy(dst_init + idy * dst_stride, best_dst + idy * 8,
           num_4x4_blocks_wide * 4);

  return best_rd;
}

static int64_t rd_pick_intra_sub_8x8_y_mode(VP10_COMP *cpi, MACROBLOCK *mb,
                                            int *rate, int *rate_y,
                                            int64_t *distortion,
                                            int64_t best_rd) {
  int i, j;
  const MACROBLOCKD *const xd = &mb->e_mbd;
  MODE_INFO *const mic = xd->mi[0];
  const MODE_INFO *above_mi = xd->above_mi;
  const MODE_INFO *left_mi = xd->left_mi;
  const BLOCK_SIZE bsize = xd->mi[0]->mbmi.sb_type;
  const int num_4x4_blocks_wide = num_4x4_blocks_wide_lookup[bsize];
  const int num_4x4_blocks_high = num_4x4_blocks_high_lookup[bsize];
  int idx, idy;
  int cost = 0;
  int64_t total_distortion = 0;
  int tot_rate_y = 0;
  int64_t total_rd = 0;
  ENTROPY_CONTEXT t_above[4], t_left[4];
  const int *bmode_costs = cpi->mbmode_cost[0];

  memcpy(t_above, xd->plane[0].above_context, sizeof(t_above));
  memcpy(t_left, xd->plane[0].left_context, sizeof(t_left));

#if CONFIG_EXT_INTRA
  mic->mbmi.ext_intra_mode_info.use_ext_intra_mode[0] = 0;
  mic->mbmi.intra_filter = INTRA_FILTER_LINEAR;
#endif  // CONFIG_EXT_INTRA

  // TODO(any): Add search of the tx_type to improve rd performance at the
  // expense of speed.
  mic->mbmi.tx_type = DCT_DCT;
  mic->mbmi.tx_size = TX_4X4;

  // Pick modes for each sub-block (of size 4x4, 4x8, or 8x4) in an 8x8 block.
  for (idy = 0; idy < 2; idy += num_4x4_blocks_high) {
    for (idx = 0; idx < 2; idx += num_4x4_blocks_wide) {
      PREDICTION_MODE best_mode = DC_PRED;
      int r = INT_MAX, ry = INT_MAX;
      int64_t d = INT64_MAX, this_rd = INT64_MAX;
      i = idy * 2 + idx;
      if (cpi->common.frame_type == KEY_FRAME) {
        const PREDICTION_MODE A = vp10_above_block_mode(mic, above_mi, i);
        const PREDICTION_MODE L = vp10_left_block_mode(mic, left_mi, i);

        bmode_costs  = cpi->y_mode_costs[A][L];
      }

      this_rd = rd_pick_intra4x4block(cpi, mb, idy, idx, &best_mode,
                                      bmode_costs, t_above + idx, t_left + idy,
                                      &r, &ry, &d, bsize, best_rd - total_rd);
      if (this_rd >= best_rd - total_rd)
        return INT64_MAX;

      total_rd += this_rd;
      cost += r;
      total_distortion += d;
      tot_rate_y += ry;

      mic->bmi[i].as_mode = best_mode;
      for (j = 1; j < num_4x4_blocks_high; ++j)
        mic->bmi[i + j * 2].as_mode = best_mode;
      for (j = 1; j < num_4x4_blocks_wide; ++j)
        mic->bmi[i + j].as_mode = best_mode;

      if (total_rd >= best_rd)
        return INT64_MAX;
    }
  }
  mic->mbmi.mode = mic->bmi[3].as_mode;

  // Add in the cost of the transform type
  if (!xd->lossless[mic->mbmi.segment_id]) {
    int rate_tx_type = 0;
#if CONFIG_EXT_TX
    if (get_ext_tx_types(TX_4X4, bsize, 0) > 1) {
      const int eset = get_ext_tx_set(TX_4X4, bsize, 0);
      rate_tx_type =
          cpi->intra_tx_type_costs[eset][TX_4X4]
                                  [mic->mbmi.mode][mic->mbmi.tx_type];
    }
#else
    rate_tx_type =
        cpi->intra_tx_type_costs[TX_4X4]
                                [intra_mode_to_tx_type_context[mic->mbmi.mode]]
                                [mic->mbmi.tx_type];
#endif
    assert(mic->mbmi.tx_size == TX_4X4);
    cost += rate_tx_type;
    tot_rate_y += rate_tx_type;
  }

  *rate = cost;
  *rate_y = tot_rate_y;
  *distortion = total_distortion;

  return RDCOST(mb->rdmult, mb->rddiv, cost, total_distortion);
}

#if CONFIG_EXT_INTRA
// Return 1 if an ext intra mode is selected; return 0 otherwise.
static int rd_pick_ext_intra_sby(VP10_COMP *cpi, MACROBLOCK *x,
                                 int *rate, int *rate_tokenonly,
                                 int64_t *distortion, int *skippable,
                                 BLOCK_SIZE bsize, int mode_cost,
                                 int64_t *best_rd) {
  MACROBLOCKD *const xd = &x->e_mbd;
  MODE_INFO *const mic = xd->mi[0];
  MB_MODE_INFO *mbmi = &mic->mbmi;
  int this_rate, this_rate_tokenonly, s;
  int ext_intra_selected_flag = 0;
  int64_t this_distortion, this_rd;
  EXT_INTRA_MODE mode;
  TX_SIZE best_tx_size = TX_4X4;
  EXT_INTRA_MODE_INFO ext_intra_mode_info;
  TX_TYPE best_tx_type;

  vp10_zero(ext_intra_mode_info);
  mbmi->ext_intra_mode_info.use_ext_intra_mode[0] = 1;
  mbmi->mode = DC_PRED;
  mbmi->palette_mode_info.palette_size[0] = 0;

  for (mode = 0; mode < FILTER_INTRA_MODES; ++mode) {
    mbmi->ext_intra_mode_info.ext_intra_mode[0] = mode;
    super_block_yrd(cpi, x, &this_rate_tokenonly, &this_distortion,
                    &s, NULL, bsize, *best_rd);
    if (this_rate_tokenonly == INT_MAX)
      continue;

    this_rate = this_rate_tokenonly +
        vp10_cost_bit(cpi->common.fc->ext_intra_probs[0], 1) +
        write_uniform_cost(FILTER_INTRA_MODES, mode) + mode_cost;
    this_rd = RDCOST(x->rdmult, x->rddiv, this_rate, this_distortion);

    if (this_rd < *best_rd) {
      *best_rd            = this_rd;
      best_tx_size        = mic->mbmi.tx_size;
      ext_intra_mode_info = mbmi->ext_intra_mode_info;
      best_tx_type        = mic->mbmi.tx_type;
      *rate               = this_rate;
      *rate_tokenonly     = this_rate_tokenonly;
      *distortion         = this_distortion;
      *skippable          = s;
      ext_intra_selected_flag = 1;
    }
  }

  if (ext_intra_selected_flag) {
    mbmi->mode = DC_PRED;
    mbmi->tx_size = best_tx_size;
    mbmi->ext_intra_mode_info.use_ext_intra_mode[0] =
        ext_intra_mode_info.use_ext_intra_mode[0];
    mbmi->ext_intra_mode_info.ext_intra_mode[0] =
        ext_intra_mode_info.ext_intra_mode[0];
    mbmi->tx_type = best_tx_type;
    return 1;
  } else {
    return 0;
  }
}

static void pick_intra_angle_routine_sby(VP10_COMP *cpi, MACROBLOCK *x,
                                         int *rate, int *rate_tokenonly,
                                         int64_t *distortion, int *skippable,
                                         int *best_angle_delta,
                                         TX_SIZE *best_tx_size,
                                         TX_TYPE *best_tx_type,
                                         INTRA_FILTER *best_filter,
                                         BLOCK_SIZE bsize, int rate_overhead,
                                         int64_t *best_rd) {
  int this_rate, this_rate_tokenonly, s;
  int64_t this_distortion, this_rd;
  MB_MODE_INFO *mbmi = &x->e_mbd.mi[0]->mbmi;
  super_block_yrd(cpi, x, &this_rate_tokenonly, &this_distortion,
                  &s, NULL, bsize, *best_rd);
  if (this_rate_tokenonly == INT_MAX)
    return;

  this_rate = this_rate_tokenonly + rate_overhead;
  this_rd = RDCOST(x->rdmult, x->rddiv, this_rate, this_distortion);

  if (this_rd < *best_rd) {
    *best_rd            = this_rd;
    *best_angle_delta   = mbmi->angle_delta[0];
    *best_tx_size       = mbmi->tx_size;
    *best_filter        = mbmi->intra_filter;
    *best_tx_type       = mbmi->tx_type;
    *rate               = this_rate;
    *rate_tokenonly     = this_rate_tokenonly;
    *distortion         = this_distortion;
    *skippable          = s;
  }
}

static int64_t rd_pick_intra_angle_sby(VP10_COMP *cpi, MACROBLOCK *x,
                                       int *rate, int *rate_tokenonly,
                                       int64_t *distortion, int *skippable,
                                       BLOCK_SIZE bsize, int rate_overhead,
                                       int64_t best_rd) {
  MACROBLOCKD *const xd = &x->e_mbd;
  MODE_INFO *const mic = xd->mi[0];
  MB_MODE_INFO *mbmi = &mic->mbmi;
  int this_rate, this_rate_tokenonly, s;
  int angle_delta, best_angle_delta = 0, p_angle;
  const int intra_filter_ctx = vp10_get_pred_context_intra_interp(xd);
  INTRA_FILTER filter, best_filter = INTRA_FILTER_LINEAR;
  const double rd_adjust = 1.2;
  int64_t this_distortion, this_rd, sse_dummy;
  TX_SIZE best_tx_size = mic->mbmi.tx_size;
  TX_TYPE best_tx_type = mbmi->tx_type;

  if (ANGLE_FAST_SEARCH) {
    int deltas_level1[3] = {0, -2, 2};
    int deltas_level2[3][2] = {
        {-1, 1}, {-3, -1}, {1, 3},
    };
    const int level1 = 3, level2 = 2;
    int i, j, best_i = -1;

    for (i = 0; i < level1; ++i) {
      mic->mbmi.angle_delta[0] = deltas_level1[i];
      p_angle = mode_to_angle_map[mbmi->mode] +
          mbmi->angle_delta[0] * ANGLE_STEP;
      for (filter = INTRA_FILTER_LINEAR; filter < INTRA_FILTERS; ++filter) {
        int64_t tmp_best_rd;
        if ((FILTER_FAST_SEARCH || !pick_intra_filter(p_angle)) &&
            filter != INTRA_FILTER_LINEAR)
          continue;
        mic->mbmi.intra_filter = filter;
        tmp_best_rd = (i == 0 && filter == INTRA_FILTER_LINEAR &&
            best_rd < INT64_MAX) ? (int64_t)(best_rd * rd_adjust) : best_rd;
        super_block_yrd(cpi, x, &this_rate_tokenonly, &this_distortion,
                        &s, NULL, bsize, tmp_best_rd);
        if (this_rate_tokenonly == INT_MAX) {
          if (i == 0 && filter == INTRA_FILTER_LINEAR)
            return best_rd;
          else
            continue;
        }
        this_rate = this_rate_tokenonly + rate_overhead +
            cpi->intra_filter_cost[intra_filter_ctx][filter];
        this_rd = RDCOST(x->rdmult, x->rddiv, this_rate, this_distortion);
        if (i == 0 && filter == INTRA_FILTER_LINEAR &&
            best_rd < INT64_MAX && this_rd > best_rd * rd_adjust)
          return best_rd;
        if (this_rd < best_rd) {
          best_i              = i;
          best_rd             = this_rd;
          best_angle_delta    = mbmi->angle_delta[0];
          best_tx_size        = mbmi->tx_size;
          best_filter         = mbmi->intra_filter;
          best_tx_type        = mbmi->tx_type;
          *rate               = this_rate;
          *rate_tokenonly     = this_rate_tokenonly;
          *distortion         = this_distortion;
          *skippable          = s;
        }
      }
    }

    if (best_i >= 0) {
      for (j = 0; j < level2; ++j) {
        mic->mbmi.angle_delta[0] = deltas_level2[best_i][j];
        p_angle = mode_to_angle_map[mbmi->mode] +
            mbmi->angle_delta[0] * ANGLE_STEP;
        for (filter = INTRA_FILTER_LINEAR; filter < INTRA_FILTERS; ++filter) {
          mic->mbmi.intra_filter = filter;
          if ((FILTER_FAST_SEARCH || !pick_intra_filter(p_angle)) &&
              filter != INTRA_FILTER_LINEAR)
            continue;
          pick_intra_angle_routine_sby(cpi, x, rate, rate_tokenonly,
                                       distortion, skippable,
                                       &best_angle_delta, &best_tx_size,
                                       &best_tx_type, &best_filter, bsize,
                                       rate_overhead +
                                       cpi->intra_filter_cost
                                       [intra_filter_ctx][filter],
                                       &best_rd);
        }
      }
    }
  } else {
    for (angle_delta = -MAX_ANGLE_DELTAS; angle_delta <= MAX_ANGLE_DELTAS;
        ++angle_delta) {
      mbmi->angle_delta[0] = angle_delta;
      p_angle = mode_to_angle_map[mbmi->mode] +
          mbmi->angle_delta[0] * ANGLE_STEP;
      for (filter = INTRA_FILTER_LINEAR; filter < INTRA_FILTERS; ++filter) {
        mic->mbmi.intra_filter = filter;
        if ((FILTER_FAST_SEARCH || !pick_intra_filter(p_angle)) &&
            filter != INTRA_FILTER_LINEAR)
          continue;
        pick_intra_angle_routine_sby(cpi, x, rate, rate_tokenonly,
                                     distortion, skippable,
                                     &best_angle_delta, &best_tx_size,
                                     &best_tx_type, &best_filter, bsize,
                                     rate_overhead +
                                     cpi->intra_filter_cost
                                     [intra_filter_ctx][filter],
                                     &best_rd);
      }
    }
  }

  if (FILTER_FAST_SEARCH && *rate_tokenonly < INT_MAX) {
    mbmi->angle_delta[0] = best_angle_delta;
    p_angle = mode_to_angle_map[mbmi->mode] +
        mbmi->angle_delta[0] * ANGLE_STEP;
    if (pick_intra_filter(p_angle)) {
      for (filter = INTRA_FILTER_LINEAR + 1; filter < INTRA_FILTERS; ++filter) {
        mic->mbmi.intra_filter = filter;
        pick_intra_angle_routine_sby(cpi, x, rate, rate_tokenonly,
                                     distortion, skippable,
                                     &best_angle_delta, &best_tx_size,
                                     &best_tx_type, &best_filter, bsize,
                                     rate_overhead + cpi->intra_filter_cost
                                     [intra_filter_ctx][filter], &best_rd);
      }
    }
  }

  mbmi->tx_size = best_tx_size;
  mbmi->angle_delta[0] = best_angle_delta;
  mic->mbmi.intra_filter = best_filter;
  mbmi->tx_type = best_tx_type;

  if (*rate_tokenonly < INT_MAX) {
    txfm_rd_in_plane(x,
                     cpi,
                     &this_rate_tokenonly, &this_distortion, &s,
                     &sse_dummy, INT64_MAX, 0, bsize, mbmi->tx_size,
                     cpi->sf.use_fast_coef_costing);
  }

  return best_rd;
}

static INLINE int get_angle_index(double angle) {
  const double step = 22.5, base = 45;
  return (int)round((angle - base) / step);
}

static void angle_estimation(const uint8_t *src, int src_stride,
                             int rows, int cols, double *hist) {
  int r, c, i, index;
  const double pi = 3.1415;
  double angle, dx, dy;
  double temp, divisor = 0;

  for (i = 0; i < DIRECTIONAL_MODES; ++i)
    hist[i] = 0;

  src += src_stride;
  for (r = 1; r < rows; ++r) {
    for (c = 1; c < cols; ++c) {
      dx = src[c] - src[c - 1];
      dy = src[c] - src[c - src_stride];
      temp = dx * dx + dy * dy;
      if (dy == 0)
        angle = 90;
      else
        angle = (atan((double)dx / (double)dy)) * 180 / pi;
      assert(angle >= -90 && angle <= 90);
      index = get_angle_index(angle + 180);
      if (index < DIRECTIONAL_MODES) {
        hist[index] += temp;
        divisor += temp;
      }
      if (angle > 0) {
        index = get_angle_index(angle);
        if (index >= 0) {
          hist[index] += temp;
          divisor += temp;
        }
      }
    }
    src += src_stride;
  }

  if (divisor < 1)
    divisor = 1;
  for (i = 0; i < DIRECTIONAL_MODES; ++i)
    hist[i] /= divisor;
}

#if CONFIG_VP9_HIGHBITDEPTH
static void highbd_angle_estimation(const uint8_t *src8, int src_stride,
                                    int rows, int cols, double *hist) {
  int r, c, i, index;
  const double pi = 3.1415;
  double angle, dx, dy;
  double temp, divisor = 0;
  uint16_t *src = CONVERT_TO_SHORTPTR(src8);

  for (i = 0; i < DIRECTIONAL_MODES; ++i)
    hist[i] = 0;

  src += src_stride;
  for (r = 1; r < rows; ++r) {
    for (c = 1; c < cols; ++c) {
      dx = src[c] - src[c - 1];
      dy = src[c] - src[c - src_stride];
      temp = dx * dx + dy * dy;
      if (dy == 0)
        angle = 90;
      else
        angle = (atan((double)dx / (double)dy)) * 180 / pi;
      assert(angle >= -90 && angle <= 90);
      index = get_angle_index(angle + 180);
      if (index < DIRECTIONAL_MODES) {
        hist[index] += temp;
        divisor += temp;
      }
      if (angle > 0) {
        index = get_angle_index(angle);
        if (index >= 0) {
          hist[index] += temp;
          divisor += temp;
        }
      }
    }
    src += src_stride;
  }

  if (divisor < 1)
    divisor = 1;
  for (i = 0; i < DIRECTIONAL_MODES; ++i)
    hist[i] /= divisor;
}
#endif  // CONFIG_VP9_HIGHBITDEPTH
#endif  // CONFIG_EXT_INTRA

// This function is used only for intra_only frames
static int64_t rd_pick_intra_sby_mode(VP10_COMP *cpi, MACROBLOCK *x,
                                      int *rate, int *rate_tokenonly,
                                      int64_t *distortion, int *skippable,
                                      BLOCK_SIZE bsize,
                                      int64_t best_rd) {
  PREDICTION_MODE mode;
  PREDICTION_MODE mode_selected = DC_PRED;
  MACROBLOCKD *const xd = &x->e_mbd;
  MODE_INFO *const mic = xd->mi[0];
  int this_rate, this_rate_tokenonly, s;
  int64_t this_distortion, this_rd;
  TX_SIZE best_tx = TX_4X4;
#if CONFIG_EXT_INTRA
  const int intra_filter_ctx = vp10_get_pred_context_intra_interp(xd);
  EXT_INTRA_MODE_INFO ext_intra_mode_info;
  int is_directional_mode, rate_overhead, best_angle_delta = 0;
  INTRA_FILTER best_filter = INTRA_FILTER_LINEAR;
  uint8_t directional_mode_skip_mask[INTRA_MODES];
  const int src_stride = x->plane[0].src.stride;
  const uint8_t *src = x->plane[0].src.buf;
  double hist[DIRECTIONAL_MODES];
#endif  // CONFIG_EXT_INTRA
  TX_TYPE best_tx_type = DCT_DCT;
  int *bmode_costs;
  PALETTE_MODE_INFO palette_mode_info;
  PALETTE_MODE_INFO *const pmi = &mic->mbmi.palette_mode_info;
  uint8_t *best_palette_color_map = cpi->common.allow_screen_content_tools ?
      x->palette_buffer->best_palette_color_map : NULL;
  const int rows = 4 * num_4x4_blocks_high_lookup[bsize];
  const int cols = 4 * num_4x4_blocks_wide_lookup[bsize];
  int palette_ctx = 0;
  const MODE_INFO *above_mi = xd->above_mi;
  const MODE_INFO *left_mi = xd->left_mi;
  const PREDICTION_MODE A = vp10_above_block_mode(mic, above_mi, 0);
  const PREDICTION_MODE L = vp10_left_block_mode(mic, left_mi, 0);
  const TX_SIZE max_tx_size = max_txsize_lookup[bsize];
  bmode_costs = cpi->y_mode_costs[A][L];

#if CONFIG_EXT_INTRA
  ext_intra_mode_info.use_ext_intra_mode[0] = 0;
  mic->mbmi.ext_intra_mode_info.use_ext_intra_mode[0] = 0;
  mic->mbmi.angle_delta[0] = 0;
  memset(directional_mode_skip_mask, 0,
         sizeof(directional_mode_skip_mask[0]) * INTRA_MODES);
#if CONFIG_VP9_HIGHBITDEPTH
  if (xd->cur_buf->flags & YV12_FLAG_HIGHBITDEPTH)
    highbd_angle_estimation(src, src_stride, rows, cols, hist);
  else
#endif
    angle_estimation(src, src_stride, rows, cols, hist);

  for (mode = 0; mode < INTRA_MODES; ++mode) {
    if (mode != DC_PRED && mode != TM_PRED) {
      int index = get_angle_index((double)mode_to_angle_map[mode]);
      double score, weight = 1.0;
      score = hist[index];
      if (index > 0) {
        score += hist[index - 1] * 0.5;
        weight += 0.5;
      }
      if (index < DIRECTIONAL_MODES - 1) {
        score += hist[index + 1] * 0.5;
        weight += 0.5;
      }
      score /= weight;
      if (score < ANGLE_SKIP_THRESH)
        directional_mode_skip_mask[mode] = 1;
    }
  }
#endif  // CONFIG_EXT_INTRA
  memset(x->skip_txfm, SKIP_TXFM_NONE, sizeof(x->skip_txfm));
  palette_mode_info.palette_size[0] = 0;
  pmi->palette_size[0] = 0;
  if (above_mi)
    palette_ctx += (above_mi->mbmi.palette_mode_info.palette_size[0] > 0);
  if (left_mi)
    palette_ctx += (left_mi->mbmi.palette_mode_info.palette_size[0] > 0);

  /* Y Search for intra prediction mode */
  for (mode = DC_PRED; mode <= TM_PRED; ++mode) {
    mic->mbmi.mode = mode;
#if CONFIG_EXT_INTRA
    is_directional_mode = (mode != DC_PRED && mode != TM_PRED);
    if (is_directional_mode && directional_mode_skip_mask[mode])
      continue;
    if (is_directional_mode) {
      rate_overhead = bmode_costs[mode] +
          write_uniform_cost(2 * MAX_ANGLE_DELTAS + 1, 0);
      this_rate_tokenonly = INT_MAX;
      this_rd =
          rd_pick_intra_angle_sby(cpi, x, &this_rate, &this_rate_tokenonly,
                                  &this_distortion, &s, bsize, rate_overhead,
                                  best_rd);
    } else {
      mic->mbmi.angle_delta[0] = 0;
      super_block_yrd(cpi, x, &this_rate_tokenonly, &this_distortion,
                      &s, NULL, bsize, best_rd);
    }
#endif  // CONFIG_EXT_INTRA
    super_block_yrd(cpi, x, &this_rate_tokenonly, &this_distortion,
                    &s, NULL, bsize, best_rd);

    if (this_rate_tokenonly == INT_MAX)
      continue;

    this_rate = this_rate_tokenonly + bmode_costs[mode];

    if (!xd->lossless[xd->mi[0]->mbmi.segment_id]) {
      // super_block_yrd above includes the cost of the tx_size in the
      // tokenonly rate, but for intra blocks, tx_size is always coded
      // (prediction granularity), so we account for it in the full rate,
      // not the tokenonly rate.
      this_rate_tokenonly -=
          cpi->tx_size_cost[max_tx_size - TX_8X8][get_tx_size_context(xd)]
                                                 [mic->mbmi.tx_size];
    }
    if (cpi->common.allow_screen_content_tools && mode == DC_PRED)
      this_rate +=
          vp10_cost_bit(vp10_default_palette_y_mode_prob[bsize - BLOCK_8X8]
                                                         [palette_ctx], 0);
#if CONFIG_EXT_INTRA
    if (mode == DC_PRED && ALLOW_FILTER_INTRA_MODES)
      this_rate += vp10_cost_bit(cpi->common.fc->ext_intra_probs[0], 0);
    if (is_directional_mode) {
      int p_angle;
      this_rate += write_uniform_cost(2 * MAX_ANGLE_DELTAS + 1,
                                      MAX_ANGLE_DELTAS +
                                      mic->mbmi.angle_delta[0]);
      p_angle = mode_to_angle_map[mic->mbmi.mode] +
          mic->mbmi.angle_delta[0] * ANGLE_STEP;
      if (pick_intra_filter(p_angle))
        this_rate +=
            cpi->intra_filter_cost[intra_filter_ctx][mic->mbmi.intra_filter];
    }
#endif  // CONFIG_EXT_INTRA
    this_rd = RDCOST(x->rdmult, x->rddiv, this_rate, this_distortion);

    if (this_rd < best_rd) {
      mode_selected   = mode;
      best_rd         = this_rd;
      best_tx         = mic->mbmi.tx_size;
#if CONFIG_EXT_INTRA
      best_angle_delta = mic->mbmi.angle_delta[0];
      best_filter     = mic->mbmi.intra_filter;
#endif  // CONFIG_EXT_INTRA
      best_tx_type    = mic->mbmi.tx_type;
      *rate           = this_rate;
      *rate_tokenonly = this_rate_tokenonly;
      *distortion     = this_distortion;
      *skippable      = s;
    }
  }

  if (cpi->common.allow_screen_content_tools)
    rd_pick_palette_intra_sby(cpi, x, bsize, palette_ctx, bmode_costs[DC_PRED],
                              &palette_mode_info, best_palette_color_map,
                              &best_tx, &mode_selected, &best_rd);

#if CONFIG_EXT_INTRA
  if (ALLOW_FILTER_INTRA_MODES) {
    if (rd_pick_ext_intra_sby(cpi, x, rate, rate_tokenonly, distortion,
                              skippable, bsize, bmode_costs[DC_PRED],
                              &best_rd)) {
      mode_selected       = mic->mbmi.mode;
      best_tx             = mic->mbmi.tx_size;
      ext_intra_mode_info = mic->mbmi.ext_intra_mode_info;
      best_tx_type        = mic->mbmi.tx_type;
    }
  }

  mic->mbmi.ext_intra_mode_info.use_ext_intra_mode[0] =
      ext_intra_mode_info.use_ext_intra_mode[0];
  if (ext_intra_mode_info.use_ext_intra_mode[0]) {
    mic->mbmi.ext_intra_mode_info.ext_intra_mode[0] =
        ext_intra_mode_info.ext_intra_mode[0];
    palette_mode_info.palette_size[0] = 0;
  }
#endif  // CONFIG_EXT_INTRA

  mic->mbmi.mode = mode_selected;
  mic->mbmi.tx_size = best_tx;
#if CONFIG_EXT_INTRA
  mic->mbmi.angle_delta[0] = best_angle_delta;
  mic->mbmi.intra_filter = best_filter;
#endif  // CONFIG_EXT_INTRA
  mic->mbmi.tx_type = best_tx_type;
  pmi->palette_size[0] = palette_mode_info.palette_size[0];
  if (palette_mode_info.palette_size[0] > 0) {
    memcpy(pmi->palette_colors, palette_mode_info.palette_colors,
           PALETTE_MAX_SIZE * sizeof(palette_mode_info.palette_colors[0]));
    memcpy(xd->plane[0].color_index_map, best_palette_color_map,
           rows * cols * sizeof(best_palette_color_map[0]));
  }

  return best_rd;
}

#if CONFIG_VAR_TX
void vp10_tx_block_rd_b(const VP10_COMP *cpi, MACROBLOCK *x, TX_SIZE tx_size,
                        int blk_row, int blk_col, int plane, int block,
                        int plane_bsize, int coeff_ctx,
                        int *rate, int64_t *dist, int64_t *bsse, int *skip) {
  MACROBLOCKD *xd = &x->e_mbd;
  const struct macroblock_plane *const p = &x->plane[plane];
  struct macroblockd_plane *const pd = &xd->plane[plane];
  int64_t tmp;
  tran_low_t *const dqcoeff = BLOCK_OFFSET(pd->dqcoeff, block);
  PLANE_TYPE plane_type = (plane == 0) ? PLANE_TYPE_Y : PLANE_TYPE_UV;
  TX_TYPE tx_type = get_tx_type(plane_type, xd, block, tx_size);
  const scan_order *const scan_order =
      get_scan(tx_size, tx_type, is_inter_block(&xd->mi[0]->mbmi));

  BLOCK_SIZE txm_bsize = txsize_to_bsize[tx_size];
  int bh = 4 * num_4x4_blocks_wide_lookup[txm_bsize];
  int src_stride = p->src.stride;
  uint8_t *src = &p->src.buf[4 * blk_row * src_stride + 4 * blk_col];
  uint8_t *dst = &pd->dst.buf[4 * blk_row * pd->dst.stride + 4 * blk_col];
#if CONFIG_VP9_HIGHBITDEPTH
  DECLARE_ALIGNED(16, uint16_t, rec_buffer_alloc_16[32 * 32]);
  uint8_t *rec_buffer;
#else
  DECLARE_ALIGNED(16, uint8_t, rec_buffer[32 * 32]);
#endif  // CONFIG_VP9_HIGHBITDEPTH
  const int diff_stride = 4 * num_4x4_blocks_wide_lookup[plane_bsize];
  const int16_t *diff = &p->src_diff[4 * (blk_row * diff_stride + blk_col)];

  int max_blocks_high = num_4x4_blocks_high_lookup[plane_bsize];
  int max_blocks_wide = num_4x4_blocks_wide_lookup[plane_bsize];

  if (xd->mb_to_bottom_edge < 0)
    max_blocks_high += xd->mb_to_bottom_edge >> (5 + pd->subsampling_y);
  if (xd->mb_to_right_edge < 0)
    max_blocks_wide += xd->mb_to_right_edge >> (5 + pd->subsampling_x);

  vp10_xform_quant(x, plane, block, blk_row, blk_col, plane_bsize, tx_size,
                   VP10_XFORM_QUANT_B);

  // TODO(any): Use dist_block to compute distortion
#if CONFIG_VP9_HIGHBITDEPTH
  if (xd->cur_buf->flags & YV12_FLAG_HIGHBITDEPTH) {
    rec_buffer = CONVERT_TO_BYTEPTR(rec_buffer_alloc_16);
    vpx_highbd_convolve_copy(dst, pd->dst.stride, rec_buffer, 32,
                             NULL, 0, NULL, 0, bh, bh, xd->bd);
  } else {
    rec_buffer = (uint8_t *)rec_buffer_alloc_16;
    vpx_convolve_copy(dst, pd->dst.stride, rec_buffer, 32,
                      NULL, 0, NULL, 0, bh, bh);
  }
#else
  vpx_convolve_copy(dst, pd->dst.stride, rec_buffer, 32,
                    NULL, 0, NULL, 0, bh, bh);
#endif  // CONFIG_VP9_HIGHBITDEPTH

  if (blk_row + (bh >> 2) > max_blocks_high ||
      blk_col + (bh >> 2) > max_blocks_wide) {
    int idx, idy;
    int blocks_height = VPXMIN(bh >> 2, max_blocks_high - blk_row);
    int blocks_width  = VPXMIN(bh >> 2, max_blocks_wide - blk_col);
    tmp = 0;
    for (idy = 0; idy < blocks_height; idy += 2) {
      for (idx = 0; idx < blocks_width; idx += 2) {
        const int16_t *d = diff + 4 * idy * diff_stride + 4 * idx;
        tmp += vpx_sum_squares_2d_i16(d, diff_stride, 8);
      }
    }
  } else {
    tmp = vpx_sum_squares_2d_i16(diff, diff_stride, bh);
  }

#if CONFIG_VP9_HIGHBITDEPTH
  if (xd->cur_buf->flags & YV12_FLAG_HIGHBITDEPTH)
    tmp = ROUNDZ_POWER_OF_TWO(tmp, (xd->bd - 8) * 2);
#endif  // CONFIG_VP9_HIGHBITDEPTH
  *bsse += tmp * 16;

  if (p->eobs[block] > 0) {
    INV_TXFM_PARAM inv_txfm_param;
    inv_txfm_param.tx_type = tx_type;
    inv_txfm_param.tx_size = tx_size;
    inv_txfm_param.eob = p->eobs[block];
    inv_txfm_param.lossless = xd->lossless[xd->mi[0]->mbmi.segment_id];
#if CONFIG_VP9_HIGHBITDEPTH
    if (xd->cur_buf->flags & YV12_FLAG_HIGHBITDEPTH) {
      inv_txfm_param.bd = xd->bd;
      highbd_inv_txfm_add(dqcoeff, rec_buffer, 32, &inv_txfm_param);
    } else {
      inv_txfm_add(dqcoeff, rec_buffer, 32, &inv_txfm_param);
    }
#else  // CONFIG_VP9_HIGHBITDEPTH
    inv_txfm_add(dqcoeff, rec_buffer, 32, &inv_txfm_param);
#endif  // CONFIG_VP9_HIGHBITDEPTH

    if ((bh >> 2) + blk_col > max_blocks_wide ||
        (bh >> 2) + blk_row > max_blocks_high) {
      int idx, idy;
      unsigned int this_dist;
      int blocks_height = VPXMIN(bh >> 2, max_blocks_high - blk_row);
      int blocks_width  = VPXMIN(bh >> 2, max_blocks_wide - blk_col);
      tmp = 0;
      for (idy = 0; idy < blocks_height; idy += 2) {
        for (idx = 0; idx < blocks_width; idx += 2) {
          cpi->fn_ptr[BLOCK_8X8].vf(src + 4 * idy * src_stride + 4 * idx,
                                    src_stride,
                                    rec_buffer + 4 * idy * 32 + 4 * idx,
                                    32, &this_dist);
          tmp += this_dist;
        }
      }
    } else {
      uint32_t this_dist;
      cpi->fn_ptr[txm_bsize].vf(src, src_stride, rec_buffer, 32, &this_dist);
      tmp = this_dist;
    }
  }
  *dist += tmp * 16;
  *rate += cost_coeffs(x, plane, block, coeff_ctx, tx_size,
                       scan_order->scan, scan_order->neighbors, 0);
  *skip &= (p->eobs[block] == 0);
}

static void select_tx_block(const VP10_COMP *cpi, MACROBLOCK *x,
                            int blk_row, int blk_col, int plane, int block,
                            TX_SIZE tx_size, BLOCK_SIZE plane_bsize,
                            ENTROPY_CONTEXT *ta, ENTROPY_CONTEXT *tl,
                            TXFM_CONTEXT *tx_above, TXFM_CONTEXT *tx_left,
                            int *rate, int64_t *dist,
                            int64_t *bsse, int *skip,
                            int64_t ref_best_rd, int *is_cost_valid) {
  MACROBLOCKD *const xd = &x->e_mbd;
  MB_MODE_INFO *const mbmi = &xd->mi[0]->mbmi;
  struct macroblock_plane *const p = &x->plane[plane];
  struct macroblockd_plane *const pd = &xd->plane[plane];
  int tx_idx = (blk_row >> (1 - pd->subsampling_y)) * 8 +
               (blk_col >> (1 - pd->subsampling_x));
  int max_blocks_high = num_4x4_blocks_high_lookup[plane_bsize];
  int max_blocks_wide = num_4x4_blocks_wide_lookup[plane_bsize];
  int64_t this_rd = INT64_MAX;
  ENTROPY_CONTEXT *pta = ta + blk_col;
  ENTROPY_CONTEXT *ptl = tl + blk_row;
  ENTROPY_CONTEXT stxa = 0, stxl = 0;
  int coeff_ctx, i;
  int ctx = txfm_partition_context(tx_above + (blk_col >> 1),
                                   tx_left + (blk_row >> 1), tx_size);

  int64_t sum_dist = 0, sum_bsse = 0;
  int64_t sum_rd = INT64_MAX;
  int sum_rate = vp10_cost_bit(cpi->common.fc->txfm_partition_prob[ctx], 1);
  int all_skip = 1;
  int tmp_eob = 0;
  int zero_blk_rate;

  if (ref_best_rd < 0) {
    *is_cost_valid = 0;
    return;
  }

  switch (tx_size) {
    case TX_4X4:
      stxa = pta[0];
      stxl = ptl[0];
      break;
    case TX_8X8:
      stxa = !!*(const uint16_t *)&pta[0];
      stxl = !!*(const uint16_t *)&ptl[0];
      break;
    case TX_16X16:
      stxa = !!*(const uint32_t *)&pta[0];
      stxl = !!*(const uint32_t *)&ptl[0];
      break;
    case TX_32X32:
      stxa = !!*(const uint64_t *)&pta[0];
      stxl = !!*(const uint64_t *)&ptl[0];
      break;
    default:
      assert(0 && "Invalid transform size.");
      break;
  }
  coeff_ctx = combine_entropy_contexts(stxa, stxl);

  if (xd->mb_to_bottom_edge < 0)
    max_blocks_high += xd->mb_to_bottom_edge >> (5 + pd->subsampling_y);
  if (xd->mb_to_right_edge < 0)
    max_blocks_wide += xd->mb_to_right_edge >> (5 + pd->subsampling_x);

  *rate = 0;
  *dist = 0;
  *bsse = 0;
  *skip = 1;

  if (blk_row >= max_blocks_high || blk_col >= max_blocks_wide)
    return;

  zero_blk_rate =
      x->token_costs[tx_size][pd->plane_type][1][0][0][coeff_ctx][EOB_TOKEN];

  if (cpi->common.tx_mode == TX_MODE_SELECT || tx_size == TX_4X4) {
    mbmi->inter_tx_size[tx_idx] = tx_size;
    vp10_tx_block_rd_b(cpi, x, tx_size, blk_row, blk_col, plane, block,
                       plane_bsize, coeff_ctx, rate, dist, bsse, skip);

    if ((RDCOST(x->rdmult, x->rddiv, *rate, *dist) >=
         RDCOST(x->rdmult, x->rddiv, zero_blk_rate, *bsse) || *skip == 1) &&
        !xd->lossless[mbmi->segment_id]) {
      *rate = zero_blk_rate;
      *dist = *bsse;
      *skip = 1;
      x->blk_skip[plane][blk_row * max_blocks_wide + blk_col] = 1;
      p->eobs[block] = 0;
    } else {
      x->blk_skip[plane][blk_row * max_blocks_wide + blk_col] = 0;
      *skip = 0;
    }

    if (tx_size > TX_4X4)
      *rate += vp10_cost_bit(cpi->common.fc->txfm_partition_prob[ctx], 0);
    this_rd = RDCOST(x->rdmult, x->rddiv, *rate, *dist);
    tmp_eob = p->eobs[block];
  }

  if (tx_size > TX_4X4) {
    BLOCK_SIZE bsize = txsize_to_bsize[tx_size];
    int bsl = b_height_log2_lookup[bsize];
    int sub_step = 1 << (2 * (tx_size - 1));
    int i;
    int this_rate;
    int64_t this_dist;
    int64_t this_bsse;
    int this_skip;
    int this_cost_valid = 1;
    int64_t tmp_rd = 0;

    --bsl;
    for (i = 0; i < 4 && this_cost_valid; ++i) {
      int offsetr = (i >> 1) << bsl;
      int offsetc = (i & 0x01) << bsl;
      select_tx_block(cpi, x, blk_row + offsetr, blk_col + offsetc,
                      plane, block + i * sub_step, tx_size - 1,
                      plane_bsize, ta, tl, tx_above, tx_left,
                      &this_rate, &this_dist,
                      &this_bsse, &this_skip,
                      ref_best_rd - tmp_rd, &this_cost_valid);
      sum_rate += this_rate;
      sum_dist += this_dist;
      sum_bsse += this_bsse;
      all_skip &= this_skip;
      tmp_rd = RDCOST(x->rdmult, x->rddiv, sum_rate, sum_dist);
      if (this_rd < tmp_rd)
        break;
    }
    if (this_cost_valid)
      sum_rd = tmp_rd;
  }

  if (this_rd < sum_rd) {
    int idx, idy;
    for (i = 0; i < (1 << tx_size); ++i)
      pta[i] = ptl[i] = !(tmp_eob == 0);
    txfm_partition_update(tx_above + (blk_col >> 1),
                          tx_left + (blk_row >> 1), tx_size);
    mbmi->inter_tx_size[tx_idx] = tx_size;

    for (idy = 0; idy < (1 << tx_size) / 2; ++idy)
      for (idx = 0; idx < (1 << tx_size) / 2; ++idx)
        mbmi->inter_tx_size[tx_idx + (idy << 3) + idx] = tx_size;
    mbmi->tx_size = tx_size;
    if (this_rd == INT64_MAX)
      *is_cost_valid = 0;
    x->blk_skip[plane][blk_row * max_blocks_wide + blk_col] = *skip;
  } else {
    *rate = sum_rate;
    *dist = sum_dist;
    *bsse = sum_bsse;
    *skip = all_skip;
    if (sum_rd == INT64_MAX)
      *is_cost_valid = 0;
  }
}

static void inter_block_yrd(const VP10_COMP *cpi, MACROBLOCK *x,
                            int *rate, int64_t *distortion, int *skippable,
                            int64_t *sse, BLOCK_SIZE bsize,
                            int64_t ref_best_rd) {
  MACROBLOCKD *const xd = &x->e_mbd;
  int is_cost_valid = 1;
  int64_t this_rd = 0;

  if (ref_best_rd < 0)
    is_cost_valid = 0;

  *rate = 0;
  *distortion = 0;
  *sse = 0;
  *skippable = 1;

  if (is_cost_valid) {
    const struct macroblockd_plane *const pd = &xd->plane[0];
    const BLOCK_SIZE plane_bsize = get_plane_block_size(bsize, pd);
    const int mi_width = num_4x4_blocks_wide_lookup[plane_bsize];
    const int mi_height = num_4x4_blocks_high_lookup[plane_bsize];
    BLOCK_SIZE txb_size = txsize_to_bsize[max_txsize_lookup[plane_bsize]];
    int bh = num_4x4_blocks_wide_lookup[txb_size];
    int idx, idy;
    int block = 0;
    int step = 1 << (max_txsize_lookup[plane_bsize] * 2);
    ENTROPY_CONTEXT ctxa[16], ctxl[16];
    TXFM_CONTEXT tx_above[8], tx_left[8];

    int pnrate = 0, pnskip = 1;
    int64_t pndist = 0, pnsse = 0;

    vp10_get_entropy_contexts(bsize, TX_4X4, pd, ctxa, ctxl);
    memcpy(tx_above, xd->above_txfm_context,
           sizeof(TXFM_CONTEXT) * (mi_width >> 1));
    memcpy(tx_left, xd->left_txfm_context,
           sizeof(TXFM_CONTEXT) * (mi_height >> 1));

    for (idy = 0; idy < mi_height; idy += bh) {
      for (idx = 0; idx < mi_width; idx += bh) {
        select_tx_block(cpi, x, idy, idx, 0, block,
                        max_txsize_lookup[plane_bsize], plane_bsize,
                        ctxa, ctxl, tx_above, tx_left,
                        &pnrate, &pndist, &pnsse, &pnskip,
                        ref_best_rd - this_rd, &is_cost_valid);
        *rate += pnrate;
        *distortion += pndist;
        *sse += pnsse;
        *skippable &= pnskip;
        this_rd += VPXMIN(RDCOST(x->rdmult, x->rddiv, pnrate, pndist),
                          RDCOST(x->rdmult, x->rddiv, 0, pnsse));
        block += step;
      }
    }
  }

  this_rd = VPXMIN(RDCOST(x->rdmult, x->rddiv, *rate, *distortion),
                   RDCOST(x->rdmult, x->rddiv, 0, *sse));
  if (this_rd > ref_best_rd)
    is_cost_valid = 0;

  if (!is_cost_valid) {
    // reset cost value
    *rate = INT_MAX;
    *distortion = INT64_MAX;
    *sse = INT64_MAX;
    *skippable = 0;
  }
}

static void select_tx_type_yrd(const VP10_COMP *cpi, MACROBLOCK *x,
                               int *rate, int64_t *distortion, int *skippable,
                               int64_t *sse, BLOCK_SIZE bsize,
                               int64_t ref_best_rd) {
  const TX_SIZE max_tx_size = max_txsize_lookup[bsize];
  const VP10_COMMON *const cm = &cpi->common;
  MACROBLOCKD *const xd = &x->e_mbd;
  MB_MODE_INFO *const mbmi = &xd->mi[0]->mbmi;
  int64_t rd = INT64_MAX;
  int64_t best_rd = INT64_MAX;
  TX_TYPE tx_type, best_tx_type = DCT_DCT;
  const int is_inter = is_inter_block(mbmi);
  vpx_prob skip_prob = vp10_get_skip_prob(cm, xd);
  int s0 = vp10_cost_bit(skip_prob, 0);
  int s1 = vp10_cost_bit(skip_prob, 1);
  TX_SIZE best_tx_size[64];
  TX_SIZE best_tx = TX_SIZES;
  uint8_t best_blk_skip[256];
  const int n4 = 1 << (num_pels_log2_lookup[bsize] - 4);
  int idx, idy;
  int prune = 0;
#if CONFIG_EXT_TX
  int ext_tx_set = get_ext_tx_set(max_tx_size, bsize, is_inter);
#endif  // CONFIG_EXT_TX

  if (is_inter && cpi->sf.tx_type_search > 0)
    prune = prune_tx_types(cpi, bsize, x, xd);

  *distortion = INT64_MAX;
  *rate       = INT_MAX;
  *skippable  = 0;
  *sse        = INT64_MAX;

  for (tx_type = DCT_DCT; tx_type < TX_TYPES; ++tx_type) {
    int this_rate = 0;
    int this_skip = 1;
    int64_t this_dist = 0;
    int64_t this_sse  = 0;
#if CONFIG_EXT_TX
    if (is_inter) {
      if (!ext_tx_used_inter[ext_tx_set][tx_type])
        continue;
      if (cpi->sf.tx_type_search > 0) {
        if (!do_tx_type_search(tx_type, prune))
          continue;
      } else if (ext_tx_set == 1 &&
                 tx_type >= DST_ADST && tx_type < IDTX &&
                 best_tx_type == DCT_DCT) {
        tx_type = IDTX - 1;
        continue;
      }
    } else {
      if (!ALLOW_INTRA_EXT_TX && bsize >= BLOCK_8X8) {
        if (tx_type != intra_mode_to_tx_type_context[mbmi->mode])
          continue;
      }
      if (!ext_tx_used_intra[ext_tx_set][tx_type])
        continue;
      if (ext_tx_set == 1 &&
          tx_type >= DST_ADST && tx_type < IDTX &&
          best_tx_type == DCT_DCT) {
        tx_type = IDTX - 1;
        break;
      }
    }

    mbmi->tx_type = tx_type;

    inter_block_yrd(cpi, x, &this_rate, &this_dist, &this_skip, &this_sse,
                    bsize, ref_best_rd);

    if (get_ext_tx_types(max_tx_size, bsize, is_inter) > 1 &&
        !xd->lossless[xd->mi[0]->mbmi.segment_id] &&
        this_rate != INT_MAX) {
      if (is_inter) {
        if (ext_tx_set > 0)
          this_rate += cpi->inter_tx_type_costs[ext_tx_set]
                                       [max_tx_size][mbmi->tx_type];
      } else {
        if (ext_tx_set > 0 && ALLOW_INTRA_EXT_TX)
          this_rate += cpi->intra_tx_type_costs[ext_tx_set][max_tx_size]
                                               [mbmi->mode][mbmi->tx_type];
      }
    }
#else  // CONFIG_EXT_TX
      if (max_tx_size >= TX_32X32 && tx_type != DCT_DCT)
        continue;

      mbmi->tx_type = tx_type;

      inter_block_yrd(cpi, x, &this_rate, &this_dist, &this_skip, &this_sse,
                      bsize, ref_best_rd);

      if (max_tx_size < TX_32X32 &&
          !xd->lossless[xd->mi[0]->mbmi.segment_id] &&
          this_rate != INT_MAX) {
        if (is_inter) {
          this_rate += cpi->inter_tx_type_costs[max_tx_size][mbmi->tx_type];
          if (cpi->sf.tx_type_search > 0 && !do_tx_type_search(tx_type, prune))
              continue;
        } else {
          this_rate += cpi->intra_tx_type_costs[max_tx_size]
              [intra_mode_to_tx_type_context[mbmi->mode]]
              [mbmi->tx_type];
        }
      }
#endif  // CONFIG_EXT_TX

    if (this_rate == INT_MAX)
      continue;

    if (this_skip)
      rd = RDCOST(x->rdmult, x->rddiv, s1, this_sse);
    else
      rd = RDCOST(x->rdmult, x->rddiv, this_rate + s0, this_dist);

    if (is_inter && !xd->lossless[xd->mi[0]->mbmi.segment_id] && !this_skip)
      rd = VPXMIN(rd, RDCOST(x->rdmult, x->rddiv, s1, this_sse));

    if (rd < (is_inter && best_tx_type == DCT_DCT ? ext_tx_th : 1) * best_rd) {
      best_rd = rd;
      *distortion = this_dist;
      *rate       = this_rate;
      *skippable  = this_skip;
      *sse        = this_sse;
      best_tx_type = mbmi->tx_type;
      best_tx = mbmi->tx_size;
      memcpy(best_blk_skip, x->blk_skip[0], sizeof(best_blk_skip[0]) * n4);
      for (idy = 0; idy < xd->n8_h; ++idy)
        for (idx = 0; idx < xd->n8_w; ++idx)
          best_tx_size[idy * 8 + idx] = mbmi->inter_tx_size[idy * 8 + idx];
    }
  }

  mbmi->tx_type = best_tx_type;
  for (idy = 0; idy < xd->n8_h; ++idy)
    for (idx = 0; idx < xd->n8_w; ++idx)
      mbmi->inter_tx_size[idy * 8 + idx] = best_tx_size[idy * 8 + idx];
  mbmi->tx_size = best_tx;
  memcpy(x->blk_skip[0], best_blk_skip, sizeof(best_blk_skip[0]) * n4);
}

static void tx_block_rd(const VP10_COMP *cpi, MACROBLOCK *x,
                        int blk_row, int blk_col, int plane, int block,
                        TX_SIZE tx_size, BLOCK_SIZE plane_bsize,
                        ENTROPY_CONTEXT *above_ctx, ENTROPY_CONTEXT *left_ctx,
                        int *rate, int64_t *dist, int64_t *bsse, int *skip) {
  MACROBLOCKD *const xd = &x->e_mbd;
  MB_MODE_INFO *const mbmi = &xd->mi[0]->mbmi;
  struct macroblock_plane *const p = &x->plane[plane];
  struct macroblockd_plane *const pd = &xd->plane[plane];
  BLOCK_SIZE bsize = txsize_to_bsize[tx_size];
  int tx_idx = (blk_row >> (1 - pd->subsampling_y)) * 8 +
               (blk_col >> (1 - pd->subsampling_x));
  TX_SIZE plane_tx_size = plane ?
      get_uv_tx_size_impl(mbmi->inter_tx_size[tx_idx], bsize,
                          0, 0) :
      mbmi->inter_tx_size[tx_idx];

  int max_blocks_high = num_4x4_blocks_high_lookup[plane_bsize];
  int max_blocks_wide = num_4x4_blocks_wide_lookup[plane_bsize];

  if (xd->mb_to_bottom_edge < 0)
    max_blocks_high += xd->mb_to_bottom_edge >> (5 + pd->subsampling_y);
  if (xd->mb_to_right_edge < 0)
    max_blocks_wide += xd->mb_to_right_edge >> (5 + pd->subsampling_x);

  if (blk_row >= max_blocks_high || blk_col >= max_blocks_wide)
    return;

  if (tx_size == plane_tx_size) {
    int coeff_ctx, i;
    ENTROPY_CONTEXT *ta = above_ctx + blk_col;
    ENTROPY_CONTEXT *tl = left_ctx  + blk_row;
    switch (tx_size) {
      case TX_4X4:
        break;
      case TX_8X8:
        ta[0] = !!*(const uint16_t *)&ta[0];
        tl[0] = !!*(const uint16_t *)&tl[0];
        break;
      case TX_16X16:
        ta[0] = !!*(const uint32_t *)&ta[0];
        tl[0] = !!*(const uint32_t *)&tl[0];
        break;
      case TX_32X32:
        ta[0] = !!*(const uint64_t *)&ta[0];
        tl[0] = !!*(const uint64_t *)&tl[0];
        break;
      default:
        assert(0 && "Invalid transform size.");
        break;
    }
    coeff_ctx = combine_entropy_contexts(ta[0], tl[0]);
    vp10_tx_block_rd_b(cpi, x, tx_size, blk_row, blk_col, plane, block,
                       plane_bsize, coeff_ctx, rate, dist, bsse, skip);
    for (i = 0; i < (1 << tx_size); ++i) {
      ta[i] = !(p->eobs[block] == 0);
      tl[i] = !(p->eobs[block] == 0);
    }
  } else {
    int bsl = b_width_log2_lookup[bsize];
    int step = 1 << (2 * (tx_size - 1));
    int i;

    assert(bsl > 0);
    --bsl;

    for (i = 0; i < 4; ++i) {
      int offsetr = (i >> 1) << bsl;
      int offsetc = (i & 0x01) << bsl;
      tx_block_rd(cpi, x, blk_row + offsetr, blk_col + offsetc, plane,
                  block + i * step, tx_size - 1, plane_bsize,
                  above_ctx, left_ctx, rate, dist, bsse, skip);
    }
  }
}

// Return value 0: early termination triggered, no valid rd cost available;
//              1: rd cost values are valid.
static int inter_block_uvrd(const VP10_COMP *cpi, MACROBLOCK *x,
                            int *rate, int64_t *distortion, int *skippable,
                            int64_t *sse, BLOCK_SIZE bsize,
                            int64_t ref_best_rd) {
  MACROBLOCKD *const xd = &x->e_mbd;
  MB_MODE_INFO *const mbmi = &xd->mi[0]->mbmi;
  int plane;
  int is_cost_valid = 1;
  int64_t this_rd;

  if (ref_best_rd < 0)
    is_cost_valid = 0;

  if (is_inter_block(mbmi) && is_cost_valid) {
    int plane;
    for (plane = 1; plane < MAX_MB_PLANE; ++plane)
      vp10_subtract_plane(x, bsize, plane);
  }

  *rate = 0;
  *distortion = 0;
  *sse = 0;
  *skippable = 1;

  for (plane = 1; plane < MAX_MB_PLANE; ++plane) {
    const struct macroblockd_plane *const pd = &xd->plane[plane];
    const BLOCK_SIZE plane_bsize = get_plane_block_size(bsize, pd);
    const int mi_width = num_4x4_blocks_wide_lookup[plane_bsize];
    const int mi_height = num_4x4_blocks_high_lookup[plane_bsize];
    BLOCK_SIZE txb_size = txsize_to_bsize[max_txsize_lookup[plane_bsize]];
    int bh = num_4x4_blocks_wide_lookup[txb_size];
    int idx, idy;
    int block = 0;
    int step = 1 << (max_txsize_lookup[plane_bsize] * 2);
    int pnrate = 0, pnskip = 1;
    int64_t pndist = 0, pnsse = 0;
    ENTROPY_CONTEXT ta[16], tl[16];

    vp10_get_entropy_contexts(bsize, TX_4X4, pd, ta, tl);

    for (idy = 0; idy < mi_height; idy += bh) {
      for (idx = 0; idx < mi_width; idx += bh) {
        tx_block_rd(cpi, x, idy, idx, plane, block,
                    max_txsize_lookup[plane_bsize], plane_bsize, ta, tl,
                    &pnrate, &pndist, &pnsse, &pnskip);
        block += step;
      }
    }

    if (pnrate == INT_MAX) {
      is_cost_valid = 0;
      break;
    }

    *rate += pnrate;
    *distortion += pndist;
    *sse += pnsse;
    *skippable &= pnskip;

    this_rd = VPXMIN(RDCOST(x->rdmult, x->rddiv, *rate, *distortion),
                     RDCOST(x->rdmult, x->rddiv, 0, *sse));

    if (this_rd > ref_best_rd) {
      is_cost_valid = 0;
      break;
    }
  }

  if (!is_cost_valid) {
    // reset cost value
    *rate = INT_MAX;
    *distortion = INT64_MAX;
    *sse = INT64_MAX;
    *skippable = 0;
  }

  return is_cost_valid;
}
#endif

// Return value 0: early termination triggered, no valid rd cost available;
//              1: rd cost values are valid.
static int super_block_uvrd(const VP10_COMP *cpi, MACROBLOCK *x,
                            int *rate, int64_t *distortion, int *skippable,
                            int64_t *sse, BLOCK_SIZE bsize,
                            int64_t ref_best_rd) {
  MACROBLOCKD *const xd = &x->e_mbd;
  MB_MODE_INFO *const mbmi = &xd->mi[0]->mbmi;
  const TX_SIZE uv_tx_size = get_uv_tx_size(mbmi, &xd->plane[1]);
  int plane;
  int pnrate = 0, pnskip = 1;
  int64_t pndist = 0, pnsse = 0;
  int is_cost_valid = 1;

  if (ref_best_rd < 0)
    is_cost_valid = 0;

  if (is_inter_block(mbmi) && is_cost_valid) {
    int plane;
    for (plane = 1; plane < MAX_MB_PLANE; ++plane)
      vp10_subtract_plane(x, bsize, plane);
  }

  *rate = 0;
  *distortion = 0;
  *sse = 0;
  *skippable = 1;

  for (plane = 1; plane < MAX_MB_PLANE; ++plane) {
    txfm_rd_in_plane(x,
                     cpi,
                     &pnrate, &pndist, &pnskip, &pnsse,
                     ref_best_rd, plane, bsize, uv_tx_size,
                     cpi->sf.use_fast_coef_costing);
    if (pnrate == INT_MAX) {
      is_cost_valid = 0;
      break;
    }
    *rate += pnrate;
    *distortion += pndist;
    *sse += pnsse;
    *skippable &= pnskip;
  }

  if (!is_cost_valid) {
    // reset cost value
    *rate = INT_MAX;
    *distortion = INT64_MAX;
    *sse = INT64_MAX;
    *skippable = 0;
  }

  return is_cost_valid;
}

static void rd_pick_palette_intra_sbuv(VP10_COMP *cpi, MACROBLOCK *x,
                                       PICK_MODE_CONTEXT *ctx, int dc_mode_cost,
                                       PALETTE_MODE_INFO *palette_mode_info,
                                       uint8_t *best_palette_color_map,
                                       PREDICTION_MODE *mode_selected,
                                       int64_t *best_rd, int *rate,
                                       int *rate_tokenonly,
                                       int64_t *distortion, int *skippable) {
  MACROBLOCKD *const xd = &x->e_mbd;
  MB_MODE_INFO *const mbmi = &xd->mi[0]->mbmi;
  const BLOCK_SIZE bsize = mbmi->sb_type;
  const int rows = (4 * num_4x4_blocks_high_lookup[bsize]) >>
      (xd->plane[1].subsampling_y);
  const int cols = (4 * num_4x4_blocks_wide_lookup[bsize]) >>
      (xd->plane[1].subsampling_x);
  int this_rate, this_rate_tokenonly, s;
  int64_t this_distortion, this_rd;
  int colors_u, colors_v, colors;
  const int src_stride = x->plane[1].src.stride;
  const uint8_t *const src_u = x->plane[1].src.buf;
  const uint8_t *const src_v = x->plane[2].src.buf;

#if CONFIG_EXT_INTRA
  mbmi->ext_intra_mode_info.use_ext_intra_mode[1] = 0;
#endif  // CONFIG_EXT_INTRA

#if CONFIG_VP9_HIGHBITDEPTH
  if (cpi->common.use_highbitdepth) {
    colors_u = vp10_count_colors_highbd(src_u, src_stride, rows, cols,
                                        cpi->common.bit_depth);
    colors_v = vp10_count_colors_highbd(src_v, src_stride, rows, cols,
                                        cpi->common.bit_depth);
  } else {
#endif  // CONFIG_VP9_HIGHBITDEPTH
    colors_u = vp10_count_colors(src_u, src_stride, rows, cols);
    colors_v = vp10_count_colors(src_v, src_stride, rows, cols);
#if CONFIG_VP9_HIGHBITDEPTH
  }
#endif  // CONFIG_VP9_HIGHBITDEPTH

  colors = colors_u > colors_v ? colors_u : colors_v;
  if (colors > 1 && colors <= 64) {
    int r, c, n, i, j;
    const int max_itr = 50;
    int color_ctx, color_idx = 0;
    int color_order[PALETTE_MAX_SIZE];
    int64_t this_sse;
    double lb_u, ub_u, val_u;
    double lb_v, ub_v, val_v;
    double *const data = x->palette_buffer->kmeans_data_buf;
    uint8_t *const indices = x->palette_buffer->kmeans_indices_buf;
    uint8_t *const pre_indices = x->palette_buffer->kmeans_pre_indices_buf;
    double centroids[2 * PALETTE_MAX_SIZE];
    uint8_t *const color_map = xd->plane[1].color_index_map;
    PALETTE_MODE_INFO *const pmi = &mbmi->palette_mode_info;

#if CONFIG_VP9_HIGHBITDEPTH
    uint16_t *src_u16 = CONVERT_TO_SHORTPTR(src_u);
    uint16_t *src_v16 = CONVERT_TO_SHORTPTR(src_v);
    if (cpi->common.use_highbitdepth) {
      lb_u = src_u16[0];
      ub_u = src_u16[0];
      lb_v = src_v16[0];
      ub_v = src_v16[0];
    } else {
#endif  // CONFIG_VP9_HIGHBITDEPTH
      lb_u = src_u[0];
      ub_u = src_u[0];
      lb_v = src_v[0];
      ub_v = src_v[0];
#if CONFIG_VP9_HIGHBITDEPTH
    }
#endif  // CONFIG_VP9_HIGHBITDEPTH

    mbmi->uv_mode = DC_PRED;
#if CONFIG_EXT_INTRA
    mbmi->ext_intra_mode_info.use_ext_intra_mode[1] = 0;
#endif  // CONFIG_EXT_INTRA
    for (r = 0; r < rows; ++r) {
      for (c = 0; c < cols; ++c) {
#if CONFIG_VP9_HIGHBITDEPTH
        if (cpi->common.use_highbitdepth) {
          val_u = src_u16[r * src_stride + c];
          val_v = src_v16[r * src_stride + c];
          data[(r * cols + c) * 2 ] = val_u;
          data[(r * cols + c) * 2 + 1] = val_v;
        } else {
#endif  // CONFIG_VP9_HIGHBITDEPTH
          val_u = src_u[r * src_stride + c];
          val_v = src_v[r * src_stride + c];
          data[(r * cols + c) * 2 ] = val_u;
          data[(r * cols + c) * 2 + 1] = val_v;
#if CONFIG_VP9_HIGHBITDEPTH
        }
#endif  // CONFIG_VP9_HIGHBITDEPTH
        if (val_u < lb_u)
          lb_u = val_u;
        else if (val_u > ub_u)
          ub_u = val_u;
        if (val_v < lb_v)
          lb_v = val_v;
        else if (val_v > ub_v)
          ub_v = val_v;
      }
    }

    for (n = colors > PALETTE_MAX_SIZE ? PALETTE_MAX_SIZE : colors;
        n >= 2; --n) {
      for (i = 0; i < n; ++i) {
        centroids[i * 2] = lb_u + (2 * i + 1) * (ub_u - lb_u) / n / 2;
        centroids[i * 2 + 1] =
            lb_v + (2 * i + 1) * (ub_v - lb_v) / n / 2;;
      }
      r = vp10_k_means(data, centroids, indices, pre_indices, rows * cols, n,
                       2, max_itr);
      pmi->palette_size[1] = n;
      for (i = 1; i < 3; ++i) {
        for (j = 0; j < n; ++j) {
#if CONFIG_VP9_HIGHBITDEPTH
          if (cpi->common.use_highbitdepth)
            pmi->palette_colors[i * PALETTE_MAX_SIZE + j] =
                clip_pixel_highbd(round(centroids[j * 2 + i - 1]),
                                  cpi->common.bit_depth);
          else
#endif  // CONFIG_VP9_HIGHBITDEPTH
            pmi->palette_colors[i * PALETTE_MAX_SIZE + j] =
                clip_pixel(round(centroids[j * 2 + i - 1]));
        }
      }
      for (r = 0; r < rows; ++r)
        for (c = 0; c < cols; ++c)
          color_map[r * cols + c] = indices[r * cols + c];

      super_block_uvrd(cpi, x, &this_rate_tokenonly,
                       &this_distortion, &s, &this_sse, bsize, *best_rd);
      if (this_rate_tokenonly == INT_MAX)
        continue;
      this_rate = this_rate_tokenonly + dc_mode_cost +
          2 * cpi->common.bit_depth * n * vp10_cost_bit(128, 0) +
          cpi->palette_uv_size_cost[bsize - BLOCK_8X8][n - 2] +
          write_uniform_cost(n, color_map[0]) +
          vp10_cost_bit(vp10_default_palette_uv_mode_prob
                        [pmi->palette_size[0] > 0], 1);

      for (i = 0; i < rows; ++i) {
        for (j = (i == 0 ? 1 : 0); j < cols; ++j) {
          color_ctx = vp10_get_palette_color_context(color_map, cols, i, j, n,
                                                     color_order);
          for (r = 0; r < n; ++r)
            if (color_map[i * cols + j] == color_order[r]) {
              color_idx = r;
              break;
            }
          assert(color_idx >= 0 && color_idx < n);
          this_rate +=
              cpi->palette_uv_color_cost[n - 2][color_ctx][color_idx];
        }
      }

      this_rd = RDCOST(x->rdmult, x->rddiv, this_rate, this_distortion);
      if (this_rd < *best_rd) {
        *best_rd = this_rd;
        *palette_mode_info = *pmi;
        memcpy(best_palette_color_map, xd->plane[1].color_index_map,
               rows * cols * sizeof(best_palette_color_map[0]));
        *mode_selected = DC_PRED;
        *rate = this_rate;
        *distortion = this_distortion;
        *rate_tokenonly = this_rate_tokenonly;
        *skippable = s;
        if (!x->select_tx_size)
          swap_block_ptr(x, ctx, 2, 0, 1, MAX_MB_PLANE);
      }
    }
  }
}

#if CONFIG_EXT_INTRA
// Return 1 if an ext intra mode is selected; return 0 otherwise.
static int rd_pick_ext_intra_sbuv(VP10_COMP *cpi, MACROBLOCK *x,
                                  PICK_MODE_CONTEXT *ctx,
                                  int *rate, int *rate_tokenonly,
                                  int64_t *distortion, int *skippable,
                                  BLOCK_SIZE bsize, int64_t *best_rd) {
  MACROBLOCKD *const xd = &x->e_mbd;
  MB_MODE_INFO *mbmi = &xd->mi[0]->mbmi;
  int ext_intra_selected_flag = 0;
  int this_rate_tokenonly, this_rate, s;
  int64_t this_distortion, this_sse, this_rd;
  EXT_INTRA_MODE mode;
  EXT_INTRA_MODE_INFO ext_intra_mode_info;

  vp10_zero(ext_intra_mode_info);
  mbmi->ext_intra_mode_info.use_ext_intra_mode[1] = 1;
  mbmi->uv_mode = DC_PRED;
  mbmi->palette_mode_info.palette_size[1] = 0;

  for (mode = 0; mode < FILTER_INTRA_MODES; ++mode) {
    mbmi->ext_intra_mode_info.ext_intra_mode[1] = mode;
    if (!super_block_uvrd(cpi, x, &this_rate_tokenonly,
                          &this_distortion, &s, &this_sse, bsize, *best_rd))
      continue;

    this_rate = this_rate_tokenonly +
        vp10_cost_bit(cpi->common.fc->ext_intra_probs[1], 1) +
        cpi->intra_uv_mode_cost[mbmi->mode][mbmi->uv_mode] +
        write_uniform_cost(FILTER_INTRA_MODES, mode);
    this_rd = RDCOST(x->rdmult, x->rddiv, this_rate, this_distortion);
    if (this_rd < *best_rd) {
      *best_rd        = this_rd;
      *rate           = this_rate;
      *rate_tokenonly = this_rate_tokenonly;
      *distortion     = this_distortion;
      *skippable      = s;
      ext_intra_mode_info = mbmi->ext_intra_mode_info;
      ext_intra_selected_flag = 1;
      if (!x->select_tx_size)
        swap_block_ptr(x, ctx, 2, 0, 1, MAX_MB_PLANE);
    }
  }


  if (ext_intra_selected_flag) {
    mbmi->uv_mode = DC_PRED;
    mbmi->ext_intra_mode_info.use_ext_intra_mode[1] =
        ext_intra_mode_info.use_ext_intra_mode[1];
    mbmi->ext_intra_mode_info.ext_intra_mode[1] =
        ext_intra_mode_info.ext_intra_mode[1];
    return 1;
  } else {
    return 0;
  }
}

static void pick_intra_angle_routine_sbuv(VP10_COMP *cpi, MACROBLOCK *x,
                                          int *rate, int *rate_tokenonly,
                                          int64_t *distortion, int *skippable,
                                          int *best_angle_delta,
                                          BLOCK_SIZE bsize, int rate_overhead,
                                          int64_t *best_rd) {
  MB_MODE_INFO *mbmi = &x->e_mbd.mi[0]->mbmi;
  int this_rate_tokenonly, this_rate, s;
  int64_t this_distortion, this_sse, this_rd;

  if (!super_block_uvrd(cpi, x, &this_rate_tokenonly,
                        &this_distortion, &s, &this_sse, bsize, *best_rd))
    return;

  this_rate = this_rate_tokenonly + rate_overhead;
  this_rd = RDCOST(x->rdmult, x->rddiv, this_rate, this_distortion);
  if (this_rd < *best_rd) {
    *best_rd          = this_rd;
    *best_angle_delta = mbmi->angle_delta[1];
    *rate             = this_rate;
    *rate_tokenonly   = this_rate_tokenonly;
    *distortion       = this_distortion;
    *skippable        = s;
  }
}

static int rd_pick_intra_angle_sbuv(VP10_COMP *cpi, MACROBLOCK *x,
                                    PICK_MODE_CONTEXT *ctx,
                                    int *rate, int *rate_tokenonly,
                                    int64_t *distortion, int *skippable,
                                    BLOCK_SIZE bsize, int rate_overhead,
                                    int64_t best_rd) {
  MACROBLOCKD *const xd = &x->e_mbd;
  MB_MODE_INFO *mbmi = &xd->mi[0]->mbmi;
  int this_rate_tokenonly, this_rate, s;
  int64_t this_distortion, this_sse, this_rd;
  int angle_delta, best_angle_delta = 0;
  const double rd_adjust = 1.2;

  (void)ctx;
  *rate_tokenonly = INT_MAX;
  if (ANGLE_FAST_SEARCH) {
    int deltas_level1[3] = {0, -2, 2};
    int deltas_level2[3][2] = {
        {-1, 1}, {-3, -1}, {1, 3},
    };
    const int level1 = 3, level2 = 2;
    int i, j, best_i = -1;

    for (i = 0; i < level1; ++i) {
      int64_t tmp_best_rd;
      mbmi->angle_delta[1] = deltas_level1[i];
      tmp_best_rd = (i == 0 && best_rd < INT64_MAX) ?
          (int64_t)(best_rd * rd_adjust) : best_rd;
      if (!super_block_uvrd(cpi, x, &this_rate_tokenonly, &this_distortion,
                            &s, &this_sse, bsize, tmp_best_rd)) {
        if (i == 0)
          break;
        else
          continue;
      }
      this_rate = this_rate_tokenonly + rate_overhead;
      this_rd = RDCOST(x->rdmult, x->rddiv, this_rate, this_distortion);
      if (i == 0 && best_rd < INT64_MAX && this_rd > best_rd * rd_adjust)
        break;
      if (this_rd < best_rd) {
        best_i           = i;
        best_rd          = this_rd;
        best_angle_delta = mbmi->angle_delta[1];
        *rate            = this_rate;
        *rate_tokenonly  = this_rate_tokenonly;
        *distortion      = this_distortion;
        *skippable       = s;
      }
    }

    if (best_i >= 0) {
      for (j = 0; j < level2; ++j) {
        mbmi->angle_delta[1] = deltas_level2[best_i][j];
        pick_intra_angle_routine_sbuv(cpi, x, rate, rate_tokenonly,
                                      distortion, skippable,
                                      &best_angle_delta, bsize,
                                      rate_overhead, &best_rd);
      }
    }
  } else {
    for (angle_delta = -MAX_ANGLE_DELTAS; angle_delta <= MAX_ANGLE_DELTAS;
        ++angle_delta) {
      mbmi->angle_delta[1] = angle_delta;
      pick_intra_angle_routine_sbuv(cpi, x, rate, rate_tokenonly,
                                    distortion, skippable,
                                    &best_angle_delta, bsize,
                                    rate_overhead, &best_rd);
    }
  }

  mbmi->angle_delta[1] = best_angle_delta;
  if (*rate_tokenonly != INT_MAX)
    super_block_uvrd(cpi, x, &this_rate_tokenonly,
                     &this_distortion, &s, &this_sse, bsize, INT64_MAX);
  return *rate_tokenonly != INT_MAX;
}
#endif  // CONFIG_EXT_INTRA

static int64_t rd_pick_intra_sbuv_mode(VP10_COMP *cpi, MACROBLOCK *x,
                                       PICK_MODE_CONTEXT *ctx,
                                       int *rate, int *rate_tokenonly,
                                       int64_t *distortion, int *skippable,
                                       BLOCK_SIZE bsize, TX_SIZE max_tx_size) {
  MACROBLOCKD *xd = &x->e_mbd;
  MB_MODE_INFO *mbmi = &xd->mi[0]->mbmi;
  PREDICTION_MODE mode;
  PREDICTION_MODE mode_selected = DC_PRED;
  int64_t best_rd = INT64_MAX, this_rd;
  int this_rate_tokenonly, this_rate, s;
  int64_t this_distortion, this_sse;
  const int rows = (4 * num_4x4_blocks_high_lookup[bsize]) >>
      (xd->plane[1].subsampling_y);
  const int cols = (4 * num_4x4_blocks_wide_lookup[bsize]) >>
      (xd->plane[1].subsampling_x);
  PALETTE_MODE_INFO palette_mode_info;
  PALETTE_MODE_INFO *const pmi = &xd->mi[0]->mbmi.palette_mode_info;
  uint8_t *best_palette_color_map = NULL;
#if CONFIG_EXT_INTRA
  int is_directional_mode, rate_overhead, best_angle_delta = 0;
  EXT_INTRA_MODE_INFO ext_intra_mode_info;

  ext_intra_mode_info.use_ext_intra_mode[1] = 0;
  mbmi->ext_intra_mode_info.use_ext_intra_mode[1] = 0;
#endif  // CONFIG_EXT_INTRA
  memset(x->skip_txfm, SKIP_TXFM_NONE, sizeof(x->skip_txfm));
  palette_mode_info.palette_size[1] = 0;
  pmi->palette_size[1] = 0;
  for (mode = DC_PRED; mode <= TM_PRED; ++mode) {
    if (!(cpi->sf.intra_uv_mode_mask[max_tx_size] & (1 << mode)))
      continue;

    mbmi->uv_mode = mode;
#if CONFIG_EXT_INTRA
    is_directional_mode = (mode != DC_PRED && mode != TM_PRED);
    rate_overhead = cpi->intra_uv_mode_cost[mbmi->mode][mode] +
        write_uniform_cost(2 * MAX_ANGLE_DELTAS + 1, 0);
    mbmi->angle_delta[1] = 0;
    if (mbmi->sb_type >= BLOCK_8X8 && is_directional_mode) {
      if (!rd_pick_intra_angle_sbuv(cpi, x, ctx, &this_rate,
                                    &this_rate_tokenonly, &this_distortion, &s,
                                    bsize, rate_overhead, best_rd))
        continue;
    } else {
      if (!super_block_uvrd(cpi, x, &this_rate_tokenonly,
                            &this_distortion, &s, &this_sse, bsize, best_rd))
        continue;
    }
    this_rate = this_rate_tokenonly +
        cpi->intra_uv_mode_cost[mbmi->mode][mode];
    if (mbmi->sb_type >= BLOCK_8X8 && is_directional_mode)
      this_rate += write_uniform_cost(2 * MAX_ANGLE_DELTAS + 1,
                                      MAX_ANGLE_DELTAS +
                                      mbmi->angle_delta[1]);
    if (mode == DC_PRED)
      this_rate += vp10_cost_bit(cpi->common.fc->ext_intra_probs[1], 0);
#else
    if (!super_block_uvrd(cpi, x, &this_rate_tokenonly,
                          &this_distortion, &s, &this_sse, bsize, best_rd))
      continue;
    this_rate = this_rate_tokenonly +
        cpi->intra_uv_mode_cost[mbmi->mode][mode];
#endif  // CONFIG_EXT_INTRA
    if (cpi->common.allow_screen_content_tools && mbmi->sb_type >= BLOCK_8X8 &&
        mode == DC_PRED)
      this_rate += vp10_cost_bit(vp10_default_palette_uv_mode_prob
                                 [pmi->palette_size[0] > 0], 0);

    this_rd = RDCOST(x->rdmult, x->rddiv, this_rate, this_distortion);

    if (this_rd < best_rd) {
      mode_selected   = mode;
#if CONFIG_EXT_INTRA
      best_angle_delta = mbmi->angle_delta[1];
#endif  // CONFIG_EXT_INTRA
      best_rd         = this_rd;
      *rate           = this_rate;
      *rate_tokenonly = this_rate_tokenonly;
      *distortion     = this_distortion;
      *skippable      = s;
      if (!x->select_tx_size)
        swap_block_ptr(x, ctx, 2, 0, 1, MAX_MB_PLANE);
    }
  }

  if (cpi->common.allow_screen_content_tools && mbmi->sb_type >= BLOCK_8X8) {
    best_palette_color_map = x->palette_buffer->best_palette_color_map;
    rd_pick_palette_intra_sbuv(cpi, x, ctx,
                               cpi->intra_uv_mode_cost[mbmi->mode][DC_PRED],
                               &palette_mode_info, best_palette_color_map,
                               &mode_selected, &best_rd, rate, rate_tokenonly,
                               distortion, skippable);
  }

#if CONFIG_EXT_INTRA
  if (mbmi->sb_type >= BLOCK_8X8 && ALLOW_FILTER_INTRA_MODES) {
    if (rd_pick_ext_intra_sbuv(cpi, x, ctx, rate, rate_tokenonly, distortion,
                               skippable, bsize, &best_rd)) {
      mode_selected   = mbmi->uv_mode;
      ext_intra_mode_info = mbmi->ext_intra_mode_info;
    }
  }

  mbmi->ext_intra_mode_info.use_ext_intra_mode[1] =
      ext_intra_mode_info.use_ext_intra_mode[1];
  if (ext_intra_mode_info.use_ext_intra_mode[1]) {
    mbmi->ext_intra_mode_info.ext_intra_mode[1] =
        ext_intra_mode_info.ext_intra_mode[1];
    palette_mode_info.palette_size[1] = 0;
  }
  mbmi->angle_delta[1] = best_angle_delta;
#endif  // CONFIG_EXT_INTRA
  mbmi->uv_mode = mode_selected;
  pmi->palette_size[1] = palette_mode_info.palette_size[1];
  if (palette_mode_info.palette_size[1] > 0) {
    memcpy(pmi->palette_colors + PALETTE_MAX_SIZE,
           palette_mode_info.palette_colors + PALETTE_MAX_SIZE,
           2 * PALETTE_MAX_SIZE * sizeof(palette_mode_info.palette_colors[0]));
    memcpy(xd->plane[1].color_index_map, best_palette_color_map,
           rows * cols * sizeof(best_palette_color_map[0]));
  }

  return best_rd;
}

static int64_t rd_sbuv_dcpred(const VP10_COMP *cpi, MACROBLOCK *x,
                              int *rate, int *rate_tokenonly,
                              int64_t *distortion, int *skippable,
                              BLOCK_SIZE bsize) {
  int64_t unused;

  x->e_mbd.mi[0]->mbmi.uv_mode = DC_PRED;
  memset(x->skip_txfm, SKIP_TXFM_NONE, sizeof(x->skip_txfm));
  super_block_uvrd(cpi, x, rate_tokenonly, distortion,
                   skippable, &unused, bsize, INT64_MAX);
  *rate = *rate_tokenonly +
      cpi->intra_uv_mode_cost[x->e_mbd.mi[0]->mbmi.mode][DC_PRED];
  return RDCOST(x->rdmult, x->rddiv, *rate, *distortion);
}

static void choose_intra_uv_mode(VP10_COMP *cpi, MACROBLOCK *const x,
                                 PICK_MODE_CONTEXT *ctx,
                                 BLOCK_SIZE bsize, TX_SIZE max_tx_size,
                                 int *rate_uv, int *rate_uv_tokenonly,
                                 int64_t *dist_uv, int *skip_uv,
                                 PREDICTION_MODE *mode_uv) {
  // Use an estimated rd for uv_intra based on DC_PRED if the
  // appropriate speed flag is set.
  if (cpi->sf.use_uv_intra_rd_estimate) {
    rd_sbuv_dcpred(cpi, x, rate_uv, rate_uv_tokenonly, dist_uv,
                   skip_uv, bsize < BLOCK_8X8 ? BLOCK_8X8 : bsize);
  // Else do a proper rd search for each possible transform size that may
  // be considered in the main rd loop.
  } else {
    rd_pick_intra_sbuv_mode(cpi, x, ctx,
                            rate_uv, rate_uv_tokenonly, dist_uv, skip_uv,
                            bsize < BLOCK_8X8 ? BLOCK_8X8 : bsize, max_tx_size);
  }
  *mode_uv = x->e_mbd.mi[0]->mbmi.uv_mode;
}

static int cost_mv_ref(const VP10_COMP *cpi, PREDICTION_MODE mode,
#if CONFIG_REF_MV && CONFIG_EXT_INTER
                       int is_compound,
#endif  // CONFIG_REF_MV && CONFIG_EXT_INTER
                       int16_t mode_context) {
#if CONFIG_REF_MV
  int mode_cost = 0;
#if CONFIG_EXT_INTER
  int16_t mode_ctx = is_compound ? mode_context :
                                   (mode_context & NEWMV_CTX_MASK);
#else
  int16_t mode_ctx = mode_context & NEWMV_CTX_MASK;
#endif  // CONFIG_EXT_INTER
  int16_t is_all_zero_mv = mode_context & (1 << ALL_ZERO_FLAG_OFFSET);

  assert(is_inter_mode(mode));

#if CONFIG_EXT_INTER
  if (is_compound) {
    return cpi->inter_compound_mode_cost[mode_context]
                                        [INTER_COMPOUND_OFFSET(mode)];
  } else {
    if (mode == NEWMV || mode == NEWFROMNEARMV) {
#else
  if (mode == NEWMV) {
#endif  // CONFIG_EXT_INTER
    mode_cost = cpi->newmv_mode_cost[mode_ctx][0];
#if CONFIG_EXT_INTER
    if (!is_compound)
      mode_cost += cpi->new2mv_mode_cost[mode == NEWFROMNEARMV];
#endif  // CONFIG_EXT_INTER
    return mode_cost;
  } else {
    mode_cost = cpi->newmv_mode_cost[mode_ctx][1];
    mode_ctx = (mode_context >> ZEROMV_OFFSET) & ZEROMV_CTX_MASK;

    if (is_all_zero_mv)
      return mode_cost;

    if (mode == ZEROMV) {
      mode_cost += cpi->zeromv_mode_cost[mode_ctx][0];
      return mode_cost;
    } else {
      mode_cost += cpi->zeromv_mode_cost[mode_ctx][1];
      mode_ctx = (mode_context >> REFMV_OFFSET) & REFMV_CTX_MASK;

      if (mode_context & (1 << SKIP_NEARESTMV_OFFSET))
        mode_ctx = 6;
      if (mode_context & (1 << SKIP_NEARMV_OFFSET))
        mode_ctx = 7;
      if (mode_context & (1 << SKIP_NEARESTMV_SUB8X8_OFFSET))
        mode_ctx = 8;

      mode_cost += cpi->refmv_mode_cost[mode_ctx][mode != NEARESTMV];
      return mode_cost;
    }
  }
#if CONFIG_EXT_INTER
  }
#endif  // CONFIG_EXT_INTER
#else
  assert(is_inter_mode(mode));
#if CONFIG_EXT_INTER
  if (is_inter_compound_mode(mode)) {
    return cpi->inter_compound_mode_cost[mode_context]
                                        [INTER_COMPOUND_OFFSET(mode)];
  } else {
#endif  // CONFIG_EXT_INTER
  return cpi->inter_mode_cost[mode_context][INTER_OFFSET(mode)];
#if CONFIG_EXT_INTER
  }
#endif  // CONFIG_EXT_INTER
#endif
}

static int set_and_cost_bmi_mvs(VP10_COMP *cpi, MACROBLOCK *x, MACROBLOCKD *xd,
                                int i,
                                PREDICTION_MODE mode, int_mv this_mv[2],
                                int_mv frame_mv[MB_MODE_COUNT][MAX_REF_FRAMES],
                                int_mv seg_mvs[MAX_REF_FRAMES],
#if CONFIG_EXT_INTER
                                int_mv compound_seg_newmvs[2],
#endif  // CONFIG_EXT_INTER
                                int_mv *best_ref_mv[2], const int *mvjcost,
                                int *mvcost[2]) {
  MODE_INFO *const mic = xd->mi[0];
  const MB_MODE_INFO *const mbmi = &mic->mbmi;
  const MB_MODE_INFO_EXT *const mbmi_ext = x->mbmi_ext;
  int thismvcost = 0;
  int idx, idy;
  const int num_4x4_blocks_wide = num_4x4_blocks_wide_lookup[mbmi->sb_type];
  const int num_4x4_blocks_high = num_4x4_blocks_high_lookup[mbmi->sb_type];
  const int is_compound = has_second_ref(mbmi);
  int mode_ctx = mbmi_ext->mode_context[mbmi->ref_frame[0]];

  switch (mode) {
    case NEWMV:
#if CONFIG_EXT_INTER
    case NEWFROMNEARMV:
#endif  // CONFIG_EXT_INTER
      this_mv[0].as_int = seg_mvs[mbmi->ref_frame[0]].as_int;
#if CONFIG_EXT_INTER
      if (!cpi->common.allow_high_precision_mv ||
          !vp10_use_mv_hp(&best_ref_mv[0]->as_mv))
        lower_mv_precision(&this_mv[0].as_mv, 0);
#endif  // CONFIG_EXT_INTER
      thismvcost += vp10_mv_bit_cost(&this_mv[0].as_mv, &best_ref_mv[0]->as_mv,
                                    mvjcost, mvcost, MV_COST_WEIGHT_SUB);
#if !CONFIG_EXT_INTER
      if (is_compound) {
        this_mv[1].as_int = seg_mvs[mbmi->ref_frame[1]].as_int;
        thismvcost += vp10_mv_bit_cost(&this_mv[1].as_mv, &best_ref_mv[1]->as_mv,
                                      mvjcost, mvcost, MV_COST_WEIGHT_SUB);
      }
#endif  // !CONFIG_EXT_INTER
      break;
    case NEARMV:
    case NEARESTMV:
      this_mv[0].as_int = frame_mv[mode][mbmi->ref_frame[0]].as_int;
      if (is_compound)
        this_mv[1].as_int = frame_mv[mode][mbmi->ref_frame[1]].as_int;
      break;
    case ZEROMV:
      this_mv[0].as_int = 0;
      if (is_compound)
        this_mv[1].as_int = 0;
      break;
#if CONFIG_EXT_INTER
    case NEW_NEWMV:
      if (compound_seg_newmvs[0].as_int == INVALID_MV ||
          compound_seg_newmvs[1].as_int == INVALID_MV) {
        this_mv[0].as_int = seg_mvs[mbmi->ref_frame[0]].as_int;
        this_mv[1].as_int = seg_mvs[mbmi->ref_frame[1]].as_int;
      } else {
        this_mv[0].as_int = compound_seg_newmvs[0].as_int;
        this_mv[1].as_int = compound_seg_newmvs[1].as_int;
      }
      if (!cpi->common.allow_high_precision_mv ||
          !vp10_use_mv_hp(&best_ref_mv[0]->as_mv))
        lower_mv_precision(&this_mv[0].as_mv, 0);
      if (!cpi->common.allow_high_precision_mv ||
          !vp10_use_mv_hp(&best_ref_mv[1]->as_mv))
        lower_mv_precision(&this_mv[1].as_mv, 0);
      thismvcost += vp10_mv_bit_cost(&this_mv[0].as_mv,
                                     &best_ref_mv[0]->as_mv,
                                     mvjcost, mvcost, MV_COST_WEIGHT_SUB);
      thismvcost += vp10_mv_bit_cost(&this_mv[1].as_mv,
                                     &best_ref_mv[1]->as_mv,
                                     mvjcost, mvcost, MV_COST_WEIGHT_SUB);
      break;
    case NEW_NEARMV:
    case NEW_NEARESTMV:
      this_mv[0].as_int = seg_mvs[mbmi->ref_frame[0]].as_int;
      if (!cpi->common.allow_high_precision_mv ||
          !vp10_use_mv_hp(&best_ref_mv[0]->as_mv))
        lower_mv_precision(&this_mv[0].as_mv, 0);
      thismvcost += vp10_mv_bit_cost(&this_mv[0].as_mv, &best_ref_mv[0]->as_mv,
                                     mvjcost, mvcost, MV_COST_WEIGHT_SUB);
      this_mv[1].as_int = frame_mv[mode][mbmi->ref_frame[1]].as_int;
      break;
    case NEAR_NEWMV:
    case NEAREST_NEWMV:
      this_mv[0].as_int = frame_mv[mode][mbmi->ref_frame[0]].as_int;
      this_mv[1].as_int = seg_mvs[mbmi->ref_frame[1]].as_int;
      if (!cpi->common.allow_high_precision_mv ||
          !vp10_use_mv_hp(&best_ref_mv[1]->as_mv))
        lower_mv_precision(&this_mv[1].as_mv, 0);
      thismvcost += vp10_mv_bit_cost(&this_mv[1].as_mv, &best_ref_mv[1]->as_mv,
                                     mvjcost, mvcost, MV_COST_WEIGHT_SUB);
      break;
    case NEAREST_NEARMV:
    case NEAR_NEARESTMV:
    case NEAREST_NEARESTMV:
      this_mv[0].as_int = frame_mv[mode][mbmi->ref_frame[0]].as_int;
      this_mv[1].as_int = frame_mv[mode][mbmi->ref_frame[1]].as_int;
       break;
    case ZERO_ZEROMV:
      this_mv[0].as_int = 0;
      this_mv[1].as_int = 0;
      break;
#endif  // CONFIG_EXT_INTER
    default:
      break;
  }

  mic->bmi[i].as_mv[0].as_int = this_mv[0].as_int;
  if (is_compound)
    mic->bmi[i].as_mv[1].as_int = this_mv[1].as_int;

  mic->bmi[i].as_mode = mode;

#if CONFIG_REF_MV
  if (mode == NEWMV) {
    mic->bmi[i].pred_mv[0].as_int =
        mbmi_ext->ref_mvs[mbmi->ref_frame[0]][0].as_int;
    if (is_compound)
      mic->bmi[i].pred_mv[1].as_int =
          mbmi_ext->ref_mvs[mbmi->ref_frame[1]][0].as_int;
  } else {
    mic->bmi[i].pred_mv[0].as_int = this_mv[0].as_int;
    if (is_compound)
      mic->bmi[i].pred_mv[1].as_int = this_mv[1].as_int;
  }
#endif

  for (idy = 0; idy < num_4x4_blocks_high; ++idy)
    for (idx = 0; idx < num_4x4_blocks_wide; ++idx)
      memmove(&mic->bmi[i + idy * 2 + idx], &mic->bmi[i], sizeof(mic->bmi[i]));

#if CONFIG_REF_MV
#if CONFIG_EXT_INTER
  if (is_compound)
    mode_ctx = mbmi_ext->compound_mode_context[mbmi->ref_frame[0]];
  else
#endif  // CONFIG_EXT_INTER
  mode_ctx = vp10_mode_context_analyzer(mbmi_ext->mode_context,
                                        mbmi->ref_frame, mbmi->sb_type, i);
#endif
#if CONFIG_REF_MV && CONFIG_EXT_INTER
  return cost_mv_ref(cpi, mode, is_compound, mode_ctx) + thismvcost;
#else
  return cost_mv_ref(cpi, mode, mode_ctx) + thismvcost;
#endif  // CONFIG_REF_MV && CONFIG_EXT_INTER
}

static int64_t encode_inter_mb_segment(VP10_COMP *cpi,
                                       MACROBLOCK *x,
                                       int64_t best_yrd,
                                       int i,
                                       int *labelyrate,
                                       int64_t *distortion, int64_t *sse,
                                       ENTROPY_CONTEXT *ta,
                                       ENTROPY_CONTEXT *tl,
                                       int ir, int ic,
                                       int mi_row, int mi_col) {
  int k;
  MACROBLOCKD *xd = &x->e_mbd;
  struct macroblockd_plane *const pd = &xd->plane[0];
  struct macroblock_plane *const p = &x->plane[0];
  MODE_INFO *const mi = xd->mi[0];
  const BLOCK_SIZE plane_bsize = get_plane_block_size(mi->mbmi.sb_type, pd);
  const int width = 4 * num_4x4_blocks_wide_lookup[plane_bsize];
  const int height = 4 * num_4x4_blocks_high_lookup[plane_bsize];
  int idx, idy;
  void (*fwd_txm4x4)(const int16_t *input, tran_low_t *output, int stride);

  const uint8_t *const src =
      &p->src.buf[vp10_raster_block_offset(BLOCK_8X8, i, p->src.stride)];
  uint8_t *const dst = &pd->dst.buf[vp10_raster_block_offset(BLOCK_8X8, i,
                                                            pd->dst.stride)];
  int64_t thisdistortion = 0, thissse = 0;
  int thisrate = 0;
  TX_TYPE tx_type = get_tx_type(PLANE_TYPE_Y, xd, i, TX_4X4);
  const scan_order *so = get_scan(TX_4X4, tx_type, 1);

  vp10_build_inter_predictor_sub8x8(xd, 0, i, ir, ic, mi_row, mi_col);

#if CONFIG_VP9_HIGHBITDEPTH
  if (xd->cur_buf->flags & YV12_FLAG_HIGHBITDEPTH) {
    fwd_txm4x4 = xd->lossless[mi->mbmi.segment_id] ? vp10_highbd_fwht4x4
                                                   : vpx_highbd_fdct4x4;
  } else {
    fwd_txm4x4 = xd->lossless[mi->mbmi.segment_id] ? vp10_fwht4x4 : vpx_fdct4x4;
  }
#else
  fwd_txm4x4 = xd->lossless[mi->mbmi.segment_id] ? vp10_fwht4x4 : vpx_fdct4x4;
#endif  // CONFIG_VP9_HIGHBITDEPTH

#if CONFIG_VP9_HIGHBITDEPTH
  if (xd->cur_buf->flags & YV12_FLAG_HIGHBITDEPTH) {
    vpx_highbd_subtract_block(
        height, width, vp10_raster_block_offset_int16(BLOCK_8X8, i, p->src_diff),
        8, src, p->src.stride, dst, pd->dst.stride, xd->bd);
  } else {
    vpx_subtract_block(
        height, width, vp10_raster_block_offset_int16(BLOCK_8X8, i, p->src_diff),
        8, src, p->src.stride, dst, pd->dst.stride);
  }
#else
  vpx_subtract_block(height, width,
                     vp10_raster_block_offset_int16(BLOCK_8X8, i, p->src_diff),
                     8, src, p->src.stride, dst, pd->dst.stride);
#endif  // CONFIG_VP9_HIGHBITDEPTH

  k = i;
  for (idy = 0; idy < height / 4; ++idy) {
    for (idx = 0; idx < width / 4; ++idx) {
      int64_t dist, ssz, rd, rd1, rd2;
      tran_low_t* coeff;
#if CONFIG_VAR_TX
      int coeff_ctx;
#endif
      k += (idy * 2 + idx);
#if CONFIG_VAR_TX
      coeff_ctx = combine_entropy_contexts(*(ta + (k & 1)),
                                           *(tl + (k >> 1)));
#endif
      coeff = BLOCK_OFFSET(p->coeff, k);
      fwd_txm4x4(vp10_raster_block_offset_int16(BLOCK_8X8, k, p->src_diff),
                 coeff, 8);
      vp10_regular_quantize_b_4x4(x, 0, k, so->scan, so->iscan);
      dist_block(cpi, x, 0, k, idy + (i >> 1), idx + (i & 0x1), TX_4X4,
                 &dist, &ssz);
      thisdistortion += dist;
      thissse += ssz;
#if CONFIG_VAR_TX
      thisrate += cost_coeffs(x, 0, k, coeff_ctx,
                              TX_4X4,
                              so->scan, so->neighbors,
                              cpi->sf.use_fast_coef_costing);
      *(ta + (k & 1)) = !(p->eobs[k] == 0);
      *(tl + (k >> 1)) = !(p->eobs[k] == 0);
#else
      thisrate += cost_coeffs(x, 0, k, ta + (k & 1), tl + (k >> 1),
                              TX_4X4,
                              so->scan, so->neighbors,
                              cpi->sf.use_fast_coef_costing);
#endif
      rd1 = RDCOST(x->rdmult, x->rddiv, thisrate, thisdistortion);
      rd2 = RDCOST(x->rdmult, x->rddiv, 0, thissse);
      rd = VPXMIN(rd1, rd2);
      if (rd >= best_yrd)
        return INT64_MAX;
    }
  }

  *distortion = thisdistortion;
  *labelyrate = thisrate;
  *sse = thissse;

  return RDCOST(x->rdmult, x->rddiv, *labelyrate, *distortion);
}

typedef struct {
  int eobs;
  int brate;
  int byrate;
  int64_t bdist;
  int64_t bsse;
  int64_t brdcost;
  int_mv mvs[2];
#if CONFIG_EXT_INTER
  int_mv ref_mv[2];
#endif  // CONFIG_EXT_INTER
  ENTROPY_CONTEXT ta[2];
  ENTROPY_CONTEXT tl[2];
} SEG_RDSTAT;

typedef struct {
  int_mv *ref_mv[2];
  int_mv mvp;

  int64_t segment_rd;
  int r;
  int64_t d;
  int64_t sse;
  int segment_yrate;
  PREDICTION_MODE modes[4];
#if CONFIG_EXT_INTER
  SEG_RDSTAT rdstat[4][INTER_MODES + INTER_COMPOUND_MODES];
#else
  SEG_RDSTAT rdstat[4][INTER_MODES];
#endif  // CONFIG_EXT_INTER
  int mvthresh;
} BEST_SEG_INFO;

static INLINE int mv_check_bounds(const MACROBLOCK *x, const MV *mv) {
  return (mv->row >> 3) < x->mv_row_min ||
         (mv->row >> 3) > x->mv_row_max ||
         (mv->col >> 3) < x->mv_col_min ||
         (mv->col >> 3) > x->mv_col_max;
}

static INLINE void mi_buf_shift(MACROBLOCK *x, int i) {
  MB_MODE_INFO *const mbmi = &x->e_mbd.mi[0]->mbmi;
  struct macroblock_plane *const p = &x->plane[0];
  struct macroblockd_plane *const pd = &x->e_mbd.plane[0];

  p->src.buf = &p->src.buf[vp10_raster_block_offset(BLOCK_8X8, i,
                                                   p->src.stride)];
  assert(((intptr_t)pd->pre[0].buf & 0x7) == 0);
  pd->pre[0].buf = &pd->pre[0].buf[vp10_raster_block_offset(BLOCK_8X8, i,
                                                           pd->pre[0].stride)];
  if (has_second_ref(mbmi))
    pd->pre[1].buf = &pd->pre[1].buf[vp10_raster_block_offset(BLOCK_8X8, i,
                                                           pd->pre[1].stride)];
}

static INLINE void mi_buf_restore(MACROBLOCK *x, struct buf_2d orig_src,
                                  struct buf_2d orig_pre[2]) {
  MB_MODE_INFO *mbmi = &x->e_mbd.mi[0]->mbmi;
  x->plane[0].src = orig_src;
  x->e_mbd.plane[0].pre[0] = orig_pre[0];
  if (has_second_ref(mbmi))
    x->e_mbd.plane[0].pre[1] = orig_pre[1];
}

// Check if NEARESTMV/NEARMV/ZEROMV is the cheapest way encode zero motion.
// TODO(aconverse): Find out if this is still productive then clean up or remove
static int check_best_zero_mv(
    const VP10_COMP *cpi, const int16_t mode_context[MAX_REF_FRAMES],
#if CONFIG_REF_MV && CONFIG_EXT_INTER
    const int16_t compound_mode_context[MAX_REF_FRAMES],
#endif  // CONFIG_REF_MV && CONFIG_EXT_INTER
    int_mv frame_mv[MB_MODE_COUNT][MAX_REF_FRAMES], int this_mode,
    const MV_REFERENCE_FRAME ref_frames[2],
    const BLOCK_SIZE bsize, int block) {

#if !CONFIG_EXT_INTER
  assert(ref_frames[1] != INTRA_FRAME);  // Just sanity check
#endif

  if ((this_mode == NEARMV || this_mode == NEARESTMV || this_mode == ZEROMV) &&
      frame_mv[this_mode][ref_frames[0]].as_int == 0 &&
      (ref_frames[1] <= INTRA_FRAME ||
       frame_mv[this_mode][ref_frames[1]].as_int == 0)) {
#if CONFIG_REF_MV
    int16_t rfc = vp10_mode_context_analyzer(mode_context,
                                             ref_frames, bsize, block);
#else
    int16_t rfc = mode_context[ref_frames[0]];
#endif
#if CONFIG_REF_MV && CONFIG_EXT_INTER
    int c1 = cost_mv_ref(cpi, NEARMV, ref_frames[1] > INTRA_FRAME, rfc);
    int c2 = cost_mv_ref(cpi, NEARESTMV, ref_frames[1] > INTRA_FRAME, rfc);
    int c3 = cost_mv_ref(cpi, ZEROMV, ref_frames[1] > INTRA_FRAME, rfc);
#else
    int c1 = cost_mv_ref(cpi, NEARMV, rfc);
    int c2 = cost_mv_ref(cpi, NEARESTMV, rfc);
    int c3 = cost_mv_ref(cpi, ZEROMV, rfc);
#endif  // CONFIG_REF_MV && CONFIG_EXT_INTER

#if !CONFIG_REF_MV
    (void)bsize;
    (void)block;
#endif

    if (this_mode == NEARMV) {
      if (c1 > c3) return 0;
    } else if (this_mode == NEARESTMV) {
      if (c2 > c3) return 0;
    } else {
      assert(this_mode == ZEROMV);
      if (ref_frames[1] <= INTRA_FRAME) {
        if ((c3 >= c2 && frame_mv[NEARESTMV][ref_frames[0]].as_int == 0) ||
            (c3 >= c1 && frame_mv[NEARMV][ref_frames[0]].as_int == 0))
          return 0;
      } else {
        if ((c3 >= c2 && frame_mv[NEARESTMV][ref_frames[0]].as_int == 0 &&
             frame_mv[NEARESTMV][ref_frames[1]].as_int == 0) ||
            (c3 >= c1 && frame_mv[NEARMV][ref_frames[0]].as_int == 0 &&
             frame_mv[NEARMV][ref_frames[1]].as_int == 0))
          return 0;
      }
    }
  }
#if CONFIG_EXT_INTER
  else if ((this_mode == NEAREST_NEARESTMV || this_mode == NEAREST_NEARMV ||
            this_mode == NEAR_NEARESTMV || this_mode == ZERO_ZEROMV) &&
            frame_mv[this_mode][ref_frames[0]].as_int == 0 &&
            frame_mv[this_mode][ref_frames[1]].as_int == 0) {
#if CONFIG_REF_MV
    int16_t rfc = compound_mode_context[ref_frames[0]];
    int c1 = cost_mv_ref(cpi, NEAREST_NEARMV, 1, rfc);
    int c2 = cost_mv_ref(cpi, NEAREST_NEARESTMV, 1, rfc);
    int c3 = cost_mv_ref(cpi, ZERO_ZEROMV, 1, rfc);
    int c4 = cost_mv_ref(cpi, NEAR_NEARESTMV, 1, rfc);
#else
    int16_t rfc = mode_context[ref_frames[0]];
    int c1 = cost_mv_ref(cpi, NEAREST_NEARMV, rfc);
    int c2 = cost_mv_ref(cpi, NEAREST_NEARESTMV, rfc);
    int c3 = cost_mv_ref(cpi, ZERO_ZEROMV, rfc);
    int c4 = cost_mv_ref(cpi, NEAR_NEARESTMV, rfc);
#endif

    if (this_mode == NEAREST_NEARMV) {
      if (c1 > c3) return 0;
    } else if (this_mode == NEAREST_NEARESTMV) {
      if (c2 > c3) return 0;
    } else if (this_mode == NEAR_NEARESTMV) {
      if (c4 > c3) return 0;
    } else {
      assert(this_mode == ZERO_ZEROMV);
      if ((c3 >= c2 &&
           frame_mv[NEAREST_NEARESTMV][ref_frames[0]].as_int == 0 &&
           frame_mv[NEAREST_NEARESTMV][ref_frames[1]].as_int == 0) ||
          (c3 >= c1 &&
           frame_mv[NEAREST_NEARMV][ref_frames[0]].as_int == 0 &&
           frame_mv[NEAREST_NEARMV][ref_frames[1]].as_int == 0) ||
          (c3 >= c4 &&
           frame_mv[NEAR_NEARESTMV][ref_frames[0]].as_int == 0 &&
           frame_mv[NEAR_NEARESTMV][ref_frames[1]].as_int == 0))
        return 0;
    }
  }
#endif  // CONFIG_EXT_INTER
  return 1;
}

static void joint_motion_search(VP10_COMP *cpi, MACROBLOCK *x,
                                BLOCK_SIZE bsize,
                                int_mv *frame_mv,
                                int mi_row, int mi_col,
#if CONFIG_EXT_INTER
                                int_mv* ref_mv_sub8x8[2],
#endif
                                int_mv single_newmv[MAX_REF_FRAMES],
                                int *rate_mv,
                                const int block) {
  const VP10_COMMON *const cm = &cpi->common;
  const int pw = 4 * num_4x4_blocks_wide_lookup[bsize];
  const int ph = 4 * num_4x4_blocks_high_lookup[bsize];
  MACROBLOCKD *xd = &x->e_mbd;
  MB_MODE_INFO *mbmi = &xd->mi[0]->mbmi;
  const int refs[2] = {mbmi->ref_frame[0],
                       mbmi->ref_frame[1] < 0 ? 0 : mbmi->ref_frame[1]};
  int_mv ref_mv[2];
  int ite, ref;
  const INTERP_FILTER interp_filter = mbmi->interp_filter;
  struct scale_factors sf;

  // Do joint motion search in compound mode to get more accurate mv.
  struct buf_2d backup_yv12[2][MAX_MB_PLANE];
  int last_besterr[2] = {INT_MAX, INT_MAX};
  const YV12_BUFFER_CONFIG *const scaled_ref_frame[2] = {
    vp10_get_scaled_ref_frame(cpi, mbmi->ref_frame[0]),
    vp10_get_scaled_ref_frame(cpi, mbmi->ref_frame[1])
  };

  // Prediction buffer from second frame.
#if CONFIG_VP9_HIGHBITDEPTH
  DECLARE_ALIGNED(16, uint16_t, second_pred_alloc_16[64 * 64]);
  uint8_t *second_pred;
#else
  DECLARE_ALIGNED(16, uint8_t, second_pred[64 * 64]);
#endif  // CONFIG_VP9_HIGHBITDEPTH

  for (ref = 0; ref < 2; ++ref) {
#if CONFIG_EXT_INTER
    if (bsize < BLOCK_8X8 && ref_mv_sub8x8 != NULL)
      ref_mv[ref].as_int = ref_mv_sub8x8[ref]->as_int;
    else
#endif  // CONFIG_EXT_INTER
    ref_mv[ref] = x->mbmi_ext->ref_mvs[refs[ref]][0];

    if (scaled_ref_frame[ref]) {
      int i;
      // Swap out the reference frame for a version that's been scaled to
      // match the resolution of the current frame, allowing the existing
      // motion search code to be used without additional modifications.
      for (i = 0; i < MAX_MB_PLANE; i++)
        backup_yv12[ref][i] = xd->plane[i].pre[ref];
      vp10_setup_pre_planes(xd, ref, scaled_ref_frame[ref], mi_row, mi_col,
                           NULL);
    }

    frame_mv[refs[ref]].as_int = single_newmv[refs[ref]].as_int;
  }

  // Since we have scaled the reference frames to match the size of the current
  // frame we must use a unit scaling factor during mode selection.
#if CONFIG_VP9_HIGHBITDEPTH
  vp10_setup_scale_factors_for_frame(&sf, cm->width, cm->height,
                                     cm->width, cm->height,
                                     cm->use_highbitdepth);
#else
  vp10_setup_scale_factors_for_frame(&sf, cm->width, cm->height,
                                     cm->width, cm->height);
#endif  // CONFIG_VP9_HIGHBITDEPTH

  // Allow joint search multiple times iteratively for each reference frame
  // and break out of the search loop if it couldn't find a better mv.
  for (ite = 0; ite < 4; ite++) {
    struct buf_2d ref_yv12[2];
    int bestsme = INT_MAX;
    int sadpb = x->sadperbit16;
    MV tmp_mv;
    int search_range = 3;

    int tmp_col_min = x->mv_col_min;
    int tmp_col_max = x->mv_col_max;
    int tmp_row_min = x->mv_row_min;
    int tmp_row_max = x->mv_row_max;
    int id = ite % 2;  // Even iterations search in the first reference frame,
                       // odd iterations search in the second. The predictor
                       // found for the 'other' reference frame is factored in.

    // Initialized here because of compiler problem in Visual Studio.
    ref_yv12[0] = xd->plane[0].pre[0];
    ref_yv12[1] = xd->plane[0].pre[1];

    // Get the prediction block from the 'other' reference frame.
#if CONFIG_VP9_HIGHBITDEPTH
    if (xd->cur_buf->flags & YV12_FLAG_HIGHBITDEPTH) {
      second_pred = CONVERT_TO_BYTEPTR(second_pred_alloc_16);
      vp10_highbd_build_inter_predictor(ref_yv12[!id].buf,
                                       ref_yv12[!id].stride,
                                       second_pred, pw,
                                       &frame_mv[refs[!id]].as_mv,
                                       &sf, pw, ph, 0,
                                       interp_filter, MV_PRECISION_Q3,
                                       mi_col * MI_SIZE, mi_row * MI_SIZE,
                                       xd->bd);
    } else {
      second_pred = (uint8_t *)second_pred_alloc_16;
      vp10_build_inter_predictor(ref_yv12[!id].buf,
                                ref_yv12[!id].stride,
                                second_pred, pw,
                                &frame_mv[refs[!id]].as_mv,
                                &sf, pw, ph, 0,
                                interp_filter, MV_PRECISION_Q3,
                                mi_col * MI_SIZE, mi_row * MI_SIZE);
    }
#else
    vp10_build_inter_predictor(ref_yv12[!id].buf,
                              ref_yv12[!id].stride,
                              second_pred, pw,
                              &frame_mv[refs[!id]].as_mv,
                              &sf, pw, ph, 0,
                              interp_filter, MV_PRECISION_Q3,
                              mi_col * MI_SIZE, mi_row * MI_SIZE);
#endif  // CONFIG_VP9_HIGHBITDEPTH

    // Do compound motion search on the current reference frame.
    if (id)
      xd->plane[0].pre[0] = ref_yv12[id];
    vp10_set_mv_search_range(x, &ref_mv[id].as_mv);

    // Use the mv result from the single mode as mv predictor.
    tmp_mv = frame_mv[refs[id]].as_mv;

    tmp_mv.col >>= 3;
    tmp_mv.row >>= 3;

#if CONFIG_REF_MV
    vp10_set_mvcost(x, refs[id]);
#endif

    // Small-range full-pixel motion search.
    bestsme = vp10_refining_search_8p_c(x, &tmp_mv, sadpb,
                                       search_range,
                                       &cpi->fn_ptr[bsize],
                                       &ref_mv[id].as_mv, second_pred);
    if (bestsme < INT_MAX)
      bestsme = vp10_get_mvpred_av_var(x, &tmp_mv, &ref_mv[id].as_mv,
                                      second_pred, &cpi->fn_ptr[bsize], 1);

    x->mv_col_min = tmp_col_min;
    x->mv_col_max = tmp_col_max;
    x->mv_row_min = tmp_row_min;
    x->mv_row_max = tmp_row_max;

    if (bestsme < INT_MAX) {
      int dis; /* TODO: use dis in distortion calculation later. */
      unsigned int sse;
      if (cpi->sf.use_upsampled_references) {
        // Use up-sampled reference frames.
        struct macroblockd_plane *const pd = &xd->plane[0];
        struct buf_2d backup_pred = pd->pre[0];
        const YV12_BUFFER_CONFIG *upsampled_ref =
            get_upsampled_ref(cpi, refs[id]);

        // Set pred for Y plane
        setup_pred_plane(&pd->pre[0], upsampled_ref->y_buffer,
                         upsampled_ref->y_stride, (mi_row << 3), (mi_col << 3),
                         NULL, pd->subsampling_x, pd->subsampling_y);

        // If bsize < BLOCK_8X8, adjust pred pointer for this block
        if (bsize < BLOCK_8X8)
          pd->pre[0].buf =
              &pd->pre[0].buf[(vp10_raster_block_offset(BLOCK_8X8, block,
              pd->pre[0].stride)) << 3];

        bestsme = cpi->find_fractional_mv_step(
            x, &tmp_mv,
            &ref_mv[id].as_mv,
            cpi->common.allow_high_precision_mv,
            x->errorperbit,
            &cpi->fn_ptr[bsize],
            0, cpi->sf.mv.subpel_iters_per_step,
            NULL,
            x->nmvjointcost, x->mvcost,
            &dis, &sse, second_pred,
            pw, ph, 1);

        // Restore the reference frames.
        pd->pre[0] = backup_pred;
      } else {
        (void) block;
        bestsme = cpi->find_fractional_mv_step(
            x, &tmp_mv,
            &ref_mv[id].as_mv,
            cpi->common.allow_high_precision_mv,
            x->errorperbit,
            &cpi->fn_ptr[bsize],
            0, cpi->sf.mv.subpel_iters_per_step,
            NULL,
            x->nmvjointcost, x->mvcost,
            &dis, &sse, second_pred,
            pw, ph, 0);
        }
    }

    // Restore the pointer to the first (possibly scaled) prediction buffer.
    if (id)
      xd->plane[0].pre[0] = ref_yv12[0];

    if (bestsme < last_besterr[id]) {
      frame_mv[refs[id]].as_mv = tmp_mv;
      last_besterr[id] = bestsme;
    } else {
      break;
    }
  }

  *rate_mv = 0;

  for (ref = 0; ref < 2; ++ref) {
    if (scaled_ref_frame[ref]) {
      // Restore the prediction frame pointers to their unscaled versions.
      int i;
      for (i = 0; i < MAX_MB_PLANE; i++)
        xd->plane[i].pre[ref] = backup_yv12[ref][i];
    }

#if CONFIG_EXT_INTER
    if (bsize >= BLOCK_8X8)
#endif  // CONFIG_EXT_INTER
    *rate_mv += vp10_mv_bit_cost(&frame_mv[refs[ref]].as_mv,
                                 &x->mbmi_ext->ref_mvs[refs[ref]][0].as_mv,
                                 x->nmvjointcost, x->mvcost, MV_COST_WEIGHT);
#if CONFIG_EXT_INTER
    else
      *rate_mv += vp10_mv_bit_cost(&frame_mv[refs[ref]].as_mv,
                                   &ref_mv_sub8x8[ref]->as_mv,
                                   x->nmvjointcost, x->mvcost, MV_COST_WEIGHT);
#endif  // CONFIG_EXT_INTER
  }
}

static int64_t rd_pick_best_sub8x8_mode(VP10_COMP *cpi, MACROBLOCK *x,
                                        int_mv *best_ref_mv,
                                        int_mv *second_best_ref_mv,
                                        int64_t best_rd, int *returntotrate,
                                        int *returnyrate,
                                        int64_t *returndistortion,
                                        int *skippable, int64_t *psse,
                                        int mvthresh,
#if CONFIG_EXT_INTER
                                        int_mv seg_mvs[4][2][MAX_REF_FRAMES],
                                        int_mv compound_seg_newmvs[4][2],
#else
                                        int_mv seg_mvs[4][MAX_REF_FRAMES],
#endif  // CONFIG_EXT_INTER
                                        BEST_SEG_INFO *bsi_buf, int filter_idx,
                                        int mi_row, int mi_col) {
  int i;
  BEST_SEG_INFO *bsi = bsi_buf + filter_idx;
  MACROBLOCKD *xd = &x->e_mbd;
  MODE_INFO *mi = xd->mi[0];
  MB_MODE_INFO *mbmi = &mi->mbmi;
  int mode_idx;
  int k, br = 0, idx, idy;
  int64_t bd = 0, block_sse = 0;
  PREDICTION_MODE this_mode;
  VP10_COMMON *cm = &cpi->common;
  struct macroblock_plane *const p = &x->plane[0];
  struct macroblockd_plane *const pd = &xd->plane[0];
  const int label_count = 4;
  int64_t this_segment_rd = 0;
  int label_mv_thresh;
  int segmentyrate = 0;
  const BLOCK_SIZE bsize = mbmi->sb_type;
  const int num_4x4_blocks_wide = num_4x4_blocks_wide_lookup[bsize];
  const int num_4x4_blocks_high = num_4x4_blocks_high_lookup[bsize];
  ENTROPY_CONTEXT t_above[2], t_left[2];
  int subpelmv = 1, have_ref = 0;
  const int has_second_rf = has_second_ref(mbmi);
  const int inter_mode_mask = cpi->sf.inter_mode_mask[bsize];
  MB_MODE_INFO_EXT *const mbmi_ext = x->mbmi_ext;

  vp10_zero(*bsi);

  bsi->segment_rd = best_rd;
  bsi->ref_mv[0] = best_ref_mv;
  bsi->ref_mv[1] = second_best_ref_mv;
  bsi->mvp.as_int = best_ref_mv->as_int;
  bsi->mvthresh = mvthresh;

  for (i = 0; i < 4; i++)
    bsi->modes[i] = ZEROMV;

  memcpy(t_above, pd->above_context, sizeof(t_above));
  memcpy(t_left, pd->left_context, sizeof(t_left));

  // 64 makes this threshold really big effectively
  // making it so that we very rarely check mvs on
  // segments.   setting this to 1 would make mv thresh
  // roughly equal to what it is for macroblocks
  label_mv_thresh = 1 * bsi->mvthresh / label_count;

  // Segmentation method overheads
  for (idy = 0; idy < 2; idy += num_4x4_blocks_high) {
    for (idx = 0; idx < 2; idx += num_4x4_blocks_wide) {
      // TODO(jingning,rbultje): rewrite the rate-distortion optimization
      // loop for 4x4/4x8/8x4 block coding. to be replaced with new rd loop
      int_mv mode_mv[MB_MODE_COUNT][2];
      int_mv frame_mv[MB_MODE_COUNT][MAX_REF_FRAMES];
      PREDICTION_MODE mode_selected = ZEROMV;
      int64_t best_rd = INT64_MAX;
      const int i = idy * 2 + idx;
      int ref;
#if CONFIG_EXT_INTER
      int mv_idx;
      int_mv ref_mvs_sub8x8[2][2];
#endif  // CONFIG_EXT_INTER

      for (ref = 0; ref < 1 + has_second_rf; ++ref) {
        const MV_REFERENCE_FRAME frame = mbmi->ref_frame[ref];
#if CONFIG_EXT_INTER
        int_mv mv_ref_list[MAX_MV_REF_CANDIDATES];
        vp10_update_mv_context(cm, xd, mi, frame, mv_ref_list, i,
                               mi_row, mi_col, NULL);
#endif  // CONFIG_EXT_INTER
        frame_mv[ZEROMV][frame].as_int = 0;
        vp10_append_sub8x8_mvs_for_idx(cm, xd, i, ref, mi_row, mi_col,
#if CONFIG_EXT_INTER
                                       mv_ref_list,
#endif  // CONFIG_EXT_INTER
                                      &frame_mv[NEARESTMV][frame],
                                      &frame_mv[NEARMV][frame]);
#if CONFIG_EXT_INTER
        mv_ref_list[0].as_int = frame_mv[NEARESTMV][frame].as_int;
        mv_ref_list[1].as_int = frame_mv[NEARMV][frame].as_int;
        vp10_find_best_ref_mvs(cm->allow_high_precision_mv, mv_ref_list,
                             &ref_mvs_sub8x8[0][ref], &ref_mvs_sub8x8[1][ref]);

        if (has_second_rf) {
          frame_mv[ZERO_ZEROMV][frame].as_int = 0;
          frame_mv[NEAREST_NEARESTMV][frame].as_int =
            frame_mv[NEARESTMV][frame].as_int;

          if (ref == 0) {
            frame_mv[NEAREST_NEARMV][frame].as_int =
              frame_mv[NEARESTMV][frame].as_int;
            frame_mv[NEAR_NEARESTMV][frame].as_int =
              frame_mv[NEARMV][frame].as_int;
            frame_mv[NEAREST_NEWMV][frame].as_int =
              frame_mv[NEARESTMV][frame].as_int;
            frame_mv[NEAR_NEWMV][frame].as_int =
              frame_mv[NEARMV][frame].as_int;
          } else if (ref == 1) {
            frame_mv[NEAREST_NEARMV][frame].as_int =
              frame_mv[NEARMV][frame].as_int;
            frame_mv[NEAR_NEARESTMV][frame].as_int =
              frame_mv[NEARESTMV][frame].as_int;
            frame_mv[NEW_NEARESTMV][frame].as_int =
              frame_mv[NEARESTMV][frame].as_int;
            frame_mv[NEW_NEARMV][frame].as_int =
              frame_mv[NEARMV][frame].as_int;
          }
        }
#endif  // CONFIG_EXT_INTER
      }

      // search for the best motion vector on this segment
#if CONFIG_EXT_INTER
      for (this_mode = (has_second_rf ? NEAREST_NEARESTMV : NEARESTMV);
           this_mode <= (has_second_rf ? NEW_NEWMV : NEWFROMNEARMV);
           ++this_mode) {
#else
      for (this_mode = NEARESTMV; this_mode <= NEWMV; ++this_mode) {
#endif  // CONFIG_EXT_INTER
        const struct buf_2d orig_src = x->plane[0].src;
        struct buf_2d orig_pre[2];

        mode_idx = INTER_OFFSET(this_mode);
#if CONFIG_EXT_INTER
        mv_idx = (this_mode == NEWFROMNEARMV) ? 1 : 0;

        for (ref = 0; ref < 1 + has_second_rf; ++ref)
          bsi->ref_mv[ref]->as_int = ref_mvs_sub8x8[mv_idx][ref].as_int;
#endif  // CONFIG_EXT_INTER
        bsi->rdstat[i][mode_idx].brdcost = INT64_MAX;
        if (!(inter_mode_mask & (1 << this_mode)))
          continue;

        if (!check_best_zero_mv(cpi, mbmi_ext->mode_context,
#if CONFIG_REF_MV && CONFIG_EXT_INTER
                                mbmi_ext->compound_mode_context,
#endif  // CONFIG_REF_MV && CONFIG_EXT_INTER
                                frame_mv,
                                this_mode, mbmi->ref_frame, bsize, i))
          continue;

        memcpy(orig_pre, pd->pre, sizeof(orig_pre));
        memcpy(bsi->rdstat[i][mode_idx].ta, t_above,
               sizeof(bsi->rdstat[i][mode_idx].ta));
        memcpy(bsi->rdstat[i][mode_idx].tl, t_left,
               sizeof(bsi->rdstat[i][mode_idx].tl));

        // motion search for newmv (single predictor case only)
        if (!has_second_rf &&
#if CONFIG_EXT_INTER
            have_newmv_in_inter_mode(this_mode) &&
            seg_mvs[i][mv_idx][mbmi->ref_frame[0]].as_int == INVALID_MV
#else
            this_mode == NEWMV &&
            seg_mvs[i][mbmi->ref_frame[0]].as_int == INVALID_MV
#endif  // CONFIG_EXT_INTER
            ) {
#if CONFIG_EXT_INTER
          MV *const new_mv = &mode_mv[this_mode][0].as_mv;
#else
          MV *const new_mv = &mode_mv[NEWMV][0].as_mv;
#endif  // CONFIG_EXT_INTER
          int step_param = 0;
          int bestsme = INT_MAX;
          int sadpb = x->sadperbit4;
          MV mvp_full;
          int max_mv;
          int cost_list[5];

          /* Is the best so far sufficiently good that we cant justify doing
           * and new motion search. */
          if (best_rd < label_mv_thresh)
            break;

          if (cpi->oxcf.mode != BEST) {
#if CONFIG_EXT_INTER
            bsi->mvp.as_int = bsi->ref_mv[0]->as_int;
#else
            // use previous block's result as next block's MV predictor.
            if (i > 0) {
              bsi->mvp.as_int = mi->bmi[i - 1].as_mv[0].as_int;
              if (i == 2)
                bsi->mvp.as_int = mi->bmi[i - 2].as_mv[0].as_int;
            }
#endif  // CONFIG_EXT_INTER
          }
          if (i == 0)
            max_mv = x->max_mv_context[mbmi->ref_frame[0]];
          else
            max_mv =
                VPXMAX(abs(bsi->mvp.as_mv.row), abs(bsi->mvp.as_mv.col)) >> 3;

          if (cpi->sf.mv.auto_mv_step_size && cm->show_frame) {
            // Take wtd average of the step_params based on the last frame's
            // max mv magnitude and the best ref mvs of the current block for
            // the given reference.
            step_param = (vp10_init_search_range(max_mv) +
                              cpi->mv_step_param) / 2;
          } else {
            step_param = cpi->mv_step_param;
          }

          mvp_full.row = bsi->mvp.as_mv.row >> 3;
          mvp_full.col = bsi->mvp.as_mv.col >> 3;

          if (cpi->sf.adaptive_motion_search) {
            mvp_full.row = x->pred_mv[mbmi->ref_frame[0]].row >> 3;
            mvp_full.col = x->pred_mv[mbmi->ref_frame[0]].col >> 3;
            step_param = VPXMAX(step_param, 8);
          }

          // adjust src pointer for this block
          mi_buf_shift(x, i);

          vp10_set_mv_search_range(x, &bsi->ref_mv[0]->as_mv);

#if CONFIG_REF_MV
          vp10_set_mvcost(x, mbmi->ref_frame[0]);
#endif
          bestsme = vp10_full_pixel_search(
              cpi, x, bsize, &mvp_full, step_param, sadpb,
              cpi->sf.mv.subpel_search_method != SUBPEL_TREE ? cost_list : NULL,
              &bsi->ref_mv[0]->as_mv, new_mv,
              INT_MAX, 1);

          if (bestsme < INT_MAX) {
            int distortion;
            if (cpi->sf.use_upsampled_references) {
              const int pw = 4 * num_4x4_blocks_wide_lookup[bsize];
              const int ph = 4 * num_4x4_blocks_high_lookup[bsize];
              // Use up-sampled reference frames.
              struct macroblockd_plane *const pd = &xd->plane[0];
              struct buf_2d backup_pred = pd->pre[0];
              const YV12_BUFFER_CONFIG *upsampled_ref =
                  get_upsampled_ref(cpi, mbmi->ref_frame[0]);

              // Set pred for Y plane
              setup_pred_plane(&pd->pre[0], upsampled_ref->y_buffer,
                               upsampled_ref->y_stride,
                               (mi_row << 3), (mi_col << 3),
                               NULL, pd->subsampling_x, pd->subsampling_y);

              // adjust pred pointer for this block
              pd->pre[0].buf =
                  &pd->pre[0].buf[(vp10_raster_block_offset(BLOCK_8X8, i,
                  pd->pre[0].stride)) << 3];

              cpi->find_fractional_mv_step(
                  x,
                  new_mv,
                  &bsi->ref_mv[0]->as_mv,
                  cm->allow_high_precision_mv,
                  x->errorperbit, &cpi->fn_ptr[bsize],
                  cpi->sf.mv.subpel_force_stop,
                  cpi->sf.mv.subpel_iters_per_step,
                  cond_cost_list(cpi, cost_list),
                  x->nmvjointcost, x->mvcost,
                  &distortion,
                  &x->pred_sse[mbmi->ref_frame[0]],
                  NULL, pw, ph, 1);

              // Restore the reference frames.
              pd->pre[0] = backup_pred;
            } else {
              cpi->find_fractional_mv_step(
                  x,
                  new_mv,
                  &bsi->ref_mv[0]->as_mv,
                  cm->allow_high_precision_mv,
                  x->errorperbit, &cpi->fn_ptr[bsize],
                  cpi->sf.mv.subpel_force_stop,
                  cpi->sf.mv.subpel_iters_per_step,
                  cond_cost_list(cpi, cost_list),
                  x->nmvjointcost, x->mvcost,
                  &distortion,
                  &x->pred_sse[mbmi->ref_frame[0]],
                  NULL, 0, 0, 0);
            }

            // save motion search result for use in compound prediction
#if CONFIG_EXT_INTER
            seg_mvs[i][mv_idx][mbmi->ref_frame[0]].as_mv = *new_mv;
#else
            seg_mvs[i][mbmi->ref_frame[0]].as_mv = *new_mv;
#endif  // CONFIG_EXT_INTER
          }

          if (cpi->sf.adaptive_motion_search)
            x->pred_mv[mbmi->ref_frame[0]] = *new_mv;

          // restore src pointers
          mi_buf_restore(x, orig_src, orig_pre);
        }

        if (has_second_rf) {
#if CONFIG_EXT_INTER
          if (seg_mvs[i][mv_idx][mbmi->ref_frame[1]].as_int == INVALID_MV ||
              seg_mvs[i][mv_idx][mbmi->ref_frame[0]].as_int == INVALID_MV)
#else
          if (seg_mvs[i][mbmi->ref_frame[1]].as_int == INVALID_MV ||
              seg_mvs[i][mbmi->ref_frame[0]].as_int == INVALID_MV)
#endif  // CONFIG_EXT_INTER
            continue;
        }

        if (has_second_rf &&
#if CONFIG_EXT_INTER
            this_mode == NEW_NEWMV &&
#else
            this_mode == NEWMV &&
#endif  // CONFIG_EXT_INTER
            mbmi->interp_filter == EIGHTTAP_REGULAR) {
          // adjust src pointers
          mi_buf_shift(x, i);
          if (cpi->sf.comp_inter_joint_search_thresh <= bsize) {
            int rate_mv;
            joint_motion_search(cpi, x, bsize, frame_mv[this_mode],
                                mi_row, mi_col,
#if CONFIG_EXT_INTER
                                bsi->ref_mv,
                                seg_mvs[i][mv_idx],
#else
                                seg_mvs[i],
#endif  // CONFIG_EXT_INTER
                                &rate_mv, i);
#if CONFIG_EXT_INTER
            compound_seg_newmvs[i][0].as_int =
                frame_mv[this_mode][mbmi->ref_frame[0]].as_int;
            compound_seg_newmvs[i][1].as_int =
                frame_mv[this_mode][mbmi->ref_frame[1]].as_int;
#else
            seg_mvs[i][mbmi->ref_frame[0]].as_int =
                frame_mv[this_mode][mbmi->ref_frame[0]].as_int;
            seg_mvs[i][mbmi->ref_frame[1]].as_int =
                frame_mv[this_mode][mbmi->ref_frame[1]].as_int;
#endif  // CONFIG_EXT_INTER
          }
          // restore src pointers
          mi_buf_restore(x, orig_src, orig_pre);
        }

        bsi->rdstat[i][mode_idx].brate =
            set_and_cost_bmi_mvs(cpi, x, xd, i, this_mode, mode_mv[this_mode],
                                 frame_mv,
#if CONFIG_EXT_INTER
                                 seg_mvs[i][mv_idx],
                                 compound_seg_newmvs[i],
#else
                                 seg_mvs[i],
#endif  // CONFIG_EXT_INTER
                                 bsi->ref_mv,
                                 x->nmvjointcost, x->mvcost);

        for (ref = 0; ref < 1 + has_second_rf; ++ref) {
          bsi->rdstat[i][mode_idx].mvs[ref].as_int =
              mode_mv[this_mode][ref].as_int;
          if (num_4x4_blocks_wide > 1)
            bsi->rdstat[i + 1][mode_idx].mvs[ref].as_int =
                mode_mv[this_mode][ref].as_int;
          if (num_4x4_blocks_high > 1)
            bsi->rdstat[i + 2][mode_idx].mvs[ref].as_int =
                mode_mv[this_mode][ref].as_int;
#if CONFIG_EXT_INTER
          bsi->rdstat[i][mode_idx].ref_mv[ref].as_int =
            bsi->ref_mv[ref]->as_int;
          if (num_4x4_blocks_wide > 1)
            bsi->rdstat[i + 1][mode_idx].ref_mv[ref].as_int =
              bsi->ref_mv[ref]->as_int;
          if (num_4x4_blocks_high > 1)
            bsi->rdstat[i + 2][mode_idx].ref_mv[ref].as_int =
              bsi->ref_mv[ref]->as_int;
#endif  // CONFIG_EXT_INTER
        }

        // Trap vectors that reach beyond the UMV borders
        if (mv_check_bounds(x, &mode_mv[this_mode][0].as_mv) ||
            (has_second_rf &&
             mv_check_bounds(x, &mode_mv[this_mode][1].as_mv)))
          continue;

        if (filter_idx > 0) {
          BEST_SEG_INFO *ref_bsi = bsi_buf;
          subpelmv = 0;
          have_ref = 1;

          for (ref = 0; ref < 1 + has_second_rf; ++ref) {
            subpelmv |= mv_has_subpel(&mode_mv[this_mode][ref].as_mv);
#if CONFIG_EXT_INTER
            if (have_newmv_in_inter_mode(this_mode))
              have_ref &= (
                  (mode_mv[this_mode][ref].as_int ==
                   ref_bsi->rdstat[i][mode_idx].mvs[ref].as_int) &&
                  (bsi->ref_mv[ref]->as_int ==
                   ref_bsi->rdstat[i][mode_idx].ref_mv[ref].as_int));
            else
#endif  // CONFIG_EXT_INTER
            have_ref &= mode_mv[this_mode][ref].as_int ==
                ref_bsi->rdstat[i][mode_idx].mvs[ref].as_int;
          }

          if (filter_idx > 1 && !subpelmv && !have_ref) {
            ref_bsi = bsi_buf + 1;
            have_ref = 1;
            for (ref = 0; ref < 1 + has_second_rf; ++ref)
#if CONFIG_EXT_INTER
              if (have_newmv_in_inter_mode(this_mode))
                have_ref &= (
                    (mode_mv[this_mode][ref].as_int ==
                     ref_bsi->rdstat[i][mode_idx].mvs[ref].as_int) &&
                    (bsi->ref_mv[ref]->as_int ==
                     ref_bsi->rdstat[i][mode_idx].ref_mv[ref].as_int));
              else
#endif  // CONFIG_EXT_INTER
              have_ref &= mode_mv[this_mode][ref].as_int ==
                  ref_bsi->rdstat[i][mode_idx].mvs[ref].as_int;
          }

          if (!subpelmv && have_ref &&
              ref_bsi->rdstat[i][mode_idx].brdcost < INT64_MAX) {
            memcpy(&bsi->rdstat[i][mode_idx], &ref_bsi->rdstat[i][mode_idx],
                   sizeof(SEG_RDSTAT));
            if (num_4x4_blocks_wide > 1)
              bsi->rdstat[i + 1][mode_idx].eobs =
                  ref_bsi->rdstat[i + 1][mode_idx].eobs;
            if (num_4x4_blocks_high > 1)
              bsi->rdstat[i + 2][mode_idx].eobs =
                  ref_bsi->rdstat[i + 2][mode_idx].eobs;

            if (bsi->rdstat[i][mode_idx].brdcost < best_rd) {
              mode_selected = this_mode;
              best_rd = bsi->rdstat[i][mode_idx].brdcost;
            }
            continue;
          }
        }

        bsi->rdstat[i][mode_idx].brdcost =
            encode_inter_mb_segment(cpi, x,
                                    bsi->segment_rd - this_segment_rd, i,
                                    &bsi->rdstat[i][mode_idx].byrate,
                                    &bsi->rdstat[i][mode_idx].bdist,
                                    &bsi->rdstat[i][mode_idx].bsse,
                                    bsi->rdstat[i][mode_idx].ta,
                                    bsi->rdstat[i][mode_idx].tl,
                                    idy, idx,
                                    mi_row, mi_col);
        if (bsi->rdstat[i][mode_idx].brdcost < INT64_MAX) {
          bsi->rdstat[i][mode_idx].brdcost += RDCOST(x->rdmult, x->rddiv,
                                            bsi->rdstat[i][mode_idx].brate, 0);
          bsi->rdstat[i][mode_idx].brate += bsi->rdstat[i][mode_idx].byrate;
          bsi->rdstat[i][mode_idx].eobs = p->eobs[i];
          if (num_4x4_blocks_wide > 1)
            bsi->rdstat[i + 1][mode_idx].eobs = p->eobs[i + 1];
          if (num_4x4_blocks_high > 1)
            bsi->rdstat[i + 2][mode_idx].eobs = p->eobs[i + 2];
        }

        if (bsi->rdstat[i][mode_idx].brdcost < best_rd) {
          mode_selected = this_mode;
          best_rd = bsi->rdstat[i][mode_idx].brdcost;
        }
      } /*for each 4x4 mode*/

      if (best_rd == INT64_MAX) {
        int iy, midx;
        for (iy = i + 1; iy < 4; ++iy)
#if CONFIG_EXT_INTER
          for (midx = 0; midx < INTER_MODES + INTER_COMPOUND_MODES; ++midx)
#else
          for (midx = 0; midx < INTER_MODES; ++midx)
#endif  // CONFIG_EXT_INTER
            bsi->rdstat[iy][midx].brdcost = INT64_MAX;
        bsi->segment_rd = INT64_MAX;
        return INT64_MAX;
      }

      mode_idx = INTER_OFFSET(mode_selected);
      memcpy(t_above, bsi->rdstat[i][mode_idx].ta, sizeof(t_above));
      memcpy(t_left, bsi->rdstat[i][mode_idx].tl, sizeof(t_left));

#if CONFIG_EXT_INTER
      mv_idx = (mode_selected == NEWFROMNEARMV) ? 1 : 0;
      bsi->ref_mv[0]->as_int = bsi->rdstat[i][mode_idx].ref_mv[0].as_int;
      if (has_second_rf)
        bsi->ref_mv[1]->as_int = bsi->rdstat[i][mode_idx].ref_mv[1].as_int;
#endif  // CONFIG_EXT_INTER
      set_and_cost_bmi_mvs(cpi, x, xd, i, mode_selected, mode_mv[mode_selected],
                           frame_mv,
#if CONFIG_EXT_INTER
                           seg_mvs[i][mv_idx],
                           compound_seg_newmvs[i],
#else
                           seg_mvs[i],
#endif  // CONFIG_EXT_INTER
                           bsi->ref_mv, x->nmvjointcost, x->mvcost);

      br += bsi->rdstat[i][mode_idx].brate;
      bd += bsi->rdstat[i][mode_idx].bdist;
      block_sse += bsi->rdstat[i][mode_idx].bsse;
      segmentyrate += bsi->rdstat[i][mode_idx].byrate;
      this_segment_rd += bsi->rdstat[i][mode_idx].brdcost;

      if (this_segment_rd > bsi->segment_rd) {
        int iy, midx;
        for (iy = i + 1; iy < 4; ++iy)
#if CONFIG_EXT_INTER
          for (midx = 0; midx < INTER_MODES + INTER_COMPOUND_MODES; ++midx)
#else
          for (midx = 0; midx < INTER_MODES; ++midx)
#endif  // CONFIG_EXT_INTER
            bsi->rdstat[iy][midx].brdcost = INT64_MAX;
        bsi->segment_rd = INT64_MAX;
        return INT64_MAX;
      }
    }
  } /* for each label */

  bsi->r = br;
  bsi->d = bd;
  bsi->segment_yrate = segmentyrate;
  bsi->segment_rd = this_segment_rd;
  bsi->sse = block_sse;

  // update the coding decisions
  for (k = 0; k < 4; ++k)
    bsi->modes[k] = mi->bmi[k].as_mode;

  if (bsi->segment_rd > best_rd)
    return INT64_MAX;
  /* set it to the best */
  for (i = 0; i < 4; i++) {
    mode_idx = INTER_OFFSET(bsi->modes[i]);
    mi->bmi[i].as_mv[0].as_int = bsi->rdstat[i][mode_idx].mvs[0].as_int;
    if (has_second_ref(mbmi))
      mi->bmi[i].as_mv[1].as_int = bsi->rdstat[i][mode_idx].mvs[1].as_int;
#if CONFIG_EXT_INTER
    mi->bmi[i].ref_mv[0].as_int = bsi->rdstat[i][mode_idx].ref_mv[0].as_int;
    if (has_second_rf)
      mi->bmi[i].ref_mv[1].as_int = bsi->rdstat[i][mode_idx].ref_mv[1].as_int;
#endif  // CONFIG_EXT_INTER
    x->plane[0].eobs[i] = bsi->rdstat[i][mode_idx].eobs;
    mi->bmi[i].as_mode = bsi->modes[i];
  }

  /*
   * used to set mbmi->mv.as_int
   */
  *returntotrate = bsi->r;
  *returndistortion = bsi->d;
  *returnyrate = bsi->segment_yrate;
  *skippable = vp10_is_skippable_in_plane(x, BLOCK_8X8, 0);
  *psse = bsi->sse;
  mbmi->mode = bsi->modes[3];

  return bsi->segment_rd;
}

static void estimate_ref_frame_costs(const VP10_COMMON *cm,
                                     const MACROBLOCKD *xd,
                                     int segment_id,
                                     unsigned int *ref_costs_single,
                                     unsigned int *ref_costs_comp,
                                     vpx_prob *comp_mode_p) {
  int seg_ref_active = segfeature_active(&cm->seg, segment_id,
                                         SEG_LVL_REF_FRAME);
  if (seg_ref_active) {
    memset(ref_costs_single, 0, MAX_REF_FRAMES * sizeof(*ref_costs_single));
    memset(ref_costs_comp,   0, MAX_REF_FRAMES * sizeof(*ref_costs_comp));
    *comp_mode_p = 128;
  } else {
    vpx_prob intra_inter_p = vp10_get_intra_inter_prob(cm, xd);
    vpx_prob comp_inter_p = 128;

    if (cm->reference_mode == REFERENCE_MODE_SELECT) {
      comp_inter_p = vp10_get_reference_mode_prob(cm, xd);
      *comp_mode_p = comp_inter_p;
    } else {
      *comp_mode_p = 128;
    }

    ref_costs_single[INTRA_FRAME] = vp10_cost_bit(intra_inter_p, 0);

    if (cm->reference_mode != COMPOUND_REFERENCE) {
      vpx_prob ref_single_p1 = vp10_get_pred_prob_single_ref_p1(cm, xd);
      vpx_prob ref_single_p2 = vp10_get_pred_prob_single_ref_p2(cm, xd);
#if CONFIG_EXT_REFS
      vpx_prob ref_single_p3 = vp10_get_pred_prob_single_ref_p3(cm, xd);
      vpx_prob ref_single_p4 = vp10_get_pred_prob_single_ref_p4(cm, xd);
      vpx_prob ref_single_p5 = vp10_get_pred_prob_single_ref_p5(cm, xd);
#endif  // CONFIG_EXT_REFS
      unsigned int base_cost = vp10_cost_bit(intra_inter_p, 1);

      ref_costs_single[LAST_FRAME] =
#if CONFIG_EXT_REFS
          ref_costs_single[LAST2_FRAME] =
          ref_costs_single[LAST3_FRAME] =
          ref_costs_single[LAST4_FRAME] =
#endif  // CONFIG_EXT_REFS
          ref_costs_single[GOLDEN_FRAME] =
          ref_costs_single[ALTREF_FRAME] = base_cost;

#if CONFIG_EXT_REFS
      ref_costs_single[LAST_FRAME]   += vp10_cost_bit(ref_single_p1, 0);
      ref_costs_single[LAST2_FRAME]  += vp10_cost_bit(ref_single_p1, 0);
      ref_costs_single[LAST3_FRAME]  += vp10_cost_bit(ref_single_p1, 0);
      ref_costs_single[LAST4_FRAME]  += vp10_cost_bit(ref_single_p1, 0);
      ref_costs_single[GOLDEN_FRAME] += vp10_cost_bit(ref_single_p1, 1);
      ref_costs_single[ALTREF_FRAME] += vp10_cost_bit(ref_single_p1, 1);

      ref_costs_single[LAST_FRAME]   += vp10_cost_bit(ref_single_p3, 0);
      ref_costs_single[LAST2_FRAME]  += vp10_cost_bit(ref_single_p3, 0);
      ref_costs_single[LAST3_FRAME]  += vp10_cost_bit(ref_single_p3, 1);
      ref_costs_single[LAST4_FRAME]  += vp10_cost_bit(ref_single_p3, 1);
      ref_costs_single[GOLDEN_FRAME] += vp10_cost_bit(ref_single_p2, 0);
      ref_costs_single[ALTREF_FRAME] += vp10_cost_bit(ref_single_p2, 1);

      ref_costs_single[LAST_FRAME]   += vp10_cost_bit(ref_single_p4, 0);
      ref_costs_single[LAST2_FRAME]  += vp10_cost_bit(ref_single_p4, 1);
      ref_costs_single[LAST3_FRAME]  += vp10_cost_bit(ref_single_p5, 0);
      ref_costs_single[LAST4_FRAME]  += vp10_cost_bit(ref_single_p5, 1);
#else
      ref_costs_single[LAST_FRAME]   += vp10_cost_bit(ref_single_p1, 0);
      ref_costs_single[GOLDEN_FRAME] += vp10_cost_bit(ref_single_p1, 1);
      ref_costs_single[ALTREF_FRAME] += vp10_cost_bit(ref_single_p1, 1);
      ref_costs_single[GOLDEN_FRAME] += vp10_cost_bit(ref_single_p2, 0);
      ref_costs_single[ALTREF_FRAME] += vp10_cost_bit(ref_single_p2, 1);
#endif  // CONFIG_EXT_REFS
    } else {
      ref_costs_single[LAST_FRAME]   = 512;
#if CONFIG_EXT_REFS
      ref_costs_single[LAST2_FRAME]  = 512;
      ref_costs_single[LAST3_FRAME]  = 512;
      ref_costs_single[LAST4_FRAME]  = 512;
#endif  // CONFIG_EXT_REFS
      ref_costs_single[GOLDEN_FRAME] = 512;
      ref_costs_single[ALTREF_FRAME] = 512;
    }

    if (cm->reference_mode != SINGLE_REFERENCE) {
      vpx_prob ref_comp_p = vp10_get_pred_prob_comp_ref_p(cm, xd);
#if CONFIG_EXT_REFS
      vpx_prob ref_comp_p1 = vp10_get_pred_prob_comp_ref_p1(cm, xd);
      vpx_prob ref_comp_p2 = vp10_get_pred_prob_comp_ref_p2(cm, xd);
      vpx_prob ref_comp_p3 = vp10_get_pred_prob_comp_ref_p3(cm, xd);
#endif  // CONFIG_EXT_REFS
      unsigned int base_cost = vp10_cost_bit(intra_inter_p, 1);

      ref_costs_comp[LAST_FRAME] =
#if CONFIG_EXT_REFS
          ref_costs_comp[LAST2_FRAME] =
          ref_costs_comp[LAST3_FRAME] =
          ref_costs_comp[LAST4_FRAME] =
#endif  // CONFIG_EXT_REFS
          ref_costs_comp[GOLDEN_FRAME] = base_cost;

#if CONFIG_EXT_REFS
      ref_costs_comp[LAST_FRAME]   += vp10_cost_bit(ref_comp_p, 0);
      ref_costs_comp[LAST2_FRAME]  += vp10_cost_bit(ref_comp_p, 0);
      ref_costs_comp[LAST3_FRAME]  += vp10_cost_bit(ref_comp_p, 1);
      ref_costs_comp[LAST4_FRAME]  += vp10_cost_bit(ref_comp_p, 1);
      ref_costs_comp[GOLDEN_FRAME] += vp10_cost_bit(ref_comp_p, 1);

      ref_costs_comp[LAST_FRAME]   += vp10_cost_bit(ref_comp_p1, 1);
      ref_costs_comp[LAST2_FRAME]  += vp10_cost_bit(ref_comp_p1, 0);
      ref_costs_comp[LAST3_FRAME]  += vp10_cost_bit(ref_comp_p2, 0);
      ref_costs_comp[LAST4_FRAME]  += vp10_cost_bit(ref_comp_p2, 0);
      ref_costs_comp[GOLDEN_FRAME] += vp10_cost_bit(ref_comp_p2, 1);

      ref_costs_comp[LAST3_FRAME]  += vp10_cost_bit(ref_comp_p3, 1);
      ref_costs_comp[LAST4_FRAME]  += vp10_cost_bit(ref_comp_p3, 0);
#else
      ref_costs_comp[LAST_FRAME]   += vp10_cost_bit(ref_comp_p, 0);
      ref_costs_comp[GOLDEN_FRAME] += vp10_cost_bit(ref_comp_p, 1);
#endif  // CONFIG_EXT_REFS
    } else {
      ref_costs_comp[LAST_FRAME]   = 512;
#if CONFIG_EXT_REFS
      ref_costs_comp[LAST2_FRAME]  = 512;
      ref_costs_comp[LAST3_FRAME]  = 512;
      ref_costs_comp[LAST4_FRAME]  = 512;
#endif  // CONFIG_EXT_REFS
      ref_costs_comp[GOLDEN_FRAME] = 512;
    }
  }
}

static void store_coding_context(MACROBLOCK *x, PICK_MODE_CONTEXT *ctx,
                         int mode_index,
                         int64_t comp_pred_diff[REFERENCE_MODES],
                         int skippable) {
  MACROBLOCKD *const xd = &x->e_mbd;

  // Take a snapshot of the coding context so it can be
  // restored if we decide to encode this way
  ctx->skip = x->skip;
  ctx->skippable = skippable;
  ctx->best_mode_index = mode_index;
  ctx->mic = *xd->mi[0];
  ctx->mbmi_ext = *x->mbmi_ext;
  ctx->single_pred_diff = (int)comp_pred_diff[SINGLE_REFERENCE];
  ctx->comp_pred_diff   = (int)comp_pred_diff[COMPOUND_REFERENCE];
  ctx->hybrid_pred_diff = (int)comp_pred_diff[REFERENCE_MODE_SELECT];
}

static void setup_buffer_inter(
    VP10_COMP *cpi, MACROBLOCK *x,
    MV_REFERENCE_FRAME ref_frame,
    BLOCK_SIZE block_size,
    int mi_row, int mi_col,
    int_mv frame_nearest_mv[MAX_REF_FRAMES],
    int_mv frame_near_mv[MAX_REF_FRAMES],
    struct buf_2d yv12_mb[MAX_REF_FRAMES][MAX_MB_PLANE]) {
  const VP10_COMMON *cm = &cpi->common;
  const YV12_BUFFER_CONFIG *yv12 = get_ref_frame_buffer(cpi, ref_frame);
  MACROBLOCKD *const xd = &x->e_mbd;
  MODE_INFO *const mi = xd->mi[0];
  int_mv *const candidates = x->mbmi_ext->ref_mvs[ref_frame];
  const struct scale_factors *const sf = &cm->frame_refs[ref_frame - 1].sf;
  MB_MODE_INFO_EXT *const mbmi_ext = x->mbmi_ext;

  assert(yv12 != NULL);

  // TODO(jkoleszar): Is the UV buffer ever used here? If so, need to make this
  // use the UV scaling factors.
  vp10_setup_pred_block(xd, yv12_mb[ref_frame], yv12, mi_row, mi_col, sf, sf);

  // Gets an initial list of candidate vectors from neighbours and orders them
  vp10_find_mv_refs(cm, xd, mi, ref_frame,
#if CONFIG_REF_MV
                    &mbmi_ext->ref_mv_count[ref_frame],
                    mbmi_ext->ref_mv_stack[ref_frame],
#if CONFIG_EXT_INTER
                    mbmi_ext->compound_mode_context,
#endif  // CONFIG_EXT_INTER
#endif
                    candidates, mi_row, mi_col,
                    NULL, NULL, mbmi_ext->mode_context);

  // Candidate refinement carried out at encoder and decoder
  vp10_find_best_ref_mvs(cm->allow_high_precision_mv, candidates,
                         &frame_nearest_mv[ref_frame],
                         &frame_near_mv[ref_frame]);

  // Further refinement that is encode side only to test the top few candidates
  // in full and choose the best as the centre point for subsequent searches.
  // The current implementation doesn't support scaling.
  if (!vp10_is_scaled(sf) && block_size >= BLOCK_8X8)
    vp10_mv_pred(cpi, x, yv12_mb[ref_frame][0].buf, yv12->y_stride,
                ref_frame, block_size);
}

static void single_motion_search(VP10_COMP *cpi, MACROBLOCK *x,
                                 BLOCK_SIZE bsize,
                                 int mi_row, int mi_col,
#if CONFIG_EXT_INTER
                                 int ref_idx,
                                 int mv_idx,
#endif  // CONFIG_EXT_INTER
                                 int_mv *tmp_mv, int *rate_mv) {
  MACROBLOCKD *xd = &x->e_mbd;
  const VP10_COMMON *cm = &cpi->common;
  MB_MODE_INFO *mbmi = &xd->mi[0]->mbmi;
  struct buf_2d backup_yv12[MAX_MB_PLANE] = {{0, 0}};
  int bestsme = INT_MAX;
  int step_param;
  int sadpb = x->sadperbit16;
  MV mvp_full;
#if CONFIG_EXT_INTER
  int ref = mbmi->ref_frame[ref_idx];
  MV ref_mv = x->mbmi_ext->ref_mvs[ref][mv_idx].as_mv;
#else
  int ref = mbmi->ref_frame[0];
  MV ref_mv = x->mbmi_ext->ref_mvs[ref][0].as_mv;
  int ref_idx = 0;
#endif  // CONFIG_EXT_INTER

  int tmp_col_min = x->mv_col_min;
  int tmp_col_max = x->mv_col_max;
  int tmp_row_min = x->mv_row_min;
  int tmp_row_max = x->mv_row_max;
  int cost_list[5];

  const YV12_BUFFER_CONFIG *scaled_ref_frame = vp10_get_scaled_ref_frame(cpi,
                                                                        ref);

  MV pred_mv[3];
  pred_mv[0] = x->mbmi_ext->ref_mvs[ref][0].as_mv;
  pred_mv[1] = x->mbmi_ext->ref_mvs[ref][1].as_mv;
  pred_mv[2] = x->pred_mv[ref];

#if CONFIG_REF_MV
  vp10_set_mvcost(x, ref);
#endif

  if (scaled_ref_frame) {
    int i;
    // Swap out the reference frame for a version that's been scaled to
    // match the resolution of the current frame, allowing the existing
    // motion search code to be used without additional modifications.
    for (i = 0; i < MAX_MB_PLANE; i++)
      backup_yv12[i] = xd->plane[i].pre[ref_idx];

    vp10_setup_pre_planes(xd, ref_idx, scaled_ref_frame, mi_row, mi_col, NULL);
  }

  vp10_set_mv_search_range(x, &ref_mv);

  // Work out the size of the first step in the mv step search.
  // 0 here is maximum length first step. 1 is VPXMAX >> 1 etc.
  if (cpi->sf.mv.auto_mv_step_size && cm->show_frame) {
    // Take wtd average of the step_params based on the last frame's
    // max mv magnitude and that based on the best ref mvs of the current
    // block for the given reference.
    step_param = (vp10_init_search_range(x->max_mv_context[ref]) +
                    cpi->mv_step_param) / 2;
  } else {
    step_param = cpi->mv_step_param;
  }

  if (cpi->sf.adaptive_motion_search && bsize < BLOCK_64X64) {
    int boffset =
        2 * (b_width_log2_lookup[BLOCK_64X64] -
             VPXMIN(b_height_log2_lookup[bsize], b_width_log2_lookup[bsize]));
    step_param = VPXMAX(step_param, boffset);
  }

  if (cpi->sf.adaptive_motion_search) {
    int bwl = b_width_log2_lookup[bsize];
    int bhl = b_height_log2_lookup[bsize];
    int tlevel = x->pred_mv_sad[ref] >> (bwl + bhl + 4);

    if (tlevel < 5)
      step_param += 2;

    // prev_mv_sad is not setup for dynamically scaled frames.
    if (cpi->oxcf.resize_mode != RESIZE_DYNAMIC) {
      int i;
      for (i = LAST_FRAME; i <= ALTREF_FRAME && cm->show_frame; ++i) {
        if ((x->pred_mv_sad[ref] >> 3) > x->pred_mv_sad[i]) {
          x->pred_mv[ref].row = 0;
          x->pred_mv[ref].col = 0;
          tmp_mv->as_int = INVALID_MV;

          if (scaled_ref_frame) {
            int i;
            for (i = 0; i < MAX_MB_PLANE; ++i)
              xd->plane[i].pre[ref_idx] = backup_yv12[i];
          }
          return;
        }
      }
    }
  }

  mvp_full = pred_mv[x->mv_best_ref_index[ref]];

  mvp_full.col >>= 3;
  mvp_full.row >>= 3;

  bestsme = vp10_full_pixel_search(cpi, x, bsize, &mvp_full, step_param, sadpb,
                                   cond_cost_list(cpi, cost_list),
                                   &ref_mv, &tmp_mv->as_mv, INT_MAX, 1);

  x->mv_col_min = tmp_col_min;
  x->mv_col_max = tmp_col_max;
  x->mv_row_min = tmp_row_min;
  x->mv_row_max = tmp_row_max;

  if (bestsme < INT_MAX) {
    int dis;  /* TODO: use dis in distortion calculation later. */
    if (cpi->sf.use_upsampled_references) {
      const int pw = 4 * num_4x4_blocks_wide_lookup[bsize];
      const int ph = 4 * num_4x4_blocks_high_lookup[bsize];
      // Use up-sampled reference frames.
      struct macroblockd_plane *const pd = &xd->plane[0];
      struct buf_2d backup_pred = pd->pre[ref_idx];
      const YV12_BUFFER_CONFIG *upsampled_ref = get_upsampled_ref(cpi, ref);

      // Set pred for Y plane
      setup_pred_plane(&pd->pre[ref_idx], upsampled_ref->y_buffer,
                       upsampled_ref->y_stride, (mi_row << 3), (mi_col << 3),
                       NULL, pd->subsampling_x, pd->subsampling_y);

      bestsme = cpi->find_fractional_mv_step(x, &tmp_mv->as_mv, &ref_mv,
                                             cm->allow_high_precision_mv,
                                             x->errorperbit,
                                             &cpi->fn_ptr[bsize],
                                             cpi->sf.mv.subpel_force_stop,
                                             cpi->sf.mv.subpel_iters_per_step,
                                             cond_cost_list(cpi, cost_list),
                                             x->nmvjointcost, x->mvcost,
                                             &dis, &x->pred_sse[ref], NULL,
                                             pw, ph, 1);

      // Restore the reference frames.
      pd->pre[ref_idx] = backup_pred;
    } else {
      cpi->find_fractional_mv_step(x, &tmp_mv->as_mv, &ref_mv,
                                   cm->allow_high_precision_mv,
                                   x->errorperbit,
                                   &cpi->fn_ptr[bsize],
                                   cpi->sf.mv.subpel_force_stop,
                                   cpi->sf.mv.subpel_iters_per_step,
                                   cond_cost_list(cpi, cost_list),
                                   x->nmvjointcost, x->mvcost,
                                   &dis, &x->pred_sse[ref], NULL, 0, 0, 0);
    }
  }
  *rate_mv = vp10_mv_bit_cost(&tmp_mv->as_mv, &ref_mv,
                             x->nmvjointcost, x->mvcost, MV_COST_WEIGHT);

  if (cpi->sf.adaptive_motion_search)
    x->pred_mv[ref] = tmp_mv->as_mv;

  if (scaled_ref_frame) {
    int i;
    for (i = 0; i < MAX_MB_PLANE; i++)
      xd->plane[i].pre[ref_idx] = backup_yv12[i];
  }
}

static INLINE void restore_dst_buf(MACROBLOCKD *xd,
                                   uint8_t *orig_dst[MAX_MB_PLANE],
                                   int orig_dst_stride[MAX_MB_PLANE]) {
  int i;
  for (i = 0; i < MAX_MB_PLANE; i++) {
    xd->plane[i].dst.buf = orig_dst[i];
    xd->plane[i].dst.stride = orig_dst_stride[i];
  }
}

#if CONFIG_EXT_INTER
static void do_masked_motion_search(VP10_COMP *cpi, MACROBLOCK *x,
                                    const uint8_t *mask, int mask_stride,
                                    BLOCK_SIZE bsize,
                                    int mi_row, int mi_col,
                                    int_mv *tmp_mv, int *rate_mv,
                                    int ref_idx,
                                    int mv_idx) {
  MACROBLOCKD *xd = &x->e_mbd;
  const VP10_COMMON *cm = &cpi->common;
  MB_MODE_INFO *mbmi = &xd->mi[0]->mbmi;
  struct buf_2d backup_yv12[MAX_MB_PLANE] = {{0, 0}};
  int bestsme = INT_MAX;
  int step_param;
  int sadpb = x->sadperbit16;
  MV mvp_full;
  int ref = mbmi->ref_frame[ref_idx];
  MV ref_mv = x->mbmi_ext->ref_mvs[ref][mv_idx].as_mv;

  int tmp_col_min = x->mv_col_min;
  int tmp_col_max = x->mv_col_max;
  int tmp_row_min = x->mv_row_min;
  int tmp_row_max = x->mv_row_max;

  const YV12_BUFFER_CONFIG *scaled_ref_frame =
      vp10_get_scaled_ref_frame(cpi, ref);

  MV pred_mv[3];
  pred_mv[0] = x->mbmi_ext->ref_mvs[ref][0].as_mv;
  pred_mv[1] = x->mbmi_ext->ref_mvs[ref][1].as_mv;
  pred_mv[2] = x->pred_mv[ref];

#if CONFIG_REF_MV
  vp10_set_mvcost(x, ref);
#endif

  if (scaled_ref_frame) {
    int i;
    // Swap out the reference frame for a version that's been scaled to
    // match the resolution of the current frame, allowing the existing
    // motion search code to be used without additional modifications.
    for (i = 0; i < MAX_MB_PLANE; i++)
      backup_yv12[i] = xd->plane[i].pre[ref_idx];

    vp10_setup_pre_planes(xd, ref_idx, scaled_ref_frame, mi_row, mi_col, NULL);
  }

  vp10_set_mv_search_range(x, &ref_mv);

  // Work out the size of the first step in the mv step search.
  // 0 here is maximum length first step. 1 is MAX >> 1 etc.
  if (cpi->sf.mv.auto_mv_step_size && cm->show_frame) {
    // Take wtd average of the step_params based on the last frame's
    // max mv magnitude and that based on the best ref mvs of the current
    // block for the given reference.
    step_param = (vp10_init_search_range(x->max_mv_context[ref]) +
                  cpi->mv_step_param) / 2;
  } else {
    step_param = cpi->mv_step_param;
  }

  // TODO(debargha): is show_frame needed here?
  if (cpi->sf.adaptive_motion_search && bsize < BLOCK_LARGEST &&
      cm->show_frame) {
    int boffset = 2 * (b_width_log2_lookup[BLOCK_LARGEST] -
          VPXMIN(b_height_log2_lookup[bsize], b_width_log2_lookup[bsize]));
    step_param = VPXMAX(step_param, boffset);
  }

  if (cpi->sf.adaptive_motion_search) {
    int bwl = b_width_log2_lookup[bsize];
    int bhl = b_height_log2_lookup[bsize];
    int tlevel = x->pred_mv_sad[ref] >> (bwl + bhl + 4);

    if (tlevel < 5)
      step_param += 2;

    // prev_mv_sad is not setup for dynamically scaled frames.
    if (cpi->oxcf.resize_mode != RESIZE_DYNAMIC) {
      int i;
      for (i = LAST_FRAME; i <= ALTREF_FRAME && cm->show_frame; ++i) {
        if ((x->pred_mv_sad[ref] >> 3) > x->pred_mv_sad[i]) {
          x->pred_mv[ref].row = 0;
          x->pred_mv[ref].col = 0;
          tmp_mv->as_int = INVALID_MV;

          if (scaled_ref_frame) {
            int i;
            for (i = 0; i < MAX_MB_PLANE; ++i)
              xd->plane[i].pre[ref_idx] = backup_yv12[i];
          }
          return;
        }
      }
    }
  }

  mvp_full = pred_mv[x->mv_best_ref_index[ref]];

  mvp_full.col >>= 3;
  mvp_full.row >>= 3;

  bestsme = vp10_masked_full_pixel_diamond(cpi, x, mask, mask_stride,
                                           &mvp_full, step_param, sadpb,
                                           MAX_MVSEARCH_STEPS - 1 - step_param,
                                           1, &cpi->fn_ptr[bsize],
                                           &ref_mv, &tmp_mv->as_mv, ref_idx);

  x->mv_col_min = tmp_col_min;
  x->mv_col_max = tmp_col_max;
  x->mv_row_min = tmp_row_min;
  x->mv_row_max = tmp_row_max;

  if (bestsme < INT_MAX) {
    int dis;  /* TODO: use dis in distortion calculation later. */
    vp10_find_best_masked_sub_pixel_tree(x, mask, mask_stride,
                                         &tmp_mv->as_mv, &ref_mv,
                                         cm->allow_high_precision_mv,
                                         x->errorperbit,
                                         &cpi->fn_ptr[bsize],
                                         cpi->sf.mv.subpel_force_stop,
                                         cpi->sf.mv.subpel_iters_per_step,
                                         x->nmvjointcost, x->mvcost,
                                         &dis, &x->pred_sse[ref], ref_idx);
  }
  *rate_mv = vp10_mv_bit_cost(&tmp_mv->as_mv, &ref_mv,
                              x->nmvjointcost, x->mvcost, MV_COST_WEIGHT);

  if (cpi->sf.adaptive_motion_search && cm->show_frame)
    x->pred_mv[ref] = tmp_mv->as_mv;

  if (scaled_ref_frame) {
    int i;
    for (i = 0; i < MAX_MB_PLANE; i++)
      xd->plane[i].pre[ref_idx] = backup_yv12[i];
  }
}

static void do_masked_motion_search_indexed(VP10_COMP *cpi, MACROBLOCK *x,
                                            int wedge_index,
                                            BLOCK_SIZE bsize,
                                            int mi_row, int mi_col,
                                            int_mv *tmp_mv, int *rate_mv,
                                            int mv_idx[2],
                                            int which) {
  // NOTE: which values: 0 - 0 only, 1 - 1 only, 2 - both
  MACROBLOCKD *xd = &x->e_mbd;
  MB_MODE_INFO *mbmi = &xd->mi[0]->mbmi;
  BLOCK_SIZE sb_type = mbmi->sb_type;
  int w = (4 << b_width_log2_lookup[sb_type]);
  int h = (4 << b_height_log2_lookup[sb_type]);
  const uint8_t *mask;
  const int mask_stride = MASK_MASTER_STRIDE;
  mask = vp10_get_soft_mask(wedge_index, sb_type, h, w);

  if (which == 0 || which == 2)
    do_masked_motion_search(cpi, x, mask, mask_stride, bsize,
                            mi_row, mi_col, &tmp_mv[0], &rate_mv[0],
                            0, mv_idx[0]);

  if (which == 1 || which == 2) {
    // get the negative mask
    mask = vp10_get_soft_mask(wedge_index ^ 1, sb_type, h, w);
    do_masked_motion_search(cpi, x, mask, mask_stride, bsize,
                            mi_row, mi_col, &tmp_mv[1], &rate_mv[1],
                            1, mv_idx[1]);
  }
}
#endif  // CONFIG_EXT_INTER

// In some situations we want to discount tha pparent cost of a new motion
// vector. Where there is a subtle motion field and especially where there is
// low spatial complexity then it can be hard to cover the cost of a new motion
// vector in a single block, even if that motion vector reduces distortion.
// However, once established that vector may be usable through the nearest and
// near mv modes to reduce distortion in subsequent blocks and also improve
// visual quality.
static int discount_newmv_test(const VP10_COMP *cpi,
                               int this_mode,
                               int_mv this_mv,
                               int_mv (*mode_mv)[MAX_REF_FRAMES],
                               int ref_frame) {
  return (!cpi->rc.is_src_frame_alt_ref &&
          (this_mode == NEWMV) &&
          (this_mv.as_int != 0) &&
          ((mode_mv[NEARESTMV][ref_frame].as_int == 0) ||
           (mode_mv[NEARESTMV][ref_frame].as_int == INVALID_MV)) &&
          ((mode_mv[NEARMV][ref_frame].as_int == 0) ||
           (mode_mv[NEARMV][ref_frame].as_int == INVALID_MV)));
}

#define LEFT_TOP_MARGIN ((VP9_ENC_BORDER_IN_PIXELS - VP9_INTERP_EXTEND) << 3)
#define RIGHT_BOTTOM_MARGIN ((VP9_ENC_BORDER_IN_PIXELS -\
                                VP9_INTERP_EXTEND) << 3)

// TODO(jingning): this mv clamping function should be block size dependent.
static INLINE void clamp_mv2(MV *mv, const MACROBLOCKD *xd) {
  clamp_mv(mv, xd->mb_to_left_edge - LEFT_TOP_MARGIN,
               xd->mb_to_right_edge + RIGHT_BOTTOM_MARGIN,
               xd->mb_to_top_edge - LEFT_TOP_MARGIN,
               xd->mb_to_bottom_edge + RIGHT_BOTTOM_MARGIN);
}

static INTERP_FILTER predict_interp_filter(const VP10_COMP *cpi,
                                           const MACROBLOCK *x,
                                           const BLOCK_SIZE bsize,
                                           const int mi_row,
                                           const int mi_col,
                                           INTERP_FILTER
                                           (*single_filter)[MAX_REF_FRAMES]
                                           ) {
  INTERP_FILTER best_filter = SWITCHABLE;
  const VP10_COMMON *cm = &cpi->common;
  const MACROBLOCKD *xd = &x->e_mbd;
  int bsl = mi_width_log2_lookup[bsize];
  int pred_filter_search = cpi->sf.cb_pred_filter_search ?
      (((mi_row + mi_col) >> bsl) +
          get_chessboard_index(cm->current_video_frame)) & 0x1 : 0;
  MB_MODE_INFO *mbmi = &xd->mi[0]->mbmi;
  const int is_comp_pred = has_second_ref(mbmi);
  const int this_mode = mbmi->mode;
  int refs[2] = { mbmi->ref_frame[0],
      (mbmi->ref_frame[1] < 0 ? 0 : mbmi->ref_frame[1]) };
  if (pred_filter_search) {
    INTERP_FILTER af = SWITCHABLE, lf = SWITCHABLE;
    if (xd->up_available)
      af = xd->mi[-xd->mi_stride]->mbmi.interp_filter;
    if (xd->left_available)
      lf = xd->mi[-1]->mbmi.interp_filter;

#if CONFIG_EXT_INTER
    if ((this_mode != NEWMV && this_mode != NEWFROMNEARMV &&
        this_mode != NEW_NEWMV) || (af == lf))
#else
    if ((this_mode != NEWMV) || (af == lf))
#endif  // CONFIG_EXT_INTER
      best_filter = af;
  }
  if (is_comp_pred) {
    if (cpi->sf.adaptive_mode_search) {
#if CONFIG_EXT_INTER
      switch (this_mode) {
        case NEAREST_NEARESTMV:
          if (single_filter[NEARESTMV][refs[0]] ==
              single_filter[NEARESTMV][refs[1]])
            best_filter = single_filter[NEARESTMV][refs[0]];
          break;
        case NEAREST_NEARMV:
          if (single_filter[NEARESTMV][refs[0]] ==
              single_filter[NEARMV][refs[1]])
            best_filter = single_filter[NEARESTMV][refs[0]];
          break;
        case NEAR_NEARESTMV:
          if (single_filter[NEARMV][refs[0]] ==
              single_filter[NEARESTMV][refs[1]])
            best_filter = single_filter[NEARMV][refs[0]];
          break;
        case ZERO_ZEROMV:
          if (single_filter[ZEROMV][refs[0]] ==
              single_filter[ZEROMV][refs[1]])
            best_filter = single_filter[ZEROMV][refs[0]];
          break;
        case NEW_NEWMV:
          if (single_filter[NEWMV][refs[0]] ==
              single_filter[NEWMV][refs[1]])
            best_filter = single_filter[NEWMV][refs[0]];
          break;
        case NEAREST_NEWMV:
          if (single_filter[NEARESTMV][refs[0]] ==
              single_filter[NEWMV][refs[1]])
            best_filter = single_filter[NEARESTMV][refs[0]];
          break;
        case NEAR_NEWMV:
          if (single_filter[NEARMV][refs[0]] ==
              single_filter[NEWMV][refs[1]])
            best_filter = single_filter[NEARMV][refs[0]];
          break;
        case NEW_NEARESTMV:
          if (single_filter[NEWMV][refs[0]] ==
              single_filter[NEARESTMV][refs[1]])
            best_filter = single_filter[NEWMV][refs[0]];
          break;
        case NEW_NEARMV:
          if (single_filter[NEWMV][refs[0]] ==
              single_filter[NEARMV][refs[1]])
            best_filter = single_filter[NEWMV][refs[0]];
          break;
        default:
          if (single_filter[this_mode][refs[0]] ==
              single_filter[this_mode][refs[1]])
            best_filter = single_filter[this_mode][refs[0]];
          break;
      }
#else
      if (single_filter[this_mode][refs[0]] ==
          single_filter[this_mode][refs[1]])
        best_filter = single_filter[this_mode][refs[0]];
#endif  // CONFIG_EXT_INTER
    }
  }
  if (cm->interp_filter != BILINEAR) {
    if (x->source_variance < cpi->sf.disable_filter_search_var_thresh) {
      best_filter = EIGHTTAP_REGULAR;
    }
#if CONFIG_EXT_INTERP
    else if (!vp10_is_interp_needed(xd) && cm->interp_filter == SWITCHABLE) {
      best_filter = EIGHTTAP_REGULAR;
    }
#endif
  }
  return best_filter;
}

static int64_t handle_inter_mode(VP10_COMP *cpi, MACROBLOCK *x,
                                 BLOCK_SIZE bsize,
                                 int *rate2, int64_t *distortion,
                                 int *skippable,
                                 int *rate_y, int *rate_uv,
                                 int *disable_skip,
                                 int_mv (*mode_mv)[MAX_REF_FRAMES],
                                 int mi_row, int mi_col,
#if CONFIG_OBMC
                                 uint8_t *dst_buf1[3],
                                 int dst_stride1[3],
                                 uint8_t *dst_buf2[3],
                                 int dst_stride2[3],
#endif  // CONFIG_OBMC
#if CONFIG_EXT_INTER
                                 int_mv single_newmvs[2][MAX_REF_FRAMES],
                                 int single_newmvs_rate[2][MAX_REF_FRAMES],
                                 int *compmode_interintra_cost,
                                 int *compmode_wedge_cost,
#else
                                 int_mv single_newmv[MAX_REF_FRAMES],
#endif  // CONFIG_EXT_INTER
                                 INTERP_FILTER (*single_filter)[MAX_REF_FRAMES],
                                 int (*single_skippable)[MAX_REF_FRAMES],
                                 int64_t *psse,
                                 const int64_t ref_best_rd) {
  VP10_COMMON *cm = &cpi->common;
  MACROBLOCKD *xd = &x->e_mbd;
  MB_MODE_INFO *mbmi = &xd->mi[0]->mbmi;
  MB_MODE_INFO_EXT *const mbmi_ext = x->mbmi_ext;
  const int is_comp_pred = has_second_ref(mbmi);
  const int this_mode = mbmi->mode;
  int_mv *frame_mv = mode_mv[this_mode];
  int i;
  int refs[2] = { mbmi->ref_frame[0],
    (mbmi->ref_frame[1] < 0 ? 0 : mbmi->ref_frame[1]) };
  int_mv cur_mv[2];
  int rate_mv = 0;
#if CONFIG_EXT_INTER
  int mv_idx = (this_mode == NEWFROMNEARMV) ? 1 : 0;
  int_mv single_newmv[MAX_REF_FRAMES];
  const int * const intra_mode_cost =
    cpi->mbmode_cost[size_group_lookup[bsize]];
  const int is_comp_interintra_pred = (mbmi->ref_frame[1] == INTRA_FRAME);
  const int tmp_buf_sz = CU_SIZE * CU_SIZE;
#if CONFIG_REF_MV
  uint8_t ref_frame_type = vp10_ref_frame_type(mbmi->ref_frame);
#endif
#endif  // CONFIG_EXT_INTER
#if CONFIG_VP9_HIGHBITDEPTH
  DECLARE_ALIGNED(16, uint16_t, tmp_buf16[MAX_MB_PLANE * CU_SIZE * CU_SIZE]);
  uint8_t *tmp_buf;
#else
  DECLARE_ALIGNED(16, uint8_t, tmp_buf[MAX_MB_PLANE * CU_SIZE * CU_SIZE]);
#endif  // CONFIG_VP9_HIGHBITDEPTH

#if CONFIG_OBMC
  int allow_obmc =
#if CONFIG_EXT_INTER
      !is_comp_interintra_pred &&
#endif  // CONFIG_EXT_INTER
      is_obmc_allowed(mbmi);
  int rate2_nocoeff, best_rate2 = INT_MAX,
      best_skippable, best_xskip, best_disable_skip = 0;
#if CONFIG_SUPERTX
  int best_rate_y, best_rate_uv;
#endif  // CONFIG_SUPERTX
#if CONFIG_VAR_TX
  uint8_t best_blk_skip[3][256];
#endif  // CONFIG_VAR_TX
  int64_t best_distortion = INT64_MAX;
  unsigned int best_pred_var = UINT_MAX;
  MB_MODE_INFO best_mbmi;
#endif  // CONFIG_OBMC

  int pred_exists = 0;
  int intpel_mv;
  int64_t rd, tmp_rd, best_rd = INT64_MAX;
  int best_needs_copy = 0;
  uint8_t *orig_dst[MAX_MB_PLANE];
  int orig_dst_stride[MAX_MB_PLANE];
  int rs = 0;
  INTERP_FILTER best_filter = SWITCHABLE;
  uint8_t skip_txfm[MAX_MB_PLANE << 2] = {0};
  int64_t bsse[MAX_MB_PLANE << 2] = {0};

  int skip_txfm_sb = 0;
  int64_t skip_sse_sb = INT64_MAX;
  int64_t distortion_y = 0, distortion_uv = 0;
  int16_t mode_ctx = mbmi_ext->mode_context[refs[0]];

#if CONFIG_EXT_INTER
  *compmode_interintra_cost = 0;
  mbmi->use_wedge_interintra = 0;
  *compmode_wedge_cost = 0;
  mbmi->use_wedge_interinter = 0;

  // is_comp_interintra_pred implies !is_comp_pred
  assert(!is_comp_interintra_pred || (!is_comp_pred));
  // is_comp_interintra_pred implies is_interintra_allowed(mbmi->sb_type)
  assert(!is_comp_interintra_pred || is_interintra_allowed(mbmi));
#endif  // CONFIG_EXT_INTER

#if CONFIG_OBMC
  tmp_rd = 0;
#endif  // CONFIG_OBMC
#if CONFIG_REF_MV
#if CONFIG_EXT_INTER
  if (is_comp_pred)
    mode_ctx = mbmi_ext->compound_mode_context[refs[0]];
  else
#endif  // CONFIG_EXT_INTER
  mode_ctx = vp10_mode_context_analyzer(mbmi_ext->mode_context,
                                        mbmi->ref_frame, bsize, -1);
#endif

#if CONFIG_VP9_HIGHBITDEPTH
  if (xd->cur_buf->flags & YV12_FLAG_HIGHBITDEPTH) {
    tmp_buf = CONVERT_TO_BYTEPTR(tmp_buf16);
  } else {
    tmp_buf = (uint8_t *)tmp_buf16;
  }
#endif  // CONFIG_VP9_HIGHBITDEPTH

  if (is_comp_pred) {
    if (frame_mv[refs[0]].as_int == INVALID_MV ||
        frame_mv[refs[1]].as_int == INVALID_MV)
      return INT64_MAX;
  }

  if (have_newmv_in_inter_mode(this_mode)) {
    if (is_comp_pred) {
#if CONFIG_EXT_INTER
      for (i = 0; i < 2; ++i) {
        single_newmv[refs[i]].as_int =
          single_newmvs[mv_idx][refs[i]].as_int;
      }

      if (this_mode == NEW_NEWMV) {
        frame_mv[refs[0]].as_int = single_newmv[refs[0]].as_int;
        frame_mv[refs[1]].as_int = single_newmv[refs[1]].as_int;

        if (cpi->sf.comp_inter_joint_search_thresh <= bsize) {
          joint_motion_search(cpi, x, bsize, frame_mv,
                              mi_row, mi_col, NULL, single_newmv, &rate_mv, 0);
        } else {
          rate_mv  = vp10_mv_bit_cost(&frame_mv[refs[0]].as_mv,
                                      &x->mbmi_ext->ref_mvs[refs[0]][0].as_mv,
                                      x->nmvjointcost, x->mvcost,
                                      MV_COST_WEIGHT);
          rate_mv += vp10_mv_bit_cost(&frame_mv[refs[1]].as_mv,
                                      &x->mbmi_ext->ref_mvs[refs[1]][0].as_mv,
                                      x->nmvjointcost, x->mvcost,
                                      MV_COST_WEIGHT);
        }
      } else if (this_mode == NEAREST_NEWMV || this_mode == NEAR_NEWMV) {
        frame_mv[refs[1]].as_int = single_newmv[refs[1]].as_int;
        rate_mv = vp10_mv_bit_cost(&frame_mv[refs[1]].as_mv,
                                   &x->mbmi_ext->ref_mvs[refs[1]][0].as_mv,
                                   x->nmvjointcost, x->mvcost, MV_COST_WEIGHT);
      } else {
        frame_mv[refs[0]].as_int = single_newmv[refs[0]].as_int;
        rate_mv = vp10_mv_bit_cost(&frame_mv[refs[0]].as_mv,
                                   &x->mbmi_ext->ref_mvs[refs[0]][0].as_mv,
                                   x->nmvjointcost, x->mvcost, MV_COST_WEIGHT);
      }
#else
      // Initialize mv using single prediction mode result.
      frame_mv[refs[0]].as_int = single_newmv[refs[0]].as_int;
      frame_mv[refs[1]].as_int = single_newmv[refs[1]].as_int;

      if (cpi->sf.comp_inter_joint_search_thresh <= bsize) {
        joint_motion_search(cpi, x, bsize, frame_mv,
                            mi_row, mi_col,
                            single_newmv, &rate_mv, 0);
      } else {
        rate_mv  = vp10_mv_bit_cost(&frame_mv[refs[0]].as_mv,
                                   &x->mbmi_ext->ref_mvs[refs[0]][0].as_mv,
                                   x->nmvjointcost, x->mvcost, MV_COST_WEIGHT);
        rate_mv += vp10_mv_bit_cost(&frame_mv[refs[1]].as_mv,
                                   &x->mbmi_ext->ref_mvs[refs[1]][0].as_mv,
                                   x->nmvjointcost, x->mvcost, MV_COST_WEIGHT);
      }
#endif  // CONFIG_EXT_INTER
    } else {
      int_mv tmp_mv;

#if CONFIG_EXT_INTER
      if (is_comp_interintra_pred) {
        tmp_mv = single_newmvs[mv_idx][refs[0]];
        rate_mv = single_newmvs_rate[mv_idx][refs[0]];
      } else {
        single_motion_search(cpi, x, bsize, mi_row, mi_col,
                             0, mv_idx, &tmp_mv, &rate_mv);
        single_newmvs[mv_idx][refs[0]] = tmp_mv;
        single_newmvs_rate[mv_idx][refs[0]] = rate_mv;
      }
#else
      single_motion_search(cpi, x, bsize, mi_row, mi_col, &tmp_mv, &rate_mv);
      single_newmv[refs[0]] = tmp_mv;
#endif  // CONFIG_EXT_INTER

      if (tmp_mv.as_int == INVALID_MV)
        return INT64_MAX;

      frame_mv[refs[0]] = tmp_mv;
      xd->mi[0]->bmi[0].as_mv[0] = tmp_mv;

      // Estimate the rate implications of a new mv but discount this
      // under certain circumstances where we want to help initiate a weak
      // motion field, where the distortion gain for a single block may not
      // be enough to overcome the cost of a new mv.
      if (discount_newmv_test(cpi, this_mode, tmp_mv, mode_mv, refs[0])) {
        rate_mv = VPXMAX((rate_mv / NEW_MV_DISCOUNT_FACTOR), 1);
      }
    }
    *rate2 += rate_mv;
  }

  for (i = 0; i < is_comp_pred + 1; ++i) {
    cur_mv[i] = frame_mv[refs[i]];
    // Clip "next_nearest" so that it does not extend to far out of image
#if CONFIG_EXT_INTER
    if (this_mode != NEWMV && this_mode != NEWFROMNEARMV)
#else
    if (this_mode != NEWMV)
#endif  // CONFIG_EXT_INTER
      clamp_mv2(&cur_mv[i].as_mv, xd);

    if (mv_check_bounds(x, &cur_mv[i].as_mv))
      return INT64_MAX;
    mbmi->mv[i].as_int = cur_mv[i].as_int;
  }

#if CONFIG_REF_MV
#if CONFIG_EXT_INTER
  if (this_mode == NEAREST_NEARESTMV) {
#else
  if (this_mode == NEARESTMV && is_comp_pred) {
    uint8_t ref_frame_type = vp10_ref_frame_type(mbmi->ref_frame);
#endif  // CONFIG_EXT_INTER
    if (mbmi_ext->ref_mv_count[ref_frame_type] > 0) {
      cur_mv[0] = mbmi_ext->ref_mv_stack[ref_frame_type][0].this_mv;
      cur_mv[1] = mbmi_ext->ref_mv_stack[ref_frame_type][0].comp_mv;

      for (i = 0; i < 2; ++i) {
        lower_mv_precision(&cur_mv[i].as_mv, cm->allow_high_precision_mv);
        clamp_mv2(&cur_mv[i].as_mv, xd);
        if (mv_check_bounds(x, &cur_mv[i].as_mv))
          return INT64_MAX;
        mbmi->mv[i].as_int = cur_mv[i].as_int;
      }
    }
  }

#if CONFIG_EXT_INTER
  if (mbmi_ext->ref_mv_count[ref_frame_type] > 0) {
    if (this_mode == NEAREST_NEWMV || this_mode == NEAREST_NEARMV) {
      cur_mv[0] = mbmi_ext->ref_mv_stack[ref_frame_type][0].this_mv;

      lower_mv_precision(&cur_mv[0].as_mv, cm->allow_high_precision_mv);
      clamp_mv2(&cur_mv[0].as_mv, xd);
      if (mv_check_bounds(x, &cur_mv[0].as_mv))
        return INT64_MAX;
      mbmi->mv[0].as_int = cur_mv[0].as_int;
    }

    if (this_mode == NEW_NEARESTMV || this_mode == NEAR_NEARESTMV) {
      cur_mv[1] = mbmi_ext->ref_mv_stack[ref_frame_type][0].comp_mv;

      lower_mv_precision(&cur_mv[1].as_mv, cm->allow_high_precision_mv);
      clamp_mv2(&cur_mv[1].as_mv, xd);
      if (mv_check_bounds(x, &cur_mv[1].as_mv))
        return INT64_MAX;
      mbmi->mv[1].as_int = cur_mv[1].as_int;
    }
  }

  if (mbmi_ext->ref_mv_count[ref_frame_type] > 1) {
    if (this_mode == NEAR_NEWMV || this_mode == NEAR_NEARESTMV) {
      cur_mv[0] = mbmi_ext->ref_mv_stack[ref_frame_type][1].this_mv;

      lower_mv_precision(&cur_mv[0].as_mv, cm->allow_high_precision_mv);
      clamp_mv2(&cur_mv[0].as_mv, xd);
      if (mv_check_bounds(x, &cur_mv[0].as_mv))
        return INT64_MAX;
      mbmi->mv[0].as_int = cur_mv[0].as_int;
    }

    if (this_mode == NEW_NEARMV || this_mode == NEAREST_NEARMV) {
      cur_mv[1] = mbmi_ext->ref_mv_stack[ref_frame_type][1].comp_mv;

      lower_mv_precision(&cur_mv[1].as_mv, cm->allow_high_precision_mv);
      clamp_mv2(&cur_mv[1].as_mv, xd);
      if (mv_check_bounds(x, &cur_mv[1].as_mv))
        return INT64_MAX;
      mbmi->mv[1].as_int = cur_mv[1].as_int;
    }
  }
#else
  if (this_mode == NEARMV && is_comp_pred) {
    uint8_t ref_frame_type = vp10_ref_frame_type(mbmi->ref_frame);
    if (mbmi_ext->ref_mv_count[ref_frame_type] > 1) {
      int ref_mv_idx = mbmi->ref_mv_idx + 1;
      cur_mv[0] = mbmi_ext->ref_mv_stack[ref_frame_type][ref_mv_idx].this_mv;
      cur_mv[1] = mbmi_ext->ref_mv_stack[ref_frame_type][ref_mv_idx].comp_mv;

      for (i = 0; i < 2; ++i) {
        lower_mv_precision(&cur_mv[i].as_mv, cm->allow_high_precision_mv);
        clamp_mv2(&cur_mv[i].as_mv, xd);
        if (mv_check_bounds(x, &cur_mv[i].as_mv))
          return INT64_MAX;
        mbmi->mv[i].as_int = cur_mv[i].as_int;
      }
    }
  }
#endif  // CONFIG_EXT_INTER
#endif  // CONFIG_REF_MV

  // do first prediction into the destination buffer. Do the next
  // prediction into a temporary buffer. Then keep track of which one
  // of these currently holds the best predictor, and use the other
  // one for future predictions. In the end, copy from tmp_buf to
  // dst if necessary.
  for (i = 0; i < MAX_MB_PLANE; i++) {
    orig_dst[i] = xd->plane[i].dst.buf;
    orig_dst_stride[i] = xd->plane[i].dst.stride;
  }

  // We don't include the cost of the second reference here, because there
  // are only three options: Last/Golden, ARF/Last or Golden/ARF, or in other
  // words if you present them in that order, the second one is always known
  // if the first is known.
  //
  // Under some circumstances we discount the cost of new mv mode to encourage
  // initiation of a motion field.
  if (discount_newmv_test(cpi, this_mode, frame_mv[refs[0]],
                          mode_mv, refs[0])) {
#if CONFIG_REF_MV && CONFIG_EXT_INTER
    *rate2 += VPXMIN(cost_mv_ref(cpi, this_mode, is_comp_pred, mode_ctx),
                     cost_mv_ref(cpi, NEARESTMV, is_comp_pred, mode_ctx));
#else
    *rate2 += VPXMIN(cost_mv_ref(cpi, this_mode, mode_ctx),
                     cost_mv_ref(cpi, NEARESTMV, mode_ctx));
#endif  // CONFIG_REF_MV && CONFIG_EXT_INTER
  } else {
#if CONFIG_REF_MV && CONFIG_EXT_INTER
    *rate2 += cost_mv_ref(cpi, this_mode, is_comp_pred, mode_ctx);
#else
    *rate2 += cost_mv_ref(cpi, this_mode, mode_ctx);
#endif  // CONFIG_REF_MV && CONFIG_EXT_INTER
  }

  if (RDCOST(x->rdmult, x->rddiv, *rate2, 0) > ref_best_rd &&
#if CONFIG_EXT_INTER
      mbmi->mode != NEARESTMV && mbmi->mode != NEAREST_NEARESTMV
#else
      mbmi->mode != NEARESTMV
#endif  // CONFIG_EXT_INTER
     )
    return INT64_MAX;

  pred_exists = 0;
  // Are all MVs integer pel for Y and UV
  intpel_mv = !mv_has_subpel(&mbmi->mv[0].as_mv);
  if (is_comp_pred)
    intpel_mv &= !mv_has_subpel(&mbmi->mv[1].as_mv);

  best_filter = predict_interp_filter(cpi, x, bsize, mi_row, mi_col,
                                      single_filter);
  if (cm->interp_filter != BILINEAR && best_filter == SWITCHABLE) {
    int newbest;
    int tmp_rate_sum = 0;
    int64_t tmp_dist_sum = 0;

    for (i = 0; i < SWITCHABLE_FILTERS; ++i) {
      int j;
      int64_t rs_rd;
      int tmp_skip_sb = 0;
      int64_t tmp_skip_sse = INT64_MAX;

      mbmi->interp_filter = i;
      rs = vp10_get_switchable_rate(cpi, xd);
      rs_rd = RDCOST(x->rdmult, x->rddiv, rs, 0);

      if (i > 0 && intpel_mv && IsInterpolatingFilter(i)) {
        rd = RDCOST(x->rdmult, x->rddiv, tmp_rate_sum, tmp_dist_sum);
        if (cm->interp_filter == SWITCHABLE)
          rd += rs_rd;
      } else {
        int rate_sum = 0;
        int64_t dist_sum = 0;

        if (i > 0 && cpi->sf.adaptive_interp_filter_search &&
            (cpi->sf.interp_filter_search_mask & (1 << i))) {
          rate_sum = INT_MAX;
          dist_sum = INT64_MAX;
          continue;
        }

        if ((cm->interp_filter == SWITCHABLE &&
             (!i || best_needs_copy)) ||
#if CONFIG_EXT_INTER
            is_comp_interintra_pred ||
#endif  // CONFIG_EXT_INTER
            (cm->interp_filter != SWITCHABLE &&
             (cm->interp_filter == mbmi->interp_filter ||
              (i == 0 && intpel_mv && IsInterpolatingFilter(i))))) {
          restore_dst_buf(xd, orig_dst, orig_dst_stride);
        } else {
          for (j = 0; j < MAX_MB_PLANE; j++) {
            xd->plane[j].dst.buf = tmp_buf + j * 64 * 64;
            xd->plane[j].dst.stride = 64;
          }
        }
        vp10_build_inter_predictors_sb(xd, mi_row, mi_col, bsize);
        model_rd_for_sb(cpi, bsize, x, xd, &rate_sum, &dist_sum,
                        &tmp_skip_sb, &tmp_skip_sse);

        rd = RDCOST(x->rdmult, x->rddiv, rate_sum, dist_sum);
        if (cm->interp_filter == SWITCHABLE)
          rd += rs_rd;

        if (i == 0 && intpel_mv && IsInterpolatingFilter(i)) {
          tmp_rate_sum = rate_sum;
          tmp_dist_sum = dist_sum;
        }
      }

      if (i == 0 && cpi->sf.use_rd_breakout && ref_best_rd < INT64_MAX) {
        if (rd / 2 > ref_best_rd) {
          restore_dst_buf(xd, orig_dst, orig_dst_stride);
          return INT64_MAX;
        }
      }
      newbest = i == 0 || rd < best_rd;

      if (newbest) {
        best_rd = rd;
        best_filter = mbmi->interp_filter;
        if (cm->interp_filter == SWITCHABLE && i &&
            !(intpel_mv && IsInterpolatingFilter(i)))
          best_needs_copy = !best_needs_copy;
      }

      if ((cm->interp_filter == SWITCHABLE && newbest) ||
          (cm->interp_filter != SWITCHABLE &&
           cm->interp_filter == mbmi->interp_filter)) {
        pred_exists = 1;
        tmp_rd = best_rd;

        skip_txfm_sb = tmp_skip_sb;
        skip_sse_sb = tmp_skip_sse;
        memcpy(skip_txfm, x->skip_txfm, sizeof(skip_txfm));
        memcpy(bsse, x->bsse, sizeof(bsse));
      } else {
        pred_exists = 0;
      }
    }
    restore_dst_buf(xd, orig_dst, orig_dst_stride);
  }

  // Set the appropriate filter
  mbmi->interp_filter = cm->interp_filter != SWITCHABLE ?
      cm->interp_filter : best_filter;
  rs = cm->interp_filter == SWITCHABLE ? vp10_get_switchable_rate(cpi, xd) : 0;

#if CONFIG_EXT_INTER
  if (is_comp_pred && get_wedge_bits(bsize)) {
    int wedge_index, best_wedge_index = WEDGE_NONE, rs;
    int rate_sum;
    int64_t dist_sum;
    int64_t best_rd_nowedge = INT64_MAX;
    int64_t best_rd_wedge = INT64_MAX;
    int wedge_types;
    int tmp_skip_txfm_sb;
    int64_t tmp_skip_sse_sb;
    rs = vp10_cost_bit(cm->fc->wedge_interinter_prob[bsize], 0);
    vp10_build_inter_predictors_sb(xd, mi_row, mi_col, bsize);
    model_rd_for_sb(cpi, bsize, x, xd, &rate_sum, &dist_sum,
                    &tmp_skip_txfm_sb, &tmp_skip_sse_sb);
    rd = RDCOST(x->rdmult, x->rddiv, rs + rate_mv + rate_sum, dist_sum);
    best_rd_nowedge = rd;
    mbmi->use_wedge_interinter = 1;
    rs = get_wedge_bits(bsize) * 256 +
        vp10_cost_bit(cm->fc->wedge_interinter_prob[bsize], 1);
    wedge_types = (1 << get_wedge_bits(bsize));
    if (have_newmv_in_inter_mode(this_mode)) {
      int_mv tmp_mv[2];
      int rate_mvs[2], tmp_rate_mv = 0;
      uint8_t pred0[2 * CU_SIZE * CU_SIZE * 3];
      uint8_t pred1[2 * CU_SIZE * CU_SIZE * 3];
      uint8_t *preds0[3] = {pred0,
                            pred0 + 2 * CU_SIZE * CU_SIZE,
                            pred0 + 4 * CU_SIZE * CU_SIZE};
      uint8_t *preds1[3] = {pred1,
                            pred1 + 2 * CU_SIZE * CU_SIZE,
                            pred1 + 4 * CU_SIZE * CU_SIZE};
      int strides[3] = {CU_SIZE, CU_SIZE, CU_SIZE};
      vp10_build_inter_predictors_for_planes_single_buf(
          xd, bsize, mi_row, mi_col, 0, preds0, strides);
      vp10_build_inter_predictors_for_planes_single_buf(
          xd, bsize, mi_row, mi_col, 1, preds1, strides);

      for (wedge_index = 0; wedge_index < wedge_types; ++wedge_index) {
        mbmi->interinter_wedge_index = wedge_index;
        vp10_build_wedge_inter_predictor_from_buf(xd, bsize, mi_row, mi_col,
                                                  preds0, strides,
                                                  preds1, strides);
        model_rd_for_sb(cpi, bsize, x, xd, &rate_sum, &dist_sum,
                        &tmp_skip_txfm_sb, &tmp_skip_sse_sb);
        rd = RDCOST(x->rdmult, x->rddiv, rs + rate_mv + rate_sum, dist_sum);
        if (rd < best_rd_wedge) {
          best_wedge_index = wedge_index;
          best_rd_wedge = rd;
        }
      }
      mbmi->interinter_wedge_index = best_wedge_index;
      if (this_mode == NEW_NEWMV) {
        int mv_idxs[2] = {0, 0};
        do_masked_motion_search_indexed(cpi, x, mbmi->interinter_wedge_index,
                                        bsize, mi_row, mi_col, tmp_mv, rate_mvs,
                                        mv_idxs, 2);
        tmp_rate_mv = rate_mvs[0] + rate_mvs[1];
        mbmi->mv[0].as_int = tmp_mv[0].as_int;
        mbmi->mv[1].as_int = tmp_mv[1].as_int;
      } else if (this_mode == NEW_NEARESTMV || this_mode == NEW_NEARMV) {
        int mv_idxs[2] = {0, 0};
        do_masked_motion_search_indexed(cpi, x, mbmi->interinter_wedge_index,
                                        bsize, mi_row, mi_col, tmp_mv, rate_mvs,
                                        mv_idxs, 0);
        tmp_rate_mv = rate_mvs[0];
        mbmi->mv[0].as_int = tmp_mv[0].as_int;
      } else if (this_mode == NEAREST_NEWMV || this_mode == NEAR_NEWMV) {
        int mv_idxs[2] = {0, 0};
        do_masked_motion_search_indexed(cpi, x, mbmi->interinter_wedge_index,
                                        bsize, mi_row, mi_col, tmp_mv, rate_mvs,
                                        mv_idxs, 1);
        tmp_rate_mv = rate_mvs[1];
        mbmi->mv[1].as_int = tmp_mv[1].as_int;
      }
      vp10_build_inter_predictors_sb(xd, mi_row, mi_col, bsize);
      model_rd_for_sb(cpi, bsize, x, xd, &rate_sum, &dist_sum,
                      &tmp_skip_txfm_sb, &tmp_skip_sse_sb);
      rd = RDCOST(x->rdmult, x->rddiv, rs + tmp_rate_mv + rate_sum, dist_sum);
      if (rd < best_rd_wedge) {
        best_rd_wedge = rd;
      } else {
        mbmi->mv[0].as_int = cur_mv[0].as_int;
        mbmi->mv[1].as_int = cur_mv[1].as_int;
        tmp_rate_mv = rate_mv;
      }
      if (best_rd_wedge < best_rd_nowedge) {
        mbmi->use_wedge_interinter = 1;
        mbmi->interinter_wedge_index = best_wedge_index;
        xd->mi[0]->bmi[0].as_mv[0].as_int = mbmi->mv[0].as_int;
        xd->mi[0]->bmi[0].as_mv[1].as_int = mbmi->mv[1].as_int;
        *rate2 += tmp_rate_mv - rate_mv;
        rate_mv = tmp_rate_mv;
      } else {
        mbmi->use_wedge_interinter = 0;
        mbmi->mv[0].as_int = cur_mv[0].as_int;
        mbmi->mv[1].as_int = cur_mv[1].as_int;
      }
    } else {
      uint8_t pred0[2 * CU_SIZE * CU_SIZE * 3];
      uint8_t pred1[2 * CU_SIZE * CU_SIZE * 3];
      uint8_t *preds0[3] = {pred0,
                            pred0 + 2 * CU_SIZE * CU_SIZE,
                            pred0 + 4 * CU_SIZE * CU_SIZE};
      uint8_t *preds1[3] = {pred1,
                            pred1 + 2 * CU_SIZE * CU_SIZE,
                            pred1 + 4 * CU_SIZE * CU_SIZE};
      int strides[3] = {CU_SIZE, CU_SIZE, CU_SIZE};
      vp10_build_inter_predictors_for_planes_single_buf(
          xd, bsize, mi_row, mi_col, 0, preds0, strides);
      vp10_build_inter_predictors_for_planes_single_buf(
          xd, bsize, mi_row, mi_col, 1, preds1, strides);
      for (wedge_index = 0; wedge_index < wedge_types; ++wedge_index) {
        mbmi->interinter_wedge_index = wedge_index;
        // vp10_build_inter_predictors_sb(xd, mi_row, mi_col, bsize);
        vp10_build_wedge_inter_predictor_from_buf(xd, bsize, mi_row, mi_col,
                                                  preds0, strides,
                                                  preds1, strides);
        model_rd_for_sb(cpi, bsize, x, xd, &rate_sum, &dist_sum,
                        &tmp_skip_txfm_sb, &tmp_skip_sse_sb);
        rd = RDCOST(x->rdmult, x->rddiv, rs + rate_mv + rate_sum, dist_sum);
        if (rd < best_rd_wedge) {
          best_wedge_index = wedge_index;
          best_rd_wedge = rd;
        }
      }
      if (best_rd_wedge < best_rd_nowedge) {
        mbmi->use_wedge_interinter = 1;
        mbmi->interinter_wedge_index = best_wedge_index;
      } else {
        mbmi->use_wedge_interinter = 0;
      }
    }
#if CONFIG_OBMC
    if (mbmi->use_wedge_interinter)
      allow_obmc = 0;
#endif  // CONFIG_OBMC
    if (ref_best_rd < INT64_MAX &&
        VPXMIN(best_rd_wedge, best_rd_nowedge) / 2 > ref_best_rd)
      return INT64_MAX;

    pred_exists = 0;
    tmp_rd = VPXMIN(best_rd_wedge, best_rd_nowedge);
    if (mbmi->use_wedge_interinter)
      *compmode_wedge_cost = get_wedge_bits(bsize) * 256 +
          vp10_cost_bit(cm->fc->wedge_interinter_prob[bsize], 1);
    else
      *compmode_wedge_cost =
          vp10_cost_bit(cm->fc->wedge_interinter_prob[bsize], 0);
  }

  if (is_comp_interintra_pred) {
    PREDICTION_MODE interintra_mode, best_interintra_mode = DC_PRED;
    int64_t best_interintra_rd = INT64_MAX;
    int rmode, rate_sum;
    int64_t dist_sum;
    int j;
    int wedge_bits, wedge_types, wedge_index, best_wedge_index = -1;
    int64_t best_interintra_rd_nowedge = INT64_MAX;
    int64_t best_interintra_rd_wedge = INT64_MAX;
    int rwedge;
    int bw = 4 << b_width_log2_lookup[mbmi->sb_type],
        bh = 4 << b_height_log2_lookup[mbmi->sb_type];
    int_mv tmp_mv;
    int tmp_rate_mv = 0;
    mbmi->ref_frame[1] = NONE;
    for (j = 0; j < MAX_MB_PLANE; j++) {
      xd->plane[j].dst.buf = tmp_buf + j * tmp_buf_sz;
      xd->plane[j].dst.stride = CU_SIZE;
    }
    vp10_build_inter_predictors_sb(xd, mi_row, mi_col, bsize);
    restore_dst_buf(xd, orig_dst, orig_dst_stride);
    mbmi->ref_frame[1] = INTRA_FRAME;

    for (interintra_mode = DC_PRED; interintra_mode <= TM_PRED;
         ++interintra_mode) {
      mbmi->interintra_mode = interintra_mode;
      mbmi->interintra_uv_mode = interintra_mode;
      rmode = intra_mode_cost[mbmi->interintra_mode];
      vp10_build_interintra_predictors(xd,
                                       tmp_buf,
                                       tmp_buf + tmp_buf_sz,
                                       tmp_buf + 2 * tmp_buf_sz,
                                       CU_SIZE,
                                       CU_SIZE,
                                       CU_SIZE,
                                       bsize);
      model_rd_for_sb(cpi, bsize, x, xd, &rate_sum, &dist_sum,
                      &skip_txfm_sb, &skip_sse_sb);
      rd = RDCOST(x->rdmult, x->rddiv, rate_mv + rmode + rate_sum, dist_sum);
      if (rd < best_interintra_rd) {
        best_interintra_rd = rd;
        best_interintra_mode = interintra_mode;
      }
    }
    mbmi->interintra_mode = best_interintra_mode;
    mbmi->interintra_uv_mode = best_interintra_mode;
    if (ref_best_rd < INT64_MAX &&
        best_interintra_rd / 2 > ref_best_rd) {
      return INT64_MAX;
    }
    wedge_bits = get_wedge_bits(bsize);
    rmode = intra_mode_cost[mbmi->interintra_mode];
    if (wedge_bits) {
      vp10_build_interintra_predictors(xd,
                                       tmp_buf,
                                       tmp_buf + tmp_buf_sz,
                                       tmp_buf + 2 * tmp_buf_sz,
                                       CU_SIZE,
                                       CU_SIZE,
                                       CU_SIZE,
                                       bsize);
      model_rd_for_sb(cpi, bsize, x, xd, &rate_sum, &dist_sum,
                      &skip_txfm_sb, &skip_sse_sb);
      rwedge = vp10_cost_bit(cm->fc->wedge_interintra_prob[bsize], 0);
      rd = RDCOST(x->rdmult, x->rddiv,
                  rmode + rate_mv + rwedge + rate_sum, dist_sum);
      best_interintra_rd_nowedge = rd;

      mbmi->use_wedge_interintra = 1;
      rwedge = wedge_bits * 256 +
          vp10_cost_bit(cm->fc->wedge_interintra_prob[bsize], 1);
      wedge_types = (1 << wedge_bits);
      for (wedge_index = 0; wedge_index < wedge_types; ++wedge_index) {
        mbmi->interintra_wedge_index = wedge_index;
        mbmi->interintra_uv_wedge_index = wedge_index;
        vp10_build_interintra_predictors(xd,
                                         tmp_buf,
                                         tmp_buf + tmp_buf_sz,
                                         tmp_buf + 2 * tmp_buf_sz,
                                         CU_SIZE,
                                         CU_SIZE,
                                         CU_SIZE,
                                         bsize);
        model_rd_for_sb(cpi, bsize, x, xd, &rate_sum, &dist_sum,
                        &skip_txfm_sb, &skip_sse_sb);
        rd = RDCOST(x->rdmult, x->rddiv,
                    rmode + rate_mv + rwedge + rate_sum, dist_sum);
        if (rd < best_interintra_rd_wedge) {
          best_interintra_rd_wedge = rd;
          best_wedge_index = wedge_index;
        }
      }
      // Refine motion vector.
      if (have_newmv_in_inter_mode(this_mode)) {
        // get negative of mask
        const uint8_t* mask = vp10_get_soft_mask(
            best_wedge_index ^ 1, bsize, bh, bw);
        mbmi->interintra_wedge_index = best_wedge_index;
        mbmi->interintra_uv_wedge_index = best_wedge_index;
        do_masked_motion_search(cpi, x, mask, MASK_MASTER_STRIDE, bsize,
                                mi_row, mi_col, &tmp_mv, &tmp_rate_mv,
                                0, mv_idx);
        mbmi->mv[0].as_int = tmp_mv.as_int;
        vp10_build_inter_predictors_sb(xd, mi_row, mi_col, bsize);
        model_rd_for_sb(cpi, bsize, x, xd, &rate_sum, &dist_sum,
                        &skip_txfm_sb, &skip_sse_sb);
        rd = RDCOST(x->rdmult, x->rddiv,
                    rmode + tmp_rate_mv + rwedge + rate_sum, dist_sum);
        if (rd < best_interintra_rd_wedge) {
          best_interintra_rd_wedge = rd;
        } else {
          tmp_mv.as_int = cur_mv[0].as_int;
          tmp_rate_mv = rate_mv;
        }
      } else {
        tmp_mv.as_int = cur_mv[0].as_int;
        tmp_rate_mv = rate_mv;
      }
      if (best_interintra_rd_wedge < best_interintra_rd_nowedge) {
        mbmi->use_wedge_interintra = 1;
        mbmi->interintra_wedge_index = best_wedge_index;
        mbmi->interintra_uv_wedge_index = best_wedge_index;
        best_interintra_rd = best_interintra_rd_wedge;
        mbmi->mv[0].as_int = tmp_mv.as_int;
        *rate2 += tmp_rate_mv - rate_mv;
        rate_mv = tmp_rate_mv;
      } else {
        mbmi->use_wedge_interintra = 0;
        best_interintra_rd = best_interintra_rd_nowedge;
        mbmi->mv[0].as_int = cur_mv[0].as_int;
      }
    }

    pred_exists = 0;
    tmp_rd = best_interintra_rd;
    *compmode_interintra_cost =
        vp10_cost_bit(cm->fc->interintra_prob[bsize], 1);
    *compmode_interintra_cost += intra_mode_cost[mbmi->interintra_mode];
    if (get_wedge_bits(bsize)) {
      *compmode_interintra_cost += vp10_cost_bit(
          cm->fc->wedge_interintra_prob[bsize], mbmi->use_wedge_interintra);
      if (mbmi->use_wedge_interintra) {
        *compmode_interintra_cost += get_wedge_bits(bsize) * 256;
      }
    }
  } else if (is_interintra_allowed(mbmi)) {
    *compmode_interintra_cost =
      vp10_cost_bit(cm->fc->interintra_prob[bsize], 0);
  }

#if CONFIG_EXT_INTERP
  if (!vp10_is_interp_needed(xd) && cm->interp_filter == SWITCHABLE) {
    mbmi->interp_filter = EIGHTTAP_REGULAR;
    pred_exists = 0;
  }
#endif  // CONFIG_EXT_INTERP
#endif  // CONFIG_EXT_INTER

  if (pred_exists) {
    if (best_needs_copy) {
      // again temporarily set the buffers to local memory to prevent a memcpy
      for (i = 0; i < MAX_MB_PLANE; i++) {
        xd->plane[i].dst.buf = tmp_buf + i * 64 * 64;
        xd->plane[i].dst.stride = 64;
      }
    }
    rd = tmp_rd;
  } else {
    int tmp_rate;
    int64_t tmp_dist;

    // Handles the special case when a filter that is not in the
    // switchable list (ex. bilinear) is indicated at the frame level, or
    // skip condition holds.
    vp10_build_inter_predictors_sb(xd, mi_row, mi_col, bsize);
    model_rd_for_sb(cpi, bsize, x, xd, &tmp_rate, &tmp_dist,
                    &skip_txfm_sb, &skip_sse_sb);
    rd = RDCOST(x->rdmult, x->rddiv, rs + tmp_rate, tmp_dist);
    memcpy(skip_txfm, x->skip_txfm, sizeof(skip_txfm));
    memcpy(bsse, x->bsse, sizeof(bsse));
  }

  if (!is_comp_pred)
    single_filter[this_mode][refs[0]] = mbmi->interp_filter;

  if (cpi->sf.adaptive_mode_search)
    if (is_comp_pred)
#if CONFIG_EXT_INTER
      switch (this_mode) {
        case NEAREST_NEARESTMV:
          if (single_skippable[NEARESTMV][refs[0]] &&
              single_skippable[NEARESTMV][refs[1]])
            memset(skip_txfm, SKIP_TXFM_AC_DC, sizeof(skip_txfm));
          break;
        case ZERO_ZEROMV:
          if (single_skippable[ZEROMV][refs[0]] &&
              single_skippable[ZEROMV][refs[1]])
            memset(skip_txfm, SKIP_TXFM_AC_DC, sizeof(skip_txfm));
          break;
        case NEW_NEWMV:
          if (single_skippable[NEWMV][refs[0]] &&
              single_skippable[NEWMV][refs[1]])
            memset(skip_txfm, SKIP_TXFM_AC_DC, sizeof(skip_txfm));
          break;
        case NEAREST_NEWMV:
          if (single_skippable[NEARESTMV][refs[0]] &&
              single_skippable[NEWMV][refs[1]])
            memset(skip_txfm, SKIP_TXFM_AC_DC, sizeof(skip_txfm));
          break;
        case NEAR_NEWMV:
          if (single_skippable[NEARMV][refs[0]] &&
              single_skippable[NEWMV][refs[1]])
            memset(skip_txfm, SKIP_TXFM_AC_DC, sizeof(skip_txfm));
          break;
        case NEW_NEARESTMV:
          if (single_skippable[NEWMV][refs[0]] &&
              single_skippable[NEARESTMV][refs[1]])
            memset(skip_txfm, SKIP_TXFM_AC_DC, sizeof(skip_txfm));
          break;
        case NEW_NEARMV:
          if (single_skippable[NEWMV][refs[0]] &&
              single_skippable[NEARMV][refs[1]])
            memset(skip_txfm, SKIP_TXFM_AC_DC, sizeof(skip_txfm));
          break;
        case NEAREST_NEARMV:
          if (single_skippable[NEARESTMV][refs[0]] &&
              single_skippable[NEARMV][refs[1]])
            memset(skip_txfm, SKIP_TXFM_AC_DC, sizeof(skip_txfm));
          break;
        case NEAR_NEARESTMV:
          if (single_skippable[NEARMV][refs[0]] &&
              single_skippable[NEARESTMV][refs[1]])
            memset(skip_txfm, SKIP_TXFM_AC_DC, sizeof(skip_txfm));
          break;
        default:
          if (single_skippable[this_mode][refs[0]] &&
              single_skippable[this_mode][refs[1]])
            memset(skip_txfm, SKIP_TXFM_AC_DC, sizeof(skip_txfm));
          break;
      }
#else
      if (single_skippable[this_mode][refs[0]] &&
          single_skippable[this_mode][refs[1]])
        memset(skip_txfm, SKIP_TXFM_AC_DC, sizeof(skip_txfm));
#endif  // CONFIG_EXT_INTER

  if (cpi->sf.use_rd_breakout && ref_best_rd < INT64_MAX) {
    // if current pred_error modeled rd is substantially more than the best
    // so far, do not bother doing full rd
    if (rd / 2 > ref_best_rd) {
      restore_dst_buf(xd, orig_dst, orig_dst_stride);
      return INT64_MAX;
    }
  }

  if (cm->interp_filter == SWITCHABLE)
    *rate2 += rs;
#if CONFIG_OBMC
  rate2_nocoeff = *rate2;
#endif  // CONFIG_OBMC

  memcpy(x->skip_txfm, skip_txfm, sizeof(skip_txfm));
  memcpy(x->bsse, bsse, sizeof(bsse));


#if CONFIG_OBMC
  best_rd = INT64_MAX;
  for (mbmi->obmc = 0; mbmi->obmc <= allow_obmc; mbmi->obmc++) {
    int64_t tmp_rd, tmp_dist;
    int tmp_rate;

    if (mbmi->obmc) {
      vp10_build_obmc_inter_prediction(cm, xd, mi_row, mi_col, 0,
                                       NULL, NULL,
                                       dst_buf1, dst_stride1,
                                       dst_buf2, dst_stride2);
      model_rd_for_sb(cpi, bsize, x, xd, &tmp_rate, &tmp_dist,
                      &skip_txfm_sb, &skip_sse_sb);
    }
#if CONFIG_VP9_HIGHBITDEPTH
    if (xd->cur_buf->flags & YV12_FLAG_HIGHBITDEPTH) {
      x->pred_variance = vp10_high_get_sby_perpixel_variance(cpi,
                                              &xd->plane[0].dst, bsize, xd->bd);
    } else {
      x->pred_variance =
        vp10_get_sby_perpixel_variance(cpi, &xd->plane[0].dst, bsize);
    }
#else
    x->pred_variance =
        vp10_get_sby_perpixel_variance(cpi, &xd->plane[0].dst, bsize);
#endif  // CONFIG_VP9_HIGHBITDEPTH

    x->skip = 0;

    *rate2 = rate2_nocoeff;
    *distortion = 0;
    if (allow_obmc)
      *rate2 += cpi->obmc_cost[bsize][mbmi->obmc];
#endif  // CONFIG_OBMC
  if (!skip_txfm_sb) {
    int skippable_y, skippable_uv;
    int64_t sseuv = INT64_MAX;
    int64_t rdcosty = INT64_MAX;

    // Y cost and distortion
    vp10_subtract_plane(x, bsize, 0);
#if CONFIG_VAR_TX
    if (cm->tx_mode == TX_MODE_SELECT || xd->lossless[mbmi->segment_id]) {
      select_tx_type_yrd(cpi, x, rate_y, &distortion_y, &skippable_y, psse,
                         bsize, ref_best_rd);
    } else {
      int idx, idy;
      super_block_yrd(cpi, x, rate_y, &distortion_y, &skippable_y, psse,
                      bsize, ref_best_rd);
      for (idy = 0; idy < xd->n8_h; ++idy)
        for (idx = 0; idx < xd->n8_w; ++idx)
          mbmi->inter_tx_size[idy * 8 + idx] = mbmi->tx_size;
    }
#else
    super_block_yrd(cpi, x, rate_y, &distortion_y, &skippable_y, psse,
                    bsize, ref_best_rd);
#endif  // CONFIG_VAR_TX

    if (*rate_y == INT_MAX) {
      *rate2 = INT_MAX;
      *distortion = INT64_MAX;
#if CONFIG_OBMC
      continue;
#else
      restore_dst_buf(xd, orig_dst, orig_dst_stride);
      return INT64_MAX;
#endif  // CONFIG_OBMC
    }

    *rate2 += *rate_y;
    *distortion += distortion_y;

    rdcosty = RDCOST(x->rdmult, x->rddiv, *rate2, *distortion);
    rdcosty = VPXMIN(rdcosty, RDCOST(x->rdmult, x->rddiv, 0, *psse));

#if CONFIG_VAR_TX
    if (!inter_block_uvrd(cpi, x, rate_uv, &distortion_uv, &skippable_uv,
                          &sseuv, bsize, ref_best_rd - rdcosty)) {
#else
    if (!super_block_uvrd(cpi, x, rate_uv, &distortion_uv, &skippable_uv,
                          &sseuv, bsize, ref_best_rd - rdcosty)) {
#endif  // CONFIG_VAR_TX
      *rate2 = INT_MAX;
      *distortion = INT64_MAX;
#if CONFIG_OBMC
      continue;
#else
      restore_dst_buf(xd, orig_dst, orig_dst_stride);
      return INT64_MAX;
#endif  // CONFIG_OBMC
    }

    *psse += sseuv;
    *rate2 += *rate_uv;
    *distortion += distortion_uv;
    *skippable = skippable_y && skippable_uv;
#if CONFIG_OBMC
    if (*skippable) {
      *rate2 -= *rate_uv + *rate_y;
      *rate_y = 0;
      *rate_uv = 0;
      *rate2 += vp10_cost_bit(vp10_get_skip_prob(cm, xd), 1);
      mbmi->skip = 0;
      // here mbmi->skip temporarily plays a role as what this_skip2 does
    } else if (!xd->lossless[mbmi->segment_id] &&
               (RDCOST(x->rdmult, x->rddiv, *rate_y + *rate_uv +
                  vp10_cost_bit(vp10_get_skip_prob(cm, xd), 0), *distortion) >=
                RDCOST(x->rdmult, x->rddiv,
                  vp10_cost_bit(vp10_get_skip_prob(cm, xd), 1), *psse))) {
      *rate2 -= *rate_uv + *rate_y;
      *rate2 += vp10_cost_bit(vp10_get_skip_prob(cm, xd), 1);
      *distortion = *psse;
      *rate_y = 0;
      *rate_uv = 0;
      mbmi->skip = 1;
    } else {
      *rate2 += vp10_cost_bit(vp10_get_skip_prob(cm, xd), 0);
      mbmi->skip = 0;
    }
    *disable_skip = 0;
#endif  // CONFIG_OBMC
  } else {
    x->skip = 1;
    *disable_skip = 1;

    // The cost of skip bit needs to be added.
#if CONFIG_OBMC
    mbmi->skip = 0;
#endif  // CONFIG_OBMC
    *rate2 += vp10_cost_bit(vp10_get_skip_prob(cm, xd), 1);

    *distortion = skip_sse_sb;
  }
#if CONFIG_OBMC
    tmp_rd = RDCOST(x->rdmult, x->rddiv, *rate2, *distortion);
    if (mbmi->obmc == 0 || (tmp_rd < best_rd)) {
      best_mbmi = *mbmi;
      best_rd = tmp_rd;
      best_rate2 = *rate2;
#if CONFIG_SUPERTX
      best_rate_y = *rate_y;
      best_rate_uv = *rate_uv;
#endif  // CONFIG_SUPERTX
#if CONFIG_VAR_TX
      for (i = 0; i < MAX_MB_PLANE; ++i)
        memcpy(best_blk_skip[i], x->blk_skip[i],
               sizeof(uint8_t) * xd->n8_h * xd->n8_w * 4);
#endif  // CONFIG_VAR_TX
      best_distortion = *distortion;
      best_skippable = *skippable;
      best_xskip = x->skip;
      best_disable_skip = *disable_skip;
      best_pred_var = x->pred_variance;
    }
  }

  if (best_rd == INT64_MAX) {
    *rate2 = INT_MAX;
    *distortion = INT64_MAX;
    restore_dst_buf(xd, orig_dst, orig_dst_stride);
    return INT64_MAX;
  }
  *mbmi = best_mbmi;
  *rate2 = best_rate2;
#if CONFIG_SUPERTX
  *rate_y = best_rate_y;
  *rate_uv = best_rate_uv;
#endif  // CONFIG_SUPERTX
#if CONFIG_VAR_TX
  for (i = 0; i < MAX_MB_PLANE; ++i)
    memcpy(x->blk_skip[i], best_blk_skip[i],
           sizeof(uint8_t) * xd->n8_h * xd->n8_w * 4);
#endif  // CONFIG_VAR_TX
  *distortion = best_distortion;
  *skippable = best_skippable;
  x->skip = best_xskip;
  *disable_skip = best_disable_skip;
  x->pred_variance = best_pred_var;
#endif  // CONFIG_OBMC

  if (!is_comp_pred)
    single_skippable[this_mode][refs[0]] = *skippable;

  restore_dst_buf(xd, orig_dst, orig_dst_stride);
  return 0;  // The rate-distortion cost will be re-calculated by caller.
}

void vp10_rd_pick_intra_mode_sb(VP10_COMP *cpi, MACROBLOCK *x,
                               RD_COST *rd_cost, BLOCK_SIZE bsize,
                               PICK_MODE_CONTEXT *ctx, int64_t best_rd) {
  VP10_COMMON *const cm = &cpi->common;
  MACROBLOCKD *const xd = &x->e_mbd;
  struct macroblockd_plane *const pd = xd->plane;
  int rate_y = 0, rate_uv = 0, rate_y_tokenonly = 0, rate_uv_tokenonly = 0;
  int y_skip = 0, uv_skip = 0;
  int64_t dist_y = 0, dist_uv = 0;
  TX_SIZE max_uv_tx_size;
  ctx->skip = 0;
  xd->mi[0]->mbmi.ref_frame[0] = INTRA_FRAME;
  xd->mi[0]->mbmi.ref_frame[1] = NONE;

  if (bsize >= BLOCK_8X8) {
    if (rd_pick_intra_sby_mode(cpi, x, &rate_y, &rate_y_tokenonly,
                               &dist_y, &y_skip, bsize,
                               best_rd) >= best_rd) {
      rd_cost->rate = INT_MAX;
      return;
    }
  } else {
    y_skip = 0;
    if (rd_pick_intra_sub_8x8_y_mode(cpi, x, &rate_y, &rate_y_tokenonly,
                                     &dist_y, best_rd) >= best_rd) {
      rd_cost->rate = INT_MAX;
      return;
    }
  }
  max_uv_tx_size = get_uv_tx_size_impl(xd->mi[0]->mbmi.tx_size, bsize,
                                       pd[1].subsampling_x,
                                       pd[1].subsampling_y);
  rd_pick_intra_sbuv_mode(cpi, x, ctx, &rate_uv, &rate_uv_tokenonly,
                          &dist_uv, &uv_skip, VPXMAX(BLOCK_8X8, bsize),
                          max_uv_tx_size);

  if (y_skip && uv_skip) {
    rd_cost->rate = rate_y + rate_uv - rate_y_tokenonly - rate_uv_tokenonly +
                    vp10_cost_bit(vp10_get_skip_prob(cm, xd), 1);
    rd_cost->dist = dist_y + dist_uv;
  } else {
    rd_cost->rate = rate_y + rate_uv +
                      vp10_cost_bit(vp10_get_skip_prob(cm, xd), 0);
    rd_cost->dist = dist_y + dist_uv;
  }

  ctx->mic = *xd->mi[0];
  ctx->mbmi_ext = *x->mbmi_ext;
  rd_cost->rdcost = RDCOST(x->rdmult, x->rddiv, rd_cost->rate, rd_cost->dist);
}

// This function is designed to apply a bias or adjustment to an rd value based
// on the relative variance of the source and reconstruction.
#define LOW_VAR_THRESH 16
#define VLOW_ADJ_MAX 25
#define VHIGH_ADJ_MAX 8
static void rd_variance_adjustment(VP10_COMP *cpi,
                                   MACROBLOCK *x,
                                   BLOCK_SIZE bsize,
                                   int64_t *this_rd,
                                   MV_REFERENCE_FRAME ref_frame,
#if CONFIG_OBMC
                                   int is_pred_var_available,
#endif  // CONFIG_OBMC
                                   unsigned int source_variance) {
  MACROBLOCKD *const xd = &x->e_mbd;
  unsigned int recon_variance;
  unsigned int absvar_diff = 0;
  int64_t var_error = 0;
  int64_t var_factor = 0;

  if (*this_rd == INT64_MAX)
    return;

#if CONFIG_OBMC
  if (is_pred_var_available) {
    recon_variance = x->pred_variance;
  } else {
#endif  // CONFIG_OBMC
#if CONFIG_VP9_HIGHBITDEPTH
  if (xd->cur_buf->flags & YV12_FLAG_HIGHBITDEPTH) {
    recon_variance =
      vp10_high_get_sby_perpixel_variance(cpi, &xd->plane[0].dst, bsize, xd->bd);
  } else {
    recon_variance =
      vp10_get_sby_perpixel_variance(cpi, &xd->plane[0].dst, bsize);
  }
#else
  recon_variance =
    vp10_get_sby_perpixel_variance(cpi, &xd->plane[0].dst, bsize);
#endif  // CONFIG_VP9_HIGHBITDEPTH
#if CONFIG_OBMC
  }
#endif  // CONFIG_OBMC

  if ((source_variance + recon_variance) > LOW_VAR_THRESH) {
    absvar_diff = (source_variance > recon_variance)
      ? (source_variance - recon_variance)
      : (recon_variance - source_variance);

    var_error = ((int64_t)200 * source_variance * recon_variance) /
      (((int64_t)source_variance * source_variance) +
       ((int64_t)recon_variance * recon_variance));
    var_error = 100 - var_error;
  }

  // Source variance above a threshold and ref frame is intra.
  // This case is targeted mainly at discouraging intra modes that give rise
  // to a predictor with a low spatial complexity compared to the source.
  if ((source_variance > LOW_VAR_THRESH) && (ref_frame == INTRA_FRAME) &&
      (source_variance > recon_variance)) {
    var_factor = VPXMIN(absvar_diff, VPXMIN(VLOW_ADJ_MAX, var_error));
  // A second possible case of interest is where the source variance
  // is very low and we wish to discourage false texture or motion trails.
  } else if ((source_variance < (LOW_VAR_THRESH >> 1)) &&
             (recon_variance > source_variance)) {
    var_factor = VPXMIN(absvar_diff, VPXMIN(VHIGH_ADJ_MAX, var_error));
  }
  *this_rd += (*this_rd * var_factor) / 100;
}


// Do we have an internal image edge (e.g. formatting bars).
int vp10_internal_image_edge(VP10_COMP *cpi) {
  return (cpi->oxcf.pass == 2) &&
    ((cpi->twopass.this_frame_stats.inactive_zone_rows > 0) ||
    (cpi->twopass.this_frame_stats.inactive_zone_cols > 0));
}

// Checks to see if a super block is on a horizontal image edge.
// In most cases this is the "real" edge unless there are formatting
// bars embedded in the stream.
int vp10_active_h_edge(VP10_COMP *cpi, int mi_row, int mi_step) {
  int top_edge = 0;
  int bottom_edge = cpi->common.mi_rows;
  int is_active_h_edge = 0;

  // For two pass account for any formatting bars detected.
  if (cpi->oxcf.pass == 2) {
    TWO_PASS *twopass = &cpi->twopass;

    // The inactive region is specified in MBs not mi units.
    // The image edge is in the following MB row.
    top_edge += (int)(twopass->this_frame_stats.inactive_zone_rows * 2);

    bottom_edge -= (int)(twopass->this_frame_stats.inactive_zone_rows * 2);
    bottom_edge = VPXMAX(top_edge, bottom_edge);
  }

  if (((top_edge >= mi_row) && (top_edge < (mi_row + mi_step))) ||
      ((bottom_edge >= mi_row) && (bottom_edge < (mi_row + mi_step)))) {
    is_active_h_edge = 1;
  }
  return is_active_h_edge;
}

// Checks to see if a super block is on a vertical image edge.
// In most cases this is the "real" edge unless there are formatting
// bars embedded in the stream.
int vp10_active_v_edge(VP10_COMP *cpi, int mi_col, int mi_step) {
  int left_edge = 0;
  int right_edge = cpi->common.mi_cols;
  int is_active_v_edge = 0;

  // For two pass account for any formatting bars detected.
  if (cpi->oxcf.pass == 2) {
    TWO_PASS *twopass = &cpi->twopass;

    // The inactive region is specified in MBs not mi units.
    // The image edge is in the following MB row.
    left_edge += (int)(twopass->this_frame_stats.inactive_zone_cols * 2);

    right_edge -= (int)(twopass->this_frame_stats.inactive_zone_cols * 2);
    right_edge = VPXMAX(left_edge, right_edge);
  }

  if (((left_edge >= mi_col) && (left_edge < (mi_col + mi_step))) ||
      ((right_edge >= mi_col) && (right_edge < (mi_col + mi_step)))) {
    is_active_v_edge = 1;
  }
  return is_active_v_edge;
}

// Checks to see if a super block is at the edge of the active image.
// In most cases this is the "real" edge unless there are formatting
// bars embedded in the stream.
int vp10_active_edge_sb(VP10_COMP *cpi,
                       int mi_row, int mi_col) {
  return vp10_active_h_edge(cpi, mi_row, MI_BLOCK_SIZE) ||
         vp10_active_v_edge(cpi, mi_col, MI_BLOCK_SIZE);
}

static void restore_uv_color_map(VP10_COMP *cpi, MACROBLOCK *x) {
  MACROBLOCKD *const xd = &x->e_mbd;
  MB_MODE_INFO *const mbmi = &xd->mi[0]->mbmi;
  PALETTE_MODE_INFO *const pmi = &mbmi->palette_mode_info;
  const BLOCK_SIZE bsize = mbmi->sb_type;
  const int rows = (4 * num_4x4_blocks_high_lookup[bsize]) >>
      (xd->plane[1].subsampling_y);
  const int cols = (4 * num_4x4_blocks_wide_lookup[bsize]) >>
      (xd->plane[1].subsampling_x);
  int src_stride = x->plane[1].src.stride;
  const uint8_t *const src_u = x->plane[1].src.buf;
  const uint8_t *const src_v = x->plane[2].src.buf;
  double *const data = x->palette_buffer->kmeans_data_buf;
  uint8_t *const indices = x->palette_buffer->kmeans_indices_buf;
  double centroids[2 * PALETTE_MAX_SIZE];
  uint8_t *const color_map = xd->plane[1].color_index_map;
  int r, c;
#if CONFIG_VP9_HIGHBITDEPTH
  const uint16_t *const src_u16 = CONVERT_TO_SHORTPTR(src_u);
  const uint16_t *const src_v16 = CONVERT_TO_SHORTPTR(src_v);
#endif  // CONFIG_VP9_HIGHBITDEPTH
  (void)cpi;

  for (r = 0; r < rows; ++r) {
    for (c = 0; c < cols; ++c) {
#if CONFIG_VP9_HIGHBITDEPTH
      if (cpi->common.use_highbitdepth) {
        data[(r * cols + c) * 2 ] =
            src_u16[r * src_stride + c];
        data[(r * cols + c) * 2 + 1] =
            src_v16[r * src_stride + c];
      } else {
#endif  // CONFIG_VP9_HIGHBITDEPTH
        data[(r * cols + c) * 2 ] =
            src_u[r * src_stride + c];
        data[(r * cols + c) * 2 + 1] =
            src_v[r * src_stride + c];
#if CONFIG_VP9_HIGHBITDEPTH
      }
#endif  // CONFIG_VP9_HIGHBITDEPTH
    }
  }

  for (r = 1; r < 3; ++r) {
    for (c = 0; c < pmi->palette_size[1]; ++c) {
      centroids[c * 2 + r - 1] = pmi->palette_colors[r * PALETTE_MAX_SIZE + c];
    }
  }

  vp10_calc_indices(data, centroids, indices, rows * cols,
                    pmi->palette_size[1], 2);

  for (r = 0; r < rows; ++r)
    for (c = 0; c < cols; ++c)
      color_map[r * cols + c] = indices[r * cols + c];
}

void vp10_rd_pick_inter_mode_sb(VP10_COMP *cpi,
                                TileDataEnc *tile_data,
                                MACROBLOCK *x,
                                int mi_row, int mi_col,
                                RD_COST *rd_cost,
#if CONFIG_SUPERTX
                                int *returnrate_nocoef,
#endif  // CONFIG_SUPERTX
                                BLOCK_SIZE bsize,
                                PICK_MODE_CONTEXT *ctx,
                                int64_t best_rd_so_far) {
  VP10_COMMON *const cm = &cpi->common;
  RD_OPT *const rd_opt = &cpi->rd;
  SPEED_FEATURES *const sf = &cpi->sf;
  MACROBLOCKD *const xd = &x->e_mbd;
  MB_MODE_INFO *const mbmi = &xd->mi[0]->mbmi;
  PALETTE_MODE_INFO *const pmi = &mbmi->palette_mode_info;
  MB_MODE_INFO_EXT *const mbmi_ext = x->mbmi_ext;
  const struct segmentation *const seg = &cm->seg;
  PREDICTION_MODE this_mode;
  MV_REFERENCE_FRAME ref_frame, second_ref_frame;
  unsigned char segment_id = mbmi->segment_id;
  int comp_pred, i, k;
  int_mv frame_mv[MB_MODE_COUNT][MAX_REF_FRAMES];
  struct buf_2d yv12_mb[MAX_REF_FRAMES][MAX_MB_PLANE];
#if CONFIG_EXT_INTER
  int_mv single_newmvs[2][MAX_REF_FRAMES] = { { { 0 } },  { { 0 } } };
  int single_newmvs_rate[2][MAX_REF_FRAMES] = { { 0 }, { 0 } };
#else
  int_mv single_newmv[MAX_REF_FRAMES] = { { 0 } };
#endif  // CONFIG_EXT_INTER
  INTERP_FILTER single_inter_filter[MB_MODE_COUNT][MAX_REF_FRAMES];
  int single_skippable[MB_MODE_COUNT][MAX_REF_FRAMES];
  static const int flag_list[REFS_PER_FRAME + 1] = {
    0,
    VP9_LAST_FLAG,
#if CONFIG_EXT_REFS
    VP9_LAST2_FLAG,
    VP9_LAST3_FLAG,
    VP9_LAST4_FLAG,
#endif  // CONFIG_EXT_REFS
    VP9_GOLD_FLAG,
    VP9_ALT_FLAG
  };
  int64_t best_rd = best_rd_so_far;
  int64_t best_pred_diff[REFERENCE_MODES];
  int64_t best_pred_rd[REFERENCE_MODES];
  MB_MODE_INFO best_mbmode;
  int best_mode_skippable = 0;
  int midx, best_mode_index = -1;
  unsigned int ref_costs_single[MAX_REF_FRAMES], ref_costs_comp[MAX_REF_FRAMES];
  vpx_prob comp_mode_p;
  int64_t best_intra_rd = INT64_MAX;
  unsigned int best_pred_sse = UINT_MAX;
  PREDICTION_MODE best_intra_mode = DC_PRED;
  int rate_uv_intra[TX_SIZES], rate_uv_tokenonly[TX_SIZES];
  int64_t dist_uv[TX_SIZES];
  int skip_uv[TX_SIZES];
  PREDICTION_MODE mode_uv[TX_SIZES];
  PALETTE_MODE_INFO pmi_uv[TX_SIZES];
#if CONFIG_EXT_INTRA
  EXT_INTRA_MODE_INFO ext_intra_mode_info_uv[TX_SIZES];
  int8_t uv_angle_delta[TX_SIZES];
  int is_directional_mode, angle_stats_ready = 0;
  int rate_overhead, rate_dummy;
  uint8_t directional_mode_skip_mask[INTRA_MODES];
#endif  // CONFIG_EXT_INTRA
  const int intra_cost_penalty = vp10_get_intra_cost_penalty(
      cm->base_qindex, cm->y_dc_delta_q, cm->bit_depth);
  const int * const intra_mode_cost =
      cpi->mbmode_cost[size_group_lookup[bsize]];
  int best_skip2 = 0;
  uint8_t ref_frame_skip_mask[2] = { 0 };
#if CONFIG_EXT_INTER
  uint32_t mode_skip_mask[MAX_REF_FRAMES] = { 0 };
#else
  uint16_t mode_skip_mask[MAX_REF_FRAMES] = { 0 };
#endif  // CONFIG_EXT_INTER
  int mode_skip_start = sf->mode_skip_start + 1;
  const int *const rd_threshes = rd_opt->threshes[segment_id][bsize];
  const int *const rd_thresh_freq_fact = tile_data->thresh_freq_fact[bsize];
  int64_t mode_threshold[MAX_MODES];
  int *mode_map = tile_data->mode_map[bsize];
  const int mode_search_skip_flags = sf->mode_search_skip_flags;
  const TX_SIZE max_tx_size = max_txsize_lookup[bsize];
  int palette_ctx = 0;
  const int rows = 4 * num_4x4_blocks_high_lookup[bsize];
  const int cols = 4 * num_4x4_blocks_wide_lookup[bsize];
  const MODE_INFO *above_mi = xd->above_mi;
  const MODE_INFO *left_mi = xd->left_mi;
#if CONFIG_OBMC
#if CONFIG_VP9_HIGHBITDEPTH
  DECLARE_ALIGNED(16, uint8_t, tmp_buf1[2 * MAX_MB_PLANE * 64 * 64]);
  DECLARE_ALIGNED(16, uint8_t, tmp_buf2[2 * MAX_MB_PLANE * 64 * 64]);
#else
  DECLARE_ALIGNED(16, uint8_t, tmp_buf1[MAX_MB_PLANE * 64 * 64]);
  DECLARE_ALIGNED(16, uint8_t, tmp_buf2[MAX_MB_PLANE * 64 * 64]);
#endif  // CONFIG_VP9_HIGHBITDEPTH
  uint8_t *dst_buf1[3], *dst_buf2[3];
  int dst_stride1[3] = {64, 64, 64};
  int dst_stride2[3] = {64, 64, 64};

#if CONFIG_VP9_HIGHBITDEPTH
  if (xd->cur_buf->flags & YV12_FLAG_HIGHBITDEPTH) {
    int len = sizeof(uint16_t);
    dst_buf1[0] = CONVERT_TO_BYTEPTR(tmp_buf1);
    dst_buf1[1] = CONVERT_TO_BYTEPTR(tmp_buf1 + 4096 * len);
    dst_buf1[2] = CONVERT_TO_BYTEPTR(tmp_buf1 + 8192 * len);
    dst_buf2[0] = CONVERT_TO_BYTEPTR(tmp_buf2);
    dst_buf2[1] = CONVERT_TO_BYTEPTR(tmp_buf2 + 4096 * len);
    dst_buf2[2] = CONVERT_TO_BYTEPTR(tmp_buf2 + 8192 * len);
  } else {
#endif  // CONFIG_VP9_HIGHBITDEPTH
  dst_buf1[0] = tmp_buf1;
  dst_buf1[1] = tmp_buf1 + 4096;
  dst_buf1[2] = tmp_buf1 + 8192;
  dst_buf2[0] = tmp_buf2;
  dst_buf2[1] = tmp_buf2 + 4096;
  dst_buf2[2] = tmp_buf2 + 8192;
#if CONFIG_VP9_HIGHBITDEPTH
  }
#endif  // CONFIG_VP9_HIGHBITDEPTH
#endif  // CONFIG_OBMC

  vp10_zero(best_mbmode);
  vp10_zero(pmi_uv);

  if (cm->allow_screen_content_tools) {
    if (above_mi)
      palette_ctx += (above_mi->mbmi.palette_mode_info.palette_size[0] > 0);
    if (left_mi)
      palette_ctx += (left_mi->mbmi.palette_mode_info.palette_size[0] > 0);
  }

#if CONFIG_EXT_INTRA
  memset(directional_mode_skip_mask, 0,
         sizeof(directional_mode_skip_mask[0]) * INTRA_MODES);
#endif  // CONFIG_EXT_INTRA

  estimate_ref_frame_costs(cm, xd, segment_id, ref_costs_single, ref_costs_comp,
                           &comp_mode_p);

  for (i = 0; i < REFERENCE_MODES; ++i)
    best_pred_rd[i] = INT64_MAX;
  for (i = 0; i < TX_SIZES; i++)
    rate_uv_intra[i] = INT_MAX;
  for (i = 0; i < MAX_REF_FRAMES; ++i)
    x->pred_sse[i] = INT_MAX;
  for (i = 0; i < MB_MODE_COUNT; ++i) {
    for (k = 0; k < MAX_REF_FRAMES; ++k) {
      single_inter_filter[i][k] = SWITCHABLE;
      single_skippable[i][k] = 0;
    }
  }

  rd_cost->rate = INT_MAX;
#if CONFIG_SUPERTX
  *returnrate_nocoef = INT_MAX;
#endif  // CONFIG_SUPERTX

  for (ref_frame = LAST_FRAME; ref_frame <= ALTREF_FRAME; ++ref_frame) {
    x->pred_mv_sad[ref_frame] = INT_MAX;
    x->mbmi_ext->mode_context[ref_frame] = 0;
#if CONFIG_REF_MV && CONFIG_EXT_INTER
    x->mbmi_ext->compound_mode_context[ref_frame] = 0;
#endif  // CONFIG_REF_MV && CONFIG_EXT_INTER
    if (cpi->ref_frame_flags & flag_list[ref_frame]) {
      assert(get_ref_frame_buffer(cpi, ref_frame) != NULL);
      setup_buffer_inter(cpi, x, ref_frame, bsize, mi_row, mi_col,
                         frame_mv[NEARESTMV], frame_mv[NEARMV], yv12_mb);
    }
    frame_mv[NEWMV][ref_frame].as_int = INVALID_MV;
    frame_mv[ZEROMV][ref_frame].as_int = 0;
#if CONFIG_EXT_INTER
    frame_mv[NEWFROMNEARMV][ref_frame].as_int = INVALID_MV;
    frame_mv[NEW_NEWMV][ref_frame].as_int = INVALID_MV;
    frame_mv[ZERO_ZEROMV][ref_frame].as_int = 0;
#endif  // CONFIG_EXT_INTER
  }

#if CONFIG_REF_MV
  for (; ref_frame < MODE_CTX_REF_FRAMES; ++ref_frame) {
    MODE_INFO *const mi = xd->mi[0];
    int_mv *const candidates = x->mbmi_ext->ref_mvs[ref_frame];
    x->mbmi_ext->mode_context[ref_frame] = 0;
    vp10_find_mv_refs(cm, xd, mi, ref_frame,
                      &mbmi_ext->ref_mv_count[ref_frame],
                      mbmi_ext->ref_mv_stack[ref_frame],
#if CONFIG_EXT_INTER
                      mbmi_ext->compound_mode_context,
#endif  // CONFIG_EXT_INTER
                      candidates, mi_row, mi_col,
                      NULL, NULL, mbmi_ext->mode_context);
  }
#endif  // CONFIG_REF_MV

#if CONFIG_OBMC
  vp10_build_prediction_by_above_preds(cpi, xd, mi_row, mi_col, dst_buf1,
                                       dst_stride1);
  vp10_build_prediction_by_left_preds(cpi, xd, mi_row, mi_col, dst_buf2,
                                      dst_stride2);
  vp10_setup_dst_planes(xd->plane, get_frame_new_buffer(cm), mi_row, mi_col);
#endif  // CONFIG_OBMC

  for (ref_frame = LAST_FRAME; ref_frame <= ALTREF_FRAME; ++ref_frame) {
    if (!(cpi->ref_frame_flags & flag_list[ref_frame])) {
      // Skip checking missing references in both single and compound reference
      // modes. Note that a mode will be skipped iff both reference frames
      // are masked out.
      ref_frame_skip_mask[0] |= (1 << ref_frame);
      ref_frame_skip_mask[1] |= SECOND_REF_FRAME_MASK;
    } else {
      for (i = LAST_FRAME; i <= ALTREF_FRAME; ++i) {
        // Skip fixed mv modes for poor references
        if ((x->pred_mv_sad[ref_frame] >> 2) > x->pred_mv_sad[i]) {
          mode_skip_mask[ref_frame] |= INTER_NEAREST_NEAR_ZERO;
          break;
        }
      }
    }
    // If the segment reference frame feature is enabled....
    // then do nothing if the current ref frame is not allowed..
    if (segfeature_active(seg, segment_id, SEG_LVL_REF_FRAME) &&
        get_segdata(seg, segment_id, SEG_LVL_REF_FRAME) != (int)ref_frame) {
      ref_frame_skip_mask[0] |= (1 << ref_frame);
      ref_frame_skip_mask[1] |= SECOND_REF_FRAME_MASK;
    }
  }

  // Disable this drop out case if the ref frame
  // segment level feature is enabled for this segment. This is to
  // prevent the possibility that we end up unable to pick any mode.
  if (!segfeature_active(seg, segment_id, SEG_LVL_REF_FRAME)) {
    // Only consider ZEROMV/ALTREF_FRAME for alt ref frame,
    // unless ARNR filtering is enabled in which case we want
    // an unfiltered alternative. We allow near/nearest as well
    // because they may result in zero-zero MVs but be cheaper.
    if (cpi->rc.is_src_frame_alt_ref && (cpi->oxcf.arnr_max_frames == 0)) {
      ref_frame_skip_mask[0] =
          (1 << LAST_FRAME) |
#if CONFIG_EXT_REFS
          (1 << LAST2_FRAME) |
          (1 << LAST3_FRAME) |
          (1 << LAST4_FRAME) |
#endif  // CONFIG_EXT_REFS
          (1 << GOLDEN_FRAME);
      ref_frame_skip_mask[1] = SECOND_REF_FRAME_MASK;
      mode_skip_mask[ALTREF_FRAME] = ~INTER_NEAREST_NEAR_ZERO;
      if (frame_mv[NEARMV][ALTREF_FRAME].as_int != 0)
        mode_skip_mask[ALTREF_FRAME] |= (1 << NEARMV);
      if (frame_mv[NEARESTMV][ALTREF_FRAME].as_int != 0)
        mode_skip_mask[ALTREF_FRAME] |= (1 << NEARESTMV);
#if CONFIG_EXT_INTER
      if (frame_mv[NEAREST_NEARESTMV][ALTREF_FRAME].as_int != 0)
        mode_skip_mask[ALTREF_FRAME] |= (1 << NEAREST_NEARESTMV);
      if (frame_mv[NEAREST_NEARMV][ALTREF_FRAME].as_int != 0)
        mode_skip_mask[ALTREF_FRAME] |= (1 << NEAREST_NEARMV);
      if (frame_mv[NEAR_NEARESTMV][ALTREF_FRAME].as_int != 0)
        mode_skip_mask[ALTREF_FRAME] |= (1 << NEAR_NEARESTMV);
#endif  // CONFIG_EXT_INTER
    }
  }

  if (cpi->rc.is_src_frame_alt_ref) {
    if (sf->alt_ref_search_fp) {
      mode_skip_mask[ALTREF_FRAME] = 0;
      ref_frame_skip_mask[0] = ~(1 << ALTREF_FRAME);
      ref_frame_skip_mask[1] = SECOND_REF_FRAME_MASK;
    }
  }

  if (sf->alt_ref_search_fp)
    if (!cm->show_frame && x->pred_mv_sad[GOLDEN_FRAME] < INT_MAX)
      if (x->pred_mv_sad[ALTREF_FRAME] > (x->pred_mv_sad[GOLDEN_FRAME] << 1))
        mode_skip_mask[ALTREF_FRAME] |= INTER_ALL;

  if (sf->adaptive_mode_search) {
    if (cm->show_frame && !cpi->rc.is_src_frame_alt_ref &&
        cpi->rc.frames_since_golden >= 3)
      if (x->pred_mv_sad[GOLDEN_FRAME] > (x->pred_mv_sad[LAST_FRAME] << 1))
        mode_skip_mask[GOLDEN_FRAME] |= INTER_ALL;
  }

  if (bsize > sf->max_intra_bsize) {
    ref_frame_skip_mask[0] |= (1 << INTRA_FRAME);
    ref_frame_skip_mask[1] |= (1 << INTRA_FRAME);
  }

  mode_skip_mask[INTRA_FRAME] |=
      ~(sf->intra_y_mode_mask[max_txsize_lookup[bsize]]);

  for (i = 0; i <= LAST_NEW_MV_INDEX; ++i)
    mode_threshold[i] = 0;
  for (i = LAST_NEW_MV_INDEX + 1; i < MAX_MODES; ++i)
    mode_threshold[i] = ((int64_t)rd_threshes[i] * rd_thresh_freq_fact[i]) >> 5;

  midx =  sf->schedule_mode_search ? mode_skip_start : 0;
  while (midx > 4) {
    uint8_t end_pos = 0;
    for (i = 5; i < midx; ++i) {
      if (mode_threshold[mode_map[i - 1]] > mode_threshold[mode_map[i]]) {
        uint8_t tmp = mode_map[i];
        mode_map[i] = mode_map[i - 1];
        mode_map[i - 1] = tmp;
        end_pos = i;
      }
    }
    midx = end_pos;
  }

  for (midx = 0; midx < MAX_MODES; ++midx) {
    int mode_index = mode_map[midx];
    int mode_excluded = 0;
    int64_t this_rd = INT64_MAX;
    int disable_skip = 0;
    int compmode_cost = 0;
#if CONFIG_EXT_INTER
    int compmode_interintra_cost = 0;
    int compmode_wedge_cost = 0;
#endif  // CONFIG_EXT_INTER
    int rate2 = 0, rate_y = 0, rate_uv = 0;
    int64_t distortion2 = 0, distortion_y = 0, distortion_uv = 0;
    int skippable = 0;
    int this_skip2 = 0;
    int64_t total_sse = INT64_MAX;
    int early_term = 0;
#if CONFIG_REF_MV
    uint8_t ref_frame_type;
#endif

    this_mode = vp10_mode_order[mode_index].mode;
    ref_frame = vp10_mode_order[mode_index].ref_frame[0];
    second_ref_frame = vp10_mode_order[mode_index].ref_frame[1];

#if CONFIG_EXT_INTER
    if (ref_frame > INTRA_FRAME && second_ref_frame == INTRA_FRAME) {
      // Mode must by compatible
      assert(is_interintra_allowed_mode(this_mode));

      if (!is_interintra_allowed_bsize(bsize))
        continue;
    }

    if (this_mode == NEAREST_NEARESTMV) {
      frame_mv[NEAREST_NEARESTMV][ref_frame].as_int =
          frame_mv[NEARESTMV][ref_frame].as_int;
      frame_mv[NEAREST_NEARESTMV][second_ref_frame].as_int =
          frame_mv[NEARESTMV][second_ref_frame].as_int;
    } else if (this_mode == NEAREST_NEARMV) {
      frame_mv[NEAREST_NEARMV][ref_frame].as_int =
          frame_mv[NEARESTMV][ref_frame].as_int;
      frame_mv[NEAREST_NEARMV][second_ref_frame].as_int =
          frame_mv[NEARMV][second_ref_frame].as_int;
    } else if (this_mode == NEAR_NEARESTMV) {
      frame_mv[NEAR_NEARESTMV][ref_frame].as_int =
          frame_mv[NEARMV][ref_frame].as_int;
      frame_mv[NEAR_NEARESTMV][second_ref_frame].as_int =
          frame_mv[NEARESTMV][second_ref_frame].as_int;
    } else if (this_mode == NEAREST_NEWMV) {
      frame_mv[NEAREST_NEWMV][ref_frame].as_int =
          frame_mv[NEARESTMV][ref_frame].as_int;
      frame_mv[NEAREST_NEWMV][second_ref_frame].as_int =
          frame_mv[NEWMV][second_ref_frame].as_int;
    } else if (this_mode == NEW_NEARESTMV) {
      frame_mv[NEW_NEARESTMV][ref_frame].as_int =
          frame_mv[NEWMV][ref_frame].as_int;
      frame_mv[NEW_NEARESTMV][second_ref_frame].as_int =
          frame_mv[NEARESTMV][second_ref_frame].as_int;
    } else if (this_mode == NEAR_NEWMV) {
      frame_mv[NEAR_NEWMV][ref_frame].as_int =
        frame_mv[NEARMV][ref_frame].as_int;
      frame_mv[NEAR_NEWMV][second_ref_frame].as_int =
          frame_mv[NEWMV][second_ref_frame].as_int;
    } else if (this_mode == NEW_NEARMV) {
      frame_mv[NEW_NEARMV][ref_frame].as_int =
          frame_mv[NEWMV][ref_frame].as_int;
      frame_mv[NEW_NEARMV][second_ref_frame].as_int =
        frame_mv[NEARMV][second_ref_frame].as_int;
    } else if (this_mode == NEW_NEWMV) {
      frame_mv[NEW_NEWMV][ref_frame].as_int =
          frame_mv[NEWMV][ref_frame].as_int;
      frame_mv[NEW_NEWMV][second_ref_frame].as_int =
          frame_mv[NEWMV][second_ref_frame].as_int;
    }
#endif  // CONFIG_EXT_INTER

    // Look at the reference frame of the best mode so far and set the
    // skip mask to look at a subset of the remaining modes.
    if (midx == mode_skip_start && best_mode_index >= 0) {
      switch (best_mbmode.ref_frame[0]) {
        case INTRA_FRAME:
          break;
        case LAST_FRAME:
          ref_frame_skip_mask[0] |= LAST_FRAME_MODE_MASK;
          ref_frame_skip_mask[1] |= SECOND_REF_FRAME_MASK;
          break;
#if CONFIG_EXT_REFS
        case LAST2_FRAME:
          ref_frame_skip_mask[0] |= LAST2_FRAME_MODE_MASK;
          ref_frame_skip_mask[1] |= SECOND_REF_FRAME_MASK;
          break;
        case LAST3_FRAME:
          ref_frame_skip_mask[0] |= LAST3_FRAME_MODE_MASK;
          ref_frame_skip_mask[1] |= SECOND_REF_FRAME_MASK;
          break;
        case LAST4_FRAME:
          ref_frame_skip_mask[0] |= LAST4_FRAME_MODE_MASK;
          ref_frame_skip_mask[1] |= SECOND_REF_FRAME_MASK;
          break;
#endif  // CONFIG_EXT_REFS
        case GOLDEN_FRAME:
          ref_frame_skip_mask[0] |= GOLDEN_FRAME_MODE_MASK;
          ref_frame_skip_mask[1] |= SECOND_REF_FRAME_MASK;
          break;
        case ALTREF_FRAME:
          ref_frame_skip_mask[0] |= ALT_REF_MODE_MASK;
          break;
        case NONE:
        case MAX_REF_FRAMES:
          assert(0 && "Invalid Reference frame");
          break;
      }
    }

    if ((ref_frame_skip_mask[0] & (1 << ref_frame)) &&
        (ref_frame_skip_mask[1] & (1 << VPXMAX(0, second_ref_frame))))
      continue;

    if (mode_skip_mask[ref_frame] & (1 << this_mode))
      continue;

    // Test best rd so far against threshold for trying this mode.
    if (best_mode_skippable && sf->schedule_mode_search)
      mode_threshold[mode_index] <<= 1;

    if (best_rd < mode_threshold[mode_index])
      continue;

    comp_pred = second_ref_frame > INTRA_FRAME;
    if (comp_pred) {
      if (!cpi->allow_comp_inter_inter)
        continue;

      // Skip compound inter modes if ARF is not available.
      if (!(cpi->ref_frame_flags & flag_list[second_ref_frame]))
        continue;

      // Do not allow compound prediction if the segment level reference frame
      // feature is in use as in this case there can only be one reference.
      if (segfeature_active(seg, segment_id, SEG_LVL_REF_FRAME))
        continue;

      if ((mode_search_skip_flags & FLAG_SKIP_COMP_BESTINTRA) &&
          best_mode_index >= 0 && best_mbmode.ref_frame[0] == INTRA_FRAME)
        continue;

      mode_excluded = cm->reference_mode == SINGLE_REFERENCE;
    } else {
      if (ref_frame != INTRA_FRAME)
        mode_excluded = cm->reference_mode == COMPOUND_REFERENCE;
    }

    if (ref_frame == INTRA_FRAME) {
      if (sf->adaptive_mode_search)
        if ((x->source_variance << num_pels_log2_lookup[bsize]) > best_pred_sse)
          continue;

      if (this_mode != DC_PRED) {
        // Disable intra modes other than DC_PRED for blocks with low variance
        // Threshold for intra skipping based on source variance
        // TODO(debargha): Specialize the threshold for super block sizes
        const unsigned int skip_intra_var_thresh = 64;
        if ((mode_search_skip_flags & FLAG_SKIP_INTRA_LOWVAR) &&
            x->source_variance < skip_intra_var_thresh)
          continue;
        // Only search the oblique modes if the best so far is
        // one of the neighboring directional modes
        if ((mode_search_skip_flags & FLAG_SKIP_INTRA_BESTINTER) &&
            (this_mode >= D45_PRED && this_mode <= TM_PRED)) {
          if (best_mode_index >= 0 &&
              best_mbmode.ref_frame[0] > INTRA_FRAME)
            continue;
        }
        if (mode_search_skip_flags & FLAG_SKIP_INTRA_DIRMISMATCH) {
          if (conditional_skipintra(this_mode, best_intra_mode))
              continue;
        }
      }
    } else {
      const MV_REFERENCE_FRAME ref_frames[2] = {ref_frame, second_ref_frame};
      if (!check_best_zero_mv(cpi, mbmi_ext->mode_context,
#if CONFIG_REF_MV && CONFIG_EXT_INTER
                              mbmi_ext->compound_mode_context,
#endif  // CONFIG_REF_MV && CONFIG_EXT_INTER
                              frame_mv,
                              this_mode, ref_frames, bsize, -1))
        continue;
    }

    mbmi->mode = this_mode;
    mbmi->uv_mode = DC_PRED;
    mbmi->ref_frame[0] = ref_frame;
    mbmi->ref_frame[1] = second_ref_frame;
    pmi->palette_size[0] = 0;
    pmi->palette_size[1] = 0;
#if CONFIG_EXT_INTRA
    mbmi->ext_intra_mode_info.use_ext_intra_mode[0] = 0;
    mbmi->ext_intra_mode_info.use_ext_intra_mode[1] = 0;
#endif  // CONFIG_EXT_INTRA
    // Evaluate all sub-pel filters irrespective of whether we can use
    // them for this frame.
    mbmi->interp_filter = cm->interp_filter == SWITCHABLE ? EIGHTTAP_REGULAR
                                                          : cm->interp_filter;
    mbmi->mv[0].as_int = mbmi->mv[1].as_int = 0;
#if CONFIG_OBMC
    mbmi->obmc = 0;
#endif  // CONFIG_OBMC

    x->skip = 0;
    set_ref_ptrs(cm, xd, ref_frame, second_ref_frame);

    // Select prediction reference frames.
    for (i = 0; i < MAX_MB_PLANE; i++) {
      xd->plane[i].pre[0] = yv12_mb[ref_frame][i];
      if (comp_pred)
        xd->plane[i].pre[1] = yv12_mb[second_ref_frame][i];
    }

#if CONFIG_EXT_INTER
    mbmi->interintra_mode = (PREDICTION_MODE)(DC_PRED - 1);
    mbmi->interintra_uv_mode = (PREDICTION_MODE)(DC_PRED - 1);
#endif  // CONFIG_EXT_INTER

    if (ref_frame == INTRA_FRAME) {
      TX_SIZE uv_tx;
      struct macroblockd_plane *const pd = &xd->plane[1];
      memset(x->skip_txfm, 0, sizeof(x->skip_txfm));
#if CONFIG_EXT_INTRA
      is_directional_mode = (mbmi->mode != DC_PRED && mbmi->mode != TM_PRED);
      if (is_directional_mode) {
        if (!angle_stats_ready) {
          const int src_stride = x->plane[0].src.stride;
          const uint8_t *src = x->plane[0].src.buf;
          const int rows = 4 * num_4x4_blocks_high_lookup[bsize];
          const int cols = 4 * num_4x4_blocks_wide_lookup[bsize];
          double hist[DIRECTIONAL_MODES];
          PREDICTION_MODE mode;

#if CONFIG_VP9_HIGHBITDEPTH
          if (xd->cur_buf->flags & YV12_FLAG_HIGHBITDEPTH)
            highbd_angle_estimation(src, src_stride, rows, cols, hist);
          else
#endif
            angle_estimation(src, src_stride, rows, cols, hist);
          for (mode = 0; mode < INTRA_MODES; ++mode) {
            if (mode != DC_PRED && mode != TM_PRED) {
              int index = get_angle_index((double)mode_to_angle_map[mode]);
              double score, weight = 1.0;
              score = hist[index];
              if (index > 0) {
                score += hist[index - 1] * 0.5;
                weight += 0.5;
              }
              if (index < DIRECTIONAL_MODES - 1) {
                score += hist[index + 1] * 0.5;
                weight += 0.5;
              }
              score /= weight;
              if (score < ANGLE_SKIP_THRESH)
                directional_mode_skip_mask[mode] = 1;
            }
          }
          angle_stats_ready = 1;
        }
        if (directional_mode_skip_mask[mbmi->mode])
          continue;
        rate_overhead = write_uniform_cost(2 * MAX_ANGLE_DELTAS + 1, 0) +
            intra_mode_cost[mbmi->mode];
        rate_y = INT_MAX;
        this_rd =
            rd_pick_intra_angle_sby(cpi, x, &rate_dummy, &rate_y, &distortion_y,
                                    &skippable, bsize, rate_overhead, best_rd);
      } else {
        mbmi->angle_delta[0] = 0;
        super_block_yrd(cpi, x, &rate_y, &distortion_y, &skippable,
                        NULL, bsize, best_rd);
      }

      // TODO(huisu): ext-intra is turned off in lossless mode for now to
      // avoid a unit test failure
      if (mbmi->mode == DC_PRED && !xd->lossless[mbmi->segment_id] &&
          ALLOW_FILTER_INTRA_MODES) {
        MB_MODE_INFO mbmi_copy = *mbmi;

        if (rate_y != INT_MAX) {
          int this_rate = rate_y + intra_mode_cost[mbmi->mode] +
              vp10_cost_bit(cm->fc->ext_intra_probs[0], 0);
          this_rd = RDCOST(x->rdmult, x->rddiv, this_rate, distortion_y);
        } else {
          this_rd = best_rd;
        }

        if (!rd_pick_ext_intra_sby(cpi, x, &rate_dummy, &rate_y, &distortion_y,
                                   &skippable, bsize,
                                   intra_mode_cost[mbmi->mode], &this_rd))
          *mbmi = mbmi_copy;
      }
#else
      super_block_yrd(cpi, x, &rate_y, &distortion_y, &skippable,
                      NULL, bsize, best_rd);
#endif  // CONFIG_EXT_INTRA

      if (rate_y == INT_MAX)
        continue;
      uv_tx = get_uv_tx_size_impl(mbmi->tx_size, bsize, pd->subsampling_x,
                                  pd->subsampling_y);
      if (rate_uv_intra[uv_tx] == INT_MAX) {
        choose_intra_uv_mode(cpi, x, ctx, bsize, uv_tx,
                             &rate_uv_intra[uv_tx], &rate_uv_tokenonly[uv_tx],
                             &dist_uv[uv_tx], &skip_uv[uv_tx], &mode_uv[uv_tx]);
        if (cm->allow_screen_content_tools)
          pmi_uv[uv_tx] = *pmi;
#if CONFIG_EXT_INTRA
        ext_intra_mode_info_uv[uv_tx] = mbmi->ext_intra_mode_info;
        uv_angle_delta[uv_tx] = mbmi->angle_delta[1];
#endif  // CONFIG_EXT_INTRA
      }

      rate_uv = rate_uv_tokenonly[uv_tx];
      distortion_uv = dist_uv[uv_tx];
      skippable = skippable && skip_uv[uv_tx];
      mbmi->uv_mode = mode_uv[uv_tx];
      if (cm->allow_screen_content_tools) {
        pmi->palette_size[1] = pmi_uv[uv_tx].palette_size[1];
        memcpy(pmi->palette_colors + PALETTE_MAX_SIZE,
               pmi_uv[uv_tx].palette_colors + PALETTE_MAX_SIZE,
               2 * PALETTE_MAX_SIZE * sizeof(pmi->palette_colors[0]));
      }
#if CONFIG_EXT_INTRA
      mbmi->angle_delta[1] = uv_angle_delta[uv_tx];
      mbmi->ext_intra_mode_info.use_ext_intra_mode[1] =
          ext_intra_mode_info_uv[uv_tx].use_ext_intra_mode[1];
      if (ext_intra_mode_info_uv[uv_tx].use_ext_intra_mode[1]) {
        mbmi->ext_intra_mode_info.ext_intra_mode[1] =
            ext_intra_mode_info_uv[uv_tx].ext_intra_mode[1];
      }
#endif  // CONFIG_EXT_INTRA

      rate2 = rate_y + intra_mode_cost[mbmi->mode] + rate_uv_intra[uv_tx];
      if (cpi->common.allow_screen_content_tools && mbmi->mode == DC_PRED)
        rate2 +=
            vp10_cost_bit(vp10_default_palette_y_mode_prob[bsize - BLOCK_8X8]
                                                          [palette_ctx], 0);

      if (!xd->lossless[mbmi->segment_id]) {
        // super_block_yrd above includes the cost of the tx_size in the
        // tokenonly rate, but for intra blocks, tx_size is always coded
        // (prediction granularity), so we account for it in the full rate,
        // not the tokenonly rate.
        rate_y -=
            cpi->tx_size_cost[max_tx_size - TX_8X8][get_tx_size_context(xd)]
                                                   [mbmi->tx_size];
      }
#if CONFIG_EXT_INTRA
      if (is_directional_mode) {
        int p_angle;
        const int intra_filter_ctx = vp10_get_pred_context_intra_interp(xd);
        rate2 += write_uniform_cost(2 * MAX_ANGLE_DELTAS + 1,
                                    MAX_ANGLE_DELTAS +
                                    mbmi->angle_delta[0]);
        p_angle = mode_to_angle_map[mbmi->mode] +
            mbmi->angle_delta[0] * ANGLE_STEP;
        if (pick_intra_filter(p_angle))
          rate2 += cpi->intra_filter_cost[intra_filter_ctx][mbmi->intra_filter];
      }

      if (mbmi->mode == DC_PRED && ALLOW_FILTER_INTRA_MODES) {
        rate2 += vp10_cost_bit(cm->fc->ext_intra_probs[0],
                               mbmi->ext_intra_mode_info.use_ext_intra_mode[0]);
        if (mbmi->ext_intra_mode_info.use_ext_intra_mode[0]) {
          EXT_INTRA_MODE ext_intra_mode =
              mbmi->ext_intra_mode_info.ext_intra_mode[0];
          rate2 += write_uniform_cost(FILTER_INTRA_MODES, ext_intra_mode);
        }
      }
#endif  // CONFIG_EXT_INTRA
      if (this_mode != DC_PRED && this_mode != TM_PRED)
        rate2 += intra_cost_penalty;
      distortion2 = distortion_y + distortion_uv;
    } else {
#if CONFIG_EXT_INTER
      if (second_ref_frame == INTRA_FRAME) {
        mbmi->interintra_mode = best_intra_mode;
        mbmi->interintra_uv_mode = best_intra_mode;
#if CONFIG_EXT_INTRA
        // TODO(debargha|geza.lore):
        // Should we use ext_intra modes for interintra?
        mbmi->ext_intra_mode_info.use_ext_intra_mode[0] = 0;
        mbmi->ext_intra_mode_info.use_ext_intra_mode[1] = 0;
        mbmi->angle_delta[0] = 0;
        mbmi->angle_delta[1] = 0;
        mbmi->intra_filter = INTRA_FILTER_LINEAR;
#endif  // CONFIG_EXT_INTRA
      }
#endif  // CONFIG_EXT_INTER
#if CONFIG_REF_MV
      mbmi->ref_mv_idx = 0;
      ref_frame_type = vp10_ref_frame_type(mbmi->ref_frame);
#endif
      this_rd = handle_inter_mode(cpi, x, bsize,
                                  &rate2, &distortion2, &skippable,
                                  &rate_y, &rate_uv,
                                  &disable_skip, frame_mv,
                                  mi_row, mi_col,
#if CONFIG_OBMC
                                  dst_buf1, dst_stride1,
                                  dst_buf2, dst_stride2,
#endif  // CONFIG_OBMC
#if CONFIG_EXT_INTER
                                  single_newmvs,
                                  single_newmvs_rate,
                                  &compmode_interintra_cost,
                                  &compmode_wedge_cost,
#else
                                  single_newmv,
#endif  // CONFIG_EXT_INTER
                                  single_inter_filter,
                                  single_skippable,
                                  &total_sse, best_rd);

#if CONFIG_REF_MV
      // TODO(jingning): This needs some refactoring to improve code quality
      // and reduce redundant steps.
      if (mbmi->mode == NEARMV &&
          mbmi_ext->ref_mv_count[ref_frame_type] > 2) {
        int_mv backup_mv = frame_mv[NEARMV][ref_frame];
        int_mv cur_mv = mbmi_ext->ref_mv_stack[ref_frame][2].this_mv;
        MB_MODE_INFO backup_mbmi = *mbmi;
        int backup_skip = x->skip;

        int64_t tmp_ref_rd = this_rd;
        int ref_idx;
        int ref_set = VPXMIN(2, mbmi_ext->ref_mv_count[ref_frame_type] - 2);

        uint8_t drl0_ctx =
            vp10_drl_ctx(mbmi_ext->ref_mv_stack[ref_frame_type], 1);
        rate2 += cpi->drl_mode_cost0[drl0_ctx][0];

        if (this_rd < INT64_MAX) {
          if (RDCOST(x->rdmult, x->rddiv, rate_y + rate_uv, distortion2) <
              RDCOST(x->rdmult, x->rddiv, 0, total_sse))
            tmp_ref_rd = RDCOST(x->rdmult, x->rddiv,
                rate2 + vp10_cost_bit(vp10_get_skip_prob(cm, xd), 0),
                distortion2);
          else
            tmp_ref_rd = RDCOST(x->rdmult, x->rddiv,
                rate2 + vp10_cost_bit(vp10_get_skip_prob(cm, xd), 1) -
                rate_y - rate_uv,
                total_sse);
        }
#if CONFIG_VAR_TX
        for (i = 0; i < MAX_MB_PLANE; ++i)
          memcpy(x->blk_skip_drl[i], x->blk_skip[i],
                 sizeof(uint8_t) * ctx->num_4x4_blk);
#endif

        for (ref_idx = 0; ref_idx < ref_set; ++ref_idx) {
          int64_t tmp_alt_rd = INT64_MAX;
          int tmp_rate = 0, tmp_rate_y = 0, tmp_rate_uv = 0;
          int tmp_skip = 1;
          int64_t tmp_dist = 0, tmp_sse = 0;
          int dummy_disable_skip = 0;

          cur_mv = mbmi_ext->ref_mv_stack[ref_frame][2 + ref_idx].this_mv;
          lower_mv_precision(&cur_mv.as_mv, cm->allow_high_precision_mv);
          clamp_mv2(&cur_mv.as_mv, xd);

          if (!mv_check_bounds(x, &cur_mv.as_mv)) {
            INTERP_FILTER dummy_single_inter_filter[MB_MODE_COUNT]
                                                   [MAX_REF_FRAMES];
            int dummy_single_skippable[MB_MODE_COUNT][MAX_REF_FRAMES];
            int dummy_disable_skip = 0;
#if CONFIG_EXT_INTER
            int_mv dummy_single_newmvs[2][MAX_REF_FRAMES] =
                                          { { { 0 } },  { { 0 } } };
            int dummy_single_newmvs_rate[2][MAX_REF_FRAMES] =
                                          { { 0 }, { 0 } };
            int dummy_compmode_interintra_cost = 0;
            int dummy_compmode_wedge_cost = 0;
#else
            int_mv dummy_single_newmv[MAX_REF_FRAMES] = { { 0 } };
#endif
            mbmi->ref_mv_idx = 1 + ref_idx;

            frame_mv[NEARMV][ref_frame] = cur_mv;
            tmp_alt_rd = handle_inter_mode(cpi, x, bsize,
                                           &tmp_rate, &tmp_dist, &tmp_skip,
                                           &tmp_rate_y, &tmp_rate_uv,
                                           &dummy_disable_skip, frame_mv,
                                           mi_row, mi_col,
#if CONFIG_OBMC
                                           dst_buf1, dst_stride1,
                                           dst_buf2, dst_stride2,
#endif  // CONFIG_OBMC
#if CONFIG_EXT_INTER
                                           dummy_single_newmvs,
                                           dummy_single_newmvs_rate,
                                           &dummy_compmode_interintra_cost,
                                           &dummy_compmode_wedge_cost,
#else
                                           dummy_single_newmv,
#endif
                                           dummy_single_inter_filter,
                                           dummy_single_skippable,
                                           &tmp_sse, best_rd);
          }

          tmp_rate += cpi->drl_mode_cost0[drl0_ctx][1];

          if (mbmi_ext->ref_mv_count[ref_frame_type] > 3) {
            uint8_t drl1_ctx =
                vp10_drl_ctx(mbmi_ext->ref_mv_stack[ref_frame_type], 2);
            tmp_rate += cpi->drl_mode_cost1[drl1_ctx][ref_idx];
          }

          if (tmp_alt_rd < INT64_MAX) {
#if CONFIG_OBMC
            tmp_alt_rd = RDCOST(x->rdmult, x->rddiv, tmp_rate, tmp_dist);
#else
            if (RDCOST(x->rdmult, x->rddiv,
                       tmp_rate_y + tmp_rate_uv, tmp_dist) <
                RDCOST(x->rdmult, x->rddiv, 0, tmp_sse))
              tmp_alt_rd = RDCOST(x->rdmult, x->rddiv,
                  tmp_rate + vp10_cost_bit(vp10_get_skip_prob(cm, xd), 0),
                  tmp_dist);
            else
              tmp_alt_rd = RDCOST(x->rdmult, x->rddiv,
                  tmp_rate + vp10_cost_bit(vp10_get_skip_prob(cm, xd), 1) -
                  tmp_rate_y - tmp_rate_uv,
                  tmp_sse);
#endif  // CONFIG_OBMC
          }

          if (tmp_ref_rd > tmp_alt_rd) {
            rate2 = tmp_rate;
            disable_skip = dummy_disable_skip;
            distortion2 = tmp_dist;
            skippable = tmp_skip;
            rate_y = tmp_rate_y;
            rate_uv = tmp_rate_uv;
            total_sse = tmp_sse;
            this_rd = tmp_alt_rd;
            tmp_ref_rd = tmp_alt_rd;
            backup_mbmi = *mbmi;
            backup_skip = x->skip;
#if CONFIG_VAR_TX
            for (i = 0; i < MAX_MB_PLANE; ++i)
              memcpy(x->blk_skip_drl[i], x->blk_skip[i],
                     sizeof(uint8_t) * ctx->num_4x4_blk);
#endif
          } else {
            *mbmi = backup_mbmi;
            x->skip = backup_skip;
          }
        }

        frame_mv[NEARMV][ref_frame] = backup_mv;
#if CONFIG_VAR_TX
        for (i = 0; i < MAX_MB_PLANE; ++i)
          memcpy(x->blk_skip[i], x->blk_skip_drl[i],
                 sizeof(uint8_t) * ctx->num_4x4_blk);
#endif
      }
#endif  // CONFIG_REF_MV

      if (this_rd == INT64_MAX)
        continue;

      compmode_cost = vp10_cost_bit(comp_mode_p, comp_pred);

      if (cm->reference_mode == REFERENCE_MODE_SELECT)
        rate2 += compmode_cost;
    }

#if CONFIG_EXT_INTER
    rate2 += compmode_interintra_cost;
    if (cm->reference_mode != SINGLE_REFERENCE && comp_pred)
      rate2 += compmode_wedge_cost;
#endif  // CONFIG_EXT_INTER

    // Estimate the reference frame signaling cost and add it
    // to the rolling cost variable.
    if (comp_pred) {
      rate2 += ref_costs_comp[ref_frame];
    } else {
      rate2 += ref_costs_single[ref_frame];
    }

#if CONFIG_OBMC
    if (ref_frame == INTRA_FRAME) {
#else
    if (!disable_skip) {
#endif  // CONFIG_OBMC
      if (skippable) {
        // Back out the coefficient coding costs
        rate2 -= (rate_y + rate_uv);
        rate_y = 0;
        rate_uv = 0;
        // Cost the skip mb case
        rate2 += vp10_cost_bit(vp10_get_skip_prob(cm, xd), 1);
      } else if (ref_frame != INTRA_FRAME && !xd->lossless[mbmi->segment_id]) {
        if (RDCOST(x->rdmult, x->rddiv, rate_y + rate_uv, distortion2) <
            RDCOST(x->rdmult, x->rddiv, 0, total_sse)) {
          // Add in the cost of the no skip flag.
          rate2 += vp10_cost_bit(vp10_get_skip_prob(cm, xd), 0);
        } else {
          // FIXME(rbultje) make this work for splitmv also
          rate2 += vp10_cost_bit(vp10_get_skip_prob(cm, xd), 1);
          distortion2 = total_sse;
          assert(total_sse >= 0);
          rate2 -= (rate_y + rate_uv);
          this_skip2 = 1;
          rate_y = 0;
          rate_uv = 0;
        }
      } else {
        // Add in the cost of the no skip flag.
        rate2 += vp10_cost_bit(vp10_get_skip_prob(cm, xd), 0);
      }

      // Calculate the final RD estimate for this mode.
      this_rd = RDCOST(x->rdmult, x->rddiv, rate2, distortion2);
#if CONFIG_OBMC
    } else {
      this_skip2 = mbmi->skip;
      this_rd = RDCOST(x->rdmult, x->rddiv, rate2, distortion2);
#endif  // CONFIG_OBMC
    }


    // Apply an adjustment to the rd value based on the similarity of the
    // source variance and reconstructed variance.
    rd_variance_adjustment(cpi, x, bsize, &this_rd, ref_frame,
#if CONFIG_OBMC
                           is_inter_block(mbmi),
#endif  // CONFIG_OBMC
                           x->source_variance);

    if (ref_frame == INTRA_FRAME) {
    // Keep record of best intra rd
      if (this_rd < best_intra_rd) {
        best_intra_rd = this_rd;
        best_intra_mode = mbmi->mode;
      }
    }

    if (!disable_skip && ref_frame == INTRA_FRAME) {
      for (i = 0; i < REFERENCE_MODES; ++i)
        best_pred_rd[i] = VPXMIN(best_pred_rd[i], this_rd);
    }

    // Did this mode help.. i.e. is it the new best mode
    if (this_rd < best_rd || x->skip) {
      int max_plane = MAX_MB_PLANE;
      if (!mode_excluded) {
        // Note index of best mode so far
        best_mode_index = mode_index;

        if (ref_frame == INTRA_FRAME) {
          /* required for left and above block mv */
          mbmi->mv[0].as_int = 0;
          max_plane = 1;
        } else {
          best_pred_sse = x->pred_sse[ref_frame];
        }

        rd_cost->rate = rate2;
#if CONFIG_SUPERTX
        if (x->skip)
          *returnrate_nocoef = rate2;
        else
          *returnrate_nocoef = rate2 - rate_y - rate_uv;
        *returnrate_nocoef -= vp10_cost_bit(vp10_get_skip_prob(cm, xd),
            disable_skip || skippable || this_skip2);
        *returnrate_nocoef -= vp10_cost_bit(vp10_get_intra_inter_prob(cm, xd),
                                            mbmi->ref_frame[0] != INTRA_FRAME);
#if CONFIG_OBMC
        if (is_inter_block(mbmi) && is_obmc_allowed(mbmi))
          *returnrate_nocoef -= cpi->obmc_cost[bsize][mbmi->obmc];
#endif  // CONFIG_OBMC
#endif  // CONFIG_SUPERTX
        rd_cost->dist = distortion2;
        rd_cost->rdcost = this_rd;
        best_rd = this_rd;
        best_mbmode = *mbmi;
        best_skip2 = this_skip2;
        best_mode_skippable = skippable;

        if (!x->select_tx_size)
          swap_block_ptr(x, ctx, 1, 0, 0, max_plane);

#if CONFIG_VAR_TX
        for (i = 0; i < MAX_MB_PLANE; ++i)
          memcpy(ctx->blk_skip[i], x->blk_skip[i],
                 sizeof(uint8_t) * ctx->num_4x4_blk);
#else
        memcpy(ctx->zcoeff_blk, x->zcoeff_blk[mbmi->tx_size],
               sizeof(ctx->zcoeff_blk[0]) * ctx->num_4x4_blk);
#endif

        // TODO(debargha): enhance this test with a better distortion prediction
        // based on qp, activity mask and history
        if ((mode_search_skip_flags & FLAG_EARLY_TERMINATE) &&
            (mode_index > MIN_EARLY_TERM_INDEX)) {
          int qstep = xd->plane[0].dequant[1];
          // TODO(debargha): Enhance this by specializing for each mode_index
          int scale = 4;
#if CONFIG_VP9_HIGHBITDEPTH
          if (xd->cur_buf->flags & YV12_FLAG_HIGHBITDEPTH) {
            qstep >>= (xd->bd - 8);
          }
#endif  // CONFIG_VP9_HIGHBITDEPTH
          if (x->source_variance < UINT_MAX) {
            const int var_adjust = (x->source_variance < 16);
            scale -= var_adjust;
          }
          if (ref_frame > INTRA_FRAME &&
              distortion2 * scale < qstep * qstep) {
            early_term = 1;
          }
        }
      }
    }

    /* keep record of best compound/single-only prediction */
    if (!disable_skip && ref_frame != INTRA_FRAME) {
      int64_t single_rd, hybrid_rd, single_rate, hybrid_rate;

      if (cm->reference_mode == REFERENCE_MODE_SELECT) {
        single_rate = rate2 - compmode_cost;
        hybrid_rate = rate2;
      } else {
        single_rate = rate2;
        hybrid_rate = rate2 + compmode_cost;
      }

      single_rd = RDCOST(x->rdmult, x->rddiv, single_rate, distortion2);
      hybrid_rd = RDCOST(x->rdmult, x->rddiv, hybrid_rate, distortion2);

      if (!comp_pred) {
        if (single_rd < best_pred_rd[SINGLE_REFERENCE])
          best_pred_rd[SINGLE_REFERENCE] = single_rd;
      } else {
        if (single_rd < best_pred_rd[COMPOUND_REFERENCE])
          best_pred_rd[COMPOUND_REFERENCE] = single_rd;
      }
      if (hybrid_rd < best_pred_rd[REFERENCE_MODE_SELECT])
        best_pred_rd[REFERENCE_MODE_SELECT] = hybrid_rd;
    }

    if (early_term)
      break;

    if (x->skip && !comp_pred)
      break;
  }

  // Only try palette mode when the best mode so far is an intra mode.
  if (cm->allow_screen_content_tools && !is_inter_mode(best_mbmode.mode)) {
    PREDICTION_MODE mode_selected;
    int rate2 = 0, rate_y = 0;
    int64_t distortion2 = 0, distortion_y = 0, dummy_rd = best_rd, this_rd;
    int skippable = 0, rate_overhead = 0;
    TX_SIZE best_tx_size, uv_tx;
    PALETTE_MODE_INFO palette_mode_info;
    uint8_t *const best_palette_color_map =
        x->palette_buffer->best_palette_color_map;
    uint8_t *const color_map = xd->plane[0].color_index_map;

    mbmi->mode = DC_PRED;
    mbmi->uv_mode = DC_PRED;
    mbmi->ref_frame[0] = INTRA_FRAME;
    mbmi->ref_frame[1] = NONE;
    memset(x->skip_txfm, SKIP_TXFM_NONE, sizeof(x->skip_txfm));
    palette_mode_info.palette_size[0] = 0;
    rate_overhead =
        rd_pick_palette_intra_sby(cpi, x, bsize, palette_ctx,
                                  intra_mode_cost[DC_PRED],
                                  &palette_mode_info, best_palette_color_map,
                                  &best_tx_size, &mode_selected, &dummy_rd);
    if (palette_mode_info.palette_size[0] == 0)
      goto PALETTE_EXIT;

    pmi->palette_size[0] =
        palette_mode_info.palette_size[0];
    if (palette_mode_info.palette_size[0] > 0) {
      memcpy(pmi->palette_colors, palette_mode_info.palette_colors,
             PALETTE_MAX_SIZE * sizeof(palette_mode_info.palette_colors[0]));
      memcpy(color_map, best_palette_color_map,
             rows * cols * sizeof(best_palette_color_map[0]));
    }
    super_block_yrd(cpi, x, &rate_y, &distortion_y, &skippable,
                    NULL, bsize, best_rd);
    if (rate_y == INT_MAX)
      goto PALETTE_EXIT;
    uv_tx = get_uv_tx_size_impl(mbmi->tx_size, bsize,
                                xd->plane[1].subsampling_x,
                                xd->plane[1].subsampling_y);
    if (rate_uv_intra[uv_tx] == INT_MAX) {
      choose_intra_uv_mode(cpi, x, ctx, bsize, uv_tx,
                           &rate_uv_intra[uv_tx], &rate_uv_tokenonly[uv_tx],
                           &dist_uv[uv_tx], &skip_uv[uv_tx], &mode_uv[uv_tx]);
      pmi_uv[uv_tx] = *pmi;
#if CONFIG_EXT_INTRA
      ext_intra_mode_info_uv[uv_tx] = mbmi->ext_intra_mode_info;
      uv_angle_delta[uv_tx] = mbmi->angle_delta[1];
#endif  // CONFIG_EXT_INTRA
    }
    mbmi->uv_mode = mode_uv[uv_tx];
    pmi->palette_size[1] = pmi_uv[uv_tx].palette_size[1];
    if (pmi->palette_size[1] > 0)
      memcpy(pmi->palette_colors + PALETTE_MAX_SIZE,
             pmi_uv[uv_tx].palette_colors + PALETTE_MAX_SIZE,
             2 * PALETTE_MAX_SIZE * sizeof(pmi->palette_colors[0]));
#if CONFIG_EXT_INTRA
    mbmi->angle_delta[1] = uv_angle_delta[uv_tx];
    mbmi->ext_intra_mode_info.use_ext_intra_mode[1] =
        ext_intra_mode_info_uv[uv_tx].use_ext_intra_mode[1];
    if (ext_intra_mode_info_uv[uv_tx].use_ext_intra_mode[1]) {
      mbmi->ext_intra_mode_info.ext_intra_mode[1] =
          ext_intra_mode_info_uv[uv_tx].ext_intra_mode[1];
    }
#endif  // CONFIG_EXT_INTRA
    skippable = skippable && skip_uv[uv_tx];
    distortion2 = distortion_y + dist_uv[uv_tx];
    rate2 = rate_y + rate_overhead + rate_uv_intra[uv_tx];
    rate2 += ref_costs_single[INTRA_FRAME];

    if (skippable) {
      rate2 -= (rate_y + rate_uv_tokenonly[uv_tx]);
      rate2 += vp10_cost_bit(vp10_get_skip_prob(cm, xd), 1);
    } else {
      rate2 += vp10_cost_bit(vp10_get_skip_prob(cm, xd), 0);
    }
    this_rd = RDCOST(x->rdmult, x->rddiv, rate2, distortion2);
    if (this_rd < best_rd) {
      int max_plane = MAX_MB_PLANE;
      best_mode_index = 3;
      mbmi->mv[0].as_int = 0;
      max_plane = 1;
      rd_cost->rate = rate2;
      rd_cost->dist = distortion2;
      rd_cost->rdcost = this_rd;
      best_rd = this_rd;
      best_mbmode = *mbmi;
      best_skip2 = 0;
      best_mode_skippable = skippable;
      if (!x->select_tx_size)
        swap_block_ptr(x, ctx, 1, 0, 0, max_plane);
      memcpy(ctx->zcoeff_blk, x->zcoeff_blk[mbmi->tx_size],
             sizeof(ctx->zcoeff_blk[0]) * ctx->num_4x4_blk);
    }
  }
  PALETTE_EXIT:

  // The inter modes' rate costs are not calculated precisely in some cases.
  // Therefore, sometimes, NEWMV is chosen instead of NEARESTMV, NEARMV, and
  // ZEROMV. Here, checks are added for those cases, and the mode decisions
  // are corrected.
  if (best_mbmode.mode == NEWMV
#if CONFIG_EXT_INTER
      || best_mbmode.mode == NEWFROMNEARMV
      || best_mbmode.mode == NEW_NEWMV
#endif  // CONFIG_EXT_INTER
  ) {
    const MV_REFERENCE_FRAME refs[2] = {best_mbmode.ref_frame[0],
        best_mbmode.ref_frame[1]};
    int comp_pred_mode = refs[1] > INTRA_FRAME;
#if CONFIG_REF_MV
    const uint8_t rf_type = vp10_ref_frame_type(best_mbmode.ref_frame);
    if (!comp_pred_mode) {
      if (best_mbmode.ref_mv_idx > 0 && refs[1] == NONE) {
        int idx = best_mbmode.ref_mv_idx + 1;
        int_mv cur_mv = mbmi_ext->ref_mv_stack[refs[0]][idx].this_mv;
        lower_mv_precision(&cur_mv.as_mv, cm->allow_high_precision_mv);
        frame_mv[NEARMV][refs[0]] = cur_mv;
      }

      if (frame_mv[NEARESTMV][refs[0]].as_int == best_mbmode.mv[0].as_int)
        best_mbmode.mode = NEARESTMV;
      else if (frame_mv[NEARMV][refs[0]].as_int == best_mbmode.mv[0].as_int)
        best_mbmode.mode = NEARMV;
      else if (best_mbmode.mv[0].as_int == 0)
        best_mbmode.mode = ZEROMV;
    } else {
      int i;
      const int allow_hp = cm->allow_high_precision_mv;
      int_mv nearestmv[2] = { frame_mv[NEARESTMV][refs[0]],
                              frame_mv[NEARESTMV][refs[1]] };

      int_mv nearmv[2] = { frame_mv[NEARMV][refs[0]],
                           frame_mv[NEARMV][refs[1]] };

      if (mbmi_ext->ref_mv_count[rf_type] >= 1) {
        nearestmv[0] = mbmi_ext->ref_mv_stack[rf_type][0].this_mv;
        nearestmv[1] = mbmi_ext->ref_mv_stack[rf_type][0].comp_mv;
      }

      if (mbmi_ext->ref_mv_count[rf_type] > 1) {
        int ref_mv_idx = best_mbmode.ref_mv_idx + 1;
        nearmv[0] = mbmi_ext->ref_mv_stack[rf_type][ref_mv_idx].this_mv;
        nearmv[1] = mbmi_ext->ref_mv_stack[rf_type][ref_mv_idx].comp_mv;
      }

      for (i = 0; i < MAX_MV_REF_CANDIDATES; ++i) {
        lower_mv_precision(&nearestmv[i].as_mv, allow_hp);
        lower_mv_precision(&nearmv[i].as_mv, allow_hp);
      }

      if (nearestmv[0].as_int == best_mbmode.mv[0].as_int &&
          nearestmv[1].as_int == best_mbmode.mv[1].as_int)
#if CONFIG_EXT_INTER
        best_mbmode.mode = NEAREST_NEARESTMV;
      else if (nearestmv[0].as_int == best_mbmode.mv[0].as_int &&
               nearmv[1].as_int == best_mbmode.mv[1].as_int)
        best_mbmode.mode = NEAREST_NEARMV;
      else if (nearmv[0].as_int == best_mbmode.mv[0].as_int &&
               nearestmv[1].as_int == best_mbmode.mv[1].as_int)
        best_mbmode.mode = NEAR_NEARESTMV;
      else if (best_mbmode.mv[0].as_int == 0 && best_mbmode.mv[1].as_int == 0)
        best_mbmode.mode = ZERO_ZEROMV;
#else
        best_mbmode.mode = NEARESTMV;
      else if (nearmv[0].as_int == best_mbmode.mv[0].as_int &&
          nearmv[1].as_int == best_mbmode.mv[1].as_int)
        best_mbmode.mode = NEARMV;
      else if (best_mbmode.mv[0].as_int == 0 && best_mbmode.mv[1].as_int == 0)
        best_mbmode.mode = ZEROMV;
#endif  // CONFIG_EXT_INTER
    }
#else
#if CONFIG_EXT_INTER
    if (!comp_pred_mode) {
#endif  // CONFIG_EXT_INTER
    if (frame_mv[NEARESTMV][refs[0]].as_int == best_mbmode.mv[0].as_int &&
        ((comp_pred_mode && frame_mv[NEARESTMV][refs[1]].as_int ==
            best_mbmode.mv[1].as_int) || !comp_pred_mode))
      best_mbmode.mode = NEARESTMV;
    else if (frame_mv[NEARMV][refs[0]].as_int == best_mbmode.mv[0].as_int &&
        ((comp_pred_mode && frame_mv[NEARMV][refs[1]].as_int ==
            best_mbmode.mv[1].as_int) || !comp_pred_mode))
      best_mbmode.mode = NEARMV;
    else if (best_mbmode.mv[0].as_int == 0 &&
        ((comp_pred_mode && best_mbmode.mv[1].as_int == 0) || !comp_pred_mode))
      best_mbmode.mode = ZEROMV;
#if CONFIG_EXT_INTER
    } else {
      const MV_REFERENCE_FRAME refs[2] = {best_mbmode.ref_frame[0],
          best_mbmode.ref_frame[1]};

      if (frame_mv[NEAREST_NEARESTMV][refs[0]].as_int ==
            best_mbmode.mv[0].as_int &&
          frame_mv[NEAREST_NEARESTMV][refs[1]].as_int ==
            best_mbmode.mv[1].as_int)
        best_mbmode.mode = NEAREST_NEARESTMV;
      else if (frame_mv[NEAREST_NEARMV][refs[0]].as_int ==
                 best_mbmode.mv[0].as_int &&
               frame_mv[NEAREST_NEARMV][refs[1]].as_int ==
                 best_mbmode.mv[1].as_int)
        best_mbmode.mode = NEAREST_NEARMV;
      else if (frame_mv[NEAR_NEARESTMV][refs[0]].as_int ==
                 best_mbmode.mv[0].as_int &&
               frame_mv[NEAR_NEARESTMV][refs[1]].as_int ==
                 best_mbmode.mv[1].as_int)
        best_mbmode.mode = NEAR_NEARESTMV;
      else if (best_mbmode.mv[0].as_int == 0 && best_mbmode.mv[1].as_int == 0)
        best_mbmode.mode = ZERO_ZEROMV;
    }
#endif  // CONFIG_EXT_INTER
#endif
  }

#if CONFIG_REF_MV
  if (best_mbmode.ref_frame[0] > INTRA_FRAME &&
      best_mbmode.mv[0].as_int == 0 &&
#if CONFIG_EXT_INTER
      (best_mbmode.ref_frame[1] <= INTRA_FRAME)
#else
      (best_mbmode.ref_frame[1] == NONE || best_mbmode.mv[1].as_int == 0)
#endif  // CONFIG_EXT_INTER
     ) {
    int16_t mode_ctx = mbmi_ext->mode_context[best_mbmode.ref_frame[0]];
#if !CONFIG_EXT_INTER
    if (best_mbmode.ref_frame[1] > NONE)
      mode_ctx &= (mbmi_ext->mode_context[best_mbmode.ref_frame[1]] | 0x00ff);
#endif  // !CONFIG_EXT_INTER

    if (mode_ctx & (1 << ALL_ZERO_FLAG_OFFSET))
      best_mbmode.mode = ZEROMV;
  }
#endif

  if (best_mode_index < 0 || best_rd >= best_rd_so_far) {
    rd_cost->rate = INT_MAX;
    rd_cost->rdcost = INT64_MAX;
    return;
  }

  // If we used an estimate for the uv intra rd in the loop above...
  if (sf->use_uv_intra_rd_estimate) {
    // Do Intra UV best rd mode selection if best mode choice above was intra.
    if (best_mbmode.ref_frame[0] == INTRA_FRAME) {
      TX_SIZE uv_tx_size;
      *mbmi = best_mbmode;
      uv_tx_size = get_uv_tx_size(mbmi, &xd->plane[1]);
      rd_pick_intra_sbuv_mode(cpi, x, ctx, &rate_uv_intra[uv_tx_size],
                              &rate_uv_tokenonly[uv_tx_size],
                              &dist_uv[uv_tx_size],
                              &skip_uv[uv_tx_size],
                              bsize < BLOCK_8X8 ? BLOCK_8X8 : bsize,
                              uv_tx_size);
    }
  }

  assert((cm->interp_filter == SWITCHABLE) ||
         (cm->interp_filter == best_mbmode.interp_filter) ||
         !is_inter_block(&best_mbmode));

  if (!cpi->rc.is_src_frame_alt_ref)
    vp10_update_rd_thresh_fact(tile_data->thresh_freq_fact,
                              sf->adaptive_rd_thresh, bsize, best_mode_index);

  // macroblock modes
  *mbmi = best_mbmode;
  x->skip |= best_skip2;

#if CONFIG_REF_MV
  for (i = 0; i < 1 + has_second_ref(mbmi); ++i) {
    if (mbmi->mode != NEWMV)
      mbmi->pred_mv[i].as_int = mbmi->mv[i].as_int;
    else
      mbmi->pred_mv[i].as_int = mbmi_ext->ref_mvs[mbmi->ref_frame[i]][0].as_int;
  }
#endif

  for (i = 0; i < REFERENCE_MODES; ++i) {
    if (best_pred_rd[i] == INT64_MAX)
      best_pred_diff[i] = INT_MIN;
    else
      best_pred_diff[i] = best_rd - best_pred_rd[i];
  }

  x->skip |= best_mode_skippable;

  if (!x->skip && !x->select_tx_size) {
    int has_high_freq_coeff = 0;
    int plane;
    int max_plane = is_inter_block(&xd->mi[0]->mbmi)
                        ? MAX_MB_PLANE : 1;
    for (plane = 0; plane < max_plane; ++plane) {
      x->plane[plane].eobs = ctx->eobs_pbuf[plane][1];
      has_high_freq_coeff |= vp10_has_high_freq_in_plane(x, bsize, plane);
    }

    for (plane = max_plane; plane < MAX_MB_PLANE; ++plane) {
      x->plane[plane].eobs = ctx->eobs_pbuf[plane][2];
      has_high_freq_coeff |= vp10_has_high_freq_in_plane(x, bsize, plane);
    }

    best_mode_skippable |= !has_high_freq_coeff;
  }

  assert(best_mode_index >= 0);

  store_coding_context(x, ctx, best_mode_index, best_pred_diff,
                       best_mode_skippable);

  if (cm->allow_screen_content_tools && pmi->palette_size[1] > 0) {
    restore_uv_color_map(cpi, x);
  }
}

void vp10_rd_pick_inter_mode_sb_seg_skip(VP10_COMP *cpi,
                                        TileDataEnc *tile_data,
                                        MACROBLOCK *x,
                                        RD_COST *rd_cost,
                                        BLOCK_SIZE bsize,
                                        PICK_MODE_CONTEXT *ctx,
                                        int64_t best_rd_so_far) {
  VP10_COMMON *const cm = &cpi->common;
  MACROBLOCKD *const xd = &x->e_mbd;
  MB_MODE_INFO *const mbmi = &xd->mi[0]->mbmi;
  unsigned char segment_id = mbmi->segment_id;
  const int comp_pred = 0;
  int i;
  int64_t best_pred_diff[REFERENCE_MODES];
  unsigned int ref_costs_single[MAX_REF_FRAMES], ref_costs_comp[MAX_REF_FRAMES];
  vpx_prob comp_mode_p;
  INTERP_FILTER best_filter = SWITCHABLE;
  int64_t this_rd = INT64_MAX;
  int rate2 = 0;
  const int64_t distortion2 = 0;

  estimate_ref_frame_costs(cm, xd, segment_id, ref_costs_single, ref_costs_comp,
                           &comp_mode_p);

  for (i = 0; i < MAX_REF_FRAMES; ++i)
    x->pred_sse[i] = INT_MAX;
  for (i = LAST_FRAME; i < MAX_REF_FRAMES; ++i)
    x->pred_mv_sad[i] = INT_MAX;

  rd_cost->rate = INT_MAX;

  assert(segfeature_active(&cm->seg, segment_id, SEG_LVL_SKIP));

  mbmi->palette_mode_info.palette_size[0] = 0;
  mbmi->palette_mode_info.palette_size[1] = 0;
#if CONFIG_EXT_INTRA
  mbmi->ext_intra_mode_info.use_ext_intra_mode[0] = 0;
  mbmi->ext_intra_mode_info.use_ext_intra_mode[1] = 0;
#endif  // CONFIG_EXT_INTRA
  mbmi->mode = ZEROMV;
  mbmi->uv_mode = DC_PRED;
  mbmi->ref_frame[0] = LAST_FRAME;
  mbmi->ref_frame[1] = NONE;
  mbmi->mv[0].as_int = 0;
  x->skip = 1;

  if (cm->interp_filter != BILINEAR) {
    best_filter = EIGHTTAP_REGULAR;
    if (cm->interp_filter == SWITCHABLE &&
#if CONFIG_EXT_INTERP
        vp10_is_interp_needed(xd) &&
#endif  // CONFIG_EXT_INTERP
        x->source_variance >= cpi->sf.disable_filter_search_var_thresh) {
      int rs;
      int best_rs = INT_MAX;
      for (i = 0; i < SWITCHABLE_FILTERS; ++i) {
        mbmi->interp_filter = i;
        rs = vp10_get_switchable_rate(cpi, xd);
        if (rs < best_rs) {
          best_rs = rs;
          best_filter = mbmi->interp_filter;
        }
      }
    }
  }
  // Set the appropriate filter
  if (cm->interp_filter == SWITCHABLE) {
    mbmi->interp_filter = best_filter;
    rate2 += vp10_get_switchable_rate(cpi, xd);
  } else {
    mbmi->interp_filter = cm->interp_filter;
  }

  if (cm->reference_mode == REFERENCE_MODE_SELECT)
    rate2 += vp10_cost_bit(comp_mode_p, comp_pred);

  // Estimate the reference frame signaling cost and add it
  // to the rolling cost variable.
  rate2 += ref_costs_single[LAST_FRAME];
  this_rd = RDCOST(x->rdmult, x->rddiv, rate2, distortion2);

  rd_cost->rate = rate2;
  rd_cost->dist = distortion2;
  rd_cost->rdcost = this_rd;

  if (this_rd >= best_rd_so_far) {
    rd_cost->rate = INT_MAX;
    rd_cost->rdcost = INT64_MAX;
    return;
  }

  assert((cm->interp_filter == SWITCHABLE) ||
         (cm->interp_filter == mbmi->interp_filter));

  vp10_update_rd_thresh_fact(tile_data->thresh_freq_fact,
                            cpi->sf.adaptive_rd_thresh, bsize, THR_ZEROMV);

  vp10_zero(best_pred_diff);

  if (!x->select_tx_size)
    swap_block_ptr(x, ctx, 1, 0, 0, MAX_MB_PLANE);
  store_coding_context(x, ctx, THR_ZEROMV,
                       best_pred_diff, 0);
}

void vp10_rd_pick_inter_mode_sub8x8(struct VP10_COMP *cpi,
                                    TileDataEnc *tile_data,
                                    struct macroblock *x,
                                    int mi_row, int mi_col,
                                    struct RD_COST *rd_cost,
#if CONFIG_SUPERTX
                                    int *returnrate_nocoef,
#endif  // CONFIG_SUPERTX
                                    BLOCK_SIZE bsize,
                                    PICK_MODE_CONTEXT *ctx,
                                    int64_t best_rd_so_far) {
  VP10_COMMON *const cm = &cpi->common;
  RD_OPT *const rd_opt = &cpi->rd;
  SPEED_FEATURES *const sf = &cpi->sf;
  MACROBLOCKD *const xd = &x->e_mbd;
  MB_MODE_INFO *const mbmi = &xd->mi[0]->mbmi;
  const struct segmentation *const seg = &cm->seg;
  MV_REFERENCE_FRAME ref_frame, second_ref_frame;
  unsigned char segment_id = mbmi->segment_id;
  int comp_pred, i;
  int_mv frame_mv[MB_MODE_COUNT][MAX_REF_FRAMES];
  struct buf_2d yv12_mb[MAX_REF_FRAMES][MAX_MB_PLANE];
  static const int flag_list[REFS_PER_FRAME + 1] = {
    0,
    VP9_LAST_FLAG,
#if CONFIG_EXT_REFS
    VP9_LAST2_FLAG,
    VP9_LAST3_FLAG,
    VP9_LAST4_FLAG,
#endif  // CONFIG_EXT_REFS
    VP9_GOLD_FLAG,
    VP9_ALT_FLAG
  };
  int64_t best_rd = best_rd_so_far;
  int64_t best_yrd = best_rd_so_far;  // FIXME(rbultje) more precise
  int64_t best_pred_diff[REFERENCE_MODES];
  int64_t best_pred_rd[REFERENCE_MODES];
  MB_MODE_INFO best_mbmode;
  int ref_index, best_ref_index = 0;
  unsigned int ref_costs_single[MAX_REF_FRAMES], ref_costs_comp[MAX_REF_FRAMES];
  vpx_prob comp_mode_p;
  INTERP_FILTER tmp_best_filter = SWITCHABLE;
  int rate_uv_intra, rate_uv_tokenonly;
  int64_t dist_uv;
  int skip_uv;
  PREDICTION_MODE mode_uv = DC_PRED;
  const int intra_cost_penalty = vp10_get_intra_cost_penalty(
    cm->base_qindex, cm->y_dc_delta_q, cm->bit_depth);
#if CONFIG_EXT_INTER
  int_mv seg_mvs[4][2][MAX_REF_FRAMES];
#else
  int_mv seg_mvs[4][MAX_REF_FRAMES];
#endif  // CONFIG_EXT_INTER
  b_mode_info best_bmodes[4];
  int best_skip2 = 0;
  int ref_frame_skip_mask[2] = { 0 };
  int internal_active_edge =
    vp10_active_edge_sb(cpi, mi_row, mi_col) && vp10_internal_image_edge(cpi);

#if CONFIG_SUPERTX
  best_rd_so_far = INT64_MAX;
  best_rd = best_rd_so_far;
  best_yrd = best_rd_so_far;
#endif  // CONFIG_SUPERTX
  memset(x->zcoeff_blk[TX_4X4], 0, 4);
  vp10_zero(best_mbmode);

#if CONFIG_EXT_INTRA
  mbmi->ext_intra_mode_info.use_ext_intra_mode[0] = 0;
  mbmi->ext_intra_mode_info.use_ext_intra_mode[1] = 0;
#endif  // CONFIG_EXT_INTRA
#if CONFIG_OBMC
  mbmi->obmc = 0;
#endif  // CONFIG_OBMC
#if CONFIG_EXT_INTER
  mbmi->use_wedge_interinter = 0;
  mbmi->use_wedge_interintra = 0;
#endif  // CONFIG_EXT_INTER

  for (i = 0; i < 4; i++) {
    int j;
#if CONFIG_EXT_INTER
    int k;

    for (k = 0; k < 2; k++)
      for (j = 0; j < MAX_REF_FRAMES; j++)
        seg_mvs[i][k][j].as_int = INVALID_MV;
#else
    for (j = 0; j < MAX_REF_FRAMES; j++)
      seg_mvs[i][j].as_int = INVALID_MV;
#endif  // CONFIG_EXT_INTER
  }

  estimate_ref_frame_costs(cm, xd, segment_id, ref_costs_single, ref_costs_comp,
                           &comp_mode_p);

  for (i = 0; i < REFERENCE_MODES; ++i)
    best_pred_rd[i] = INT64_MAX;
  rate_uv_intra = INT_MAX;

  rd_cost->rate = INT_MAX;
#if CONFIG_SUPERTX
  *returnrate_nocoef = INT_MAX;
#endif

  for (ref_frame = LAST_FRAME; ref_frame <= ALTREF_FRAME; ref_frame++) {
    x->mbmi_ext->mode_context[ref_frame] = 0;
#if CONFIG_REF_MV && CONFIG_EXT_INTER
    x->mbmi_ext->compound_mode_context[ref_frame] = 0;
#endif  // CONFIG_REF_MV && CONFIG_EXT_INTER
    if (cpi->ref_frame_flags & flag_list[ref_frame]) {
      setup_buffer_inter(cpi, x, ref_frame, bsize, mi_row, mi_col,
                         frame_mv[NEARESTMV], frame_mv[NEARMV],
                         yv12_mb);
    } else {
      ref_frame_skip_mask[0] |= (1 << ref_frame);
      ref_frame_skip_mask[1] |= SECOND_REF_FRAME_MASK;
    }
    frame_mv[NEWMV][ref_frame].as_int = INVALID_MV;
#if CONFIG_EXT_INTER
    frame_mv[NEWFROMNEARMV][ref_frame].as_int = INVALID_MV;
#endif  // CONFIG_EXT_INTER
    frame_mv[ZEROMV][ref_frame].as_int = 0;
  }

  mbmi->palette_mode_info.palette_size[0] = 0;
  mbmi->palette_mode_info.palette_size[1] = 0;

  for (ref_index = 0; ref_index < MAX_REFS; ++ref_index) {
    int mode_excluded = 0;
    int64_t this_rd = INT64_MAX;
    int disable_skip = 0;
    int compmode_cost = 0;
    int rate2 = 0, rate_y = 0, rate_uv = 0;
    int64_t distortion2 = 0, distortion_y = 0, distortion_uv = 0;
    int skippable = 0;
    int i;
    int this_skip2 = 0;
    int64_t total_sse = INT_MAX;
    int early_term = 0;

    ref_frame = vp10_ref_order[ref_index].ref_frame[0];
    second_ref_frame = vp10_ref_order[ref_index].ref_frame[1];

    // Look at the reference frame of the best mode so far and set the
    // skip mask to look at a subset of the remaining modes.
    if (ref_index > 2 && sf->mode_skip_start < MAX_MODES) {
      if (ref_index == 3) {
        switch (best_mbmode.ref_frame[0]) {
          case INTRA_FRAME:
            break;
          case LAST_FRAME:
            ref_frame_skip_mask[0] |= (1 << GOLDEN_FRAME) |
#if CONFIG_EXT_REFS
                                      (1 << LAST2_FRAME) |
                                      (1 << LAST3_FRAME) |
                                      (1 << LAST4_FRAME) |
#endif  // CONFIG_EXT_REFS
                                      (1 << ALTREF_FRAME);
            ref_frame_skip_mask[1] |= SECOND_REF_FRAME_MASK;
            break;
#if CONFIG_EXT_REFS
          case LAST2_FRAME:
            ref_frame_skip_mask[0] |= (1 << LAST_FRAME) |
                                      (1 << LAST3_FRAME) |
                                      (1 << LAST4_FRAME) |
                                      (1 << GOLDEN_FRAME) |
                                      (1 << ALTREF_FRAME);
            ref_frame_skip_mask[1] |= SECOND_REF_FRAME_MASK;
            break;
          case LAST3_FRAME:
            ref_frame_skip_mask[0] |= (1 << LAST_FRAME) |
                                      (1 << LAST2_FRAME) |
                                      (1 << LAST4_FRAME) |
                                      (1 << GOLDEN_FRAME) |
                                      (1 << ALTREF_FRAME);
            ref_frame_skip_mask[1] |= SECOND_REF_FRAME_MASK;
            break;
          case LAST4_FRAME:
            ref_frame_skip_mask[0] |= (1 << LAST_FRAME) |
                                      (1 << LAST2_FRAME) |
                                      (1 << LAST3_FRAME) |
                                      (1 << GOLDEN_FRAME) |
                                      (1 << ALTREF_FRAME);
            ref_frame_skip_mask[1] |= SECOND_REF_FRAME_MASK;
            break;
#endif  // CONFIG_EXT_REFS
          case GOLDEN_FRAME:
            ref_frame_skip_mask[0] |= (1 << LAST_FRAME) |
#if CONFIG_EXT_REFS
                                      (1 << LAST2_FRAME) |
                                      (1 << LAST3_FRAME) |
                                      (1 << LAST4_FRAME) |
#endif  // CONFIG_EXT_REFS
                                      (1 << ALTREF_FRAME);
            ref_frame_skip_mask[1] |= SECOND_REF_FRAME_MASK;
            break;
          case ALTREF_FRAME:
            ref_frame_skip_mask[0] |= (1 << GOLDEN_FRAME) |
#if CONFIG_EXT_REFS
                                      (1 << LAST2_FRAME) |
                                      (1 << LAST3_FRAME) |
                                      (1 << LAST4_FRAME) |
#endif  // CONFIG_EXT_REFS
                                      (1 << LAST_FRAME);
            break;
          case NONE:
          case MAX_REF_FRAMES:
            assert(0 && "Invalid Reference frame");
            break;
        }
      }
    }

    if ((ref_frame_skip_mask[0] & (1 << ref_frame)) &&
        (ref_frame_skip_mask[1] & (1 << VPXMAX(0, second_ref_frame))))
      continue;

    // Test best rd so far against threshold for trying this mode.
    if (!internal_active_edge &&
        rd_less_than_thresh(best_rd,
                            rd_opt->threshes[segment_id][bsize][ref_index],
                            tile_data->thresh_freq_fact[bsize][ref_index]))
      continue;

    comp_pred = second_ref_frame > INTRA_FRAME;
    if (comp_pred) {
      if (!cpi->allow_comp_inter_inter)
        continue;
      if (!(cpi->ref_frame_flags & flag_list[second_ref_frame]))
        continue;
      // Do not allow compound prediction if the segment level reference frame
      // feature is in use as in this case there can only be one reference.
      if (segfeature_active(seg, segment_id, SEG_LVL_REF_FRAME))
        continue;

      if ((sf->mode_search_skip_flags & FLAG_SKIP_COMP_BESTINTRA) &&
          best_mbmode.ref_frame[0] == INTRA_FRAME)
        continue;
    }

    // TODO(jingning, jkoleszar): scaling reference frame not supported for
    // sub8x8 blocks.
    if (ref_frame > INTRA_FRAME &&
        vp10_is_scaled(&cm->frame_refs[ref_frame - 1].sf))
      continue;

    if (second_ref_frame > INTRA_FRAME &&
        vp10_is_scaled(&cm->frame_refs[second_ref_frame - 1].sf))
      continue;

    if (comp_pred)
      mode_excluded = cm->reference_mode == SINGLE_REFERENCE;
    else if (ref_frame != INTRA_FRAME)
      mode_excluded = cm->reference_mode == COMPOUND_REFERENCE;

    // If the segment reference frame feature is enabled....
    // then do nothing if the current ref frame is not allowed..
    if (segfeature_active(seg, segment_id, SEG_LVL_REF_FRAME) &&
        get_segdata(seg, segment_id, SEG_LVL_REF_FRAME) != (int)ref_frame) {
      continue;
    // Disable this drop out case if the ref frame
    // segment level feature is enabled for this segment. This is to
    // prevent the possibility that we end up unable to pick any mode.
    } else if (!segfeature_active(seg, segment_id, SEG_LVL_REF_FRAME)) {
      // Only consider ZEROMV/ALTREF_FRAME for alt ref frame,
      // unless ARNR filtering is enabled in which case we want
      // an unfiltered alternative. We allow near/nearest as well
      // because they may result in zero-zero MVs but be cheaper.
      if (cpi->rc.is_src_frame_alt_ref && (cpi->oxcf.arnr_max_frames == 0))
        continue;
    }

    mbmi->tx_size = TX_4X4;
    mbmi->uv_mode = DC_PRED;
    mbmi->ref_frame[0] = ref_frame;
    mbmi->ref_frame[1] = second_ref_frame;
    // Evaluate all sub-pel filters irrespective of whether we can use
    // them for this frame.
    mbmi->interp_filter = cm->interp_filter == SWITCHABLE ? EIGHTTAP_REGULAR
                                                          : cm->interp_filter;
    x->skip = 0;
    set_ref_ptrs(cm, xd, ref_frame, second_ref_frame);

    // Select prediction reference frames.
    for (i = 0; i < MAX_MB_PLANE; i++) {
      xd->plane[i].pre[0] = yv12_mb[ref_frame][i];
      if (comp_pred)
        xd->plane[i].pre[1] = yv12_mb[second_ref_frame][i];
    }

#if CONFIG_VAR_TX
    mbmi->inter_tx_size[0] = mbmi->tx_size;
#endif

    if (ref_frame == INTRA_FRAME) {
      int rate;
      if (rd_pick_intra_sub_8x8_y_mode(cpi, x, &rate, &rate_y,
                                       &distortion_y, best_rd) >= best_rd)
        continue;
      rate2 += rate;
      rate2 += intra_cost_penalty;
      distortion2 += distortion_y;

      if (rate_uv_intra == INT_MAX) {
        choose_intra_uv_mode(cpi, x, ctx, bsize, TX_4X4,
                             &rate_uv_intra,
                             &rate_uv_tokenonly,
                             &dist_uv, &skip_uv,
                             &mode_uv);
      }
      rate2 += rate_uv_intra;
      rate_uv = rate_uv_tokenonly;
      distortion2 += dist_uv;
      distortion_uv = dist_uv;
      mbmi->uv_mode = mode_uv;
    } else {
      int rate;
      int64_t distortion;
      int64_t this_rd_thresh;
      int64_t tmp_rd, tmp_best_rd = INT64_MAX, tmp_best_rdu = INT64_MAX;
      int tmp_best_rate = INT_MAX, tmp_best_ratey = INT_MAX;
      int64_t tmp_best_distortion = INT_MAX, tmp_best_sse, uv_sse;
      int tmp_best_skippable = 0;
      int switchable_filter_index;
      int_mv *second_ref = comp_pred ?
                             &x->mbmi_ext->ref_mvs[second_ref_frame][0] : NULL;
      b_mode_info tmp_best_bmodes[16];
      MB_MODE_INFO tmp_best_mbmode;
      BEST_SEG_INFO bsi[SWITCHABLE_FILTERS];
      int pred_exists = 0;
      int uv_skippable;
#if CONFIG_EXT_INTER
      int_mv compound_seg_newmvs[4][2];
      int i;

      for (i = 0; i < 4; i++) {
        compound_seg_newmvs[i][0].as_int = INVALID_MV;
        compound_seg_newmvs[i][1].as_int = INVALID_MV;
      }
#endif  // CONFIG_EXT_INTER

      this_rd_thresh = (ref_frame == LAST_FRAME) ?
          rd_opt->threshes[segment_id][bsize][THR_LAST] :
          rd_opt->threshes[segment_id][bsize][THR_ALTR];
#if CONFIG_EXT_REFS
      this_rd_thresh = (ref_frame == LAST2_FRAME) ?
          rd_opt->threshes[segment_id][bsize][THR_LAST2] : this_rd_thresh;
      this_rd_thresh = (ref_frame == LAST3_FRAME) ?
          rd_opt->threshes[segment_id][bsize][THR_LAST3] : this_rd_thresh;
      this_rd_thresh = (ref_frame == LAST4_FRAME) ?
          rd_opt->threshes[segment_id][bsize][THR_LAST4] : this_rd_thresh;
#endif  // CONFIG_EXT_REFS
      this_rd_thresh = (ref_frame == GOLDEN_FRAME) ?
          rd_opt->threshes[segment_id][bsize][THR_GOLD] : this_rd_thresh;

      // TODO(any): Add search of the tx_type to improve rd performance at the
      // expense of speed.
      mbmi->tx_type = DCT_DCT;

      if (cm->interp_filter != BILINEAR) {
        tmp_best_filter = EIGHTTAP_REGULAR;
        if (x->source_variance < sf->disable_filter_search_var_thresh) {
          tmp_best_filter = EIGHTTAP_REGULAR;
        } else if (sf->adaptive_pred_interp_filter == 1 &&
                   ctx->pred_interp_filter < SWITCHABLE) {
          tmp_best_filter = ctx->pred_interp_filter;
        } else if (sf->adaptive_pred_interp_filter == 2) {
          tmp_best_filter = ctx->pred_interp_filter < SWITCHABLE ?
                              ctx->pred_interp_filter : 0;
        } else {
          for (switchable_filter_index = 0;
               switchable_filter_index < SWITCHABLE_FILTERS;
               ++switchable_filter_index) {
            int newbest, rs;
            int64_t rs_rd;
            MB_MODE_INFO_EXT *mbmi_ext = x->mbmi_ext;
            mbmi->interp_filter = switchable_filter_index;
            tmp_rd = rd_pick_best_sub8x8_mode(cpi, x,
                                              &mbmi_ext->ref_mvs[ref_frame][0],
                                              second_ref, best_yrd, &rate,
                                              &rate_y, &distortion,
                                              &skippable, &total_sse,
                                              (int) this_rd_thresh, seg_mvs,
#if CONFIG_EXT_INTER
                                              compound_seg_newmvs,
#endif  // CONFIG_EXT_INTER
                                              bsi, switchable_filter_index,
                                              mi_row, mi_col);
#if CONFIG_EXT_INTERP
            if (!vp10_is_interp_needed(xd) && cm->interp_filter == SWITCHABLE &&
                mbmi->interp_filter != EIGHTTAP_REGULAR)  // invalid config
              continue;
#endif  // CONFIG_EXT_INTERP
            if (tmp_rd == INT64_MAX)
              continue;
            rs = vp10_get_switchable_rate(cpi, xd);
            rs_rd = RDCOST(x->rdmult, x->rddiv, rs, 0);
            if (cm->interp_filter == SWITCHABLE)
              tmp_rd += rs_rd;

            newbest = (tmp_rd < tmp_best_rd);
            if (newbest) {
              tmp_best_filter = mbmi->interp_filter;
              tmp_best_rd = tmp_rd;
            }
            if ((newbest && cm->interp_filter == SWITCHABLE) ||
                (mbmi->interp_filter == cm->interp_filter &&
                 cm->interp_filter != SWITCHABLE)) {
              tmp_best_rdu = tmp_rd;
              tmp_best_rate = rate;
              tmp_best_ratey = rate_y;
              tmp_best_distortion = distortion;
              tmp_best_sse = total_sse;
              tmp_best_skippable = skippable;
              tmp_best_mbmode = *mbmi;
              for (i = 0; i < 4; i++) {
                tmp_best_bmodes[i] = xd->mi[0]->bmi[i];
                x->zcoeff_blk[TX_4X4][i] = !x->plane[0].eobs[i];
              }
              pred_exists = 1;
              if (switchable_filter_index == 0 &&
                  sf->use_rd_breakout &&
                  best_rd < INT64_MAX) {
                if (tmp_best_rdu / 2 > best_rd) {
                  // skip searching the other filters if the first is
                  // already substantially larger than the best so far
                  tmp_best_filter = mbmi->interp_filter;
                  tmp_best_rdu = INT64_MAX;
                  break;
                }
              }
            }
          }  // switchable_filter_index loop
        }
      }

      if (tmp_best_rdu == INT64_MAX && pred_exists)
        continue;

      mbmi->interp_filter = (cm->interp_filter == SWITCHABLE ?
                             tmp_best_filter : cm->interp_filter);

      if (!pred_exists) {
        // Handles the special case when a filter that is not in the
        // switchable list (bilinear) is indicated at the frame level
        tmp_rd = rd_pick_best_sub8x8_mode(cpi, x,
                                          &x->mbmi_ext->ref_mvs[ref_frame][0],
                                          second_ref, best_yrd, &rate, &rate_y,
                                          &distortion, &skippable, &total_sse,
                                          (int) this_rd_thresh, seg_mvs,
#if CONFIG_EXT_INTER
                                          compound_seg_newmvs,
#endif  // CONFIG_EXT_INTER
                                          bsi, 0,
                                          mi_row, mi_col);
#if CONFIG_EXT_INTERP
        if (!vp10_is_interp_needed(xd) && cm->interp_filter == SWITCHABLE &&
            mbmi->interp_filter != EIGHTTAP_REGULAR) {
          mbmi->interp_filter = EIGHTTAP_REGULAR;
        }
#endif  // CONFIG_EXT_INTERP
        if (tmp_rd == INT64_MAX)
          continue;
      } else {
        total_sse = tmp_best_sse;
        rate = tmp_best_rate;
        rate_y = tmp_best_ratey;
        distortion = tmp_best_distortion;
        skippable = tmp_best_skippable;
        *mbmi = tmp_best_mbmode;
        for (i = 0; i < 4; i++)
          xd->mi[0]->bmi[i] = tmp_best_bmodes[i];
      }
      // Add in the cost of the transform type
      if (!xd->lossless[mbmi->segment_id]) {
        int rate_tx_type = 0;
#if CONFIG_EXT_TX
        if (get_ext_tx_types(mbmi->tx_size, bsize, 1) > 1) {
          const int eset = get_ext_tx_set(mbmi->tx_size, bsize, 1);
          rate_tx_type =
              cpi->inter_tx_type_costs[eset][mbmi->tx_size][mbmi->tx_type];
        }
#else
        if (mbmi->tx_size < TX_32X32) {
          rate_tx_type = cpi->inter_tx_type_costs[mbmi->tx_size][mbmi->tx_type];
        }
#endif
        rate += rate_tx_type;
        rate_y += rate_tx_type;
      }

      rate2 += rate;
      distortion2 += distortion;

      if (cm->interp_filter == SWITCHABLE)
        rate2 += vp10_get_switchable_rate(cpi, xd);

      if (!mode_excluded)
        mode_excluded = comp_pred ? cm->reference_mode == SINGLE_REFERENCE
                                  : cm->reference_mode == COMPOUND_REFERENCE;

      compmode_cost = vp10_cost_bit(comp_mode_p, comp_pred);

      tmp_best_rdu = best_rd -
          VPXMIN(RDCOST(x->rdmult, x->rddiv, rate2, distortion2),
                 RDCOST(x->rdmult, x->rddiv, 0, total_sse));

      if (tmp_best_rdu > 0) {
        // If even the 'Y' rd value of split is higher than best so far
        // then dont bother looking at UV
        vp10_build_inter_predictors_sbuv(&x->e_mbd, mi_row, mi_col,
                                        BLOCK_8X8);
        memset(x->skip_txfm, SKIP_TXFM_NONE, sizeof(x->skip_txfm));
#if CONFIG_VAR_TX
        if (!inter_block_uvrd(cpi, x, &rate_uv, &distortion_uv, &uv_skippable,
                              &uv_sse, BLOCK_8X8, tmp_best_rdu))
          continue;
#else
        if (!super_block_uvrd(cpi, x, &rate_uv, &distortion_uv, &uv_skippable,
                              &uv_sse, BLOCK_8X8, tmp_best_rdu))
          continue;
#endif
        rate2 += rate_uv;
        distortion2 += distortion_uv;
        skippable = skippable && uv_skippable;
        total_sse += uv_sse;
      }
    }

    if (cm->reference_mode == REFERENCE_MODE_SELECT)
      rate2 += compmode_cost;

    // Estimate the reference frame signaling cost and add it
    // to the rolling cost variable.
    if (second_ref_frame > INTRA_FRAME) {
      rate2 += ref_costs_comp[ref_frame];
    } else {
      rate2 += ref_costs_single[ref_frame];
    }

    if (!disable_skip) {
      // Skip is never coded at the segment level for sub8x8 blocks and instead
      // always coded in the bitstream at the mode info level.

      if (ref_frame != INTRA_FRAME && !xd->lossless[mbmi->segment_id]) {
        if (RDCOST(x->rdmult, x->rddiv, rate_y + rate_uv, distortion2) <
            RDCOST(x->rdmult, x->rddiv, 0, total_sse)) {
          // Add in the cost of the no skip flag.
          rate2 += vp10_cost_bit(vp10_get_skip_prob(cm, xd), 0);
        } else {
          // FIXME(rbultje) make this work for splitmv also
          rate2 += vp10_cost_bit(vp10_get_skip_prob(cm, xd), 1);
          distortion2 = total_sse;
          assert(total_sse >= 0);
          rate2 -= (rate_y + rate_uv);
          rate_y = 0;
          rate_uv = 0;
          this_skip2 = 1;
        }
      } else {
        // Add in the cost of the no skip flag.
        rate2 += vp10_cost_bit(vp10_get_skip_prob(cm, xd), 0);
      }

      // Calculate the final RD estimate for this mode.
      this_rd = RDCOST(x->rdmult, x->rddiv, rate2, distortion2);
    }

    if (!disable_skip && ref_frame == INTRA_FRAME) {
      for (i = 0; i < REFERENCE_MODES; ++i)
        best_pred_rd[i] = VPXMIN(best_pred_rd[i], this_rd);
    }

    // Did this mode help.. i.e. is it the new best mode
    if (this_rd < best_rd || x->skip) {
      if (!mode_excluded) {
        int max_plane = MAX_MB_PLANE;
        // Note index of best mode so far
        best_ref_index = ref_index;

        if (ref_frame == INTRA_FRAME) {
          /* required for left and above block mv */
          mbmi->mv[0].as_int = 0;
          max_plane = 1;
        }

        rd_cost->rate = rate2;
#if CONFIG_SUPERTX
        *returnrate_nocoef = rate2 - rate_y - rate_uv;
        if (!disable_skip)
          *returnrate_nocoef -= vp10_cost_bit(vp10_get_skip_prob(cm, xd),
                                              this_skip2);
        *returnrate_nocoef -= vp10_cost_bit(vp10_get_intra_inter_prob(cm, xd),
                                            mbmi->ref_frame[0] != INTRA_FRAME);
        assert(*returnrate_nocoef > 0);
#endif  // CONFIG_SUPERTX
        rd_cost->dist = distortion2;
        rd_cost->rdcost = this_rd;
        best_rd = this_rd;
        best_yrd = best_rd -
                   RDCOST(x->rdmult, x->rddiv, rate_uv, distortion_uv);
        best_mbmode = *mbmi;
        best_skip2 = this_skip2;
        if (!x->select_tx_size)
          swap_block_ptr(x, ctx, 1, 0, 0, max_plane);

#if CONFIG_VAR_TX
        for (i = 0; i < MAX_MB_PLANE; ++i)
          memset(ctx->blk_skip[i], 0, sizeof(uint8_t) * ctx->num_4x4_blk);
#else
        memcpy(ctx->zcoeff_blk, x->zcoeff_blk[TX_4X4],
               sizeof(ctx->zcoeff_blk[0]) * ctx->num_4x4_blk);
#endif

        for (i = 0; i < 4; i++)
          best_bmodes[i] = xd->mi[0]->bmi[i];

        // TODO(debargha): enhance this test with a better distortion prediction
        // based on qp, activity mask and history
        if ((sf->mode_search_skip_flags & FLAG_EARLY_TERMINATE) &&
            (ref_index > MIN_EARLY_TERM_INDEX)) {
          int qstep = xd->plane[0].dequant[1];
          // TODO(debargha): Enhance this by specializing for each mode_index
          int scale = 4;
#if CONFIG_VP9_HIGHBITDEPTH
          if (xd->cur_buf->flags & YV12_FLAG_HIGHBITDEPTH) {
            qstep >>= (xd->bd - 8);
          }
#endif  // CONFIG_VP9_HIGHBITDEPTH
          if (x->source_variance < UINT_MAX) {
            const int var_adjust = (x->source_variance < 16);
            scale -= var_adjust;
          }
          if (ref_frame > INTRA_FRAME &&
              distortion2 * scale < qstep * qstep) {
            early_term = 1;
          }
        }
      }
    }

    /* keep record of best compound/single-only prediction */
    if (!disable_skip && ref_frame != INTRA_FRAME) {
      int64_t single_rd, hybrid_rd, single_rate, hybrid_rate;

      if (cm->reference_mode == REFERENCE_MODE_SELECT) {
        single_rate = rate2 - compmode_cost;
        hybrid_rate = rate2;
      } else {
        single_rate = rate2;
        hybrid_rate = rate2 + compmode_cost;
      }

      single_rd = RDCOST(x->rdmult, x->rddiv, single_rate, distortion2);
      hybrid_rd = RDCOST(x->rdmult, x->rddiv, hybrid_rate, distortion2);

      if (!comp_pred && single_rd < best_pred_rd[SINGLE_REFERENCE])
        best_pred_rd[SINGLE_REFERENCE] = single_rd;
      else if (comp_pred && single_rd < best_pred_rd[COMPOUND_REFERENCE])
        best_pred_rd[COMPOUND_REFERENCE] = single_rd;

      if (hybrid_rd < best_pred_rd[REFERENCE_MODE_SELECT])
        best_pred_rd[REFERENCE_MODE_SELECT] = hybrid_rd;
    }

    if (early_term)
      break;

    if (x->skip && !comp_pred)
      break;
  }

  if (best_rd >= best_rd_so_far) {
    rd_cost->rate = INT_MAX;
    rd_cost->rdcost = INT64_MAX;
#if CONFIG_SUPERTX
    *returnrate_nocoef = INT_MAX;
#endif  // CONFIG_SUPERTX
    return;
  }

  // If we used an estimate for the uv intra rd in the loop above...
  if (sf->use_uv_intra_rd_estimate) {
    // Do Intra UV best rd mode selection if best mode choice above was intra.
    if (best_mbmode.ref_frame[0] == INTRA_FRAME) {
      *mbmi = best_mbmode;
      rd_pick_intra_sbuv_mode(cpi, x, ctx, &rate_uv_intra,
                              &rate_uv_tokenonly,
                              &dist_uv,
                              &skip_uv,
                              BLOCK_8X8, TX_4X4);
    }
  }

  if (best_rd == INT64_MAX) {
    rd_cost->rate = INT_MAX;
    rd_cost->dist = INT64_MAX;
    rd_cost->rdcost = INT64_MAX;
#if CONFIG_SUPERTX
    *returnrate_nocoef = INT_MAX;
#endif  // CONFIG_SUPERTX
    return;
  }

  assert((cm->interp_filter == SWITCHABLE) ||
         (cm->interp_filter == best_mbmode.interp_filter) ||
         !is_inter_block(&best_mbmode));

  vp10_update_rd_thresh_fact(tile_data->thresh_freq_fact,
                            sf->adaptive_rd_thresh, bsize, best_ref_index);

  // macroblock modes
  *mbmi = best_mbmode;
  x->skip |= best_skip2;
  if (!is_inter_block(&best_mbmode)) {
    for (i = 0; i < 4; i++)
      xd->mi[0]->bmi[i].as_mode = best_bmodes[i].as_mode;
  } else {
    for (i = 0; i < 4; ++i)
      memcpy(&xd->mi[0]->bmi[i], &best_bmodes[i], sizeof(b_mode_info));

    mbmi->mv[0].as_int = xd->mi[0]->bmi[3].as_mv[0].as_int;
    mbmi->mv[1].as_int = xd->mi[0]->bmi[3].as_mv[1].as_int;
#if CONFIG_REF_MV
    mbmi->pred_mv[0].as_int = xd->mi[0]->bmi[3].pred_mv[0].as_int;
    mbmi->pred_mv[1].as_int = xd->mi[0]->bmi[3].pred_mv[1].as_int;
#endif
  }

  for (i = 0; i < REFERENCE_MODES; ++i) {
    if (best_pred_rd[i] == INT64_MAX)
      best_pred_diff[i] = INT_MIN;
    else
      best_pred_diff[i] = best_rd - best_pred_rd[i];
  }

  store_coding_context(x, ctx, best_ref_index,
                       best_pred_diff, 0);
}

#if CONFIG_OBMC
void vp10_build_prediction_by_above_preds(VP10_COMP *cpi,
                                          MACROBLOCKD *xd,
                                          int mi_row, int mi_col,
                                          uint8_t *tmp_buf[MAX_MB_PLANE],
                                          int tmp_stride[MAX_MB_PLANE]) {
  VP10_COMMON *const cm = &cpi->common;
  BLOCK_SIZE bsize = xd->mi[0]->mbmi.sb_type;
  int i, j, mi_step, ref;

  if (mi_row == 0)
    return;

  for (i = 0; i < VPXMIN(xd->n8_w, cm->mi_cols - mi_col); i += mi_step) {
    int mi_row_offset = -1;
    int mi_col_offset = i;
    int mi_x, mi_y, bw, bh;
    MODE_INFO *above_mi = xd->mi[mi_col_offset +
                                 mi_row_offset * xd->mi_stride];
    MB_MODE_INFO *above_mbmi = &above_mi->mbmi;

    mi_step = VPXMIN(xd->n8_w,
                     num_8x8_blocks_wide_lookup[above_mbmi->sb_type]);

    if (!is_neighbor_overlappable(above_mbmi))
      continue;

    for (j = 0; j < MAX_MB_PLANE; ++j) {
      struct macroblockd_plane *const pd = &xd->plane[j];
      setup_pred_plane(&pd->dst,
                       tmp_buf[j], tmp_stride[j],
                       0, i, NULL,
                       pd->subsampling_x, pd->subsampling_y);
    }
    /*
    set_ref_ptrs(cm, xd, above_mbmi->ref_frame[0], above_mbmi->ref_frame[1]);
    for (ref = 0; ref < 1 + has_second_ref(above_mbmi); ++ref) {
      YV12_BUFFER_CONFIG *cfg = get_ref_frame_buffer(
          cpi, above_mbmi->ref_frame[ref]);
      assert(cfg != NULL);
      vp10_setup_pre_planes(xd, ref, cfg, mi_row, mi_col + i,
                            &xd->block_refs[ref]->sf);
    }
    */
    for (ref = 0; ref < 1 + has_second_ref(above_mbmi); ++ref) {
      MV_REFERENCE_FRAME frame = above_mbmi->ref_frame[ref];
      RefBuffer *ref_buf = &cm->frame_refs[frame - LAST_FRAME];

      xd->block_refs[ref] = ref_buf;
      if ((!vp10_is_valid_scale(&ref_buf->sf)))
        vpx_internal_error(xd->error_info, VPX_CODEC_UNSUP_BITSTREAM,
                           "Reference frame has invalid dimensions");
      vp10_setup_pre_planes(xd, ref, ref_buf->buf, mi_row, mi_col + i,
                            &ref_buf->sf);
    }

    xd->mb_to_left_edge   = -(((mi_col + i) * MI_SIZE) * 8);
    mi_x = (mi_col + i) << MI_SIZE_LOG2;
    mi_y = mi_row << MI_SIZE_LOG2;

    for (j = 0; j < MAX_MB_PLANE; ++j) {
      const struct macroblockd_plane *pd = &xd->plane[j];
      bw = (mi_step * 8) >> pd->subsampling_x;
      bh = VPXMAX((num_4x4_blocks_high_lookup[bsize] * 2) >> pd->subsampling_y,
                  4);

      if (above_mbmi->sb_type < BLOCK_8X8) {
        const PARTITION_TYPE bp = BLOCK_8X8 - above_mbmi->sb_type;
        const int have_vsplit = bp != PARTITION_HORZ;
        const int have_hsplit = bp != PARTITION_VERT;
        const int num_4x4_w = 2 >> ((!have_vsplit) | pd->subsampling_x);
        const int num_4x4_h = 2 >> ((!have_hsplit) | pd->subsampling_y);
        const int pw = 8 >> (have_vsplit | pd->subsampling_x);
        int x, y;

        for (y = 0; y < num_4x4_h; ++y)
          for (x = 0; x < num_4x4_w; ++x) {
            if ((bp == PARTITION_HORZ || bp == PARTITION_SPLIT)
                && y == 0 && !pd->subsampling_y)
              continue;

            build_inter_predictors(xd, j, mi_col_offset, mi_row_offset,
                                   y * 2 + x, bw, bh,
                                   4 * x, 0, pw, bh,
#if CONFIG_SUPERTX && CONFIG_EXT_INTER
                                   0, 0,
#endif  // CONFIG_SUPERTX && CONFIG_EXT_INTER
                                   mi_x, mi_y);
          }
      } else {
        build_inter_predictors(xd, j, mi_col_offset, mi_row_offset,
                               0, bw, bh, 0, 0, bw, bh,
#if CONFIG_SUPERTX && CONFIG_EXT_INTER
                               0, 0,
#endif  // CONFIG_SUPERTX && CONFIG_EXT_INTER
                               mi_x, mi_y);
      }
    }
  }
  xd->mb_to_left_edge   = -((mi_col * MI_SIZE) * 8);
}

void vp10_build_prediction_by_left_preds(VP10_COMP *cpi,
                                         MACROBLOCKD *xd,
                                         int mi_row, int mi_col,
                                         uint8_t *tmp_buf[MAX_MB_PLANE],
                                         int tmp_stride[MAX_MB_PLANE]) {
  VP10_COMMON *const cm = &cpi->common;
  const TileInfo *const tile = &xd->tile;
  BLOCK_SIZE bsize = xd->mi[0]->mbmi.sb_type;
  int i, j, mi_step, ref;

  if (mi_col == 0 || (mi_col - 1 < tile->mi_col_start) ||
      (mi_col - 1) >= tile->mi_col_end)
    return;

  for (i = 0; i < VPXMIN(xd->n8_h, cm->mi_rows - mi_row); i += mi_step) {
    int mi_row_offset = i;
    int mi_col_offset = -1;
    int mi_x, mi_y, bw, bh;
    MODE_INFO *left_mi = xd->mi[mi_col_offset +
                                mi_row_offset * xd->mi_stride];
    MB_MODE_INFO *left_mbmi = &left_mi->mbmi;
    const int is_compound = has_second_ref(left_mbmi);

    mi_step = VPXMIN(xd->n8_h,
                     num_8x8_blocks_high_lookup[left_mbmi->sb_type]);

    if (!is_neighbor_overlappable(left_mbmi))
      continue;

    for (j = 0; j < MAX_MB_PLANE; ++j) {
      struct macroblockd_plane *const pd = &xd->plane[j];
      setup_pred_plane(&pd->dst,
                       tmp_buf[j], tmp_stride[j],
                       i, 0, NULL,
                       pd->subsampling_x, pd->subsampling_y);
    }
    /*
    set_ref_ptrs(cm, xd, left_mbmi->ref_frame[0], left_mbmi->ref_frame[1]);
    for (ref = 0; ref < 1 + has_second_ref(left_mbmi); ++ref) {
      YV12_BUFFER_CONFIG *cfg = get_ref_frame_buffer(cpi,
                                                     left_mbmi->ref_frame[ref]);
      assert(cfg != NULL);
      vp10_setup_pre_planes(xd, ref, cfg, mi_row + i, mi_col,
                            &xd->block_refs[ref]->sf);
    }
    */
    for (ref = 0; ref < 1 + is_compound; ++ref) {
      MV_REFERENCE_FRAME frame = left_mbmi->ref_frame[ref];
      RefBuffer *ref_buf = &cm->frame_refs[frame - LAST_FRAME];

      xd->block_refs[ref] = ref_buf;
      if ((!vp10_is_valid_scale(&ref_buf->sf)))
        vpx_internal_error(xd->error_info, VPX_CODEC_UNSUP_BITSTREAM,
                           "Reference frame has invalid dimensions");
      vp10_setup_pre_planes(xd, ref, ref_buf->buf, mi_row + i, mi_col,
                            &ref_buf->sf);
    }

    xd->mb_to_top_edge    = -(((mi_row + i) * MI_SIZE) * 8);
    mi_x = mi_col << MI_SIZE_LOG2;
    mi_y = (mi_row + i) << MI_SIZE_LOG2;

    for (j = 0; j < MAX_MB_PLANE; ++j) {
      const struct macroblockd_plane *pd = &xd->plane[j];
      bw = VPXMAX((num_4x4_blocks_wide_lookup[bsize] * 2) >> pd->subsampling_x,
                  4);
      bh = (mi_step << MI_SIZE_LOG2) >> pd->subsampling_y;

      if (left_mbmi->sb_type < BLOCK_8X8) {
        const PARTITION_TYPE bp = BLOCK_8X8 - left_mbmi->sb_type;
        const int have_vsplit = bp != PARTITION_HORZ;
        const int have_hsplit = bp != PARTITION_VERT;
        const int num_4x4_w = 2 >> ((!have_vsplit) | pd->subsampling_x);
        const int num_4x4_h = 2 >> ((!have_hsplit) | pd->subsampling_y);
        const int ph = 8 >> (have_hsplit | pd->subsampling_y);
        int x, y;

        for (y = 0; y < num_4x4_h; ++y)
          for (x = 0; x < num_4x4_w; ++x) {
            if ((bp == PARTITION_VERT || bp == PARTITION_SPLIT)
                && x == 0 && !pd->subsampling_x)
              continue;

            build_inter_predictors(xd, j, mi_col_offset, mi_row_offset,
                                   y * 2 + x, bw, bh,
                                   0, 4 * y, bw, ph,
#if CONFIG_SUPERTX && CONFIG_EXT_INTER
                                   0, 0,
#endif  // CONFIG_SUPERTX && CONFIG_EXT_INTER
                                   mi_x, mi_y);
          }
      } else {
        build_inter_predictors(xd, j, mi_col_offset, mi_row_offset, 0,
                               bw, bh, 0, 0, bw, bh,
#if CONFIG_SUPERTX && CONFIG_EXT_INTER
                               0, 0,
#endif  // CONFIG_SUPERTX && CONFIG_EXT_INTER
                               mi_x, mi_y);
      }
    }
  }
  xd->mb_to_top_edge    = -((mi_row * MI_SIZE) * 8);
}
#endif  // CONFIG_OBMC
