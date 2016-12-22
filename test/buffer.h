/*
 *  Copyright (c) 2016 The WebM project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#ifndef TEST_BUFFER_H_
#define TEST_BUFFER_H_

#include <limits>

#include "third_party/googletest/src/include/gtest/gtest.h"

#include "vpx/vpx_integer.h"

namespace libvpx_test {

template <typename T>
class Buffer {
 public:
  Buffer(int width, int height, int top_padding, int left_padding,
         int right_padding, int bottom_padding)
      : width_(width), height_(height), top_padding_(top_padding),
        left_padding_(left_padding), right_padding_(right_padding),
        bottom_padding_(bottom_padding) {
    assert(width_ > 0);
    assert(height_ > 0);
    assert(top_padding_ >= 0);
    assert(left_padding_ >= 0);
    assert(right_padding_ >= 0);
    assert(bottom_padding_ >= 0);
    stride_ = left_padding_ + width_ + right_padding_;
    raw_size_ = stride_ * (top_padding_ + height_ + bottom_padding_);
    raw_buffer_ = new T[raw_size_];
    if (NULL == raw_buffer_) {
      printf("Failed to allocate buffer\n");
      Buffer::~Buffer();
      return;
    }
    SetPadding(std::numeric_limits<T>::max());
  }

  Buffer(int width, int height, int padding)
      : width_(width), height_(height), top_padding_(padding),
        left_padding_(padding), right_padding_(padding),
        bottom_padding_(padding) {
    assert(width_ > 0);
    assert(height_ > 0);
    assert(top_padding_ >= 0);
    stride_ = left_padding_ + width_ + right_padding_;
    raw_size_ = stride_ * (top_padding_ + height_ + bottom_padding_);
    raw_buffer_ = new T[raw_size_];
    if (NULL == raw_buffer_) {
      printf("Failed to allocate buffer\n");
      Buffer::~Buffer();
      return;
    }
    SetPadding(std::numeric_limits<T>::max());
  }

  T *TopLeftPixel();

  int Stride() { return stride_; }

  // Set the buffer (excluding padding) to 'value'.
  void Set(const int value);

  void DumpBuffer();

  bool HasPadding() {
    bool condition;
    (top_padding_ || left_padding_ || right_padding_ || bottom_padding_)
        ? condition = true
        : condition = false;
    return condition;
  }

  // Sets all the values in the buffer to 'padding_value'.
  void SetPadding(const int padding_value);

  // Checks if all the values (excluding padding) are equal to 'value'.
  bool CheckValues(const int value);

  // Check that padding matches the expected value or there is no padding.
  bool CheckPadding();

  ~Buffer() {
    if (raw_buffer_) {
      delete[] raw_buffer_;
    }
    raw_buffer_ = NULL;
  }

 private:
  const int width_;
  const int height_;
  const int top_padding_;
  const int left_padding_;
  const int right_padding_;
  const int bottom_padding_;
  int padding_value_;
  int stride_;
  int raw_size_;
  T *raw_buffer_;
};

template <typename T>
T *Buffer<T>::TopLeftPixel() {
  return raw_buffer_ ? raw_buffer_ + (top_padding_ * Stride()) + left_padding_
                     : NULL;
}

template <typename T>
void Buffer<T>::Set(const int value) {
  if (HasPadding()) {
    T *src = TopLeftPixel();
    for (int i = 0; i < height_; ++i) {
      memset(src, value, sizeof(T) * width_);
      src += Stride();
    }
  } else {
    memset(raw_buffer_, value, sizeof(T) * raw_size_);
  }
}

template <typename T>
void Buffer<T>::DumpBuffer() {
  for (int height = 0; height < height_ + top_padding_ + bottom_padding_;
       ++height) {
    for (int width = 0; width < Stride(); ++width) {
      printf("%4d", raw_buffer_[height + width * Stride()]);
    }
    printf("\n");
  }
}

template <typename T>
void Buffer<T>::SetPadding(const int padding_value) {
  if (!HasPadding()) {
    return;
  }

  padding_value_ = padding_value;

  memset(raw_buffer_, padding_value_, sizeof(T) * raw_size_);
}

template <typename T>
bool Buffer<T>::CheckValues(const int value) {
  T *src = TopLeftPixel();
  for (int height = 0; height < height_; ++height) {
    for (int width = 0; width < width_; ++width) {
      if (value != src[width]) {
        fprintf(stderr, "Buffer does not match %d.\n", value);
        DumpBuffer();
        return false;
      }
    }
    src += Stride();
  }
  return true;
}

template <typename T>
bool Buffer<T>::CheckPadding() {
  if (!HasPadding()) {
    return true;
  }

  bool error = true;
  // Top padding.
  T *top = raw_buffer_;
  for (int i = 0; i < Stride() * top_padding_; ++i) {
    if (padding_value_ != top[i]) {
      error = false;
    }
  }

  // Left padding.
  T *left = TopLeftPixel() - left_padding_;
  for (int height = 0; height < height_; ++height) {
    for (int width = 0; width < left_padding_; ++width) {
      if (padding_value_ != left[width]) {
        error = false;
      }
    }
    left += Stride();
  }

  // Right padding.
  T *right = TopLeftPixel() + width_;
  for (int height = 0; height < height_; ++height) {
    for (int width = 0; width < right_padding_; ++width) {
      if (padding_value_ != right[width]) {
        error = false;
      }
    }
    right += Stride();
  }

  // Bottom padding
  T *bottom = raw_buffer_ + (top_padding_ + height_) * Stride();
  for (int i = 0; i < Stride() * bottom_padding_; ++i) {
    if (padding_value_ != bottom[i]) {
      error = false;
    }
  }

  if (!error) {
    fprintf(stderr, "Padding does not match %d.\n", padding_value_);
    DumpBuffer();
  }

  return error;
}
}  // namespace libvpx_test
#endif  // TEST_BUFFER_H_
