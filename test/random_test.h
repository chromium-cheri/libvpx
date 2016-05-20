/*
 *  Copyright (c) 2016 The WebM project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#ifndef TEST_RANDOM_TEST_H_
#define TEST_RANDOM_TEST_H_

#include <stdint.h>

#include <limits>

#include "third_party/googletest/src/include/gtest/gtest.h"

#include "test/acm_random.h"

namespace libvpx_test {

// TODO(any): Replace this when built with C++11
#define STATIC_ASSERT_INTEGER_TYPE_(T) \
  GTEST_COMPILE_ASSERT_(std::numeric_limits<T>::is_integer, \
    integer_type_required);

class RandomTest : public ::testing::Test {
 protected:
  virtual ~RandomTest() { }

  virtual void SetUp() {
    rnd_.Reset(ACMRandom::DeterministicSeed());
  }

  // Uniformly distributed random number from the range
  // [std::numeric_limits<R>::min(), and std::numeric_limits<R>::max()]
  template<typename R>
  R uniform() {
    STATIC_ASSERT_INTEGER_TYPE_(R);
  }

  // Uniformly distributed random number from the range
  // [0, hi)
  template<typename R, typename H>
  R uniform(H hi) {
    assert(hi > 0);
    R v = uniform<R>();
    if (std::numeric_limits<R>::is_signed && v < 0)
      return -v % hi;
    else
      return v % hi;
  }

  // Uniformly distributed random number from the range
  // [lo, hi)
  template<typename R, typename L, typename H>
  R uniform(L lo, H hi) {
    assert(hi > lo);
    return uniform<R, H>(hi - lo) + lo;
  }

  template<typename T>
  void randomise(T &e) {  // NOLINT
    STATIC_ASSERT_INTEGER_TYPE_(T);
    e = uniform<T>();
  }

  template<typename T, typename H>
  void randomise(T &e, H hi) {  // NOLINT
    STATIC_ASSERT_INTEGER_TYPE_(T);
    e = uniform<T, H>(hi);
  }

  template<typename T, typename L, typename H>
  void randomise(T &e, L lo, H hi) {  // NOLINT
    STATIC_ASSERT_INTEGER_TYPE_(T);
    e = uniform<T, L, H>(lo, hi);
  }

  template<typename T, size_t n>
  void randomise(T (&arr)[n]) {
    STATIC_ASSERT_INTEGER_TYPE_(T);
    for (size_t i = 0; i < n ; i++) {
      arr[i] = uniform<T>();
    }
  }

  template<typename T, size_t n, typename H>
  void randomise(T (&arr)[n], H hi) {
    STATIC_ASSERT_INTEGER_TYPE_(T);
    for (size_t i = 0; i < n ; i++) {
      arr[i] = uniform<T, H>(hi);
    }
  }

  template<typename T, size_t n, typename L, typename H>
  void randomise(T (&arr)[n], L lo, H hi) {
    STATIC_ASSERT_INTEGER_TYPE_(T);
    for (size_t i = 0; i < n ; i++) {
      arr[i] = uniform<T, L, H>(lo, hi);
    }
  }

  template<typename T, size_t n, size_t m>
  void randomise(T (&arr)[n][m]) {
    STATIC_ASSERT_INTEGER_TYPE_(T);
    for (size_t i = 0; i < n ; i++) {
      for (size_t j = 0; j < m ; j++) {
        arr[i][j] = uniform<T>();
      }
    }
  }

  template<typename T, size_t n, size_t m, typename H>
  void randomise(T (&arr)[n][m], H hi) {
    STATIC_ASSERT_INTEGER_TYPE_(T);
    for (size_t i = 0; i < n ; i++) {
      for (size_t j = 0; j < m ; j++) {
        arr[i][j] = uniform<T, H>(hi);
      }
    }
  }

  template<typename T, size_t n, size_t m, typename L, typename H>
  void randomise(T (&arr)[n][m], L lo, H hi) {
    STATIC_ASSERT_INTEGER_TYPE_(T);
    for (size_t i = 0; i < n ; i++) {
      for (size_t j = 0; j < m ; j++) {
        arr[i][j] = uniform<T, L, H>(lo, hi);
      }
    }
  }

 private:
  libvpx_test::ACMRandom rnd_;
};

// Add further specialisations as necessary

template<>
bool RandomTest::uniform<bool>() {
  return rnd_.Rand8() & 1 ? true : false;
}

template<>
uint8_t RandomTest::uniform<uint8_t>() {
  return rnd_.Rand8();
}

template<>
uint16_t RandomTest::uniform<uint16_t>() {
  return rnd_.Rand16();
}

template<>
uint32_t RandomTest::uniform<uint32_t>() {
  const uint32_t l = uniform<uint16_t>();
  const uint32_t h = uniform<uint16_t>();
  return h << 16 | l;
}

template<>
uint64_t RandomTest::uniform<uint64_t>() {
  const uint64_t l = uniform<uint32_t>();
  const uint64_t h = uniform<uint32_t>();
  return h << 32 | l;
}

template<>
int8_t RandomTest::uniform<int8_t>() { return uniform<uint8_t>(); }

template<>
int16_t RandomTest::uniform<int16_t>() { return uniform<uint16_t>(); }

template<>
int32_t RandomTest::uniform<int32_t>() { return uniform<uint32_t>(); }

template<>
int64_t RandomTest::uniform<int64_t>() { return uniform<uint64_t>(); }

}  // namespace libvpx_test

#endif  // TEST_RANDOM_TEST_H_
