/*
 *  Copyright (c) 2010 The WebM project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */


#include "./vp10_rtcd.h"
#include "./vpx_config.h"
#include "./vpx_dsp_rtcd.h"

#include "vpx_dsp/quantize.h"
#include "vpx_mem/vpx_mem.h"
#include "vpx_ports/mem.h"

#include "vp10/common/idct.h"
#include "vp10/common/reconinter.h"
#include "vp10/common/reconintra.h"
#include "vp10/common/scan.h"

#include "vp10/encoder/encodemb.h"
#include "vp10/encoder/hybrid_fwd_txfm.h"
#include "vp10/encoder/quantize.h"
#include "vp10/encoder/rd.h"
#include "vp10/encoder/tokenize.h"

struct optimize_ctx {
  ENTROPY_CONTEXT ta[MAX_MB_PLANE][16];
  ENTROPY_CONTEXT tl[MAX_MB_PLANE][16];
};

void vp10_subtract_plane(MACROBLOCK *x, BLOCK_SIZE bsize, int plane) {
  struct macroblock_plane *const p = &x->plane[plane];
  const struct macroblockd_plane *const pd = &x->e_mbd.plane[plane];
  const BLOCK_SIZE plane_bsize = get_plane_block_size(bsize, pd);
  const int bw = 4 * num_4x4_blocks_wide_lookup[plane_bsize];
  const int bh = 4 * num_4x4_blocks_high_lookup[plane_bsize];

#if CONFIG_VP9_HIGHBITDEPTH
  if (x->e_mbd.cur_buf->flags & YV12_FLAG_HIGHBITDEPTH) {
    vpx_highbd_subtract_block(bh, bw, p->src_diff, bw, p->src.buf,
                              p->src.stride, pd->dst.buf, pd->dst.stride,
                              x->e_mbd.bd);
    return;
  }
#endif  // CONFIG_VP9_HIGHBITDEPTH
  vpx_subtract_block(bh, bw, p->src_diff, bw, p->src.buf, p->src.stride,
                     pd->dst.buf, pd->dst.stride);
}

#define RDTRUNC(RM, DM, R, D)                        \
  (((1 << (VP9_PROB_COST_SHIFT - 1)) + (R) * (RM)) & \
   ((1 << VP9_PROB_COST_SHIFT) - 1))

typedef struct vp10_token_state {
  int           rate;
  int           error;
  int           next;
  int16_t       token;
  short         qc;
} vp10_token_state;

// TODO(jimbankoski): experiment to find optimal RD numbers.
static const int plane_rd_mult[PLANE_TYPES] = { 4, 2 };

#define UPDATE_RD_COST()\
{\
  rd_cost0 = RDCOST(rdmult, rddiv, rate0, error0);\
  rd_cost1 = RDCOST(rdmult, rddiv, rate1, error1);\
  if (rd_cost0 == rd_cost1) {\
    rd_cost0 = RDTRUNC(rdmult, rddiv, rate0, error0);\
    rd_cost1 = RDTRUNC(rdmult, rddiv, rate1, error1);\
  }\
}

// This function is a place holder for now but may ultimately need
// to scan previous tokens to work out the correct context.
static int trellis_get_coeff_context(const int16_t *scan,
                                     const int16_t *nb,
                                     int idx, int token,
                                     uint8_t *token_cache) {
  int bak = token_cache[scan[idx]], pt;
  token_cache[scan[idx]] = vp10_pt_energy_class[token];
  pt = get_coef_context(nb, token_cache, idx + 1);
  token_cache[scan[idx]] = bak;
  return pt;
}

