/*
 * Copyright (c) 2016, Alliance for Open Media. All rights reserved
 *
 * This source code is subject to the terms of the BSD 2 Clause License and
 * the Alliance for Open Media Patent License 1.0. If the BSD 2 Clause License
 * was not distributed with this source code in the LICENSE file, you can
 * obtain it at www.aomedia.org/license/software. If the Alliance for Open
 * Media Patent License 1.0 was not distributed with this source code in the
 * PATENTS file, you can obtain it at www.aomedia.org/license/patent.
 */

#include <immintrin.h>  // avx2

#include "./av1_rtcd.h"
#include "./aom_dsp_rtcd.h"

#include "aom_dsp/txfm_common.h"
#include "aom_dsp/x86/txfm_common_avx2.h"

static INLINE void mm256_reverse_epi16(__m256i *u) {
  const __m256i control = _mm256_set_epi16(
      0x0100, 0x0302, 0x0504, 0x0706, 0x0908, 0x0B0A, 0x0D0C, 0x0F0E, 0x0100,
      0x0302, 0x0504, 0x0706, 0x0908, 0x0B0A, 0x0D0C, 0x0F0E);
  __m256i v = _mm256_shuffle_epi8(*u, control);
  *u = _mm256_permute2x128_si256(v, v, 1);
}

void aom_fdct16x16_1_avx2(const int16_t *input, tran_low_t *output,
                          int stride) {
  __m256i r0, r1, r2, r3, u0, u1;
  __m256i zero = _mm256_setzero_si256();
  __m256i sum = _mm256_setzero_si256();
  const int16_t *blockBound = input + (stride << 4);
  __m128i v0, v1;

  while (input < blockBound) {
    r0 = _mm256_loadu_si256((__m256i const *)input);
    r1 = _mm256_loadu_si256((__m256i const *)(input + stride));
    r2 = _mm256_loadu_si256((__m256i const *)(input + 2 * stride));
    r3 = _mm256_loadu_si256((__m256i const *)(input + 3 * stride));

    u0 = _mm256_add_epi16(r0, r1);
    u1 = _mm256_add_epi16(r2, r3);
    sum = _mm256_add_epi16(sum, u0);
    sum = _mm256_add_epi16(sum, u1);

    input += stride << 2;
  }

  // unpack 16 int16_t into 2x8 int32_t
  u0 = _mm256_unpacklo_epi16(zero, sum);
  u1 = _mm256_unpackhi_epi16(zero, sum);
  u0 = _mm256_srai_epi32(u0, 16);
  u1 = _mm256_srai_epi32(u1, 16);
  sum = _mm256_add_epi32(u0, u1);

  u0 = _mm256_srli_si256(sum, 8);
  u1 = _mm256_add_epi32(sum, u0);

  v0 = _mm_add_epi32(_mm256_extracti128_si256(u1, 1),
                     _mm256_castsi256_si128(u1));
  v1 = _mm_srli_si128(v0, 4);
  v0 = _mm_add_epi32(v0, v1);
  v0 = _mm_srai_epi32(v0, 1);
  output[0] = (tran_low_t)_mm_extract_epi32(v0, 0);
}

static void mm256_transpose_16x16(__m256i *in) {
  __m256i tr0_0 = _mm256_unpacklo_epi16(in[0], in[1]);
  __m256i tr0_1 = _mm256_unpackhi_epi16(in[0], in[1]);
  __m256i tr0_2 = _mm256_unpacklo_epi16(in[2], in[3]);
  __m256i tr0_3 = _mm256_unpackhi_epi16(in[2], in[3]);
  __m256i tr0_4 = _mm256_unpacklo_epi16(in[4], in[5]);
  __m256i tr0_5 = _mm256_unpackhi_epi16(in[4], in[5]);
  __m256i tr0_6 = _mm256_unpacklo_epi16(in[6], in[7]);
  __m256i tr0_7 = _mm256_unpackhi_epi16(in[6], in[7]);

  __m256i tr0_8 = _mm256_unpacklo_epi16(in[8], in[9]);
  __m256i tr0_9 = _mm256_unpackhi_epi16(in[8], in[9]);
  __m256i tr0_a = _mm256_unpacklo_epi16(in[10], in[11]);
  __m256i tr0_b = _mm256_unpackhi_epi16(in[10], in[11]);
  __m256i tr0_c = _mm256_unpacklo_epi16(in[12], in[13]);
  __m256i tr0_d = _mm256_unpackhi_epi16(in[12], in[13]);
  __m256i tr0_e = _mm256_unpacklo_epi16(in[14], in[15]);
  __m256i tr0_f = _mm256_unpackhi_epi16(in[14], in[15]);

  // 00 10 01 11 02 12 03 13  08 18 09 19 0a 1a 0b 1b
  // 04 14 05 15 06 16 07 17  0c 1c 0d 1d 0e 1e 0f 1f
  // 20 30 21 31 22 32 23 33  28 38 29 39 2a 3a 2b 3b
  // 24 34 25 35 26 36 27 37  2c 3c 2d 3d 2e 3e 2f 3f
  // 40 50 41 51 42 52 43 53  48 58 49 59 4a 5a 4b 5b
  // 44 54 45 55 46 56 47 57  4c 5c 4d 5d 4e 5e 4f 5f
  // 60 70 61 71 62 72 63 73  68 78 69 79 6a 7a 6b 7b
  // 64 74 65 75 66 76 67 77  6c 7c 6d 7d 6e 7e 6f 7f

  // 80 90 81 91 82 92 83 93  88 98 89 99 8a 9a 8b 9b
  // 84 94 85 95 86 96 87 97  8c 9c 8d 9d 8e 9e 8f 9f
  // a0 b0 a1 b1 a2 b2 a3 b3  a8 b8 a9 b9 aa ba ab bb
  // a4 b4 a5 b5 a6 b6 a7 b7  ac bc ad bd ae be af bf
  // c0 d0 c1 d1 c2 d2 c3 d3  c8 d8 c9 d9 ca da cb db
  // c4 d4 c5 d5 c6 d6 c7 d7  cc dc cd dd ce de cf df
  // e0 f0 e1 f1 e2 f2 e3 f3  e8 f8 e9 f9 ea fa eb fb
  // e4 f4 e5 f5 e6 f6 e7 f7  ec fc ed fd ee fe ef ff

  __m256i tr1_0 = _mm256_unpacklo_epi32(tr0_0, tr0_2);
  __m256i tr1_1 = _mm256_unpackhi_epi32(tr0_0, tr0_2);
  __m256i tr1_2 = _mm256_unpacklo_epi32(tr0_1, tr0_3);
  __m256i tr1_3 = _mm256_unpackhi_epi32(tr0_1, tr0_3);
  __m256i tr1_4 = _mm256_unpacklo_epi32(tr0_4, tr0_6);
  __m256i tr1_5 = _mm256_unpackhi_epi32(tr0_4, tr0_6);
  __m256i tr1_6 = _mm256_unpacklo_epi32(tr0_5, tr0_7);
  __m256i tr1_7 = _mm256_unpackhi_epi32(tr0_5, tr0_7);

  __m256i tr1_8 = _mm256_unpacklo_epi32(tr0_8, tr0_a);
  __m256i tr1_9 = _mm256_unpackhi_epi32(tr0_8, tr0_a);
  __m256i tr1_a = _mm256_unpacklo_epi32(tr0_9, tr0_b);
  __m256i tr1_b = _mm256_unpackhi_epi32(tr0_9, tr0_b);
  __m256i tr1_c = _mm256_unpacklo_epi32(tr0_c, tr0_e);
  __m256i tr1_d = _mm256_unpackhi_epi32(tr0_c, tr0_e);
  __m256i tr1_e = _mm256_unpacklo_epi32(tr0_d, tr0_f);
  __m256i tr1_f = _mm256_unpackhi_epi32(tr0_d, tr0_f);

  // 00 10 20 30 01 11 21 31  08 18 28 38 09 19 29 39
  // 02 12 22 32 03 13 23 33  0a 1a 2a 3a 0b 1b 2b 3b
  // 04 14 24 34 05 15 25 35  0c 1c 2c 3c 0d 1d 2d 3d
  // 06 16 26 36 07 17 27 37  0e 1e 2e 3e 0f 1f 2f 3f
  // 40 50 60 70 41 51 61 71  48 58 68 78 49 59 69 79
  // 42 52 62 72 43 53 63 73  4a 5a 6a 7a 4b 5b 6b 7b
  // 44 54 64 74 45 55 65 75  4c 5c 6c 7c 4d 5d 6d 7d
  // 46 56 66 76 47 57 67 77  4e 5e 6e 7e 4f 5f 6f 7f

  // 80 90 a0 b0 81 91 a1 b1  88 98 a8 b8 89 99 a9 b9
  // 82 92 a2 b2 83 93 a3 b3  8a 9a aa ba 8b 9b ab bb
  // 84 94 a4 b4 85 95 a5 b5  8c 9c ac bc 8d 9d ad bd
  // 86 96 a6 b6 87 97 a7 b7  8e ae 9e be 8f 9f af bf
  // c0 d0 e0 f0 c1 d1 e1 f1  c8 d8 e8 f8 c9 d9 e9 f9
  // c2 d2 e2 f2 c3 d3 e3 f3  ca da ea fa cb db eb fb
  // c4 d4 e4 f4 c5 d5 e5 f5  cc dc ef fc cd dd ed fd
  // c6 d6 e6 f6 c7 d7 e7 f7  ce de ee fe cf df ef ff

  tr0_0 = _mm256_unpacklo_epi64(tr1_0, tr1_4);
  tr0_1 = _mm256_unpackhi_epi64(tr1_0, tr1_4);
  tr0_2 = _mm256_unpacklo_epi64(tr1_1, tr1_5);
  tr0_3 = _mm256_unpackhi_epi64(tr1_1, tr1_5);
  tr0_4 = _mm256_unpacklo_epi64(tr1_2, tr1_6);
  tr0_5 = _mm256_unpackhi_epi64(tr1_2, tr1_6);
  tr0_6 = _mm256_unpacklo_epi64(tr1_3, tr1_7);
  tr0_7 = _mm256_unpackhi_epi64(tr1_3, tr1_7);

  tr0_8 = _mm256_unpacklo_epi64(tr1_8, tr1_c);
  tr0_9 = _mm256_unpackhi_epi64(tr1_8, tr1_c);
  tr0_a = _mm256_unpacklo_epi64(tr1_9, tr1_d);
  tr0_b = _mm256_unpackhi_epi64(tr1_9, tr1_d);
  tr0_c = _mm256_unpacklo_epi64(tr1_a, tr1_e);
  tr0_d = _mm256_unpackhi_epi64(tr1_a, tr1_e);
  tr0_e = _mm256_unpacklo_epi64(tr1_b, tr1_f);
  tr0_f = _mm256_unpackhi_epi64(tr1_b, tr1_f);

  // 00 10 20 30 40 50 60 70  08 18 28 38 48 58 68 78
  // 01 11 21 31 41 51 61 71  09 19 29 39 49 59 69 79
  // 02 12 22 32 42 52 62 72  0a 1a 2a 3a 4a 5a 6a 7a
  // 03 13 23 33 43 53 63 73  0b 1b 2b 3b 4b 5b 6b 7b
  // 04 14 24 34 44 54 64 74  0c 1c 2c 3c 4c 5c 6c 7c
  // 05 15 25 35 45 55 65 75  0d 1d 2d 3d 4d 5d 6d 7d
  // 06 16 26 36 46 56 66 76  0e 1e 2e 3e 4e 5e 6e 7e
  // 07 17 27 37 47 57 67 77  0f 1f 2f 3f 4f 5f 6f 7f

  // 80 90 a0 b0 c0 d0 e0 f0  88 98 a8 b8 c8 d8 e8 f8
  // 81 91 a1 b1 c1 d1 e1 f1  89 99 a9 b9 c9 d9 e9 f9
  // 82 92 a2 b2 c2 d2 e2 f2  8a 9a aa ba ca da ea fa
  // 83 93 a3 b3 c3 d3 e3 f3  8b 9b ab bb cb db eb fb
  // 84 94 a4 b4 c4 d4 e4 f4  8c 9c ac bc cc dc ef fc
  // 85 95 a5 b5 c5 d5 e5 f5  8d 9d ad bd cd dd ed fd
  // 86 96 a6 b6 c6 d6 e6 f6  8e ae 9e be ce de ee fe
  // 87 97 a7 b7 c7 d7 e7 f7  8f 9f af bf cf df ef ff

  in[0] = _mm256_permute2x128_si256(tr0_0, tr0_8, 0x20);  // 0010 0000
  in[8] = _mm256_permute2x128_si256(tr0_0, tr0_8, 0x31);  // 0011 0001
  in[1] = _mm256_permute2x128_si256(tr0_1, tr0_9, 0x20);
  in[9] = _mm256_permute2x128_si256(tr0_1, tr0_9, 0x31);
  in[2] = _mm256_permute2x128_si256(tr0_2, tr0_a, 0x20);
  in[10] = _mm256_permute2x128_si256(tr0_2, tr0_a, 0x31);
  in[3] = _mm256_permute2x128_si256(tr0_3, tr0_b, 0x20);
  in[11] = _mm256_permute2x128_si256(tr0_3, tr0_b, 0x31);

  in[4] = _mm256_permute2x128_si256(tr0_4, tr0_c, 0x20);
  in[12] = _mm256_permute2x128_si256(tr0_4, tr0_c, 0x31);
  in[5] = _mm256_permute2x128_si256(tr0_5, tr0_d, 0x20);
  in[13] = _mm256_permute2x128_si256(tr0_5, tr0_d, 0x31);
  in[6] = _mm256_permute2x128_si256(tr0_6, tr0_e, 0x20);
  in[14] = _mm256_permute2x128_si256(tr0_6, tr0_e, 0x31);
  in[7] = _mm256_permute2x128_si256(tr0_7, tr0_f, 0x20);
  in[15] = _mm256_permute2x128_si256(tr0_7, tr0_f, 0x31);
}

