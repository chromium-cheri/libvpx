/*
 *  Copyright (c) 2014 The WebM project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "./vpx_config.h"
#include "./vpx_scale_rtcd.h"
#include "./vp9_rtcd.h"

#include "vp9/common/vp9_onyxc_int.h"
#include "vp9/common/vp9_postproc.h"

// TODO(jackychen): Replace this function with SSE2 code. There is
// one SSE2 implementation in vp8, so will consider how to share it
// between vp8 and vp9.
static void filter_by_weight(const uint8_t *src, int src_stride,
                             uint8_t *dst, int dst_stride,
                             int block_size, int src_weight) {
  const int dst_weight = (1 << MFQE_PRECISION) - src_weight;
  const int rounding_bit = 1 << (MFQE_PRECISION - 1);
  int r, c;

  for (r = 0; r < block_size; r++) {
    for (c = 0; c < block_size; c++) {
      dst[c] = (src[c] * src_weight + dst[c] * dst_weight + rounding_bit)
               >> MFQE_PRECISION;
    }
    src += src_stride;
    dst += dst_stride;
  }
}

static void filter_by_weight32x32(const uint8_t *src, int src_stride,
                                  uint8_t *dst, int dst_stride, int weight) {
  filter_by_weight(src, src_stride, dst, dst_stride, 16, weight);
  filter_by_weight(src + 16, src_stride, dst + 16, dst_stride, 16, weight);
  filter_by_weight(src + src_stride * 16, src_stride, dst + dst_stride * 16,
                   dst_stride, 16, weight);
  filter_by_weight(src + src_stride * 16 + 16, src_stride,
                   dst + dst_stride * 16 + 16, dst_stride, 16, weight);
}

static void filter_by_weight64x64(const uint8_t *src, int src_stride,
                                  uint8_t *dst, int dst_stride, int weight) {
  filter_by_weight32x32(src, src_stride, dst, dst_stride, weight);
  filter_by_weight32x32(src + 32, src_stride, dst + 32,
                        dst_stride, weight);
  filter_by_weight32x32(src + src_stride * 32, src_stride,
                        dst + dst_stride * 32, dst_stride, weight);
  filter_by_weight32x32(src + src_stride * 32 + 32, src_stride,
                        dst + dst_stride * 32 + 32, dst_stride, weight);
}

static void apply_ifactor(uint8_t *y, int y_stride, uint8_t *yd,
                          int yd_stride, uint8_t *u, uint8_t *v,
                          int uv_stride, uint8_t *ud, uint8_t *vd,
                          int uvd_stride, BLOCK_SIZE block_size,
                          int weight) {
  if (block_size == BLOCK_16X16) {
    filter_by_weight(y, y_stride, yd, yd_stride, 16, weight);
    filter_by_weight(u, uv_stride, ud, uvd_stride, 8, weight);
    filter_by_weight(v, uv_stride, vd, uvd_stride, 8, weight);
  } else if (block_size == BLOCK_32X32) {
    filter_by_weight32x32(y, y_stride, yd, yd_stride, weight);
    filter_by_weight(u, uv_stride, ud, uvd_stride, 16, weight);
    filter_by_weight(v, uv_stride, vd, uvd_stride, 16, weight);
  } else if (block_size == BLOCK_64X64) {
    filter_by_weight64x64(y, y_stride, yd, yd_stride, weight);
    filter_by_weight32x32(u, uv_stride, ud, uvd_stride, weight);
    filter_by_weight32x32(v, uv_stride, vd, uvd_stride, weight);
  }
}

// TODO(jackychen): Determine whether replace it with assembly code.
static void copy_mem8x8(uint8_t *src, int src_stride,
                        uint8_t *dst, int dst_stride) {
  int r;
  for (r = 0; r < 8; r++) {
    memcpy(dst, src, 8);
    src += src_stride;
    dst += dst_stride;
  }
}

static void copy_mem16x16(uint8_t *src, int src_stride,
                          uint8_t *dst, int dst_stride) {
  int r;
  for (r = 0; r < 16; r++) {
    memcpy(dst, src, 16);
    src += src_stride;
    dst += dst_stride;
  }
}

static void copy_mem32x32(uint8_t *src, int src_stride,
                          uint8_t *dst, int dst_stride) {
  copy_mem16x16(src, src_stride, dst, dst_stride);
  copy_mem16x16(src + 16, src_stride, dst + 16, dst_stride);
  copy_mem16x16(src + src_stride * 16, src_stride,
                dst + dst_stride * 16, dst_stride);
  copy_mem16x16(src + src_stride * 16 + 16, src_stride,
                dst + dst_stride * 16 + 16, dst_stride);
}

void copy_mem64x64(uint8_t *src, int src_stride,
                   uint8_t *dst, int dst_stride) {
  copy_mem32x32(src, src_stride, dst, dst_stride);
  copy_mem32x32(src + 32, src_stride, dst + 32, dst_stride);
  copy_mem32x32(src + src_stride * 32, src_stride,
                dst + src_stride * 32, dst_stride);
  copy_mem32x32(src + src_stride * 32 + 32, src_stride,
                dst + src_stride * 32 + 32, dst_stride);
}

static void copy_block(uint8_t *y, uint8_t *u, uint8_t *v,
                       int y_stride, int uv_stride, uint8_t *yd,
                       uint8_t *ud, uint8_t *vd, int yd_stride,
                       int uvd_stride, BLOCK_SIZE bs) {
  if (bs == BLOCK_16X16) {
    copy_mem16x16(y, y_stride, yd, yd_stride);
    copy_mem8x8(u, uv_stride, ud, uvd_stride);
    copy_mem8x8(v, uv_stride, vd, uvd_stride);
  } else if (bs == BLOCK_32X32) {
    copy_mem32x32(y, y_stride, yd, yd_stride);
    copy_mem16x16(u, uv_stride, ud, uvd_stride);
    copy_mem16x16(v, uv_stride, vd, uvd_stride);
  } else {
    copy_mem64x64(y, y_stride, yd, yd_stride);
    copy_mem32x32(u, uv_stride, ud, uvd_stride);
    copy_mem32x32(v, uv_stride, vd, uvd_stride);
  }
}

static void mfqe_block(BLOCK_SIZE bs, uint8_t *y, uint8_t *u,
                       uint8_t *v, int y_stride, int uv_stride,
                       uint8_t *yd, uint8_t *ud, uint8_t *vd,
                       int yd_stride, int uvd_stride) {
  int sad, vdiff;
  int sad_thr = 8;
  uint32_t sse;

  if (bs == BLOCK_16X16) {
    vdiff = (vp9_variance16x16(y, y_stride, yd, yd_stride, &sse) + 128) >> 8;
    sad = (vp9_sad16x16(y, y_stride, yd, yd_stride) + 128) >> 8;
  } else if (bs == BLOCK_32X32) {
    vdiff = (vp9_variance32x32(y, y_stride, yd, yd_stride, &sse) + 512) >> 10;
    sad = (vp9_sad32x32(y, y_stride, yd, yd_stride) + 512) >> 10;
  } else /* if (bs == BLOCK_64X64) */ {
    vdiff = (vp9_variance64x64(y, y_stride, yd, yd_stride, &sse) + 2048) >> 12;
    sad = (vp9_sad64x64(y, y_stride, yd, yd_stride) + 2048) >> 12;
  }

  if (bs == BLOCK_16X16) {
    sad_thr = 8;
  } else if (bs == BLOCK_32X32) {
    sad_thr = 7;
  } else {  // BLOCK_64X64
    sad_thr = 6;
  }

  // TODO(jackychen): More experiments and remove magic numbers.
  // vdiff > sad * 3 means vdiff should not be too small, otherwise,
  // it might be a lighting change in smooth area. When there is a
  // lighting change in smooth area, it is dangerous to do MFQE.
  if (sad > 1 && sad < sad_thr && vdiff > sad * 3 && vdiff < 150) {
    // TODO(jackychen): Add weighted average in the calculation.
    // Currently, the data is copied from last frame without averaging.
    apply_ifactor(y, y_stride, yd, yd_stride, u, v, uv_stride,
                  ud, vd, uvd_stride, bs, 0);
  } else {
    // Copy the block from current frame(i.e., no mfqe is done).
    copy_block(y, u, v, y_stride, uv_stride, yd, ud, vd,
               yd_stride, uvd_stride, bs);
  }
}

