/*
 *  Copyright (c) 2010 The WebM project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#ifndef VP9_COMMON_VP9_INVTRANS_H_
#define VP9_COMMON_VP9_INVTRANS_H_

<<<<<<< HEAD   (89ac94 Removed mmx versions of vp9_bilinear_predict filters)
#include "vpx_ports/config.h"
#include "vpx/vpx_integer.h"
=======
#include "./vpx_config.h"
>>>>>>> BRANCH (16810c Merge branch 'vp9-preview' of review:webm/libvpx)
#include "vp9/common/vp9_blockd.h"

extern void vp9_inverse_transform_b_4x4(MACROBLOCKD *xd, int block, int pitch);

extern void vp9_inverse_transform_mb_4x4(MACROBLOCKD *xd);

extern void vp9_inverse_transform_mby_4x4(MACROBLOCKD *xd);

extern void vp9_inverse_transform_mbuv_4x4(MACROBLOCKD *xd);

extern void vp9_inverse_transform_b_8x8(int16_t *input_dqcoeff,
                                        int16_t *output_coeff, int pitch);

extern void vp9_inverse_transform_mb_8x8(MACROBLOCKD *xd);

extern void vp9_inverse_transform_mby_8x8(MACROBLOCKD *xd);

extern void vp9_inverse_transform_mbuv_8x8(MACROBLOCKD *xd);

extern void vp9_inverse_transform_b_16x16(int16_t *input_dqcoeff,
                                          int16_t *output_coeff, int pitch);

extern void vp9_inverse_transform_mb_16x16(MACROBLOCKD *xd);

extern void vp9_inverse_transform_mby_16x16(MACROBLOCKD *xd);

#if CONFIG_TX32X32 && CONFIG_SUPERBLOCKS
extern void vp9_inverse_transform_sby_32x32(SUPERBLOCKD *xd_sb);
extern void vp9_inverse_transform_sbuv_16x16(SUPERBLOCKD *xd_sb);
#endif

#endif  // VP9_COMMON_VP9_INVTRANS_H_
