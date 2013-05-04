/*
 *  Copyright (c) 2010 The WebM project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */


#include <stdio.h>
#include <math.h>
#include <limits.h>
#include <assert.h>

#include "vp9/common/vp9_pragmas.h"
#include "vp9/encoder/vp9_tokenize.h"
#include "vp9/encoder/vp9_treewriter.h"
#include "vp9/encoder/vp9_onyx_int.h"
#include "vp9/encoder/vp9_modecosts.h"
#include "vp9/encoder/vp9_encodeintra.h"
#include "vp9/common/vp9_entropymode.h"
#include "vp9/common/vp9_reconinter.h"
#include "vp9/common/vp9_reconintra.h"
#include "vp9/common/vp9_findnearmv.h"
#include "vp9/common/vp9_quant_common.h"
#include "vp9/encoder/vp9_encodemb.h"
#include "vp9/encoder/vp9_quantize.h"
#include "vp9/encoder/vp9_variance.h"
#include "vp9/encoder/vp9_mcomp.h"
#include "vp9/encoder/vp9_rdopt.h"
#include "vp9/encoder/vp9_ratectrl.h"
#include "vpx_mem/vpx_mem.h"
#include "vp9/common/vp9_systemdependent.h"
#include "vp9/encoder/vp9_encodemv.h"
#include "vp9/common/vp9_seg_common.h"
#include "vp9/common/vp9_pred_common.h"
#include "vp9/common/vp9_entropy.h"
#include "vp9_rtcd.h"
#include "vp9/common/vp9_mvref_common.h"
#include "vp9/common/vp9_common.h"

#define INVALID_MV 0x80008000

/* Factor to weigh the rate for switchable interp filters */
#define SWITCHABLE_INTERP_RATE_FACTOR 1

static const int auto_speed_thresh[17] = {
  1000,
  200,
  150,
  130,
  150,
  125,
  120,
  115,
  115,
  115,
  115,
  115,
  115,
  115,
  115,
  115,
  105
};

const MODE_DEFINITION vp9_mode_order[MAX_MODES] = {
  {ZEROMV,    LAST_FRAME,   NONE},
  {DC_PRED,   INTRA_FRAME,  NONE},

  {NEARESTMV, LAST_FRAME,   NONE},
  {NEARMV,    LAST_FRAME,   NONE},

  {ZEROMV,    GOLDEN_FRAME, NONE},
  {NEARESTMV, GOLDEN_FRAME, NONE},

  {ZEROMV,    ALTREF_FRAME, NONE},
  {NEARESTMV, ALTREF_FRAME, NONE},

  {NEARMV,    GOLDEN_FRAME, NONE},
  {NEARMV,    ALTREF_FRAME, NONE},

  {V_PRED,    INTRA_FRAME,  NONE},
  {H_PRED,    INTRA_FRAME,  NONE},
  {D45_PRED,  INTRA_FRAME,  NONE},
  {D135_PRED, INTRA_FRAME,  NONE},
  {D117_PRED, INTRA_FRAME,  NONE},
  {D153_PRED, INTRA_FRAME,  NONE},
  {D27_PRED,  INTRA_FRAME,  NONE},
  {D63_PRED,  INTRA_FRAME,  NONE},

  {TM_PRED,   INTRA_FRAME,  NONE},

  {NEWMV,     LAST_FRAME,   NONE},
  {NEWMV,     GOLDEN_FRAME, NONE},
  {NEWMV,     ALTREF_FRAME, NONE},

  {SPLITMV,   LAST_FRAME,   NONE},
  {SPLITMV,   GOLDEN_FRAME, NONE},
  {SPLITMV,   ALTREF_FRAME, NONE},

  {I4X4_PRED,    INTRA_FRAME,  NONE},
#if !CONFIG_SB8X8
  {I8X8_PRED, INTRA_FRAME,  NONE},
#endif

  /* compound prediction modes */
  {ZEROMV,    LAST_FRAME,   GOLDEN_FRAME},
  {NEARESTMV, LAST_FRAME,   GOLDEN_FRAME},
  {NEARMV,    LAST_FRAME,   GOLDEN_FRAME},

  {ZEROMV,    ALTREF_FRAME, LAST_FRAME},
  {NEARESTMV, ALTREF_FRAME, LAST_FRAME},
  {NEARMV,    ALTREF_FRAME, LAST_FRAME},

  {ZEROMV,    GOLDEN_FRAME, ALTREF_FRAME},
  {NEARESTMV, GOLDEN_FRAME, ALTREF_FRAME},
  {NEARMV,    GOLDEN_FRAME, ALTREF_FRAME},

  {NEWMV,     LAST_FRAME,   GOLDEN_FRAME},
  {NEWMV,     ALTREF_FRAME, LAST_FRAME  },
  {NEWMV,     GOLDEN_FRAME, ALTREF_FRAME},

  {SPLITMV,   LAST_FRAME,   GOLDEN_FRAME},
  {SPLITMV,   ALTREF_FRAME, LAST_FRAME  },
  {SPLITMV,   GOLDEN_FRAME, ALTREF_FRAME},

#if CONFIG_COMP_INTERINTRA_PRED
  /* compound inter-intra prediction */
  {ZEROMV,    LAST_FRAME,   INTRA_FRAME},
  {NEARESTMV, LAST_FRAME,   INTRA_FRAME},
  {NEARMV,    LAST_FRAME,   INTRA_FRAME},
  {NEWMV,     LAST_FRAME,   INTRA_FRAME},

  {ZEROMV,    GOLDEN_FRAME,   INTRA_FRAME},
  {NEARESTMV, GOLDEN_FRAME,   INTRA_FRAME},
  {NEARMV,    GOLDEN_FRAME,   INTRA_FRAME},
  {NEWMV,     GOLDEN_FRAME,   INTRA_FRAME},

  {ZEROMV,    ALTREF_FRAME,   INTRA_FRAME},
  {NEARESTMV, ALTREF_FRAME,   INTRA_FRAME},
  {NEARMV,    ALTREF_FRAME,   INTRA_FRAME},
  {NEWMV,     ALTREF_FRAME,   INTRA_FRAME},
#endif
};

static void fill_token_costs(vp9_coeff_count *c,
                             vp9_coeff_probs *p,
                             TX_SIZE tx_size) {
  int i, j, k, l;

  for (i = 0; i < BLOCK_TYPES; i++)
    for (j = 0; j < REF_TYPES; j++)
      for (k = 0; k < COEF_BANDS; k++)
        for (l = 0; l < PREV_COEF_CONTEXTS; l++)
          vp9_cost_tokens_skip((int *)c[i][j][k][l], p[i][j][k][l],
                               vp9_coef_tree);
}

static int rd_iifactor[32] =  { 4, 4, 3, 2, 1, 0, 0, 0,
                                0, 0, 0, 0, 0, 0, 0, 0,
                                0, 0, 0, 0, 0, 0, 0, 0,
                                0, 0, 0, 0, 0, 0, 0, 0, };

// 3* dc_qlookup[Q]*dc_qlookup[Q];

/* values are now correlated to quantizer */
static int sad_per_bit16lut[QINDEX_RANGE];
static int sad_per_bit4lut[QINDEX_RANGE];

void vp9_init_me_luts() {
  int i;

  // Initialize the sad lut tables using a formulaic calculation for now
  // This is to make it easier to resolve the impact of experimental changes
  // to the quantizer tables.
  for (i = 0; i < QINDEX_RANGE; i++) {
    sad_per_bit16lut[i] =
      (int)((0.0418 * vp9_convert_qindex_to_q(i)) + 2.4107);
    sad_per_bit4lut[i] = (int)(0.063 * vp9_convert_qindex_to_q(i) + 2.742);
  }
}

static int compute_rd_mult(int qindex) {
  const int q = vp9_dc_quant(qindex, 0);
  return (11 * q * q) >> 2;
}

void vp9_initialize_me_consts(VP9_COMP *cpi, int qindex) {
  cpi->mb.sadperbit16 = sad_per_bit16lut[qindex];
  cpi->mb.sadperbit4 = sad_per_bit4lut[qindex];
}


void vp9_initialize_rd_consts(VP9_COMP *cpi, int qindex) {
  int q, i;

  vp9_clear_system_state();  // __asm emms;

  // Further tests required to see if optimum is different
  // for key frames, golden frames and arf frames.
  // if (cpi->common.refresh_golden_frame ||
  //     cpi->common.refresh_alt_ref_frame)
  qindex = clamp(qindex, 0, MAXQ);

  cpi->RDMULT = compute_rd_mult(qindex);
  if (cpi->pass == 2 && (cpi->common.frame_type != KEY_FRAME)) {
    if (cpi->twopass.next_iiratio > 31)
      cpi->RDMULT += (cpi->RDMULT * rd_iifactor[31]) >> 4;
    else
      cpi->RDMULT +=
          (cpi->RDMULT * rd_iifactor[cpi->twopass.next_iiratio]) >> 4;
  }
  cpi->mb.errorperbit = cpi->RDMULT >> 6;
  cpi->mb.errorperbit += (cpi->mb.errorperbit == 0);

  vp9_set_speed_features(cpi);

  q = (int)pow(vp9_dc_quant(qindex, 0) >> 2, 1.25);
  q <<= 2;
  if (q < 8)
    q = 8;

  if (cpi->RDMULT > 1000) {
    cpi->RDDIV = 1;
    cpi->RDMULT /= 100;

    for (i = 0; i < MAX_MODES; i++) {
      if (cpi->sf.thresh_mult[i] < INT_MAX) {
        cpi->rd_threshes[i] = cpi->sf.thresh_mult[i] * q / 100;
      } else {
        cpi->rd_threshes[i] = INT_MAX;
      }
      cpi->rd_baseline_thresh[i] = cpi->rd_threshes[i];
    }
  } else {
    cpi->RDDIV = 100;

    for (i = 0; i < MAX_MODES; i++) {
      if (cpi->sf.thresh_mult[i] < (INT_MAX / q)) {
        cpi->rd_threshes[i] = cpi->sf.thresh_mult[i] * q;
      } else {
        cpi->rd_threshes[i] = INT_MAX;
      }
      cpi->rd_baseline_thresh[i] = cpi->rd_threshes[i];
    }
  }

  fill_token_costs(cpi->mb.token_costs[TX_4X4],
                   cpi->common.fc.coef_probs_4x4, TX_4X4);
  fill_token_costs(cpi->mb.token_costs[TX_8X8],
                   cpi->common.fc.coef_probs_8x8, TX_8X8);
  fill_token_costs(cpi->mb.token_costs[TX_16X16],
                   cpi->common.fc.coef_probs_16x16, TX_16X16);
  fill_token_costs(cpi->mb.token_costs[TX_32X32],
                   cpi->common.fc.coef_probs_32x32, TX_32X32);

  for (i = 0; i < NUM_PARTITION_CONTEXTS; i++)
    vp9_cost_tokens(cpi->mb.partition_cost[i],
                    cpi->common.fc.partition_prob[i],
                    vp9_partition_tree);

  /*rough estimate for costing*/
  cpi->common.kf_ymode_probs_index = cpi->common.base_qindex >> 4;
  vp9_init_mode_costs(cpi);

  if (cpi->common.frame_type != KEY_FRAME) {
    vp9_build_nmv_cost_table(
        cpi->mb.nmvjointcost,
        cpi->mb.e_mbd.allow_high_precision_mv ?
        cpi->mb.nmvcost_hp : cpi->mb.nmvcost,
        &cpi->common.fc.nmvc,
        cpi->mb.e_mbd.allow_high_precision_mv, 1, 1);
  }
}

int vp9_block_error_c(int16_t *coeff, int16_t *dqcoeff, int block_size) {
  int i, error = 0;

  for (i = 0; i < block_size; i++) {
    int this_diff = coeff[i] - dqcoeff[i];
    error += this_diff * this_diff;
  }

  return error;
}

static INLINE int cost_coeffs(VP9_COMMON *const cm, MACROBLOCK *mb,
                              int plane, int block, PLANE_TYPE type,
                              ENTROPY_CONTEXT *A,
                              ENTROPY_CONTEXT *L,
                              TX_SIZE tx_size,
                              int y_blocks) {
  MACROBLOCKD *const xd = &mb->e_mbd;
  MB_MODE_INFO *mbmi = &xd->mode_info_context->mbmi;
  int pt;
  int c = 0;
  int cost = 0, pad;
  const int *scan, *nb;
  const int eob = xd->plane[plane].eobs[block];
  const int16_t *qcoeff_ptr = BLOCK_OFFSET(xd->plane[plane].qcoeff,
                                           block, 16);
  const int ref = mbmi->ref_frame != INTRA_FRAME;
  unsigned int (*token_costs)[PREV_COEF_CONTEXTS][MAX_ENTROPY_TOKENS] =
      mb->token_costs[tx_size][type][ref];
  ENTROPY_CONTEXT above_ec, left_ec;
  TX_TYPE tx_type = DCT_DCT;

#if CONFIG_CODE_ZEROGROUP
  int last_nz_pos[3] = {-1, -1, -1};  // Encoder only
  int is_eoo_list[3] = {0, 0, 0};
  int is_eoo_negative[3] = {0, 0, 0};
  int is_last_zero[3] = {0, 0, 0};
  int o, rc, skip_coef_val;
  vp9_zpc_probs *zpc_probs;
  uint8_t token_cache_full[1024];
#endif
  const int segment_id = xd->mode_info_context->mbmi.segment_id;
  vp9_prob (*coef_probs)[REF_TYPES][COEF_BANDS][PREV_COEF_CONTEXTS]
                        [ENTROPY_NODES];
  int seg_eob, default_eob;
  uint8_t token_cache[1024];

#if CONFIG_CODE_ZEROGROUP
  vpx_memset(token_cache, UNKNOWN_TOKEN, sizeof(token_cache));
#endif

  // Check for consistency of tx_size with mode info
  assert((!type && !plane) || (type && plane));
  if (type == PLANE_TYPE_Y_WITH_DC) {
    assert(xd->mode_info_context->mbmi.txfm_size == tx_size);
  } else {
    TX_SIZE tx_size_uv = get_uv_tx_size(xd);
    assert(tx_size == tx_size_uv);
  }

  switch (tx_size) {
    case TX_4X4: {
      tx_type = (type == PLANE_TYPE_Y_WITH_DC) ?
          get_tx_type_4x4(xd, block) : DCT_DCT;
      above_ec = A[0] != 0;
      left_ec = L[0] != 0;
      coef_probs = cm->fc.coef_probs_4x4;
      seg_eob = 16;
      scan = get_scan_4x4(tx_type);
#if CONFIG_CODE_ZEROGROUP
      zpc_probs = &cm->fc.zpc_probs_4x4;
#endif
      break;
    }
    case TX_8X8: {
      const BLOCK_SIZE_TYPE sb_type = xd->mode_info_context->mbmi.sb_type;
      const int sz = 1 + b_width_log2(sb_type);
      const int x = block & ((1 << sz) - 1), y = block - x;
      TX_TYPE tx_type = (type == PLANE_TYPE_Y_WITH_DC) ?
          get_tx_type_8x8(xd, y + (x >> 1)) : DCT_DCT;
      above_ec = (A[0] + A[1]) != 0;
      left_ec = (L[0] + L[1]) != 0;
      scan = get_scan_8x8(tx_type);
      coef_probs = cm->fc.coef_probs_8x8;
      seg_eob = 64;
#if CONFIG_CODE_ZEROGROUP
      zpc_probs = &cm->fc.zpc_probs_8x8;
#endif
      break;
    }
    case TX_16X16: {
      const BLOCK_SIZE_TYPE sb_type = xd->mode_info_context->mbmi.sb_type;
      const int sz = 2 + b_width_log2(sb_type);
      const int x = block & ((1 << sz) - 1), y = block - x;
      TX_TYPE tx_type = (type == PLANE_TYPE_Y_WITH_DC) ?
          get_tx_type_16x16(xd, y + (x >> 2)) : DCT_DCT;
      scan = get_scan_16x16(tx_type);
      coef_probs = cm->fc.coef_probs_16x16;
      seg_eob = 256;
      above_ec = (A[0] + A[1] + A[2] + A[3]) != 0;
      left_ec = (L[0] + L[1] + L[2] + L[3]) != 0;
#if CONFIG_CODE_ZEROGROUP
      zpc_probs = &cm->fc.zpc_probs_16x16;
#endif
      break;
    }
    case TX_32X32:
      scan = vp9_default_zig_zag1d_32x32;
      coef_probs = cm->fc.coef_probs_32x32;
      seg_eob = 1024;
      above_ec = (A[0] + A[1] + A[2] + A[3] + A[4] + A[5] + A[6] + A[7]) != 0;
      left_ec = (L[0] + L[1] + L[2] + L[3] + L[4] + L[5] + L[6] + L[7]) != 0;

#if CONFIG_CODE_ZEROGROUP
      zpc_probs = &cm->fc.zpc_probs_32x32;
#endif
      break;
    default:
      abort();
      break;
  }
  assert(eob <= seg_eob);

  pt = combine_entropy_contexts(above_ec, left_ec);
  nb = vp9_get_coef_neighbors_handle(scan, &pad);
  default_eob = seg_eob;

  if (vp9_segfeature_active(xd, segment_id, SEG_LVL_SKIP))
    seg_eob = 0;

  /* sanity check to ensure that we do not have spurious non-zero q values */
  if (eob < seg_eob)
    assert(qcoeff_ptr[scan[eob]] == 0);

#if CONFIG_CODE_ZEROGROUP
  vpx_memset(token_cache_full, ZERO_TOKEN, sizeof(token_cache_full));
  for (c = 0; c < eob; ++c) {
    rc = scan[c];
    token_cache_full[rc] = vp9_dct_value_tokens_ptr[qcoeff_ptr[rc]].token;
    o = vp9_get_orientation(rc, tx_size);
    if (qcoeff_ptr[rc] != 0)
      last_nz_pos[o] = c;
  }
#endif
  {
    for (c = 0; c < eob; c++) {
      int v = qcoeff_ptr[scan[c]];
      int t = vp9_dct_value_tokens_ptr[v].token;
      int band = get_coef_band(scan, tx_size, c);
      if (c)
        pt = vp9_get_coef_context(scan, nb, pad, token_cache, c, default_eob);
#if CONFIG_CODE_ZEROGROUP
      rc = scan[c];
      o = vp9_get_orientation(rc, tx_size);
      skip_coef_val = (token_cache[rc] == ZERO_TOKEN || is_eoo_list[o]);
      if (!skip_coef_val) {
        cost += token_costs[band][pt][t] + vp9_dct_value_cost_ptr[v];
      } else {
        assert(v == 0);
      }
#else
      cost += token_costs[band][pt][t] + vp9_dct_value_cost_ptr[v];
#endif
      if (!c || token_cache[scan[c - 1]])
        cost += vp9_cost_bit(coef_probs[type][ref][band][pt][0], 1);
      token_cache[scan[c]] = t;
#if CONFIG_CODE_ZEROGROUP
      if (t == ZERO_TOKEN && !skip_coef_val) {
        int eoo = 0, use_eoo;
#if USE_ZPC_EOORIENT == 1
        use_eoo = vp9_use_eoo(c, seg_eob, scan, tx_size,
                              is_last_zero, is_eoo_list);
#else
        use_eoo = 0;
#endif
        if (use_eoo) {
          eoo = vp9_is_eoo(c, eob, scan, tx_size, qcoeff_ptr, last_nz_pos);
          if (eoo && is_eoo_negative[o]) eoo = 0;
          if (eoo) {
            int c_;
            int savings = 0;
            int zsaved = 0;
            savings = vp9_cost_bit((*zpc_probs)[ref]
                                   [coef_to_zpc_band(band)]
                                   [coef_to_zpc_ptok(pt)][0], 1) -
                      vp9_cost_bit((*zpc_probs)[ref]
                                   [coef_to_zpc_band(band)]
                                   [coef_to_zpc_ptok(pt)][0], 0);
            for (c_ = c + 1; c_ < eob; ++c_) {
              if (o == vp9_get_orientation(scan[c_], tx_size)) {
                int pt_ = vp9_get_coef_context(scan, nb, pad,
                                               token_cache_full, c_,
                                               default_eob);
                int band_ = get_coef_band(scan, tx_size, c_);
                assert(token_cache_full[scan[c_]] == ZERO_TOKEN);
                if (!c_ || token_cache_full[scan[c_ - 1]])
                  savings += vp9_cost_bit(
                      coef_probs[type][ref][band_][pt_][0], 1);
                savings += vp9_cost_bit(
                    coef_probs[type][ref][band_][pt_][1], 0);
                zsaved++;
              }
            }
            if (savings < 0) {
            // if (zsaved < ZPC_ZEROSSAVED_EOO) {
              eoo = 0;
              is_eoo_negative[o] = 1;
            }
          }
        }
        if (use_eoo) {
          cost += vp9_cost_bit((*zpc_probs)[ref]
                                           [coef_to_zpc_band(band)]
                                           [coef_to_zpc_ptok(pt)][0], !eoo);
          if (eoo) {
            assert(is_eoo_list[o] == 0);
            is_eoo_list[o] = 1;
          }
        }
      }
      is_last_zero[o] = (t == ZERO_TOKEN);
#endif
    }
    if (c < seg_eob) {
      if (c)
        pt = vp9_get_coef_context(scan, nb, pad, token_cache, c, default_eob);
      cost += mb->token_costs[tx_size][type][ref]
          [get_coef_band(scan, tx_size, c)]
          [pt][DCT_EOB_TOKEN];
    }
  }

  // is eob first coefficient;
  for (pt = 0; pt < (1 << tx_size); pt++) {
    A[pt] = L[pt] = c > 0;
  }

  return cost;
}

static void choose_txfm_size_from_rd(VP9_COMP *cpi, MACROBLOCK *x,
                                     int (*r)[2], int *rate,
                                     int *d, int *distortion,
                                     int *s, int *skip,
                                     int64_t txfm_cache[NB_TXFM_MODES],
                                     TX_SIZE max_txfm_size) {
  VP9_COMMON *const cm = &cpi->common;
  MACROBLOCKD *const xd = &x->e_mbd;
  MB_MODE_INFO *const mbmi = &xd->mode_info_context->mbmi;
  vp9_prob skip_prob = vp9_get_pred_prob(cm, xd, PRED_MBSKIP);
  int64_t rd[TX_SIZE_MAX_SB][2];
  int n, m;
  int s0, s1;

  for (n = TX_4X4; n <= max_txfm_size; n++) {
    r[n][1] = r[n][0];
    for (m = 0; m <= n - (n == max_txfm_size); m++) {
      if (m == n)
        r[n][1] += vp9_cost_zero(cm->prob_tx[m]);
      else
        r[n][1] += vp9_cost_one(cm->prob_tx[m]);
    }
  }

  assert(skip_prob > 0);
  s0 = vp9_cost_bit(skip_prob, 0);
  s1 = vp9_cost_bit(skip_prob, 1);

  for (n = TX_4X4; n <= max_txfm_size; n++) {
    if (s[n]) {
      rd[n][0] = rd[n][1] = RDCOST(x->rdmult, x->rddiv, s1, d[n]);
    } else {
      rd[n][0] = RDCOST(x->rdmult, x->rddiv, r[n][0] + s0, d[n]);
      rd[n][1] = RDCOST(x->rdmult, x->rddiv, r[n][1] + s0, d[n]);
    }
  }

  if (max_txfm_size == TX_32X32 &&
      (cm->txfm_mode == ALLOW_32X32 ||
       (cm->txfm_mode == TX_MODE_SELECT &&
        rd[TX_32X32][1] < rd[TX_16X16][1] && rd[TX_32X32][1] < rd[TX_8X8][1] &&
        rd[TX_32X32][1] < rd[TX_4X4][1]))) {
    mbmi->txfm_size = TX_32X32;
  } else if (max_txfm_size >= TX_16X16 &&
             (cm->txfm_mode == ALLOW_16X16 ||
              cm->txfm_mode == ALLOW_32X32 ||
              (cm->txfm_mode == TX_MODE_SELECT &&
               rd[TX_16X16][1] < rd[TX_8X8][1] &&
               rd[TX_16X16][1] < rd[TX_4X4][1]))) {
    mbmi->txfm_size = TX_16X16;
  } else if (cm->txfm_mode == ALLOW_8X8 ||
             cm->txfm_mode == ALLOW_16X16 ||
             cm->txfm_mode == ALLOW_32X32 ||
           (cm->txfm_mode == TX_MODE_SELECT && rd[TX_8X8][1] < rd[TX_4X4][1])) {
    mbmi->txfm_size = TX_8X8;
  } else {
    mbmi->txfm_size = TX_4X4;
  }

  *distortion = d[mbmi->txfm_size];
  *rate       = r[mbmi->txfm_size][cm->txfm_mode == TX_MODE_SELECT];
  *skip       = s[mbmi->txfm_size];

  txfm_cache[ONLY_4X4] = rd[TX_4X4][0];
  txfm_cache[ALLOW_8X8] = rd[TX_8X8][0];
  txfm_cache[ALLOW_16X16] = rd[MIN(max_txfm_size, TX_16X16)][0];
  txfm_cache[ALLOW_32X32] = rd[MIN(max_txfm_size, TX_32X32)][0];
  if (max_txfm_size == TX_32X32 &&
      rd[TX_32X32][1] < rd[TX_16X16][1] && rd[TX_32X32][1] < rd[TX_8X8][1] &&
      rd[TX_32X32][1] < rd[TX_4X4][1])
    txfm_cache[TX_MODE_SELECT] = rd[TX_32X32][1];
  else if (max_txfm_size >= TX_16X16 &&
           rd[TX_16X16][1] < rd[TX_8X8][1] && rd[TX_16X16][1] < rd[TX_4X4][1])
    txfm_cache[TX_MODE_SELECT] = rd[TX_16X16][1];
  else
    txfm_cache[TX_MODE_SELECT] = rd[TX_4X4][1] < rd[TX_8X8][1] ?
                                 rd[TX_4X4][1] : rd[TX_8X8][1];
}

static int block_error(int16_t *coeff, int16_t *dqcoeff,
                       int block_size, int shift) {
  int i;
  int64_t error = 0;

  for (i = 0; i < block_size; i++) {
    int this_diff = coeff[i] - dqcoeff[i];
    error += (unsigned)this_diff * this_diff;
  }
  error >>= shift;

  return error > INT_MAX ? INT_MAX : (int)error;
}

static int block_error_sby(MACROBLOCK *x, BLOCK_SIZE_TYPE bsize, int shift) {
  const int bwl = b_width_log2(bsize), bhl = b_height_log2(bsize);
  return block_error(x->plane[0].coeff, x->e_mbd.plane[0].dqcoeff,
                     16 << (bwl + bhl), shift);
}

static int block_error_sbuv(MACROBLOCK *x, BLOCK_SIZE_TYPE bsize, int shift) {
  const int bwl = b_width_log2(bsize), bhl = b_height_log2(bsize);
  int64_t sum = 0;
  int plane;

  for (plane = 1; plane < MAX_MB_PLANE; plane++) {
    const int subsampling = x->e_mbd.plane[plane].subsampling_x +
                            x->e_mbd.plane[plane].subsampling_y;
    sum += block_error(x->plane[plane].coeff, x->e_mbd.plane[plane].dqcoeff,
                       16 << (bwl + bhl - subsampling), 0);
  }
  sum >>= shift;
  return sum > INT_MAX ? INT_MAX : (int)sum;
}

static int rdcost_plane(VP9_COMMON *const cm, MACROBLOCK *x,
                        int plane, BLOCK_SIZE_TYPE bsize, TX_SIZE tx_size) {
  MACROBLOCKD *const xd = &x->e_mbd;
  const int bwl = b_width_log2(bsize) - xd->plane[plane].subsampling_x;
  const int bhl = b_height_log2(bsize) - xd->plane[plane].subsampling_y;
  const int bw = 1 << bwl, bh = 1 << bhl;
  ENTROPY_CONTEXT t_above[16], t_left[16];
  int block, cost;

  vpx_memcpy(&t_above, xd->plane[plane].above_context,
             sizeof(ENTROPY_CONTEXT) * bw);
  vpx_memcpy(&t_left,  xd->plane[plane].left_context,
             sizeof(ENTROPY_CONTEXT) * bh);

  cost = 0;
  for (block = 0; block < bw * bh; block += 1 << (tx_size * 2)) {
    int x_idx, y_idx;

    txfrm_block_to_raster_xy(xd, bsize, plane, block, tx_size * 2,
                             &x_idx, &y_idx);

    cost += cost_coeffs(cm, x, plane, block, xd->plane[plane].plane_type,
                        t_above + x_idx, t_left + y_idx,
                        tx_size, bw * bh);
  }

  return cost;
}

static int rdcost_uv(VP9_COMMON *const cm, MACROBLOCK *x,
                     BLOCK_SIZE_TYPE bsize, TX_SIZE tx_size) {
  int cost = 0, plane;

  for (plane = 1; plane < MAX_MB_PLANE; plane++) {
    cost += rdcost_plane(cm, x, plane, bsize, tx_size);
  }
  return cost;
}

static void super_block_yrd_for_txfm(VP9_COMMON *const cm, MACROBLOCK *x,
                                     int *rate, int *distortion, int *skippable,
                                     BLOCK_SIZE_TYPE bsize, TX_SIZE tx_size) {
  MACROBLOCKD *const xd = &x->e_mbd;
  xd->mode_info_context->mbmi.txfm_size = tx_size;
  vp9_xform_quant_sby(cm, x, bsize);

  *distortion = block_error_sby(x, bsize, tx_size == TX_32X32 ? 0 : 2);
  *rate       = rdcost_plane(cm, x, 0, bsize, tx_size);
  *skippable  = vp9_sby_is_skippable(xd, bsize);
}

