/*
 *  Copyright (c) 2012 The WebM project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */
#ifndef VP10_COMMON_MVREF_COMMON_H_
#define VP10_COMMON_MVREF_COMMON_H_

#include "vp10/common/onyxc_int.h"
#include "vp10/common/blockd.h"

#ifdef __cplusplus
extern "C" {
#endif

#define MVREF_NEIGHBOURS 8

typedef struct position {
  int row;
  int col;
} POSITION;

typedef enum {
  BOTH_ZERO = 0,
  ZERO_PLUS_PREDICTED = 1,
  BOTH_PREDICTED = 2,
  NEW_PLUS_NON_INTRA = 3,
  BOTH_NEW = 4,
  INTRA_PLUS_NON_INTRA = 5,
  BOTH_INTRA = 6,
  INVALID_CASE = 9
} motion_vector_context;

// This is used to figure out a context for the ref blocks. The code flattens
// an array that would have 3 possible counts (0, 1 & 2) for 3 choices by
// adding 9 for each intra block, 3 for each zero mv and 1 for each new
// motion vector. This single number is then converted into a context
// with a single lookup ( counter_to_context ).
static const int mode_2_counter[MB_MODE_COUNT] = {
  9,  // DC_PRED
  9,  // V_PRED
  9,  // H_PRED
  9,  // D45_PRED
  9,  // D135_PRED
  9,  // D117_PRED
  9,  // D153_PRED
  9,  // D207_PRED
  9,  // D63_PRED
  9,  // TM_PRED
  0,  // NEARESTMV
  0,  // NEARMV
  3,  // ZEROMV
  1,  // NEWMV
};

// There are 3^3 different combinations of 3 counts that can be either 0,1 or
// 2. However the actual count can never be greater than 2 so the highest
// counter we need is 18. 9 is an invalid counter that's never used.
static const int counter_to_context[19] = {
  BOTH_PREDICTED,  // 0
  NEW_PLUS_NON_INTRA,  // 1
  BOTH_NEW,  // 2
  ZERO_PLUS_PREDICTED,  // 3
  NEW_PLUS_NON_INTRA,  // 4
  INVALID_CASE,  // 5
  BOTH_ZERO,  // 6
  INVALID_CASE,  // 7
  INVALID_CASE,  // 8
  INTRA_PLUS_NON_INTRA,  // 9
  INTRA_PLUS_NON_INTRA,  // 10
  INVALID_CASE,  // 11
  INTRA_PLUS_NON_INTRA,  // 12
  INVALID_CASE,  // 13
  INVALID_CASE,  // 14
  INVALID_CASE,  // 15
  INVALID_CASE,  // 16
  INVALID_CASE,  // 17
  BOTH_INTRA  // 18
};

static const POSITION mv_ref_blocks[BLOCK_SIZES][MVREF_NEIGHBOURS] = {
  // 4X4
  {{-1, 0}, {0, -1}, {-1, -1}, {-2, 0}, {0, -2}, {-2, -1}, {-1, -2}, {-2, -2}},
  // 4X8
  {{-1, 0}, {0, -1}, {-1, -1}, {-2, 0}, {0, -2}, {-2, -1}, {-1, -2}, {-2, -2}},
  // 8X4
  {{-1, 0}, {0, -1}, {-1, -1}, {-2, 0}, {0, -2}, {-2, -1}, {-1, -2}, {-2, -2}},
  // 8X8
  {{-1, 0}, {0, -1}, {-1, -1}, {-2, 0}, {0, -2}, {-2, -1}, {-1, -2}, {-2, -2}},
  // 8X16
  {{0, -1}, {-1, 0}, {1, -1}, {-1, -1}, {0, -2}, {-2, 0}, {-2, -1}, {-1, -2}},
  // 16X8
  {{-1, 0}, {0, -1}, {-1, 1}, {-1, -1}, {-2, 0}, {0, -2}, {-1, -2}, {-2, -1}},
  // 16X16
  {{-1, 0}, {0, -1}, {-1, 1}, {1, -1}, {-1, -1}, {-3, 0}, {0, -3}, {-3, -3}},
  // 16X32
  {{0, -1}, {-1, 0}, {2, -1}, {-1, -1}, {-1, 1}, {0, -3}, {-3, 0}, {-3, -3}},
  // 32X16
  {{-1, 0}, {0, -1}, {-1, 2}, {-1, -1}, {1, -1}, {-3, 0}, {0, -3}, {-3, -3}},
  // 32X32
  {{-1, 1}, {1, -1}, {-1, 2}, {2, -1}, {-1, -1}, {-3, 0}, {0, -3}, {-3, -3}},
  // 32X64
  {{0, -1}, {-1, 0}, {4, -1}, {-1, 2}, {-1, -1}, {0, -3}, {-3, 0}, {2, -1}},
  // 64X32
  {{-1, 0}, {0, -1}, {-1, 4}, {2, -1}, {-1, -1}, {-3, 0}, {0, -3}, {-1, 2}},
  // 64X64
  {{-1, 3}, {3, -1}, {-1, 4}, {4, -1}, {-1, -1}, {-1, 0}, {0, -1}, {-1, 6}}
};

static const int idx_n_column_to_subblock[4][2] = {
  {1, 2},
  {1, 3},
  {3, 2},
  {3, 3}
};

// clamp_mv_ref
#define MV_BORDER (8 << 3)  // Allow 8 pels in 1/8th pel units

static INLINE void clamp_mv_ref(MV *mv, int bw, int bh, const MACROBLOCKD *xd) {
  clamp_mv(mv, xd->mb_to_left_edge - bw * 8 - MV_BORDER,
               xd->mb_to_right_edge + bw * 8 + MV_BORDER,
               xd->mb_to_top_edge - bh * 8 - MV_BORDER,
               xd->mb_to_bottom_edge + bh * 8 + MV_BORDER);
}

// This function returns either the appropriate sub block or block's mv
// on whether the block_size < 8x8 and we have check_sub_blocks set.
static INLINE int_mv get_sub_block_mv(const MODE_INFO *candidate, int which_mv,
                                      int search_col, int block_idx) {
  return block_idx >= 0 && candidate->mbmi.sb_type < BLOCK_8X8
          ? candidate->bmi[idx_n_column_to_subblock[block_idx][search_col == 0]]
              .as_mv[which_mv]
          : candidate->mbmi.mv[which_mv];
}


// Performs mv sign inversion if indicated by the reference frame combination.
static INLINE int_mv scale_mv(const MB_MODE_INFO *mbmi, int ref,
                              const MV_REFERENCE_FRAME this_ref_frame,
                              const int *ref_sign_bias) {
  int_mv mv = mbmi->mv[ref];
  if (ref_sign_bias[mbmi->ref_frame[ref]] != ref_sign_bias[this_ref_frame]) {
    mv.as_mv.row *= -1;
    mv.as_mv.col *= -1;
  }
  return mv;
}

#define CLIP_IN_ADD(mv, bw, bh, xd) clamp_mv_ref(mv, bw, bh, xd)

// This macro is used to add a motion vector mv_ref list if it isn't
// already in the list.  If it's the second motion vector it will also
// skip all additional processing and jump to done!
#define ADD_MV_REF_LIST(mv, refmv_count, mv_ref_list, bw, bh, xd, Done) \
  do { \
    (mv_ref_list)[(refmv_count)] = (mv); \
    CLIP_IN_ADD(&(mv_ref_list)[(refmv_count)].as_mv, (bw), (bh), (xd)); \
    if (refmv_count && (mv_ref_list)[1].as_int != (mv_ref_list)[0].as_int) { \
        (refmv_count) = 2; \
        goto Done; \
    } \
    (refmv_count) = 1; \
  } while (0)

// If either reference frame is different, not INTRA, and they
// are different from each other scale and add the mv to our list.
#define IF_DIFF_REF_FRAME_ADD_MV(mbmi, ref_frame, ref_sign_bias, refmv_count, \
                                 mv_ref_list, bw, bh, xd, Done) \
  do { \
    if (is_inter_block(mbmi)) { \
      if ((mbmi)->ref_frame[0] != ref_frame) \
        ADD_MV_REF_LIST(scale_mv((mbmi), 0, ref_frame, ref_sign_bias), \
                        refmv_count, mv_ref_list, bw, bh, xd, Done); \
      if (has_second_ref(mbmi) && \
          (mbmi)->ref_frame[1] != ref_frame) \
        ADD_MV_REF_LIST(scale_mv((mbmi), 1, ref_frame, ref_sign_bias), \
                        refmv_count, mv_ref_list, bw, bh, xd, Done); \
    } \
  } while (0)

