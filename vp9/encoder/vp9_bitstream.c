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
#include <stdio.h>
#include <limits.h>

#include "vpx/vpx_encoder.h"
#include "vpx_mem/vpx_mem.h"

#include "vp9/common/vp9_entropymode.h"
#include "vp9/common/vp9_entropymv.h"
#include "vp9/common/vp9_findnearmv.h"
#include "vp9/common/vp9_tile_common.h"
#include "vp9/common/vp9_systemdependent.h"
#include "vp9/common/vp9_pragmas.h"
#include "vp9/common/vp9_seg_common.h"
#include "vp9/common/vp9_pred_common.h"
#include "vp9/common/vp9_entropy.h"
#include "vp9/common/vp9_entropymv.h"
#include "vp9/common/vp9_mvref_common.h"
#include "vp9/common/vp9_treecoder.h"

#include "vp9/encoder/vp9_encodemv.h"
#include "vp9/encoder/vp9_mcomp.h"
#include "vp9/encoder/vp9_bitstream.h"
#include "vp9/encoder/vp9_segmentation.h"
#include "vp9/encoder/vp9_write_bit_buffer.h"

#if defined(SECTIONBITS_OUTPUT)
unsigned __int64 Sectionbits[500];
#endif

#ifdef ENTROPY_STATS
int intra_mode_stats[VP9_KF_BINTRAMODES]
                    [VP9_KF_BINTRAMODES]
                    [VP9_KF_BINTRAMODES];
vp9_coeff_stats tree_update_hist_4x4[BLOCK_TYPES];
vp9_coeff_stats tree_update_hist_8x8[BLOCK_TYPES];
vp9_coeff_stats tree_update_hist_16x16[BLOCK_TYPES];
vp9_coeff_stats tree_update_hist_32x32[BLOCK_TYPES];

extern unsigned int active_section;
#endif

#define vp9_cost_upd  ((int)(vp9_cost_one(upd) - vp9_cost_zero(upd)) >> 8)
#define vp9_cost_upd256  ((int)(vp9_cost_one(upd) - vp9_cost_zero(upd)))

static int update_bits[255];

static INLINE void write_le16(uint8_t *p, int value) {
  p[0] = value;
  p[1] = value >> 8;
}

static INLINE void write_le32(uint8_t *p, int value) {
  p[0] = value;
  p[1] = value >> 8;
  p[2] = value >> 16;
  p[3] = value >> 24;
}

void vp9_encode_unsigned_max(vp9_writer *br, int data, int max) {
  assert(data <= max);
  while (max) {
    vp9_write_bit(br, data & 1);
    data >>= 1;
    max >>= 1;
  }
}

int recenter_nonneg(int v, int m) {
  if (v > (m << 1))
    return v;
  else if (v >= m)
    return ((v - m) << 1);
  else
    return ((m - v) << 1) - 1;
}

static int get_unsigned_bits(unsigned num_values) {
  int cat = 0;
  if ((num_values--) <= 1) return 0;
  while (num_values > 0) {
    cat++;
    num_values >>= 1;
  }
  return cat;
}

void encode_uniform(vp9_writer *w, int v, int n) {
  int l = get_unsigned_bits(n);
  int m;
  if (l == 0)
    return;
  m = (1 << l) - n;
  if (v < m) {
    vp9_write_literal(w, v, l - 1);
  } else {
    vp9_write_literal(w, m + ((v - m) >> 1), l - 1);
    vp9_write_literal(w, (v - m) & 1, 1);
  }
}

int count_uniform(int v, int n) {
  int l = get_unsigned_bits(n);
  int m;
  if (l == 0) return 0;
  m = (1 << l) - n;
  if (v < m)
    return l - 1;
  else
    return l;
}

void encode_term_subexp(vp9_writer *w, int word, int k, int num_syms) {
  int i = 0;
  int mk = 0;
  while (1) {
    int b = (i ? k + i - 1 : k);
    int a = (1 << b);
    if (num_syms <= mk + 3 * a) {
      encode_uniform(w, word - mk, num_syms - mk);
      break;
    } else {
      int t = (word >= mk + a);
      vp9_write_literal(w, t, 1);
      if (t) {
        i = i + 1;
        mk += a;
      } else {
        vp9_write_literal(w, word - mk, b);
        break;
      }
    }
  }
}

int count_term_subexp(int word, int k, int num_syms) {
  int count = 0;
  int i = 0;
  int mk = 0;
  while (1) {
    int b = (i ? k + i - 1 : k);
    int a = (1 << b);
    if (num_syms <= mk + 3 * a) {
      count += count_uniform(word - mk, num_syms - mk);
      break;
    } else {
      int t = (word >= mk + a);
      count++;
      if (t) {
        i = i + 1;
        mk += a;
      } else {
        count += b;
        break;
      }
    }
  }
  return count;
}

static void compute_update_table() {
  int i;
  for (i = 0; i < 255; i++)
    update_bits[i] = count_term_subexp(i, SUBEXP_PARAM, 255);
}

static int split_index(int i, int n, int modulus) {
  int max1 = (n - 1 - modulus / 2) / modulus + 1;
  if (i % modulus == modulus / 2) i = i / modulus;
  else i = max1 + i - (i + modulus - modulus / 2) / modulus;
  return i;
}

static int remap_prob(int v, int m) {
  const int n = 256;
  const int modulus = MODULUS_PARAM;
  int i;
  if ((m << 1) <= n)
    i = recenter_nonneg(v, m) - 1;
  else
    i = recenter_nonneg(n - 1 - v, n - 1 - m) - 1;

  i = split_index(i, n - 1, modulus);
  return i;
}

static void write_prob_diff_update(vp9_writer *w,
                                   vp9_prob newp, vp9_prob oldp) {
  int delp = remap_prob(newp, oldp);
  encode_term_subexp(w, delp, SUBEXP_PARAM, 255);
}

static int prob_diff_update_cost(vp9_prob newp, vp9_prob oldp) {
  int delp = remap_prob(newp, oldp);
  return update_bits[delp] * 256;
}

static void update_mode(
  vp9_writer *w,
  int n,
  const struct vp9_token tok[/* n */],
  vp9_tree tree,
  vp9_prob Pnew               [/* n-1 */],
  vp9_prob Pcur               [/* n-1 */],
  unsigned int bct            [/* n-1 */] [2],
  const unsigned int num_events[/* n */]
) {
  unsigned int new_b = 0, old_b = 0;
  int i = 0;

  vp9_tree_probs_from_distribution(tree, Pnew, bct, num_events, 0);
  n--;

  do {
    new_b += cost_branch(bct[i], Pnew[i]);
    old_b += cost_branch(bct[i], Pcur[i]);
  } while (++i < n);

  if (new_b + (n << 8) < old_b) {
    int i = 0;

    vp9_write_bit(w, 1);

    do {
      const vp9_prob p = Pnew[i];

      vp9_write_literal(w, Pcur[i] = p ? p : 1, 8);
    } while (++i < n);
  } else
    vp9_write_bit(w, 0);
}

static void update_mbintra_mode_probs(VP9_COMP* const cpi,
                                      vp9_writer* const bc) {
  VP9_COMMON *const cm = &cpi->common;

  vp9_prob pnew[VP9_YMODES - 1];
  unsigned int bct[VP9_YMODES - 1][2];

  update_mode(bc, VP9_YMODES, vp9_ymode_encodings, vp9_ymode_tree, pnew,
              cm->fc.ymode_prob, bct, (unsigned int *)cpi->ymode_count);

  update_mode(bc, VP9_I32X32_MODES, vp9_sb_ymode_encodings,
              vp9_sb_ymode_tree, pnew, cm->fc.sb_ymode_prob, bct,
              (unsigned int *)cpi->sb_ymode_count);
}

void vp9_update_skip_probs(VP9_COMP *cpi) {
  VP9_COMMON *const pc = &cpi->common;
  int k;

  for (k = 0; k < MBSKIP_CONTEXTS; ++k)
    pc->mbskip_pred_probs[k] = get_binary_prob(cpi->skip_false_count[k],
                                               cpi->skip_true_count[k]);
}

static void update_switchable_interp_probs(VP9_COMP *cpi,
                                           vp9_writer* const bc) {
  VP9_COMMON *const pc = &cpi->common;
  unsigned int branch_ct[32][2];
  int i, j;
  for (j = 0; j <= VP9_SWITCHABLE_FILTERS; ++j) {
    vp9_tree_probs_from_distribution(
        vp9_switchable_interp_tree,
        pc->fc.switchable_interp_prob[j], branch_ct,
        cpi->switchable_interp_count[j], 0);
    for (i = 0; i < VP9_SWITCHABLE_FILTERS - 1; ++i) {
      if (pc->fc.switchable_interp_prob[j][i] < 1)
        pc->fc.switchable_interp_prob[j][i] = 1;
      vp9_write_prob(bc, pc->fc.switchable_interp_prob[j][i]);
    }
  }
}

// This function updates the reference frame prediction stats
static void update_refpred_stats(VP9_COMP *cpi) {
  VP9_COMMON *const cm = &cpi->common;
  int i;
  vp9_prob new_pred_probs[PREDICTION_PROBS];
  int old_cost, new_cost;

  // Set the prediction probability structures to defaults
  if (cm->frame_type != KEY_FRAME) {
    // From the prediction counts set the probabilities for each context
    for (i = 0; i < PREDICTION_PROBS; i++) {
      const int c0 = cpi->ref_pred_count[i][0];
      const int c1 = cpi->ref_pred_count[i][1];

      new_pred_probs[i] = get_binary_prob(c0, c1);

      // Decide whether or not to update the reference frame probs.
      // Returned costs are in 1/256 bit units.
      old_cost = c0 * vp9_cost_zero(cm->ref_pred_probs[i]) +
                 c1 * vp9_cost_one(cm->ref_pred_probs[i]);

      new_cost = c0 * vp9_cost_zero(new_pred_probs[i]) +
                 c1 * vp9_cost_one(new_pred_probs[i]);

      // Cost saving must be >= 8 bits (2048 in these units)
      if ((old_cost - new_cost) >= 2048) {
        cpi->ref_pred_probs_update[i] = 1;
        cm->ref_pred_probs[i] = new_pred_probs[i];
      } else
        cpi->ref_pred_probs_update[i] = 0;
    }
  }
}

