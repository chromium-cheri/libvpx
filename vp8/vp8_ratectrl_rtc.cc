/*
 *  Copyright (c) 2021 The WebM project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include <new>
#include "vp8/vp8_ratectrl_rtc.h"
#include "vp8/encoder/ratectrl.h"

namespace libvpx {

static const unsigned char kf_high_motion_minq[QINDEX_RANGE] = {
  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  1,  1,  1,  1,  1,  1,
  1,  1,  2,  2,  2,  2,  3,  3,  3,  3,  3,  3,  3,  3,  4,  4,  4,  4,  5,
  5,  5,  5,  5,  5,  6,  6,  6,  6,  7,  7,  8,  8,  8,  8,  9,  9,  10, 10,
  10, 10, 11, 11, 11, 11, 12, 12, 13, 13, 13, 13, 14, 14, 15, 15, 15, 15, 16,
  16, 16, 16, 17, 17, 18, 18, 18, 18, 19, 19, 20, 20, 20, 20, 21, 21, 21, 21,
  22, 22, 23, 23, 24, 25, 25, 26, 26, 27, 28, 28, 29, 30
};

static const unsigned char gf_high_motion_minq[QINDEX_RANGE] = {
  0,  0,  0,  0,  1,  1,  1,  1,  1,  2,  2,  2,  3,  3,  3,  4,  4,  4,  5,
  5,  5,  6,  6,  6,  7,  7,  7,  8,  8,  8,  9,  9,  9,  10, 10, 10, 11, 11,
  12, 12, 13, 13, 14, 14, 15, 15, 16, 16, 17, 17, 18, 18, 19, 19, 20, 20, 21,
  21, 22, 22, 23, 23, 24, 24, 25, 25, 26, 26, 27, 27, 28, 28, 29, 29, 30, 30,
  31, 31, 32, 32, 33, 33, 34, 34, 35, 35, 36, 36, 37, 37, 38, 38, 39, 39, 40,
  40, 41, 41, 42, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, 52, 53, 54, 55, 56,
  57, 58, 59, 60, 62, 64, 66, 68, 70, 72, 74, 76, 78, 80
};

static const unsigned char inter_minq[QINDEX_RANGE] = {
  0,  0,  1,  1,  2,  3,  3,  4,  4,  5,  6,  6,  7,  8,  8,  9,  9,  10, 11,
  11, 12, 13, 13, 14, 15, 15, 16, 17, 17, 18, 19, 20, 20, 21, 22, 22, 23, 24,
  24, 25, 26, 27, 27, 28, 29, 30, 30, 31, 32, 33, 33, 34, 35, 36, 36, 37, 38,
  39, 39, 40, 41, 42, 42, 43, 44, 45, 46, 46, 47, 48, 49, 50, 50, 51, 52, 53,
  54, 55, 55, 56, 57, 58, 59, 60, 60, 61, 62, 63, 64, 65, 66, 67, 67, 68, 69,
  70, 71, 72, 73, 74, 75, 75, 76, 77, 78, 79, 80, 81, 82, 83, 84, 85, 86, 86,
  87, 88, 89, 90, 91, 92, 93, 94, 95, 96, 97, 98, 99, 100
};

std::unique_ptr<VP8RateControlRTC> VP8RateControlRTC::Create(
    const VP8RateControlRtcConfig &cfg) {
  std::unique_ptr<VP8RateControlRTC> rc_api(new (std::nothrow)
                                                VP8RateControlRTC());
  if (!rc_api) return nullptr;
  rc_api->cpi_ = static_cast<VP8_COMP *>(vpx_memalign(32, sizeof(*cpi_)));
  if (!rc_api->cpi_) return nullptr;
  vp8_zero(*rc_api->cpi_);

  rc_api->InitRateControl(cfg);

  rc_api->cpi_->gf_active_count =
      rc_api->cpi_->common.mb_rows * rc_api->cpi_->common.mb_cols;
  rc_api->cpi_->gf_active_flags = static_cast<unsigned char *>(vpx_calloc(
      sizeof(*rc_api->cpi_->gf_active_flags), rc_api->cpi_->gf_active_count));

  return rc_api;
}

void VP8RateControlRTC::InitRateControl(const VP8RateControlRtcConfig &rc_cfg) {
  VP8_COMMON *cm = &cpi_->common;
  VP8_CONFIG *oxcf = &cpi_->oxcf;
  oxcf->end_usage = USAGE_STREAM_FROM_SERVER;
  cm->show_frame = 1;
  oxcf->drop_frames_water_mark = 0;
  cm->current_video_frame = 0;
  cpi_->auto_gold = 1;
  cpi_->key_frame_count = 1;
  cpi_->rate_correction_factor = 1.0;
  cpi_->key_frame_rate_correction_factor = 1.0;
  cpi_->cyclic_refresh_mode_enabled = 0;
  cpi_->auto_worst_q = 1;
  UpdateRateControl(rc_cfg);
}

void VP8RateControlRTC::UpdateRateControl(
    const VP8RateControlRtcConfig &rc_cfg) {
  VP8_COMMON *cm = &cpi_->common;
  VP8_CONFIG *oxcf = &cpi_->oxcf;

  cm->Width = rc_cfg.width;
  cm->Height = rc_cfg.height;
  oxcf->Width = rc_cfg.width;
  oxcf->Height = rc_cfg.height;
  oxcf->worst_allowed_q = kQTrans[rc_cfg.max_quantizer];
  oxcf->best_allowed_q = kQTrans[rc_cfg.min_quantizer];
  cpi_->worst_quality = oxcf->worst_allowed_q;
  cpi_->best_quality = oxcf->best_allowed_q;
  cpi_->output_framerate = rc_cfg.framerate;
  oxcf->target_bandwidth = 1000 * rc_cfg.target_bandwidth;
  oxcf->fixed_q = -1;
  oxcf->error_resilient_mode = 1;
  oxcf->starting_buffer_level_in_ms = rc_cfg.buf_initial_sz;
  oxcf->optimal_buffer_level_in_ms = rc_cfg.buf_optimal_sz;
  oxcf->maximum_buffer_size_in_ms = rc_cfg.buf_sz;
  oxcf->starting_buffer_level = 1000 * rc_cfg.buf_initial_sz;
  oxcf->optimal_buffer_level = 1000 * rc_cfg.buf_optimal_sz;
  oxcf->maximum_buffer_size = 1000 * rc_cfg.buf_sz;
  cpi_->buffered_mode = oxcf->optimal_buffer_level > 0;
  oxcf->under_shoot_pct = rc_cfg.undershoot_pct;
  oxcf->over_shoot_pct = rc_cfg.overshoot_pct;
  cpi_->oxcf.rc_max_intra_bitrate_pct = rc_cfg.max_intra_bitrate_pct;
  cpi_->framerate = rc_cfg.framerate;

  /* Initialise the starting buffer levels */
  cpi_->buffer_level = oxcf->starting_buffer_level;
  cpi_->bits_off_target = oxcf->starting_buffer_level;
  cpi_->total_actual_bits = 0;
  cpi_->total_target_vs_actual = 0;

  cm->mb_rows = cm->Height >> 4;
  cm->mb_cols = cm->Width >> 4;
  cm->MBs = cm->mb_rows * cm->mb_cols;
  cm->mode_info_stride = cm->mb_cols + 1;
  vp8_new_framerate(cpi_, cpi_->framerate);
}

