
/*
 *  Copyright (c) 2016 The WebM project authors. All Rights Reserved.
 *
 *  Use of this source code is governed  by a BSD-style license that can be
 *  found in the LICENSE file in the root of the source tree. An additional
 *  intellectual property  rights grant can  be found in the  file PATENTS.
 *  All contributing  project authors may be  found in the AUTHORS  file in
 *  the root of the source tree.
 */

#include <assert.h>
#include <string.h>
#include <math.h>

#include "vpx_ports/system_state.h"
#include "vpx_dsp/vpx_dsp_common.h"
#include "vpx/vpx_integer.h"
#include "vpx_mem/vpx_mem.h"

#include "vp9/common/vp9_common.h"
#include "vp9/common/vp9_onyxc_int.h"
#include "vp9/common/vp9_seg_common.h"
#include "vp9/common/vp9_blockd.h"

#include "vp9/encoder/vp9_encoder.h"
#include "vp9/encoder/vp9_segmentation.h"

#include "vp9/encoder/vp9_alt_ref_aq_private.h"
#include "vp9/encoder/vp9_alt_ref_aq.h"

/* #ifndef NDEBUG */
/* #  define ALT_REF_DEBUG_DIR "files/maps" */
/* #endif */

static void vp9_mat8u_init(struct MATX_8U *self) {
  self->rows = 0;
  self->cols = 0;
  self->stride = 0;
  self->data = NULL;
}

static void vp9_mat8u_create(struct MATX_8U *self, int rows, int cols,
                             int stride) {
  assert(stride >= cols || stride <= 0);

  if (stride <= cols) stride = cols;

  if (rows * stride != self->rows * self->stride) {
    int nbytes = rows * stride * sizeof(uint8_t);
    self->data = vpx_realloc(self->data, nbytes);
  }

  self->rows = rows;
  self->cols = cols;
  self->stride = stride;

  assert(self->data != NULL);
}

static void vp9_mat8u_destroy(struct MATX_8U *self) { vpx_free(self->data); }

static void vp9_mat8u_wrap(struct MATX_8U *self, int rows, int cols, int stride,
                           uint8_t *data) {
  self->rows = rows;
  self->cols = cols;

  self->stride = stride;

  if (!self->stride) self->stride = cols;

  self->data = data;

  assert(self->stride >= self->cols);
  assert(self->data != NULL);
}

static void vp9_mat8u_zerofill(struct MATX_8U *image) {
  memset(image->data, 0, image->rows * image->stride * sizeof(uint8_t));
}

static void vp9_mat8u_copy_to(const struct MATX_8U *src, struct MATX_8U *dst) {
  int i;

  if (dst->data == src->data) return;

  vp9_mat8u_create(dst, src->rows, src->cols, src->stride);

  for (i = 0; i < src->rows; ++i) {
    const uint8_t *copy_from = src->data + i * src->stride;
    uint8_t *copy_to = dst->data + i * dst->stride;

    memcpy(copy_to, copy_from, src->cols * sizeof(uint8_t));
  }
}

#ifdef ALT_REF_DEBUG_DIR

static void vp9_mat8u_imwrite(const struct MATX_8U *image, const char *filename,
                              int max_value) {
  int i, j;

  FILE *const image_file = fopen(filename, "wt");

  assert(image->rows > 0 && image->cols > 0);
  assert(image->data != NULL);

  if (max_value <= 0) max_value = 255;

  fprintf(image_file, "P2\n");
  fprintf(image_file, "%d ", image->cols);
  fprintf(image_file, "%d ", image->rows);
  fprintf(image_file, "%d ", max_value);

  for (i = 0; i < image->rows; ++i) {
    const uint8_t *row_data = &image->data[i * image->stride];

    fprintf(image_file, "\n");
    for (j = 0; j < image->cols; ++j) fprintf(image_file, "%hhu ", row_data[j]);
  }

  fclose(image_file);
}

