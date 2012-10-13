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
#include "onyx_int.h"
#include "tokenize.h"
#include "vpx_mem/vpx_mem.h"

#include "vp8/common/pred_common.h"
#include "vp8/common/seg_common.h"
#include "vp8/common/entropy.h"

/* Global event counters used for accumulating statistics across several
   compressions, then generating context.c = initial stats. */

#ifdef ENTROPY_STATS
INT64 context_counters[BLOCK_TYPES] [COEF_BANDS] [PREV_COEF_CONTEXTS] [MAX_ENTROPY_TOKENS];
#if CONFIG_HYBRIDTRANSFORM
INT64 hybrid_context_counters[BLOCK_TYPES] [COEF_BANDS] [PREV_COEF_CONTEXTS] [MAX_ENTROPY_TOKENS];
#endif

INT64 context_counters_8x8[BLOCK_TYPES_8X8] [COEF_BANDS] [PREV_COEF_CONTEXTS] [MAX_ENTROPY_TOKENS];
#if CONFIG_HYBRIDTRANSFORM8X8
INT64 hybrid_context_counters_8x8[BLOCK_TYPES_8X8] [COEF_BANDS] [PREV_COEF_CONTEXTS] [MAX_ENTROPY_TOKENS];
#endif

INT64 context_counters_16x16[BLOCK_TYPES_16X16] [COEF_BANDS] [PREV_COEF_CONTEXTS] [MAX_ENTROPY_TOKENS];
#if CONFIG_HYBRIDTRANSFORM16X16
INT64 hybrid_context_counters_16x16[BLOCK_TYPES_16X16] [COEF_BANDS] [PREV_COEF_CONTEXTS] [MAX_ENTROPY_TOKENS];
#endif

extern unsigned int tree_update_hist[BLOCK_TYPES][COEF_BANDS]
                    [PREV_COEF_CONTEXTS][ENTROPY_NODES][2];
#if CONFIG_HYBRIDTRANSFORM
extern unsigned int hybrid_tree_update_hist[BLOCK_TYPES][COEF_BANDS]
                    [PREV_COEF_CONTEXTS][ENTROPY_NODES][2];
#endif
extern unsigned int tree_update_hist_8x8[BLOCK_TYPES_8X8][COEF_BANDS]
                    [PREV_COEF_CONTEXTS][ENTROPY_NODES] [2];
#if CONFIG_HYBRIDTRANSFORM8X8
extern unsigned int hybrid_tree_update_hist_8x8[BLOCK_TYPES_8X8][COEF_BANDS]
                    [PREV_COEF_CONTEXTS][ENTROPY_NODES] [2];
#endif
extern unsigned int tree_update_hist_16x16[BLOCK_TYPES_16X16][COEF_BANDS]
                    [PREV_COEF_CONTEXTS][ENTROPY_NODES] [2];
#if CONFIG_HYBRIDTRANSFORM16X16
extern unsigned int hybrid_tree_update_hist_16x16[BLOCK_TYPES_16X16][COEF_BANDS]
                    [PREV_COEF_CONTEXTS][ENTROPY_NODES] [2];
#endif
#endif  /* ENTROPY_STATS */

void vp8_stuff_mb_4x4(VP8_COMP *cpi,
                      MACROBLOCKD *xd, TOKENEXTRA **t, int dry_run);
void vp8_stuff_mb_8x8(VP8_COMP *cpi,
                      MACROBLOCKD *xd, TOKENEXTRA **t, int dry_run);
void vp8_stuff_mb_8x8_4x4uv(VP8_COMP *cpi,
                            MACROBLOCKD *xd, TOKENEXTRA **t, int dry_run);
void vp8_stuff_mb_16x16(VP8_COMP *cpi, MACROBLOCKD *xd,
                        TOKENEXTRA **t, int dry_run);
void vp8_fix_contexts(MACROBLOCKD *xd);

static TOKENVALUE dct_value_tokens[DCT_MAX_VALUE * 2];
const TOKENVALUE *vp8_dct_value_tokens_ptr;
static int dct_value_cost[DCT_MAX_VALUE * 2];
const int *vp8_dct_value_cost_ptr;

static void fill_value_tokens() {

  TOKENVALUE *const t = dct_value_tokens + DCT_MAX_VALUE;
  vp8_extra_bit_struct *const e = vp8_extra_bits;

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
      vp8_extra_bit_struct *p = vp8_extra_bits + t[i].Token;

      if (p->base_val) {
        const int extra = t[i].Extra;
        const int Length = p->Len;

        if (Length)
          cost += vp8_treed_cost(p->tree, p->prob, extra >> 1, Length);

        cost += vp8_cost_bit(vp8_prob_half, extra & 1); /* sign */
        dct_value_cost[i + DCT_MAX_VALUE] = cost;
      }

    }

  } while (++i < DCT_MAX_VALUE);

  vp8_dct_value_tokens_ptr = dct_value_tokens + DCT_MAX_VALUE;
  vp8_dct_value_cost_ptr   = dct_value_cost + DCT_MAX_VALUE;
}

static void tokenize1st_order_b_16x16(MACROBLOCKD *xd,
                                      const BLOCKD *const b,
                                      TOKENEXTRA **tp,
                                      const int type,
                                      const FRAME_TYPE frametype,
                                      ENTROPY_CONTEXT *a,
                                      ENTROPY_CONTEXT *l,
                                      VP8_COMP *cpi,
                                      int dry_run) {
  int pt; /* near block/prev token context index */
  int c = 0;                  /* start at DC unless type 0 */
  const int eob = b->eob;     /* one beyond last nonzero coeff */
  TOKENEXTRA *t = *tp;        /* store tokens starting here */
  int x;
  const short *qcoeff_ptr = b->qcoeff;
#if CONFIG_HYBRIDTRANSFORM16X16
  TX_TYPE tx_type = get_tx_type(xd, b);
#endif

  int seg_eob = 256;
  int segment_id = xd->mode_info_context->mbmi.segment_id;

  //if (!dry_run) printf("16: %d\n", tx_type);

  if (segfeature_active(xd, segment_id, SEG_LVL_EOB))
    seg_eob = get_segdata(xd, segment_id, SEG_LVL_EOB);

  VP8_COMBINEENTROPYCONTEXTS(pt, *a, *l);

  do {
    const int band = vp8_coef_bands_16x16[c];
    int v;

    x = DCT_EOB_TOKEN;
    if (c < eob) {
      int rc = vp8_default_zig_zag1d_16x16[c];
      v = qcoeff_ptr[rc];

      assert(-DCT_MAX_VALUE <= v  &&  v < (DCT_MAX_VALUE));

      t->Extra = vp8_dct_value_tokens_ptr[v].Extra;
      x        = vp8_dct_value_tokens_ptr[v].Token;
    }

    t->Token = x;
#if CONFIG_HYBRIDTRANSFORM16X16
    if (tx_type != DCT_DCT)
      t->context_tree = cpi->common.fc.hybrid_coef_probs_16x16[type][band][pt];
    else
#endif
      t->context_tree = cpi->common.fc.coef_probs_16x16[type][band][pt];

    t->skip_eob_node = pt == 0 && ((band > 0 && type > 0) || (band > 1 && type == 0));
    if (!dry_run) {
#if CONFIG_HYBRIDTRANSFORM16X16
      if (tx_type != DCT_DCT)
        ++cpi->hybrid_coef_counts_16x16[type][band][pt][x];
      else
#endif
        ++cpi->coef_counts_16x16[type][band][pt][x];
    }
  } while (pt = vp8_prev_token_class[x], ++t, c < eob  &&  ++c < seg_eob);

  *tp = t;
  pt = (c != !type); /* 0 <-> all coeff data is zero */
  *a = *l = pt;
}

