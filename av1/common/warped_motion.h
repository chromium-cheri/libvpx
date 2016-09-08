/*
 *  Copyright (c) 2016 The WebM project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be
 *  found  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#ifndef AV1_COMMON_WARPED_MOTION_H
#define AV1_COMMON_WARPED_MOTION_H

#include <stdio.h>
#include <stdlib.h>
#include <memory.h>
#include <math.h>
#include <assert.h>

#include "./aom_config.h"
#include "aom_ports/mem.h"
#include "aom_dsp/aom_dsp_common.h"
#include "av1/common/mv.h"

#define DEFAULT_WMTYPE AFFINE

// Bits of precision used for the model
#define WARPEDMODEL_PREC_BITS 8
#define WARPEDMODEL_ROW3HOMO_PREC_BITS 12

// Bits of subpel precision for warped interpolation
#define WARPEDPIXEL_PREC_BITS 6
#define WARPEDPIXEL_PREC_SHIFTS (1 << WARPEDPIXEL_PREC_BITS)

// Taps for ntap filter
#define WARPEDPIXEL_FILTER_TAPS 6

// Precision of filter taps
#define WARPEDPIXEL_FILTER_BITS 7

#define WARPEDDIFF_PREC_BITS (WARPEDMODEL_PREC_BITS - WARPEDPIXEL_PREC_BITS)

#define MAX_PARAMDIM 9

typedef void (*ProjectPointsType)(int16_t *mat, int *points, int *proj,
                                  const int n, const int stride_points,
                                  const int stride_proj,
                                  const int subsampling_x,
                                  const int subsampling_y);

void projectPointsHomography(int16_t *mat, int *points, int *proj, const int n,
                             const int stride_points, const int stride_proj,
                             const int subsampling_x, const int subsampling_y);

void projectPointsAffine(int16_t *mat, int *points, int *proj, const int n,
                         const int stride_points, const int stride_proj,
                         const int subsampling_x, const int subsampling_y);

void projectPointsRotZoom(int16_t *mat, int *points, int *proj, const int n,
                          const int stride_points, const int stride_proj,
                          const int subsampling_x, const int subsampling_y);

void projectPointsTranslation(int16_t *mat, int *points, int *proj, const int n,
                              const int stride_points, const int stride_proj,
                              const int subsampling_x, const int subsampling_y);

void projectPoints(WarpedMotionParams *wm_params, int *points, int *proj,
                   const int n, const int stride_points, const int stride_proj,
                   const int subsampling_x, const int subsampling_y);

double av1_warp_erroradv(WarpedMotionParams *wm,
#if CONFIG_AOM_HIGHBITDEPTH
                         int use_hbd, int bd,
#endif  // CONFIG_AOM_HIGHBITDEPTH
                         uint8_t *ref, int width, int height, int stride,
                         uint8_t *dst, int p_col, int p_row, int p_width,
                         int p_height, int p_stride, int subsampling_x,
                         int subsampling_y, int x_scale, int y_scale);

void av1_warp_plane(WarpedMotionParams *wm,
#if CONFIG_AOM_HIGHBITDEPTH
                    int use_hbd, int bd,
#endif  // CONFIG_AOM_HIGHBITDEPTH
                    uint8_t *ref, int width, int height, int stride,
                    uint8_t *pred, int p_col, int p_row, int p_width,
                    int p_height, int p_stride, int subsampling_x,
                    int subsampling_y, int x_scale, int y_scale);

// Integerize model into the WarpedMotionParams structure
void av1_integerize_model(const double *model, TransformationType wmtype,
                          WarpedMotionParams *wm);

// Estimate projection models from projection samples
int findTranslation(const int np, double *pts1, double *pts2, double *mat);
int findRotZoom(const int np, double *pts1, double *pts2, double *mat);
int findAffine(const int np, double *pts1, double *pts2, double *mat);
int findHomography(const int np, double *pts1, double *pts2, double *mat);
int findHomographyScale1(const int np, double *pts1, double *pts2, double *mat);
int findProjection(const int np, double *pts1, double *pts2,
                   WarpedMotionParams *wm_params);
#endif  // AV1_COMMON_WARPED_MOTION_H
