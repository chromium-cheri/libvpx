/*
 *  Copyright (c) 2014 The WebM project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include <arm_neon.h>

#include <math.h>

#include "vpx_mem/vpx_mem.h"

#include "vp9/common/vp9_quant_common.h"
#include "vp9/common/vp9_seg_common.h"

#include "vp9/encoder/vp9_encoder.h"
#include "vp9/encoder/vp9_quantize.h"
#include "vp9/encoder/vp9_rd.h"

#include "vpx_dsp/arm/idct_neon.h"
#include "vpx_dsp/arm/mem_neon.h"
#include "vpx_dsp/vpx_dsp_common.h"

void vp9_quantize_fp_neon(const tran_low_t *coeff_ptr, intptr_t count,
                          int skip_block, const int16_t *round_ptr,
                          const int16_t *quant_ptr, tran_low_t *qcoeff_ptr,
                          tran_low_t *dqcoeff_ptr, const int16_t *dequant_ptr,
                          uint16_t *eob_ptr, const int16_t *scan,
                          const int16_t *iscan) {
  (void)scan;

  if (!skip_block) {
    // Quantization pass: All coefficients with index >= zero_flag are
    // skippable. Note: zero_flag can be zero.
    int i;
    const int16x8_t v_zero = vdupq_n_s16(0);
    const int16x8_t v_one = vdupq_n_s16(1);
    int16x8_t v_eobmax_76543210 = vdupq_n_s16(-1);
    int16x8_t v_round = vmovq_n_s16(round_ptr[1]);
    int16x8_t v_quant = vmovq_n_s16(quant_ptr[1]);
    int16x8_t v_dequant = vmovq_n_s16(dequant_ptr[1]);
    // adjust for dc
    v_round = vsetq_lane_s16(round_ptr[0], v_round, 0);
    v_quant = vsetq_lane_s16(quant_ptr[0], v_quant, 0);
    v_dequant = vsetq_lane_s16(dequant_ptr[0], v_dequant, 0);
    // process dc and the first seven ac coeffs
    {
      const int16x8_t v_iscan = vld1q_s16(&iscan[0]);
      const int16x8_t v_coeff = load_tran_low_to_s16q(coeff_ptr);
      const int16x8_t v_coeff_sign = vshrq_n_s16(v_coeff, 15);
      const int16x8_t v_tmp = vabaq_s16(v_round, v_coeff, v_zero);
      const int32x4_t v_tmp_lo =
          vmull_s16(vget_low_s16(v_tmp), vget_low_s16(v_quant));
      const int32x4_t v_tmp_hi =
          vmull_s16(vget_high_s16(v_tmp), vget_high_s16(v_quant));
      const int16x8_t v_tmp2 =
          vcombine_s16(vshrn_n_s32(v_tmp_lo, 16), vshrn_n_s32(v_tmp_hi, 16));
      const uint16x8_t v_nz_mask = vceqq_s16(v_tmp2, v_zero);
      const int16x8_t v_iscan_plus1 = vaddq_s16(v_iscan, v_one);
      const int16x8_t v_nz_iscan = vbslq_s16(v_nz_mask, v_zero, v_iscan_plus1);
      const int16x8_t v_qcoeff_a = veorq_s16(v_tmp2, v_coeff_sign);
      const int16x8_t v_qcoeff = vsubq_s16(v_qcoeff_a, v_coeff_sign);
      const int16x8_t v_dqcoeff = vmulq_s16(v_qcoeff, v_dequant);
      v_eobmax_76543210 = vmaxq_s16(v_eobmax_76543210, v_nz_iscan);
      store_s16q_to_tran_low(qcoeff_ptr, v_qcoeff);
      store_s16q_to_tran_low(dqcoeff_ptr, v_dqcoeff);
      v_round = vmovq_n_s16(round_ptr[1]);
      v_quant = vmovq_n_s16(quant_ptr[1]);
      v_dequant = vmovq_n_s16(dequant_ptr[1]);
    }
    // now process the rest of the ac coeffs
    for (i = 8; i < count; i += 8) {
      const int16x8_t v_iscan = vld1q_s16(&iscan[i]);
      const int16x8_t v_coeff = load_tran_low_to_s16q(coeff_ptr + i);
      const int16x8_t v_coeff_sign = vshrq_n_s16(v_coeff, 15);
      const int16x8_t v_tmp = vabaq_s16(v_round, v_coeff, v_zero);
      const int32x4_t v_tmp_lo =
          vmull_s16(vget_low_s16(v_tmp), vget_low_s16(v_quant));
      const int32x4_t v_tmp_hi =
          vmull_s16(vget_high_s16(v_tmp), vget_high_s16(v_quant));
      const int16x8_t v_tmp2 =
          vcombine_s16(vshrn_n_s32(v_tmp_lo, 16), vshrn_n_s32(v_tmp_hi, 16));
      const uint16x8_t v_nz_mask = vceqq_s16(v_tmp2, v_zero);
      const int16x8_t v_iscan_plus1 = vaddq_s16(v_iscan, v_one);
      const int16x8_t v_nz_iscan = vbslq_s16(v_nz_mask, v_zero, v_iscan_plus1);
      const int16x8_t v_qcoeff_a = veorq_s16(v_tmp2, v_coeff_sign);
      const int16x8_t v_qcoeff = vsubq_s16(v_qcoeff_a, v_coeff_sign);
      const int16x8_t v_dqcoeff = vmulq_s16(v_qcoeff, v_dequant);
      v_eobmax_76543210 = vmaxq_s16(v_eobmax_76543210, v_nz_iscan);
      store_s16q_to_tran_low(qcoeff_ptr + i, v_qcoeff);
      store_s16q_to_tran_low(dqcoeff_ptr + i, v_dqcoeff);
    }
    {
      const int16x4_t v_eobmax_3210 = vmax_s16(
          vget_low_s16(v_eobmax_76543210), vget_high_s16(v_eobmax_76543210));
      const int64x1_t v_eobmax_xx32 =
          vshr_n_s64(vreinterpret_s64_s16(v_eobmax_3210), 32);
      const int16x4_t v_eobmax_tmp =
          vmax_s16(v_eobmax_3210, vreinterpret_s16_s64(v_eobmax_xx32));
      const int64x1_t v_eobmax_xxx3 =
          vshr_n_s64(vreinterpret_s64_s16(v_eobmax_tmp), 16);
      const int16x4_t v_eobmax_final =
          vmax_s16(v_eobmax_tmp, vreinterpret_s16_s64(v_eobmax_xxx3));

      *eob_ptr = (uint16_t)vget_lane_s16(v_eobmax_final, 0);
    }
  } else {
    memset(qcoeff_ptr, 0, count * sizeof(*qcoeff_ptr));
    memset(dqcoeff_ptr, 0, count * sizeof(*dqcoeff_ptr));
    *eob_ptr = 0;
  }
}

void vp9_quantize_fp_32x32_neon(const tran_low_t *coeff_ptr, intptr_t count,
                                int skip_block, const int16_t *round_ptr,
                                const int16_t *quant_ptr,
                                tran_low_t *qcoeff_ptr, tran_low_t *dqcoeff_ptr,
                                const int16_t *dequant_ptr, uint16_t *eob_ptr,
                                const int16_t *scan, const int16_t *iscan_ptr) {
  const int16x8_t one = vdupq_n_s16(1);
  const int16x8_t neg_one = vdupq_n_s16(-1);
  uint16x8_t eob_max;
  int i;
  (void)scan;
  (void)count;

  if (skip_block) {
    const int16x8_t zero = vdupq_n_s16(0);
    for (i = 0; i < 32 * 32 / 8; ++i) {
      store_s16q_to_tran_low(qcoeff_ptr, zero);
      store_s16q_to_tran_low(dqcoeff_ptr, zero);
      qcoeff_ptr += 8;
      dqcoeff_ptr += 8;
    }
    *eob_ptr = 0;
    return;
  }

  {
    // ROUND_POWER_OF_TWO(round_ptr[], 1)
    const int16x8_t round = vrshrq_n_s16(vld1q_s16(round_ptr), 1);
    const int16x8_t quant = vld1q_s16(quant_ptr);
    const int16x4_t dequant = vld1_s16(dequant_ptr);
    // dequant >> 2 is used similar to zbin as a threshold.
    const int16x8_t dequant_thresh = vshrq_n_s16(vld1q_s16(dequant_ptr), 2);

    // Process dc and the first seven ac coeffs.
    const uint16x8_t iscan =
        vreinterpretq_u16_s16(vaddq_s16(vld1q_s16(iscan_ptr), one));
    const int16x8_t coeff = load_tran_low_to_s16q(coeff_ptr);
    const int16x8_t coeff_sign = vshrq_n_s16(coeff, 15);
    const int16x8_t coeff_abs = vabsq_s16(coeff);
    const int16x8_t dequant_mask =
        vreinterpretq_s16_u16(vcgeq_s16(coeff_abs, dequant_thresh));

    int16x8_t qcoeff = vaddq_s16(coeff_abs, round);
    int32x4_t dqcoeff_0, dqcoeff_1, dqcoeff_0_sign, dqcoeff_1_sign;
    int16x8_t dqcoeff;

    // coeff * quant_ptr[]) >> 15
    qcoeff = vqdmulhq_s16(qcoeff, quant);

    // Restore sign.
    qcoeff = veorq_s16(qcoeff, coeff_sign);
    qcoeff = vsubq_s16(qcoeff, coeff_sign);
    qcoeff = vandq_s16(qcoeff, dequant_mask);

    // qcoeff * dequant[] / 2
    dqcoeff_0 = vmull_s16(vget_low_s16(qcoeff), dequant);
    dqcoeff_1 = vmull_n_s16(vget_high_s16(qcoeff), dequant_ptr[1]);

    // The way the C processes the devision requires us to convert to positive
    // before shifting or even narrowing, then put the sign back.
    dqcoeff_0_sign = vshrq_n_s32(dqcoeff_0, 31);
    dqcoeff_1_sign = vshrq_n_s32(dqcoeff_1, 31);
    dqcoeff_0 = vabsq_s32(dqcoeff_0);
    dqcoeff_1 = vabsq_s32(dqcoeff_1);
    dqcoeff_0 = vshrq_n_s32(dqcoeff_0, 1);
    dqcoeff_1 = vshrq_n_s32(dqcoeff_1, 1);
    dqcoeff_0 = veorq_s32(dqcoeff_0, dqcoeff_0_sign);
    dqcoeff_1 = veorq_s32(dqcoeff_1, dqcoeff_1_sign);
    dqcoeff_0 = vsubq_s32(dqcoeff_0, dqcoeff_0_sign);
    dqcoeff_1 = vsubq_s32(dqcoeff_1, dqcoeff_1_sign);

    dqcoeff = vcombine_s16(vmovn_s32(dqcoeff_0), vmovn_s32(dqcoeff_1));

    eob_max = vandq_u16(vtstq_s16(qcoeff, neg_one), iscan);

    store_s16q_to_tran_low(qcoeff_ptr, qcoeff);
    store_s16q_to_tran_low(dqcoeff_ptr, dqcoeff);

    iscan_ptr += 8;
    coeff_ptr += 8;
    qcoeff_ptr += 8;
    dqcoeff_ptr += 8;
  }

  {
    const int16x8_t round = vrshrq_n_s16(vmovq_n_s16(round_ptr[1]), 1);
    const int16x8_t quant = vmovq_n_s16(quant_ptr[1]);
    const int16x8_t dequant_thresh =
        vshrq_n_s16(vmovq_n_s16(dequant_ptr[1]), 2);

    // Process the rest of the ac coeffs.
    for (i = 8; i < 32 * 32; i += 8) {
      const uint16x8_t iscan =
          vreinterpretq_u16_s16(vaddq_s16(vld1q_s16(iscan_ptr), one));
      const int16x8_t coeff = load_tran_low_to_s16q(coeff_ptr);
      const int16x8_t coeff_sign = vshrq_n_s16(coeff, 15);
      const int16x8_t coeff_abs = vabsq_s16(coeff);
      const int16x8_t dequant_mask =
          vreinterpretq_s16_u16(vcgeq_s16(coeff_abs, dequant_thresh));

      int16x8_t qcoeff = vaddq_s16(coeff_abs, round);
      int32x4_t dqcoeff_0, dqcoeff_1, dqcoeff_0_sign, dqcoeff_1_sign;
          int16x8_t dqcoeff;

      qcoeff = vqdmulhq_s16(qcoeff, quant);
      qcoeff = veorq_s16(qcoeff, coeff_sign);
      qcoeff = vsubq_s16(qcoeff, coeff_sign);
      qcoeff = vandq_s16(qcoeff, dequant_mask);

      dqcoeff_0 = vmull_n_s16(vget_low_s16(qcoeff), dequant_ptr[1]);
      dqcoeff_1 = vmull_n_s16(vget_high_s16(qcoeff), dequant_ptr[1]);

      dqcoeff_0_sign = vshrq_n_s32(dqcoeff_0, 31);
      dqcoeff_1_sign = vshrq_n_s32(dqcoeff_1, 31);
      dqcoeff_0 = vabsq_s32(dqcoeff_0);
      dqcoeff_1 = vabsq_s32(dqcoeff_1);
      dqcoeff_0 = vshrq_n_s32(dqcoeff_0, 1);
      dqcoeff_1 = vshrq_n_s32(dqcoeff_1, 1);
      dqcoeff_0 = veorq_s32(dqcoeff_0, dqcoeff_0_sign);
      dqcoeff_1 = veorq_s32(dqcoeff_1, dqcoeff_1_sign);
      dqcoeff_0 = vsubq_s32(dqcoeff_0, dqcoeff_0_sign);
      dqcoeff_1 = vsubq_s32(dqcoeff_1, dqcoeff_1_sign);

      dqcoeff = vcombine_s16(vmovn_s32(dqcoeff_0), vmovn_s32(dqcoeff_1));

      eob_max =
          vmaxq_u16(eob_max, vandq_u16(vtstq_s16(qcoeff, neg_one), iscan));

      store_s16q_to_tran_low(qcoeff_ptr, qcoeff);
      store_s16q_to_tran_low(dqcoeff_ptr, dqcoeff);

      iscan_ptr += 8;
      coeff_ptr += 8;
      qcoeff_ptr += 8;
      dqcoeff_ptr += 8;
    }

    {
      const uint16x4_t eob_max_0 =
          vmax_u16(vget_low_u16(eob_max), vget_high_u16(eob_max));
      const uint16x4_t eob_max_1 = vpmax_u16(eob_max_0, eob_max_0);
      const uint16x4_t eob_max_2 = vpmax_u16(eob_max_1, eob_max_1);
      vst1_lane_u16(eob_ptr, eob_max_2, 0);
    }
  }
}
