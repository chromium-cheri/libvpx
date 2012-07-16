/*
 *  Copyright (c) 2010 The WebM project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */


#include "vpx_ports/config.h"
#include "encodemb.h"
#include "vp8/common/reconinter.h"
#include "quantize.h"
#include "tokenize.h"
#include "vp8/common/invtrans.h"
#include "vp8/common/recon.h"
#include "vp8/common/reconintra.h"
#include "dct.h"
#include "vpx_mem/vpx_mem.h"
#include "rdopt.h"
#include "vp8/common/systemdependent.h"

#if CONFIG_RUNTIME_CPU_DETECT
#define IF_RTCD(x) (x)
#else
#define IF_RTCD(x) NULL
#endif

#ifdef ENC_DEBUG
extern int enc_debug;
#endif

void vp8_subtract_b_c(BLOCK *be, BLOCKD *bd, int pitch) {
  unsigned char *src_ptr = (*(be->base_src) + be->src);
  short *diff_ptr = be->src_diff;
  unsigned char *pred_ptr = bd->predictor;
  int src_stride = be->src_stride;

  int r, c;

  for (r = 0; r < 4; r++) {
    for (c = 0; c < 4; c++) {
      diff_ptr[c] = src_ptr[c] - pred_ptr[c];
    }

    diff_ptr += pitch;
    pred_ptr += pitch;
    src_ptr  += src_stride;
  }
}

void vp8_subtract_4b_c(BLOCK *be, BLOCKD *bd, int pitch) {
  unsigned char *src_ptr = (*(be->base_src) + be->src);
  short *diff_ptr = be->src_diff;
  unsigned char *pred_ptr = bd->predictor;
  int src_stride = be->src_stride;
  int r, c;
  for (r = 0; r < 8; r++) {
    for (c = 0; c < 8; c++) {
      diff_ptr[c] = src_ptr[c] - pred_ptr[c];
    }
    diff_ptr += pitch;
    pred_ptr += pitch;
    src_ptr  += src_stride;
  }
}

void vp8_subtract_mbuv_c(short *diff, unsigned char *usrc, unsigned char *vsrc, unsigned char *pred, int stride) {
  short *udiff = diff + 256;
  short *vdiff = diff + 320;
  unsigned char *upred = pred + 256;
  unsigned char *vpred = pred + 320;

  int r, c;

  for (r = 0; r < 8; r++) {
    for (c = 0; c < 8; c++) {
      udiff[c] = usrc[c] - upred[c];
    }

    udiff += 8;
    upred += 8;
    usrc  += stride;
  }

  for (r = 0; r < 8; r++) {
    for (c = 0; c < 8; c++) {
      vdiff[c] = vsrc[c] - vpred[c];
    }

    vdiff += 8;
    vpred += 8;
    vsrc  += stride;
  }
}

void vp8_subtract_mby_c(short *diff, unsigned char *src, unsigned char *pred, int stride) {
  int r, c;

  for (r = 0; r < 16; r++) {
    for (c = 0; c < 16; c++) {
      diff[c] = src[c] - pred[c];
    }

    diff += 16;
    pred += 16;
    src  += stride;
  }
}

static void vp8_subtract_mb(const VP8_ENCODER_RTCD *rtcd, MACROBLOCK *x) {
  BLOCK *b = &x->block[0];

  ENCODEMB_INVOKE(&rtcd->encodemb, submby)(x->src_diff, *(b->base_src), x->e_mbd.predictor, b->src_stride);
  ENCODEMB_INVOKE(&rtcd->encodemb, submbuv)(x->src_diff, x->src.u_buffer, x->src.v_buffer, x->e_mbd.predictor, x->src.uv_stride);
}

static void build_dcblock(MACROBLOCK *x) {
  short *src_diff_ptr = &x->src_diff[384];
  int i;

  for (i = 0; i < 16; i++) {
    src_diff_ptr[i] = x->coeff[i * 16];
  }
}
void vp8_build_dcblock_8x8(MACROBLOCK *x) {
  short *src_diff_ptr = &x->src_diff[384];
  int i;
  for (i = 0; i < 16; i++) {
    src_diff_ptr[i] = 0;
  }
  src_diff_ptr[0] = x->coeff[0 * 16];
  src_diff_ptr[1] = x->coeff[4 * 16];
  src_diff_ptr[4] = x->coeff[8 * 16];
  src_diff_ptr[8] = x->coeff[12 * 16];
}

void vp8_transform_mbuv(MACROBLOCK *x) {
  int i;

  for (i = 16; i < 24; i += 2) {
    x->vp8_short_fdct8x4(&x->block[i].src_diff[0],
                         &x->block[i].coeff[0], 16);
  }
}


void vp8_transform_intra_mby(MACROBLOCK *x) {
  int i;

  for (i = 0; i < 16; i += 2) {
    x->vp8_short_fdct8x4(&x->block[i].src_diff[0],
                         &x->block[i].coeff[0], 32);
  }

  // build dc block from 16 y dc values
  build_dcblock(x);

  // do 2nd order transform on the dc block
  x->short_walsh4x4(&x->block[24].src_diff[0],
                    &x->block[24].coeff[0], 8);

}