static int optimize_b(MACROBLOCK *mb, int plane, int block,
                      TX_SIZE tx_size, int ctx) {
  MACROBLOCKD *const xd = &mb->e_mbd;
  struct macroblock_plane *const p = &mb->plane[plane];
  struct macroblockd_plane *const pd = &xd->plane[plane];
  const int ref = is_inter_block(&xd->mi[0]->mbmi);
  vp10_token_state tokens[1025][2];
  unsigned best_index[1025][2];
  uint8_t token_cache[1024];
  const tran_low_t *const coeff = BLOCK_OFFSET(mb->plane[plane].coeff, block);
  tran_low_t *const qcoeff = BLOCK_OFFSET(p->qcoeff, block);
  tran_low_t *const dqcoeff = BLOCK_OFFSET(pd->dqcoeff, block);
  const int eob = p->eobs[block];
  const PLANE_TYPE type = pd->plane_type;
  const int default_eob = 16 << (tx_size << 1);
  const int mul = 1 + (tx_size == TX_32X32);
  const int16_t *dequant_ptr = pd->dequant;
  const uint8_t *const band_translate = get_band_translate(tx_size);
  TX_TYPE tx_type = get_tx_type(type, xd, block, tx_size);
  const scan_order *const so =
      get_scan(tx_size, tx_type, is_inter_block(&xd->mi[0]->mbmi));
  const int16_t *const scan = so->scan;
  const int16_t *const nb = so->neighbors;
  int next = eob, sz = 0;
  int64_t rdmult = mb->rdmult * plane_rd_mult[type], rddiv = mb->rddiv;
  int64_t rd_cost0, rd_cost1;
  int rate0, rate1, error0, error1;
  int16_t t0, t1;
  EXTRABIT e0;
  int best, band, pt, i, final_eob;
#if CONFIG_VP9_HIGHBITDEPTH
  const int *cat6_high_cost = vp10_get_high_cost_table(xd->bd);
#else
  const int *cat6_high_cost = vp10_get_high_cost_table(8);
#endif

  assert((!type && !plane) || (type && plane));
  assert(eob <= default_eob);

  /* Now set up a Viterbi trellis to evaluate alternative roundings. */
  if (!ref)
    rdmult = (rdmult * 9) >> 4;

  /* Initialize the sentinel node of the trellis. */
  tokens[eob][0].rate = 0;
  tokens[eob][0].error = 0;
  tokens[eob][0].next = default_eob;
  tokens[eob][0].token = EOB_TOKEN;
  tokens[eob][0].qc = 0;
  tokens[eob][1] = tokens[eob][0];

  for (i = 0; i < eob; i++)
    token_cache[scan[i]] =
        vp10_pt_energy_class[vp10_get_token(qcoeff[scan[i]])];

  for (i = eob; i-- > 0;) {
    int base_bits, d2, dx;
    const int rc = scan[i];
    int x = qcoeff[rc];
    /* Only add a trellis state for non-zero coefficients. */
    if (x) {
      int shortcut = 0;
      error0 = tokens[next][0].error;
      error1 = tokens[next][1].error;
      /* Evaluate the first possibility for this state. */
      rate0 = tokens[next][0].rate;
      rate1 = tokens[next][1].rate;
      vp10_get_token_extra(x, &t0, &e0);
      /* Consider both possible successor states. */
      if (next < default_eob) {
        band = band_translate[i + 1];
        pt = trellis_get_coeff_context(scan, nb, i, t0, token_cache);
        rate0 += mb->token_costs[tx_size][type][ref][band][0][pt]
                                [tokens[next][0].token];
        rate1 += mb->token_costs[tx_size][type][ref][band][0][pt]
                                [tokens[next][1].token];
      }
      UPDATE_RD_COST();
      /* And pick the best. */
      best = rd_cost1 < rd_cost0;
      base_bits = vp10_get_cost(t0, e0, cat6_high_cost);
      dx = mul * (dqcoeff[rc] - coeff[rc]);
#if CONFIG_VP9_HIGHBITDEPTH
      if (xd->cur_buf->flags & YV12_FLAG_HIGHBITDEPTH) {
        dx >>= xd->bd - 8;
      }
#endif  // CONFIG_VP9_HIGHBITDEPTH
      d2 = dx * dx;
      tokens[i][0].rate = base_bits + (best ? rate1 : rate0);
      tokens[i][0].error = d2 + (best ? error1 : error0);
      tokens[i][0].next = next;
      tokens[i][0].token = t0;
      tokens[i][0].qc = x;
      best_index[i][0] = best;

      /* Evaluate the second possibility for this state. */
      rate0 = tokens[next][0].rate;
      rate1 = tokens[next][1].rate;

      if ((abs(x) * dequant_ptr[rc != 0] > abs(coeff[rc]) * mul) &&
          (abs(x) * dequant_ptr[rc != 0] < abs(coeff[rc]) * mul +
                                               dequant_ptr[rc != 0]))
        shortcut = 1;
      else
        shortcut = 0;

      if (shortcut) {
        sz = -(x < 0);
        x -= 2 * sz + 1;
      }

      /* Consider both possible successor states. */
      if (!x) {
        /* If we reduced this coefficient to zero, check to see if
         *  we need to move the EOB back here.
         */
        t0 = tokens[next][0].token == EOB_TOKEN ? EOB_TOKEN : ZERO_TOKEN;
        t1 = tokens[next][1].token == EOB_TOKEN ? EOB_TOKEN : ZERO_TOKEN;
        e0 = 0;
      } else {
        vp10_get_token_extra(x, &t0, &e0);
        t1 = t0;
      }
      if (next < default_eob) {
        band = band_translate[i + 1];
        if (t0 != EOB_TOKEN) {
          pt = trellis_get_coeff_context(scan, nb, i, t0, token_cache);
          rate0 += mb->token_costs[tx_size][type][ref][band][!x][pt]
                                  [tokens[next][0].token];
        }
        if (t1 != EOB_TOKEN) {
          pt = trellis_get_coeff_context(scan, nb, i, t1, token_cache);
          rate1 += mb->token_costs[tx_size][type][ref][band][!x][pt]
                                  [tokens[next][1].token];
        }
      }

      UPDATE_RD_COST();
      /* And pick the best. */
      best = rd_cost1 < rd_cost0;
      base_bits = vp10_get_cost(t0, e0, cat6_high_cost);

      if (shortcut) {
#if CONFIG_VP9_HIGHBITDEPTH
        if (xd->cur_buf->flags & YV12_FLAG_HIGHBITDEPTH) {
          dx -= ((dequant_ptr[rc != 0] >> (xd->bd - 8)) + sz) ^ sz;
        } else {
          dx -= (dequant_ptr[rc != 0] + sz) ^ sz;
        }
#else
        dx -= (dequant_ptr[rc != 0] + sz) ^ sz;
#endif  // CONFIG_VP9_HIGHBITDEPTH
        d2 = dx * dx;
      }
      tokens[i][1].rate = base_bits + (best ? rate1 : rate0);
      tokens[i][1].error = d2 + (best ? error1 : error0);
      tokens[i][1].next = next;
      tokens[i][1].token = best ? t1 : t0;
      tokens[i][1].qc = x;
      best_index[i][1] = best;
      /* Finally, make this the new head of the trellis. */
      next = i;
    } else {
      /* There's no choice to make for a zero coefficient, so we don't
       *  add a new trellis node, but we do need to update the costs.
       */
      band = band_translate[i + 1];
      t0 = tokens[next][0].token;
      t1 = tokens[next][1].token;
      /* Update the cost of each path if we're past the EOB token. */
      if (t0 != EOB_TOKEN) {
        tokens[next][0].rate +=
            mb->token_costs[tx_size][type][ref][band][1][0][t0];
        tokens[next][0].token = ZERO_TOKEN;
      }
      if (t1 != EOB_TOKEN) {
        tokens[next][1].rate +=
            mb->token_costs[tx_size][type][ref][band][1][0][t1];
        tokens[next][1].token = ZERO_TOKEN;
      }
      best_index[i][0] = best_index[i][1] = 0;
      /* Don't update next, because we didn't add a new node. */
    }
  }

  /* Now pick the best path through the whole trellis. */
  band = band_translate[i + 1];
  rate0 = tokens[next][0].rate;
  rate1 = tokens[next][1].rate;
  error0 = tokens[next][0].error;
  error1 = tokens[next][1].error;
  t0 = tokens[next][0].token;
  t1 = tokens[next][1].token;
  rate0 += mb->token_costs[tx_size][type][ref][band][0][ctx][t0];
  rate1 += mb->token_costs[tx_size][type][ref][band][0][ctx][t1];
  UPDATE_RD_COST();
  best = rd_cost1 < rd_cost0;
  final_eob = -1;
  memset(qcoeff, 0, sizeof(*qcoeff) * (16 << (tx_size * 2)));
  memset(dqcoeff, 0, sizeof(*dqcoeff) * (16 << (tx_size * 2)));
  for (i = next; i < eob; i = next) {
    const int x = tokens[i][best].qc;
    const int rc = scan[i];
    if (x) {
      final_eob = i;
    }

    qcoeff[rc] = x;
    dqcoeff[rc] = (x * dequant_ptr[rc != 0]) / mul;

    next = tokens[i][best].next;
    best = best_index[i][best];
  }
  final_eob++;

  mb->plane[plane].eobs[block] = final_eob;
  assert(final_eob <= default_eob);
  return final_eob;
}

