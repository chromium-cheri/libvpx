/*
 *  Copyright (c) 2015 The WebM project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#ifndef VPX_DSP_VARIANCE_H_
#define VPX_DSP_VARIANCE_H_

#include "./vpx_config.h"

#include "vpx/vpx_integer.h"

#ifdef __cplusplus
extern "C" {
#endif

#define FILTER_BITS 7
#define FILTER_WEIGHT 128

typedef unsigned int(*vpx_sad_fn_t)(const uint8_t *a, int a_stride,
                                    const uint8_t *b_ptr, int b_stride);

typedef unsigned int(*vpx_sad_avg_fn_t)(const uint8_t *a_ptr, int a_stride,
                                        const uint8_t *b_ptr, int b_stride,
                                        const uint8_t *second_pred);

typedef void (*vp8_copy32xn_fn_t)(const uint8_t *a, int a_stride,
                                  uint8_t *b, int b_stride, int n);

typedef void (*vpx_sad_multi_fn_t)(const uint8_t *a, int a_stride,
                                   const uint8_t *b, int b_stride,
                                   unsigned int *sad_array);

typedef void (*vpx_sad_multi_d_fn_t)(const uint8_t *a, int a_stride,
                                     const uint8_t *const b_array[],
                                     int b_stride,
                                     unsigned int *sad_array);

typedef unsigned int (*vpx_variance_fn_t)(const uint8_t *a, int a_stride,
                                          const uint8_t *b, int b_stride,
                                          unsigned int *sse);

typedef unsigned int (*vpx_subpixvariance_fn_t)(const uint8_t *a, int a_stride,
                                                int xoffset, int yoffset,
                                                const uint8_t *b, int b_stride,
                                                unsigned int *sse);

typedef unsigned int (*vpx_subp_avg_variance_fn_t)(const uint8_t *a_ptr,
                                                   int a_stride,
                                                   int xoffset, int yoffset,
                                                   const uint8_t *b_ptr,
                                                   int b_stride,
                                                   unsigned int *sse,
                                                   const uint8_t *second_pred);
#if CONFIG_VP8
typedef struct variance_vtable {
  vpx_sad_fn_t            sdf;
  vpx_variance_fn_t       vf;
  vpx_subpixvariance_fn_t svf;
  vpx_variance_fn_t       svf_halfpix_h;
  vpx_variance_fn_t       svf_halfpix_v;
  vpx_variance_fn_t       svf_halfpix_hv;
  vpx_sad_multi_fn_t      sdx3f;
  vpx_sad_multi_fn_t      sdx8f;
  vpx_sad_multi_d_fn_t    sdx4df;
#if ARCH_X86 || ARCH_X86_64
  vp8_copy32xn_fn_t       copymem;
#endif
} vp8_variance_fn_ptr_t;
#endif  // CONFIG_VP8

#if CONFIG_VP10 && CONFIG_EXT_INTER
typedef unsigned int(*vpx_masked_sad_fn_t)(const uint8_t *src_ptr,
                                           int source_stride,
                                           const uint8_t *ref_ptr,
                                           int ref_stride,
                                           const uint8_t *msk_ptr,
                                           int msk_stride);
typedef unsigned int (*vpx_masked_variance_fn_t)(const uint8_t *src_ptr,
                                                 int source_stride,
                                                 const uint8_t *ref_ptr,
                                                 int ref_stride,
                                                 const uint8_t *msk_ptr,
                                                 int msk_stride,
                                                 unsigned int *sse);
typedef unsigned int (*vpx_masked_subpixvariance_fn_t)(const uint8_t *src_ptr,
                                                       int source_stride,
                                                       int xoffset,
                                                       int yoffset,
                                                       const uint8_t *ref_ptr,
                                                       int Refstride,
                                                       const uint8_t *msk_ptr,
                                                       int msk_stride,
                                                       unsigned int *sse);
#endif  // CONFIG_VP10 && CONFIG_EXT_INTER

#if CONFIG_VP9
typedef struct vp9_variance_vtable {
  vpx_sad_fn_t               sdf;
  vpx_sad_avg_fn_t           sdaf;
  vpx_variance_fn_t          vf;
  vpx_subpixvariance_fn_t    svf;
  vpx_subp_avg_variance_fn_t svaf;
  vpx_sad_multi_fn_t         sdx3f;
  vpx_sad_multi_fn_t         sdx8f;
  vpx_sad_multi_d_fn_t       sdx4df;
} vp9_variance_fn_ptr_t;
#endif  // CONFIG_VP9

#if CONFIG_VP10
typedef struct vp10_variance_vtable {
  vpx_sad_fn_t                   sdf;
  vpx_sad_avg_fn_t               sdaf;
  vpx_variance_fn_t              vf;
  vpx_subpixvariance_fn_t        svf;
  vpx_subp_avg_variance_fn_t     svaf;
  vpx_sad_multi_fn_t             sdx3f;
  vpx_sad_multi_fn_t             sdx8f;
  vpx_sad_multi_d_fn_t           sdx4df;
#if CONFIG_EXT_INTER
  vpx_masked_sad_fn_t            msdf;
  vpx_masked_variance_fn_t       mvf;
  vpx_masked_subpixvariance_fn_t msvf;
#endif  // CONFIG_EXT_INTER
} vp10_variance_fn_ptr_t;
#endif  // CONFIG_VP10

void highbd_var_filter_block2d_bil_first_pass(
    const uint8_t *src_ptr8,
    uint16_t *output_ptr,
    unsigned int src_pixels_per_line,
    int pixel_step,
    unsigned int output_height,
    unsigned int output_width,
    const uint8_t *filter);

void highbd_var_filter_block2d_bil_second_pass(
    const uint16_t *src_ptr,
    uint16_t *output_ptr,
    unsigned int src_pixels_per_line,
    unsigned int pixel_step,
    unsigned int output_height,
    unsigned int output_width,
    const uint8_t *filter);

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // VPX_DSP_VARIANCE_H_