static void load_buffer_16x16(const int16_t *input, int stride, int flipud,
                              int fliplr, __m256i *in) {
  if (!flipud) {
    in[0] = _mm256_loadu_si256((const __m256i *)(input + 0 * stride));
    in[1] = _mm256_loadu_si256((const __m256i *)(input + 1 * stride));
    in[2] = _mm256_loadu_si256((const __m256i *)(input + 2 * stride));
    in[3] = _mm256_loadu_si256((const __m256i *)(input + 3 * stride));
    in[4] = _mm256_loadu_si256((const __m256i *)(input + 4 * stride));
    in[5] = _mm256_loadu_si256((const __m256i *)(input + 5 * stride));
    in[6] = _mm256_loadu_si256((const __m256i *)(input + 6 * stride));
    in[7] = _mm256_loadu_si256((const __m256i *)(input + 7 * stride));
    in[8] = _mm256_loadu_si256((const __m256i *)(input + 8 * stride));
    in[9] = _mm256_loadu_si256((const __m256i *)(input + 9 * stride));
    in[10] = _mm256_loadu_si256((const __m256i *)(input + 10 * stride));
    in[11] = _mm256_loadu_si256((const __m256i *)(input + 11 * stride));
    in[12] = _mm256_loadu_si256((const __m256i *)(input + 12 * stride));
    in[13] = _mm256_loadu_si256((const __m256i *)(input + 13 * stride));
    in[14] = _mm256_loadu_si256((const __m256i *)(input + 14 * stride));
    in[15] = _mm256_loadu_si256((const __m256i *)(input + 15 * stride));
  } else {
    in[0] = _mm256_loadu_si256((const __m256i *)(input + 15 * stride));
    in[1] = _mm256_loadu_si256((const __m256i *)(input + 14 * stride));
    in[2] = _mm256_loadu_si256((const __m256i *)(input + 13 * stride));
    in[3] = _mm256_loadu_si256((const __m256i *)(input + 12 * stride));
    in[4] = _mm256_loadu_si256((const __m256i *)(input + 11 * stride));
    in[5] = _mm256_loadu_si256((const __m256i *)(input + 10 * stride));
    in[6] = _mm256_loadu_si256((const __m256i *)(input + 9 * stride));
    in[7] = _mm256_loadu_si256((const __m256i *)(input + 8 * stride));
    in[8] = _mm256_loadu_si256((const __m256i *)(input + 7 * stride));
    in[9] = _mm256_loadu_si256((const __m256i *)(input + 6 * stride));
    in[10] = _mm256_loadu_si256((const __m256i *)(input + 5 * stride));
    in[11] = _mm256_loadu_si256((const __m256i *)(input + 4 * stride));
    in[12] = _mm256_loadu_si256((const __m256i *)(input + 3 * stride));
    in[13] = _mm256_loadu_si256((const __m256i *)(input + 2 * stride));
    in[14] = _mm256_loadu_si256((const __m256i *)(input + 1 * stride));
    in[15] = _mm256_loadu_si256((const __m256i *)(input + 0 * stride));
  }

  if (fliplr) {
    mm256_reverse_epi16(&in[0]);
    mm256_reverse_epi16(&in[1]);
    mm256_reverse_epi16(&in[2]);
    mm256_reverse_epi16(&in[3]);
    mm256_reverse_epi16(&in[4]);
    mm256_reverse_epi16(&in[5]);
    mm256_reverse_epi16(&in[6]);
    mm256_reverse_epi16(&in[7]);
    mm256_reverse_epi16(&in[8]);
    mm256_reverse_epi16(&in[9]);
    mm256_reverse_epi16(&in[10]);
    mm256_reverse_epi16(&in[11]);
    mm256_reverse_epi16(&in[12]);
    mm256_reverse_epi16(&in[13]);
    mm256_reverse_epi16(&in[14]);
    mm256_reverse_epi16(&in[15]);
  }

  in[0] = _mm256_slli_epi16(in[0], 2);
  in[1] = _mm256_slli_epi16(in[1], 2);
  in[2] = _mm256_slli_epi16(in[2], 2);
  in[3] = _mm256_slli_epi16(in[3], 2);
  in[4] = _mm256_slli_epi16(in[4], 2);
  in[5] = _mm256_slli_epi16(in[5], 2);
  in[6] = _mm256_slli_epi16(in[6], 2);
  in[7] = _mm256_slli_epi16(in[7], 2);
  in[8] = _mm256_slli_epi16(in[8], 2);
  in[9] = _mm256_slli_epi16(in[9], 2);
  in[10] = _mm256_slli_epi16(in[10], 2);
  in[11] = _mm256_slli_epi16(in[11], 2);
  in[12] = _mm256_slli_epi16(in[12], 2);
  in[13] = _mm256_slli_epi16(in[13], 2);
  in[14] = _mm256_slli_epi16(in[14], 2);
  in[15] = _mm256_slli_epi16(in[15], 2);
}

