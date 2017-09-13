/*
 *  Copyright (c) 2017 The WebM project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "./vp8_rtcd.h"
#include "vpx_ports/mem.h"
#include "vpx_ports/asmdefs_mmi.h"

/* clang-format off */
#define TRANSPOSE_4H                                         \
  MMI_LI(%[tmp0], 0x93)                                      \
  "xor        %[ftmp0],   %[ftmp0],   %[ftmp0]         \n\t" \
  "mtc1       %[tmp0],    %[ftmp10]                    \n\t" \
  "punpcklhw  %[ftmp5],   %[ftmp1],   %[ftmp0]         \n\t" \
  "punpcklhw  %[ftmp9],   %[ftmp2],   %[ftmp0]         \n\t" \
  "pshufh     %[ftmp9],   %[ftmp9],   %[ftmp10]        \n\t" \
  "or         %[ftmp5],   %[ftmp5],   %[ftmp9]         \n\t" \
  "punpckhhw  %[ftmp6],   %[ftmp1],   %[ftmp0]         \n\t" \
  "punpckhhw  %[ftmp9],   %[ftmp2],   %[ftmp0]         \n\t" \
  "pshufh     %[ftmp9],   %[ftmp9],   %[ftmp10]        \n\t" \
  "or         %[ftmp6],   %[ftmp6],   %[ftmp9]         \n\t" \
  "punpcklhw  %[ftmp7],   %[ftmp3],   %[ftmp0]         \n\t" \
  "punpcklhw  %[ftmp9],   %[ftmp4],   %[ftmp0]         \n\t" \
  "pshufh     %[ftmp9],   %[ftmp9],   %[ftmp10]        \n\t" \
  "or         %[ftmp7],   %[ftmp7],   %[ftmp9]         \n\t" \
  "punpckhhw  %[ftmp8],   %[ftmp3],   %[ftmp0]         \n\t" \
  "punpckhhw  %[ftmp9],   %[ftmp4],   %[ftmp0]         \n\t" \
  "pshufh     %[ftmp9],   %[ftmp9],   %[ftmp10]        \n\t" \
  "or         %[ftmp8],   %[ftmp8],   %[ftmp9]         \n\t" \
  "punpcklwd  %[ftmp1],   %[ftmp5],   %[ftmp7]         \n\t" \
  "punpckhwd  %[ftmp2],   %[ftmp5],   %[ftmp7]         \n\t" \
  "punpcklwd  %[ftmp3],   %[ftmp6],   %[ftmp8]         \n\t" \
  "punpckhwd  %[ftmp4],   %[ftmp6],   %[ftmp8]         \n\t"
/* clang-format on */

