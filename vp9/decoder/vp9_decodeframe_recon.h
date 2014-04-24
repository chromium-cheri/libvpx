/*
 *  Copyright (c) 2013 The WebM project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */


#ifndef VP9_DECODER_VP9_DECODEFRAME_RECON_H_
#define VP9_DECODER_VP9_DECODEFRAME_RECON_H_

struct VP9Common;
struct VP9Decompressor;

int vp9_decode_frame_recon(struct VP9Decompressor *cpi,
                           const uint8_t **p_data_end);


void decode_tile_recon_entropy(VP9D_COMP *pbi, const TileInfo *const tile,
                               vp9_reader *r, int tile_col);

void decode_tile_recon_inter_transform(VP9D_COMP *pbi,
                                       const TileInfo *const tile,
                                       vp9_reader *r, int tile_col);

void decode_tile_recon_inter(VP9D_COMP *pbi, const TileInfo *const tile,
                             vp9_reader *r, int tile_col);

void decode_tile_recon_inter_rs(VP9D_COMP *pbi, const TileInfo *const tile,
                                vp9_reader *r, int tile_col);


void decode_tile_recon_intra(VP9D_COMP *pbi, const TileInfo *const tile,
                             vp9_reader *r, int tile_col);

int vp9_decode_frame_tail(VP9D_COMP *pbi);

#endif  // VP9_DECODER_VP9_DECODEFRAME_RECON_H_