static void vp9_alt_ref_aq_dump_debug_data(struct ALT_REF_AQ *self) {
  char format[] = ALT_REF_DEBUG_DIR "/alt_ref_quality_map%d.ppm";
  char filename[sizeof(format) / sizeof(format[0]) + 10];

  ++self->alt_ref_number;

  snprintf(filename, sizeof(filename), format, self->alt_ref_number);
  vp9_mat8u_imwrite(&self->segmentation_map, filename, self->nsegments - 1);
}

#endif

static void vp9_alt_ref_aq_update_number_of_segments(struct ALT_REF_AQ *self) {
  int i, j;

  for (i = 0, j = 0; i < self->nsegments; ++i)
    j = VPXMAX(j, self->_segment_changes[i]);

  self->nsegments = j + 1;
}

static void vp9_alt_ref_aq_setup_segment_deltas(struct ALT_REF_AQ *self) {
  int i;

  vp9_zero_array(&self->segment_deltas[1], self->nsegments);

  for (i = 0; i < self->nsegments; ++i)
    ++self->segment_deltas[self->_segment_changes[i] + 1];

  for (i = 2; i < self->nsegments; ++i)
    self->segment_deltas[i] += self->segment_deltas[i - 1];
}

static void vp9_alt_ref_aq_compress_segment_hist(struct ALT_REF_AQ *self) {
  int i, j;

  // join few highest segments
  for (i = 0, j = 0; i < self->nsegments; ++i) {
    if (j >= ALT_REF_AQ_NUM_NONZERO_SEGMENTS) break;

    if (self->_segment_hist[i] > ALT_REF_AQ_SEGMENT_MIN_AREA) ++j;
  }

  for (; i < self->nsegments; ++i) self->_segment_hist[i] = 0;

  // this always at least doesn't harm, but sometimes helps
  self->_segment_hist[self->nsegments - 1] = 0;

  // invert segment ids and drop out segments with zero
  // areas, so we can save few segment numbers for other
  // stuff  and  save bitrate  on segmentation overhead
  self->_segment_changes[self->nsegments] = 0;

  for (i = self->nsegments - 1, j = 0; i >= 0; --i)
    if (self->_segment_hist[i] > ALT_REF_AQ_SEGMENT_MIN_AREA)
      self->_segment_changes[i] = ++j;
    else
      self->_segment_changes[i] = self->_segment_changes[i + 1];
}

static void vp9_alt_ref_aq_update_segmentation_map(struct ALT_REF_AQ *self) {
  int i, j;

  vp9_zero_array(self->_segment_hist, self->nsegments);

  for (i = 0; i < self->segmentation_map.rows; ++i) {
    uint8_t *data = self->segmentation_map.data;
    uint8_t *row_data = data + i * self->segmentation_map.stride;

    for (j = 0; j < self->segmentation_map.cols; ++j) {
      row_data[j] = self->_segment_changes[row_data[j]];
      ++self->_segment_hist[row_data[j]];
    }
  }
}

static void vp9_alt_ref_aq_set_single_delta(struct ALT_REF_AQ *const self,
                                            const struct VP9_COMP *cpi) {
  float ndeltas = VPXMIN(self->_nsteps, MAX_SEGMENTS - 1);
  int total_delta = cpi->rc.avg_frame_qindex[INTER_FRAME];
  total_delta -= cpi->common.base_qindex;

  // Don't ask me why it is (... + self->delta_shrink). It just works better.
  self->single_delta = total_delta / VPXMAX(ndeltas + self->delta_shrink, 1);
}

static void vp9_alt_ref_aq_get_overall_quality(struct ALT_REF_AQ *const self) {
  int i;

  for (self->overall_quality = 0.0f, i = 1; i < self->nsegments; ++i)
    self->overall_quality += (float)i * self->_segment_hist[i];

  self->overall_quality /= (float)self->nsegments;
  self->overall_quality /= (float)self->segmentation_map.rows;
  self->overall_quality /= (float)self->segmentation_map.cols;
}

