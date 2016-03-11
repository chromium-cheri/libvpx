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

#include "vpx_mem/vpx_mem.h"

#include "vp10/common/pred_common.h"
#include "vp10/common/tile_common.h"

#include "vp10/encoder/cost.h"
#include "vp10/encoder/segmentation.h"
#include "vp10/encoder/subexp.h"

void vp10_enable_segmentation(struct segmentation *seg) {
  seg->enabled = 1;
  seg->update_map = 1;
  seg->update_data = 1;
}

void vp10_disable_segmentation(struct segmentation *seg) {
  seg->enabled = 0;
  seg->update_map = 0;
  seg->update_data = 0;
}

void vp10_set_segment_data(struct segmentation *seg,
                          signed char *feature_data,
                          unsigned char abs_delta) {
  seg->abs_delta = abs_delta;

  memcpy(seg->feature_data, feature_data, sizeof(seg->feature_data));
}
void vp10_disable_segfeature(struct segmentation *seg, int segment_id,
                            SEG_LVL_FEATURES feature_id) {
  seg->feature_mask[segment_id] &= ~(1 << feature_id);
}

void vp10_clear_segdata(struct segmentation *seg, int segment_id,
                       SEG_LVL_FEATURES feature_id) {
  seg->feature_data[segment_id][feature_id] = 0;
}

// Based on set of segment counts calculate a probability tree
static void calc_segtree_probs(unsigned *segcounts,
    vpx_prob *segment_tree_probs, const vpx_prob *cur_tree_probs) {
  // Work out probabilities of each segment
  const unsigned cc[4] = {
    segcounts[0] + segcounts[1], segcounts[2] + segcounts[3],
    segcounts[4] + segcounts[5], segcounts[6] + segcounts[7]
  };
  const unsigned ccc[2] = { cc[0] + cc[1], cc[2] + cc[3] };
  int i;

  segment_tree_probs[0] = get_binary_prob(ccc[0], ccc[1]);
  segment_tree_probs[1] = get_binary_prob(cc[0], cc[1]);
  segment_tree_probs[2] = get_binary_prob(cc[2], cc[3]);
  segment_tree_probs[3] = get_binary_prob(segcounts[0], segcounts[1]);
  segment_tree_probs[4] = get_binary_prob(segcounts[2], segcounts[3]);
  segment_tree_probs[5] = get_binary_prob(segcounts[4], segcounts[5]);
  segment_tree_probs[6] = get_binary_prob(segcounts[6], segcounts[7]);

  for (i = 0; i < 7; i++) {
    const unsigned *ct = i == 0 ? ccc : i < 3 ? cc + (i & 2)
        : segcounts + (i - 3) * 2;
    vp10_prob_diff_update_savings_search(ct,
        cur_tree_probs[i], &segment_tree_probs[i], DIFF_UPDATE_PROB);
  }
}

// Based on set of segment counts and probabilities calculate a cost estimate
static int cost_segmap(unsigned *segcounts, vpx_prob *probs) {
  const int c01 = segcounts[0] + segcounts[1];
  const int c23 = segcounts[2] + segcounts[3];
  const int c45 = segcounts[4] + segcounts[5];
  const int c67 = segcounts[6] + segcounts[7];
  const int c0123 = c01 + c23;
  const int c4567 = c45 + c67;

  // Cost the top node of the tree
  int cost = c0123 * vp10_cost_zero(probs[0]) +
             c4567 * vp10_cost_one(probs[0]);

  // Cost subsequent levels
  if (c0123 > 0) {
    cost += c01 * vp10_cost_zero(probs[1]) +
            c23 * vp10_cost_one(probs[1]);

    if (c01 > 0)
      cost += segcounts[0] * vp10_cost_zero(probs[3]) +
              segcounts[1] * vp10_cost_one(probs[3]);
    if (c23 > 0)
      cost += segcounts[2] * vp10_cost_zero(probs[4]) +
              segcounts[3] * vp10_cost_one(probs[4]);
  }

  if (c4567 > 0) {
    cost += c45 * vp10_cost_zero(probs[2]) +
            c67 * vp10_cost_one(probs[2]);

    if (c45 > 0)
      cost += segcounts[4] * vp10_cost_zero(probs[5]) +
              segcounts[5] * vp10_cost_one(probs[5]);
    if (c67 > 0)
      cost += segcounts[6] * vp10_cost_zero(probs[6]) +
              segcounts[7] * vp10_cost_one(probs[6]);
  }

  return cost;
}

