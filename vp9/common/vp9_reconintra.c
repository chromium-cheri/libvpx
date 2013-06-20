/*
 *  Copyright (c) 2010 The WebM project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include <stdio.h>

#include "./vpx_config.h"
#include "vp9_rtcd.h"
#include "vp9/common/vp9_reconintra.h"
#include "vp9/common/vp9_onyxc_int.h"
#include "vpx_mem/vpx_mem.h"

static void d27_predictor(uint8_t *ypred_ptr, int y_stride,
                          int bw, int bh,
                          uint8_t *yabove_row, uint8_t *yleft_col) {
  int r, c;
  // first column
  for (r = 0; r < bh - 1; ++r) {
      ypred_ptr[r * y_stride] = ROUND_POWER_OF_TWO(yleft_col[r] +
                                                   yleft_col[r + 1], 1);
  }
  ypred_ptr[(bh - 1) * y_stride] = yleft_col[bh-1];
  ypred_ptr++;
  // second column
  for (r = 0; r < bh - 2; ++r) {
      ypred_ptr[r * y_stride] = ROUND_POWER_OF_TWO(yleft_col[r] +
                                                   yleft_col[r + 1] * 2 +
                                                   yleft_col[r + 2], 2);
  }
  ypred_ptr[(bh - 2) * y_stride] = ROUND_POWER_OF_TWO(yleft_col[bh - 2] +
                                                      yleft_col[bh - 1] * 3,
                                                      2);
  ypred_ptr[(bh - 1) * y_stride] = yleft_col[bh-1];
  ypred_ptr++;

  // rest of last row
  for (c = 0; c < bw - 2; ++c) {
    ypred_ptr[(bh - 1) * y_stride + c] = yleft_col[bh-1];
  }

  for (r = bh - 2; r >= 0; --r) {
    for (c = 0; c < bw - 2; ++c) {
      ypred_ptr[r * y_stride + c] = ypred_ptr[(r + 1) * y_stride + c - 2];
    }
  }
}

static void d63_predictor(uint8_t *ypred_ptr, int y_stride,
                          int bw, int bh,
                          uint8_t *yabove_row, uint8_t *yleft_col) {
  int r, c;
  for (r = 0; r < bh; ++r) {
    for (c = 0; c < bw; ++c) {
      if (r & 1) {
        ypred_ptr[c] = ROUND_POWER_OF_TWO(yabove_row[r/2 + c] +
                                          yabove_row[r/2 + c + 1] * 2 +
                                          yabove_row[r/2 + c + 2], 2);
      } else {
        ypred_ptr[c] =ROUND_POWER_OF_TWO(yabove_row[r/2 + c] +
                                         yabove_row[r/2+ c + 1], 1);
      }
    }
    ypred_ptr += y_stride;
  }
}

static void d45_predictor(uint8_t *ypred_ptr, int y_stride,
                          int bw, int bh,
                          uint8_t *yabove_row, uint8_t *yleft_col) {
  int r, c;
  for (r = 0; r < bh; ++r) {
    for (c = 0; c < bw; ++c) {
      if (r + c + 2 < bw * 2)
        ypred_ptr[c] = ROUND_POWER_OF_TWO(yabove_row[r + c] +
                                          yabove_row[r + c + 1] * 2 +
                                          yabove_row[r + c + 2], 2);
      else
        ypred_ptr[c] = yabove_row[bw * 2 - 1];
    }
    ypred_ptr += y_stride;
  }
}

static void d117_predictor(uint8_t *ypred_ptr, int y_stride,
                           int bw, int bh,
                           uint8_t *yabove_row, uint8_t *yleft_col) {
  int r, c;
  // first row
  for (c = 0; c < bw; c++)
    ypred_ptr[c] = ROUND_POWER_OF_TWO(yabove_row[c - 1] + yabove_row[c], 1);
  ypred_ptr += y_stride;

  // second row
  ypred_ptr[0] = ROUND_POWER_OF_TWO(yleft_col[0] +
                                    yabove_row[-1] * 2 +
                                    yabove_row[0], 2);
  for (c = 1; c < bw; c++)
    ypred_ptr[c] = ROUND_POWER_OF_TWO(yabove_row[c - 2] +
                                      yabove_row[c - 1] * 2 +
                                      yabove_row[c], 2);
  ypred_ptr += y_stride;

  // the rest of first col
  ypred_ptr[0] = ROUND_POWER_OF_TWO(yabove_row[-1] +
                                    yleft_col[0] * 2 +
                                    yleft_col[1], 2);
  for (r = 3; r < bh; ++r)
    ypred_ptr[(r-2) * y_stride] = ROUND_POWER_OF_TWO(yleft_col[r - 3] +
                                                     yleft_col[r - 2] * 2 +
                                                     yleft_col[r - 1], 2);
  // the rest of the block
  for (r = 2; r < bh; ++r) {
    for (c = 1; c < bw; c++)
      ypred_ptr[c] = ypred_ptr[-2 * y_stride + c - 1];
    ypred_ptr += y_stride;
  }
}


static void d135_predictor(uint8_t *ypred_ptr, int y_stride,
                           int bw, int bh,
                           uint8_t *yabove_row, uint8_t *yleft_col) {
  int r, c;
  ypred_ptr[0] = ROUND_POWER_OF_TWO(yleft_col[0] +
                                    yabove_row[-1] * 2 +
                                    yabove_row[0], 2);
  for (c = 1; c < bw; c++)
    ypred_ptr[c] = ROUND_POWER_OF_TWO(yabove_row[c - 2] +
                                      yabove_row[c - 1] * 2 +
                                      yabove_row[c], 2);

  ypred_ptr[y_stride] = ROUND_POWER_OF_TWO(yabove_row[-1] +
                                           yleft_col[0] * 2 +
                                           yleft_col[1], 2);
  for (r = 2; r < bh; ++r)
    ypred_ptr[r * y_stride] = ROUND_POWER_OF_TWO(yleft_col[r - 2] +
                                                 yleft_col[r - 1] * 2 +
                                                 yleft_col[r], 2);

  ypred_ptr += y_stride;
  for (r = 1; r < bh; ++r) {
    for (c = 1; c < bw; c++)
      ypred_ptr[c] = ypred_ptr[-y_stride + c - 1];
    ypred_ptr += y_stride;
  }
}

static void d153_predictor(uint8_t *ypred_ptr,
                           int y_stride,
                           int bw, int bh,
                           uint8_t *yabove_row,
                           uint8_t *yleft_col) {
  int r, c;
  ypred_ptr[0] = ROUND_POWER_OF_TWO(yabove_row[-1] + yleft_col[0], 1);
  for (r = 1; r < bh; r++)
    ypred_ptr[r * y_stride] =
        ROUND_POWER_OF_TWO(yleft_col[r - 1] + yleft_col[r], 1);
  ypred_ptr++;

  ypred_ptr[0] = ROUND_POWER_OF_TWO(yleft_col[0] +
                                    yabove_row[-1] * 2 +
                                    yabove_row[0], 2);
  ypred_ptr[y_stride] = ROUND_POWER_OF_TWO(yabove_row[-1] +
                                           yleft_col[0] * 2 +
                                           yleft_col[1], 2);
  for (r = 2; r < bh; r++)
    ypred_ptr[r * y_stride] = ROUND_POWER_OF_TWO(yleft_col[r - 2] +
                                                 yleft_col[r - 1] * 2 +
                                                 yleft_col[r], 2);
  ypred_ptr++;

  for (c = 0; c < bw - 2; c++)
    ypred_ptr[c] = ROUND_POWER_OF_TWO(yabove_row[c - 1] +
                                      yabove_row[c] * 2 +
                                      yabove_row[c + 1], 2);
  ypred_ptr += y_stride;
  for (r = 1; r < bh; ++r) {
    for (c = 0; c < bw - 2; c++)
      ypred_ptr[c] = ypred_ptr[-y_stride + c - 2];
    ypred_ptr += y_stride;
  }
}

void vp9_build_intra_predictors(uint8_t *src, int src_stride,
                                uint8_t *ypred_ptr,
                                int y_stride, int mode,
                                int bw, int bh,
                                int up_available, int left_available,
                                int right_available) {
  int r, c, i;
  uint8_t yleft_col[64], yabove_data[129], ytop_left;
  uint8_t *yabove_row = yabove_data + 1;

  // 127 127 127 .. 127 127 127 127 127 127
  // 129  A   B  ..  Y   Z
  // 129  C   D  ..  W   X
  // 129  E   F  ..  U   V
  // 129  G   H  ..  S   T   T   T   T   T
  // ..

  if (left_available) {
    for (i = 0; i < bh; i++)
      yleft_col[i] = src[i * src_stride - 1];
  } else {
    vpx_memset(yleft_col, 129, bh);
  }

  if (up_available) {
    uint8_t *yabove_ptr = src - src_stride;
    vpx_memcpy(yabove_row, yabove_ptr, bw);
    if (bw == 4 && right_available)
      vpx_memcpy(yabove_row + bw, yabove_ptr + bw, bw);
    else
      vpx_memset(yabove_row + bw, yabove_row[bw -1], bw);
    ytop_left = left_available ? yabove_ptr[-1] : 129;
  } else {
    vpx_memset(yabove_row, 127, bw * 2);
    ytop_left = 127;
  }
  yabove_row[-1] = ytop_left;

  switch (mode) {
    case DC_PRED: {
      int i;
      int expected_dc = 128;
      int average = 0;
      int count = 0;

      if (up_available || left_available) {
        if (up_available) {
          for (i = 0; i < bw; i++)
            average += yabove_row[i];
          count += bw;
        }
        if (left_available) {
          for (i = 0; i < bh; i++)
            average += yleft_col[i];
          count += bh;
        }
        expected_dc = (average + (count >> 1)) / count;
      }
      for (r = 0; r < bh; r++) {
        vpx_memset(ypred_ptr, expected_dc, bw);
        ypred_ptr += y_stride;
      }
    }
    break;
    case V_PRED:
      for (r = 0; r < bh; r++) {
        vpx_memcpy(ypred_ptr, yabove_row, bw);
        ypred_ptr += y_stride;
      }
      break;
    case H_PRED:
      for (r = 0; r < bh; r++) {
        vpx_memset(ypred_ptr, yleft_col[r], bw);
        ypred_ptr += y_stride;
      }
      break;
    case TM_PRED:
      for (r = 0; r < bh; r++) {
        for (c = 0; c < bw; c++)
          ypred_ptr[c] = clip_pixel(yleft_col[r] + yabove_row[c] - ytop_left);
        ypred_ptr += y_stride;
      }
      break;
    case D45_PRED:
    case D135_PRED:
    case D117_PRED:
    case D153_PRED:
    case D27_PRED:
    case D63_PRED:
      if (bw == bh) {
        switch (mode) {
          case D45_PRED:
            d45_predictor(ypred_ptr, y_stride, bw, bh,  yabove_row, yleft_col);
            break;
          case D135_PRED:
            d135_predictor(ypred_ptr, y_stride, bw, bh,  yabove_row, yleft_col);
            break;
          case D117_PRED:
            d117_predictor(ypred_ptr, y_stride, bw, bh,  yabove_row, yleft_col);
            break;
          case D153_PRED:
            d153_predictor(ypred_ptr, y_stride, bw, bh,  yabove_row, yleft_col);
            break;
          case D27_PRED:
            d27_predictor(ypred_ptr, y_stride, bw, bh,  yabove_row, yleft_col);
            break;
          case D63_PRED:
            d63_predictor(ypred_ptr, y_stride, bw, bh,  yabove_row, yleft_col);
            break;
          default:
            assert(0);
        }
      } else if (bw > bh) {
        uint8_t pred[64*64];
        vpx_memset(yleft_col + bh, yleft_col[bh - 1], bw - bh);
        switch (mode) {
          case D45_PRED:
            d45_predictor(pred, 64, bw, bw,  yabove_row, yleft_col);
            break;
          case D135_PRED:
            d135_predictor(pred, 64, bw, bw,  yabove_row, yleft_col);
            break;
          case D117_PRED:
            d117_predictor(pred, 64, bw, bw,  yabove_row, yleft_col);
            break;
          case D153_PRED:
            d153_predictor(pred, 64, bw, bw,  yabove_row, yleft_col);
            break;
          case D27_PRED:
            d27_predictor(pred, 64, bw, bw,  yabove_row, yleft_col);
            break;
          case D63_PRED:
            d63_predictor(pred, 64, bw, bw,  yabove_row, yleft_col);
            break;
          default:
            assert(0);
        }
        for (i = 0; i < bh; i++)
          vpx_memcpy(ypred_ptr + y_stride * i, pred + i * 64, bw);
      } else {
        uint8_t pred[64 * 64];
        vpx_memset(yabove_row + bw * 2, yabove_row[bw * 2 - 1], (bh - bw) * 2);
        switch (mode) {
          case D45_PRED:
            d45_predictor(pred, 64, bh, bh,  yabove_row, yleft_col);
            break;
          case D135_PRED:
            d135_predictor(pred, 64, bh, bh,  yabove_row, yleft_col);
            break;
          case D117_PRED:
            d117_predictor(pred, 64, bh, bh,  yabove_row, yleft_col);
            break;
          case D153_PRED:
            d153_predictor(pred, 64, bh, bh,  yabove_row, yleft_col);
            break;
          case D27_PRED:
            d27_predictor(pred, 64, bh, bh,  yabove_row, yleft_col);
            break;
          case D63_PRED:
            d63_predictor(pred, 64, bh, bh,  yabove_row, yleft_col);
            break;
          default:
            assert(0);
        }
        for (i = 0; i < bh; i++)
          vpx_memcpy(ypred_ptr + y_stride * i, pred + i * 64, bw);
      }
      break;
    default:
      break;
  }
}

void vp9_build_intra_predictors_sby_s(MACROBLOCKD *xd,
                                      BLOCK_SIZE_TYPE bsize) {
  const struct macroblockd_plane* const pd = &xd->plane[0];
  const int bw = plane_block_width(bsize, pd);
  const int bh = plane_block_height(bsize, pd);
  vp9_build_intra_predictors(pd->dst.buf, pd->dst.stride,
                             pd->dst.buf, pd->dst.stride,
                             xd->mode_info_context->mbmi.mode,
                             bw, bh, xd->up_available, xd->left_available,
                             0 /*xd->right_available*/);
}

