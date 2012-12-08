/*
 *  Copyright (c) 2010 The WebM project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */


#include <math.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include "vp9/encoder/vp9_onyx_int.h"
#include "vp9/encoder/vp9_tokenize.h"
#include "vpx_mem/vpx_mem.h"

#include "vp9/common/vp9_pred_common.h"
#include "vp9/common/vp9_seg_common.h"
#include "vp9/common/vp9_entropy.h"

/* Global event counters used for accumulating statistics across several
   compressions, then generating vp9_context.c = initial stats. */

#ifdef ENTROPY_STATS
vp9_coeff_accum context_counters_4x4[BLOCK_TYPES_4X4];
vp9_coeff_accum hybrid_context_counters_4x4[BLOCK_TYPES_4X4];
vp9_coeff_accum context_counters_8x8[BLOCK_TYPES_8X8];
vp9_coeff_accum hybrid_context_counters_8x8[BLOCK_TYPES_8X8];
vp9_coeff_accum context_counters_16x16[BLOCK_TYPES_16X16];
vp9_coeff_accum hybrid_context_counters_16x16[BLOCK_TYPES_16X16];
#if CONFIG_TX32X32
vp9_coeff_accum context_counters_32x32[BLOCK_TYPES_32X32];
#endif

extern vp9_coeff_stats tree_update_hist_4x4[BLOCK_TYPES_4X4];
extern vp9_coeff_stats hybrid_tree_update_hist_4x4[BLOCK_TYPES_4X4];
extern vp9_coeff_stats tree_update_hist_8x8[BLOCK_TYPES_8X8];
extern vp9_coeff_stats hybrid_tree_update_hist_8x8[BLOCK_TYPES_8X8];
extern vp9_coeff_stats tree_update_hist_16x16[BLOCK_TYPES_16X16];
extern vp9_coeff_stats hybrid_tree_update_hist_16x16[BLOCK_TYPES_16X16];
#if CONFIG_TX32X32
extern vp9_coeff_stats tree_update_hist_32x32[BLOCK_TYPES_32X32];
#endif
#endif  /* ENTROPY_STATS */

static TOKENVALUE dct_value_tokens[DCT_MAX_VALUE * 2];
const TOKENVALUE *vp9_dct_value_tokens_ptr;
static int dct_value_cost[DCT_MAX_VALUE * 2];
const int *vp9_dct_value_cost_ptr;

static void fill_value_tokens() {

  TOKENVALUE *const t = dct_value_tokens + DCT_MAX_VALUE;
  vp9_extra_bit_struct *const e = vp9_extra_bits;

  int i = -DCT_MAX_VALUE;
  int sign = 1;

  do {
    if (!i)
      sign = 0;

    {
      const int a = sign ? -i : i;
      int eb = sign;

      if (a > 4) {
        int j = 4;

        while (++j < 11  &&  e[j].base_val <= a) {}

        t[i].Token = --j;
        eb |= (a - e[j].base_val) << 1;
      } else
        t[i].Token = a;

      t[i].Extra = eb;
    }

    // initialize the cost for extra bits for all possible coefficient value.
    {
      int cost = 0;
      vp9_extra_bit_struct *p = vp9_extra_bits + t[i].Token;

      if (p->base_val) {
        const int extra = t[i].Extra;
        const int Length = p->Len;

        if (Length)
          cost += treed_cost(p->tree, p->prob, extra >> 1, Length);

        cost += vp9_cost_bit(vp9_prob_half, extra & 1); /* sign */
        dct_value_cost[i + DCT_MAX_VALUE] = cost;
      }

    }

  } while (++i < DCT_MAX_VALUE);

  vp9_dct_value_tokens_ptr = dct_value_tokens + DCT_MAX_VALUE;
  vp9_dct_value_cost_ptr   = dct_value_cost + DCT_MAX_VALUE;
}