static INLINE void write_buffer_16x16(const __m256i *in, int stride,
                                      tran_low_t *output) {
  _mm256_storeu_si256((__m256i *)output, in[0]);
  _mm256_storeu_si256((__m256i *)(output + stride), in[1]);
  _mm256_storeu_si256((__m256i *)(output + 2 * stride), in[2]);
  _mm256_storeu_si256((__m256i *)(output + 3 * stride), in[3]);
  _mm256_storeu_si256((__m256i *)(output + 4 * stride), in[4]);
  _mm256_storeu_si256((__m256i *)(output + 5 * stride), in[5]);
  _mm256_storeu_si256((__m256i *)(output + 6 * stride), in[6]);
  _mm256_storeu_si256((__m256i *)(output + 7 * stride), in[7]);
  _mm256_storeu_si256((__m256i *)(output + 8 * stride), in[8]);
  _mm256_storeu_si256((__m256i *)(output + 9 * stride), in[9]);
  _mm256_storeu_si256((__m256i *)(output + 10 * stride), in[10]);
  _mm256_storeu_si256((__m256i *)(output + 11 * stride), in[11]);
  _mm256_storeu_si256((__m256i *)(output + 12 * stride), in[12]);
  _mm256_storeu_si256((__m256i *)(output + 13 * stride), in[13]);
  _mm256_storeu_si256((__m256i *)(output + 14 * stride), in[14]);
  _mm256_storeu_si256((__m256i *)(output + 15 * stride), in[15]);
}

static void right_shift_16x16(__m256i *in) {
  const __m256i one = _mm256_set1_epi16(1);
  __m256i s0 = _mm256_srai_epi16(in[0], 15);
  __m256i s1 = _mm256_srai_epi16(in[1], 15);
  __m256i s2 = _mm256_srai_epi16(in[2], 15);
  __m256i s3 = _mm256_srai_epi16(in[3], 15);
  __m256i s4 = _mm256_srai_epi16(in[4], 15);
  __m256i s5 = _mm256_srai_epi16(in[5], 15);
  __m256i s6 = _mm256_srai_epi16(in[6], 15);
  __m256i s7 = _mm256_srai_epi16(in[7], 15);
  __m256i s8 = _mm256_srai_epi16(in[8], 15);
  __m256i s9 = _mm256_srai_epi16(in[9], 15);
  __m256i s10 = _mm256_srai_epi16(in[10], 15);
  __m256i s11 = _mm256_srai_epi16(in[11], 15);
  __m256i s12 = _mm256_srai_epi16(in[12], 15);
  __m256i s13 = _mm256_srai_epi16(in[13], 15);
  __m256i s14 = _mm256_srai_epi16(in[14], 15);
  __m256i s15 = _mm256_srai_epi16(in[15], 15);

  in[0] = _mm256_add_epi16(in[0], one);
  in[1] = _mm256_add_epi16(in[1], one);
  in[2] = _mm256_add_epi16(in[2], one);
  in[3] = _mm256_add_epi16(in[3], one);
  in[4] = _mm256_add_epi16(in[4], one);
  in[5] = _mm256_add_epi16(in[5], one);
  in[6] = _mm256_add_epi16(in[6], one);
  in[7] = _mm256_add_epi16(in[7], one);
  in[8] = _mm256_add_epi16(in[8], one);
  in[9] = _mm256_add_epi16(in[9], one);
  in[10] = _mm256_add_epi16(in[10], one);
  in[11] = _mm256_add_epi16(in[11], one);
  in[12] = _mm256_add_epi16(in[12], one);
  in[13] = _mm256_add_epi16(in[13], one);
  in[14] = _mm256_add_epi16(in[14], one);
  in[15] = _mm256_add_epi16(in[15], one);

  in[0] = _mm256_sub_epi16(in[0], s0);
  in[1] = _mm256_sub_epi16(in[1], s1);
  in[2] = _mm256_sub_epi16(in[2], s2);
  in[3] = _mm256_sub_epi16(in[3], s3);
  in[4] = _mm256_sub_epi16(in[4], s4);
  in[5] = _mm256_sub_epi16(in[5], s5);
  in[6] = _mm256_sub_epi16(in[6], s6);
  in[7] = _mm256_sub_epi16(in[7], s7);
  in[8] = _mm256_sub_epi16(in[8], s8);
  in[9] = _mm256_sub_epi16(in[9], s9);
  in[10] = _mm256_sub_epi16(in[10], s10);
  in[11] = _mm256_sub_epi16(in[11], s11);
  in[12] = _mm256_sub_epi16(in[12], s12);
  in[13] = _mm256_sub_epi16(in[13], s13);
  in[14] = _mm256_sub_epi16(in[14], s14);
  in[15] = _mm256_sub_epi16(in[15], s15);

  in[0] = _mm256_srai_epi16(in[0], 2);
  in[1] = _mm256_srai_epi16(in[1], 2);
  in[2] = _mm256_srai_epi16(in[2], 2);
  in[3] = _mm256_srai_epi16(in[3], 2);
  in[4] = _mm256_srai_epi16(in[4], 2);
  in[5] = _mm256_srai_epi16(in[5], 2);
  in[6] = _mm256_srai_epi16(in[6], 2);
  in[7] = _mm256_srai_epi16(in[7], 2);
  in[8] = _mm256_srai_epi16(in[8], 2);
  in[9] = _mm256_srai_epi16(in[9], 2);
  in[10] = _mm256_srai_epi16(in[10], 2);
  in[11] = _mm256_srai_epi16(in[11], 2);
  in[12] = _mm256_srai_epi16(in[12], 2);
  in[13] = _mm256_srai_epi16(in[13], 2);
  in[14] = _mm256_srai_epi16(in[14], 2);
  in[15] = _mm256_srai_epi16(in[15], 2);
}

static INLINE __m256i butter_fly(__m256i a0, __m256i a1, const __m256i cospi) {
  const __m256i dct_rounding = _mm256_set1_epi32(DCT_CONST_ROUNDING);
  __m256i y0 = _mm256_madd_epi16(a0, cospi);
  __m256i y1 = _mm256_madd_epi16(a1, cospi);

  y0 = _mm256_add_epi32(y0, dct_rounding);
  y1 = _mm256_add_epi32(y1, dct_rounding);
  y0 = _mm256_srai_epi32(y0, DCT_CONST_BITS);
  y1 = _mm256_srai_epi32(y1, DCT_CONST_BITS);

  return _mm256_packs_epi32(y0, y1);
}

