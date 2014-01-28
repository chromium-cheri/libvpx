/*
 *  Copyright (c) 2014 The WebM project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include <string>

#include "test/codec_factory.h"
#include "test/decode_test_driver.h"
#include "test/ivf_video_source.h"
#include "test/md5_helper.h"
#include "test/test_vectors.h"
#include "test/util.h"
#include "test/webm_video_source.h"

namespace {

const int kVideoNameParam = 1;
const char kVP9TestFile[] = "vp90-2-02-size-lf-1920x1080.webm";

struct ExternalFrameBuffer {
  uint8_t* data;
  size_t size;
  int in_use;
};

// Class to manipulate a list of external frame buffers.
class ExternalFrameBufferList {
 public:
  ExternalFrameBufferList()
      : num_buffers_(0),
        ext_fb_list_(NULL) {}

  virtual ~ExternalFrameBufferList() {
    for (int i = 0; i < num_buffers_; ++i) {
      delete [] ext_fb_list_[i].data;
    }
    delete [] ext_fb_list_;
  }

  // Creates the list to hold the external buffers. Returns true on success.
  bool CreateBufferList(int num_buffers) {
    if (num_buffers < 0)
      return false;

    num_buffers_ = num_buffers;
    ext_fb_list_ = new ExternalFrameBuffer[num_buffers_];
    memset(ext_fb_list_, 0, sizeof(ext_fb_list_[0]) * num_buffers_);
    return true;
  }

  // Searches the frame buffer list for a free frame buffer. Makes sure
  // that the frame buffer is at least |min_size| in bytes. Marks that the
  // frame buffer is in use by libvpx. Finally sets |fb| to point to the
  // external frame buffer. Returns < 0 on an error.
  int GetFreeFrameBuffer(size_t min_size, vpx_codec_frame_buffer_t *fb) {
    if (fb == NULL)
      return -1;

    const int idx = FindFreeBufferIndex();
    if (idx == num_buffers_)
      return -1;

    if (ext_fb_list_[idx].size < min_size) {
      delete [] ext_fb_list_[idx].data;
      ext_fb_list_[idx].data = new uint8_t[min_size];
      ext_fb_list_[idx].size = min_size;
    }

    SetFrameBuffer(idx, fb);
    return 0;
  }

  // Marks the external frame buffer that |fb| is pointing too as free.
  // Returns < 0 on an error.
  int ReturnFrameBuffer(vpx_codec_frame_buffer_t *fb) {
    if (fb == NULL)
      return -1;
    ExternalFrameBuffer *const ext_fb =
        static_cast<ExternalFrameBuffer*>(fb->frame_priv);
    ext_fb->in_use = 0;
    return 0;
  }

  // Test function that will not allocate any data for the frame buffer.
  // Returns < 0 on an error.
  int GetZeroFrameBuffer(size_t min_size, vpx_codec_frame_buffer_t *fb) {
    if (fb == NULL)
      return -1;

    const int idx = FindFreeBufferIndex();
    if (idx == num_buffers_)
      return -1;

    if (ext_fb_list_[idx].size < min_size) {
      delete [] ext_fb_list_[idx].data;
      ext_fb_list_[idx].data = NULL;
      ext_fb_list_[idx].size = min_size;
    }

    SetFrameBuffer(idx, fb);
    return 0;
  }

  // Test function that will allocate data for the frame buffer that is one
  // byte smaller than |min_size|. Returns < 0 on an error.
  int GetOneLessByteFrameBuffer(size_t min_size, vpx_codec_frame_buffer_t *fb) {
    if (fb == NULL)
      return -1;

    const int idx = FindFreeBufferIndex();
    if (idx == num_buffers_)
      return -1;

    if (ext_fb_list_[idx].size < min_size) {
      delete [] ext_fb_list_[idx].data;
      const size_t error_size = min_size - 1;
      ext_fb_list_[idx].data = new uint8_t[error_size];
      ext_fb_list_[idx].size = error_size;
    }

    SetFrameBuffer(idx, fb);
    return 0;
  }

  // Checks that the ximage data is contained within the external frame buffer
  // private data passed back in the ximage.
  void CheckXImageFrameBuffer(const vpx_image_t *img) {
    if (img->ext_fb_priv != NULL) {
      const struct ExternalFrameBuffer *const ext_fb =
          (struct ExternalFrameBuffer*)img->ext_fb_priv;

      ASSERT_TRUE(img->planes[0] >= ext_fb->data &&
                  img->planes[0] < (ext_fb->data + ext_fb->size));
    }
  }

 private:
  // Returns the index of the first free frame buffer. Returns |num_buffers_|
  // if there are no free frame buffers.
  int FindFreeBufferIndex() {
    int i;
    // Find a free frame buffer.
    for (i = 0; i < num_buffers_; ++i) {
      if (!ext_fb_list_[i].in_use)
        break;
    }
    return i;
  }

  // Sets |fb| to an external frame buffer. idx is the index into the frame
  // buffer list.
  void SetFrameBuffer(int idx, vpx_codec_frame_buffer_t *fb) {
    fb->data = ext_fb_list_[idx].data;
    fb->size = ext_fb_list_[idx].size;
    ext_fb_list_[idx].in_use = 1;
    fb->frame_priv = static_cast<void*>(&ext_fb_list_[idx]);
  }

  int num_buffers_;
  ExternalFrameBuffer *ext_fb_list_;
};

// Callback used by libvpx to request the application to return a frame
// buffer of at least |min_size| in bytes.
static int get_vp9_frame_buffer(void *user_priv, size_t min_size,
                                vpx_codec_frame_buffer_t *fb) {
  ExternalFrameBufferList *const fb_list =
      static_cast<ExternalFrameBufferList*>(user_priv);
  return fb_list->GetFreeFrameBuffer(min_size, fb);
}

// Callback used by libvpx to tell the application that |fb| is not needed
// anymore.
static int release_vp9_frame_buffer(void *user_priv,
                                    vpx_codec_frame_buffer_t *fb) {
  ExternalFrameBufferList *const fb_list =
        static_cast<ExternalFrameBufferList*>(user_priv);
  return fb_list->ReturnFrameBuffer(fb);
}

// Callback will not allocate data for frame buffer.
static int get_vp9_zero_frame_buffer(void *user_priv, size_t min_size,
                                     vpx_codec_frame_buffer_t *fb) {
  ExternalFrameBufferList *const fb_list =
      static_cast<ExternalFrameBufferList*>(user_priv);
  return fb_list->GetZeroFrameBuffer(min_size, fb);
}

// Callback will allocate one less byte than |min_size|.
static int get_vp9_one_less_byte_frame_buffer(
    void *user_priv, size_t min_size, vpx_codec_frame_buffer_t *fb) {
  ExternalFrameBufferList *const fb_list =
      static_cast<ExternalFrameBufferList*>(user_priv);
  return fb_list->GetOneLessByteFrameBuffer(min_size, fb);
}

// Callback will not release the external frame buffer.
static int do_not_release_vp9_frame_buffer(
    void *user_priv, vpx_codec_frame_buffer_t *fb) {
  (void)user_priv;
  (void)fb;
  return 0;
}

// Class for testing passing in external frame buffers to libvpx.
class ExternalFrameBufferMD5Test
    : public ::libvpx_test::DecoderTest,
      public ::libvpx_test::CodecTestWithParam<const char*> {
 protected:
  ExternalFrameBufferMD5Test()
      : DecoderTest(GET_PARAM(::libvpx_test::kCodecFactoryParam)),
        md5_file_(NULL),
        num_buffers_(0) {}

  virtual ~ExternalFrameBufferMD5Test() {
    if (md5_file_ != NULL)
      fclose(md5_file_);
  }

  virtual void PreDecodeFrameHook(
      const libvpx_test::CompressedVideoSource &video,
      libvpx_test::Decoder *decoder) {
    if (num_buffers_ > 0 && video.frame_number() == 0) {
      // Have libvpx use frame buffers we create.
      ASSERT_TRUE(fb_list_.CreateBufferList(num_buffers_));
      ASSERT_EQ(VPX_CODEC_OK,
                decoder->SetExternalFrameBufferFunctions(
                    GetVp9FrameBuffer, ReleaseVp9FrameBuffer, this));
    }
  }

  void OpenMD5File(const std::string &md5_file_name_) {
    md5_file_ = libvpx_test::OpenTestDataFile(md5_file_name_);
    ASSERT_TRUE(md5_file_ != NULL) << "Md5 file open failed. Filename: "
        << md5_file_name_;
  }

  virtual void DecompressedFrameHook(const vpx_image_t &img,
                                     const unsigned int frame_number) {
    ASSERT_TRUE(md5_file_ != NULL);
    char expected_md5[33];
    char junk[128];

    // Read correct md5 checksums.
    const int res = fscanf(md5_file_, "%s  %s", expected_md5, junk);
    ASSERT_NE(EOF, res) << "Read md5 data failed";
    expected_md5[32] = '\0';

    ::libvpx_test::MD5 md5_res;
    md5_res.Add(&img);
    const char *const actual_md5 = md5_res.Get();

    // Check md5 match.
    ASSERT_STREQ(expected_md5, actual_md5)
        << "Md5 checksums don't match: frame number = " << frame_number;
  }

  // Callback to get a free external frame buffer. Return value < 0 is an
  // error.
  static int GetVp9FrameBuffer(void *user_priv, size_t min_size,
                               vpx_codec_frame_buffer_t *fb) {
    ExternalFrameBufferMD5Test *const md5Test =
        static_cast<ExternalFrameBufferMD5Test*>(user_priv);
    return md5Test->fb_list_.GetFreeFrameBuffer(min_size, fb);
  }

  // Callback to release an external frame buffer. Return value < 0 is an
  // error.
  static int ReleaseVp9FrameBuffer(void *user_priv,
                                   vpx_codec_frame_buffer_t *fb) {
    ExternalFrameBufferMD5Test *const md5Test =
        static_cast<ExternalFrameBufferMD5Test*>(user_priv);
    return md5Test->fb_list_.ReturnFrameBuffer(fb);
  }

  void set_num_buffers(int num_buffers) { num_buffers_ = num_buffers; }
  int num_buffers() const { return num_buffers_; }

 private:
  FILE *md5_file_;
  int num_buffers_;
  ExternalFrameBufferList fb_list_;
};

// Class for testing passing in external frame buffers to libvpx.
class ExternalFrameBufferTest : public ::testing::Test {
 protected:
  ExternalFrameBufferTest()
      : video_(NULL),
        decoder_(NULL),
        num_buffers_(0) {}

  virtual void SetUp() {
    video_ = new libvpx_test::WebMVideoSource(kVP9TestFile);
    video_->Init();
    video_->Begin();

    vpx_codec_dec_cfg_t cfg = {0};
    decoder_ = new libvpx_test::VP9Decoder(cfg, 0);
  }

  virtual void TearDown() {
    delete decoder_;
    delete video_;
  }

  // Passes the external frame buffer information to libvpx.
  vpx_codec_err_t SetExternalFrameBufferFunctions(
      int num_buffers,
      vpx_get_frame_buffer_cb_fn_t cb_get,
      vpx_release_frame_buffer_cb_fn_t cb_release) {
    if (num_buffers > 0) {
      num_buffers_ = num_buffers;
      EXPECT_TRUE(fb_list_.CreateBufferList(num_buffers_));
    }

    return decoder_->SetExternalFrameBufferFunctions(
        cb_get, cb_release, (void*)&fb_list_);
  }

  vpx_codec_err_t DecodeOneFrame() {
    const vpx_codec_err_t res =
        decoder_->DecodeFrame(video_->cxdata(), video_->frame_size());
    CheckDecodedFrames();
    if (res == VPX_CODEC_OK)
      video_->Next();
    return res;
  }

  vpx_codec_err_t DecodeRemainingFrames() {
    for (; video_->cxdata(); video_->Next()) {
      const vpx_codec_err_t res =
          decoder_->DecodeFrame(video_->cxdata(), video_->frame_size());
      if (res != VPX_CODEC_OK)
        return res;
      CheckDecodedFrames();
    }
    return VPX_CODEC_OK;
  }

 private:
  void CheckDecodedFrames() {
    libvpx_test::DxDataIterator dec_iter = decoder_->GetDxData();
    const vpx_image_t *img = NULL;

    // Get decompressed data
    while ((img = dec_iter.Next())) {
      fb_list_.CheckXImageFrameBuffer(img);
    }
  }

  libvpx_test::WebMVideoSource *video_;
  libvpx_test::VP9Decoder *decoder_;
  int num_buffers_;
  ExternalFrameBufferList fb_list_;
};

// This test runs through the set of test vectors, and decodes them.
// Libvpx will call into the application to allocate a frame buffer when
// needed. The md5 checksums are computed for each frame in the video file.
// If md5 checksums match the correct md5 data, then the test is passed.
// Otherwise, the test failed.
TEST_P(ExternalFrameBufferMD5Test, ExtFBMD5Match) {
  const std::string filename = GET_PARAM(kVideoNameParam);
  libvpx_test::CompressedVideoSource *video = NULL;

  // Number of buffers equals #VP9_MAXIMUM_REF_BUFFERS +
  // #VPX_MAXIMUM_WORK_BUFFERS + four jitter buffers.
  const int jitter_buffers = 4;
  const int num_buffers =
      VP9_MAXIMUM_REF_BUFFERS + VPX_MAXIMUM_WORK_BUFFERS + jitter_buffers;
  set_num_buffers(num_buffers);

#if CONFIG_VP8_DECODER
  // Tell compiler we are not using kVP8TestVectors.
  (void)libvpx_test::kVP8TestVectors;
#endif

  // Open compressed video file.
  if (filename.substr(filename.length() - 3, 3) == "ivf") {
    video = new libvpx_test::IVFVideoSource(filename);
  } else if (filename.substr(filename.length() - 4, 4) == "webm") {
    video = new libvpx_test::WebMVideoSource(filename);
  }
  video->Init();

  // Construct md5 file name.
  const std::string md5_filename = filename + ".md5";
  OpenMD5File(md5_filename);

  // Decode frame, and check the md5 matching.
  ASSERT_NO_FATAL_FAILURE(RunLoop(video));
  delete video;
}

TEST_F(ExternalFrameBufferTest, MinFrameBuffers) {
  // Minimum number of external frame buffers for VP9 is
  // #VP9_MAXIMUM_REF_BUFFERS + #VPX_MAXIMUM_WORK_BUFFERS.
  const int num_buffers = VP9_MAXIMUM_REF_BUFFERS + VPX_MAXIMUM_WORK_BUFFERS;
  ASSERT_EQ(VPX_CODEC_OK,
            SetExternalFrameBufferFunctions(
                num_buffers, get_vp9_frame_buffer, release_vp9_frame_buffer));
  ASSERT_EQ(VPX_CODEC_OK, DecodeRemainingFrames());
}

TEST_F(ExternalFrameBufferTest, EightJitterBuffers) {
  // Number of buffers equals #VP9_MAXIMUM_REF_BUFFERS +
  // #VPX_MAXIMUM_WORK_BUFFERS + eight jitter buffers.
  const int jitter_buffers = 8;
  const int num_buffers =
      VP9_MAXIMUM_REF_BUFFERS + VPX_MAXIMUM_WORK_BUFFERS + jitter_buffers;
  ASSERT_EQ(VPX_CODEC_OK,
            SetExternalFrameBufferFunctions(
                num_buffers, get_vp9_frame_buffer, release_vp9_frame_buffer));
  ASSERT_EQ(VPX_CODEC_OK, DecodeRemainingFrames());
}

TEST_F(ExternalFrameBufferTest, NotEnoughBuffers) {
  // Minimum number of external frame buffers for VP9 is
  // #VP9_MAXIMUM_REF_BUFFERS + #VPX_MAXIMUM_WORK_BUFFERS. Most files will
  // only use 5 frame buffers at one time.
  const int num_buffers = 2;
  ASSERT_EQ(VPX_CODEC_OK,
            SetExternalFrameBufferFunctions(
                num_buffers, get_vp9_frame_buffer, release_vp9_frame_buffer));
  ASSERT_EQ(VPX_CODEC_OK, DecodeOneFrame());
  ASSERT_EQ(VPX_CODEC_MEM_ERROR, DecodeRemainingFrames());
}

TEST_F(ExternalFrameBufferTest, NoRelease) {
  const int num_buffers = VP9_MAXIMUM_REF_BUFFERS + VPX_MAXIMUM_WORK_BUFFERS;
  ASSERT_EQ(VPX_CODEC_OK,
            SetExternalFrameBufferFunctions(
                num_buffers, get_vp9_frame_buffer,
                do_not_release_vp9_frame_buffer));
  ASSERT_EQ(VPX_CODEC_OK, DecodeOneFrame());
  ASSERT_EQ(VPX_CODEC_MEM_ERROR, DecodeRemainingFrames());
}

TEST_F(ExternalFrameBufferTest, NullRealloc) {
  const int num_buffers = VP9_MAXIMUM_REF_BUFFERS + VPX_MAXIMUM_WORK_BUFFERS;
  ASSERT_EQ(VPX_CODEC_OK,
            SetExternalFrameBufferFunctions(num_buffers,
                                    get_vp9_zero_frame_buffer, release_vp9_frame_buffer));
  ASSERT_EQ(VPX_CODEC_MEM_ERROR, DecodeOneFrame());
}

TEST_F(ExternalFrameBufferTest, ReallocOneLessByte) {
  const int num_buffers = VP9_MAXIMUM_REF_BUFFERS + VPX_MAXIMUM_WORK_BUFFERS;
  ASSERT_EQ(VPX_CODEC_OK,
            SetExternalFrameBufferFunctions(
                num_buffers, get_vp9_one_less_byte_frame_buffer,
                release_vp9_frame_buffer));
  ASSERT_EQ(VPX_CODEC_MEM_ERROR, DecodeOneFrame());
}

TEST_F(ExternalFrameBufferTest, NullGetFunction) {
  const int num_buffers = VP9_MAXIMUM_REF_BUFFERS + VPX_MAXIMUM_WORK_BUFFERS;
  ASSERT_EQ(VPX_CODEC_INVALID_PARAM,
            SetExternalFrameBufferFunctions(
                num_buffers, NULL, release_vp9_frame_buffer));
}

TEST_F(ExternalFrameBufferTest, NullReleaseFunction) {
  const int num_buffers = VP9_MAXIMUM_REF_BUFFERS + VPX_MAXIMUM_WORK_BUFFERS;
  ASSERT_EQ(VPX_CODEC_INVALID_PARAM,
            SetExternalFrameBufferFunctions(
                num_buffers, get_vp9_frame_buffer, NULL));
}

VP9_INSTANTIATE_TEST_CASE(ExternalFrameBufferMD5Test,
                          ::testing::ValuesIn(libvpx_test::kVP9TestVectors));
}  // namespace
