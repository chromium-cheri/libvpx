/*
 *  Copyright (c) 2010 The WebM project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */


#ifndef VP9_ENCODER_VP9_RDOPT_H_
#define VP9_ENCODER_VP9_RDOPT_H_

#define RDCOST(RM,DM,R,D) ( ((128+((int64_t)R)*(RM)) >> 8) + ((int64_t)DM)*(D) )
#define RDCOST_8x8(RM,DM,R,D) ( ((128+((int64_t)R)*(RM)) >> 8) + ((int64_t)DM)*(D) )

extern void vp9_initialize_rd_consts(VP9_COMP *cpi, int Qvalue);

extern void vp9_initialize_me_consts(VP9_COMP *cpi, int QIndex);

extern void vp9_rd_pick_intra_mode(VP9_COMP *cpi, MACROBLOCK *x,
                                   int *r, int *d);

extern void vp9_rd_pick_intra_mode_sb32(VP9_COMP *cpi, MACROBLOCK *x,
                                        int *r, int *d);

extern void vp9_rd_pick_intra_mode_sb64(VP9_COMP *cpi, MACROBLOCK *x,
                                        int *r, int *d);

extern void vp9_pick_mode_inter_macroblock(VP9_COMP *cpi, MACROBLOCK *x,
                                           int mb_row, int mb_col,
                                           int *r, int *d);

extern int64_t vp9_rd_pick_inter_mode_sb32(VP9_COMP *cpi, MACROBLOCK *x,
                                           int mb_row, int mb_col,
                                           int *r, int *d);

extern int64_t vp9_rd_pick_inter_mode_sb64(VP9_COMP *cpi, MACROBLOCK *x,
                                           int mb_row, int mb_col,
                                           int *r, int *d);

extern void vp9_init_me_luts();

extern void vp9_set_mbmode_and_mvs(MACROBLOCK *x,
                                   MB_PREDICTION_MODE mb, int_mv *mv);

static void setup_pred_block_pointers(uint8_t **y, uint8_t **u, uint8_t **v,
                                      const YV12_BUFFER_CONFIG *src,
                                      int mb_row, int mb_col) {
  const int recon_y_stride = src->y_stride;
  const int recon_uv_stride = src->uv_stride;
  const int recon_yoffset = 16 * mb_row * recon_y_stride + 16 * mb_col;
  const int recon_uvoffset = 8 * mb_row * recon_uv_stride + 8 * mb_col;

  *y = src->y_buffer + recon_yoffset;
  *u = src->u_buffer + recon_uvoffset;
  *v = src->v_buffer + recon_uvoffset;
}

static void setup_pred_block(YV12_BUFFER_CONFIG *dst,
                             const YV12_BUFFER_CONFIG *src,
                             int mb_row, int mb_col) {
  *dst = *src;
  setup_pred_block_pointers(&dst->y_buffer, &dst->u_buffer, & dst->v_buffer,
                            src, mb_row, mb_col);
}

#endif  // VP9_ENCODER_VP9_RDOPT_H_
