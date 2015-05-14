/*
 *  Copyright (c) 2015 The WebM project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be
 *  found  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include <stdio.h>
#include <stdlib.h>
#include <memory.h>
#include <math.h>
#include <assert.h>


#include "vpx_mem/vpx_mem.h"
#include "vp9/encoder/vp9_segmentation.h"
#include "vp9/encoder/vp9_mcomp.h"
#include "vp9/common/vp9_blockd.h"
#include "vp9/common/vp9_reconinter.h"
#include "vp9/common/vp9_reconintra.h"
#include "vp9/common/vp9_systemdependent.h"
#include "vp9/common/vp9_motion_model.h"
#include "vp9/encoder/vp9_corner_detect.h"
#include "vp9/encoder/vp9_corner_match.h"
#include "vp9/encoder/vp9_ransac.h"
#include "vp9/encoder/vp9_global_motion.h"
#include "vp9/encoder/vp9_motion_field.h"

// #define VERBOSE

// Default is Harris
#define USE_FAST_CORNER

#define MIN_INLIER_PROB 0.1

#define MAX_CORNERS 4096

INLINE ransacType get_ransacType(TransformationType type) {
  switch (type) {
    case HOMOGRAPHY:
      return ransacHomography;
    case AFFINE:
      return ransacAffine;
    case ROTZOOM:
      return ransacRotZoom;
    case TRANSLATION:
      return ransacTranslation;
    default:
      assert(0);
      return NULL;
  }
}

static double compute_error_score(TransformationType type,
                                  int *points1, int stride1,
                                  int *points2, int stride2,
                                  int npoints, double *H,
                                  int *map) {
  int i, n = 0;
  double ot[2], pt[2];
  int *mp1 = points1;
  int *mp2 = points2;
  double sqerr = 0.0;
  const int numparams = get_numparams(type);

  projectPointsType projectPoints = get_projectPointsType(type);
  if (projectPoints == NULL) return -1.0;
  if (map) {
    for (i = 0; i < npoints; ++i, mp1+=stride1, mp2+=stride2) {
      if (map[i] != -1) {
        ot[0] = mp1[0];
        ot[1] = mp1[1];
        projectPoints(&H[map[i] * numparams], ot, pt, 1, stride1, stride2);
        sqerr += (pt[0] - mp2[0]) * (pt[0] - mp2[0]) +
                 (pt[1] - mp2[1]) * (pt[1] - mp2[1]);
        n++;
      }
    }
  } else {
    for (i = 0; i < npoints; ++i, mp1+=stride1, mp2+=stride2) {
      ot[0] = mp1[0];
      ot[1] = mp1[1];
      projectPoints(H, ot, pt, 1, stride1, stride2);
      sqerr += (pt[0] - mp2[0]) * (pt[0] - mp2[0]) +
               (pt[1] - mp2[1]) * (pt[1] - mp2[1]);
      n++;
    }
  }
  return sqrt(sqerr / n);
}

static int compute_global_motion_single(TransformationType type,
                                        int *correspondences,
                                        int num_correspondences,
                                        double *H,
                                        int *inlier_map) {
  double *mp, *matched_points;
  int *cp = correspondences;
  int i, result;
  int num_inliers = 0;
  ransacType ransac = get_ransacType(type);
  if (ransac == NULL)
    return 0;
  matched_points =
      (double *)malloc(4 * num_correspondences * sizeof(double));

  for (mp = matched_points, cp = correspondences, i = 0;
       i < num_correspondences; ++i) {
    *mp++ = *cp++;
    *mp++ = *cp++;
    *mp++ = *cp++;
    *mp++ = *cp++;
  }
  result = ransac(matched_points, num_correspondences,
                  &num_inliers, inlier_map, H);
  if (!result && num_inliers < MIN_INLIER_PROB * num_correspondences) {
    result = 1;
    num_inliers = 0;
  }
  if (!result) {
    for (i = 0; i < num_correspondences; ++i) {
      inlier_map[i] = inlier_map[i] - 1;
    }
  }
  free(matched_points);
  return num_inliers;
}

// Returns number of models actually returned: 1 - if success, 0 - if failure
int vp9_compute_global_motion_single_feature_based(
    struct VP9_COMP *cpi,
    TransformationType type,
    YV12_BUFFER_CONFIG *frm,
    YV12_BUFFER_CONFIG *ref,
    double *H) {
  int num_frm_corners, num_ref_corners;
  int num_correspondences;
  int *correspondences;
  int num_inliers;
  int *inlier_map = NULL;
  int frm_corners[2 * MAX_CORNERS], ref_corners[2 * MAX_CORNERS];
  (void) cpi;

#ifdef USE_FAST_CORNER
  num_frm_corners = FastCornerDetect(frm->y_buffer, frm->y_width,
                                     frm->y_height, frm->y_stride,
                                     frm_corners, MAX_CORNERS);
  num_ref_corners = FastCornerDetect(ref->y_buffer, ref->y_width,
                                     ref->y_height, ref->y_stride,
                                     ref_corners, MAX_CORNERS);
#else
  num_frm_corners = HarrisCornerDetect(frmbuf, width, height, frm_stride,
                                       frm_corners, MAX_CORNERS);
  num_ref_corners = HarrisCornerDetect(refbuf, width, height, ref_stride,
                                       ref_corners, MAX_CORNERS);
#endif
#ifdef VERBOSE
  printf("Frame corners     = %d\n", num_frm_corners);
  printf("Reference corners = %d\n", num_ref_corners);
#endif

  correspondences = (int *) malloc(num_frm_corners * 4 *
                                   sizeof(*correspondences));

  num_correspondences = determine_correspondence(frm->y_buffer,
                                                (int*)frm_corners,
                                                 num_frm_corners,
                                                 ref->y_buffer,
                                                 (int*)ref_corners,
                                                 num_ref_corners,
                                                 frm->y_width, frm->y_height,
                                                 frm->y_stride, ref->y_stride,
                                                 correspondences);
#ifdef VERBOSE
  printf("Number of correspondences = %d\n", num_correspondences);
#endif
  inlier_map = (int *)malloc(num_correspondences * sizeof(int));
  num_inliers = compute_global_motion_single(type, correspondences,
                                             num_correspondences, H,
                                             inlier_map);
#ifdef VERBOSE
  printf("Inliers = %d\n", num_inliers);
  printf("Error Score (inliers) = %g\n",
         compute_error_score(type, correspondences, 4, correspondences + 2, 4,
                             num_correspondences, H, inlier_map));
#endif
  free(correspondences);
  free(inlier_map);
  return (num_inliers > 0);
}

static int compute_global_motion_multiple(TransformationType type,
                                          int *correspondences,
                                          int num_correspondences,
                                          double *H,
                                          int max_models,
                                          double inlier_prob,
                                          int *num_models,
                                          int *processed_mask) {
  int *cp = correspondences;
  double *mp, *matched_points;
  int *best_inlier_mask;
  int i, result;
  int num_points = 0;
  int num_inliers = 0;
  int num_inliers_sum = 0;
  const int numparams = get_numparams(type);
  ransacType ransac = get_ransacType(type);
  if (ransac == NULL)
    return 0;
  matched_points =
      (double *)malloc(4 * num_correspondences * sizeof(double));
  best_inlier_mask =
      (int *)malloc(num_correspondences * sizeof(int));
  for (i = 0; i < num_correspondences; ++i)
    processed_mask[i] = -1;
  *num_models = 0;

  while ((double)num_inliers_sum / (double)num_correspondences < inlier_prob &&
         *num_models < max_models) {
    num_points = 0;
    for (mp = matched_points, cp = correspondences, i = 0;
         i < num_correspondences; ++i) {
      if (processed_mask[i] == -1) {
        *mp++ = *cp++;
        *mp++ = *cp++;
        *mp++ = *cp++;
        *mp++ = *cp++;
        num_points++;
      } else {
        cp += 4;
      }
    }
    result = ransac(matched_points, num_points,
                    &num_inliers, best_inlier_mask,
                    &H[(*num_models) * numparams]);
    if (!result && num_inliers < MIN_INLIER_PROB * num_correspondences) {
      result = 1;
      num_inliers = 0;
    }
    if (!result) {
      num_points = 0;
      for (i = 0; i < num_correspondences; ++i) {
        if (processed_mask[i] == -1) {
          if (best_inlier_mask[num_points]) processed_mask[i] = *num_models;
          num_points++;
        }
      }
      num_inliers_sum += num_inliers;
      (*num_models)++;
    } else {
      break;
    }
  }
  free(matched_points);
  free(best_inlier_mask);
  return num_inliers_sum;
}

// Returns number of models actually returned
int vp9_compute_global_motion_multiple_feature_based(
    struct VP9_COMP *cpi,
    TransformationType type,
    YV12_BUFFER_CONFIG *frm,
    YV12_BUFFER_CONFIG *ref,
    int max_models,
    double inlier_prob,
    double *H) {
  int num_frm_corners, num_ref_corners;
  int num_correspondences;
  int *correspondences;
  int num_inliers;
  int frm_corners[2 * MAX_CORNERS], ref_corners[2 * MAX_CORNERS];
  int num_models = 0;
  int *inlier_map = NULL;
  (void) cpi;

#ifdef USE_FAST_CORNER
  num_frm_corners = FastCornerDetect(frm->y_buffer, frm->y_width,
                                     frm->y_height, frm->y_stride,
                                     frm_corners, MAX_CORNERS);
  num_ref_corners = FastCornerDetect(ref->y_buffer, ref->y_width,
                                     ref->y_height, ref->y_stride,
                                     ref_corners, MAX_CORNERS);
#else
  num_frm_corners = HarrisCornerDetect(frmbuf, width, height, frm_stride,
                                       frm_corners, MAX_CORNERS);
  num_ref_corners = HarrisCornerDetect(refbuf, width, height, ref_stride,
                                       ref_corners, MAX_CORNERS);
#endif
#ifdef VERBOSE
  printf("Frame corners     = %d\n", num_frm_corners);
  printf("Reference corners = %d\n", num_ref_corners);
#endif

  correspondences = (int *) malloc(num_frm_corners * 4 *
                                   sizeof(*correspondences));

  num_correspondences = determine_correspondence(frm->y_buffer,
                                                 (int*)frm_corners,
                                                 num_frm_corners,
                                                 ref->y_buffer,
                                                 (int*)ref_corners,
                                                 num_ref_corners,
                                                 frm->y_width, frm->y_height,
                                                 frm->y_stride, ref->y_stride,
                                                 correspondences);
#ifdef VERBOSE
  printf("Number of correspondences = %d\n", num_correspondences);
#endif
  inlier_map = (int *)malloc(num_correspondences * sizeof(int));
  num_inliers = compute_global_motion_multiple(type, correspondences,
                                               num_correspondences, H,
                                               max_models, inlier_prob,
                                               &num_models, inlier_map);
#ifdef VERBOSE
  printf("Models = %d, Inliers = %d\n", num_models, num_inliers);
  if (num_models)
    printf("Error Score (inliers) = %g\n",
           compute_error_score(type, correspondences, 4, correspondences + 2, 4,
                               num_correspondences, H, inlier_map));
#endif
  (void) num_inliers;
  free(correspondences);
  free(inlier_map);
  return num_models;
}

// Returns number of models actually returned: 1 - if success, 0 - if failure
int vp9_compute_global_motion_single_block_based(struct VP9_COMP *cpi,
                                                 TransformationType type,
                                                 YV12_BUFFER_CONFIG *frm,
                                                 YV12_BUFFER_CONFIG *ref,
                                                 BLOCK_SIZE bsize,
                                                 double *H) {
  VP9_COMMON *const cm = &cpi->common;
  int num_correspondences = 0;
  int *correspondences;
  int num_inliers;
  int *inlier_map = NULL;
  int bwidth = num_4x4_blocks_wide_lookup[bsize] << 2;
  int bheight = num_4x4_blocks_high_lookup[bsize] << 2;
  int i;
  MV motionfield[4096];
  double confidence[4096];

  vp9_get_frame_motionfield(cpi, frm, ref, bsize, motionfield, confidence);

  correspondences = (int *)malloc(4 * cm->mb_rows * cm->mb_cols *
                                  sizeof(*correspondences));

  for (i = 0; i < cm->mb_rows * cm->mb_cols; i ++) {
      int x = (i % cm->mb_cols) * bwidth + bwidth / 2;
      int y = (i / cm->mb_cols) * bheight + bheight / 2;
      if (confidence[i] > CONFIDENCE_THRESHOLD) {
        correspondences[num_correspondences * 4]   = x;
        correspondences[num_correspondences * 4 + 1] = y;
        correspondences[num_correspondences * 4 + 2] =
            (double)motionfield[i].col / 8 + x;
        correspondences[num_correspondences * 4 + 3] =
            (double)motionfield[i].row / 8 + y;
        num_correspondences++;
      }
  }

  inlier_map = (int *)malloc(num_correspondences * sizeof(*inlier_map));

  num_inliers = compute_global_motion_single(type, correspondences,
                                             num_correspondences, H,
                                             inlier_map);
#ifdef VERBOSE
  printf("Inliers = %d\n", num_inliers);
  printf("Error Score (inliers) = %g\n",
         compute_error_score(type, correspondences, 4, correspondences + 2, 4,
                             num_correspondences, H, inlier_map));
#endif
  free(correspondences);
  free(inlier_map);
  return (num_inliers > 0);
}

// Returns number of models actually returned
int vp9_compute_global_motion_multiple_block_based(struct VP9_COMP *cpi,
                                                   TransformationType type,
                                                   YV12_BUFFER_CONFIG *frm,
                                                   YV12_BUFFER_CONFIG *ref,
                                                   BLOCK_SIZE bsize,
                                                   int max_models,
                                                   double inlier_prob,
                                                   double *H) {
  VP9_COMMON *const cm = &cpi->common;
  int num_correspondences = 0;
  int *correspondences;
  int num_inliers;
  int num_models = 0;
  int *inlier_map = NULL;
  int bwidth = num_4x4_blocks_wide_lookup[bsize] << 2;
  int bheight = num_4x4_blocks_high_lookup[bsize] << 2;

  int i;
  MV motionfield[4096];
  double confidence[4096];
  vp9_get_frame_motionfield(cpi, frm, ref, bsize, motionfield, confidence);

  correspondences = (int *)malloc(4 * cm->mb_rows * cm->mb_cols *
                                  sizeof(*correspondences));

  for (i = 0; i < cm->mb_rows * cm->mb_cols; i ++) {
      int x = (i % cm->mb_cols) * bwidth + bwidth / 2;
      int y = (i / cm->mb_cols) * bheight + bheight / 2;
      if (confidence[i] > CONFIDENCE_THRESHOLD) {
        correspondences[num_correspondences * 4]   = x;
        correspondences[num_correspondences * 4 + 1] = y;
        correspondences[num_correspondences * 4 + 2] =
            (double)motionfield[i].col / 8 + x;
        correspondences[num_correspondences * 4 + 3] =
            (double)motionfield[i].row / 8 + y;
        num_correspondences++;
      }
  }

  inlier_map = (int *)malloc(num_correspondences * sizeof(*inlier_map));
  num_inliers = compute_global_motion_multiple(type, correspondences,
                                               num_correspondences, H,
                                               max_models, inlier_prob,
                                               &num_models, inlier_map);
#ifdef VERBOSE
  printf("Models = %d, Inliers = %d\n", num_models, num_inliers);
  if (num_models)
    printf("Error Score (inliers) = %g\n",
           compute_error_score(type, correspondences, 4, correspondences + 2, 4,
                               num_correspondences, H, inlier_map));
#endif
  (void) num_inliers;
  free(correspondences);
  free(inlier_map);
  return num_models;
}