static void tokenize_b(VP9_COMP *cpi,
                       MACROBLOCKD *xd,
                       const BLOCKD * const b,
                       TOKENEXTRA **tp,
                       PLANE_TYPE type,
                       ENTROPY_CONTEXT *a,
                       ENTROPY_CONTEXT *l,
                       TX_SIZE tx_size,
                       int dry_run) {
  int pt; /* near block/prev token context index */
  int c = (type == PLANE_TYPE_Y_NO_DC) ? 1 : 0;
  int eob = b->eob;     /* one beyond last nonzero coeff */
  TOKENEXTRA *t = *tp;        /* store tokens starting here */
  const short *qcoeff_ptr = b->qcoeff;
  int seg_eob;
  int segment_id = xd->mode_info_context->mbmi.segment_id;
  const int *bands, *scan;
  vp9_coeff_count *counts;
  vp9_coeff_probs *probs;
  const TX_TYPE tx_type = (type == PLANE_TYPE_Y_WITH_DC) ?
                          get_tx_type(xd, b) : DCT_DCT;

  VP9_COMBINEENTROPYCONTEXTS(pt, *a, *l);
  switch (tx_size) {
    default:
    case TX_4X4:
      seg_eob = 16;
      bands = vp9_coef_bands;
      scan = vp9_default_zig_zag1d;
      if (tx_type != DCT_DCT) {
        counts = cpi->hybrid_coef_counts_4x4;
        probs = cpi->common.fc.hybrid_coef_probs_4x4;
        if (tx_type == ADST_DCT) {
          scan = vp9_row_scan;
        } else if (tx_type == DCT_ADST) {
          scan = vp9_col_scan;
        }
      } else {
        counts = cpi->coef_counts_4x4;
        probs = cpi->common.fc.coef_probs_4x4;
      }
      break;
    case TX_8X8:
      if (type == PLANE_TYPE_Y2) {
        seg_eob = 4;
        bands = vp9_coef_bands;
        scan = vp9_default_zig_zag1d;
      } else {
        seg_eob = 64;
        bands = vp9_coef_bands_8x8;
        scan = vp9_default_zig_zag1d_8x8;
      }
      if (tx_type != DCT_DCT) {
        counts = cpi->hybrid_coef_counts_8x8;
        probs = cpi->common.fc.hybrid_coef_probs_8x8;
      } else {
        counts = cpi->coef_counts_8x8;
        probs = cpi->common.fc.coef_probs_8x8;
      }
      break;
    case TX_16X16:
      seg_eob = 256;
      bands = vp9_coef_bands_16x16;
      scan = vp9_default_zig_zag1d_16x16;
      if (tx_type != DCT_DCT) {
        counts = cpi->hybrid_coef_counts_16x16;
        probs = cpi->common.fc.hybrid_coef_probs_16x16;
      } else {
        counts = cpi->coef_counts_16x16;
        probs = cpi->common.fc.coef_probs_16x16;
      }
#if CONFIG_SUPERBLOCKS && CONFIG_TX32X32
      if (type == PLANE_TYPE_UV) {
        int uv_idx = (((int) (b - xd->block)) - 16) >> 2;
        qcoeff_ptr = xd->sb_coeff_data.qcoeff + 1024 + 256 * uv_idx;
      }
#endif
      break;
#if CONFIG_TX32X32 && CONFIG_SUPERBLOCKS
    case TX_32X32:
      seg_eob = 1024;
      bands = vp9_coef_bands_32x32;
      scan = vp9_default_zig_zag1d_32x32;
      counts = cpi->coef_counts_32x32;
      probs = cpi->common.fc.coef_probs_32x32;
      qcoeff_ptr = xd->sb_coeff_data.qcoeff;
      break;
#endif
  }

  if (vp9_segfeature_active(xd, segment_id, SEG_LVL_EOB))
    seg_eob = vp9_get_segdata(xd, segment_id, SEG_LVL_EOB);

  do {
    const int band = bands[c];
    int token;

    if (c < eob) {
      const int rc = scan[c];
      const int v = qcoeff_ptr[rc];

      assert(-DCT_MAX_VALUE <= v  &&  v < DCT_MAX_VALUE);

      t->Extra = vp9_dct_value_tokens_ptr[v].Extra;
      token    = vp9_dct_value_tokens_ptr[v].Token;
    } else {
      token = DCT_EOB_TOKEN;
    }

    t->Token = token;
    t->context_tree = probs[type][band][pt];
    t->skip_eob_node = (pt == 0) && ((band > 0 && type != PLANE_TYPE_Y_NO_DC) ||
                                     (band > 1 && type == PLANE_TYPE_Y_NO_DC));
    assert(vp9_coef_encodings[t->Token].Len - t->skip_eob_node > 0);
    if (!dry_run) {
      ++counts[type][band][pt][token];
    }
    pt = vp9_prev_token_class[token];
    ++t;
  } while (c < eob && ++c < seg_eob);

  *tp = t;
  *a = *l = (c > !type); /* 0 <-> all coeff data is zero */
}

int vp9_mby_is_skippable_4x4(MACROBLOCKD *xd, int has_2nd_order) {
  int skip = 1;
  int i = 0;

  if (has_2nd_order) {
    for (i = 0; i < 16; i++)
      skip &= (xd->block[i].eob < 2);
    skip &= (!xd->block[24].eob);
  } else {
    for (i = 0; i < 16; i++)
      skip &= (!xd->block[i].eob);
  }
  return skip;
}

int vp9_mbuv_is_skippable_4x4(MACROBLOCKD *xd) {
  int skip = 1;
  int i;

  for (i = 16; i < 24; i++)
    skip &= (!xd->block[i].eob);
  return skip;
}

static int mb_is_skippable_4x4(MACROBLOCKD *xd, int has_2nd_order) {
  return (vp9_mby_is_skippable_4x4(xd, has_2nd_order) &
          vp9_mbuv_is_skippable_4x4(xd));
}

int vp9_mby_is_skippable_8x8(MACROBLOCKD *xd, int has_2nd_order) {
  int skip = 1;
  int i = 0;

  if (has_2nd_order) {
    for (i = 0; i < 16; i += 4)
      skip &= (xd->block[i].eob < 2);
    skip &= (!xd->block[24].eob);
  } else {
    for (i = 0; i < 16; i += 4)
      skip &= (!xd->block[i].eob);
  }
  return skip;
}

int vp9_mbuv_is_skippable_8x8(MACROBLOCKD *xd) {
  return (!xd->block[16].eob) & (!xd->block[20].eob);
}

static int mb_is_skippable_8x8(MACROBLOCKD *xd, int has_2nd_order) {
  return (vp9_mby_is_skippable_8x8(xd, has_2nd_order) &
          vp9_mbuv_is_skippable_8x8(xd));
}

static int mb_is_skippable_8x8_4x4uv(MACROBLOCKD *xd, int has_2nd_order) {
  return (vp9_mby_is_skippable_8x8(xd, has_2nd_order) &
          vp9_mbuv_is_skippable_4x4(xd));
}

int vp9_mby_is_skippable_16x16(MACROBLOCKD *xd) {
  int skip = 1;
  skip &= !xd->block[0].eob;
  return skip;
}

