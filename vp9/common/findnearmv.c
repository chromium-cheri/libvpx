/*
 *  Copyright (c) 2010 The WebM project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */


#include "findnearmv.h"
#include "vp9/common/sadmxn.h"
#include <limits.h>

const unsigned char vp9_mbsplit_offset[4][16] = {
  { 0,  8,  0,  0,  0,  0,  0,  0,  0,  0,   0,  0,  0,  0,  0,  0},
  { 0,  2,  0,  0,  0,  0,  0,  0,  0,  0,   0,  0,  0,  0,  0,  0},
  { 0,  2,  8, 10,  0,  0,  0,  0,  0,  0,   0,  0,  0,  0,  0,  0},
  { 0,  1,  2,  3,  4,  5,  6,  7,  8,  9,  10, 11, 12, 13, 14, 15}
};

static void lower_mv_precision(int_mv *mv, int usehp)
{
  if (!usehp || !vp9_use_nmv_hp(&mv->as_mv)) {
    if (mv->as_mv.row & 1)
      mv->as_mv.row += (mv->as_mv.row > 0 ? -1 : 1);
    if (mv->as_mv.col & 1)
      mv->as_mv.col += (mv->as_mv.col > 0 ? -1 : 1);
  }
}

/* Predict motion vectors using those from already-decoded nearby blocks.
   Note that we only consider one 4x4 subblock from each candidate 16x16
   macroblock.   */

void vp9_find_near_mvs
(
  MACROBLOCKD *xd,
  const MODE_INFO *here,
  const MODE_INFO *lf_here,
  int_mv *nearest,
  int_mv *nearby,
  int_mv *best_mv,
  int cnt[4],
  int refframe,
  int *ref_frame_sign_bias) {
  const MODE_INFO *above = here - xd->mode_info_stride;
  const MODE_INFO *left = here - 1;
  const MODE_INFO *aboveleft = above - 1;
  const MODE_INFO *third = NULL;
  int_mv            near_mvs[4];
  int_mv           *mv = near_mvs;
  int             *cntx = cnt;
  enum {CNT_INTRA, CNT_NEAREST, CNT_NEAR, CNT_SPLITMV};

  /* Zero accumulators */
  mv[0].as_int = mv[1].as_int = mv[2].as_int = 0;
  cnt[0] = cnt[1] = cnt[2] = cnt[3] = 0;

  /* Process above */
  if (above->mbmi.ref_frame != INTRA_FRAME) {
    if (above->mbmi.mv[0].as_int) {
      ++ mv;
      mv->as_int = above->mbmi.mv[0].as_int;
      mv_bias(ref_frame_sign_bias[above->mbmi.ref_frame],
              refframe, mv, ref_frame_sign_bias);
      ++cntx;
    }
    *cntx += 2;
  }

  /* Process left */
  if (left->mbmi.ref_frame != INTRA_FRAME) {
    if (left->mbmi.mv[0].as_int) {
      int_mv this_mv;
      this_mv.as_int = left->mbmi.mv[0].as_int;
      mv_bias(ref_frame_sign_bias[left->mbmi.ref_frame],
              refframe, &this_mv, ref_frame_sign_bias);

      if (this_mv.as_int != mv->as_int) {
        ++ mv;
        mv->as_int = this_mv.as_int;
        ++ cntx;
      }
      *cntx += 2;
    } else
      cnt[CNT_INTRA] += 2;
  }
  /* Process above left or the one from last frame */
  if (aboveleft->mbmi.ref_frame != INTRA_FRAME ||
      (lf_here->mbmi.ref_frame == LAST_FRAME && refframe == LAST_FRAME)) {
    if (aboveleft->mbmi.mv[0].as_int) {
      third = aboveleft;
    } else if (lf_here->mbmi.mv[0].as_int) {
      third = lf_here;
    }
    if (third) {
      int_mv this_mv;
      this_mv.as_int = third->mbmi.mv[0].as_int;
      mv_bias(ref_frame_sign_bias[third->mbmi.ref_frame],
              refframe, &this_mv, ref_frame_sign_bias);

      if (this_mv.as_int != mv->as_int) {
        ++ mv;
        mv->as_int = this_mv.as_int;
        ++ cntx;
      }
      *cntx += 1;
    } else
      cnt[CNT_INTRA] += 1;
  }

  /* If we have three distinct MV's ... */
  if (cnt[CNT_SPLITMV]) {
    /* See if the third MV can be merged with NEAREST */
    if (mv->as_int == near_mvs[CNT_NEAREST].as_int)
      cnt[CNT_NEAREST] += 1;
  }

  cnt[CNT_SPLITMV] = ((above->mbmi.mode == SPLITMV)
                      + (left->mbmi.mode == SPLITMV)) * 2
                     + (
                       lf_here->mbmi.mode == SPLITMV ||
                       aboveleft->mbmi.mode == SPLITMV);

  /* Swap near and nearest if necessary */
  if (cnt[CNT_NEAR] > cnt[CNT_NEAREST]) {
    int tmp;
    tmp = cnt[CNT_NEAREST];
    cnt[CNT_NEAREST] = cnt[CNT_NEAR];
    cnt[CNT_NEAR] = tmp;
    tmp = near_mvs[CNT_NEAREST].as_int;
    near_mvs[CNT_NEAREST].as_int = near_mvs[CNT_NEAR].as_int;
    near_mvs[CNT_NEAR].as_int = tmp;
  }

  /* Use near_mvs[0] to store the "best" MV */
  if (cnt[CNT_NEAREST] >= cnt[CNT_INTRA])
    near_mvs[CNT_INTRA] = near_mvs[CNT_NEAREST];

  /* Set up return values */
  best_mv->as_int = near_mvs[0].as_int;
  nearest->as_int = near_mvs[CNT_NEAREST].as_int;
  nearby->as_int = near_mvs[CNT_NEAR].as_int;

  /* Make sure that the 1/8th bits of the Mvs are zero if high_precision
   * is not being used, by truncating the last bit towards 0
   */
  lower_mv_precision(best_mv, xd->allow_high_precision_mv);
  lower_mv_precision(nearest, xd->allow_high_precision_mv);
  lower_mv_precision(nearby, xd->allow_high_precision_mv);

  // TODO: move clamp outside findnearmv
  clamp_mv2(nearest, xd);
  clamp_mv2(nearby, xd);
  clamp_mv2(best_mv, xd);
}

