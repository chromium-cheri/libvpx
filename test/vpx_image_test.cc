/*
 *  Copyright (c) 2024 The WebM project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "vpx/vpx_image.h"
#include "third_party/googletest/src/include/gtest/gtest.h"

TEST(VpxImageTest, VpxImgWrapInvalidAlign) {
  const int kWidth = 128;
  const int kHeight = 128;
  unsigned char buf[kWidth * kHeight * 3];

  vpx_image_t img;
  // Set img_data and img_data_owner to junk values. vpx_img_wrap() should
  // not read these values on failure.
  img.img_data = (unsigned char *)"";
  img.img_data_owner = 1;

  vpx_img_fmt_t format = VPX_IMG_FMT_I444;
  // 'align' must be a power of 2 but is not. This causes the vpx_img_wrap()
  // call to fail. The test verifies we do not read the junk values in 'img'.
  unsigned int align = 31;
  EXPECT_EQ(vpx_img_wrap(&img, format, kWidth, kHeight, align, buf), nullptr);
}

TEST(VpxImageTest, VpxImgSetRectOverflow) {
  const int kWidth = 128;
  const int kHeight = 128;
  unsigned char buf[kWidth * kHeight * 3];

  vpx_image_t img;
  vpx_img_fmt_t format = VPX_IMG_FMT_I444;
  unsigned int align = 32;
  EXPECT_EQ(vpx_img_wrap(&img, format, kWidth, kHeight, align, buf), &img);

  EXPECT_EQ(vpx_img_set_rect(&img, 0, 0, kWidth, kHeight), 0);
  // This would result in overflow because -1 is cast to UINT_MAX.
  EXPECT_NE(vpx_img_set_rect(&img, static_cast<unsigned int>(-1),
                             static_cast<unsigned int>(-1), kWidth, kHeight),
            0);
}

TEST(VpxImageTest, VpxImgAllocNone) {
  const int kWidth = 128;
  const int kHeight = 128;

  vpx_image_t img;
  vpx_img_fmt_t format = VPX_IMG_FMT_NONE;
  unsigned int align = 32;
  ASSERT_EQ(vpx_img_alloc(&img, format, kWidth, kHeight, align), nullptr);
}

TEST(VpxImageTest, VpxImgAllocNv12) {
  const int kWidth = 128;
  const int kHeight = 128;

  vpx_image_t img;
  vpx_img_fmt_t format = VPX_IMG_FMT_NV12;
  unsigned int align = 32;
  EXPECT_EQ(vpx_img_alloc(&img, format, kWidth, kHeight, align), &img);
  EXPECT_EQ(img.stride[VPX_PLANE_U], img.stride[VPX_PLANE_Y]);
  EXPECT_EQ(img.stride[VPX_PLANE_V], 0);
  EXPECT_EQ(img.planes[VPX_PLANE_V], nullptr);
  vpx_img_free(&img);
}