static void super_block_yrd(VP9_COMP *cpi,
                            MACROBLOCK *x, int *rate, int *distortion,
                            int *skip, BLOCK_SIZE_TYPE bs,
                            int64_t txfm_cache[NB_TXFM_MODES]) {
  VP9_COMMON *const cm = &cpi->common;
  int r[TX_SIZE_MAX_SB][2], d[TX_SIZE_MAX_SB], s[TX_SIZE_MAX_SB];

  vp9_subtract_sby(x, bs);

  if (bs >= BLOCK_SIZE_SB32X32)
    super_block_yrd_for_txfm(cm, x, &r[TX_32X32][0], &d[TX_32X32], &s[TX_32X32],
                             bs, TX_32X32);
  if (bs >= BLOCK_SIZE_MB16X16)
    super_block_yrd_for_txfm(cm, x, &r[TX_16X16][0], &d[TX_16X16], &s[TX_16X16],
                             bs, TX_16X16);
  super_block_yrd_for_txfm(cm, x, &r[TX_8X8][0], &d[TX_8X8], &s[TX_8X8], bs,
                           TX_8X8);
  super_block_yrd_for_txfm(cm, x, &r[TX_4X4][0], &d[TX_4X4], &s[TX_4X4], bs,
                           TX_4X4);

  choose_txfm_size_from_rd(cpi, x, r, rate, d, distortion, s, skip, txfm_cache,
                           TX_32X32 - (bs < BLOCK_SIZE_SB32X32)
#if CONFIG_SB8X8
                           - (bs < BLOCK_SIZE_MB16X16)
#endif
                           );
}

static int64_t rd_pick_intra4x4block(VP9_COMP *cpi, MACROBLOCK *x, int ib,
                                     B_PREDICTION_MODE *best_mode,
                                     int *bmode_costs,
                                     ENTROPY_CONTEXT *a, ENTROPY_CONTEXT *l,
                                     int *bestrate, int *bestratey,
                                     int *bestdistortion) {
  B_PREDICTION_MODE mode;
  MACROBLOCKD *xd = &x->e_mbd;
  int64_t best_rd = INT64_MAX;
  int rate = 0;
  int distortion;
  VP9_COMMON *const cm = &cpi->common;
  const int src_stride = x->plane[0].src.stride;
  uint8_t* const src =
      raster_block_offset_uint8(xd,
#if CONFIG_SB8X8
                                BLOCK_SIZE_SB8X8,
#else
                                BLOCK_SIZE_MB16X16,
#endif
                                0, ib,
                                x->plane[0].src.buf, src_stride);
  int16_t* const src_diff =
      raster_block_offset_int16(xd,
#if CONFIG_SB8X8
                                BLOCK_SIZE_SB8X8,
#else
                                BLOCK_SIZE_MB16X16,
#endif
                                0, ib,
                                x->plane[0].src_diff);
  int16_t* const diff =
      raster_block_offset_int16(xd,
#if CONFIG_SB8X8
                                BLOCK_SIZE_SB8X8,
#else
                                BLOCK_SIZE_MB16X16,
#endif
                                0, ib,
                                xd->plane[0].diff);
  int16_t* const coeff = BLOCK_OFFSET(x->plane[0].coeff, ib, 16);
  uint8_t* const dst =
      raster_block_offset_uint8(xd,
#if CONFIG_SB8X8
                                BLOCK_SIZE_SB8X8,
#else
                                BLOCK_SIZE_MB16X16,
#endif
                                0, ib,
                                xd->plane[0].dst.buf, xd->plane[0].dst.stride);
  ENTROPY_CONTEXT ta = *a, tempa = *a;
  ENTROPY_CONTEXT tl = *l, templ = *l;
  TX_TYPE tx_type = DCT_DCT;
  TX_TYPE best_tx_type = DCT_DCT;
  /*
   * The predictor buffer is a 2d buffer with a stride of 16.  Create
   * a temp buffer that meets the stride requirements, but we are only
   * interested in the left 4x4 block
   * */
  DECLARE_ALIGNED_ARRAY(16, int16_t, best_dqcoeff, 16);

  assert(ib < (16 >> (2 * CONFIG_SB8X8)));
#if CONFIG_NEWBINTRAMODES
  xd->mode_info_context->bmi[ib].as_mode.context =
    vp9_find_bpred_context(xd, ib, dst, xd->plane[0].dst.stride);
#endif
  xd->mode_info_context->mbmi.txfm_size = TX_4X4;
  for (mode = B_DC_PRED; mode < LEFT4X4; mode++) {
    int64_t this_rd;
    int ratey;

#if CONFIG_NEWBINTRAMODES
    if (xd->frame_type == KEY_FRAME) {
      if (mode == B_CONTEXT_PRED) continue;
    } else {
      if (mode >= B_CONTEXT_PRED - CONTEXT_PRED_REPLACEMENTS &&
          mode < B_CONTEXT_PRED)
        continue;
    }
#endif

    xd->mode_info_context->bmi[ib].as_mode.first = mode;
#if CONFIG_NEWBINTRAMODES
    rate = bmode_costs[
        mode == B_CONTEXT_PRED ? mode - CONTEXT_PRED_REPLACEMENTS : mode];
#else
    rate = bmode_costs[mode];
#endif

    vp9_intra4x4_predict(xd, ib,
#if CONFIG_SB8X8
                         BLOCK_SIZE_SB8X8,
#else
                         BLOCK_SIZE_MB16X16,
#endif
                         mode, dst, xd->plane[0].dst.stride);
    vp9_subtract_block(4, 4, src_diff, 16 >> CONFIG_SB8X8,
                       src, src_stride,
                       dst, xd->plane[0].dst.stride);

    xd->mode_info_context->bmi[ib].as_mode.first = mode;
    tx_type = get_tx_type_4x4(xd, ib);
    if (tx_type != DCT_DCT) {
      vp9_short_fht4x4(src_diff, coeff, 16 >> CONFIG_SB8X8, tx_type);
      x->quantize_b_4x4(x, ib, tx_type, 16);
    } else {
      x->fwd_txm4x4(src_diff, coeff, 32 >> CONFIG_SB8X8);
      x->quantize_b_4x4(x, ib, tx_type, 16);
    }

    tempa = ta;
    templ = tl;

    ratey = cost_coeffs(cm, x, 0, ib,
                        PLANE_TYPE_Y_WITH_DC, &tempa, &templ, TX_4X4, 16);
    rate += ratey;
    distortion = vp9_block_error(coeff,
                                 BLOCK_OFFSET(xd->plane[0].dqcoeff, ib, 16),
                                 16) >> 2;

    this_rd = RDCOST(x->rdmult, x->rddiv, rate, distortion);

    if (this_rd < best_rd) {
      *bestrate = rate;
      *bestratey = ratey;
      *bestdistortion = distortion;
      best_rd = this_rd;
      *best_mode = mode;
      best_tx_type = tx_type;
      *a = tempa;
      *l = templ;
      vpx_memcpy(best_dqcoeff, BLOCK_OFFSET(xd->plane[0].dqcoeff, ib, 16), 32);
    }
  }
  xd->mode_info_context->bmi[ib].as_mode.first =
    (B_PREDICTION_MODE)(*best_mode);

  // inverse transform
  if (best_tx_type != DCT_DCT)
    vp9_short_iht4x4(best_dqcoeff, diff, 16 >> CONFIG_SB8X8, best_tx_type);
  else
    xd->inv_txm4x4(best_dqcoeff, diff, 32 >> CONFIG_SB8X8);

  vp9_intra4x4_predict(xd, ib,
#if CONFIG_SB8X8
                       BLOCK_SIZE_SB8X8,
#else
                       BLOCK_SIZE_MB16X16,
#endif
                       *best_mode,
                       dst, xd->plane[0].dst.stride);
  vp9_recon_b(dst, diff, 16 >> CONFIG_SB8X8,
              dst, xd->plane[0].dst.stride);

  return best_rd;
}

static int64_t rd_pick_intra4x4mby_modes(VP9_COMP *cpi, MACROBLOCK *mb,
                                         int *Rate, int *rate_y,
                                         int *Distortion, int64_t best_rd) {
  int i;
  MACROBLOCKD *const xd = &mb->e_mbd;
  int cost = mb->mbmode_cost[xd->frame_type][I4X4_PRED];
  int distortion = 0;
  int tot_rate_y = 0;
  int64_t total_rd = 0;
  ENTROPY_CONTEXT t_above[4 >> CONFIG_SB8X8], t_left[4 >> CONFIG_SB8X8];
  int *bmode_costs;

  vpx_memcpy(t_above, xd->plane[0].above_context, sizeof(t_above));
  vpx_memcpy(t_left, xd->plane[0].left_context, sizeof(t_left));

  xd->mode_info_context->mbmi.mode = I4X4_PRED;
  bmode_costs = mb->inter_bmode_costs;

  for (i = 0; i < (16 >> (2 * CONFIG_SB8X8)); i++) {
    const int x_idx = i & (3 >> CONFIG_SB8X8), y_idx = i >> (2 >> CONFIG_SB8X8);
    MODE_INFO *const mic = xd->mode_info_context;
    const int mis = xd->mode_info_stride;
    B_PREDICTION_MODE UNINITIALIZED_IS_SAFE(best_mode);
    int UNINITIALIZED_IS_SAFE(r), UNINITIALIZED_IS_SAFE(ry), UNINITIALIZED_IS_SAFE(d);
#if CONFIG_NEWBINTRAMODES
    uint8_t* const dst =
        raster_block_offset_uint8(xd,
#if CONFIG_SB8X8
                                  BLOCK_SIZE_SB8X8,
#else
                                  BLOCK_SIZE_MB16X16,
#endif
                                  0, i,
                                  xd->plane[0].dst.buf,
                                  xd->plane[0].dst.stride);
#endif

    if (xd->frame_type == KEY_FRAME) {
      const B_PREDICTION_MODE A = above_block_mode(mic, i, mis);
      const B_PREDICTION_MODE L = left_block_mode(mic, i);

      bmode_costs  = mb->bmode_costs[A][L];
    }
#if CONFIG_NEWBINTRAMODES
    mic->bmi[i].as_mode.context = vp9_find_bpred_context(xd, i, dst,
        xd->plane[0].dst.stride);
#endif

    total_rd += rd_pick_intra4x4block(cpi, mb, i, &best_mode, bmode_costs,
                                      t_above + x_idx, t_left + y_idx,
                                      &r, &ry, &d);

    cost += r;
    distortion += d;
    tot_rate_y += ry;

    mic->bmi[i].as_mode.first = best_mode;

#if 0  // CONFIG_NEWBINTRAMODES
    printf("%d %d\n", mic->bmi[i].as_mode.first, mic->bmi[i].as_mode.context);
#endif

    if (total_rd >= best_rd)
      break;
  }

  if (total_rd >= best_rd)
    return INT64_MAX;

  *Rate = cost;
  *rate_y = tot_rate_y;
  *Distortion = distortion;

  return RDCOST(mb->rdmult, mb->rddiv, cost, distortion);
}

static int64_t rd_pick_intra_sby_mode(VP9_COMP *cpi, MACROBLOCK *x,
                                      int *rate, int *rate_tokenonly,
                                      int *distortion, int *skippable,
                                      BLOCK_SIZE_TYPE bsize,
                                      int64_t txfm_cache[NB_TXFM_MODES]) {
  MB_PREDICTION_MODE mode;
  MB_PREDICTION_MODE UNINITIALIZED_IS_SAFE(mode_selected);
  int this_rate, this_rate_tokenonly;
  int this_distortion, s;
  int64_t best_rd = INT64_MAX, this_rd;
  TX_SIZE UNINITIALIZED_IS_SAFE(best_tx);
  int i;

  for (i = 0; i < NB_TXFM_MODES; i++)
    txfm_cache[i] = INT64_MAX;

  /* Y Search for 32x32 intra prediction mode */
  for (mode = DC_PRED; mode <= TM_PRED; mode++) {
    int64_t local_txfm_cache[NB_TXFM_MODES];

    x->e_mbd.mode_info_context->mbmi.mode = mode;
    vp9_build_intra_predictors_sby_s(&x->e_mbd, bsize);

    super_block_yrd(cpi, x, &this_rate_tokenonly, &this_distortion, &s,
                    bsize, local_txfm_cache);
    this_rate = this_rate_tokenonly + x->mbmode_cost[x->e_mbd.frame_type][mode];
    this_rd = RDCOST(x->rdmult, x->rddiv, this_rate, this_distortion);

    if (this_rd < best_rd) {
      mode_selected   = mode;
      best_rd         = this_rd;
      best_tx         = x->e_mbd.mode_info_context->mbmi.txfm_size;
      *rate           = this_rate;
      *rate_tokenonly = this_rate_tokenonly;
      *distortion     = this_distortion;
      *skippable      = s;
    }

    for (i = 0; i < NB_TXFM_MODES; i++) {
      int64_t adj_rd = this_rd + local_txfm_cache[i] -
                       local_txfm_cache[cpi->common.txfm_mode];
      if (adj_rd < txfm_cache[i]) {
        txfm_cache[i] = adj_rd;
      }
    }
  }

  x->e_mbd.mode_info_context->mbmi.mode = mode_selected;
  x->e_mbd.mode_info_context->mbmi.txfm_size = best_tx;

  return best_rd;
}

#if !CONFIG_SB8X8
static int64_t rd_pick_intra8x8block(VP9_COMP *cpi, MACROBLOCK *x, int ib,
                                     B_PREDICTION_MODE *best_mode,
                                     int *mode_costs,
                                     ENTROPY_CONTEXT *a, ENTROPY_CONTEXT *l,
                                     int *bestrate, int *bestratey,
                                     int *bestdistortion) {
  VP9_COMMON *const cm = &cpi->common;
  MB_PREDICTION_MODE mode;
  MACROBLOCKD *xd = &x->e_mbd;
  int64_t best_rd = INT64_MAX;
  int distortion = 0, rate = 0;
  ENTROPY_CONTEXT ta[2], tl[2], ta_temp[2], tl_temp[2];
  // perform transformation of dimension 8x8
  // note the input and output index mapping
  int idx = (ib & 0x02) ? (ib + 2) : ib;
  const int src_stride = x->plane[0].src.stride;
  uint8_t* const src =
      raster_block_offset_uint8(xd, BLOCK_SIZE_MB16X16, 0, ib,
                                x->plane[0].src.buf, src_stride);
  int16_t* const src_diff =
      raster_block_offset_int16(xd, BLOCK_SIZE_MB16X16, 0, ib,
                                x->plane[0].src_diff);
  int16_t* const coeff = BLOCK_OFFSET(x->plane[0].coeff, idx, 16);
  uint8_t* const dst =
      raster_block_offset_uint8(xd, BLOCK_SIZE_MB16X16, 0, ib,
                                xd->plane[0].dst.buf, xd->plane[0].dst.stride);

  assert(ib < 16);
  vpx_memcpy(ta, a, sizeof(ta));
  vpx_memcpy(tl, l, sizeof(tl));
  for (mode = DC_PRED; mode <= TM_PRED; mode++) {
    int64_t this_rd;
    int rate_t = 0;

    // FIXME rate for compound mode and second intrapred mode
    rate = mode_costs[mode];
    xd->mode_info_context->bmi[ib].as_mode.first = mode;

    vp9_intra8x8_predict(xd, ib, mode, dst, xd->plane[0].dst.stride);

    vp9_subtract_block(8, 8, src_diff, 16,
                       src, src_stride,
                       dst, xd->plane[0].dst.stride);

    vpx_memcpy(ta_temp, ta, sizeof(ta));
    vpx_memcpy(tl_temp, tl, sizeof(tl));

    if (xd->mode_info_context->mbmi.txfm_size == TX_8X8) {
      TX_TYPE tx_type = get_tx_type_8x8(xd, ib);
      if (tx_type != DCT_DCT)
        vp9_short_fht8x8(src_diff, coeff, 16, tx_type);
      else
        x->fwd_txm8x8(src_diff, coeff, 32);
      x->quantize_b_8x8(x, idx, tx_type, 16);

      // compute quantization mse of 8x8 block
      distortion = vp9_block_error_c(coeff,
          BLOCK_OFFSET(xd->plane[0].dqcoeff, idx, 16), 64);

      rate_t = cost_coeffs(cm, x, 0, idx, PLANE_TYPE_Y_WITH_DC,
                           ta_temp, tl_temp, TX_8X8, 16);

      rate += rate_t;
    } else {
      static const int iblock[4] = {0, 1, 4, 5};
      TX_TYPE tx_type;
      int i;

      distortion = 0;
      rate_t = 0;
      for (i = 0; i < 4; ++i) {
        int16_t* const src_diff =
            raster_block_offset_int16(xd, BLOCK_SIZE_MB16X16,
                                      0, ib + iblock[i],
                                      x->plane[0].src_diff);
        int16_t* const coeff = BLOCK_OFFSET(x->plane[0].coeff,
                                            ib + iblock[i], 16);
        int do_two = 0;
        tx_type = get_tx_type_4x4(xd, ib + iblock[i]);
        if (tx_type != DCT_DCT) {
          vp9_short_fht4x4(src_diff, coeff, 16, tx_type);
          x->quantize_b_4x4(x, ib + iblock[i], tx_type, 16);
        } else if (!(i & 1) &&
                   get_tx_type_4x4(xd, ib + iblock[i] + 1) == DCT_DCT) {
          x->fwd_txm8x4(src_diff, coeff, 32);
          x->quantize_b_4x4_pair(x, ib + iblock[i], ib + iblock[i] + 1, 16);
          do_two = 1;
        } else {
          x->fwd_txm4x4(src_diff, coeff, 32);
          x->quantize_b_4x4(x, ib + iblock[i], tx_type, 16);
        }
        distortion += vp9_block_error_c(coeff,
            BLOCK_OFFSET(xd->plane[0].dqcoeff, ib + iblock[i], 16),
            16 << do_two);
        rate_t += cost_coeffs(cm, x, 0, ib + iblock[i], PLANE_TYPE_Y_WITH_DC,
                              &ta_temp[i & 1], &tl_temp[i >> 1],
                              TX_4X4, 16);
        if (do_two) {
          i++;
          rate_t += cost_coeffs(cm, x, 0, ib + iblock[i], PLANE_TYPE_Y_WITH_DC,
                                &ta_temp[i & 1], &tl_temp[i >> 1],
                                TX_4X4, 16);
        }
      }
      rate += rate_t;
    }

    distortion >>= 2;
    this_rd = RDCOST(x->rdmult, x->rddiv, rate, distortion);
    if (this_rd < best_rd) {
      *bestrate = rate;
      *bestratey = rate_t;
      *bestdistortion = distortion;
      vpx_memcpy(a, ta_temp, sizeof(ta_temp));
      vpx_memcpy(l, tl_temp, sizeof(tl_temp));
      best_rd = this_rd;
      *best_mode = mode;
    }
  }
  xd->mode_info_context->bmi[ib].as_mode.first = (*best_mode);
  vp9_encode_intra8x8(x, ib);

  return best_rd;
}

static int64_t rd_pick_intra8x8mby_modes(VP9_COMP *cpi, MACROBLOCK *mb,
                                         int *Rate, int *rate_y,
                                         int *Distortion, int64_t best_rd) {
  MACROBLOCKD *const xd = &mb->e_mbd;
  int i, ib;
  int cost = mb->mbmode_cost [xd->frame_type] [I8X8_PRED];
  int distortion = 0;
  int tot_rate_y = 0;
  int64_t total_rd = 0;
  ENTROPY_CONTEXT t_above[4], t_left[4];
  int *i8x8mode_costs;

  vpx_memcpy(t_above, xd->plane[0].above_context, sizeof(t_above));
  vpx_memcpy(t_left, xd->plane[0].left_context, sizeof(t_left));

  xd->mode_info_context->mbmi.mode = I8X8_PRED;
  i8x8mode_costs  = mb->i8x8_mode_costs;

  for (i = 0; i < 4; i++) {
    const int x_idx = i & 1, y_idx = i >> 1;
    MODE_INFO *const mic = xd->mode_info_context;
    B_PREDICTION_MODE UNINITIALIZED_IS_SAFE(best_mode);
    int UNINITIALIZED_IS_SAFE(r), UNINITIALIZED_IS_SAFE(ry), UNINITIALIZED_IS_SAFE(d);

    ib = vp9_i8x8_block[i];
    total_rd += rd_pick_intra8x8block(cpi, mb, ib, &best_mode, i8x8mode_costs,
                                      t_above + x_idx * 2, t_left + y_idx * 2,
                                      &r, &ry, &d);
    cost += r;
    distortion += d;
    tot_rate_y += ry;
    mic->bmi[ib].as_mode.first = best_mode;
  }

  *Rate = cost;
  *rate_y = tot_rate_y;
  *Distortion = distortion;
  return RDCOST(mb->rdmult, mb->rddiv, cost, distortion);
}

static int64_t rd_pick_intra8x8mby_modes_and_txsz(VP9_COMP *cpi, MACROBLOCK *x,
                                                  int *rate, int *rate_y,
                                                  int *distortion,
                                                  int *mode8x8,
                                                  int64_t best_yrd,
                                                  int64_t *txfm_cache) {
  VP9_COMMON *const cm = &cpi->common;
  MACROBLOCKD *const xd = &x->e_mbd;
  MB_MODE_INFO *mbmi = &xd->mode_info_context->mbmi;
  int cost0 = vp9_cost_bit(cm->prob_tx[0], 0);
  int cost1 = vp9_cost_bit(cm->prob_tx[0], 1);
  int64_t tmp_rd_4x4s, tmp_rd_8x8s;
  int64_t tmp_rd_4x4, tmp_rd_8x8, tmp_rd;
  int r4x4, tok4x4, d4x4, r8x8, tok8x8, d8x8;

  mbmi->txfm_size = TX_4X4;
  tmp_rd_4x4 = rd_pick_intra8x8mby_modes(cpi, x, &r4x4, &tok4x4,
                                         &d4x4, best_yrd);
  mode8x8[0] = xd->mode_info_context->bmi[0].as_mode.first;
  mode8x8[1] = xd->mode_info_context->bmi[2].as_mode.first;
  mode8x8[2] = xd->mode_info_context->bmi[8].as_mode.first;
  mode8x8[3] = xd->mode_info_context->bmi[10].as_mode.first;
  mbmi->txfm_size = TX_8X8;
  tmp_rd_8x8 = rd_pick_intra8x8mby_modes(cpi, x, &r8x8, &tok8x8,
                                         &d8x8, best_yrd);
  txfm_cache[ONLY_4X4]  = tmp_rd_4x4;
  txfm_cache[ALLOW_8X8] = tmp_rd_8x8;
  txfm_cache[ALLOW_16X16] = tmp_rd_8x8;
  tmp_rd_4x4s = tmp_rd_4x4 + RDCOST(x->rdmult, x->rddiv, cost0, 0);
  tmp_rd_8x8s = tmp_rd_8x8 + RDCOST(x->rdmult, x->rddiv, cost1, 0);
  txfm_cache[TX_MODE_SELECT] = tmp_rd_4x4s < tmp_rd_8x8s ?
                               tmp_rd_4x4s : tmp_rd_8x8s;
  if (cm->txfm_mode == TX_MODE_SELECT) {
    if (tmp_rd_4x4s < tmp_rd_8x8s) {
      *rate = r4x4 + cost0;
      *rate_y = tok4x4 + cost0;
      *distortion = d4x4;
      mbmi->txfm_size = TX_4X4;
      tmp_rd = tmp_rd_4x4s;
    } else {
      *rate = r8x8 + cost1;
      *rate_y = tok8x8 + cost1;
      *distortion = d8x8;
      mbmi->txfm_size = TX_8X8;
      tmp_rd = tmp_rd_8x8s;

      mode8x8[0] = xd->mode_info_context->bmi[0].as_mode.first;
      mode8x8[1] = xd->mode_info_context->bmi[2].as_mode.first;
      mode8x8[2] = xd->mode_info_context->bmi[8].as_mode.first;
      mode8x8[3] = xd->mode_info_context->bmi[10].as_mode.first;
    }
  } else if (cm->txfm_mode == ONLY_4X4) {
    *rate = r4x4;
    *rate_y = tok4x4;
    *distortion = d4x4;
    mbmi->txfm_size = TX_4X4;
    tmp_rd = tmp_rd_4x4;
  } else {
    *rate = r8x8;
    *rate_y = tok8x8;
    *distortion = d8x8;
    mbmi->txfm_size = TX_8X8;
    tmp_rd = tmp_rd_8x8;

    mode8x8[0] = xd->mode_info_context->bmi[0].as_mode.first;
    mode8x8[1] = xd->mode_info_context->bmi[2].as_mode.first;
    mode8x8[2] = xd->mode_info_context->bmi[8].as_mode.first;
    mode8x8[3] = xd->mode_info_context->bmi[10].as_mode.first;
  }

  return tmp_rd;
}
#endif  // !CONFIG_SB8X8

static void super_block_uvrd_for_txfm(VP9_COMMON *const cm, MACROBLOCK *x,
                                      int *rate, int *distortion,
                                      int *skippable, BLOCK_SIZE_TYPE bsize,
                                      TX_SIZE uv_tx_size) {
  MACROBLOCKD *const xd = &x->e_mbd;
  vp9_xform_quant_sbuv(cm, x, bsize);

  *distortion = block_error_sbuv(x, bsize, uv_tx_size == TX_32X32 ? 0 : 2);
  *rate       = rdcost_uv(cm, x, bsize, uv_tx_size);
  *skippable  = vp9_sbuv_is_skippable(xd, bsize);
}

static void super_block_uvrd(VP9_COMMON *const cm, MACROBLOCK *x,
                             int *rate, int *distortion, int *skippable,
                             BLOCK_SIZE_TYPE bsize) {
  MACROBLOCKD *const xd = &x->e_mbd;
  MB_MODE_INFO *const mbmi = &xd->mode_info_context->mbmi;

  vp9_subtract_sbuv(x, bsize);

  if (mbmi->txfm_size >= TX_32X32 && bsize >= BLOCK_SIZE_SB64X64) {
    super_block_uvrd_for_txfm(cm, x, rate, distortion, skippable, bsize,
                              TX_32X32);
  } else if (mbmi->txfm_size >= TX_16X16 && bsize >= BLOCK_SIZE_SB32X32) {
    super_block_uvrd_for_txfm(cm, x, rate, distortion, skippable, bsize,
                              TX_16X16);
  } else if (mbmi->txfm_size >= TX_8X8 && bsize >= BLOCK_SIZE_MB16X16) {
    super_block_uvrd_for_txfm(cm, x, rate, distortion, skippable, bsize,
                              TX_8X8);
  } else {
    super_block_uvrd_for_txfm(cm, x, rate, distortion, skippable, bsize,
                              TX_4X4);
  }
}

static int64_t rd_pick_intra_sbuv_mode(VP9_COMP *cpi, MACROBLOCK *x,
                                       int *rate, int *rate_tokenonly,
                                       int *distortion, int *skippable,
                                       BLOCK_SIZE_TYPE bsize) {
  MB_PREDICTION_MODE mode;
  MB_PREDICTION_MODE UNINITIALIZED_IS_SAFE(mode_selected);
  int64_t best_rd = INT64_MAX, this_rd;
  int this_rate_tokenonly, this_rate;
  int this_distortion, s;

  for (mode = DC_PRED; mode <= TM_PRED; mode++) {
    x->e_mbd.mode_info_context->mbmi.uv_mode = mode;
    vp9_build_intra_predictors_sbuv_s(&x->e_mbd, bsize);

    super_block_uvrd(&cpi->common, x, &this_rate_tokenonly,
                     &this_distortion, &s, bsize);
    this_rate = this_rate_tokenonly +
                x->intra_uv_mode_cost[x->e_mbd.frame_type][mode];
    this_rd = RDCOST(x->rdmult, x->rddiv, this_rate, this_distortion);

    if (this_rd < best_rd) {
      mode_selected   = mode;
      best_rd         = this_rd;
      *rate           = this_rate;
      *rate_tokenonly = this_rate_tokenonly;
      *distortion     = this_distortion;
      *skippable      = s;
    }
  }

  x->e_mbd.mode_info_context->mbmi.uv_mode = mode_selected;

  return best_rd;
}

int vp9_cost_mv_ref(VP9_COMP *cpi,
                    MB_PREDICTION_MODE m,
                    const int mode_context) {
  MACROBLOCKD *xd = &cpi->mb.e_mbd;
  int segment_id = xd->mode_info_context->mbmi.segment_id;

  // Dont account for mode here if segment skip is enabled.
  if (!vp9_segfeature_active(xd, segment_id, SEG_LVL_SKIP)) {
    VP9_COMMON *pc = &cpi->common;

    vp9_prob p [VP9_MVREFS - 1];
    assert(NEARESTMV <= m  &&  m <= SPLITMV);
    vp9_mv_ref_probs(pc, p, mode_context);
    return cost_token(vp9_mv_ref_tree, p,
                      vp9_mv_ref_encoding_array - NEARESTMV + m);
  } else
    return 0;
}

void vp9_set_mbmode_and_mvs(MACROBLOCK *x, MB_PREDICTION_MODE mb, int_mv *mv) {
  x->e_mbd.mode_info_context->mbmi.mode = mb;
  x->e_mbd.mode_info_context->mbmi.mv[0].as_int = mv->as_int;
}

