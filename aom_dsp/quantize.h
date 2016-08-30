/*
 *  Copyright (c) 2015 The WebM project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#ifndef VPX_DSP_QUANTIZE_H_
#define VPX_DSP_QUANTIZE_H_

#include "./vpx_config.h"
#include "aom_dsp/vpx_dsp_common.h"

#ifdef __cplusplus
extern "C" {
#endif

#if CONFIG_AOM_QM
void vpx_quantize_dc(const tran_low_t *coeff_ptr, int n_coeffs, int skip_block,
                     const int16_t *round_ptr, const int16_t quant_ptr,
                     tran_low_t *qcoeff_ptr, tran_low_t *dqcoeff_ptr,
                     const int16_t dequant_ptr, uint16_t *eob_ptr,
                     const qm_val_t *qm_ptr, const qm_val_t *iqm_ptr);
void vpx_quantize_dc_32x32(const tran_low_t *coeff_ptr, int skip_block,
                           const int16_t *round_ptr, const int16_t quant_ptr,
                           tran_low_t *qcoeff_ptr, tran_low_t *dqcoeff_ptr,
                           const int16_t dequant_ptr, uint16_t *eob_ptr,
                           const qm_val_t *qm_ptr, const qm_val_t *iqm_ptr);
void vpx_quantize_b_c(const tran_low_t *coeff_ptr, intptr_t n_coeffs,
                      int skip_block, const int16_t *zbin_ptr,
                      const int16_t *round_ptr, const int16_t *quant_ptr,
                      const int16_t *quant_shift_ptr, tran_low_t *qcoeff_ptr,
                      tran_low_t *dqcoeff_ptr, const int16_t *dequant_ptr,
                      uint16_t *eob_ptr, const int16_t *scan,
                      const int16_t *iscan, const qm_val_t *qm_ptr,
                      const qm_val_t *iqm_ptr);
#if CONFIG_VP9_HIGHBITDEPTH
void vpx_highbd_quantize_dc(const tran_low_t *coeff_ptr, int n_coeffs,
                            int skip_block, const int16_t *round_ptr,
                            const int16_t quant_ptr, tran_low_t *qcoeff_ptr,
                            tran_low_t *dqcoeff_ptr, const int16_t dequant_ptr,
                            uint16_t *eob_ptr, const qm_val_t *qm_ptr,
                            const qm_val_t *iqm_ptr);
void vpx_highbd_quantize_dc_32x32(
    const tran_low_t *coeff_ptr, int skip_block, const int16_t *round_ptr,
    const int16_t quant_ptr, tran_low_t *qcoeff_ptr, tran_low_t *dqcoeff_ptr,
    const int16_t dequant_ptr, uint16_t *eob_ptr, const qm_val_t *qm_ptr,
    const qm_val_t *iqm_ptr);
void vpx_highbd_quantize_b_c(const tran_low_t *coeff_ptr, intptr_t n_coeffs,
                             int skip_block, const int16_t *zbin_ptr,
                             const int16_t *round_ptr, const int16_t *quant_ptr,
                             const int16_t *quant_shift_ptr,
                             tran_low_t *qcoeff_ptr, tran_low_t *dqcoeff_ptr,
                             const int16_t *dequant_ptr, uint16_t *eob_ptr,
                             const int16_t *scan, const int16_t *iscan,
                             const qm_val_t *qm_ptr, const qm_val_t *iqm_ptr);
#endif
#else
void vpx_quantize_dc(const tran_low_t *coeff_ptr, int n_coeffs, int skip_block,
                     const int16_t *round_ptr, const int16_t quant_ptr,
                     tran_low_t *qcoeff_ptr, tran_low_t *dqcoeff_ptr,
                     const int16_t dequant_ptr, uint16_t *eob_ptr);
void vpx_quantize_dc_32x32(const tran_low_t *coeff_ptr, int skip_block,
                           const int16_t *round_ptr, const int16_t quant_ptr,
                           tran_low_t *qcoeff_ptr, tran_low_t *dqcoeff_ptr,
                           const int16_t dequant_ptr, uint16_t *eob_ptr);

#if CONFIG_VP9_HIGHBITDEPTH
void vpx_highbd_quantize_dc(const tran_low_t *coeff_ptr, int n_coeffs,
                            int skip_block, const int16_t *round_ptr,
                            const int16_t quant_ptr, tran_low_t *qcoeff_ptr,
                            tran_low_t *dqcoeff_ptr, const int16_t dequant_ptr,
                            uint16_t *eob_ptr);
void vpx_highbd_quantize_dc_32x32(const tran_low_t *coeff_ptr, int skip_block,
                                  const int16_t *round_ptr,
                                  const int16_t quant_ptr,
                                  tran_low_t *qcoeff_ptr,
                                  tran_low_t *dqcoeff_ptr,
                                  const int16_t dequant_ptr, uint16_t *eob_ptr);
#endif
#endif

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // VPX_DSP_QUANTIZE_H_
