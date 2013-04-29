/*
 *  Copyright (c) 2010 The WebM project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */


#include "./vpx_config.h"
#include "vp9/common/vp9_blockd.h"
#include "vpx_mem/vpx_mem.h"
#include "vp9/common/vp9_onyxc_int.h"
#include "vp9/common/vp9_findnearmv.h"
#include "vp9/common/vp9_entropymode.h"
#include "vp9/common/vp9_entropymv.h"
#include "vp9/common/vp9_systemdependent.h"


void vp9_update_mode_info_border(VP9_COMMON *cpi, MODE_INFO *mi) {
  const int stride = cpi->mode_info_stride;
  int i;

  // Clear down top border row
  vpx_memset(mi, 0, sizeof(MODE_INFO) * stride);

  // Clear left border column
  for (i = 1; i < cpi->mi_rows + 1; i++)
    vpx_memset(&mi[i * stride], 0, sizeof(MODE_INFO));
}

void vp9_update_mode_info_in_image(VP9_COMMON *cpi, MODE_INFO *mi) {
  int i, j;
  MODE_INFO *ptr;

  // For each in image mode_info element set the in image flag to 1
  for (i = 0; i < cpi->mi_rows; i++) {
    ptr = mi;
    for (j = 0; j < cpi->mi_cols; j++) {
      ptr->mbmi.mb_in_image = 1;
      ptr++;  // Next element in the row
    }

    // Step over border element at start of next row
    mi += cpi->mode_info_stride;
  }
}

void vp9_free_frame_buffers(VP9_COMMON *oci) {
  int i;

  for (i = 0; i < NUM_YV12_BUFFERS; i++)
    vp8_yv12_de_alloc_frame_buffer(&oci->yv12_fb[i]);

  vp8_yv12_de_alloc_frame_buffer(&oci->temp_scale_frame);
  vp8_yv12_de_alloc_frame_buffer(&oci->post_proc_buffer);

  vpx_free(oci->mip);
  vpx_free(oci->prev_mip);
  vpx_free(oci->above_seg_context);

  vpx_free(oci->above_context[0]);
  for (i = 0; i < MAX_MB_PLANE; i++)
    oci->above_context[i] = 0;
  oci->mip = 0;
  oci->prev_mip = 0;
  oci->above_seg_context = 0;
}