static void tokenize2nd_order_b_8x8
(
  MACROBLOCKD *xd,
  const BLOCKD *const b,
  TOKENEXTRA **tp,
  const int type,     /* which plane: 0=Y no DC, 1=Y2, 2=UV, 3=Y with DC */
  const FRAME_TYPE frametype,
  ENTROPY_CONTEXT *a,
  ENTROPY_CONTEXT *l,
  VP8_COMP *cpi,
  int dry_run) {
  int pt; /* near block/prev token context index */
  int c = 0;          /* start at DC */
  const int eob = b->eob;     /* one beyond last nonzero coeff */
  TOKENEXTRA *t = *tp;        /* store tokens starting here */
  int x;
  const short *qcoeff_ptr = b->qcoeff;

  int seg_eob = 4;
  int segment_id = xd->mode_info_context->mbmi.segment_id;

  if (segfeature_active(xd, segment_id, SEG_LVL_EOB)) {
    seg_eob = get_segdata(xd, segment_id, SEG_LVL_EOB);
  }

  VP8_COMBINEENTROPYCONTEXTS(pt, *a, *l);

  assert(eob <= 4);

  do {
    const int band = vp8_coef_bands[c];
    int v = 0;

    if (c < eob) {
      int rc = vp8_default_zig_zag1d[c];
      v = qcoeff_ptr[rc];

      assert(-DCT_MAX_VALUE <= v  &&  v < (DCT_MAX_VALUE));

      t->Extra = vp8_dct_value_tokens_ptr[v].Extra;
      x        = vp8_dct_value_tokens_ptr[v].Token;
    } else
      x = DCT_EOB_TOKEN;

    t->Token = x;
    // printf("Token : %d\n", x);
    t->context_tree = cpi->common.fc.coef_probs_8x8 [type] [band] [pt];

    t->skip_eob_node = pt == 0 && ((band > 0 && type > 0) || (band > 1 && type == 0));
    assert(vp8_coef_encodings[t->Token].Len - t->skip_eob_node > 0);

    if (!dry_run)
      ++cpi->coef_counts_8x8       [type] [band] [pt] [x];
  } while (pt = vp8_prev_token_class[x], ++t, c < eob  &&  ++c < seg_eob);


  *tp = t;
  pt = (c != !type); /* 0 <-> all coeff data is zero */
  *a = *l = pt;

}

static void tokenize2nd_order_b_4x4(MACROBLOCKD *xd,
                                    TOKENEXTRA **tp,
                                    VP8_COMP *cpi,
                                    int dry_run) {
  int pt;             /* near block/prev token context index */
  int c;              /* start at DC */
  TOKENEXTRA *t = *tp;/* store tokens starting here */
  const BLOCKD *b;
  const short *qcoeff_ptr;
  ENTROPY_CONTEXT *a;
  ENTROPY_CONTEXT *l;
  int band, rc, v, token;

  int seg_eob = 16;
  int segment_id = xd->mode_info_context->mbmi.segment_id;

  if (segfeature_active(xd, segment_id, SEG_LVL_EOB))
    seg_eob = get_segdata(xd, segment_id, SEG_LVL_EOB);

  b = xd->block + 24;
  qcoeff_ptr = b->qcoeff;
  a = (ENTROPY_CONTEXT *)xd->above_context + 8;
  l = (ENTROPY_CONTEXT *)xd->left_context + 8;

  VP8_COMBINEENTROPYCONTEXTS(pt, *a, *l);

  for (c = 0; c < b->eob; c++) {
    rc = vp8_default_zig_zag1d[c];
    band = vp8_coef_bands[c];
    v = qcoeff_ptr[rc];

    t->Extra = vp8_dct_value_tokens_ptr[v].Extra;
    token    = vp8_dct_value_tokens_ptr[v].Token;

    t->Token = token;
    t->context_tree = cpi->common.fc.coef_probs [1] [band] [pt];

    t->skip_eob_node = ((pt == 0) && (band > 0));
    assert(vp8_coef_encodings[t->Token].Len - t->skip_eob_node > 0);

    if (!dry_run)
      ++cpi->coef_counts       [1] [band] [pt] [token];

    pt = vp8_prev_token_class[token];
    t++;
  }

  if (c < seg_eob) {
    band = vp8_coef_bands[c];
    t->Token = DCT_EOB_TOKEN;
    t->context_tree = cpi->common.fc.coef_probs [1] [band] [pt];

    t->skip_eob_node = ((pt == 0) && (band > 0));
    assert(vp8_coef_encodings[t->Token].Len - t->skip_eob_node > 0);

    if (!dry_run)
      ++cpi->coef_counts       [1] [band] [pt] [DCT_EOB_TOKEN];

    t++;
  }


  *tp = t;
  pt = (c != 0); /* 0 <-> all coeff data is zero */
  *a = *l = pt;

}

static void tokenize1st_order_b_8x8
(
  MACROBLOCKD *xd,
  const BLOCKD *const b,
  TOKENEXTRA **tp,
  const int type,     /* which plane: 0=Y no DC, 1=Y2, 2=UV, 3=Y with DC */
  const FRAME_TYPE frametype,
  ENTROPY_CONTEXT *a,
  ENTROPY_CONTEXT *l,
  VP8_COMP *cpi,
  int dry_run) {
  int pt; /* near block/prev token context index */
  int c = type ? 0 : 1;       /* start at DC unless type 0 */
  TOKENEXTRA *t = *tp;        /* store tokens starting here */
  const short *qcoeff_ptr = b->qcoeff;
#if CONFIG_HYBRIDTRANSFORM8X8
  TX_TYPE tx_type = xd->mode_info_context->mbmi.mode == I8X8_PRED ?
      get_tx_type(xd, b) : DCT_DCT;
#endif

  int seg_eob = 64;
  int segment_id = xd->mode_info_context->mbmi.segment_id;

  if (segfeature_active(xd, segment_id, SEG_LVL_EOB))
    seg_eob = get_segdata(xd, segment_id, SEG_LVL_EOB);

  VP8_COMBINEENTROPYCONTEXTS(pt, *a, *l);

  //if (!dry_run) printf("8: %d\n", tx_type);
  for (; c < b->eob; ++c) {
    const int band = vp8_coef_bands_8x8[c];
    int rc = vp8_default_zig_zag1d_8x8[c];
    int v = qcoeff_ptr[rc], x;

    assert(-DCT_MAX_VALUE <= v  &&  v < (DCT_MAX_VALUE));

    t->Extra = vp8_dct_value_tokens_ptr[v].Extra;
    x        = vp8_dct_value_tokens_ptr[v].Token;

    t->Token = x;
#if CONFIG_HYBRIDTRANSFORM8X8
    if (tx_type != DCT_DCT)
      t->context_tree = cpi->common.fc.hybrid_coef_probs_8x8[type][band][pt];
    else
#endif
      t->context_tree = cpi->common.fc.coef_probs_8x8[type][band][pt];

    t->skip_eob_node = pt == 0 && ((band > 0 && type > 0) || (band > 1 && type == 0));
    assert(vp8_coef_encodings[t->Token].Len - t->skip_eob_node > 0);

    if (!dry_run) {
#if CONFIG_HYBRIDTRANSFORM8X8
      if (tx_type != DCT_DCT)
        ++cpi->hybrid_coef_counts_8x8[type][band][pt][x];
      else
#endif
        ++cpi->coef_counts_8x8[type][band][pt][x];
    }

    pt = vp8_prev_token_class[x];
    ++t;
  }

  if (c < seg_eob) {
    const int band = vp8_coef_bands_8x8[c];
    t->Token = DCT_EOB_TOKEN;

#if CONFIG_HYBRIDTRANSFORM8X8
    if (tx_type != DCT_DCT)
      t->context_tree = cpi->common.fc.hybrid_coef_probs_8x8 [type] [band] [pt];
    else
#endif
      t->context_tree = cpi->common.fc.coef_probs_8x8 [type] [band] [pt];

    t->skip_eob_node = pt == 0 && ((band > 0 && type > 0) || (band > 1 && type == 0));
    if (vp8_coef_encodings[t->Token].Len - t->skip_eob_node <= 0) {
      printf("type %d, seg-eob %d, eob %d, pt %d, c %d band %d\n", type, seg_eob, b->eob, pt, c, band);
      fflush(stdout);
    }
    assert(vp8_coef_encodings[t->Token].Len - t->skip_eob_node > 0);

    if (!dry_run) {
#if CONFIG_HYBRIDTRANSFORM8X8
      if (tx_type != DCT_DCT)
        ++cpi->hybrid_coef_counts_8x8[type][band][pt][DCT_EOB_TOKEN];
      else
#endif
        ++cpi->coef_counts_8x8[type][band][pt][DCT_EOB_TOKEN];
    }
    ++t;
  }

  *tp = t;
  pt = (c != !type); /* 0 <-> all coeff data is zero */
  *a = *l = pt;
}