#if CONFIG_SB8X8
static int labels2mode(MACROBLOCK *x,
                       int const *labelings, int which_label,
                       B_PREDICTION_MODE this_mode,
                       int_mv *this_mv, int_mv *this_second_mv,
                       int_mv seg_mvs[MAX_REF_FRAMES - 1],
                       int_mv *best_ref_mv,
                       int_mv *second_best_ref_mv,
                       int *mvjcost, int *mvcost[2], VP9_COMP *cpi) {
  MACROBLOCKD *const xd = &x->e_mbd;
  MODE_INFO *const mic = xd->mode_info_context;
  MB_MODE_INFO * mbmi = &mic->mbmi;
  const int mis = xd->mode_info_stride;
  int i, cost = 0, thismvcost = 0;

  /* We have to be careful retrieving previously-encoded motion vectors.
   Ones from this macroblock have to be pulled from the BLOCKD array
   as they have not yet made it to the bmi array in our MB_MODE_INFO. */
  for (i = 0; i < 4; ++i) {
    const int row = i >> 1, col = i & 1;
    B_PREDICTION_MODE m;

    if (labelings[i] != which_label)
      continue;

    if (col  &&  labelings[i] == labelings[i - 1])
      m = LEFT4X4;
    else if (row  &&  labelings[i] == labelings[i - 2])
      m = ABOVE4X4;
    else {
      // the only time we should do costing for new motion vector or mode
      // is when we are on a new label  (jbb May 08, 2007)
      switch (m = this_mode) {
        case NEW4X4 :
          if (mbmi->second_ref_frame > 0) {
            this_mv->as_int = seg_mvs[mbmi->ref_frame - 1].as_int;
            this_second_mv->as_int =
            seg_mvs[mbmi->second_ref_frame - 1].as_int;
          }

          thismvcost  = vp9_mv_bit_cost(this_mv, best_ref_mv, mvjcost, mvcost,
                                        102, xd->allow_high_precision_mv);
          if (mbmi->second_ref_frame > 0) {
            thismvcost += vp9_mv_bit_cost(this_second_mv, second_best_ref_mv,
                                          mvjcost, mvcost, 102,
                                          xd->allow_high_precision_mv);
          }
          break;
        case LEFT4X4:
          this_mv->as_int = col ? mic->bmi[i - 1].as_mv[0].as_int :
          left_block_mv(xd, mic, i);
          if (mbmi->second_ref_frame > 0)
            this_second_mv->as_int = col ? mic->bmi[i - 1].as_mv[1].as_int :
            left_block_second_mv(xd, mic, i);
          break;
        case ABOVE4X4:
          this_mv->as_int = row ? mic->bmi[i - 2].as_mv[0].as_int :
          above_block_mv(mic, i, mis);
          if (mbmi->second_ref_frame > 0)
            this_second_mv->as_int = row ? mic->bmi[i - 2].as_mv[1].as_int :
            above_block_second_mv(mic, i, mis);
          break;
        case ZERO4X4:
          this_mv->as_int = 0;
          if (mbmi->second_ref_frame > 0)
            this_second_mv->as_int = 0;
          break;
        default:
          break;
      }

      if (m == ABOVE4X4) {  // replace above with left if same
        int_mv left_mv, left_second_mv;

        left_second_mv.as_int = 0;
        left_mv.as_int = col ? mic->bmi[i - 1].as_mv[0].as_int :
        left_block_mv(xd, mic, i);
        if (mbmi->second_ref_frame > 0)
          left_second_mv.as_int = col ? mic->bmi[i - 1].as_mv[1].as_int :
          left_block_second_mv(xd, mic, i);

        if (left_mv.as_int == this_mv->as_int &&
            (mbmi->second_ref_frame <= 0 ||
             left_second_mv.as_int == this_second_mv->as_int))
          m = LEFT4X4;
      }

#if CONFIG_NEWBINTRAMODES
      cost = x->inter_bmode_costs[m == B_CONTEXT_PRED ?
                                  m - CONTEXT_PRED_REPLACEMENTS : m];
#else
      cost = x->inter_bmode_costs[m];
#endif
    }

    mic->bmi[i].as_mv[0].as_int = this_mv->as_int;
    if (mbmi->second_ref_frame > 0)
      mic->bmi[i].as_mv[1].as_int = this_second_mv->as_int;

    x->partition_info->bmi[i].mode = m;
    x->partition_info->bmi[i].mv.as_int = this_mv->as_int;
    if (mbmi->second_ref_frame > 0)
      x->partition_info->bmi[i].second_mv.as_int = this_second_mv->as_int;
  }

  cost += thismvcost;
  return cost;
}

static int64_t encode_inter_mb_segment(VP9_COMMON *const cm,
                                       MACROBLOCK *x,
                                       int const *labels,
                                       int which_label,
                                       int *labelyrate,
                                       int *distortion,
                                       ENTROPY_CONTEXT *ta,
                                       ENTROPY_CONTEXT *tl) {
  int i;
  MACROBLOCKD *xd = &x->e_mbd;

  *labelyrate = 0;
  *distortion = 0;
  for (i = 0; i < 4; i++) {
    if (labels[i] == which_label) {
      const int src_stride = x->plane[0].src.stride;
      uint8_t* const src =
      raster_block_offset_uint8(xd, BLOCK_SIZE_SB8X8, 0, i,
                                x->plane[0].src.buf, src_stride);
      int16_t* const src_diff =
      raster_block_offset_int16(xd, BLOCK_SIZE_SB8X8, 0, i,
                                x->plane[0].src_diff);
      int16_t* const coeff = BLOCK_OFFSET(x->plane[0].coeff, 16, i);
      uint8_t* const pre =
      raster_block_offset_uint8(xd, BLOCK_SIZE_SB8X8, 0, i,
                                xd->plane[0].pre[0].buf,
                                xd->plane[0].pre[0].stride);
      uint8_t* const dst =
      raster_block_offset_uint8(xd, BLOCK_SIZE_SB8X8, 0, i,
                                xd->plane[0].dst.buf,
                                xd->plane[0].dst.stride);
      int thisdistortion;

      vp9_build_inter_predictor(pre,
                                xd->plane[0].pre[0].stride,
                                dst,
                                xd->plane[0].dst.stride,
                                &xd->mode_info_context->bmi[i].as_mv[0],
                                &xd->scale_factor[0],
                                4, 4, 0 /* no avg */, &xd->subpix);

      // TODO(debargha): Make this work properly with the
      // implicit-compoundinter-weight experiment when implicit
      // weighting for splitmv modes is turned on.
      if (xd->mode_info_context->mbmi.second_ref_frame > 0) {
        uint8_t* const second_pre =
        raster_block_offset_uint8(xd, BLOCK_SIZE_SB8X8, 0, i,
                                  xd->plane[0].pre[1].buf,
                                  xd->plane[0].pre[1].stride);
        vp9_build_inter_predictor(second_pre, xd->plane[0].pre[1].stride,
                                  dst, xd->plane[0].dst.stride,
                                  &xd->mode_info_context->bmi[i].as_mv[1],
                                  &xd->scale_factor[1], 4, 4, 1,
                                  &xd->subpix);
      }

      vp9_subtract_block(4, 4, src_diff, 8,
                         src, src_stride,
                         dst, xd->plane[0].dst.stride);
      x->fwd_txm4x4(src_diff, coeff, 16);
      x->quantize_b_4x4(x, i, DCT_DCT, 16);
      thisdistortion = vp9_block_error(coeff,
                                       BLOCK_OFFSET(xd->plane[0].dqcoeff,
                                                    i, 16), 16);
      *distortion += thisdistortion;
      *labelyrate += cost_coeffs(cm, x, 0, i, PLANE_TYPE_Y_WITH_DC,
                                 ta + (i & 1),
                                 tl + (i >> 1), TX_4X4, 16);
    }
  }
  *distortion >>= 2;
  return RDCOST(x->rdmult, x->rddiv, *labelyrate, *distortion);
}

typedef struct {
  int_mv *ref_mv, *second_ref_mv;
  int_mv mvp;

  int64_t segment_rd;
  int r;
  int d;
  int segment_yrate;
  B_PREDICTION_MODE modes[4];
  int_mv mvs[4], second_mvs[4];
  int eobs[4];

  int mvthresh;
  int *mdcounts;
} BEST_SEG_INFO;
#endif  // CONFIG_SB8X8

static INLINE int mv_check_bounds(MACROBLOCK *x, int_mv *mv) {
  int r = 0;
  r |= (mv->as_mv.row >> 3) < x->mv_row_min;
  r |= (mv->as_mv.row >> 3) > x->mv_row_max;
  r |= (mv->as_mv.col >> 3) < x->mv_col_min;
  r |= (mv->as_mv.col >> 3) > x->mv_col_max;
  return r;
}

#if CONFIG_SB8X8
static void rd_check_segment_txsize(VP9_COMP *cpi, MACROBLOCK *x,
                                    BEST_SEG_INFO *bsi,
                                    int_mv seg_mvs[4][MAX_REF_FRAMES - 1]) {
  int i, j;
  static const int labels[4] = { 0, 1, 2, 3 };
  int br = 0, bd = 0;
  B_PREDICTION_MODE this_mode;
  MB_MODE_INFO * mbmi = &x->e_mbd.mode_info_context->mbmi;
  const int label_count = 4;
  int64_t this_segment_rd = 0, other_segment_rd;
  int label_mv_thresh;
  int rate = 0;
  int sbr = 0, sbd = 0;
  int segmentyrate = 0;
  int best_eobs[4] = { 0 };

  vp9_variance_fn_ptr_t *v_fn_ptr;

  ENTROPY_CONTEXT t_above[2], t_left[2];
  ENTROPY_CONTEXT t_above_b[2], t_left_b[2];

  vpx_memcpy(t_above, x->e_mbd.plane[0].above_context, sizeof(t_above));
  vpx_memcpy(t_left, x->e_mbd.plane[0].left_context, sizeof(t_left));

  v_fn_ptr = &cpi->fn_ptr[BLOCK_4X4];

  // 64 makes this threshold really big effectively
  // making it so that we very rarely check mvs on
  // segments.   setting this to 1 would make mv thresh
  // roughly equal to what it is for macroblocks
  label_mv_thresh = 1 * bsi->mvthresh / label_count;

  // Segmentation method overheads
  rate += vp9_cost_mv_ref(cpi, SPLITMV,
                          mbmi->mb_mode_context[mbmi->ref_frame]);
  this_segment_rd += RDCOST(x->rdmult, x->rddiv, rate, 0);
  br += rate;
  other_segment_rd = this_segment_rd;

  for (i = 0; i < label_count && this_segment_rd < bsi->segment_rd; i++) {
    int_mv mode_mv[B_MODE_COUNT], second_mode_mv[B_MODE_COUNT];
    int64_t best_label_rd = INT64_MAX, best_other_rd = INT64_MAX;
    B_PREDICTION_MODE mode_selected = ZERO4X4;
    int bestlabelyrate = 0;

    // search for the best motion vector on this segment
    for (this_mode = LEFT4X4; this_mode <= NEW4X4; this_mode ++) {
      int64_t this_rd;
      int distortion;
      int labelyrate;
      ENTROPY_CONTEXT t_above_s[2], t_left_s[2];

      vpx_memcpy(t_above_s, t_above, sizeof(t_above_s));
      vpx_memcpy(t_left_s, t_left, sizeof(t_left_s));

      // motion search for newmv (single predictor case only)
      if (mbmi->second_ref_frame <= 0 && this_mode == NEW4X4) {
        int sseshift, n;
        int step_param = 0;
        int further_steps;
        int thissme, bestsme = INT_MAX;
        const struct buf_2d orig_src = x->plane[0].src;
        const struct buf_2d orig_pre = x->e_mbd.plane[0].pre[0];

        /* Is the best so far sufficiently good that we cant justify doing
         * and new motion search. */
        if (best_label_rd < label_mv_thresh)
          break;

        if (cpi->compressor_speed) {
          // use previous block's result as next block's MV predictor.
          if (i > 0) {
            bsi->mvp.as_int =
            x->e_mbd.mode_info_context->bmi[i - 1].as_mv[0].as_int;
            if (i == 2)
              bsi->mvp.as_int =
              x->e_mbd.mode_info_context->bmi[i - 2].as_mv[0].as_int;
            step_param = 2;
          }
        }

        further_steps = (MAX_MVSEARCH_STEPS - 1) - step_param;

        {
          int sadpb = x->sadperbit4;
          int_mv mvp_full;

          mvp_full.as_mv.row = bsi->mvp.as_mv.row >> 3;
          mvp_full.as_mv.col = bsi->mvp.as_mv.col >> 3;

          // find first label
          n = i;

          // adjust src pointer for this segment
          x->plane[0].src.buf =
          raster_block_offset_uint8(&x->e_mbd, BLOCK_SIZE_SB8X8, 0, n,
                                    x->plane[0].src.buf,
                                    x->plane[0].src.stride);
          assert(((intptr_t)x->e_mbd.plane[0].pre[0].buf & 0x7) == 0);
          x->e_mbd.plane[0].pre[0].buf =
          raster_block_offset_uint8(&x->e_mbd, BLOCK_SIZE_SB8X8, 0, n,
                                    x->e_mbd.plane[0].pre[0].buf,
                                    x->e_mbd.plane[0].pre[0].stride);

          bestsme = vp9_full_pixel_diamond(cpi, x, &mvp_full, step_param,
                                           sadpb, further_steps, 0, v_fn_ptr,
                                           bsi->ref_mv, &mode_mv[NEW4X4]);

          sseshift = 0;

          // Should we do a full search (best quality only)
          if ((cpi->compressor_speed == 0) && (bestsme >> sseshift) > 4000) {
            /* Check if mvp_full is within the range. */
            clamp_mv(&mvp_full, x->mv_col_min, x->mv_col_max,
                     x->mv_row_min, x->mv_row_max);

            thissme = cpi->full_search_sad(x, &mvp_full,
                                           sadpb, 16, v_fn_ptr,
                                           x->nmvjointcost, x->mvcost,
                                           bsi->ref_mv,
                                           n);

            if (thissme < bestsme) {
              bestsme = thissme;
              mode_mv[NEW4X4].as_int =
              x->e_mbd.mode_info_context->bmi[n].as_mv[0].as_int;
            } else {
              /* The full search result is actually worse so re-instate the
               * previous best vector */
              x->e_mbd.mode_info_context->bmi[n].as_mv[0].as_int =
              mode_mv[NEW4X4].as_int;
            }
          }
        }

        if (bestsme < INT_MAX) {
          int distortion;
          unsigned int sse;
          cpi->find_fractional_mv_step(x, &mode_mv[NEW4X4],
                                       bsi->ref_mv, x->errorperbit, v_fn_ptr,
                                       x->nmvjointcost, x->mvcost,
                                       &distortion, &sse);

          // safe motion search result for use in compound prediction
          seg_mvs[i][mbmi->ref_frame - 1].as_int = mode_mv[NEW4X4].as_int;
        }

        // restore src pointers
        x->plane[0].src = orig_src;
        x->e_mbd.plane[0].pre[0] = orig_pre;
      } else if (mbmi->second_ref_frame > 0 && this_mode == NEW4X4) {
        /* NEW4X4 */
        /* motion search not completed? Then skip newmv for this block with
         * comppred */
        if (seg_mvs[i][mbmi->second_ref_frame - 1].as_int == INVALID_MV ||
            seg_mvs[i][mbmi->ref_frame        - 1].as_int == INVALID_MV) {
          continue;
        }
      }

      rate = labels2mode(x, labels, i, this_mode, &mode_mv[this_mode],
                         &second_mode_mv[this_mode], seg_mvs[i],
                         bsi->ref_mv, bsi->second_ref_mv, x->nmvjointcost,
                         x->mvcost, cpi);

      // Trap vectors that reach beyond the UMV borders
      if (((mode_mv[this_mode].as_mv.row >> 3) < x->mv_row_min) ||
          ((mode_mv[this_mode].as_mv.row >> 3) > x->mv_row_max) ||
          ((mode_mv[this_mode].as_mv.col >> 3) < x->mv_col_min) ||
          ((mode_mv[this_mode].as_mv.col >> 3) > x->mv_col_max)) {
        continue;
      }
      if (mbmi->second_ref_frame > 0 &&
          mv_check_bounds(x, &second_mode_mv[this_mode]))
        continue;

      this_rd = encode_inter_mb_segment(&cpi->common,
                                        x, labels, i, &labelyrate,
                                        &distortion, t_above_s, t_left_s);
      this_rd += RDCOST(x->rdmult, x->rddiv, rate, 0);
      rate += labelyrate;

      if (this_rd < best_label_rd) {
        sbr = rate;
        sbd = distortion;
        bestlabelyrate = labelyrate;
        mode_selected = this_mode;
        best_label_rd = this_rd;
        for (j = 0; j < 4; j++)
          if (labels[j] == i)
            best_eobs[j] = x->e_mbd.plane[0].eobs[j];

        vpx_memcpy(t_above_b, t_above_s, sizeof(t_above_s));
        vpx_memcpy(t_left_b, t_left_s, sizeof(t_left_s));
      }
    } /*for each 4x4 mode*/

    vpx_memcpy(t_above, t_above_b, sizeof(t_above));
    vpx_memcpy(t_left, t_left_b, sizeof(t_left));

    labels2mode(x, labels, i, mode_selected, &mode_mv[mode_selected],
                &second_mode_mv[mode_selected], seg_mvs[i],
                bsi->ref_mv, bsi->second_ref_mv, x->nmvjointcost,
                x->mvcost, cpi);

    br += sbr;
    bd += sbd;
    segmentyrate += bestlabelyrate;
    this_segment_rd += best_label_rd;
    other_segment_rd += best_other_rd;
  } /* for each label */

  if (this_segment_rd < bsi->segment_rd) {
    bsi->r = br;
    bsi->d = bd;
    bsi->segment_yrate = segmentyrate;
    bsi->segment_rd = this_segment_rd;

    // store everything needed to come back to this!!
    for (i = 0; i < 4; i++) {
      bsi->mvs[i].as_mv = x->partition_info->bmi[i].mv.as_mv;
      if (mbmi->second_ref_frame > 0)
        bsi->second_mvs[i].as_mv = x->partition_info->bmi[i].second_mv.as_mv;
      bsi->modes[i] = x->partition_info->bmi[i].mode;
      bsi->eobs[i] = best_eobs[i];
    }
  }
}

static void rd_check_segment(VP9_COMP *cpi, MACROBLOCK *x,
                             BEST_SEG_INFO *bsi,
                             int_mv seg_mvs[4][MAX_REF_FRAMES - 1]) {
  rd_check_segment_txsize(cpi, x, bsi, seg_mvs);
}

static int rd_pick_best_mbsegmentation(VP9_COMP *cpi, MACROBLOCK *x,
                                       int_mv *best_ref_mv,
                                       int_mv *second_best_ref_mv,
                                       int64_t best_rd,
                                       int *mdcounts,
                                       int *returntotrate,
                                       int *returnyrate,
                                       int *returndistortion,
                                       int *skippable, int mvthresh,
                                       int_mv seg_mvs[4][MAX_REF_FRAMES - 1]) {
  int i;
  BEST_SEG_INFO bsi;
  MB_MODE_INFO * mbmi = &x->e_mbd.mode_info_context->mbmi;

  vpx_memset(&bsi, 0, sizeof(bsi));

  bsi.segment_rd = best_rd;
  bsi.ref_mv = best_ref_mv;
  bsi.second_ref_mv = second_best_ref_mv;
  bsi.mvp.as_int = best_ref_mv->as_int;
  bsi.mvthresh = mvthresh;
  bsi.mdcounts = mdcounts;

  for (i = 0; i < 4; i++)
    bsi.modes[i] = ZERO4X4;

  rd_check_segment(cpi, x, &bsi, seg_mvs);

  /* set it to the best */
  for (i = 0; i < 4; i++) {
    x->e_mbd.mode_info_context->bmi[i].as_mv[0].as_int = bsi.mvs[i].as_int;
    if (mbmi->second_ref_frame > 0)
      x->e_mbd.mode_info_context->bmi[i].as_mv[1].as_int =
      bsi.second_mvs[i].as_int;
    x->e_mbd.plane[0].eobs[i] = bsi.eobs[i];
  }

  /* save partitions */
  x->partition_info->count = 4;

  for (i = 0; i < x->partition_info->count; i++) {
    x->partition_info->bmi[i].mode = bsi.modes[i];
    x->partition_info->bmi[i].mv.as_mv = bsi.mvs[i].as_mv;
    if (mbmi->second_ref_frame > 0)
      x->partition_info->bmi[i].second_mv.as_mv = bsi.second_mvs[i].as_mv;
  }
  /*
   * used to set mbmi->mv.as_int
   */
  x->partition_info->bmi[3].mv.as_int = bsi.mvs[3].as_int;
  if (mbmi->second_ref_frame > 0)
    x->partition_info->bmi[3].second_mv.as_int = bsi.second_mvs[3].as_int;

  *returntotrate = bsi.r;
  *returndistortion = bsi.d;
  *returnyrate = bsi.segment_yrate;
  *skippable = vp9_sby_is_skippable(&x->e_mbd, BLOCK_SIZE_SB8X8);

  return (int)(bsi.segment_rd);
}

#else  // !CONFIG_SB8X8

static int labels2mode(
  MACROBLOCK *x,
  int const *labelings, int which_label,
  B_PREDICTION_MODE this_mode,
  int_mv *this_mv, int_mv *this_second_mv,
  int_mv seg_mvs[MAX_REF_FRAMES - 1],
  int_mv *best_ref_mv,
  int_mv *second_best_ref_mv,
  int *mvjcost, int *mvcost[2], VP9_COMP *cpi) {
  MACROBLOCKD *const xd = &x->e_mbd;
  MODE_INFO *const mic = xd->mode_info_context;
  MB_MODE_INFO * mbmi = &mic->mbmi;
  const int mis = xd->mode_info_stride;

  int i, cost = 0, thismvcost = 0;

  /* We have to be careful retrieving previously-encoded motion vectors.
     Ones from this macroblock have to be pulled from the BLOCKD array
     as they have not yet made it to the bmi array in our MB_MODE_INFO. */
  for (i = 0; i < 16; ++i) {
    const int row = i >> 2,  col = i & 3;

    B_PREDICTION_MODE m;

    if (labelings[i] != which_label)
      continue;

    if (col  &&  labelings[i] == labelings[i - 1])
      m = LEFT4X4;
    else if (row  &&  labelings[i] == labelings[i - 4])
      m = ABOVE4X4;
    else {
      // the only time we should do costing for new motion vector or mode
      // is when we are on a new label  (jbb May 08, 2007)
      switch (m = this_mode) {
        case NEW4X4 :
          if (mbmi->second_ref_frame > 0) {
            this_mv->as_int = seg_mvs[mbmi->ref_frame - 1].as_int;
            this_second_mv->as_int =
              seg_mvs[mbmi->second_ref_frame - 1].as_int;
          }

          thismvcost  = vp9_mv_bit_cost(this_mv, best_ref_mv, mvjcost, mvcost,
                                        102, xd->allow_high_precision_mv);
          if (mbmi->second_ref_frame > 0) {
            thismvcost += vp9_mv_bit_cost(this_second_mv, second_best_ref_mv,
                                          mvjcost, mvcost, 102,
                                          xd->allow_high_precision_mv);
          }
          break;
        case LEFT4X4:
          this_mv->as_int = col ? mic->bmi[i - 1].as_mv[0].as_int :
                                  left_block_mv(xd, mic, i);
          if (mbmi->second_ref_frame > 0)
            this_second_mv->as_int = col ? mic->bmi[i - 1].as_mv[1].as_int :
                                           left_block_second_mv(xd, mic, i);
          break;
        case ABOVE4X4:
          this_mv->as_int = row ? mic->bmi[i - 4].as_mv[0].as_int :
                                  above_block_mv(mic, i, mis);
          if (mbmi->second_ref_frame > 0)
            this_second_mv->as_int = row ? mic->bmi[i - 4].as_mv[1].as_int :
                                           above_block_second_mv(mic, i, mis);
          break;
        case ZERO4X4:
          this_mv->as_int = 0;
          if (mbmi->second_ref_frame > 0)
            this_second_mv->as_int = 0;
          break;
        default:
          break;
      }

      if (m == ABOVE4X4) { // replace above with left if same
        int_mv left_mv, left_second_mv;

        left_second_mv.as_int = 0;
        left_mv.as_int = col ? mic->bmi[i - 1].as_mv[0].as_int :
                         left_block_mv(xd, mic, i);
        if (mbmi->second_ref_frame > 0)
          left_second_mv.as_int = col ? mic->bmi[i - 1].as_mv[1].as_int :
                                  left_block_second_mv(xd, mic, i);

        if (left_mv.as_int == this_mv->as_int &&
            (mbmi->second_ref_frame <= 0 ||
             left_second_mv.as_int == this_second_mv->as_int))
          m = LEFT4X4;
      }

#if CONFIG_NEWBINTRAMODES
      cost = x->inter_bmode_costs[
          m == B_CONTEXT_PRED ? m - CONTEXT_PRED_REPLACEMENTS : m];
#else
      cost = x->inter_bmode_costs[m];
#endif
    }

    mic->bmi[i].as_mv[0].as_int = this_mv->as_int;
    if (mbmi->second_ref_frame > 0)
      mic->bmi[i].as_mv[1].as_int = this_second_mv->as_int;

    x->partition_info->bmi[i].mode = m;
    x->partition_info->bmi[i].mv.as_int = this_mv->as_int;
    if (mbmi->second_ref_frame > 0)
      x->partition_info->bmi[i].second_mv.as_int = this_second_mv->as_int;
  }

  cost += thismvcost;
  return cost;
}

static int64_t encode_inter_mb_segment(VP9_COMMON *const cm,
                                       MACROBLOCK *x,
                                       int const *labels,
                                       int which_label,
                                       int *labelyrate,
                                       int *distortion,
                                       ENTROPY_CONTEXT *ta,
                                       ENTROPY_CONTEXT *tl) {
  int i;
  MACROBLOCKD *xd = &x->e_mbd;

  *labelyrate = 0;
  *distortion = 0;
  for (i = 0; i < 16; i++) {
    if (labels[i] == which_label) {
      const int src_stride = x->plane[0].src.stride;
      uint8_t* const src =
          raster_block_offset_uint8(xd, BLOCK_SIZE_MB16X16, 0, i,
                                    x->plane[0].src.buf, src_stride);
      int16_t* const src_diff =
          raster_block_offset_int16(xd, BLOCK_SIZE_MB16X16, 0, i,
                                    x->plane[0].src_diff);
      int16_t* const coeff = BLOCK_OFFSET(x->plane[0].coeff, 16, i);
      uint8_t* const pre =
          raster_block_offset_uint8(xd, BLOCK_SIZE_MB16X16, 0, i,
                                    xd->plane[0].pre[0].buf,
                                    xd->plane[0].pre[0].stride);
      uint8_t* const dst =
          raster_block_offset_uint8(xd, BLOCK_SIZE_MB16X16, 0, i,
                                    xd->plane[0].dst.buf,
                                    xd->plane[0].dst.stride);
      int thisdistortion;

      vp9_build_inter_predictor(pre,
                                xd->plane[0].pre[0].stride,
                                dst,
                                xd->plane[0].dst.stride,
                                &xd->mode_info_context->bmi[i].as_mv[0],
                                &xd->scale_factor[0],
                                4, 4, 0 /* no avg */, &xd->subpix);

      // TODO(debargha): Make this work properly with the
      // implicit-compoundinter-weight experiment when implicit
      // weighting for splitmv modes is turned on.
      if (xd->mode_info_context->mbmi.second_ref_frame > 0) {
        uint8_t* const second_pre =
          raster_block_offset_uint8(xd, BLOCK_SIZE_MB16X16, 0, i,
                                    xd->plane[0].pre[1].buf,
                                    xd->plane[0].pre[1].stride);
        vp9_build_inter_predictor(
            second_pre, xd->plane[0].pre[1].stride,
            dst, xd->plane[0].dst.stride,
            &xd->mode_info_context->bmi[i].as_mv[1],
            &xd->scale_factor[1], 4, 4, 1,
            &xd->subpix);
      }

      vp9_subtract_block(4, 4, src_diff, 16,
                         src, src_stride,
                         dst, xd->plane[0].dst.stride);
      x->fwd_txm4x4(src_diff, coeff, 32);
      x->quantize_b_4x4(x, i, DCT_DCT, 16);
      thisdistortion = vp9_block_error(coeff,
          BLOCK_OFFSET(xd->plane[0].dqcoeff, i, 16), 16);
      *distortion += thisdistortion;
      *labelyrate += cost_coeffs(cm, x, 0, i, PLANE_TYPE_Y_WITH_DC,
                                 ta + (i & 3),
                                 tl + (i >> 2), TX_4X4, 16);
    }
  }
  *distortion >>= 2;
  return RDCOST(x->rdmult, x->rddiv, *labelyrate, *distortion);
}

