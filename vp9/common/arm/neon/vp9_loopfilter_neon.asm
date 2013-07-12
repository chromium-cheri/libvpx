;
;  Copyright (c) 2013 The WebM project authors. All Rights Reserved.
;
;  Use of this source code is governed by a BSD-style license
;  that can be found in the LICENSE file in the root of the source
;  tree. An additional intellectual property rights grant can be found
;  in the file PATENTS.  All contributing project authors may
;  be found in the AUTHORS file in the root of the source tree.
;

    EXPORT  |vp9_loop_filter_horizontal_edge_neon|
    EXPORT  |vp9_loop_filter_vertical_edge_neon|
    EXPORT  |vp9_mbloop_filter_horizontal_edge_neon|
    EXPORT  |vp9_mbloop_filter_vertical_edge_neon|
    ARM

    AREA ||.text||, CODE, READONLY, ALIGN=2

; Currently vp9 only works on iterations 8 at a time. The vp8 loop filter
; works on 16 iterations at a time.
; TODO(fgalligan): See about removing the count code as this function is only
; called with a count of 1.
;
; void vp9_loop_filter_horizontal_edge_neon(uint8_t *s,
;                                           int p /* pitch */,
;                                           const uint8_t *blimit,
;                                           const uint8_t *limit,
;                                           const uint8_t *thresh,
;                                           int count)
;
; r0    uint8_t *s,
; r1    int p, /* pitch */
; r2    const uint8_t *blimit,
; r3    const uint8_t *limit,
; sp    const uint8_t *thresh,
; sp+4  int count
|vp9_loop_filter_horizontal_edge_neon| PROC
    push        {lr}

    ldr         r12, [sp,#8]               ; load count
    cmp         r12, #0
    beq         end_vp9_lf_h_edge

    vld1.8      {d0[]}, [r2]               ; duplicate *blimit
    ldr         r2, [sp, #4]               ; load thresh
    vld1.8      {d1[]}, [r3]               ; duplicate *limit
    vld1.8      {d2[]}, [r2]               ; duplicate *thresh

count_lf_h_loop
    sub         r2, r0, r1, lsl #2         ; move src pointer down by 4 lines
    add         r3, r2, r1
    add         r1, r1, r1

    vld1.u8     {d3}, [r2@64], r1          ; p3
    vld1.u8     {d4}, [r3@64], r1          ; p2
    vld1.u8     {d5}, [r2@64], r1          ; p1
    vld1.u8     {d6}, [r3@64], r1          ; p0
    vld1.u8     {d7}, [r2@64], r1          ; q0
    vld1.u8     {d16}, [r3@64], r1         ; q1
    vld1.u8     {d17}, [r2@64]             ; q2
    vld1.u8     {d18}, [r3@64]             ; q3

    sub         r2, r2, r1, lsl #1
    sub         r3, r3, r1, lsl #1

    bl          vp9_loop_filter_neon

    vst1.u8     {d4}, [r2@64], r1          ; store op1
    vst1.u8     {d5}, [r3@64], r1          ; store op0
    vst1.u8     {d6}, [r2@64], r1          ; store oq0
    vst1.u8     {d7}, [r3@64], r1          ; store oq1

    add         r0, r0, #8
    subs        r12, r12, #1
    bne         count_lf_h_loop

end_vp9_lf_h_edge
    pop         {pc}
    ENDP        ; |vp9_loop_filter_horizontal_edge_neon|

; Currently vp9 only works on iterations 8 at a time. The vp8 loop filter
; works on 16 iterations at a time.
; TODO(fgalligan): See about removing the count code as this function is only
; called with a count of 1.
;
; void vp9_loop_filter_vertical_edge_neon(uint8_t *s,
;                                         int p /* pitch */,
;                                         const uint8_t *blimit,
;                                         const uint8_t *limit,
;                                         const uint8_t *thresh,
;                                         int count)
;
; r0    uint8_t *s,
; r1    int p, /* pitch */
; r2    const uint8_t *blimit,
; r3    const uint8_t *limit,
; sp    const uint8_t *thresh,
; sp+4  int count
|vp9_loop_filter_vertical_edge_neon| PROC
    push        {lr}

    ldr         r12, [sp,#8]               ; load count
    cmp         r12, #0
    beq         end_vp9_lf_v_edge

    vld1.8      {d0[]}, [r2]               ; duplicate *blimit
    ldr         r2, [sp, #4]               ; load thresh
    vld1.8      {d1[]}, [r3]               ; duplicate *limit
    vld1.8      {d2[]}, [r2]               ; duplicate *thresh

count_lf_v_loop
    sub         r2, r0, #4                 ; move s pointer down by 4 columns

    vld1.u8     {d3}, [r2], r1             ; load s data
    vld1.u8     {d4}, [r2], r1
    vld1.u8     {d5}, [r2], r1
    vld1.u8     {d6}, [r2], r1
    vld1.u8     {d7}, [r2], r1
    vld1.u8     {d16}, [r2], r1
    vld1.u8     {d17}, [r2], r1
    vld1.u8     {d18}, [r2]

    ;transpose to 8x16 matrix
    vtrn.32     d3, d7
    vtrn.32     d4, d16
    vtrn.32     d5, d17
    vtrn.32     d6, d18

    vtrn.16     d3, d5
    vtrn.16     d4, d6
    vtrn.16     d7, d17
    vtrn.16     d16, d18

    vtrn.8      d3, d4
    vtrn.8      d5, d6
    vtrn.8      d7, d16
    vtrn.8      d17, d18

    bl          vp9_loop_filter_neon

    sub         r0, r0, #2

    ;store op1, op0, oq0, oq1
    vst4.8      {d4[0], d5[0], d6[0], d7[0]}, [r0], r1
    vst4.8      {d4[1], d5[1], d6[1], d7[1]}, [r0], r1
    vst4.8      {d4[2], d5[2], d6[2], d7[2]}, [r0], r1
    vst4.8      {d4[3], d5[3], d6[3], d7[3]}, [r0], r1
    vst4.8      {d4[4], d5[4], d6[4], d7[4]}, [r0], r1
    vst4.8      {d4[5], d5[5], d6[5], d7[5]}, [r0], r1
    vst4.8      {d4[6], d5[6], d6[6], d7[6]}, [r0], r1
    vst4.8      {d4[7], d5[7], d6[7], d7[7]}, [r0]

    add         r0, r0, r1, lsl #3         ; s += pitch * 8
    subs        r12, r12, #1
    bne         count_lf_v_loop

end_vp9_lf_v_edge
    pop         {pc}
    ENDP        ; |vp9_loop_filter_vertical_edge_neon|

; void vp9_loop_filter_neon();
; This is a helper function for the loopfilters. The invidual functions do the
; necessary load, transpose (if necessary) and store. The function does not use
; registers d8-d15.
;
; r0-r3 PRESERVE
; d0    blimit
; d1    limit
; d2    thresh
; d3    p3
; d4    p2
; d5    p1
; d6    p0
; d7    q0
; d16   q1
; d17   q2
; d18   q3
|vp9_loop_filter_neon| PROC
    ; filter_mask
    vabd.u8     d19, d3, d4                 ; abs(p3 - p2)
    vabd.u8     d20, d4, d5                 ; abs(p2 - p1)
    vabd.u8     d21, d5, d6                 ; abs(p1 - p0)
    vabd.u8     d22, d16, d7                ; abs(q1 - q0)
    vabd.u8     d3, d17, d16                ; abs(q2 - q1)
    vabd.u8     d4, d18, d17                ; abs(q3 - q2)

    ; only compare the largest value to limit
    vmax.u8     d19, d19, d20
    vmax.u8     d20, d21, d22
    vmax.u8     d3, d3, d4
    vmax.u8     d23, d19, d20

    vabd.u8     d17, d6, d7                 ; abs(p0 - q0)

    ; hevmask
    vcgt.u8     d21, d21, d2                ; (abs(p1 - p0) > thresh)*-1
    vcgt.u8     d22, d22, d2                ; (abs(q1 - q0) > thresh)*-1
    vmax.u8     d23, d23, d3

    vmov.u8     d18, #0x80

    vabd.u8     d28, d5, d16                ; a = abs(p1 - q1)
    vqadd.u8    d17, d17, d17               ; b = abs(p0 - q0) * 2

    ; abs () > limit
    vcge.u8     d23, d1, d23

    ; filter() function
    ; convert to signed
    veor        d7, d7, d18                 ; qs0
    vshr.u8     d28, d28, #1                ; a = a / 2
    veor        d6, d6, d18                 ; ps0

    veor        d5, d5, d18                 ; ps1
    vqadd.u8    d17, d17, d28               ; a = b + a

    veor        d16, d16, d18               ; qs1

    vmov.u8     d19, #3

    vsub.s8     d28, d7, d6                 ; ( qs0 - ps0)

    vcge.u8     d17, d0, d17                ; a > blimit

    vqsub.s8    d27, d5, d16                ; filter = clamp(ps1-qs1)
    vorr        d22, d21, d22               ; hevmask

    vmull.s8    q12, d28, d19               ; 3 * ( qs0 - ps0)

    vand        d27, d27, d22               ; filter &= hev
    vand        d23, d23, d17               ; filter_mask

    vaddw.s8    q12, q12, d27               ; filter + 3 * (qs0 - ps0)

    vmov.u8     d17, #4

    ; filter = clamp(filter + 3 * ( qs0 - ps0))
    vqmovn.s16  d27, q12

    vand        d27, d27, d23               ; filter &= mask

    vqadd.s8    d28, d27, d19               ; filter2 = clamp(filter+3)
    vqadd.s8    d27, d27, d17               ; filter1 = clamp(filter+4)
    vshr.s8     d28, d28, #3                ; filter2 >>= 3
    vshr.s8     d27, d27, #3                ; filter1 >>= 3


    vqadd.s8    d19, d6, d28                ; u = clamp(ps0 + filter2)
    vqsub.s8    d26, d7, d27                ; u = clamp(qs0 - filter1)

    ; outer tap adjustments: ++filter >> 1
    vrshr.s8    d27, d27, #1
    vbic        d27, d27, d22               ; filter &= ~hev

    vqadd.s8    d21, d5, d27                ; u = clamp(ps1 + filter)
    vqsub.s8    d20, d16, d27               ; u = clamp(qs1 - filter)

    veor        d5, d19, d18                ; *op0 = u^0x80
    veor        d6, d26, d18                ; *oq0 = u^0x80
    veor        d4, d21, d18                ; *op1 = u^0x80
    veor        d7, d20, d18                ; *oq1 = u^0x80

    bx          lr
    ENDP        ; |vp9_loop_filter_neon|

; void vp9_mbloop_filter_horizontal_edge_neon(uint8_t *s, int p,
;                                             const uint8_t *blimit,
;                                             const uint8_t *limit,
;                                             const uint8_t *thresh,
;                                             int count)
; r0    uint8_t *s,
; r1    int p, /* pitch */
; r2    const uint8_t *blimit,
; r3    const uint8_t *limit,
; sp    const uint8_t *thresh,
; sp+4  int count
|vp9_mbloop_filter_horizontal_edge_neon| PROC
    push        {r4-r6, lr}

    ldr         r12, [sp,#20]              ; load count
    cmp         r12, #0
    beq         end_vp9_mblf_h_edge

    vld1.8      {d0[]}, [r2]               ; duplicate *blimit
    ldr         r2, [sp, #16]              ; load thresh
    vld1.8      {d1[]}, [r3]               ; duplicate *limit
    vld1.8      {d2[]}, [r2]               ; duplicate *thresh

count_mblf_h_loop
    sub         r3, r0, r1, lsl #2         ; move src pointer down by 4 lines
    add         r2, r3, r1
    add         r1, r1, r1

    vld1.u8     {d3}, [r3@64], r1          ; p3
    vld1.u8     {d4}, [r2@64], r1          ; p2
    vld1.u8     {d5}, [r3@64], r1          ; p1
    vld1.u8     {d6}, [r2@64], r1          ; p0
    vld1.u8     {d7}, [r3@64], r1          ; q0
    vld1.u8     {d16}, [r2@64], r1         ; q1
    vld1.u8     {d17}, [r3@64]             ; q2
    vld1.u8     {d18}, [r2@64], r1         ; q3

    sub         r3, r3, r1, lsl #1
    sub         r2, r2, r1, lsl #2

    bl          vp9_mbloop_filter_neon

    vst1.u8     {d2}, [r2@64], r1          ; store op2
    vst1.u8     {d3}, [r3@64], r1          ; store op1
    vst1.u8     {d4}, [r2@64], r1          ; store op0
    vst1.u8     {d5}, [r3@64], r1          ; store oq0
    vst1.u8     {d6}, [r2@64], r1          ; store oq1
    vst1.u8     {d7}, [r3@64], r1          ; store oq2

    add         r0, r0, #8
    subs        r12, r12, #1
    bne         count_mblf_h_loop

end_vp9_mblf_h_edge
    pop         {r4-r6, pc}

    ENDP        ; |vp9_mbloop_filter_horizontal_edge_neon|

; void vp9_mbloop_filter_vertical_edge_neon(uint8_t *s,
;                                           int pitch,
;                                           const uint8_t *blimit,
;                                           const uint8_t *limit,
;                                           const uint8_t *thresh,
;                                           int count)
;
; r0    uint8_t *s,
; r1    int pitch,
; r2    const uint8_t *blimit,
; r3    const uint8_t *limit,
; sp    const uint8_t *thresh,
; sp+4  int count
|vp9_mbloop_filter_vertical_edge_neon| PROC
    push        {r4-r6, lr}

    ldr         r12, [sp,#20]              ; load count
    cmp         r12, #0
    beq         end_vp9_mblf_v_edge

    vld1.8      {d0[]}, [r2]               ; duplicate *blimit
    ldr         r2, [sp, #16]              ; load thresh
    vld1.8      {d1[]}, [r3]               ; duplicate *limit
    vld1.8      {d2[]}, [r2]               ; duplicate *thresh

count_mblf_v_loop
    sub         r2, r0, #4                 ; move s pointer down by 4 columns

    vld1.u8     {d3}, [r2], r1             ; load s data
    vld1.u8     {d4}, [r2], r1
    vld1.u8     {d5}, [r2], r1
    vld1.u8     {d6}, [r2], r1
    vld1.u8     {d7}, [r2], r1
    vld1.u8     {d16}, [r2], r1
    vld1.u8     {d17}, [r2], r1
    vld1.u8     {d18}, [r2]

    ;transpose to 8x16 matrix
    vtrn.32     d3, d7
    vtrn.32     d4, d16
    vtrn.32     d5, d17
    vtrn.32     d6, d18

    vtrn.16     d3, d5
    vtrn.16     d4, d6
    vtrn.16     d7, d17
    vtrn.16     d16, d18

    vtrn.8      d3, d4
    vtrn.8      d5, d6
    vtrn.8      d7, d16
    vtrn.8      d17, d18

    sub         r2, r0, #3
    add         r3, r0, #1

    bl          vp9_mbloop_filter_neon

    ;store op2, op1, op0, oq0
    vst4.8      {d2[0], d3[0], d4[0], d5[0]}, [r2], r1
    vst4.8      {d2[1], d3[1], d4[1], d5[1]}, [r2], r1
    vst4.8      {d2[2], d3[2], d4[2], d5[2]}, [r2], r1
    vst4.8      {d2[3], d3[3], d4[3], d5[3]}, [r2], r1
    vst4.8      {d2[4], d3[4], d4[4], d5[4]}, [r2], r1
    vst4.8      {d2[5], d3[5], d4[5], d5[5]}, [r2], r1
    vst4.8      {d2[6], d3[6], d4[6], d5[6]}, [r2], r1
    vst4.8      {d2[7], d3[7], d4[7], d5[7]}, [r2]

    ;store oq1, oq2
    vst2.8      {d6[0], d7[0]}, [r3], r1
    vst2.8      {d6[1], d7[1]}, [r3], r1
    vst2.8      {d6[2], d7[2]}, [r3], r1
    vst2.8      {d6[3], d7[3]}, [r3], r1
    vst2.8      {d6[4], d7[4]}, [r3], r1
    vst2.8      {d6[5], d7[5]}, [r3], r1
    vst2.8      {d6[6], d7[6]}, [r3], r1
    vst2.8      {d6[7], d7[7]}, [r3]

    add         r0, r0, r1, lsl #3         ; s += pitch * 8
    subs        r12, r12, #1
    bne         count_mblf_v_loop

end_vp9_mblf_v_edge
    pop         {r4-r6, pc}
    ENDP        ; |vp9_mbloop_filter_vertical_edge_neon|

; void vp9_mbloop_filter_neon();
; This is a helper function for the loopfilters. The invidual functions do the
; necessary load, transpose (if necessary) and store. The function does not use
; registers d8-d15.
;
; r0-r3 PRESERVE
; d0    blimit
; d1    limit
; d2    thresh
; d3    p3
; d4    p2
; d5    p1
; d6    p0
; d7    q0
; d16   q1
; d17   q2
; d18   q3
|vp9_mbloop_filter_neon| PROC
    ; filter_mask
    sub         sp, sp, #8                 ; Space for flat & mask
    mov         r6, sp

    vabd.u8     d19, d3, d4                ; abs(p3 - p2)
    vabd.u8     d20, d4, d5                ; abs(p2 - p1)
    vabd.u8     d21, d5, d6                ; abs(p1 - p0)
    vabd.u8     d22, d16, d7               ; abs(q1 - q0)
    vabd.u8     d23, d17, d16              ; abs(q2 - q1)
    vabd.u8     d24, d18, d17              ; abs(q3 - q2)

    ; only compare the largest value to limit
    vmax.u8     d19, d19, d20              ; max(abs(p3 - p2), abs(p2 - p1))
    vmax.u8     d20, d21, d22              ; max(abs(p1 - p0), abs(q1 - q0))
    vmax.u8     d23, d23, d24              ; max(abs(q2 - q1), abs(q3 - q2))
    vmax.u8     d19, d19, d20

    vabd.u8     d24, d6, d7                ; abs(p0 - q0)

    vmax.u8     d19, d19, d23

    vabd.u8     d23, d5, d16               ; a = abs(p1 - q1)
    vqadd.u8    d24, d24, d24              ; b = abs(p0 - q0) * 2

    ; abs () > limit
    vcge.u8     d19, d1, d19

    ; flatmask4
    vabd.u8     d25, d6, d4                ; abs(p0 - p2)
    vabd.u8     d26, d7, d17               ; abs(q0 - q2)
    vabd.u8     d27, d3, d6                ; abs(p3 - p0)
    vabd.u8     d28, d18, d7               ; abs(q3 - q0)

    ; only compare the largest value to thresh
    vmax.u8     d25, d25, d26              ; max(abs(p0 - p2), abs(q0 - q2))
    vmax.u8     d26, d27, d28              ; max(abs(p3 - p0), abs(q3 - q0))
    vmax.u8     d25, d25, d26
    vmax.u8     d20, d20, d25

    vshr.u8     d23, d23, #1               ; a = a / 2
    vqadd.u8    d24, d24, d23              ; a = b + a

    vmov.u8     d23, #1
    vcge.u8     d24, d0, d24               ; a > blimit

    vcge.u8     d20, d23, d20              ; flat

    vand        d19, d19, d24              ; mask

    vand        d20, d20, d19              ; flat & mask

    vst1.u8     {d20}, [r6]                ; flat & mask

    ldrd        r4, r5, [r6]               ; load flat & mask

    and         r6, r4, r5
    adds        r6, #1                     ; Check for all 1's
    beq         power_branch_only

    orrs        r6, r4, r5                 ; Check for 0, set flag for later

    ; hevmask
    vcgt.u8     d21, d21, d2               ; (abs(p1 - p0) > thresh)*-1
    vcgt.u8     d22, d22, d2               ; (abs(q1 - q0) > thresh)*-1
    vorr        d21, d21, d22              ; hev

    vmov.u8     d22, #0x80

    ; mbfilter() function

    ; filter() function
    ; convert to signed
    veor        d23, d7, d22               ; qs0
    veor        d24, d6, d22               ; ps0
    veor        d25, d5, d22               ; ps1
    veor        d26, d16, d22              ; qs1

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

    beq         filter_branch_only

    veor        d24, d24, d22              ; *f_op0 = u^0x80
    veor        d23, d23, d22              ; *f_oq0 = u^0x80
    veor        d25, d25, d22              ; *f_op1 = u^0x80
    veor        d26, d26, d22              ; *f_oq1 = u^0x80

    ; mbfilter flat && mask branch
    ; TODO(fgalligan): Can I decrease the cycles shifting to consective d's
    ; and using vibt on the q's?
    vmov.u8     d21, #2
    vaddl.u8    q14, d6, d7                ; op2 = p0 + q0
    vmlal.u8    q14, d3, d27               ; op2 += p3 * 3
    vmlal.u8    q14, d4, d21               ; op2 += p2 * 2
    vaddw.u8    q14, d5                    ; op2 += p1
    vqrshrn.u16 d30, q14, #3               ; r_op2

    vsubw.u8    q14, d3                    ; op1 = op2 - p3
    vsubw.u8    q14, d4                    ; op1 -= p2
    vaddw.u8    q14, d5                    ; op1 += p1
    vaddw.u8    q14, d16                   ; op1 += q1
    vqrshrn.u16 d31, q14, #3               ; r_op1

    vsubw.u8    q14, d3                    ; op0 = op1 - p3
    vsubw.u8    q14, d5                    ; op0 -= p1
    vaddw.u8    q14, d6                    ; op0 += p0
    vaddw.u8    q14, d17                   ; op0 += q2
    vqrshrn.u16 d21, q14, #3               ; r_op0

    vsubw.u8    q14, d3                    ; oq0 = op0 - p3
    vsubw.u8    q14, d6                    ; oq0 -= p0
    vaddw.u8    q14, d7                    ; oq0 += q0
    vaddw.u8    q14, d18                   ; oq0 += q3
    vqrshrn.u16 d22, q14, #3               ; r_oq0

    vsubw.u8    q14, d4                    ; oq1 = oq0 - p2
    vsubw.u8    q14, d7                    ; oq1 -= q0
    vaddw.u8    q14, d16                   ; oq1 += q1
    vaddw.u8    q14, d18                   ; oq1 += q3
    vqrshrn.u16 d0, q14, #3                ; r_oq1

    vsubw.u8    q14, d5                    ; oq2 = oq0 - p1
    vsubw.u8    q14, d16                   ; oq2 -= q1
    vaddw.u8    q14, d17                   ; oq2 += q2
    vaddw.u8    q14, d18                   ; oq2 += q3
    vqrshrn.u16 d1, q14, #3                ; r_oq2

    ; Filter does not set op2 or oq2, so use p2 and q2.
    vbit        d2, d30, d20               ; op2 |= r_op2 & (flat & mask)
    vbif        d2, d4, d20                ; op2 |= op2 & ~(flat & mask)

    vbit        d3, d31, d20               ; op1 |= r_op1 & (flat & mask)
    vbif        d3, d25, d20               ; op1 |= f_op1 & ~(flat & mask)

    vbit        d4, d21, d20               ; op0 |= r_op0 & (flat & mask)
    vbif        d4, d24, d20               ; op0 |= f_op0 & ~(flat & mask)

    vbit        d5, d22, d20               ; oq0 |= r_oq0 & (flat & mask)
    vbif        d5, d23, d20               ; oq0 |= f_oq0 & ~(flat & mask)

    vbit        d6, d0, d20                ; oq1 |= r_oq1 & (flat & mask)
    vbif        d6, d26, d20               ; oq1 |= f_oq1 & ~(flat & mask)

    vbit        d7, d1, d20                ; oq2 |= r_oq2 & (flat & mask)
    vbif        d7, d17, d20               ; oq2 |= oq2 & ~(flat & mask)

    add         sp, sp, #8                 ; delete space
    bx          lr

power_branch_only
    vmov.u8     d27, #3
    vmov.u8     d21, #2
    vaddl.u8    q14, d6, d7                ; op2 = p0 + q0
    vmlal.u8    q14, d3, d27               ; op2 += p3 * 3
    vmlal.u8    q14, d4, d21               ; op2 += p2 * 2
    vaddw.u8    q14, d5                    ; op2 += p1
    vqrshrn.u16 d2, q14, #3                ; op2

    vsubw.u8    q14, d3                    ; op1 = op2 - p3
    vsubw.u8    q14, d4                    ; op1 -= p2
    vaddw.u8    q14, d5                    ; op1 += p1
    vaddw.u8    q14, d16                   ; op1 += q1
    vqrshrn.u16 d31, q14, #3               ; op1

    vsubw.u8    q14, d3                    ; op0 = op1 - p3
    vsubw.u8    q14, d5                    ; op0 -= p1
    vaddw.u8    q14, d6                    ; op0 += p0
    vaddw.u8    q14, d17                   ; op0 += q2
    vqrshrn.u16 d21, q14, #3               ; op0

    vsubw.u8    q14, d3                    ; oq0 = op0 - p3
    vsubw.u8    q14, d6                    ; oq0 -= p0
    vaddw.u8    q14, d7                    ; oq0 += q0
    vaddw.u8    q14, d18                   ; oq0 += q3
    vqrshrn.u16 d22, q14, #3               ; oq0

    vsubw.u8    q14, d4                    ; oq1 = oq0 - p2
    vsubw.u8    q14, d7                    ; oq1 -= q0
    vaddw.u8    q14, d16                   ; oq1 += q1
    vaddw.u8    q14, d18                   ; oq1 += q3
    vqrshrn.u16 d6, q14, #3                ; oq1

    vsubw.u8    q14, d5                    ; oq2 = oq0 - p1
    vsubw.u8    q14, d16                   ; oq2 -= q1
    vaddw.u8    q14, d17                   ; oq2 += q2
    vaddw.u8    q14, d18                   ; oq2 += q3
    vqrshrn.u16 d7, q14, #3                ; oq2

    vswp        d3, d31
    vswp        d4, d21
    vswp        d5, d22

    add         sp, sp, #8                  ; delete space
    bx          lr

filter_branch_only
    ; TODO(fgalligan): See if we can rearange registers so we do not need to
    ; do the 2 vswp.
    vswp        d2, d4                      ; op2
    vswp        d7, d17                     ; oq2
    veor        d4, d24, d22                ; *op0 = u^0x80
    veor        d5, d23, d22                ; *oq0 = u^0x80
    veor        d3, d25, d22                ; *op1 = u^0x80
    veor        d6, d26, d22                ; *oq1 = u^0x80

    add         sp, sp, #8                  ; delete space
    bx          lr

    ENDP        ; |vp9_mbloop_filter_neon|

    END