static void transform_mb(MACROBLOCK *x) {
  int i;

  for (i = 0; i < 16; i += 2) {
    x->vp8_short_fdct8x4(&x->block[i].src_diff[0],
                         &x->block[i].coeff[0], 32);
  }

  // build dc block from 16 y dc values
  if (x->e_mbd.mode_info_context->mbmi.mode != SPLITMV)
    build_dcblock(x);

  for (i = 16; i < 24; i += 2) {
    x->vp8_short_fdct8x4(&x->block[i].src_diff[0],
                         &x->block[i].coeff[0], 16);
  }

  // do 2nd order transform on the dc block
  if (x->e_mbd.mode_info_context->mbmi.mode != SPLITMV)
    x->short_walsh4x4(&x->block[24].src_diff[0],
                      &x->block[24].coeff[0], 8);

}


static void transform_mby(MACROBLOCK *x) {
  int i;

  for (i = 0; i < 16; i += 2) {
    x->vp8_short_fdct8x4(&x->block[i].src_diff[0],
                         &x->block[i].coeff[0], 32);
  }

  // build dc block from 16 y dc values
  if (x->e_mbd.mode_info_context->mbmi.mode != SPLITMV) {
    build_dcblock(x);
    x->short_walsh4x4(&x->block[24].src_diff[0],
                      &x->block[24].coeff[0], 8);
  }
}

void vp8_transform_mbuv_8x8(MACROBLOCK *x) {
  int i;

#if !CONFIG_INT_8X8FDCT
  vp8_clear_system_state();
#endif

  for (i = 16; i < 24; i += 4) {
    x->vp8_short_fdct8x8(&x->block[i].src_diff[0],
                         &x->block[i].coeff[0], 16);
  }
}


void vp8_transform_intra_mby_8x8(MACROBLOCK *x) { // changed
  int i;
#if !CONFIG_INT_8X8FDCT
  vp8_clear_system_state();
#endif
  for (i = 0; i < 9; i += 8) {
    x->vp8_short_fdct8x8(&x->block[i].src_diff[0],
                         &x->block[i].coeff[0], 32);
  }
  for (i = 2; i < 11; i += 8) {
    x->vp8_short_fdct8x8(&x->block[i].src_diff[0],
                         &x->block[i + 2].coeff[0], 32);
  }
  // build dc block from 16 y dc values
  vp8_build_dcblock_8x8(x);
  // vp8_build_dcblock(x);

  // do 2nd order transform on the dc block
  x->short_fhaar2x2(&x->block[24].src_diff[0],
                    &x->block[24].coeff[0], 8);

}


void vp8_transform_mb_8x8(MACROBLOCK *x) {
  int i;
#if !CONFIG_INT_8X8FDCT
  vp8_clear_system_state();
#endif
  for (i = 0; i < 9; i += 8) {
    x->vp8_short_fdct8x8(&x->block[i].src_diff[0],
                         &x->block[i].coeff[0], 32);
  }
  for (i = 2; i < 11; i += 8) {
    x->vp8_short_fdct8x8(&x->block[i].src_diff[0],
                         &x->block[i + 2].coeff[0], 32);
  }
  // build dc block from 16 y dc values
  if (x->e_mbd.mode_info_context->mbmi.mode != B_PRED && x->e_mbd.mode_info_context->mbmi.mode != SPLITMV)
    vp8_build_dcblock_8x8(x);
  // vp8_build_dcblock(x);

  for (i = 16; i < 24; i += 4) {
    x->vp8_short_fdct8x8(&x->block[i].src_diff[0],
                         &x->block[i].coeff[0], 16);
  }

  // do 2nd order transform on the dc block
  if (x->e_mbd.mode_info_context->mbmi.mode != B_PRED && x->e_mbd.mode_info_context->mbmi.mode != SPLITMV)
    x->short_fhaar2x2(&x->block[24].src_diff[0],
                      &x->block[24].coeff[0], 8);
}

void vp8_transform_mby_8x8(MACROBLOCK *x) {
  int i;
#if !CONFIG_INT_8X8FDCT
  vp8_clear_system_state();
#endif
  for (i = 0; i < 9; i += 8) {
    x->vp8_short_fdct8x8(&x->block[i].src_diff[0],
                         &x->block[i].coeff[0], 32);
  }
  for (i = 2; i < 11; i += 8) {
    x->vp8_short_fdct8x8(&x->block[i].src_diff[0],
                         &x->block[i + 2].coeff[0], 32);
  }
  // build dc block from 16 y dc values
  if (x->e_mbd.mode_info_context->mbmi.mode != SPLITMV) {
    // vp8_build_dcblock(x);
    vp8_build_dcblock_8x8(x);
    x->short_fhaar2x2(&x->block[24].src_diff[0],
                      &x->block[24].coeff[0], 8);
  }
}


#define RDTRUNC(RM,DM,R,D) ( (128+(R)*(RM)) & 0xFF )
#define RDTRUNC_8x8(RM,DM,R,D) ( (128+(R)*(RM)) & 0xFF )
typedef struct vp8_token_state vp8_token_state;

struct vp8_token_state {
  int           rate;
  int           error;
  signed char   next;
  signed char   token;
  short         qc;
};

// TODO: experiments to find optimal multiple numbers
#define Y1_RD_MULT 4
#define UV_RD_MULT 2
#define Y2_RD_MULT 4

static const int plane_rd_mult[4] = {
  Y1_RD_MULT,
  Y2_RD_MULT,
  UV_RD_MULT,
  Y1_RD_MULT
};