static int64_t encode_inter_mb_segment_8x8(VP9_COMMON *const cm,
                                           MACROBLOCK *x,
                                           int const *labels,
                                           int which_label,
                                           int *labelyrate,
                                           int *distortion,
                                           int64_t *otherrd,
                                           ENTROPY_CONTEXT *ta,
                                           ENTROPY_CONTEXT *tl) {
  int i, j;
  MACROBLOCKD *xd = &x->e_mbd;
  const int iblock[4] = { 0, 1, 4, 5 };
  int othercost = 0, otherdist = 0;
  ENTROPY_CONTEXT tac[4], tlc[4];

  if (otherrd) {
    memcpy(&tac, ta, sizeof(tac));
    memcpy(&tlc, tl, sizeof(tlc));
  }

  *distortion = 0;
  *labelyrate = 0;
  for (i = 0; i < 4; i++) {
    int ib = vp9_i8x8_block[i];

    if (labels[ib] == which_label) {
      const int use_second_ref =
          xd->mode_info_context->mbmi.second_ref_frame > 0;
      int which_mv;
      const int idx = (ib & 8) + ((ib & 2) << 1);
      const int src_stride = x->plane[0].src.stride;
      uint8_t* const src =
          raster_block_offset_uint8(xd, BLOCK_SIZE_MB16X16, 0, ib,
                                    x->plane[0].src.buf, src_stride);
      int16_t* const src_diff =
          raster_block_offset_int16(xd, BLOCK_SIZE_MB16X16, 0, ib,
                                    x->plane[0].src_diff);
      int16_t* const coeff = BLOCK_OFFSET(x->plane[0].coeff, idx, 16);
      int thisdistortion;
      uint8_t* const dst =
          raster_block_offset_uint8(xd, BLOCK_SIZE_MB16X16, 0, ib,
                                    xd->plane[0].dst.buf,
                                    xd->plane[0].dst.stride);

      assert(idx < 16);
      for (which_mv = 0; which_mv < 1 + use_second_ref; ++which_mv) {
        uint8_t* const pre =
            raster_block_offset_uint8(xd, BLOCK_SIZE_MB16X16, 0, ib,
                                      xd->plane[0].pre[which_mv].buf,
                                      xd->plane[0].pre[which_mv].stride);

        // TODO(debargha): Make this work properly with the
        // implicit-compoundinter-weight experiment when implicit
        // weighting for splitmv modes is turned on.
        vp9_build_inter_predictor(
            pre, xd->plane[0].pre[which_mv].stride,
            dst, xd->plane[0].dst.stride,
            &xd->mode_info_context->bmi[ib].as_mv[which_mv],
            &xd->scale_factor[which_mv], 8, 8,
            which_mv, &xd->subpix);
      }

      vp9_subtract_block(8, 8, src_diff, 16,
                         src, src_stride,
                         dst, xd->plane[0].dst.stride);

      if (xd->mode_info_context->mbmi.txfm_size == TX_4X4) {
        if (otherrd) {
          x->fwd_txm8x8(src_diff, coeff, 32);
          x->quantize_b_8x8(x, idx, DCT_DCT, 16);
          thisdistortion = vp9_block_error_c(coeff,
              BLOCK_OFFSET(xd->plane[0].dqcoeff, idx, 16), 64);
          otherdist += thisdistortion;
          xd->mode_info_context->mbmi.txfm_size = TX_8X8;
          othercost += cost_coeffs(cm, x, 0, idx, PLANE_TYPE_Y_WITH_DC,
                                   tac + (i & 1) * 2,
                                   tlc + (i & 2),
                                   TX_8X8, 16);
          xd->mode_info_context->mbmi.txfm_size = TX_4X4;
        }
        for (j = 0; j < 4; j += 2) {
          int16_t* const src_diff =
              raster_block_offset_int16(xd, BLOCK_SIZE_MB16X16,
                                        0, ib + iblock[j],
                                        x->plane[0].src_diff);
          int16_t* const coeff = BLOCK_OFFSET(x->plane[0].coeff,
                                              ib + iblock[j], 16);
          x->fwd_txm8x4(src_diff, coeff, 32);
          x->quantize_b_4x4_pair(x, ib + iblock[j], ib + iblock[j] + 1, 16);
          thisdistortion = vp9_block_error_c(coeff,
              BLOCK_OFFSET(xd->plane[0].dqcoeff, ib + iblock[j], 16), 32);
          *distortion += thisdistortion;
          *labelyrate +=
              cost_coeffs(cm, x, 0, ib + iblock[j], PLANE_TYPE_Y_WITH_DC,
                          ta + (i & 1) * 2,
                          tl + (i & 2) + ((j & 2) >> 1),
                          TX_4X4, 16);
          *labelyrate +=
              cost_coeffs(cm, x, 0, ib + iblock[j] + 1,
                          PLANE_TYPE_Y_WITH_DC,
                          ta + (i & 1) * 2 + 1,
                          tl + (i & 2) + ((j & 2) >> 1),
                          TX_4X4, 16);
        }
      } else /* 8x8 */ {
        if (otherrd) {
          for (j = 0; j < 4; j += 2) {
            int16_t* const src_diff =
                raster_block_offset_int16(xd, BLOCK_SIZE_MB16X16,
                                          0, ib + iblock[j],
                                          x->plane[0].src_diff);
            int16_t* const coeff = BLOCK_OFFSET(x->plane[0].coeff,
                                                ib + iblock[j], 16);
            x->fwd_txm8x4(src_diff, coeff, 32);
            x->quantize_b_4x4_pair(x, ib + iblock[j], ib + iblock[j] + 1, 16);
            thisdistortion = vp9_block_error_c(coeff,
                BLOCK_OFFSET(xd->plane[0].dqcoeff, ib + iblock[j], 16), 32);
            otherdist += thisdistortion;
            xd->mode_info_context->mbmi.txfm_size = TX_4X4;
            othercost +=
                cost_coeffs(cm, x, 0, ib + iblock[j], PLANE_TYPE_Y_WITH_DC,
                            tac + (i & 1) * 2,
                            tlc + (i & 2) + ((j & 2) >> 1),
                            TX_4X4, 16);
            othercost +=
                cost_coeffs(cm, x, 0, ib + iblock[j] + 1,
                            PLANE_TYPE_Y_WITH_DC,
                            tac + (i & 1) * 2 + 1,
                            tlc + (i & 2) + ((j & 2) >> 1),
                            TX_4X4, 16);
            xd->mode_info_context->mbmi.txfm_size = TX_8X8;
          }
        }
        x->fwd_txm8x8(src_diff, coeff, 32);
        x->quantize_b_8x8(x, idx, DCT_DCT, 16);
        thisdistortion = vp9_block_error_c(coeff,
            BLOCK_OFFSET(xd->plane[0].dqcoeff, idx, 16), 64);
        *distortion += thisdistortion;
        *labelyrate += cost_coeffs(cm, x, 0, idx, PLANE_TYPE_Y_WITH_DC,
                                   ta + (i & 1) * 2,
                                   tl + (i & 2),
                                   TX_8X8, 16);
      }
    }
  }
  *distortion >>= 2;
  if (otherrd) {
    otherdist >>= 2;
    *otherrd = RDCOST(x->rdmult, x->rddiv, othercost, otherdist);
  }
  return RDCOST(x->rdmult, x->rddiv, *labelyrate, *distortion);
}

static const unsigned int segmentation_to_sseshift[4] = {3, 3, 2, 0};


typedef struct {
  int_mv *ref_mv, *second_ref_mv;
  int_mv mvp;

  int64_t segment_rd;
  SPLITMV_PARTITIONING_TYPE segment_num;
  TX_SIZE txfm_size;
  int r;
  int d;
  int segment_yrate;
  B_PREDICTION_MODE modes[16];
  int_mv mvs[16], second_mvs[16];
  int eobs[16];

  int mvthresh;
  int *mdcounts;

  int_mv sv_mvp[4];     // save 4 mvp from 8x8
  int sv_istep[2];  // save 2 initial step_param for 16x8/8x16

} BEST_SEG_INFO;

static void rd_check_segment_txsize(VP9_COMP *cpi, MACROBLOCK *x,
                                    BEST_SEG_INFO *bsi,
                                    SPLITMV_PARTITIONING_TYPE segmentation,
                                    TX_SIZE tx_size, int64_t *otherrds,
                                    int64_t *rds, int *completed,
                                    /* 16 = n_blocks */
                                    int_mv seg_mvs[16 /* n_blocks */]
                                                  [MAX_REF_FRAMES - 1]) {
  int i, j;
  int const *labels;
  int br = 0, bd = 0;
  B_PREDICTION_MODE this_mode;
  MB_MODE_INFO * mbmi = &x->e_mbd.mode_info_context->mbmi;

  int label_count;
  int64_t this_segment_rd = 0, other_segment_rd;
  int label_mv_thresh;
  int rate = 0;
  int sbr = 0, sbd = 0;
  int segmentyrate = 0;
  int best_eobs[16] = { 0 };

  vp9_variance_fn_ptr_t *v_fn_ptr;

  ENTROPY_CONTEXT t_above[4], t_left[4];
  ENTROPY_CONTEXT t_above_b[4], t_left_b[4];

  vpx_memcpy(t_above, x->e_mbd.plane[0].above_context, sizeof(t_above));
  vpx_memcpy(t_left, x->e_mbd.plane[0].left_context, sizeof(t_left));

  v_fn_ptr = &cpi->fn_ptr[segmentation];
  labels = vp9_mbsplits[segmentation];
  label_count = vp9_mbsplit_count[segmentation];

  // 64 makes this threshold really big effectively
  // making it so that we very rarely check mvs on
  // segments.   setting this to 1 would make mv thresh
  // roughly equal to what it is for macroblocks
  label_mv_thresh = 1 * bsi->mvthresh / label_count;

  // Segmentation method overheads
  rate = cost_token(vp9_mbsplit_tree, vp9_mbsplit_probs,
                    vp9_mbsplit_encodings + segmentation);
  rate += vp9_cost_mv_ref(cpi, SPLITMV,
                          mbmi->mb_mode_context[mbmi->ref_frame]);
  this_segment_rd += RDCOST(x->rdmult, x->rddiv, rate, 0);
  br += rate;
  other_segment_rd = this_segment_rd;

  mbmi->txfm_size = tx_size;
  for (i = 0; i < label_count && this_segment_rd < bsi->segment_rd; i++) {
    int_mv mode_mv[B_MODE_COUNT], second_mode_mv[B_MODE_COUNT];
    int64_t best_label_rd = INT64_MAX, best_other_rd = INT64_MAX;
    B_PREDICTION_MODE mode_selected = ZERO4X4;
    int bestlabelyrate = 0;

    // search for the best motion vector on this segment
    for (this_mode = LEFT4X4; this_mode <= NEW4X4; this_mode ++) {
      int64_t this_rd, other_rd;
      int distortion;
      int labelyrate;
      ENTROPY_CONTEXT t_above_s[4], t_left_s[4];

      vpx_memcpy(t_above_s, t_above, sizeof(t_above_s));
      vpx_memcpy(t_left_s, t_left, sizeof(t_left_s));

      // motion search for newmv (single predictor case only)
      if (mbmi->second_ref_frame <= 0 && this_mode == NEW4X4) {
        int sseshift, n;
        int step_param = 0;
        int further_steps;
        int thissme, bestsme = INT_MAX;
        const struct buf_2d orig_src = x->plane[0].src;
        const struct buf_2d orig_pre = x->e_mbd.plane[0].pre[0];

        /* Is the best so far sufficiently good that we cant justify doing
         * and new motion search. */
        if (best_label_rd < label_mv_thresh)
          break;

        if (cpi->compressor_speed) {
          if (segmentation == PARTITIONING_8X16 ||
              segmentation == PARTITIONING_16X8) {
            bsi->mvp.as_int = bsi->sv_mvp[i].as_int;
            if (i == 1 && segmentation == PARTITIONING_16X8)
              bsi->mvp.as_int = bsi->sv_mvp[2].as_int;

            step_param = bsi->sv_istep[i];
          }

          // use previous block's result as next block's MV predictor.
          if (segmentation == PARTITIONING_4X4 && i > 0) {
            bsi->mvp.as_int =
              x->e_mbd.mode_info_context->bmi[i - 1].as_mv[0].as_int;
            if (i == 4 || i == 8 || i == 12)
              bsi->mvp.as_int =
                x->e_mbd.mode_info_context->bmi[i - 4].as_mv[0].as_int;
            step_param = 2;
          }
        }

        further_steps = (MAX_MVSEARCH_STEPS - 1) - step_param;

        {
          int sadpb = x->sadperbit4;
          int_mv mvp_full;

          mvp_full.as_mv.row = bsi->mvp.as_mv.row >> 3;
          mvp_full.as_mv.col = bsi->mvp.as_mv.col >> 3;

          // find first label
          n = vp9_mbsplit_offset[segmentation][i];

          // adjust src pointer for this segment
          x->plane[0].src.buf =
              raster_block_offset_uint8(&x->e_mbd, BLOCK_SIZE_MB16X16, 0, n,
                                        x->plane[0].src.buf,
                                        x->plane[0].src.stride);
          assert(((intptr_t)x->e_mbd.plane[0].pre[0].buf & 0xf) == 0);
          x->e_mbd.plane[0].pre[0].buf =
              raster_block_offset_uint8(&x->e_mbd, BLOCK_SIZE_MB16X16, 0, n,
                                        x->e_mbd.plane[0].pre[0].buf,
                                        x->e_mbd.plane[0].pre[0].stride);

          bestsme = vp9_full_pixel_diamond(cpi, x, &mvp_full, step_param,
                                           sadpb, further_steps, 0, v_fn_ptr,
                                           bsi->ref_mv, &mode_mv[NEW4X4]);

          sseshift = segmentation_to_sseshift[segmentation];

          // Should we do a full search (best quality only)
          if ((cpi->compressor_speed == 0) && (bestsme >> sseshift) > 4000) {
            /* Check if mvp_full is within the range. */
            clamp_mv(&mvp_full, x->mv_col_min, x->mv_col_max,
                     x->mv_row_min, x->mv_row_max);

            thissme = cpi->full_search_sad(x, &mvp_full,
                                           sadpb, 16, v_fn_ptr,
                                           x->nmvjointcost, x->mvcost,
                                           bsi->ref_mv,
                                           n);

            if (thissme < bestsme) {
              bestsme = thissme;
              mode_mv[NEW4X4].as_int =
                x->e_mbd.mode_info_context->bmi[n].as_mv[0].as_int;
            } else {
              /* The full search result is actually worse so re-instate the
               * previous best vector */
              x->e_mbd.mode_info_context->bmi[n].as_mv[0].as_int =
                mode_mv[NEW4X4].as_int;
            }
          }
        }

        if (bestsme < INT_MAX) {
          int distortion;
          unsigned int sse;
          cpi->find_fractional_mv_step(x, &mode_mv[NEW4X4],
                                       bsi->ref_mv, x->errorperbit, v_fn_ptr,
                                       x->nmvjointcost, x->mvcost,
                                       &distortion, &sse);

          // safe motion search result for use in compound prediction
          seg_mvs[i][mbmi->ref_frame - 1].as_int = mode_mv[NEW4X4].as_int;
        }

        // restore src pointers
        x->plane[0].src = orig_src;
        x->e_mbd.plane[0].pre[0] = orig_pre;
      } else if (mbmi->second_ref_frame > 0 && this_mode == NEW4X4) {
        /* NEW4X4 */
        /* motion search not completed? Then skip newmv for this block with
         * comppred */
        if (seg_mvs[i][mbmi->second_ref_frame - 1].as_int == INVALID_MV ||
            seg_mvs[i][mbmi->ref_frame        - 1].as_int == INVALID_MV) {
          continue;
        }
      }

      rate = labels2mode(x, labels, i, this_mode, &mode_mv[this_mode],
                         &second_mode_mv[this_mode], seg_mvs[i],
                         bsi->ref_mv, bsi->second_ref_mv, x->nmvjointcost,
                         x->mvcost, cpi);

      // Trap vectors that reach beyond the UMV borders
      if (((mode_mv[this_mode].as_mv.row >> 3) < x->mv_row_min) ||
          ((mode_mv[this_mode].as_mv.row >> 3) > x->mv_row_max) ||
          ((mode_mv[this_mode].as_mv.col >> 3) < x->mv_col_min) ||
          ((mode_mv[this_mode].as_mv.col >> 3) > x->mv_col_max)) {
        continue;
      }
      if (mbmi->second_ref_frame > 0 &&
          mv_check_bounds(x, &second_mode_mv[this_mode]))
        continue;

      if (segmentation == PARTITIONING_4X4) {
        this_rd = encode_inter_mb_segment(&cpi->common,
                                          x, labels, i, &labelyrate,
                                          &distortion, t_above_s, t_left_s);
        other_rd = this_rd;
      } else {
        this_rd = encode_inter_mb_segment_8x8(&cpi->common,
                                              x, labels, i, &labelyrate,
                                              &distortion, &other_rd,
                                              t_above_s, t_left_s);
      }
      this_rd += RDCOST(x->rdmult, x->rddiv, rate, 0);
      rate += labelyrate;

      if (this_rd < best_label_rd) {
        sbr = rate;
        sbd = distortion;
        bestlabelyrate = labelyrate;
        mode_selected = this_mode;
        best_label_rd = this_rd;
        if (x->e_mbd.mode_info_context->mbmi.txfm_size == TX_4X4) {
          for (j = 0; j < 16; j++)
            if (labels[j] == i)
              best_eobs[j] = x->e_mbd.plane[0].eobs[j];
        } else {
          for (j = 0; j < 4; j++) {
            int ib = vp9_i8x8_block[j], idx = j * 4;

            if (labels[ib] == i)
              best_eobs[idx] = x->e_mbd.plane[0].eobs[idx];
          }
        }
        if (other_rd < best_other_rd)
          best_other_rd = other_rd;

        vpx_memcpy(t_above_b, t_above_s, sizeof(t_above_s));
        vpx_memcpy(t_left_b, t_left_s, sizeof(t_left_s));

      }
    } /*for each 4x4 mode*/

    vpx_memcpy(t_above, t_above_b, sizeof(t_above));
    vpx_memcpy(t_left, t_left_b, sizeof(t_left));

    labels2mode(x, labels, i, mode_selected, &mode_mv[mode_selected],
                &second_mode_mv[mode_selected], seg_mvs[i],
                bsi->ref_mv, bsi->second_ref_mv, x->nmvjointcost,
                x->mvcost, cpi);

    br += sbr;
    bd += sbd;
    segmentyrate += bestlabelyrate;
    this_segment_rd += best_label_rd;
    other_segment_rd += best_other_rd;
    if (rds)
      rds[i] = this_segment_rd;
    if (otherrds)
      otherrds[i] = other_segment_rd;
  } /* for each label */

  if (this_segment_rd < bsi->segment_rd) {
    bsi->r = br;
    bsi->d = bd;
    bsi->segment_yrate = segmentyrate;
    bsi->segment_rd = this_segment_rd;
    bsi->segment_num = segmentation;
    bsi->txfm_size = mbmi->txfm_size;

    // store everything needed to come back to this!!
    for (i = 0; i < 16; i++) {
      bsi->mvs[i].as_mv = x->partition_info->bmi[i].mv.as_mv;
      if (mbmi->second_ref_frame > 0)
        bsi->second_mvs[i].as_mv = x->partition_info->bmi[i].second_mv.as_mv;
      bsi->modes[i] = x->partition_info->bmi[i].mode;
      bsi->eobs[i] = best_eobs[i];
    }
  }

  if (completed) {
    *completed = i;
  }
}

static void rd_check_segment(VP9_COMP *cpi, MACROBLOCK *x,
                             BEST_SEG_INFO *bsi,
                             unsigned int segmentation,
                             /* 16 = n_blocks */
                             int_mv seg_mvs[16][MAX_REF_FRAMES - 1],
                             int64_t txfm_cache[NB_TXFM_MODES]) {
  int i, n, c = vp9_mbsplit_count[segmentation];

  if (segmentation == PARTITIONING_4X4) {
    int64_t rd[16];

    rd_check_segment_txsize(cpi, x, bsi, segmentation, TX_4X4, NULL,
                            rd, &n, seg_mvs);
    if (n == c) {
      for (i = 0; i < NB_TXFM_MODES; i++) {
        if (rd[c - 1] < txfm_cache[i])
          txfm_cache[i] = rd[c - 1];
      }
    }
  } else {
    int64_t diff, base_rd;
    int cost4x4 = vp9_cost_bit(cpi->common.prob_tx[0], 0);
    int cost8x8 = vp9_cost_bit(cpi->common.prob_tx[0], 1);

    if (cpi->common.txfm_mode == TX_MODE_SELECT) {
      int64_t rd4x4[4], rd8x8[4];
      int n4x4, n8x8, nmin;
      BEST_SEG_INFO bsi4x4, bsi8x8;

      /* factor in cost of cost4x4/8x8 in decision */
      vpx_memcpy(&bsi4x4, bsi, sizeof(*bsi));
      vpx_memcpy(&bsi8x8, bsi, sizeof(*bsi));
      rd_check_segment_txsize(cpi, x, &bsi4x4, segmentation,
                              TX_4X4, NULL, rd4x4, &n4x4, seg_mvs);
      rd_check_segment_txsize(cpi, x, &bsi8x8, segmentation,
                              TX_8X8, NULL, rd8x8, &n8x8, seg_mvs);
      if (bsi4x4.segment_num == segmentation) {
        bsi4x4.segment_rd += RDCOST(x->rdmult, x->rddiv, cost4x4, 0);
        if (bsi4x4.segment_rd < bsi->segment_rd)
          vpx_memcpy(bsi, &bsi4x4, sizeof(*bsi));
      }
      if (bsi8x8.segment_num == segmentation) {
        bsi8x8.segment_rd += RDCOST(x->rdmult, x->rddiv, cost8x8, 0);
        if (bsi8x8.segment_rd < bsi->segment_rd)
          vpx_memcpy(bsi, &bsi8x8, sizeof(*bsi));
      }
      n = n4x4 > n8x8 ? n4x4 : n8x8;
      if (n == c) {
        nmin = n4x4 < n8x8 ? n4x4 : n8x8;
        diff = rd8x8[nmin - 1] - rd4x4[nmin - 1];
        if (n == n4x4) {
          base_rd = rd4x4[c - 1];
        } else {
          base_rd = rd8x8[c - 1] - diff;
        }
      }
    } else {
      int64_t rd[4], otherrd[4];

      if (cpi->common.txfm_mode == ONLY_4X4) {
        rd_check_segment_txsize(cpi, x, bsi, segmentation, TX_4X4, otherrd,
                                rd, &n, seg_mvs);
        if (n == c) {
          base_rd = rd[c - 1];
          diff = otherrd[c - 1] - rd[c - 1];
        }
      } else /* use 8x8 transform */ {
        rd_check_segment_txsize(cpi, x, bsi, segmentation, TX_8X8, otherrd,
                                rd, &n, seg_mvs);
        if (n == c) {
          diff = rd[c - 1] - otherrd[c - 1];
          base_rd = otherrd[c - 1];
        }
      }
    }

    if (n == c) {
      if (base_rd < txfm_cache[ONLY_4X4]) {
        txfm_cache[ONLY_4X4] = base_rd;
      }
      if (base_rd + diff < txfm_cache[ALLOW_8X8]) {
        txfm_cache[ALLOW_8X8] = txfm_cache[ALLOW_16X16] =
            txfm_cache[ALLOW_32X32] = base_rd + diff;
      }
      if (diff < 0) {
        base_rd += diff + RDCOST(x->rdmult, x->rddiv, cost8x8, 0);
      } else {
        base_rd += RDCOST(x->rdmult, x->rddiv, cost4x4, 0);
      }
      if (base_rd < txfm_cache[TX_MODE_SELECT]) {
        txfm_cache[TX_MODE_SELECT] = base_rd;
      }
    }
  }
}

static INLINE void cal_step_param(int sr, int *sp) {
  int step = 0;

  if (sr > MAX_FIRST_STEP) sr = MAX_FIRST_STEP;
  else if (sr < 1) sr = 1;

  while (sr >>= 1)
    step++;

  *sp = MAX_MVSEARCH_STEPS - 1 - step;
}

static int rd_pick_best_mbsegmentation(VP9_COMP *cpi, MACROBLOCK *x,
                                       int_mv *best_ref_mv,
                                       int_mv *second_best_ref_mv,
                                       int64_t best_rd,
                                       int *mdcounts,
                                       int *returntotrate,
                                       int *returnyrate,
                                       int *returndistortion,
                                       int *skippable, int mvthresh,
                                       int_mv seg_mvs[NB_PARTITIONINGS]
                                                     [16 /* n_blocks */]
                                                     [MAX_REF_FRAMES - 1],
                                       int64_t txfm_cache[NB_TXFM_MODES]) {
  int i;
  BEST_SEG_INFO bsi;
  MB_MODE_INFO * mbmi = &x->e_mbd.mode_info_context->mbmi;

  vpx_memset(&bsi, 0, sizeof(bsi));
  for (i = 0; i < NB_TXFM_MODES; i++)
    txfm_cache[i] = INT64_MAX;

  bsi.segment_rd = best_rd;
  bsi.ref_mv = best_ref_mv;
  bsi.second_ref_mv = second_best_ref_mv;
  bsi.mvp.as_int = best_ref_mv->as_int;
  bsi.mvthresh = mvthresh;
  bsi.mdcounts = mdcounts;
  bsi.txfm_size = TX_4X4;

  for (i = 0; i < 16; i++)
    bsi.modes[i] = ZERO4X4;

  if (cpi->compressor_speed == 0) {
    /* for now, we will keep the original segmentation order
       when in best quality mode */
    rd_check_segment(cpi, x, &bsi, PARTITIONING_16X8,
                     seg_mvs[PARTITIONING_16X8], txfm_cache);
    rd_check_segment(cpi, x, &bsi, PARTITIONING_8X16,
                     seg_mvs[PARTITIONING_8X16], txfm_cache);
    rd_check_segment(cpi, x, &bsi, PARTITIONING_8X8,
                     seg_mvs[PARTITIONING_8X8], txfm_cache);
    rd_check_segment(cpi, x, &bsi, PARTITIONING_4X4,
                     seg_mvs[PARTITIONING_4X4], txfm_cache);
  } else {
    int sr;

    rd_check_segment(cpi, x, &bsi, PARTITIONING_8X8,
                     seg_mvs[PARTITIONING_8X8], txfm_cache);

    if (bsi.segment_rd < best_rd) {
      int tmp_col_min = x->mv_col_min;
      int tmp_col_max = x->mv_col_max;
      int tmp_row_min = x->mv_row_min;
      int tmp_row_max = x->mv_row_max;

      vp9_clamp_mv_min_max(x, best_ref_mv);

      /* Get 8x8 result */
      bsi.sv_mvp[0].as_int = bsi.mvs[0].as_int;
      bsi.sv_mvp[1].as_int = bsi.mvs[2].as_int;
      bsi.sv_mvp[2].as_int = bsi.mvs[8].as_int;
      bsi.sv_mvp[3].as_int = bsi.mvs[10].as_int;

      /* Use 8x8 result as 16x8/8x16's predictor MV. Adjust search range
       * according to the closeness of 2 MV. */
      /* block 8X16 */
      sr = MAX(abs(bsi.sv_mvp[0].as_mv.row - bsi.sv_mvp[2].as_mv.row) >> 3,
               abs(bsi.sv_mvp[0].as_mv.col - bsi.sv_mvp[2].as_mv.col) >> 3);
      cal_step_param(sr, &bsi.sv_istep[0]);

      sr = MAX(abs(bsi.sv_mvp[1].as_mv.row - bsi.sv_mvp[3].as_mv.row) >> 3,
               abs(bsi.sv_mvp[1].as_mv.col - bsi.sv_mvp[3].as_mv.col) >> 3);
      cal_step_param(sr, &bsi.sv_istep[1]);

      rd_check_segment(cpi, x, &bsi, PARTITIONING_8X16,
                       seg_mvs[PARTITIONING_8X16], txfm_cache);

      /* block 16X8 */
      sr = MAX(abs(bsi.sv_mvp[0].as_mv.row - bsi.sv_mvp[1].as_mv.row) >> 3,
               abs(bsi.sv_mvp[0].as_mv.col - bsi.sv_mvp[1].as_mv.col) >> 3);
      cal_step_param(sr, &bsi.sv_istep[0]);

      sr = MAX(abs(bsi.sv_mvp[2].as_mv.row - bsi.sv_mvp[3].as_mv.row) >> 3,
               abs(bsi.sv_mvp[2].as_mv.col - bsi.sv_mvp[3].as_mv.col) >> 3);
      cal_step_param(sr, &bsi.sv_istep[1]);

      rd_check_segment(cpi, x, &bsi, PARTITIONING_16X8,
                       seg_mvs[PARTITIONING_16X8], txfm_cache);

      /* If 8x8 is better than 16x8/8x16, then do 4x4 search */
      /* Not skip 4x4 if speed=0 (good quality) */
      if (cpi->sf.no_skip_block4x4_search ||
          bsi.segment_num == PARTITIONING_8X8) {
        /* || (sv_segment_rd8x8-bsi.segment_rd) < sv_segment_rd8x8>>5) */
        bsi.mvp.as_int = bsi.sv_mvp[0].as_int;
        rd_check_segment(cpi, x, &bsi, PARTITIONING_4X4,
                         seg_mvs[PARTITIONING_4X4], txfm_cache);
      }

      /* restore UMV window */
      x->mv_col_min = tmp_col_min;
      x->mv_col_max = tmp_col_max;
      x->mv_row_min = tmp_row_min;
      x->mv_row_max = tmp_row_max;
    }
  }

  /* set it to the best */
  for (i = 0; i < 16; i++) {
    x->e_mbd.mode_info_context->bmi[i].as_mv[0].as_int = bsi.mvs[i].as_int;
    if (mbmi->second_ref_frame > 0)
      x->e_mbd.mode_info_context->bmi[i].as_mv[1].as_int =
        bsi.second_mvs[i].as_int;
    x->e_mbd.plane[0].eobs[i] = bsi.eobs[i];
  }

  /* save partitions */
  mbmi->txfm_size = bsi.txfm_size;
  mbmi->partitioning = bsi.segment_num;
  x->partition_info->count = vp9_mbsplit_count[bsi.segment_num];

  for (i = 0; i < x->partition_info->count; i++) {
    int j;

    j = vp9_mbsplit_offset[bsi.segment_num][i];

    x->partition_info->bmi[i].mode = bsi.modes[j];
    x->partition_info->bmi[i].mv.as_mv = bsi.mvs[j].as_mv;
    if (mbmi->second_ref_frame > 0)
      x->partition_info->bmi[i].second_mv.as_mv = bsi.second_mvs[j].as_mv;
  }
  /*
   * used to set mbmi->mv.as_int
   */
  x->partition_info->bmi[15].mv.as_int = bsi.mvs[15].as_int;
  if (mbmi->second_ref_frame > 0)
    x->partition_info->bmi[15].second_mv.as_int = bsi.second_mvs[15].as_int;

  *returntotrate = bsi.r;
  *returndistortion = bsi.d;
  *returnyrate = bsi.segment_yrate;
  *skippable = vp9_sby_is_skippable(&x->e_mbd, BLOCK_SIZE_MB16X16);

  return (int)(bsi.segment_rd);
}
#endif  // !CONFIG_SB8X8

