/*
 *  Copyright (c) 2012 The WebM project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */
#include <climits>
#include <vector>
#include "third_party/googletest/src/include/gtest/gtest.h"
#include "test/codec_factory.h"
#include "test/encode_test_driver.h"
#include "test/i420_video_source.h"
#include "test/util.h"

namespace {

class AqSegmentTest : public ::libvpx_test::EncoderTest,
    public ::libvpx_test::CodecTestWith2Params<
        libvpx_test::TestMode, int> {
 protected:
  AqSegmentTest() : EncoderTest(GET_PARAM(0)) {}

  virtual void SetUp() {
    InitializeConfig();
    SetMode(GET_PARAM(1));
    set_cpu_used_ = GET_PARAM(2);
  }

  virtual void PreEncodeFrameHook(::libvpx_test::VideoSource *video,
                                  ::libvpx_test::Encoder *encoder) {
    if (video->frame() == 1) {
      encoder->Control(VP8E_SET_CPUUSED, set_cpu_used_);
      encoder->Control(VP9E_SET_AQ_MODE, 3);
      encoder->Control(VP8E_SET_MAX_INTRA_BITRATE_PCT, 100);
    }
  }

  virtual void FramePktHook(const vpx_codec_cx_pkt_t *pkt) {
    if (pkt->data.frame.flags & VPX_FRAME_IS_KEY) {
    }
  }
  int set_cpu_used_;
};

// Validate that this AQ segmentation mode (AQ=3, cyclic_refresh)
// encodes and decodes without a mismatch.
TEST_P(AqSegmentTest, TestNoMisMatchAQ3) {
  cfg_.rc_min_quantizer = 8;
  cfg_.rc_max_quantizer = 56;
  cfg_.rc_end_usage = VPX_CBR;
  cfg_.g_lag_in_frames = 0;
  cfg_.rc_buf_initial_sz = 500;
  cfg_.rc_buf_optimal_sz = 500;
  cfg_.rc_buf_sz = 1000;
  cfg_.rc_target_bitrate = 300;

  ::libvpx_test::I420VideoSource video("hantro_collage_w352h288.yuv", 352, 288,
                                        30, 1, 0, 100);

  // Error_resilience off.
  cfg_.g_error_resilient = 0;
  ASSERT_NO_FATAL_FAILURE(RunLoop(&video));

  // Error_resilience on.
  cfg_.g_error_resilient = 1;
  ASSERT_NO_FATAL_FAILURE(RunLoop(&video));
}

using std::tr1::make_tuple;

#define VP9_FACTORY \
  static_cast<const libvpx_test::CodecFactory*> (&libvpx_test::kVP9)

VP9_INSTANTIATE_TEST_CASE(AqSegmentTest,
                          ::testing::Values(::libvpx_test::kRealTime),
                          ::testing::Range(3, 8));
}  // namespace