void vp8_short_fdct4x4_mmi(short *input, short *output, int pitch) {
  int pitch_half = pitch / 2;
  uint64_t tmp[1];

#if _MIPS_SIM == _ABIO32
  register double ftmp0 asm("$f0");
  register double ftmp1 asm("$f2");
  register double ftmp2 asm("$f4");
  register double ftmp3 asm("$f6");
  register double ftmp4 asm("$f8");
  register double ftmp5 asm("$f10");
  register double ftmp6 asm("$f12");
  register double ftmp7 asm("$f14");
  register double ftmp8 asm("$f16");
  register double ftmp9 asm("$f18");
  register double ftmp10 asm("$f20");
  register double ftmp11 asm("$f22");
  register double ftmp12 asm("$f24");
#else
  register double ftmp0 asm("$f0");
  register double ftmp1 asm("$f1");
  register double ftmp2 asm("$f2");
  register double ftmp3 asm("$f3");
  register double ftmp4 asm("$f4");
  register double ftmp5 asm("$f5");
  register double ftmp6 asm("$f6");
  register double ftmp7 asm("$f7");
  register double ftmp8 asm("$f8");
  register double ftmp9 asm("$f9");
  register double ftmp10 asm("$f10");
  register double ftmp11 asm("$f11");
  register double ftmp12 asm("$f12");
#endif  // _MIPS_SIM == _ABIO32

  DECLARE_ALIGNED(8, const uint64_t, ff_ph_01) = { 0x0001000100010001ULL };
  DECLARE_ALIGNED(8, const uint64_t, ff_ph_07) = { 0x0007000700070007ULL };
  DECLARE_ALIGNED(8, const uint64_t, ff_pw_12000) = { 0x00002ee000002ee0ULL };
  DECLARE_ALIGNED(8, const uint64_t, ff_pw_51000) = { 0x0000c7380000c738ULL };
  DECLARE_ALIGNED(8, const uint64_t, ff_pw_14500) = { 0x000038a4000038a4ULL };
  DECLARE_ALIGNED(8, const uint64_t, ff_pw_7500) = { 0x00001d4c00001d4cULL };
  DECLARE_ALIGNED(8, const uint64_t, ff_ph_op1) = { 0x14e808a914e808a9ULL };
  DECLARE_ALIGNED(8, const uint64_t, ff_ph_op3) = { 0xeb1808a9eb1808a9ULL };

  DECLARE_ALIGNED(16, int, a[4]);
  DECLARE_ALIGNED(16, int, b[4]);
  DECLARE_ALIGNED(16, int, c[4]);
  DECLARE_ALIGNED(16, int, d[4]);

  // stage1
  a[0] = (input[0] + input[3]) * 8;
  a[1] = (input[0 + pitch_half] + input[3 + pitch_half]) * 8;
  a[2] = (input[0 + 2 * pitch_half] + input[3 + 2 * pitch_half]) * 8;
  a[3] = (input[0 + 3 * pitch_half] + input[3 + 3 * pitch_half]) * 8;

  b[0] = (input[1] + input[2]) * 8;
  b[1] = (input[1 + pitch_half] + input[2 + pitch_half]) * 8;
  b[2] = (input[1 + 2 * pitch_half] + input[2 + 2 * pitch_half]) * 8;
  b[3] = (input[1 + 3 * pitch_half] + input[2 + 3 * pitch_half]) * 8;

  c[0] = (input[1] - input[2]) * 8;
  c[1] = (input[1 + pitch_half] - input[2 + pitch_half]) * 8;
  c[2] = (input[1 + 2 * pitch_half] - input[2 + 2 * pitch_half]) * 8;
  c[3] = (input[1 + 3 * pitch_half] - input[2 + 3 * pitch_half]) * 8;

  d[0] = (input[0] - input[3]) * 8;
  d[1] = (input[0 + pitch_half] - input[3 + pitch_half]) * 8;
  d[2] = (input[0 + 2 * pitch_half] - input[3 + 2 * pitch_half]) * 8;
  d[3] = (input[0 + 3 * pitch_half] - input[3 + 3 * pitch_half]) * 8;

  __asm__ volatile (
    "gslqc1     %[ftmp2],   %[ftmp1],       0x00(%[a])      \n\t"
    "gslqc1     %[ftmp4],   %[ftmp3],       0x00(%[b])      \n\t"
    "gslqc1     %[ftmp6],   %[ftmp5],       0x00(%[c])      \n\t"
    "gslqc1     %[ftmp8],   %[ftmp7],       0x00(%[d])      \n\t"

    "paddw      %[ftmp9],   %[ftmp1],       %[ftmp3]        \n\t"
    "paddw      %[ftmp10],  %[ftmp2],       %[ftmp4]        \n\t"
    "psubw      %[ftmp11],  %[ftmp1],       %[ftmp3]        \n\t"
    "psubw      %[ftmp12],  %[ftmp2],       %[ftmp4]        \n\t"
    "packsswh   %[ftmp1],   %[ftmp9],       %[ftmp10]       \n\t"
    "packsswh   %[ftmp3],   %[ftmp11],      %[ftmp12]       \n\t"
    "packsswh   %[ftmp2],   %[ftmp5],       %[ftmp6]        \n\t"
    "packsswh   %[ftmp4],   %[ftmp7],       %[ftmp8]        \n\t"
    MMI_LI(%[tmp0], 0x0c)
    "mov.d      %[ftmp7],   %[ftmp2]                        \n\t"
    "mov.d      %[ftmp8],   %[ftmp4]                        \n\t"
    "mtc1       %[tmp0],    %[ftmp11]                       \n\t"

    "ldc1       %[ftmp12],  %[ff_pw_14500]                  \n\t"
    "punpcklhw  %[ftmp9],   %[ftmp7],       %[ftmp8]        \n\t"
    "pmaddhw    %[ftmp5],   %[ftmp9],       %[ff_ph_op1]    \n\t"
    "punpckhhw  %[ftmp9],   %[ftmp7],       %[ftmp8]        \n\t"
    "pmaddhw    %[ftmp6],   %[ftmp9],       %[ff_ph_op1]    \n\t"
    "paddw      %[ftmp5],   %[ftmp5],       %[ftmp12]       \n\t"
    "paddw      %[ftmp6],   %[ftmp6],       %[ftmp12]       \n\t"
    "psraw      %[ftmp5],   %[ftmp5],       %[ftmp11]       \n\t"
    "psraw      %[ftmp6],   %[ftmp6],       %[ftmp11]       \n\t"
    "packsswh   %[ftmp2],   %[ftmp5],       %[ftmp6]        \n\t"

    "ldc1       %[ftmp12],  %[ff_pw_7500]                   \n\t"
    "punpcklhw  %[ftmp9],   %[ftmp8],       %[ftmp7]        \n\t"
    "pmaddhw    %[ftmp5],   %[ftmp9],       %[ff_ph_op3]    \n\t"
    "punpckhhw  %[ftmp9],   %[ftmp8],       %[ftmp7]        \n\t"
    "pmaddhw    %[ftmp6],   %[ftmp9],       %[ff_ph_op3]    \n\t"
    "paddw      %[ftmp5],   %[ftmp5],       %[ftmp12]       \n\t"
    "paddw      %[ftmp6],   %[ftmp6],       %[ftmp12]       \n\t"
    "psraw      %[ftmp5],   %[ftmp5],       %[ftmp11]       \n\t"
    "psraw      %[ftmp6],   %[ftmp6],       %[ftmp11]       \n\t"
    "packsswh   %[ftmp4],   %[ftmp5],       %[ftmp6]        \n\t"
    TRANSPOSE_4H

    "xor        %[ftmp0],   %[ftmp0],       %[ftmp0]        \n\t"
    "paddh      %[ftmp5],   %[ftmp1],       %[ftmp4]        \n\t"
    "paddh      %[ftmp6],   %[ftmp2],       %[ftmp3]        \n\t"
    "psubh      %[ftmp7],   %[ftmp2],       %[ftmp3]        \n\t"
    "psubh      %[ftmp8],   %[ftmp1],       %[ftmp4]        \n\t"

    "pcmpeqh    %[ftmp0],   %[ftmp8],       %[ftmp0]        \n\t"
    "ldc1       %[ftmp9],   %[ff_ph_01]                     \n\t"
    "paddh      %[ftmp0],   %[ftmp0],       %[ftmp9]        \n\t"

    "paddh      %[ftmp1],   %[ftmp5],       %[ftmp6]        \n\t"
    "psubh      %[ftmp2],   %[ftmp5],       %[ftmp6]        \n\t"
    "ldc1       %[ftmp9],   %[ff_ph_07]                     \n\t"
    MMI_LI(%[tmp0], 0x04)
    "paddh      %[ftmp1],   %[ftmp1],       %[ftmp9]        \n\t"
    "paddh      %[ftmp2],   %[ftmp2],       %[ftmp9]        \n\t"
    "mtc1       %[tmp0],    %[ftmp9]                        \n\t"
    "psrah      %[ftmp1],   %[ftmp1],       %[ftmp9]        \n\t"
    "psrah      %[ftmp2],   %[ftmp2],       %[ftmp9]        \n\t"

    MMI_LI(%[tmp0], 0x10)
    "ldc1       %[ftmp12],  %[ff_pw_12000]                  \n\t"
    "mtc1       %[tmp0],    %[ftmp9]                        \n\t"

    "punpcklhw  %[ftmp5],   %[ftmp7],       %[ftmp8]        \n\t"
    "pmaddhw    %[ftmp10],  %[ftmp5],       %[ff_ph_op1]    \n\t"
    "punpckhhw  %[ftmp5],   %[ftmp7],       %[ftmp8]        \n\t"
    "pmaddhw    %[ftmp11],  %[ftmp5],       %[ff_ph_op1]    \n\t"
    "paddw      %[ftmp10],  %[ftmp10],      %[ftmp12]       \n\t"
    "paddw      %[ftmp11],  %[ftmp11],      %[ftmp12]       \n\t"
    "psraw      %[ftmp10],  %[ftmp10],      %[ftmp9]        \n\t"
    "psraw      %[ftmp11],  %[ftmp11],      %[ftmp9]        \n\t"
    "packsswh   %[ftmp3],   %[ftmp10],      %[ftmp11]       \n\t"
    "paddh      %[ftmp3],   %[ftmp3],       %[ftmp0]        \n\t"

    "ldc1       %[ftmp12],  %[ff_pw_51000]                  \n\t"
    "punpcklhw  %[ftmp5],   %[ftmp8],       %[ftmp7]        \n\t"
    "pmaddhw    %[ftmp10],  %[ftmp5],       %[ff_ph_op3]    \n\t"
    "punpckhhw  %[ftmp5],   %[ftmp8],       %[ftmp7]        \n\t"
    "pmaddhw    %[ftmp11],  %[ftmp5],       %[ff_ph_op3]    \n\t"
    "paddw      %[ftmp10],  %[ftmp10],      %[ftmp12]       \n\t"
    "paddw      %[ftmp11],  %[ftmp11],      %[ftmp12]       \n\t"
    "psraw      %[ftmp10],  %[ftmp10],      %[ftmp9]        \n\t"
    "psraw      %[ftmp11],  %[ftmp11],      %[ftmp9]        \n\t"
    "packsswh   %[ftmp4],   %[ftmp10],      %[ftmp11]       \n\t"

    : [ftmp0] "=&f"(ftmp0), [ftmp1] "=&f"(ftmp1), [ftmp2] "=&f"(ftmp2),
      [ftmp3] "=&f"(ftmp3), [ftmp4] "=&f"(ftmp4), [ftmp5] "=&f"(ftmp5),
      [ftmp6] "=&f"(ftmp6), [ftmp7] "=&f"(ftmp7), [ftmp8] "=&f"(ftmp8),
      [ftmp9] "=&f"(ftmp9), [ftmp10] "=&f"(ftmp10), [ftmp11] "=&f"(ftmp11),
      [ftmp12] "=&f"(ftmp12), [tmp0] "=&r"(tmp[0])
    : [ff_ph_01] "m"(ff_ph_01), [ff_ph_07] "m"(ff_ph_07), [a] "r"(a),
      [b] "r"(b), [c] "r"(c), [d] "r"(d), [ff_ph_op1] "f"(ff_ph_op1),
      [ff_ph_op3] "f"(ff_ph_op3), [ff_pw_14500] "m"(ff_pw_14500),
      [ff_pw_7500] "m"(ff_pw_7500), [ff_pw_12000] "m"(ff_pw_12000),
      [ff_pw_51000] "m"(ff_pw_51000)
  );

  __asm__ volatile(
      "gssdlc1    %[ftmp1],   0x07(%[output])                 \n\t"
      "gssdrc1    %[ftmp1],   0x00(%[output])                 \n\t"
      "gssdlc1    %[ftmp3],   0x0f(%[output])                 \n\t"
      "gssdrc1    %[ftmp3],   0x08(%[output])                 \n\t"
      "gssdlc1    %[ftmp2],   0x17(%[output])                 \n\t"
      "gssdrc1    %[ftmp2],   0x10(%[output])                 \n\t"
      "gssdlc1    %[ftmp4],   0x1f(%[output])                 \n\t"
      "gssdrc1    %[ftmp4],   0x18(%[output])                 \n\t"
      :
      : [ftmp1] "f"(ftmp1), [ftmp2] "f"(ftmp2), [ftmp3] "f"(ftmp3),
        [ftmp4] "f"(ftmp4), [output] "r"(output)
      : "memory");
}

