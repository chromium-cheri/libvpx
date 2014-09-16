/*
 *  Copyright (c) 2012 The WebM project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#ifndef VP9_ENCODER_DENOISER_H_
#define VP9_ENCODER_DENOISER_H_

#include "vp9/encoder/vp9_block.h"
#include "vpx_scale/yv12config.h"

#ifdef __cplusplus
extern "C" {
#endif

#define MOTION_MAGNITUDE_THRESHOLD (8 * 3)
#define SUM_DIFF_THRESHOLD (16 * 16 * 2)
#define SUM_DIFF_THRESHOLD_HIGH (600)


typedef enum vp9_denoiser_decision {
  COPY_BLOCK,
  FILTER_BLOCK
} VP9_DENOISER_DECISION;

typedef struct vp9_denoiser {
  YV12_BUFFER_CONFIG running_avg_y[MAX_REF_FRAMES];
  YV12_BUFFER_CONFIG mc_running_avg_y;
  int increase_denoising;
} VP9_DENOISER;

void vp9_denoiser_update_frame_info(VP9_DENOISER *denoiser,
                                    YV12_BUFFER_CONFIG src,
                                    FRAME_TYPE frame_type,
                                    int refresh_alt_ref_frame,
                                    int refresh_golden_frame,
                                    int refresh_last_frame);

void vp9_denoiser_denoise(VP9_DENOISER *denoiser, MACROBLOCK *mb,
                          int mi_row, int mi_col, BLOCK_SIZE bs,
                          PICK_MODE_CONTEXT *ctx);

void vp9_denoiser_reset_frame_stats(PICK_MODE_CONTEXT *ctx);

void vp9_denoiser_update_frame_stats(MB_MODE_INFO *mbmi,
                                     unsigned int sse, PREDICTION_MODE mode,
                                     PICK_MODE_CONTEXT *ctx);

int vp9_denoiser_alloc(VP9_DENOISER *denoiser, int width, int height,
                       int ssx, int ssy,
#if CONFIG_VP9_HIGHBITDEPTH
                       int use_highbitdepth,
#endif
                       int border);

void vp9_denoiser_free(VP9_DENOISER *denoiser);

// SSE version of vp9 denoiser for block 16x16
int vp9_denoiser_16x16_sse2(unsigned char *mc_running_avg_y,
                             int mc_avg_y_stride,
                             unsigned char *running_avg_y, int avg_y_stride,
                             unsigned char *sig, int sig_stride,
                             unsigned int motion_magnitude,
                             int increase_denoising);


#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // VP9_ENCODER_DENOISER_H_
