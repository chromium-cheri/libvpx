/*
 *  Copyright (c) 2012 The WebM project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "third_party/googletest/src/include/gtest/gtest.h"
#include "test/acm_random.h"
#include "test/clear_system_state.h"
#include "test/register_state_check.h"
#include "test/util.h"

#include "./vp9_rtcd.h"
#include "vp9/common/vp9_entropy.h"
#include "vpx/vpx_integer.h"

extern "C" {
void vp9_idct4x4_16_add_c(const tran_low_t *input, uint8_t *output, int pitch);
}

using libvpx_test::ACMRandom;

namespace {
const int kNumCoeffs = 16;
typedef void (*fdct_t)(const int16_t *in, tran_low_t *out, int stride);
typedef void (*idct_t)(const tran_low_t *in, uint8_t *out, int stride);
typedef void (*fht_t) (const int16_t *in, tran_low_t *out, int stride,
                       int tx_type);
typedef void (*iht_t) (const tran_low_t *in, uint8_t *out, int stride,
                       int tx_type);

typedef std::tr1::tuple<fdct_t, idct_t, int, int> dct_4x4_param_t;
typedef std::tr1::tuple<fht_t, iht_t, int, int> ht_4x4_param_t;

void fdct4x4_ref(const int16_t *in, tran_low_t *out, int stride, int tx_type) {
  vp9_fdct4x4_c(in, out, stride);
}

void fht4x4_ref(const int16_t *in, tran_low_t *out, int stride, int tx_type) {
  vp9_fht4x4_c(in, out, stride, tx_type);
}

void fwht4x4_ref(const int16_t *in, tran_low_t *out, int stride, int tx_type) {
  vp9_fwht4x4_c(in, out, stride);
}

#if CONFIG_VP9_HIGH
void idct4x4_10(const tran_low_t *in, uint8_t *out, int stride) {
  vp9_high_idct4x4_16_add_c(in, out, stride, 10);
}

void idct4x4_12(const tran_low_t *in, uint8_t *out, int stride) {
  vp9_high_idct4x4_16_add_c(in, out, stride, 12);
}

void iht4x4_10(const tran_low_t *in, uint8_t *out, int stride, int tx_type) {
  vp9_high_iht4x4_16_add_c(in, out, stride, tx_type, 10);
}

void iht4x4_12(const tran_low_t *in, uint8_t *out, int stride, int tx_type) {
  vp9_high_iht4x4_16_add_c(in, out, stride, tx_type, 12);
}

void iwht4x4_10(const tran_low_t *in, uint8_t *out, int stride) {
  vp9_high_iwht4x4_16_add_c(in, out, stride, 10);
}

void iwht4x4_12(const tran_low_t *in, uint8_t *out, int stride) {
  vp9_high_iwht4x4_16_add_c(in, out, stride, 12);
}
#endif

class Trans4x4TestBase {
 public:
  virtual ~Trans4x4TestBase() {}

 protected:
  virtual void RunFwdTxfm(const int16_t *in, tran_low_t *out, int stride) = 0;

  virtual void RunInvTxfm(const tran_low_t *out, uint8_t *dst, int stride) = 0;

  void RunAccuracyCheck(int limit) {
    ACMRandom rnd(ACMRandom::DeterministicSeed());
    uint32_t max_error = 0;
    int64_t total_error = 0;
    const int count_test_block = 10000;
    for (int i = 0; i < count_test_block; ++i) {
      DECLARE_ALIGNED_ARRAY(16, int16_t, test_input_block, kNumCoeffs);
      DECLARE_ALIGNED_ARRAY(16, tran_low_t, test_temp_block, kNumCoeffs);
      DECLARE_ALIGNED_ARRAY(16, uint8_t, dst, kNumCoeffs);
      DECLARE_ALIGNED_ARRAY(16, uint16_t, dst16, kNumCoeffs);
      DECLARE_ALIGNED_ARRAY(16, uint8_t, src, kNumCoeffs);
      DECLARE_ALIGNED_ARRAY(16, uint16_t, src16, kNumCoeffs);

      // Initialize a test block with input range [-255, 255].
      for (int j = 0; j < kNumCoeffs; ++j) {
        if (bit_depth_ == 8) {
          src[j] = rnd.Rand8();
          dst[j] = rnd.Rand8();
          test_input_block[j] = src[j] - dst[j];
        } else {
          src16[j] = rnd.Rand16() & mask_;
          dst16[j] = rnd.Rand16() & mask_;
          test_input_block[j] = src16[j] - dst16[j];
        }
      }

      REGISTER_STATE_CHECK(RunFwdTxfm(test_input_block,
                                      test_temp_block, pitch_));
      if (bit_depth_ == 8)
        REGISTER_STATE_CHECK(RunInvTxfm(test_temp_block, dst, pitch_));
#if CONFIG_VP9_HIGH
      else
        REGISTER_STATE_CHECK(RunInvTxfm(test_temp_block,
                                        CONVERT_TO_BYTEPTR(dst16), pitch_));
#endif

      for (int j = 0; j < kNumCoeffs; ++j) {
        const uint32_t diff = bit_depth_ == 8 ? dst[j] - src[j] :
                                                dst16[j] - src16[j];
        const uint32_t error = diff * diff;
        if (max_error < error)
          max_error = error;
        total_error += error;
      }
    }

    EXPECT_GE(static_cast<uint32_t>(limit), max_error)
        << "Error: 4x4 FHT/IHT has an individual round trip error > "
        << limit;

    EXPECT_GE(count_test_block * limit, total_error)
        << "Error: 4x4 FHT/IHT has average round trip error > " << limit
        << " per block";
  }

  void RunCoeffCheck() {
    ACMRandom rnd(ACMRandom::DeterministicSeed());
    const int count_test_block = 5000;
    DECLARE_ALIGNED_ARRAY(16, int16_t, input_block, kNumCoeffs);
    DECLARE_ALIGNED_ARRAY(16, tran_low_t, output_ref_block, kNumCoeffs);
    DECLARE_ALIGNED_ARRAY(16, tran_low_t, output_block, kNumCoeffs);

    for (int i = 0; i < count_test_block; ++i) {
      // Initialize a test block with input range [-255, 255].
      for (int j = 0; j < kNumCoeffs; ++j)
        input_block[j] = (rnd.Rand16() & mask_) - (rnd.Rand16() & mask_);

      fwd_txfm_ref(input_block, output_ref_block, pitch_, tx_type_);
      REGISTER_STATE_CHECK(RunFwdTxfm(input_block, output_block, pitch_));

      // The minimum quant value is 4.
      for (int j = 0; j < kNumCoeffs; ++j)
        EXPECT_EQ(output_block[j], output_ref_block[j]);
    }
  }

  void RunMemCheck() {
    ACMRandom rnd(ACMRandom::DeterministicSeed());
    const int count_test_block = 5000;
    DECLARE_ALIGNED_ARRAY(16, int16_t, input_block, kNumCoeffs);
    DECLARE_ALIGNED_ARRAY(16, int16_t, input_extreme_block, kNumCoeffs);
    DECLARE_ALIGNED_ARRAY(16, tran_low_t, output_ref_block, kNumCoeffs);
    DECLARE_ALIGNED_ARRAY(16, tran_low_t, output_block, kNumCoeffs);

    for (int i = 0; i < count_test_block; ++i) {
      // Initialize a test block with input range [-mask_, mask_].
      for (int j = 0; j < kNumCoeffs; ++j) {
        input_block[j] = (rnd.Rand16() & mask_) - (rnd.Rand16() & mask_);
        input_extreme_block[j] = rnd.Rand8() % 2 ? mask_ : -mask_;
      }
      if (i == 0) {
        for (int j = 0; j < kNumCoeffs; ++j)
          input_extreme_block[j] = mask_;
      } else if (i == 1) {
        for (int j = 0; j < kNumCoeffs; ++j)
          input_extreme_block[j] = -mask_;
      }

      fwd_txfm_ref(input_extreme_block, output_ref_block, pitch_, tx_type_);
      REGISTER_STATE_CHECK(RunFwdTxfm(input_extreme_block,
                                      output_block, pitch_));

      // The minimum quant value is 4.
      for (int j = 0; j < kNumCoeffs; ++j) {
        EXPECT_EQ(output_block[j], output_ref_block[j]);
        EXPECT_GE(4 * DCT_MAX_VALUE << (bit_depth_ - 8), abs(output_block[j]))
            << "Error: 4x4 FDCT has coefficient larger than 4*DCT_MAX_VALUE";
      }
    }
  }

  void RunInvAccuracyCheck(int limit) {
    ACMRandom rnd(ACMRandom::DeterministicSeed());
    const int count_test_block = 1000;
    DECLARE_ALIGNED_ARRAY(16, int16_t, in, kNumCoeffs);
    DECLARE_ALIGNED_ARRAY(16, tran_low_t, coeff, kNumCoeffs);
    DECLARE_ALIGNED_ARRAY(16, uint8_t, dst, kNumCoeffs);
    DECLARE_ALIGNED_ARRAY(16, uint16_t, dst16, kNumCoeffs);
    DECLARE_ALIGNED_ARRAY(16, uint8_t, src, kNumCoeffs);
    DECLARE_ALIGNED_ARRAY(16, uint16_t, src16, kNumCoeffs);

    for (int i = 0; i < count_test_block; ++i) {
      // Initialize a test block with input range [-mask_, mask_].
      for (int j = 0; j < kNumCoeffs; ++j) {
        if (bit_depth_ == 8) {
          src[j] = rnd.Rand8();
          dst[j] = rnd.Rand8();
          in[j] = src[j] - dst[j];
        } else {
          src16[j] = rnd.Rand16() & mask_;
          dst16[j] = rnd.Rand16() & mask_;
          in[j] = src16[j] - dst16[j];
        }
      }

      fwd_txfm_ref(in, coeff, pitch_, tx_type_);

      if (bit_depth_ == 8)
        REGISTER_STATE_CHECK(RunInvTxfm(coeff, dst, pitch_));
#if CONFIG_VP9_HIGH
      else
        REGISTER_STATE_CHECK(RunInvTxfm(coeff, CONVERT_TO_BYTEPTR(dst16),
                                        pitch_));
#endif

      for (int j = 0; j < kNumCoeffs; ++j) {
        const uint32_t diff = bit_depth_ == 8 ? dst[j] - src[j] :
                                                dst16[j] - src16[j];
        const uint32_t error = diff * diff;
        EXPECT_GE(static_cast<uint32_t>(limit), error)
            << "Error: 4x4 IDCT has error " << error
            << " at index " << j;
      }
    }
  }

  int pitch_;
  int tx_type_;
  fht_t fwd_txfm_ref;
  int bit_depth_;
  int mask_;
};

class Trans4x4DCT
    : public Trans4x4TestBase,
      public ::testing::TestWithParam<dct_4x4_param_t> {
 public:
  virtual ~Trans4x4DCT() {}

  virtual void SetUp() {
    fwd_txfm_  = GET_PARAM(0);
    inv_txfm_  = GET_PARAM(1);
    tx_type_   = GET_PARAM(2);
    bit_depth_ = GET_PARAM(3);
    pitch_     = 4;
    fwd_txfm_ref = fdct4x4_ref;
    mask_ = (1 << bit_depth_) - 1;
  }
  virtual void TearDown() { libvpx_test::ClearSystemState(); }

 protected:
  void RunFwdTxfm(const int16_t *in, tran_low_t *out, int stride) {
    fwd_txfm_(in, out, stride);
  }
  void RunInvTxfm(const tran_low_t *out, uint8_t *dst, int stride) {
    inv_txfm_(out, dst, stride);
  }

  fdct_t fwd_txfm_;
  idct_t inv_txfm_;
};

TEST_P(Trans4x4DCT, AccuracyCheck) {
  RunAccuracyCheck(1);
}

TEST_P(Trans4x4DCT, CoeffCheck) {
  RunCoeffCheck();
}

TEST_P(Trans4x4DCT, MemCheck) {
  RunMemCheck();
}

TEST_P(Trans4x4DCT, InvAccuracyCheck) {
  RunInvAccuracyCheck(1);
}

class Trans4x4HT
    : public Trans4x4TestBase,
      public ::testing::TestWithParam<ht_4x4_param_t> {
 public:
  virtual ~Trans4x4HT() {}

  virtual void SetUp() {
    fwd_txfm_ = GET_PARAM(0);
    inv_txfm_ = GET_PARAM(1);
    tx_type_  = GET_PARAM(2);
    bit_depth_ = GET_PARAM(3);
    pitch_    = 4;
    fwd_txfm_ref = fht4x4_ref;
    mask_ = (1 << bit_depth_) - 1;
  }
  virtual void TearDown() { libvpx_test::ClearSystemState(); }

 protected:
  void RunFwdTxfm(const int16_t *in, tran_low_t *out, int stride) {
    fwd_txfm_(in, out, stride, tx_type_);
  }

  void RunInvTxfm(const tran_low_t *out, uint8_t *dst, int stride) {
    inv_txfm_(out, dst, stride, tx_type_);
  }

  fht_t fwd_txfm_;
  iht_t inv_txfm_;
};

TEST_P(Trans4x4HT, AccuracyCheck) {
  RunAccuracyCheck(1);
}

TEST_P(Trans4x4HT, CoeffCheck) {
  RunCoeffCheck();
}

TEST_P(Trans4x4HT, MemCheck) {
  RunMemCheck();
}

TEST_P(Trans4x4HT, InvAccuracyCheck) {
  RunInvAccuracyCheck(1);
}

class Trans4x4WHT
    : public Trans4x4TestBase,
      public ::testing::TestWithParam<dct_4x4_param_t> {
 public:
  virtual ~Trans4x4WHT() {}

  virtual void SetUp() {
    fwd_txfm_  = GET_PARAM(0);
    inv_txfm_  = GET_PARAM(1);
    tx_type_   = GET_PARAM(2);
    bit_depth_ = GET_PARAM(3);
    pitch_     = 4;
    fwd_txfm_ref = fwht4x4_ref;
    mask_ = (1 << bit_depth_) - 1;
  }
  virtual void TearDown() { libvpx_test::ClearSystemState(); }

 protected:
  void RunFwdTxfm(const int16_t *in, tran_low_t *out, int stride) {
    fwd_txfm_(in, out, stride);
  }
  void RunInvTxfm(const tran_low_t *out, uint8_t *dst, int stride) {
    inv_txfm_(out, dst, stride);
  }

  fdct_t fwd_txfm_;
  idct_t inv_txfm_;
};

TEST_P(Trans4x4WHT, AccuracyCheck) {
  RunAccuracyCheck(0);
}

TEST_P(Trans4x4WHT, CoeffCheck) {
  RunCoeffCheck();
}

TEST_P(Trans4x4WHT, MemCheck) {
  RunMemCheck();
}

TEST_P(Trans4x4WHT, InvAccuracyCheck) {
  RunInvAccuracyCheck(0);
}
using std::tr1::make_tuple;


INSTANTIATE_TEST_CASE_P(
    C, Trans4x4DCT,
    ::testing::Values(
#if CONFIG_VP9_HIGH && CONFIG_HIGH_TRANSFORMS
        make_tuple(&vp9_fdct4x4_c, &vp9_idct4x4_16_add_c, 0, 8),
        make_tuple(&vp9_high_fdct4x4_c, &idct4x4_10, 0, 10),
        make_tuple(&vp9_high_fdct4x4_c, &idct4x4_12, 0, 12)));
#else
        make_tuple(&vp9_fdct4x4_c, &vp9_idct4x4_16_add_c, 0, 8)));
#endif
INSTANTIATE_TEST_CASE_P(
    C, Trans4x4HT,
    ::testing::Values(
#if CONFIG_VP9_HIGH && CONFIG_HIGH_TRANSFORMS
        make_tuple(&vp9_fht4x4_c, &vp9_iht4x4_16_add_c, 0, 8),
        make_tuple(&vp9_fht4x4_c, &vp9_iht4x4_16_add_c, 1, 8),
        make_tuple(&vp9_fht4x4_c, &vp9_iht4x4_16_add_c, 2, 8),
        make_tuple(&vp9_fht4x4_c, &vp9_iht4x4_16_add_c, 3, 8),
        make_tuple(&vp9_high_fht4x4_c, &iht4x4_10, 0, 10),
        make_tuple(&vp9_high_fht4x4_c, &iht4x4_10, 1, 10),
        make_tuple(&vp9_high_fht4x4_c, &iht4x4_10, 2, 10),
        make_tuple(&vp9_high_fht4x4_c, &iht4x4_10, 3, 10),
        make_tuple(&vp9_high_fht4x4_c, &iht4x4_12, 0, 12),
        make_tuple(&vp9_high_fht4x4_c, &iht4x4_12, 1, 12),
        make_tuple(&vp9_high_fht4x4_c, &iht4x4_12, 2, 12),
        make_tuple(&vp9_high_fht4x4_c, &iht4x4_12, 3, 12)));
#else
        make_tuple(&vp9_fht4x4_c, &vp9_iht4x4_16_add_c, 0, 8),
        make_tuple(&vp9_fht4x4_c, &vp9_iht4x4_16_add_c, 1, 8),
        make_tuple(&vp9_fht4x4_c, &vp9_iht4x4_16_add_c, 2, 8),
        make_tuple(&vp9_fht4x4_c, &vp9_iht4x4_16_add_c, 3, 8)));
#endif
INSTANTIATE_TEST_CASE_P(
    C, Trans4x4WHT,
    ::testing::Values(
#if CONFIG_VP9_HIGH && CONFIG_HIGH_TRANSFORMS
        make_tuple(&vp9_fwht4x4_c, &vp9_iwht4x4_16_add_c, 0, 8),
        make_tuple(&vp9_high_fwht4x4_c, &iwht4x4_10, 0, 10),
        make_tuple(&vp9_high_fwht4x4_c, &iwht4x4_12, 0, 12)));
#else
        make_tuple(&vp9_fwht4x4_c, &vp9_iwht4x4_16_add_c, 0, 8)));
#endif

#if HAVE_NEON_ASM && !CONFIG_HIGH_TRANSFORMS
INSTANTIATE_TEST_CASE_P(
    NEON, Trans4x4DCT,
    ::testing::Values(
        make_tuple(&vp9_fdct4x4_c,
                   &vp9_idct4x4_16_add_neon, 0, 8)));
INSTANTIATE_TEST_CASE_P(
    DISABLED_NEON, Trans4x4HT,
    ::testing::Values(
        make_tuple(&vp9_fht4x4_c, &vp9_iht4x4_16_add_neon, 0, 8),
        make_tuple(&vp9_fht4x4_c, &vp9_iht4x4_16_add_neon, 1, 8),
        make_tuple(&vp9_fht4x4_c, &vp9_iht4x4_16_add_neon, 2, 8),
        make_tuple(&vp9_fht4x4_c, &vp9_iht4x4_16_add_neon, 3, 8)));
#endif

#if CONFIG_USE_X86INC && HAVE_MMX && !CONFIG_HIGH_TRANSFORMS
INSTANTIATE_TEST_CASE_P(
    MMX, Trans4x4WHT,
    ::testing::Values(
        make_tuple(&vp9_fwht4x4_mmx, &vp9_iwht4x4_16_add_c, 0, 8)));
#endif

#if HAVE_SSE2 && !CONFIG_HIGH_TRANSFORMS
INSTANTIATE_TEST_CASE_P(
    SSE2, Trans4x4DCT,
    ::testing::Values(
        make_tuple(&vp9_fdct4x4_sse2,
                   &vp9_idct4x4_16_add_sse2, 0, 8)));
INSTANTIATE_TEST_CASE_P(
    SSE2, Trans4x4HT,
    ::testing::Values(
        make_tuple(&vp9_fht4x4_sse2, &vp9_iht4x4_16_add_sse2, 0, 8),
        make_tuple(&vp9_fht4x4_sse2, &vp9_iht4x4_16_add_sse2, 1, 8),
        make_tuple(&vp9_fht4x4_sse2, &vp9_iht4x4_16_add_sse2, 2, 8),
        make_tuple(&vp9_fht4x4_sse2, &vp9_iht4x4_16_add_sse2, 3, 8)));
#endif

#if HAVE_AVX2
INSTANTIATE_TEST_CASE_P(
    AVX2, Trans4x4DCT,
    ::testing::Values(
        make_tuple(&vp9_fdct4x4_avx2,
                   &vp9_idct4x4_16_add_c, 0)));
INSTANTIATE_TEST_CASE_P(
    AVX2, Trans4x4HT,
    ::testing::Values(
        make_tuple(&vp9_fht4x4_avx2, &vp9_iht4x4_16_add_c, 0),
        make_tuple(&vp9_fht4x4_avx2, &vp9_iht4x4_16_add_c, 1),
        make_tuple(&vp9_fht4x4_avx2, &vp9_iht4x4_16_add_c, 2),
        make_tuple(&vp9_fht4x4_avx2, &vp9_iht4x4_16_add_c, 3)));
#endif

}  // namespace