static void count_segs(const VP10_COMMON *cm, MACROBLOCKD *xd,
                       const TileInfo *tile, MODE_INFO **mi,
                       unsigned *no_pred_segcounts,
                       unsigned (*temporal_predictor_count)[2],
                       unsigned *t_unpred_seg_counts,
                       int bw, int bh, int mi_row, int mi_col) {
  int segment_id;

  if (mi_row >= cm->mi_rows || mi_col >= cm->mi_cols)
    return;

  xd->mi = mi;
  segment_id = xd->mi[0]->mbmi.segment_id;

  set_mi_row_col(xd, tile, mi_row, bh, mi_col, bw, cm->mi_rows, cm->mi_cols);

  // Count the number of hits on each segment with no prediction
  no_pred_segcounts[segment_id]++;

  // Temporal prediction not allowed on key frames
  if (cm->frame_type != KEY_FRAME) {
    const BLOCK_SIZE bsize = xd->mi[0]->mbmi.sb_type;
    // Test to see if the segment id matches the predicted value.
    const int pred_segment_id = get_segment_id(cm, cm->last_frame_seg_map,
                                               bsize, mi_row, mi_col);
    const int pred_flag = pred_segment_id == segment_id;
    const int pred_context = vp10_get_pred_context_seg_id(xd);

    // Store the prediction status for this mb and update counts
    // as appropriate
    xd->mi[0]->mbmi.seg_id_predicted = pred_flag;
    temporal_predictor_count[pred_context][pred_flag]++;

    // Update the "unpredicted" segment count
    if (!pred_flag)
      t_unpred_seg_counts[segment_id]++;
  }
}

static void count_segs_sb(const VP10_COMMON *cm, MACROBLOCKD *xd,
                          const TileInfo *tile, MODE_INFO **mi,
                          unsigned *no_pred_segcounts,
                          unsigned (*temporal_predictor_count)[2],
                          unsigned *t_unpred_seg_counts,
                          int mi_row, int mi_col,
                          BLOCK_SIZE bsize) {
  const int mis = cm->mi_stride;
  int bw, bh;
  const int bs = num_8x8_blocks_wide_lookup[bsize], hbs = bs / 2;

  if (mi_row >= cm->mi_rows || mi_col >= cm->mi_cols)
    return;

  bw = num_8x8_blocks_wide_lookup[mi[0]->mbmi.sb_type];
  bh = num_8x8_blocks_high_lookup[mi[0]->mbmi.sb_type];

  if (bw == bs && bh == bs) {
    count_segs(cm, xd, tile, mi, no_pred_segcounts, temporal_predictor_count,
               t_unpred_seg_counts, bs, bs, mi_row, mi_col);
  } else if (bw == bs && bh < bs) {
    count_segs(cm, xd, tile, mi, no_pred_segcounts, temporal_predictor_count,
               t_unpred_seg_counts, bs, hbs, mi_row, mi_col);
    count_segs(cm, xd, tile, mi + hbs * mis, no_pred_segcounts,
               temporal_predictor_count, t_unpred_seg_counts, bs, hbs,
               mi_row + hbs, mi_col);
  } else if (bw < bs && bh == bs) {
    count_segs(cm, xd, tile, mi, no_pred_segcounts, temporal_predictor_count,
               t_unpred_seg_counts, hbs, bs, mi_row, mi_col);
    count_segs(cm, xd, tile, mi + hbs,
               no_pred_segcounts, temporal_predictor_count, t_unpred_seg_counts,
               hbs, bs, mi_row, mi_col + hbs);
  } else {
    const BLOCK_SIZE subsize = subsize_lookup[PARTITION_SPLIT][bsize];
    int n;

    assert(bw < bs && bh < bs);

    for (n = 0; n < 4; n++) {
      const int mi_dc = hbs * (n & 1);
      const int mi_dr = hbs * (n >> 1);

      count_segs_sb(cm, xd, tile, &mi[mi_dr * mis + mi_dc],
                    no_pred_segcounts, temporal_predictor_count,
                    t_unpred_seg_counts,
                    mi_row + mi_dr, mi_col + mi_dc, subsize);
    }
  }
}