static int mb_is_skippable_16x16(MACROBLOCKD *xd) {
  return (vp9_mby_is_skippable_16x16(xd) & vp9_mbuv_is_skippable_8x8(xd));
}

#if CONFIG_TX32X32 && CONFIG_SUPERBLOCKS
int vp9_sby_is_skippable_32x32(MACROBLOCKD *xd) {
  int skip = 1;
  skip &= !xd->block[0].eob;
  return skip;
}

int vp9_sbuv_is_skippable_16x16(MACROBLOCKD *xd) {
  return (!xd->block[16].eob) & (!xd->block[20].eob);
}

static int sb_is_skippable_32x32(MACROBLOCKD *xd) {
  return vp9_sby_is_skippable_32x32(xd) &&
         vp9_sbuv_is_skippable_16x16(xd);
}

void vp9_tokenize_sb(VP9_COMP *cpi,
                     MACROBLOCKD *xd,
                     TOKENEXTRA **t,
                     int dry_run) {
  VP9_COMMON * const cm = &cpi->common;
  MB_MODE_INFO * const mbmi = &xd->mode_info_context->mbmi;
  TOKENEXTRA *t_backup = *t;
  ENTROPY_CONTEXT *A[2] = { (ENTROPY_CONTEXT *) (xd->above_context + 0),
                            (ENTROPY_CONTEXT *) (xd->above_context + 1), };
  ENTROPY_CONTEXT *L[2] = { (ENTROPY_CONTEXT *) (xd->left_context + 0),
                            (ENTROPY_CONTEXT *) (xd->left_context + 1), };
  const int mb_skip_context = vp9_get_pred_context(cm, xd, PRED_MBSKIP);
  const int segment_id = mbmi->segment_id;
  const int skip_inc =  !vp9_segfeature_active(xd, segment_id, SEG_LVL_EOB) ||
                        (vp9_get_segdata(xd, segment_id, SEG_LVL_EOB) != 0);
  int b;

  mbmi->mb_skip_coeff = sb_is_skippable_32x32(xd);

  if (mbmi->mb_skip_coeff) {
    if (!dry_run)
      cpi->skip_true_count[mb_skip_context] += skip_inc;
    if (!cm->mb_no_coeff_skip) {
      vp9_stuff_sb(cpi, xd, t, dry_run);
    } else {
      vp9_fix_contexts_sb(xd);
    }
    if (dry_run)
      *t = t_backup;
    return;
  }

  if (!dry_run)
    cpi->skip_false_count[mb_skip_context] += skip_inc;

  tokenize_b(cpi, xd, xd->block, t, PLANE_TYPE_Y_WITH_DC,
             A[0], L[0], TX_32X32, dry_run);
  A[0][1] = A[0][2] = A[0][3] = A[0][0];
  L[0][1] = L[0][2] = L[0][3] = L[0][0];

  for (b = 16; b < 24; b += 4) {
    tokenize_b(cpi, xd, xd->block + b, t, PLANE_TYPE_UV,
               A[0] + vp9_block2above_8x8[b], L[0] + vp9_block2left_8x8[b],
               TX_16X16, dry_run);
    A[0][vp9_block2above_8x8[b] + 1] = A[0][vp9_block2above_8x8[b]];
    L[0][vp9_block2left_8x8[b] + 1]  = L[0][vp9_block2left_8x8[b]];
  }
  vpx_memset(&A[0][8], 0, sizeof(A[0][8]));
  vpx_memset(&L[0][8], 0, sizeof(L[0][8]));
  vpx_memcpy(A[1], A[0], sizeof(ENTROPY_CONTEXT_PLANES));
  vpx_memcpy(L[1], L[0], sizeof(ENTROPY_CONTEXT_PLANES));

  if (dry_run)
    *t = t_backup;
}
#endif

