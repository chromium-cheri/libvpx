/*
 *  Copyright (c) 2014 The WebM project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include <cstdio>
#include <cstdlib>
#include <string>
#include "third_party/googletest/src/include/gtest/gtest.h"
#include "./vpx_config.h"
#include "test/codec_factory.h"
#include "test/decode_test_driver.h"
#include "test/ivf_video_source.h"
#include "test/md5_helper.h"
#include "test/util.h"
#if CONFIG_WEBM_IO
#include "test/webm_video_source.h"
#endif
#include "vpx_mem/vpx_mem.h"

namespace {

using std::string;

#if CONFIG_WEBM_IO

struct FileList {
  const char *name;
  // md5 sum for decoded frames which do not include skipped frames.
  const char *expected_md5;
  const int pause_frame_num;
};

// Decodes |filename| with |num_threads|. Pause at the specify frame_num,
// seek to next key frame and then continue decode until the end. Return
// the md5 of the decoded frames which do not include skipped frames.
string DecodeFile(const string& filename, int num_threads, int pause_num) {
  libvpx_test::WebMVideoSource video(filename);
  video.Init();
  int in_frames = 0;
  int out_frames = 0;

  vpx_codec_dec_cfg_t cfg = {0};
  cfg.threads = num_threads;
  vpx_codec_flags_t flags = 0;
  flags |= VPX_CODEC_USE_FRAME_THREADING;
  libvpx_test::VP9Decoder decoder(cfg, flags, 0);

  libvpx_test::MD5 md5;
  video.Begin();

  do {
      ++in_frames;
      const vpx_codec_err_t res =
          decoder.DecodeFrame(video.cxdata(), video.frame_size());
      if (res != VPX_CODEC_OK) {
        EXPECT_EQ(VPX_CODEC_OK, res) << decoder.DecodeError();
        break;
      }

      // Pause at specific frame number.
      if (in_frames == pause_num) {
        // Flush the decoder and then jump to next key frame.
        decoder.DecodeFrame(NULL, 0);
        video.SeekToNextKeyFrame();
      } else {
        video.Next();
      }

    libvpx_test::DxDataIterator dec_iter = decoder.GetDxData();
    const vpx_image_t *img = NULL;

    // Get decompressed data
    while ((img = dec_iter.Next())) {
      ++out_frames;
      md5.Add(img);
    }
  } while(video.cxdata());

  EXPECT_EQ(in_frames, out_frames) <<
      "Iuput frames does not match output frames";

  return string(md5.Get());
}

void DecodeFiles(const FileList files[]) {
  for (const FileList *iter = files; iter->name != NULL; ++iter) {
    SCOPED_TRACE(iter->name);
    for (int t = 4; t <= 4; ++t) {
      EXPECT_EQ(iter->expected_md5, DecodeFile(iter->name, t,
                iter->pause_frame_num)) << "threads = " << t;
    }
  }
}

TEST(VP9MultiThreadedFrameParallel2, Decode1) {
  static const FileList files[] = {
    { "vp90-2-07-frame_parallel-1.webm",
      "6ea7c3875d67252e7caf2bc6e75b36b1", 6},
    { "vp90-2-07-frame_parallel-1.webm",
      "4bb634160c7356a8d7d4299b6dc83a45", 12},
    { "vp90-2-07-frame_parallel-1.webm",
      "89772591e6ef461f9fa754f916c78ed8", 26},
  };

  DecodeFiles(files);
}

#endif  // CONFIG_WEBM_IO

}  // namespace
