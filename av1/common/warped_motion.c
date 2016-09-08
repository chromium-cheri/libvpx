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

#include "av1/common/warped_motion.h"

int IMIN(int a, int b) { return (((a) < (b)) ? (a) : (b)); }

static ProjectPointsType get_project_points_type(TransformationType type) {
  switch (type) {
    case HOMOGRAPHY: return projectPointsHomography;
    case AFFINE: return projectPointsAffine;
    case ROTZOOM: return projectPointsRotZoom;
    case TRANSLATION: return projectPointsTranslation;
    default: assert(0); return NULL;
  }
}

void projectPointsTranslation(int16_t *mat, int *points, int *proj, const int n,
                              const int stride_points, const int stride_proj,
                              const int subsampling_x,
                              const int subsampling_y) {
  int i;
  for (i = 0; i < n; ++i) {
    const int x = *(points++), y = *(points++);
    if (subsampling_x)
      *(proj++) = ROUND_POWER_OF_TWO_SIGNED(
          ((x << (WARPEDMODEL_PREC_BITS + 1)) + mat[1]),
          WARPEDDIFF_PREC_BITS + 1);
    else
      *(proj++) = ROUND_POWER_OF_TWO_SIGNED(
          ((x << WARPEDMODEL_PREC_BITS) + mat[1]), WARPEDDIFF_PREC_BITS);
    if (subsampling_y)
      *(proj++) = ROUND_POWER_OF_TWO_SIGNED(
          ((y << (WARPEDMODEL_PREC_BITS + 1)) + mat[0]),
          WARPEDDIFF_PREC_BITS + 1);
    else
      *(proj++) = ROUND_POWER_OF_TWO_SIGNED(
          ((y << WARPEDMODEL_PREC_BITS)) + mat[0], WARPEDDIFF_PREC_BITS);
    points += stride_points - 2;
    proj += stride_proj - 2;
  }
}

void projectPointsRotZoom(int16_t *mat, int *points, int *proj, const int n,
                          const int stride_points, const int stride_proj,
                          const int subsampling_x, const int subsampling_y) {
  int i;
  for (i = 0; i < n; ++i) {
    const int x = *(points++), y = *(points++);
    if (subsampling_x)
      *(proj++) = ROUND_POWER_OF_TWO_SIGNED(
          mat[3] * 2 * x + mat[2] * 2 * y + mat[1] +
              (mat[3] + mat[2] - (1 << WARPEDMODEL_PREC_BITS)) / 2,
          WARPEDDIFF_PREC_BITS + 1);
    else
      *(proj++) = ROUND_POWER_OF_TWO_SIGNED(mat[3] * x + mat[2] * y + mat[1],
                                            WARPEDDIFF_PREC_BITS);
    if (subsampling_y)
      *(proj++) = ROUND_POWER_OF_TWO_SIGNED(
          -mat[2] * 2 * x + mat[3] * 2 * y + mat[0] +
              (-mat[2] + mat[3] - (1 << WARPEDMODEL_PREC_BITS)) / 2,
          WARPEDDIFF_PREC_BITS + 1);
    else
      *(proj++) = ROUND_POWER_OF_TWO_SIGNED(-mat[2] * x + mat[3] * y + mat[0],
                                            WARPEDDIFF_PREC_BITS);
    points += stride_points - 2;
    proj += stride_proj - 2;
  }
}

void projectPointsAffine(int16_t *mat, int *points, int *proj, const int n,
                         const int stride_points, const int stride_proj,
                         const int subsampling_x, const int subsampling_y) {
  int i;
  for (i = 0; i < n; ++i) {
    const int x = *(points++), y = *(points++);
    if (subsampling_x)
      *(proj++) = ROUND_POWER_OF_TWO_SIGNED(
          mat[3] * 2 * x + mat[2] * 2 * y + mat[1] +
              (mat[3] + mat[2] - (1 << WARPEDMODEL_PREC_BITS)) / 2,
          WARPEDDIFF_PREC_BITS + 1);
    else
      *(proj++) = ROUND_POWER_OF_TWO_SIGNED(mat[3] * x + mat[2] * y + mat[1],
                                            WARPEDDIFF_PREC_BITS);
    if (subsampling_y)
      *(proj++) = ROUND_POWER_OF_TWO_SIGNED(
          mat[5] * 2 * x + mat[4] * 2 * y + mat[0] +
              (mat[5] + mat[4] - (1 << WARPEDMODEL_PREC_BITS)) / 2,
          WARPEDDIFF_PREC_BITS + 1);
    else
      *(proj++) = ROUND_POWER_OF_TWO_SIGNED(mat[5] * x + mat[4] * y + mat[0],
                                            WARPEDDIFF_PREC_BITS);
    points += stride_points - 2;
    proj += stride_proj - 2;
  }
}

void projectPointsHomography(int16_t *mat, int *points, int *proj, const int n,
                             const int stride_points, const int stride_proj,
                             const int subsampling_x, const int subsampling_y) {
  int i;
  int64_t x, y, Z;
  int64_t xp, yp;
  for (i = 0; i < n; ++i) {
    x = *(points++), y = *(points++);
    x = (subsampling_x ? 4 * x + 1 : 2 * x);
    y = (subsampling_y ? 4 * y + 1 : 2 * y);

    Z = (mat[7] * x + mat[6] * y + (1 << (WARPEDMODEL_ROW3HOMO_PREC_BITS + 1)));
    xp = (mat[1] * x + mat[0] * y + 2 * mat[3])
         << (WARPEDPIXEL_PREC_BITS + WARPEDMODEL_ROW3HOMO_PREC_BITS -
             WARPEDMODEL_PREC_BITS);
    yp = (mat[2] * x + mat[5] * y + 2 * mat[4])
         << (WARPEDPIXEL_PREC_BITS + WARPEDMODEL_ROW3HOMO_PREC_BITS -
             WARPEDMODEL_PREC_BITS);

    xp = xp > 0 ? (xp + Z / 2) / Z : (xp - Z / 2) / Z;
    yp = yp > 0 ? (yp + Z / 2) / Z : (yp - Z / 2) / Z;

    if (subsampling_x) xp = (xp - (1 << (WARPEDPIXEL_PREC_BITS - 1))) / 2;
    if (subsampling_y) yp = (yp - (1 << (WARPEDPIXEL_PREC_BITS - 1))) / 2;
    *(proj++) = xp;
    *(proj++) = yp;

    points += stride_points - 2;
    proj += stride_proj - 2;
  }
}

// 'points' are at original scale, output 'proj's are scaled up by
// 1 << WARPEDPIXEL_PREC_BITS
void projectPoints(WarpedMotionParams *wm_params, int *points, int *proj,
                   const int n, const int stride_points, const int stride_proj,
                   const int subsampling_x, const int subsampling_y) {
  switch (wm_params->wmtype) {
    case AFFINE:
      projectPointsAffine((int16_t *)wm_params->wmmat, points, proj, n,
                          stride_points, stride_proj, subsampling_x,
                          subsampling_y);
      break;
    case ROTZOOM:
      projectPointsRotZoom((int16_t *)wm_params->wmmat, points, proj, n,
                           stride_points, stride_proj, subsampling_x,
                           subsampling_y);
      break;
    case HOMOGRAPHY:
      projectPointsHomography((int16_t *)wm_params->wmmat, points, proj, n,
                              stride_points, stride_proj, subsampling_x,
                              subsampling_y);
      break;
    default:
      assert("Invalid warped motion type!\n");
      return;
  }
}