// set up histogram (I use it for filtering zero-area segments out)
static void vp9_alt_ref_aq_setup_histogram(struct ALT_REF_AQ *const self) {
  struct MATX_8U segm_map;
  int i, j;

  segm_map = self->segmentation_map;

  vp9_zero_array(self->_segment_hist, self->nsegments);

  for (i = 0; i < segm_map.rows; ++i) {
    const uint8_t *const data = segm_map.data;
    int offset = i * segm_map.stride;

    for (j = 0; j < segm_map.cols; ++j) ++self->_segment_hist[data[offset + j]];
  }
}

struct ALT_REF_AQ *vp9_alt_ref_aq_create() {
  struct ALT_REF_AQ *self = vpx_malloc(sizeof(struct ALT_REF_AQ));

  if (self == NULL) return self;

  self->aq_mode = LOOKAHEAD_AQ;

  self->nsegments = -1;
  self->_nsteps = -1;
  self->single_delta = -1;

  memset(self->_segment_hist, 0, sizeof(self->_segment_hist));

  // This is just initiallization, allocation
  // is going to happen on the first request
  vp9_mat8u_init(&self->segmentation_map);
  vp9_mat8u_init(&self->cpi_segmentation_map);

  return self;
}

static void vp9_alt_ref_aq_process_map(struct ALT_REF_AQ *const self) {
  int i, j;

  self->_nsteps = self->nsegments - 1;

  assert(self->nsegments > 0);

  vp9_alt_ref_aq_setup_histogram(self);
  vp9_alt_ref_aq_get_overall_quality(self);

  // super special case, e.g., riverbed sequence
  if (self->nsegments == 1) {
    self->segment_deltas[0] = 1;
    self->delta_shrink = ALT_REF_AQ_SINGLE_SEGMENT_DELTA_SHRINK;
    return;
  }

  // special case: all blocks belong to the same segment,
  // and then we can just update frame's base_qindex
  for (i = 0, j = 0; i < self->nsegments; ++i)
    if (self->_segment_hist[i] >= 0) ++j;

  if (j == 1) {
    for (i = 0; self->_segment_hist[i] <= 0;) ++i;
    self->segment_deltas[0] = self->nsegments - i - 1;

    self->nsegments = 1;
    self->delta_shrink = ALT_REF_AQ_SINGLE_SEGMENT_DELTA_SHRINK;
  } else {
    vp9_alt_ref_aq_compress_segment_hist(self);
    vp9_alt_ref_aq_setup_segment_deltas(self);

    vp9_alt_ref_aq_update_number_of_segments(self);
    self->delta_shrink = ALT_REF_AQ_DELTA_SHRINK;
  }

  if (self->nsegments == 1) return;

#ifdef ALT_REF_DEBUG_DIR
  vp9_alt_ref_aq_dump_debug_data(self);
#endif

  vp9_alt_ref_aq_update_segmentation_map(self);
}

static void vp9_alt_ref_aq_self_destroy(struct ALT_REF_AQ *const self) {
  vpx_free(self);
}

void vp9_alt_ref_aq_destroy(struct ALT_REF_AQ *const self) {
  vp9_mat8u_destroy(&self->segmentation_map);
  vp9_alt_ref_aq_self_destroy(self);
}

void vp9_alt_ref_aq_upload_map(struct ALT_REF_AQ *const self,
                               const struct MATX_8U *segmentation_map) {
  // TODO(yuryg): I don't really need this
  vp9_mat8u_copy_to(segmentation_map, &self->segmentation_map);
}

void vp9_alt_ref_aq_set_nsegments(struct ALT_REF_AQ *const self,
                                  int nsegments) {
  self->nsegments = nsegments;
}

void vp9_alt_ref_aq_setup_mode(struct ALT_REF_AQ *const self,
                               struct VP9_COMP *const cpi) {
  VPX_SWAP(AQ_MODE, self->aq_mode, cpi->oxcf.aq_mode);
  vp9_alt_ref_aq_process_map(self);
}

