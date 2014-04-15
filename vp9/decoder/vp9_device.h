/*
 *  Copyright (c) 2013 The WebM project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#ifndef VP9_DECODER_VP9_DEVICE_H_
#define VP9_DECODER_VP9_DEVICE_H_

#include "./vpx_config.h"

#if CONFIG_MULTITHREAD

#include "vp9/sched/sched.h"

void vp9_register_devices(struct scheduler *sched);

#endif

#endif  // VP9_DECODER_VP9_DEVICE_H_