static const int16_t filter_4tap[WARPEDPIXEL_PREC_SHIFTS][4] = {
  { 0, 128, 0, 0 },     { -1, 127, 2, 0 },    { -2, 127, 4, -1 },
  { -3, 126, 6, -1 },   { -3, 125, 8, -2 },   { -4, 124, 11, -3 },
  { -5, 123, 13, -3 },  { -5, 121, 15, -3 },  { -6, 120, 18, -4 },
  { -7, 119, 20, -4 },  { -7, 118, 22, -5 },  { -8, 116, 25, -5 },
  { -8, 115, 27, -6 },  { -9, 113, 30, -6 },  { -9, 112, 32, -7 },
  { -9, 110, 34, -7 },  { -10, 108, 37, -7 }, { -10, 107, 39, -8 },
  { -10, 105, 41, -8 }, { -11, 103, 44, -8 }, { -11, 101, 47, -9 },
  { -11, 99, 49, -9 },  { -11, 97, 51, -9 },  { -11, 95, 54, -10 },
  { -11, 93, 56, -10 }, { -12, 91, 59, -10 }, { -12, 89, 61, -10 },
  { -12, 87, 64, -11 }, { -12, 85, 66, -11 }, { -12, 82, 69, -11 },
  { -12, 80, 71, -11 }, { -12, 78, 73, -11 }, { -11, 75, 75, -11 },
  { -11, 73, 78, -12 }, { -11, 71, 80, -12 }, { -11, 69, 82, -12 },
  { -11, 66, 85, -12 }, { -11, 64, 87, -12 }, { -10, 61, 89, -12 },
  { -10, 59, 91, -12 }, { -10, 56, 93, -11 }, { -10, 54, 95, -11 },
  { -9, 51, 97, -11 },  { -9, 49, 99, -11 },  { -9, 47, 101, -11 },
  { -8, 44, 103, -11 }, { -8, 41, 105, -10 }, { -8, 39, 107, -10 },
  { -7, 37, 108, -10 }, { -7, 34, 110, -9 },  { -7, 32, 112, -9 },
  { -6, 30, 113, -9 },  { -6, 27, 115, -8 },  { -5, 25, 116, -8 },
  { -5, 22, 118, -7 },  { -4, 20, 119, -7 },  { -4, 18, 120, -6 },
  { -3, 15, 121, -5 },  { -3, 13, 123, -5 },  { -3, 11, 124, -4 },
  { -2, 8, 125, -3 },   { -1, 6, 126, -3 },   { -1, 4, 127, -2 },
  { 0, 2, 127, -1 },
};

static const int16_t
    filter_ntap[WARPEDPIXEL_PREC_SHIFTS][WARPEDPIXEL_FILTER_TAPS] = {
      { 0, 0, 128, 0, 0, 0 },      { 0, -1, 128, 2, -1, 0 },
      { 1, -3, 127, 4, -1, 0 },    { 1, -4, 126, 6, -2, 1 },
      { 1, -5, 126, 8, -3, 1 },    { 1, -6, 125, 11, -4, 1 },
      { 1, -7, 124, 13, -4, 1 },   { 2, -8, 123, 15, -5, 1 },
      { 2, -9, 122, 18, -6, 1 },   { 2, -10, 121, 20, -6, 1 },
      { 2, -11, 120, 22, -7, 2 },  { 2, -12, 119, 25, -8, 2 },
      { 3, -13, 117, 27, -8, 2 },  { 3, -13, 116, 29, -9, 2 },
      { 3, -14, 114, 32, -10, 3 }, { 3, -15, 113, 35, -10, 2 },
      { 3, -15, 111, 37, -11, 3 }, { 3, -16, 109, 40, -11, 3 },
      { 3, -16, 108, 42, -12, 3 }, { 4, -17, 106, 45, -13, 3 },
      { 4, -17, 104, 47, -13, 3 }, { 4, -17, 102, 50, -14, 3 },
      { 4, -17, 100, 52, -14, 3 }, { 4, -18, 98, 55, -15, 4 },
      { 4, -18, 96, 58, -15, 3 },  { 4, -18, 94, 60, -16, 4 },
      { 4, -18, 91, 63, -16, 4 },  { 4, -18, 89, 65, -16, 4 },
      { 4, -18, 87, 68, -17, 4 },  { 4, -18, 85, 70, -17, 4 },
      { 4, -18, 82, 73, -17, 4 },  { 4, -18, 80, 75, -17, 4 },
      { 4, -18, 78, 78, -18, 4 },  { 4, -17, 75, 80, -18, 4 },
      { 4, -17, 73, 82, -18, 4 },  { 4, -17, 70, 85, -18, 4 },
      { 4, -17, 68, 87, -18, 4 },  { 4, -16, 65, 89, -18, 4 },
      { 4, -16, 63, 91, -18, 4 },  { 4, -16, 60, 94, -18, 4 },
      { 3, -15, 58, 96, -18, 4 },  { 4, -15, 55, 98, -18, 4 },
      { 3, -14, 52, 100, -17, 4 }, { 3, -14, 50, 102, -17, 4 },
      { 3, -13, 47, 104, -17, 4 }, { 3, -13, 45, 106, -17, 4 },
      { 3, -12, 42, 108, -16, 3 }, { 3, -11, 40, 109, -16, 3 },
      { 3, -11, 37, 111, -15, 3 }, { 2, -10, 35, 113, -15, 3 },
      { 3, -10, 32, 114, -14, 3 }, { 2, -9, 29, 116, -13, 3 },
      { 2, -8, 27, 117, -13, 3 },  { 2, -8, 25, 119, -12, 2 },
      { 2, -7, 22, 120, -11, 2 },  { 1, -6, 20, 121, -10, 2 },
      { 1, -6, 18, 122, -9, 2 },   { 1, -5, 15, 123, -8, 2 },
      { 1, -4, 13, 124, -7, 1 },   { 1, -4, 11, 125, -6, 1 },
      { 1, -3, 8, 126, -5, 1 },    { 1, -2, 6, 126, -4, 1 },
      { 0, -1, 4, 127, -3, 1 },    { 0, -1, 2, 128, -1, 0 },
    };

static int32_t do_ntap_filter(int32_t *p, int x) {
  int i;
  int32_t sum = 0;
  for (i = 0; i < WARPEDPIXEL_FILTER_TAPS; ++i) {
    sum += p[i - WARPEDPIXEL_FILTER_TAPS / 2 + 1] * filter_ntap[x][i];
  }
  return sum;
}

static int32_t do_cubic_filter(int32_t *p, int x) {
  if (x == 0) {
    return p[0];
  } else if (x == (1 << WARPEDPIXEL_PREC_BITS)) {
    return p[1];
  } else {
    const int64_t v1 = x * x * x * (3 * (p[0] - p[1]) + p[2] - p[-1]);
    const int64_t v2 = x * x * (2 * p[-1] - 5 * p[0] + 4 * p[1] - p[2]);
    const int64_t v3 = x * (p[1] - p[-1]);
    const int64_t v4 = 2 * p[0];
    return (int32_t)ROUND_POWER_OF_TWO_SIGNED(
        (v4 << (3 * WARPEDPIXEL_PREC_BITS)) +
            (v3 << (2 * WARPEDPIXEL_PREC_BITS)) +
            (v2 << WARPEDPIXEL_PREC_BITS) + v1,
        3 * WARPEDPIXEL_PREC_BITS + 1 - WARPEDPIXEL_FILTER_BITS);
  }
}

