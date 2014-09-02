/*
 *  Copyright (c) 2010 The WebM project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include <emmintrin.h>  // SSE2

#include "./vpx_config.h"

#include "vp9/encoder/vp9_variance.h"
#include "vpx_ports/mem.h"

typedef unsigned int (*variance_fn_t) (const unsigned char *src, int src_stride,
                                       const unsigned char *ref, int ref_stride,
                                       unsigned int *sse, int *sum);

#define READ64(p, stride, i) \
  _mm_unpacklo_epi8(_mm_cvtsi32_si128(*(const uint32_t *)(p + i * stride)), \
      _mm_cvtsi32_si128(*(const uint32_t *)(p + (i + 1) * stride)))

unsigned int vp9_get4x4var_sse2(const uint8_t *src, int src_stride,
                                const uint8_t *ref, int ref_stride,
                                unsigned int *sse, int *sum) {
  const __m128i zero = _mm_setzero_si128();
  const __m128i src0 = _mm_unpacklo_epi8(READ64(src, src_stride, 0), zero);
  const __m128i src1 = _mm_unpacklo_epi8(READ64(src, src_stride, 2), zero);
  const __m128i ref0 = _mm_unpacklo_epi8(READ64(ref, ref_stride, 0), zero);
  const __m128i ref1 = _mm_unpacklo_epi8(READ64(ref, ref_stride, 2), zero);
  const __m128i diff0 = _mm_sub_epi16(src0, ref0);
  const __m128i diff1 = _mm_sub_epi16(src1, ref1);

  // sum
  __m128i vsum = _mm_add_epi16(diff0, diff1);
  vsum = _mm_add_epi16(vsum, _mm_srli_si128(vsum, 8));
  vsum = _mm_add_epi16(vsum, _mm_srli_si128(vsum, 4));
  vsum = _mm_add_epi16(vsum, _mm_srli_si128(vsum, 2));
  *sum = (int16_t)_mm_extract_epi16(vsum, 0);

  // sse
  vsum = _mm_add_epi32(_mm_madd_epi16(diff0, diff0),
                       _mm_madd_epi16(diff1, diff1));
  vsum = _mm_add_epi32(vsum, _mm_srli_si128(vsum, 8));
  vsum = _mm_add_epi32(vsum, _mm_srli_si128(vsum, 4));
  *sse = _mm_cvtsi128_si32(vsum);

  return 0;
}

unsigned int vp9_get8x8var_sse2(const uint8_t *src, int src_stride,
                                const uint8_t *ref, int ref_stride,
                                unsigned int *sse, int *sum) {
  const __m128i zero = _mm_setzero_si128();
  __m128i vsum = _mm_setzero_si128();
  __m128i vsse = _mm_setzero_si128();
  int i;

  for (i = 0; i < 8; i += 2) {
    const __m128i src0 = _mm_unpacklo_epi8(_mm_loadl_epi64(
        (const __m128i *)(src + i * src_stride)), zero);
    const __m128i ref0 = _mm_unpacklo_epi8(_mm_loadl_epi64(
        (const __m128i *)(ref + i * ref_stride)), zero);
    const __m128i diff0 = _mm_sub_epi16(src0, ref0);

    const __m128i src1 = _mm_unpacklo_epi8(_mm_loadl_epi64(
        (const __m128i *)(src + (i + 1) * src_stride)), zero);
    const __m128i ref1 = _mm_unpacklo_epi8(_mm_loadl_epi64(
        (const __m128i *)(ref + (i + 1) * ref_stride)), zero);
    const __m128i diff1 = _mm_sub_epi16(src1, ref1);

    vsum = _mm_add_epi16(vsum, diff0);
    vsum = _mm_add_epi16(vsum, diff1);
    vsse = _mm_add_epi32(vsse, _mm_madd_epi16(diff0, diff0));
    vsse = _mm_add_epi32(vsse, _mm_madd_epi16(diff1, diff1));
  }

  // sum
  vsum = _mm_add_epi16(vsum, _mm_srli_si128(vsum, 8));
  vsum = _mm_add_epi16(vsum, _mm_srli_si128(vsum, 4));
  vsum = _mm_add_epi16(vsum, _mm_srli_si128(vsum, 2));
  *sum = (int16_t)_mm_extract_epi16(vsum, 0);

  // sse
  vsse = _mm_add_epi32(vsse, _mm_srli_si128(vsse, 8));
  vsse = _mm_add_epi32(vsse, _mm_srli_si128(vsse, 4));
  *sse = _mm_cvtsi128_si32(vsse);

  return 0;
}

unsigned int vp9_get16x16var_sse2(const unsigned char *src, int src_stride,
                                  const unsigned char *ref, int ref_stride,
                                  unsigned int *sse, int *sum);

static void variance_sse2(const unsigned char *src, int src_stride,
                          const unsigned char *ref, int ref_stride,
                          int w, int h, unsigned int *sse, int *sum,
                          variance_fn_t var_fn, int block_size) {
  int i, j;

  *sse = 0;
  *sum = 0;

  for (i = 0; i < h; i += block_size) {
    for (j = 0; j < w; j += block_size) {
      unsigned int sse0;
      int sum0;
      var_fn(src + src_stride * i + j, src_stride,
             ref + ref_stride * i + j, ref_stride, &sse0, &sum0);
      *sse += sse0;
      *sum += sum0;
    }
  }
}

unsigned int vp9_variance4x4_sse2(const unsigned char *src, int src_stride,
                                  const unsigned char *ref, int ref_stride,
                                  unsigned int *sse) {
  int sum;
  vp9_get4x4var_sse2(src, src_stride, ref, ref_stride, sse, &sum);
  return *sse - (((unsigned int)sum * sum) >> 4);
}

unsigned int vp9_variance8x4_sse2(const uint8_t *src, int src_stride,
                                  const uint8_t *ref, int ref_stride,
                                  unsigned int *sse) {
  int sum;
  variance_sse2(src, src_stride, ref, ref_stride, 8, 4,
                sse, &sum, vp9_get4x4var_sse2, 4);
  return *sse - (((unsigned int)sum * sum) >> 5);
}

unsigned int vp9_variance4x8_sse2(const uint8_t *src, int src_stride,
                                  const uint8_t *ref, int ref_stride,
                                  unsigned int *sse) {
  int sum;
  variance_sse2(src, src_stride, ref, ref_stride, 4, 8,
                sse, &sum, vp9_get4x4var_sse2, 4);
  return *sse - (((unsigned int)sum * sum) >> 5);
}

unsigned int vp9_variance8x8_sse2(const unsigned char *src, int src_stride,
                                  const unsigned char *ref, int ref_stride,
                                  unsigned int *sse) {
  int sum;
  vp9_get8x8var_sse2(src, src_stride, ref, ref_stride, sse, &sum);
  return *sse - (((unsigned int)sum * sum) >> 6);
}

unsigned int vp9_variance16x8_sse2(const unsigned char *src, int src_stride,
                                   const unsigned char *ref, int ref_stride,
                                   unsigned int *sse) {
  int sum;
  variance_sse2(src, src_stride, ref, ref_stride, 16, 8,
                sse, &sum, vp9_get8x8var_sse2, 8);
  return *sse - (((unsigned int)sum * sum) >> 7);
}

unsigned int vp9_variance8x16_sse2(const unsigned char *src, int src_stride,
                                   const unsigned char *ref, int ref_stride,
                                   unsigned int *sse) {
  int sum;
  variance_sse2(src, src_stride, ref, ref_stride, 8, 16,
                sse, &sum, vp9_get8x8var_sse2, 8);
  return *sse - (((unsigned int)sum * sum) >> 7);
}

unsigned int vp9_variance16x16_sse2(const unsigned char *src, int src_stride,
                                    const unsigned char *ref, int ref_stride,
                                    unsigned int *sse) {
  int sum;
  variance_sse2(src, src_stride, ref, ref_stride, 16, 16,
                sse, &sum, vp9_get16x16var_sse2, 16);
  return *sse - (((unsigned int)sum * sum) >> 8);
}

unsigned int vp9_variance32x32_sse2(const uint8_t *src, int src_stride,
                                    const uint8_t *ref, int ref_stride,
                                    unsigned int *sse) {
  int sum;
  variance_sse2(src, src_stride, ref, ref_stride, 32, 32,
                sse, &sum, vp9_get16x16var_sse2, 16);
  return *sse - (((int64_t)sum * sum) >> 10);
}

unsigned int vp9_variance32x16_sse2(const uint8_t *src, int src_stride,
                                    const uint8_t *ref, int ref_stride,
                                    unsigned int *sse) {
  int sum;
  variance_sse2(src, src_stride, ref, ref_stride, 32, 16,
                sse, &sum, vp9_get16x16var_sse2, 16);
  return *sse - (((int64_t)sum * sum) >> 9);
}

unsigned int vp9_variance16x32_sse2(const uint8_t *src, int src_stride,
                                    const uint8_t *ref, int ref_stride,
                                    unsigned int *sse) {
  int sum;
  variance_sse2(src, src_stride, ref, ref_stride, 16, 32,
                sse, &sum, vp9_get16x16var_sse2, 16);
  return *sse - (((int64_t)sum * sum) >> 9);
}

unsigned int vp9_variance64x64_sse2(const uint8_t *src, int src_stride,
                                    const uint8_t *ref, int ref_stride,
                                    unsigned int *sse) {
  int sum;
  variance_sse2(src, src_stride, ref, ref_stride, 64, 64,
                sse, &sum, vp9_get16x16var_sse2, 16);
  return *sse - (((int64_t)sum * sum) >> 12);
}

unsigned int vp9_variance64x32_sse2(const uint8_t *src, int src_stride,
                                    const uint8_t *ref, int ref_stride,
                                    unsigned int *sse) {
  int sum;
  variance_sse2(src, src_stride, ref, ref_stride, 64, 32,
                sse, &sum, vp9_get16x16var_sse2, 16);
  return *sse - (((int64_t)sum * sum) >> 11);
}

unsigned int vp9_variance32x64_sse2(const uint8_t *src, int src_stride,
                                    const uint8_t *ref, int ref_stride,
                                    unsigned int *sse) {
  int sum;
  variance_sse2(src, src_stride, ref, ref_stride, 32, 64,
                sse, &sum, vp9_get16x16var_sse2, 16);
  return *sse - (((int64_t)sum * sum) >> 11);
}

unsigned int vp9_mse8x8_sse2(const uint8_t *src, int src_stride,
                             const uint8_t *ref, int ref_stride,
                             unsigned int *sse) {
  vp9_variance8x8_sse2(src, src_stride, ref, ref_stride, sse);
  return *sse;
}

unsigned int vp9_mse8x16_sse2(const uint8_t *src, int src_stride,
                              const uint8_t *ref, int ref_stride,
                              unsigned int *sse) {
  vp9_variance8x16_sse2(src, src_stride, ref, ref_stride, sse);
  return *sse;
}

unsigned int vp9_mse16x8_sse2(const uint8_t *src, int src_stride,
                              const uint8_t *ref, int ref_stride,
                              unsigned int *sse) {
  vp9_variance16x8_sse2(src, src_stride, ref, ref_stride, sse);
  return *sse;
}

unsigned int vp9_mse16x16_sse2(const uint8_t *src, int src_stride,
                               const uint8_t *ref, int ref_stride,
                               unsigned int *sse) {
  vp9_variance16x16_sse2(src, src_stride, ref, ref_stride, sse);
  return *sse;
}

#define DECL(w, opt) \
int vp9_sub_pixel_variance##w##xh_##opt(const uint8_t *src, \
                                        ptrdiff_t src_stride, \
                                        int x_offset, int y_offset, \
                                        const uint8_t *dst, \
                                        ptrdiff_t dst_stride, \
                                        int height, unsigned int *sse)
#define DECLS(opt1, opt2) \
DECL(4, opt2); \
DECL(8, opt1); \
DECL(16, opt1)

DECLS(sse2, sse);
DECLS(ssse3, ssse3);
#undef DECLS
#undef DECL

#define FN(w, h, wf, wlog2, hlog2, opt, cast) \
unsigned int vp9_sub_pixel_variance##w##x##h##_##opt(const uint8_t *src, \
                                                     int src_stride, \
                                                     int x_offset, \
                                                     int y_offset, \
                                                     const uint8_t *dst, \
                                                     int dst_stride, \
                                                     unsigned int *sse_ptr) { \
  unsigned int sse; \
  int se = vp9_sub_pixel_variance##wf##xh_##opt(src, src_stride, x_offset, \
                                                y_offset, dst, dst_stride, \
                                                h, &sse); \
  if (w > wf) { \
    unsigned int sse2; \
    int se2 = vp9_sub_pixel_variance##wf##xh_##opt(src + 16, src_stride, \
                                                   x_offset, y_offset, \
                                                   dst + 16, dst_stride, \
                                                   h, &sse2); \
    se += se2; \
    sse += sse2; \
    if (w > wf * 2) { \
      se2 = vp9_sub_pixel_variance##wf##xh_##opt(src + 32, src_stride, \
                                                 x_offset, y_offset, \
                                                 dst + 32, dst_stride, \
                                                 h, &sse2); \
      se += se2; \
      sse += sse2; \
      se2 = vp9_sub_pixel_variance##wf##xh_##opt(src + 48, src_stride, \
                                                 x_offset, y_offset, \
                                                 dst + 48, dst_stride, \
                                                 h, &sse2); \
      se += se2; \
      sse += sse2; \
    } \
  } \
  *sse_ptr = sse; \
  return sse - ((cast se * se) >> (wlog2 + hlog2)); \
}

#define FNS(opt1, opt2) \
FN(64, 64, 16, 6, 6, opt1, (int64_t)); \
FN(64, 32, 16, 6, 5, opt1, (int64_t)); \
FN(32, 64, 16, 5, 6, opt1, (int64_t)); \
FN(32, 32, 16, 5, 5, opt1, (int64_t)); \
FN(32, 16, 16, 5, 4, opt1, (int64_t)); \
FN(16, 32, 16, 4, 5, opt1, (int64_t)); \
FN(16, 16, 16, 4, 4, opt1, (unsigned int)); \
FN(16,  8, 16, 4, 3, opt1, (unsigned int)); \
FN(8,  16,  8, 3, 4, opt1, (unsigned int)); \
FN(8,   8,  8, 3, 3, opt1, (unsigned int)); \
FN(8,   4,  8, 3, 2, opt1, (unsigned int)); \
FN(4,   8,  4, 2, 3, opt2, (unsigned int)); \
FN(4,   4,  4, 2, 2, opt2, (unsigned int))

FNS(sse2, sse);
FNS(ssse3, ssse3);

#undef FNS
#undef FN

#define DECL(w, opt) \
int vp9_sub_pixel_avg_variance##w##xh_##opt(const uint8_t *src, \
                                            ptrdiff_t src_stride, \
                                            int x_offset, int y_offset, \
                                            const uint8_t *dst, \
                                            ptrdiff_t dst_stride, \
                                            const uint8_t *sec, \
                                            ptrdiff_t sec_stride, \
                                            int height, unsigned int *sse)
#define DECLS(opt1, opt2) \
DECL(4, opt2); \
DECL(8, opt1); \
DECL(16, opt1)

DECLS(sse2, sse);
DECLS(ssse3, ssse3);
#undef DECL
#undef DECLS

#define FN(w, h, wf, wlog2, hlog2, opt, cast) \
unsigned int vp9_sub_pixel_avg_variance##w##x##h##_##opt(const uint8_t *src, \
                                                         int src_stride, \
                                                         int x_offset, \
                                                         int y_offset, \
                                                         const uint8_t *dst, \
                                                         int dst_stride, \
                                                         unsigned int *sseptr, \
                                                         const uint8_t *sec) { \
  unsigned int sse; \
  int se = vp9_sub_pixel_avg_variance##wf##xh_##opt(src, src_stride, x_offset, \
                                                    y_offset, dst, dst_stride, \
                                                    sec, w, h, &sse); \
  if (w > wf) { \
    unsigned int sse2; \
    int se2 = vp9_sub_pixel_avg_variance##wf##xh_##opt(src + 16, src_stride, \
                                                       x_offset, y_offset, \
                                                       dst + 16, dst_stride, \
                                                       sec + 16, w, h, &sse2); \
    se += se2; \
    sse += sse2; \
    if (w > wf * 2) { \
      se2 = vp9_sub_pixel_avg_variance##wf##xh_##opt(src + 32, src_stride, \
                                                     x_offset, y_offset, \
                                                     dst + 32, dst_stride, \
                                                     sec + 32, w, h, &sse2); \
      se += se2; \
      sse += sse2; \
      se2 = vp9_sub_pixel_avg_variance##wf##xh_##opt(src + 48, src_stride, \
                                                     x_offset, y_offset, \
                                                     dst + 48, dst_stride, \
                                                     sec + 48, w, h, &sse2); \
      se += se2; \
      sse += sse2; \
    } \
  } \
  *sseptr = sse; \
  return sse - ((cast se * se) >> (wlog2 + hlog2)); \
}

#define FNS(opt1, opt2) \
FN(64, 64, 16, 6, 6, opt1, (int64_t)); \
FN(64, 32, 16, 6, 5, opt1, (int64_t)); \
FN(32, 64, 16, 5, 6, opt1, (int64_t)); \
FN(32, 32, 16, 5, 5, opt1, (int64_t)); \
FN(32, 16, 16, 5, 4, opt1, (int64_t)); \
FN(16, 32, 16, 4, 5, opt1, (int64_t)); \
FN(16, 16, 16, 4, 4, opt1, (unsigned int)); \
FN(16,  8, 16, 4, 3, opt1, (unsigned int)); \
FN(8,  16,  8, 3, 4, opt1, (unsigned int)); \
FN(8,   8,  8, 3, 3, opt1, (unsigned int)); \
FN(8,   4,  8, 3, 2, opt1, (unsigned int)); \
FN(4,   8,  4, 2, 3, opt2, (unsigned int)); \
FN(4,   4,  4, 2, 2, opt2, (unsigned int))

FNS(sse2, sse);
FNS(ssse3, ssse3);

#undef FNS
#undef FN