#if CONFIG_HYBRIDTRANSFORM
static void tokenize1st_order_ht_4x4(MACROBLOCKD *xd,
                                     TOKENEXTRA **tp,
                                     int type,
                                     VP8_COMP *cpi,
                                     int dry_run) {
  unsigned int block;
  const BLOCKD *b;
  int pt;             /* near block/prev token context index */
  int c;
  int token;
  TOKENEXTRA *t = *tp;/* store tokens starting here */
  const short *qcoeff_ptr;
  ENTROPY_CONTEXT * a;
  ENTROPY_CONTEXT * l;
  int band, rc, v;
  int tmp1, tmp2;

  int const *pt_scan ;

  int seg_eob = 16;
  int segment_id = xd->mode_info_context->mbmi.segment_id;

  if ( segfeature_active( xd, segment_id, SEG_LVL_EOB ) ) {
    seg_eob = get_segdata( xd, segment_id, SEG_LVL_EOB );
  }

  b = xd->block;

  /* Luma */
  for (block = 0; block < 16; block++, b++) {
    B_PREDICTION_MODE b_mode;
    TX_TYPE tx_type = DCT_DCT;

    if( xd->mode_info_context->mbmi.mode == B_PRED ) {
      b_mode = b->bmi.as_mode.first;
      tx_type = get_tx_type(xd, b);
    }

    // assign scanning order for luma components coded in intra4x4 mode
    if( (xd->mode_info_context->mbmi.mode == B_PRED) &&
        (type == PLANE_TYPE_Y_WITH_DC) ) {
      switch(b_mode) {
        case B_VE_PRED :
        case B_VR_PRED :
          pt_scan = vp8_row_scan;
          break;

        case B_HE_PRED :
        case B_HD_PRED :
        case B_HU_PRED :
          pt_scan = vp8_col_scan;
          break;

        default :
          pt_scan = vp8_default_zig_zag1d;
          break;
      }
    } else {
      pt_scan = vp8_default_zig_zag1d;
    }

    tmp1 = vp8_block2above[block];
    tmp2 = vp8_block2left[block];
    qcoeff_ptr = b->qcoeff;
    a = (ENTROPY_CONTEXT *)xd->above_context + tmp1;
    l = (ENTROPY_CONTEXT *)xd->left_context + tmp2;
    VP8_COMBINEENTROPYCONTEXTS(pt, *a, *l);

    c = type ? 0 : 1;

    for (; c < b->eob; c++) {
      rc = pt_scan[c];
      band = vp8_coef_bands[c];
      v = qcoeff_ptr[rc];

      t->Extra = vp8_dct_value_tokens_ptr[v].Extra;
      token    = vp8_dct_value_tokens_ptr[v].Token;

      t->Token = token;
      if (tx_type != DCT_DCT)
        t->context_tree = cpi->common.fc.hybrid_coef_probs [type] [band] [pt];
      else
        t->context_tree = cpi->common.fc.coef_probs [type] [band] [pt];

      t->skip_eob_node = pt == 0 &&
          ((band > 0 && type > 0) || (band > 1 && type == 0));
      assert(vp8_coef_encodings[t->Token].Len - t->skip_eob_node > 0);

      if (!dry_run) {
        if (tx_type != DCT_DCT)
          ++cpi->hybrid_coef_counts[type] [band] [pt] [token];
        else
          ++cpi->coef_counts       [type] [band] [pt] [token];
      }

      pt = vp8_prev_token_class[token];
      t++;
    }

    if (c < seg_eob) {
      band = vp8_coef_bands[c];
      t->Token = DCT_EOB_TOKEN;
      if (tx_type != DCT_DCT)
        t->context_tree = cpi->common.fc.hybrid_coef_probs [type] [band] [pt];
      else
        t->context_tree = cpi->common.fc.coef_probs [type] [band] [pt];

      t->skip_eob_node = pt == 0 &&
          ((band > 0 && type > 0) || (band > 1 && type == 0));
      assert(vp8_coef_encodings[t->Token].Len - t->skip_eob_node > 0);
      if (!dry_run) {
        if (tx_type != DCT_DCT)
          ++cpi->hybrid_coef_counts[type] [band] [pt] [DCT_EOB_TOKEN];
        else
          ++cpi->coef_counts       [type] [band] [pt] [DCT_EOB_TOKEN];
      }

      t++;
    }

    *tp = t;
    pt = (c != !type); /* 0 <-> all coeff data is zero */
    *a = *l = pt;
  }

  // reset scanning order for chroma components
  pt_scan = vp8_default_zig_zag1d ;

  /* Chroma */
  for (block = 16; block < 24; block++, b++) {
    tmp1 = vp8_block2above[block];
    tmp2 = vp8_block2left[block];
    qcoeff_ptr = b->qcoeff;
    a = (ENTROPY_CONTEXT *)xd->above_context + tmp1;
    l = (ENTROPY_CONTEXT *)xd->left_context + tmp2;

    VP8_COMBINEENTROPYCONTEXTS(pt, *a, *l);

    for (c = 0; c < b->eob; c++) {
      rc = pt_scan[c];
      band = vp8_coef_bands[c];
      v = qcoeff_ptr[rc];

      t->Extra = vp8_dct_value_tokens_ptr[v].Extra;
      token    = vp8_dct_value_tokens_ptr[v].Token;

      t->Token = token;
      t->context_tree = cpi->common.fc.coef_probs [2] [band] [pt];

      t->skip_eob_node = ((pt == 0) && (band > 0));
      assert(vp8_coef_encodings[t->Token].Len - t->skip_eob_node > 0);

      if (!dry_run)
        ++cpi->coef_counts       [2] [band] [pt] [token];

      pt = vp8_prev_token_class[token];
      t++;
  }

    if (c < seg_eob) {
      band = vp8_coef_bands[c];
      t->Token = DCT_EOB_TOKEN;
      t->context_tree = cpi->common.fc.coef_probs [2] [band] [pt];

      t->skip_eob_node = ((pt == 0) && (band > 0));
      assert(vp8_coef_encodings[t->Token].Len - t->skip_eob_node > 0);
      if (!dry_run)
        ++cpi->coef_counts       [2] [band] [pt] [DCT_EOB_TOKEN];
      t++;
    }

    *tp = t;
    pt = (c != 0); /* 0 <-> all coeff data is zero */
    *a = *l = pt;
  }
}
#endif