static void fdct16_avx2(__m256i *in) {
  // sequence: cospi_L_H = pairs(L, H) and L first
  const __m256i cospi_p16_m16 = pair256_set_epi16(cospi_16_64, -cospi_16_64);
  const __m256i cospi_p16_p16 = pair256_set_epi16(cospi_16_64, cospi_16_64);
  const __m256i cospi_p24_p08 = pair256_set_epi16(cospi_24_64, cospi_8_64);
  const __m256i cospi_m08_p24 = pair256_set_epi16(-cospi_8_64, cospi_24_64);
  const __m256i cospi_m24_m08 = pair256_set_epi16(-cospi_24_64, -cospi_8_64);

  const __m256i cospi_p28_p04 = pair256_set_epi16(cospi_28_64, cospi_4_64);
  const __m256i cospi_m04_p28 = pair256_set_epi16(-cospi_4_64, cospi_28_64);
  const __m256i cospi_p12_p20 = pair256_set_epi16(cospi_12_64, cospi_20_64);
  const __m256i cospi_m20_p12 = pair256_set_epi16(-cospi_20_64, cospi_12_64);

  const __m256i cospi_p30_p02 = pair256_set_epi16(cospi_30_64, cospi_2_64);
  const __m256i cospi_m02_p30 = pair256_set_epi16(-cospi_2_64, cospi_30_64);

  const __m256i cospi_p14_p18 = pair256_set_epi16(cospi_14_64, cospi_18_64);
  const __m256i cospi_m18_p14 = pair256_set_epi16(-cospi_18_64, cospi_14_64);

  const __m256i cospi_p22_p10 = pair256_set_epi16(cospi_22_64, cospi_10_64);
  const __m256i cospi_m10_p22 = pair256_set_epi16(-cospi_10_64, cospi_22_64);

  const __m256i cospi_p06_p26 = pair256_set_epi16(cospi_6_64, cospi_26_64);
  const __m256i cospi_m26_p06 = pair256_set_epi16(-cospi_26_64, cospi_6_64);

  __m256i u0, u1, u2, u3, u4, u5, u6, u7;
  __m256i s0, s1, s2, s3, s4, s5, s6, s7;
  __m256i t0, t1, t2, t3, t4, t5, t6, t7;
  __m256i v0, v1, v2, v3;
  __m256i x0, x1;

  // 0, 4, 8, 12
  u0 = _mm256_add_epi16(in[0], in[15]);
  u1 = _mm256_add_epi16(in[1], in[14]);
  u2 = _mm256_add_epi16(in[2], in[13]);
  u3 = _mm256_add_epi16(in[3], in[12]);
  u4 = _mm256_add_epi16(in[4], in[11]);
  u5 = _mm256_add_epi16(in[5], in[10]);
  u6 = _mm256_add_epi16(in[6], in[9]);
  u7 = _mm256_add_epi16(in[7], in[8]);

  s0 = _mm256_add_epi16(u0, u7);
  s1 = _mm256_add_epi16(u1, u6);
  s2 = _mm256_add_epi16(u2, u5);
  s3 = _mm256_add_epi16(u3, u4);

  // 0, 8
  v0 = _mm256_add_epi16(s0, s3);
  v1 = _mm256_add_epi16(s1, s2);

  x0 = _mm256_unpacklo_epi16(v0, v1);
  x1 = _mm256_unpackhi_epi16(v0, v1);

  t0 = butter_fly(x0, x1, cospi_p16_p16);
  t1 = butter_fly(x0, x1, cospi_p16_m16);

  // 4, 12
  v0 = _mm256_sub_epi16(s1, s2);
  v1 = _mm256_sub_epi16(s0, s3);

  x0 = _mm256_unpacklo_epi16(v0, v1);
  x1 = _mm256_unpackhi_epi16(v0, v1);

  t2 = butter_fly(x0, x1, cospi_p24_p08);
  t3 = butter_fly(x0, x1, cospi_m08_p24);

  // 2, 6, 10, 14
  s0 = _mm256_sub_epi16(u3, u4);
  s1 = _mm256_sub_epi16(u2, u5);
  s2 = _mm256_sub_epi16(u1, u6);
  s3 = _mm256_sub_epi16(u0, u7);

  v0 = s0;  // output[4]
  v3 = s3;  // output[7]

  x0 = _mm256_unpacklo_epi16(s2, s1);
  x1 = _mm256_unpackhi_epi16(s2, s1);

  v2 = butter_fly(x0, x1, cospi_p16_p16);  // output[5]
  v1 = butter_fly(x0, x1, cospi_p16_m16);  // output[6]

  s0 = _mm256_add_epi16(v0, v1);  // step[4]
  s1 = _mm256_sub_epi16(v0, v1);  // step[5]
  s2 = _mm256_sub_epi16(v3, v2);  // step[6]
  s3 = _mm256_add_epi16(v3, v2);  // step[7]

  // 2, 14
  x0 = _mm256_unpacklo_epi16(s0, s3);
  x1 = _mm256_unpackhi_epi16(s0, s3);

  t4 = butter_fly(x0, x1, cospi_p28_p04);
  t5 = butter_fly(x0, x1, cospi_m04_p28);

  // 10, 6
  x0 = _mm256_unpacklo_epi16(s1, s2);
  x1 = _mm256_unpackhi_epi16(s1, s2);
  t6 = butter_fly(x0, x1, cospi_p12_p20);
  t7 = butter_fly(x0, x1, cospi_m20_p12);

  // 1, 3, 5, 7, 9, 11, 13, 15
  s0 = _mm256_sub_epi16(in[7], in[8]);  // step[8]
  s1 = _mm256_sub_epi16(in[6], in[9]);  // step[9]
  u2 = _mm256_sub_epi16(in[5], in[10]);
  u3 = _mm256_sub_epi16(in[4], in[11]);
  u4 = _mm256_sub_epi16(in[3], in[12]);
  u5 = _mm256_sub_epi16(in[2], in[13]);
  s6 = _mm256_sub_epi16(in[1], in[14]);  // step[14]
  s7 = _mm256_sub_epi16(in[0], in[15]);  // step[15]

  in[0] = t0;
  in[8] = t1;
  in[4] = t2;
  in[12] = t3;
  in[2] = t4;
  in[14] = t5;
  in[10] = t6;
  in[6] = t7;

  x0 = _mm256_unpacklo_epi16(u5, u2);
  x1 = _mm256_unpackhi_epi16(u5, u2);

  s2 = butter_fly(x0, x1, cospi_p16_p16);  // step[13]
  s5 = butter_fly(x0, x1, cospi_p16_m16);  // step[10]

  x0 = _mm256_unpacklo_epi16(u4, u3);
  x1 = _mm256_unpackhi_epi16(u4, u3);

  s3 = butter_fly(x0, x1, cospi_p16_p16);  // step[12]
  s4 = butter_fly(x0, x1, cospi_p16_m16);  // step[11]

  u0 = _mm256_add_epi16(s0, s4);  // output[8]
  u1 = _mm256_add_epi16(s1, s5);
  u2 = _mm256_sub_epi16(s1, s5);
  u3 = _mm256_sub_epi16(s0, s4);
  u4 = _mm256_sub_epi16(s7, s3);
  u5 = _mm256_sub_epi16(s6, s2);
  u6 = _mm256_add_epi16(s6, s2);
  u7 = _mm256_add_epi16(s7, s3);

  // stage 4
  s0 = u0;
  s3 = u3;
  s4 = u4;
  s7 = u7;

  x0 = _mm256_unpacklo_epi16(u1, u6);
  x1 = _mm256_unpackhi_epi16(u1, u6);

  s1 = butter_fly(x0, x1, cospi_m08_p24);
  s6 = butter_fly(x0, x1, cospi_p24_p08);

  x0 = _mm256_unpacklo_epi16(u2, u5);
  x1 = _mm256_unpackhi_epi16(u2, u5);

  s2 = butter_fly(x0, x1, cospi_m24_m08);
  s5 = butter_fly(x0, x1, cospi_m08_p24);

  // stage 5
  u0 = _mm256_add_epi16(s0, s1);
  u1 = _mm256_sub_epi16(s0, s1);
  u2 = _mm256_sub_epi16(s3, s2);
  u3 = _mm256_add_epi16(s3, s2);
  u4 = _mm256_add_epi16(s4, s5);
  u5 = _mm256_sub_epi16(s4, s5);
  u6 = _mm256_sub_epi16(s7, s6);
  u7 = _mm256_add_epi16(s7, s6);

  // stage 6
  x0 = _mm256_unpacklo_epi16(u0, u7);
  x1 = _mm256_unpackhi_epi16(u0, u7);
  in[1] = butter_fly(x0, x1, cospi_p30_p02);
  in[15] = butter_fly(x0, x1, cospi_m02_p30);

  x0 = _mm256_unpacklo_epi16(u1, u6);
  x1 = _mm256_unpackhi_epi16(u1, u6);
  in[9] = butter_fly(x0, x1, cospi_p14_p18);
  in[7] = butter_fly(x0, x1, cospi_m18_p14);

  x0 = _mm256_unpacklo_epi16(u2, u5);
  x1 = _mm256_unpackhi_epi16(u2, u5);
  in[5] = butter_fly(x0, x1, cospi_p22_p10);
  in[11] = butter_fly(x0, x1, cospi_m10_p22);

  x0 = _mm256_unpacklo_epi16(u3, u4);
  x1 = _mm256_unpackhi_epi16(u3, u4);
  in[13] = butter_fly(x0, x1, cospi_p06_p26);
  in[3] = butter_fly(x0, x1, cospi_m26_p06);

  mm256_transpose_16x16(in);
}

