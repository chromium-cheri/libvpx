;
;  Copyright (c) 2013 The WebM project authors. All Rights Reserved.
;
;  Use of this source code is governed by a BSD-style license
;  that can be found in the LICENSE file in the root of the source
;  tree. An additional intellectual property rights grant can be found
;  in the file PATENTS.  All contributing project authors may
;  be found in the AUTHORS file in the root of the source tree.
;

    EXPORT  |vp9_mb_lpf_horizontal_edge_w_neon|
    ARM

    AREA ||.text||, CODE, READONLY, ALIGN=2

; void vp9_mb_lpf_horizontal_edge_w_neon(uint8_t *s, int p,
;                                        const uint8_t *blimit,
;                                        const uint8_t *limit,
;                                        const uint8_t *thresh)
; r0    uint8_t *s,
; r1    int p, /* pitch */
; r2    const uint8_t *blimit,
; r3    const uint8_t *limit,
; sp    const uint8_t *thresh,
|vp9_mb_lpf_horizontal_edge_w_neon| PROC
    push        {r4-r7, lr}

    ldr         r12, [sp, #24]             ; load count

    ldrb        r4, [r2]                   ; load *blimit
    ldrb        r5, [r3]                   ; load *limit
    ldr         r3, [sp, #20]              ; load thresh
    vdup.u8     d16, r4                    ; duplicate blimit
    ldrb        r6, [r3]                   ; load *thresh
    vdup.u8     d17, r5                    ; duplicate limit
    vdup.u8     d18, r6                    ; duplicate thresh

    cmp         r12, #0
    beq         end_vp9_mb_lpf_h_edge

    vstmdb  sp!, {d8-d15}                  ; Save Neon registers

    add         r1, r1, r1

count_mb_lpf_h_loop
    sub         r3, r0, r1, lsl #2         ; move src pointer down by 8 lines
    add         r2, r3, r1, lsr #1         ; set to 7 lines down

    vpush       {d16-d18}                  ; Save blimit, limit, thresh

    vld1.u8     {d0}, [r3@64], r1          ; p7
    vld1.u8     {d1}, [r2@64], r1          ; p6
    vld1.u8     {d2}, [r3@64], r1          ; p5
    vld1.u8     {d3}, [r2@64], r1          ; p4
    vld1.u8     {d4}, [r3@64], r1          ; p3
    vld1.u8     {d5}, [r2@64], r1          ; p2
    vld1.u8     {d6}, [r3@64], r1          ; p1
    vld1.u8     {d7}, [r2@64], r1          ; p0
    vld1.u8     {d8}, [r3@64], r1          ; q0
    vld1.u8     {d9}, [r2@64], r1          ; q1
    vld1.u8     {d10}, [r3@64], r1         ; q2
    vld1.u8     {d11}, [r2@64], r1         ; q3
    vld1.u8     {d12}, [r3@64], r1         ; q4
    vld1.u8     {d13}, [r2@64], r1         ; q5
    vld1.u8     {d14}, [r3@64]             ; q6
    vld1.u8     {d15}, [r2@64], r1         ; q7

    ;sub         r3, r3, r1, lsl #2         ; move back 8
    ;sub         r2, r2, r1, lsl #3         ; move back 16
    ;sub         r3, r3, r1, lsl #1         ; move back 4

    bl          vp9_wide_mbfilter_neon

    ;vst1.u8     {d16}, [r2@64], r1         ; store op6
    ;vst1.u8     {d24}, [r3@64], r1         ; store op5
    ;vst1.u8     {d25}, [r2@64], r1         ; store op4
    ;vst1.u8     {d26}, [r3@64], r1         ; store op3
    ;vst1.u8     {d27}, [r2@64], r1         ; store op2
    ;vst1.u8     {d18}, [r3@64], r1         ; store op1
    ;vst1.u8     {d19}, [r2@64], r1         ; store op0
    ;vst1.u8     {d20}, [r3@64], r1         ; store oq0
    ;vst1.u8     {d21}, [r2@64], r1         ; store oq1
    ;vst1.u8     {d22}, [r3@64], r1         ; store oq2
    ;vst1.u8     {d23}, [r2@64], r1         ; store oq3
    ;vst1.u8     {d0}, [r3@64], r1          ; store oq4
    ;vst1.u8     {d1}, [r2@64]              ; store oq5
    ;vst1.u8     {d2}, [r3@64]              ; store oq6

    add         r0, r0, #8
    subs        r12, r12, #1

    vpop        {d16-d18}                  ; Restore blimit, limit, thresh

    bne         count_mb_lpf_h_loop

    vldmia  sp!, {d8-d15}                  ; Restore Neon registers

end_vp9_mb_lpf_h_edge
    pop         {r4-r7, pc}

    ENDP        ; |vp9_mb_lpf_horizontal_edge_w_neon|

; void vp9_wide_mbfilter_neon();
; This is a helper function for the loopfilters. The invidual functions do the
; necessary load, transpose (if necessary) and store.
;
; r0-r3 PRESERVE
; d16    blimit
; d17    limit
; d18    thresh
; d0    p7
; d1    p6
; d2    p5
; d3    p4
; d4    p3
; d5    p2
; d6    p1
; d7    p0
; d8    q0
; d9    q1
; d10   q2
; d11   q3
; d12   q4
; d13   q5
; d14   q6
; d15   q7
|vp9_wide_mbfilter_neon| PROC
    ; filter_mask
    sub         sp, sp, #64                 ; Space for flat & mask
    mov         r4, sp

    vabd.u8     d19, d4, d5                ; m1 = abs(p3 - p2)
    vabd.u8     d20, d5, d6                ; m2 = abs(p2 - p1)
    vabd.u8     d21, d6, d7                ; m3 = abs(p1 - p0)
    vabd.u8     d22, d9, d8                ; m4 = abs(q1 - q0)
    vabd.u8     d23, d10, d9               ; m5 = abs(q2 - q1)
    vabd.u8     d24, d11, d10              ; m6 = abs(q3 - q2)

    ; only compare the largest value to limit
    vmax.u8     d19, d19, d20              ; m1 = max(m1, m2)
    vmax.u8     d20, d21, d22              ; m2 = max(m3, m4)
    vmax.u8     d23, d23, d24              ; m3 = max(m5, m6)
    vmax.u8     d19, d19, d20

    vabd.u8     d24, d7, d8                ; m9 = abs(p0 - q0)

    vmax.u8     d19, d19, d23

    vabd.u8     d23, d6, d9                ; a = abs(p1 - q1)
    vqadd.u8    d24, d24, d24              ; b = abs(p0 - q0) * 2

    ; abs () > limit
    vcge.u8     d19, d17, d19

    ; flatmask4
    vabd.u8     d25, d7, d5                ; m7 = abs(p0 - p2)
    vabd.u8     d26, d8, d10               ; m8 = abs(q0 - q2)
    vabd.u8     d27, d4, d7                ; m10 = abs(p3 - p0)
    vabd.u8     d28, d11, d8               ; m11 = abs(q3 - q0)

    ; only compare the largest value to thresh
    vmax.u8     d25, d25, d26              ; m4 = max(m7, m8)
    vmax.u8     d26, d27, d28              ; m5 = max(m10, m11)
    vmax.u8     d25, d25, d26              ; m4 = max(m4, m5)
    vmax.u8     d20, d20, d25              ; m2 = max(m2, m4)

    vshr.u8     d23, d23, #1               ; a = a / 2
    vqadd.u8    d24, d24, d23              ; a = b + a

    vmov.u8     d30, #1
    vcge.u8     d24, d16, d24              ; a > blimit

    vcge.u8     d20, d30, d20              ; flat

    vand        d19, d19, d24              ; mask

    ; hevmask
    vcgt.u8     d21, d21, d18              ; (abs(p1 - p0) > thresh)*-1
    vcgt.u8     d22, d22, d18              ; (abs(q1 - q0) > thresh)*-1
    vorr        d21, d21, d22              ; hev

    vand        d16, d20, d19              ; flat && mask

    vst1.u8     {d16}, [r4]                ; Store flat & mask

    mov         r4, sp
    ldmia       r4, {r5, r6}               ; load flat & mask
    orrs        r7, r5, r6                 ; Check for 0
    moveq       r7, #1                     ; Only do filter branch

    ; flatmask5(1, p7, p6, p5, p4, p0, q0, q4, q5, q6, q7)
    vabd.u8     d22, d3, d7                ; abs(p4 - p0)
    vabd.u8     d23, d12, d8               ; abs(q4 - q0)
    vabd.u8     d24, d7, d2                ; abs(p0 - p5)
    vabd.u8     d25, d8, d13               ; abs(q0 - q5)
    vabd.u8     d26, d1, d7                ; abs(p6 - p0)
    vabd.u8     d27, d14, d8               ; abs(q6 - q0)
    vabd.u8     d28, d0, d7                ; abs(p7 - p0)
    vabd.u8     d29, d15, d8               ; abs(q7 - q0)

    ; only compare the largest value to thresh
    vmax.u8     d22, d22, d23              ; max(abs(p4 - p0), abs(q4 - q0))
    vmax.u8     d23, d24, d25              ; max(abs(p0 - p5), abs(q0 - q5))
    vmax.u8     d24, d26, d27              ; max(abs(p6 - p0), abs(q6 - q0))
    vmax.u8     d25, d28, d29              ; max(abs(p7 - p0), abs(q7 - q0))

    vmax.u8     d26, d22, d23
    vmax.u8     d27, d24, d25
    vmax.u8     d23, d26, d27

    vcge.u8     d18, d30, d23              ; flat2

    vmov.u8     d22, #0x80

    vand        d17, d18, d16              ; flat2 && flat && mask

    vst1.u8     {d17}, [r4]                ; Store flat2 && flat && mask

    mov         r4, sp
    ldmia       r4, {r5, r6}               ; load flat2 && flat && mask
    orrs        r4, r5, r6                 ; Check for 0
    moveq       r4, #1                     ; Only do mbfilter branch

    ; mbfilter() function

    ; filter() function
    ; convert to signed
    veor        d23, d8, d22               ; qs0
    veor        d24, d7, d22               ; ps0
    veor        d25, d6, d22               ; ps1
    veor        d26, d9, d22               ; qs1

    vmov.u8     d27, #3

    vsub.s8     d28, d23, d24              ; ( qs0 - ps0)

    vqsub.s8    d29, d25, d26              ; filter = clamp(ps1-qs1)

    vmull.s8    q15, d28, d27              ; 3 * ( qs0 - ps0)

    vand        d29, d29, d21              ; filter &= hev

    vaddw.s8    q15, q15, d29              ; filter + 3 * (qs0 - ps0)

    vmov.u8     d29, #4

    ; filter = clamp(filter + 3 * ( qs0 - ps0))
    vqmovn.s16  d28, q15

    vand        d28, d28, d19              ; filter &= mask

    vqadd.s8    d30, d28, d27              ; filter2 = clamp(filter+3)
    vqadd.s8    d29, d28, d29              ; filter1 = clamp(filter+4)
    vshr.s8     d30, d30, #3               ; filter2 >>= 3
    vshr.s8     d29, d29, #3               ; filter1 >>= 3


    vqadd.s8    d24, d24, d30              ; op0 = clamp(ps0 + filter2)
    vqsub.s8    d23, d23, d29              ; oq0 = clamp(qs0 - filter1)

    ; outer tap adjustments: ++filter1 >> 1
    vrshr.s8    d29, d29, #1
    vbic        d29, d29, d21              ; filter &= ~hev

    vqadd.s8    d25, d25, d29              ; op1 = clamp(ps1 + filter)
    vqsub.s8    d26, d26, d29              ; oq1 = clamp(qs1 - filter)

    veor        d24, d24, d22              ; *f_op0 = u^0x80
    veor        d23, d23, d22              ; *f_oq0 = u^0x80
    veor        d25, d25, d22              ; *f_op1 = u^0x80
    veor        d26, d26, d22              ; *f_oq1 = u^0x80

    cmp         r7, #1
    bne         mbfilter

    ; flat && mask were not set for any of the channels. Just store the values
    ; from filter.
    sub         r2, r2, r1, lsl #2         ; move back 8
    sub         r3, r3, r1, lsl #2         ; move back 8
    sub         r2, r2, r1                 ; move back 2

    vst1.u8     {d25}, [r3@64], r1         ; store op1
    vst1.u8     {d24}, [r2@64], r1         ; store op0
    vst1.u8     {d23}, [r3@64], r1         ; store oq0
    vst1.u8     {d26}, [r2@64], r1         ; store oq1

    add         sp, sp, #64                ; delete space
    bx          lr

    ; mbfilter flat && mask branch
    ; TODO(fgalligan): Can I decrease the cycles shifting to consective d's
    ; and using vibt on the q's?
mbfilter
    vmov.u8     d29, #2
    vaddl.u8    q15, d7, d8                ; op2 = p0 + q0
    vmlal.u8    q15, d4, d27               ; op2 = p0 + q0 + p3 * 3
    vmlal.u8    q15, d5, d29               ; op2 = p0 + q0 + p3 * 3 + p2 * 2
    vaddw.u8    q15, d6                    ; op2=p1 + p0 + q0 + p3 * 3 + p2 *2
    vqrshrn.u16 d18, q15, #3               ; r_op2

    vsubw.u8    q15, d4                    ; op1 = op2 - p3
    vsubw.u8    q15, d5                    ; op1 -= p2
    vaddw.u8    q15, d6                    ; op1 += p1
    vaddw.u8    q15, d9                    ; op1 += q1
    vqrshrn.u16 d19, q15, #3               ; r_op1

    vsubw.u8    q15, d4                    ; op0 = op1 - p3
    vsubw.u8    q15, d6                    ; op0 -= p1
    vaddw.u8    q15, d7                    ; op0 += p0
    vaddw.u8    q15, d10                   ; op0 += q2
    vqrshrn.u16 d20, q15, #3               ; r_op0

    vsubw.u8    q15, d4                    ; oq0 = op0 - p3
    vsubw.u8    q15, d7                    ; oq0 -= p0
    vaddw.u8    q15, d8                    ; oq0 += q0
    vaddw.u8    q15, d11                   ; oq0 += q3
    vqrshrn.u16 d21, q15, #3               ; r_oq0

    vsubw.u8    q15, d5                    ; oq1 = oq0 - p2
    vsubw.u8    q15, d8                    ; oq1 -= q0
    vaddw.u8    q15, d9                    ; oq1 += q1
    vaddw.u8    q15, d11                   ; oq1 += q3
    vqrshrn.u16 d22, q15, #3               ; r_oq1

    vsubw.u8    q15, d6                    ; oq2 = oq0 - p1
    vsubw.u8    q15, d9                    ; oq2 -= q1
    vaddw.u8    q15, d10                   ; oq2 += q2
    vaddw.u8    q15, d11                   ; oq2 += q3
    vqrshrn.u16 d27, q15, #3               ; r_oq2

    ; Filter does not set op2 or oq2, so use p2 and q2.
    vbit        d18, d18, d16              ; t_op2 |= r_op2 & (flat & mask)
    vbif        d18, d5, d16               ; t_op2 |= p2 & ~(flat & mask)

    vbit        d19, d19, d16              ; t_op1 |= r_op1 & (flat & mask)
    vbif        d19, d25, d16              ; t_op1 |= f_op1 & ~(flat & mask)

    vbit        d20, d20, d16              ; t_op0 |= r_op0 & (flat & mask)
    vbif        d20, d24, d16              ; t_op0 |= f_op0 & ~(flat & mask)

    vbit        d21, d21, d16              ; t_oq0 |= r_oq0 & (flat & mask)
    vbif        d21, d23, d16              ; t_oq0 |= f_oq0 & ~(flat & mask)

    vbit        d22, d22, d16              ; t_oq1 |= r_oq1 & (flat & mask)
    vbif        d22, d26, d16              ; t_oq1 |= f_oq1 & ~(flat & mask)

    vbit        d23, d27, d16              ; t_oq2 |= r_oq2 & (flat & mask)
    vbif        d23, d10, d16              ; t_oq2 |= q2 & ~(flat & mask)

    cmp         r4, #1
    bne         wide_mbfilter

    ; flat2 was not set for any of the channels. Just store the values from
    ; mbfilter.
    sub         r2, r2, r1, lsl #2         ; move back 8
    sub         r3, r3, r1, lsl #2         ; move back 8
    sub         r2, r2, r1, lsl #1         ; move back 4

    vst1.u8     {d18}, [r2@64], r1         ; store op2
    vst1.u8     {d19}, [r3@64], r1         ; store op1
    vst1.u8     {d20}, [r2@64], r1         ; store op0
    vst1.u8     {d21}, [r3@64], r1         ; store oq0
    vst1.u8     {d22}, [r2@64], r1         ; store oq1
    vst1.u8     {d23}, [r3@64], r1         ; store oq2

    add         sp, sp, #64                ; delete space
    bx          lr

    ; wide_mbfilter flat2 && flat && mask branch
wide_mbfilter
    vmov.u8     d16, #7
    vaddl.u8    q15, d7, d8                ; op6 = p0 + q0
    vmlal.u8    q15, d0, d16               ; op6 += p7 * 3
    vmlal.u8    q15, d1, d29               ; op6 += p6 * 2
    vaddw.u8    q15, d2                    ; op6 += p5
    vaddw.u8    q15, d3                    ; op6 += p4
    vaddw.u8    q15, d4                    ; op6 += p3
    vaddw.u8    q15, d5                    ; op6 += p2
    vaddw.u8    q15, d6                    ; op6 += p1
    vqrshrn.u16 d16, q15, #4               ; w_op6

    vsubw.u8    q15, d0                    ; op5 = op6 - p7
    vsubw.u8    q15, d1                    ; op5 -= p6
    vaddw.u8    q15, d2                    ; op5 += p5
    vaddw.u8    q15, d9                    ; op5 += q1
    vqrshrn.u16 d24, q15, #4               ; w_op5

    vsubw.u8    q15, d0                    ; op4 = op5 - p7
    vsubw.u8    q15, d2                    ; op4 -= p5
    vaddw.u8    q15, d3                    ; op4 += p4
    vaddw.u8    q15, d10                   ; op4 += q2
    vqrshrn.u16 d25, q15, #4               ; w_op4

    vsubw.u8    q15, d0                    ; op3 = op4 - p7
    vsubw.u8    q15, d3                    ; op3 -= p4
    vaddw.u8    q15, d4                    ; op3 += p3
    vaddw.u8    q15, d11                   ; op3 += q3
    vqrshrn.u16 d26, q15, #4               ; w_op3

    vsubw.u8    q15, d0                    ; op2 = op3 - p7
    vsubw.u8    q15, d4                    ; op2 -= p3
    vaddw.u8    q15, d5                    ; op2 += p2
    vaddw.u8    q15, d12                   ; op2 += q4
    vqrshrn.u16 d27, q15, #4               ; w_op2

    vbit        d27, d27, d17              ; op2 |= w_op2 & (f2 & f & m)
    vbif        d27, d18, d17              ; op2 |= t_op2 & ~(f2 & f & m)

    vsubw.u8    q15, d0                    ; op1 = op2 - p7
    vsubw.u8    q15, d5                    ; op1 -= p2
    vaddw.u8    q15, d6                    ; op1 += p1
    vaddw.u8    q15, d13                   ; op1 += q5
    vqrshrn.u16 d18, q15, #4               ; w_op1

    vbit        d18, d18, d17              ; op1 |= w_op1 & (f2 & f & m)
    vbif        d18, d19, d17              ; op1 |= t_op1 & ~(f2 & f & m)

    vsubw.u8    q15, d0                    ; op0 = op1 - p7
    vsubw.u8    q15, d6                    ; op0 -= p1
    vaddw.u8    q15, d7                    ; op0 += p0
    vaddw.u8    q15, d14                   ; op0 += q6
    vqrshrn.u16 d19, q15, #4               ; w_op0

    vbit        d19, d19, d17              ; op0 |= w_op0 & (f2 & f & m)
    vbif        d19, d20, d17              ; op0 |= t_op0 & ~(f2 & f & m)

    vsubw.u8    q15, d0                    ; oq0 = op0 - p7
    vsubw.u8    q15, d7                    ; oq0 -= p0
    vaddw.u8    q15, d8                    ; oq0 += q0
    vaddw.u8    q15, d15                   ; oq0 += q7
    vqrshrn.u16 d20, q15, #4               ; w_oq0

    vbit        d20, d20, d17              ; oq0 |= w_oq0 & (f2 & f & m)
    vbif        d20, d21, d17              ; oq0 |= t_oq0 & ~(f2 & f & m)

    vsubw.u8    q15, d1                    ; oq1 = oq0 - p6
    vsubw.u8    q15, d8                    ; oq1 -= q0
    vaddw.u8    q15, d9                    ; oq1 += q1
    vaddw.u8    q15, d15                   ; oq1 += q7
    vqrshrn.u16 d21, q15, #4               ; w_oq1

    vbit        d21, d21, d17              ; oq1 |= w_oq1 & (f2 & f & m)
    vbif        d21, d22, d17              ; oq1 |= t_oq1 & ~(f2 & f & m)

    vsubw.u8    q15, d2                    ; oq2 = oq1 - p5
    vsubw.u8    q15, d9                    ; oq2 -= q1
    vaddw.u8    q15, d10                   ; oq2 += q2
    vaddw.u8    q15, d15                   ; oq2 += q7
    vqrshrn.u16 d22, q15, #4               ; w_oq2

    vbit        d22, d22, d17              ; oq2 |= w_oq2 & (f2 & f & m)
    vbif        d22, d23, d17              ; oq2 |= t_oq2 & ~(f2 & f & m)

    vsubw.u8    q15, d3                    ; oq3 = oq2 - p4
    vsubw.u8    q15, d10                   ; oq3 -= q2
    vaddw.u8    q15, d11                   ; oq3 += q3
    vaddw.u8    q15, d15                   ; oq3 += q7
    vqrshrn.u16 d23, q15, #4               ; w_oq3

    vbit        d16, d16, d17              ; op6 |= w_op6 & (f2 & f & m)
    vbif        d16, d1, d17               ; op6 |= p6 & ~(f2 & f & m)

    vsubw.u8    q15, d4                    ; oq4 = oq3 - p3
    vsubw.u8    q15, d11                   ; oq4 -= q3
    vaddw.u8    q15, d12                   ; oq4 += q4
    vaddw.u8    q15, d15                   ; oq4 += q7
    vqrshrn.u16 d0, q15, #4                ; w_oq4

    vbit        d24, d24, d17              ; op5 |= w_op5 & (f2 & f & m)
    vbif        d24, d2, d17               ; op5 |= p5 & ~(f2 & f & m)

    vsubw.u8    q15, d5                    ; oq5 = oq4 - p2
    vsubw.u8    q15, d12                   ; oq5 -= q4
    vaddw.u8    q15, d13                   ; oq5 += q5
    vaddw.u8    q15, d15                   ; oq5 += q7
    vqrshrn.u16 d1, q15, #4                ; w_oq5

    vbit        d25, d25, d17              ; op4 |= w_op4 & (f2 & f & m)
    vbif        d25, d3, d17               ; op4 |= p4 & ~(f2 & f & m)

    vsubw.u8    q15, d6                    ; oq6 = oq5 - p1
    vsubw.u8    q15, d13                   ; oq6 -= q5
    vaddw.u8    q15, d14                   ; oq6 += q6
    vaddw.u8    q15, d15                   ; oq6 += q7
    vqrshrn.u16 d2, q15, #4                ; w_oq6

    vbit        d26, d26, d17              ; op3 |= w_op3 & (f2 & f & m)
    vbif        d26, d4, d17               ; op3 |= p3 & ~(f2 & f & m)

    vbit        d23, d23, d17              ; oq3 |= w_oq3 & (f2 & f & m)
    vbif        d23, d11, d17              ; oq3 |= q3 & ~(f2 & f & m)

    vbit        d0, d0, d17                ; oq4 |= w_oq4 & (f2 & f & m)
    vbif        d0, d12, d17               ; oq4 |= q4 & ~(f2 & f & m)

    vbit        d1, d1, d17                ; oq5 |= w_oq5 & (f2 & f & m)
    vbif        d1, d13, d17               ; oq5 |= q5 & ~(f2 & f & m)

    vbit        d2, d2, d17                ; oq6 |= w_oq6 & (f2 & f & m)
    vbif        d2, d14, d17               ; oq6 |= q6 & ~(f2 & f & m)

    sub         r3, r3, r1, lsl #2         ; move back 8
    sub         r2, r2, r1, lsl #3         ; move back 16
    sub         r3, r3, r1, lsl #1         ; move back 4

    vst1.u8     {d16}, [r2@64], r1         ; store op6
    vst1.u8     {d24}, [r3@64], r1         ; store op5
    vst1.u8     {d25}, [r2@64], r1         ; store op4
    vst1.u8     {d26}, [r3@64], r1         ; store op3
    vst1.u8     {d27}, [r2@64], r1         ; store op2
    vst1.u8     {d18}, [r3@64], r1         ; store op1
    vst1.u8     {d19}, [r2@64], r1         ; store op0
    vst1.u8     {d20}, [r3@64], r1         ; store oq0
    vst1.u8     {d21}, [r2@64], r1         ; store oq1
    vst1.u8     {d22}, [r3@64], r1         ; store oq2
    vst1.u8     {d23}, [r2@64], r1         ; store oq3
    vst1.u8     {d0}, [r3@64], r1          ; store oq4
    vst1.u8     {d1}, [r2@64]              ; store oq5
    vst1.u8     {d2}, [r3@64]              ; store oq6

    add         sp, sp, #64                ; delete space
    bx          lr
    ENDP        ; |vp9_wide_mbfilter_neon|

    END