static void optimize_b(MACROBLOCK *mb, int ib, int type,
                       ENTROPY_CONTEXT *a, ENTROPY_CONTEXT *l,
                       const VP8_ENCODER_RTCD *rtcd) {
  BLOCK *b;
  BLOCKD *d;
  vp8_token_state tokens[17][2];
  unsigned best_mask[2];
  const short *dequant_ptr;
  const short *coeff_ptr;
  short *qcoeff_ptr;
  short *dqcoeff_ptr;
  int eob;
  int i0;
  int rc;
  int x;
  int sz = 0;
  int next;
  int rdmult;
  int rddiv;
  int final_eob;
  int rd_cost0;
  int rd_cost1;
  int rate0;
  int rate1;
  int error0;
  int error1;
  int t0;
  int t1;
  int best;
  int band;
  int pt;
  int i;
  int err_mult = plane_rd_mult[type];

  b = &mb->block[ib];
  d = &mb->e_mbd.block[ib];

  dequant_ptr = d->dequant;
  coeff_ptr = b->coeff;
  qcoeff_ptr = d->qcoeff;
  dqcoeff_ptr = d->dqcoeff;
  i0 = !type;
  eob = d->eob;

  /* Now set up a Viterbi trellis to evaluate alternative roundings. */
  rdmult = mb->rdmult * err_mult;
  if (mb->e_mbd.mode_info_context->mbmi.ref_frame == INTRA_FRAME)
    rdmult = (rdmult * 9) >> 4;

  rddiv = mb->rddiv;
  best_mask[0] = best_mask[1] = 0;
  /* Initialize the sentinel node of the trellis. */
  tokens[eob][0].rate = 0;
  tokens[eob][0].error = 0;
  tokens[eob][0].next = 16;
  tokens[eob][0].token = DCT_EOB_TOKEN;
  tokens[eob][0].qc = 0;
  *(tokens[eob] + 1) = *(tokens[eob] + 0);
  next = eob;
  for (i = eob; i-- > i0;) {
    int base_bits;
    int d2;
    int dx;

    rc = vp8_default_zig_zag1d[i];
    x = qcoeff_ptr[rc];
    /* Only add a trellis state for non-zero coefficients. */
    if (x) {
      int shortcut = 0;
      error0 = tokens[next][0].error;
      error1 = tokens[next][1].error;
      /* Evaluate the first possibility for this state. */
      rate0 = tokens[next][0].rate;
      rate1 = tokens[next][1].rate;
      t0 = (vp8_dct_value_tokens_ptr + x)->Token;
      /* Consider both possible successor states. */
      if (next < 16) {
        band = vp8_coef_bands[i + 1];
        pt = vp8_prev_token_class[t0];
        rate0 +=
          mb->token_costs[type][band][pt][tokens[next][0].token];
        rate1 +=
          mb->token_costs[type][band][pt][tokens[next][1].token];
      }
      rd_cost0 = RDCOST(rdmult, rddiv, rate0, error0);
      rd_cost1 = RDCOST(rdmult, rddiv, rate1, error1);
      if (rd_cost0 == rd_cost1) {
        rd_cost0 = RDTRUNC(rdmult, rddiv, rate0, error0);
        rd_cost1 = RDTRUNC(rdmult, rddiv, rate1, error1);
      }
      /* And pick the best. */
      best = rd_cost1 < rd_cost0;
      base_bits = *(vp8_dct_value_cost_ptr + x);
      dx = dqcoeff_ptr[rc] - coeff_ptr[rc];
      d2 = dx * dx;
      tokens[i][0].rate = base_bits + (best ? rate1 : rate0);
      tokens[i][0].error = d2 + (best ? error1 : error0);
      tokens[i][0].next = next;
      tokens[i][0].token = t0;
      tokens[i][0].qc = x;
      best_mask[0] |= best << i;
      /* Evaluate the second possibility for this state. */
      rate0 = tokens[next][0].rate;
      rate1 = tokens[next][1].rate;

      if ((abs(x)*dequant_ptr[rc] > abs(coeff_ptr[rc])) &&
          (abs(x)*dequant_ptr[rc] < abs(coeff_ptr[rc]) + dequant_ptr[rc]))
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
        t0 = tokens[next][0].token == DCT_EOB_TOKEN ?
             DCT_EOB_TOKEN : ZERO_TOKEN;
        t1 = tokens[next][1].token == DCT_EOB_TOKEN ?
             DCT_EOB_TOKEN : ZERO_TOKEN;
      } else {
        t0 = t1 = (vp8_dct_value_tokens_ptr + x)->Token;
      }
      if (next < 16) {
        band = vp8_coef_bands[i + 1];
        if (t0 != DCT_EOB_TOKEN) {
          pt = vp8_prev_token_class[t0];
          rate0 += mb->token_costs[type][band][pt][
                     tokens[next][0].token];
        }
        if (t1 != DCT_EOB_TOKEN) {
          pt = vp8_prev_token_class[t1];
          rate1 += mb->token_costs[type][band][pt][
                     tokens[next][1].token];
        }
      }

      rd_cost0 = RDCOST(rdmult, rddiv, rate0, error0);
      rd_cost1 = RDCOST(rdmult, rddiv, rate1, error1);
      if (rd_cost0 == rd_cost1) {
        rd_cost0 = RDTRUNC(rdmult, rddiv, rate0, error0);
        rd_cost1 = RDTRUNC(rdmult, rddiv, rate1, error1);
      }
      /* And pick the best. */
      best = rd_cost1 < rd_cost0;
      base_bits = *(vp8_dct_value_cost_ptr + x);

      if (shortcut) {
        dx -= (dequant_ptr[rc] + sz) ^ sz;
        d2 = dx * dx;
      }
      tokens[i][1].rate = base_bits + (best ? rate1 : rate0);
      tokens[i][1].error = d2 + (best ? error1 : error0);
      tokens[i][1].next = next;
      tokens[i][1].token = best ? t1 : t0;
      tokens[i][1].qc = x;
      best_mask[1] |= best << i;
      /* Finally, make this the new head of the trellis. */
      next = i;
    }
    /* There's no choice to make for a zero coefficient, so we don't
     *  add a new trellis node, but we do need to update the costs.
     */
    else {
      band = vp8_coef_bands[i + 1];
      t0 = tokens[next][0].token;
      t1 = tokens[next][1].token;
      /* Update the cost of each path if we're past the EOB token. */
      if (t0 != DCT_EOB_TOKEN) {
        tokens[next][0].rate += mb->token_costs[type][band][0][t0];
        tokens[next][0].token = ZERO_TOKEN;
      }
      if (t1 != DCT_EOB_TOKEN) {
        tokens[next][1].rate += mb->token_costs[type][band][0][t1];
        tokens[next][1].token = ZERO_TOKEN;
      }
      /* Don't update next, because we didn't add a new node. */
    }
  }

  /* Now pick the best path through the whole trellis. */
  band = vp8_coef_bands[i + 1];
  VP8_COMBINEENTROPYCONTEXTS(pt, *a, *l);
  rate0 = tokens[next][0].rate;
  rate1 = tokens[next][1].rate;
  error0 = tokens[next][0].error;
  error1 = tokens[next][1].error;
  t0 = tokens[next][0].token;
  t1 = tokens[next][1].token;
  rate0 += mb->token_costs[type][band][pt][t0];
  rate1 += mb->token_costs[type][band][pt][t1];
  rd_cost0 = RDCOST(rdmult, rddiv, rate0, error0);
  rd_cost1 = RDCOST(rdmult, rddiv, rate1, error1);
  if (rd_cost0 == rd_cost1) {
    rd_cost0 = RDTRUNC(rdmult, rddiv, rate0, error0);
    rd_cost1 = RDTRUNC(rdmult, rddiv, rate1, error1);
  }
  best = rd_cost1 < rd_cost0;
  final_eob = i0 - 1;
  for (i = next; i < eob; i = next) {
    x = tokens[i][best].qc;
    if (x)
      final_eob = i;
    rc = vp8_default_zig_zag1d[i];
    qcoeff_ptr[rc] = x;
    dqcoeff_ptr[rc] = x * dequant_ptr[rc];
    next = tokens[i][best].next;
    best = (best_mask[best] >> i) & 1;
  }
  final_eob++;

  d->eob = final_eob;
  *a = *l = (d->eob != !type);
}

