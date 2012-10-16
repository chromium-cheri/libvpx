/*
 *  Copyright (c) 2010 The WebM project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */


#include "vpx_ports/config.h"
#include "vpx_ports/x86.h"
#include "vp8/encoder/variance.h"
#include "vp8/encoder/onyx_int.h"


#if HAVE_MMX
void vp8_short_fdct8x4_mmx(short *input, short *output, int pitch) {
  vp8_short_fdct4x4_mmx(input,   output,    pitch);
  vp8_short_fdct4x4_mmx(input + 4, output + 16, pitch);
}

int vp8_mbblock_error_mmx_impl(short *coeff_ptr, short *dcoef_ptr, int dc);
int vp8_mbblock_error_mmx(MACROBLOCK *mb, int dc) {
  short *coeff_ptr =  mb->block[0].coeff;
  short *dcoef_ptr =  mb->e_mbd.block[0].dqcoeff;
  return vp8_mbblock_error_mmx_impl(coeff_ptr, dcoef_ptr, dc);
}

int vp8_mbuverror_mmx_impl(short *s_ptr, short *d_ptr);
int vp8_mbuverror_mmx(MACROBLOCK *mb) {
  short *s_ptr = &mb->coeff[256];
  short *d_ptr = &mb->e_mbd.dqcoeff[256];
  return vp8_mbuverror_mmx_impl(s_ptr, d_ptr);
}

void vp8_subtract_b_mmx_impl(unsigned char *z,  int src_stride,
                             short *diff, unsigned char *predictor,
                             int pitch);
void vp8_subtract_b_mmx(BLOCK *be, BLOCKD *bd, int pitch) {
  unsigned char *z = *(be->base_src) + be->src;
  unsigned int  src_stride = be->src_stride;
  short *diff = &be->src_diff[0];
  unsigned char *predictor = &bd->predictor[0];
  vp8_subtract_b_mmx_impl(z, src_stride, diff, predictor, pitch);
}

#endif

#if HAVE_SSE2
int vp8_mbblock_error_xmm_impl(short *coeff_ptr, short *dcoef_ptr, int dc);
int vp8_mbblock_error_xmm(MACROBLOCK *mb, int dc) {
  short *coeff_ptr =  mb->block[0].coeff;
  short *dcoef_ptr =  mb->e_mbd.block[0].dqcoeff;
  return vp8_mbblock_error_xmm_impl(coeff_ptr, dcoef_ptr, dc);
}

int vp8_mbuverror_xmm_impl(short *s_ptr, short *d_ptr);
int vp8_mbuverror_xmm(MACROBLOCK *mb) {
  short *s_ptr = &mb->coeff[256];
  short *d_ptr = &mb->e_mbd.dqcoeff[256];
  return vp8_mbuverror_xmm_impl(s_ptr, d_ptr);
}

void vp8_subtract_b_sse2_impl(unsigned char *z,  int src_stride,
                              short *diff, unsigned char *predictor,
                              int pitch);
void vp8_subtract_b_sse2(BLOCK *be, BLOCKD *bd, int pitch) {
  unsigned char *z = *(be->base_src) + be->src;
  unsigned int  src_stride = be->src_stride;
  short *diff = &be->src_diff[0];
  unsigned char *predictor = &bd->predictor[0];
  vp8_subtract_b_sse2_impl(z, src_stride, diff, predictor, pitch);
}

#endif