static int mfqe_decision(MODE_INFO *mi, BLOCK_SIZE cur_bs) {
  // Check the motion in current block(for inter frame),
  // or check the motion in the correlated block in last frame(for keyframe).
  const int mv_len_square = mi->mbmi.mv[0].as_mv.row *
                            mi->mbmi.mv[0].as_mv.row +
                            mi->mbmi.mv[0].as_mv.col *
                            mi->mbmi.mv[0].as_mv.col;
  const int mv_threshold = 100;
  return mi->mbmi.mode >= NEARESTMV &&  // Not an intra block
         cur_bs >= BLOCK_16X16 &&
         mv_len_square <= mv_threshold;
}

// Process each partiton in a super block, recursively.
static void mfqe_partition(VP9_COMMON *cm, MODE_INFO *mi, BLOCK_SIZE bs,
                           uint8_t *y, uint8_t *u, uint8_t *v, int y_stride,
                           int uv_stride, uint8_t *yd, uint8_t *ud, uint8_t *vd,
                           int yd_stride, int uvd_stride) {
  int mi_offset, y_offset, uv_offset;
  const BLOCK_SIZE cur_bs = mi->mbmi.sb_type;
  // TODO(jackychen): Consider how and whether to use qdiff in MFQE.
  // int qdiff = cm->base_qindex - cm->postproc_state.last_base_qindex;
  const int bsl = b_width_log2_lookup[bs];
  PARTITION_TYPE partition = partition_lookup[bsl][cur_bs];
  const BLOCK_SIZE subsize = get_subsize(bs, partition);

  if (cur_bs < BLOCK_8X8) {
    // If there are blocks smaller than 8x8, it must be on the boundary.
    return;
  }
  // No MFQE on blocks smaller than 16x16
  if (partition == PARTITION_SPLIT && bs == BLOCK_16X16) {
    partition = PARTITION_NONE;
  }
  switch (partition) {
    case PARTITION_HORZ:
    case PARTITION_VERT:
      // If current block size is not square.
      // Copy the block from current frame(i.e., no mfqe is done).
      // TODO(jackychen): Rectangle blocks should also be taken into account.
      copy_block(y, u, v, y_stride, uv_stride, yd, ud, vd,
                 yd_stride, uvd_stride, bs);
      break;
    case PARTITION_NONE:
      if (mfqe_decision(mi, cur_bs)) {
        // Do mfqe on this partition.
        mfqe_block(cur_bs, y, u, v, y_stride, uv_stride,
                   yd, ud, vd, yd_stride, uvd_stride);
      } else {
        // Copy the block from current frame(i.e., no mfqe is done).
        copy_block(y, u, v, y_stride, uv_stride, yd, ud, vd,
                   yd_stride, uvd_stride, bs);
      }
      break;
    case PARTITION_SPLIT:
      if (bs == BLOCK_64X64) {
        mi_offset = 4;
        y_offset = 32;
        uv_offset = 16;
      } else {
        mi_offset = 2;
        y_offset = 16;
        uv_offset = 8;
      }
      // Recursion on four square partitions, e.g. if bs is 64X64,
      // then look into four 32X32 blocks in it.
      mfqe_partition(cm, mi, subsize, y, u, v, y_stride, uv_stride, yd, ud, vd,
                     yd_stride, uvd_stride);
      mfqe_partition(cm, mi + mi_offset, subsize, y + y_offset, u + uv_offset,
                     v + uv_offset, y_stride, uv_stride, yd + y_offset,
                     ud + uv_offset, vd + uv_offset, yd_stride, uvd_stride);
      mfqe_partition(cm, mi + mi_offset * cm->mi_stride, subsize,
                     y + y_offset * y_stride, u + uv_offset * uv_stride,
                     v + uv_offset * uv_stride, y_stride, uv_stride,
                     yd + y_offset * yd_stride, ud + uv_offset * uvd_stride,
                     vd + uv_offset * uvd_stride, yd_stride, uvd_stride);
      mfqe_partition(cm, mi + mi_offset * cm->mi_stride + mi_offset,
                     subsize, y + y_offset * y_stride + y_offset,
                     u + uv_offset * uv_stride + uv_offset,
                     v + uv_offset * uv_stride + uv_offset, y_stride,
                     uv_stride, yd + y_offset * yd_stride + y_offset,
                     ud + uv_offset * uvd_stride + uv_offset,
                     vd + uv_offset * uvd_stride + uv_offset,
                     yd_stride, uvd_stride);
      break;
    default:
      assert(0);
  }
}

