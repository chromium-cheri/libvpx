/*
 *  Copyright (c) 2018 The WebM project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include <stdio.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <stdint.h>

#include "./vp9_job_queue.h"

void vp9_jobq_init(jobq_t *jobq, uint8_t *buf, int buf_size) {
#if CONFIG_MULTITHREAD
  pthread_mutex_init(&jobq->mutex, NULL);
  pthread_cond_init(&jobq->cond, NULL);
#endif
  jobq->buf_base = buf;
  jobq->buf_wr = buf;
  jobq->buf_rd = buf;
  jobq->buf_end = buf + buf_size;
  jobq->terminate = 0;
}

void vp9_jobq_reset(jobq_t *jobq) {
#if CONFIG_MULTITHREAD
  pthread_mutex_lock(&jobq->mutex);
#endif
  jobq->buf_wr = jobq->buf_base;
  jobq->buf_rd = jobq->buf_base;
  jobq->terminate = 0;
#if CONFIG_MULTITHREAD
  pthread_mutex_unlock(&jobq->mutex);
#endif
  return;
}

void vp9_jobq_deinit(jobq_t *jobq) {
  vp9_jobq_reset(jobq);
#if CONFIG_MULTITHREAD
  pthread_mutex_destroy(&jobq->mutex);
  pthread_cond_destroy(&jobq->cond);
#endif
  return;
}

void vp9_jobq_terminate(jobq_t *jobq) {
#if CONFIG_MULTITHREAD
  pthread_mutex_lock(&jobq->mutex);
#endif
  jobq->terminate = 1;
#if CONFIG_MULTITHREAD
  pthread_cond_broadcast(&jobq->cond);
  pthread_mutex_unlock(&jobq->mutex);
#endif
  return;
}

int vp9_jobq_queue(jobq_t *jobq, void *job, int job_size) {
  int ret = 0;
#if CONFIG_MULTITHREAD
  pthread_mutex_lock(&jobq->mutex);
#endif
  if (jobq->terminate != 1) {
    if (jobq->buf_end >= (jobq->buf_wr + job_size)) {
      memcpy(jobq->buf_wr, job, job_size);
      jobq->buf_wr = jobq->buf_wr + job_size;
#if CONFIG_MULTITHREAD
      pthread_cond_signal(&jobq->cond);
#endif
      ret = 0;
    } else {
      /* Wrap around case is not supported */
      assert(0);
      ret = 1;
    }
  }
#if CONFIG_MULTITHREAD
  pthread_mutex_unlock(&jobq->mutex);
#endif
  return ret;
}

int vp9_jobq_dequeue(jobq_t *jobq, void *job, int job_size, int blocking) {
  int ret = 0;
#if CONFIG_MULTITHREAD
  pthread_mutex_lock(&jobq->mutex);
#endif
  if (jobq->buf_end >= (jobq->buf_rd + job_size)) {
    while (1) {
      if (jobq->buf_wr >= (jobq->buf_rd + job_size)) {
        memcpy(job, jobq->buf_rd, job_size);
        jobq->buf_rd = jobq->buf_rd + job_size;
        ret = 0;
        break;
      } else {
        /* If all the entries have been dequeued, then break and return */
        if (1 == jobq->terminate) {
          ret = 1;
          break;
        }
        if (1 == blocking) {
#if CONFIG_MULTITHREAD
          pthread_cond_wait(&jobq->cond, &jobq->mutex);
#endif
        } else {
          /* If there is no job available,
           * and this is non blocking call then return fail */
          ret = 1;
          break;
        }
      }
    }
  } else {
    /* Wrap around case is not supported */
    if (1 != jobq->terminate) assert(0);

    ret = 1;
  }
#if CONFIG_MULTITHREAD
  pthread_mutex_unlock(&jobq->mutex);
#endif

  return ret;
}