vp9_prob *vp9_mv_ref_probs(VP9_COMMON *pc,
                           vp9_prob p[VP9_MVREFS - 1], const int near_mv_ref_ct[4]
                          ) {
  p[0] = pc->fc.vp8_mode_contexts [near_mv_ref_ct[0]] [0];
  p[1] = pc->fc.vp8_mode_contexts [near_mv_ref_ct[1]] [1];
  p[2] = pc->fc.vp8_mode_contexts [near_mv_ref_ct[2]] [2];
  p[3] = pc->fc.vp8_mode_contexts [near_mv_ref_ct[3]] [3];
  return p;
}

#if CONFIG_NEWBESTREFMV
#define SP(x) (((x) & 7) << 1)
unsigned int vp9_sad3x16_c(
  const unsigned char *src_ptr,
  int  src_stride,
  const unsigned char *ref_ptr,
  int  ref_stride,
  int max_sad) {
  return sad_mx_n_c(src_ptr, src_stride, ref_ptr, ref_stride, 3, 16);
}
unsigned int vp9_sad16x3_c(
  const unsigned char *src_ptr,
  int  src_stride,
  const unsigned char *ref_ptr,
  int  ref_stride,
  int max_sad) {
  return sad_mx_n_c(src_ptr, src_stride, ref_ptr, ref_stride, 16, 3);
}

/* check a list of motion vectors by sad score using a number rows of pixels
 * above and a number cols of pixels in the left to select the one with best
 * score to use as ref motion vector
 */
