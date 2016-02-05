/*
 *  Copyright (c) 2010 The WebM project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include <assert.h>

#include "vp10/common/filter.h"

DECLARE_ALIGNED(256, static const InterpKernel,
                bilinear_filters[SUBPEL_SHIFTS]) = {
  { 0, 0, 0, 128,   0, 0, 0, 0 },
  { 0, 0, 0, 120,   8, 0, 0, 0 },
  { 0, 0, 0, 112,  16, 0, 0, 0 },
  { 0, 0, 0, 104,  24, 0, 0, 0 },
  { 0, 0, 0,  96,  32, 0, 0, 0 },
  { 0, 0, 0,  88,  40, 0, 0, 0 },
  { 0, 0, 0,  80,  48, 0, 0, 0 },
  { 0, 0, 0,  72,  56, 0, 0, 0 },
  { 0, 0, 0,  64,  64, 0, 0, 0 },
  { 0, 0, 0,  56,  72, 0, 0, 0 },
  { 0, 0, 0,  48,  80, 0, 0, 0 },
  { 0, 0, 0,  40,  88, 0, 0, 0 },
  { 0, 0, 0,  32,  96, 0, 0, 0 },
  { 0, 0, 0,  24, 104, 0, 0, 0 },
  { 0, 0, 0,  16, 112, 0, 0, 0 },
  { 0, 0, 0,   8, 120, 0, 0, 0 }
};

DECLARE_ALIGNED(256, static const InterpKernel,
                sub_pel_filters_8[SUBPEL_SHIFTS]) = {
#if CONFIG_EXT_INTERP
  // intfilt 0.575
  {0,   0,   0, 128,   0,   0,   0, 0},
  {0,   1,  -5, 126,   8,  -3,   1, 0},
  {-1,   3, -10, 123,  18,  -6,   2, -1},
  {-1,   4, -14, 118,  27,  -9,   3, 0},
  {-1,   5, -16, 112,  37, -12,   4, -1},
  {-1,   5, -18, 105,  48, -14,   4, -1},
  {-1,   6, -19,  97,  58, -17,   5, -1},
  {-1,   6, -20,  88,  68, -18,   6, -1},
  {-1,   6, -19,  78,  78, -19,   6, -1},
  {-1,   6, -18,  68,  88, -20,   6, -1},
  {-1,   5, -17,  58,  97, -19,   6, -1},
  {-1,   4, -14,  48, 105, -18,   5, -1},
  {-1,   4, -12,  37, 112, -16,   5, -1},
  {0,   3,  -9,  27, 118, -14,   4, -1},
  {-1,   2,  -6,  18, 123, -10,   3, -1},
  {0,   1,  -3,   8, 126,  -5,   1, 0},
#else
  // Lagrangian interpolation filter
  { 0,   0,   0, 128,   0,   0,   0,  0},
  { 0,   1,  -5, 126,   8,  -3,   1,  0},
  { -1,   3, -10, 122,  18,  -6,   2,  0},
  { -1,   4, -13, 118,  27,  -9,   3, -1},
  { -1,   4, -16, 112,  37, -11,   4, -1},
  { -1,   5, -18, 105,  48, -14,   4, -1},
  { -1,   5, -19,  97,  58, -16,   5, -1},
  { -1,   6, -19,  88,  68, -18,   5, -1},
  { -1,   6, -19,  78,  78, -19,   6, -1},
  { -1,   5, -18,  68,  88, -19,   6, -1},
  { -1,   5, -16,  58,  97, -19,   5, -1},
  { -1,   4, -14,  48, 105, -18,   5, -1},
  { -1,   4, -11,  37, 112, -16,   4, -1},
  { -1,   3,  -9,  27, 118, -13,   4, -1},
  { 0,   2,  -6,  18, 122, -10,   3, -1},
  { 0,   1,  -3,   8, 126,  -5,   1,  0}
#endif  // CONFIG_EXT_INTERP
};

DECLARE_ALIGNED(256, static const InterpKernel,
                sub_pel_filters_8sharp[SUBPEL_SHIFTS]) = {
#if CONFIG_EXT_INTERP
  // intfilt 0.8
  {0,   0,   0, 128,   0,   0,   0, 0},
  {-1,   2,  -6, 127,   9,  -4,   2, -1},
  {-2,   5, -12, 124,  18,  -7,   4, -2},
  {-2,   7, -16, 119,  28, -11,   5, -2},
  {-3,   8, -19, 114,  38, -14,   7, -3},
  {-3,   9, -22, 107,  49, -17,   8, -3},
  {-4,  10, -23,  99,  60, -20,  10, -4},
  {-4,  11, -23,  90,  70, -22,  10, -4},
  {-4,  11, -23,  80,  80, -23,  11, -4},
  {-4,  10, -22,  70,  90, -23,  11, -4},
  {-4,  10, -20,  60,  99, -23,  10, -4},
  {-3,   8, -17,  49, 107, -22,   9, -3},
  {-3,   7, -14,  38, 114, -19,   8, -3},
  {-2,   5, -11,  28, 119, -16,   7, -2},
  {-2,   4,  -7,  18, 124, -12,   5, -2},
  {-1,   2,  -4,   9, 127,  -6,   2, -1},
#else
  // DCT based filter
  {0,   0,   0, 128,   0,   0,   0, 0},
  {-1,   3,  -7, 127,   8,  -3,   1, 0},
  {-2,   5, -13, 125,  17,  -6,   3, -1},
  {-3,   7, -17, 121,  27, -10,   5, -2},
  {-4,   9, -20, 115,  37, -13,   6, -2},
  {-4,  10, -23, 108,  48, -16,   8, -3},
  {-4,  10, -24, 100,  59, -19,   9, -3},
  {-4,  11, -24,  90,  70, -21,  10, -4},
  {-4,  11, -23,  80,  80, -23,  11, -4},
  {-4,  10, -21,  70,  90, -24,  11, -4},
  {-3,   9, -19,  59, 100, -24,  10, -4},
  {-3,   8, -16,  48, 108, -23,  10, -4},
  {-2,   6, -13,  37, 115, -20,   9, -4},
  {-2,   5, -10,  27, 121, -17,   7, -3},
  {-1,   3,  -6,  17, 125, -13,   5, -2},
  {0,   1,  -3,   8, 127,  -7,   3, -1}
#endif  // CONFIG_EXT_INTERP
};

#if CONFIG_EXT_INTERP && SWITCHABLE_FILTERS == 4

DECLARE_ALIGNED(256, static const InterpKernel,
                sub_pel_filters_8smooth2[SUBPEL_SHIFTS]) = {
// freqmultiplier = 0.35
  {0,  0,  0, 128,  0,  0,  0,  0},
  {-1,  8, 31, 47, 34, 10,  0, -1},
  {-1,  7, 29, 46, 36, 12,  0, -1},
  {-1,  6, 28, 46, 37, 13,  0, -1},
  {-1,  5, 26, 46, 38, 14,  1, -1},
  {-1,  4, 25, 45, 39, 16,  1, -1},
  {-1,  4, 23, 44, 41, 17,  1, -1},
  {-1,  3, 21, 44, 42, 18,  2, -1},
  {-1,  2, 20, 43, 43, 20,  2, -1},
  {-1,  2, 18, 42, 44, 21,  3, -1},
  {-1,  1, 17, 41, 44, 23,  4, -1},
  {-1,  1, 16, 39, 45, 25,  4, -1},
  {-1,  1, 14, 38, 46, 26,  5, -1},
  {-1,  0, 13, 37, 46, 28,  6, -1},
  {-1,  0, 12, 36, 46, 29,  7, -1},
  {-1,  0, 10, 34, 47, 31,  8, -1},
};

DECLARE_ALIGNED(256, static const InterpKernel,
                sub_pel_filters_8smooth[SUBPEL_SHIFTS]) = {
// freqmultiplier = 0.75
  {0,  0,  0, 128,  0,  0,  0,  0},
  {2, -10,  19,  95,  31, -11,   2, 0},
  {2,  -9,  14,  94,  37, -12,   2, 0},
  {2,  -8,   9,  92,  43, -12,   1, 1},
  {2,  -7,   5,  90,  49, -12,   1, 0},
  {2,  -5,   1,  86,  55, -12,   0, 1},
  {1,  -4,  -2,  82,  61, -11,   0, 1},
  {1, -3, -5, 77, 67, -9, -1, 1},
  {1, -2, -7, 72, 72, -7, -2, 1},
  {1, -1, -9, 67, 77, -5, -3, 1},
  {1,   0, -11,  61,  82,  -2,  -4, 1},
  {1,   0, -12,  55,  86,   1,  -5, 2},
  {0,   1, -12,  49,  90,   5,  -7, 2},
  {1,   1, -12,  43,  92,   9,  -8, 2},
  {0,   2, -12,  37,  94,  14,  -9, 2},
  {0,   2, -11,  31,  95,  19, -10, 2},
};

#else

DECLARE_ALIGNED(256, static const InterpKernel,
                sub_pel_filters_8smooth[SUBPEL_SHIFTS]) = {
// freqmultiplier = 0.5
  { 0,  0,  0, 128,  0,  0,  0,  0},
  {-3, -1, 32,  64, 38,  1, -3,  0},
  {-2, -2, 29,  63, 41,  2, -3,  0},
  {-2, -2, 26,  63, 43,  4, -4,  0},
  {-2, -3, 24,  62, 46,  5, -4,  0},
  {-2, -3, 21,  60, 49,  7, -4,  0},
  {-1, -4, 18,  59, 51,  9, -4,  0},
  {-1, -4, 16,  57, 53, 12, -4, -1},
  {-1, -4, 14,  55, 55, 14, -4, -1},
  {-1, -4, 12,  53, 57, 16, -4, -1},
  { 0, -4,  9,  51, 59, 18, -4, -1},
  { 0, -4,  7,  49, 60, 21, -3, -2},
  { 0, -4,  5,  46, 62, 24, -3, -2},
  { 0, -4,  4,  43, 63, 26, -2, -2},
  { 0, -3,  2,  41, 63, 29, -2, -2},
  { 0, -3,  1,  38, 64, 32, -1, -3}
};

#endif  // CONFIG_EXT_INTERP

const InterpKernel *vp10_filter_kernels[SWITCHABLE_FILTERS + 1] = {
  sub_pel_filters_8,
  sub_pel_filters_8smooth,
  sub_pel_filters_8sharp,
#if CONFIG_EXT_INTERP && SWITCHABLE_FILTERS == 4
  sub_pel_filters_8smooth2,
#endif
  bilinear_filters
};

static const InterpFilterSet
vp10_interp_filter_set_list[SWITCHABLE_FILTERS + 1] = {
  {(int16_t*)sub_pel_filters_8, SUBPEL_TAPS, SUBPEL_SHIFTS},
  {(int16_t*)sub_pel_filters_8smooth, SUBPEL_TAPS, SUBPEL_SHIFTS},
  {(int16_t*)sub_pel_filters_8sharp, SUBPEL_TAPS, SUBPEL_SHIFTS},
#if CONFIG_EXT_INTERP && SWITCHABLE_FILTERS == 4
  {(int16_t*)sub_pel_filters_8smooth2, SUBPEL_TAPS, SUBPEL_SHIFTS},
#endif
  {(int16_t*)bilinear_filters, SUBPEL_TAPS, SUBPEL_SHIFTS}
};

InterpFilterSet vp10_get_interp_filter_set(const INTERP_FILTER interp_filter) {
  return vp10_interp_filter_set_list[interp_filter];
}