#if CONFIG_VP9_HIGHBITDEPTH
typedef enum QUANT_FUNC {
  QUANT_FUNC_LOWBD = 0,
  QUANT_FUNC_LOWBD_32 = 1,
  QUANT_FUNC_HIGHBD = 2,
  QUANT_FUNC_HIGHBD_32 = 3,
  QUANT_FUNC_LAST = 4
} QUANT_FUNC;

static VP10_QUANT_FACADE
    quant_func_list[VP10_XFORM_QUANT_LAST][QUANT_FUNC_LAST] = {
        {vp10_quantize_fp_facade, vp10_quantize_fp_32x32_facade,
         vp10_highbd_quantize_fp_facade, vp10_highbd_quantize_fp_32x32_facade},
        {vp10_quantize_b_facade, vp10_quantize_b_32x32_facade,
         vp10_highbd_quantize_b_facade, vp10_highbd_quantize_b_32x32_facade},
        {vp10_quantize_dc_facade, vp10_quantize_dc_32x32_facade,
         vp10_highbd_quantize_dc_facade, vp10_highbd_quantize_dc_32x32_facade},
        {NULL, NULL, NULL, NULL}};

#else
typedef enum QUANT_FUNC {
  QUANT_FUNC_LOWBD = 0,
  QUANT_FUNC_LOWBD_32 = 1,
  QUANT_FUNC_LAST = 2
} QUANT_FUNC;

