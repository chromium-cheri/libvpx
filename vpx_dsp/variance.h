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

typedef unsigned int (*vpx_sad_fn_t)(const uint8_t *a, int a_stride,
                                     const uint8_t *b_ptr, int b_stride);

typedef unsigned int (*vpx_sad_avg_fn_t)(const uint8_t *a_ptr, int a_stride,
                                         const uint8_t *b_ptr, int b_stride,
                                         const uint8_t *second_pred);

typedef void (*vp8_copy32xn_fn_t)(const uint8_t *a, int a_stride, uint8_t *b,
                                  int b_stride, int n);

typedef void (*vpx_sad_multi_fn_t)(const uint8_t *a, int a_stride,
                                   const uint8_t *b, int b_stride,
                                   unsigned int *sad_array);

typedef void (*vpx_sad_multi_d_fn_t)(const uint8_t *a, int a_stride,
                                     const uint8_t *const b_array[],
                                     int b_stride, unsigned int *sad_array);

typedef uint32_t (*vpx_variance_fn_t)(const uint8_t *a, int a_stride,
                                      const uint8_t *b, int b_stride,
                                      uint32_t *sse);

typedef void (*vpx_variance_four_fn_t)(const uint8_t *const src,
                                       const int src_stride,
                                       const uint8_t **const ref /*[4]*/,
                                       const int ref_stride,
                                       uint32_t *const sse /*[4]*/,
                                       uint32_t *const var /*[4]*/);

typedef int (*vpx_half_pixel_fn_t)(const uint8_t *const a, const int a_stride,
                                   uint8_t *const b);

typedef void (*vpx_half_pixel_avg_variance_four_fn_t)(
    const uint8_t *const src, const int src_stride,
    const uint8_t **const ref0 /*[4]*/, const int ref0_stride,
    uint32_t *const sse /*[4]*/, uint32_t *const var /*[4]*/,
    const uint8_t *const ref1);

typedef uint32_t (*vpx_sub_pixel_variance_fn_t)(const uint8_t *a, int a_stride,
                                                int xoffset, int yoffset,
                                                const uint8_t *b, int b_stride,
                                                uint32_t *sse);

typedef uint32_t (*vpx_sub_pixel_avg_variance_fn_t)(const uint8_t *a_ptr,
                                                    int a_stride, int xoffset,
                                                    int yoffset,
                                                    const uint8_t *b_ptr,
                                                    int b_stride, uint32_t *sse,
                                                    const uint8_t *second_pred);
#if CONFIG_VP8
typedef struct variance_vtable {
  vpx_sad_fn_t sdf;
  vpx_variance_fn_t vf;
  vpx_sub_pixel_variance_fn_t svf;
  vpx_sad_multi_fn_t sdx3f;
  vpx_sad_multi_fn_t sdx8f;
  vpx_sad_multi_d_fn_t sdx4df;
#if ARCH_X86 || ARCH_X86_64
  vp8_copy32xn_fn_t copymem;
#endif
} vp8_variance_fn_ptr_t;
#endif  // CONFIG_VP8

#if CONFIG_VP9
typedef struct vp9_variance_vtable {
  int highbd_flag;
  vpx_sad_fn_t sdf;
  vpx_sad_avg_fn_t sdaf;
  vpx_variance_fn_t vf;
  vpx_variance_four_fn_t v4f;
  vpx_half_pixel_fn_t hhf;
  vpx_half_pixel_fn_t vhf;
  vpx_half_pixel_avg_variance_four_fn_t hav4f;
  vpx_sub_pixel_variance_fn_t svf;
  vpx_sub_pixel_avg_variance_fn_t svaf;
  vpx_sad_multi_d_fn_t sdx4df;
} vp9_variance_fn_ptr_t;
#endif  // CONFIG_VP9

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // VPX_DSP_VARIANCE_H_
