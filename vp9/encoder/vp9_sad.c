/*
 *  Copyright (c) 2010 The WebM project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include <stdlib.h>

#include "./vp9_rtcd.h"
#include "./vpx_config.h"

#include "vpx/vpx_integer.h"
#if CONFIG_VP9_HIGH
#include "vp9/common/vp9_common.h"
#endif
#include "vp9/encoder/vp9_variance.h"

static INLINE unsigned int sad(const uint8_t *a, int a_stride,
                               const uint8_t *b, int b_stride,
                               int width, int height) {
  int y, x;
  unsigned int sad = 0;

  for (y = 0; y < height; y++) {
    for (x = 0; x < width; x++)
      sad += abs(a[x] - b[x]);

    a += a_stride;
    b += b_stride;
  }

  return sad;
}

#define sadMxN(m, n) \
unsigned int vp9_sad##m##x##n##_c(const uint8_t *src, int src_stride, \
                                  const uint8_t *ref, int ref_stride, \
                                  unsigned int max_sad) { \
  return sad(src, src_stride, ref, ref_stride, m, n); \
} \
unsigned int vp9_sad##m##x##n##_avg_c(const uint8_t *src, int src_stride, \
                                      const uint8_t *ref, int ref_stride, \
                                      const uint8_t *second_pred, \
                                      unsigned int max_sad) { \
  uint8_t comp_pred[m * n]; \
  vp9_comp_avg_pred(comp_pred, second_pred, m, n, ref, ref_stride); \
  return sad(src, src_stride, comp_pred, m, m, n); \
}

#define sadMxNxK(m, n, k) \
void vp9_sad##m##x##n##x##k##_c(const uint8_t *src, int src_stride, \
                                const uint8_t *ref, int ref_stride, \
                                unsigned int *sads) { \
  int i; \
  for (i = 0; i < k; ++i) \
    sads[i] = vp9_sad##m##x##n##_c(src, src_stride, &ref[i], ref_stride, \
                                   0x7fffffff); \
}

#define sadMxNx4D(m, n) \
void vp9_sad##m##x##n##x4d_c(const uint8_t *src, int src_stride, \
                             const uint8_t *const refs[], int ref_stride, \
                             unsigned int *sads) { \
  int i; \
  for (i = 0; i < 4; ++i) \
    sads[i] = vp9_sad##m##x##n##_c(src, src_stride, refs[i], ref_stride, \
                                   0x7fffffff); \
}

// 64x64
sadMxN(64, 64)
sadMxNxK(64, 64, 3)
sadMxNxK(64, 64, 8)
sadMxNx4D(64, 64)

// 64x32
sadMxN(64, 32)
sadMxNx4D(64, 32)

// 32x64
sadMxN(32, 64)
sadMxNx4D(32, 64)

// 32x32
sadMxN(32, 32)
sadMxNxK(32, 32, 3)
sadMxNxK(32, 32, 8)
sadMxNx4D(32, 32)

// 32x16
sadMxN(32, 16)
sadMxNx4D(32, 16)

// 16x32
sadMxN(16, 32)
sadMxNx4D(16, 32)

// 16x16
sadMxN(16, 16)
sadMxNxK(16, 16, 3)
sadMxNxK(16, 16, 8)
sadMxNx4D(16, 16)

// 16x8
sadMxN(16, 8)
sadMxNxK(16, 8, 3)
sadMxNxK(16, 8, 8)
sadMxNx4D(16, 8)

// 8x16
sadMxN(8, 16)
sadMxNxK(8, 16, 3)
sadMxNxK(8, 16, 8)
sadMxNx4D(8, 16)

// 8x8
sadMxN(8, 8)
sadMxNxK(8, 8, 3)
sadMxNxK(8, 8, 8)
sadMxNx4D(8, 8)

// 8x4
sadMxN(8, 4)
sadMxNxK(8, 4, 8)
sadMxNx4D(8, 4)

// 4x8
sadMxN(4, 8)
sadMxNxK(4, 8, 8)
sadMxNx4D(4, 8)

// 4x4
sadMxN(4, 4)
sadMxNxK(4, 4, 3)
sadMxNxK(4, 4, 8)
sadMxNx4D(4, 4)

#if CONFIG_VP9_HIGH
static INLINE unsigned int high_sad(const uint8_t *a8, int a_stride,
                               const uint8_t *b8, int b_stride,
                               int width, int height) {
  int y, x;
  unsigned int sad = 0;
  uint16_t *a = CONVERT_TO_SHORTPTR(a8);
  uint16_t *b = CONVERT_TO_SHORTPTR(b8);
  for (y = 0; y < height; y++) {
    for (x = 0; x < width; x++)
      sad += abs(a[x] - b[x]);

    a += a_stride;
    b += b_stride;
  }

  return sad;
}

static INLINE unsigned int high_sadb(const uint8_t *a8, int a_stride,
                               const uint16_t *b, int b_stride,
                               int width, int height) {
  int y, x;
  unsigned int sad = 0;
  uint16_t *a = CONVERT_TO_SHORTPTR(a8);
  for (y = 0; y < height; y++) {
    for (x = 0; x < width; x++)
      sad += abs(a[x] - b[x]);

    a += a_stride;
    b += b_stride;
  }

  return sad;
}

#define high_sad_mxn_func(m, n) \
unsigned int vp9_high_sad##m##x##n##_c(const uint8_t *src_ptr, int src_stride, \
                                       const uint8_t *ref_ptr, int ref_stride, \
                                       unsigned int max_sad) { \
  return high_sad(src_ptr, src_stride, ref_ptr, ref_stride, m, n); \
} \
unsigned int vp9_high_sad##m##x##n##_avg_c(const uint8_t *src_ptr, \
                                           int src_stride, \
                                           const uint8_t *ref_ptr, \
                                           int ref_stride, \
                                           const uint8_t *second_pred, \
                                           unsigned int max_sad) { \
  uint16_t comp_pred[m * n]; \
  high_comp_avg_pred(comp_pred, second_pred, m, n, ref_ptr, ref_stride); \
  return high_sadb(src_ptr, src_stride, comp_pred, m, m, n); \
}

high_sad_mxn_func(64, 64)
high_sad_mxn_func(64, 32)
high_sad_mxn_func(32, 64)
high_sad_mxn_func(32, 32)
high_sad_mxn_func(32, 16)
high_sad_mxn_func(16, 32)
high_sad_mxn_func(16, 16)
high_sad_mxn_func(16, 8)
high_sad_mxn_func(8, 16)
high_sad_mxn_func(8, 8)
high_sad_mxn_func(8, 4)
high_sad_mxn_func(4, 8)
high_sad_mxn_func(4, 4)

void vp9_high_sad64x32x4d_c(const uint8_t *src_ptr, int src_stride,
                       const uint8_t* const ref_ptr[], int ref_stride,
                       unsigned int *sad_array) {
  int i;
  for (i = 0; i < 4; ++i)
    sad_array[i] = vp9_high_sad64x32(src_ptr, src_stride, ref_ptr[i],
                                     ref_stride, 0x7fffffff);
}

void vp9_high_sad32x64x4d_c(const uint8_t *src_ptr, int src_stride,
                            const uint8_t* const ref_ptr[],
                            int ref_stride, unsigned int *sad_array) {
  int i;
  for (i = 0; i < 4; ++i)
    sad_array[i] = vp9_high_sad32x64(src_ptr, src_stride, ref_ptr[i],
                                     ref_stride, 0x7fffffff);
}

void vp9_high_sad32x16x4d_c(const uint8_t *src_ptr, int src_stride,
                            const uint8_t* const ref_ptr[], int ref_stride,
                            unsigned int *sad_array) {
  int i;
  for (i = 0; i < 4; ++i)
    sad_array[i] = vp9_high_sad32x16(src_ptr, src_stride, ref_ptr[i],
                                     ref_stride, 0x7fffffff);
}

void vp9_high_sad16x32x4d_c(const uint8_t *src_ptr, int src_stride,
                            const uint8_t* const ref_ptr[],
                            int ref_stride, unsigned int *sad_array) {
  int i;
  for (i = 0; i < 4; ++i)
    sad_array[i] = vp9_high_sad16x32(src_ptr, src_stride, ref_ptr[i],
                                     ref_stride, 0x7fffffff);
}

void vp9_high_sad64x64x3_c(const uint8_t *src_ptr, int src_stride,
                      const uint8_t *ref_ptr, int ref_stride,
                      unsigned int *sad_array) {
  int i;
  for (i = 0; i < 3; ++i)
    sad_array[i] = vp9_high_sad64x64(src_ptr, src_stride, ref_ptr + i,
                                     ref_stride, 0x7fffffff);
}

void vp9_high_sad32x32x3_c(const uint8_t *src_ptr, int src_stride,
                      const uint8_t *ref_ptr, int ref_stride,
                      unsigned int *sad_array) {
  int i;
  for (i = 0; i < 3; ++i)
    sad_array[i] = vp9_high_sad32x32(src_ptr, src_stride, ref_ptr + i,
                                     ref_stride, 0x7fffffff);
}

void vp9_high_sad64x64x8_c(const uint8_t *src_ptr, int src_stride,
                           const uint8_t *ref_ptr, int ref_stride,
                           unsigned int *sad_array) {
  int i;
  for (i = 0; i < 8; ++i)
    sad_array[i] = vp9_high_sad64x64(src_ptr, src_stride, ref_ptr + i,
                                     ref_stride, 0x7fffffff);
}

void vp9_high_sad32x32x8_c(const uint8_t *src_ptr, int src_stride,
                           const uint8_t *ref_ptr, int ref_stride,
                           unsigned int *sad_array) {
  int i;
  for (i = 0; i < 8; ++i)
    sad_array[i] = vp9_high_sad32x32(src_ptr, src_stride, ref_ptr + i,
                                     ref_stride, 0x7fffffff);
}

void vp9_high_sad16x16x3_c(const uint8_t *src_ptr, int src_stride,
                           const uint8_t *ref_ptr, int ref_stride,
                           unsigned int *sad_array) {
  int i;
  for (i = 0; i < 3; ++i)
    sad_array[i] = vp9_high_sad16x16(src_ptr, src_stride, ref_ptr + i,
                                     ref_stride, 0x7fffffff);
}

void vp9_high_sad16x16x8_c(const uint8_t *src_ptr, int src_stride,
                           const uint8_t *ref_ptr, int ref_stride,
                           uint32_t *sad_array) {
  int i;
  for (i = 0; i < 8; ++i)
    sad_array[i] = vp9_high_sad16x16(src_ptr, src_stride, ref_ptr + i,
                                     ref_stride, 0x7fffffff);
}

void vp9_high_sad16x8x3_c(const uint8_t *src_ptr, int src_stride,
                          const uint8_t *ref_ptr, int ref_stride,
                          unsigned int *sad_array) {
  int i;
  for (i = 0; i < 3; ++i)
    sad_array[i] = vp9_high_sad16x8(src_ptr, src_stride, ref_ptr + i,
                                    ref_stride, 0x7fffffff);
}

void vp9_high_sad16x8x8_c(const uint8_t *src_ptr, int src_stride,
                          const uint8_t *ref_ptr, int ref_stride,
                          uint32_t *sad_array) {
  int i;
  for (i = 0; i < 8; ++i)
    sad_array[i] = vp9_high_sad16x8(src_ptr, src_stride, ref_ptr + i,
                                    ref_stride, 0x7fffffff);
}

void vp9_high_sad8x8x3_c(const uint8_t *src_ptr, int src_stride,
                         const uint8_t *ref_ptr, int ref_stride,
                         unsigned int *sad_array) {
  int i;
  for (i = 0; i < 3; ++i)
    sad_array[i] = vp9_high_sad8x8(src_ptr, src_stride, ref_ptr + i,
                                   ref_stride, 0x7fffffff);
}

void vp9_high_sad8x8x8_c(const uint8_t *src_ptr, int src_stride,
                         const uint8_t *ref_ptr, int ref_stride,
                         uint32_t *sad_array) {
  int i;
  for (i = 0; i < 8; ++i)
    sad_array[i] = vp9_high_sad8x8(src_ptr, src_stride, ref_ptr + i,
                                   ref_stride, 0x7fffffff);
}

void vp9_high_sad8x16x3_c(const uint8_t *src_ptr, int src_stride,
                          const uint8_t *ref_ptr, int ref_stride,
                          unsigned int *sad_array) {
  int i;
  for (i = 0; i < 3; ++i)
    sad_array[i] = vp9_high_sad8x16(src_ptr, src_stride, ref_ptr + i,
                                    ref_stride, 0x7fffffff);
}

void vp9_high_sad8x16x8_c(const uint8_t *src_ptr, int src_stride,
                          const uint8_t *ref_ptr, int ref_stride,
                          uint32_t *sad_array) {
  int i;
  for (i = 0; i < 8; ++i)
    sad_array[i] = vp9_high_sad8x16(src_ptr, src_stride, ref_ptr + i,
                                    ref_stride, 0x7fffffff);
}

void vp9_high_sad4x4x3_c(const uint8_t *src_ptr, int src_stride,
                         const uint8_t *ref_ptr, int ref_stride,
                         unsigned int *sad_array) {
  int i;
  for (i = 0; i < 3; ++i)
    sad_array[i] = vp9_high_sad4x4(src_ptr, src_stride, ref_ptr + i,
                                   ref_stride, 0x7fffffff);
}

void vp9_high_sad4x4x8_c(const uint8_t *src_ptr, int src_stride,
                    const uint8_t *ref_ptr, int ref_stride,
                    uint32_t *sad_array) {
  int i;
  for (i = 0; i < 8; ++i)
    sad_array[i] = vp9_high_sad4x4(src_ptr, src_stride, ref_ptr + i,
                                   ref_stride, 0x7fffffff);
}

void vp9_high_sad64x64x4d_c(const uint8_t *src_ptr, int src_stride,
                            const uint8_t* const ref_ptr[], int ref_stride,
                            unsigned int *sad_array) {
  int i;
  for (i = 0; i < 4; ++i)
    sad_array[i] = vp9_high_sad64x64(src_ptr, src_stride, ref_ptr[i],
                                     ref_stride, 0x7fffffff);
}

void vp9_high_sad32x32x4d_c(const uint8_t *src_ptr, int src_stride,
                            const uint8_t* const ref_ptr[], int ref_stride,
                            unsigned int *sad_array) {
  int i;
  for (i = 0; i < 4; ++i)
    sad_array[i] = vp9_high_sad32x32(src_ptr, src_stride, ref_ptr[i],
                                     ref_stride, 0x7fffffff);
}

void vp9_high_sad16x16x4d_c(const uint8_t *src_ptr, int src_stride,
                            const uint8_t* const ref_ptr[], int ref_stride,
                            unsigned int *sad_array) {
  int i;
  for (i = 0; i < 4; ++i)
    sad_array[i] = vp9_high_sad16x16(src_ptr, src_stride, ref_ptr[i],
                                     ref_stride, 0x7fffffff);
}

void vp9_high_sad16x8x4d_c(const uint8_t *src_ptr, int src_stride,
                           const uint8_t* const ref_ptr[], int ref_stride,
                           unsigned int *sad_array) {
  int i;
  for (i = 0; i < 4; ++i)
    sad_array[i] = vp9_high_sad16x8(src_ptr, src_stride, ref_ptr[i],
                                    ref_stride, 0x7fffffff);
}

void vp9_high_sad8x8x4d_c(const uint8_t *src_ptr, int src_stride,
                          const uint8_t* const ref_ptr[], int ref_stride,
                          unsigned int *sad_array) {
  int i;
  for (i = 0; i < 4; ++i)
    sad_array[i] = vp9_high_sad8x8(src_ptr, src_stride, ref_ptr[i],
                                   ref_stride, 0x7fffffff);
}

void vp9_high_sad8x16x4d_c(const uint8_t *src_ptr, int src_stride,
                           const uint8_t* const ref_ptr[], int ref_stride,
                           unsigned int *sad_array) {
  int i;
  for (i = 0; i < 4; ++i)
    sad_array[i] = vp9_high_sad8x16(src_ptr, src_stride, ref_ptr[i],
                                    ref_stride, 0x7fffffff);
}

void vp9_high_sad8x4x4d_c(const uint8_t *src_ptr, int src_stride,
                          const uint8_t* const ref_ptr[], int ref_stride,
                          unsigned int *sad_array) {
  int i;
  for (i = 0; i < 4; ++i)
    sad_array[i] = vp9_high_sad8x4(src_ptr, src_stride, ref_ptr[i],
                                   ref_stride, 0x7fffffff);
}

void vp9_high_sad8x4x8_c(const uint8_t *src_ptr, int src_stride,
                         const uint8_t *ref_ptr, int ref_stride,
                         uint32_t *sad_array) {
  int i;
  for (i = 0; i < 8; ++i)
    sad_array[i] = vp9_high_sad8x4(src_ptr, src_stride, ref_ptr + i,
                                   ref_stride, 0x7fffffff);
}

void vp9_high_sad4x8x4d_c(const uint8_t *src_ptr, int src_stride,
                          const uint8_t* const ref_ptr[], int ref_stride,
                          unsigned int *sad_array) {
  int i;
  for (i = 0; i < 4; ++i)
    sad_array[i] = vp9_high_sad4x8(src_ptr, src_stride, ref_ptr[i],
                                   ref_stride, 0x7fffffff);
}

void vp9_high_sad4x8x8_c(const uint8_t *src_ptr, int src_stride,
                         const uint8_t *ref_ptr, int ref_stride,
                         uint32_t *sad_array) {
  int i;
  for (i = 0; i < 8; ++i)
    sad_array[i] = vp9_high_sad4x8(src_ptr, src_stride, ref_ptr + i,
                                   ref_stride, 0x7fffffff);
}

void vp9_high_sad4x4x4d_c(const uint8_t *src_ptr, int src_stride,
                          const uint8_t* const ref_ptr[], int ref_stride,
                          unsigned int *sad_array) {
  int i;
  for (i = 0; i < 4; ++i)
    sad_array[i] = vp9_high_sad4x4(src_ptr, src_stride, ref_ptr[i],
                                   ref_stride, 0x7fffffff);
}

#endif