/**************************************************************************
our inverse hadamard transform effectively is weighted sum of all 16 inputs
with weight either 1 or -1. It has a last stage scaling of (sum+1)>>2. And
dc only idct is (dc+16)>>5. So if all the sums are between -65 and 63 the
output after inverse wht and idct will be all zero. A sum of absolute value
smaller than 65 guarantees all 16 different (+1/-1) weighted sums in wht
fall between -65 and +65.
**************************************************************************/
#define SUM_2ND_COEFF_THRESH 65

static void check_reset_2nd_coeffs(MACROBLOCKD *x, int type,
                                   ENTROPY_CONTEXT *a, ENTROPY_CONTEXT *l) {
  int sum = 0;
  int i;
  BLOCKD *bd = &x->block[24];
  if (bd->dequant[0] >= SUM_2ND_COEFF_THRESH
      && bd->dequant[1] >= SUM_2ND_COEFF_THRESH)
    return;

  for (i = 0; i < bd->eob; i++) {
    int coef = bd->dqcoeff[vp8_default_zig_zag1d[i]];
    sum += (coef >= 0) ? coef : -coef;
    if (sum >= SUM_2ND_COEFF_THRESH)
      return;
  }

  if (sum < SUM_2ND_COEFF_THRESH) {
    for (i = 0; i < bd->eob; i++) {
      int rc = vp8_default_zig_zag1d[i];
      bd->qcoeff[rc] = 0;
      bd->dqcoeff[rc] = 0;
    }
    bd->eob = 0;
    *a = *l = (bd->eob != !type);
  }
}
#define SUM_2ND_COEFF_THRESH_8X8 32
static void check_reset_8x8_2nd_coeffs(MACROBLOCKD *x, int type,
                                       ENTROPY_CONTEXT *a, ENTROPY_CONTEXT *l) {
  int sum = 0;
  BLOCKD *bd = &x->block[24];
  int coef;

  coef = bd->dqcoeff[0];
  sum += (coef >= 0) ? coef : -coef;
  coef = bd->dqcoeff[1];
  sum += (coef >= 0) ? coef : -coef;
  coef = bd->dqcoeff[4];
  sum += (coef >= 0) ? coef : -coef;
  coef = bd->dqcoeff[8];
  sum += (coef >= 0) ? coef : -coef;

  if (sum < SUM_2ND_COEFF_THRESH_8X8) {
    bd->qcoeff[0] = 0;
    bd->dqcoeff[0] = 0;
    bd->qcoeff[1] = 0;
    bd->dqcoeff[1] = 0;
    bd->qcoeff[4] = 0;
    bd->dqcoeff[4] = 0;
    bd->qcoeff[8] = 0;
    bd->dqcoeff[8] = 0;
    bd->eob = 0;
    *a = *l = (bd->eob != !type);
  }
}