#if CONFIG_PREV_MVREF
// Check whether smooth translational motion exist cross adjacent frames
// through forward prediciton
#define HAVE_SMOOTH_FORWARD_PREDICTION(cm, prev_ref_frame, curr_ref_frame) \
  (((prev_ref_frame) >= LAST_FRAME) && \
  ((prev_ref_frame) <= ALTREF_FRAME) && \
  ((cm)->frame_refs[(curr_ref_frame)-LAST_FRAME].idx == \
    (cm)->prev_new_fb_idx) && \
  ((cm)->prev_ref_buf_idx[(prev_ref_frame)-LAST_FRAME] == \
    (cm)->prev2_new_fb_idx) && \
  ((cm)->ref_frame_sign_bias[curr_ref_frame] == \
    (cm)->ref_frame_sign_bias[LAST_FRAME]) && \
  ((cm)->prev_ref_sign_bias[prev_ref_frame] == \
    (cm)->prev_ref_sign_bias[LAST_FRAME]))
#endif  // CONFIG_PREV_MVREF

#if CONFIG_PREV_MVREF
// Check whether the two reference frames are indeed identical and
// both provide a backward prediction
#define ARE_IDENTICAL_BACKWARD_REFS(cm, prev_ref_frame, curr_ref_frame) \
  (ARE_IDENTICAL_REF_FRAMES(cm, prev_ref_frame, curr_ref_frame) && \
  ((cm)->ref_frame_sign_bias[curr_ref_frame] == \
    (cm)->ref_frame_sign_bias[ALTREF_FRAME]))