static void tokenize1st_order_chroma_4x4
(
  MACROBLOCKD *xd,
  TOKENEXTRA **tp,
  int type,           /* which plane: 0=Y no DC, 1=Y2, 2=UV, 3=Y with DC */
  VP8_COMP *cpi,
  int dry_run) {
  unsigned int block;
  const BLOCKD *b;
  int pt;             /* near block/prev token context index */
  int c;
  int token;
  TOKENEXTRA *t = *tp;/* store tokens starting here */
  const short *qcoeff_ptr;
  ENTROPY_CONTEXT *a;
  ENTROPY_CONTEXT *l;
  int band, rc, v;
  int tmp1, tmp2;

  int seg_eob = 16;
  int segment_id = xd->mode_info_context->mbmi.segment_id;

  if (segfeature_active(xd, segment_id, SEG_LVL_EOB)) {
    seg_eob = get_segdata(xd, segment_id, SEG_LVL_EOB);
  }

  b = xd->block;
  b += 16;

  /* Chroma */
  for (block = 16; block < 24; block++, b++) {
    tmp1 = vp8_block2above[block];
    tmp2 = vp8_block2left[block];
    qcoeff_ptr = b->qcoeff;
    a = (ENTROPY_CONTEXT *)xd->above_context + tmp1;
    l = (ENTROPY_CONTEXT *)xd->left_context + tmp2;

    VP8_COMBINEENTROPYCONTEXTS(pt, *a, *l);

    for (c = 0; c < b->eob; c++) {
      rc = vp8_default_zig_zag1d[c];
      band = vp8_coef_bands[c];
      v = qcoeff_ptr[rc];

      t->Extra = vp8_dct_value_tokens_ptr[v].Extra;
      token    = vp8_dct_value_tokens_ptr[v].Token;

      t->Token = token;
      t->context_tree = cpi->common.fc.coef_probs [2] [band] [pt];

      t->skip_eob_node = ((pt == 0) && (band > 0));
      assert(vp8_coef_encodings[t->Token].Len - t->skip_eob_node > 0);

      if (!dry_run)
        ++cpi->coef_counts       [2] [band] [pt] [token];

      pt = vp8_prev_token_class[token];
      t++;
    }

    if (c < seg_eob) {
      band = vp8_coef_bands[c];
      t->Token = DCT_EOB_TOKEN;
      t->context_tree = cpi->common.fc.coef_probs [2] [band] [pt];

      t->skip_eob_node = ((pt == 0) && (band > 0));
      assert(vp8_coef_encodings[t->Token].Len - t->skip_eob_node > 0);

      if (!dry_run)
        ++cpi->coef_counts       [2] [band] [pt] [DCT_EOB_TOKEN];

      t++;
    }
    *tp = t;
    pt = (c != 0); /* 0 <-> all coeff data is zero */
    *a = *l = pt;
  }
}

static void tokenize1st_order_b_4x4
(
  MACROBLOCKD *xd,
  TOKENEXTRA **tp,
  int type,           /* which plane: 0=Y no DC, 1=Y2, 2=UV, 3=Y with DC */
  VP8_COMP *cpi,
  int dry_run) {
  unsigned int block;
  const BLOCKD *b;
  int pt;             /* near block/prev token context index */
  int band, rc, v, c, token;
  TOKENEXTRA *t = *tp;/* store tokens starting here */
  const short *qcoeff_ptr;
  ENTROPY_CONTEXT *a, *l;

  int seg_eob = 16;
  int segment_id = xd->mode_info_context->mbmi.segment_id;

  if (segfeature_active(xd, segment_id, SEG_LVL_EOB)) {
    seg_eob = get_segdata(xd, segment_id, SEG_LVL_EOB);
  }

  b = xd->block;
  /* Luma */
  for (block = 0; block < 16; block++, b++) {
    qcoeff_ptr = b->qcoeff;
    a = (ENTROPY_CONTEXT *)xd->above_context + vp8_block2above[block];
    l = (ENTROPY_CONTEXT *)xd->left_context + vp8_block2left[block];
    VP8_COMBINEENTROPYCONTEXTS(pt, *a, *l);

    c = type ? 0 : 1;

    assert(b->eob <= 16);

    for (; c < b->eob; c++) {
      rc = vp8_default_zig_zag1d[c];
      band = vp8_coef_bands[c];
      v = qcoeff_ptr[rc];

      t->Extra = vp8_dct_value_tokens_ptr[v].Extra;
      token    = vp8_dct_value_tokens_ptr[v].Token;

      t->Token = token;
      t->context_tree = cpi->common.fc.coef_probs [type] [band] [pt];

      t->skip_eob_node = pt == 0 &&
                         ((band > 0 && type > 0) || (band > 1 && type == 0));
      assert(vp8_coef_encodings[t->Token].Len - t->skip_eob_node > 0);
      if (!dry_run)
        ++cpi->coef_counts       [type] [band] [pt] [token];

      pt = vp8_prev_token_class[token];
      t++;
    }

    if (c < seg_eob) {
      band = vp8_coef_bands[c];
      t->Token = DCT_EOB_TOKEN;
      t->context_tree = cpi->common.fc.coef_probs [type] [band] [pt];

      t->skip_eob_node = pt == 0 &&
                         ((band > 0 && type > 0) || (band > 1 && type == 0));
      assert(vp8_coef_encodings[t->Token].Len - t->skip_eob_node > 0);
      if (!dry_run)
        ++cpi->coef_counts       [type] [band] [pt] [DCT_EOB_TOKEN];

      t++;
    }
    *tp = t;
    pt = (c != !type); /* 0 <-> all coeff data is zero */
    *a = *l = pt;

  }
  /* Chroma */
  for (block = 16; block < 24; block++, b++) {
    qcoeff_ptr = b->qcoeff;
    a = (ENTROPY_CONTEXT *)xd->above_context + vp8_block2above[block];
    l = (ENTROPY_CONTEXT *)xd->left_context + vp8_block2left[block];

    VP8_COMBINEENTROPYCONTEXTS(pt, *a, *l);

    assert(b->eob <= 16);
    for (c = 0; c < b->eob; c++) {
      rc = vp8_default_zig_zag1d[c];
      band = vp8_coef_bands[c];
      v = qcoeff_ptr[rc];

      t->Extra = vp8_dct_value_tokens_ptr[v].Extra;
      token    = vp8_dct_value_tokens_ptr[v].Token;

      t->Token = token;
      t->context_tree = cpi->common.fc.coef_probs [2] [band] [pt];

      t->skip_eob_node = ((pt == 0) && (band > 0));
      assert(vp8_coef_encodings[t->Token].Len - t->skip_eob_node > 0);

      if (!dry_run)
        ++cpi->coef_counts       [2] [band] [pt] [token];

      pt = vp8_prev_token_class[token];
      t++;
    }

    if (c < seg_eob) {
      band = vp8_coef_bands[c];
      t->Token = DCT_EOB_TOKEN;
      t->context_tree = cpi->common.fc.coef_probs [2] [band] [pt];

      t->skip_eob_node = ((pt == 0) && (band > 0));
      assert(vp8_coef_encodings[t->Token].Len - t->skip_eob_node > 0);

      if (!dry_run)
        ++cpi->coef_counts       [2] [band] [pt] [DCT_EOB_TOKEN];

      t++;
    }
    *tp = t;
    pt = (c != 0); /* 0 <-> all coeff data is zero */
    *a = *l = pt;
  }
}