void vp9_mfqe(VP9_COMMON *cm) {
  int mi_row, mi_col;
  // Current decoded frame.
  const YV12_BUFFER_CONFIG *show = cm->frame_to_show;
  // Last decoded frame and will store the MFQE result.
  YV12_BUFFER_CONFIG *dest = &cm->post_proc_buffer;
  // Loop through each super block.
  for (mi_row = 0; mi_row < cm->mi_rows; mi_row += MI_BLOCK_SIZE) {
    for (mi_col = 0; mi_col < cm->mi_cols; mi_col += MI_BLOCK_SIZE) {
      MODE_INFO *mi;
      MODE_INFO *mi_local = cm->mi + (mi_row * cm->mi_stride + mi_col);
      // Motion Info in last frame.
      MODE_INFO *mi_prev = cm->postproc_state.prev_mi +
                           (mi_row * cm->mi_stride + mi_col);
      uint32_t y_stride = show->y_stride;
      uint32_t uv_stride = show->uv_stride;
      uint32_t yd_stride = dest->y_stride;
      uint32_t uvd_stride = dest->uv_stride;
      uint32_t row_offset_y = mi_row << 3;
      uint32_t row_offset_uv = mi_row << 2;
      uint32_t col_offset_y = mi_col << 3;
      uint32_t col_offset_uv = mi_col << 2;
      uint8_t *y = show->y_buffer + row_offset_y * y_stride + col_offset_y;
      uint8_t *u = show->u_buffer + row_offset_uv * uv_stride + col_offset_uv;
      uint8_t *v = show->v_buffer + row_offset_uv * uv_stride + col_offset_uv;
      uint8_t *yd = dest->y_buffer + row_offset_y * yd_stride + col_offset_y;
      uint8_t *ud = dest->u_buffer + row_offset_uv * uvd_stride +
                    col_offset_uv;
      uint8_t *vd = dest->v_buffer + row_offset_uv * uvd_stride +
                    col_offset_uv;
      if (cm->frame_type == KEY_FRAME || cm->intra_only) {
        mi = mi_prev;
      } else {
        mi = mi_local;
      }
      mfqe_partition(cm, mi, BLOCK_64X64, y, u, v, y_stride, uv_stride, yd, ud,
                     vd, yd_stride, uvd_stride);
    }
  }
}