// This function is called to update the mode probability context used to encode
// inter modes. It assumes the branch counts table has already been populated
// prior to the actual packing of the bitstream (in rd stage or dummy pack)
//
// The branch counts table is re-populated during the actual pack stage and in
// the decoder to facilitate backwards update of the context.
static void update_inter_mode_probs(VP9_COMMON *cm,
                                    int mode_context[INTER_MODE_CONTEXTS][4]) {
  int i, j;
  unsigned int (*mv_ref_ct)[4][2] = cm->fc.mv_ref_ct;

  vpx_memcpy(mode_context, cm->fc.vp9_mode_contexts,
             sizeof(cm->fc.vp9_mode_contexts));

  for (i = 0; i < INTER_MODE_CONTEXTS; i++) {
    for (j = 0; j < 4; j++) {
      int new_prob, old_cost, new_cost;

      // Work out cost of coding branches with the old and optimal probability
      old_cost = cost_branch256(mv_ref_ct[i][j], mode_context[i][j]);
      new_prob = get_binary_prob(mv_ref_ct[i][j][0], mv_ref_ct[i][j][1]);
      new_cost = cost_branch256(mv_ref_ct[i][j], new_prob);

      // If cost saving is >= 14 bits then update the mode probability.
      // This is the approximate net cost of updating one probability given
      // that the no update case ismuch more common than the update case.
      if (new_cost <= (old_cost - (14 << 8))) {
        mode_context[i][j] = new_prob;
      }
    }
  }
}

static void write_ymode(vp9_writer *bc, int m, const vp9_prob *p) {
  write_token(bc, vp9_ymode_tree, p, vp9_ymode_encodings + m);
}

static void kfwrite_ymode(vp9_writer *bc, int m, const vp9_prob *p) {
  write_token(bc, vp9_kf_ymode_tree, p, vp9_kf_ymode_encodings + m);
}

static void write_sb_ymode(vp9_writer *bc, int m, const vp9_prob *p) {
  write_token(bc, vp9_sb_ymode_tree, p, vp9_sb_ymode_encodings + m);
}

static void sb_kfwrite_ymode(vp9_writer *bc, int m, const vp9_prob *p) {
  write_token(bc, vp9_uv_mode_tree, p, vp9_sb_kf_ymode_encodings + m);
}

static void write_uv_mode(vp9_writer *bc, int m, const vp9_prob *p) {
  write_token(bc, vp9_uv_mode_tree, p, vp9_uv_mode_encodings + m);
}

static void write_kf_bmode(vp9_writer *bc, int m, const vp9_prob *p) {
  write_token(bc, vp9_kf_bmode_tree, p, vp9_kf_bmode_encodings + m);
}

static int prob_update_savings(const unsigned int *ct,
                               const vp9_prob oldp, const vp9_prob newp,
                               const vp9_prob upd) {
  const int old_b = cost_branch256(ct, oldp);
  const int new_b = cost_branch256(ct, newp);
  const int update_b = 2048 + vp9_cost_upd256;
  return old_b - new_b - update_b;
}

static int prob_diff_update_savings_search(const unsigned int *ct,
                                           const vp9_prob oldp, vp9_prob *bestp,
                                           const vp9_prob upd) {
  const int old_b = cost_branch256(ct, oldp);
  int new_b, update_b, savings, bestsavings, step;
  vp9_prob newp, bestnewp;

  bestsavings = 0;
  bestnewp = oldp;

  step = (*bestp > oldp ? -1 : 1);
  for (newp = *bestp; newp != oldp; newp += step) {
    new_b = cost_branch256(ct, newp);
    update_b = prob_diff_update_cost(newp, oldp) + vp9_cost_upd256;
    savings = old_b - new_b - update_b;
    if (savings > bestsavings) {
      bestsavings = savings;
      bestnewp = newp;
    }
  }
  *bestp = bestnewp;
  return bestsavings;
}

static int prob_diff_update_savings_search_model(const unsigned int *ct,
                                                 const vp9_prob *oldp,
                                                 vp9_prob *bestp,
                                                 const vp9_prob upd,
                                                 int b, int r) {
  int i, old_b, new_b, update_b, savings, bestsavings, step;
  int newp;
  vp9_prob bestnewp, newplist[ENTROPY_NODES], oldplist[ENTROPY_NODES];
  vp9_model_to_full_probs(oldp, oldplist);
  vpx_memcpy(newplist, oldp, sizeof(vp9_prob) * UNCONSTRAINED_NODES);
  for (i = UNCONSTRAINED_NODES, old_b = 0; i < ENTROPY_NODES; ++i)
    old_b += cost_branch256(ct + 2 * i, oldplist[i]);
  old_b += cost_branch256(ct + 2 * PIVOT_NODE, oldplist[PIVOT_NODE]);

  bestsavings = 0;
  bestnewp = oldp[PIVOT_NODE];

  step = (*bestp > oldp[PIVOT_NODE] ? -1 : 1);
  newp = *bestp;
  for (; newp != oldp[PIVOT_NODE]; newp += step) {
    if (newp < 1 || newp > 255) continue;
    newplist[PIVOT_NODE] = newp;
    vp9_model_to_full_probs(newplist, newplist);
    for (i = UNCONSTRAINED_NODES, new_b = 0; i < ENTROPY_NODES; ++i)
      new_b += cost_branch256(ct + 2 * i, newplist[i]);
    new_b += cost_branch256(ct + 2 * PIVOT_NODE, newplist[PIVOT_NODE]);
    update_b = prob_diff_update_cost(newp, oldp[PIVOT_NODE]) +
        vp9_cost_upd256;
    savings = old_b - new_b - update_b;
    if (savings > bestsavings) {
      bestsavings = savings;
      bestnewp = newp;
    }
  }
  *bestp = bestnewp;
  return bestsavings;
}

static void vp9_cond_prob_update(vp9_writer *bc, vp9_prob *oldp, vp9_prob upd,
                                 unsigned int *ct) {
  vp9_prob newp;
  int savings;
  newp = get_binary_prob(ct[0], ct[1]);
  savings = prob_update_savings(ct, *oldp, newp, upd);
  if (savings > 0) {
    vp9_write(bc, 1, upd);
    vp9_write_prob(bc, newp);
    *oldp = newp;
  } else {
    vp9_write(bc, 0, upd);
  }
}

static void pack_mb_tokens(vp9_writer* const bc,
                           TOKENEXTRA **tp,
                           const TOKENEXTRA *const stop) {
  TOKENEXTRA *p = *tp;

  while (p < stop) {
    const int t = p->token;
    const struct vp9_token *const a = vp9_coef_encodings + t;
    const vp9_extra_bit *const b = vp9_extra_bits + t;
    int i = 0;
    const vp9_prob *pp;
    int v = a->value;
    int n = a->len;
    int ncount = n;
    vp9_prob probs[ENTROPY_NODES];

    if (t == EOSB_TOKEN) {
      ++p;
      break;
    }
    if (t >= TWO_TOKEN) {
      vp9_model_to_full_probs(p->context_tree, probs);
      pp = probs;
    } else {
      pp = p->context_tree;
    }
    assert(pp != 0);

    /* skip one or two nodes */
    if (p->skip_eob_node) {
      n -= p->skip_eob_node;
      i = 2 * p->skip_eob_node;
      ncount -= p->skip_eob_node;
    }

    do {
      const int bb = (v >> --n) & 1;
      vp9_write(bc, bb, pp[i >> 1]);
      i = vp9_coef_tree[i + bb];
      ncount--;
    } while (n && ncount);

    if (b->base_val) {
      const int e = p->extra, l = b->len;

      if (l) {
        const unsigned char *pb = b->prob;
        int v = e >> 1;
        int n = l;              /* number of bits in v, assumed nonzero */
        int i = 0;

        do {
          const int bb = (v >> --n) & 1;
          vp9_write(bc, bb, pb[i >> 1]);
          i = b->tree[i + bb];
        } while (n);
      }

      vp9_write_bit(bc, e & 1);
    }
    ++p;
  }

  *tp = p;
}

static void write_mv_ref(vp9_writer *bc, MB_PREDICTION_MODE m,
                         const vp9_prob *p) {
#if CONFIG_DEBUG
  assert(NEARESTMV <= m  &&  m <= SPLITMV);
#endif
  write_token(bc, vp9_mv_ref_tree, p,
              vp9_mv_ref_encoding_array - NEARESTMV + m);
}

static void write_sb_mv_ref(vp9_writer *bc, MB_PREDICTION_MODE m,
                            const vp9_prob *p) {
#if CONFIG_DEBUG
  assert(NEARESTMV <= m  &&  m < SPLITMV);
#endif
  write_token(bc, vp9_sb_mv_ref_tree, p,
              vp9_sb_mv_ref_encoding_array - NEARESTMV + m);
}