static void mv_pred(VP9_COMP *cpi, MACROBLOCK *x,
                    uint8_t *ref_y_buffer, int ref_y_stride,
                    int ref_frame, enum BlockSize block_size ) {
  MACROBLOCKD *xd = &x->e_mbd;
  MB_MODE_INFO *mbmi = &xd->mode_info_context->mbmi;
  int_mv this_mv;
  int i;
  int zero_seen = 0;
  int best_index = 0;
  int best_sad = INT_MAX;
  int this_sad = INT_MAX;

  uint8_t *src_y_ptr = x->plane[0].src.buf;
  uint8_t *ref_y_ptr;
  int row_offset, col_offset;

  // Get the sad for each candidate reference mv
  for (i = 0; i < MAX_MV_REF_CANDIDATES; i++) {
    this_mv.as_int = mbmi->ref_mvs[ref_frame][i].as_int;

    // The list is at an end if we see 0 for a second time.
    if (!this_mv.as_int && zero_seen)
      break;
    zero_seen = zero_seen || !this_mv.as_int;

    row_offset = this_mv.as_mv.row >> 3;
    col_offset = this_mv.as_mv.col >> 3;
    ref_y_ptr = ref_y_buffer + (ref_y_stride * row_offset) + col_offset;

    // Find sad for current vector.
    this_sad = cpi->fn_ptr[block_size].sdf(src_y_ptr, x->plane[0].src.stride,
                                           ref_y_ptr, ref_y_stride,
                                           0x7fffffff);

    // Note if it is the best so far.
    if (this_sad < best_sad) {
      best_sad = this_sad;
      best_index = i;
    }
  }

  // Note the index of the mv that worked best in the reference list.
  x->mv_best_ref_index[ref_frame] = best_index;
}

#if !CONFIG_SB8X8
static void set_i8x8_block_modes(MACROBLOCK *x, int modes[4]) {
  int i;
  MACROBLOCKD *xd = &x->e_mbd;
  for (i = 0; i < 4; i++) {
    int ib = vp9_i8x8_block[i];
    xd->mode_info_context->bmi[ib + 0].as_mode.first = modes[i];
    xd->mode_info_context->bmi[ib + 1].as_mode.first = modes[i];
    xd->mode_info_context->bmi[ib + 4].as_mode.first = modes[i];
    xd->mode_info_context->bmi[ib + 5].as_mode.first = modes[i];
    // printf("%d,%d,%d,%d\n",
    //       modes[0], modes[1], modes[2], modes[3]);
  }
}
#endif

extern void vp9_calc_ref_probs(int *count, vp9_prob *probs);
static void estimate_curframe_refprobs(VP9_COMP *cpi, vp9_prob mod_refprobs[3], int pred_ref) {
  int norm_cnt[MAX_REF_FRAMES];
  const int *const rfct = cpi->count_mb_ref_frame_usage;
  int intra_count = rfct[INTRA_FRAME];
  int last_count  = rfct[LAST_FRAME];
  int gf_count    = rfct[GOLDEN_FRAME];
  int arf_count   = rfct[ALTREF_FRAME];

  // Work out modified reference frame probabilities to use where prediction
  // of the reference frame fails
  if (pred_ref == INTRA_FRAME) {
    norm_cnt[0] = 0;
    norm_cnt[1] = last_count;
    norm_cnt[2] = gf_count;
    norm_cnt[3] = arf_count;
    vp9_calc_ref_probs(norm_cnt, mod_refprobs);
    mod_refprobs[0] = 0;    // This branch implicit
  } else if (pred_ref == LAST_FRAME) {
    norm_cnt[0] = intra_count;
    norm_cnt[1] = 0;
    norm_cnt[2] = gf_count;
    norm_cnt[3] = arf_count;
    vp9_calc_ref_probs(norm_cnt, mod_refprobs);
    mod_refprobs[1] = 0;    // This branch implicit
  } else if (pred_ref == GOLDEN_FRAME) {
    norm_cnt[0] = intra_count;
    norm_cnt[1] = last_count;
    norm_cnt[2] = 0;
    norm_cnt[3] = arf_count;
    vp9_calc_ref_probs(norm_cnt, mod_refprobs);
    mod_refprobs[2] = 0;  // This branch implicit
  } else {
    norm_cnt[0] = intra_count;
    norm_cnt[1] = last_count;
    norm_cnt[2] = gf_count;
    norm_cnt[3] = 0;
    vp9_calc_ref_probs(norm_cnt, mod_refprobs);
    mod_refprobs[2] = 0;  // This branch implicit
  }
}

static INLINE unsigned weighted_cost(vp9_prob *tab0, vp9_prob *tab1,
                                     int idx, int val, int weight) {
  unsigned cost0 = tab0[idx] ? vp9_cost_bit(tab0[idx], val) : 0;
  unsigned cost1 = tab1[idx] ? vp9_cost_bit(tab1[idx], val) : 0;
  // weight is 16-bit fixed point, so this basically calculates:
  // 0.5 + weight * cost1 + (1.0 - weight) * cost0
  return (0x8000 + weight * cost1 + (0x10000 - weight) * cost0) >> 16;
}

static void estimate_ref_frame_costs(VP9_COMP *cpi, int segment_id, unsigned int *ref_costs) {
  VP9_COMMON *cm = &cpi->common;
  MACROBLOCKD *xd = &cpi->mb.e_mbd;
  vp9_prob *mod_refprobs;

  unsigned int cost;
  int pred_ref;
  int pred_flag;
  int pred_ctx;
  int i;

  vp9_prob pred_prob, new_pred_prob;
  int seg_ref_active;
  int seg_ref_count = 0;
  seg_ref_active = vp9_segfeature_active(xd,
                                         segment_id,
                                         SEG_LVL_REF_FRAME);

  if (seg_ref_active) {
    seg_ref_count = vp9_check_segref(xd, segment_id, INTRA_FRAME)  +
                    vp9_check_segref(xd, segment_id, LAST_FRAME)   +
                    vp9_check_segref(xd, segment_id, GOLDEN_FRAME) +
                    vp9_check_segref(xd, segment_id, ALTREF_FRAME);
  }

  // Get the predicted reference for this mb
  pred_ref = vp9_get_pred_ref(cm, xd);

  // Get the context probability for the prediction flag (based on last frame)
  pred_prob = vp9_get_pred_prob(cm, xd, PRED_REF);

  // Predict probability for current frame based on stats so far
  pred_ctx = vp9_get_pred_context(cm, xd, PRED_REF);
  new_pred_prob = get_binary_prob(cpi->ref_pred_count[pred_ctx][0],
                                  cpi->ref_pred_count[pred_ctx][1]);

  // Get the set of probabilities to use if prediction fails
  mod_refprobs = cm->mod_refprobs[pred_ref];

  // For each possible selected reference frame work out a cost.
  for (i = 0; i < MAX_REF_FRAMES; i++) {
    if (seg_ref_active && seg_ref_count == 1) {
      cost = 0;
    } else {
      pred_flag = (i == pred_ref);

      // Get the prediction for the current mb
      cost = weighted_cost(&pred_prob, &new_pred_prob, 0,
                           pred_flag, cpi->seg0_progress);
      if (cost > 1024) cost = 768; // i.e. account for 4 bits max.

      // for incorrectly predicted cases
      if (! pred_flag) {
        vp9_prob curframe_mod_refprobs[3];

        if (cpi->seg0_progress) {
          estimate_curframe_refprobs(cpi, curframe_mod_refprobs, pred_ref);
        } else {
          vpx_memset(curframe_mod_refprobs, 0, sizeof(curframe_mod_refprobs));
        }

        cost += weighted_cost(mod_refprobs, curframe_mod_refprobs, 0,
                              (i != INTRA_FRAME), cpi->seg0_progress);
        if (i != INTRA_FRAME) {
          cost += weighted_cost(mod_refprobs, curframe_mod_refprobs, 1,
                                (i != LAST_FRAME), cpi->seg0_progress);
          if (i != LAST_FRAME) {
            cost += weighted_cost(mod_refprobs, curframe_mod_refprobs, 2,
                                  (i != GOLDEN_FRAME), cpi->seg0_progress);
          }
        }
      }
    }

    ref_costs[i] = cost;
  }
}

static void store_coding_context(MACROBLOCK *x, PICK_MODE_CONTEXT *ctx,
                                 int mode_index,
                                 PARTITION_INFO *partition,
                                 int_mv *ref_mv,
                                 int_mv *second_ref_mv,
                                 int64_t comp_pred_diff[NB_PREDICTION_TYPES],
                                 int64_t txfm_size_diff[NB_TXFM_MODES]) {
  MACROBLOCKD *const xd = &x->e_mbd;

  // Take a snapshot of the coding context so it can be
  // restored if we decide to encode this way
  ctx->skip = x->skip;
  ctx->best_mode_index = mode_index;
  vpx_memcpy(&ctx->mic, xd->mode_info_context,
             sizeof(MODE_INFO));
  if (partition)
    vpx_memcpy(&ctx->partition_info, partition,
               sizeof(PARTITION_INFO));
  ctx->best_ref_mv.as_int = ref_mv->as_int;
  ctx->second_best_ref_mv.as_int = second_ref_mv->as_int;

  ctx->single_pred_diff = (int)comp_pred_diff[SINGLE_PREDICTION_ONLY];
  ctx->comp_pred_diff   = (int)comp_pred_diff[COMP_PREDICTION_ONLY];
  ctx->hybrid_pred_diff = (int)comp_pred_diff[HYBRID_PREDICTION];

  memcpy(ctx->txfm_rd_diff, txfm_size_diff, sizeof(ctx->txfm_rd_diff));
}

static void setup_buffer_inter(VP9_COMP *cpi, MACROBLOCK *x,
                               int idx, MV_REFERENCE_FRAME frame_type,
                               enum BlockSize block_size,
                               int mi_row, int mi_col,
                               int_mv frame_nearest_mv[MAX_REF_FRAMES],
                               int_mv frame_near_mv[MAX_REF_FRAMES],
                               int frame_mdcounts[4][4],
                               YV12_BUFFER_CONFIG yv12_mb[4],
                               struct scale_factors scale[MAX_REF_FRAMES]) {
  VP9_COMMON *cm = &cpi->common;
  YV12_BUFFER_CONFIG *yv12 = &cm->yv12_fb[cpi->common.ref_frame_map[idx]];
  MACROBLOCKD *const xd = &x->e_mbd;
  MB_MODE_INFO *const mbmi = &xd->mode_info_context->mbmi;
  int use_prev_in_find_mv_refs;

  // set up scaling factors
  scale[frame_type] = cpi->common.active_ref_scale[frame_type - 1];
  scale[frame_type].x_offset_q4 =
      (mi_col * MI_SIZE * scale[frame_type].x_num /
       scale[frame_type].x_den) & 0xf;
  scale[frame_type].y_offset_q4 =
      (mi_row * MI_SIZE * scale[frame_type].y_num /
       scale[frame_type].y_den) & 0xf;

  // TODO(jkoleszar): Is the UV buffer ever used here? If so, need to make this
  // use the UV scaling factors.
  setup_pred_block(&yv12_mb[frame_type], yv12, mi_row, mi_col,
                   &scale[frame_type], &scale[frame_type]);

  // Gets an initial list of candidate vectors from neighbours and orders them
  use_prev_in_find_mv_refs = cm->width == cm->last_width &&
                             cm->height == cm->last_height &&
                             !cpi->common.error_resilient_mode;
  vp9_find_mv_refs(&cpi->common, xd, xd->mode_info_context,
                   use_prev_in_find_mv_refs ? xd->prev_mode_info_context : NULL,
                   frame_type,
                   mbmi->ref_mvs[frame_type],
                   cpi->common.ref_frame_sign_bias);

  // Candidate refinement carried out at encoder and decoder
  vp9_find_best_ref_mvs(xd,
                        mbmi->ref_mvs[frame_type],
                        &frame_nearest_mv[frame_type],
                        &frame_near_mv[frame_type]);

  // Further refinement that is encode side only to test the top few candidates
  // in full and choose the best as the centre point for subsequent searches.
  // The current implementation doesn't support scaling.
  if (scale[frame_type].x_num == scale[frame_type].x_den &&
      scale[frame_type].y_num == scale[frame_type].y_den)
    mv_pred(cpi, x, yv12_mb[frame_type].y_buffer, yv12->y_stride,
            frame_type, block_size);
}

static void model_rd_from_var_lapndz(int var, int n, int qstep,
                                     int *rate, int *dist) {
  // This function models the rate and distortion for a Laplacian
  // source with given variance when quantized with a uniform quantizer
  // with given stepsize. The closed form expressions are in:
  // Hang and Chen, "Source Model for transform video coder and its
  // application - Part I: Fundamental Theory", IEEE Trans. Circ.
  // Sys. for Video Tech., April 1997.
  // The function is implemented as piecewise approximation to the
  // exact computation.
  // TODO(debargha): Implement the functions by interpolating from a
  // look-up table
  vp9_clear_system_state();
  {
    double D, R;
    double s2 = (double) var / n;
    double s = sqrt(s2);
    double x = qstep / s;
    if (x > 1.0) {
      double y = exp(-x / 2);
      double y2 = y * y;
      D = 2.069981728764738 * y2 - 2.764286806516079 * y + 1.003956960819275;
      R = 0.924056758535089 * y2 + 2.738636469814024 * y - 0.005169662030017;
    } else {
      double x2 = x * x;
      D = 0.075303187668830 * x2 + 0.004296954321112 * x - 0.000413209252807;
      if (x > 0.125)
        R = 1 / (-0.03459733614226 * x2 + 0.36561675733603 * x +
                 0.1626989668625);
      else
        R = -1.442252874826093 * log(x) + 1.944647760719664;
    }
    if (R < 0) {
      *rate = 0;
      *dist = var;
    } else {
      *rate = (n * R * 256 + 0.5);
      *dist = (n * D * s2 + 0.5);
    }
  }
  vp9_clear_system_state();
}

static enum BlockSize y_to_uv_block_size(enum BlockSize bs) {
  switch (bs) {
    case BLOCK_64X64: return BLOCK_32X32;
    case BLOCK_64X32: return BLOCK_32X16;
    case BLOCK_32X64: return BLOCK_16X32;
    case BLOCK_32X32: return BLOCK_16X16;
    case BLOCK_32X16: return BLOCK_16X8;
    case BLOCK_16X32: return BLOCK_8X16;
    case BLOCK_16X16: return BLOCK_8X8;
#if CONFIG_SB8X8
    case BLOCK_16X8:  return BLOCK_8X4;
    case BLOCK_8X16:  return BLOCK_4X8;
    case BLOCK_8X8:   return BLOCK_4X4;
#endif
    default:
      assert(0);
      return -1;
  }
}

static enum BlockSize y_bsizet_to_block_size(BLOCK_SIZE_TYPE bs) {
  switch (bs) {
    case BLOCK_SIZE_SB64X64: return BLOCK_64X64;
    case BLOCK_SIZE_SB64X32: return BLOCK_64X32;
    case BLOCK_SIZE_SB32X64: return BLOCK_32X64;
    case BLOCK_SIZE_SB32X32: return BLOCK_32X32;
    case BLOCK_SIZE_SB32X16: return BLOCK_32X16;
    case BLOCK_SIZE_SB16X32: return BLOCK_16X32;
    case BLOCK_SIZE_MB16X16: return BLOCK_16X16;
#if CONFIG_SB8X8
    case BLOCK_SIZE_SB16X8:  return BLOCK_16X8;
    case BLOCK_SIZE_SB8X16:  return BLOCK_8X16;
    case BLOCK_SIZE_SB8X8:   return BLOCK_8X8;
#endif
    default:
      assert(0);
      return -1;
  }
}

static int64_t handle_inter_mode(VP9_COMP *cpi, MACROBLOCK *x,
                                 BLOCK_SIZE_TYPE bsize,
                                 int mdcounts[4], int64_t txfm_cache[],
                                 int *rate2, int *distortion, int *skippable,
                                 int *compmode_cost,
#if CONFIG_COMP_INTERINTRA_PRED
                                 int *compmode_interintra_cost,
#endif
                                 int *rate_y, int *distortion_y,
                                 int *rate_uv, int *distortion_uv,
                                 int *mode_excluded, int *disable_skip,
                                 int mode_index,
                                 INTERPOLATIONFILTERTYPE *best_filter,
                                 int_mv frame_mv[MB_MODE_COUNT]
                                                [MAX_REF_FRAMES],
                                 YV12_BUFFER_CONFIG *scaled_ref_frame,
                                 int mi_row, int mi_col) {
  const int bw = 1 << mi_width_log2(bsize), bh = 1 << mi_height_log2(bsize);
  const enum BlockSize block_size = y_bsizet_to_block_size(bsize);
  const enum BlockSize uv_block_size = y_to_uv_block_size(block_size);
  VP9_COMMON *cm = &cpi->common;
  MACROBLOCKD *xd = &x->e_mbd;
  MB_MODE_INFO *mbmi = &xd->mode_info_context->mbmi;
  const int is_comp_pred = (mbmi->second_ref_frame > 0);
#if CONFIG_COMP_INTERINTRA_PRED
  const int is_comp_interintra_pred = (mbmi->second_ref_frame == INTRA_FRAME);
#endif
  const int num_refs = is_comp_pred ? 2 : 1;
  const int this_mode = mbmi->mode;
  int i;
  int refs[2] = { mbmi->ref_frame,
                  (mbmi->second_ref_frame < 0 ? 0 : mbmi->second_ref_frame) };
  int_mv cur_mv[2];
  int_mv ref_mv[2];
  int64_t this_rd = 0;
  unsigned char tmp_ybuf[64 * 64];
  unsigned char tmp_ubuf[32 * 32];
  unsigned char tmp_vbuf[32 * 32];
  int pred_exists = 0;
  int interpolating_intpel_seen = 0;
  int intpel_mv;
  int64_t rd, best_rd = INT64_MAX;

  switch (this_mode) {
    case NEWMV:
      ref_mv[0] = mbmi->ref_mvs[refs[0]][0];
      ref_mv[1] = mbmi->ref_mvs[refs[1]][0];

      if (is_comp_pred) {
        if (frame_mv[NEWMV][refs[0]].as_int == INVALID_MV ||
            frame_mv[NEWMV][refs[1]].as_int == INVALID_MV)
          return INT64_MAX;
        *rate2 += vp9_mv_bit_cost(&frame_mv[NEWMV][refs[0]],
                                  &ref_mv[0],
                                  x->nmvjointcost, x->mvcost, 96,
                                  x->e_mbd.allow_high_precision_mv);
        *rate2 += vp9_mv_bit_cost(&frame_mv[NEWMV][refs[1]],
                                  &ref_mv[1],
                                  x->nmvjointcost, x->mvcost, 96,
                                  x->e_mbd.allow_high_precision_mv);
      } else {
        struct buf_2d backup_yv12[MAX_MB_PLANE] = {{0}};
        int bestsme = INT_MAX;
        int further_steps, step_param = cpi->sf.first_step;
        int sadpb = x->sadperbit16;
        int_mv mvp_full, tmp_mv;
        int sr = 0;

        int tmp_col_min = x->mv_col_min;
        int tmp_col_max = x->mv_col_max;
        int tmp_row_min = x->mv_row_min;
        int tmp_row_max = x->mv_row_max;

        if (scaled_ref_frame) {
          int i;

          // Swap out the reference frame for a version that's been scaled to
          // match the resolution of the current frame, allowing the existing
          // motion search code to be used without additional modifications.
          for (i = 0; i < MAX_MB_PLANE; i++)
            backup_yv12[i] = xd->plane[i].pre[0];

          setup_pre_planes(xd, scaled_ref_frame, NULL, mi_row, mi_col,
                           NULL, NULL);
        }

        vp9_clamp_mv_min_max(x, &ref_mv[0]);

        sr = vp9_init_search_range(cpi->common.width, cpi->common.height);

        // mvp_full.as_int = ref_mv[0].as_int;
        mvp_full.as_int =
         mbmi->ref_mvs[refs[0]][x->mv_best_ref_index[refs[0]]].as_int;

        mvp_full.as_mv.col >>= 3;
        mvp_full.as_mv.row >>= 3;

        // adjust search range according to sr from mv prediction
        step_param = MAX(step_param, sr);

        // Further step/diamond searches as necessary
        further_steps = (cpi->sf.max_step_search_steps - 1) - step_param;

        bestsme = vp9_full_pixel_diamond(cpi, x, &mvp_full, step_param,
                                         sadpb, further_steps, 1,
                                         &cpi->fn_ptr[block_size],
                                         &ref_mv[0], &tmp_mv);

        x->mv_col_min = tmp_col_min;
        x->mv_col_max = tmp_col_max;
        x->mv_row_min = tmp_row_min;
        x->mv_row_max = tmp_row_max;

        if (bestsme < INT_MAX) {
          int dis; /* TODO: use dis in distortion calculation later. */
          unsigned int sse;
          cpi->find_fractional_mv_step(x, &tmp_mv,
                                       &ref_mv[0],
                                       x->errorperbit,
                                       &cpi->fn_ptr[block_size],
                                       x->nmvjointcost, x->mvcost,
                                       &dis, &sse);
        }
        frame_mv[NEWMV][refs[0]].as_int =
          xd->mode_info_context->bmi[0].as_mv[0].as_int = tmp_mv.as_int;

        // Add the new motion vector cost to our rolling cost variable
        *rate2 += vp9_mv_bit_cost(&tmp_mv, &ref_mv[0],
                                  x->nmvjointcost, x->mvcost,
                                  96, xd->allow_high_precision_mv);

        // restore the predictor, if required
        if (scaled_ref_frame) {
          int i;

          for (i = 0; i < MAX_MB_PLANE; i++)
            xd->plane[i].pre[0] = backup_yv12[i];
        }
      }
      break;
    case NEARMV:
    case NEARESTMV:
    case ZEROMV:
    default:
      break;
  }
  for (i = 0; i < num_refs; ++i) {
    cur_mv[i] = frame_mv[this_mode][refs[i]];
    // Clip "next_nearest" so that it does not extend to far out of image
    if (this_mode == NEWMV)
      assert(!clamp_mv2(&cur_mv[i], xd));
    else
      clamp_mv2(&cur_mv[i], xd);

    if (mv_check_bounds(x, &cur_mv[i]))
      return INT64_MAX;
    mbmi->mv[i].as_int = cur_mv[i].as_int;
  }


  /* We don't include the cost of the second reference here, because there
   * are only three options: Last/Golden, ARF/Last or Golden/ARF, or in other
   * words if you present them in that order, the second one is always known
   * if the first is known */
  *compmode_cost = vp9_cost_bit(vp9_get_pred_prob(cm, xd, PRED_COMP),
                                is_comp_pred);
  *rate2 += vp9_cost_mv_ref(cpi, this_mode,
                            mbmi->mb_mode_context[mbmi->ref_frame]);
#if CONFIG_COMP_INTERINTRA_PRED
  if (!is_comp_pred) {
    *compmode_interintra_cost = vp9_cost_bit(cm->fc.interintra_prob,
                                             is_comp_interintra_pred);
    if (is_comp_interintra_pred) {
      *compmode_interintra_cost +=
          x->mbmode_cost[xd->frame_type][mbmi->interintra_mode];
#if SEPARATE_INTERINTRA_UV
      *compmode_interintra_cost +=
          x->intra_uv_mode_cost[xd->frame_type][mbmi->interintra_uv_mode];
#endif
    }
  }
#endif

  pred_exists = 0;
  interpolating_intpel_seen = 0;
  // Are all MVs integer pel for Y and UV
  intpel_mv = (mbmi->mv[0].as_mv.row & 15) == 0 &&
              (mbmi->mv[0].as_mv.col & 15) == 0;
  if (is_comp_pred)
    intpel_mv &= (mbmi->mv[1].as_mv.row & 15) == 0 &&
                 (mbmi->mv[1].as_mv.col & 15) == 0;
  // Search for best switchable filter by checking the variance of
  // pred error irrespective of whether the filter will be used
  if (1) {
    int switchable_filter_index, newbest;
    int tmp_rate_y_i = 0, tmp_rate_u_i = 0, tmp_rate_v_i = 0;
    int tmp_dist_y_i = 0, tmp_dist_u_i = 0, tmp_dist_v_i = 0;
    for (switchable_filter_index = 0;
         switchable_filter_index < VP9_SWITCHABLE_FILTERS;
         ++switchable_filter_index) {
      int rs = 0;
      mbmi->interp_filter = vp9_switchable_interp[switchable_filter_index];
      vp9_setup_interp_filters(xd, mbmi->interp_filter, &cpi->common);

      if (cpi->common.mcomp_filter_type == SWITCHABLE) {
        const int c = vp9_get_pred_context(cm, xd, PRED_SWITCHABLE_INTERP);
        const int m = vp9_switchable_interp_map[mbmi->interp_filter];
        rs = SWITCHABLE_INTERP_RATE_FACTOR * x->switchable_interp_costs[c][m];
      }
      if (interpolating_intpel_seen && intpel_mv &&
          vp9_is_interpolating_filter[mbmi->interp_filter]) {
        rd = RDCOST(x->rdmult, x->rddiv,
                    rs + tmp_rate_y_i + tmp_rate_u_i + tmp_rate_v_i,
                    tmp_dist_y_i + tmp_dist_u_i + tmp_dist_v_i);
      } else {
        unsigned int sse, var;
        int tmp_rate_y, tmp_rate_u, tmp_rate_v;
        int tmp_dist_y, tmp_dist_u, tmp_dist_v;
        vp9_build_inter_predictors_sb(xd, mi_row, mi_col, bsize);
        var = cpi->fn_ptr[block_size].vf(x->plane[0].src.buf,
                                         x->plane[0].src.stride,
                                         xd->plane[0].dst.buf,
                                         xd->plane[0].dst.stride,
                                         &sse);
        // Note our transform coeffs are 8 times an orthogonal transform.
        // Hence quantizer step is also 8 times. To get effective quantizer
        // we need to divide by 8 before sending to modeling function.
        model_rd_from_var_lapndz(var, MI_SIZE * bw * MI_SIZE * bh,
                                 xd->plane[0].dequant[1] >> 3,
                                 &tmp_rate_y, &tmp_dist_y);
        var = cpi->fn_ptr[uv_block_size].vf(x->plane[1].src.buf,
                                            x->plane[1].src.stride,
                                            xd->plane[1].dst.buf,
                                            xd->plane[1].dst.stride,
                                            &sse);
        model_rd_from_var_lapndz(var, MI_UV_SIZE * bw * MI_UV_SIZE * bh,
                                 xd->plane[1].dequant[1] >> 3,
                                 &tmp_rate_u, &tmp_dist_u);
        var = cpi->fn_ptr[uv_block_size].vf(x->plane[2].src.buf,
                                            x->plane[1].src.stride,
                                            xd->plane[2].dst.buf,
                                            xd->plane[1].dst.stride,
                                            &sse);
        model_rd_from_var_lapndz(var, MI_UV_SIZE * bw * MI_UV_SIZE * bh,
                                 xd->plane[2].dequant[1] >> 3,
                                 &tmp_rate_v, &tmp_dist_v);
        rd = RDCOST(x->rdmult, x->rddiv,
                    rs + tmp_rate_y + tmp_rate_u + tmp_rate_v,
                    tmp_dist_y + tmp_dist_u + tmp_dist_v);
        if (!interpolating_intpel_seen && intpel_mv &&
            vp9_is_interpolating_filter[mbmi->interp_filter]) {
          tmp_rate_y_i = tmp_rate_y;
          tmp_rate_u_i = tmp_rate_u;
          tmp_rate_v_i = tmp_rate_v;
          tmp_dist_y_i = tmp_dist_y;
          tmp_dist_u_i = tmp_dist_u;
          tmp_dist_v_i = tmp_dist_v;
        }
      }
      newbest = (switchable_filter_index == 0 || rd < best_rd);
      if (newbest) {
        best_rd = rd;
        *best_filter = mbmi->interp_filter;
      }
      if ((cm->mcomp_filter_type == SWITCHABLE && newbest) ||
          (cm->mcomp_filter_type != SWITCHABLE &&
           cm->mcomp_filter_type == mbmi->interp_filter)) {
        int i;
        for (i = 0; i < MI_SIZE * bh; ++i)
          vpx_memcpy(tmp_ybuf + i * MI_SIZE * bw,
                     xd->plane[0].dst.buf + i * xd->plane[0].dst.stride,
                     sizeof(unsigned char) * MI_SIZE * bw);
        for (i = 0; i < MI_UV_SIZE * bh; ++i)
          vpx_memcpy(tmp_ubuf + i * MI_UV_SIZE * bw,
                     xd->plane[1].dst.buf + i * xd->plane[1].dst.stride,
                     sizeof(unsigned char) * MI_UV_SIZE * bw);
        for (i = 0; i < MI_UV_SIZE * bh; ++i)
          vpx_memcpy(tmp_vbuf + i * MI_UV_SIZE * bw,
                     xd->plane[2].dst.buf + i * xd->plane[1].dst.stride,
                     sizeof(unsigned char) * MI_UV_SIZE * bw);
        pred_exists = 1;
      }
      interpolating_intpel_seen |=
        intpel_mv && vp9_is_interpolating_filter[mbmi->interp_filter];
    }
  }

  // Set the appripriate filter
  if (cm->mcomp_filter_type != SWITCHABLE)
    mbmi->interp_filter = cm->mcomp_filter_type;
  else
    mbmi->interp_filter = *best_filter;
  vp9_setup_interp_filters(xd, mbmi->interp_filter, &cpi->common);

  if (pred_exists) {
    // FIXME(rbultje): mb code still predicts into xd->predictor
    for (i = 0; i < bh * MI_SIZE; ++i)
      vpx_memcpy(xd->plane[0].dst.buf + i * xd->plane[0].dst.stride,
                 tmp_ybuf + i * bw * MI_SIZE,
                 sizeof(unsigned char) * bw * MI_SIZE);
    for (i = 0; i < bh * MI_UV_SIZE; ++i)
      vpx_memcpy(xd->plane[1].dst.buf + i * xd->plane[1].dst.stride,
                 tmp_ubuf + i * bw * MI_UV_SIZE,
                 sizeof(unsigned char) * bw * MI_UV_SIZE);
    for (i = 0; i < bh * MI_UV_SIZE; ++i)
      vpx_memcpy(xd->plane[2].dst.buf + i * xd->plane[1].dst.stride,
                 tmp_vbuf + i * bw * MI_UV_SIZE,
                 sizeof(unsigned char) * bw * MI_UV_SIZE);
  } else {
    // Handles the special case when a filter that is not in the
    // switchable list (ex. bilinear, 6-tap) is indicated at the frame level
    vp9_build_inter_predictors_sb(xd, mi_row, mi_col, bsize);
  }

  if (cpi->common.mcomp_filter_type == SWITCHABLE) {
    const int c = vp9_get_pred_context(cm, xd, PRED_SWITCHABLE_INTERP);
    const int m = vp9_switchable_interp_map[mbmi->interp_filter];
    *rate2 += SWITCHABLE_INTERP_RATE_FACTOR * x->switchable_interp_costs[c][m];
  }

  if (cpi->active_map_enabled && x->active_ptr[0] == 0)
    x->skip = 1;
  else if (x->encode_breakout) {
    unsigned int var, sse;
    int threshold = (xd->plane[0].dequant[1]
                     * xd->plane[0].dequant[1] >> 4);

    if (threshold < x->encode_breakout)
      threshold = x->encode_breakout;

    if (bsize != BLOCK_SIZE_MB16X16) {
      var = cpi->fn_ptr[block_size].vf(x->plane[0].src.buf,
                                       x->plane[0].src.stride,
                                       xd->plane[0].dst.buf,
                                       xd->plane[0].dst.stride,
                                       &sse);
    } else {
      var = vp9_variance16x16(x->plane[0].src.buf, x->plane[0].src.stride,
                              xd->plane[0].dst.buf, xd->plane[0].dst.stride,
                              &sse);
    }

    if ((int)sse < threshold) {
      unsigned int q2dc = xd->plane[0].dequant[0];
      /* If there is no codeable 2nd order dc
         or a very small uniform pixel change change */
      if ((sse - var < q2dc * q2dc >> 4) ||
          (sse / 2 > var && sse - var < 64)) {
        // Check u and v to make sure skip is ok
        int sse2;

        if (bsize != BLOCK_SIZE_MB16X16) {
          unsigned int sse2u, sse2v;
          // FIXME(rbultje): mb predictors predict into xd->predictor
          var = cpi->fn_ptr[uv_block_size].vf(x->plane[1].src.buf,
                                              x->plane[1].src.stride,
                                              xd->plane[1].dst.buf,
                                              xd->plane[1].dst.stride, &sse2u);
          var = cpi->fn_ptr[uv_block_size].vf(x->plane[2].src.buf,
                                              x->plane[1].src.stride,
                                              xd->plane[2].dst.buf,
                                              xd->plane[1].dst.stride, &sse2v);
          sse2 = sse2u + sse2v;
        } else {
          unsigned int sse2u, sse2v;
          var = vp9_variance8x8(x->plane[1].src.buf, x->plane[1].src.stride,
                                xd->plane[1].dst.buf, xd->plane[1].dst.stride,
                                &sse2u);
          var = vp9_variance8x8(x->plane[2].src.buf, x->plane[1].src.stride,
                                xd->plane[2].dst.buf, xd->plane[1].dst.stride,
                                &sse2v);
          sse2 = sse2u + sse2v;
        }

        if (sse2 * 2 < threshold) {
          x->skip = 1;
          *distortion = sse + sse2;
          *rate2 = 500;

          /* for best_yrd calculation */
          *rate_uv = 0;
          *distortion_uv = sse2;

          *disable_skip = 1;
          this_rd = RDCOST(x->rdmult, x->rddiv, *rate2, *distortion);
        }
      }
    }
  }

  if (!x->skip) {
    int skippable_y, skippable_uv;

    // Y cost and distortion
    super_block_yrd(cpi, x, rate_y, distortion_y, &skippable_y,
                    bsize, txfm_cache);
    *rate2 += *rate_y;
    *distortion += *distortion_y;

    super_block_uvrd(cm, x, rate_uv, distortion_uv,
                     &skippable_uv, bsize);

    *rate2 += *rate_uv;
    *distortion += *distortion_uv;
    *skippable = skippable_y && skippable_uv;
  }

  if (!(*mode_excluded)) {
    if (is_comp_pred) {
      *mode_excluded = (cpi->common.comp_pred_mode == SINGLE_PREDICTION_ONLY);
    } else {
      *mode_excluded = (cpi->common.comp_pred_mode == COMP_PREDICTION_ONLY);
    }
#if CONFIG_COMP_INTERINTRA_PRED
    if (is_comp_interintra_pred && !cm->use_interintra) *mode_excluded = 1;
#endif
  }

  return this_rd;  // if 0, this will be re-calculated by caller
}

