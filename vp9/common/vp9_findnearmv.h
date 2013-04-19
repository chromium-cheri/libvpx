/*
 *  Copyright (c) 2010 The WebM project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */


#ifndef VP9_COMMON_VP9_FINDNEARMV_H_
#define VP9_COMMON_VP9_FINDNEARMV_H_

#include "vp9/common/vp9_mv.h"
#include "vp9/common/vp9_blockd.h"
#include "vp9/common/vp9_treecoder.h"
#include "vp9/common/vp9_onyxc_int.h"

#define LEFT_TOP_MARGIN     ((VP9BORDERINPIXELS - VP9_INTERP_EXTEND) << 3)
#define RIGHT_BOTTOM_MARGIN ((VP9BORDERINPIXELS - VP9_INTERP_EXTEND) << 3)

static void mv_bias(int refmb_ref_frame_sign_bias, int refframe,
                    int_mv *mvp, const int *ref_frame_sign_bias) {
  MV xmv = mvp->as_mv;

  if (refmb_ref_frame_sign_bias != ref_frame_sign_bias[refframe]) {
    xmv.row *= -1;
    xmv.col *= -1;
  }

  mvp->as_mv = xmv;
}

// TODO(jingning): this mv clamping function should be block size dependent.
static void clamp_mv(int_mv *mv,
                     int mb_to_left_edge,
                     int mb_to_right_edge,
                     int mb_to_top_edge,
                     int mb_to_bottom_edge) {
  mv->as_mv.col = clamp(mv->as_mv.col, mb_to_left_edge, mb_to_right_edge);
  mv->as_mv.row = clamp(mv->as_mv.row, mb_to_top_edge, mb_to_bottom_edge);
}

static int clamp_mv2(int_mv *mv, const MACROBLOCKD *xd) {
  int_mv tmp_mv;
  tmp_mv.as_int = mv->as_int;
  clamp_mv(mv,
           xd->mb_to_left_edge - LEFT_TOP_MARGIN,
           xd->mb_to_right_edge + RIGHT_BOTTOM_MARGIN,
           xd->mb_to_top_edge - LEFT_TOP_MARGIN,
           xd->mb_to_bottom_edge + RIGHT_BOTTOM_MARGIN);
  return tmp_mv.as_int != mv->as_int;
}

static int check_mv_bounds(int_mv *mv,
                           int mb_to_left_edge, int mb_to_right_edge,
                           int mb_to_top_edge, int mb_to_bottom_edge) {
  return mv->as_mv.col < mb_to_left_edge ||
         mv->as_mv.col > mb_to_right_edge ||
         mv->as_mv.row < mb_to_top_edge ||
         mv->as_mv.row > mb_to_bottom_edge;
}

vp9_prob *vp9_mv_ref_probs(VP9_COMMON *pc,
                           vp9_prob p[VP9_MVREFS - 1],
                           const int context);

extern const uint8_t vp9_mbsplit_offset[4][16];

static int left_block_mv(const MACROBLOCKD *xd,
                         const MODE_INFO *cur_mb, int b) {
  if (!(b & 3)) {
    if (!xd->left_available)
      return 0;

    // On L edge, get from MB to left of us
    --cur_mb;

    if (cur_mb->mbmi.mode != SPLITMV)
      return cur_mb->mbmi.mv[0].as_int;

    b += 4;
  }

  return (cur_mb->bmi + b - 1)->as_mv[0].as_int;
}

static int left_block_second_mv(const MACROBLOCKD *xd,
                                const MODE_INFO *cur_mb, int b) {
  if (!(b & 3)) {
    if (!xd->left_available)
      return 0;

    /* On L edge, get from MB to left of us */
    --cur_mb;

    if (cur_mb->mbmi.mode != SPLITMV)
      return cur_mb->mbmi.second_ref_frame > 0 ?
          cur_mb->mbmi.mv[1].as_int : cur_mb->mbmi.mv[0].as_int;
    b += 4;
  }

  return cur_mb->mbmi.second_ref_frame > 0 ?
      (cur_mb->bmi + b - 1)->as_mv[1].as_int :
      (cur_mb->bmi + b - 1)->as_mv[0].as_int;
}

static int above_block_mv(const MODE_INFO *cur_mb, int b, int mi_stride) {
  if (!(b >> 2)) {
    /* On top edge, get from MB above us */
    cur_mb -= mi_stride;

    if (cur_mb->mbmi.mode != SPLITMV)
      return cur_mb->mbmi.mv[0].as_int;
    b += 16;
  }

  return (cur_mb->bmi + b - 4)->as_mv[0].as_int;
}

static int above_block_second_mv(const MODE_INFO *cur_mb, int b, int mi_stride) {
  if (!(b >> 2)) {
    /* On top edge, get from MB above us */
    cur_mb -= mi_stride;

    if (cur_mb->mbmi.mode != SPLITMV)
      return cur_mb->mbmi.second_ref_frame > 0 ?
          cur_mb->mbmi.mv[1].as_int : cur_mb->mbmi.mv[0].as_int;
    b += 16;
  }

  return cur_mb->mbmi.second_ref_frame > 0 ?
      (cur_mb->bmi + b - 4)->as_mv[1].as_int :
      (cur_mb->bmi + b - 4)->as_mv[0].as_int;
}

static B_PREDICTION_MODE left_block_mode(const MODE_INFO *cur_mb, int b) {
  if (!(b & 3)) {
    /* On L edge, get from MB to left of us */
    --cur_mb;

    if (cur_mb->mbmi.mode < I8X8_PRED) {
      return pred_mode_conv(cur_mb->mbmi.mode);
    } else if (cur_mb->mbmi.mode == I8X8_PRED) {
      return pred_mode_conv(
          (MB_PREDICTION_MODE)(cur_mb->bmi + 3 + b)->as_mode.first);
    } else if (cur_mb->mbmi.mode == I4X4_PRED) {
      return ((cur_mb->bmi + 3 + b)->as_mode.first);
    } else {
      return B_DC_PRED;
    }
  }
  return (cur_mb->bmi + b - 1)->as_mode.first;
}

static B_PREDICTION_MODE above_block_mode(const MODE_INFO *cur_mb,
                                          int b, int mi_stride) {
  if (!(b >> 2)) {
    /* On top edge, get from MB above us */
    cur_mb -= mi_stride;

    if (cur_mb->mbmi.mode < I8X8_PRED) {
      return pred_mode_conv(cur_mb->mbmi.mode);
    } else if (cur_mb->mbmi.mode == I8X8_PRED) {
      return pred_mode_conv(
          (MB_PREDICTION_MODE)(cur_mb->bmi + 12 + b)->as_mode.first);
    } else if (cur_mb->mbmi.mode == I4X4_PRED) {
      return ((cur_mb->bmi + 12 + b)->as_mode.first);
    } else {
      return B_DC_PRED;
    }
  }

  return (cur_mb->bmi + b - 4)->as_mode.first;
}

#endif  // VP9_COMMON_VP9_FINDNEARMV_H_