// This function writes the current macro block's segnment id to the bitstream
// It should only be called if a segment map update is indicated.
static void write_mb_segid(vp9_writer *bc,
                           const MB_MODE_INFO *mi, const MACROBLOCKD *xd) {
  if (xd->segmentation_enabled && xd->update_mb_segmentation_map)
    treed_write(bc, vp9_segment_tree, xd->mb_segment_tree_probs,
                mi->segment_id, 3);
}

// This function encodes the reference frame
static void encode_ref_frame(vp9_writer *const bc,
                             VP9_COMMON *const cm,
                             MACROBLOCKD *xd,
                             int segment_id,
                             MV_REFERENCE_FRAME rf) {
  int seg_ref_active;
  int seg_ref_count = 0;
  seg_ref_active = vp9_segfeature_active(xd,
                                         segment_id,
                                         SEG_LVL_REF_FRAME);

  if (seg_ref_active) {
    seg_ref_count = vp9_check_segref(xd, segment_id, INTRA_FRAME) +
                    vp9_check_segref(xd, segment_id, LAST_FRAME) +
                    vp9_check_segref(xd, segment_id, GOLDEN_FRAME) +
                    vp9_check_segref(xd, segment_id, ALTREF_FRAME);
  }

  // If segment level coding of this signal is disabled...
  // or the segment allows multiple reference frame options
  if (!seg_ref_active || (seg_ref_count > 1)) {
    // Values used in prediction model coding
    unsigned char prediction_flag;
    vp9_prob pred_prob;
    MV_REFERENCE_FRAME pred_rf;

    // Get the context probability the prediction flag
    pred_prob = vp9_get_pred_prob(cm, xd, PRED_REF);

    // Get the predicted value.
    pred_rf = vp9_get_pred_ref(cm, xd);

    // Did the chosen reference frame match its predicted value.
    prediction_flag =
      (xd->mode_info_context->mbmi.ref_frame == pred_rf);

    vp9_set_pred_flag(xd, PRED_REF, prediction_flag);
    vp9_write(bc, prediction_flag, pred_prob);

    // If not predicted correctly then code value explicitly
    if (!prediction_flag) {
      vp9_prob mod_refprobs[PREDICTION_PROBS];

      vpx_memcpy(mod_refprobs,
                 cm->mod_refprobs[pred_rf], sizeof(mod_refprobs));

      // If segment coding enabled blank out options that cant occur by
      // setting the branch probability to 0.
      if (seg_ref_active) {
        mod_refprobs[INTRA_FRAME] *=
          vp9_check_segref(xd, segment_id, INTRA_FRAME);
        mod_refprobs[LAST_FRAME] *=
          vp9_check_segref(xd, segment_id, LAST_FRAME);
        mod_refprobs[GOLDEN_FRAME] *=
          (vp9_check_segref(xd, segment_id, GOLDEN_FRAME) *
           vp9_check_segref(xd, segment_id, ALTREF_FRAME));
      }

      if (mod_refprobs[0]) {
        vp9_write(bc, (rf != INTRA_FRAME), mod_refprobs[0]);
      }

      // Inter coded
      if (rf != INTRA_FRAME) {
        if (mod_refprobs[1]) {
          vp9_write(bc, (rf != LAST_FRAME), mod_refprobs[1]);
        }

        if (rf != LAST_FRAME) {
          if (mod_refprobs[2]) {
            vp9_write(bc, (rf != GOLDEN_FRAME), mod_refprobs[2]);
          }
        }
      }
    }
  }

  // if using the prediction mdoel we have nothing further to do because
  // the reference frame is fully coded by the segment
}

// Update the probabilities used to encode reference frame data
static void update_ref_probs(VP9_COMP *const cpi) {
  VP9_COMMON *const cm = &cpi->common;

  const int *const rfct = cpi->count_mb_ref_frame_usage;
  const int rf_intra = rfct[INTRA_FRAME];
  const int rf_inter = rfct[LAST_FRAME] +
                       rfct[GOLDEN_FRAME] + rfct[ALTREF_FRAME];

  cm->prob_intra_coded = get_binary_prob(rf_intra, rf_inter);
  cm->prob_last_coded = get_prob(rfct[LAST_FRAME], rf_inter);
  cm->prob_gf_coded = get_binary_prob(rfct[GOLDEN_FRAME], rfct[ALTREF_FRAME]);

  // Compute a modified set of probabilities to use when prediction of the
  // reference frame fails
  vp9_compute_mod_refprobs(cm);
}

static void pack_inter_mode_mvs(VP9_COMP *cpi, MODE_INFO *m,
                                vp9_writer *bc, int mi_row, int mi_col) {
  VP9_COMMON *const pc = &cpi->common;
  const nmv_context *nmvc = &pc->fc.nmvc;
  MACROBLOCK *const x = &cpi->mb;
  MACROBLOCKD *const xd = &x->e_mbd;
  MB_MODE_INFO *const mi = &m->mbmi;
  const MV_REFERENCE_FRAME rf = mi->ref_frame;
  const MB_PREDICTION_MODE mode = mi->mode;
  const int segment_id = mi->segment_id;
  int skip_coeff;

  xd->prev_mode_info_context = pc->prev_mi + (m - pc->mi);
  x->partition_info = x->pi + (m - pc->mi);

#ifdef ENTROPY_STATS
  active_section = 9;
#endif

  if (cpi->mb.e_mbd.update_mb_segmentation_map) {
    // Is temporal coding of the segment map enabled
    if (pc->temporal_update) {
      unsigned char prediction_flag = vp9_get_pred_flag(xd, PRED_SEG_ID);
      vp9_prob pred_prob = vp9_get_pred_prob(pc, xd, PRED_SEG_ID);

      // Code the segment id prediction flag for this mb
      vp9_write(bc, prediction_flag, pred_prob);

      // If the mb segment id wasn't predicted code explicitly
      if (!prediction_flag)
        write_mb_segid(bc, mi, &cpi->mb.e_mbd);
    } else {
      // Normal unpredicted coding
      write_mb_segid(bc, mi, &cpi->mb.e_mbd);
    }
  }

  if (vp9_segfeature_active(xd, segment_id, SEG_LVL_SKIP)) {
    skip_coeff = 1;
  } else {
    skip_coeff = m->mbmi.mb_skip_coeff;
    vp9_write(bc, skip_coeff,
              vp9_get_pred_prob(pc, xd, PRED_MBSKIP));
  }

  // Encode the reference frame.
  encode_ref_frame(bc, pc, xd, segment_id, rf);

  if (mi->sb_type >= BLOCK_SIZE_SB8X8 && pc->txfm_mode == TX_MODE_SELECT &&
      !(rf != INTRA_FRAME &&
        (skip_coeff || vp9_segfeature_active(xd, segment_id, SEG_LVL_SKIP)))) {
    TX_SIZE sz = mi->txfm_size;
    // FIXME(rbultje) code ternary symbol once all experiments are merged
    vp9_write(bc, sz != TX_4X4, pc->prob_tx[0]);
    if (mi->sb_type >= BLOCK_SIZE_MB16X16 && sz != TX_4X4) {
      vp9_write(bc, sz != TX_8X8, pc->prob_tx[1]);
      if (mi->sb_type >= BLOCK_SIZE_SB32X32 && sz != TX_8X8)
        vp9_write(bc, sz != TX_16X16, pc->prob_tx[2]);
    }
  }

  if (rf == INTRA_FRAME) {
#ifdef ENTROPY_STATS
    active_section = 6;
#endif

    if (m->mbmi.sb_type >= BLOCK_SIZE_SB8X8)
      write_sb_ymode(bc, mode, pc->fc.sb_ymode_prob);

    if (m->mbmi.sb_type < BLOCK_SIZE_SB8X8) {
      int idx, idy;
      int bw = 1 << b_width_log2(mi->sb_type);
      int bh = 1 << b_height_log2(mi->sb_type);
      for (idy = 0; idy < 2; idy += bh)
        for (idx = 0; idx < 2; idx += bw)
          write_sb_ymode(bc, m->bmi[idy * 2 + idx].as_mode.first,
                         pc->fc.sb_ymode_prob);
    }
    write_uv_mode(bc, mi->uv_mode,
                  pc->fc.uv_mode_prob[mode]);
  } else {
    vp9_prob mv_ref_p[VP9_MVREFS - 1];

    vp9_mv_ref_probs(&cpi->common, mv_ref_p, mi->mb_mode_context[rf]);

#ifdef ENTROPY_STATS
    active_section = 3;
#endif

    // If segment skip is not enabled code the mode.
    if (!vp9_segfeature_active(xd, segment_id, SEG_LVL_SKIP)) {
      if (mi->sb_type >= BLOCK_SIZE_SB8X8)
        write_sb_mv_ref(bc, mode, mv_ref_p);
      vp9_accum_mv_refs(&cpi->common, mode, mi->mb_mode_context[rf]);
    }

    if (is_inter_mode(mode)) {
      if (cpi->common.mcomp_filter_type == SWITCHABLE) {
        write_token(bc, vp9_switchable_interp_tree,
                    vp9_get_pred_probs(&cpi->common, xd,
                                       PRED_SWITCHABLE_INTERP),
                    vp9_switchable_interp_encodings +
                    vp9_switchable_interp_map[mi->interp_filter]);
      } else {
        assert(mi->interp_filter == cpi->common.mcomp_filter_type);
      }
    }

    // does the feature use compound prediction or not
    // (if not specified at the frame/segment level)
    if (cpi->common.comp_pred_mode == HYBRID_PREDICTION) {
      vp9_write(bc, mi->second_ref_frame > INTRA_FRAME,
                vp9_get_pred_prob(pc, xd, PRED_COMP));
    }

    switch (mode) { /* new, split require MVs */
      case NEWMV:
#ifdef ENTROPY_STATS
        active_section = 5;
#endif
        vp9_encode_mv(bc,
                      &mi->mv[0].as_mv, &mi->best_mv.as_mv,
                      nmvc, xd->allow_high_precision_mv);

        if (mi->second_ref_frame > 0)
          vp9_encode_mv(bc,
                        &mi->mv[1].as_mv, &mi->best_second_mv.as_mv,
                        nmvc, xd->allow_high_precision_mv);
        break;
      case SPLITMV: {
        int j;
        MB_PREDICTION_MODE blockmode;
        int_mv blockmv;
        int bwl = b_width_log2(mi->sb_type), bw = 1 << bwl;
        int bhl = b_height_log2(mi->sb_type), bh = 1 << bhl;
        int idx, idy;
        for (idy = 0; idy < 2; idy += bh) {
          for (idx = 0; idx < 2; idx += bw) {
            j = idy * 2 + idx;
            blockmode = cpi->mb.partition_info->bmi[j].mode;
            blockmv = cpi->mb.partition_info->bmi[j].mv;
            write_sb_mv_ref(bc, blockmode, mv_ref_p);
            vp9_accum_mv_refs(&cpi->common, blockmode, mi->mb_mode_context[rf]);
            if (blockmode == NEWMV) {
#ifdef ENTROPY_STATS
              active_section = 11;
#endif
              vp9_encode_mv(bc, &blockmv.as_mv, &mi->best_mv.as_mv,
                            nmvc, xd->allow_high_precision_mv);

              if (mi->second_ref_frame > 0)
                vp9_encode_mv(bc,
                              &cpi->mb.partition_info->bmi[j].second_mv.as_mv,
                              &mi->best_second_mv.as_mv,
                              nmvc, xd->allow_high_precision_mv);
            }
          }
        }

#ifdef MODE_STATS
        ++count_mb_seg[mi->partitioning];
#endif
        break;
      }
      default:
        break;
    }
  }
}