static void optimize_mb(MACROBLOCK *x, const VP8_ENCODER_RTCD *rtcd) {
  int b;
  int type;
  int has_2nd_order;
  ENTROPY_CONTEXT_PLANES t_above, t_left;
  ENTROPY_CONTEXT *ta;
  ENTROPY_CONTEXT *tl;

  vpx_memcpy(&t_above, x->e_mbd.above_context, sizeof(ENTROPY_CONTEXT_PLANES));
  vpx_memcpy(&t_left, x->e_mbd.left_context, sizeof(ENTROPY_CONTEXT_PLANES));

  ta = (ENTROPY_CONTEXT *)&t_above;
  tl = (ENTROPY_CONTEXT *)&t_left;

  has_2nd_order = (x->e_mbd.mode_info_context->mbmi.mode != B_PRED
                   && x->e_mbd.mode_info_context->mbmi.mode != I8X8_PRED
                   && x->e_mbd.mode_info_context->mbmi.mode != SPLITMV);
  type = has_2nd_order ? PLANE_TYPE_Y_NO_DC : PLANE_TYPE_Y_WITH_DC;

  for (b = 0; b < 16; b++) {
    optimize_b(x, b, type,
               ta + vp8_block2above[b], tl + vp8_block2left[b], rtcd);
  }

  for (b = 16; b < 24; b++) {
    optimize_b(x, b, PLANE_TYPE_UV,
               ta + vp8_block2above[b], tl + vp8_block2left[b], rtcd);
  }

  if (has_2nd_order) {
    b = 24;
    optimize_b(x, b, PLANE_TYPE_Y2,
               ta + vp8_block2above[b], tl + vp8_block2left[b], rtcd);
    check_reset_2nd_coeffs(&x->e_mbd, PLANE_TYPE_Y2,
                           ta + vp8_block2above[b], tl + vp8_block2left[b]);
  }
}


void vp8_optimize_mby(MACROBLOCK *x, const VP8_ENCODER_RTCD *rtcd) {
  int b;
  int type;
  int has_2nd_order;

  ENTROPY_CONTEXT_PLANES t_above, t_left;
  ENTROPY_CONTEXT *ta;
  ENTROPY_CONTEXT *tl;

  if (!x->e_mbd.above_context)
    return;

  if (!x->e_mbd.left_context)
    return;

  vpx_memcpy(&t_above, x->e_mbd.above_context, sizeof(ENTROPY_CONTEXT_PLANES));
  vpx_memcpy(&t_left, x->e_mbd.left_context, sizeof(ENTROPY_CONTEXT_PLANES));

  ta = (ENTROPY_CONTEXT *)&t_above;
  tl = (ENTROPY_CONTEXT *)&t_left;

  has_2nd_order = (x->e_mbd.mode_info_context->mbmi.mode != B_PRED
                   && x->e_mbd.mode_info_context->mbmi.mode != I8X8_PRED
                   && x->e_mbd.mode_info_context->mbmi.mode != SPLITMV);
  type = has_2nd_order ? PLANE_TYPE_Y_NO_DC : PLANE_TYPE_Y_WITH_DC;

  for (b = 0; b < 16; b++) {
    optimize_b(x, b, type,
               ta + vp8_block2above[b], tl + vp8_block2left[b], rtcd);
  }


  if (has_2nd_order) {
    b = 24;
    optimize_b(x, b, PLANE_TYPE_Y2,
               ta + vp8_block2above[b], tl + vp8_block2left[b], rtcd);
    check_reset_2nd_coeffs(&x->e_mbd, PLANE_TYPE_Y2,
                           ta + vp8_block2above[b], tl + vp8_block2left[b]);
  }
}

void vp8_optimize_mbuv(MACROBLOCK *x, const VP8_ENCODER_RTCD *rtcd) {
  int b;
  ENTROPY_CONTEXT_PLANES t_above, t_left;
  ENTROPY_CONTEXT *ta;
  ENTROPY_CONTEXT *tl;

  if (!x->e_mbd.above_context)
    return;

  if (!x->e_mbd.left_context)
    return;

  vpx_memcpy(&t_above, x->e_mbd.above_context, sizeof(ENTROPY_CONTEXT_PLANES));
  vpx_memcpy(&t_left, x->e_mbd.left_context, sizeof(ENTROPY_CONTEXT_PLANES));

  ta = (ENTROPY_CONTEXT *)&t_above;
  tl = (ENTROPY_CONTEXT *)&t_left;

  for (b = 16; b < 24; b++) {
    optimize_b(x, b, PLANE_TYPE_UV,
               ta + vp8_block2above[b], tl + vp8_block2left[b], rtcd);
  }
}

