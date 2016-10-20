/*
 *  Copyright (c) 2016 The WebM project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#ifndef VPX_PORTS_PPC_H_
#define VPX_PORTS_PPC_H_
#include <stdlib.h>

#include "vpx_config.h"

#ifdef __cplusplus
extern "C" {
#endif

#define HAS_VSX 0x01

int ppc_simd_caps(void);

// Earlier gcc compilers doesn't have support for Altivec
#if !defined(__clang__) && defined(__GNUC__) && __GNUC__ == 4 && \
  __GNUC_MINOR__ < 3
#define VPX_INCOMPATIBLE_GCC
#endif

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // VPX_PORTS_PPC_H_
