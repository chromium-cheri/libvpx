;
;  Copyright (c) 2010 The WebM project authors. All Rights Reserved.
;
;  Use of this source code is governed by a BSD-style license
;  that can be found in the LICENSE file in the root of the source
;  tree. An additional intellectual property rights grant can be found
;  in the file PATENTS.  All contributing project authors may
;  be found in the AUTHORS file in the root of the source tree.
;

    EXPORT |vp8_denoiser_filter_neon|

.equ sum_diff_threshold, (16 * 16 * 2)
.equ sum_diff_threshold_high, (16 * 16 * 3)
.equ motion_magnitude_threshold, (8*3)
.equ copy_block, 0
.equ filter_block, 1

    ARM
    REQUIRE8
    PRESERVE8

    AREA ||.text||, CODE, READONLY, ALIGN=2

;int vp8_denoiser_filter_neon(unsigned char *mc_running_avg_y,
;                              int mc_running_avg_y_stride,
;                              unsigned char *running_avg_y,
;                              int running_avg_y_stride,
;                              unsigned char *sig, int sig_stride,
;                              unsigned int level1_adjustment,
;                              int increase_denoising);

|vp8_denoiser_filter_neon| PROC

    push            {r4-r8}
    
    ldr             r4, [sp, #20]           ;sig
    ldr             r5, [sp, #24]           ;sig_stride
    ldr             r6, [sp, #28]           ;motion_magnitude
    ldr             r8, [sp, #32]           ;increase_denoising

    cmp             r6, #motion_magnitude_threshold
    movhi           r7, #0
    movls           r7, #1
    and             r8, r8, r7
    add             r8, r8, #4

    mov             r7, #1
    vdup.8          q14, r7
    mov             r7, #2
    vdup.8          q13, r7
;    mov             r7, #4
;    vdup.8          q12, r7
    vdup.8          q12, r8
    mov             r7, #8
    vdup.8          q11, r7
    mov             r7, #16
    vdup.8          q10, r7
    
    cmp             r6, #motion_magnitude_threshold
    movhi           r6, #3
    movls           r6, r8 ;#4
    vdup.8          q15, r6                 ;v_level1_adjustment

    mov             r6, #16
    veor.u8         q0, q0, q0              ;v_sum_diff
    ldr             r8, [sp, #32]           ;increase_denoising

denoising_loop
    vld1.8          {q1}, [r4], r5          ;load sig
    vld1.8          {q2}, [r0], r1          ;load mc_running_avg_y

    ;/* Calculate absolute difference and sign masks. */
    vabd.u8         q3, q1, q2              ;v_abs_diff
    vcgt.u8         q9, q2, q1              ;v_diff_pos_mask
    vcgt.u8         q8, q1, q2              ;v_diff_neg_mask

    ;/* Figure out which level that put us in. */
    vcge.u8         q7, q3, q12             ;v_level1_mask
    vcge.u8         q6, q3, q11             ;v_level2_mask
    vcge.u8         q5, q3, q10             ;v_level3_mask

    ;/* Calculate absolute adjustments for level 1, 2 and 3. */
    vand            q6, q6, q14             ;v_level2_adjustment
    vand            q5, q5, q13             ;v_level3_adjustment

    vqadd.u8        q6, q15, q6             ;v_level1and2_adjustment

    vqadd.u8        q5, q6, q5              ;v_level1and2and3_adjustment

    ;/* Figure adjustment absolute value by selecting between the absolute
    ; * difference if in level0 or the value for level 1, 2 and 3.
    ; */
    vbsl            q7, q5, q3              ;v_abs_adjustment
    ;--

    ;/* Calculate positive and negative adjustments. Apply them to the signal
    ; * and accumulate them. Adjustments are less than eight and the maximum
    ; * sum of them (7 * 16) can fit in a signed char.
    ; */
    vand            q9, q9, q7              ;v_pos_adjustment aka v_sum_diff
    vand            q8, q8, q7              ;v_neg_adjustment
    
    vqadd.u8        q1, q1, q9              ;v_running_avg_y
    vqsub.u8        q1, q1, q8              ;v_running_avg_y
    
    vqsub.s8        q2, q9, q8              ;v_sum_diff
    
    vst1.8          {q1}, [r2], r3          ;running_avg_y

    ;/* Sum all the accumulators to have the sum of all pixel differences
    ; * for this macroblock.
    ; */
    vpaddl.s8       q2, q2
    vpaddl.s16      q2, q2
    vpaddl.s32      q2, q2
    subs            r6, r6, #1

    vqadd.s32       q0, q2                  ;sum_diff
    
    bne             denoising_loop

    vqadd.s64       d0, d0, d1              ;finish summing differences
    vabs.s32        d0, d0                  ;abs(sum_diff)
    vmov.s32        r6, d0[0]

    cmp             r8, #0                  ;check increase_denoising
    movhi           r7, #sum_diff_threshold_high
    movls           r7, #sum_diff_threshold
    sub             r0, r0, r1, lsl #4      ;reset mc_running_avg_y
    sub             r2, r2, r3, lsl #4      ;reset running_avg_y
    sub             r4, r4, r5, lsl #4      ;reset sig
    cmp             r6, r7
    bgt             denoising_weaker

    mov             r6, #4
denoising_copy
    vld1.8          {q0}, [r2], r3          ;load running_avg_y
    vld1.8          {q1}, [r2], r3
    vld1.8          {q2}, [r2], r3
    vld1.8          {q3}, [r2], r3
    subs            r6, r6, #1
    vst1.8          {q0}, [r4], r5          ;store sig
    vst1.8          {q1}, [r4], r5
    vst1.8          {q2}, [r4], r5
    vst1.8          {q3}, [r4], r5
    bne             denoising_copy

    mov             r0, #filter_block

    b               denoising_done

denoising_weaker
    sub             r8, r6, r7, LSR #8
    add             r8, r8, #1              ;aka delta
    cmp             r8, #4
    bge             denoising_ret_copy_block

    vdup.8          q15, r8                 ;k_delta
    mov             r6, #16
denoising_loop_weak
    vld1.8          {q1}, [r4], r5          ;load sig
    vld1.8          {q2}, [r0], r1          ;load mc_running_avg_y
    vld1.8          {q10}, [r2]             ;load running_avg_y

    ;/* Calculate absolute difference and sign masks. */
    vabd.u8         q3, q1, q2              ;v_abs_diff
    vcgt.u8         q9, q2, q1              ;v_diff_pos_mask
    vcgt.u8         q8, q1, q2              ;v_diff_neg_mask

    ;// Clamp absolute difference to delta to get the adjustment.
    vmin.u8         q7, q3, q15            ;v_abs_adjustment

    ;/* Calculate positive and negative adjustments. Apply them to the signal
    ; * and accumulate them. Adjustments are less than eight and the maximum
    ; * sum of them (7 * 16) can fit in a signed char.
    ; */
    vand            q9, q9, q7              ;v_pos_adjustment
    vand            q8, q8, q7              ;v_neg_adjustment aka v_sum_diff

    vqsub.u8        q10, q10, q9            ;v_running_avg_y
    vqadd.u8        q10, q10, q8            ;v_running_avg_y

    vqsub.s8        q2, q8, q9              ;v_sum_diff

    vst1.8          {q10}, [r2], r3         ;running_avg_y

    ;/* Sum all the accumulators to have the sum of all pixel differences
    ; * for this macroblock.
    ; */
    subs            r6, r6, #1
    vpaddl.s8       q2, q2
    vpaddl.s16      q2, q2
    vpaddl.s32      q2, q2
    vqadd.s32       q0, q2                  ;sum_diff
    bne             denoising_loop_weak

    vqadd.s64       d0, d0, d1              ;finish summing differences
    vabs.s32        d0, d0                  ;abs(sum_diff)
    vmov.s32        r1, d0[0]

    cmp             r1, r7
    mov             r6, #4
    sub             r2, r2, r3, lsl #4      ;reset running_avg_y
    sub             r4, r4, r5, lsl #4      ;reset sig
    ble             denoising_copy

denoising_ret_copy_block
    mov             r0, #copy_block

denoising_done
    pop             {r4-r8}
    bx              lr
    ENDP


    END