#if !CONFIG_SB8X8
static void rd_pick_inter_mode(VP9_COMP *cpi, MACROBLOCK *x,
                               int mi_row, int mi_col,
                               int *returnrate, int *returndistortion,
                               int64_t *returnintra) {
  static const int flag_list[4] = { 0, VP9_LAST_FLAG, VP9_GOLD_FLAG,
    VP9_ALT_FLAG };
  VP9_COMMON *cm = &cpi->common;
  MACROBLOCKD *xd = &x->e_mbd;
  union b_mode_info best_bmodes[16];
  MB_MODE_INFO best_mbmode;
  PARTITION_INFO best_partition;
  int_mv best_ref_mv, second_best_ref_mv;
  MB_PREDICTION_MODE this_mode;
  MB_PREDICTION_MODE best_mode = DC_PRED;
  MB_MODE_INFO * mbmi = &xd->mode_info_context->mbmi;
  int i, best_mode_index = 0;
  int mode8x8[4];
  unsigned char segment_id = mbmi->segment_id;

  int mode_index;
  int mdcounts[4];
  int rate, distortion;
  int rate2, distortion2;
  int64_t best_txfm_rd[NB_TXFM_MODES];
  int64_t best_txfm_diff[NB_TXFM_MODES];
  int64_t best_pred_diff[NB_PREDICTION_TYPES];
  int64_t best_pred_rd[NB_PREDICTION_TYPES];
  int64_t best_rd = INT64_MAX, best_intra_rd = INT64_MAX;
#if CONFIG_COMP_INTERINTRA_PRED
  int is_best_interintra = 0;
  int64_t best_intra16_rd = INT64_MAX;
  int best_intra16_mode = DC_PRED;
#if SEPARATE_INTERINTRA_UV
  int best_intra16_uv_mode = DC_PRED;
#endif
#endif
  int64_t best_overall_rd = INT64_MAX;
  INTERPOLATIONFILTERTYPE best_filter = SWITCHABLE;
  INTERPOLATIONFILTERTYPE tmp_best_filter = SWITCHABLE;
  int uv_intra_rate[2], uv_intra_distortion[2], uv_intra_rate_tokenonly[2];
  int uv_intra_skippable[2];
  MB_PREDICTION_MODE uv_intra_mode[2];
  int rate_y, UNINITIALIZED_IS_SAFE(rate_uv);
  int distortion_uv = INT_MAX;
  int64_t best_yrd = INT64_MAX;

  int_mv frame_mv[MB_MODE_COUNT][MAX_REF_FRAMES];
  int frame_mdcounts[4][4];
  YV12_BUFFER_CONFIG yv12_mb[4];

  unsigned int ref_costs[MAX_REF_FRAMES];
  int_mv seg_mvs[NB_PARTITIONINGS][16 /* n_blocks */][MAX_REF_FRAMES - 1];

  int intra_cost_penalty = 20 * vp9_dc_quant(cpi->common.base_qindex,
                                             cpi->common.y_dc_delta_q);
  int64_t mode_distortions[MB_MODE_COUNT] = {-1};
  int64_t frame_distortions[MAX_REF_FRAMES] = {-1};
  int ref_frame;

  struct scale_factors scale_factor[4];

  vpx_memset(mode8x8, 0, sizeof(mode8x8));
  vpx_memset(&frame_mv, 0, sizeof(frame_mv));
  vpx_memset(&best_mbmode, 0, sizeof(best_mbmode));
  vpx_memset(&best_bmodes, 0, sizeof(best_bmodes));
  vpx_memset(&x->mb_context[xd->sb_index][xd->mb_index], 0,
             sizeof(PICK_MODE_CONTEXT));

  x->mb_context[xd->sb_index][xd->mb_index].frames_with_high_error = 0;
  x->mb_context[xd->sb_index][xd->mb_index].modes_with_high_error = 0;

  for (i = 0; i < MAX_REF_FRAMES; i++)
    frame_mv[NEWMV][i].as_int = INVALID_MV;
  for (i = 0; i < NB_PREDICTION_TYPES; ++i)
    best_pred_rd[i] = INT64_MAX;
  for (i = 0; i < NB_TXFM_MODES; i++)
    best_txfm_rd[i] = INT64_MAX;

  for (i = 0; i < NB_PARTITIONINGS; i++) {
    int j, k;

    for (j = 0; j < 16; j++)
      for (k = 0; k < MAX_REF_FRAMES - 1; k++)
        seg_mvs[i][j][k].as_int = INVALID_MV;
  }

  if (cpi->ref_frame_flags & VP9_LAST_FLAG) {
    setup_buffer_inter(cpi, x, cpi->lst_fb_idx,
                       LAST_FRAME, BLOCK_16X16, mi_row, mi_col,
                       frame_mv[NEARESTMV], frame_mv[NEARMV],
                       frame_mdcounts, yv12_mb, scale_factor);
  }

  if (cpi->ref_frame_flags & VP9_GOLD_FLAG) {
    setup_buffer_inter(cpi, x, cpi->gld_fb_idx,
                       GOLDEN_FRAME, BLOCK_16X16, mi_row, mi_col,
                       frame_mv[NEARESTMV], frame_mv[NEARMV],
                       frame_mdcounts, yv12_mb, scale_factor);
  }

  if (cpi->ref_frame_flags & VP9_ALT_FLAG) {
    setup_buffer_inter(cpi, x, cpi->alt_fb_idx,
                       ALTREF_FRAME, BLOCK_16X16, mi_row, mi_col,
                       frame_mv[NEARESTMV], frame_mv[NEARMV],
                       frame_mdcounts, yv12_mb, scale_factor);
  }

  *returnintra = INT64_MAX;

  mbmi->ref_frame = INTRA_FRAME;

  /* Initialize zbin mode boost for uv costing */
  cpi->zbin_mode_boost = 0;
  vp9_update_zbin_extra(cpi, x);

  xd->mode_info_context->mbmi.mode = DC_PRED;

  for (i = 0; i <= TX_8X8; i++) {
    mbmi->txfm_size = i;
    rd_pick_intra_sbuv_mode(cpi, x, &uv_intra_rate[i],
                            &uv_intra_rate_tokenonly[i],
                            &uv_intra_distortion[i],
                            &uv_intra_skippable[i],
                            BLOCK_SIZE_MB16X16);
    uv_intra_mode[i] = mbmi->uv_mode;
  }

  // Get estimates of reference frame costs for each reference frame
  // that depend on the current prediction etc.
  estimate_ref_frame_costs(cpi, segment_id, ref_costs);

  for (mode_index = 0; mode_index < MAX_MODES; ++mode_index) {
    int64_t this_rd = INT64_MAX;
    int disable_skip = 0, skippable = 0;
    int other_cost = 0;
    int compmode_cost = 0;
#if CONFIG_COMP_INTERINTRA_PRED
    int compmode_interintra_cost = 0;
#endif
    int mode_excluded = 0;
    int64_t txfm_cache[NB_TXFM_MODES] = { 0 };
    YV12_BUFFER_CONFIG *scaled_ref_frame;

    // These variables hold are rolling total cost and distortion for this mode
    rate2 = 0;
    distortion2 = 0;
    rate_y = 0;
    rate_uv = 0;

    x->skip = 0;

    this_mode = vp9_mode_order[mode_index].mode;
    mbmi->mode = this_mode;
    mbmi->uv_mode = DC_PRED;
    mbmi->ref_frame = vp9_mode_order[mode_index].ref_frame;
    mbmi->second_ref_frame = vp9_mode_order[mode_index].second_ref_frame;

    mbmi->interp_filter = cm->mcomp_filter_type;

    set_scale_factors(xd, mbmi->ref_frame, mbmi->second_ref_frame,
                      scale_factor);

    vp9_setup_interp_filters(xd, mbmi->interp_filter, &cpi->common);

    // Test best rd so far against threshold for trying this mode.
    if (best_rd <= cpi->rd_threshes[mode_index])
      continue;

    // Ensure that the references used by this mode are available.
    if (mbmi->ref_frame &&
        !(cpi->ref_frame_flags & flag_list[mbmi->ref_frame]))
      continue;

    if (mbmi->second_ref_frame > 0 &&
        !(cpi->ref_frame_flags & flag_list[mbmi->second_ref_frame]))
      continue;

    // only scale on zeromv.
    if (mbmi->ref_frame > 0 &&
          (yv12_mb[mbmi->ref_frame].y_width != cm->mb_cols * 16 ||
           yv12_mb[mbmi->ref_frame].y_height != cm->mb_rows * 16) &&
        this_mode != ZEROMV)
      continue;

    if (mbmi->second_ref_frame > 0 &&
          (yv12_mb[mbmi->second_ref_frame].y_width != cm->mb_cols * 16 ||
           yv12_mb[mbmi->second_ref_frame].y_height != cm->mb_rows * 16) &&
        this_mode != ZEROMV)
      continue;

    // current coding mode under rate-distortion optimization test loop
#if CONFIG_COMP_INTERINTRA_PRED
    mbmi->interintra_mode = (MB_PREDICTION_MODE)(DC_PRED - 1);
    mbmi->interintra_uv_mode = (MB_PREDICTION_MODE)(DC_PRED - 1);
#endif

    // If the segment reference frame feature is enabled....
    // then do nothing if the current ref frame is not allowed..
    if (vp9_segfeature_active(xd, segment_id, SEG_LVL_REF_FRAME) &&
        !vp9_check_segref(xd, segment_id, mbmi->ref_frame)) {
      continue;
    // If the segment skip feature is enabled....
    // then do nothing if the current mode is not allowed..
    } else if (vp9_segfeature_active(xd, segment_id, SEG_LVL_SKIP) &&
               (this_mode != ZEROMV)) {
      continue;
    // Disable this drop out case if  the ref frame segment
    // level feature is enabled for this segment. This is to
    // prevent the possibility that the we end up unable to pick any mode.
    } else if (!vp9_segfeature_active(xd, segment_id, SEG_LVL_REF_FRAME)) {
      // Only consider ZEROMV/ALTREF_FRAME for alt ref frame overlay,
      // unless ARNR filtering is enabled in which case we want
      // an unfiltered alternative
      if (cpi->is_src_frame_alt_ref && (cpi->oxcf.arnr_max_frames == 0)) {
        if (this_mode != ZEROMV ||
            mbmi->ref_frame != ALTREF_FRAME) {
          continue;
        }
      }
    }

    /* everything but intra */
    scaled_ref_frame = NULL;
    if (mbmi->ref_frame) {
      int ref = mbmi->ref_frame;
      int fb;

      best_ref_mv = mbmi->ref_mvs[ref][0];
      vpx_memcpy(mdcounts, frame_mdcounts[ref], sizeof(mdcounts));

      if (mbmi->ref_frame == LAST_FRAME) {
        fb = cpi->lst_fb_idx;
      } else if (mbmi->ref_frame == GOLDEN_FRAME) {
        fb = cpi->gld_fb_idx;
      } else {
        fb = cpi->alt_fb_idx;
      }

      if (cpi->scaled_ref_idx[fb] != cm->ref_frame_map[fb])
        scaled_ref_frame = &cm->yv12_fb[cpi->scaled_ref_idx[fb]];
    }

    if (mbmi->second_ref_frame > 0) {
      int ref = mbmi->second_ref_frame;

      second_best_ref_mv = mbmi->ref_mvs[ref][0];
    }

    // TODO(jkoleszar) scaling/translation handled during creation of yv12_mb
    // currently.
    setup_pre_planes(xd, &yv12_mb[mbmi->ref_frame],
        mbmi->second_ref_frame > 0 ? &yv12_mb[mbmi->second_ref_frame] : NULL,
        0, 0, NULL, NULL);

    // Experimental code. Special case for gf and arf zeromv modes.
    // Increase zbin size to suppress noise
    if (cpi->zbin_mode_boost_enabled) {
      if (vp9_mode_order[mode_index].ref_frame == INTRA_FRAME)
        cpi->zbin_mode_boost = 0;
      else {
        if (vp9_mode_order[mode_index].mode == ZEROMV) {
          if (vp9_mode_order[mode_index].ref_frame != LAST_FRAME)
            cpi->zbin_mode_boost = GF_ZEROMV_ZBIN_BOOST;
          else
            cpi->zbin_mode_boost = LF_ZEROMV_ZBIN_BOOST;
        } else if (vp9_mode_order[mode_index].mode == SPLITMV)
          cpi->zbin_mode_boost = 0;
        else
          cpi->zbin_mode_boost = MV_ZBIN_BOOST;
      }

      vp9_update_zbin_extra(cpi, x);
    }

    // Intra
    if (!mbmi->ref_frame) {
      switch (this_mode) {
        default:
        case V_PRED:
        case H_PRED:
        case D45_PRED:
        case D135_PRED:
        case D117_PRED:
        case D153_PRED:
        case D27_PRED:
        case D63_PRED:
          rate2 += intra_cost_penalty;
        case DC_PRED:
        case TM_PRED:
          mbmi->ref_frame = INTRA_FRAME;
          // FIXME compound intra prediction
          vp9_build_intra_predictors_sby_s(&x->e_mbd, BLOCK_SIZE_MB16X16);
          // vp9_build_intra_predictors_mby(&x->e_mbd);
          super_block_yrd(cpi, x, &rate_y, &distortion, &skippable,
                          BLOCK_SIZE_MB16X16, txfm_cache);
          rate2 += rate_y;
          distortion2 += distortion;
          rate2 += x->mbmode_cost[xd->frame_type][mbmi->mode];

          rate2 += uv_intra_rate[mbmi->txfm_size != TX_4X4];
          rate_uv = uv_intra_rate_tokenonly[mbmi->txfm_size != TX_4X4];
          distortion2 += uv_intra_distortion[mbmi->txfm_size != TX_4X4];
          distortion_uv = uv_intra_distortion[mbmi->txfm_size != TX_4X4];
          skippable = skippable &&
                      uv_intra_skippable[mbmi->txfm_size != TX_4X4];
          break;
        case I4X4_PRED: {
          int64_t tmp_rd;

          // Note the rate value returned here includes the cost of coding
          // the I4X4_PRED mode : x->mbmode_cost[xd->frame_type][I4X4_PRED];
          mbmi->txfm_size = TX_4X4;
          tmp_rd = rd_pick_intra4x4mby_modes(cpi, x, &rate, &rate_y,
                                             &distortion, best_yrd);
          rate2 += rate;
          rate2 += intra_cost_penalty;
          distortion2 += distortion;

          if (tmp_rd < best_yrd) {
            rate2 += uv_intra_rate[TX_4X4];
            rate_uv = uv_intra_rate_tokenonly[TX_4X4];
            distortion2 += uv_intra_distortion[TX_4X4];
            distortion_uv = uv_intra_distortion[TX_4X4];
          } else {
            this_rd = INT64_MAX;
            disable_skip = 1;
          }
        }
        break;
        case I8X8_PRED: {
          int64_t tmp_rd;

          tmp_rd = rd_pick_intra8x8mby_modes_and_txsz(cpi, x, &rate, &rate_y,
                                                      &distortion, mode8x8,
                                                      best_yrd, txfm_cache);
          rate2 += rate;
          rate2 += intra_cost_penalty;
          distortion2 += distortion;

          /* TODO: uv rate maybe over-estimated here since there is UV intra
                   mode coded in I8X8_PRED prediction */
          if (tmp_rd < best_yrd) {
            rate2 += uv_intra_rate[TX_4X4];
            rate_uv = uv_intra_rate_tokenonly[TX_4X4];
            distortion2 += uv_intra_distortion[TX_4X4];
            distortion_uv = uv_intra_distortion[TX_4X4];
          } else {
            this_rd = INT64_MAX;
            disable_skip = 1;
          }
        }
        break;
      }
    }
    // Split MV. The code is very different from the other inter modes so
    // special case it.
    else if (this_mode == SPLITMV) {
      const int is_comp_pred = mbmi->second_ref_frame > 0;
      int64_t this_rd_thresh;
      int64_t tmp_rd, tmp_best_rd = INT64_MAX, tmp_best_rdu = INT64_MAX;
      int tmp_best_rate = INT_MAX, tmp_best_ratey = INT_MAX;
      int tmp_best_distortion = INT_MAX, tmp_best_skippable = 0;
      int switchable_filter_index;
      int_mv *second_ref = is_comp_pred ? &second_best_ref_mv : NULL;
      union b_mode_info tmp_best_bmodes[16];
      MB_MODE_INFO tmp_best_mbmode;
      PARTITION_INFO tmp_best_partition;
      int pred_exists = 0;

      this_rd_thresh =
          (mbmi->ref_frame == LAST_FRAME) ?
          cpi->rd_threshes[THR_NEWMV] : cpi->rd_threshes[THR_NEWA];
      this_rd_thresh =
          (mbmi->ref_frame == GOLDEN_FRAME) ?
          cpi->rd_threshes[THR_NEWG] : this_rd_thresh;
      xd->mode_info_context->mbmi.txfm_size = TX_4X4;

      for (switchable_filter_index = 0;
           switchable_filter_index < VP9_SWITCHABLE_FILTERS;
           ++switchable_filter_index) {
        int newbest;
        mbmi->interp_filter =
            vp9_switchable_interp[switchable_filter_index];
        vp9_setup_interp_filters(xd, mbmi->interp_filter, &cpi->common);

        tmp_rd = rd_pick_best_mbsegmentation(cpi, x, &best_ref_mv,
                                             second_ref, best_yrd, mdcounts,
                                             &rate, &rate_y, &distortion,
                                             &skippable,
                                             (int)this_rd_thresh, seg_mvs,
                                             txfm_cache);
        if (cpi->common.mcomp_filter_type == SWITCHABLE) {
          int rs = SWITCHABLE_INTERP_RATE_FACTOR * x->switchable_interp_costs
                   [vp9_get_pred_context(&cpi->common, xd,
                                         PRED_SWITCHABLE_INTERP)]
                   [vp9_switchable_interp_map[mbmi->interp_filter]];
          tmp_rd += RDCOST(x->rdmult, x->rddiv, rs, 0);
        }
        newbest = (tmp_rd < tmp_best_rd);
        if (newbest) {
          tmp_best_filter = mbmi->interp_filter;
          tmp_best_rd = tmp_rd;
        }
        if ((newbest && cm->mcomp_filter_type == SWITCHABLE) ||
            (mbmi->interp_filter == cm->mcomp_filter_type &&
             cm->mcomp_filter_type != SWITCHABLE)) {
          tmp_best_rdu = tmp_rd;
          tmp_best_rate = rate;
          tmp_best_ratey = rate_y;
          tmp_best_distortion = distortion;
          tmp_best_skippable = skippable;
          vpx_memcpy(&tmp_best_mbmode, mbmi, sizeof(MB_MODE_INFO));
          vpx_memcpy(&tmp_best_partition, x->partition_info,
                     sizeof(PARTITION_INFO));
          for (i = 0; i < 16; i++) {
            tmp_best_bmodes[i] = xd->mode_info_context->bmi[i];
          }
          pred_exists = 1;
        }
      }  // switchable_filter_index loop

      mbmi->interp_filter = (cm->mcomp_filter_type == SWITCHABLE ?
                             tmp_best_filter : cm->mcomp_filter_type);
      vp9_setup_interp_filters(xd, mbmi->interp_filter, &cpi->common);
      if (!pred_exists) {
        // Handles the special case when a filter that is not in the
        // switchable list (bilinear, 6-tap) is indicated at the frame level
        tmp_rd = rd_pick_best_mbsegmentation(cpi, x, &best_ref_mv,
                                             second_ref, best_yrd, mdcounts,
                                             &rate, &rate_y, &distortion,
                                             &skippable,
                                             (int)this_rd_thresh, seg_mvs,
                                             txfm_cache);
      } else {
        if (cpi->common.mcomp_filter_type == SWITCHABLE) {
          int rs = SWITCHABLE_INTERP_RATE_FACTOR * x->switchable_interp_costs
                   [vp9_get_pred_context(&cpi->common, xd,
                                         PRED_SWITCHABLE_INTERP)]
                   [vp9_switchable_interp_map[mbmi->interp_filter]];
          tmp_best_rdu -= RDCOST(x->rdmult, x->rddiv, rs, 0);
        }
        tmp_rd = tmp_best_rdu;
        rate = tmp_best_rate;
        rate_y = tmp_best_ratey;
        distortion = tmp_best_distortion;
        skippable = tmp_best_skippable;
        vpx_memcpy(mbmi, &tmp_best_mbmode, sizeof(MB_MODE_INFO));
        vpx_memcpy(x->partition_info, &tmp_best_partition,
                   sizeof(PARTITION_INFO));
        for (i = 0; i < 16; i++) {
          xd->mode_info_context->bmi[i] = tmp_best_bmodes[i];
        }
      }

      rate2 += rate;
      distortion2 += distortion;

      if (cpi->common.mcomp_filter_type == SWITCHABLE)
        rate2 += SWITCHABLE_INTERP_RATE_FACTOR * x->switchable_interp_costs
            [vp9_get_pred_context(&cpi->common, xd, PRED_SWITCHABLE_INTERP)]
            [vp9_switchable_interp_map[mbmi->interp_filter]];

      // If even the 'Y' rd value of split is higher than best so far
      // then dont bother looking at UV
      if (tmp_rd < best_yrd) {
        int uv_skippable;

        vp9_build_inter_predictors_sbuv(&x->e_mbd, mi_row, mi_col,
                                        BLOCK_SIZE_MB16X16);

        vp9_subtract_sbuv(x, BLOCK_SIZE_MB16X16);

        super_block_uvrd_for_txfm(cm, x, &rate_uv, &distortion_uv,
                                  &uv_skippable, BLOCK_SIZE_MB16X16, TX_4X4);
        rate2 += rate_uv;
        distortion2 += distortion_uv;
        skippable = skippable && uv_skippable;
      } else {
        this_rd = INT64_MAX;
        disable_skip = 1;
      }

      if (!mode_excluded) {
        if (is_comp_pred)
          mode_excluded = cpi->common.comp_pred_mode == SINGLE_PREDICTION_ONLY;
        else
          mode_excluded = cpi->common.comp_pred_mode == COMP_PREDICTION_ONLY;
      }

      compmode_cost =
        vp9_cost_bit(vp9_get_pred_prob(cm, xd, PRED_COMP), is_comp_pred);
      mbmi->mode = this_mode;
    }
    else {
#if CONFIG_COMP_INTERINTRA_PRED
      if (mbmi->second_ref_frame == INTRA_FRAME) {
        if (best_intra16_mode == DC_PRED - 1) continue;
        mbmi->interintra_mode = best_intra16_mode;
#if SEPARATE_INTERINTRA_UV
        mbmi->interintra_uv_mode = best_intra16_uv_mode;
#else
        mbmi->interintra_uv_mode = best_intra16_mode;
#endif
      }
#endif
      this_rd = handle_inter_mode(cpi, x, BLOCK_SIZE_MB16X16,
                                  mdcounts, txfm_cache,
                                  &rate2, &distortion2, &skippable,
                                  &compmode_cost,
#if CONFIG_COMP_INTERINTRA_PRED
                                  &compmode_interintra_cost,
#endif
                                  &rate_y, &distortion,
                                  &rate_uv, &distortion_uv,
                                  &mode_excluded, &disable_skip,
                                  mode_index, &tmp_best_filter, frame_mv,
                                  scaled_ref_frame, mi_row, mi_col);
      if (this_rd == INT64_MAX)
        continue;
    }

#if CONFIG_COMP_INTERINTRA_PRED
    if (cpi->common.use_interintra)
      rate2 += compmode_interintra_cost;
#endif

    if (cpi->common.comp_pred_mode == HYBRID_PREDICTION)
      rate2 += compmode_cost;

    // Estimate the reference frame signaling cost and add it
    // to the rolling cost variable.
    rate2 += ref_costs[mbmi->ref_frame];

    if (!disable_skip) {
      // Test for the condition where skip block will be activated
      // because there are no non zero coefficients and make any
      // necessary adjustment for rate. Ignore if skip is coded at
      // segment level as the cost wont have been added in.
      int mb_skip_allowed;

      // Is Mb level skip allowed (i.e. not coded at segment level).
      mb_skip_allowed = !vp9_segfeature_active(xd, segment_id, SEG_LVL_SKIP);

      if (skippable) {
        mbmi->mb_skip_coeff = 1;

        // Back out the coefficient coding costs
        rate2 -= (rate_y + rate_uv);
        // for best_yrd calculation
        rate_uv = 0;

        if (mb_skip_allowed) {
          int prob_skip_cost;

          // Cost the skip mb case
          vp9_prob skip_prob =
            vp9_get_pred_prob(cm, &x->e_mbd, PRED_MBSKIP);

          if (skip_prob) {
            prob_skip_cost = vp9_cost_bit(skip_prob, 1);
            rate2 += prob_skip_cost;
            other_cost += prob_skip_cost;
          }
        }
      } else {
        // Add in the cost of the no skip flag.
        mbmi->mb_skip_coeff = 0;
        if (mb_skip_allowed) {
          int prob_skip_cost = vp9_cost_bit(
                 vp9_get_pred_prob(cm, &x->e_mbd, PRED_MBSKIP), 0);
          rate2 += prob_skip_cost;
          other_cost += prob_skip_cost;
        }
      }

      // Calculate the final RD estimate for this mode.
      this_rd = RDCOST(x->rdmult, x->rddiv, rate2, distortion2);
    }

    // Keep record of best intra distortion
    if ((mbmi->ref_frame == INTRA_FRAME) &&
        (this_rd < best_intra_rd)) {
      best_intra_rd = this_rd;
      *returnintra = distortion2;
    }
#if CONFIG_COMP_INTERINTRA_PRED
    if ((mbmi->ref_frame == INTRA_FRAME) &&
        (this_mode <= TM_PRED) &&
        (this_rd < best_intra16_rd)) {
      best_intra16_rd = this_rd;
      best_intra16_mode = this_mode;
#if SEPARATE_INTERINTRA_UV
      best_intra16_uv_mode = uv_intra_mode[mbmi->txfm_size != TX_4X4];
#endif
    }
#endif

    if (!disable_skip && mbmi->ref_frame == INTRA_FRAME)
      for (i = 0; i < NB_PREDICTION_TYPES; ++i)
        best_pred_rd[i] = MIN(best_pred_rd[i], this_rd);

    if (this_rd < best_overall_rd) {
      best_overall_rd = this_rd;
      best_filter = tmp_best_filter;
      best_mode = this_mode;
#if CONFIG_COMP_INTERINTRA_PRED
      is_best_interintra = (mbmi->second_ref_frame == INTRA_FRAME);
#endif
    }

    // Store the respective mode distortions for later use.
    // Store the respective mode distortions for later use.
    if (mode_distortions[this_mode] == -1
        || distortion2 < mode_distortions[this_mode]) {
      mode_distortions[this_mode] = distortion2;
    }
    if (frame_distortions[mbmi->ref_frame] == -1 ||
        distortion2 < frame_distortions[mbmi->ref_frame]) {
       frame_distortions[mbmi->ref_frame] = distortion2;
    }

    // Did this mode help.. i.e. is it the new best mode
    if (this_rd < best_rd || x->skip) {
      if (!mode_excluded) {
        /*
        if (mbmi->second_ref_frame == INTRA_FRAME) {
          printf("rd %d best %d bestintra16 %d\n", this_rd, best_rd, best_intra16_rd);
        }
        */
        // Note index of best mode so far
        best_mode_index = mode_index;

        if (this_mode <= I4X4_PRED) {
          if (mbmi->txfm_size != TX_4X4
              && this_mode != I4X4_PRED
              && this_mode != I8X8_PRED)
            mbmi->uv_mode = uv_intra_mode[TX_8X8];
          else
            mbmi->uv_mode = uv_intra_mode[TX_4X4];
          /* required for left and above block mv */
          mbmi->mv[0].as_int = 0;
        }

        other_cost += ref_costs[mbmi->ref_frame];

        /* Calculate the final y RD estimate for this mode */
        best_yrd = RDCOST(x->rdmult, x->rddiv, (rate2 - rate_uv - other_cost),
                          (distortion2 - distortion_uv));

        *returnrate = rate2;
        *returndistortion = distortion2;
        best_rd = this_rd;
        vpx_memcpy(&best_mbmode, mbmi, sizeof(MB_MODE_INFO));
        vpx_memcpy(&best_partition, x->partition_info, sizeof(PARTITION_INFO));

        if ((this_mode == I4X4_PRED)
            || (this_mode == I8X8_PRED)
            || (this_mode == SPLITMV))
          for (i = 0; i < 16; i++) {
            best_bmodes[i] = xd->mode_info_context->bmi[i];
          }
      }

      // Testing this mode gave rise to an improvement in best error score.
      // Lower threshold a bit for next time
      cpi->rd_thresh_mult[mode_index] =
          (cpi->rd_thresh_mult[mode_index] >= (MIN_THRESHMULT + 2)) ?
          cpi->rd_thresh_mult[mode_index] - 2 : MIN_THRESHMULT;
      cpi->rd_threshes[mode_index] =
          (cpi->rd_baseline_thresh[mode_index] >> 7) *
          cpi->rd_thresh_mult[mode_index];
    } else {
      // If the mode did not help improve the best error case then raise the
      // threshold for testing that mode next time around.
      cpi->rd_thresh_mult[mode_index] += 4;

      if (cpi->rd_thresh_mult[mode_index] > MAX_THRESHMULT)
        cpi->rd_thresh_mult[mode_index] = MAX_THRESHMULT;

      cpi->rd_threshes[mode_index] = (cpi->rd_baseline_thresh[mode_index] >> 7)
          * cpi->rd_thresh_mult[mode_index];
    }

    /* keep record of best compound/single-only prediction */
    if (!disable_skip && mbmi->ref_frame != INTRA_FRAME) {
      int64_t single_rd, hybrid_rd;
      int single_rate, hybrid_rate;

      if (cpi->common.comp_pred_mode == HYBRID_PREDICTION) {
        single_rate = rate2 - compmode_cost;
        hybrid_rate = rate2;
      } else {
        single_rate = rate2;
        hybrid_rate = rate2 + compmode_cost;
      }

      single_rd = RDCOST(x->rdmult, x->rddiv, single_rate, distortion2);
      hybrid_rd = RDCOST(x->rdmult, x->rddiv, hybrid_rate, distortion2);

      if (mbmi->second_ref_frame <= INTRA_FRAME &&
          single_rd < best_pred_rd[SINGLE_PREDICTION_ONLY]) {
        best_pred_rd[SINGLE_PREDICTION_ONLY] = single_rd;
      } else if (mbmi->second_ref_frame > INTRA_FRAME &&
                 single_rd < best_pred_rd[COMP_PREDICTION_ONLY]) {
        best_pred_rd[COMP_PREDICTION_ONLY] = single_rd;
      }
      if (hybrid_rd < best_pred_rd[HYBRID_PREDICTION])
        best_pred_rd[HYBRID_PREDICTION] = hybrid_rd;
    }

    /* keep record of best txfm size */
    if (!mode_excluded && this_rd != INT64_MAX) {
      for (i = 0; i < NB_TXFM_MODES; i++) {
        int64_t adj_rd;
        if (this_mode != I4X4_PRED) {
          const int64_t txfm_mode_diff =
              txfm_cache[i] - txfm_cache[cm->txfm_mode];
          adj_rd = this_rd + txfm_mode_diff;
        } else {
          adj_rd = this_rd;
        }
        if (adj_rd < best_txfm_rd[i])
          best_txfm_rd[i] = adj_rd;
      }
    }

    if (x->skip && !mode_excluded)
      break;
  }

  assert((cm->mcomp_filter_type == SWITCHABLE) ||
         (cm->mcomp_filter_type == best_mbmode.interp_filter) ||
         (best_mbmode.mode <= I4X4_PRED));

#if CONFIG_COMP_INTERINTRA_PRED
  ++cpi->interintra_select_count[is_best_interintra];
#endif

  // Accumulate filter usage stats
  // TODO(agrange): Use RD criteria to select interpolation filter mode.
  if (is_inter_mode(best_mode))
    ++cpi->best_switchable_interp_count[vp9_switchable_interp_map[best_filter]];

  // Reduce the activation RD thresholds for the best choice mode
  if ((cpi->rd_baseline_thresh[best_mode_index] > 0) &&
      (cpi->rd_baseline_thresh[best_mode_index] < (INT_MAX >> 2))) {
    int best_adjustment = (cpi->rd_thresh_mult[best_mode_index] >> 2);

    cpi->rd_thresh_mult[best_mode_index] =
        (cpi->rd_thresh_mult[best_mode_index] >=
         (MIN_THRESHMULT + best_adjustment)) ?
        cpi->rd_thresh_mult[best_mode_index] - best_adjustment : MIN_THRESHMULT;
    cpi->rd_threshes[best_mode_index] =
        (cpi->rd_baseline_thresh[best_mode_index] >> 7) *
        cpi->rd_thresh_mult[best_mode_index];
  }

  // This code forces Altref,0,0 and skip for the frame that overlays a
  // an alrtef unless Altref is filtered. However, this is unsafe if
  // segment level coding of ref frame is enabled for this
  // segment.
  if (!vp9_segfeature_active(xd, segment_id, SEG_LVL_REF_FRAME) &&
      cpi->is_src_frame_alt_ref &&
      (cpi->oxcf.arnr_max_frames == 0) &&
      (best_mbmode.mode != ZEROMV || best_mbmode.ref_frame != ALTREF_FRAME)) {
    mbmi->mode = ZEROMV;
    if (cm->txfm_mode <= ALLOW_8X8)
      mbmi->txfm_size = cm->txfm_mode;
    else
      mbmi->txfm_size = TX_16X16;
    mbmi->ref_frame = ALTREF_FRAME;
    mbmi->mv[0].as_int = 0;
    mbmi->uv_mode = DC_PRED;
    mbmi->mb_skip_coeff = 1;
    mbmi->partitioning = 0;
    set_scale_factors(xd, mbmi->ref_frame, mbmi->second_ref_frame,
                      scale_factor);

    vpx_memset(best_pred_diff, 0, sizeof(best_pred_diff));
    vpx_memset(best_txfm_diff, 0, sizeof(best_txfm_diff));
    goto end;
  }

  // macroblock modes
  vpx_memcpy(mbmi, &best_mbmode, sizeof(MB_MODE_INFO));
  if (best_mbmode.mode == I4X4_PRED) {
    for (i = 0; i < 16; i++) {
      xd->mode_info_context->bmi[i].as_mode = best_bmodes[i].as_mode;
    }
  }

  if (best_mbmode.mode == I8X8_PRED)
    set_i8x8_block_modes(x, mode8x8);

  if (best_mbmode.mode == SPLITMV) {
    for (i = 0; i < 16; i++)
      xd->mode_info_context->bmi[i].as_mv[0].as_int =
          best_bmodes[i].as_mv[0].as_int;
    if (mbmi->second_ref_frame > 0)
      for (i = 0; i < 16; i++)
        xd->mode_info_context->bmi[i].as_mv[1].as_int =
            best_bmodes[i].as_mv[1].as_int;

    vpx_memcpy(x->partition_info, &best_partition, sizeof(PARTITION_INFO));

    mbmi->mv[0].as_int = x->partition_info->bmi[15].mv.as_int;
    mbmi->mv[1].as_int = x->partition_info->bmi[15].second_mv.as_int;
  }

  for (i = 0; i < NB_PREDICTION_TYPES; ++i) {
    if (best_pred_rd[i] == INT64_MAX)
      best_pred_diff[i] = INT_MIN;
    else
      best_pred_diff[i] = best_rd - best_pred_rd[i];
  }

  if (!x->skip) {
    for (i = 0; i < NB_TXFM_MODES; i++) {
      if (best_txfm_rd[i] == INT64_MAX)
        best_txfm_diff[i] = 0;
      else
        best_txfm_diff[i] = best_rd - best_txfm_rd[i];
    }
  } else {
    vpx_memset(best_txfm_diff, 0, sizeof(best_txfm_diff));
  }

end:

  // Flag all modes that have a distortion thats > 2x the best we found at
  // this level.
  for (mode_index = 0; mode_index < MB_MODE_COUNT; ++mode_index) {
    if (mode_index == NEARESTMV || mode_index == NEARMV || mode_index == NEWMV
        || mode_index == SPLITMV)
      continue;

    if (mode_distortions[mode_index] > 2 * *returndistortion) {
      x->mb_context[xd->sb_index][xd->mb_index].modes_with_high_error |= (1
          << mode_index);
    }
  }

  // Flag all ref frames that have a distortion thats > 2x the best we found at
  // this level.
  for (ref_frame = INTRA_FRAME; ref_frame <= ALTREF_FRAME; ref_frame++) {
    if (frame_distortions[ref_frame] > 2 * *returndistortion) {
      x->mb_context[xd->sb_index][xd->mb_index].frames_with_high_error |= (1
          << ref_frame);
    }
  }

  set_scale_factors(xd, mbmi->ref_frame, mbmi->second_ref_frame,
                    scale_factor);
  store_coding_context(x, &x->mb_context[xd->sb_index][xd->mb_index],
                       best_mode_index, &best_partition,
                       &mbmi->ref_mvs[mbmi->ref_frame][0],
                       &mbmi->ref_mvs[mbmi->second_ref_frame < 0 ? 0 :
                                      mbmi->second_ref_frame][0],
                       best_pred_diff, best_txfm_diff);
}
#endif  // !CONFIG_SB8X8

