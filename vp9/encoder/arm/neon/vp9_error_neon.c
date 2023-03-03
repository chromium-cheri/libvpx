/*
 *  Copyright (c) 2015 The WebM project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include <arm_neon.h>
#include <assert.h>

#include "./vp9_rtcd.h"
#include "vpx_dsp/arm/mem_neon.h"
#include "vpx_dsp/arm/sum_neon.h"

int64_t vp9_block_error_fp_neon(const tran_low_t *coeff,
                                const tran_low_t *dqcoeff, int block_size) {
  uint64x2_t err_u64[2] = { vdupq_n_u64(0), vdupq_n_u64(0) };

  do {
    uint32x4_t err0, err1;

    const int16x8_t c0 = load_tran_low_to_s16q(coeff);
    const int16x8_t c1 = load_tran_low_to_s16q(coeff + 8);
    const int16x8_t d0 = load_tran_low_to_s16q(dqcoeff);
    const int16x8_t d1 = load_tran_low_to_s16q(dqcoeff + 8);

    const uint16x8_t diff0 = vreinterpretq_u16_s16(vabdq_s16(c0, d0));
    const uint16x8_t diff1 = vreinterpretq_u16_s16(vabdq_s16(c1, d1));

    // diff is 15-bits, the squares 30, so we can store 2 in 31-bits before
    // accumulating them in 64-bits.
    err0 = vmull_u16(vget_low_u16(diff0), vget_low_u16(diff0));
    err0 = vmlal_u16(err0, vget_high_u16(diff0), vget_high_u16(diff0));
    err_u64[0] = vpadalq_u32(err_u64[0], err0);

    err1 = vmull_u16(vget_low_u16(diff1), vget_low_u16(diff1));
    err1 = vmlal_u16(err1, vget_high_u16(diff1), vget_high_u16(diff1));
    err_u64[1] = vpadalq_u32(err_u64[1], err1);

    coeff += 16;
    dqcoeff += 16;
    block_size -= 16;
  } while (block_size != 0);

  return horizontal_add_uint64x2(vaddq_u64(err_u64[0], err_u64[1]));
}