void fadst16_avx2(__m256i *in) {
  const __m256i cospi_p01_p31 = pair256_set_epi16(cospi_1_64, cospi_31_64);
  const __m256i cospi_p31_m01 = pair256_set_epi16(cospi_31_64, -cospi_1_64);
  const __m256i cospi_p05_p27 = pair256_set_epi16(cospi_5_64, cospi_27_64);
  const __m256i cospi_p27_m05 = pair256_set_epi16(cospi_27_64, -cospi_5_64);
  const __m256i cospi_p09_p23 = pair256_set_epi16(cospi_9_64, cospi_23_64);
  const __m256i cospi_p23_m09 = pair256_set_epi16(cospi_23_64, -cospi_9_64);
  const __m256i cospi_p13_p19 = pair256_set_epi16(cospi_13_64, cospi_19_64);
  const __m256i cospi_p19_m13 = pair256_set_epi16(cospi_19_64, -cospi_13_64);
  const __m256i cospi_p17_p15 = pair256_set_epi16(cospi_17_64, cospi_15_64);
  const __m256i cospi_p15_m17 = pair256_set_epi16(cospi_15_64, -cospi_17_64);
  const __m256i cospi_p21_p11 = pair256_set_epi16(cospi_21_64, cospi_11_64);
  const __m256i cospi_p11_m21 = pair256_set_epi16(cospi_11_64, -cospi_21_64);
  const __m256i cospi_p25_p07 = pair256_set_epi16(cospi_25_64, cospi_7_64);
  const __m256i cospi_p07_m25 = pair256_set_epi16(cospi_7_64, -cospi_25_64);
  const __m256i cospi_p29_p03 = pair256_set_epi16(cospi_29_64, cospi_3_64);
  const __m256i cospi_p03_m29 = pair256_set_epi16(cospi_3_64, -cospi_29_64);
  const __m256i cospi_p04_p28 = pair256_set_epi16(cospi_4_64, cospi_28_64);
  const __m256i cospi_p28_m04 = pair256_set_epi16(cospi_28_64, -cospi_4_64);
  const __m256i cospi_p20_p12 = pair256_set_epi16(cospi_20_64, cospi_12_64);
  const __m256i cospi_p12_m20 = pair256_set_epi16(cospi_12_64, -cospi_20_64);
  const __m256i cospi_m28_p04 = pair256_set_epi16(-cospi_28_64, cospi_4_64);
  const __m256i cospi_m12_p20 = pair256_set_epi16(-cospi_12_64, cospi_20_64);
  const __m256i cospi_p08_p24 = pair256_set_epi16(cospi_8_64, cospi_24_64);
  const __m256i cospi_p24_m08 = pair256_set_epi16(cospi_24_64, -cospi_8_64);
  const __m256i cospi_m24_p08 = pair256_set_epi16(-cospi_24_64, cospi_8_64);
  const __m256i cospi_m16_m16 = _mm256_set1_epi16((int16_t)-cospi_16_64);
  const __m256i cospi_p16_p16 = _mm256_set1_epi16((int16_t)cospi_16_64);
  const __m256i cospi_p16_m16 = pair256_set_epi16(cospi_16_64, -cospi_16_64);
  const __m256i cospi_m16_p16 = pair256_set_epi16(-cospi_16_64, cospi_16_64);
  const __m256i zero = _mm256_setzero_si256();
  const __m256i dct_rounding = _mm256_set1_epi32(DCT_CONST_ROUNDING);
  __m256i s0, s1, s2, s3, s4, s5, s6, s7, s8, s9, s10, s11, s12, s13, s14, s15;
  __m256i x0, x1, x2, x3, x4, x5, x6, x7, x8, x9, x10, x11, x12, x13, x14, x15;
  __m256i u0, u1, u2, u3, u4, u5, u6, u7, u8, u9, u10, u11, u12, u13, u14, u15;
  __m256i v0, v1, v2, v3, v4, v5, v6, v7, v8, v9, v10, v11, v12, v13, v14, v15;
  __m256i y0, y1;

  // stage 1, s takes low 256 bits; x takes high 256 bits
  y0 = _mm256_unpacklo_epi16(in[15], in[0]);
  y1 = _mm256_unpackhi_epi16(in[15], in[0]);
  s0 = _mm256_madd_epi16(y0, cospi_p01_p31);
  x0 = _mm256_madd_epi16(y1, cospi_p01_p31);
  s1 = _mm256_madd_epi16(y0, cospi_p31_m01);
  x1 = _mm256_madd_epi16(y1, cospi_p31_m01);

  y0 = _mm256_unpacklo_epi16(in[13], in[2]);
  y1 = _mm256_unpackhi_epi16(in[13], in[2]);
  s2 = _mm256_madd_epi16(y0, cospi_p05_p27);
  x2 = _mm256_madd_epi16(y1, cospi_p05_p27);
  s3 = _mm256_madd_epi16(y0, cospi_p27_m05);
  x3 = _mm256_madd_epi16(y1, cospi_p27_m05);

  y0 = _mm256_unpacklo_epi16(in[11], in[4]);
  y1 = _mm256_unpackhi_epi16(in[11], in[4]);
  s4 = _mm256_madd_epi16(y0, cospi_p09_p23);
  x4 = _mm256_madd_epi16(y1, cospi_p09_p23);
  s5 = _mm256_madd_epi16(y0, cospi_p23_m09);
  x5 = _mm256_madd_epi16(y1, cospi_p23_m09);

  y0 = _mm256_unpacklo_epi16(in[9], in[6]);
  y1 = _mm256_unpackhi_epi16(in[9], in[6]);
  s6 = _mm256_madd_epi16(y0, cospi_p13_p19);
  x6 = _mm256_madd_epi16(y1, cospi_p13_p19);
  s7 = _mm256_madd_epi16(y0, cospi_p19_m13);
  x7 = _mm256_madd_epi16(y1, cospi_p19_m13);

  y0 = _mm256_unpacklo_epi16(in[7], in[8]);
  y1 = _mm256_unpackhi_epi16(in[7], in[8]);
  s8 = _mm256_madd_epi16(y0, cospi_p17_p15);
  x8 = _mm256_madd_epi16(y1, cospi_p17_p15);
  s9 = _mm256_madd_epi16(y0, cospi_p15_m17);
  x9 = _mm256_madd_epi16(y1, cospi_p15_m17);

  y0 = _mm256_unpacklo_epi16(in[5], in[10]);
  y1 = _mm256_unpackhi_epi16(in[5], in[10]);
  s10 = _mm256_madd_epi16(y0, cospi_p21_p11);
  x10 = _mm256_madd_epi16(y1, cospi_p21_p11);
  s11 = _mm256_madd_epi16(y0, cospi_p11_m21);
  x11 = _mm256_madd_epi16(y1, cospi_p11_m21);

  y0 = _mm256_unpacklo_epi16(in[3], in[12]);
  y1 = _mm256_unpackhi_epi16(in[3], in[12]);
  s12 = _mm256_madd_epi16(y0, cospi_p25_p07);
  x12 = _mm256_madd_epi16(y1, cospi_p25_p07);
  s13 = _mm256_madd_epi16(y0, cospi_p07_m25);
  x13 = _mm256_madd_epi16(y1, cospi_p07_m25);

  y0 = _mm256_unpacklo_epi16(in[1], in[14]);
  y1 = _mm256_unpackhi_epi16(in[1], in[14]);
  s14 = _mm256_madd_epi16(y0, cospi_p29_p03);
  x14 = _mm256_madd_epi16(y1, cospi_p29_p03);
  s15 = _mm256_madd_epi16(y0, cospi_p03_m29);
  x15 = _mm256_madd_epi16(y1, cospi_p03_m29);

  // u takes low 256 bits; v takes high 256 bits
  u0 = _mm256_add_epi32(s0, s8);
  u1 = _mm256_add_epi32(s1, s9);
  u2 = _mm256_add_epi32(s2, s10);
  u3 = _mm256_add_epi32(s3, s11);
  u4 = _mm256_add_epi32(s4, s12);
  u5 = _mm256_add_epi32(s5, s13);
  u6 = _mm256_add_epi32(s6, s14);
  u7 = _mm256_add_epi32(s7, s15);

  u8 = _mm256_sub_epi32(s0, s8);
  u9 = _mm256_sub_epi32(s1, s9);
  u10 = _mm256_sub_epi32(s2, s10);
  u11 = _mm256_sub_epi32(s3, s11);
  u12 = _mm256_sub_epi32(s4, s12);
  u13 = _mm256_sub_epi32(s5, s13);
  u14 = _mm256_sub_epi32(s6, s14);
  u15 = _mm256_sub_epi32(s7, s15);

  v0 = _mm256_add_epi32(x0, x8);
  v1 = _mm256_add_epi32(x1, x9);
  v2 = _mm256_add_epi32(x2, x10);
  v3 = _mm256_add_epi32(x3, x11);
  v4 = _mm256_add_epi32(x4, x12);
  v5 = _mm256_add_epi32(x5, x13);
  v6 = _mm256_add_epi32(x6, x14);
  v7 = _mm256_add_epi32(x7, x15);

  v8 = _mm256_sub_epi32(x0, x8);
  v9 = _mm256_sub_epi32(x1, x9);
  v10 = _mm256_sub_epi32(x2, x10);
  v11 = _mm256_sub_epi32(x3, x11);
  v12 = _mm256_sub_epi32(x4, x12);
  v13 = _mm256_sub_epi32(x5, x13);
  v14 = _mm256_sub_epi32(x6, x14);
  v15 = _mm256_sub_epi32(x7, x15);

  // low 256 bits rounding
  u0 = _mm256_add_epi32(u0, dct_rounding);
  u1 = _mm256_add_epi32(u1, dct_rounding);
  u2 = _mm256_add_epi32(u2, dct_rounding);
  u3 = _mm256_add_epi32(u3, dct_rounding);
  u4 = _mm256_add_epi32(u4, dct_rounding);
  u5 = _mm256_add_epi32(u5, dct_rounding);
  u6 = _mm256_add_epi32(u6, dct_rounding);
  u7 = _mm256_add_epi32(u7, dct_rounding);

  u0 = _mm256_srai_epi32(u0, DCT_CONST_BITS);
  u1 = _mm256_srai_epi32(u1, DCT_CONST_BITS);
  u2 = _mm256_srai_epi32(u2, DCT_CONST_BITS);
  u3 = _mm256_srai_epi32(u3, DCT_CONST_BITS);
  u4 = _mm256_srai_epi32(u4, DCT_CONST_BITS);
  u5 = _mm256_srai_epi32(u5, DCT_CONST_BITS);
  u6 = _mm256_srai_epi32(u6, DCT_CONST_BITS);
  u7 = _mm256_srai_epi32(u7, DCT_CONST_BITS);

  u8 = _mm256_add_epi32(u8, dct_rounding);
  u9 = _mm256_add_epi32(u9, dct_rounding);
  u10 = _mm256_add_epi32(u10, dct_rounding);
  u11 = _mm256_add_epi32(u11, dct_rounding);
  u12 = _mm256_add_epi32(u12, dct_rounding);
  u13 = _mm256_add_epi32(u13, dct_rounding);
  u14 = _mm256_add_epi32(u14, dct_rounding);
  u15 = _mm256_add_epi32(u15, dct_rounding);

  u8 = _mm256_srai_epi32(u8, DCT_CONST_BITS);
  u9 = _mm256_srai_epi32(u9, DCT_CONST_BITS);
  u10 = _mm256_srai_epi32(u10, DCT_CONST_BITS);
  u11 = _mm256_srai_epi32(u11, DCT_CONST_BITS);
  u12 = _mm256_srai_epi32(u12, DCT_CONST_BITS);
  u13 = _mm256_srai_epi32(u13, DCT_CONST_BITS);
  u14 = _mm256_srai_epi32(u14, DCT_CONST_BITS);
  u15 = _mm256_srai_epi32(u15, DCT_CONST_BITS);

  // high 256 bits rounding
  v0 = _mm256_add_epi32(v0, dct_rounding);
  v1 = _mm256_add_epi32(v1, dct_rounding);
  v2 = _mm256_add_epi32(v2, dct_rounding);
  v3 = _mm256_add_epi32(v3, dct_rounding);
  v4 = _mm256_add_epi32(v4, dct_rounding);
  v5 = _mm256_add_epi32(v5, dct_rounding);
  v6 = _mm256_add_epi32(v6, dct_rounding);
  v7 = _mm256_add_epi32(v7, dct_rounding);

  v0 = _mm256_srai_epi32(v0, DCT_CONST_BITS);
  v1 = _mm256_srai_epi32(v1, DCT_CONST_BITS);
  v2 = _mm256_srai_epi32(v2, DCT_CONST_BITS);
  v3 = _mm256_srai_epi32(v3, DCT_CONST_BITS);
  v4 = _mm256_srai_epi32(v4, DCT_CONST_BITS);
  v5 = _mm256_srai_epi32(v5, DCT_CONST_BITS);
  v6 = _mm256_srai_epi32(v6, DCT_CONST_BITS);
  v7 = _mm256_srai_epi32(v7, DCT_CONST_BITS);

  v8 = _mm256_add_epi32(v8, dct_rounding);
  v9 = _mm256_add_epi32(v9, dct_rounding);
  v10 = _mm256_add_epi32(v10, dct_rounding);
  v11 = _mm256_add_epi32(v11, dct_rounding);
  v12 = _mm256_add_epi32(v12, dct_rounding);
  v13 = _mm256_add_epi32(v13, dct_rounding);
  v14 = _mm256_add_epi32(v14, dct_rounding);
  v15 = _mm256_add_epi32(v15, dct_rounding);

  v8 = _mm256_srai_epi32(v8, DCT_CONST_BITS);
  v9 = _mm256_srai_epi32(v9, DCT_CONST_BITS);
  v10 = _mm256_srai_epi32(v10, DCT_CONST_BITS);
  v11 = _mm256_srai_epi32(v11, DCT_CONST_BITS);
  v12 = _mm256_srai_epi32(v12, DCT_CONST_BITS);
  v13 = _mm256_srai_epi32(v13, DCT_CONST_BITS);
  v14 = _mm256_srai_epi32(v14, DCT_CONST_BITS);
  v15 = _mm256_srai_epi32(v15, DCT_CONST_BITS);

  // Saturation pack 32-bit to 16-bit
  x0 = _mm256_packs_epi32(u0, v0);
  x1 = _mm256_packs_epi32(u1, v1);
  x2 = _mm256_packs_epi32(u2, v2);
  x3 = _mm256_packs_epi32(u3, v3);
  x4 = _mm256_packs_epi32(u4, v4);
  x5 = _mm256_packs_epi32(u5, v5);
  x6 = _mm256_packs_epi32(u6, v6);
  x7 = _mm256_packs_epi32(u7, v7);
  x8 = _mm256_packs_epi32(u8, v8);
  x9 = _mm256_packs_epi32(u9, v9);
  x10 = _mm256_packs_epi32(u10, v10);
  x11 = _mm256_packs_epi32(u11, v11);
  x12 = _mm256_packs_epi32(u12, v12);
  x13 = _mm256_packs_epi32(u13, v13);
  x14 = _mm256_packs_epi32(u14, v14);
  x15 = _mm256_packs_epi32(u15, v15);

  // stage 2
  s0 = x0;
  s1 = x1;
  s2 = x2;
  s3 = x3;
  s4 = x4;
  s5 = x5;
  s6 = x6;
  s7 = x7;

  y0 = _mm256_unpacklo_epi16(x8, x9);
  y1 = _mm256_unpackhi_epi16(x8, x9);
  s8 = _mm256_madd_epi16(y0, cospi_p04_p28);
  x8 = _mm256_madd_epi16(y1, cospi_p04_p28);
  s9 = _mm256_madd_epi16(y0, cospi_p28_m04);
  x9 = _mm256_madd_epi16(y1, cospi_p28_m04);

  y0 = _mm256_unpacklo_epi16(x10, x11);
  y1 = _mm256_unpackhi_epi16(x10, x11);
  s10 = _mm256_madd_epi16(y0, cospi_p20_p12);
  x10 = _mm256_madd_epi16(y1, cospi_p20_p12);
  s11 = _mm256_madd_epi16(y0, cospi_p12_m20);
  x11 = _mm256_madd_epi16(y1, cospi_p12_m20);

  y0 = _mm256_unpacklo_epi16(x12, x13);
  y1 = _mm256_unpackhi_epi16(x12, x13);
  s12 = _mm256_madd_epi16(y0, cospi_m28_p04);
  x12 = _mm256_madd_epi16(y1, cospi_m28_p04);
  s13 = _mm256_madd_epi16(y0, cospi_p04_p28);
  x13 = _mm256_madd_epi16(y1, cospi_p04_p28);

  y0 = _mm256_unpacklo_epi16(x14, x15);
  y1 = _mm256_unpackhi_epi16(x14, x15);
  s14 = _mm256_madd_epi16(y0, cospi_m12_p20);
  x14 = _mm256_madd_epi16(y1, cospi_m12_p20);
  s15 = _mm256_madd_epi16(y0, cospi_p20_p12);
  x15 = _mm256_madd_epi16(y1, cospi_p20_p12);

  x0 = _mm256_add_epi16(s0, s4);
  x1 = _mm256_add_epi16(s1, s5);
  x2 = _mm256_add_epi16(s2, s6);
  x3 = _mm256_add_epi16(s3, s7);
  x4 = _mm256_sub_epi16(s0, s4);
  x5 = _mm256_sub_epi16(s1, s5);
  x6 = _mm256_sub_epi16(s2, s6);
  x7 = _mm256_sub_epi16(s3, s7);

  u8 = _mm256_add_epi32(s8, s12);
  u9 = _mm256_add_epi32(s9, s13);
  u10 = _mm256_add_epi32(s10, s14);
  u11 = _mm256_add_epi32(s11, s15);
  u12 = _mm256_sub_epi32(s8, s12);
  u13 = _mm256_sub_epi32(s9, s13);
  u14 = _mm256_sub_epi32(s10, s14);
  u15 = _mm256_sub_epi32(s11, s15);

  v8 = _mm256_add_epi32(x8, x12);
  v9 = _mm256_add_epi32(x9, x13);
  v10 = _mm256_add_epi32(x10, x14);
  v11 = _mm256_add_epi32(x11, x15);
  v12 = _mm256_sub_epi32(x8, x12);
  v13 = _mm256_sub_epi32(x9, x13);
  v14 = _mm256_sub_epi32(x10, x14);
  v15 = _mm256_sub_epi32(x11, x15);

  u8 = _mm256_add_epi32(u8, dct_rounding);
  u9 = _mm256_add_epi32(u9, dct_rounding);
  u10 = _mm256_add_epi32(u10, dct_rounding);
  u11 = _mm256_add_epi32(u11, dct_rounding);
  u12 = _mm256_add_epi32(u12, dct_rounding);
  u13 = _mm256_add_epi32(u13, dct_rounding);
  u14 = _mm256_add_epi32(u14, dct_rounding);
  u15 = _mm256_add_epi32(u15, dct_rounding);

  u8 = _mm256_srai_epi32(u8, DCT_CONST_BITS);
  u9 = _mm256_srai_epi32(u9, DCT_CONST_BITS);
  u10 = _mm256_srai_epi32(u10, DCT_CONST_BITS);
  u11 = _mm256_srai_epi32(u11, DCT_CONST_BITS);
  u12 = _mm256_srai_epi32(u12, DCT_CONST_BITS);
  u13 = _mm256_srai_epi32(u13, DCT_CONST_BITS);
  u14 = _mm256_srai_epi32(u14, DCT_CONST_BITS);
  u15 = _mm256_srai_epi32(u15, DCT_CONST_BITS);

  v8 = _mm256_add_epi32(v8, dct_rounding);
  v9 = _mm256_add_epi32(v9, dct_rounding);
  v10 = _mm256_add_epi32(v10, dct_rounding);
  v11 = _mm256_add_epi32(v11, dct_rounding);
  v12 = _mm256_add_epi32(v12, dct_rounding);
  v13 = _mm256_add_epi32(v13, dct_rounding);
  v14 = _mm256_add_epi32(v14, dct_rounding);
  v15 = _mm256_add_epi32(v15, dct_rounding);

  v8 = _mm256_srai_epi32(v8, DCT_CONST_BITS);
  v9 = _mm256_srai_epi32(v9, DCT_CONST_BITS);
  v10 = _mm256_srai_epi32(v10, DCT_CONST_BITS);
  v11 = _mm256_srai_epi32(v11, DCT_CONST_BITS);
  v12 = _mm256_srai_epi32(v12, DCT_CONST_BITS);
  v13 = _mm256_srai_epi32(v13, DCT_CONST_BITS);
  v14 = _mm256_srai_epi32(v14, DCT_CONST_BITS);
  v15 = _mm256_srai_epi32(v15, DCT_CONST_BITS);

  x8 = _mm256_packs_epi32(u8, v8);
  x9 = _mm256_packs_epi32(u9, v9);
  x10 = _mm256_packs_epi32(u10, v10);
  x11 = _mm256_packs_epi32(u11, v11);
  x12 = _mm256_packs_epi32(u12, v12);
  x13 = _mm256_packs_epi32(u13, v13);
  x14 = _mm256_packs_epi32(u14, v14);
  x15 = _mm256_packs_epi32(u15, v15);

  // stage 3
  s0 = x0;
  s1 = x1;
  s2 = x2;
  s3 = x3;

  y0 = _mm256_unpacklo_epi16(x4, x5);
  y1 = _mm256_unpackhi_epi16(x4, x5);
  s4 = _mm256_madd_epi16(y0, cospi_p08_p24);
  x4 = _mm256_madd_epi16(y1, cospi_p08_p24);
  s5 = _mm256_madd_epi16(y0, cospi_p24_m08);
  x5 = _mm256_madd_epi16(y1, cospi_p24_m08);

  y0 = _mm256_unpacklo_epi16(x6, x7);
  y1 = _mm256_unpackhi_epi16(x6, x7);
  s6 = _mm256_madd_epi16(y0, cospi_m24_p08);
  x6 = _mm256_madd_epi16(y1, cospi_m24_p08);
  s7 = _mm256_madd_epi16(y0, cospi_p08_p24);
  x7 = _mm256_madd_epi16(y1, cospi_p08_p24);

  s8 = x8;
  s9 = x9;
  s10 = x10;
  s11 = x11;

  y0 = _mm256_unpacklo_epi16(x12, x13);
  y1 = _mm256_unpackhi_epi16(x12, x13);
  s12 = _mm256_madd_epi16(y0, cospi_p08_p24);
  x12 = _mm256_madd_epi16(y1, cospi_p08_p24);
  s13 = _mm256_madd_epi16(y0, cospi_p24_m08);
  x13 = _mm256_madd_epi16(y1, cospi_p24_m08);

  y0 = _mm256_unpacklo_epi16(x14, x15);
  y1 = _mm256_unpackhi_epi16(x14, x15);
  s14 = _mm256_madd_epi16(y0, cospi_m24_p08);
  x14 = _mm256_madd_epi16(y1, cospi_m24_p08);
  s15 = _mm256_madd_epi16(y0, cospi_p08_p24);
  x15 = _mm256_madd_epi16(y1, cospi_p08_p24);

  in[0] = _mm256_add_epi16(s0, s2);
  x1 = _mm256_add_epi16(s1, s3);
  x2 = _mm256_sub_epi16(s0, s2);
  x3 = _mm256_sub_epi16(s1, s3);

  // Rounding on s4 + s6, s5 + s7, s4 - s6, s5 - s7
  u4 = _mm256_add_epi32(s4, s6);
  u5 = _mm256_add_epi32(s5, s7);
  u6 = _mm256_sub_epi32(s4, s6);
  u7 = _mm256_sub_epi32(s5, s7);

  v4 = _mm256_add_epi32(x4, x6);
  v5 = _mm256_add_epi32(x5, x7);
  v6 = _mm256_sub_epi32(x4, x6);
  v7 = _mm256_sub_epi32(x5, x7);

  u4 = _mm256_add_epi32(u4, dct_rounding);
  u5 = _mm256_add_epi32(u5, dct_rounding);
  u6 = _mm256_add_epi32(u6, dct_rounding);
  u7 = _mm256_add_epi32(u7, dct_rounding);

  u4 = _mm256_srai_epi32(u4, DCT_CONST_BITS);
  u5 = _mm256_srai_epi32(u5, DCT_CONST_BITS);
  u6 = _mm256_srai_epi32(u6, DCT_CONST_BITS);
  u7 = _mm256_srai_epi32(u7, DCT_CONST_BITS);

  v4 = _mm256_add_epi32(v4, dct_rounding);
  v5 = _mm256_add_epi32(v5, dct_rounding);
  v6 = _mm256_add_epi32(v6, dct_rounding);
  v7 = _mm256_add_epi32(v7, dct_rounding);

  v4 = _mm256_srai_epi32(v4, DCT_CONST_BITS);
  v5 = _mm256_srai_epi32(v5, DCT_CONST_BITS);
  v6 = _mm256_srai_epi32(v6, DCT_CONST_BITS);
  v7 = _mm256_srai_epi32(v7, DCT_CONST_BITS);

  x4 = _mm256_packs_epi32(u4, v4);
  in[12] = _mm256_packs_epi32(u5, v5);
  x6 = _mm256_packs_epi32(u6, v6);
  x7 = _mm256_packs_epi32(u7, v7);

  x8 = _mm256_add_epi16(s8, s10);
  in[14] = _mm256_add_epi16(s9, s11);
  x10 = _mm256_sub_epi16(s8, s10);
  x11 = _mm256_sub_epi16(s9, s11);

  // Rounding on s12 + s14, s13 + s15, s12 - s14, s13 - s15
  u12 = _mm256_add_epi32(s12, s14);
  u13 = _mm256_add_epi32(s13, s15);
  u14 = _mm256_sub_epi32(s12, s14);
  u15 = _mm256_sub_epi32(s13, s15);

  v12 = _mm256_add_epi32(x12, x14);
  v13 = _mm256_add_epi32(x13, x15);
  v14 = _mm256_sub_epi32(x12, x14);
  v15 = _mm256_sub_epi32(x13, x15);

  u12 = _mm256_add_epi32(u12, dct_rounding);
  u13 = _mm256_add_epi32(u13, dct_rounding);
  u14 = _mm256_add_epi32(u14, dct_rounding);
  u15 = _mm256_add_epi32(u15, dct_rounding);

  u12 = _mm256_srai_epi32(u12, DCT_CONST_BITS);
  u13 = _mm256_srai_epi32(u13, DCT_CONST_BITS);
  u14 = _mm256_srai_epi32(u14, DCT_CONST_BITS);
  u15 = _mm256_srai_epi32(u15, DCT_CONST_BITS);

  v12 = _mm256_add_epi32(v12, dct_rounding);
  v13 = _mm256_add_epi32(v13, dct_rounding);
  v14 = _mm256_add_epi32(v14, dct_rounding);
  v15 = _mm256_add_epi32(v15, dct_rounding);

  v12 = _mm256_srai_epi32(v12, DCT_CONST_BITS);
  v13 = _mm256_srai_epi32(v13, DCT_CONST_BITS);
  v14 = _mm256_srai_epi32(v14, DCT_CONST_BITS);
  v15 = _mm256_srai_epi32(v15, DCT_CONST_BITS);

  x12 = _mm256_packs_epi32(u12, v12);
  x13 = _mm256_packs_epi32(u13, v13);
  x14 = _mm256_packs_epi32(u14, v14);
  x15 = _mm256_packs_epi32(u15, v15);
  in[2] = x12;

  // stage 4
  y0 = _mm256_unpacklo_epi16(x2, x3);
  y1 = _mm256_unpackhi_epi16(x2, x3);
  s2 = _mm256_madd_epi16(y0, cospi_m16_m16);
  x2 = _mm256_madd_epi16(y1, cospi_m16_m16);
  s3 = _mm256_madd_epi16(y0, cospi_p16_m16);
  x3 = _mm256_madd_epi16(y1, cospi_p16_m16);

  y0 = _mm256_unpacklo_epi16(x6, x7);
  y1 = _mm256_unpackhi_epi16(x6, x7);
  s6 = _mm256_madd_epi16(y0, cospi_p16_p16);
  x6 = _mm256_madd_epi16(y1, cospi_p16_p16);
  s7 = _mm256_madd_epi16(y0, cospi_m16_p16);
  x7 = _mm256_madd_epi16(y1, cospi_m16_p16);

  y0 = _mm256_unpacklo_epi16(x10, x11);
  y1 = _mm256_unpackhi_epi16(x10, x11);
  s10 = _mm256_madd_epi16(y0, cospi_p16_p16);
  x10 = _mm256_madd_epi16(y1, cospi_p16_p16);
  s11 = _mm256_madd_epi16(y0, cospi_m16_p16);
  x11 = _mm256_madd_epi16(y1, cospi_m16_p16);

  y0 = _mm256_unpacklo_epi16(x14, x15);
  y1 = _mm256_unpackhi_epi16(x14, x15);
  s14 = _mm256_madd_epi16(y0, cospi_m16_m16);
  x14 = _mm256_madd_epi16(y1, cospi_m16_m16);
  s15 = _mm256_madd_epi16(y0, cospi_p16_m16);
  x15 = _mm256_madd_epi16(y1, cospi_p16_m16);

  // Rounding
  u2 = _mm256_add_epi32(s2, dct_rounding);
  u3 = _mm256_add_epi32(s3, dct_rounding);
  u6 = _mm256_add_epi32(s6, dct_rounding);
  u7 = _mm256_add_epi32(s7, dct_rounding);

  u10 = _mm256_add_epi32(s10, dct_rounding);
  u11 = _mm256_add_epi32(s11, dct_rounding);
  u14 = _mm256_add_epi32(s14, dct_rounding);
  u15 = _mm256_add_epi32(s15, dct_rounding);

  u2 = _mm256_srai_epi32(u2, DCT_CONST_BITS);
  u3 = _mm256_srai_epi32(u3, DCT_CONST_BITS);
  u6 = _mm256_srai_epi32(u6, DCT_CONST_BITS);
  u7 = _mm256_srai_epi32(u7, DCT_CONST_BITS);

  u10 = _mm256_srai_epi32(u10, DCT_CONST_BITS);
  u11 = _mm256_srai_epi32(u11, DCT_CONST_BITS);
  u14 = _mm256_srai_epi32(u14, DCT_CONST_BITS);
  u15 = _mm256_srai_epi32(u15, DCT_CONST_BITS);

  v2 = _mm256_add_epi32(x2, dct_rounding);
  v3 = _mm256_add_epi32(x3, dct_rounding);
  v6 = _mm256_add_epi32(x6, dct_rounding);
  v7 = _mm256_add_epi32(x7, dct_rounding);

  v10 = _mm256_add_epi32(x10, dct_rounding);
  v11 = _mm256_add_epi32(x11, dct_rounding);
  v14 = _mm256_add_epi32(x14, dct_rounding);
  v15 = _mm256_add_epi32(x15, dct_rounding);

  v2 = _mm256_srai_epi32(v2, DCT_CONST_BITS);
  v3 = _mm256_srai_epi32(v3, DCT_CONST_BITS);
  v6 = _mm256_srai_epi32(v6, DCT_CONST_BITS);
  v7 = _mm256_srai_epi32(v7, DCT_CONST_BITS);

  v10 = _mm256_srai_epi32(v10, DCT_CONST_BITS);
  v11 = _mm256_srai_epi32(v11, DCT_CONST_BITS);
  v14 = _mm256_srai_epi32(v14, DCT_CONST_BITS);
  v15 = _mm256_srai_epi32(v15, DCT_CONST_BITS);

  in[7] = _mm256_packs_epi32(u2, v2);
  in[8] = _mm256_packs_epi32(u3, v3);

  in[4] = _mm256_packs_epi32(u6, v6);
  in[11] = _mm256_packs_epi32(u7, v7);

  in[6] = _mm256_packs_epi32(u10, v10);
  in[9] = _mm256_packs_epi32(u11, v11);

  in[5] = _mm256_packs_epi32(u14, v14);
  in[10] = _mm256_packs_epi32(u15, v15);

  in[1] = _mm256_sub_epi16(zero, x8);
  in[3] = _mm256_sub_epi16(zero, x4);
  in[13] = _mm256_sub_epi16(zero, x13);
  in[15] = _mm256_sub_epi16(zero, x1);

  mm256_transpose_16x16(in);
}

