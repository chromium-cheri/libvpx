
/*
 *  Copyright (c) 2012 The WebM project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include <limits.h>

#include "vp9/common/vp9_common.h"
#include "vp9/common/vp9_pred_common.h"
#include "vp9/common/vp9_seg_common.h"
#include "vp9/common/vp9_treecoder.h"

// TBD prediction functions for various bitstream signals

// Returns a context number for the given MB prediction signal
unsigned char vp9_get_pred_context(const VP9_COMMON *const cm,
                                   const MACROBLOCKD *const xd,
                                   PRED_ID pred_id) {
  int pred_context;
  const MODE_INFO *const mi = xd->mode_info_context;
  const MODE_INFO *const above_mi = mi - cm->mode_info_stride;
  const MODE_INFO *const left_mi = mi - 1;
  // Note:
  // The mode info data structure has a one element border above and to the
  // left of the entries correpsonding to real macroblocks.
  // The prediction flags in these dummy entries are initialised to 0.
  switch (pred_id) {
    case PRED_SEG_ID:
      pred_context = above_mi->mbmi.seg_id_predicted;
      if (xd->left_available)
        pred_context += left_mi->mbmi.seg_id_predicted;
      break;

    case PRED_REF:
      pred_context = above_mi->mbmi.ref_predicted;
      if (xd->left_available)
        pred_context += left_mi->mbmi.ref_predicted;
      break;

    case PRED_COMP:
      if (mi->mbmi.ref_frame == LAST_FRAME)
        pred_context = 0;
      else
        pred_context = 1;
      break;

    case PRED_MBSKIP:
      pred_context = above_mi->mbmi.mb_skip_coeff;
      if (xd->left_available)
        pred_context += left_mi->mbmi.mb_skip_coeff;
      break;

    case PRED_SWITCHABLE_INTERP: {
      // left
      const int left_in_image = xd->left_available && left_mi->mbmi.mb_in_image;
      const int left_mv_pred = is_inter_mode(left_mi->mbmi.mode);
      const int left_interp = left_in_image && left_mv_pred ?
                    vp9_switchable_interp_map[left_mi->mbmi.interp_filter] :
                    VP9_SWITCHABLE_FILTERS;

      // above
      const int above_in_image = above_mi->mbmi.mb_in_image;
      const int above_mv_pred = is_inter_mode(above_mi->mbmi.mode);
      const int above_interp = above_in_image && above_mv_pred ?
                    vp9_switchable_interp_map[above_mi->mbmi.interp_filter] :
                    VP9_SWITCHABLE_FILTERS;

      assert(left_interp != -1);
      assert(above_interp != -1);

      if (left_interp == above_interp)
        pred_context = left_interp;
      else if (left_interp == VP9_SWITCHABLE_FILTERS &&
               above_interp != VP9_SWITCHABLE_FILTERS)
         pred_context = above_interp;
      else if (left_interp != VP9_SWITCHABLE_FILTERS &&
               above_interp == VP9_SWITCHABLE_FILTERS)
        pred_context = left_interp;
      else
        pred_context = VP9_SWITCHABLE_FILTERS;

      break;
    }

    default:
      pred_context = 0;  // *** add error trap code.
      break;
  }

  return pred_context;
}

// This function returns a context probability for coding a given
// prediction signal
vp9_prob vp9_get_pred_prob(const VP9_COMMON *const cm,
                          const MACROBLOCKD *const xd,
                          PRED_ID pred_id) {
  const int pred_context = vp9_get_pred_context(cm, xd, pred_id);

  switch (pred_id) {
    case PRED_SEG_ID:
      return cm->segment_pred_probs[pred_context];
    case PRED_REF:
      return cm->ref_pred_probs[pred_context];
    case PRED_COMP:
      // In keeping with convention elsewhre the probability returned is
      // the probability of a "0" outcome which in this case means the
      // probability of comp pred off.
      return cm->prob_comppred[pred_context];
    case PRED_MBSKIP:
      return cm->mbskip_pred_probs[pred_context];
    default:
      return 128;  // *** add error trap code.
  }
}

// This function returns a context probability ptr for coding a given
// prediction signal
const vp9_prob *vp9_get_pred_probs(const VP9_COMMON *const cm,
                                   const MACROBLOCKD *const xd,
                                   PRED_ID pred_id) {
  const int pred_context = vp9_get_pred_context(cm, xd, pred_id);

  switch (pred_id) {
    case PRED_SEG_ID:
      return &cm->segment_pred_probs[pred_context];
    case PRED_REF:
      return &cm->ref_pred_probs[pred_context];
    case PRED_COMP:
      // In keeping with convention elsewhre the probability returned is
      // the probability of a "0" outcome which in this case means the
      // probability of comp pred off.
      return &cm->prob_comppred[pred_context];
    case PRED_MBSKIP:
      return &cm->mbskip_pred_probs[pred_context];
    case PRED_SWITCHABLE_INTERP:
      return &cm->fc.switchable_interp_prob[pred_context][0];
    default:
      return NULL;  // *** add error trap code.
  }
}

// This function returns the status of the given prediction signal.
// I.e. is the predicted value for the given signal correct.
unsigned char vp9_get_pred_flag(const MACROBLOCKD *const xd,
                                PRED_ID pred_id) {
  switch (pred_id) {
    case PRED_SEG_ID:
      return xd->mode_info_context->mbmi.seg_id_predicted;
    case PRED_REF:
      return  xd->mode_info_context->mbmi.ref_predicted;
    case PRED_MBSKIP:
      return xd->mode_info_context->mbmi.mb_skip_coeff;
    default:
      return 0;  // *** add error trap code.
  }
}

// This function sets the status of the given prediction signal.
// I.e. is the predicted value for the given signal correct.
void vp9_set_pred_flag(MACROBLOCKD *const xd,
                       PRED_ID pred_id,
                       unsigned char pred_flag) {
  const int mis = xd->mode_info_stride;
  BLOCK_SIZE_TYPE bsize = xd->mode_info_context->mbmi.sb_type;
  const int bh = 1 << mb_height_log2(bsize);
  const int bw = 1 << mb_width_log2(bsize);
#define sub(a, b) (b) < 0 ? (a) + (b) : (a)
  const int x_mbs = sub(bw, xd->mb_to_right_edge >> 7);
  const int y_mbs = sub(bh, xd->mb_to_bottom_edge >> 7);
#undef sub
  int x, y;

  switch (pred_id) {
    case PRED_SEG_ID:
      for (y = 0; y < y_mbs; y++) {
        for (x = 0; x < x_mbs; x++) {
          xd->mode_info_context[y * mis + x].mbmi.seg_id_predicted =
              pred_flag;
        }
      }
      break;

    case PRED_REF:
      for (y = 0; y < y_mbs; y++) {
        for (x = 0; x < x_mbs; x++) {
          xd->mode_info_context[y * mis + x].mbmi.ref_predicted = pred_flag;
        }
      }
      break;

    case PRED_MBSKIP:
      for (y = 0; y < y_mbs; y++) {
        for (x = 0; x < x_mbs; x++) {
          xd->mode_info_context[y * mis + x].mbmi.mb_skip_coeff = pred_flag;
        }
      }
      break;

    default:
      // *** add error trap code.
      break;
  }
}


// The following contain the guts of the prediction code used to
// peredict various bitstream signals.

// Macroblock segment id prediction function
int vp9_get_pred_mb_segid(VP9_COMMON *cm, BLOCK_SIZE_TYPE sb_type,
                          int mb_row, int mb_col) {
  const int mb_index = mb_row * cm->mb_cols + mb_col;
  if (sb_type > BLOCK_SIZE_MB16X16) {
    const int bw = 1 << mb_width_log2(sb_type);
    const int bh = 1 << mb_height_log2(sb_type);
    const int ymbs = MIN(cm->mb_rows - mb_row, bh);
    const int xmbs = MIN(cm->mb_cols - mb_col, bw);
    int segment_id = INT_MAX;
    int x, y;

    for (y = 0; y < ymbs; y++) {
      for (x = 0; x < xmbs; x++) {
        const int index = mb_index + (y * cm->mb_cols + x);
        segment_id = MIN(segment_id, cm->last_frame_seg_map[index]);
      }
    }
    return segment_id;
  } else {
    return cm->last_frame_seg_map[mb_index];
  }
}

MV_REFERENCE_FRAME vp9_get_pred_ref(const VP9_COMMON *const cm,
                                    const MACROBLOCKD *const xd) {
  MODE_INFO *m = xd->mode_info_context;

  MV_REFERENCE_FRAME left;
  MV_REFERENCE_FRAME above;
  MV_REFERENCE_FRAME above_left;
  MV_REFERENCE_FRAME pred_ref = LAST_FRAME;

  int segment_id = xd->mode_info_context->mbmi.segment_id;
  int i;

  unsigned char frame_allowed[MAX_REF_FRAMES] = {1, 1, 1, 1};
  unsigned char ref_score[MAX_REF_FRAMES];
  unsigned char best_score = 0;
  unsigned char left_in_image;
  unsigned char above_in_image;
  unsigned char above_left_in_image;

  // Is segment coding ennabled
  int seg_ref_active = vp9_segfeature_active(xd, segment_id, SEG_LVL_REF_FRAME);

  // Special case treatment if segment coding is enabled.
  // Dont allow prediction of a reference frame that the segment
  // does not allow
  if (seg_ref_active) {
    for (i = 0; i < MAX_REF_FRAMES; i++) {
      frame_allowed[i] =
        vp9_check_segref(xd, segment_id, i);

      // Score set to 0 if ref frame not allowed
      ref_score[i] = cm->ref_scores[i] * frame_allowed[i];
    }
  } else
    vpx_memcpy(ref_score, cm->ref_scores, sizeof(ref_score));

  // Reference frames used by neighbours
  left = (m - 1)->mbmi.ref_frame;
  above = (m - cm->mode_info_stride)->mbmi.ref_frame;
  above_left = (m - 1 - cm->mode_info_stride)->mbmi.ref_frame;

  // Are neighbours in image
  left_in_image = (m - 1)->mbmi.mb_in_image && xd->left_available;
  above_in_image = (m - cm->mode_info_stride)->mbmi.mb_in_image;
  above_left_in_image = (m - 1 - cm->mode_info_stride)->mbmi.mb_in_image &&
                        xd->left_available;

  // Adjust scores for candidate reference frames based on neigbours
  if (frame_allowed[left] && left_in_image) {
    ref_score[left] += 16;
    if (above_left_in_image && (left == above_left))
      ref_score[left] += 4;
  }
  if (frame_allowed[above] && above_in_image) {
    ref_score[above] += 16;
    if (above_left_in_image && (above == above_left))
      ref_score[above] += 4;
  }

  // Now choose the candidate with the highest score
  for (i = 0; i < MAX_REF_FRAMES; i++) {
    if (ref_score[i] > best_score) {
      pred_ref = i;
      best_score = ref_score[i];
    }
  }

  return pred_ref;
}

// Functions to computes a set of modified reference frame probabilities
// to use when the prediction of the reference frame value fails
void vp9_calc_ref_probs(int *count, vp9_prob *probs) {
  int tot_count = count[0] + count[1] + count[2] + count[3];
  probs[0] = get_prob(count[0], tot_count);

  tot_count -= count[0];
  probs[1] = get_prob(count[1], tot_count);

  tot_count -= count[1];
  probs[2] = get_prob(count[2], tot_count);
}

// Computes a set of modified conditional probabilities for the reference frame
// Values willbe set to 0 for reference frame options that are not possible
// because wither they were predicted and prediction has failed or because
// they are not allowed for a given segment.
void vp9_compute_mod_refprobs(VP9_COMMON *const cm) {
  int norm_cnt[MAX_REF_FRAMES];
  const int intra_count = cm->prob_intra_coded;
  const int inter_count = (255 - intra_count);
  const int last_count = (inter_count * cm->prob_last_coded) / 255;
  const int gfarf_count = inter_count - last_count;
  const int gf_count = (gfarf_count * cm->prob_gf_coded) / 255;
  const int arf_count = gfarf_count - gf_count;

  // Work out modified reference frame probabilities to use where prediction
  // of the reference frame fails
  norm_cnt[0] = 0;
  norm_cnt[1] = last_count;
  norm_cnt[2] = gf_count;
  norm_cnt[3] = arf_count;
  vp9_calc_ref_probs(norm_cnt, cm->mod_refprobs[INTRA_FRAME]);
  cm->mod_refprobs[INTRA_FRAME][0] = 0;    // This branch implicit

  norm_cnt[0] = intra_count;
  norm_cnt[1] = 0;
  norm_cnt[2] = gf_count;
  norm_cnt[3] = arf_count;
  vp9_calc_ref_probs(norm_cnt, cm->mod_refprobs[LAST_FRAME]);
  cm->mod_refprobs[LAST_FRAME][1] = 0;    // This branch implicit

  norm_cnt[0] = intra_count;
  norm_cnt[1] = last_count;
  norm_cnt[2] = 0;
  norm_cnt[3] = arf_count;
  vp9_calc_ref_probs(norm_cnt, cm->mod_refprobs[GOLDEN_FRAME]);
  cm->mod_refprobs[GOLDEN_FRAME][2] = 0;  // This branch implicit

  norm_cnt[0] = intra_count;
  norm_cnt[1] = last_count;
  norm_cnt[2] = gf_count;
  norm_cnt[3] = 0;
  vp9_calc_ref_probs(norm_cnt, cm->mod_refprobs[ALTREF_FRAME]);
  cm->mod_refprobs[ALTREF_FRAME][2] = 0;  // This branch implicit

  // Score the reference frames based on overal frequency.
  // These scores contribute to the prediction choices.
  // Max score 17 min 1
  cm->ref_scores[INTRA_FRAME] = 1 + (intra_count * 16 / 255);
  cm->ref_scores[LAST_FRAME] = 1 + (last_count * 16 / 255);
  cm->ref_scores[GOLDEN_FRAME] = 1 + (gf_count * 16 / 255);
  cm->ref_scores[ALTREF_FRAME] = 1 + (arf_count * 16 / 255);
}