/*
static int32_t do_linear_filter(int32_t *p, int x) {
  int32_t sum = 0;
  sum = p[0] * (WARPEDPIXEL_PREC_SHIFTS - x) + p[1] * x;
  sum <<= (WARPEDPIXEL_FILTER_BITS - WARPEDPIXEL_PREC_BITS);
  return sum;
}

static int32_t do_4tap_filter(int32_t *p, int x) {
  int i;
  int32_t sum = 0;
  for (i = 0; i < 4; ++i) {
    sum += p[i - 1] * filter_4tap[x][i];
  }
  return sum;
}
*/

static INLINE void get_subcolumn(int taps, uint8_t *ref, int32_t *col,
                                 int stride, int x, int y_start) {
  int i;
  for (i = 0; i < taps; ++i) {
    col[i] = ref[(i + y_start) * stride + x];
  }
}

static uint8_t bi_ntap_filter(uint8_t *ref, int x, int y, int stride) {
  int32_t val, arr[WARPEDPIXEL_FILTER_TAPS];
  int k;
  int i = (int)x >> WARPEDPIXEL_PREC_BITS;
  int j = (int)y >> WARPEDPIXEL_PREC_BITS;
  for (k = 0; k < WARPEDPIXEL_FILTER_TAPS; ++k) {
    int32_t arr_temp[WARPEDPIXEL_FILTER_TAPS];
    get_subcolumn(WARPEDPIXEL_FILTER_TAPS, ref, arr_temp, stride,
                  i + k + 1 - WARPEDPIXEL_FILTER_TAPS / 2,
                  j + 1 - WARPEDPIXEL_FILTER_TAPS / 2);
    arr[k] = do_ntap_filter(arr_temp + WARPEDPIXEL_FILTER_TAPS / 2 - 1,
                            y - (j << WARPEDPIXEL_PREC_BITS));
  }
  val = do_ntap_filter(arr + WARPEDPIXEL_FILTER_TAPS / 2 - 1,
                       x - (i << WARPEDPIXEL_PREC_BITS));
  val = ROUND_POWER_OF_TWO_SIGNED(val, WARPEDPIXEL_FILTER_BITS * 2);
  return (uint8_t)clip_pixel(val);
}

static uint8_t bi_cubic_filter(uint8_t *ref, int x, int y, int stride) {
  int32_t val, arr[4];
  int k;
  int i = (int)x >> WARPEDPIXEL_PREC_BITS;
  int j = (int)y >> WARPEDPIXEL_PREC_BITS;
  for (k = 0; k < 4; ++k) {
    int32_t arr_temp[4];
    get_subcolumn(4, ref, arr_temp, stride, i + k - 1, j - 1);
    arr[k] = do_cubic_filter(arr_temp + 1, y - (j << WARPEDPIXEL_PREC_BITS));
  }
  val = do_cubic_filter(arr + 1, x - (i << WARPEDPIXEL_PREC_BITS));
  val = ROUND_POWER_OF_TWO_SIGNED(val, WARPEDPIXEL_FILTER_BITS * 2);
  return (uint8_t)clip_pixel(val);
}

static uint8_t bi_linear_filter(uint8_t *ref, int x, int y, int stride) {
  const int ix = x >> WARPEDPIXEL_PREC_BITS;
  const int iy = y >> WARPEDPIXEL_PREC_BITS;
  const int sx = x - (ix << WARPEDPIXEL_PREC_BITS);
  const int sy = y - (iy << WARPEDPIXEL_PREC_BITS);
  int32_t val;
  val = ROUND_POWER_OF_TWO_SIGNED(
      ref[iy * stride + ix] * (WARPEDPIXEL_PREC_SHIFTS - sy) *
              (WARPEDPIXEL_PREC_SHIFTS - sx) +
          ref[iy * stride + ix + 1] * (WARPEDPIXEL_PREC_SHIFTS - sy) * sx +
          ref[(iy + 1) * stride + ix] * sy * (WARPEDPIXEL_PREC_SHIFTS - sx) +
          ref[(iy + 1) * stride + ix + 1] * sy * sx,
      WARPEDPIXEL_PREC_BITS * 2);
  return (uint8_t)clip_pixel(val);
}

static uint8_t warp_interpolate(uint8_t *ref, int x, int y, int width,
                                int height, int stride) {
  int ix = x >> WARPEDPIXEL_PREC_BITS;
  int iy = y >> WARPEDPIXEL_PREC_BITS;
  int sx = x - (ix << WARPEDPIXEL_PREC_BITS);
  int sy = y - (iy << WARPEDPIXEL_PREC_BITS);
  int32_t v;

  if (ix < 0 && iy < 0)
    return ref[0];
  else if (ix < 0 && iy > height - 1)
    return ref[(height - 1) * stride];
  else if (ix > width - 1 && iy < 0)
    return ref[width - 1];
  else if (ix > width - 1 && iy > height - 1)
    return ref[(height - 1) * stride + (width - 1)];
  else if (ix < 0) {
    v = ROUND_POWER_OF_TWO_SIGNED(
        ref[iy * stride] * (WARPEDPIXEL_PREC_SHIFTS - sy) +
            ref[(iy + 1) * stride] * sy,
        WARPEDPIXEL_PREC_BITS);
    return clip_pixel(v);
  } else if (iy < 0) {
    v = ROUND_POWER_OF_TWO_SIGNED(
        ref[ix] * (WARPEDPIXEL_PREC_SHIFTS - sx) + ref[ix + 1] * sx,
        WARPEDPIXEL_PREC_BITS);
    return clip_pixel(v);
  } else if (ix > width - 1) {
    v = ROUND_POWER_OF_TWO_SIGNED(
        ref[iy * stride + width - 1] * (WARPEDPIXEL_PREC_SHIFTS - sy) +
            ref[(iy + 1) * stride + width - 1] * sy,
        WARPEDPIXEL_PREC_BITS);
    return clip_pixel(v);
  } else if (iy > height - 1) {
    v = ROUND_POWER_OF_TWO_SIGNED(
        ref[(height - 1) * stride + ix] * (WARPEDPIXEL_PREC_SHIFTS - sx) +
            ref[(height - 1) * stride + ix + 1] * sx,
        WARPEDPIXEL_PREC_BITS);
    return clip_pixel(v);
  } else if (ix >= WARPEDPIXEL_FILTER_TAPS / 2 - 1 &&
             iy >= WARPEDPIXEL_FILTER_TAPS / 2 - 1 &&
             ix < width - WARPEDPIXEL_FILTER_TAPS / 2 &&
             iy < height - WARPEDPIXEL_FILTER_TAPS / 2) {
    return bi_ntap_filter(ref, x, y, stride);
  } else if (ix >= 1 && iy >= 1 && ix < width - 2 && iy < height - 2) {
    return bi_cubic_filter(ref, x, y, stride);
  } else {
    return bi_linear_filter(ref, x, y, stride);
  }
}

#if CONFIG_AOM_HIGHBITDEPTH
static INLINE void highbd_get_subcolumn(int taps, uint16_t *ref, int32_t *col,
                                        int stride, int x, int y_start) {
  int i;
  for (i = 0; i < taps; ++i) {
    col[i] = ref[(i + y_start) * stride + x];
  }
}