int mby_is_skippable_4x4(MACROBLOCKD *xd, int has_y2_block) {
  int skip = 1;
  int i = 0;

  if (has_y2_block) {
    for (i = 0; i < 16; i++)
      skip &= (xd->block[i].eob < 2);
    skip &= (!xd->block[24].eob);
  } else {
    for (i = 0; i < 16; i++)
      skip &= (!xd->block[i].eob);
  }
  return skip;
}

int mbuv_is_skippable_4x4(MACROBLOCKD *xd) {
  int skip = 1;
  int i;

  for (i = 16; i < 24; i++)
    skip &= (!xd->block[i].eob);
  return skip;
}

int mb_is_skippable_4x4(MACROBLOCKD *xd, int has_y2_block) {
  return (mby_is_skippable_4x4(xd, has_y2_block) &
          mbuv_is_skippable_4x4(xd));
}

int mby_is_skippable_8x8(MACROBLOCKD *xd, int has_y2_block) {
  int skip = 1;
  int i = 0;

  if (has_y2_block) {
    for (i = 0; i < 16; i += 4)
      skip &= (xd->block[i].eob < 2);
    skip &= (!xd->block[24].eob);
  } else {
    for (i = 0; i < 16; i += 4)
      skip &= (!xd->block[i].eob);
  }
  return skip;
}

int mbuv_is_skippable_8x8(MACROBLOCKD *xd) {
  return (!xd->block[16].eob) & (!xd->block[20].eob);
}

int mb_is_skippable_8x8(MACROBLOCKD *xd, int has_y2_block) {
  return (mby_is_skippable_8x8(xd, has_y2_block) &
          mbuv_is_skippable_8x8(xd));
}

int mb_is_skippable_8x8_4x4uv(MACROBLOCKD *xd, int has_y2_block) {
  return (mby_is_skippable_8x8(xd, has_y2_block) &
          mbuv_is_skippable_4x4(xd));
}

int mby_is_skippable_16x16(MACROBLOCKD *xd) {
  int skip = 1;
  //skip &= (xd->block[0].eob < 2); // I think this should be commented? No second order == DC must be coded
  //skip &= (xd->block[0].eob < 1);
  //skip &= (!xd->block[24].eob);
  skip &= !xd->block[0].eob;
  return skip;
}

int mb_is_skippable_16x16(MACROBLOCKD *xd) {
  return (mby_is_skippable_16x16(xd) & mbuv_is_skippable_8x8(xd));
}

void vp8_tokenize_mb(VP8_COMP *cpi,
                     MACROBLOCKD *xd,
                     TOKENEXTRA **t,
                     int dry_run) {
  int plane_type;
  int has_y2_block;
  int b;
  int tx_size = xd->mode_info_context->mbmi.txfm_size;
  int mb_skip_context = get_pred_context(&cpi->common, xd, PRED_MBSKIP);
  TOKENEXTRA *t_backup = *t;

  // If the MB is going to be skipped because of a segment level flag
  // exclude this from the skip count stats used to calculate the
  // transmitted skip probability;
  int skip_inc;
  int segment_id = xd->mode_info_context->mbmi.segment_id;

#if CONFIG_HYBRIDTRANSFORM
    int QIndex = cpi->mb.q_index;
    int active_ht = (QIndex < ACTIVE_HT) &&
                    (xd->mode_info_context->mbmi.mode == B_PRED);
#endif

  if (!segfeature_active(xd, segment_id, SEG_LVL_EOB) ||
      (get_segdata(xd, segment_id, SEG_LVL_EOB) != 0)) {
    skip_inc = 1;
  } else
    skip_inc = 0;

  has_y2_block = (xd->mode_info_context->mbmi.mode != B_PRED
                  && xd->mode_info_context->mbmi.mode != I8X8_PRED
                  && xd->mode_info_context->mbmi.mode != SPLITMV);
  if (tx_size == TX_16X16) has_y2_block = 0; // Because of inter frames

  switch (tx_size) {
    case TX_16X16:
      xd->mode_info_context->mbmi.mb_skip_coeff = mb_is_skippable_16x16(xd);
      break;
    case TX_8X8:
      if (xd->mode_info_context->mbmi.mode == I8X8_PRED)
        xd->mode_info_context->mbmi.mb_skip_coeff = mb_is_skippable_8x8_4x4uv(xd, 0);
      else
        xd->mode_info_context->mbmi.mb_skip_coeff = mb_is_skippable_8x8(xd, has_y2_block);
      break;

    default:
      xd->mode_info_context->mbmi.mb_skip_coeff = mb_is_skippable_4x4(xd, has_y2_block);
      break;
  }

  if (xd->mode_info_context->mbmi.mb_skip_coeff) {
    if (!dry_run)
      cpi->skip_true_count[mb_skip_context] += skip_inc;
    if (!cpi->common.mb_no_coeff_skip) {
      if (tx_size == TX_16X16)
        vp8_stuff_mb_16x16(cpi, xd, t, dry_run);
      else
      if (tx_size == TX_8X8) {
        if (xd->mode_info_context->mbmi.mode == I8X8_PRED)
          vp8_stuff_mb_8x8_4x4uv(cpi, xd, t, dry_run);
        else
          vp8_stuff_mb_8x8(cpi, xd, t, dry_run);
      } else
        vp8_stuff_mb_4x4(cpi, xd, t, dry_run);
    } else {
      vp8_fix_contexts(xd);
    }
    if (dry_run)
      *t = t_backup;
    return;
  }

  if (!dry_run)
    cpi->skip_false_count[mb_skip_context] += skip_inc;

  plane_type = 3;
  if (has_y2_block) {
    if (tx_size == TX_8X8) {
      ENTROPY_CONTEXT *A = (ENTROPY_CONTEXT *)xd->above_context;
      ENTROPY_CONTEXT *L = (ENTROPY_CONTEXT *)xd->left_context;
      tokenize2nd_order_b_8x8(xd,
                              xd->block + 24, t, 1, xd->frame_type,
                              A + vp8_block2above_8x8[24],
                              L + vp8_block2left_8x8[24],
                              cpi, dry_run);
    } else
      tokenize2nd_order_b_4x4(xd, t, cpi, dry_run);

    plane_type = 0;
  }

  if (tx_size == TX_16X16) {
    ENTROPY_CONTEXT * A = (ENTROPY_CONTEXT *)xd->above_context;
    ENTROPY_CONTEXT * L = (ENTROPY_CONTEXT *)xd->left_context;

    tokenize1st_order_b_16x16(xd, xd->block, t, 3,
                              xd->frame_type, A, L, cpi, dry_run);

    for (b = 1; b < 16; b++) {
      *(A + vp8_block2above[b]) = *(A);
      *(L + vp8_block2left[b] ) = *(L);
    }
    for (b = 16; b < 24; b += 4) {
      tokenize1st_order_b_8x8(xd, xd->block + b, t, 2, xd->frame_type,
          A + vp8_block2above_8x8[b], L + vp8_block2left_8x8[b], cpi, dry_run);
      *(A + vp8_block2above_8x8[b]+1) = *(A + vp8_block2above_8x8[b]);
      *(L + vp8_block2left_8x8[b]+1 ) = *(L + vp8_block2left_8x8[b]);
    }
    vpx_memset(&A[8], 0, sizeof(A[8]));
    vpx_memset(&L[8], 0, sizeof(L[8]));
  }
  else if (tx_size == TX_8X8) {
    ENTROPY_CONTEXT *A = (ENTROPY_CONTEXT *)xd->above_context;
    ENTROPY_CONTEXT *L = (ENTROPY_CONTEXT *)xd->left_context;
    if (xd->mode_info_context->mbmi.mode == I8X8_PRED) {
      plane_type = PLANE_TYPE_Y_WITH_DC;
    }
    for (b = 0; b < 16; b += 4) {
      tokenize1st_order_b_8x8(xd,
                              xd->block + b,
                              t, plane_type, xd->frame_type,
                              A + vp8_block2above_8x8[b],
                              L + vp8_block2left_8x8[b],
                              cpi, dry_run);
      *(A + vp8_block2above_8x8[b] + 1) = *(A + vp8_block2above_8x8[b]);
      *(L + vp8_block2left_8x8[b] + 1)  = *(L + vp8_block2left_8x8[b]);
    }
    if (xd->mode_info_context->mbmi.mode == I8X8_PRED) {
      tokenize1st_order_chroma_4x4(xd, t, PLANE_TYPE_UV, cpi, dry_run);
    } else {
      for (b = 16; b < 24; b += 4) {
        tokenize1st_order_b_8x8(xd,
                                xd->block + b, t, 2, xd->frame_type,
                                A + vp8_block2above_8x8[b],
                                L + vp8_block2left_8x8[b],
                                cpi, dry_run);
        *(A + vp8_block2above_8x8[b] + 1) = *(A + vp8_block2above_8x8[b]);
        *(L + vp8_block2left_8x8[b] + 1) = *(L + vp8_block2left_8x8[b]);
      }
    }
  } else {
#if CONFIG_HYBRIDTRANSFORM
    if (active_ht)
      tokenize1st_order_ht_4x4(xd, t, plane_type, cpi, dry_run);
    else
#endif
      tokenize1st_order_b_4x4(xd, t, plane_type, cpi, dry_run);
  }
  if (dry_run)
    *t = t_backup;
}


