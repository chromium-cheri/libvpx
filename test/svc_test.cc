#include "third_party/googletest/src/include/gtest/gtest.h"
#include "test/i420_video_source.h"
#include "test/decode_test_driver.h"
#include "vpx/vpx_encoder.h"
#include "vpx/vp8cx.h"
#include "test/codec_factory.h"

extern "C" {
#include "vpx/svc_context.h"
}

namespace {

using libvpx_test::CodecFactory;
using libvpx_test::VP9CodecFactory;
using libvpx_test::Decoder;

class SvcTest : public ::testing::Test {
 protected:
  SvcTest() : codec_iface_(0) {}

  virtual void SetUp() {
    memset(&svc_, 0, sizeof(svc_));
    svc_.first_frame_full_size = 1;
    svc_.encoding_mode = INTER_LAYER_PREDICTION_IP;
    svc_.log_level = SVC_LOG_DEBUG;
    svc_.log_print = 1;
    svc_.gop_size = 100;

    codec_iface_ = vpx_codec_vp9_cx();
    vpx_codec_err_t res =
        vpx_codec_enc_config_default(codec_iface_, &codec_enc_, 0);
    EXPECT_EQ(res, VPX_CODEC_OK);

    codec_enc_.g_w = 1920;
    codec_enc_.g_h = 800;
    codec_enc_.g_timebase.num = 1;
    codec_enc_.g_timebase.den = 60;

    vpx_codec_dec_cfg_t dec_cfg = {0};
    VP9CodecFactory codec_factory;
    _decoder = codec_factory.CreateDecoder(dec_cfg, 0);
  }

  SvcContext svc_;
  vpx_codec_ctx_t codec_;
  struct vpx_codec_enc_cfg codec_enc_;
  vpx_codec_iface_t* codec_iface_;

  Decoder* _decoder;
};

TEST_F(SvcTest, SvcInit) {
  svc_.spatial_layers = 0;  // will cause an error
  vpx_codec_err_t res = vpx_svc_init(&svc_, &codec_, codec_iface_, &codec_enc_);
  EXPECT_EQ(res, VPX_CODEC_INVALID_PARAM);

  svc_.spatial_layers = 5;
  res = vpx_svc_init(&svc_, &codec_, codec_iface_, &codec_enc_);
  EXPECT_EQ(res, VPX_CODEC_OK);
}

// test that decoder can handle an svc frame as the first frame in a sequence
// this test is disabled since it with the deco
TEST_F(SvcTest, DISABLED_FirstFrameHasLayers) {
  svc_.first_frame_full_size = 0;
  svc_.spatial_layers = 2;
  svc_.scale_factors = "4/16,16/16";
  svc_.quantizer_values = "40,30";

  vpx_codec_err_t res =
      vpx_svc_init(&svc_, &codec_, vpx_codec_vp9_cx(), &codec_enc_);
  EXPECT_EQ(res, VPX_CODEC_OK);

  libvpx_test::I420VideoSource video(
      "mango.yuv", codec_enc_.g_w, codec_enc_.g_h,
      codec_enc_.g_timebase.den, codec_enc_.g_timebase.num, 0, 30);
  video.Begin();

  res = vpx_svc_encode(&svc_, &codec_, video.img(), video.pts(),
                       video.duration(), VPX_DL_REALTIME);
  EXPECT_EQ(res, VPX_CODEC_OK);

  vpx_codec_err_t res_dec = _decoder->DecodeFrame(
      (const uint8_t*)svc_get_buffer(&svc_),
      svc_get_frame_size(&svc_));

  // this test fails with a decoder error
  ASSERT_EQ(VPX_CODEC_OK, res_dec) << _decoder->DecodeError();
}


TEST_F(SvcTest, EncodeThreeFrames) {
  svc_.first_frame_full_size = 1;
  svc_.spatial_layers = 2;
  svc_.scale_factors = "4/16,16/16";
  svc_.quantizer_values = "40,30";

  vpx_codec_err_t res =
      vpx_svc_init(&svc_, &codec_, vpx_codec_vp9_cx(), &codec_enc_);
  ASSERT_EQ(res, VPX_CODEC_OK);

  libvpx_test::I420VideoSource video(
      "mango.yuv", codec_enc_.g_w, codec_enc_.g_h,
      codec_enc_.g_timebase.den, codec_enc_.g_timebase.num, 0, 30);

  // FRAME 1
  video.Begin();
  // this frame is full size, with only one layer
  res = vpx_svc_encode(&svc_, &codec_, video.img(), video.pts(),
                       video.duration(), VPX_DL_REALTIME);
  ASSERT_EQ(res, VPX_CODEC_OK);
  EXPECT_EQ(1, svc_is_keyframe(&svc_));

  vpx_codec_err_t res_dec = _decoder->DecodeFrame(
      (const uint8_t*)svc_get_buffer(&svc_),
      svc_get_frame_size(&svc_));
  ASSERT_EQ(VPX_CODEC_OK, res_dec) << _decoder->DecodeError();


  // FRAME 2
  video.Next();
  // this is an I-frame
  res = vpx_svc_encode(&svc_, &codec_, video.img(), video.pts(),
                       video.duration(), VPX_DL_REALTIME);
  ASSERT_EQ(res, VPX_CODEC_OK);
  EXPECT_EQ(1, svc_is_keyframe(&svc_));

  res_dec = _decoder->DecodeFrame(
      (const uint8_t*)svc_get_buffer(&svc_),
      svc_get_frame_size(&svc_));
  ASSERT_EQ(VPX_CODEC_OK, res_dec) << _decoder->DecodeError();

  // FRAME 2
  video.Next();
  // this is a P-frame
  res = vpx_svc_encode(&svc_, &codec_, video.img(), video.pts(),
                       video.duration(), VPX_DL_REALTIME);
  ASSERT_EQ(res, VPX_CODEC_OK);
  EXPECT_EQ(0, svc_is_keyframe(&svc_));

  res_dec = _decoder->DecodeFrame(
      (const uint8_t*)svc_get_buffer(&svc_),
      svc_get_frame_size(&svc_));
  ASSERT_EQ(VPX_CODEC_OK, res_dec) << _decoder->DecodeError();
}

}  // namespace