void vp9_build_intra_predictors_sbuv_s(MACROBLOCKD *xd,
                                       BLOCK_SIZE_TYPE bsize) {
  const int bwl = b_width_log2(bsize), bw = 2 << bwl;
  const int bhl = b_height_log2(bsize), bh = 2 << bhl;

  vp9_build_intra_predictors(xd->plane[1].dst.buf, xd->plane[1].dst.stride,
                             xd->plane[1].dst.buf, xd->plane[1].dst.stride,
                             xd->mode_info_context->mbmi.uv_mode,
                             bw, bh, xd->up_available,
                             xd->left_available, 0 /*xd->right_available*/);
  vp9_build_intra_predictors(xd->plane[2].dst.buf, xd->plane[1].dst.stride,
                             xd->plane[2].dst.buf, xd->plane[1].dst.stride,
                             xd->mode_info_context->mbmi.uv_mode,
                             bw, bh, xd->up_available,
                             xd->left_available, 0 /*xd->right_available*/);
}

void vp9_predict_intra_block(MACROBLOCKD *xd,
                            int block_idx,
                            int bwl_in,
                            TX_SIZE tx_size,
                            int mode,
                            uint8_t *predictor, int pre_stride) {
  const int bwl = bwl_in - tx_size;
  const int wmask = (1 << bwl) - 1;
  const int have_top = (block_idx >> bwl) || xd->up_available;
  const int have_left = (block_idx & wmask) || xd->left_available;
  const int have_right = ((block_idx & wmask) != wmask);
  const int txfm_block_size = 4 << tx_size;

  assert(bwl >= 0);
  vp9_build_intra_predictors(predictor, pre_stride,
                             predictor, pre_stride,
                             mode,
                             txfm_block_size,
                             txfm_block_size,
                             have_top, have_left,
                             have_right);
}