void vp9_tokenize_mb(VP9_COMP *cpi,
                     MACROBLOCKD *xd,
                     TOKENEXTRA **t,
                     int dry_run) {
  PLANE_TYPE plane_type;
  int has_2nd_order;
  int b;
  int tx_size = xd->mode_info_context->mbmi.txfm_size;
  int mb_skip_context = vp9_get_pred_context(&cpi->common, xd, PRED_MBSKIP);
  TOKENEXTRA *t_backup = *t;
  ENTROPY_CONTEXT * A = (ENTROPY_CONTEXT *) xd->above_context;
  ENTROPY_CONTEXT * L = (ENTROPY_CONTEXT *) xd->left_context;

  // If the MB is going to be skipped because of a segment level flag
  // exclude this from the skip count stats used to calculate the
  // transmitted skip probability;
  int skip_inc;
  int segment_id = xd->mode_info_context->mbmi.segment_id;

  if (!vp9_segfeature_active(xd, segment_id, SEG_LVL_EOB) ||
      (vp9_get_segdata(xd, segment_id, SEG_LVL_EOB) != 0)) {
    skip_inc = 1;
  } else
    skip_inc = 0;

  has_2nd_order = get_2nd_order_usage(xd);

  switch (tx_size) {
    case TX_16X16:
      xd->mode_info_context->mbmi.mb_skip_coeff = mb_is_skippable_16x16(xd);
      break;
    case TX_8X8:
      if (xd->mode_info_context->mbmi.mode == I8X8_PRED ||
          xd->mode_info_context->mbmi.mode == SPLITMV)
        xd->mode_info_context->mbmi.mb_skip_coeff =
            mb_is_skippable_8x8_4x4uv(xd, 0);
      else
        xd->mode_info_context->mbmi.mb_skip_coeff =
            mb_is_skippable_8x8(xd, has_2nd_order);
      break;

    default:
      xd->mode_info_context->mbmi.mb_skip_coeff =
          mb_is_skippable_4x4(xd, has_2nd_order);
      break;
  }

  if (xd->mode_info_context->mbmi.mb_skip_coeff) {
    if (!dry_run)
      cpi->skip_true_count[mb_skip_context] += skip_inc;
    if (!cpi->common.mb_no_coeff_skip) {
      vp9_stuff_mb(cpi, xd, t, dry_run);
    } else {
      vp9_fix_contexts(xd);
    }
    if (dry_run)
      *t = t_backup;
    return;
  }

  if (!dry_run)
    cpi->skip_false_count[mb_skip_context] += skip_inc;

  if (has_2nd_order) {
    if (tx_size == TX_8X8) {
      tokenize_b(cpi, xd, xd->block + 24, t, PLANE_TYPE_Y2,
                 A + vp9_block2above_8x8[24], L + vp9_block2left_8x8[24],
                 TX_8X8, dry_run);
    } else {
      tokenize_b(cpi, xd, xd->block + 24, t, PLANE_TYPE_Y2,
                 A + vp9_block2above[24], L + vp9_block2left[24],
                 TX_4X4, dry_run);
    }

    plane_type = PLANE_TYPE_Y_NO_DC;
  } else {
    xd->above_context->y2 = 1;
    xd->left_context->y2 = 1;
    plane_type = PLANE_TYPE_Y_WITH_DC;
  }

  if (tx_size == TX_16X16) {
    tokenize_b(cpi, xd, xd->block, t, PLANE_TYPE_Y_WITH_DC,
               A, L, TX_16X16, dry_run);
    A[1] = A[2] = A[3] = A[0];
    L[1] = L[2] = L[3] = L[0];

    for (b = 16; b < 24; b += 4) {
      tokenize_b(cpi, xd, xd->block + b, t, PLANE_TYPE_UV,
                 A + vp9_block2above_8x8[b], L + vp9_block2left_8x8[b],
                 TX_8X8, dry_run);
      A[vp9_block2above_8x8[b] + 1] = A[vp9_block2above_8x8[b]];
      L[vp9_block2left_8x8[b] + 1]  = L[vp9_block2left_8x8[b]];
    }
    A[8] = 0;
    L[8] = 0;
  } else if (tx_size == TX_8X8) {
    for (b = 0; b < 16; b += 4) {
      tokenize_b(cpi, xd, xd->block + b, t, plane_type,
                 A + vp9_block2above_8x8[b], L + vp9_block2left_8x8[b],
                 TX_8X8, dry_run);
      A[vp9_block2above_8x8[b] + 1] = A[vp9_block2above_8x8[b]];
      L[vp9_block2left_8x8[b] + 1]  = L[vp9_block2left_8x8[b]];
    }
    if (xd->mode_info_context->mbmi.mode == I8X8_PRED ||
        xd->mode_info_context->mbmi.mode == SPLITMV) {
      for (b = 16; b < 24; b++) {
        tokenize_b(cpi, xd, xd->block + b, t, PLANE_TYPE_UV,
                   A + vp9_block2above[b], L + vp9_block2left[b],
                   TX_4X4, dry_run);
      }
    } else {
      for (b = 16; b < 24; b += 4) {
        tokenize_b(cpi, xd, xd->block + b, t, PLANE_TYPE_UV,
                   A + vp9_block2above_8x8[b], L + vp9_block2left_8x8[b],
                   TX_8X8, dry_run);
        A[vp9_block2above_8x8[b] + 1] = A[vp9_block2above_8x8[b]];
        L[vp9_block2left_8x8[b] + 1]  = L[vp9_block2left_8x8[b]];
      }
    }
  } else {
    for (b = 0; b < 16; b++) {
      tokenize_b(cpi, xd, xd->block + b, t, plane_type,
                 A + vp9_block2above[b], L + vp9_block2left[b],
                 TX_4X4, dry_run);
    }

    for (b = 16; b < 24; b++) {
      tokenize_b(cpi, xd, xd->block + b, t, PLANE_TYPE_UV,
                 A + vp9_block2above[b], L + vp9_block2left[b],
                 TX_4X4, dry_run);
    }
  }
  if (dry_run)
    *t = t_backup;
}