static uint16_t highbd_bi_ntap_filter(uint16_t *ref, int x, int y, int stride,
                                      int bd) {
  int32_t val, arr[WARPEDPIXEL_FILTER_TAPS];
  int k;
  int i = (int)x >> WARPEDPIXEL_PREC_BITS;
  int j = (int)y >> WARPEDPIXEL_PREC_BITS;
  for (k = 0; k < WARPEDPIXEL_FILTER_TAPS; ++k) {
    int32_t arr_temp[WARPEDPIXEL_FILTER_TAPS];
    highbd_get_subcolumn(WARPEDPIXEL_FILTER_TAPS, ref, arr_temp, stride,
                         i + k + 1 - WARPEDPIXEL_FILTER_TAPS / 2,
                         j + 1 - WARPEDPIXEL_FILTER_TAPS / 2);
    arr[k] = do_ntap_filter(arr_temp + WARPEDPIXEL_FILTER_TAPS / 2 - 1,
                            y - (j << WARPEDPIXEL_PREC_BITS));
  }
  val = do_ntap_filter(arr + WARPEDPIXEL_FILTER_TAPS / 2 - 1,
                       x - (i << WARPEDPIXEL_PREC_BITS));
  val = ROUND_POWER_OF_TWO_SIGNED(val, WARPEDPIXEL_FILTER_BITS * 2);
  return (uint16_t)clip_pixel_highbd(val, bd);
}

static uint16_t highbd_bi_cubic_filter(uint16_t *ref, int x, int y, int stride,
                                       int bd) {
  int32_t val, arr[4];
  int k;
  int i = (int)x >> WARPEDPIXEL_PREC_BITS;
  int j = (int)y >> WARPEDPIXEL_PREC_BITS;
  for (k = 0; k < 4; ++k) {
    int32_t arr_temp[4];
    highbd_get_subcolumn(4, ref, arr_temp, stride, i + k - 1, j - 1);
    arr[k] = do_cubic_filter(arr_temp + 1, y - (j << WARPEDPIXEL_PREC_BITS));
  }
  val = do_cubic_filter(arr + 1, x - (i << WARPEDPIXEL_PREC_BITS));
  val = ROUND_POWER_OF_TWO_SIGNED(val, WARPEDPIXEL_FILTER_BITS * 2);
  return (uint16_t)clip_pixel_highbd(val, bd);
}

static uint16_t highbd_bi_linear_filter(uint16_t *ref, int x, int y, int stride,
                                        int bd) {
  const int ix = x >> WARPEDPIXEL_PREC_BITS;
  const int iy = y >> WARPEDPIXEL_PREC_BITS;
  const int sx = x - (ix << WARPEDPIXEL_PREC_BITS);
  const int sy = y - (iy << WARPEDPIXEL_PREC_BITS);
  int32_t val;
  val = ROUND_POWER_OF_TWO_SIGNED(
      ref[iy * stride + ix] * (WARPEDPIXEL_PREC_SHIFTS - sy) *
              (WARPEDPIXEL_PREC_SHIFTS - sx) +
          ref[iy * stride + ix + 1] * (WARPEDPIXEL_PREC_SHIFTS - sy) * sx +
          ref[(iy + 1) * stride + ix] * sy * (WARPEDPIXEL_PREC_SHIFTS - sx) +
          ref[(iy + 1) * stride + ix + 1] * sy * sx,
      WARPEDPIXEL_PREC_BITS * 2);
  return (uint16_t)clip_pixel_highbd(val, bd);
}

static uint16_t highbd_warp_interpolate(uint16_t *ref, int x, int y, int width,
                                        int height, int stride, int bd) {
  int ix = x >> WARPEDPIXEL_PREC_BITS;
  int iy = y >> WARPEDPIXEL_PREC_BITS;
  int sx = x - (ix << WARPEDPIXEL_PREC_BITS);
  int sy = y - (iy << WARPEDPIXEL_PREC_BITS);
  int32_t v;

  if (ix < 0 && iy < 0)
    return ref[0];
  else if (ix < 0 && iy > height - 1)
    return ref[(height - 1) * stride];
  else if (ix > width - 1 && iy < 0)
    return ref[width - 1];
  else if (ix > width - 1 && iy > height - 1)
    return ref[(height - 1) * stride + (width - 1)];
  else if (ix < 0) {
    v = ROUND_POWER_OF_TWO_SIGNED(
        ref[iy * stride] * (WARPEDPIXEL_PREC_SHIFTS - sy) +
            ref[(iy + 1) * stride] * sy,
        WARPEDPIXEL_PREC_BITS);
    return clip_pixel_highbd(v, bd);
  } else if (iy < 0) {
    v = ROUND_POWER_OF_TWO_SIGNED(
        ref[ix] * (WARPEDPIXEL_PREC_SHIFTS - sx) + ref[ix + 1] * sx,
        WARPEDPIXEL_PREC_BITS);
    return clip_pixel_highbd(v, bd);
  } else if (ix > width - 1) {
    v = ROUND_POWER_OF_TWO_SIGNED(
        ref[iy * stride + width - 1] * (WARPEDPIXEL_PREC_SHIFTS - sy) +
            ref[(iy + 1) * stride + width - 1] * sy,
        WARPEDPIXEL_PREC_BITS);
    return clip_pixel_highbd(v, bd);
  } else if (iy > height - 1) {
    v = ROUND_POWER_OF_TWO_SIGNED(
        ref[(height - 1) * stride + ix] * (WARPEDPIXEL_PREC_SHIFTS - sx) +
            ref[(height - 1) * stride + ix + 1] * sx,
        WARPEDPIXEL_PREC_BITS);
    return clip_pixel_highbd(v, bd);
  } else if (ix >= WARPEDPIXEL_FILTER_TAPS / 2 - 1 &&
             iy >= WARPEDPIXEL_FILTER_TAPS / 2 - 1 &&
             ix < width - WARPEDPIXEL_FILTER_TAPS / 2 &&
             iy < height - WARPEDPIXEL_FILTER_TAPS / 2) {
    return highbd_bi_ntap_filter(ref, x, y, stride, bd);
  } else if (ix >= 1 && iy >= 1 && ix < width - 2 && iy < height - 2) {
    return highbd_bi_cubic_filter(ref, x, y, stride, bd);
  } else {
    return highbd_bi_linear_filter(ref, x, y, stride, bd);
  }
}

static double highbd_warp_erroradv(WarpedMotionParams *wm, uint8_t *ref8,
                                   int width, int height, int stride,
                                   uint8_t *dst8, int p_col, int p_row,
                                   int p_width, int p_height, int p_stride,
                                   int subsampling_x, int subsampling_y,
                                   int x_scale, int y_scale, int bd) {
  int i, j;
  ProjectPointsType projectpoints = get_project_points_type(wm->wmtype);
  uint16_t *dst = CONVERT_TO_SHORTPTR(dst8);
  uint16_t *ref = CONVERT_TO_SHORTPTR(ref8);
  int gm_err = 0, no_gm_err = 0;
  int64_t gm_sumerr = 0, no_gm_sumerr = 0;
  for (i = p_row; i < p_row + p_height; ++i) {
    for (j = p_col; j < p_col + p_width; ++j) {
      int in[2], out[2];
      in[0] = j;
      in[1] = i;
      projectpoints((int16_t *)wm->wmmat, in, out, 1, 2, 2, subsampling_x,
                    subsampling_y);
      out[0] = ROUND_POWER_OF_TWO_SIGNED(out[0] * x_scale, 4);
      out[1] = ROUND_POWER_OF_TWO_SIGNED(out[1] * y_scale, 4);
      gm_err = dst[(j - p_col) + (i - p_row) * p_stride] -
               highbd_warp_interpolate(ref, out[0], out[1], width, height,
                                       stride, bd);
      no_gm_err = dst[(j - p_col) + (i - p_row) * p_stride] -
                  ref[(j - p_col) + (i - p_row) * stride];
      gm_sumerr += (int64_t)gm_err * gm_err;
      no_gm_sumerr += (int64_t)no_gm_err * no_gm_err;
    }
  }
  return (double)gm_sumerr / no_gm_sumerr;
}

