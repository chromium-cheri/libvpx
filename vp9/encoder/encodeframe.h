/*
 *  Copyright (c) 2010 The WebM project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */


#ifndef __INC_ENCODEFRAME_H
#define __INC_ENCODEFRAME_H

#include "block.h"

extern void vp9_build_block_offsets(MACROBLOCK *x);

extern void vp9_setup_block_ptrs(MACROBLOCK *x);

#endif  // __INC_ENCODEFRAME_H