static VP10_QUANT_FACADE
    quant_func_list[VP10_XFORM_QUANT_LAST][QUANT_FUNC_LAST] = {
        {vp10_quantize_fp_facade, vp10_quantize_fp_32x32_facade},
        {vp10_quantize_b_facade, vp10_quantize_b_32x32_facade},
        {vp10_quantize_dc_facade, vp10_quantize_dc_32x32_facade},
        {NULL, NULL}};
#endif

static FWD_TXFM_OPT fwd_txfm_opt_list[VP10_XFORM_QUANT_LAST] = {
    FWD_TXFM_OPT_NORMAL, FWD_TXFM_OPT_NORMAL, FWD_TXFM_OPT_DC,
    FWD_TXFM_OPT_NORMAL};

void vp10_xform_quant(MACROBLOCK *x, int plane, int block, int blk_row,
                      int blk_col, BLOCK_SIZE plane_bsize, TX_SIZE tx_size,
                      VP10_XFORM_QUANT xform_quant_idx) {
  MACROBLOCKD *const xd = &x->e_mbd;
  const struct macroblock_plane *const p = &x->plane[plane];
  const struct macroblockd_plane *const pd = &xd->plane[plane];
  PLANE_TYPE plane_type = (plane == 0) ? PLANE_TYPE_Y : PLANE_TYPE_UV;
  TX_TYPE tx_type = get_tx_type(plane_type, xd, block, tx_size);
  const scan_order *const scan_order =
      get_scan(tx_size, tx_type, is_inter_block(&xd->mi[0]->mbmi));
  tran_low_t *const coeff = BLOCK_OFFSET(p->coeff, block);
  tran_low_t *const qcoeff = BLOCK_OFFSET(p->qcoeff, block);
  tran_low_t *const dqcoeff = BLOCK_OFFSET(pd->dqcoeff, block);
  uint16_t *const eob = &p->eobs[block];
  const int diff_stride = 4 * num_4x4_blocks_wide_lookup[plane_bsize];
  const int16_t *src_diff;
  const int tx1d_size = get_tx1d_size(tx_size);
  const int tx2d_size = tx1d_size * tx1d_size;

  FWD_TXFM_PARAM fwd_txfm_param;
  fwd_txfm_param.tx_type = get_tx_type(plane_type, xd, block, tx_size);
  fwd_txfm_param.tx_size = tx_size;
  fwd_txfm_param.fwd_txfm_opt = fwd_txfm_opt_list[xform_quant_idx];
  fwd_txfm_param.rd_transform = x->use_lp32x32fdct;
  fwd_txfm_param.lossless = xd->lossless[xd->mi[0]->mbmi.segment_id];

  src_diff = &p->src_diff[4 * (blk_row * diff_stride + blk_col)];

#if CONFIG_VP9_HIGHBITDEPTH
  fwd_txfm_param.bd = xd->bd;
  if (xd->cur_buf->flags & YV12_FLAG_HIGHBITDEPTH) {
    highbd_fwd_txfm(src_diff, coeff, diff_stride, &fwd_txfm_param);
    if (xform_quant_idx != VP10_XFORM_QUANT_SKIP_QUANT) {
      if (x->skip_block) {
        vp10_quantize_skip(tx2d_size, qcoeff, dqcoeff, eob);
      } else {
        if (tx_size == TX_32X32)
          quant_func_list[xform_quant_idx][QUANT_FUNC_HIGHBD_32](
              coeff, tx2d_size, p, qcoeff, pd, dqcoeff, eob, scan_order);
        else
          quant_func_list[xform_quant_idx][QUANT_FUNC_HIGHBD](
              coeff, tx2d_size, p, qcoeff, pd, dqcoeff, eob, scan_order);
      }
    }
    return;
  }
#endif  // CONFIG_VP9_HIGHBITDEPTH

  fwd_txfm(src_diff, coeff, diff_stride, &fwd_txfm_param);
  if (xform_quant_idx != VP10_XFORM_QUANT_SKIP_QUANT) {
    if (x->skip_block) {
      vp10_quantize_skip(tx2d_size, qcoeff, dqcoeff, eob);
    } else {
      if (tx_size == TX_32X32)
        quant_func_list[xform_quant_idx][QUANT_FUNC_LOWBD_32](
            coeff, tx2d_size, p, qcoeff, pd, dqcoeff, eob, scan_order);
      else
        quant_func_list[xform_quant_idx][QUANT_FUNC_LOWBD](
            coeff, tx2d_size, p, qcoeff, pd, dqcoeff, eob, scan_order);
    }
  }
}