void vp8_short_fdct8x4_mmi(short *input, short *output, int pitch) {
  vp8_short_fdct4x4_mmi(input, output, pitch);
  vp8_short_fdct4x4_mmi(input + 4, output + 16, pitch);
}

void vp8_short_walsh4x4_mmi(short *input, short *output, int pitch) {
  double ftmp[13];
  uint32_t tmp[1];
  DECLARE_ALIGNED(8, const uint64_t, ff_ph_01) = { 0x0001000100010001ULL };
  DECLARE_ALIGNED(8, const uint64_t, ff_pw_01) = { 0x0000000100000001ULL };
  DECLARE_ALIGNED(8, const uint64_t, ff_pw_03) = { 0x0000000300000003ULL };
  DECLARE_ALIGNED(8, const uint64_t, ff_pw_mask) = { 0x0001000000010000ULL };

  __asm__ volatile (
    MMI_LI(%[tmp0], 0x02)
    "xor        %[ftmp0],   %[ftmp0],       %[ftmp0]            \n\t"
    "mtc1       %[tmp0],    %[ftmp11]                           \n\t"

    "gsldlc1    %[ftmp1],   0x07(%[ip])                         \n\t"
    "gsldrc1    %[ftmp1],   0x00(%[ip])                         \n\t"
    MMI_ADDU(%[ip], %[ip], %[pitch])
    "gsldlc1    %[ftmp2],   0x07(%[ip])                         \n\t"
    "gsldrc1    %[ftmp2],   0x00(%[ip])                         \n\t"
    MMI_ADDU(%[ip], %[ip], %[pitch])
    "gsldlc1    %[ftmp3],   0x07(%[ip])                         \n\t"
    "gsldrc1    %[ftmp3],   0x00(%[ip])                         \n\t"
    MMI_ADDU(%[ip], %[ip], %[pitch])
    "gsldlc1    %[ftmp4],   0x07(%[ip])                         \n\t"
    "gsldrc1    %[ftmp4],   0x00(%[ip])                         \n\t"
    TRANSPOSE_4H

    "psllh      %[ftmp1],   %[ftmp1],       %[ftmp11]           \n\t"
    "psllh      %[ftmp2],   %[ftmp2],       %[ftmp11]           \n\t"
    "psllh      %[ftmp3],   %[ftmp3],       %[ftmp11]           \n\t"
    "psllh      %[ftmp4],   %[ftmp4],       %[ftmp11]           \n\t"
    // a
    "paddh      %[ftmp5],   %[ftmp1],       %[ftmp3]            \n\t"
    // d
    "paddh      %[ftmp6],   %[ftmp2],       %[ftmp4]            \n\t"
    // c
    "psubh      %[ftmp7],   %[ftmp2],       %[ftmp4]            \n\t"
    // b
    "psubh      %[ftmp8],   %[ftmp1],       %[ftmp3]            \n\t"

    // a + d
    "paddh      %[ftmp1],   %[ftmp5],       %[ftmp6]            \n\t"
    // b + c
    "paddh      %[ftmp2],   %[ftmp8],       %[ftmp7]            \n\t"
    // b - c
    "psubh      %[ftmp3],   %[ftmp8],       %[ftmp7]            \n\t"
    // a - d
    "psubh      %[ftmp4],   %[ftmp5],       %[ftmp6]            \n\t"

    "pcmpeqh    %[ftmp6],   %[ftmp5],       %[ftmp0]            \n\t"
    "paddh      %[ftmp6],   %[ftmp6],       %[ff_ph_01]           \n\t"
    "paddh      %[ftmp1],   %[ftmp1],       %[ftmp6]            \n\t"
    TRANSPOSE_4H

    // op[2], op[0]
    "pmaddhw    %[ftmp5],   %[ftmp1],       %[ff_pw_01]         \n\t"
    // op[3], op[1]
    "pmaddhw    %[ftmp1],   %[ftmp1],       %[ff_pw_mask]       \n\t"

    // op[6], op[4]
    "pmaddhw    %[ftmp6],   %[ftmp2],       %[ff_pw_01]         \n\t"
    // op[7], op[5]
    "pmaddhw    %[ftmp2],   %[ftmp2],       %[ff_pw_mask]       \n\t"

    // op[10], op[8]
    "pmaddhw    %[ftmp7],   %[ftmp3],       %[ff_pw_01]         \n\t"
    // op[11], op[9]
    "pmaddhw    %[ftmp3],   %[ftmp3],       %[ff_pw_mask]       \n\t"

    // op[14], op[12]
    "pmaddhw    %[ftmp8],   %[ftmp4],       %[ff_pw_01]         \n\t"
    // op[15], op[13]
    "pmaddhw    %[ftmp4],   %[ftmp4],       %[ff_pw_mask]       \n\t"

    // a1, a3
    "paddw      %[ftmp9],   %[ftmp5],       %[ftmp7]            \n\t"
    // d1, d3
    "paddw      %[ftmp10],  %[ftmp6],       %[ftmp8]            \n\t"
    // c1, c3
    "psubw      %[ftmp11],  %[ftmp6],       %[ftmp8]            \n\t"
    // b1, b3
    "psubw      %[ftmp12],  %[ftmp5],       %[ftmp7]            \n\t"

    // a1 + d1, a3 + d3
    "paddw      %[ftmp5],   %[ftmp9],       %[ftmp10]           \n\t"
    // b1 + c1, b3 + c3
    "paddw      %[ftmp6],   %[ftmp12],      %[ftmp11]           \n\t"
    // b1 - c1, b3 - c3
    "psubw      %[ftmp7],   %[ftmp12],      %[ftmp11]           \n\t"
    // a1 - d1, a3 - d3
    "psubw      %[ftmp8],   %[ftmp9],       %[ftmp10]           \n\t"

    // a2, a4
    "paddw      %[ftmp9],   %[ftmp1],       %[ftmp3]            \n\t"
    // d2, d4
    "paddw      %[ftmp10],  %[ftmp2],       %[ftmp4]            \n\t"
    // c2, c4
    "psubw      %[ftmp11],  %[ftmp2],       %[ftmp4]            \n\t"
    // b2, b4
    "psubw      %[ftmp12],  %[ftmp1],       %[ftmp3]            \n\t"

    // a2 + d2, a4 + d4
    "paddw      %[ftmp1],   %[ftmp9],       %[ftmp10]           \n\t"
    // b2 + c2, b4 + c4
    "paddw      %[ftmp2],   %[ftmp12],      %[ftmp11]           \n\t"
    // b2 - c2, b4 - c4
    "psubw      %[ftmp3],   %[ftmp12],      %[ftmp11]           \n\t"
    // a2 - d2, a4 - d4
    "psubw      %[ftmp4],   %[ftmp9],       %[ftmp10]           \n\t"

    MMI_LI(%[tmp0], 0x03)
    "mtc1       %[tmp0],    %[ftmp11]                           \n\t"

    "pcmpgtw    %[ftmp9],   %[ftmp0],       %[ftmp1]            \n\t"
    "and        %[ftmp9],   %[ftmp9],       %[ff_pw_01]         \n\t"
    "paddw      %[ftmp1],   %[ftmp1],       %[ftmp9]            \n\t"
    "paddw      %[ftmp1],   %[ftmp1],       %[ff_pw_03]         \n\t"
    "psraw      %[ftmp1],   %[ftmp1],       %[ftmp11]           \n\t"

    "pcmpgtw    %[ftmp9],   %[ftmp0],       %[ftmp2]            \n\t"
    "and        %[ftmp9],   %[ftmp9],       %[ff_pw_01]         \n\t"
    "paddw      %[ftmp2],   %[ftmp2],       %[ftmp9]            \n\t"
    "paddw      %[ftmp2],   %[ftmp2],       %[ff_pw_03]         \n\t"
    "psraw      %[ftmp2],   %[ftmp2],       %[ftmp11]           \n\t"

    "pcmpgtw    %[ftmp9],   %[ftmp0],       %[ftmp3]            \n\t"
    "and        %[ftmp9],   %[ftmp9],       %[ff_pw_01]         \n\t"
    "paddw      %[ftmp3],   %[ftmp3],       %[ftmp9]            \n\t"
    "paddw      %[ftmp3],   %[ftmp3],       %[ff_pw_03]         \n\t"
    "psraw      %[ftmp3],   %[ftmp3],       %[ftmp11]           \n\t"

    "pcmpgtw    %[ftmp9],   %[ftmp0],       %[ftmp4]            \n\t"
    "and        %[ftmp9],   %[ftmp9],       %[ff_pw_01]         \n\t"
    "paddw      %[ftmp4],   %[ftmp4],       %[ftmp9]            \n\t"
    "paddw      %[ftmp4],   %[ftmp4],       %[ff_pw_03]         \n\t"
    "psraw      %[ftmp4],   %[ftmp4],       %[ftmp11]           \n\t"

    "pcmpgtw    %[ftmp9],   %[ftmp0],       %[ftmp5]            \n\t"
    "and        %[ftmp9],   %[ftmp9],       %[ff_pw_01]         \n\t"
    "paddw      %[ftmp5],   %[ftmp5],       %[ftmp9]            \n\t"
    "paddw      %[ftmp5],   %[ftmp5],       %[ff_pw_03]         \n\t"
    "psraw      %[ftmp5],   %[ftmp5],       %[ftmp11]           \n\t"

    "pcmpgtw    %[ftmp9],   %[ftmp0],       %[ftmp6]            \n\t"
    "and        %[ftmp9],   %[ftmp9],       %[ff_pw_01]         \n\t"
    "paddw      %[ftmp6],   %[ftmp6],       %[ftmp9]            \n\t"
    "paddw      %[ftmp6],   %[ftmp6],       %[ff_pw_03]         \n\t"
    "psraw      %[ftmp6],   %[ftmp6],       %[ftmp11]           \n\t"

    "pcmpgtw    %[ftmp9],   %[ftmp0],       %[ftmp7]            \n\t"
    "and        %[ftmp9],   %[ftmp9],       %[ff_pw_01]         \n\t"
    "paddw      %[ftmp7],   %[ftmp7],       %[ftmp9]            \n\t"
    "paddw      %[ftmp7],   %[ftmp7],       %[ff_pw_03]         \n\t"
    "psraw      %[ftmp7],   %[ftmp7],       %[ftmp11]           \n\t"

    "pcmpgtw    %[ftmp9],   %[ftmp0],       %[ftmp8]            \n\t"
    "and        %[ftmp9],   %[ftmp9],       %[ff_pw_01]         \n\t"
    "paddw      %[ftmp8],   %[ftmp8],       %[ftmp9]            \n\t"
    "paddw      %[ftmp8],   %[ftmp8],       %[ff_pw_03]         \n\t"
    "psraw      %[ftmp8],   %[ftmp8],       %[ftmp11]           \n\t"

    "packsswh   %[ftmp1],   %[ftmp1],       %[ftmp5]            \n\t"
    "packsswh   %[ftmp2],   %[ftmp2],       %[ftmp6]            \n\t"
    "packsswh   %[ftmp3],   %[ftmp3],       %[ftmp7]            \n\t"
    "packsswh   %[ftmp4],   %[ftmp4],       %[ftmp8]            \n\t"

    MMI_LI(%[tmp0], 0x72)
    "mtc1       %[tmp0],    %[ftmp11]                           \n\t"
    "pshufh     %[ftmp1],   %[ftmp1],       %[ftmp11]           \n\t"
    "pshufh     %[ftmp2],   %[ftmp2],       %[ftmp11]           \n\t"
    "pshufh     %[ftmp3],   %[ftmp3],       %[ftmp11]           \n\t"
    "pshufh     %[ftmp4],   %[ftmp4],       %[ftmp11]           \n\t"

    "gssdlc1    %[ftmp1],   0x07(%[op])                         \n\t"
    "gssdrc1    %[ftmp1],   0x00(%[op])                         \n\t"
    "gssdlc1    %[ftmp2],   0x0f(%[op])                         \n\t"
    "gssdrc1    %[ftmp2],   0x08(%[op])                         \n\t"
    "gssdlc1    %[ftmp3],   0x17(%[op])                         \n\t"
    "gssdrc1    %[ftmp3],   0x10(%[op])                         \n\t"
    "gssdlc1    %[ftmp4],   0x1f(%[op])                         \n\t"
    "gssdrc1    %[ftmp4],   0x18(%[op])                         \n\t"
    : [ftmp0]"=&f"(ftmp[0]),            [ftmp1]"=&f"(ftmp[1]),
      [ftmp2]"=&f"(ftmp[2]),            [ftmp3]"=&f"(ftmp[3]),
      [ftmp4]"=&f"(ftmp[4]),            [ftmp5]"=&f"(ftmp[5]),
      [ftmp6]"=&f"(ftmp[6]),            [ftmp7]"=&f"(ftmp[7]),
      [ftmp8]"=&f"(ftmp[8]),            [ftmp9]"=&f"(ftmp[9]),
      [ftmp10]"=&f"(ftmp[10]),          [ftmp11]"=&f"(ftmp[11]),
      [ftmp12]"=&f"(ftmp[12]),
      [tmp0]"=&r"(tmp[0]),
      [ip]"+&r"(input)
    : [op]"r"(output),
      [ff_pw_01]"f"(ff_pw_01),          [pitch]"r"((mips_reg)pitch),
      [ff_pw_03]"f"(ff_pw_03),          [ff_pw_mask]"f"(ff_pw_mask),
      [ff_ph_01]"f"(ff_ph_01)
    : "memory"
  );
}