void optimize_b_8x8(MACROBLOCK *mb, int i, int type,
                    ENTROPY_CONTEXT *a, ENTROPY_CONTEXT *l,
                    const VP8_ENCODER_RTCD *rtcd) {
  BLOCK *b;
  BLOCKD *d;
  vp8_token_state tokens[65][2];
  unsigned best_mask[2];
  const short *dequant_ptr;
  const short *coeff_ptr;
  short *qcoeff_ptr;
  short *dqcoeff_ptr;
  int eob;
  int i0;
  int rc;
  int x;
  int sz = 0;
  int next;
  int rdmult;
  int rddiv;
  int final_eob;
  int rd_cost0;
  int rd_cost1;
  int rate0;
  int rate1;
  int error0;
  int error1;
  int t0;
  int t1;
  int best;
  int band;
  int pt;
  int err_mult = plane_rd_mult[type];

  b = &mb->block[i];
  d = &mb->e_mbd.block[i];

  dequant_ptr = d->dequant;
  coeff_ptr = b->coeff;
  qcoeff_ptr = d->qcoeff;
  dqcoeff_ptr = d->dqcoeff;
  i0 = !type;
  eob = d->eob;

  /* Now set up a Viterbi trellis to evaluate alternative roundings. */
  rdmult = mb->rdmult * err_mult;
  if (mb->e_mbd.mode_info_context->mbmi.ref_frame == INTRA_FRAME)
    rdmult = (rdmult * 9) >> 4;
  rddiv = mb->rddiv;
  best_mask[0] = best_mask[1] = 0;
  /* Initialize the sentinel node of the trellis. */
  tokens[eob][0].rate = 0;
  tokens[eob][0].error = 0;
  tokens[eob][0].next = 64;
  tokens[eob][0].token = DCT_EOB_TOKEN;
  tokens[eob][0].qc = 0;
  *(tokens[eob] + 1) = *(tokens[eob] + 0);
  next = eob;
  for (i = eob; i-- > i0;) {
    int base_bits;
    int d2;
    int dx;

    rc = vp8_default_zig_zag1d_8x8[i];
    x = qcoeff_ptr[rc];
    /* Only add a trellis state for non-zero coefficients. */
    if (x) {
      int shortcut = 0;
      error0 = tokens[next][0].error;
      error1 = tokens[next][1].error;
      /* Evaluate the first possibility for this state. */
      rate0 = tokens[next][0].rate;
      rate1 = tokens[next][1].rate;
      t0 = (vp8_dct_value_tokens_ptr + x)->Token;
      /* Consider both possible successor states. */
      if (next < 64) {
        band = vp8_coef_bands_8x8[i + 1];
        pt = vp8_prev_token_class[t0];
        rate0 +=
          mb->token_costs_8x8[type][band][pt][tokens[next][0].token];
        rate1 +=
          mb->token_costs_8x8[type][band][pt][tokens[next][1].token];
      }
      rd_cost0 = RDCOST_8x8(rdmult, rddiv, rate0, error0);
      rd_cost1 = RDCOST_8x8(rdmult, rddiv, rate1, error1);
      if (rd_cost0 == rd_cost1) {
        rd_cost0 = RDTRUNC_8x8(rdmult, rddiv, rate0, error0);
        rd_cost1 = RDTRUNC_8x8(rdmult, rddiv, rate1, error1);
      }
      /* And pick the best. */
      best = rd_cost1 < rd_cost0;
      base_bits = *(vp8_dct_value_cost_ptr + x);
      dx = dqcoeff_ptr[rc] - coeff_ptr[rc];
      d2 = dx * dx;
      tokens[i][0].rate = base_bits + (best ? rate1 : rate0);
      tokens[i][0].error = d2 + (best ? error1 : error0);
      tokens[i][0].next = next;
      tokens[i][0].token = t0;
      tokens[i][0].qc = x;
      best_mask[0] |= best << i;
      /* Evaluate the second possibility for this state. */
      rate0 = tokens[next][0].rate;
      rate1 = tokens[next][1].rate;

      if ((abs(x)*dequant_ptr[rc != 0] > abs(coeff_ptr[rc])) &&
          (abs(x)*dequant_ptr[rc != 0] < abs(coeff_ptr[rc]) + dequant_ptr[rc != 0]))
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
        t0 = tokens[next][0].token == DCT_EOB_TOKEN ?
             DCT_EOB_TOKEN : ZERO_TOKEN;
        t1 = tokens[next][1].token == DCT_EOB_TOKEN ?
             DCT_EOB_TOKEN : ZERO_TOKEN;
      } else {
        t0 = t1 = (vp8_dct_value_tokens_ptr + x)->Token;
      }
      if (next < 64) {
        band = vp8_coef_bands_8x8[i + 1];
        if (t0 != DCT_EOB_TOKEN) {
          pt = vp8_prev_token_class[t0];
          rate0 += mb->token_costs_8x8[type][band][pt][
                     tokens[next][0].token];
        }
        if (t1 != DCT_EOB_TOKEN) {
          pt = vp8_prev_token_class[t1];
          rate1 += mb->token_costs_8x8[type][band][pt][
                     tokens[next][1].token];
        }
      }

      rd_cost0 = RDCOST_8x8(rdmult, rddiv, rate0, error0);
      rd_cost1 = RDCOST_8x8(rdmult, rddiv, rate1, error1);
      if (rd_cost0 == rd_cost1) {
        rd_cost0 = RDTRUNC_8x8(rdmult, rddiv, rate0, error0);
        rd_cost1 = RDTRUNC_8x8(rdmult, rddiv, rate1, error1);
      }
      /* And pick the best. */
      best = rd_cost1 < rd_cost0;
      base_bits = *(vp8_dct_value_cost_ptr + x);

      if (shortcut) {
        dx -= (dequant_ptr[rc != 0] + sz) ^ sz;
        d2 = dx * dx;
      }
      tokens[i][1].rate = base_bits + (best ? rate1 : rate0);
      tokens[i][1].error = d2 + (best ? error1 : error0);
      tokens[i][1].next = next;
      tokens[i][1].token = best ? t1 : t0;
      tokens[i][1].qc = x;
      best_mask[1] |= best << i;
      /* Finally, make this the new head of the trellis. */
      next = i;
    }
    /* There's no choice to make for a zero coefficient, so we don't
     *  add a new trellis node, but we do need to update the costs.
     */
    else {
      band = vp8_coef_bands_8x8[i + 1];
      t0 = tokens[next][0].token;
      t1 = tokens[next][1].token;
      /* Update the cost of each path if we're past the EOB token. */
      if (t0 != DCT_EOB_TOKEN) {
        tokens[next][0].rate += mb->token_costs_8x8[type][band][0][t0];
        tokens[next][0].token = ZERO_TOKEN;
      }
      if (t1 != DCT_EOB_TOKEN) {
        tokens[next][1].rate += mb->token_costs_8x8[type][band][0][t1];
        tokens[next][1].token = ZERO_TOKEN;
      }
      /* Don't update next, because we didn't add a new node. */
    }
  }

  /* Now pick the best path through the whole trellis. */
  band = vp8_coef_bands_8x8[i + 1];
  VP8_COMBINEENTROPYCONTEXTS(pt, *a, *l);
  rate0 = tokens[next][0].rate;
  rate1 = tokens[next][1].rate;
  error0 = tokens[next][0].error;
  error1 = tokens[next][1].error;
  t0 = tokens[next][0].token;
  t1 = tokens[next][1].token;
  rate0 += mb->token_costs_8x8[type][band][pt][t0];
  rate1 += mb->token_costs_8x8[type][band][pt][t1];
  rd_cost0 = RDCOST_8x8(rdmult, rddiv, rate0, error0);
  rd_cost1 = RDCOST_8x8(rdmult, rddiv, rate1, error1);
  if (rd_cost0 == rd_cost1) {
    rd_cost0 = RDTRUNC_8x8(rdmult, rddiv, rate0, error0);
    rd_cost1 = RDTRUNC_8x8(rdmult, rddiv, rate1, error1);
  }
  best = rd_cost1 < rd_cost0;
  final_eob = i0 - 1;
  for (i = next; i < eob; i = next) {
    x = tokens[i][best].qc;
    if (x)
      final_eob = i;
    rc = vp8_default_zig_zag1d_8x8[i];
    qcoeff_ptr[rc] = x;
    dqcoeff_ptr[rc] = (x * dequant_ptr[rc != 0]);

    next = tokens[i][best].next;
    best = (best_mask[best] >> i) & 1;
  }
  final_eob++;

  d->eob = final_eob;
  *a = *l = (d->eob != !type);

}