static void highbd_warp_plane(WarpedMotionParams *wm, uint8_t *ref8, int width,
                              int height, int stride, uint8_t *pred8, int p_col,
                              int p_row, int p_width, int p_height,
                              int p_stride, int subsampling_x,
                              int subsampling_y, int x_scale, int y_scale,
                              int bd) {
  int i, j;
  ProjectPointsType projectpoints = get_project_points_type(wm->wmtype);
  uint16_t *pred = CONVERT_TO_SHORTPTR(pred8);
  uint16_t *ref = CONVERT_TO_SHORTPTR(ref8);
  if (projectpoints == NULL) return;
  for (i = p_row; i < p_row + p_height; ++i) {
    for (j = p_col; j < p_col + p_width; ++j) {
      int in[2], out[2];
      in[0] = j;
      in[1] = i;
      projectpoints((int16_t *)wm->wmmat, in, out, 1, 2, 2, subsampling_x,
                    subsampling_y);
      out[0] = ROUND_POWER_OF_TWO_SIGNED(out[0] * x_scale, 4);
      out[1] = ROUND_POWER_OF_TWO_SIGNED(out[1] * y_scale, 4);
      pred[(j - p_col) + (i - p_row) * p_stride] = highbd_warp_interpolate(
          ref, out[0], out[1], width, height, stride, bd);
    }
  }
}
#endif  // CONFIG_AOM_HIGHBITDEPTH

static double warp_erroradv(WarpedMotionParams *wm, uint8_t *ref, int width,
                            int height, int stride, uint8_t *dst, int p_col,
                            int p_row, int p_width, int p_height, int p_stride,
                            int subsampling_x, int subsampling_y, int x_scale,
                            int y_scale) {
  int gm_err = 0, no_gm_err = 0;
  int gm_sumerr = 0, no_gm_sumerr = 0;
  int i, j;
  ProjectPointsType projectpoints = get_project_points_type(wm->wmtype);
  for (i = p_row; i < p_row + p_height; ++i) {
    for (j = p_col; j < p_col + p_width; ++j) {
      int in[2], out[2];
      in[0] = j;
      in[1] = i;
      projectpoints((int16_t *)wm->wmmat, in, out, 1, 2, 2, subsampling_x,
                    subsampling_y);
      out[0] = ROUND_POWER_OF_TWO_SIGNED(out[0] * x_scale, 4);
      out[1] = ROUND_POWER_OF_TWO_SIGNED(out[1] * y_scale, 4);
      gm_err = dst[(j - p_col) + (i - p_row) * p_stride] -
               warp_interpolate(ref, out[0], out[1], width, height, stride);
      no_gm_err = dst[(j - p_col) + (i - p_row) * p_stride] -
                  ref[(j - p_col) + (i - p_row) * stride];
      gm_sumerr += gm_err * gm_err;
      no_gm_sumerr += no_gm_err * no_gm_err;
    }
  }
  return (double)gm_sumerr / no_gm_sumerr;
}

static void warp_plane(WarpedMotionParams *wm, uint8_t *ref, int width,
                       int height, int stride, uint8_t *pred, int p_col,
                       int p_row, int p_width, int p_height, int p_stride,
                       int subsampling_x, int subsampling_y, int x_scale,
                       int y_scale) {
  int i, j;
  ProjectPointsType projectpoints = get_project_points_type(wm->wmtype);
  if (projectpoints == NULL) return;
  for (i = p_row; i < p_row + p_height; ++i) {
    for (j = p_col; j < p_col + p_width; ++j) {
      int in[2], out[2];
      in[0] = j;
      in[1] = i;
      projectpoints((int16_t *)wm->wmmat, in, out, 1, 2, 2, subsampling_x,
                    subsampling_y);
      out[0] = ROUND_POWER_OF_TWO_SIGNED(out[0] * x_scale, 4);
      out[1] = ROUND_POWER_OF_TWO_SIGNED(out[1] * y_scale, 4);
      pred[(j - p_col) + (i - p_row) * p_stride] =
          warp_interpolate(ref, out[0], out[1], width, height, stride);
    }
  }
}

double av1_warp_erroradv(WarpedMotionParams *wm,
#if CONFIG_AOM_HIGHBITDEPTH
                         int use_hbd, int bd,
#endif  // CONFIG_AOM_HIGHBITDEPTH
                         uint8_t *ref, int width, int height, int stride,
                         uint8_t *dst, int p_col, int p_row, int p_width,
                         int p_height, int p_stride, int subsampling_x,
                         int subsampling_y, int x_scale, int y_scale) {
#if CONFIG_AOM_HIGHBITDEPTH
  if (use_hbd)
    return highbd_warp_erroradv(
        wm, ref, width, height, stride, dst, p_col, p_row, p_width, p_height,
        p_stride, subsampling_x, subsampling_y, x_scale, y_scale, bd);
  else
#endif  // CONFIG_AOM_HIGHBITDEPTH
    return warp_erroradv(wm, ref, width, height, stride, dst, p_col, p_row,
                         p_width, p_height, p_stride, subsampling_x,
                         subsampling_y, x_scale, y_scale);
}

void av1_warp_plane(WarpedMotionParams *wm,
#if CONFIG_AOM_HIGHBITDEPTH
                    int use_hbd, int bd,
#endif  // CONFIG_AOM_HIGHBITDEPTH
                    uint8_t *ref, int width, int height, int stride,
                    uint8_t *pred, int p_col, int p_row, int p_width,
                    int p_height, int p_stride, int subsampling_x,
                    int subsampling_y, int x_scale, int y_scale) {
#if CONFIG_AOM_HIGHBITDEPTH
  if (use_hbd)
    highbd_warp_plane(wm, ref, width, height, stride, pred, p_col, p_row,
                      p_width, p_height, p_stride, subsampling_x, subsampling_y,
                      x_scale, y_scale, bd);
  else
#endif  // CONFIG_AOM_HIGHBITDEPTH
    warp_plane(wm, ref, width, height, stride, pred, p_col, p_row, p_width,
               p_height, p_stride, subsampling_x, subsampling_y, x_scale,
               y_scale);
}

void av1_integerize_model(const double *model, TransformationType wmtype,
                          WarpedMotionParams *wm) {
  wm->wmtype = wmtype;
  switch (wmtype) {
    case HOMOGRAPHY:
      assert(fabs(model[8] - 1.0) < 1e-12);
      wm->wmmat[3].as_mv.row =
          (int16_t)lrint(model[6] * (1 << WARPEDMODEL_ROW3HOMO_PREC_BITS));
      wm->wmmat[3].as_mv.col =
          (int16_t)lrint(model[7] * (1 << WARPEDMODEL_ROW3HOMO_PREC_BITS));
    /* fallthrough intended */
    case AFFINE:
      wm->wmmat[2].as_mv.row =
          (int16_t)lrint(model[4] * (1 << WARPEDMODEL_PREC_BITS));
      wm->wmmat[2].as_mv.col =
          (int16_t)lrint(model[5] * (1 << WARPEDMODEL_PREC_BITS));
    /* fallthrough intended */
    case ROTZOOM:
      wm->wmmat[1].as_mv.row =
          (int16_t)lrint(model[2] * (1 << WARPEDMODEL_PREC_BITS));
      wm->wmmat[1].as_mv.col =
          (int16_t)lrint(model[3] * (1 << WARPEDMODEL_PREC_BITS));
    /* fallthrough intended */
    case TRANSLATION:
      wm->wmmat[0].as_mv.row =
          (int16_t)lrint(model[0] * (1 << WARPEDMODEL_PREC_BITS));
      wm->wmmat[0].as_mv.col =
          (int16_t)lrint(model[1] * (1 << WARPEDMODEL_PREC_BITS));
      break;
    default: assert(0 && "Invalid TransformationType");
  }
}