static void write_mb_modes_kf(const VP9_COMP *cpi,
                              MODE_INFO *m,
                              vp9_writer *bc, int mi_row, int mi_col) {
  const VP9_COMMON *const c = &cpi->common;
  const MACROBLOCKD *const xd = &cpi->mb.e_mbd;
  const int ym = m->mbmi.mode;
  const int mis = c->mode_info_stride;
  const int segment_id = m->mbmi.segment_id;
  int skip_coeff;

  if (xd->update_mb_segmentation_map)
    write_mb_segid(bc, &m->mbmi, xd);

  if (vp9_segfeature_active(xd, segment_id, SEG_LVL_SKIP)) {
    skip_coeff = 1;
  } else {
    skip_coeff = m->mbmi.mb_skip_coeff;
    vp9_write(bc, skip_coeff, vp9_get_pred_prob(c, xd, PRED_MBSKIP));
  }

  if (m->mbmi.sb_type >= BLOCK_SIZE_SB8X8 && c->txfm_mode == TX_MODE_SELECT) {
    TX_SIZE sz = m->mbmi.txfm_size;
    // FIXME(rbultje) code ternary symbol once all experiments are merged
    vp9_write(bc, sz != TX_4X4, c->prob_tx[0]);
    if (m->mbmi.sb_type >= BLOCK_SIZE_MB16X16 && sz != TX_4X4) {
      vp9_write(bc, sz != TX_8X8, c->prob_tx[1]);
      if (m->mbmi.sb_type >= BLOCK_SIZE_SB32X32 && sz != TX_8X8)
        vp9_write(bc, sz != TX_16X16, c->prob_tx[2]);
    }
  }

  if (m->mbmi.sb_type >= BLOCK_SIZE_SB8X8) {
    const B_PREDICTION_MODE A = above_block_mode(m, 0, mis);
    const B_PREDICTION_MODE L = xd->left_available ?
                                 left_block_mode(m, 0) : DC_PRED;
    write_kf_bmode(bc, ym, c->kf_bmode_prob[A][L]);
  }

  if (m->mbmi.sb_type < BLOCK_SIZE_SB8X8) {
    int idx, idy;
    int bw = 1 << b_width_log2(m->mbmi.sb_type);
    int bh = 1 << b_height_log2(m->mbmi.sb_type);
    for (idy = 0; idy < 2; idy += bh) {
      for (idx = 0; idx < 2; idx += bw) {
        int i = idy * 2 + idx;
        const B_PREDICTION_MODE A = above_block_mode(m, i, mis);
        const B_PREDICTION_MODE L = (xd->left_available || idx) ?
                                     left_block_mode(m, i) : DC_PRED;
        write_kf_bmode(bc, m->bmi[i].as_mode.first,
                       c->kf_bmode_prob[A][L]);
      }
    }
  }

  write_uv_mode(bc, m->mbmi.uv_mode, c->kf_uv_mode_prob[ym]);
}

static void write_modes_b(VP9_COMP *cpi, MODE_INFO *m, vp9_writer *bc,
                          TOKENEXTRA **tok, TOKENEXTRA *tok_end,
                          int mi_row, int mi_col) {
  VP9_COMMON *const cm = &cpi->common;
  MACROBLOCKD *const xd = &cpi->mb.e_mbd;

  if (m->mbmi.sb_type < BLOCK_SIZE_SB8X8)
    if (xd->ab_index > 0)
      return;
  xd->mode_info_context = m;
  set_mi_row_col(&cpi->common, xd, mi_row,
                 1 << mi_height_log2(m->mbmi.sb_type),
                 mi_col, 1 << mi_width_log2(m->mbmi.sb_type));
  if (cm->frame_type == KEY_FRAME) {
    write_mb_modes_kf(cpi, m, bc, mi_row, mi_col);
#ifdef ENTROPY_STATS
    active_section = 8;
#endif
  } else {
    pack_inter_mode_mvs(cpi, m, bc, mi_row, mi_col);
#ifdef ENTROPY_STATS
    active_section = 1;
#endif
  }

  assert(*tok < tok_end);
  pack_mb_tokens(bc, tok, tok_end);
}

static void write_modes_sb(VP9_COMP *cpi, MODE_INFO *m, vp9_writer *bc,
                           TOKENEXTRA **tok, TOKENEXTRA *tok_end,
                           int mi_row, int mi_col,
                           BLOCK_SIZE_TYPE bsize) {
  VP9_COMMON *const cm = &cpi->common;
  MACROBLOCKD *xd = &cpi->mb.e_mbd;
  const int mis = cm->mode_info_stride;
  int bwl, bhl;
  int bsl = b_width_log2(bsize);
  int bs = (1 << bsl) / 4;  // mode_info step for subsize
  int n;
  PARTITION_TYPE partition;
  BLOCK_SIZE_TYPE subsize;

  if (mi_row >= cm->mi_rows || mi_col >= cm->mi_cols)
    return;

  bwl = b_width_log2(m->mbmi.sb_type);
  bhl = b_height_log2(m->mbmi.sb_type);

  // parse the partition type
  if ((bwl == bsl) && (bhl == bsl))
    partition = PARTITION_NONE;
  else if ((bwl == bsl) && (bhl < bsl))
    partition = PARTITION_HORZ;
  else if ((bwl < bsl) && (bhl == bsl))
    partition = PARTITION_VERT;
  else if ((bwl < bsl) && (bhl < bsl))
    partition = PARTITION_SPLIT;
  else
    assert(0);

  if (bsize < BLOCK_SIZE_SB8X8)
    if (xd->ab_index > 0)
      return;

  if (bsize >= BLOCK_SIZE_SB8X8) {
    int pl;
    xd->left_seg_context = cm->left_seg_context + (mi_row & MI_MASK);
    xd->above_seg_context = cm->above_seg_context + mi_col;
    pl = partition_plane_context(xd, bsize);
    // encode the partition information
    write_token(bc, vp9_partition_tree, cm->fc.partition_prob[pl],
                vp9_partition_encodings + partition);
  }

  subsize = get_subsize(bsize, partition);
  *(get_sb_index(xd, subsize)) = 0;

  switch (partition) {
    case PARTITION_NONE:
      write_modes_b(cpi, m, bc, tok, tok_end, mi_row, mi_col);
      break;
    case PARTITION_HORZ:
      write_modes_b(cpi, m, bc, tok, tok_end, mi_row, mi_col);
      *(get_sb_index(xd, subsize)) = 1;
      if ((mi_row + bs) < cm->mi_rows)
        write_modes_b(cpi, m + bs * mis, bc, tok, tok_end, mi_row + bs, mi_col);
      break;
    case PARTITION_VERT:
      write_modes_b(cpi, m, bc, tok, tok_end, mi_row, mi_col);
      *(get_sb_index(xd, subsize)) = 1;
      if ((mi_col + bs) < cm->mi_cols)
        write_modes_b(cpi, m + bs, bc, tok, tok_end, mi_row, mi_col + bs);
      break;
    case PARTITION_SPLIT:
      for (n = 0; n < 4; n++) {
        int j = n >> 1, i = n & 0x01;
        *(get_sb_index(xd, subsize)) = n;
        write_modes_sb(cpi, m + j * bs * mis + i * bs, bc, tok, tok_end,
                       mi_row + j * bs, mi_col + i * bs, subsize);
      }
      break;
    default:
      assert(0);
  }

  // update partition context
  if (bsize >= BLOCK_SIZE_SB8X8 &&
      (bsize == BLOCK_SIZE_SB8X8 || partition != PARTITION_SPLIT)) {
    set_partition_seg_context(cm, xd, mi_row, mi_col);
    update_partition_context(xd, subsize, bsize);
  }
}