#ifdef ENTROPY_STATS
void init_context_counters(void) {
  FILE *f = fopen("context.bin", "rb");
  if (!f) {
    vpx_memset(context_counters_4x4, 0, sizeof(context_counters_4x4));
    vpx_memset(hybrid_context_counters_4x4, 0,
               sizeof(hybrid_context_counters_4x4));
    vpx_memset(context_counters_8x8, 0, sizeof(context_counters_8x8));
    vpx_memset(hybrid_context_counters_8x8, 0,
               sizeof(hybrid_context_counters_8x8));
    vpx_memset(context_counters_16x16, 0, sizeof(context_counters_16x16));
    vpx_memset(hybrid_context_counters_16x16, 0,
               sizeof(hybrid_context_counters_16x16));
#if CONFIG_TX32X32
    vpx_memset(context_counters_32x32, 0, sizeof(context_counters_32x32));
#endif
  } else {
    fread(context_counters_4x4, sizeof(context_counters_4x4), 1, f);
    fread(hybrid_context_counters_4x4,
          sizeof(hybrid_context_counters_4x4), 1, f);
    fread(context_counters_8x8, sizeof(context_counters_8x8), 1, f);
    fread(hybrid_context_counters_8x8,
          sizeof(hybrid_context_counters_8x8), 1, f);
    fread(context_counters_16x16, sizeof(context_counters_16x16), 1, f);
    fread(hybrid_context_counters_16x16,
          sizeof(hybrid_context_counters_16x16), 1, f);
#if CONFIG_TX32X32
    fread(context_counters_32x32, sizeof(context_counters_32x32), 1, f);
#endif
    fclose(f);
  }

  f = fopen("treeupdate.bin", "rb");
  if (!f) {
    vpx_memset(tree_update_hist_4x4, 0, sizeof(tree_update_hist_4x4));
    vpx_memset(hybrid_tree_update_hist_4x4, 0,
               sizeof(hybrid_tree_update_hist_4x4));
    vpx_memset(tree_update_hist_8x8, 0, sizeof(tree_update_hist_8x8));
    vpx_memset(hybrid_tree_update_hist_8x8, 0,
               sizeof(hybrid_tree_update_hist_8x8));
    vpx_memset(tree_update_hist_16x16, 0, sizeof(tree_update_hist_16x16));
    vpx_memset(hybrid_tree_update_hist_16x16, 0,
               sizeof(hybrid_tree_update_hist_16x16));
#if CONFIG_TX32X32
    vpx_memset(tree_update_hist_32x32, 0, sizeof(tree_update_hist_32x32));
#endif
  } else {
    fread(tree_update_hist_4x4, sizeof(tree_update_hist_4x4), 1, f);
    fread(hybrid_tree_update_hist_4x4,
          sizeof(hybrid_tree_update_hist_4x4), 1, f);
    fread(tree_update_hist_8x8, sizeof(tree_update_hist_8x8), 1, f);
    fread(hybrid_tree_update_hist_8x8,
          sizeof(hybrid_tree_update_hist_8x8), 1, f);
    fread(tree_update_hist_16x16, sizeof(tree_update_hist_16x16), 1, f);
    fread(hybrid_tree_update_hist_16x16,
          sizeof(hybrid_tree_update_hist_16x16), 1, f);
#if CONFIG_TX32X32
    fread(tree_update_hist_32x32, sizeof(tree_update_hist_32x32), 1, f);
#endif
    fclose(f);
  }
}

static void print_counter(FILE *f, vp9_coeff_accum *context_counters,
                          int block_types, const char *header) {
  int type, band, pt, t;

  fprintf(f, "static const vp9_coeff_count %s = {\n", header);

# define Comma( X) (X? ",":"")
  type = 0;
  do {
    fprintf(f, "%s\n  { /* block Type %d */", Comma(type), type);
    band = 0;
    do {
      fprintf(f, "%s\n    { /* Coeff Band %d */", Comma(band), band);
      pt = 0;
      do {
        fprintf(f, "%s\n      {", Comma(pt));

        t = 0;
        do {
          const INT64 x = context_counters [type] [band] [pt] [t];
          const int y = (int) x;

          assert(x == (INT64) y);  /* no overflow handling yet */
          fprintf(f, "%s %d", Comma(t), y);
        } while (++t < MAX_ENTROPY_TOKENS);
        fprintf(f, "}");
      } while (++pt < PREV_COEF_CONTEXTS);
      fprintf(f, "\n    }");
    } while (++band < COEF_BANDS);
    fprintf(f, "\n  }");
  } while (++type < block_types);
  fprintf(f, "\n};\n");
}

static void print_probs(FILE *f, vp9_coeff_accum *context_counters,
                        int block_types, const char *header) {
  int type, band, pt, t;

  fprintf(f, "static const vp9_coeff_probs %s = {\n", header);

  type = 0;
  do {
    fprintf(f, "%s\n  { /* block Type %d */", Comma(type), type);
    band = 0;
    do {
      fprintf(f, "%s\n    { /* Coeff Band %d */", Comma(band), band);
      pt = 0;
      do {
        unsigned int branch_ct [ENTROPY_NODES] [2];
        unsigned int coef_counts[MAX_ENTROPY_TOKENS];
        vp9_prob coef_probs[ENTROPY_NODES];

        for (t = 0; t < MAX_ENTROPY_TOKENS; ++t)
          coef_counts[t] = context_counters[type][band][pt][t];
        vp9_tree_probs_from_distribution(MAX_ENTROPY_TOKENS,
                                         vp9_coef_encodings, vp9_coef_tree,
                                         coef_probs, branch_ct, coef_counts,
                                         256, 1);
        fprintf(f, "%s\n      {", Comma(pt));

        t = 0;
        do {
          fprintf(f, "%s %d", Comma(t), coef_probs[t]);
        } while (++t < ENTROPY_NODES);

        fprintf(f, "}");
      } while (++pt < PREV_COEF_CONTEXTS);
      fprintf(f, "\n    }");
    } while (++band < COEF_BANDS);
    fprintf(f, "\n  }");
  } while (++type < block_types);
  fprintf(f, "\n};\n");
}

