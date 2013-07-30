/*
  Copyright (c) 2010 The WebM project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include <assert.h>

#include "vp9/common/vp9_common.h"
#include "vp9/common/vp9_entropy.h"
#include "vp9/common/vp9_entropymode.h"
#include "vp9/common/vp9_entropymv.h"
#include "vp9/common/vp9_findnearmv.h"
#include "vp9/common/vp9_mvref_common.h"
#include "vp9/common/vp9_pred_common.h"
#include "vp9/common/vp9_reconinter.h"
#include "vp9/common/vp9_seg_common.h"

#include "vp9/decoder/vp9_decodemv.h"
#include "vp9/decoder/vp9_decodframe.h"
#include "vp9/decoder/vp9_onyxd_int.h"
#include "vp9/decoder/vp9_dsubexp.h"
#include "vp9/decoder/vp9_treereader.h"

// #define DEBUG_DEC_MV
#ifdef DEBUG_DEC_MV
int dec_mvcount = 0;
#endif

// #define DEC_DEBUG
#ifdef DEC_DEBUG
extern int dec_debug;
#endif

static MB_PREDICTION_MODE read_intra_mode(vp9_reader *r, const vp9_prob *p) {
  return (MB_PREDICTION_MODE)treed_read(r, vp9_intra_mode_tree, p);
}

static MB_PREDICTION_MODE read_inter_mode(vp9_reader *r, const vp9_prob *p) {
  return (MB_PREDICTION_MODE)treed_read(r, vp9_sb_mv_ref_tree, p);
}

static int read_segment_id(vp9_reader *r, MACROBLOCKD *xd) {
  return treed_read(r, vp9_segment_tree, xd->mb_segment_tree_probs);
}

static TX_SIZE read_selected_txfm_size(VP9_COMMON *cm, MACROBLOCKD *xd,
                                       BLOCK_SIZE_TYPE bsize, vp9_reader *r) {
  const int context = vp9_get_pred_context(cm, xd, PRED_TX_SIZE);
  const vp9_prob *tx_probs = vp9_get_pred_probs(cm, xd, PRED_TX_SIZE);
  TX_SIZE txfm_size = vp9_read(r, tx_probs[0]);
  if (txfm_size != TX_4X4 && bsize >= BLOCK_SIZE_MB16X16) {
    txfm_size += vp9_read(r, tx_probs[1]);
    if (txfm_size != TX_8X8 && bsize >= BLOCK_SIZE_SB32X32)
      txfm_size += vp9_read(r, tx_probs[2]);
  }

  if (bsize >= BLOCK_SIZE_SB32X32)
    cm->fc.tx_count_32x32p[context][txfm_size]++;
  else if (bsize >= BLOCK_SIZE_MB16X16)
    cm->fc.tx_count_16x16p[context][txfm_size]++;
  else
    cm->fc.tx_count_8x8p[context][txfm_size]++;

  return txfm_size;
}

static TX_SIZE read_txfm_size(VP9D_COMP *pbi, TXFM_MODE txfm_mode,
                              BLOCK_SIZE_TYPE bsize, int select_cond,
                              vp9_reader *r) {
  VP9_COMMON *const cm = &pbi->common;
  MACROBLOCKD *const xd = &pbi->mb;

  if (txfm_mode == TX_MODE_SELECT && bsize >= BLOCK_SIZE_SB8X8 && select_cond)
    return read_selected_txfm_size(cm, xd, bsize, r);
  else if (txfm_mode >= ALLOW_32X32 && bsize >= BLOCK_SIZE_SB32X32)
    return TX_32X32;
  else if (txfm_mode >= ALLOW_16X16 && bsize >= BLOCK_SIZE_MB16X16)
    return TX_16X16;
  else if (txfm_mode >= ALLOW_8X8 && bsize >= BLOCK_SIZE_SB8X8)
    return TX_8X8;
  else
    return TX_4X4;
}

static void set_segment_id(VP9_COMMON *cm, BLOCK_SIZE_TYPE bsize,
                           int mi_row, int mi_col, int segment_id) {
  const int mi_offset = mi_row * cm->mi_cols + mi_col;
  const int bw = 1 << mi_width_log2(bsize);
  const int bh = 1 << mi_height_log2(bsize);
  const int xmis = MIN(cm->mi_cols - mi_col, bw);
  const int ymis = MIN(cm->mi_rows - mi_row, bh);
  int x, y;

  assert(segment_id >= 0 && segment_id < MAX_MB_SEGMENTS);

  for (y = 0; y < ymis; y++)
    for (x = 0; x < xmis; x++)
      cm->last_frame_seg_map[mi_offset + y * cm->mi_cols + x] = segment_id;
}

static int read_intra_segment_id(VP9D_COMP *pbi, int mi_row, int mi_col,
                                 vp9_reader *r) {
  VP9_COMMON *const cm = &pbi->common;
  MACROBLOCKD *const xd = &pbi->mb;
  const BLOCK_SIZE_TYPE bsize = xd->mode_info_context->mbmi.sb_type;

  if (xd->segmentation_enabled && xd->update_mb_segmentation_map) {
    const int segment_id = read_segment_id(r, xd);
    set_segment_id(cm, bsize, mi_row, mi_col, segment_id);
    return segment_id;
  } else {
    return 0;
  }
}

static uint8_t read_skip_coeff(VP9D_COMP *pbi, int segment_id, vp9_reader *r) {
  VP9_COMMON *const cm = &pbi->common;
  MACROBLOCKD *const xd = &pbi->mb;
  int skip_coeff = vp9_segfeature_active(xd, segment_id, SEG_LVL_SKIP);
  if (!skip_coeff) {
    const uint8_t ctx = vp9_get_pred_context(cm, xd, PRED_MBSKIP);
    skip_coeff = vp9_read(r, vp9_get_pred_prob(cm, xd, PRED_MBSKIP));
    cm->fc.mbskip_count[ctx][skip_coeff]++;
  }
  return skip_coeff;
}

static void read_intra_mode_info(VP9D_COMP *pbi, MODE_INFO *m,
                                 int mi_row, int mi_col, vp9_reader *r) {
  VP9_COMMON *const cm = &pbi->common;
  MACROBLOCKD *const xd = &pbi->mb;
  MB_MODE_INFO *const mbmi = &m->mbmi;
  const BLOCK_SIZE_TYPE bsize = mbmi->sb_type;
  const int mis = cm->mode_info_stride;

  mbmi->segment_id = read_intra_segment_id(pbi, mi_row, mi_col, r);
  mbmi->mb_skip_coeff = read_skip_coeff(pbi, mbmi->segment_id, r);
  mbmi->txfm_size = read_txfm_size(pbi, cm->txfm_mode, bsize, 1, r);
  mbmi->ref_frame[0] = INTRA_FRAME;

  if (bsize >= BLOCK_SIZE_SB8X8) {
    const MB_PREDICTION_MODE A = above_block_mode(m, 0, mis);
    const MB_PREDICTION_MODE L = xd->left_available ?
                                  left_block_mode(m, 0) : DC_PRED;
    mbmi->mode = read_intra_mode(r, cm->kf_y_mode_prob[A][L]);
  } else {
    // Only 4x4, 4x8, 8x4 blocks
    const int bw = 1 << b_width_log2(bsize);
    const int bh = 1 << b_height_log2(bsize);
    int idx, idy;

    for (idy = 0; idy < 2; idy += bh) {
      for (idx = 0; idx < 2; idx += bw) {
        const int ib = idy * 2 + idx;
        const MB_PREDICTION_MODE A = above_block_mode(m, ib, mis);
        const MB_PREDICTION_MODE L = (xd->left_available || idx) ?
                                      left_block_mode(m, ib) : DC_PRED;
        const MB_PREDICTION_MODE b_mode = read_intra_mode(r,
                                              cm->kf_y_mode_prob[A][L]);
        m->bmi[ib].as_mode = b_mode;
        if (bh == 2)
          m->bmi[ib + 2].as_mode = b_mode;
        if (bw == 2)
          m->bmi[ib + 1].as_mode = b_mode;
      }
    }

    mbmi->mode = m->bmi[3].as_mode;
  }

  mbmi->uv_mode = read_intra_mode(r, cm->kf_uv_mode_prob[mbmi->mode]);
}

static int read_mv_component(vp9_reader *r,
                             const nmv_component *mvcomp, int usehp) {

  int mag, d, fr, hp;
  const int sign = vp9_read(r, mvcomp->sign);
  const int mv_class = treed_read(r, vp9_mv_class_tree, mvcomp->classes);
  const int class0 = mv_class == MV_CLASS_0;

  // Integer part
  if (class0) {
    d = treed_read(r, vp9_mv_class0_tree, mvcomp->class0);
  } else {
    int i;
    const int n = mv_class + CLASS0_BITS - 1;  // number of bits

    d = 0;
    for (i = 0; i < n; ++i)
      d |= vp9_read(r, mvcomp->bits[i]) << i;
  }

  // Fractional part
  fr = treed_read(r, vp9_mv_fp_tree,
                  class0 ? mvcomp->class0_fp[d] : mvcomp->fp);


  // High precision part (if hp is not used, the default value of the hp is 1)
  hp = usehp ? vp9_read(r, class0 ? mvcomp->class0_hp : mvcomp->hp)
             : 1;

  // Result
  mag = vp9_get_mv_mag(mv_class, (d << 3) | (fr << 1) | hp) + 1;
  return sign ? -mag : mag;
}

static INLINE void read_mv(vp9_reader *r, MV *mv, const MV *ref,
                           const nmv_context *ctx,
                           nmv_context_counts *counts, int usehp) {
  const MV_JOINT_TYPE j = treed_read(r, vp9_mv_joint_tree, ctx->joints);
  MV diff = {0, 0};

  usehp = usehp && vp9_use_mv_hp(ref);
  if (mv_joint_vertical(j))
    diff.row = read_mv_component(r, &ctx->comps[0], usehp);

  if (mv_joint_horizontal(j))
    diff.col = read_mv_component(r, &ctx->comps[1], usehp);

  vp9_inc_mv(&diff, ref, counts, usehp);

  mv->row = ref->row + diff.row;
  mv->col = ref->col + diff.col;
}

static void update_mv(vp9_reader *r, vp9_prob *p, vp9_prob upd_p) {
  if (vp9_read(r, upd_p)) {
#ifdef LOW_PRECISION_MV_UPDATE
    *p = (vp9_read_literal(r, 7) << 1) | 1;
#else
    *p = vp9_read_literal(r, 8);
#endif
  }
}

static void read_mv_probs(vp9_reader *r, nmv_context *mvc, int usehp) {
  int i, j, k;

#ifdef MV_GROUP_UPDATE
  if (!vp9_read_bit(r))
    return;
#endif

  for (j = 0; j < MV_JOINTS - 1; ++j)
    update_mv(r, &mvc->joints[j], VP9_NMV_UPDATE_PROB);

  for (i = 0; i < 2; ++i) {
    nmv_component *const comp = &mvc->comps[i];

    update_mv(r, &comp->sign, VP9_NMV_UPDATE_PROB);
    for (j = 0; j < MV_CLASSES - 1; ++j)
      update_mv(r, &comp->classes[j], VP9_NMV_UPDATE_PROB);

    for (j = 0; j < CLASS0_SIZE - 1; ++j)
      update_mv(r, &comp->class0[j], VP9_NMV_UPDATE_PROB);

    for (j = 0; j < MV_OFFSET_BITS; ++j)
      update_mv(r, &comp->bits[j], VP9_NMV_UPDATE_PROB);
  }

  for (i = 0; i < 2; ++i) {
    nmv_component *const comp = &mvc->comps[i];

    for (j = 0; j < CLASS0_SIZE; ++j)
      for (k = 0; k < 3; ++k)
        update_mv(r, &comp->class0_fp[j][k], VP9_NMV_UPDATE_PROB);

    for (j = 0; j < 3; ++j)
      update_mv(r, &comp->fp[j], VP9_NMV_UPDATE_PROB);
  }

  if (usehp) {
    for (i = 0; i < 2; ++i) {
      update_mv(r, &mvc->comps[i].class0_hp, VP9_NMV_UPDATE_PROB);
      update_mv(r, &mvc->comps[i].hp, VP9_NMV_UPDATE_PROB);
    }
  }
}

// Read the referncence frame
static void read_ref_frame(VP9D_COMP *pbi, vp9_reader *r,
                           int segment_id, MV_REFERENCE_FRAME ref_frame[2]) {
  VP9_COMMON *const cm = &pbi->common;
  MACROBLOCKD *const xd = &pbi->mb;
  FRAME_CONTEXT *const fc = &cm->fc;

  if (vp9_segfeature_active(xd, segment_id, SEG_LVL_REF_FRAME)) {
    ref_frame[0] = vp9_get_segdata(xd, segment_id, SEG_LVL_REF_FRAME);
    ref_frame[1] = NONE;
  } else {
    const int comp_ctx = vp9_get_pred_context(cm, xd, PRED_COMP_INTER_INTER);
    int is_comp;

    if (cm->comp_pred_mode == HYBRID_PREDICTION) {
      is_comp = vp9_read(r, fc->comp_inter_prob[comp_ctx]);
      fc->comp_inter_count[comp_ctx][is_comp]++;
    } else {
      is_comp = cm->comp_pred_mode == COMP_PREDICTION_ONLY;
    }

    // FIXME(rbultje) I'm pretty sure this breaks segmentation ref frame coding
    if (is_comp) {
      const int fix_ref_idx = cm->ref_frame_sign_bias[cm->comp_fixed_ref];
      const int ref_ctx = vp9_get_pred_context(cm, xd, PRED_COMP_REF_P);
      const int b = vp9_read(r, fc->comp_ref_prob[ref_ctx]);
      fc->comp_ref_count[ref_ctx][b]++;
      ref_frame[fix_ref_idx] = cm->comp_fixed_ref;
      ref_frame[!fix_ref_idx] = cm->comp_var_ref[b];
    } else {
      const int ref1_ctx = vp9_get_pred_context(cm, xd, PRED_SINGLE_REF_P1);
      ref_frame[1] = NONE;
      if (vp9_read(r, fc->single_ref_prob[ref1_ctx][0])) {
        const int ref2_ctx = vp9_get_pred_context(cm, xd, PRED_SINGLE_REF_P2);
        const int b = vp9_read(r, fc->single_ref_prob[ref2_ctx][1]);
        ref_frame[0] = b ? ALTREF_FRAME : GOLDEN_FRAME;
        fc->single_ref_count[ref1_ctx][0][1]++;
        fc->single_ref_count[ref2_ctx][1][b]++;
      } else {
        ref_frame[0] = LAST_FRAME;
        fc->single_ref_count[ref1_ctx][0][0]++;
      }
    }
  }
}

#ifdef VPX_MODE_COUNT
unsigned int vp9_mv_cont_count[5][4] = {
  { 0, 0, 0, 0 },
  { 0, 0, 0, 0 },
  { 0, 0, 0, 0 },
  { 0, 0, 0, 0 },
  { 0, 0, 0, 0 }
};
#endif

static void read_switchable_interp_probs(FRAME_CONTEXT *fc, vp9_reader *r) {
  int i, j;
  for (j = 0; j < VP9_SWITCHABLE_FILTERS + 1; ++j)
    for (i = 0; i < VP9_SWITCHABLE_FILTERS - 1; ++i)
      if (vp9_read(r, VP9_MODE_UPDATE_PROB))
        vp9_diff_update_prob(r, &fc->switchable_interp_prob[j][i]);
}

static void read_inter_mode_probs(FRAME_CONTEXT *fc, vp9_reader *r) {
  int i, j;
  for (i = 0; i < INTER_MODE_CONTEXTS; ++i)
    for (j = 0; j < VP9_INTER_MODES - 1; ++j)
      if (vp9_read(r, VP9_MODE_UPDATE_PROB))
        vp9_diff_update_prob(r, &fc->inter_mode_probs[i][j]);
}

static INLINE COMPPREDMODE_TYPE read_comp_pred_mode(vp9_reader *r) {
  COMPPREDMODE_TYPE mode = vp9_read_bit(r);
  if (mode)
    mode += vp9_read_bit(r);
  return mode;
}

static int read_inter_segment_id(VP9D_COMP *pbi, int mi_row, int mi_col,
                                 vp9_reader *r) {
  VP9_COMMON *const cm = &pbi->common;
  MACROBLOCKD *const xd = &pbi->mb;
  const BLOCK_SIZE_TYPE bsize = xd->mode_info_context->mbmi.sb_type;
  const int pred_segment_id = vp9_get_segment_id(cm, cm->last_frame_seg_map,
                                                 bsize, mi_row, mi_col);
  int segment_id;

  if (!xd->segmentation_enabled)
    return 0;  // Default for disabled segmentation

  if (!xd->update_mb_segmentation_map)
    return pred_segment_id;

  if (cm->temporal_update) {
    const vp9_prob pred_prob = vp9_get_pred_prob(cm, xd, PRED_SEG_ID);
    const int pred_flag = vp9_read(r, pred_prob);
    vp9_set_pred_flag(xd, bsize, PRED_SEG_ID, pred_flag);
    segment_id = pred_flag ? pred_segment_id
                           : read_segment_id(r, xd);
  } else {
    segment_id = read_segment_id(r, xd);
  }
  set_segment_id(cm, bsize, mi_row, mi_col, segment_id);
  return segment_id;
}


static INLINE void assign_and_clamp_mv(int_mv *dst, const int_mv *src,
                                       int mb_to_left_edge,
                                       int mb_to_right_edge,
                                       int mb_to_top_edge,
                                       int mb_to_bottom_edge) {
  dst->as_int = src->as_int;
  clamp_mv(dst, mb_to_left_edge, mb_to_right_edge, mb_to_top_edge,
           mb_to_bottom_edge);
}

static INLINE INTERPOLATIONFILTERTYPE read_switchable_filter_type(
    VP9D_COMP *pbi, vp9_reader *r) {
  VP9_COMMON *const cm = &pbi->common;
  MACROBLOCKD *const xd = &pbi->mb;
  const vp9_prob *probs = vp9_get_pred_probs(cm, xd, PRED_SWITCHABLE_INTERP);
  const int index = treed_read(r, vp9_switchable_interp_tree, probs);
  const int ctx = vp9_get_pred_context(cm, xd, PRED_SWITCHABLE_INTERP);
  ++cm->fc.switchable_interp_count[ctx][index];
  return vp9_switchable_interp[index];
}

static void read_intra_block_modes(VP9D_COMP *pbi, MODE_INFO *mi,
                                   vp9_reader *r) {
  VP9_COMMON *const cm = &pbi->common;
  MB_MODE_INFO *const mbmi = &mi->mbmi;
  const BLOCK_SIZE_TYPE bsize = mi->mbmi.sb_type;
  const int bwl = b_width_log2(bsize), bhl = b_height_log2(bsize);

  if (bsize >= BLOCK_SIZE_SB8X8) {
    const int size_group = MIN(3, MIN(bwl, bhl));
    mbmi->mode = read_intra_mode(r, cm->fc.y_mode_prob[size_group]);
    cm->fc.y_mode_counts[size_group][mbmi->mode]++;
  } else {
     // Only 4x4, 4x8, 8x4 blocks
     const int bw = 1 << bwl, bh = 1 << bhl;
     int idx, idy;

     for (idy = 0; idy < 2; idy += bh) {
       for (idx = 0; idx < 2; idx += bw) {
         const int ib = idy * 2 + idx;
         const int b_mode = read_intra_mode(r, cm->fc.y_mode_prob[0]);
         mi->bmi[ib].as_mode = b_mode;
         cm->fc.y_mode_counts[0][b_mode]++;

         if (bh == 2)
           mi->bmi[ib + 2].as_mode = b_mode;
         if (bw == 2)
           mi->bmi[ib + 1].as_mode = b_mode;
      }
    }
    mbmi->mode = mi->bmi[3].as_mode;
  }

  mbmi->uv_mode = read_intra_mode(r, cm->fc.uv_mode_prob[mbmi->mode]);
  cm->fc.uv_mode_counts[mbmi->mode][mbmi->uv_mode]++;
}

static MV_REFERENCE_FRAME read_reference_frame(VP9D_COMP *pbi, int segment_id,
                                               vp9_reader *r) {
  VP9_COMMON *const cm = &pbi->common;
  MACROBLOCKD *const xd = &pbi->mb;

  MV_REFERENCE_FRAME ref;
  if (!vp9_segfeature_active(xd, segment_id, SEG_LVL_REF_FRAME)) {
    const int ctx = vp9_get_pred_context(cm, xd, PRED_INTRA_INTER);
    ref = (MV_REFERENCE_FRAME)
              vp9_read(r, vp9_get_pred_prob(cm, xd, PRED_INTRA_INTER));
    cm->fc.intra_inter_count[ctx][ref != INTRA_FRAME]++;
  } else {
    ref = (MV_REFERENCE_FRAME)
              vp9_get_segdata(xd, segment_id, SEG_LVL_REF_FRAME) != INTRA_FRAME;
  }
  return ref;
}

static void read_inter_mode_info(VP9D_COMP *pbi, MODE_INFO *mi,
                                 int mi_row, int mi_col, vp9_reader *r) {
  VP9_COMMON *const cm = &pbi->common;
  MACROBLOCKD *const xd = &pbi->mb;
  nmv_context *const nmvc = &cm->fc.nmvc;
  MB_MODE_INFO *const mbmi = &mi->mbmi;

  int_mv *const mv0 = &mbmi->mv[0];
  int_mv *const mv1 = &mbmi->mv[1];
  const BLOCK_SIZE_TYPE bsize = mi->mbmi.sb_type;
  const int bw = 1 << b_width_log2(bsize);
  const int bh = 1 << b_height_log2(bsize);

  int idx, idy;

  // Make sure the MACROBLOCKD mode info pointer is pointed at the
  // correct entry for the current macroblock.
  xd->mode_info_context = mi;

  // Distance of Mb to the various image edges.
  // These specified to 8th pel as they are always compared to MV values
  // that are in 1/8th pel units
  set_mi_row_col(cm, xd, mi_row, 1 << mi_height_log2(bsize),
                         mi_col, 1 << mi_width_log2(bsize));

  mbmi->segment_id = read_inter_segment_id(pbi, mi_row, mi_col, r);
  mbmi->mb_skip_coeff = read_skip_coeff(pbi, mbmi->segment_id, r);
  mbmi->ref_frame[0] = read_reference_frame(pbi, mbmi->segment_id, r);
  mbmi->ref_frame[1] = NONE;
  mbmi->txfm_size = read_txfm_size(pbi, cm->txfm_mode, bsize,
     (!mbmi->mb_skip_coeff || mbmi->ref_frame[0] == INTRA_FRAME), r);

  if (mbmi->ref_frame[0] != INTRA_FRAME) {
    int_mv nearest, nearby, best_mv;
    int_mv nearest_second, nearby_second, best_mv_second;
    vp9_prob *mv_ref_p;
    MV_REFERENCE_FRAME ref0, ref1;

    read_ref_frame(pbi, r, mbmi->segment_id, mbmi->ref_frame);
    ref0 = mbmi->ref_frame[0];
    ref1 = mbmi->ref_frame[1];

#ifdef DEC_DEBUG
    if (dec_debug)
      printf("%d %d\n", xd->mode_info_context->mbmi.mv[0].as_mv.row,
             xd->mode_info_context->mbmi.mv[0].as_mv.col);
#endif
    vp9_find_mv_refs(cm, xd, mi, xd->prev_mode_info_context,
                     ref0, mbmi->ref_mvs[ref0], cm->ref_frame_sign_bias);

    mv_ref_p = cm->fc.inter_mode_probs[mbmi->mb_mode_context[ref0]];

    if (vp9_segfeature_active(xd, mbmi->segment_id, SEG_LVL_SKIP)) {
      mbmi->mode = ZEROMV;
    } else if (bsize >= BLOCK_SIZE_SB8X8) {
      mbmi->mode = read_inter_mode(r, mv_ref_p);
      vp9_accum_mv_refs(cm, mbmi->mode, mbmi->mb_mode_context[ref0]);
    }
    mbmi->uv_mode = DC_PRED;

    // nearest, nearby
    if (bsize < BLOCK_SIZE_SB8X8 || mbmi->mode != ZEROMV) {
      vp9_find_best_ref_mvs(xd, mbmi->ref_mvs[ref0], &nearest, &nearby);
      best_mv.as_int = mbmi->ref_mvs[ref0][0].as_int;
    }

#ifdef DEC_DEBUG
    if (dec_debug)
      printf("[D %d %d] %d %d %d %d\n", ref_frame,
             mbmi->mb_mode_context[ref_frame],
             mv_ref_p[0], mv_ref_p[1], mv_ref_p[2], mv_ref_p[3]);
#endif

    mbmi->interp_filter = cm->mcomp_filter_type == SWITCHABLE
                              ? read_switchable_filter_type(pbi, r)
                              : cm->mcomp_filter_type;

    if (ref1 > INTRA_FRAME) {
      vp9_find_mv_refs(cm, xd, mi, xd->prev_mode_info_context,
                       ref1, mbmi->ref_mvs[ref1], cm->ref_frame_sign_bias);

      if (bsize < BLOCK_SIZE_SB8X8 || mbmi->mode != ZEROMV) {
        vp9_find_best_ref_mvs(xd, mbmi->ref_mvs[ref1],
                              &nearest_second, &nearby_second);
        best_mv_second.as_int = mbmi->ref_mvs[ref1][0].as_int;
      }
    }

#if CONFIG_INTERINTRA
    if (mbmi->ref_frame[1] <= INTRA_FRAME) {
      if ((pbi->common.use_interintra)
          && (mbmi->sb_type >= BLOCK_SIZE_SB8X8)
          && (mbmi->mode >= NEARESTMV) && (mbmi->mode <= NEWMV)
          && (mbmi->ref_frame[1] == NONE)
          ) {
        mbmi->ref_frame[1] = (vp9_read(r, pbi->common.fc.interintra_prob) ?
                              INTRA_FRAME : NONE);
        pbi->common.fc.interintra_counts[mbmi->ref_frame[1] == INTRA_FRAME]++;
        if (mbmi->ref_frame[1] == INTRA_FRAME) {
          int bsg = MIN(MIN(b_width_log2(mbmi->sb_type),
                            b_height_log2(mbmi->sb_type)), 3);
          mbmi->interintra_mode = read_intra_mode(r,
                                  pbi->common.fc.y_mode_prob[bsg]);
          pbi->common.fc.y_mode_counts[bsg][mbmi->interintra_mode]++;
#if SEPARATE_INTERINTRA_UV
          mbmi->interintra_uv_mode = read_intra_mode(r,
                           pbi->common.fc.uv_mode_prob[mbmi->interintra_mode]);
          pbi->common.fc.uv_mode_counts[mbmi->interintra_mode]
                                       [mbmi->interintra_uv_mode]++;
#else
          mbmi->interintra_uv_mode = mbmi->interintra_mode;
#endif
        }
      }
    }
#endif

    if (mbmi->sb_type < BLOCK_SIZE_SB8X8) {
      for (idy = 0; idy < 2; idy += bh) {
        for (idx = 0; idx < 2; idx += bw) {
          int_mv blockmv, secondmv;
          const int j = idy * 2 + idx;
          const int blockmode = read_inter_mode(r, mv_ref_p);

          vp9_accum_mv_refs(cm, blockmode, mbmi->mb_mode_context[ref0]);
          if (blockmode == NEARESTMV || blockmode == NEARMV) {
            vp9_append_sub8x8_mvs_for_idx(cm, xd, &nearest, &nearby, j, 0);
            if (ref1 > 0)
              vp9_append_sub8x8_mvs_for_idx(cm, xd,  &nearest_second,
                                            &nearby_second, j, 1);
          }

          switch (blockmode) {
            case NEWMV:
              read_mv(r, &blockmv.as_mv, &best_mv.as_mv, nmvc,
                      &cm->fc.NMVcount, xd->allow_high_precision_mv);

              if (ref1 > 0)
                read_mv(r, &secondmv.as_mv, &best_mv_second.as_mv, nmvc,
                        &cm->fc.NMVcount, xd->allow_high_precision_mv);

#ifdef VPX_MODE_COUNT
              vp9_mv_cont_count[mv_contz][3]++;
#endif
              break;
            case NEARESTMV:
              blockmv.as_int = nearest.as_int;
              if (ref1 > 0)
                secondmv.as_int = nearest_second.as_int;
#ifdef VPX_MODE_COUNT
              vp9_mv_cont_count[mv_contz][0]++;
#endif
              break;
            case NEARMV:
              blockmv.as_int = nearby.as_int;
              if (ref1 > 0)
                secondmv.as_int = nearby_second.as_int;
#ifdef VPX_MODE_COUNT
              vp9_mv_cont_count[mv_contz][1]++;
#endif
              break;
            case ZEROMV:
              blockmv.as_int = 0;
              if (ref1 > 0)
                secondmv.as_int = 0;
#ifdef VPX_MODE_COUNT
              vp9_mv_cont_count[mv_contz][2]++;
#endif
              break;
            default:
              assert(!"Invalid inter mode value");
          }
          mi->bmi[j].as_mv[0].as_int = blockmv.as_int;
          if (ref1 > 0)
            mi->bmi[j].as_mv[1].as_int = secondmv.as_int;

          if (bh == 2)
            vpx_memcpy(&mi->bmi[j + 2], &mi->bmi[j], sizeof(mi->bmi[j]));
          if (bw == 2)
            vpx_memcpy(&mi->bmi[j + 1], &mi->bmi[j], sizeof(mi->bmi[j]));
          mi->mbmi.mode = blockmode;
        }
      }

      mv0->as_int = mi->bmi[3].as_mv[0].as_int;
      mv1->as_int = mi->bmi[3].as_mv[1].as_int;
    } else {
      const int mb_to_top_edge = xd->mb_to_top_edge - LEFT_TOP_MARGIN;
      const int mb_to_bottom_edge = xd->mb_to_bottom_edge + RIGHT_BOTTOM_MARGIN;
      const int mb_to_left_edge = xd->mb_to_left_edge - LEFT_TOP_MARGIN;
      const int mb_to_right_edge = xd->mb_to_right_edge + RIGHT_BOTTOM_MARGIN;

      switch (mbmi->mode) {
        case NEARMV:
          // Clip "next_nearest" so that it does not extend to far out of image
          assign_and_clamp_mv(mv0, &nearby, mb_to_left_edge,
                                            mb_to_right_edge,
                                            mb_to_top_edge,
                                            mb_to_bottom_edge);
          if (ref1 > 0)
            assign_and_clamp_mv(mv1, &nearby_second, mb_to_left_edge,
                                                     mb_to_right_edge,
                                                     mb_to_top_edge,
                                                     mb_to_bottom_edge);
          break;

        case NEARESTMV:
          // Clip "next_nearest" so that it does not extend to far out of image
          assign_and_clamp_mv(mv0, &nearest, mb_to_left_edge,
                                             mb_to_right_edge,
                                             mb_to_top_edge,
                                             mb_to_bottom_edge);
          if (ref1 > 0)
            assign_and_clamp_mv(mv1, &nearest_second, mb_to_left_edge,
                                                      mb_to_right_edge,
                                                      mb_to_top_edge,
                                                      mb_to_bottom_edge);
          break;

        case ZEROMV:
          mv0->as_int = 0;
          if (ref1 > 0)
            mv1->as_int = 0;
          break;

        case NEWMV:
          read_mv(r, &mv0->as_mv, &best_mv.as_mv, nmvc, &cm->fc.NMVcount,
                  xd->allow_high_precision_mv);
          if (ref1 > 0)
            read_mv(r, &mv1->as_mv, &best_mv_second.as_mv, nmvc,
                    &cm->fc.NMVcount, xd->allow_high_precision_mv);
          break;
        default:
          assert(!"Invalid inter mode value");
      }
    }
  } else {
    mv0->as_int = 0;  // required for left and above block mv
    read_intra_block_modes(pbi, mi, r);
  }
}

void vp9_prepare_read_mode_info(VP9D_COMP* pbi, vp9_reader *r) {
  VP9_COMMON *const cm = &pbi->common;
  int k;

  // TODO(jkoleszar): does this clear more than MBSKIP_CONTEXTS? Maybe remove.
  // vpx_memset(cm->fc.mbskip_probs, 0, sizeof(cm->fc.mbskip_probs));
  for (k = 0; k < MBSKIP_CONTEXTS; ++k)
    if (vp9_read(r, VP9_MODE_UPDATE_PROB))
      vp9_diff_update_prob(r, &cm->fc.mbskip_probs[k]);

  if (cm->frame_type != KEY_FRAME && !cm->intra_only) {
    nmv_context *const nmvc = &pbi->common.fc.nmvc;
    MACROBLOCKD *const xd = &pbi->mb;
    int i, j;

    read_inter_mode_probs(&cm->fc, r);

    if (cm->mcomp_filter_type == SWITCHABLE)
      read_switchable_interp_probs(&cm->fc, r);

#if CONFIG_INTERINTRA
    if (cm->use_interintra) {
      if (vp9_read(r, VP9_UPD_INTERINTRA_PROB))
        cm->fc.interintra_prob = vp9_read_prob(r);
    }
#endif

    for (i = 0; i < INTRA_INTER_CONTEXTS; i++)
      if (vp9_read(r, VP9_MODE_UPDATE_PROB))
        vp9_diff_update_prob(r, &cm->fc.intra_inter_prob[i]);

    if (cm->allow_comp_inter_inter) {
      cm->comp_pred_mode = read_comp_pred_mode(r);
      if (cm->comp_pred_mode == HYBRID_PREDICTION)
        for (i = 0; i < COMP_INTER_CONTEXTS; i++)
          if (vp9_read(r, VP9_MODE_UPDATE_PROB))
            vp9_diff_update_prob(r, &cm->fc.comp_inter_prob[i]);
    } else {
      cm->comp_pred_mode = SINGLE_PREDICTION_ONLY;
    }

    if (cm->comp_pred_mode != COMP_PREDICTION_ONLY)
      for (i = 0; i < REF_CONTEXTS; i++) {
        if (vp9_read(r, VP9_MODE_UPDATE_PROB))
          vp9_diff_update_prob(r, &cm->fc.single_ref_prob[i][0]);

        if (vp9_read(r, VP9_MODE_UPDATE_PROB))
          vp9_diff_update_prob(r, &cm->fc.single_ref_prob[i][1]);
      }

    if (cm->comp_pred_mode != SINGLE_PREDICTION_ONLY)
      for (i = 0; i < REF_CONTEXTS; i++)
        if (vp9_read(r, VP9_MODE_UPDATE_PROB))
          vp9_diff_update_prob(r, &cm->fc.comp_ref_prob[i]);

    // VP9_INTRA_MODES
    for (j = 0; j < BLOCK_SIZE_GROUPS; j++)
      for (i = 0; i < VP9_INTRA_MODES - 1; ++i)
        if (vp9_read(r, VP9_MODE_UPDATE_PROB))
          vp9_diff_update_prob(r, &cm->fc.y_mode_prob[j][i]);

    for (j = 0; j < NUM_PARTITION_CONTEXTS; ++j)
      for (i = 0; i < PARTITION_TYPES - 1; ++i)
        if (vp9_read(r, VP9_MODE_UPDATE_PROB))
          vp9_diff_update_prob(r, &cm->fc.partition_prob[INTER_FRAME][j][i]);

    read_mv_probs(r, nmvc, xd->allow_high_precision_mv);
  }
}

void vp9_read_mode_info(VP9D_COMP* pbi, int mi_row, int mi_col, vp9_reader *r) {
  VP9_COMMON *const cm = &pbi->common;
  MACROBLOCKD *const xd = &pbi->mb;
  MODE_INFO *mi = xd->mode_info_context;
  const BLOCK_SIZE_TYPE bsize = mi->mbmi.sb_type;

  if (cm->frame_type == KEY_FRAME || cm->intra_only)
    read_intra_mode_info(pbi, mi, mi_row, mi_col, r);
  else
    read_inter_mode_info(pbi, mi, mi_row, mi_col, r);

  if (1) {
    const int bw = 1 << mi_width_log2(bsize);
    const int bh = 1 << mi_height_log2(bsize);
    const int y_mis = MIN(bh, cm->mi_rows - mi_row);
    const int x_mis = MIN(bw, cm->mi_cols - mi_col);
    int x, y;

    for (y = 0; y < y_mis; y++)
      for (x = !y; x < x_mis; x++)
        mi[y * cm->mode_info_stride + x] = *mi;
  }
}
