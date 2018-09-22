/*
 *  Copyright (c) 2015 The WebM project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#ifndef VPX_VPX_PORTS_SYSTEM_STATE_H_
#define VPX_VPX_PORTS_SYSTEM_STATE_H_

#include "./vpx_config.h"

#ifdef __cplusplus
extern "C" {
#endif

#if (ARCH_X86 || ARCH_X86_64) && HAVE_MMX
extern void vpx_clear_system_state(void);
extern int vpx_check_system_state(void);
#else
#define vpx_clear_system_state()
#define vpx_check_system_state() 1
#endif  // (ARCH_X86 || ARCH_X86_64) && HAVE_MMX

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // VPX_VPX_PORTS_SYSTEM_STATE_H_