void print_context_counters() {
  FILE *f = fopen("vp9_context.c", "w");

  fprintf(f, "#include \"vp9_entropy.h\"\n");
  fprintf(f, "\n/* *** GENERATED FILE: DO NOT EDIT *** */\n\n");

  /* print counts */
  print_counter(f, context_counters_4x4, BLOCK_TYPES_4X4,
                "vp9_default_coef_counts_4x4[BLOCK_TYPES_4X4]");
  print_counter(f, hybrid_context_counters_4x4, BLOCK_TYPES_4X4,
                "vp9_default_hybrid_coef_counts_4x4[BLOCK_TYPES_4X4]");
  print_counter(f, context_counters_8x8, BLOCK_TYPES_8X8,
                "vp9_default_coef_counts_8x8[BLOCK_TYPES_8X8]");
  print_counter(f, hybrid_context_counters_8x8, BLOCK_TYPES_8X8,
                "vp9_default_hybrid_coef_counts_8x8[BLOCK_TYPES_8X8]");
  print_counter(f, context_counters_16x16, BLOCK_TYPES_16X16,
                "vp9_default_coef_counts_16x16[BLOCK_TYPES_16X16]");
  print_counter(f, hybrid_context_counters_16x16, BLOCK_TYPES_16X16,
                "vp9_default_hybrid_coef_counts_16x16[BLOCK_TYPES_16X16]");
#if CONFIG_TX32X32
  print_counter(f, context_counters_32x32, BLOCK_TYPES_32X32,
                "vp9_default_coef_counts_32x32[BLOCK_TYPES_32X32]");
#endif

  /* print coefficient probabilities */
  print_probs(f, context_counters_4x4, BLOCK_TYPES_4X4,
              "vp9_default_coef_probs_4x4[BLOCK_TYPES_4X4]");
  print_probs(f, hybrid_context_counters_4x4, BLOCK_TYPES_4X4,
              "vp9_default_hybrid_coef_probs_4x4[BLOCK_TYPES_4X4]");
  print_probs(f, context_counters_8x8, BLOCK_TYPES_8X8,
              "vp9_default_coef_probs_8x8[BLOCK_TYPES_8X8]");
  print_probs(f, hybrid_context_counters_8x8, BLOCK_TYPES_8X8,
              "vp9_default_hybrid_coef_probs_8x8[BLOCK_TYPES_8X8]");
  print_probs(f, context_counters_16x16, BLOCK_TYPES_16X16,
              "vp9_default_coef_probs_16x16[BLOCK_TYPES_16X16]");
  print_probs(f, hybrid_context_counters_16x16, BLOCK_TYPES_16X16,
              "vp9_default_hybrid_coef_probs_16x16[BLOCK_TYPES_16X16]");
#if CONFIG_TX32X32
  print_probs(f, context_counters_32x32, BLOCK_TYPES_32X32,
              "vp9_default_coef_probs_32x32[BLOCK_TYPES_32X32]");
#endif

  fclose(f);

  f = fopen("context.bin", "wb");
  fwrite(context_counters_4x4, sizeof(context_counters_4x4), 1, f);
  fwrite(hybrid_context_counters_4x4,
         sizeof(hybrid_context_counters_4x4), 1, f);
  fwrite(context_counters_8x8, sizeof(context_counters_8x8), 1, f);
  fwrite(hybrid_context_counters_8x8,
         sizeof(hybrid_context_counters_8x8), 1, f);
  fwrite(context_counters_16x16, sizeof(context_counters_16x16), 1, f);
  fwrite(hybrid_context_counters_16x16,
         sizeof(hybrid_context_counters_16x16), 1, f);
#if CONFIG_TX32X32
  fwrite(context_counters_32x32, sizeof(context_counters_32x32), 1, f);
#endif
  fclose(f);
}
#endif

void vp9_tokenize_initialize() {
  fill_value_tokens();
}

static __inline void stuff_b(VP9_COMP *cpi,
                             MACROBLOCKD *xd,
                             const BLOCKD * const b,
                             TOKENEXTRA **tp,
                             PLANE_TYPE type,
                             ENTROPY_CONTEXT *a,
                             ENTROPY_CONTEXT *l,
                             TX_SIZE tx_size,
                             int dry_run) {
  const int *bands;
  vp9_coeff_count *counts;
  vp9_coeff_probs *probs;
  int pt, band;
  TOKENEXTRA *t = *tp;
  const TX_TYPE tx_type = (type == PLANE_TYPE_Y_WITH_DC) ?
                          get_tx_type(xd, b) : DCT_DCT;
  VP9_COMBINEENTROPYCONTEXTS(pt, *a, *l);

  switch (tx_size) {
    default:
    case TX_4X4:
      bands = vp9_coef_bands;
      if (tx_type != DCT_DCT) {
        counts = cpi->hybrid_coef_counts_4x4;
        probs = cpi->common.fc.hybrid_coef_probs_4x4;
      } else {
        counts = cpi->coef_counts_4x4;
        probs = cpi->common.fc.coef_probs_4x4;
      }
      break;
    case TX_8X8:
      bands = vp9_coef_bands_8x8;
      if (tx_type != DCT_DCT) {
        counts = cpi->hybrid_coef_counts_8x8;
        probs = cpi->common.fc.hybrid_coef_probs_8x8;
      } else {
        counts = cpi->coef_counts_8x8;
        probs = cpi->common.fc.coef_probs_8x8;
      }
      break;
    case TX_16X16:
      bands = vp9_coef_bands_16x16;
      if (tx_type != DCT_DCT) {
        counts = cpi->hybrid_coef_counts_16x16;
        probs = cpi->common.fc.hybrid_coef_probs_16x16;
      } else {
        counts = cpi->coef_counts_16x16;
        probs = cpi->common.fc.coef_probs_16x16;
      }
      break;
#if CONFIG_TX32X32 && CONFIG_SUPERBLOCKS
    case TX_32X32:
      bands = vp9_coef_bands_32x32;
      counts = cpi->coef_counts_32x32;
      probs = cpi->common.fc.coef_probs_32x32;
      break;
#endif
  }
  band = bands[(type == PLANE_TYPE_Y_NO_DC) ? 1 : 0];
  t->Token = DCT_EOB_TOKEN;
  t->context_tree = probs[type][band][pt];
  t->skip_eob_node = 0;
  ++t;
  *tp = t;
  *a = *l = 0;
  if (!dry_run) {
    ++counts[type][band][pt][DCT_EOB_TOKEN];
  }
}