int vp9_alloc_frame_buffers(VP9_COMMON *oci, int width, int height) {
  int i, mb_cols;

  // Our internal buffers are always multiples of 16
  const int aligned_width = multiple16(width);
  const int aligned_height = multiple16(height);

  vp9_free_frame_buffers(oci);

  for (i = 0; i < NUM_YV12_BUFFERS; i++) {
    oci->fb_idx_ref_cnt[i] = 0;
    if (vp8_yv12_alloc_frame_buffer(&oci->yv12_fb[i], width, height,
                                    VP9BORDERINPIXELS) < 0) {
      vp9_free_frame_buffers(oci);
      return 1;
    }
  }

  oci->new_fb_idx = NUM_YV12_BUFFERS - 1;
  oci->fb_idx_ref_cnt[oci->new_fb_idx] = 1;

  for (i = 0; i < ALLOWED_REFS_PER_FRAME; i++)
    oci->active_ref_idx[i] = i;

  for (i = 0; i < NUM_REF_FRAMES; i++) {
    oci->ref_frame_map[i] = i;
    oci->fb_idx_ref_cnt[i] = 1;
  }

  if (vp8_yv12_alloc_frame_buffer(&oci->temp_scale_frame, width, 16,
                                  VP9BORDERINPIXELS) < 0) {
    vp9_free_frame_buffers(oci);
    return 1;
  }

  if (vp8_yv12_alloc_frame_buffer(&oci->post_proc_buffer, width, height,
                                  VP9BORDERINPIXELS) < 0) {
    vp9_free_frame_buffers(oci);
    return 1;
  }

  oci->mb_rows = aligned_height >> 4;
  oci->mi_rows = aligned_height >> LOG2_MI_SIZE;
  oci->mb_cols = aligned_width >> 4;
  oci->mi_cols = aligned_width >> LOG2_MI_SIZE;
  oci->MBs = oci->mb_rows * oci->mb_cols;
  oci->mode_info_stride = oci->mi_cols + 1;
  oci->mip = vpx_calloc(oci->mode_info_stride * (oci->mi_rows + 1),
                        sizeof(MODE_INFO));

  if (!oci->mip) {
    vp9_free_frame_buffers(oci);
    return 1;
  }

  oci->mi = oci->mip + oci->mode_info_stride + 1;

  /* allocate memory for last frame MODE_INFO array */

  oci->prev_mip = vpx_calloc(oci->mode_info_stride * (oci->mi_rows + 1),
                             sizeof(MODE_INFO));

  if (!oci->prev_mip) {
    vp9_free_frame_buffers(oci);
    return 1;
  }

  oci->prev_mi = oci->prev_mip + oci->mode_info_stride + 1;

  // FIXME(jkoleszar): allocate subsampled arrays for U/V once subsampling
  // information is exposed at this level
  mb_cols = mb_cols_aligned_to_sb(oci);
  oci->above_context[0] = vpx_calloc(sizeof(ENTROPY_CONTEXT) * 12 * mb_cols, 1);
  if (!oci->above_context[0]) {
    vp9_free_frame_buffers(oci);
    return 1;
  }
  oci->above_context[1] =
    oci->above_context[0] + sizeof(ENTROPY_CONTEXT) * 4 * mb_cols;
  oci->above_context[2] =
    oci->above_context[1] + sizeof(ENTROPY_CONTEXT) * 4 * mb_cols;

  oci->above_seg_context =
    vpx_calloc(sizeof(PARTITION_CONTEXT) * mb_cols_aligned_to_sb(oci), 1);

  if (!oci->above_seg_context) {
    vp9_free_frame_buffers(oci);
    return 1;
  }

  vp9_update_mode_info_border(oci, oci->mip);
  vp9_update_mode_info_in_image(oci, oci->mi);

  return 0;
}

void vp9_setup_version(VP9_COMMON *cm) {
  if (cm->version & 0x4) {
    if (!CONFIG_EXPERIMENTAL)
      vpx_internal_error(&cm->error, VPX_CODEC_UNSUP_BITSTREAM,
                         "Bitstream was created by an experimental "
                         "encoder");
    cm->experimental = 1;
  }

  switch (cm->version & 0x3) {
    case 0:
      cm->no_lpf = 0;
      cm->filter_type = NORMAL_LOOPFILTER;
      cm->use_bilinear_mc_filter = 0;
      break;
    case 1:
      cm->no_lpf = 0;
      cm->filter_type = SIMPLE_LOOPFILTER;
      cm->use_bilinear_mc_filter = 1;
      break;
    case 2:
    case 3:
      cm->no_lpf = 1;
      cm->filter_type = NORMAL_LOOPFILTER;
      cm->use_bilinear_mc_filter = 1;
      break;
  }
}
void vp9_create_common(VP9_COMMON *oci) {
  vp9_machine_specific_config(oci);

  vp9_init_mbmode_probs(oci);

  vp9_default_bmode_probs(oci->fc.bmode_prob);

  oci->txfm_mode = ONLY_4X4;
  oci->comp_pred_mode = HYBRID_PREDICTION;
  oci->no_lpf = 0;
  oci->filter_type = NORMAL_LOOPFILTER;
  oci->use_bilinear_mc_filter = 0;
  oci->clr_type = REG_YUV;
  oci->clamp_type = RECON_CLAMP_REQUIRED;

  // Initialize reference frame sign bias structure to defaults
  vpx_memset(oci->ref_frame_sign_bias, 0, sizeof(oci->ref_frame_sign_bias));

  oci->kf_ymode_probs_update = 0;
}

void vp9_remove_common(VP9_COMMON *oci) {
  vp9_free_frame_buffers(oci);
}

void vp9_initialize_common() {
  vp9_coef_tree_initialize();
  vp9_entropy_mode_init();
  vp9_entropy_mv_init();
}