void vp10_choose_segmap_coding_method(VP10_COMMON *cm, MACROBLOCKD *xd) {
  struct segmentation *seg = &cm->seg;
  struct segmentation_probs *segp = &cm->fc->seg;

  int no_pred_cost;
  int t_pred_cost = INT_MAX;

  int i, tile_col, tile_row, mi_row, mi_col;

  unsigned (*temporal_predictor_count)[2] = cm->counts.seg.pred;
  unsigned *no_pred_segcounts = cm->counts.seg.tree_total;
  unsigned *t_unpred_seg_counts = cm->counts.seg.tree_mispred;

  vpx_prob no_pred_tree[SEG_TREE_PROBS];
  vpx_prob t_pred_tree[SEG_TREE_PROBS];
  vpx_prob t_nopred_prob[PREDICTION_PROBS];

  (void) xd;

  // First of all generate stats regarding how well the last segment map
  // predicts this one
  for (tile_row = 0; tile_row < cm->tile_rows; tile_row++) {
    TileInfo tile_info;
    vp10_tile_set_row(&tile_info, cm, tile_row);
    for (tile_col = 0; tile_col < cm->tile_cols; tile_col++) {
      MODE_INFO **mi_ptr;
      vp10_tile_set_col(&tile_info, cm, tile_col);
      mi_ptr = cm->mi_grid_visible + tile_info.mi_row_start * cm->mi_stride +
                 tile_info.mi_col_start;
      for (mi_row = tile_info.mi_row_start; mi_row < tile_info.mi_row_end;
           mi_row += 8, mi_ptr += 8 * cm->mi_stride) {
        MODE_INFO **mi = mi_ptr;
        for (mi_col = tile_info.mi_col_start; mi_col < tile_info.mi_col_end;
             mi_col += 8, mi += 8) {
          count_segs_sb(cm, xd, &tile_info, mi, no_pred_segcounts,
                        temporal_predictor_count, t_unpred_seg_counts,
                        mi_row, mi_col, BLOCK_64X64);
        }
      }
    }
  }


  // Work out probability tree for coding segments without prediction
  // and the cost.
  calc_segtree_probs(no_pred_segcounts, no_pred_tree, segp->tree_probs);
  no_pred_cost = cost_segmap(no_pred_segcounts, no_pred_tree);

  // Key frames cannot use temporal prediction
  if (!frame_is_intra_only(cm) && !cm->error_resilient_mode) {
    // Work out probability tree for coding those segments not
    // predicted using the temporal method and the cost.
    calc_segtree_probs(t_unpred_seg_counts, t_pred_tree, segp->tree_probs);
    t_pred_cost = cost_segmap(t_unpred_seg_counts, t_pred_tree);

    // Add in the cost of the signaling for each prediction context.
    for (i = 0; i < PREDICTION_PROBS; i++) {
      const int count0 = temporal_predictor_count[i][0];
      const int count1 = temporal_predictor_count[i][1];

      vp10_prob_diff_update_savings_search(temporal_predictor_count[i],
                                           segp->pred_probs[i],
                                           &t_nopred_prob[i], DIFF_UPDATE_PROB);

      // Add in the predictor signaling cost
      t_pred_cost += count0 * vp10_cost_zero(t_nopred_prob[i]) +
                     count1 * vp10_cost_one(t_nopred_prob[i]);
    }
  }

  // Now choose which coding method to use.
  if (t_pred_cost < no_pred_cost) {
    assert(!cm->error_resilient_mode);
    seg->temporal_update = 1;
  } else {
    seg->temporal_update = 0;
  }
}

void vp10_reset_segment_features(VP10_COMMON *cm) {
  struct segmentation *seg = &cm->seg;

  // Set up default state for MB feature flags
  seg->enabled = 0;
  seg->update_map = 0;
  seg->update_data = 0;
  vp10_clearall_segfeatures(seg);
}