static void stuff_mb_8x8(VP9_COMP *cpi, MACROBLOCKD *xd,
                         TOKENEXTRA **t, int dry_run) {
  ENTROPY_CONTEXT *A = (ENTROPY_CONTEXT *)xd->above_context;
  ENTROPY_CONTEXT *L = (ENTROPY_CONTEXT *)xd->left_context;
  PLANE_TYPE plane_type;
  int b;
  int has_2nd_order = get_2nd_order_usage(xd);

  if (has_2nd_order) {
    stuff_b(cpi, xd, xd->block + 24, t, PLANE_TYPE_Y2,
            A + vp9_block2above_8x8[24], L + vp9_block2left_8x8[24],
            TX_8X8, dry_run);
    plane_type = PLANE_TYPE_Y_NO_DC;
  } else {
    xd->above_context->y2 = 1;
    xd->left_context->y2 = 1;
    plane_type = PLANE_TYPE_Y_WITH_DC;
  }

  for (b = 0; b < 16; b += 4) {
    stuff_b(cpi, xd, xd->block + b, t, plane_type, A + vp9_block2above_8x8[b],
            L + vp9_block2left_8x8[b], TX_8X8, dry_run);
    A[vp9_block2above_8x8[b] + 1] = A[vp9_block2above_8x8[b]];
    L[vp9_block2left_8x8[b] + 1]  = L[vp9_block2left_8x8[b]];
  }

  for (b = 16; b < 24; b += 4) {
    stuff_b(cpi, xd, xd->block + b, t, PLANE_TYPE_UV,
            A + vp9_block2above_8x8[b], L + vp9_block2left_8x8[b],
            TX_8X8, dry_run);
    A[vp9_block2above_8x8[b] + 1] = A[vp9_block2above_8x8[b]];
    L[vp9_block2left_8x8[b] + 1]  = L[vp9_block2left_8x8[b]];
  }
}

static void stuff_mb_16x16(VP9_COMP *cpi, MACROBLOCKD *xd,
                           TOKENEXTRA **t, int dry_run) {
  ENTROPY_CONTEXT * A = (ENTROPY_CONTEXT *)xd->above_context;
  ENTROPY_CONTEXT * L = (ENTROPY_CONTEXT *)xd->left_context;
  int b;

  stuff_b(cpi, xd, xd->block, t, PLANE_TYPE_Y_WITH_DC, A, L, TX_16X16, dry_run);
  A[1] = A[2] = A[3] = A[0];
  L[1] = L[2] = L[3] = L[0];
  for (b = 16; b < 24; b += 4) {
    stuff_b(cpi, xd, xd->block + b, t, PLANE_TYPE_UV,
            A + vp9_block2above_8x8[b],
            L + vp9_block2above_8x8[b], TX_8X8, dry_run);
    A[vp9_block2above_8x8[b] + 1] = A[vp9_block2above_8x8[b]];
    L[vp9_block2left_8x8[b] + 1]  = L[vp9_block2left_8x8[b]];
  }
  vpx_memset(&A[8], 0, sizeof(A[8]));
  vpx_memset(&L[8], 0, sizeof(L[8]));
}

static void stuff_mb_4x4(VP9_COMP *cpi, MACROBLOCKD *xd,
                         TOKENEXTRA **t, int dry_run) {
  ENTROPY_CONTEXT *A = (ENTROPY_CONTEXT *)xd->above_context;
  ENTROPY_CONTEXT *L = (ENTROPY_CONTEXT *)xd->left_context;
  int b;
  PLANE_TYPE plane_type;
  int has_2nd_order = (xd->mode_info_context->mbmi.mode != B_PRED &&
                      xd->mode_info_context->mbmi.mode != I8X8_PRED &&
                      xd->mode_info_context->mbmi.mode != SPLITMV);
  if (has_2nd_order && get_tx_type(xd, &xd->block[0]) != DCT_DCT)
    has_2nd_order = 0;

  if (has_2nd_order) {
    stuff_b(cpi, xd, xd->block + 24, t, PLANE_TYPE_Y2, A + vp9_block2above[24],
            L + vp9_block2left[24], TX_4X4, dry_run);
    plane_type = PLANE_TYPE_Y_NO_DC;
  } else {
    xd->above_context->y2 = 1;
    xd->left_context->y2 = 1;
    plane_type = PLANE_TYPE_Y_WITH_DC;
  }

  for (b = 0; b < 16; b++)
    stuff_b(cpi, xd, xd->block + b, t, plane_type, A + vp9_block2above[b],
            L + vp9_block2left[b], TX_4X4, dry_run);

  for (b = 16; b < 24; b++)
    stuff_b(cpi, xd, xd->block + b, t, PLANE_TYPE_UV, A + vp9_block2above[b],
            L + vp9_block2left[b], TX_4X4, dry_run);
}