// set basic segmentation to the altref's one
void vp9_alt_ref_aq_setup_map(struct ALT_REF_AQ *const self,
                              struct VP9_COMP *const cpi) {
  int i, j;
  float coef = 1.0f;

  assert(self->segmentation_map.rows == cpi->common.mi_rows);
  assert(self->segmentation_map.cols == cpi->common.mi_cols);

  // set cpi segmentation to the altref's one
  if (self->single_delta < 0) {
    vp9_mat8u_wrap(&self->cpi_segmentation_map, cpi->common.mi_rows,
                   cpi->common.mi_cols, cpi->common.mi_cols,
                   cpi->segmentation_map);

    // TODO(yuryg): avoid this copy
    assert(self->segmentation_map.rows == cpi->common.mi_rows);
    assert(self->segmentation_map.cols == cpi->common.mi_cols);

    vp9_mat8u_copy_to(&self->segmentation_map, &self->cpi_segmentation_map);
  }

  // clear down mmx registers (only need for x86 arch)
  vpx_clear_system_state();

  // set single quantizer step between segments
  vp9_alt_ref_aq_set_single_delta(self, cpi);

  // TODO(yuryg): figure out better (may be adaptive) factor
  if (self->overall_quality < ALT_REF_AQ_OVERALL_QUALITY_THRESH) {
    float alpha = (float)ALT_REF_AQ_OVERALL_QUALITY_ALPHA;
    coef -= alpha * (1.0f - sqrtf(self->overall_quality) / sqrtf(0.2f));
  }

  cpi->rc.this_frame_target = (int)(coef * cpi->rc.this_frame_target);

  // TODO(yuryg): this is probably not nice for rate control,
  if (self->nsegments == 1) {
    float qdelta = self->single_delta * self->segment_deltas[0];
    cpi->common.base_qindex += (int)(qdelta + 1e-2);
    // TODO(yuryg): do I need this?
    vp9_mat8u_zerofill(&self->cpi_segmentation_map);
    return;
  }

  // set segment deltas
  cpi->common.seg.abs_delta = SEGMENT_DELTADATA;

  vp9_enable_segmentation(&cpi->common.seg);
  vp9_clearall_segfeatures(&cpi->common.seg);

  for (i = 1, j = 1; i < self->nsegments; ++i) {
    int qdelta = (int)(self->single_delta * self->segment_deltas[i] + 1e-2f);

    vp9_enable_segfeature(&cpi->common.seg, j, SEG_LVL_ALT_Q);
    vp9_set_segdata(&cpi->common.seg, j++, SEG_LVL_ALT_Q, qdelta);
  }
}

// restore cpi->aq_mode
void vp9_alt_ref_aq_unset_all(struct ALT_REF_AQ *const self,
                              struct VP9_COMP *const cpi) {
  cpi->force_update_segmentation = 1;

  self->nsegments = -1;
  self->_nsteps = -1;
  self->single_delta = -1;

  VPX_SWAP(AQ_MODE, self->aq_mode, cpi->oxcf.aq_mode);

  // TODO(yuryg): may be it is better to move this to encoder.c
  if (cpi->oxcf.aq_mode == NO_AQ) vp9_disable_segmentation(&cpi->common.seg);
}

int vp9_alt_ref_aq_disable_if(const struct ALT_REF_AQ *self,
                              int segmentation_overhead, int bandwidth) {
  float estimate;
  float bytes_per_segment;
  int sum, i;

  for (i = 1, sum = 0; i < self->nsegments; ++i) sum += self->_segment_hist[i];
  bytes_per_segment = segmentation_overhead / (float)sum;

  estimate = bytes_per_segment;

  (void)bandwidth;

  return estimate > ALT_REF_AQ_PROTECT_GAIN_THRESH;
}