void vp9_intra4x4_predict(MACROBLOCKD *xd,
                          int block_idx,
                          BLOCK_SIZE_TYPE bsize,
                          int mode,
                          uint8_t *predictor, int pre_stride) {
  vp9_predict_intra_block(xd, block_idx, b_width_log2(bsize), TX_4X4,
                          mode, predictor, pre_stride);
}
#if CONFIG_BM_INTRA
const int mi_index_map[8][8] =
{
    {0, 1, 4, 5, 16, 17, 20, 21},
    {2, 3, 6, 7, 18, 19, 22, 23},
    {8, 9, 12, 13, 24, 25, 28, 29},
    {10, 11, 14, 15, 26, 27, 30, 31},
    {32, 33, 36, 37, 48, 49, 52, 53},
    {34, 35, 38, 39, 50, 51, 54, 55},
    {40, 41, 44, 45, 56, 57, 60, 61},
    {42, 43, 46, 47, 58, 59, 62, 63}
};

int sad_L_A(uint8_t *src, int src_stride, uint8_t *pred_ptr, int pred_stride,
        int l, int n, int *sad_l, int *sad_a)
{
  assert(src != pred_ptr);

  int sad = 0, r_idx, c_idx, i, temp;
  int sad_temp_l = 0, sad_temp_a = 0;

  for(i = 0; i < l; i++)
  {
    for(r_idx = l; r_idx < (l + n); r_idx++)
    {
      temp = abs(src[r_idx * src_stride + i] - pred_ptr[r_idx * pred_stride + i]) * (2);
      sad = sad + temp;
      sad_temp_l = sad_temp_l + temp;
    }
    for(c_idx = l; c_idx < (l + n); c_idx++)
    {
      temp = abs(src[c_idx + i * src_stride] - pred_ptr[c_idx + i * pred_stride]) * (2);
      sad = sad + temp;
      sad_temp_a = sad_temp_a + temp;
    }
  }

  for(r_idx = 0; r_idx < l; r_idx ++)
    for(c_idx = 0; c_idx < l; c_idx ++)
    {
      temp = abs(src[r_idx * src_stride + c_idx] - pred_ptr[r_idx * pred_stride + c_idx]) * 2;
      sad = sad + temp;
    }

  *sad_l = sad_temp_l;
  *sad_a = sad_temp_a;
  return sad;
}