#ifdef ENTROPY_STATS
void init_context_counters(void) {
  FILE *f = fopen("context.bin", "rb");
  if (!f) {
    vpx_memset(context_counters, 0, sizeof(context_counters));
    vpx_memset(context_counters_8x8, 0, sizeof(context_counters_8x8));
    vpx_memset(context_counters_16x16, 0, sizeof(context_counters_16x16));
  } else {
    fread(context_counters, sizeof(context_counters), 1, f);
    fread(context_counters_8x8, sizeof(context_counters_8x8), 1, f);
    fread(context_counters_16x16, sizeof(context_counters_16x16), 1, f);
    fclose(f);
  }

  f = fopen("treeupdate.bin", "rb");
  if (!f) {
    vpx_memset(tree_update_hist, 0, sizeof(tree_update_hist));
    vpx_memset(tree_update_hist_8x8, 0, sizeof(tree_update_hist_8x8));
    vpx_memset(tree_update_hist_16x16, 0, sizeof(tree_update_hist_16x16));
  } else {
    fread(tree_update_hist, sizeof(tree_update_hist), 1, f);
    fread(tree_update_hist_8x8, sizeof(tree_update_hist_8x8), 1, f);
    fread(tree_update_hist_16x16, sizeof(tree_update_hist_16x16), 1, f);
    fclose(f);
  }
}

void print_context_counters() {
  int type, band, pt, t;
  FILE *f = fopen("context.c", "w");

  fprintf(f, "#include \"entropy.h\"\n");
  fprintf(f, "\n/* *** GENERATED FILE: DO NOT EDIT *** */\n\n");
  fprintf(f, "static const unsigned int\n"
          "vp8_default_coef_counts[BLOCK_TYPES]\n"
          "                      [COEF_BANDS]\n"
          "                      [PREV_COEF_CONTEXTS]\n"
          "                      [MAX_ENTROPY_TOKENS]={\n");

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
  } while (++type < BLOCK_TYPES);
  fprintf(f, "\n};\n");

  fprintf(f, "static const unsigned int\nvp8_default_coef_counts_8x8"
          "[BLOCK_TYPES_8X8] [COEF_BANDS]"
          "[PREV_COEF_CONTEXTS] [MAX_ENTROPY_TOKENS] = {");
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
          const INT64 x = context_counters_8x8 [type] [band] [pt] [t];
          const int y = (int) x;

          assert(x == (INT64) y);  /* no overflow handling yet */
          fprintf(f, "%s %d", Comma(t), y);

        } while (++t < MAX_ENTROPY_TOKENS);

        fprintf(f, "}");
      } while (++pt < PREV_COEF_CONTEXTS);

      fprintf(f, "\n    }");

    } while (++band < COEF_BANDS);

    fprintf(f, "\n  }");
  } while (++type < BLOCK_TYPES_8X8);
  fprintf(f, "\n};\n");

  fprintf(f, "static const unsigned int\nvp8_default_coef_counts_16x16"
          "[BLOCK_TYPES_16X16] [COEF_BANDS]"
          "[PREV_COEF_CONTEXTS] [MAX_ENTROPY_TOKENS] = {");
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
          const INT64 x = context_counters_16x16 [type] [band] [pt] [t];
          const int y = (int) x;

          assert(x == (INT64) y);  /* no overflow handling yet */
          fprintf(f, "%s %d", Comma(t), y);

        } while (++t < MAX_ENTROPY_TOKENS);

        fprintf(f, "}");
      } while (++pt < PREV_COEF_CONTEXTS);

      fprintf(f, "\n    }");

    } while (++band < COEF_BANDS);

    fprintf(f, "\n  }");
  } while (++type < BLOCK_TYPES_16X16);
  fprintf(f, "\n};\n");

  fprintf(f, "static const vp8_prob\n"
          "vp8_default_coef_probs[BLOCK_TYPES] [COEF_BANDS] \n"
          "[PREV_COEF_CONTEXTS] [ENTROPY_NODES] = {");
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
        vp8_prob coef_probs[ENTROPY_NODES];
        for (t = 0; t < MAX_ENTROPY_TOKENS; ++t)
          coef_counts[t] = context_counters [type] [band] [pt] [t];
        vp8_tree_probs_from_distribution(
          MAX_ENTROPY_TOKENS, vp8_coef_encodings, vp8_coef_tree,
          coef_probs, branch_ct, coef_counts, 256, 1);
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
  } while (++type < BLOCK_TYPES);
  fprintf(f, "\n};\n");

  fprintf(f, "static const vp8_prob\n"
          "vp8_default_coef_probs_8x8[BLOCK_TYPES_8X8] [COEF_BANDS]\n"
          "[PREV_COEF_CONTEXTS] [ENTROPY_NODES] = {");
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
        vp8_prob coef_probs[ENTROPY_NODES];
        for (t = 0; t < MAX_ENTROPY_TOKENS; ++t)
          coef_counts[t] = context_counters_8x8[type] [band] [pt] [t];
        vp8_tree_probs_from_distribution(
          MAX_ENTROPY_TOKENS, vp8_coef_encodings, vp8_coef_tree,
          coef_probs, branch_ct, coef_counts, 256, 1);
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
  } while (++type < BLOCK_TYPES_8X8);
  fprintf(f, "\n};\n");

  fprintf(f, "static const vp8_prob\n"
          "vp8_default_coef_probs_16x16[BLOCK_TYPES_16X16] [COEF_BANDS]\n"
          "[PREV_COEF_CONTEXTS] [ENTROPY_NODES] = {");
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
        vp8_prob coef_probs[ENTROPY_NODES];
        for (t = 0; t < MAX_ENTROPY_TOKENS; ++t)
          coef_counts[t] = context_counters_16x16[type] [band] [pt] [t];
        vp8_tree_probs_from_distribution(
          MAX_ENTROPY_TOKENS, vp8_coef_encodings, vp8_coef_tree,
          coef_probs, branch_ct, coef_counts, 256, 1);
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
  } while (++type < BLOCK_TYPES_16X16);
  fprintf(f, "\n};\n");

  fclose(f);

  f = fopen("context.bin", "wb");
  fwrite(context_counters, sizeof(context_counters), 1, f);
  fwrite(context_counters_8x8, sizeof(context_counters_8x8), 1, f);
  fwrite(context_counters_16x16, sizeof(context_counters_16x16), 1, f);
  fclose(f);
}
#endif


