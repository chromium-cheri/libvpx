/*
 *  Copyright (c) 2017 The WebM project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include <assert.h>
#include <immintrin.h>  // AVX2

#include "./vp9_rtcd.h"
#include "vpx/vpx_integer.h"
#include "vpx_dsp/vpx_dsp_common.h"
#include "vpx_dsp/x86/bitdepth_conversion_avx2.h"
#include "vpx_dsp/x86/quantize_sse2.h"

// Zero fill 8 positions in the output buffer.
static VPX_FORCE_INLINE void store_zero_tran_low(tran_low_t *a) {
  const __m256i zero = _mm256_setzero_si256();
#if CONFIG_VP9_HIGHBITDEPTH
  _mm256_storeu_si256((__m256i *)(a), zero);
  _mm256_storeu_si256((__m256i *)(a + 8), zero);
#else
  _mm256_storeu_si256((__m256i *)(a), zero);
#endif
}

static VPX_FORCE_INLINE void load_fp_values_avx2(
    const int16_t *round_ptr, __m256i *round, const int16_t *quant_ptr,
    __m256i *quant, const int16_t *dequant_ptr, __m256i *dequant) {
  *round = _mm256_castsi128_si256(_mm_load_si128((const __m128i *)round_ptr));
  *round = _mm256_permute4x64_epi64(*round, 0x54);
  *quant = _mm256_castsi128_si256(_mm_load_si128((const __m128i *)quant_ptr));
  *quant = _mm256_permute4x64_epi64(*quant, 0x54);
  *dequant =
      _mm256_castsi128_si256(_mm_load_si128((const __m128i *)dequant_ptr));
  *dequant = _mm256_permute4x64_epi64(*dequant, 0x54);
}

static VPX_FORCE_INLINE __m256i get_max_lane_eob(const int16_t *iscan,
                                                 __m256i v_eobmax,
                                                 __m256i v_mask) {
  const __m256i v_iscan = _mm256_loadu_si256((const __m256i *)iscan);
#if CONFIG_VP9_HIGHBITDEPTH
  // typedef int32_t tran_low_t;
  const __m256i v_iscan_perm = _mm256_permute4x64_epi64(v_iscan, 0xD8);
  const __m256i v_iscan_plus1 = _mm256_sub_epi16(v_iscan_perm, v_mask);
#else
  // typedef int16_t tran_low_t;
  const __m256i v_iscan_plus1 = _mm256_sub_epi16(v_iscan, v_mask);
#endif
  const __m256i v_nz_iscan = _mm256_and_si256(v_iscan_plus1, v_mask);
  return _mm256_max_epi16(v_eobmax, v_nz_iscan);
}

static VPX_FORCE_INLINE uint16_t get_max_eob(__m256i eob256) {
  const __m256i eob_lo = eob256;
  // Copy upper 128 to lower 128
  const __m256i eob_hi = _mm256_permute2x128_si256(eob256, eob256, 0X81);
  __m256i eob = _mm256_max_epi16(eob_lo, eob_hi);
  __m256i eob_s = _mm256_shuffle_epi32(eob, 0xe);
  eob = _mm256_max_epi16(eob, eob_s);
  eob_s = _mm256_shufflelo_epi16(eob, 0xe);
  eob = _mm256_max_epi16(eob, eob_s);
  eob_s = _mm256_shufflelo_epi16(eob, 1);
  eob = _mm256_max_epi16(eob, eob_s);
  return (uint16_t)_mm256_extract_epi16(eob, 0);
}

static VPX_FORCE_INLINE void quantize_fp_16(
    const __m256i *round, const __m256i *quant, const __m256i *dequant,
    const __m256i *thr, const tran_low_t *coeff_ptr, const int16_t *iscan_ptr,
    tran_low_t *qcoeff_ptr, tran_low_t *dqcoeff_ptr, __m256i *eob_max) {
  const __m256i coeff = load_tran_low(coeff_ptr);
  const __m256i abs_coeff = _mm256_abs_epi16(coeff);
  const int32_t nzflag =
      _mm256_movemask_epi8(_mm256_cmpgt_epi16(abs_coeff, *thr));

  if (nzflag) {
    const __m256i tmp_rnd = _mm256_adds_epi16(abs_coeff, *round);
    const __m256i abs_qcoeff = _mm256_mulhi_epi16(tmp_rnd, *quant);
    const __m256i qcoeff = _mm256_sign_epi16(abs_qcoeff, coeff);
    const __m256i dqcoeff = _mm256_mullo_epi16(qcoeff, *dequant);
    const __m256i nz_mask =
        _mm256_cmpgt_epi16(abs_qcoeff, _mm256_setzero_si256());
    store_tran_low(qcoeff, qcoeff_ptr);
    store_tran_low(dqcoeff, dqcoeff_ptr);

    *eob_max = get_max_lane_eob(iscan_ptr, *eob_max, nz_mask);
  } else {
    store_zero_tran_low(qcoeff_ptr);
    store_zero_tran_low(dqcoeff_ptr);
  }
}

void vp9_quantize_fp_avx2(const tran_low_t *coeff_ptr, intptr_t n_coeffs,
                          const int16_t *round_ptr, const int16_t *quant_ptr,
                          tran_low_t *qcoeff_ptr, tran_low_t *dqcoeff_ptr,
                          const int16_t *dequant_ptr, uint16_t *eob_ptr,
                          const int16_t *scan, const int16_t *iscan) {
  __m256i round, quant, dequant, thr;
  __m256i eob_max = _mm256_setzero_si256();
  (void)scan;

  coeff_ptr += n_coeffs;
  iscan += n_coeffs;
  qcoeff_ptr += n_coeffs;
  dqcoeff_ptr += n_coeffs;
  n_coeffs = -n_coeffs;

  // Setup global values
  load_fp_values_avx2(round_ptr, &round, quant_ptr, &quant, dequant_ptr,
                      &dequant);
  thr = _mm256_setzero_si256();

  quantize_fp_16(&round, &quant, &dequant, &thr, coeff_ptr + n_coeffs,
                 iscan + n_coeffs, qcoeff_ptr + n_coeffs,
                 dqcoeff_ptr + n_coeffs, &eob_max);

  n_coeffs += 8 * 2;

  // remove dc constants
  dequant = _mm256_permute2x128_si256(dequant, dequant, 0x31);
  quant = _mm256_permute2x128_si256(quant, quant, 0x31);
  round = _mm256_permute2x128_si256(round, round, 0x31);
  thr = _mm256_srai_epi16(dequant, 1);

  // AC only loop
  while (n_coeffs < 0) {
    quantize_fp_16(&round, &quant, &dequant, &thr, coeff_ptr + n_coeffs,
                   iscan + n_coeffs, qcoeff_ptr + n_coeffs,
                   dqcoeff_ptr + n_coeffs, &eob_max);
    n_coeffs += 8 * 2;
  }

  *eob_ptr = get_max_eob(eob_max);
}