static void write_modes(VP9_COMP *cpi, vp9_writer* const bc,
                        TOKENEXTRA **tok, TOKENEXTRA *tok_end) {
  VP9_COMMON *const c = &cpi->common;
  const int mis = c->mode_info_stride;
  MODE_INFO *m, *m_ptr = c->mi;
  int mi_row, mi_col;

  m_ptr += c->cur_tile_mi_col_start + c->cur_tile_mi_row_start * mis;
  vpx_memset(c->above_seg_context, 0, sizeof(PARTITION_CONTEXT) *
             mi_cols_aligned_to_sb(c));

  for (mi_row = c->cur_tile_mi_row_start;
       mi_row < c->cur_tile_mi_row_end;
       mi_row += 8, m_ptr += 8 * mis) {
    m = m_ptr;
    vpx_memset(c->left_seg_context, 0, sizeof(c->left_seg_context));
    for (mi_col = c->cur_tile_mi_col_start;
         mi_col < c->cur_tile_mi_col_end;
         mi_col += 64 / MI_SIZE, m += 64 / MI_SIZE)
      write_modes_sb(cpi, m, bc, tok, tok_end, mi_row, mi_col,
                     BLOCK_SIZE_SB64X64);
  }
}

/* This function is used for debugging probability trees. */
static void print_prob_tree(vp9_coeff_probs *coef_probs, int block_types) {
  /* print coef probability tree */
  int i, j, k, l, m;
  FILE *f = fopen("enc_tree_probs.txt", "a");
  fprintf(f, "{\n");
  for (i = 0; i < block_types; i++) {
    fprintf(f, "  {\n");
    for (j = 0; j < REF_TYPES; ++j) {
      fprintf(f, "  {\n");
      for (k = 0; k < COEF_BANDS; k++) {
        fprintf(f, "    {\n");
        for (l = 0; l < PREV_COEF_CONTEXTS; l++) {
          fprintf(f, "      {");
          for (m = 0; m < ENTROPY_NODES; m++) {
            fprintf(f, "%3u, ",
                    (unsigned int)(coef_probs[i][j][k][l][m]));
          }
        }
        fprintf(f, " }\n");
      }
      fprintf(f, "    }\n");
    }
    fprintf(f, "  }\n");
  }
  fprintf(f, "}\n");
  fclose(f);
}

static void build_tree_distribution(vp9_coeff_probs *coef_probs,
                                    vp9_coeff_count *coef_counts,
                                    unsigned int (*eob_branch_ct)[REF_TYPES]
                                                                 [COEF_BANDS]
                                                          [PREV_COEF_CONTEXTS],
#ifdef ENTROPY_STATS
                                    VP9_COMP *cpi,
                                    vp9_coeff_accum *context_counters,
#endif
                                    vp9_coeff_stats *coef_branch_ct,
                                    int block_types) {
  int i, j, k, l;
#ifdef ENTROPY_STATS
  int t = 0;
#endif

  for (i = 0; i < block_types; ++i) {
    for (j = 0; j < REF_TYPES; ++j) {
      for (k = 0; k < COEF_BANDS; ++k) {
        for (l = 0; l < PREV_COEF_CONTEXTS; ++l) {
          if (l >= 3 && k == 0)
            continue;
          vp9_tree_probs_from_distribution(vp9_coef_tree,
                                           coef_probs[i][j][k][l],
                                           coef_branch_ct[i][j][k][l],
                                           coef_counts[i][j][k][l], 0);
          coef_branch_ct[i][j][k][l][0][1] = eob_branch_ct[i][j][k][l] -
                                             coef_branch_ct[i][j][k][l][0][0];
          coef_probs[i][j][k][l][0] =
              get_binary_prob(coef_branch_ct[i][j][k][l][0][0],
                              coef_branch_ct[i][j][k][l][0][1]);
#ifdef ENTROPY_STATS
          if (!cpi->dummy_packing) {
            for (t = 0; t < MAX_ENTROPY_TOKENS; ++t)
              context_counters[i][j][k][l][t] += coef_counts[i][j][k][l][t];
            context_counters[i][j][k][l][MAX_ENTROPY_TOKENS] +=
                eob_branch_ct[i][j][k][l];
          }
#endif
        }
      }
    }
  }
}

static void build_coeff_contexts(VP9_COMP *cpi) {
  build_tree_distribution(cpi->frame_coef_probs_4x4,
                          cpi->coef_counts_4x4,
                          cpi->common.fc.eob_branch_counts[TX_4X4],
#ifdef ENTROPY_STATS
                          cpi, context_counters_4x4,
#endif
                          cpi->frame_branch_ct_4x4, BLOCK_TYPES);
  build_tree_distribution(cpi->frame_coef_probs_8x8,
                          cpi->coef_counts_8x8,
                          cpi->common.fc.eob_branch_counts[TX_8X8],
#ifdef ENTROPY_STATS
                          cpi, context_counters_8x8,
#endif
                          cpi->frame_branch_ct_8x8, BLOCK_TYPES);
  build_tree_distribution(cpi->frame_coef_probs_16x16,
                          cpi->coef_counts_16x16,
                          cpi->common.fc.eob_branch_counts[TX_16X16],
#ifdef ENTROPY_STATS
                          cpi, context_counters_16x16,
#endif
                          cpi->frame_branch_ct_16x16, BLOCK_TYPES);
  build_tree_distribution(cpi->frame_coef_probs_32x32,
                          cpi->coef_counts_32x32,
                          cpi->common.fc.eob_branch_counts[TX_32X32],
#ifdef ENTROPY_STATS
                          cpi, context_counters_32x32,
#endif
                          cpi->frame_branch_ct_32x32, BLOCK_TYPES);
}

static void update_coef_probs_common(
    vp9_writer* const bc,
    VP9_COMP *cpi,
#ifdef ENTROPY_STATS
    vp9_coeff_stats *tree_update_hist,
#endif
    vp9_coeff_probs *new_frame_coef_probs,
    vp9_coeff_probs_model *old_frame_coef_probs,
    vp9_coeff_stats *frame_branch_ct,
    TX_SIZE tx_size) {
  int i, j, k, l, t;
  int update[2] = {0, 0};
  int savings;

  const int entropy_nodes_update = UNCONSTRAINED_NODES;
  // vp9_prob bestupd = find_coef_update_prob(cpi);

  const int tstart = 0;
  /* dry run to see if there is any udpate at all needed */
  savings = 0;
  for (i = 0; i < BLOCK_TYPES; ++i) {
    for (j = 0; j < REF_TYPES; ++j) {
      for (k = 0; k < COEF_BANDS; ++k) {
        // int prev_coef_savings[ENTROPY_NODES] = {0};
        for (l = 0; l < PREV_COEF_CONTEXTS; ++l) {
          for (t = tstart; t < entropy_nodes_update; ++t) {
            vp9_prob newp = new_frame_coef_probs[i][j][k][l][t];
            const vp9_prob oldp = old_frame_coef_probs[i][j][k][l][t];
            const vp9_prob upd = vp9_coef_update_prob[t];
            int s;  // = prev_coef_savings[t];
            int u = 0;

            if (l >= 3 && k == 0)
              continue;
            if (t == PIVOT_NODE)
              s = prob_diff_update_savings_search_model(
                  frame_branch_ct[i][j][k][l][0],
                  old_frame_coef_probs[i][j][k][l], &newp, upd, i, j);
            else
              s = prob_diff_update_savings_search(
                  frame_branch_ct[i][j][k][l][t], oldp, &newp, upd);
            if (s > 0 && newp != oldp)
              u = 1;
            if (u)
              savings += s - (int)(vp9_cost_zero(upd));
            else
              savings -= (int)(vp9_cost_zero(upd));
            update[u]++;
          }
        }
      }
    }
  }

  // printf("Update %d %d, savings %d\n", update[0], update[1], savings);
  /* Is coef updated at all */
  if (update[1] == 0 || savings < 0) {
    vp9_write_bit(bc, 0);
    return;
  }
  vp9_write_bit(bc, 1);
  for (i = 0; i < BLOCK_TYPES; ++i) {
    for (j = 0; j < REF_TYPES; ++j) {
      for (k = 0; k < COEF_BANDS; ++k) {
        // int prev_coef_savings[ENTROPY_NODES] = {0};
        for (l = 0; l < PREV_COEF_CONTEXTS; ++l) {
          // calc probs and branch cts for this frame only
          for (t = tstart; t < entropy_nodes_update; ++t) {
            vp9_prob newp = new_frame_coef_probs[i][j][k][l][t];
            vp9_prob *oldp = old_frame_coef_probs[i][j][k][l] + t;
            const vp9_prob upd = vp9_coef_update_prob[t];
            int s;  // = prev_coef_savings[t];
            int u = 0;
            if (l >= 3 && k == 0)
              continue;

            if (t == PIVOT_NODE)
              s = prob_diff_update_savings_search_model(
                  frame_branch_ct[i][j][k][l][0],
                  old_frame_coef_probs[i][j][k][l], &newp, upd, i, j);
            else
              s = prob_diff_update_savings_search(
                  frame_branch_ct[i][j][k][l][t],
                  *oldp, &newp, upd);
            if (s > 0 && newp != *oldp)
              u = 1;
            vp9_write(bc, u, upd);
#ifdef ENTROPY_STATS
            if (!cpi->dummy_packing)
              ++tree_update_hist[i][j][k][l][t][u];
#endif
            if (u) {
              /* send/use new probability */
              write_prob_diff_update(bc, newp, *oldp);
              *oldp = newp;
            }
          }
        }
      }
    }
  }
}