///////////////////////////////////////////////////////////////////////////////
// svdcmp
// Adopted from Numerical Recipes in C

static const double TINY_NEAR_ZERO = 1.0E-12;

static inline double SIGN(double a, double b) {
  return ((b) >= 0 ? fabs(a) : -fabs(a));
}

static inline double PYTHAG(double a, double b) {
  double absa, absb, ct;
  absa = fabs(a);
  absb = fabs(b);

  if (absa > absb) {
    ct = absb / absa;
    return absa * sqrt(1.0 + ct * ct);
  } else {
    ct = absa / absb;
    return (absb == 0) ? 0 : absb * sqrt(1.0 + ct * ct);
  }
}

void MultiplyMat(double *m1, double *m2, double *res, const int M1,
                 const int N1, const int N2) {
  int timesInner = N1;
  int timesRows = M1;
  int timesCols = N2;
  double sum;

  int row, col, inner;
  for (row = 0; row < timesRows; ++row) {
    for (col = 0; col < timesCols; ++col) {
      sum = 0;
      for (inner = 0; inner < timesInner; ++inner)
        sum += m1[row * N1 + inner] * m2[inner * N2 + col];
      *(res++) = sum;
    }
  }
}

static int svdcmp_(double **u, int m, int n, double w[], double **v) {
  const int max_its = 30;
  int flag, i, its, j, jj, k, l, nm;
  double anorm, c, f, g, h, s, scale, x, y, z;
  double *rv1 = (double *)malloc(sizeof(*rv1) * (n + 1));
  g = scale = anorm = 0.0;
  for (i = 0; i < n; i++) {
    l = i + 1;
    rv1[i] = scale * g;
    g = s = scale = 0.0;
    if (i < m) {
      for (k = i; k < m; k++) scale += fabs(u[k][i]);
      if (scale) {
        for (k = i; k < m; k++) {
          u[k][i] /= scale;
          s += u[k][i] * u[k][i];
        }
        f = u[i][i];
        g = -SIGN(sqrt(s), f);
        h = f * g - s;
        u[i][i] = f - g;
        for (j = l; j < n; j++) {
          for (s = 0.0, k = i; k < m; k++) s += u[k][i] * u[k][j];
          f = s / h;
          for (k = i; k < m; k++) u[k][j] += f * u[k][i];
        }
        for (k = i; k < m; k++) u[k][i] *= scale;
      }
    }
    w[i] = scale * g;
    g = s = scale = 0.0;
    if (i < m && i != n - 1) {
      for (k = l; k < n; k++) scale += fabs(u[i][k]);
      if (scale) {
        for (k = l; k < n; k++) {
          u[i][k] /= scale;
          s += u[i][k] * u[i][k];
        }
        f = u[i][l];
        g = -SIGN(sqrt(s), f);
        h = f * g - s;
        u[i][l] = f - g;
        for (k = l; k < n; k++) rv1[k] = u[i][k] / h;
        for (j = l; j < m; j++) {
          for (s = 0.0, k = l; k < n; k++) s += u[j][k] * u[i][k];
          for (k = l; k < n; k++) u[j][k] += s * rv1[k];
        }
        for (k = l; k < n; k++) u[i][k] *= scale;
      }
    }
    anorm = fmax(anorm, (fabs(w[i]) + fabs(rv1[i])));
  }

  for (i = n - 1; i >= 0; i--) {
    if (i < n - 1) {
      if (g) {
        for (j = l; j < n; j++) v[j][i] = (u[i][j] / u[i][l]) / g;
        for (j = l; j < n; j++) {
          for (s = 0.0, k = l; k < n; k++) s += u[i][k] * v[k][j];
          for (k = l; k < n; k++) v[k][j] += s * v[k][i];
        }
      }
      for (j = l; j < n; j++) v[i][j] = v[j][i] = 0.0;
    }
    v[i][i] = 1.0;
    g = rv1[i];
    l = i;
  }

  for (i = IMIN(m, n) - 1; i >= 0; i--) {
    l = i + 1;
    g = w[i];
    for (j = l; j < n; j++) u[i][j] = 0.0;
    if (g) {
      g = 1.0 / g;
      for (j = l; j < n; j++) {
        for (s = 0.0, k = l; k < m; k++) s += u[k][i] * u[k][j];
        f = (s / u[i][i]) * g;
        for (k = i; k < m; k++) u[k][j] += f * u[k][i];
      }
      for (j = i; j < m; j++) u[j][i] *= g;
    } else {
      for (j = i; j < m; j++) u[j][i] = 0.0;
    }
    ++u[i][i];
  }
  for (k = n - 1; k >= 0; k--) {
    for (its = 0; its < max_its; its++) {
      flag = 1;
      for (l = k; l >= 0; l--) {
        nm = l - 1;
        if ((double)(fabs(rv1[l]) + anorm) == anorm || nm < 0) {
          flag = 0;
          break;
        }
        if ((double)(fabs(w[nm]) + anorm) == anorm) break;
      }
      if (flag) {
        c = 0.0;
        s = 1.0;
        for (i = l; i <= k; i++) {
          f = s * rv1[i];
          rv1[i] = c * rv1[i];
          if ((double)(fabs(f) + anorm) == anorm) break;
          g = w[i];
          h = PYTHAG(f, g);
          w[i] = h;
          h = 1.0 / h;
          c = g * h;
          s = -f * h;
          for (j = 0; j < m; j++) {
            y = u[j][nm];
            z = u[j][i];
            u[j][nm] = y * c + z * s;
            u[j][i] = z * c - y * s;
          }
        }
      }
      z = w[k];
      if (l == k) {
        if (z < 0.0) {
          w[k] = -z;
          for (j = 0; j < n; j++) v[j][k] = -v[j][k];
        }
        break;
      }
      if (its == max_its - 1) {
        return 1;
      }
      assert(k > 0);
      x = w[l];
      nm = k - 1;
      y = w[nm];
      g = rv1[nm];
      h = rv1[k];
      f = ((y - z) * (y + z) + (g - h) * (g + h)) / (2.0 * h * y);
      g = PYTHAG(f, 1.0);
      f = ((x - z) * (x + z) + h * ((y / (f + SIGN(g, f))) - h)) / x;
      c = s = 1.0;
      for (j = l; j <= nm; j++) {
        i = j + 1;
        g = rv1[i];
        y = w[i];
        h = s * g;
        g = c * g;
        z = PYTHAG(f, h);
        rv1[j] = z;
        c = f / z;
        s = h / z;
        f = x * c + g * s;
        g = g * c - x * s;
        h = y * s;
        y *= c;
        for (jj = 0; jj < n; jj++) {
          x = v[jj][j];
          z = v[jj][i];
          v[jj][j] = x * c + z * s;
          v[jj][i] = z * c - x * s;
        }
        z = PYTHAG(f, h);
        w[j] = z;
        if (z) {
          z = 1.0 / z;
          c = f * z;
          s = h * z;
        }
        f = c * g + s * y;
        x = c * y - s * g;
        for (jj = 0; jj < m; jj++) {
          y = u[jj][j];
          z = u[jj][i];
          u[jj][j] = y * c + z * s;
          u[jj][i] = z * c - y * s;
        }
      }
      rv1[l] = 0.0;
      rv1[k] = f;
      w[k] = x;
    }
  }
  free(rv1);
  return 0;
}

