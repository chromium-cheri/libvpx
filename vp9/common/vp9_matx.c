/*
 *  Copyright (c) 2016 The WebM project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

/*
 *  This file contains generic templated matrix class
 *  designed like OpenCV Mat class, but much simpler
 *
 *  TODO(yuryg): ... detailed description, may be ...
 */

#include <assert.h>
#include <string.h>
#include <stdio.h>

#include "vpx_dsp/vpx_dsp_common.h"
#include "vpx/vpx_integer.h"
#include "vpx_mem/vpx_mem.h"

#include "vp9/common/vp9_matx_enums.h"
#include "vp9/common/vp9_matx.h"


#define vpx_runtime_assert(expression) assert(expression)

#define VP9_MATX_REALLOC(type, self) \
  (self)->data = vpx_realloc((self)->data, VP9_MATX_NBYTES(type, self));

void vp9_matx_init(MATX* const self) {
  self->data = NULL;
  self->is_wrapper = 0;

  self->rows = 0;
  self->cols = 0;
  self->stride = 0;
  self->cn = -1;
  self->typeid = MATX_NO_TYPE;
}

void vp9_matx_create(MATX* const self,
                     int rows,
                     int cols,
                     int stride,
                     int cn,
                     MATX_TYPE typeid) {
  uint8_t reallocation_not_needed = 1;

  if (self->rows != rows) {
    self->rows = rows;
    reallocation_not_needed = 0;
  }

  if (self->cols != cols) {
    self->cols = cols;
    reallocation_not_needed = 0;
  }

  if (self->stride != stride && stride != 0) {
    self->stride = stride;
    reallocation_not_needed = 0;
  }

  if (!stride)
    self->stride = cols*cn;

  if (self->cn != cn || self->typeid != typeid) {
    self->typeid = typeid;
    self->cn = cn;
    reallocation_not_needed = 0;
  }

  if (reallocation_not_needed)
    return;

  vpx_runtime_assert(!self->is_wrapper);
  vpx_runtime_assert(self->stride >= self->cols*self->cn);

  switch (self->typeid) {
    case TYPE_8U:
      VP9_MATX_REALLOC(uint8_t, self);
      break;
    case TYPE_8S:
      VP9_MATX_REALLOC(int8_t, self);
      break;
    case TYPE_16U:
      VP9_MATX_REALLOC(uint16_t, self);
      break;
    case TYPE_16S:
      VP9_MATX_REALLOC(int16_t, self);
      break;
    case TYPE_32U:
      VP9_MATX_REALLOC(uint32_t, self);
      break;
    case TYPE_32S:
      VP9_MATX_REALLOC(int32_t, self);
      break;
    case TYPE_32F:
      VP9_MATX_REALLOC(float, self);
      break;
    case TYPE_64F:
      VP9_MATX_REALLOC(double, self);
      break;
    default:
      vpx_runtime_assert(0 /* matx: inapprorpiate type */);
  }

  vpx_runtime_assert(self->data != NULL);
}

void vp9_matx_wrap(MATX* const self,
                   int rows,
                   int cols,
                   int stride,
                   int cn,
                   void *data,
                   MATX_TYPE typeid) {
  self->rows = rows;
  self->cols = cols;
  self->cn = cn;
  self->typeid = typeid;

  self->stride = stride;
  if (!self->stride)
    self->stride = cols*cn;

  vpx_runtime_assert(self->stride >= self->cols*self->cn);

  // you can't wrap matrix around itself
  vpx_runtime_assert(self->is_wrapper || self->data != data);

  vp9_matx_destroy(self);
  self->is_wrapper = 1;
  self->data = data;

  vpx_runtime_assert(self->data != NULL);
}

void vp9_matx_destroy(MATX* const self) {
  if (!self->is_wrapper)
    vpx_free(self->data);
}