void vp8_arch_x86_encoder_init(VP8_COMP *cpi) {
#if CONFIG_RUNTIME_CPU_DETECT
  int flags = x86_simd_caps();

  /* Note:
   *
   * This platform can be built without runtime CPU detection as well. If
   * you modify any of the function mappings present in this file, be sure
   * to also update them in static mapings (<arch>/filename_<arch>.h)
   */

  /* Override default functions with fastest ones for this CPU. */
#if HAVE_MMX
  if (flags & HAS_MMX) {
    cpi->rtcd.variance.sad16x16              = vp8_sad16x16_mmx;
    cpi->rtcd.variance.sad16x8               = vp8_sad16x8_mmx;
    cpi->rtcd.variance.sad8x16               = vp8_sad8x16_mmx;
    cpi->rtcd.variance.sad8x8                = vp8_sad8x8_mmx;
    cpi->rtcd.variance.sad4x4                = vp8_sad4x4_mmx;

    cpi->rtcd.variance.var4x4                = vp8_variance4x4_mmx;
    cpi->rtcd.variance.var8x8                = vp8_variance8x8_mmx;
    cpi->rtcd.variance.var8x16               = vp8_variance8x16_mmx;
    cpi->rtcd.variance.var16x8               = vp8_variance16x8_mmx;
    cpi->rtcd.variance.var16x16              = vp8_variance16x16_mmx;

    cpi->rtcd.variance.subpixvar4x4          = vp8_sub_pixel_variance4x4_mmx;
    cpi->rtcd.variance.subpixvar8x8          = vp8_sub_pixel_variance8x8_mmx;
    cpi->rtcd.variance.subpixvar8x16         = vp8_sub_pixel_variance8x16_mmx;
    cpi->rtcd.variance.subpixvar16x8         = vp8_sub_pixel_variance16x8_mmx;
    cpi->rtcd.variance.subpixvar16x16        = vp8_sub_pixel_variance16x16_mmx;
    cpi->rtcd.variance.halfpixvar16x16_h     = vp8_variance_halfpixvar16x16_h_mmx;
    cpi->rtcd.variance.halfpixvar16x16_v     = vp8_variance_halfpixvar16x16_v_mmx;
    cpi->rtcd.variance.halfpixvar16x16_hv    = vp8_variance_halfpixvar16x16_hv_mmx;
    cpi->rtcd.variance.subpixmse16x16        = vp8_sub_pixel_mse16x16_mmx;

    cpi->rtcd.variance.mse16x16              = vp8_mse16x16_mmx;
    cpi->rtcd.variance.getmbss               = vp8_get_mb_ss_mmx;

    cpi->rtcd.encodemb.berr                  = vp8_block_error_mmx;
    cpi->rtcd.encodemb.mberr                 = vp8_mbblock_error_mmx;
    cpi->rtcd.encodemb.mbuverr               = vp8_mbuverror_mmx;
    cpi->rtcd.encodemb.subb                  = vp8_subtract_b_mmx;
    cpi->rtcd.encodemb.submby                = vp8_subtract_mby_mmx;
    cpi->rtcd.encodemb.submbuv               = vp8_subtract_mbuv_mmx;
  }
#endif

#if HAVE_SSE2
  if (flags & HAS_SSE2) {
    cpi->rtcd.variance.sad16x16              = vp8_sad16x16_wmt;
    cpi->rtcd.variance.sad16x8               = vp8_sad16x8_wmt;
    cpi->rtcd.variance.sad8x16               = vp8_sad8x16_wmt;
    cpi->rtcd.variance.sad8x8                = vp8_sad8x8_wmt;
    cpi->rtcd.variance.sad4x4                = vp8_sad4x4_wmt;
    cpi->rtcd.variance.copy32xn              = vp8_copy32xn_sse2;

    cpi->rtcd.variance.var4x4                = vp8_variance4x4_wmt;
    cpi->rtcd.variance.var8x8                = vp8_variance8x8_wmt;
    cpi->rtcd.variance.var8x16               = vp8_variance8x16_wmt;
    cpi->rtcd.variance.var16x8               = vp8_variance16x8_wmt;
    cpi->rtcd.variance.var16x16              = vp8_variance16x16_wmt;

    cpi->rtcd.variance.subpixvar4x4          = vp8_sub_pixel_variance4x4_wmt;
    cpi->rtcd.variance.subpixvar8x8          = vp8_sub_pixel_variance8x8_wmt;
    cpi->rtcd.variance.subpixvar8x16         = vp8_sub_pixel_variance8x16_wmt;
    cpi->rtcd.variance.subpixvar16x8         = vp8_sub_pixel_variance16x8_wmt;
    cpi->rtcd.variance.subpixvar16x16        = vp8_sub_pixel_variance16x16_wmt;
    cpi->rtcd.variance.halfpixvar16x16_h     = vp8_variance_halfpixvar16x16_h_wmt;
    cpi->rtcd.variance.halfpixvar16x16_v     = vp8_variance_halfpixvar16x16_v_wmt;
    cpi->rtcd.variance.halfpixvar16x16_hv    = vp8_variance_halfpixvar16x16_hv_wmt;
    cpi->rtcd.variance.subpixmse16x16        = vp8_sub_pixel_mse16x16_wmt;

    cpi->rtcd.variance.mse16x16              = vp8_mse16x16_wmt;
    cpi->rtcd.variance.getmbss               = vp8_get_mb_ss_sse2;

    cpi->rtcd.encodemb.berr                  = vp8_block_error_xmm;
    cpi->rtcd.encodemb.mberr                 = vp8_mbblock_error_xmm;
    cpi->rtcd.encodemb.mbuverr               = vp8_mbuverror_xmm;
    cpi->rtcd.encodemb.subb                  = vp8_subtract_b_sse2;
    cpi->rtcd.encodemb.submby                = vp8_subtract_mby_sse2;
    cpi->rtcd.encodemb.submbuv               = vp8_subtract_mbuv_sse2;
    cpi->rtcd.temporal.apply                 = vp8_temporal_filter_apply_sse2;

#if CONFIG_INTERNAL_STATS
#if ARCH_X86_64
    cpi->rtcd.variance.ssimpf_8x8            = vp8_ssim_parms_8x8_sse2;
    cpi->rtcd.variance.ssimpf_16x16          = vp8_ssim_parms_16x16_sse2;
#endif
#endif
  }
#endif

#if HAVE_SSE3
  if (flags & HAS_SSE3) {
    cpi->rtcd.variance.sad16x16              = vp8_sad16x16_sse3;
    cpi->rtcd.variance.sad16x16x3            = vp8_sad16x16x3_sse3;
    cpi->rtcd.variance.sad16x8x3             = vp8_sad16x8x3_sse3;
    cpi->rtcd.variance.sad8x16x3             = vp8_sad8x16x3_sse3;
    cpi->rtcd.variance.sad8x8x3              = vp8_sad8x8x3_sse3;
    cpi->rtcd.variance.sad4x4x3              = vp8_sad4x4x3_sse3;
    cpi->rtcd.search.full_search             = vp8_full_search_sadx3;
    cpi->rtcd.variance.sad16x16x4d           = vp8_sad16x16x4d_sse3;
    cpi->rtcd.variance.sad16x8x4d            = vp8_sad16x8x4d_sse3;
    cpi->rtcd.variance.sad8x16x4d            = vp8_sad8x16x4d_sse3;
    cpi->rtcd.variance.sad8x8x4d             = vp8_sad8x8x4d_sse3;
    cpi->rtcd.variance.sad4x4x4d             = vp8_sad4x4x4d_sse3;
    cpi->rtcd.variance.copy32xn              = vp8_copy32xn_sse3;
    cpi->rtcd.search.diamond_search          = vp8_diamond_search_sadx4;
    cpi->rtcd.search.refining_search         = vp8_refining_search_sadx4;
  }
#endif

#if HAVE_SSSE3
  if (flags & HAS_SSSE3) {
    cpi->rtcd.variance.sad16x16x3            = vp8_sad16x16x3_ssse3;
    cpi->rtcd.variance.sad16x8x3             = vp8_sad16x8x3_ssse3;

    cpi->rtcd.variance.subpixvar16x8         = vp8_sub_pixel_variance16x8_ssse3;
    cpi->rtcd.variance.subpixvar16x16        = vp8_sub_pixel_variance16x16_ssse3;
  }
#endif



#if HAVE_SSE4_1
  if (flags & HAS_SSE4_1) {
    cpi->rtcd.variance.sad16x16x8            = vp8_sad16x16x8_sse4;
    cpi->rtcd.variance.sad16x8x8             = vp8_sad16x8x8_sse4;
    cpi->rtcd.variance.sad8x16x8             = vp8_sad8x16x8_sse4;
    cpi->rtcd.variance.sad8x8x8              = vp8_sad8x8x8_sse4;
    cpi->rtcd.variance.sad4x4x8              = vp8_sad4x4x8_sse4;
    cpi->rtcd.search.full_search             = vp8_full_search_sadx8;
  }
#endif

#endif
}
