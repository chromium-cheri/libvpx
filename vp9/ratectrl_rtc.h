/*
 *  Copyright (c) 2020 The WebM project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#ifndef VPX_VP9_RATECTRL_RTC_H_
#define VPX_VP9_RATECTRL_RTC_H_

#include <cstdint>
#include <memory>

#include "vp9/common/vp9_entropymode.h"
#include "vp9/common/vp9_enums.h"
#include "vp9/common/vp9_onyxc_int.h"
#include "vp9/vp9_iface_common.h"
#include "vp9/encoder/vp9_aq_cyclicrefresh.h"
#include "vp9/encoder/vp9_encoder.h"
#include "vp9/encoder/vp9_firstpass.h"
#include "vp9/vp9_cx_iface.h"
#include "vpx_mem/vpx_mem.h"

namespace libvpx {

struct VP9RateControlRtcConfig {
 public:
  VP9RateControlRtcConfig() {
    width = 1280;
    height = 720;
    max_quantizer = 63;
    min_quantizer = 2;
    target_bandwidth = 1000;
    buf_initial_sz = 600;
    buf_optimal_sz = 600;
    buf_sz = 1000;
    undershoot_pct = overshoot_pct = 50;
    max_intra_bitrate_pct = 50;
    max_inter_bitrate_pct = 0;
    framerate = 30.0;
    ss_number_layers = ts_number_layers = 1;
    rc_mode = VPX_CBR;
    aq_mode = 0;
    vp9_zero(max_quantizers);
    vp9_zero(min_quantizers);
    vp9_zero(scaling_factor_den);
    vp9_zero(scaling_factor_num);
    vp9_zero(layer_target_bitrate);
    vp9_zero(ts_rate_decimator);
    scaling_factor_num[0] = 1;
    scaling_factor_den[0] = 1;
    layer_target_bitrate[0] = target_bandwidth;
    max_quantizers[0] = max_quantizer;
    min_quantizers[0] = min_quantizer;
    ts_rate_decimator[0] = 1;
  }

  int width;
  int height;
  // 0-63
  int max_quantizer;
  int min_quantizer;
  int64_t target_bandwidth;
  int64_t buf_initial_sz;
  int64_t buf_optimal_sz;
  int64_t buf_sz;
  int undershoot_pct;
  int overshoot_pct;
  int max_intra_bitrate_pct;
  int max_inter_bitrate_pct;
  double framerate;
  // Number of spatial layers
  int ss_number_layers;
  // Number of temporal layers
  int ts_number_layers;
  int max_quantizers[VPX_MAX_LAYERS];
  int min_quantizers[VPX_MAX_LAYERS];
  int scaling_factor_num[VPX_SS_MAX_LAYERS];
  int scaling_factor_den[VPX_SS_MAX_LAYERS];
  int layer_target_bitrate[VPX_MAX_LAYERS];
  int ts_rate_decimator[VPX_TS_MAX_LAYERS];
  // vbr, cbr
  enum vpx_rc_mode rc_mode;
  int aq_mode;
};

struct VP9FrameParamsQpRTC {
  FRAME_TYPE frame_type;
  int spatial_layer_id;
  int temporal_layer_id;
};

// This interface allows using VP9 real-time rate control without initializing
// the encoder. To use this interface, you need to link with libvp9rc.a.
//
// #include "vp9/ratectrl_rtc.h"
// VP9RateControlRTC rc_api;
// VP9RateControlRtcConfig cfg;
// VP9FrameParamsQpRTC frame_params;
//
// YourFunctionToInitializeConfig(cfg);
// rc_api.InitRateControl(cfg);
// // start encoding
// while (frame_to_encode) {
//   if (config_changed)
//     rc_api.UpdateRateControl(cfg);
//   YourFunctionToFillFrameParams(frame_params);
//   rc_api.ComputeQP(frame_params);
//   YourFunctionToUseQP(rc_api.GetQP());
//   YourFunctionToUseLoopfilter(rc_api.GetLoopfilterLevel());
//   // After encoding
//   rc_api.PostEncode(encoded_frame_size);
// }
class VP9RateControlRTC {
 public:
  static std::unique_ptr<VP9RateControlRTC> Create(
      const VP9RateControlRtcConfig &cfg);
  ~VP9RateControlRTC() {
    if (cpi_) {
      for (int sl = 0; sl < cpi_->svc.number_spatial_layers; sl++) {
        for (int tl = 0; tl < cpi_->svc.number_temporal_layers; tl++) {
          int layer = LAYER_IDS_TO_IDX(sl, tl, cpi_->oxcf.ts_number_layers);
          LAYER_CONTEXT *const lc = &cpi_->svc.layer_context[layer];
          vpx_free(lc->map);
          vpx_free(lc->last_coded_q_map);
          vpx_free(lc->consec_zero_mv);
        }
      }
      vp9_cyclic_refresh_free(cpi_->cyclic_refresh);
      vpx_free(cpi_);
    }
  }

  void UpdateRateControl(const VP9RateControlRtcConfig &rc_cfg);
  // GetQP() needs to be called after ComputeQP() to get the latest QP
  int GetQP() const;
  int GetLoopfilterLevel() const;
  signed char *GetCyclicRefreshMap() const;
  int *GetDeltaQ() const;
  void ComputeQP(const VP9FrameParamsQpRTC &frame_params);
  // Feedback to rate control with the size of current encoded frame
  void PostEncodeUpdate(uint64_t encoded_frame_size);

 private:
  VP9RateControlRTC() {}
  void InitRateControl(const VP9RateControlRtcConfig &cfg);
  VP9_COMP *cpi_;
};

}  // namespace libvpx

#endif  // VPX_VP9_RATECTRL_RTC_H_
