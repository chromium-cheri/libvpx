/*
 *  Copyright (c) 2016 The WebM project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#ifndef AOM_UTIL_DEBUG_UTIL_H_
#define AOM_UTIL_DEBUG_UTIL_H_

#include "./aom_config.h"

#ifdef __cplusplus
extern "C" {
#endif

#if CONFIG_BITSTREAM_DEBUG
/* This is a debug tool used to detect bitstream error. On encoder side, it
 * pushes each bit and probability into a queue before the bit is written into
 * the Arithmetic coder. On decoder side, whenever a bit is read out from the
 * Arithmetic coder, it pops out the reference bit and probability from the
 * queue as well. If the two results do not match, this debug tool will report
 * an error.  This tool can be used to pin down the bitstream error precisely.
 * By combining gdb's backtrace method, we can detect which module causes the
 * bitstream error. */
int bitstream_queue_get_write(void);
int bitstream_queue_get_read(void);
void bitstream_queue_record_write(void);
void bitstream_queue_reset_write(void);
void bitstream_queue_pop(int *result, int *prob);
void bitstream_queue_push(int result, int prob);
void bitstream_queue_set_skip_write(int skip);
void bitstream_queue_set_skip_read(int skip);
#endif  // CONFIG_BITSTREAM_DEBUG

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // AOM_UTIL_DEBUG_UTIL_H_
