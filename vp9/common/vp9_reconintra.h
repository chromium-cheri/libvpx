/*
 *  Copyright (c) 2010 The WebM project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#ifndef VP9_COMMON_VP9_RECONINTRA_H_
#define VP9_COMMON_VP9_RECONINTRA_H_

#include "vpx/vpx_integer.h"
#include "vp9/common/vp9_blockd.h"

#ifdef __cplusplus
extern "C" {
#endif

void vp9_init_intra_predictors();

void vp9_predict_intra_block(const MACROBLOCKD *xd, int block_idx, int bwl_in,
                             TX_SIZE tx_size, PREDICTION_MODE mode,
#if CONFIG_FILTERINTRA
                             int filterbit,
#endif
                             const uint8_t *ref, int ref_stride,
                             uint8_t *dst, int dst_stride,
                             int aoff, int loff, int plane);

#if CONFIG_INTERINTRA
void vp9_build_interintra_predictors(MACROBLOCKD *xd,
                                     uint8_t *ypred,
                                     uint8_t *upred,
                                     uint8_t *vpred,
                                     int ystride,
                                     int ustride,
                                     int vstride,
                                     BLOCK_SIZE bsize);
void vp9_build_interintra_predictors_sby(MACROBLOCKD *xd,
                                         uint8_t *ypred,
                                         int ystride,
                                         BLOCK_SIZE bsize);
void vp9_build_interintra_predictors_sbuv(MACROBLOCKD *xd,
                                          uint8_t *upred,
                                          uint8_t *vpred,
                                          int ustride, int vstride,
                                          BLOCK_SIZE bsize);
#if CONFIG_WEDGE_PARTITION
void vp9_generate_masked_weight_interintra(int wedge_index,
                                           BLOCK_SIZE sb_type,
                                           int h, int w,
                                           uint8_t *mask, int stride);
#endif  // CONFIG_WEDGE_PARTITION
#endif  // CONFIG_INTERINTRA
#ifdef __cplusplus
}  // extern "C"
#endif

#if CONFIG_PALETTE
int generate_palette(const uint8_t *src, int stride, int rows, int cols,
                     uint8_t *palette, int *count, uint8_t *map);
int nearest_number(int num, int denom, int l_bound, int r_bound);
void reduce_palette(uint8_t *palette, int *count, int n, uint8_t *map,
                    int rows, int cols);
int run_lengh_encoding(uint8_t *seq, int n, uint16_t *runs, int max_run);
int run_lengh_decoding(uint16_t *runs, int l, uint8_t *seq);
void transpose_block(uint8_t *seq_in, uint8_t *seq_out, int rows, int cols);
/*void palette_color_insersion(uint8_t *old_colors, int *m, uint8_t *new_colors,
                             int n, int *count);*/
void palette_color_insersion1(uint8_t *old_colors, int *m, int *count,
                              MB_MODE_INFO *mbmi);
int palette_color_lookup(uint8_t *dic, int n, uint8_t val, int bits);
int palette_max_run(BLOCK_SIZE bsize);
int get_bit_depth(int n);
#endif

#endif  // VP9_COMMON_VP9_RECONINTRA_H_