static void encode_block(int plane, int block, int blk_row, int blk_col,
                         BLOCK_SIZE plane_bsize,
                         TX_SIZE tx_size, void *arg) {
  struct encode_b_args *const args = arg;
  MACROBLOCK *const x = args->x;
  MACROBLOCKD *const xd = &x->e_mbd;
  struct optimize_ctx *const ctx = args->ctx;
  struct macroblock_plane *const p = &x->plane[plane];
  struct macroblockd_plane *const pd = &xd->plane[plane];
  tran_low_t *const dqcoeff = BLOCK_OFFSET(pd->dqcoeff, block);
  uint8_t *dst;
  ENTROPY_CONTEXT *a, *l;
  INV_TXFM_PARAM inv_txfm_param;
#if CONFIG_VAR_TX
  int i;
  const int bwl = b_width_log2_lookup[plane_bsize];
#endif
  dst = &pd->dst.buf[4 * blk_row * pd->dst.stride + 4 * blk_col];
  a = &ctx->ta[plane][blk_col];
  l = &ctx->tl[plane][blk_row];

  // TODO(jingning): per transformed block zero forcing only enabled for
  // luma component. will integrate chroma components as well.
  // Turn this back on when the rate-distortion loop is synchronized with
  // the recursive transform block coding.
//  if (x->zcoeff_blk[tx_size][block] && plane == 0) {
//    p->eobs[block] = 0;
//    *a = *l = 0;
//    return;
//  }

#if CONFIG_VAR_TX
  if (!x->skip_recode &&
      x->blk_skip[plane][(blk_row << bwl) + blk_col] == 0) {
#else
  if (!x->skip_recode) {
#endif
    if (x->quant_fp) {
      // Encoding process for rtc mode
      if (x->skip_txfm[0][0] == SKIP_TXFM_AC_DC && plane == 0) {
        // skip forward transform
        p->eobs[block] = 0;
        *a = *l = 0;
        return;
      } else {
        vp10_xform_quant(x, plane, block, blk_row, blk_col, plane_bsize,
                         tx_size, VP10_XFORM_QUANT_FP);
      }
    } else {
      if (max_txsize_lookup[plane_bsize] == tx_size) {
        int blk_index = (block >> (tx_size << 1));
        if (x->skip_txfm[plane][blk_index] == SKIP_TXFM_NONE) {
          // full forward transform and quantization
          vp10_xform_quant(x, plane, block, blk_row, blk_col, plane_bsize,
                           tx_size, VP10_XFORM_QUANT_B);
        } else if (x->skip_txfm[plane][blk_index] == SKIP_TXFM_AC_ONLY) {
          // fast path forward transform and quantization
          vp10_xform_quant(x, plane, block, blk_row, blk_col, plane_bsize,
                           tx_size, VP10_XFORM_QUANT_DC);
        } else {
          // skip forward transform
          p->eobs[block] = 0;
          *a = *l = 0;
#if !CONFIG_VAR_TX
          return;
#endif
        }
      } else {
        vp10_xform_quant(x, plane, block, blk_row, blk_col, plane_bsize,
                         tx_size, VP10_XFORM_QUANT_B);
      }
    }
  }
#if CONFIG_VAR_TX
  else {
    if (!x->skip_recode)
      p->eobs[block] = 0;
  }
#endif

  if (x->optimize && (!x->skip_recode || !x->skip_optimize)) {
    int ctx;
#if CONFIG_VAR_TX
    switch (tx_size) {
      case TX_4X4:
        break;
      case TX_8X8:
        a[0] = !!*(const uint16_t *)&a[0];
        l[0] = !!*(const uint16_t *)&l[0];
        break;
      case TX_16X16:
        a[0] = !!*(const uint32_t *)&a[0];
        l[0] = !!*(const uint32_t *)&l[0];
        break;
      case TX_32X32:
        a[0] = !!*(const uint64_t *)&a[0];
        l[0] = !!*(const uint64_t *)&l[0];
        break;
      default:
        assert(0 && "Invalid transform size.");
        break;
    }
#endif
    ctx = combine_entropy_contexts(*a, *l);
    *a = *l = optimize_b(x, plane, block, tx_size, ctx) > 0;
  } else {
    *a = *l = p->eobs[block] > 0;
  }

#if CONFIG_VAR_TX
  for (i = 0; i < (1 << tx_size); ++i) {
    a[i] = a[0];
    l[i] = l[0];
  }
#endif

  if (p->eobs[block])
    *(args->skip) = 0;

  if (p->eobs[block] == 0)
    return;

  // inverse transform parameters
  inv_txfm_param.tx_type = get_tx_type(pd->plane_type, xd, block, tx_size);
  inv_txfm_param.tx_size = tx_size;
  inv_txfm_param.eob = p->eobs[block];
  inv_txfm_param.lossless = xd->lossless[xd->mi[0]->mbmi.segment_id];

#if CONFIG_VP9_HIGHBITDEPTH
  if (xd->cur_buf->flags & YV12_FLAG_HIGHBITDEPTH) {
    inv_txfm_param.bd = xd->bd;
    highbd_inv_txfm_add(dqcoeff, dst, pd->dst.stride, &inv_txfm_param);
    return;
  }
#endif  // CONFIG_VP9_HIGHBITDEPTH
  inv_txfm_add(dqcoeff, dst, pd->dst.stride, &inv_txfm_param);
}

#if CONFIG_VAR_TX
static void encode_block_inter(int plane, int block, int blk_row, int blk_col,
                               BLOCK_SIZE plane_bsize, TX_SIZE tx_size,
                               void *arg) {
  struct encode_b_args *const args = arg;
  MACROBLOCK *const x = args->x;
  MACROBLOCKD *const xd = &x->e_mbd;
  MB_MODE_INFO *const mbmi = &xd->mi[0]->mbmi;
  const BLOCK_SIZE bsize = txsize_to_bsize[tx_size];
  const struct macroblockd_plane *const pd = &xd->plane[plane];
  const int tx_row = blk_row >> (1 - pd->subsampling_y);
  const int tx_col = blk_col >> (1 - pd->subsampling_x);
  const TX_SIZE plane_tx_size = plane ?
      get_uv_tx_size_impl(mbmi->inter_tx_size[tx_row][tx_col], bsize, 0, 0) :
      mbmi->inter_tx_size[tx_row][tx_col];

  int max_blocks_high = num_4x4_blocks_high_lookup[plane_bsize];
  int max_blocks_wide = num_4x4_blocks_wide_lookup[plane_bsize];

  if (xd->mb_to_bottom_edge < 0)
    max_blocks_high += xd->mb_to_bottom_edge >> (5 + pd->subsampling_y);
  if (xd->mb_to_right_edge < 0)
    max_blocks_wide += xd->mb_to_right_edge >> (5 + pd->subsampling_x);

  if (blk_row >= max_blocks_high || blk_col >= max_blocks_wide)
    return;

  if (tx_size == plane_tx_size) {
    encode_block(plane, block, blk_row, blk_col, plane_bsize,
                 tx_size, arg);
  } else {
    int bsl = b_width_log2_lookup[bsize];
    int i;

    assert(bsl > 0);
    --bsl;

    for (i = 0; i < 4; ++i) {
      const int offsetr = blk_row + ((i >> 1) << bsl);
      const int offsetc = blk_col + ((i & 0x01) << bsl);
      int step = 1 << (2 * (tx_size - 1));

      if (offsetr >= max_blocks_high || offsetc >= max_blocks_wide)
        continue;

      encode_block_inter(plane, block + i * step, offsetr, offsetc,
                         plane_bsize, tx_size - 1, arg);
    }
  }
}
#endif

static void encode_block_pass1(int plane, int block, int blk_row, int blk_col,
                               BLOCK_SIZE plane_bsize,
                               TX_SIZE tx_size, void *arg) {
  MACROBLOCK *const x = (MACROBLOCK *)arg;
  MACROBLOCKD *const xd = &x->e_mbd;
  struct macroblock_plane *const p = &x->plane[plane];
  struct macroblockd_plane *const pd = &xd->plane[plane];
  tran_low_t *const dqcoeff = BLOCK_OFFSET(pd->dqcoeff, block);
  uint8_t *dst;
  dst = &pd->dst.buf[4 * blk_row * pd->dst.stride + 4 * blk_col];

  vp10_xform_quant(x, plane, block, blk_row, blk_col, plane_bsize,
                   tx_size, VP10_XFORM_QUANT_B);

  if (p->eobs[block] > 0) {
#if CONFIG_VP9_HIGHBITDEPTH
    if (xd->cur_buf->flags & YV12_FLAG_HIGHBITDEPTH) {
      if (xd->lossless[xd->mi[0]->mbmi.segment_id]) {
        vp10_highbd_iwht4x4_add(dqcoeff, dst, pd->dst.stride,
                                p->eobs[block], xd->bd);
      } else {
        vp10_highbd_idct4x4_add(dqcoeff, dst, pd->dst.stride,
                                p->eobs[block], xd->bd);
      }
      return;
    }
#endif  // CONFIG_VP9_HIGHBITDEPTH
    if (xd->lossless[xd->mi[0]->mbmi.segment_id]) {
      vp10_iwht4x4_add(dqcoeff, dst, pd->dst.stride, p->eobs[block]);
    } else {
      vp10_idct4x4_add(dqcoeff, dst, pd->dst.stride, p->eobs[block]);
    }
  }
}

void vp10_encode_sby_pass1(MACROBLOCK *x, BLOCK_SIZE bsize) {
  vp10_subtract_plane(x, bsize, 0);
  vp10_foreach_transformed_block_in_plane(&x->e_mbd, bsize, 0,
                                          encode_block_pass1, x);
}

void vp10_encode_sb(MACROBLOCK *x, BLOCK_SIZE bsize) {
  MACROBLOCKD *const xd = &x->e_mbd;
  struct optimize_ctx ctx;
  MB_MODE_INFO *mbmi = &xd->mi[0]->mbmi;
  struct encode_b_args arg = {x, &ctx, &mbmi->skip};
  int plane;

  mbmi->skip = 1;

  if (x->skip)
    return;

  for (plane = 0; plane < MAX_MB_PLANE; ++plane) {
#if CONFIG_VAR_TX
    // TODO(jingning): Clean this up.
    const struct macroblockd_plane *const pd = &xd->plane[plane];
    const BLOCK_SIZE plane_bsize = get_plane_block_size(bsize, pd);
    const int mi_width = num_4x4_blocks_wide_lookup[plane_bsize];
    const int mi_height = num_4x4_blocks_high_lookup[plane_bsize];
    const TX_SIZE max_tx_size = max_txsize_lookup[plane_bsize];
    const BLOCK_SIZE txb_size = txsize_to_bsize[max_tx_size];
    const int bh = num_4x4_blocks_wide_lookup[txb_size];
    int idx, idy;
    int block = 0;
    int step = 1 << (max_tx_size * 2);
#endif
    if (!x->skip_recode)
      vp10_subtract_plane(x, bsize, plane);

    if (x->optimize && (!x->skip_recode || !x->skip_optimize)) {
#if CONFIG_VAR_TX
      vp10_get_entropy_contexts(bsize, TX_4X4, pd,
                                ctx.ta[plane], ctx.tl[plane]);
#else
      const struct macroblockd_plane* const pd = &xd->plane[plane];
      const TX_SIZE tx_size = plane ? get_uv_tx_size(mbmi, pd) : mbmi->tx_size;
      vp10_get_entropy_contexts(bsize, tx_size, pd,
                                ctx.ta[plane], ctx.tl[plane]);
#endif
    }

#if CONFIG_VAR_TX
    for (idy = 0; idy < mi_height; idy += bh) {
      for (idx = 0; idx < mi_width; idx += bh) {
        encode_block_inter(plane, block, idy, idx, plane_bsize,
                           max_tx_size, &arg);
        block += step;
      }
    }
#else
    vp10_foreach_transformed_block_in_plane(xd, bsize, plane, encode_block,
                                            &arg);
#endif
  }
}

#if CONFIG_SUPERTX
void vp10_encode_sb_supertx(MACROBLOCK *x, BLOCK_SIZE bsize) {
  MACROBLOCKD *const xd = &x->e_mbd;
  struct optimize_ctx ctx;
  MB_MODE_INFO *mbmi = &xd->mi[0]->mbmi;
  struct encode_b_args arg = {x, &ctx, &mbmi->skip};
  int plane;

  mbmi->skip = 1;
  if (x->skip)
    return;

  for (plane = 0; plane < MAX_MB_PLANE; ++plane) {
    const struct macroblockd_plane* const pd = &xd->plane[plane];
    const TX_SIZE tx_size = plane ? get_uv_tx_size(mbmi, pd) : mbmi->tx_size;
    vp10_subtract_plane(x, bsize, plane);
    vp10_get_entropy_contexts(bsize, tx_size, pd,
                              ctx.ta[plane], ctx.tl[plane]);
    vp10_foreach_transformed_block_in_plane(xd, bsize, plane, encode_block,
                                           &arg);
  }
}
#endif  // CONFIG_SUPERTX

void vp10_encode_block_intra(int plane, int block, int blk_row, int blk_col,
                             BLOCK_SIZE plane_bsize,
                             TX_SIZE tx_size, void *arg) {
  struct encode_b_args* const args = arg;
  MACROBLOCK *const x = args->x;
  MACROBLOCKD *const xd = &x->e_mbd;
  MB_MODE_INFO *mbmi = &xd->mi[0]->mbmi;
  struct macroblock_plane *const p = &x->plane[plane];
  struct macroblockd_plane *const pd = &xd->plane[plane];
  tran_low_t *dqcoeff = BLOCK_OFFSET(pd->dqcoeff, block);
  PLANE_TYPE plane_type = (plane == 0) ? PLANE_TYPE_Y : PLANE_TYPE_UV;
  const TX_TYPE tx_type = get_tx_type(plane_type, xd, block, tx_size);
  PREDICTION_MODE mode;
  const int bwl = b_width_log2_lookup[plane_bsize];
  const int bhl = b_height_log2_lookup[plane_bsize];
  const int diff_stride = 4 * (1 << bwl);
  uint8_t *src, *dst;
  int16_t *src_diff;
  uint16_t *eob = &p->eobs[block];
  const int src_stride = p->src.stride;
  const int dst_stride = pd->dst.stride;

  const int tx1d_size = get_tx1d_size(tx_size);

  INV_TXFM_PARAM inv_txfm_param;

  dst = &pd->dst.buf[4 * (blk_row * dst_stride + blk_col)];
  src = &p->src.buf[4 * (blk_row * src_stride + blk_col)];
  src_diff = &p->src_diff[4 * (blk_row * diff_stride + blk_col)];

  mode = plane == 0 ? get_y_mode(xd->mi[0], block) : mbmi->uv_mode;
  vp10_predict_intra_block(xd, bwl, bhl, tx_size, mode, dst, dst_stride, dst,
                           dst_stride, blk_col, blk_row, plane);
#if CONFIG_VP9_HIGHBITDEPTH
  if (xd->cur_buf->flags & YV12_FLAG_HIGHBITDEPTH) {
    vpx_highbd_subtract_block(tx1d_size, tx1d_size, src_diff, diff_stride, src,
                              src_stride, dst, dst_stride, xd->bd);
  } else {
    vpx_subtract_block(tx1d_size, tx1d_size, src_diff, diff_stride, src,
                       src_stride, dst, dst_stride);
  }
#else
  vpx_subtract_block(tx1d_size, tx1d_size, src_diff, diff_stride, src,
                     src_stride, dst, dst_stride);
#endif  // CONFIG_VP9_HIGHBITDEPTH

#if CONFIG_EXT_INTRA
  vp10_xform_quant(x, plane, block, blk_row, blk_col, plane_bsize, tx_size,
                   VP10_XFORM_QUANT_B);
#else
  if (!x->skip_recode)
    vp10_xform_quant(x, plane, block, blk_row, blk_col, plane_bsize, tx_size,
                     VP10_XFORM_QUANT_B);
  else
    vp10_xform_quant(x, plane, block, blk_row, blk_col, plane_bsize, tx_size,
                     VP10_XFORM_QUANT_SKIP_QUANT);
#endif  // CONFIG_EXT_INTRA

  if (*eob) {
    // inverse transform
    inv_txfm_param.tx_type = tx_type;
    inv_txfm_param.tx_size = tx_size;
    inv_txfm_param.eob = *eob;
    inv_txfm_param.lossless = xd->lossless[mbmi->segment_id];
#if CONFIG_VP9_HIGHBITDEPTH
    inv_txfm_param.bd = xd->bd;
    if (xd->cur_buf->flags & YV12_FLAG_HIGHBITDEPTH) {
      highbd_inv_txfm_add(dqcoeff, dst, dst_stride, &inv_txfm_param);
    } else {
      inv_txfm_add(dqcoeff, dst, dst_stride, &inv_txfm_param);
    }
#else
    inv_txfm_add(dqcoeff, dst, dst_stride, &inv_txfm_param);
#endif  // CONFIG_VP9_HIGHBITDEPTH

    *(args->skip) = 0;
  }
}

void vp10_encode_intra_block_plane(MACROBLOCK *x, BLOCK_SIZE bsize, int plane) {
  const MACROBLOCKD *const xd = &x->e_mbd;
  struct encode_b_args arg = {x, NULL, &xd->mi[0]->mbmi.skip};

  vp10_foreach_transformed_block_in_plane(xd, bsize, plane,
                                          vp10_encode_block_intra, &arg);
}