void optimize_mb_8x8(MACROBLOCK *x, const VP8_ENCODER_RTCD *rtcd) {
  int b;
  int type;
  ENTROPY_CONTEXT_PLANES t_above, t_left;
  ENTROPY_CONTEXT *ta;
  ENTROPY_CONTEXT *tl;

  vpx_memcpy(&t_above, x->e_mbd.above_context, sizeof(ENTROPY_CONTEXT_PLANES));
  vpx_memcpy(&t_left, x->e_mbd.left_context, sizeof(ENTROPY_CONTEXT_PLANES));

  ta = (ENTROPY_CONTEXT *)&t_above;
  tl = (ENTROPY_CONTEXT *)&t_left;

  type = 0;
  for (b = 0; b < 16; b += 4) {
    optimize_b_8x8(x, b, type,
                   ta + vp8_block2above_8x8[b], tl + vp8_block2left_8x8[b],
                   rtcd);
    *(ta + vp8_block2above_8x8[b] + 1) = *(ta + vp8_block2above_8x8[b]);
    *(tl + vp8_block2left_8x8[b] + 1)  = *(tl + vp8_block2left_8x8[b]);
  }

  for (b = 16; b < 24; b += 4) {
    optimize_b_8x8(x, b, PLANE_TYPE_UV,
                   ta + vp8_block2above_8x8[b], tl + vp8_block2left_8x8[b],
                   rtcd);
    *(ta + vp8_block2above_8x8[b] + 1) = *(ta + vp8_block2above_8x8[b]);
    *(tl + vp8_block2left_8x8[b] + 1) = *(tl + vp8_block2left_8x8[b]);
  }

  // 8x8 always have 2nd roder haar block
  check_reset_8x8_2nd_coeffs(&x->e_mbd, PLANE_TYPE_Y2,
                             ta + vp8_block2above_8x8[24], tl + vp8_block2left_8x8[24]);

}

void vp8_optimize_mby_8x8(MACROBLOCK *x, const VP8_ENCODER_RTCD *rtcd) {
  int b;
  int type;

  ENTROPY_CONTEXT_PLANES t_above, t_left;
  ENTROPY_CONTEXT *ta;
  ENTROPY_CONTEXT *tl;


  if (!x->e_mbd.above_context)
    return;

  if (!x->e_mbd.left_context)
    return;

  vpx_memcpy(&t_above, x->e_mbd.above_context, sizeof(ENTROPY_CONTEXT_PLANES));
  vpx_memcpy(&t_left, x->e_mbd.left_context, sizeof(ENTROPY_CONTEXT_PLANES));

  ta = (ENTROPY_CONTEXT *)&t_above;
  tl = (ENTROPY_CONTEXT *)&t_left;
  type = 0;
  for (b = 0; b < 16; b += 4) {
    optimize_b_8x8(x, b, type,
                   ta + vp8_block2above[b], tl + vp8_block2left[b],
                   rtcd);
    *(ta + vp8_block2above_8x8[b] + 1) = *(ta + vp8_block2above_8x8[b]);
    *(tl + vp8_block2left_8x8[b] + 1)  = *(tl + vp8_block2left_8x8[b]);
  }
  // 8x8 always have 2nd roder haar block
  check_reset_8x8_2nd_coeffs(&x->e_mbd, PLANE_TYPE_Y2,
                             ta + vp8_block2above_8x8[24], tl + vp8_block2left_8x8[24]);

}