void vp8_tokenize_initialize() {
  fill_value_tokens();
}


static __inline void stuff2nd_order_b_8x8(
  MACROBLOCKD *xd,
  const BLOCKD *const b,
  TOKENEXTRA **tp,
  const int type,     /* which plane: 0=Y no DC, 1=Y2, 2=UV, 3=Y with DC */
  const FRAME_TYPE frametype,
  ENTROPY_CONTEXT *a,
  ENTROPY_CONTEXT *l,
  VP8_COMP *cpi,
  int dry_run) {
  int pt; /* near block/prev token context index */
  TOKENEXTRA *t = *tp;        /* store tokens starting here */
  VP8_COMBINEENTROPYCONTEXTS(pt, *a, *l);
  (void) frametype;
  (void) type;
  (void) b;

  t->Token = DCT_EOB_TOKEN;
  t->context_tree = cpi->common.fc.coef_probs_8x8 [1] [0] [pt];
  // t->section = 11;
  t->skip_eob_node = 0;
  ++t;

  *tp = t;
  if (!dry_run)
    ++cpi->coef_counts_8x8       [1] [0] [pt] [DCT_EOB_TOKEN];
  pt = 0;
  *a = *l = pt;

}

static __inline void stuff1st_order_b_8x8
(
  MACROBLOCKD *xd,
  const BLOCKD *const b,
  TOKENEXTRA **tp,
  const int type,     /* which plane: 0=Y no DC, 1=Y2, 2=UV, 3=Y with DC */
  const FRAME_TYPE frametype,
  ENTROPY_CONTEXT *a,
  ENTROPY_CONTEXT *l,
  VP8_COMP *cpi,
  int dry_run) {
  int pt; /* near block/prev token context index */
  TOKENEXTRA *t = *tp;        /* store tokens starting here */
#if CONFIG_HYBRIDTRANSFORM8X8
  TX_TYPE tx_type = xd->mode_info_context->mbmi.mode == I8X8_PRED ?
      get_tx_type(xd, b) : DCT_DCT;
#endif

  VP8_COMBINEENTROPYCONTEXTS(pt, *a, *l);
  (void) frametype;
  (void) type;
  (void) b;

  t->Token = DCT_EOB_TOKEN;
#if CONFIG_HYBRIDTRANSFORM8X8
  if (tx_type != DCT_DCT)
    t->context_tree = cpi->common.fc.hybrid_coef_probs_8x8 [0] [1] [pt];
  else
#endif
    t->context_tree = cpi->common.fc.coef_probs_8x8 [0] [1] [pt];
  // t->section = 8;
  t->skip_eob_node = 0;
  ++t;
  *tp = t;
  if (!dry_run) {
#if CONFIG_HYBRIDTRANSFORM8X8
    if (tx_type == DCT_DCT)
      ++cpi->hybrid_coef_counts_8x8[0] [1] [pt] [DCT_EOB_TOKEN];
    else
#endif
      ++cpi->coef_counts_8x8[0] [1] [pt] [DCT_EOB_TOKEN];
  }
  pt = 0; /* 0 <-> all coeff data is zero */
  *a = *l = pt;


}

static __inline
void stuff1st_order_buv_8x8
(
  MACROBLOCKD *xd,
  const BLOCKD *const b,
  TOKENEXTRA **tp,
  const int type,     /* which plane: 0=Y no DC, 1=Y2, 2=UV, 3=Y with DC */
  const FRAME_TYPE frametype,
  ENTROPY_CONTEXT *a,
  ENTROPY_CONTEXT *l,
  VP8_COMP *cpi,
  int dry_run) {
  int pt; /* near block/prev token context index */
  TOKENEXTRA *t = *tp;        /* store tokens starting here */
  VP8_COMBINEENTROPYCONTEXTS(pt, *a, *l);
  (void) frametype;
  (void) type;
  (void) b;

  t->Token = DCT_EOB_TOKEN;
  t->context_tree = cpi->common.fc.coef_probs_8x8 [2] [0] [pt];
  // t->section = 13;
  t->skip_eob_node = 0;
  ++t;
  *tp = t;
  if (!dry_run)
    ++cpi->coef_counts_8x8[2] [0] [pt] [DCT_EOB_TOKEN];
  pt = 0; /* 0 <-> all coeff data is zero */
  *a = *l = pt;

}

void vp8_stuff_mb_8x8(VP8_COMP *cpi,
                      MACROBLOCKD *xd,
                      TOKENEXTRA **t,
                      int dry_run) {
  ENTROPY_CONTEXT *A = (ENTROPY_CONTEXT *)xd->above_context;
  ENTROPY_CONTEXT *L = (ENTROPY_CONTEXT *)xd->left_context;
  int plane_type;
  int b;
  TOKENEXTRA *t_backup = *t;

  stuff2nd_order_b_8x8(xd, xd->block + 24, t, 1, xd->frame_type,
                       A + vp8_block2above_8x8[24],
                       L + vp8_block2left_8x8[24], cpi, dry_run);
  plane_type = 0;

  for (b = 0; b < 16; b += 4) {
    stuff1st_order_b_8x8(xd, xd->block + b, t, plane_type, xd->frame_type,
                         A + vp8_block2above_8x8[b],
                         L + vp8_block2left_8x8[b],
                         cpi, dry_run);
    *(A + vp8_block2above_8x8[b] + 1) = *(A + vp8_block2above_8x8[b]);
    *(L + vp8_block2left_8x8[b] + 1)  = *(L + vp8_block2left_8x8[b]);
  }

  for (b = 16; b < 24; b += 4) {
    stuff1st_order_buv_8x8(xd, xd->block + b, t, 2, xd->frame_type,
                           A + vp8_block2above[b],
                           L + vp8_block2left[b],
                           cpi, dry_run);
    *(A + vp8_block2above_8x8[b] + 1) = *(A + vp8_block2above_8x8[b]);
    *(L + vp8_block2left_8x8[b] + 1) = *(L + vp8_block2left_8x8[b]);
  }
  if (dry_run)
    *t = t_backup;
}

static __inline
void stuff1st_order_b_16x16(MACROBLOCKD *xd,
                            const BLOCKD *const b,
                            TOKENEXTRA **tp,
                            const FRAME_TYPE frametype,
                            ENTROPY_CONTEXT *a,
                            ENTROPY_CONTEXT *l,
                            VP8_COMP *cpi,
                            int dry_run){
    int pt; /* near block/prev token context index */
    TOKENEXTRA *t = *tp;        /* store tokens starting here */
#if CONFIG_HYBRIDTRANSFORM16X16
    TX_TYPE tx_type = get_tx_type(xd, b);
#endif
    VP8_COMBINEENTROPYCONTEXTS(pt, *a, *l);
    (void) frametype;
    (void) b;

    t->Token = DCT_EOB_TOKEN;
#if CONFIG_HYBRIDTRANSFORM16X16
    if (tx_type != DCT_DCT)
      t->context_tree = cpi->common.fc.hybrid_coef_probs_16x16[3][1][pt];
    else
#endif
    t->context_tree = cpi->common.fc.coef_probs_16x16[3][1][pt];
    t->skip_eob_node = 0;
    ++t;
    *tp = t;
    if (!dry_run) {
#if CONFIG_HYBRIDTRANSFORM16X16
      if (tx_type != DCT_DCT)
        ++cpi->hybrid_coef_counts_16x16[3][1][pt][DCT_EOB_TOKEN];
      else
#endif
      ++cpi->coef_counts_16x16[3][1][pt][DCT_EOB_TOKEN];
    }
    pt = 0; /* 0 <-> all coeff data is zero */
    *a = *l = pt;
}

