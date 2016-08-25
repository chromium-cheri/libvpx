/*
 *  Copyright (c) 2016 The WebM project authors. All Rights Reserved.
 *
 *  Use of this source code is governed  by a BSD-style license that can be
 *  found in the LICENSE file in the root of the source tree. An additional
 *  intellectual property  rights grant can  be found in the  file PATENTS.
 *  All contributing  project authors may be  found in the AUTHORS  file in
 *  the root of the source tree.
 */

/*
 *  \file vp9_alt_ref_aq_private.h
 *
 *  This file  describes class  used for  setting up  adaptive segmentation
 *  for  altref frames.   It  is  private file  and  most  likely you  need
 *  alt_ref_aq.h instead.
 */

#ifndef VP9_ENCODER_VP9_ALT_REF_AQ_PRIVATE_H_
#define VP9_ENCODER_VP9_ALT_REF_AQ_PRIVATE_H_

#include "vpx/vpx_integer.h"
#include "vp9/encoder/vp9_alt_ref_aq.h"
#include "vp9/encoder/vp9_encoder.h"

#ifdef __cplusplus
extern "C" {
#endif

struct ALT_REF_AQ {
  // private member used for segment ids convertion
  int segment_changes[ALT_REF_MAX_FRAMES + 1];

  // private member for storing distribution of the segment ids
  int segment_hist[ALT_REF_MAX_FRAMES];

  // number of delta-quantizer segments,
  // it can be different from nsteps
  // because of the range compression
  int nsegments;

  // single qdelta step between segments
  float single_delta;

  // qdelta[i] = (int) single_delta*segment_deltas[i];
  // ------------------------------------------------
  // qdelta[0] has single delta quantizer
  // for the frame in the case we need it
  int segment_deltas[ALT_REF_MAX_FRAMES];

  // this one is just for debugging purposes
  int alt_ref_number;

  // basic aq mode (I keep original
  // aq mode when encoding altref frame)
  AQ_MODE aq_mode;

  // wrapper around basic segmentation_map
  struct MATX_8U cpi_segmentation_map;

  // altref segmentation
  struct MATX_8U segmentation_map;
};

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // VP9_ENCODER_VP9_ALT_REF_AQ_PRIVATE_H_