static void update_coef_probs(VP9_COMP* const cpi, vp9_writer* const bc) {
  vp9_clear_system_state();

  // Build the cofficient contexts based on counts collected in encode loop
  build_coeff_contexts(cpi);

  update_coef_probs_common(bc,
                           cpi,
#ifdef ENTROPY_STATS
                           tree_update_hist_4x4,
#endif
                           cpi->frame_coef_probs_4x4,
                           cpi->common.fc.coef_probs_4x4,
                           cpi->frame_branch_ct_4x4,
                           TX_4X4);

  /* do not do this if not even allowed */
  if (cpi->common.txfm_mode != ONLY_4X4) {
    update_coef_probs_common(bc,
                             cpi,
#ifdef ENTROPY_STATS
                             tree_update_hist_8x8,
#endif
                             cpi->frame_coef_probs_8x8,
                             cpi->common.fc.coef_probs_8x8,
                             cpi->frame_branch_ct_8x8,
                             TX_8X8);
  }

  if (cpi->common.txfm_mode > ALLOW_8X8) {
    update_coef_probs_common(bc,
                             cpi,
#ifdef ENTROPY_STATS
                             tree_update_hist_16x16,
#endif
                             cpi->frame_coef_probs_16x16,
                             cpi->common.fc.coef_probs_16x16,
                             cpi->frame_branch_ct_16x16,
                             TX_16X16);
  }

  if (cpi->common.txfm_mode > ALLOW_16X16) {
    update_coef_probs_common(bc,
                             cpi,
#ifdef ENTROPY_STATS
                             tree_update_hist_32x32,
#endif
                             cpi->frame_coef_probs_32x32,
                             cpi->common.fc.coef_probs_32x32,
                             cpi->frame_branch_ct_32x32,
                             TX_32X32);
  }
}

static void decide_kf_ymode_entropy(VP9_COMP *cpi) {
  int mode_cost[MB_MODE_COUNT];
  int bestcost = INT_MAX;
  int bestindex = 0;
  int i, j;

  for (i = 0; i < 8; i++) {
    int cost = 0;

    vp9_cost_tokens(mode_cost, cpi->common.kf_ymode_prob[i], vp9_kf_ymode_tree);

    for (j = 0; j < VP9_YMODES; j++)
      cost += mode_cost[j] * cpi->ymode_count[j];

    vp9_cost_tokens(mode_cost, cpi->common.sb_kf_ymode_prob[i],
                    vp9_sb_ymode_tree);
    for (j = 0; j < VP9_I32X32_MODES; j++)
      cost += mode_cost[j] * cpi->sb_ymode_count[j];

    if (cost < bestcost) {
      bestindex = i;
      bestcost = cost;
    }
  }
  cpi->common.kf_ymode_probs_index = bestindex;

}
static void segment_reference_frames(VP9_COMP *cpi) {
  VP9_COMMON *oci = &cpi->common;
  MODE_INFO *mi = oci->mi;
  int ref[MAX_MB_SEGMENTS] = {0};
  int i, j;
  int mb_index = 0;
  MACROBLOCKD *const xd = &cpi->mb.e_mbd;

  for (i = 0; i < oci->mb_rows; i++) {
    for (j = 0; j < oci->mb_cols; j++, mb_index++)
      ref[mi[mb_index].mbmi.segment_id] |= (1 << mi[mb_index].mbmi.ref_frame);
    mb_index++;
  }
  for (i = 0; i < MAX_MB_SEGMENTS; i++) {
    vp9_enable_segfeature(xd, i, SEG_LVL_REF_FRAME);
    vp9_set_segdata(xd, i, SEG_LVL_REF_FRAME, ref[i]);
  }
}

static void encode_loopfilter(VP9_COMMON *pc, MACROBLOCKD *xd, vp9_writer *w) {
  int i;

  // Encode the loop filter level and type
  vp9_write_literal(w, pc->filter_level, 6);
  vp9_write_literal(w, pc->sharpness_level, 3);
#if CONFIG_LOOP_DERING
  if (pc->dering_enabled) {
    vp9_write_bit(w, 1);
    vp9_write_literal(w, pc->dering_enabled - 1, 4);
  } else {
    vp9_write_bit(w, 0);
  }
#endif

  // Write out loop filter deltas applied at the MB level based on mode or
  // ref frame (if they are enabled).
  vp9_write_bit(w, xd->mode_ref_lf_delta_enabled);

  if (xd->mode_ref_lf_delta_enabled) {
    // Do the deltas need to be updated
    vp9_write_bit(w, xd->mode_ref_lf_delta_update);
    if (xd->mode_ref_lf_delta_update) {
      // Send update
      for (i = 0; i < MAX_REF_LF_DELTAS; i++) {
        const int delta = xd->ref_lf_deltas[i];

        // Frame level data
        if (delta != xd->last_ref_lf_deltas[i]) {
          xd->last_ref_lf_deltas[i] = delta;
          vp9_write_bit(w, 1);

          if (delta > 0) {
            vp9_write_literal(w, delta & 0x3F, 6);
            vp9_write_bit(w, 0);  // sign
          } else {
            assert(delta < 0);
            vp9_write_literal(w, (-delta) & 0x3F, 6);
            vp9_write_bit(w, 1);  // sign
          }
        } else {
          vp9_write_bit(w, 0);
        }
      }

      // Send update
      for (i = 0; i < MAX_MODE_LF_DELTAS; i++) {
        const int delta = xd->mode_lf_deltas[i];
        if (delta != xd->last_mode_lf_deltas[i]) {
          xd->last_mode_lf_deltas[i] = delta;
          vp9_write_bit(w, 1);

          if (delta > 0) {
            vp9_write_literal(w, delta & 0x3F, 6);
            vp9_write_bit(w, 0);  // sign
          } else {
            assert(delta < 0);
            vp9_write_literal(w, (-delta) & 0x3F, 6);
            vp9_write_bit(w, 1);  // sign
          }
        } else {
          vp9_write_bit(w, 0);
        }
      }
    }
  }
}

static void put_delta_q(vp9_writer *bc, int delta_q) {
  if (delta_q != 0) {
    vp9_write_bit(bc, 1);
    vp9_write_literal(bc, abs(delta_q), 4);
    vp9_write_bit(bc, delta_q < 0);
  } else {
    vp9_write_bit(bc, 0);
  }
}

static void encode_quantization(VP9_COMMON *pc, vp9_writer *w) {
  vp9_write_literal(w, pc->base_qindex, QINDEX_BITS);
  put_delta_q(w, pc->y_dc_delta_q);
  put_delta_q(w, pc->uv_dc_delta_q);
  put_delta_q(w, pc->uv_ac_delta_q);
}


static void encode_segmentation(VP9_COMP *cpi, vp9_writer *w) {
  int i, j;
  VP9_COMMON *const pc = &cpi->common;
  MACROBLOCKD *const xd = &cpi->mb.e_mbd;

  vp9_write_bit(w, xd->segmentation_enabled);
  if (!xd->segmentation_enabled)
    return;

  // Segmentation map
  vp9_write_bit(w, xd->update_mb_segmentation_map);
#if CONFIG_IMPLICIT_SEGMENTATION
  vp9_write_bit(w, xd->allow_implicit_segment_update);
#endif
  if (xd->update_mb_segmentation_map) {
    // Select the coding strategy (temporal or spatial)
    vp9_choose_segmap_coding_method(cpi);
    // Write out probabilities used to decode unpredicted  macro-block segments
    for (i = 0; i < MB_SEG_TREE_PROBS; i++) {
      const int prob = xd->mb_segment_tree_probs[i];
      if (prob != MAX_PROB) {
        vp9_write_bit(w, 1);
        vp9_write_prob(w, prob);
      } else {
        vp9_write_bit(w, 0);
      }
    }

    // Write out the chosen coding method.
    vp9_write_bit(w, pc->temporal_update);
    if (pc->temporal_update) {
      for (i = 0; i < PREDICTION_PROBS; i++) {
        const int prob = pc->segment_pred_probs[i];
        if (prob != MAX_PROB) {
          vp9_write_bit(w, 1);
          vp9_write_prob(w, prob);
        } else {
          vp9_write_bit(w, 0);
        }
      }
    }
  }

  // Segmentation data
  vp9_write_bit(w, xd->update_mb_segmentation_data);
  // segment_reference_frames(cpi);
  if (xd->update_mb_segmentation_data) {
    vp9_write_bit(w, xd->mb_segment_abs_delta);

    for (i = 0; i < MAX_MB_SEGMENTS; i++) {
      for (j = 0; j < SEG_LVL_MAX; j++) {
        const int data = vp9_get_segdata(xd, i, j);
        const int data_max = vp9_seg_feature_data_max(j);

        if (vp9_segfeature_active(xd, i, j)) {
          vp9_write_bit(w, 1);

          if (vp9_is_segfeature_signed(j)) {
            if (data < 0) {
              vp9_encode_unsigned_max(w, -data, data_max);
              vp9_write_bit(w, 1);
            } else {
              vp9_encode_unsigned_max(w, data, data_max);
              vp9_write_bit(w, 0);
            }
          } else {
            vp9_encode_unsigned_max(w, data, data_max);
          }
        } else {
          vp9_write_bit(w, 0);
        }
      }
    }
  }
}