void vp8_stuff_mb_16x16(VP8_COMP *cpi,
                        MACROBLOCKD *xd,
                        TOKENEXTRA **t,
                        int dry_run) {
  ENTROPY_CONTEXT * A = (ENTROPY_CONTEXT *)xd->above_context;
  ENTROPY_CONTEXT * L = (ENTROPY_CONTEXT *)xd->left_context;
  int b, i;
  TOKENEXTRA *t_backup = *t;

  stuff1st_order_b_16x16(xd, xd->block, t, xd->frame_type, A, L, cpi, dry_run);
  for (i = 1; i < 16; i++) {
    *(A + vp8_block2above[i]) = *(A);
    *(L +  vp8_block2left[i]) = *(L);
  }
  for (b = 16; b < 24; b += 4) {
    stuff1st_order_buv_8x8(xd, xd->block + b, t, 2, xd->frame_type,
        A + vp8_block2above[b],
        L + vp8_block2left[b],
        cpi, dry_run);
    *(A + vp8_block2above_8x8[b]+1) = *(A + vp8_block2above_8x8[b]);
    *(L + vp8_block2left_8x8[b]+1 ) = *(L + vp8_block2left_8x8[b]);
  }
  vpx_memset(&A[8], 0, sizeof(A[8]));
  vpx_memset(&L[8], 0, sizeof(L[8]));
  if (dry_run)
    *t = t_backup;
}

static __inline void stuff2nd_order_b_4x4
(
  MACROBLOCKD *xd,
  const BLOCKD *const b,
  TOKENEXTRA **tp,
  ENTROPY_CONTEXT *a,
  ENTROPY_CONTEXT *l,
  VP8_COMP *cpi,
  int dry_run) {
  int pt; /* near block/prev token context index */
  TOKENEXTRA *t = *tp;        /* store tokens starting here */
  VP8_COMBINEENTROPYCONTEXTS(pt, *a, *l);

  t->Token = DCT_EOB_TOKEN;
  t->context_tree = cpi->common.fc.coef_probs [1] [0] [pt];
  t->skip_eob_node = 0;
  ++t;
  *tp = t;
  if (!dry_run)
    ++cpi->coef_counts       [1] [0] [pt] [DCT_EOB_TOKEN];

  pt = 0;
  *a = *l = pt;

}

static __inline void stuff1st_order_b_4x4(MACROBLOCKD *xd,
                                          const BLOCKD *const b,
                                          TOKENEXTRA **tp,
                                          ENTROPY_CONTEXT *a,
                                          ENTROPY_CONTEXT *l,
                                          VP8_COMP *cpi,
                                          int dry_run) {
  int pt; /* near block/prev token context index */
  TOKENEXTRA *t = *tp;        /* store tokens starting here */
#if CONFIG_HYBRIDTRANSFORM
  TX_TYPE tx_type = get_tx_type(xd, b);
#endif
  VP8_COMBINEENTROPYCONTEXTS(pt, *a, *l);

  t->Token = DCT_EOB_TOKEN;
#if CONFIG_HYBRIDTRANSFORM
  if (tx_type != DCT_DCT)
    t->context_tree = cpi->common.fc.hybrid_coef_probs [0] [1] [pt];
  else
#endif
    t->context_tree = cpi->common.fc.coef_probs [0] [1] [pt];
  t->skip_eob_node = 0;
  ++t;
  *tp = t;
  if (!dry_run) {
#if CONFIG_HYBRIDTRANSFORM
    if (tx_type != DCT_DCT)
      ++cpi->hybrid_coef_counts[0] [1] [pt] [DCT_EOB_TOKEN];
    else
#endif
      ++cpi->coef_counts[0] [1] [pt] [DCT_EOB_TOKEN];
  }
  pt = 0; /* 0 <-> all coeff data is zero */
  *a = *l = pt;

}
static __inline
void stuff1st_order_buv_4x4
(
  MACROBLOCKD *xd,
  const BLOCKD *const b,
  TOKENEXTRA **tp,
  ENTROPY_CONTEXT *a,
  ENTROPY_CONTEXT *l,
  VP8_COMP *cpi,
  int dry_run) {
  int pt; /* near block/prev token context index */
  TOKENEXTRA *t = *tp;        /* store tokens starting here */
  VP8_COMBINEENTROPYCONTEXTS(pt, *a, *l);

  t->Token = DCT_EOB_TOKEN;
  t->context_tree = cpi->common.fc.coef_probs [2] [0] [pt];
  t->skip_eob_node = 0;
  ++t;
  *tp = t;
  if (!dry_run)
    ++cpi->coef_counts[2] [0] [pt] [DCT_EOB_TOKEN];
  pt = 0; /* 0 <-> all coeff data is zero */
  *a = *l = pt;
}

void vp8_stuff_mb_4x4(VP8_COMP *cpi, MACROBLOCKD *xd,
                      TOKENEXTRA **t, int dry_run) {
  ENTROPY_CONTEXT *A = (ENTROPY_CONTEXT *)xd->above_context;
  ENTROPY_CONTEXT *L = (ENTROPY_CONTEXT *)xd->left_context;
  int b;
  TOKENEXTRA *t_backup = *t;

  stuff2nd_order_b_4x4(xd, xd->block + 24, t,
                       A + vp8_block2above[24],
                       L + vp8_block2left[24],
                       cpi, dry_run);

  for (b = 0; b < 16; b++)
    stuff1st_order_b_4x4(xd, xd->block + b, t,
                         A + vp8_block2above[b],
                         L + vp8_block2left[b],
                         cpi, dry_run);

  for (b = 16; b < 24; b++)
    stuff1st_order_buv_4x4(xd, xd->block + b, t,
                           A + vp8_block2above[b],
                           L + vp8_block2left[b],
                           cpi, dry_run);

  if (dry_run)
    *t = t_backup;
}

void vp8_stuff_mb_8x8_4x4uv(VP8_COMP *cpi,
                            MACROBLOCKD *xd,
                            TOKENEXTRA **t,
                            int dry_run) {
  ENTROPY_CONTEXT *A = (ENTROPY_CONTEXT *)xd->above_context;
  ENTROPY_CONTEXT *L = (ENTROPY_CONTEXT *)xd->left_context;
  int plane_type;
  int b;
  TOKENEXTRA *t_backup = *t;

  stuff2nd_order_b_8x8(xd, xd->block + 24, t, 1, xd->frame_type,
                       A + vp8_block2above_8x8[24],
                       L + vp8_block2left_8x8[24], cpi, dry_run);
  plane_type = 3;

  for (b = 0; b < 16; b += 4) {
    stuff1st_order_b_8x8(xd, xd->block + b, t, plane_type, xd->frame_type,
                         A + vp8_block2above_8x8[b],
                         L + vp8_block2left_8x8[b],
                         cpi, dry_run);
    *(A + vp8_block2above_8x8[b] + 1) = *(A + vp8_block2above_8x8[b]);
    *(L + vp8_block2left_8x8[b] + 1)  = *(L + vp8_block2left_8x8[b]);
  }

  for (b = 16; b < 24; b++)
    stuff1st_order_buv_4x4(xd, xd->block + b, t,
                           A + vp8_block2above[b],
                           L + vp8_block2left[b],
                           cpi, dry_run);

  if (dry_run)
    *t = t_backup;
}

void vp8_fix_contexts(MACROBLOCKD *xd) {
  /* Clear entropy contexts for Y2 blocks */
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
  }
}