void vp9_rd_pick_intra_mode_sb(VP9_COMP *cpi, MACROBLOCK *x,
                               int *returnrate, int *returndist,
                               BLOCK_SIZE_TYPE bsize,
                               PICK_MODE_CONTEXT *ctx) {
  VP9_COMMON *cm = &cpi->common;
  MACROBLOCKD *xd = &x->e_mbd;
  int rate_y = 0, rate_uv;
  int rate_y_tokenonly = 0, rate_uv_tokenonly;
  int dist_y = 0, dist_uv;
  int y_skip = 0, uv_skip;
  int64_t txfm_cache[NB_TXFM_MODES], err;
#if CONFIG_SB8X8
  MB_PREDICTION_MODE mode;
  TX_SIZE txfm_size;
  int rate4x4_y, rate4x4_y_tokenonly, dist4x4_y;
  int64_t err4x4 = INT64_MAX;
#endif
  int i;

  ctx->skip = 0;
  xd->mode_info_context->mbmi.mode = DC_PRED;
  err = rd_pick_intra_sby_mode(cpi, x, &rate_y, &rate_y_tokenonly,
                               &dist_y, &y_skip, bsize, txfm_cache);
#if CONFIG_SB8X8
  mode = xd->mode_info_context->mbmi.mode;
  txfm_size = xd->mode_info_context->mbmi.txfm_size;
#endif
  rd_pick_intra_sbuv_mode(cpi, x, &rate_uv, &rate_uv_tokenonly,
                          &dist_uv, &uv_skip, bsize);
#if CONFIG_SB8X8
  if (bsize == BLOCK_SIZE_SB8X8)
    err4x4 = rd_pick_intra4x4mby_modes(cpi, x, &rate4x4_y,
                                       &rate4x4_y_tokenonly,
                                       &dist4x4_y, err);
#endif

  if (y_skip && uv_skip) {
    *returnrate = rate_y + rate_uv - rate_y_tokenonly - rate_uv_tokenonly +
                  vp9_cost_bit(vp9_get_pred_prob(cm, xd, PRED_MBSKIP), 1);
    *returndist = dist_y + (dist_uv >> 2);
    memset(ctx->txfm_rd_diff, 0,
           sizeof(x->sb32_context[xd->sb_index].txfm_rd_diff));
#if CONFIG_SB8X8
    xd->mode_info_context->mbmi.mode = mode;
    xd->mode_info_context->mbmi.txfm_size = txfm_size;
  } else if (bsize == BLOCK_SIZE_SB8X8 && err4x4 < err) {
    *returnrate = rate4x4_y + rate_uv +
        vp9_cost_bit(vp9_get_pred_prob(cm, xd, PRED_MBSKIP), 0);
    *returndist = dist4x4_y + (dist_uv >> 2);
    for (i = 0; i < NB_TXFM_MODES; i++) {
      ctx->txfm_rd_diff[i] = MIN(err4x4, err - txfm_cache[i]);
    }
    xd->mode_info_context->mbmi.txfm_size = TX_4X4;
#endif
  } else {
    *returnrate = rate_y + rate_uv +
        vp9_cost_bit(vp9_get_pred_prob(cm, xd, PRED_MBSKIP), 0);
    *returndist = dist_y + (dist_uv >> 2);
    for (i = 0; i < NB_TXFM_MODES; i++) {
#if CONFIG_SB8X8
      ctx->txfm_rd_diff[i] = MIN(err4x4, err - txfm_cache[i]);
#else
      ctx->txfm_rd_diff[i] = err - txfm_cache[i];
#endif
    }
#if CONFIG_SB8X8
    xd->mode_info_context->mbmi.txfm_size = txfm_size;
    xd->mode_info_context->mbmi.mode = mode;
#endif
  }

  vpx_memcpy(&ctx->mic, xd->mode_info_context, sizeof(MODE_INFO));
}

#if !CONFIG_SB8X8
void vp9_rd_pick_intra_mode(VP9_COMP *cpi, MACROBLOCK *x,
                            int *returnrate, int *returndist) {
  VP9_COMMON *cm = &cpi->common;
  MACROBLOCKD *xd = &x->e_mbd;
  MB_MODE_INFO * mbmi = &x->e_mbd.mode_info_context->mbmi;
  int64_t error4x4, error16x16;
  int rate4x4, rate16x16 = 0, rateuv[2];
  int dist4x4 = 0, dist16x16 = 0, distuv[2];
  int rate;
  int rate4x4_tokenonly = 0;
  int rate16x16_tokenonly = 0;
  int rateuv_tokenonly[2];
  int64_t error8x8;
  int rate8x8_tokenonly=0;
  int rate8x8, dist8x8;
  int mode16x16;
  int mode8x8[4];
  int dist;
  int modeuv[2], uv_intra_skippable[2];
  int y_intra16x16_skippable = 0;
  int64_t txfm_cache[2][NB_TXFM_MODES];
  TX_SIZE txfm_size_16x16, txfm_size_8x8;
  int i;

  x->mb_context[xd->sb_index][xd->mb_index].skip = 0;
  mbmi->ref_frame = INTRA_FRAME;
  mbmi->mode = DC_PRED;
  for (i = 0; i <= TX_8X8; i++) {
    mbmi->txfm_size = i;
    rd_pick_intra_sbuv_mode(cpi, x, &rateuv[i], &rateuv_tokenonly[i],
                            &distuv[i], &uv_intra_skippable[i],
                            BLOCK_SIZE_MB16X16);
    modeuv[i] = mbmi->uv_mode;
  }

  // current macroblock under rate-distortion optimization test loop
  error16x16 = rd_pick_intra_sby_mode(cpi, x, &rate16x16,
                                      &rate16x16_tokenonly, &dist16x16,
                                      &y_intra16x16_skippable,
                                      BLOCK_SIZE_MB16X16, txfm_cache[1]);
  mode16x16 = mbmi->mode;
  txfm_size_16x16 = mbmi->txfm_size;
  if (y_intra16x16_skippable &&
      ((cm->txfm_mode == ONLY_4X4 && uv_intra_skippable[TX_4X4]) ||
       (cm->txfm_mode != ONLY_4X4 && uv_intra_skippable[TX_8X8]))) {
    error16x16 -= RDCOST(x->rdmult, x->rddiv, rate16x16_tokenonly, 0);
    rate16x16 -= rate16x16_tokenonly;
  }
  for (i = 0; i < NB_TXFM_MODES; i++) {
    txfm_cache[0][i] = error16x16 - txfm_cache[1][cm->txfm_mode] +
                       txfm_cache[1][i];
  }

  error8x8 = rd_pick_intra8x8mby_modes_and_txsz(cpi, x, &rate8x8,
                                                &rate8x8_tokenonly,
                                                &dist8x8, mode8x8,
                                                error16x16, txfm_cache[1]);
  txfm_size_8x8 = mbmi->txfm_size;
  for (i = 0; i < NB_TXFM_MODES; i++) {
    int64_t tmp_rd = error8x8 - txfm_cache[1][cm->txfm_mode] + txfm_cache[1][i];
    if (tmp_rd < txfm_cache[0][i])
      txfm_cache[0][i] = tmp_rd;
  }

  mbmi->txfm_size = TX_4X4;
  error4x4 = rd_pick_intra4x4mby_modes(cpi, x,
                                       &rate4x4, &rate4x4_tokenonly,
                                       &dist4x4, error16x16);
  for (i = 0; i < NB_TXFM_MODES; i++) {
    if (error4x4 < txfm_cache[0][i])
      txfm_cache[0][i] = error4x4;
  }

  mbmi->mb_skip_coeff = 0;
  if (y_intra16x16_skippable &&
      ((cm->txfm_mode == ONLY_4X4 && uv_intra_skippable[TX_4X4]) ||
       (cm->txfm_mode != ONLY_4X4 && uv_intra_skippable[TX_8X8]))) {
    mbmi->mb_skip_coeff = 1;
    mbmi->mode = mode16x16;
    mbmi->uv_mode = modeuv[cm->txfm_mode != ONLY_4X4];
    rate = rate16x16 + vp9_cost_bit(vp9_get_pred_prob(cm, xd, PRED_MBSKIP), 1);
    dist = dist16x16;
    rate += rateuv[cm->txfm_mode != ONLY_4X4] -
            rateuv_tokenonly[cm->txfm_mode != ONLY_4X4];
    dist += (distuv[cm->txfm_mode != ONLY_4X4] >> 2);
    mbmi->txfm_size = txfm_size_16x16;
  } else if (error8x8 > error16x16) {
    if (error4x4 < error16x16) {
      rate = rateuv[TX_4X4] + rate4x4;
      mbmi->mode = I4X4_PRED;
      mbmi->txfm_size = TX_4X4;
      dist = dist4x4 + (distuv[TX_4X4] >> 2);
      mbmi->uv_mode = modeuv[TX_4X4];
    } else {
      mbmi->txfm_size = txfm_size_16x16;
      mbmi->mode = mode16x16;
      rate = rate16x16 + rateuv[mbmi->txfm_size != TX_4X4];
      dist = dist16x16 + (distuv[mbmi->txfm_size != TX_4X4] >> 2);
      mbmi->uv_mode = modeuv[mbmi->txfm_size != TX_4X4];
    }
    rate += vp9_cost_bit(vp9_get_pred_prob(cm, xd, PRED_MBSKIP), 0);
  } else {
    if (error4x4 < error8x8) {
      rate = rateuv[TX_4X4] + rate4x4;
      mbmi->mode = I4X4_PRED;
      mbmi->txfm_size = TX_4X4;
      dist = dist4x4 + (distuv[TX_4X4] >> 2);
      mbmi->uv_mode = modeuv[TX_4X4];
    } else {
      mbmi->mode = I8X8_PRED;
      mbmi->txfm_size = txfm_size_8x8;
      set_i8x8_block_modes(x, mode8x8);
      rate = rate8x8 + rateuv[TX_4X4];
      dist = dist8x8 + (distuv[TX_4X4] >> 2);
    }
    rate += vp9_cost_bit(vp9_get_pred_prob(cm, xd, PRED_MBSKIP), 0);
  }

  for (i = 0; i < NB_TXFM_MODES; i++) {
    x->mb_context[xd->sb_index][xd->mb_index].txfm_rd_diff[i] =
        txfm_cache[0][cm->txfm_mode] - txfm_cache[0][i];
  }

  *returnrate = rate;
  *returndist = dist;
}
#endif