static int SVD(double *U, double *W, double *V, double *matx, int M, int N) {
  // Assumes allocation for U is MxN
  double **nrU, **nrV;
  int problem, i;

  nrU = (double **)malloc((M) * sizeof(*nrU));
  nrV = (double **)malloc((N) * sizeof(*nrV));
  problem = !(nrU && nrV);
  if (!problem) {
    problem = 0;
    for (i = 0; i < M; i++) {
      nrU[i] = &U[i * N];
    }
    for (i = 0; i < N; i++) {
      nrV[i] = &V[i * N];
    }
  }
  if (problem) {
    return 1;
  }

  /* copy from given matx into nrU */
  for (i = 0; i < M; i++) {
    memcpy(&(nrU[i][0]), matx + N * i, N * sizeof(*matx));
  }

  /* HERE IT IS: do SVD */
  if (svdcmp_(nrU, M, N, W, nrV)) {
    return 1;
  }

  /* free Numerical Recipes arrays */
  free(nrU);
  free(nrV);

  return 0;
}

int PseudoInverse(double *inv, double *matx, const int M, const int N) {
  double *U, *W, *V, ans;
  int i, j, k;
  U = (double *)malloc(M * N * sizeof(*matx));
  W = (double *)malloc(N * sizeof(*matx));
  V = (double *)malloc(N * N * sizeof(*matx));

  if (!(U && W && V)) {
    return 1;
  }
  if (SVD(U, W, V, matx, M, N)) {
    return 1;
  }
  for (i = 0; i < N; i++) {
    if (fabs(W[i]) < TINY_NEAR_ZERO) {
      return 1;
    }
  }

  for (i = 0; i < N; i++) {
    for (j = 0; j < M; j++) {
      ans = 0;
      for (k = 0; k < N; k++) {
        ans += V[k + N * i] * U[k + N * j] / W[k];
      }
      inv[j + M * i] = ans;
    }
  }
  free(U);
  free(W);
  free(V);
  return 0;
}

static void normalizeHomography(double *pts, int n, double *T) {
  // Assume the points are 2d coordinates with scale = 1
  double *p = pts;
  double mean[2] = {0, 0};
  double msqe = 0;
  double scale;
  int i;
  for (i = 0; i < n; ++i, p += 2) {
    mean[0] += p[0];
    mean[1] += p[1];
  }
  mean[0] /= n;
  mean[1] /= n;
  for (p = pts, i = 0; i < n; ++i, p += 2) {
    p[0] -= mean[0];
    p[1] -= mean[1];
    msqe += sqrt(p[0] * p[0] + p[1] * p[1]);
  }
  msqe /= n;
  scale = sqrt(2) / msqe;
  T[0] = scale;
  T[1] = 0;
  T[2] = -scale * mean[0];
  T[3] = 0;
  T[4] = scale;
  T[5] = -scale * mean[1];
  T[6] = 0;
  T[7] = 0;
  T[8] = 1;
  for (p = pts, i = 0; i < n; ++i, p += 2) {
    p[0] *= scale;
    p[1] *= scale;
  }
}

static void invnormalize_mat(double *T, double *iT) {
  double is = 1.0 / T[0];
  double m0 = -T[2] * is;
  double m1 = -T[5] * is;
  iT[0] = is;
  iT[1] = 0;
  iT[2] = m0;
  iT[3] = 0;
  iT[4] = is;
  iT[5] = m1;
  iT[6] = 0;
  iT[7] = 0;
  iT[8] = 1;
}

static void denormalizeHomography(double *H, double *T1, double *T2) {
  double iT2[9];
  double H2[9];
  invnormalize_mat(T2, iT2);
  MultiplyMat(H, T1, H2, 3, 3, 3);
  MultiplyMat(iT2, H2, H, 3, 3, 3);
}

static void denormalizeAffine(double *H, double *T1, double *T2) {
  double Ha[MAX_PARAMDIM];
  Ha[0] = H[0];
  Ha[1] = H[1];
  Ha[2] = H[4];
  Ha[3] = H[2];
  Ha[4] = H[3];
  Ha[5] = H[5];
  Ha[6] = Ha[7] = 0;
  Ha[8] = 1;
  denormalizeHomography(Ha, T1, T2);
  H[0] = Ha[5];
  H[1] = Ha[2];
  H[2] = Ha[1];
  H[3] = Ha[0];
  H[4] = Ha[4];
  H[5] = Ha[3];
}

static void denormalizeRotZoom(double *H, double *T1, double *T2) {
  double Ha[MAX_PARAMDIM];
  Ha[0] = H[0];
  Ha[1] = H[1];
  Ha[2] = H[2];
  Ha[3] = -H[1];
  Ha[4] = H[0];
  Ha[5] = H[3];
  Ha[6] = Ha[7] = 0;
  Ha[8] = 1;
  denormalizeHomography(Ha, T1, T2);
  H[0] = Ha[5];
  H[1] = Ha[2];
  H[2] = Ha[1];
  H[3] = Ha[0];
}

static void denormalizeTranslation(double *H, double *T1, double *T2) {
  double Ha[MAX_PARAMDIM];
  Ha[0] = 1;
  Ha[1] = 0;
  Ha[2] = H[0];
  Ha[3] = 0;
  Ha[4] = 1;
  Ha[5] = H[1];
  Ha[6] = Ha[7] = 0;
  Ha[8] = 1;
  denormalizeHomography(Ha, T1, T2);
  H[0] = Ha[5];
  H[1] = Ha[2];
}

int findTranslation(const int np, double *pts1, double *pts2, double *mat) {
  int i;
  double sx, sy, dx, dy;
  double sumx, sumy;

  double T1[9], T2[9];
  normalizeHomography(pts1, np, T1);
  normalizeHomography(pts2, np, T2);

  sumx = 0;
  sumy = 0;
  for (i = 0; i < np; ++i) {
    dx = *(pts2++);
    dy = *(pts2++);
    sx = *(pts1++);
    sy = *(pts1++);

    sumx += dx - sx;
    sumy += dy - sy;
  }
  mat[0] = sumx / np;
  mat[1] = sumy / np;
  denormalizeTranslation(mat, T1, T2);
  return 0;
}