static void stuff_mb_8x8_4x4uv(VP9_COMP *cpi, MACROBLOCKD *xd,
                               TOKENEXTRA **t, int dry_run) {
  ENTROPY_CONTEXT *A = (ENTROPY_CONTEXT *)xd->above_context;
  ENTROPY_CONTEXT *L = (ENTROPY_CONTEXT *)xd->left_context;
  PLANE_TYPE plane_type;
  int b;

  int has_2nd_order = get_2nd_order_usage(xd);
  if (has_2nd_order) {
    stuff_b(cpi, xd, xd->block + 24, t, PLANE_TYPE_Y2,
            A + vp9_block2above_8x8[24], L + vp9_block2left_8x8[24],
            TX_8X8, dry_run);
    plane_type = PLANE_TYPE_Y_NO_DC;
  } else {
    plane_type = PLANE_TYPE_Y_WITH_DC;
  }

  for (b = 0; b < 16; b += 4) {
    stuff_b(cpi, xd, xd->block + b, t, plane_type,
            A + vp9_block2above_8x8[b], L + vp9_block2left_8x8[b],
            TX_8X8, dry_run);
    A[vp9_block2above_8x8[b] + 1] = A[vp9_block2above_8x8[b]];
    L[vp9_block2left_8x8[b] + 1]  = L[vp9_block2left_8x8[b]];
  }

  for (b = 16; b < 24; b++)
    stuff_b(cpi, xd, xd->block + b, t, PLANE_TYPE_UV, A + vp9_block2above[b],
            L + vp9_block2left[b], TX_4X4, dry_run);
  xd->above_context->y2 = 1;
  xd->left_context->y2 = 1;
}

void vp9_stuff_mb(VP9_COMP *cpi, MACROBLOCKD *xd, TOKENEXTRA **t, int dry_run) {
  TX_SIZE tx_size = xd->mode_info_context->mbmi.txfm_size;
  TOKENEXTRA * const t_backup = *t;

  if (tx_size == TX_16X16) {
    stuff_mb_16x16(cpi, xd, t, dry_run);
  } else if (tx_size == TX_8X8) {
    if (xd->mode_info_context->mbmi.mode == I8X8_PRED ||
        xd->mode_info_context->mbmi.mode == SPLITMV) {
      stuff_mb_8x8_4x4uv(cpi, xd, t, dry_run);
    } else {
      stuff_mb_8x8(cpi, xd, t, dry_run);
    }
  } else {
    stuff_mb_4x4(cpi, xd, t, dry_run);
  }

  if (dry_run) {
    *t = t_backup;
  }
}

#if CONFIG_TX32X32 && CONFIG_SUPERBLOCKS
static void stuff_sb_32x32(VP9_COMP *cpi, MACROBLOCKD *xd,
                               TOKENEXTRA **t, int dry_run) {
  ENTROPY_CONTEXT *A[2] = { (ENTROPY_CONTEXT *) (xd->above_context + 0),
                            (ENTROPY_CONTEXT *) (xd->above_context + 1), };
  ENTROPY_CONTEXT *L[2] = { (ENTROPY_CONTEXT *) (xd->left_context + 0),
                            (ENTROPY_CONTEXT *) (xd->left_context + 1), };
  int b;

  stuff_b(cpi, xd, xd->block, t, PLANE_TYPE_Y_WITH_DC,
          A[0], L[0], TX_32X32, dry_run);
  A[0][1] = A[0][2] = A[0][3] = A[0][0];
  L[0][1] = L[0][2] = L[0][3] = L[0][0];
  for (b = 16; b < 24; b += 4) {
    stuff_b(cpi, xd, xd->block + b, t, PLANE_TYPE_UV,
            A[0] + vp9_block2above_8x8[b],
            L[0] + vp9_block2above_8x8[b], TX_16X16, dry_run);
    A[0][vp9_block2above_8x8[b] + 1] = A[0][vp9_block2above_8x8[b]];
    L[0][vp9_block2left_8x8[b] + 1]  = L[0][vp9_block2left_8x8[b]];
  }
  vpx_memset(&A[0][8], 0, sizeof(A[0][8]));
  vpx_memset(&L[0][8], 0, sizeof(L[0][8]));
  vpx_memcpy(A[1], A[0], sizeof(ENTROPY_CONTEXT_PLANES));
  vpx_memcpy(L[1], L[0], sizeof(ENTROPY_CONTEXT_PLANES));
}

void vp9_stuff_sb(VP9_COMP *cpi, MACROBLOCKD *xd, TOKENEXTRA **t, int dry_run) {
  TOKENEXTRA * const t_backup = *t;

  stuff_sb_32x32(cpi, xd, t, dry_run);

  if (dry_run) {
    *t = t_backup;
  }
}
#endif

void vp9_fix_contexts(MACROBLOCKD *xd) {
  /* Clear entropy contexts for blocks */
  if ((xd->mode_info_context->mbmi.mode != B_PRED
       && xd->mode_info_context->mbmi.mode != I8X8_PRED
       && xd->mode_info_context->mbmi.mode != SPLITMV)
      || xd->mode_info_context->mbmi.txfm_size == TX_16X16
      ) {
    vpx_memset(xd->above_context, 0, sizeof(ENTROPY_CONTEXT_PLANES));
    vpx_memset(xd->left_context, 0, sizeof(ENTROPY_CONTEXT_PLANES));
  } else {
    vpx_memset(xd->above_context, 0, sizeof(ENTROPY_CONTEXT_PLANES) - 1);
    vpx_memset(xd->left_context, 0, sizeof(ENTROPY_CONTEXT_PLANES) - 1);
    xd->above_context->y2 = 1;
    xd->left_context->y2 = 1;
  }
}

#if CONFIG_TX32X32 && CONFIG_SUPERBLOCKS
void vp9_fix_contexts_sb(MACROBLOCKD *xd) {
  vpx_memset(xd->above_context, 0, sizeof(ENTROPY_CONTEXT_PLANES) * 2);
  vpx_memset(xd->left_context, 0, sizeof(ENTROPY_CONTEXT_PLANES) * 2);
}
#endif
