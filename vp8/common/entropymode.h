/*
 *  Copyright (c) 2010 The WebM project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */


#ifndef __INC_ENTROPYMODE_H
#define __INC_ENTROPYMODE_H

#include "onyxc_int.h"
#include "treecoder.h"

typedef const int vp8_mbsplit[16];

extern vp8_mbsplit vp8_mbsplits [VP8_NUMMBSPLITS];

extern const int vp8_mbsplit_count [VP8_NUMMBSPLITS];    /* # of subsets */

extern const vp8_prob vp8_mbsplit_probs [VP8_NUMMBSPLITS - 1];

extern int vp8_mv_cont(const int_mv *l, const int_mv *a);

extern const vp8_prob vp8_sub_mv_ref_prob [VP8_SUBMVREFS - 1];
extern const vp8_prob vp8_sub_mv_ref_prob2 [SUBMVREF_COUNT][VP8_SUBMVREFS - 1];


extern const unsigned int vp8_kf_default_bmode_counts [VP8_BINTRAMODES] [VP8_BINTRAMODES] [VP8_BINTRAMODES];


extern const vp8_tree_index vp8_bmode_tree[];

extern const vp8_tree_index  vp8_ymode_tree[];
extern const vp8_tree_index  vp8_kf_ymode_tree[];
extern const vp8_tree_index  vp8_uv_mode_tree[];
extern const vp8_tree_index  vp8_i8x8_mode_tree[];
extern const vp8_tree_index  vp8_mbsplit_tree[];
extern const vp8_tree_index  vp8_mv_ref_tree[];
extern const vp8_tree_index  vp8_sub_mv_ref_tree[];

extern struct vp8_token_struct vp8_bmode_encodings   [VP8_BINTRAMODES];
extern struct vp8_token_struct vp8_ymode_encodings   [VP8_YMODES];
extern struct vp8_token_struct vp8_kf_ymode_encodings [VP8_YMODES];
extern struct vp8_token_struct vp8_i8x8_mode_encodings  [VP8_UV_MODES];
extern struct vp8_token_struct vp8_uv_mode_encodings  [VP8_UV_MODES];
extern struct vp8_token_struct vp8_mbsplit_encodings  [VP8_NUMMBSPLITS];

/* Inter mode values do not start at zero */

extern struct vp8_token_struct vp8_mv_ref_encoding_array    [VP8_MVREFS];
extern struct vp8_token_struct vp8_sub_mv_ref_encoding_array [VP8_SUBMVREFS];

void vp8_entropy_mode_init(void);

void vp8_init_mbmode_probs(VP8_COMMON *x);
extern void vp8_init_mode_contexts(VP8_COMMON *pc);
extern void vp8_update_mode_context(VP8_COMMON *pc);;
extern void vp8_accum_mv_refs(VP8_COMMON *pc,
                              MB_PREDICTION_MODE m,
                              const int ct[4]);

void vp8_default_bmode_probs(vp8_prob dest [VP8_BINTRAMODES - 1]);
void vp8_kf_default_bmode_probs(vp8_prob dest [VP8_BINTRAMODES] [VP8_BINTRAMODES] [VP8_BINTRAMODES - 1]);

#if CONFIG_ADAPTIVE_ENTROPY
void vp8_adapt_mode_probs(struct VP8Common *);
#endif
#endif
