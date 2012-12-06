/*
 *  Copyright (c) 2010 The WebM project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */


#ifndef __INC_EXTEND_H
#define __INC_EXTEND_H

#include "vpx_mem/yv12config.h"

void vp8_extend_mb_row(YV12_BUFFER_CONFIG *ybf, unsigned char *YPtr, unsigned char *UPtr, unsigned char *VPtr);
void vp8_copy_and_extend_frame(YV12_BUFFER_CONFIG *src,
                               YV12_BUFFER_CONFIG *dst);
void vp8_copy_and_extend_frame_with_rect(YV12_BUFFER_CONFIG *src,
                                         YV12_BUFFER_CONFIG *dst,
                                         int srcy, int srcx,
                                         int srch, int srcw);

#endif