void vp8_optimize_mbuv_8x8(MACROBLOCK *x, const VP8_ENCODER_RTCD *rtcd) {
  int b;
  ENTROPY_CONTEXT_PLANES t_above, t_left;
  ENTROPY_CONTEXT *ta;
  ENTROPY_CONTEXT *tl;

  if (!x->e_mbd.above_context)
    return;

  if (!x->e_mbd.left_context)
    return;

  vpx_memcpy(&t_above, x->e_mbd.above_context, sizeof(ENTROPY_CONTEXT_PLANES));
  vpx_memcpy(&t_left, x->e_mbd.left_context, sizeof(ENTROPY_CONTEXT_PLANES));

  ta = (ENTROPY_CONTEXT *)&t_above;
  tl = (ENTROPY_CONTEXT *)&t_left;

  for (b = 16; b < 24; b += 4) {
    optimize_b_8x8(x, b, PLANE_TYPE_UV,
                   ta + vp8_block2above_8x8[b], tl + vp8_block2left_8x8[b],
                   rtcd);
    *(ta + vp8_block2above_8x8[b] + 1) = *(ta + vp8_block2above_8x8[b]);
    *(tl + vp8_block2left_8x8[b] + 1) = *(tl + vp8_block2left_8x8[b]);
  }

}

void vp8_encode_inter16x16(const VP8_ENCODER_RTCD *rtcd, MACROBLOCK *x) {
  int tx_type = x->e_mbd.mode_info_context->mbmi.txfm_size;
  vp8_build_inter_predictors_mb(&x->e_mbd);

  vp8_subtract_mb(rtcd, x);

  if (tx_type == TX_8X8)
    vp8_transform_mb_8x8(x);
  else
    transform_mb(x);

  if (tx_type == TX_8X8)
    vp8_quantize_mb_8x8(x);
  else
    vp8_quantize_mb(x);

  if (x->optimize) {
    if (tx_type == TX_8X8)
      optimize_mb_8x8(x, rtcd);
    else
      optimize_mb(x, rtcd);
  }

  if (tx_type == TX_8X8)
    vp8_inverse_transform_mb_8x8(IF_RTCD(&rtcd->common->idct), &x->e_mbd);
  else
    vp8_inverse_transform_mb(IF_RTCD(&rtcd->common->idct), &x->e_mbd);

  if (tx_type == TX_8X8) {
#ifdef ENC_DEBUG
    if (enc_debug) {
      int i;
      printf("qcoeff:\n");
      printf("%d %d:\n", x->e_mbd.mb_to_left_edge, x->e_mbd.mb_to_top_edge);
      for (i = 0; i < 400; i++) {
        printf("%3d ", x->e_mbd.qcoeff[i]);
        if (i % 16 == 15) printf("\n");
      }
      printf("dqcoeff:\n");
      for (i = 0; i < 400; i++) {
        printf("%3d ", x->e_mbd.dqcoeff[i]);
        if (i % 16 == 15) printf("\n");
      }
      printf("diff:\n");
      for (i = 0; i < 400; i++) {
        printf("%3d ", x->e_mbd.diff[i]);
        if (i % 16 == 15) printf("\n");
      }
      printf("predictor:\n");
      for (i = 0; i < 400; i++) {
        printf("%3d ", x->e_mbd.predictor[i]);
        if (i % 16 == 15) printf("\n");
      }
      printf("\n");
    }
#endif
  }

  RECON_INVOKE(&rtcd->common->recon, recon_mb)
  (IF_RTCD(&rtcd->common->recon), &x->e_mbd);
#ifdef ENC_DEBUG
  if (enc_debug) {
    int i, j, k;
    printf("Final Reconstruction\n");
    for (i = 0; i < 16; i += 4) {
      BLOCKD *b = &x->e_mbd.block[i];
      unsigned char *d = *(b->base_dst) + b->dst;
      for (k = 0; k < 4; k++) {
        for (j = 0; j < 16; j++)
          printf("%3d ", d[j]);
        printf("\n");
        d += b->dst_stride;
      }
    }
  }
#endif
}


/* this function is used by first pass only */
void vp8_encode_inter16x16y(const VP8_ENCODER_RTCD *rtcd, MACROBLOCK *x) {
  int tx_type = x->e_mbd.mode_info_context->mbmi.txfm_size;

  BLOCK *b = &x->block[0];

#if CONFIG_PRED_FILTER
  // Disable the prediction filter for firstpass
  x->e_mbd.mode_info_context->mbmi.pred_filter_enabled = 0;
#endif

  vp8_build_inter16x16_predictors_mby(&x->e_mbd);

  ENCODEMB_INVOKE(&rtcd->encodemb, submby)(x->src_diff, *(b->base_src), x->e_mbd.predictor, b->src_stride);

  if (tx_type == TX_8X8)
    vp8_transform_mby_8x8(x);
  else
    transform_mby(x);

  vp8_quantize_mby(x);

  if (tx_type == TX_8X8)
    vp8_inverse_transform_mby_8x8(IF_RTCD(&rtcd->common->idct), &x->e_mbd);
  else
    vp8_inverse_transform_mby(IF_RTCD(&rtcd->common->idct), &x->e_mbd);

  RECON_INVOKE(&rtcd->common->recon, recon_mby)
  (IF_RTCD(&rtcd->common->recon), &x->e_mbd);
}