int findRotZoom(const int np, double *pts1, double *pts2, double *mat) {
  const int np2 = np * 2;
  double *a = (double *)malloc(sizeof(*a) * np2 * 9);
  double *b = a + np2 * 4;
  double *temp = b + np2;
  int i;
  double sx, sy, dx, dy;

  double T1[9], T2[9];
  normalizeHomography(pts1, np, T1);
  normalizeHomography(pts2, np, T2);

  for (i = 0; i < np; ++i) {
    dx = *(pts2++);
    dy = *(pts2++);
    sx = *(pts1++);
    sy = *(pts1++);

    a[i * 2 * 4 + 0] = sx;
    a[i * 2 * 4 + 1] = sy;
    a[i * 2 * 4 + 2] = 1;
    a[i * 2 * 4 + 3] = 0;
    a[(i * 2 + 1) * 4 + 0] = sy;
    a[(i * 2 + 1) * 4 + 1] = -sx;
    a[(i * 2 + 1) * 4 + 2] = 0;
    a[(i * 2 + 1) * 4 + 3] = 1;

    b[2 * i] = dx;
    b[2 * i + 1] = dy;
  }
  if (PseudoInverse(temp, a, np2, 4)) {
    free(a);
    return 1;
  }
  MultiplyMat(temp, b, mat, 4, np2, 1);
  denormalizeRotZoom(mat, T1, T2);
  free(a);
  return 0;
}

int findAffine(const int np, double *pts1, double *pts2, double *mat) {
  const int np2 = np * 2;
  double *a = (double *)malloc(sizeof(*a) * np2 * 13);
  double *b = a + np2 * 6;
  double *temp = b + np2;
  int i;
  double sx, sy, dx, dy;

  double T1[9], T2[9];
  normalizeHomography(pts1, np, T1);
  normalizeHomography(pts2, np, T2);

  for (i = 0; i < np; ++i) {
    dx = *(pts2++);
    dy = *(pts2++);
    sx = *(pts1++);
    sy = *(pts1++);

    a[i * 2 * 6 + 0] = sx;
    a[i * 2 * 6 + 1] = sy;
    a[i * 2 * 6 + 2] = 0;
    a[i * 2 * 6 + 3] = 0;
    a[i * 2 * 6 + 4] = 1;
    a[i * 2 * 6 + 5] = 0;
    a[(i * 2 + 1) * 6 + 0] = 0;
    a[(i * 2 + 1) * 6 + 1] = 0;
    a[(i * 2 + 1) * 6 + 2] = sx;
    a[(i * 2 + 1) * 6 + 3] = sy;
    a[(i * 2 + 1) * 6 + 4] = 0;
    a[(i * 2 + 1) * 6 + 5] = 1;

    b[2 * i] = dx;
    b[2 * i + 1] = dy;
  }
  if (PseudoInverse(temp, a, np2, 6)) {
    free(a);
    return 1;
  }
  MultiplyMat(temp, b, mat, 6, np2, 1);
  denormalizeAffine(mat, T1, T2);
  free(a);
  return 0;
}

int findHomography(const int np, double *pts1, double *pts2, double *mat) {
  // Implemented from Peter Kovesi's normalized implementation
  const int np3 = np * 3;
  double *a = (double *)malloc(sizeof(*a) * np3 * 18);
  double *U = a + np3 * 9;
  double S[9], V[9 * 9];
  int i, mini;
  double sx, sy, dx, dy;
  double T1[9], T2[9];

  normalizeHomography(pts1, np, T1);
  normalizeHomography(pts2, np, T2);

  for (i = 0; i < np; ++i) {
    dx = *(pts2++);
    dy = *(pts2++);
    sx = *(pts1++);
    sy = *(pts1++);

    a[i * 3 * 9 + 0] = a[i * 3 * 9 + 1] = a[i * 3 * 9 + 2] = 0;
    a[i * 3 * 9 + 3] = -sx;
    a[i * 3 * 9 + 4] = -sy;
    a[i * 3 * 9 + 5] = -1;
    a[i * 3 * 9 + 6] = dy * sx;
    a[i * 3 * 9 + 7] = dy * sy;
    a[i * 3 * 9 + 8] = dy;

    a[(i * 3 + 1) * 9 + 0] = sx;
    a[(i * 3 + 1) * 9 + 1] = sy;
    a[(i * 3 + 1) * 9 + 2] = 1;
    a[(i * 3 + 1) * 9 + 3] = a[(i * 3 + 1) * 9 + 4] = a[(i * 3 + 1) * 9 + 5] =
        0;
    a[(i * 3 + 1) * 9 + 6] = -dx * sx;
    a[(i * 3 + 1) * 9 + 7] = -dx * sy;
    a[(i * 3 + 1) * 9 + 8] = -dx;

    a[(i * 3 + 2) * 9 + 0] = -dy * sx;
    a[(i * 3 + 2) * 9 + 1] = -dy * sy;
    a[(i * 3 + 2) * 9 + 2] = -dy;
    a[(i * 3 + 2) * 9 + 3] = dx * sx;
    a[(i * 3 + 2) * 9 + 4] = dx * sy;
    a[(i * 3 + 2) * 9 + 5] = dx;
    a[(i * 3 + 2) * 9 + 6] = a[(i * 3 + 2) * 9 + 7] = a[(i * 3 + 2) * 9 + 8] =
        0;
  }

  if (SVD(U, S, V, a, np3, 9)) {
    free(a);
    return 1;
  } else {
    double minS = 1e12;
    mini = -1;
    for (i = 0; i < 9; ++i) {
      if (S[i] < minS) {
        minS = S[i];
        mini = i;
      }
    }
  }

  for (i = 0; i < 9; i++) mat[i] = V[i * 9 + mini];
  denormalizeHomography(mat, T1, T2);
  free(a);
  if (mat[8] == 0.0) {
    return 1;
  }
  return 0;
}

int findHomographyScale1(const int np, double *pts1, double *pts2,
                         double *mat) {
  // This implementation assumes h33 = 1, but does not seem to give good results
  const int np2 = np * 2;
  double *a = (double *)malloc(sizeof(*a) * np2 * 17);
  double *b = a + np2 * 8;
  double *temp = b + np2;
  int i, j;
  double sx, sy, dx, dy;
  double T1[9], T2[9];

  normalizeHomography(pts1, np, T1);
  normalizeHomography(pts2, np, T2);

  for (i = 0, j = np; i < np; ++i, ++j) {
    dx = *(pts2++);
    dy = *(pts2++);
    sx = *(pts1++);
    sy = *(pts1++);
    a[i * 8 + 0] = a[j * 8 + 3] = sx;
    a[i * 8 + 1] = a[j * 8 + 4] = sy;
    a[i * 8 + 2] = a[j * 8 + 5] = 1;
    a[i * 8 + 3] = a[i * 8 + 4] = a[i * 8 + 5] = a[j * 8 + 0] = a[j * 8 + 1] =
        a[j * 8 + 2] = 0;
    a[i * 8 + 6] = -dx * sx;
    a[i * 8 + 7] = -dx * sy;
    a[j * 8 + 6] = -dy * sx;
    a[j * 8 + 7] = -dy * sy;
    b[i] = dx;
    b[j] = dy;
  }

  if (PseudoInverse(temp, a, np2, 8)) {
    free(a);
    return 1;
  }
  MultiplyMat(temp, b, &*mat, 8, np2, 1);
  mat[8] = 1;

  denormalizeHomography(mat, T1, T2);
  free(a);
  return 0;
}

int findProjection(const int np, double *pts1, double *pts2,
                   WarpedMotionParams *wm_params) {
  double H[9];
  int result = 1;

  switch (wm_params->wmtype) {
    case AFFINE:
      result = findAffine(np, pts1, pts2, H);
      break;
    case ROTZOOM:
      result = findRotZoom(np, pts1, pts2, H);
      break;
    case HOMOGRAPHY:
      result = findHomography(np, pts1, pts2, H);
      break;
    default:
      assert("Invalid warped motion type!\n");
      return 1;
  }
  if (result == 0)
    av1_integerize_model(H, wm_params->wmtype, wm_params);

  return result;
}
