/*
 *  Copyright (c) 2015 The WebM project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#ifndef AV1_COMMON_DIVIDE_H_
#define AV1_COMMON_DIVIDE_H_
// An implemntation of the divide by multiply alogrithm
// https://gmplib.org/~tege/divcnst-pldi94.pdf

#include <limits.h>

#include "./aom_config.h"
#include "aom/aom_integer.h"

#ifdef __cplusplus
extern "C" {
#endif  // __cplusplus

struct fastdiv_elem {
  unsigned mult;
  unsigned shift;
};

extern const struct fastdiv_elem av1_fastdiv_tab[256];

static INLINE unsigned fastdiv(unsigned x, int y) {
  unsigned t =
      ((uint64_t)x * av1_fastdiv_tab[y].mult) >> (sizeof(x) * CHAR_BIT);
  return (t + x) >> av1_fastdiv_tab[y].shift;
}
#ifdef __cplusplus
}  // extern "C"
#endif  // __cplusplus
#endif  // AV1_COMMON_DIVIDE_H_