void vp9_pack_bitstream(VP9_COMP *cpi, uint8_t *dest, unsigned long *size) {
  int i;
  VP9_COMMON *const pc = &cpi->common;
  MACROBLOCKD *const xd = &cpi->mb.e_mbd;
  vp9_writer header_bc, residual_bc;
  int extra_bytes_packed = 0;
  uint8_t *cx_data = dest;

  cx_data += HEADER_SIZE_IN_BYTES;

#if defined(SECTIONBITS_OUTPUT)
  Sectionbits[active_section = 1] += HEADER_SIZE_IN_BYTES * 8 * 256;
#endif

  compute_update_table();

  if (pc->frame_type == KEY_FRAME) {
    // Start / synch code
    cx_data[0] = 0x49;
    cx_data[1] = 0x83;
    cx_data[2] = 0x42;
    extra_bytes_packed += 3;
    cx_data += 3;
  }

  if (pc->width != pc->display_width || pc->height != pc->display_height) {
    write_le16(cx_data, pc->display_width);
    write_le16(cx_data + 2, pc->display_height);
    extra_bytes_packed += 4;
    cx_data += 4;
  }

  write_le16(cx_data, pc->width);
  write_le16(cx_data + 2, pc->height);
  extra_bytes_packed += 4;
  cx_data += 4;

  vp9_start_encode(&header_bc, cx_data);

  encode_loopfilter(pc, xd, &header_bc);

  encode_quantization(pc, &header_bc);

  // When there is a key frame all reference buffers are updated using the new key frame
  if (pc->frame_type != KEY_FRAME) {
    int refresh_mask;

    // Should the GF or ARF be updated using the transmitted frame or buffer
#if CONFIG_MULTIPLE_ARF
    if (!cpi->multi_arf_enabled && cpi->refresh_golden_frame &&
        !cpi->refresh_alt_ref_frame) {
#else
      if (cpi->refresh_golden_frame && !cpi->refresh_alt_ref_frame) {
#endif
      /* Preserve the previously existing golden frame and update the frame in
       * the alt ref slot instead. This is highly specific to the use of
       * alt-ref as a forward reference, and this needs to be generalized as
       * other uses are implemented (like RTC/temporal scaling)
       *
       * gld_fb_idx and alt_fb_idx need to be swapped for future frames, but
       * that happens in vp9_onyx_if.c:update_reference_frames() so that it can
       * be done outside of the recode loop.
       */
      refresh_mask = (cpi->refresh_last_frame << cpi->lst_fb_idx) |
                     (cpi->refresh_golden_frame << cpi->alt_fb_idx);
    } else {
      int arf_idx = cpi->alt_fb_idx;
#if CONFIG_MULTIPLE_ARF
      // Determine which ARF buffer to use to encode this ARF frame.
      if (cpi->multi_arf_enabled) {
        int sn = cpi->sequence_number;
        arf_idx = (cpi->frame_coding_order[sn] < 0) ?
            cpi->arf_buffer_idx[sn + 1] :
            cpi->arf_buffer_idx[sn];
      }
#endif
      refresh_mask = (cpi->refresh_last_frame << cpi->lst_fb_idx) |
                     (cpi->refresh_golden_frame << cpi->gld_fb_idx) |
                     (cpi->refresh_alt_ref_frame << arf_idx);
    }

    vp9_write_literal(&header_bc, refresh_mask, NUM_REF_FRAMES);
    vp9_write_literal(&header_bc, cpi->lst_fb_idx, NUM_REF_FRAMES_LG2);
    vp9_write_literal(&header_bc, cpi->gld_fb_idx, NUM_REF_FRAMES_LG2);
    vp9_write_literal(&header_bc, cpi->alt_fb_idx, NUM_REF_FRAMES_LG2);

    // Indicate the sign bias for each reference frame buffer.
    for (i = 0; i < ALLOWED_REFS_PER_FRAME; ++i)
      vp9_write_bit(&header_bc, pc->ref_frame_sign_bias[LAST_FRAME + i]);

    // Signal whether to allow high MV precision
    vp9_write_bit(&header_bc, (xd->allow_high_precision_mv) ? 1 : 0);
    if (pc->mcomp_filter_type == SWITCHABLE) {
      /* Check to see if only one of the filters is actually used */
      int count[VP9_SWITCHABLE_FILTERS];
      int i, j, c = 0;
      for (i = 0; i < VP9_SWITCHABLE_FILTERS; ++i) {
        count[i] = 0;
        for (j = 0; j <= VP9_SWITCHABLE_FILTERS; ++j)
          count[i] += cpi->switchable_interp_count[j][i];
        c += (count[i] > 0);
      }
      if (c == 1) {
        /* Only one filter is used. So set the filter at frame level */
        for (i = 0; i < VP9_SWITCHABLE_FILTERS; ++i) {
          if (count[i]) {
            pc->mcomp_filter_type = vp9_switchable_interp[i];
            break;
          }
        }
      }
    }
    // Signal the type of subpel filter to use
    vp9_write_bit(&header_bc, (pc->mcomp_filter_type == SWITCHABLE));
    if (pc->mcomp_filter_type != SWITCHABLE)
      vp9_write_literal(&header_bc, (pc->mcomp_filter_type), 2);
  }

  vp9_write_literal(&header_bc, pc->frame_context_idx,
                    NUM_FRAME_CONTEXTS_LG2);

#ifdef ENTROPY_STATS
  if (pc->frame_type == INTER_FRAME)
    active_section = 0;
  else
    active_section = 7;
#endif

  encode_segmentation(cpi, &header_bc);

  // Encode the common prediction model status flag probability updates for
  // the reference frame
  update_refpred_stats(cpi);
  if (pc->frame_type != KEY_FRAME) {
    for (i = 0; i < PREDICTION_PROBS; i++) {
      if (cpi->ref_pred_probs_update[i]) {
        vp9_write_bit(&header_bc, 1);
        vp9_write_prob(&header_bc, pc->ref_pred_probs[i]);
      } else {
        vp9_write_bit(&header_bc, 0);
      }
    }
  }

  if (cpi->mb.e_mbd.lossless) {
    pc->txfm_mode = ONLY_4X4;
  } else {
    if (pc->txfm_mode == TX_MODE_SELECT) {
      pc->prob_tx[0] = get_prob(cpi->txfm_count_32x32p[TX_4X4] +
                                cpi->txfm_count_16x16p[TX_4X4] +
                                cpi->txfm_count_8x8p[TX_4X4],
                                cpi->txfm_count_32x32p[TX_4X4] +
                                cpi->txfm_count_32x32p[TX_8X8] +
                                cpi->txfm_count_32x32p[TX_16X16] +
                                cpi->txfm_count_32x32p[TX_32X32] +
                                cpi->txfm_count_16x16p[TX_4X4] +
                                cpi->txfm_count_16x16p[TX_8X8] +
                                cpi->txfm_count_16x16p[TX_16X16] +
                                cpi->txfm_count_8x8p[TX_4X4] +
                                cpi->txfm_count_8x8p[TX_8X8]);
      pc->prob_tx[1] = get_prob(cpi->txfm_count_32x32p[TX_8X8] +
                                cpi->txfm_count_16x16p[TX_8X8],
                                cpi->txfm_count_32x32p[TX_8X8] +
                                cpi->txfm_count_32x32p[TX_16X16] +
                                cpi->txfm_count_32x32p[TX_32X32] +
                                cpi->txfm_count_16x16p[TX_8X8] +
                                cpi->txfm_count_16x16p[TX_16X16]);
      pc->prob_tx[2] = get_prob(cpi->txfm_count_32x32p[TX_16X16],
                                cpi->txfm_count_32x32p[TX_16X16] +
                                cpi->txfm_count_32x32p[TX_32X32]);
    } else {
      pc->prob_tx[0] = 128;
      pc->prob_tx[1] = 128;
      pc->prob_tx[2] = 128;
    }
    vp9_write_literal(&header_bc, pc->txfm_mode <= 3 ? pc->txfm_mode : 3, 2);
    if (pc->txfm_mode > ALLOW_16X16) {
      vp9_write_bit(&header_bc, pc->txfm_mode == TX_MODE_SELECT);
    }
    if (pc->txfm_mode == TX_MODE_SELECT) {
      vp9_write_prob(&header_bc, pc->prob_tx[0]);
      vp9_write_prob(&header_bc, pc->prob_tx[1]);
      vp9_write_prob(&header_bc, pc->prob_tx[2]);
    }
  }

  // If appropriate update the inter mode probability context and code the
  // changes in the bitstream.
  if (pc->frame_type != KEY_FRAME) {
    int i, j;
    int new_context[INTER_MODE_CONTEXTS][4];
    if (!cpi->dummy_packing) {
      update_inter_mode_probs(pc, new_context);
    } else {
      // In dummy pack assume context unchanged.
      vpx_memcpy(new_context, pc->fc.vp9_mode_contexts,
                 sizeof(pc->fc.vp9_mode_contexts));
    }

    for (i = 0; i < INTER_MODE_CONTEXTS; i++) {
      for (j = 0; j < 4; j++) {
        if (new_context[i][j] != pc->fc.vp9_mode_contexts[i][j]) {
          vp9_write(&header_bc, 1, 252);
          vp9_write_prob(&header_bc, new_context[i][j]);

          // Only update the persistent copy if this is the "real pack"
          if (!cpi->dummy_packing) {
            pc->fc.vp9_mode_contexts[i][j] = new_context[i][j];
          }
        } else {
          vp9_write(&header_bc, 0, 252);
        }
      }
    }
  }

  vp9_clear_system_state();  // __asm emms;

  vp9_copy(cpi->common.fc.pre_coef_probs_4x4,
           cpi->common.fc.coef_probs_4x4);
  vp9_copy(cpi->common.fc.pre_coef_probs_8x8,
           cpi->common.fc.coef_probs_8x8);
  vp9_copy(cpi->common.fc.pre_coef_probs_16x16,
           cpi->common.fc.coef_probs_16x16);
  vp9_copy(cpi->common.fc.pre_coef_probs_32x32,
           cpi->common.fc.coef_probs_32x32);

  vp9_copy(cpi->common.fc.pre_sb_ymode_prob, cpi->common.fc.sb_ymode_prob);
  vp9_copy(cpi->common.fc.pre_ymode_prob, cpi->common.fc.ymode_prob);
  vp9_copy(cpi->common.fc.pre_uv_mode_prob, cpi->common.fc.uv_mode_prob);
  vp9_copy(cpi->common.fc.pre_bmode_prob, cpi->common.fc.bmode_prob);
  vp9_copy(cpi->common.fc.pre_partition_prob, cpi->common.fc.partition_prob);
  cpi->common.fc.pre_nmvc = cpi->common.fc.nmvc;
  vp9_zero(cpi->common.fc.mv_ref_ct);

  update_coef_probs(cpi, &header_bc);

#ifdef ENTROPY_STATS
  active_section = 2;
#endif

  vp9_update_skip_probs(cpi);
  for (i = 0; i < MBSKIP_CONTEXTS; ++i) {
    vp9_write_prob(&header_bc, pc->mbskip_pred_probs[i]);
  }

  if (pc->frame_type == KEY_FRAME) {
    if (!pc->kf_ymode_probs_update) {
      vp9_write_literal(&header_bc, pc->kf_ymode_probs_index, 3);
    }
  } else {
    // Update the probabilities used to encode reference frame data
    update_ref_probs(cpi);

#ifdef ENTROPY_STATS
    active_section = 1;
#endif

    if (pc->mcomp_filter_type == SWITCHABLE)
      update_switchable_interp_probs(cpi, &header_bc);

    vp9_write_prob(&header_bc, pc->prob_intra_coded);
    vp9_write_prob(&header_bc, pc->prob_last_coded);
    vp9_write_prob(&header_bc, pc->prob_gf_coded);

    {
      const int comp_pred_mode = cpi->common.comp_pred_mode;
      const int use_compound_pred = (comp_pred_mode != SINGLE_PREDICTION_ONLY);
      const int use_hybrid_pred = (comp_pred_mode == HYBRID_PREDICTION);

      vp9_write_bit(&header_bc, use_compound_pred);
      if (use_compound_pred) {
        vp9_write_bit(&header_bc, use_hybrid_pred);
        if (use_hybrid_pred) {
          for (i = 0; i < COMP_PRED_CONTEXTS; i++) {
            pc->prob_comppred[i] = get_binary_prob(cpi->single_pred_count[i],
                                                   cpi->comp_pred_count[i]);
            vp9_write_prob(&header_bc, pc->prob_comppred[i]);
          }
        }
      }
    }
    update_mbintra_mode_probs(cpi, &header_bc);

    for (i = 0; i < NUM_PARTITION_CONTEXTS; ++i) {
      vp9_prob Pnew[PARTITION_TYPES - 1];
      unsigned int bct[PARTITION_TYPES - 1][2];
      update_mode(&header_bc, PARTITION_TYPES, vp9_partition_encodings,
                  vp9_partition_tree, Pnew, pc->fc.partition_prob[i], bct,
                  (unsigned int *)cpi->partition_count[i]);
    }

    vp9_write_nmv_probs(cpi, xd->allow_high_precision_mv, &header_bc);
  }

  /* tiling */
  {
    int min_log2_tiles, delta_log2_tiles, n_tile_bits, n;

    vp9_get_tile_n_bits(pc, &min_log2_tiles, &delta_log2_tiles);
    n_tile_bits = pc->log2_tile_columns - min_log2_tiles;
    for (n = 0; n < delta_log2_tiles; n++) {
      if (n_tile_bits--) {
        vp9_write_bit(&header_bc, 1);
      } else {
        vp9_write_bit(&header_bc, 0);
        break;
      }
    }
    vp9_write_bit(&header_bc, pc->log2_tile_rows != 0);
    if (pc->log2_tile_rows != 0)
      vp9_write_bit(&header_bc, pc->log2_tile_rows != 1);
  }

  vp9_stop_encode(&header_bc);

  /* update frame tag */
  {
    const int first_partition_length_in_bytes = header_bc.pos;
    int scaling = (pc->width != pc->display_width ||
                   pc->height != pc->display_height);

    struct vp9_write_bit_buffer wb = {dest, 0};

    assert(first_partition_length_in_bytes <= 0xffff);

    vp9_wb_write_bit(&wb, pc->frame_type);
    vp9_wb_write_literal(&wb, pc->version, 3);
    vp9_wb_write_bit(&wb, pc->show_frame);
    vp9_wb_write_bit(&wb, scaling);
    vp9_wb_write_bit(&wb, pc->subsampling_x);
    vp9_wb_write_bit(&wb, pc->subsampling_y);

    vp9_wb_write_bit(&wb, pc->clr_type);
    vp9_wb_write_bit(&wb, pc->error_resilient_mode);
    if (!pc->error_resilient_mode) {
      vp9_wb_write_bit(&wb, pc->refresh_frame_context);
      vp9_wb_write_bit(&wb, pc->frame_parallel_decoding_mode);
    }


    vp9_wb_write_literal(&wb, first_partition_length_in_bytes, 16);
  }

  *size = HEADER_SIZE_IN_BYTES + extra_bytes_packed + header_bc.pos;

  if (pc->frame_type == KEY_FRAME) {
    decide_kf_ymode_entropy(cpi);
  } else {
    /* This is not required if the counts in cpi are consistent with the
     * final packing pass */
    // if (!cpi->dummy_packing) vp9_zero(cpi->NMVcount);
  }

  {
    int tile_row, tile_col, total_size = 0;
    unsigned char *data_ptr = cx_data + header_bc.pos;
    TOKENEXTRA *tok[1 << 6], *tok_end;

    tok[0] = cpi->tok;
    for (tile_col = 1; tile_col < pc->tile_columns; tile_col++)
      tok[tile_col] = tok[tile_col - 1] + cpi->tok_count[tile_col - 1];

    for (tile_row = 0; tile_row < pc->tile_rows; tile_row++) {
      vp9_get_tile_row_offsets(pc, tile_row);
      tok_end = cpi->tok + cpi->tok_count[0];
      for (tile_col = 0; tile_col < pc->tile_columns;
           tile_col++, tok_end += cpi->tok_count[tile_col]) {
        vp9_get_tile_col_offsets(pc, tile_col);

        if (tile_col < pc->tile_columns - 1 || tile_row < pc->tile_rows - 1)
          vp9_start_encode(&residual_bc, data_ptr + total_size + 4);
        else
          vp9_start_encode(&residual_bc, data_ptr + total_size);
        write_modes(cpi, &residual_bc, &tok[tile_col], tok_end);
        vp9_stop_encode(&residual_bc);
        if (tile_col < pc->tile_columns - 1 || tile_row < pc->tile_rows - 1) {
          // size of this tile
          write_le32(data_ptr + total_size, residual_bc.pos);
          total_size += 4;
        }

        total_size += residual_bc.pos;
      }
    }

    assert((unsigned int)(tok[0] - cpi->tok) == cpi->tok_count[0]);
    for (tile_col = 1; tile_col < pc->tile_columns; tile_col++)
      assert((unsigned int)(tok[tile_col] - tok[tile_col - 1]) ==
                  cpi->tok_count[tile_col]);

    *size += total_size;
  }
}

#ifdef ENTROPY_STATS
static void print_tree_update_for_type(FILE *f,
                                       vp9_coeff_stats *tree_update_hist,
                                       int block_types, const char *header) {
  int i, j, k, l, m;

  fprintf(f, "const vp9_coeff_prob %s = {\n", header);
  for (i = 0; i < block_types; i++) {
    fprintf(f, "  { \n");
    for (j = 0; j < REF_TYPES; j++) {
      fprintf(f, "  { \n");
      for (k = 0; k < COEF_BANDS; k++) {
        fprintf(f, "    {\n");
        for (l = 0; l < PREV_COEF_CONTEXTS; l++) {
          fprintf(f, "      {");
          for (m = 0; m < ENTROPY_NODES; m++) {
            fprintf(f, "%3d, ",
                    get_binary_prob(tree_update_hist[i][j][k][l][m][0],
                                    tree_update_hist[i][j][k][l][m][1]));
          }
          fprintf(f, "},\n");
        }
        fprintf(f, "},\n");
      }
      fprintf(f, "    },\n");
    }
    fprintf(f, "  },\n");
  }
  fprintf(f, "};\n");
}

void print_tree_update_probs() {
  FILE *f = fopen("coefupdprob.h", "w");
  fprintf(f, "\n/* Update probabilities for token entropy tree. */\n\n");

  print_tree_update_for_type(f, tree_update_hist_4x4, BLOCK_TYPES,
                             "vp9_coef_update_probs_4x4[BLOCK_TYPES]");
  print_tree_update_for_type(f, tree_update_hist_8x8, BLOCK_TYPES,
                             "vp9_coef_update_probs_8x8[BLOCK_TYPES]");
  print_tree_update_for_type(f, tree_update_hist_16x16, BLOCK_TYPES,
                             "vp9_coef_update_probs_16x16[BLOCK_TYPES]");
  print_tree_update_for_type(f, tree_update_hist_32x32, BLOCK_TYPES,
                             "vp9_coef_update_probs_32x32[BLOCK_TYPES]");

  fclose(f);
  f = fopen("treeupdate.bin", "wb");
  fwrite(tree_update_hist_4x4, sizeof(tree_update_hist_4x4), 1, f);
  fwrite(tree_update_hist_8x8, sizeof(tree_update_hist_8x8), 1, f);
  fwrite(tree_update_hist_16x16, sizeof(tree_update_hist_16x16), 1, f);
  fwrite(tree_update_hist_32x32, sizeof(tree_update_hist_32x32), 1, f);
  fclose(f);
}
#endif
