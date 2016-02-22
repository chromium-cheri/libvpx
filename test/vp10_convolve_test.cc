#include "third_party/googletest/src/include/gtest/gtest.h"

#include "./vpx_dsp_rtcd.h"
#include "test/acm_random.h"
#include "vp10/common/filter.h"
#include "vp10/common/vp10_convolve.h"
#include "vpx_dsp/vpx_dsp_common.h"
#include "vpx_ports/mem.h"

using libvpx_test::ACMRandom;

namespace {
TEST(VP10ConvolveTest, vp10_convolve8) {
  ACMRandom rnd(ACMRandom::DeterministicSeed());
  INTERP_FILTER interp_filter = EIGHTTAP;
  InterpFilterParams filter_params =
      vp10_get_interp_filter_params(interp_filter);
  ptrdiff_t filter_size = filter_params.tap;
  int filter_center = filter_size / 2 - 1;
  uint8_t src[12 * 12];
  int src_stride = filter_size;
  uint8_t dst[1] = {0};
  uint8_t dst1[1] = {0};
  int dst_stride = 1;
  int x_step_q4 = 16;
  int y_step_q4 = 16;
  int subpel_x_q4 = 3;
  int subpel_y_q4 = 2;
  int avg = 0;

  int w = 1;
  int h = 1;

  for (int i = 0; i < filter_size * filter_size; i++) {
    src[i] = rnd.Rand16() % (1 << 8);
  }

  vp10_convolve(src + src_stride * filter_center + filter_center, src_stride,
                dst, dst_stride, w, h, filter_params, subpel_x_q4, x_step_q4,
                subpel_y_q4, y_step_q4, avg);

  const int16_t* x_filter =
      vp10_get_interp_filter_kernel(filter_params, subpel_x_q4);
  const int16_t* y_filter =
      vp10_get_interp_filter_kernel(filter_params, subpel_y_q4);

  vpx_convolve8_c(src + src_stride * filter_center + filter_center, src_stride,
                  dst1, dst_stride, x_filter, 16, y_filter, 16, w, h);
  EXPECT_EQ(dst[0], dst1[0]);
}
TEST(VP10ConvolveTest, vp10_convolve) {
  ACMRandom rnd(ACMRandom::DeterministicSeed());
  INTERP_FILTER interp_filter = EIGHTTAP;
  InterpFilterParams filter_params =
      vp10_get_interp_filter_params(interp_filter);
  ptrdiff_t filter_size = filter_params.tap;
  int filter_center = filter_size / 2 - 1;
  uint8_t src[12 * 12];
  int src_stride = filter_size;
  uint8_t dst[1] = {0};
  int dst_stride = 1;
  int x_step_q4 = 16;
  int y_step_q4 = 16;
  int subpel_x_q4 = 3;
  int subpel_y_q4 = 2;
  int avg = 0;

  int w = 1;
  int h = 1;

  for (int i = 0; i < filter_size * filter_size; i++) {
    src[i] = rnd.Rand16() % (1 << 8);
  }

  vp10_convolve(src + src_stride * filter_center + filter_center, src_stride,
                dst, dst_stride, w, h, filter_params, subpel_x_q4, x_step_q4,
                subpel_y_q4, y_step_q4, avg);

  const int16_t* x_filter =
      vp10_get_interp_filter_kernel(filter_params, subpel_x_q4);
  const int16_t* y_filter =
      vp10_get_interp_filter_kernel(filter_params, subpel_y_q4);

  int temp[12];
  int dst_ref = 0;
  for (int r = 0; r < filter_size; r++) {
    temp[r] = 0;
    for (int c = 0; c < filter_size; c++) {
      temp[r] += x_filter[c] * src[r * filter_size + c];
    }
    temp[r] = clip_pixel(ROUND_POWER_OF_TWO(temp[r], FILTER_BITS));
    dst_ref += temp[r] * y_filter[r];
  }
  dst_ref = clip_pixel(ROUND_POWER_OF_TWO(dst_ref, FILTER_BITS));
  EXPECT_EQ(dst[0], dst_ref);
}

TEST(VP10ConvolveTest, vp10_convolve_avg) {
  ACMRandom rnd(ACMRandom::DeterministicSeed());
  INTERP_FILTER interp_filter = EIGHTTAP;
  InterpFilterParams filter_params =
      vp10_get_interp_filter_params(interp_filter);
  ptrdiff_t filter_size = filter_params.tap;
  int filter_center = filter_size / 2 - 1;
  uint8_t src0[12 * 12];
  uint8_t src1[12 * 12];
  int src_stride = filter_size;
  uint8_t dst0[1] = {0};
  uint8_t dst1[1] = {0};
  uint8_t dst[1] = {0};
  int dst_stride = 1;
  int x_step_q4 = 16;
  int y_step_q4 = 16;
  int subpel_x_q4 = 3;
  int subpel_y_q4 = 2;
  int avg = 0;

  int w = 1;
  int h = 1;

  for (int i = 0; i < filter_size * filter_size; i++) {
    src0[i] = rnd.Rand16() % (1 << 8);
    src1[i] = rnd.Rand16() % (1 << 8);
  }

  int offset = filter_size * filter_center + filter_center;

  avg = 0;
  vp10_convolve(src0 + offset, src_stride, dst0, dst_stride, w, h,
                filter_params, subpel_x_q4, x_step_q4, subpel_y_q4, y_step_q4,
                avg);
  avg = 0;
  vp10_convolve(src1 + offset, src_stride, dst1, dst_stride, w, h,
                filter_params, subpel_x_q4, x_step_q4, subpel_y_q4, y_step_q4,
                avg);

  avg = 0;
  vp10_convolve(src0 + offset, src_stride, dst, dst_stride, w, h, filter_params,
                subpel_x_q4, x_step_q4, subpel_y_q4, y_step_q4, avg);
  avg = 1;
  vp10_convolve(src1 + offset, src_stride, dst, dst_stride, w, h, filter_params,
                subpel_x_q4, x_step_q4, subpel_y_q4, y_step_q4, avg);

  EXPECT_EQ(dst[0], ROUND_POWER_OF_TWO(dst0[0] + dst1[0], 1));
}

#if CONFIG_VP9_HIGHBITDEPTH
TEST(VP10ConvolveTest, vp10_highbd_convolve) {
  ACMRandom rnd(ACMRandom::DeterministicSeed());
  INTERP_FILTER interp_filter = EIGHTTAP;
  InterpFilterParams filter_params =
      vp10_get_interp_filter_params(interp_filter);
  ptrdiff_t filter_size = filter_params.tap;
  int filter_center = filter_size / 2 - 1;
  uint16_t src[12 * 12];
  int src_stride = filter_size;
  uint16_t dst[1] = {0};
  int dst_stride = 1;
  int x_step_q4 = 16;
  int y_step_q4 = 16;
  int subpel_x_q4 = 8;
  int subpel_y_q4 = 6;
  int avg = 0;
  int bd = 10;

  int w = 1;
  int h = 1;

  for (int i = 0; i < filter_size * filter_size; i++) {
    src[i] = rnd.Rand16() % (1 << bd);
  }

  vp10_highbd_convolve(
      CONVERT_TO_BYTEPTR(src + src_stride * filter_center + filter_center),
      src_stride, CONVERT_TO_BYTEPTR(dst), dst_stride, w, h, filter_params,
      subpel_x_q4, x_step_q4, subpel_y_q4, y_step_q4, avg, bd);

  const int16_t* x_filter =
      vp10_get_interp_filter_kernel(filter_params, subpel_x_q4);
  const int16_t* y_filter =
      vp10_get_interp_filter_kernel(filter_params, subpel_y_q4);

  int temp[12];
  int dst_ref = 0;
  for (int r = 0; r < filter_size; r++) {
    temp[r] = 0;
    for (int c = 0; c < filter_size; c++) {
      temp[r] += x_filter[c] * src[r * filter_size + c];
    }
    temp[r] = clip_pixel_highbd(ROUND_POWER_OF_TWO(temp[r], FILTER_BITS), bd);
    dst_ref += temp[r] * y_filter[r];
  }
  dst_ref = clip_pixel_highbd(ROUND_POWER_OF_TWO(dst_ref, FILTER_BITS), bd);
  EXPECT_EQ(dst[0], dst_ref);
}

TEST(VP10ConvolveTest, vp10_highbd_convolve_avg) {
  ACMRandom rnd(ACMRandom::DeterministicSeed());
  INTERP_FILTER interp_filter = EIGHTTAP;
  InterpFilterParams filter_params =
      vp10_get_interp_filter_params(interp_filter);
  ptrdiff_t filter_size = filter_params.tap;
  int filter_center = filter_size / 2 - 1;
  uint16_t src0[12 * 12];
  uint16_t src1[12 * 12];
  int src_stride = filter_size;
  uint16_t dst0[1] = {0};
  uint16_t dst1[1] = {0};
  uint16_t dst[1] = {0};
  int dst_stride = 1;
  int x_step_q4 = 16;
  int y_step_q4 = 16;
  int subpel_x_q4 = 3;
  int subpel_y_q4 = 2;
  int avg = 0;
  int bd = 10;

  int w = 1;
  int h = 1;

  for (int i = 0; i < filter_size * filter_size; i++) {
    src0[i] = rnd.Rand16() % (1 << bd);
    src1[i] = rnd.Rand16() % (1 << bd);
  }

  int offset = filter_size * filter_center + filter_center;

  avg = 0;
  vp10_highbd_convolve(CONVERT_TO_BYTEPTR(src0 + offset), src_stride,
                       CONVERT_TO_BYTEPTR(dst0), dst_stride, w, h,
                       filter_params, subpel_x_q4, x_step_q4, subpel_y_q4,
                       y_step_q4, avg, bd);
  avg = 0;
  vp10_highbd_convolve(CONVERT_TO_BYTEPTR(src1 + offset), src_stride,
                       CONVERT_TO_BYTEPTR(dst1), dst_stride, w, h,
                       filter_params, subpel_x_q4, x_step_q4, subpel_y_q4,
                       y_step_q4, avg, bd);

  avg = 0;
  vp10_highbd_convolve(CONVERT_TO_BYTEPTR(src0 + offset), src_stride,
                       CONVERT_TO_BYTEPTR(dst), dst_stride, w, h, filter_params,
                       subpel_x_q4, x_step_q4, subpel_y_q4, y_step_q4, avg, bd);
  avg = 1;
  vp10_highbd_convolve(CONVERT_TO_BYTEPTR(src1 + offset), src_stride,
                       CONVERT_TO_BYTEPTR(dst), dst_stride, w, h, filter_params,
                       subpel_x_q4, x_step_q4, subpel_y_q4, y_step_q4, avg, bd);

  EXPECT_EQ(dst[0], ROUND_POWER_OF_TWO(dst0[0] + dst1[0], 1));
}

TEST(VP10ConvolveTest, convolve_speed_h) {
  ACMRandom rnd(ACMRandom::DeterministicSeed());
  INTERP_FILTER interp_filter = EIGHTTAP;
  InterpFilterParams filter_params =
      vp10_get_interp_filter_params(interp_filter);
  ptrdiff_t filter_size = filter_params.tap;
  int filter_center = filter_size / 2 - 1;
  DECLARE_ALIGNED(16, uint16_t, src[71 * 71]) = {0};
  int src_stride = 71;
  DECLARE_ALIGNED(16, uint16_t, dst[64 * 64]) = {0};
  int dst_stride = 64;
  int x_step_q4 = 16;
  int y_step_q4 = 16;
  int subpel_x_q4 = 8;
  int subpel_y_q4 = 6;
  int bd = 10;

  int w = 64;
  int h = 64;

  const int16_t* filter_x = vp10_get_interp_filter_kernel(filter_params, subpel_x_q4);
  const int16_t* filter_y = vp10_get_interp_filter_kernel(filter_params, subpel_y_q4);

  for (int i = 0; i < src_stride * src_stride; i++) {
    src[i] = rnd.Rand16() % (1 << bd);
  }

  int offset = filter_center*src_stride + filter_center;

  for(int i = 0; i < 100000; i++) {
    vpx_highbd_convolve8_sse2(CONVERT_TO_BYTEPTR(src+offset), src_stride,
                                 CONVERT_TO_BYTEPTR(dst), dst_stride,
                                 filter_x, x_step_q4,
                                 filter_y, y_step_q4,
                                 w, h, bd);
  }
}

TEST(VP10ConvolveTest, convolve_speed_l) {
  ACMRandom rnd(ACMRandom::DeterministicSeed());
  INTERP_FILTER interp_filter = EIGHTTAP;
  InterpFilterParams filter_params =
      vp10_get_interp_filter_params(interp_filter);
  ptrdiff_t filter_size = filter_params.tap;
  int filter_center = filter_size / 2 - 1;
  DECLARE_ALIGNED(16, uint8_t, src[71 * 71]);
  int src_stride = 71;
  DECLARE_ALIGNED(16, uint8_t, dst[64 * 64]);
  int dst_stride = 64;
  int x_step_q4 = 16;
  int y_step_q4 = 16;
  int subpel_x_q4 = 8;
  int subpel_y_q4 = 6;
  int bd = 8;

  int w = 64;
  int h = 64;

  const int16_t* filter_x = vp10_get_interp_filter_kernel(filter_params, subpel_x_q4);
  const int16_t* filter_y = vp10_get_interp_filter_kernel(filter_params, subpel_y_q4);

  for (int i = 0; i < src_stride * src_stride; i++) {
    src[i] = rnd.Rand16() % (1 << bd);
  }

  int offset = filter_center*src_stride + filter_center;

  for(int i = 0; i < 100000; i++) {
    vpx_convolve8_sse2(src+offset, src_stride,
                                 dst, dst_stride,
                                 filter_x, x_step_q4,
                                 filter_y, y_step_q4,
                                 w, h);
  }
}
#endif  // CONFIG_VP9_HIGHBITDEPTH
}  // namespace
