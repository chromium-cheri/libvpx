/*
 *  Copyright (c) 2010 The WebM project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */


#include "string.h"
#include "vp8/common/blockd.h"
#include "onyx_int.h"

#ifndef __INC_SEGMENTATION_H__
#define __INC_SEGMENTATION_H__ 1

extern void vp8_update_gf_useage_maps(VP8_COMP *cpi, VP8_COMMON *cm, MACROBLOCK *x);

extern void vp8_enable_segmentation(VP8_PTR ptr);
extern void vp8_disable_segmentation(VP8_PTR ptr);

// Valid values for a segment are 0 to 3
// Segmentation map is arrange as [Rows][Columns]
extern void vp8_set_segmentation_map(VP8_PTR ptr, unsigned char *segmentation_map);

// The values given for each segment can be either deltas (from the default
// value chosen for the frame) or absolute values.
//
// Valid range for abs values is (0-127 for MB_LVL_ALT_Q), (0-63 for
// SEGMENT_ALT_LF)
// Valid range for delta values are (+/-127 for MB_LVL_ALT_Q), (+/-63 for
// SEGMENT_ALT_LF)
//
// abs_delta = SEGMENT_DELTADATA (deltas) abs_delta = SEGMENT_ABSDATA (use
// the absolute values given).
//
extern void vp8_set_segment_data(VP8_PTR ptr, signed char *feature_data, unsigned char abs_delta);

extern void choose_segmap_coding_method(VP8_COMP *cpi);

#endif /* __INC_SEGMENTATION_H__ */
