/*
 *  Copyright (c) 2010 The WebM project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#ifndef VP9_COMMON_VP9_ENUMS_H_
#define VP9_COMMON_VP9_ENUMS_H_

#include "./vpx_config.h"

typedef enum BLOCK_SIZE_TYPE {
  BLOCK_SIZE_MB16X16,
#if CONFIG_SBSEGMENT
  BLOCK_SIZE_SB16X32,
  BLOCK_SIZE_SB32X16,
#endif
  BLOCK_SIZE_SB32X32,
#if CONFIG_SBSEGMENT
  BLOCK_SIZE_SB32X64,
  BLOCK_SIZE_SB64X32,
#endif
  BLOCK_SIZE_SB64X64,
} BLOCK_SIZE_TYPE;

#endif  // VP9_COMMON_VP9_ENUMS_H_
