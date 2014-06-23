/*
 *  Copyright (c) 2013 The WebM project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include <string>

#include "third_party/googletest/src/include/gtest/gtest.h"
#include "./vpx_config.h"
#include "test/codec_factory.h"
#include "test/decode_test_driver.h"
#include "test/md5_helper.h"
#if CONFIG_WEBM_IO
#include "test/webm_video_source.h"
#endif
#include "vp9/decoder/vp9_thread.h"

namespace {

using std::string;

class VP9WorkerThreadTest : public ::testing::TestWithParam<bool> {
 protected:
  virtual ~VP9WorkerThreadTest() {}
  virtual void SetUp() {
    vp9_worker_init(&worker_);
  }

  virtual void TearDown() {
    vp9_worker_end(&worker_);
  }

  VP9Worker worker_;
};

int ThreadHook(void* data, void* return_value) {
  int* const hook_data = reinterpret_cast<int*>(data);
  *hook_data = 5;
  return *reinterpret_cast<int*>(return_value);
}

TEST_P(VP9WorkerThreadTest, HookSuccess) {
  EXPECT_NE(vp9_worker_sync(&worker_), 0);  // should be a no-op.

  for (int i = 0; i < 2; ++i) {
    EXPECT_NE(vp9_worker_reset(&worker_), 0);

    int hook_data = 0;
    int return_value = 1;  // return successfully from the hook
    worker_.hook = ThreadHook;
    worker_.data1 = &hook_data;
    worker_.data2 = &return_value;

    const bool synchronous = GetParam();
    if (synchronous) {
      vp9_worker_execute(&worker_);
    } else {
      vp9_worker_launch(&worker_);
    }
    EXPECT_NE(vp9_worker_sync(&worker_), 0);
    EXPECT_FALSE(worker_.had_error);
    EXPECT_EQ(5, hook_data);

    EXPECT_NE(vp9_worker_sync(&worker_), 0);  // should be a no-op.
  }
}

TEST_P(VP9WorkerThreadTest, HookFailure) {
  EXPECT_NE(vp9_worker_reset(&worker_), 0);

  int hook_data = 0;
  int return_value = 0;  // return failure from the hook
  worker_.hook = ThreadHook;
  worker_.data1 = &hook_data;
  worker_.data2 = &return_value;

  const bool synchronous = GetParam();
  if (synchronous) {
    vp9_worker_execute(&worker_);
  } else {
    vp9_worker_launch(&worker_);
  }
  EXPECT_FALSE(vp9_worker_sync(&worker_));
  EXPECT_EQ(1, worker_.had_error);

  // Ensure _reset() clears the error and _launch() can be called again.
  return_value = 1;
  EXPECT_NE(vp9_worker_reset(&worker_), 0);
  EXPECT_FALSE(worker_.had_error);
  vp9_worker_launch(&worker_);
  EXPECT_NE(vp9_worker_sync(&worker_), 0);
  EXPECT_FALSE(worker_.had_error);
}

// -----------------------------------------------------------------------------
// Multi-threaded decode tests

#if CONFIG_WEBM_IO
// Decodes |filename| with |num_threads|. Returns the md5 of the decoded frames.
string DecodeFile(const string& filename, int num_threads) {
  libvpx_test::WebMVideoSource video(filename);
  video.Init();

  vpx_codec_dec_cfg_t cfg = {0};
  cfg.threads = num_threads;
  libvpx_test::VP9Decoder decoder(cfg, 0);

  libvpx_test::MD5 md5;
  for (video.Begin(); video.cxdata(); video.Next()) {
    const vpx_codec_err_t res =
        decoder.DecodeFrame(video.cxdata(), video.frame_size(), NULL);
    if (res != VPX_CODEC_OK) {
      EXPECT_EQ(VPX_CODEC_OK, res) << decoder.DecodeError();
      break;
    }

    libvpx_test::DxDataIterator dec_iter = decoder.GetDxData();
    const vpx_image_t *img = NULL;

    // Get decompressed data
    while ((img = dec_iter.Next())) {
      md5.Add(img);
    }
  }
  return string(md5.Get());
}

TEST(VP9DecodeMTTest, MTDecode) {
  // no tiles or frame parallel; this exercises loop filter threading.
  EXPECT_STREQ("b35a1b707b28e82be025d960aba039bc",
               DecodeFile("vp90-2-03-size-226x226.webm", 2).c_str());
}

TEST(VP9DecodeMTTest, MTDecode2) {
  static const struct {
    const char *name;
    const char *expected_md5;
  } files[] = {
    { "vp90-2-08-tile_1x2_frame_parallel.webm",
      "68ede6abd66bae0a2edf2eb9232241b6" },
    { "vp90-2-08-tile_1x4_frame_parallel.webm",
      "368ebc6ebf3a5e478d85b2c3149b2848" },
    { "vp90-2-08-tile_1x8_frame_parallel.webm",
      "17e439da2388aff3a0f69cb22579c6c1" },
  };

  for (int i = 0; i < static_cast<int>(sizeof(files) / sizeof(files[0])); ++i) {
    for (int t = 2; t <= 8; ++t) {
      EXPECT_STREQ(files[i].expected_md5, DecodeFile(files[i].name, t).c_str())
          << "threads = " << t;
    }
  }
}

// Test tile quantity changes within one file.
TEST(VP9DecodeMTTest, MTDecode3) {
  static const struct {
    const char *name;
    const char *expected_md5;
  } files[] = {
    { "vp90-2-14-resize-fp-tiles-1-16.webm",
      "0cd5e632c326297e975f38949c31ea94" },
    { "vp90-2-14-resize-fp-tiles-1-2-4-8-16.webm",
      "5c78a96a42e7f4a4f6b2edcdb791e44c" },
    { "vp90-2-14-resize-fp-tiles-1-2.webm",
      "e030450ae85c3277be2a418769df98e2" },
    { "vp90-2-14-resize-fp-tiles-1-4.webm",
      "312eed4e2b64eb7a4e7f18916606a430" },
    { "vp90-2-14-resize-fp-tiles-16-1.webm",
      "1755c16d8af16a9cb3fe7338d90abe52" },
    { "vp90-2-14-resize-fp-tiles-16-2.webm",
      "500300592d3fcb6f12fab25e48aaf4df" },
    { "vp90-2-14-resize-fp-tiles-16-4.webm",
      "47c48379fa6331215d91c67648e1af6e" },
    { "vp90-2-14-resize-fp-tiles-16-8-4-2-1.webm",
      "eecf17290739bc708506fa4827665989" },
    { "vp90-2-14-resize-fp-tiles-16-8.webm",
      "29b6bb54e4c26b5ca85d5de5fed94e76" },
    { "vp90-2-14-resize-fp-tiles-1-8.webm",
      "1b6f175e08cd82cf84bb800ac6d1caa3" },
    { "vp90-2-14-resize-fp-tiles-2-16.webm",
      "ca3b03e4197995d8d5444ede7a6c0804" },
    { "vp90-2-14-resize-fp-tiles-2-1.webm",
      "99aec065369d70bbb78ccdff65afed3f" },
    { "vp90-2-14-resize-fp-tiles-2-4.webm",
      "22d0ebdb49b87d2920a85aea32e1afd5" },
    { "vp90-2-14-resize-fp-tiles-2-8.webm",
      "c2115cf051c62e0f7db1d4a783831541" },
    { "vp90-2-14-resize-fp-tiles-4-16.webm",
      "c690d7e1719b31367564cac0af0939cb" },
    { "vp90-2-14-resize-fp-tiles-4-1.webm",
      "a926020b2cc3e15ad4cc271853a0ff26" },
    { "vp90-2-14-resize-fp-tiles-4-2.webm",
      "42699063d9e581f1993d0cf890c2be78" },
    { "vp90-2-14-resize-fp-tiles-4-8.webm",
      "7f76d96036382f45121e3d5aa6f8ec52" },
    { "vp90-2-14-resize-fp-tiles-8-16.webm",
      "76a43fcdd7e658542913ea43216ec55d" },
    { "vp90-2-14-resize-fp-tiles-8-1.webm",
      "8e3fbe89486ca60a59299dea9da91378" },
    { "vp90-2-14-resize-fp-tiles-8-2.webm",
      "ae96f21f21b6370cc0125621b441fc52" },
    { "vp90-2-14-resize-fp-tiles-8-4.webm",
      "3eb4f24f10640d42218f7fd7b9fd30d4" },
  };

  for (int i = 0; i < static_cast<int>(sizeof(files) / sizeof(files[0])); ++i) {
    for (int t = 2; t <= 8; ++t) {
      EXPECT_STREQ(files[i].expected_md5, DecodeFile(files[i].name, t).c_str())
          << "threads = " << t;
    }
  }
}
#endif  // CONFIG_WEBM_IO

INSTANTIATE_TEST_CASE_P(Synchronous, VP9WorkerThreadTest, ::testing::Bool());

}  // namespace
