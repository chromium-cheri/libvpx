/*
 *  Copyright (c) 2012 The WebM project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#ifndef TEST_BUFFER_H_
#define TEST_BUFFER_H_

#include "third_party/googletest/src/include/gtest/gtest.h"

#include "vpx/vpx_integer.h"

namespace libvpx_test {

template <typename T>

class Buffer {
 public:
  Buffer() : x_(0), y_(0), p_(0), b_(NULL) {}
  Buffer(int x, int y, int p) : x_(x), y_(y), p_(p) {
    assert(x_ > 0);
    assert(y_ > 0);
    assert(p_ >= 0);
    stride_ = x_ + 2 * p_;
    raw_size_ = stride_ * (y_ + 2 * p_);
    b_ = new T[raw_size_];
    SetPadding();
  }

  // Return the address of the top left pixel.
  T *Src() {
    if (!b_) return NULL;
    return b_ + (p_ * stride_) + p_;
  }

  int Stride() { return stride_; }

  // Set the buffer (excluding padding) to a.
  void Set(int a) {
    if (p_) {
      T *src = Src();
      for (int i = 0; i < y_; ++i) {
        memset(src, a, sizeof(T) * x_);
        src += stride_;
      }
    } else {  // No border.
      memset(b_, a, sizeof(T) * raw_size_);
    }
  }

  void DumpBuffer() {
    for (int y = 0; y < y_ + 2 * p_; ++y) {
      for (int x = 0; x < stride_; ++x) {
        printf("%4d", b_[x + y * stride_]);
      }
      printf("\n");
    }
  }

  void SetPadding() { SetPadding(255); }

  void SetPadding(int a) {
    if (0 == p_) {
      return;
    }

    p_v_ = a;

    // Top padding.
    memset(b_, p_v_, stride_ * p_);

    // Left padding.
    T *left = Src() - p_;
    for (int y = 0; y < y_; ++y) {
      for (int x = 0; x < p_; ++x) {
        left[x] = p_v_;
      }
      left += stride_;
    }

    // Right padding.
    T *right = Src() + x_;
    for (int y = 0; y < y_; ++y) {
      for (int x = 0; x < p_; ++x) {
        right[x] = p_v_;
      }
      right += stride_;
    }

    // Bottom padding
    T *bottom = b_ + (p_ + y_) * stride_;
    memset(bottom, p_v_, stride_ * p_);
  }

  // Returns 0 if all the values (excluding padding) are equal to 'a'.
  int CheckValues(int a) {
    for (int y = 0; y < y_; ++y) {
      for (int x = 0; x < x_; ++x) {
        if (a != *(Src() + x + y * stride_)) {
          printf("Buffer does not match %d.\n", a);
          DumpBuffer();
          return 1;
        }
      }
    }
    return 0;
  }

  // Returns 0 when padding matches the expected value or there is no padding.
  int CheckPadding() {
    if (0 == p_) {
      return 0;
    }

    int error = 0;
    // Top padding.
    T *top = b_;
    for (int i = 0; i < stride_ * p_; ++i) {
      if (p_v_ != top[i]) {
        error = 1;
      }
    }

    // Left padding.
    T *left = Src() - p_;
    for (int y = 0; y < y_; ++y) {
      for (int x = 0; x < p_; ++x) {
        if(p_v_ != left[x]) {
          error = 1;
        }
      }
      left += stride_;
    }

    // Right padding.
    T *right = Src() + x_;
    for (int y = 0; y < y_; ++y) {
      for (int x = 0; x < p_; ++x) {
        if (p_v_ != right[x]) {
          error = 1;
        }
      }
      right += stride_;
    }

    // Bottom padding
    T *bottom = b_ + (p_ + y_) * stride_;
    for (int i = 0; i < stride_ * p_; ++i) {
      if (p_v_ != bottom[i]) {
        error = 1;
      }
    }

    if (error) {
      printf("Padding does not match %d.\n", p_v_);
      DumpBuffer();
    }

    return error;
  }

  virtual void TearDown() {
    if (b_) {
      delete[] b_;
    }
    b_ = NULL;
  }

 private:
  // testing::internal::Random random_;
  int x_;         // Columns or width.
  int y_;         // Rows or height.
  int p_;         // Padding on each side.
  int p_v_;       // Value stored in padding.
  int stride_;    // Padding + width + padding.
  int raw_size_;  // The total number of bytes including padding.
  T *b_;          // The actual buffer.
};

}  // namespace libvpx_test

#endif  // TEST_BUFFER_H_