#endif  // CONFIG_PREV_MVREF

#if CONFIG_PREV_MVREF
// Check whether the two reference frames are indeed identical
#define ARE_IDENTICAL_REF_FRAMES(cm, prev_ref_frame, curr_ref_frame) \
  (((prev_ref_frame) >= LAST_FRAME) && \
  ((prev_ref_frame) <= ALTREF_FRAME) && \
  ((cm)->frame_refs[(curr_ref_frame)-LAST_FRAME].idx == \
  (cm)->prev_ref_buf_idx[(prev_ref_frame)-LAST_FRAME]))
#endif  // CONFIG_PREV_MVREF

// Checks that the given mi_row, mi_col and search point
// are inside the borders of the tile.
static INLINE int is_inside(const TileInfo *const tile,
                            int mi_col, int mi_row, int mi_rows,
                            const POSITION *mi_pos) {
  return !(mi_row + mi_pos->row < 0 ||
           mi_col + mi_pos->col < tile->mi_col_start ||
           mi_row + mi_pos->row >= mi_rows ||
           mi_col + mi_pos->col >= tile->mi_col_end);
}

typedef void (*find_mv_refs_sync)(void *const data, int mi_row);
void vp10_find_mv_refs(const VP10_COMMON *cm, const MACROBLOCKD *xd,
                       MODE_INFO *mi, MV_REFERENCE_FRAME ref_frame,
#if CONFIG_REF_MV
                       uint8_t *ref_mv_count,
                       CANDIDATE_MV *ref_mv_stack,
#endif
                       int_mv *mv_ref_list, int mi_row, int mi_col,
                       find_mv_refs_sync sync, void *const data,
                       int16_t *mode_context);

// check a list of motion vectors by sad score using a number rows of pixels
// above and a number cols of pixels in the left to select the one with best
// score to use as ref motion vector
void vp10_find_best_ref_mvs(int allow_hp,
                           int_mv *mvlist, int_mv *nearest_mv, int_mv *near_mv);

void vp10_append_sub8x8_mvs_for_idx(VP10_COMMON *cm, MACROBLOCKD *xd,
                                    int block, int ref, int mi_row, int mi_col,
                                    int_mv *nearest_mv, int_mv *near_mv);

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // VP10_COMMON_MVREF_COMMON_H_