void vp9_find_best_ref_mvs(MACROBLOCKD *xd,
                           unsigned char *ref_y_buffer,
                           int ref_y_stride,
                           int_mv *mvlist,
                           int_mv *best_mv,
                           int_mv *nearest,
                           int_mv *near) {
  int i, j;
  unsigned char *above_src;
  unsigned char *left_src;
  unsigned char *above_ref;
  unsigned char *left_ref;
  int score;
  int sse;
  int ref_scores[MAX_MV_REFS] = {0};
  int_mv sorted_mvs[MAX_MV_REFS];
  int zero_seen = FALSE;

  // Default all to 0,0 if nothing else available
  best_mv->as_int = nearest->as_int = near->as_int = 0;
  vpx_memset(sorted_mvs, 0, sizeof(sorted_mvs));

#if CONFIG_SUBPELREFMV
  above_src = xd->dst.y_buffer - xd->dst.y_stride * 2;
  left_src  = xd->dst.y_buffer - 2;
  above_ref = ref_y_buffer - ref_y_stride * 2;
  left_ref  = ref_y_buffer - 2;
#else
  above_src = xd->dst.y_buffer - xd->dst.y_stride * 3;
  left_src  = xd->dst.y_buffer - 3;
  above_ref = ref_y_buffer - ref_y_stride * 3;
  left_ref  = ref_y_buffer - 3;
#endif

  //for(i = 0; i < MAX_MV_REFS; ++i) {
  // Limit search to the predicted best 4
  for(i = 0; i < 4; ++i) {
    int_mv this_mv;
    int offset = 0;
    int row_offset, col_offset;

    this_mv.as_int = mvlist[i].as_int;

    // If we see a 0,0 vector for a second time we have reached the end of
    // the list of valid candidate vectors.
    if (!this_mv.as_int && zero_seen)
      break;

    zero_seen = zero_seen || !this_mv.as_int;

    clamp_mv(&this_mv,
             xd->mb_to_left_edge - LEFT_TOP_MARGIN + 24,
             xd->mb_to_right_edge + RIGHT_BOTTOM_MARGIN,
             xd->mb_to_top_edge - LEFT_TOP_MARGIN + 24,
             xd->mb_to_bottom_edge + RIGHT_BOTTOM_MARGIN);
#if CONFIG_SUBPELREFMV
    row_offset = this_mv.as_mv.row >> 3;
    col_offset = this_mv.as_mv.col >> 3;
    offset = ref_y_stride * row_offset + col_offset;
    score = 0;
    if (xd->up_available) {
      vp9_sub_pixel_variance16x2_c(above_ref + offset, ref_y_stride,
                                   SP(this_mv.as_mv.col), SP(this_mv.as_mv.row),
                                   above_src, xd->dst.y_stride, &sse);
      score += sse;
    }
    if (xd->left_available) {
      vp9_sub_pixel_variance2x16_c(left_ref + offset, ref_y_stride,
                                   SP(this_mv.as_mv.col), SP(this_mv.as_mv.row),
                                   left_src, xd->dst.y_stride, &sse);
      score += sse;
    }
#else
    row_offset = (this_mv.as_mv.row > 0) ?
      ((this_mv.as_mv.row + 3) >> 3):((this_mv.as_mv.row + 4) >> 3);
    col_offset = (this_mv.as_mv.col > 0) ?
      ((this_mv.as_mv.col + 3) >> 3):((this_mv.as_mv.col + 4) >> 3);
    offset = ref_y_stride * row_offset + col_offset;
    score = 0;
    if (xd->up_available) {
      score += vp9_sad16x3(above_src, xd->dst.y_stride,
                           above_ref + offset, ref_y_stride, INT_MAX);
    }
    if (xd->left_available) {
      score += vp9_sad3x16(left_src, xd->dst.y_stride,
                           left_ref + offset, ref_y_stride, INT_MAX);
    }
#endif
    // Add the entry to our list and then resort the list on score.
    ref_scores[i] = score;
    sorted_mvs[i].as_int = this_mv.as_int;
    j = i;
    while (j > 0) {
      if (ref_scores[j] < ref_scores[j-1]) {
        ref_scores[j] = ref_scores[j-1];
        sorted_mvs[j].as_int = sorted_mvs[j-1].as_int;
        ref_scores[j-1] = score;
        sorted_mvs[j-1].as_int = this_mv.as_int;
        j--;
      } else
        break;
    }
  }

  // Make sure all the candidates are properly clamped etc
  for (i = 0; i < 4; ++i) {
    lower_mv_precision(&sorted_mvs[i], xd->allow_high_precision_mv);
    clamp_mv2(&sorted_mvs[i], xd);
  }

  // Set the best mv to the first entry in the sorted list
  best_mv->as_int = sorted_mvs[0].as_int;

  // Provided that there are non zero vectors available there will not
  // be more than one 0,0 entry in the sorted list.
  // The best ref mv is always set to the first entry (which gave the best
  // results. The nearest is set to the first non zero vector if available and
  // near to the second non zero vector if available.
  // We do not use 0,0 as a nearest or near as 0,0 has its own mode.
  if ( sorted_mvs[0].as_int ) {
    nearest->as_int = sorted_mvs[0].as_int;
    if ( sorted_mvs[1].as_int )
      near->as_int = sorted_mvs[1].as_int;
    else
      near->as_int = sorted_mvs[2].as_int;
  } else {
      nearest->as_int = sorted_mvs[1].as_int;
      near->as_int = sorted_mvs[2].as_int;
  }

  // Copy back the re-ordered mv list
  vpx_memcpy(mvlist, sorted_mvs, sizeof(sorted_mvs));
}

#endif  // CONFIG_NEWBESTREFMV