int64_t vp9_rd_pick_inter_mode_sb(VP9_COMP *cpi, MACROBLOCK *x,
                                  int mi_row, int mi_col,
                                  int *returnrate,
                                  int *returndistortion,
                                  BLOCK_SIZE_TYPE bsize,
                                  PICK_MODE_CONTEXT *ctx) {
  const enum BlockSize block_size = y_bsizet_to_block_size(bsize);
  VP9_COMMON *cm = &cpi->common;
  MACROBLOCKD *xd = &x->e_mbd;
  MB_MODE_INFO *mbmi = &xd->mode_info_context->mbmi;
  MB_PREDICTION_MODE this_mode;
  MB_PREDICTION_MODE best_mode = DC_PRED;
  MV_REFERENCE_FRAME ref_frame, second_ref;
  unsigned char segment_id = xd->mode_info_context->mbmi.segment_id;
  int comp_pred, i;
  int_mv frame_mv[MB_MODE_COUNT][MAX_REF_FRAMES];
  int frame_mdcounts[4][4];
  YV12_BUFFER_CONFIG yv12_mb[4];
  static const int flag_list[4] = { 0, VP9_LAST_FLAG, VP9_GOLD_FLAG,
                                    VP9_ALT_FLAG };
  int idx_list[4] = {0,
                     cpi->lst_fb_idx,
                     cpi->gld_fb_idx,
                     cpi->alt_fb_idx};
  int mdcounts[4];
  int64_t best_rd = INT64_MAX;
  int64_t best_txfm_rd[NB_TXFM_MODES];
  int64_t best_txfm_diff[NB_TXFM_MODES];
  int64_t best_pred_diff[NB_PREDICTION_TYPES];
  int64_t best_pred_rd[NB_PREDICTION_TYPES];
  MB_MODE_INFO best_mbmode;
  int j;
  int mode_index, best_mode_index = 0;
  unsigned int ref_costs[MAX_REF_FRAMES];
#if CONFIG_COMP_INTERINTRA_PRED
  int is_best_interintra = 0;
  int64_t best_intra16_rd = INT64_MAX;
  int best_intra16_mode = DC_PRED;
#if SEPARATE_INTERINTRA_UV
  int best_intra16_uv_mode = DC_PRED;
#endif
#endif
  int64_t best_overall_rd = INT64_MAX;
  INTERPOLATIONFILTERTYPE best_filter = SWITCHABLE;
  INTERPOLATIONFILTERTYPE tmp_best_filter = SWITCHABLE;
  int rate_uv_intra[TX_SIZE_MAX_SB], rate_uv_tokenonly[TX_SIZE_MAX_SB];
  int dist_uv[TX_SIZE_MAX_SB], skip_uv[TX_SIZE_MAX_SB];
  MB_PREDICTION_MODE mode_uv[TX_SIZE_MAX_SB];
  struct scale_factors scale_factor[4];
  unsigned int ref_frame_mask = 0;
  unsigned int mode_mask = 0;
  int64_t mode_distortions[MB_MODE_COUNT] = {-1};
  int64_t frame_distortions[MAX_REF_FRAMES] = {-1};
  int intra_cost_penalty = 20 * vp9_dc_quant(cpi->common.base_qindex,
                                             cpi->common.y_dc_delta_q);
#if CONFIG_SB8X8
  int_mv seg_mvs[4][MAX_REF_FRAMES - 1];
  union b_mode_info best_bmodes[4];
  PARTITION_INFO best_partition;
#endif

#if CONFIG_SB8X8
  for (i = 0; i < 4; i++) {
    int j;

    for (j = 0; j < MAX_REF_FRAMES - 1; j++)
      seg_mvs[i][j].as_int = INVALID_MV;
  }
#endif
  // Everywhere the flag is set the error is much higher than its neighbors.
  ctx->frames_with_high_error = 0;
  ctx->modes_with_high_error = 0;

  xd->mode_info_context->mbmi.segment_id = segment_id;
  estimate_ref_frame_costs(cpi, segment_id, ref_costs);
  vpx_memset(&best_mbmode, 0, sizeof(best_mbmode));

  for (i = 0; i < NB_PREDICTION_TYPES; ++i)
    best_pred_rd[i] = INT64_MAX;
  for (i = 0; i < NB_TXFM_MODES; i++)
    best_txfm_rd[i] = INT64_MAX;

  // Create a mask set to 1 for each frame used by a smaller resolution.
  if (cpi->Speed > 0) {
    switch (block_size) {
      case BLOCK_64X64:
        for (i = 0; i < 4; i++) {
          for (j = 0; j < 4; j++) {
            ref_frame_mask |= x->mb_context[i][j].frames_with_high_error;
            mode_mask |= x->mb_context[i][j].modes_with_high_error;
          }
        }
        for (i = 0; i < 4; i++) {
          ref_frame_mask |= x->sb32_context[i].frames_with_high_error;
          mode_mask |= x->sb32_context[i].modes_with_high_error;
        }
        break;
      case BLOCK_32X32:
        for (i = 0; i < 4; i++) {
          ref_frame_mask |=
              x->mb_context[xd->sb_index][i].frames_with_high_error;
          mode_mask |= x->mb_context[xd->sb_index][i].modes_with_high_error;
        }
        break;
      default:
        // Until we handle all block sizes set it to present;
        ref_frame_mask = 0;
        mode_mask = 0;
        break;
    }
    ref_frame_mask = ~ref_frame_mask;
    mode_mask = ~mode_mask;
  }

  for (ref_frame = LAST_FRAME; ref_frame <= ALTREF_FRAME; ref_frame++) {
    if (cpi->ref_frame_flags & flag_list[ref_frame]) {
      setup_buffer_inter(cpi, x, idx_list[ref_frame], ref_frame, block_size,
                         mi_row, mi_col, frame_mv[NEARESTMV], frame_mv[NEARMV],
                         frame_mdcounts, yv12_mb, scale_factor);
    }
    frame_mv[NEWMV][ref_frame].as_int = INVALID_MV;
    frame_mv[ZEROMV][ref_frame].as_int = 0;
  }
  if (cpi->Speed == 0
      || (cpi->Speed > 0 && (ref_frame_mask & (1 << INTRA_FRAME)))) {
    mbmi->mode = DC_PRED;
    for (i = 0; i <= (bsize < BLOCK_SIZE_MB16X16 ? TX_4X4 :
                      (bsize < BLOCK_SIZE_SB32X32 ? TX_8X8 :
                       (bsize < BLOCK_SIZE_SB64X64 ? TX_16X16 : TX_32X32)));
         i++) {
      mbmi->txfm_size = i;
      rd_pick_intra_sbuv_mode(cpi, x, &rate_uv_intra[i], &rate_uv_tokenonly[i],
                              &dist_uv[i], &skip_uv[i], bsize);
      mode_uv[i] = mbmi->uv_mode;
    }
  }

  for (mode_index = 0; mode_index < MAX_MODES; ++mode_index) {
    int mode_excluded = 0;
    int64_t this_rd = INT64_MAX;
    int disable_skip = 0;
    int other_cost = 0;
    int compmode_cost = 0;
    int rate2 = 0, rate_y = 0, rate_uv = 0;
    int distortion2 = 0, distortion_y = 0, distortion_uv = 0;
    int skippable;
    int64_t txfm_cache[NB_TXFM_MODES];
#if CONFIG_COMP_INTERINTRA_PRED
    int compmode_interintra_cost = 0;
#endif

    // Test best rd so far against threshold for trying this mode.
    if (best_rd <= cpi->rd_threshes[mode_index] ||
        cpi->rd_threshes[mode_index] == INT_MAX) {
      continue;
    }

    x->skip = 0;
    this_mode = vp9_mode_order[mode_index].mode;
    ref_frame = vp9_mode_order[mode_index].ref_frame;
    if (!(ref_frame == INTRA_FRAME
        || (cpi->ref_frame_flags & flag_list[ref_frame]))) {
      continue;
    }
    if (cpi->Speed > 0) {
      if (!(ref_frame_mask & (1 << ref_frame))) {
        continue;
      }
      if (!(mode_mask & (1 << this_mode))) {
        continue;
      }
      if (vp9_mode_order[mode_index].second_ref_frame != NONE
          && !(ref_frame_mask
              & (1 << vp9_mode_order[mode_index].second_ref_frame))) {
        continue;
      }
    }

    mbmi->ref_frame = ref_frame;
    mbmi->second_ref_frame = vp9_mode_order[mode_index].second_ref_frame;
    set_scale_factors(xd, mbmi->ref_frame, mbmi->second_ref_frame,
                      scale_factor);
    comp_pred = mbmi->second_ref_frame > INTRA_FRAME;
    mbmi->mode = this_mode;
    mbmi->uv_mode = DC_PRED;
#if CONFIG_COMP_INTERINTRA_PRED
    mbmi->interintra_mode = (MB_PREDICTION_MODE)(DC_PRED - 1);
    mbmi->interintra_uv_mode = (MB_PREDICTION_MODE)(DC_PRED - 1);
#endif

    // Evaluate all sub-pel filters irrespective of whether we can use
    // them for this frame.
    mbmi->interp_filter = cm->mcomp_filter_type;
    vp9_setup_interp_filters(xd, mbmi->interp_filter, &cpi->common);

    // if (!(cpi->ref_frame_flags & flag_list[ref_frame]))
    //  continue;

    if (
#if CONFIG_SB8X8
        bsize != BLOCK_SIZE_SB8X8 &&
        (this_mode == I4X4_PRED || this_mode == SPLITMV)
#else
        this_mode == I4X4_PRED ||
        this_mode == I8X8_PRED ||
        this_mode == SPLITMV
#endif
        )
      continue;
    //  if (vp9_mode_order[mode_index].second_ref_frame == INTRA_FRAME)
    //  continue;

    if (comp_pred) {
      if (ref_frame == ALTREF_FRAME) {
        second_ref = LAST_FRAME;
      } else {
        second_ref = ref_frame + 1;
      }
      if (!(cpi->ref_frame_flags & flag_list[second_ref]))
        continue;
      mbmi->second_ref_frame = second_ref;
      set_scale_factors(xd, mbmi->ref_frame, mbmi->second_ref_frame,
                        scale_factor);

      mode_excluded =
          mode_excluded ?
              mode_excluded : cm->comp_pred_mode == SINGLE_PREDICTION_ONLY;
    } else {
      // mbmi->second_ref_frame = vp9_mode_order[mode_index].second_ref_frame;
      if (ref_frame != INTRA_FRAME) {
        if (mbmi->second_ref_frame != INTRA_FRAME)
          mode_excluded =
              mode_excluded ?
                  mode_excluded : cm->comp_pred_mode == COMP_PREDICTION_ONLY;
#if CONFIG_COMP_INTERINTRA_PRED
        else
          mode_excluded = mode_excluded ? mode_excluded : !cm->use_interintra;
#endif
      }
    }

    setup_pre_planes(xd, &yv12_mb[ref_frame],
        comp_pred ? &yv12_mb[second_ref] : NULL, 0, 0, NULL, NULL);

    vpx_memcpy(mdcounts, frame_mdcounts[ref_frame], sizeof(mdcounts));

    // If the segment reference frame feature is enabled....
    // then do nothing if the current ref frame is not allowed..
    if (vp9_segfeature_active(xd, segment_id, SEG_LVL_REF_FRAME) &&
        !vp9_check_segref(xd, segment_id, ref_frame)) {
      continue;
    // If the segment skip feature is enabled....
    // then do nothing if the current mode is not allowed..
    } else if (vp9_segfeature_active(xd, segment_id, SEG_LVL_SKIP) &&
               (this_mode != ZEROMV)) {
      continue;
    // Disable this drop out case if the ref frame
    // segment level feature is enabled for this segment. This is to
    // prevent the possibility that we end up unable to pick any mode.
    } else if (!vp9_segfeature_active(xd, segment_id, SEG_LVL_REF_FRAME)) {
      // Only consider ZEROMV/ALTREF_FRAME for alt ref frame,
      // unless ARNR filtering is enabled in which case we want
      // an unfiltered alternative
      if (cpi->is_src_frame_alt_ref && (cpi->oxcf.arnr_max_frames == 0)) {
        if (this_mode != ZEROMV || ref_frame != ALTREF_FRAME) {
          continue;
        }
      }
    }

#if CONFIG_SB8X8
    if (this_mode == I4X4_PRED) {
      int rate;

      // Note the rate value returned here includes the cost of coding
      // the I4X4_PRED mode : x->mbmode_cost[xd->frame_type][I4X4_PRED];
      assert(bsize == BLOCK_SIZE_SB8X8);
      mbmi->txfm_size = TX_4X4;
      rd_pick_intra4x4mby_modes(cpi, x, &rate, &rate_y,
                                &distortion_y, INT64_MAX);
      rate2 += rate;
      rate2 += intra_cost_penalty;
      distortion2 += distortion_y;

      rate2 += rate_uv_intra[TX_4X4];
      rate_uv = rate_uv_intra[TX_4X4];
      distortion2 += dist_uv[TX_4X4];
      distortion_uv = dist_uv[TX_4X4];
      mbmi->uv_mode = mode_uv[TX_4X4];
    } else
#endif
    if (ref_frame == INTRA_FRAME) {
      TX_SIZE uv_tx;
      vp9_build_intra_predictors_sby_s(xd, bsize);
      super_block_yrd(cpi, x, &rate_y, &distortion_y, &skippable,
                      bsize, txfm_cache);

      uv_tx = mbmi->txfm_size;
      if (bsize < BLOCK_SIZE_MB16X16 && uv_tx == TX_8X8)
        uv_tx = TX_4X4;
      if (bsize < BLOCK_SIZE_SB32X32 && uv_tx == TX_16X16)
        uv_tx = TX_8X8;
      else if (bsize < BLOCK_SIZE_SB64X64 && uv_tx == TX_32X32)
        uv_tx = TX_16X16;

      rate_uv = rate_uv_intra[uv_tx];
      distortion_uv = dist_uv[uv_tx];
      skippable = skippable && skip_uv[uv_tx];
      mbmi->uv_mode = mode_uv[uv_tx];

      rate2 = rate_y + x->mbmode_cost[cm->frame_type][mbmi->mode] + rate_uv;
      if (mbmi->mode != DC_PRED && mbmi->mode != TM_PRED)
        rate2 += intra_cost_penalty;
      distortion2 = distortion_y + distortion_uv;
#if CONFIG_SB8X8
    } else if (this_mode == SPLITMV) {
      const int is_comp_pred = mbmi->second_ref_frame > 0;
      int rate, distortion;
      int64_t this_rd_thresh;
      int64_t tmp_rd, tmp_best_rd = INT64_MAX, tmp_best_rdu = INT64_MAX;
      int tmp_best_rate = INT_MAX, tmp_best_ratey = INT_MAX;
      int tmp_best_distortion = INT_MAX, tmp_best_skippable = 0;
      int switchable_filter_index;
      int_mv *second_ref = is_comp_pred ?
          &mbmi->ref_mvs[mbmi->second_ref_frame][0] : NULL;
      union b_mode_info tmp_best_bmodes[16];
      MB_MODE_INFO tmp_best_mbmode;
      PARTITION_INFO tmp_best_partition;
      int pred_exists = 0;
      int uv_skippable;

      this_rd_thresh = (mbmi->ref_frame == LAST_FRAME) ?
          cpi->rd_threshes[THR_NEWMV] : cpi->rd_threshes[THR_NEWA];
      this_rd_thresh = (mbmi->ref_frame == GOLDEN_FRAME) ?
          cpi->rd_threshes[THR_NEWG] : this_rd_thresh;
      xd->mode_info_context->mbmi.txfm_size = TX_4X4;

      for (switchable_filter_index = 0;
           switchable_filter_index < VP9_SWITCHABLE_FILTERS;
           ++switchable_filter_index) {
        int newbest;
        mbmi->interp_filter =
        vp9_switchable_interp[switchable_filter_index];
        vp9_setup_interp_filters(xd, mbmi->interp_filter, &cpi->common);

        tmp_rd = rd_pick_best_mbsegmentation(cpi, x,
                                             &mbmi->ref_mvs[mbmi->ref_frame][0],
                                             second_ref, INT64_MAX, mdcounts,
                                             &rate, &rate_y, &distortion,
                                             &skippable,
                                             (int)this_rd_thresh, seg_mvs);
        if (cpi->common.mcomp_filter_type == SWITCHABLE) {
          int rs = SWITCHABLE_INTERP_RATE_FACTOR * x->switchable_interp_costs
          [vp9_get_pred_context(&cpi->common, xd,
                                PRED_SWITCHABLE_INTERP)]
          [vp9_switchable_interp_map[mbmi->interp_filter]];
          tmp_rd += RDCOST(x->rdmult, x->rddiv, rs, 0);
        }
        newbest = (tmp_rd < tmp_best_rd);
        if (newbest) {
          tmp_best_filter = mbmi->interp_filter;
          tmp_best_rd = tmp_rd;
        }
        if ((newbest && cm->mcomp_filter_type == SWITCHABLE) ||
            (mbmi->interp_filter == cm->mcomp_filter_type &&
             cm->mcomp_filter_type != SWITCHABLE)) {
              tmp_best_rdu = tmp_rd;
              tmp_best_rate = rate;
              tmp_best_ratey = rate_y;
              tmp_best_distortion = distortion;
              tmp_best_skippable = skippable;
              vpx_memcpy(&tmp_best_mbmode, mbmi, sizeof(MB_MODE_INFO));
              vpx_memcpy(&tmp_best_partition, x->partition_info,
                         sizeof(PARTITION_INFO));
              for (i = 0; i < 4; i++) {
                tmp_best_bmodes[i] = xd->mode_info_context->bmi[i];
              }
              pred_exists = 1;
            }
      }  // switchable_filter_index loop

      mbmi->interp_filter = (cm->mcomp_filter_type == SWITCHABLE ?
                             tmp_best_filter : cm->mcomp_filter_type);
      vp9_setup_interp_filters(xd, mbmi->interp_filter, &cpi->common);
      if (!pred_exists) {
        // Handles the special case when a filter that is not in the
        // switchable list (bilinear, 6-tap) is indicated at the frame level
        tmp_rd = rd_pick_best_mbsegmentation(cpi, x,
                                             &mbmi->ref_mvs[mbmi->ref_frame][0],
                                             second_ref, INT64_MAX, mdcounts,
                                             &rate, &rate_y, &distortion,
                                             &skippable,
                                             (int)this_rd_thresh, seg_mvs);
      } else {
        if (cpi->common.mcomp_filter_type == SWITCHABLE) {
          int rs = SWITCHABLE_INTERP_RATE_FACTOR * x->switchable_interp_costs
              [vp9_get_pred_context(&cpi->common, xd,
                                    PRED_SWITCHABLE_INTERP)]
              [vp9_switchable_interp_map[mbmi->interp_filter]];
          tmp_best_rdu -= RDCOST(x->rdmult, x->rddiv, rs, 0);
        }
        tmp_rd = tmp_best_rdu;
        rate = tmp_best_rate;
        rate_y = tmp_best_ratey;
        distortion = tmp_best_distortion;
        skippable = tmp_best_skippable;
        vpx_memcpy(mbmi, &tmp_best_mbmode, sizeof(MB_MODE_INFO));
        vpx_memcpy(x->partition_info, &tmp_best_partition,
                   sizeof(PARTITION_INFO));
        for (i = 0; i < 4; i++) {
          xd->mode_info_context->bmi[i] = tmp_best_bmodes[i];
        }
      }

      rate2 += rate;
      distortion2 += distortion;

      if (cpi->common.mcomp_filter_type == SWITCHABLE)
        rate2 += SWITCHABLE_INTERP_RATE_FACTOR * x->switchable_interp_costs
            [vp9_get_pred_context(&cpi->common, xd, PRED_SWITCHABLE_INTERP)]
            [vp9_switchable_interp_map[mbmi->interp_filter]];

      // If even the 'Y' rd value of split is higher than best so far
      // then dont bother looking at UV
      vp9_build_inter_predictors_sbuv(&x->e_mbd, mi_row, mi_col,
                                      bsize);
      vp9_subtract_sbuv(x, bsize);
      super_block_uvrd_for_txfm(cm, x, &rate_uv, &distortion_uv,
                                &uv_skippable, bsize, TX_4X4);
      rate2 += rate_uv;
      distortion2 += distortion_uv;
      skippable = skippable && uv_skippable;

      if (!mode_excluded) {
        if (is_comp_pred)
          mode_excluded = cpi->common.comp_pred_mode == SINGLE_PREDICTION_ONLY;
        else
          mode_excluded = cpi->common.comp_pred_mode == COMP_PREDICTION_ONLY;
      }

      compmode_cost =
          vp9_cost_bit(vp9_get_pred_prob(cm, xd, PRED_COMP), is_comp_pred);
      mbmi->mode = this_mode;
#endif
    } else {
      YV12_BUFFER_CONFIG *scaled_ref_frame = NULL;
      int fb;

      if (mbmi->ref_frame == LAST_FRAME) {
        fb = cpi->lst_fb_idx;
      } else if (mbmi->ref_frame == GOLDEN_FRAME) {
        fb = cpi->gld_fb_idx;
      } else {
        fb = cpi->alt_fb_idx;
      }

      if (cpi->scaled_ref_idx[fb] != cm->ref_frame_map[fb])
        scaled_ref_frame = &cm->yv12_fb[cpi->scaled_ref_idx[fb]];

#if CONFIG_COMP_INTERINTRA_PRED
      if (mbmi->second_ref_frame == INTRA_FRAME) {
        if (best_intra16_mode == DC_PRED - 1) continue;
        mbmi->interintra_mode = best_intra16_mode;
#if SEPARATE_INTERINTRA_UV
        mbmi->interintra_uv_mode = best_intra16_uv_mode;
#else
        mbmi->interintra_uv_mode = best_intra16_mode;
#endif
      }
#endif
      this_rd = handle_inter_mode(cpi, x, bsize,
                                  mdcounts, txfm_cache,
                                  &rate2, &distortion2, &skippable,
                                  &compmode_cost,
#if CONFIG_COMP_INTERINTRA_PRED
                                  &compmode_interintra_cost,
#endif
                                  &rate_y, &distortion_y,
                                  &rate_uv, &distortion_uv,
                                  &mode_excluded, &disable_skip,
                                  mode_index, &tmp_best_filter, frame_mv,
                                  scaled_ref_frame, mi_row, mi_col);
      if (this_rd == INT64_MAX)
        continue;
    }

#if CONFIG_COMP_INTERINTRA_PRED
    if (cpi->common.use_interintra) {
      rate2 += compmode_interintra_cost;
    }
#endif
    if (cpi->common.comp_pred_mode == HYBRID_PREDICTION) {
      rate2 += compmode_cost;
    }

    // Estimate the reference frame signaling cost and add it
    // to the rolling cost variable.
    rate2 += ref_costs[xd->mode_info_context->mbmi.ref_frame];

    if (!disable_skip) {
      // Test for the condition where skip block will be activated
      // because there are no non zero coefficients and make any
      // necessary adjustment for rate. Ignore if skip is coded at
      // segment level as the cost wont have been added in.
      int mb_skip_allowed;

      // Is Mb level skip allowed (i.e. not coded at segment level).
      mb_skip_allowed = !vp9_segfeature_active(xd, segment_id, SEG_LVL_SKIP);

      if (skippable) {
        // Back out the coefficient coding costs
        rate2 -= (rate_y + rate_uv);
        // for best_yrd calculation
        rate_uv = 0;

        if (mb_skip_allowed) {
          int prob_skip_cost;

          // Cost the skip mb case
          vp9_prob skip_prob =
            vp9_get_pred_prob(cm, xd, PRED_MBSKIP);

          if (skip_prob) {
            prob_skip_cost = vp9_cost_bit(skip_prob, 1);
            rate2 += prob_skip_cost;
            other_cost += prob_skip_cost;
          }
        }
      } else if (mb_skip_allowed) {
        // Add in the cost of the no skip flag.
        int prob_skip_cost = vp9_cost_bit(vp9_get_pred_prob(cm, xd,
                                                        PRED_MBSKIP), 0);
        rate2 += prob_skip_cost;
        other_cost += prob_skip_cost;
      }

      // Calculate the final RD estimate for this mode.
      this_rd = RDCOST(x->rdmult, x->rddiv, rate2, distortion2);
    }

#if 0
    // Keep record of best intra distortion
    if ((xd->mode_info_context->mbmi.ref_frame == INTRA_FRAME) &&
        (this_rd < best_intra_rd)) {
      best_intra_rd = this_rd;
      *returnintra = distortion2;
    }
#endif
#if CONFIG_COMP_INTERINTRA_PRED
    if ((mbmi->ref_frame == INTRA_FRAME) &&
        (this_mode <= TM_PRED) &&
        (this_rd < best_intra16_rd)) {
      best_intra16_rd = this_rd;
      best_intra16_mode = this_mode;
#if SEPARATE_INTERINTRA_UV
      best_intra16_uv_mode = (mbmi->txfm_size != TX_4X4 ?
                              mode_uv_8x8 : mode_uv_4x4);
#endif
    }
#endif

    if (!disable_skip && mbmi->ref_frame == INTRA_FRAME)
      for (i = 0; i < NB_PREDICTION_TYPES; ++i)
        best_pred_rd[i] = MIN(best_pred_rd[i], this_rd);

    if (this_rd < best_overall_rd) {
      best_overall_rd = this_rd;
      best_filter = tmp_best_filter;
      best_mode = this_mode;
#if CONFIG_COMP_INTERINTRA_PRED
      is_best_interintra = (mbmi->second_ref_frame == INTRA_FRAME);
#endif
    }

    // Store the respective mode distortions for later use.
    if (mode_distortions[this_mode] == -1
        || distortion2 < mode_distortions[this_mode]) {
      mode_distortions[this_mode] = distortion2;
    }
    if (frame_distortions[mbmi->ref_frame] == -1
        || distortion2 < frame_distortions[mbmi->ref_frame]) {
      frame_distortions[mbmi->ref_frame] = distortion2;
    }

    // Did this mode help.. i.e. is it the new best mode
    if (this_rd < best_rd || x->skip) {
      if (!mode_excluded) {
        // Note index of best mode so far
        best_mode_index = mode_index;

        if (this_mode <= I4X4_PRED) {
          /* required for left and above block mv */
          mbmi->mv[0].as_int = 0;
        }

        other_cost += ref_costs[xd->mode_info_context->mbmi.ref_frame];
        *returnrate = rate2;
        *returndistortion = distortion2;
        best_rd = this_rd;
        vpx_memcpy(&best_mbmode, mbmi, sizeof(MB_MODE_INFO));
#if CONFIG_SB8X8
        vpx_memcpy(&best_partition, x->partition_info, sizeof(PARTITION_INFO));

        if (this_mode == I4X4_PRED || this_mode == SPLITMV) {
          for (i = 0; i < 4; i++) {
            best_bmodes[i] = xd->mode_info_context->bmi[i];
          }
        }
#endif
      }
#if 0
      // Testing this mode gave rise to an improvement in best error score.
      // Lower threshold a bit for next time
      cpi->rd_thresh_mult[mode_index] =
          (cpi->rd_thresh_mult[mode_index] >= (MIN_THRESHMULT + 2)) ?
              cpi->rd_thresh_mult[mode_index] - 2 : MIN_THRESHMULT;
      cpi->rd_threshes[mode_index] =
          (cpi->rd_baseline_thresh[mode_index] >> 7)
              * cpi->rd_thresh_mult[mode_index];
#endif
    } else {
      // If the mode did not help improve the best error case then
      // raise the threshold for testing that mode next time around.
#if 0
      cpi->rd_thresh_mult[mode_index] += 4;

      if (cpi->rd_thresh_mult[mode_index] > MAX_THRESHMULT)
        cpi->rd_thresh_mult[mode_index] = MAX_THRESHMULT;

      cpi->rd_threshes[mode_index] =
          (cpi->rd_baseline_thresh[mode_index] >> 7)
              * cpi->rd_thresh_mult[mode_index];
#endif
    }

    /* keep record of best compound/single-only prediction */
    if (!disable_skip && mbmi->ref_frame != INTRA_FRAME) {
      int single_rd, hybrid_rd, single_rate, hybrid_rate;

      if (cpi->common.comp_pred_mode == HYBRID_PREDICTION) {
        single_rate = rate2 - compmode_cost;
        hybrid_rate = rate2;
      } else {
        single_rate = rate2;
        hybrid_rate = rate2 + compmode_cost;
      }

      single_rd = RDCOST(x->rdmult, x->rddiv, single_rate, distortion2);
      hybrid_rd = RDCOST(x->rdmult, x->rddiv, hybrid_rate, distortion2);

      if (mbmi->second_ref_frame <= INTRA_FRAME &&
          single_rd < best_pred_rd[SINGLE_PREDICTION_ONLY]) {
        best_pred_rd[SINGLE_PREDICTION_ONLY] = single_rd;
      } else if (mbmi->second_ref_frame > INTRA_FRAME &&
                 single_rd < best_pred_rd[COMP_PREDICTION_ONLY]) {
        best_pred_rd[COMP_PREDICTION_ONLY] = single_rd;
      }
      if (hybrid_rd < best_pred_rd[HYBRID_PREDICTION])
        best_pred_rd[HYBRID_PREDICTION] = hybrid_rd;
    }

    /* keep record of best txfm size */
    if (bsize < BLOCK_SIZE_SB32X32) {
      if (bsize < BLOCK_SIZE_MB16X16) {
        if (this_mode == SPLITMV || this_mode == I4X4_PRED)
          txfm_cache[ALLOW_8X8] = txfm_cache[ONLY_4X4];
        txfm_cache[ALLOW_16X16] = txfm_cache[ALLOW_8X8];
      }
      txfm_cache[ALLOW_32X32] = txfm_cache[ALLOW_16X16];
    }
    if (!mode_excluded && this_rd != INT64_MAX) {
      for (i = 0; i < NB_TXFM_MODES; i++) {
        int64_t adj_rd;
        if (this_mode != I4X4_PRED) {
          adj_rd = this_rd + txfm_cache[i] - txfm_cache[cm->txfm_mode];
        } else {
          adj_rd = this_rd;
        }
        if (adj_rd < best_txfm_rd[i])
          best_txfm_rd[i] = adj_rd;
      }
    }

    if (x->skip && !mode_excluded)
      break;
  }
  // Flag all modes that have a distortion thats > 2x the best we found at
  // this level.
  for (mode_index = 0; mode_index < MB_MODE_COUNT; ++mode_index) {
    if (mode_index == NEARESTMV || mode_index == NEARMV || mode_index == NEWMV
        || mode_index == SPLITMV)
      continue;

    if (mode_distortions[mode_index] > 2 * *returndistortion) {
      ctx->modes_with_high_error |= (1 << mode_index);
    }
  }

  // Flag all ref frames that have a distortion thats > 2x the best we found at
  // this level.
  for (ref_frame = INTRA_FRAME; ref_frame <= ALTREF_FRAME; ref_frame++) {
    if (frame_distortions[ref_frame] > 2 * *returndistortion) {
      ctx->frames_with_high_error |= (1 << ref_frame);
    }
  }



  assert((cm->mcomp_filter_type == SWITCHABLE) ||
         (cm->mcomp_filter_type == best_mbmode.interp_filter) ||
         (best_mbmode.mode <= I4X4_PRED));

#if CONFIG_COMP_INTERINTRA_PRED
  ++cpi->interintra_select_count[is_best_interintra];
  // if (is_best_interintra)  printf("best_interintra\n");
#endif

  // Accumulate filter usage stats
  // TODO(agrange): Use RD criteria to select interpolation filter mode.
  if (is_inter_mode(best_mode))
    ++cpi->best_switchable_interp_count[vp9_switchable_interp_map[best_filter]];

  // TODO(rbultje) integrate with RD thresholding
#if 0
  // Reduce the activation RD thresholds for the best choice mode
  if ((cpi->rd_baseline_thresh[best_mode_index] > 0) &&
      (cpi->rd_baseline_thresh[best_mode_index] < (INT_MAX >> 2))) {
    int best_adjustment = (cpi->rd_thresh_mult[best_mode_index] >> 2);

    cpi->rd_thresh_mult[best_mode_index] =
      (cpi->rd_thresh_mult[best_mode_index] >= (MIN_THRESHMULT + best_adjustment)) ?
      cpi->rd_thresh_mult[best_mode_index] - best_adjustment : MIN_THRESHMULT;
    cpi->rd_threshes[best_mode_index] =
      (cpi->rd_baseline_thresh[best_mode_index] >> 7) * cpi->rd_thresh_mult[best_mode_index];
  }
#endif

  // This code forces Altref,0,0 and skip for the frame that overlays a
  // an alrtef unless Altref is filtered. However, this is unsafe if
  // segment level coding of ref frame is enabled for this segment.
  if (!vp9_segfeature_active(xd, segment_id, SEG_LVL_REF_FRAME) &&
      cpi->is_src_frame_alt_ref &&
      (cpi->oxcf.arnr_max_frames == 0) &&
      (best_mbmode.mode != ZEROMV || best_mbmode.ref_frame != ALTREF_FRAME)) {
    mbmi->mode = ZEROMV;
    mbmi->ref_frame = ALTREF_FRAME;
    mbmi->second_ref_frame = NONE;
    mbmi->mv[0].as_int = 0;
    mbmi->uv_mode = DC_PRED;
    mbmi->mb_skip_coeff = 1;
#if !CONFIG_SB8X8
    mbmi->partitioning = 0;
#endif
    if (cm->txfm_mode == TX_MODE_SELECT) {
      if (bsize >= BLOCK_SIZE_SB32X32)
        mbmi->txfm_size = TX_32X32;
#if CONFIG_SB8X8
      else if (bsize >= BLOCK_SIZE_MB16X16)
#else
      else
#endif
        mbmi->txfm_size = TX_16X16;
#if CONFIG_SB8X8
      else
        mbmi->txfm_size = TX_8X8;
#endif
    }

    vpx_memset(best_txfm_diff, 0, sizeof(best_txfm_diff));
    vpx_memset(best_pred_diff, 0, sizeof(best_pred_diff));
    goto end;
  }

  // macroblock modes
  vpx_memcpy(mbmi, &best_mbmode, sizeof(MB_MODE_INFO));
#if CONFIG_SB8X8
  if (best_mbmode.mode == I4X4_PRED) {
    for (i = 0; i < 4; i++) {
      xd->mode_info_context->bmi[i].as_mode = best_bmodes[i].as_mode;
    }
  }

  if (best_mbmode.mode == SPLITMV) {
    for (i = 0; i < 4; i++)
      xd->mode_info_context->bmi[i].as_mv[0].as_int =
          best_bmodes[i].as_mv[0].as_int;
    if (mbmi->second_ref_frame > 0)
      for (i = 0; i < 4; i++)
        xd->mode_info_context->bmi[i].as_mv[1].as_int =
            best_bmodes[i].as_mv[1].as_int;

    vpx_memcpy(x->partition_info, &best_partition, sizeof(PARTITION_INFO));

    mbmi->mv[0].as_int = x->partition_info->bmi[3].mv.as_int;
    mbmi->mv[1].as_int = x->partition_info->bmi[3].second_mv.as_int;
  }
#endif
  for (i = 0; i < NB_PREDICTION_TYPES; ++i) {
    if (best_pred_rd[i] == INT64_MAX)
      best_pred_diff[i] = INT_MIN;
    else
      best_pred_diff[i] = best_rd - best_pred_rd[i];
  }

  if (!x->skip) {
    for (i = 0; i < NB_TXFM_MODES; i++) {
      if (best_txfm_rd[i] == INT64_MAX)
        best_txfm_diff[i] = 0;
      else
        best_txfm_diff[i] = best_rd - best_txfm_rd[i];
    }
  } else {
    vpx_memset(best_txfm_diff, 0, sizeof(best_txfm_diff));
  }

 end:
  set_scale_factors(xd, mbmi->ref_frame, mbmi->second_ref_frame,
                    scale_factor);
  store_coding_context(x, ctx, best_mode_index,
#if CONFIG_SB8X8
                       &best_partition,
#else
                       NULL,
#endif
                       &mbmi->ref_mvs[mbmi->ref_frame][0],
                       &mbmi->ref_mvs[mbmi->second_ref_frame < 0 ? 0 :
                                      mbmi->second_ref_frame][0],
                       best_pred_diff, best_txfm_diff);

  return best_rd;
}

#if !CONFIG_SB8X8
void vp9_pick_mode_inter_macroblock(VP9_COMP *cpi, MACROBLOCK *x,
                                    int mi_row, int mi_col,
                                    int *totalrate, int *totaldist) {
  MACROBLOCKD *const xd = &x->e_mbd;
  MB_MODE_INFO * mbmi = &x->e_mbd.mode_info_context->mbmi;
  int rate, distortion;
  int64_t intra_error = 0;
  unsigned char *segment_id = &mbmi->segment_id;

  x->encode_breakout = xd->segmentation_enabled ?
                         cpi->segment_encode_breakout[*segment_id] :
                         cpi->oxcf.encode_breakout;

  // if (cpi->sf.RD)
  // For now this codebase is limited to a single rd encode path
  {
    int zbin_mode_boost_enabled = cpi->zbin_mode_boost_enabled;

    rd_pick_inter_mode(cpi, x, mi_row, mi_col, &rate,
                       &distortion, &intra_error);

    /* restore cpi->zbin_mode_boost_enabled */
    cpi->zbin_mode_boost_enabled = zbin_mode_boost_enabled;
  }
  // else
  // The non rd encode path has been deleted from this code base
  // to simplify development
  //    vp9_pick_inter_mode

  // Store metrics so they can be added in to totals if this mode is picked
  x->mb_context[xd->sb_index][xd->mb_index].distortion  = distortion;
  x->mb_context[xd->sb_index][xd->mb_index].intra_error = intra_error;

  *totalrate = rate;
  *totaldist = distortion;
}
#endif