void VP8RateControlRTC::ComputeQP(const VP8FrameParamsQpRTC &frame_params) {
  VP8_COMMON *const cm = &cpi_->common;
  cm->frame_type = frame_params.frame_type;
  cm->refresh_golden_frame = (cm->frame_type == KEY_FRAME) ? 1 : 0;
  cm->refresh_alt_ref_frame = (cm->frame_type == KEY_FRAME) ? 1 : 0;

  vp8_pick_frame_size(cpi_);

  if (cpi_->buffer_level >= cpi_->oxcf.optimal_buffer_level &&
      cpi_->buffered_mode) {
    /* Max adjustment is 1/4 */
    int Adjustment = cpi_->active_worst_quality / 4;
    if (Adjustment) {
      int buff_lvl_step;
      if (cpi_->buffer_level < cpi_->oxcf.maximum_buffer_size) {
        buff_lvl_step = (int)((cpi_->oxcf.maximum_buffer_size -
                               cpi_->oxcf.optimal_buffer_level) /
                              Adjustment);
        if (buff_lvl_step) {
          Adjustment =
              (int)((cpi_->buffer_level - cpi_->oxcf.optimal_buffer_level) /
                    buff_lvl_step);
        } else {
          Adjustment = 0;
        }
      }
      cpi_->active_worst_quality -= Adjustment;
      if (cpi_->active_worst_quality < cpi_->active_best_quality) {
        cpi_->active_worst_quality = cpi_->active_best_quality;
      }
    }
  }

  if (cpi_->ni_frames > 150) {
    int q = cpi_->active_worst_quality;
    if (cm->frame_type == KEY_FRAME) {
      cpi_->active_best_quality = kf_high_motion_minq[q];
    } else if (cpi_->oxcf.number_of_layers == 1 &&
               (cm->refresh_golden_frame ||
                cpi_->common.refresh_alt_ref_frame)) {
      /* Use the lower of cpi->active_worst_quality and recent
       * average Q as basis for GF/ARF Q limit unless last frame was
       * a key frame.
       */
      if ((cpi_->frames_since_key > 1) &&
          (cpi_->avg_frame_qindex < cpi_->active_worst_quality)) {
        q = cpi_->avg_frame_qindex;
      }
      cpi_->active_best_quality = gf_high_motion_minq[q];
    } else {
      cpi_->active_best_quality = inter_minq[q];
    }
  }

  /* Clip the active best and worst quality values to limits */
  if (cpi_->active_worst_quality > cpi_->worst_quality) {
    cpi_->active_worst_quality = cpi_->worst_quality;
  }
  if (cpi_->active_best_quality < cpi_->best_quality) {
    cpi_->active_best_quality = cpi_->best_quality;
  }
  if (cpi_->active_worst_quality < cpi_->active_best_quality) {
    cpi_->active_worst_quality = cpi_->active_best_quality;
  }

  q_ = vp8_regulate_q(cpi_, cpi_->this_frame_target);
  vp8_set_quantizer(cpi_, q_);
  vp8_compute_frame_size_bounds(cpi_, &frame_under_shoot_limit_,
                                &frame_over_shoot_limit_);
}