#if CONFIG_EXT_TX
static void fidtx16_avx2(__m256i *in) {
  const __m256i zero = _mm256_setzero_si256();
  const __m256i sqrt2_epi16 = _mm256_set1_epi16((int16_t)Sqrt2);
  const __m256i dct_const_rounding = _mm256_set1_epi32(DCT_CONST_ROUNDING);
  __m256i u0, u1;
  int i = 0;

  while (i < 16) {
    in[i] = _mm256_slli_epi16(in[i], 1);

    u0 = _mm256_unpacklo_epi16(zero, in[i]);
    u1 = _mm256_unpackhi_epi16(zero, in[i]);

    u0 = _mm256_madd_epi16(u0, sqrt2_epi16);
    u1 = _mm256_madd_epi16(u1, sqrt2_epi16);

    u0 = _mm256_add_epi32(u0, dct_const_rounding);
    u1 = _mm256_add_epi32(u1, dct_const_rounding);

    u0 = _mm256_srai_epi32(u0, DCT_CONST_BITS);
    u1 = _mm256_srai_epi32(u1, DCT_CONST_BITS);
    in[i] = _mm256_packs_epi32(u0, u1);
    i++;
  }
  mm256_transpose_16x16(in);
}
#endif

void av1_fht16x16_avx2(const int16_t *input, tran_low_t *output, int stride,
                       int tx_type) {
  __m256i in[16];

  switch (tx_type) {
    case DCT_DCT:
      load_buffer_16x16(input, stride, 0, 0, in);
      fdct16_avx2(in);
      right_shift_16x16(in);
      fdct16_avx2(in);
      break;
    case ADST_DCT:
      load_buffer_16x16(input, stride, 0, 0, in);
      fadst16_avx2(in);
      right_shift_16x16(in);
      fdct16_avx2(in);
      break;
    case DCT_ADST:
      load_buffer_16x16(input, stride, 0, 0, in);
      fdct16_avx2(in);
      right_shift_16x16(in);
      fadst16_avx2(in);
      break;
    case ADST_ADST:
      load_buffer_16x16(input, stride, 0, 0, in);
      fadst16_avx2(in);
      right_shift_16x16(in);
      fadst16_avx2(in);
      break;
#if CONFIG_EXT_TX
    case FLIPADST_DCT:
      load_buffer_16x16(input, stride, 1, 0, in);
      fadst16_avx2(in);
      right_shift_16x16(in);
      fdct16_avx2(in);
      break;
    case DCT_FLIPADST:
      load_buffer_16x16(input, stride, 0, 1, in);
      fdct16_avx2(in);
      right_shift_16x16(in);
      fadst16_avx2(in);
      break;
    case FLIPADST_FLIPADST:
      load_buffer_16x16(input, stride, 1, 1, in);
      fadst16_avx2(in);
      right_shift_16x16(in);
      fadst16_avx2(in);
      break;
    case ADST_FLIPADST:
      load_buffer_16x16(input, stride, 0, 1, in);
      fadst16_avx2(in);
      right_shift_16x16(in);
      fadst16_avx2(in);
      break;
    case FLIPADST_ADST:
      load_buffer_16x16(input, stride, 1, 0, in);
      fadst16_avx2(in);
      right_shift_16x16(in);
      fadst16_avx2(in);
      break;
    case V_DCT:
      load_buffer_16x16(input, stride, 0, 0, in);
      fdct16_avx2(in);
      right_shift_16x16(in);
      fidtx16_avx2(in);
      break;
    case H_DCT:
      load_buffer_16x16(input, stride, 0, 0, in);
      fidtx16_avx2(in);
      right_shift_16x16(in);
      fdct16_avx2(in);
      break;
    case V_ADST:
      load_buffer_16x16(input, stride, 0, 0, in);
      fadst16_avx2(in);
      right_shift_16x16(in);
      fidtx16_avx2(in);
      break;
    case H_ADST:
      load_buffer_16x16(input, stride, 0, 0, in);
      fidtx16_avx2(in);
      right_shift_16x16(in);
      fadst16_avx2(in);
      break;
    case V_FLIPADST:
      load_buffer_16x16(input, stride, 1, 0, in);
      fadst16_avx2(in);
      right_shift_16x16(in);
      fidtx16_avx2(in);
      break;
    case H_FLIPADST:
      load_buffer_16x16(input, stride, 0, 1, in);
      fidtx16_avx2(in);
      right_shift_16x16(in);
      fadst16_avx2(in);
      break;
#endif  // CONFIG_EXT_TX
    default: assert(0); break;
  }
  write_buffer_16x16(in, 16, output);
}