int template_matching_L(VP9_COMMON *cm, MACROBLOCKD *xd, uint8_t *src, int src_stride,
                            int mi_row, int mi_col, int *best_r, int *best_c,
                            int l, int n, int *sad_l, int *sad_a)
{
  int r_idx, c_idx;
  int best_sad = n*n*256, this_sad;
  const struct macroblockd_plane* const pd = &xd->plane[0];
  int64_t local_txfm_cache[NB_TXFM_MODES];
  int mi_row_this, mi_col_this;
  int r_start, r_end, c_start, c_end;
  int sad_l_temp, sad_a_temp;

  assert(xd->mode_info_context->mbmi.sb_type == BLOCK_SIZE_SB8X8);

  r_start = (8 * (mi_row - 4) > 0) ? (8 * (mi_row - 4) > 0) : 0;
  r_end = (8 * (mi_row + 4)) < 8 * (cm->mi_rows) ? (8 * (mi_row + 4)) : 8 * (cm->mi_rows);
  c_start = (8 * (mi_col - 3) > 0) ? (8 * (mi_col - 3) > 0) : 0;
  c_end = (8 * (mi_col + 3)) < 8 * (cm->mi_cols) ? (8 * (mi_col + 3)) : 8 * (cm->mi_cols);

  r_start = 0;
  r_end = cm -> height - l - n ;
  c_start = 0;
  c_end = cm -> width - l - n;


  *best_r = *best_c = 0;
  for(r_idx = r_start; r_idx < r_end; r_idx++)
  {
    mi_row_this = (r_idx + l + n - 1) >> 3;
    for(c_idx = c_start; c_idx < c_end; c_idx++)
    {
      mi_col_this = (c_idx + l + n - 1) >> 3;
      if((mi_row_this >> 3) > (mi_row >> 3))
        continue;
      else if((mi_row_this >> 3) == (mi_row >> 3))
      {
        if((mi_col_this >> 3) > (mi_col >> 3))
          continue;
        else if ((mi_col_this >> 3) == (mi_col >> 3))
        {
          int this_r = mi_row_this - ((mi_row_this >> 3) << 3), this_c = mi_col_this - ((mi_col_this >> 3) << 3);
          int r = mi_row - ((mi_row >> 3) << 3), c = mi_col - ((mi_col >> 3) << 3);

          if(mi_index_map[this_r][this_c] >= mi_index_map[r][c])
            continue;
        }
      }

      const int x = (1 * c_idx) >> pd->subsampling_x;
      const int y = (1 * r_idx) >> pd->subsampling_y;
      this_sad = sad_L_A(src - l * src_stride - l, src_stride,
                         cm -> yv12_fb[cm -> new_fb_idx].y_buffer + r_idx * pd->dst.stride + c_idx, pd->dst.stride,
                         l, n, &sad_l_temp, &sad_a_temp);
      //this_sad = sad_L(src - l * src_stride - l, src_stride,
        //                       cm -> yv12_fb[cm -> new_fb_idx].y_buffer + r_idx * pd->dst.stride + c_idx, pd->dst.stride,
          //                     l, n);
      if(this_sad < best_sad)
      {
        best_sad = this_sad;
        *best_r = r_idx;
        *best_c = c_idx;
        *sad_l = sad_l_temp;
        *sad_a = sad_a_temp;
      }
    }
  }
  *best_r = *best_r + l;
  *best_c = *best_c + l;
  return best_sad;
}

void copy_block(uint8_t *dst_ptr, int dst_stride, uint8_t *src_ptr, int src_stride, int rows, int cols)
{
  int r_idx, c_idx;
  for(r_idx = 0; r_idx < rows; r_idx++)
    for(c_idx = 0; c_idx < cols; c_idx++)
    {
      dst_ptr[r_idx * dst_stride + c_idx] = src_ptr[r_idx * src_stride + c_idx];
    }
}
#endif