int VP8RateControlRTC::GetQP() const { return q_; }

void VP8RateControlRTC::PostEncodeUpdate(uint64_t encoded_frame_size) {
  VP8_COMMON *const cm = &cpi_->common;
  int active_worst_qchanged = 0;
  if (frame_over_shoot_limit_ == 0) frame_over_shoot_limit_ = 1;

  if (q_ == cpi_->active_worst_quality &&
      cpi_->active_worst_quality < cpi_->worst_quality &&
      cpi_->projected_frame_size > frame_over_shoot_limit_) {
    int over_size_percent =
        ((cpi_->projected_frame_size - frame_over_shoot_limit_) * 100) /
        frame_over_shoot_limit_;

    /* If so is there any scope for relaxing it */
    while ((cpi_->active_worst_quality < cpi_->worst_quality) &&
           (over_size_percent > 0)) {
      cpi_->active_worst_quality++;
      /* Assume 1 qstep = about 4% on frame size. */
      over_size_percent = (int)(over_size_percent * 0.96);
    }
    /* If we have updated the active max Q do not call
     * vp8_update_rate_correction_factors() this loop.
     */
    active_worst_qchanged = 1;
  } else {
    active_worst_qchanged = 0;
  }

  cpi_->total_byte_count += encoded_frame_size;
  cpi_->projected_frame_size = (int)(encoded_frame_size << 3);

  if (!active_worst_qchanged) vp8_update_rate_correction_factors(cpi_, 2);

  cpi_->last_q[cm->frame_type] = cm->base_qindex;

  if (cm->frame_type == KEY_FRAME) {
    vp8_adjust_key_frame_context(cpi_);
  }

  /* Keep a record of ambient average Q. */
  if (cm->frame_type != KEY_FRAME) {
    cpi_->avg_frame_qindex =
        (2 + 3 * cpi_->avg_frame_qindex + cm->base_qindex) >> 2;
  }
  /* Keep a record from which we can calculate the average Q excluding
   * GF updates and key frames
   */
  if ((cm->frame_type != KEY_FRAME) &&
      ((cpi_->oxcf.number_of_layers > 1) ||
       (!cm->refresh_golden_frame && !cm->refresh_alt_ref_frame))) {
    cpi_->ni_frames++;
    /* Damp value for first few frames */
    if (cpi_->ni_frames > 150) {
      cpi_->ni_tot_qi += q_;
      cpi_->ni_av_qi = (cpi_->ni_tot_qi / cpi_->ni_frames);
    } else {
      cpi_->ni_tot_qi += q_;
      cpi_->ni_av_qi =
          ((cpi_->ni_tot_qi / cpi_->ni_frames) + cpi_->worst_quality + 1) / 2;
    }

    /* If the average Q is higher than what was used in the last
     * frame (after going through the recode loop to keep the frame
     * size within range) then use the last frame value - 1. The -1
     * is designed to stop Q and hence the data rate, from
     * progressively falling away during difficult sections, but at
     * the same time reduce the number of itterations around the
     * recode loop.
     */
    if (q_ > cpi_->ni_av_qi) cpi_->ni_av_qi = q_ - 1;
  }

  cpi_->bits_off_target +=
      cpi_->av_per_frame_bandwidth - cpi_->projected_frame_size;

  if (cpi_->bits_off_target > cpi_->oxcf.maximum_buffer_size) {
    cpi_->bits_off_target = cpi_->oxcf.maximum_buffer_size;
  }

  cpi_->total_actual_bits += cpi_->projected_frame_size;
  cpi_->buffer_level = cpi_->bits_off_target;

  if (cm->refresh_alt_ref_frame && cm->frame_type != KEY_FRAME) {
    /* Update the alternate reference frame stats as appropriate. */
    vp8_update_alt_ref_frame_stats(cpi_);
  } else {
    /* Update the Golden frame stats as appropriate. */
    vp8_update_golden_frame_stats(cpi_);
  }

  cpi_->common.current_video_frame++;
  cpi_->frames_since_key++;
}
}  // namespace libvpx
