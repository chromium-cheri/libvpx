/*
 *  Copyright (c) 2014 The WebM project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#ifndef VP9_ENCODER_VP9_MULTI_THREAD_H
#define VP9_ENCODER_VP9_MULTI_THREAD_H

#include "vp9/encoder/vp9_encoder.h"
#include "vp9/encoder/vp9_job_queue.h"

void *vp9_enc_grp_get_next_job(MultiThreadHandle *multi_thread_ctxt,
                               int job_type, int tile_id);

void vp9_prepare_job_queue(MultiThreadHandle *multi_thread_ctxt, int tile_cols,
                           ENC_JOB_TYPES_T task_type, int num_vert_units,
                           void *v_job_queue, int num_of_jobs);

int vp9_get_job_queue_status(MultiThreadHandle *multi_thread_ctxt,
                             int cur_tile_id, ENC_JOB_TYPES_T job_type);

void vp9_assign_tile_to_thread(MultiThreadHandle *multi_thread_ctxt,
                               int tile_cols);

void vp9_multi_thread_init(VP9_COMP *cpi, YV12_BUFFER_CONFIG *Source);

void vp9_multi_thread_tile_init(VP9_COMP *cpi);

void vp9_wpp_mem_alloc(VP9_COMP *cpi);

void vp9_wpp_mem_dealloc(VP9_COMP *cpi);

int vp9_get_tiles_proc_status(MultiThreadHandle *multi_thread_ctxt,
                              int *cur_tile_id, ENC_JOB_TYPES_T job_type,
                              int tile_cols);

#endif  // VP9_ENCODER_VP9_MULTI_THREAD_H
