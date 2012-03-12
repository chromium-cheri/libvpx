/*
 *  Copyright (c) 2010 The WebM project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */


#include <stdlib.h>
#include "filter.h"
#include "vpx_ports/mem.h"

DECLARE_ALIGNED(16, const short, vp8_bilinear_filters[SUBPEL_SHIFTS][2]) =
{
#if SUBPEL_SHIFTS==16
    { 128,   0 },
    { 120,   8 },
    { 112,  16 },
    { 104,  24 },
    {  96,  32 },
    {  88,  40 },
    {  80,  48 },
    {  72,  56 },
    {  64,  64 },
    {  56,  72 },
    {  48,  80 },
    {  40,  88 },
    {  32,  96 },
    {  24, 104 },
    {  16, 112 },
    {   8, 120 }
#else
    { 128,   0 },
    { 112,  16 },
    {  96,  32 },
    {  80,  48 },
    {  64,  64 },
    {  48,  80 },
    {  32,  96 },
    {  16, 112 }
#endif  /* SUBPEL_SHIFTS==16 */
};

#if CONFIG_ENHANCED_INTERP
#define FILTER_ALPHA 60
DECLARE_ALIGNED(16, const short, vp8_sub_pel_filters[SUBPEL_SHIFTS][2*INTERP_EXTEND]) =
{
    /* Generated using MATLAB:
     * alpha = 0.6;
     * b=intfilt(8,4,alpha);
     * bi=round(128*b);
     * ba=flipud(reshape([bi 0], 8, 8));
     * disp(num2str(ba, '%d,'))
     */
#if SUBPEL_SHIFTS==16
#if FILTER_ALPHA == 70
    /* alpha = 0.70 */
    { 0,   0,   0, 128,   0,   0,   0,  0},
    { 0,   2,  -6, 126,   8,  -3,   1,  0},
    {-1,   4, -11, 123,  18,  -7,   3, -1},
    {-1,   5, -15, 119,  27, -10,   4, -1},
    {-2,   6, -18, 113,  38, -13,   5, -1},
    {-2,   7, -20, 106,  49, -16,   6, -2},
    {-2,   8, -22,  98,  59, -18,   7, -2},
    {-2,   8, -22,  89,  69, -20,   8, -2},
    {-2,   8, -21,  79,  79, -21,   8, -2},
    {-2,   8, -20,  69,  89, -22,   8, -2},
    {-2,   7, -18,  59,  98, -22,   8, -2},
    {-2,   6, -16,  49, 106, -20,   7, -2},
    {-1,   5, -13,  38, 113, -18,   6, -2},
    {-1,   4, -10,  27, 119, -15,   5, -1},
    {-1,   3,  -7,  18, 123, -11,   4, -1},
    { 0,   1,  -3,   8, 126,  -6,   2,  0}
#elif FILTER_ALPHA == 65
    /* alpha = 0.65 */
    { 0,   0,   0, 128,   0,   0,   0,  0},
    { 0,   2,  -6, 126,   8,  -3,   1,  0},
    {-1,   3, -10, 123,  18,  -6,   2, -1},
    {-1,   5, -14, 118,  27, -10,   4, -1},
    {-1,   5, -17, 112,  38, -13,   5, -1},
    {-2,   6, -19, 106,  48, -15,   5, -1},
    {-2,   7, -21,  98,  59, -17,   6, -2},
    {-2,   7, -21,  89,  69, -19,   7, -2},
    {-2,   7, -20,  79,  79, -20,   7, -2},
    {-2,   7, -19,  69,  89, -21,   7, -2},
    {-2,   6, -17,  59,  98, -21,   7, -2},
    {-1,   5, -15,  48, 106, -19,   6, -2},
    {-1,   5, -13,  38, 112, -17,   5, -1},
    {-1,   4, -10,  27, 118, -14,   5, -1},
    {-1,   2,  -6,  18, 123, -10,   3, -1},
    { 0,   1,  -3,   8, 126,  -6,   2,  0}
#elif FILTER_ALPHA == 60
    /* alpha = 0.60 */
    { 0,   0,   0, 128,   0,   0,   0,  0},
    { 0,   2,  -6, 126,   8,  -3,   1,  0},
    {-1,   3, -10, 123,  18,  -6,   2, -1},
    {-1,   4, -14, 118,  28,  -9,   3, -1},
    {-1,   5, -17, 112,  38, -12,   4, -1},
    {-1,   6, -19, 105,  48, -15,   5, -1},
    {-1,   6, -20,  97,  58, -17,   6, -1},
    {-1,   6, -20,  88,  69, -19,   6, -1},
    {-1,   6, -20,  79,  79, -20,   6, -1},
    {-1,   6, -19,  69,  88, -20,   6, -1},
    {-1,   6, -17,  58,  97, -20,   6, -1},
    {-1,   5, -15,  48, 105, -19,   6, -1},
    {-1,   4, -12,  38, 112, -17,   5, -1},
    {-1,   3,  -9,  28, 118, -14,   4, -1},
    {-1,   2,  -6,  18, 123, -10,   3, -1},
    { 0,   1,  -3,   8, 126,  -6,   2,  0}
#elif FILTER_ALPHA == 55
    /* alpha = 0.55 */
    { 0,   0,   0, 128,   0,   0,   0,  0},
    { 0,   1,  -5, 126,   8,  -3,   1,  0},
    {-1,   2, -10, 123,  18,  -6,   2,  0},
    {-1,   4, -13, 118,  27,  -9,   3, -1},
    {-1,   5, -16, 112,  37, -12,   4, -1},
    {-1,   5, -18, 105,  48, -14,   4, -1},
    {-1,   5, -19,  97,  58, -16,   5, -1},
    {-1,   6, -19,  88,  68, -18,   5, -1},
    {-1,   6, -19,  78,  78, -19,   6, -1},
    {-1,   5, -18,  68,  88, -19,   6, -1},
    {-1,   5, -16,  58,  97, -19,   5, -1},
    {-1,   4, -14,  48, 105, -18,   5, -1},
    {-1,   4, -12,  37, 112, -16,   5, -1},
    {-1,   3,  -9,  27, 118, -13,   4, -1},
    { 0,   2,  -6,  18, 123, -10,   2, -1},
    { 0,   1,  -3,   8, 126,  -5,   1,  0}
#elif FILTER_ALPHA == 50
    /* alpha = 0.50 */
    { 0,   0,   0, 128,   0,   0,   0,  0},
    { 0,   1,  -5, 126,   8,  -3,   1,  0},
    { 0,   2, -10, 122,  18,  -6,   2,  0},
    {-1,   3, -13, 118,  27,  -9,   3,  0},
    {-1,   4, -16, 112,  37, -11,   3,  0},
    {-1,   5, -17, 104,  48, -14,   4, -1},
    {-1,   5, -18,  96,  58, -16,   5, -1},
    {-1,   5, -19,  88,  68, -17,   5, -1},
    {-1,   5, -18,  78,  78, -18,   5, -1},
    {-1,   5, -17,  68,  88, -19,   5, -1},
    {-1,   5, -16,  58,  96, -18,   5, -1},
    {-1,   4, -14,  48, 104, -17,   5, -1},
    { 0,   3, -11,  37, 112, -16,   4, -1},
    { 0,   3,  -9,  27, 118, -13,   3, -1},
    { 0,   2,  -6,  18, 122, -10,   2,  0},
    { 0,   1,  -3,   8, 126,  -5,   1,  0}
#elif FILTER_ALPHA == 0
    /* Lagrangian interpolation filter */
    { 0,   0,   0, 128,   0,   0,   0,   0},
    { 0,   1,  -5, 126,   8,  -3,   1,   0},
    {-1,   3, -10, 122,  18,  -6,   2,   0},
    {-1,   4, -13, 118,  27,  -9,   3,  -1},
    {-1,   4, -16, 112,  37, -11,   4,  -1},
    {-1,   5, -18, 105,  48, -14,   4,  -1},
    {-1,   5, -19,  97,  58, -16,   5,  -1},
    {-1,   6, -19,  88,  68, -18,   5,  -1},
    {-1,   6, -19,  78,  78, -19,   6,  -1},
    {-1,   5, -18,  68,  88, -19,   6,  -1},
    {-1,   5, -16,  58,  97, -19,   5,  -1},
    {-1,   4, -14,  48, 105, -18,   5,  -1},
    {-1,   4, -11,  37, 112, -16,   4,  -1},
    {-1,   3,  -9,  27, 118, -13,   4,  -1},
    { 0,   2,  -6,  18, 122, -10,   3,  -1},
    { 0,   1,  -3,   8, 126,  -5,   1,   0}
#endif  /* FILTER_ALPHA */
#else   /* SUBPEL_SHIFTS==16 */
#if FILTER_ALPHA == 70
    /* alpha = 0.70 */
    { 0,   0,   0, 128,   0,   0,   0,  0},
    {-1,   4, -11, 123,  18,  -7,   3, -1},
    {-2,   6, -18, 113,  38, -13,   5, -1},
    {-2,   8, -22,  98,  59, -18,   7, -2},
    {-2,   8, -21,  79,  79, -21,   8, -2},
    {-2,   7, -18,  59,  98, -22,   8, -2},
    {-1,   5, -13,  38, 113, -18,   6, -2},
    {-1,   3,  -7,  18, 123, -11,   4, -1}
#elif FILTER_ALPHA == 65
    /* alpha = 0.65 */
    { 0,   0,   0, 128,   0,   0,   0, 0},
    {-1,   3, -10, 123,  18,  -6,   2, -1},
    {-1,   5, -17, 112,  38, -13,   5, -1},
    {-2,   7, -21,  98,  59, -17,   6, -2},
    {-2,   7, -20,  79,  79, -20,   7, -2},
    {-2,   6, -17,  59,  98, -21,   7, -2},
    {-1,   5, -13,  38, 112, -17,   5, -1},
    {-1,   2,  -6,  18, 123, -10,   3, -1}
#elif FILTER_ALPHA == 60
    /* alpha = 0.60 */
    { 0,   0,   0, 128,   0,   0,   0, 0},
    {-1,   3, -10, 123,  18,  -6,   2, -1},
    {-1,   5, -17, 112,  38, -12,   4, -1},
    {-1,   6, -20,  97,  58, -17,   6, -1},
    {-1,   6, -20,  79,  79, -20,   6, -1},
    {-1,   6, -17,  58,  97, -20,   6, -1},
    {-1,   4, -12,  38, 112, -17,   5, -1},
    {-1,   2,  -6,  18, 123, -10,   3, -1}
#elif FILTER_ALPHA == 55
    /* alpha = 0.55 */
    { 0,   0,   0, 128,   0,   0,   0,  0},
    {-1,   2, -10, 123,  18,  -6,   2,  0},
    {-1,   5, -16, 112,  37, -12,   4, -1},
    {-1,   5, -19,  97,  58, -16,   5, -1},
    {-1,   6, -19,  78,  78, -19,   6, -1},
    {-1,   5, -16,  58,  97, -19,   5, -1},
    {-1,   4, -12,  37, 112, -16,   5, -1},
    { 0,   2,  -6,  18, 123, -10,   2, -1}
#elif FILTER_ALPHA == 50
    /* alpha = 0.50 */
    { 0,   0,   0, 128,   0,   0,   0,  0},
    { 0,   2, -10, 122,  18,  -6,   2,  0},
    {-1,   4, -16, 112,  37, -11,   3,  0},
    {-1,   5, -18,  96,  58, -16,   5, -1},
    {-1,   5, -18,  78,  78, -18,   5, -1},
    {-1,   5, -16,  58,  96, -18,   5, -1},
    { 0,   3, -11,  37, 112, -16,   4, -1},
    { 0,   2,  -6,  18, 122, -10,   2,  0}
#elif FILTER_ALPHA == 0
    /* Lagrangian interpolation filter */
    { 0,   0,   0, 128,   0,   0,   0,   0},
    {-1,   3, -10, 122,  18,  -6,   2,   0},
    {-1,   4, -16, 112,  37, -11,   4,  -1},
    {-1,   5, -19,  97,  58, -16,   5,  -1},
    {-1,   6, -19,  78,  78, -19,   6,  -1},
    {-1,   5, -16,  58,  97, -19,   5,  -1},
    {-1,   4, -11,  37, 112, -16,   4,  -1},
    { 0,   2,  -6,  18, 122, -10,   3,  -1},
#endif  /* FILTER_ALPHA */
#endif  /* SUBPEL_SHIFTS==16 */
};

#else  // CONFIG_ENHANCED_INTERP

DECLARE_ALIGNED(16, const short, vp8_sub_pel_filters[8][6]) =
{

    { 0,  0,  128,    0,   0,  0 },         /* note that 1/8 pel positions are just as per alpha -0.5 bicubic */
    { 0, -6,  123,   12,  -1,  0 },
    { 2, -11, 108,   36,  -8,  1 },         /* New 1/4 pel 6 tap filter */
    { 0, -9,   93,   50,  -6,  0 },
    { 3, -16,  77,   77, -16,  3 },         /* New 1/2 pel 6 tap filter */
    { 0, -6,   50,   93,  -9,  0 },
    { 1, -8,   36,  108, -11,  2 },         /* New 1/4 pel 6 tap filter */
    { 0, -1,   12,  123,  -6,  0 },
};

#endif  // CONFIG_ENHANCED_INTERP

static void filter_block2d_first_pass
(
    unsigned char *src_ptr,
    int *output_ptr,
    unsigned int src_pixels_per_line,
    unsigned int pixel_step,
    unsigned int output_height,
    unsigned int output_width,
    const short *vp8_filter
)
{
    unsigned int i, j;
    int  Temp;

    for (i = 0; i < output_height; i++)
    {
        for (j = 0; j < output_width; j++)
        {
#if INTERP_EXTEND == 3
            Temp = ((int)src_ptr[-2 * (int)pixel_step] * vp8_filter[0]) +
                   ((int)src_ptr[-1 * (int)pixel_step] * vp8_filter[1]) +
                   ((int)src_ptr[0]                    * vp8_filter[2]) +
                   ((int)src_ptr[pixel_step]           * vp8_filter[3]) +
                   ((int)src_ptr[2*pixel_step]         * vp8_filter[4]) +
                   ((int)src_ptr[3*pixel_step]         * vp8_filter[5]) +
                   (VP8_FILTER_WEIGHT >> 1);      /* Rounding */
#elif INTERP_EXTEND == 4
            Temp = ((int)src_ptr[-3 * (int)pixel_step] * vp8_filter[0]) +
                   ((int)src_ptr[-2 * (int)pixel_step] * vp8_filter[1]) +
                   ((int)src_ptr[-1 * (int)pixel_step] * vp8_filter[2]) +
                   ((int)src_ptr[0]                    * vp8_filter[3]) +
                   ((int)src_ptr[pixel_step]           * vp8_filter[4]) +
                   ((int)src_ptr[2 * pixel_step]       * vp8_filter[5]) +
                   ((int)src_ptr[3 * pixel_step]       * vp8_filter[6]) +
                   ((int)src_ptr[4 * pixel_step]       * vp8_filter[7]) +
                   (VP8_FILTER_WEIGHT >> 1);      /* Rounding */
#elif INTERP_EXTEND == 5
            Temp = ((int)src_ptr[-4 * (int)pixel_step] * vp8_filter[0]) +
                   ((int)src_ptr[-3 * (int)pixel_step] * vp8_filter[1]) +
                   ((int)src_ptr[-2 * (int)pixel_step] * vp8_filter[2]) +
                   ((int)src_ptr[-1 * (int)pixel_step] * vp8_filter[3]) +
                   ((int)src_ptr[0]                    * vp8_filter[4]) +
                   ((int)src_ptr[pixel_step]           * vp8_filter[5]) +
                   ((int)src_ptr[2 * pixel_step]       * vp8_filter[6]) +
                   ((int)src_ptr[3 * pixel_step]       * vp8_filter[7]) +
                   ((int)src_ptr[4 * pixel_step]       * vp8_filter[8]) +
                   ((int)src_ptr[5 * pixel_step]       * vp8_filter[9]) +
                   (VP8_FILTER_WEIGHT >> 1);      /* Rounding */
#endif

            /* Normalize back to 0-255 */
            Temp = Temp >> VP8_FILTER_SHIFT;

            if (Temp < 0)
                Temp = 0;
            else if (Temp > 255)
                Temp = 255;

            output_ptr[j] = Temp;
            src_ptr++;
        }

        /* Next row... */
        src_ptr    += src_pixels_per_line - output_width;
        output_ptr += output_width;
    }
}

static void filter_block2d_second_pass
(
    int *src_ptr,
    unsigned char *output_ptr,
    int output_pitch,
    unsigned int src_pixels_per_line,
    unsigned int pixel_step,
    unsigned int output_height,
    unsigned int output_width,
    const short *vp8_filter
)
{
    unsigned int i, j;
    int  Temp;

    for (i = 0; i < output_height; i++)
    {
        for (j = 0; j < output_width; j++)
        {
            /* Apply filter */
#if INTERP_EXTEND == 3
            Temp = ((int)src_ptr[-2 * (int)pixel_step] * vp8_filter[0]) +
                   ((int)src_ptr[-1 * (int)pixel_step] * vp8_filter[1]) +
                   ((int)src_ptr[0]                    * vp8_filter[2]) +
                   ((int)src_ptr[pixel_step]           * vp8_filter[3]) +
                   ((int)src_ptr[2*pixel_step]         * vp8_filter[4]) +
                   ((int)src_ptr[3*pixel_step]         * vp8_filter[5]) +
                   (VP8_FILTER_WEIGHT >> 1);   /* Rounding */
#elif INTERP_EXTEND == 4
            Temp = ((int)src_ptr[-3 * (int)pixel_step] * vp8_filter[0]) +
                   ((int)src_ptr[-2 * (int)pixel_step] * vp8_filter[1]) +
                   ((int)src_ptr[-1 * (int)pixel_step] * vp8_filter[2]) +
                   ((int)src_ptr[0]                    * vp8_filter[3]) +
                   ((int)src_ptr[pixel_step]           * vp8_filter[4]) +
                   ((int)src_ptr[2 * pixel_step]       * vp8_filter[5]) +
                   ((int)src_ptr[3 * pixel_step]       * vp8_filter[6]) +
                   ((int)src_ptr[4 * pixel_step]       * vp8_filter[7]) +
                   (VP8_FILTER_WEIGHT >> 1);      /* Rounding */
#elif INTERP_EXTEND == 5
            Temp = ((int)src_ptr[-4 * (int)pixel_step] * vp8_filter[0]) +
                   ((int)src_ptr[-3 * (int)pixel_step] * vp8_filter[1]) +
                   ((int)src_ptr[-2 * (int)pixel_step] * vp8_filter[2]) +
                   ((int)src_ptr[-1 * (int)pixel_step] * vp8_filter[3]) +
                   ((int)src_ptr[0]                    * vp8_filter[4]) +
                   ((int)src_ptr[pixel_step]           * vp8_filter[5]) +
                   ((int)src_ptr[2 * pixel_step]       * vp8_filter[6]) +
                   ((int)src_ptr[3 * pixel_step]       * vp8_filter[7]) +
                   ((int)src_ptr[4 * pixel_step]       * vp8_filter[8]) +
                   ((int)src_ptr[5 * pixel_step]       * vp8_filter[9]) +
                   (VP8_FILTER_WEIGHT >> 1);      /* Rounding */
#endif

            /* Normalize back to 0-255 */
            Temp = Temp >> VP8_FILTER_SHIFT;

            if (Temp < 0)
                Temp = 0;
            else if (Temp > 255)
                Temp = 255;

            output_ptr[j] = (unsigned char)Temp;
            src_ptr++;
        }

        /* Start next row */
        src_ptr    += src_pixels_per_line - output_width;
        output_ptr += output_pitch;
    }
}

/*
 * The only functional difference between filter_block2d_second_pass()
 * and this function is that filter_block2d_second_pass() does a sixtap
 * filter on the input and stores it in the output. This function
 * (filter_block2d_second_pass_avg()) does a sixtap filter on the input,
 * and then averages that with the content already present in the output
 * ((filter_result + dest + 1) >> 1) and stores that in the output.
 */
static void filter_block2d_second_pass_avg
(
    int *src_ptr,
    unsigned char *output_ptr,
    int output_pitch,
    unsigned int src_pixels_per_line,
    unsigned int pixel_step,
    unsigned int output_height,
    unsigned int output_width,
    const short *vp8_filter
)
{
    unsigned int i, j;
    int  Temp;

    for (i = 0; i < output_height; i++)
    {
        for (j = 0; j < output_width; j++)
        {
            /* Apply filter */
#if INTERP_EXTEND == 3
            Temp = ((int)src_ptr[-2 * (int)pixel_step] * vp8_filter[0]) +
                   ((int)src_ptr[-1 * (int)pixel_step] * vp8_filter[1]) +
                   ((int)src_ptr[0]                    * vp8_filter[2]) +
                   ((int)src_ptr[pixel_step]           * vp8_filter[3]) +
                   ((int)src_ptr[2*pixel_step]         * vp8_filter[4]) +
                   ((int)src_ptr[3*pixel_step]         * vp8_filter[5]) +
                   (VP8_FILTER_WEIGHT >> 1);   /* Rounding */
#elif INTERP_EXTEND == 4
            Temp = ((int)src_ptr[-3 * (int)pixel_step] * vp8_filter[0]) +
                   ((int)src_ptr[-2 * (int)pixel_step] * vp8_filter[1]) +
                   ((int)src_ptr[-1 * (int)pixel_step] * vp8_filter[2]) +
                   ((int)src_ptr[0]                    * vp8_filter[3]) +
                   ((int)src_ptr[pixel_step]           * vp8_filter[4]) +
                   ((int)src_ptr[2 * pixel_step]       * vp8_filter[5]) +
                   ((int)src_ptr[3 * pixel_step]       * vp8_filter[6]) +
                   ((int)src_ptr[4 * pixel_step]       * vp8_filter[7]) +
                   (VP8_FILTER_WEIGHT >> 1);      /* Rounding */
#elif INTERP_EXTEND == 5
            Temp = ((int)src_ptr[-4 * (int)pixel_step] * vp8_filter[0]) +
                   ((int)src_ptr[-3 * (int)pixel_step] * vp8_filter[1]) +
                   ((int)src_ptr[-2 * (int)pixel_step] * vp8_filter[2]) +
                   ((int)src_ptr[-1 * (int)pixel_step] * vp8_filter[3]) +
                   ((int)src_ptr[0]                    * vp8_filter[4]) +
                   ((int)src_ptr[pixel_step]           * vp8_filter[5]) +
                   ((int)src_ptr[2 * pixel_step]       * vp8_filter[6]) +
                   ((int)src_ptr[3 * pixel_step]       * vp8_filter[7]) +
                   ((int)src_ptr[4 * pixel_step]       * vp8_filter[8]) +
                   ((int)src_ptr[5 * pixel_step]       * vp8_filter[9]) +
                   (VP8_FILTER_WEIGHT >> 1);      /* Rounding */
#endif

            /* Normalize back to 0-255 */
            Temp = Temp >> VP8_FILTER_SHIFT;

            if (Temp < 0)
                Temp = 0;
            else if (Temp > 255)
                Temp = 255;

            output_ptr[j] = (unsigned char) ((output_ptr[j] + Temp + 1) >> 1);
            src_ptr++;
        }

        /* Start next row */
        src_ptr    += src_pixels_per_line - output_width;
        output_ptr += output_pitch;
    }
}

static void filter_block2d
(
    unsigned char  *src_ptr,
    unsigned char  *output_ptr,
    unsigned int src_pixels_per_line,
    int output_pitch,
    const short  *HFilter,
    const short  *VFilter
)
{
    int FData[(3+INTERP_EXTEND*2)*4]; /* Temp data buffer used in filtering */

    /* First filter 1-D horizontally... */
    filter_block2d_first_pass(src_ptr - ((INTERP_EXTEND-1) * src_pixels_per_line), FData, src_pixels_per_line, 1,
                              3+INTERP_EXTEND*2, 4, HFilter);

    /* then filter verticaly... */
    filter_block2d_second_pass(FData + 4*(INTERP_EXTEND-1), output_ptr, output_pitch, 4, 4, 4, 4, VFilter);
}


void vp8_sixtap_predict_c
(
    unsigned char  *src_ptr,
    int   src_pixels_per_line,
    int  xoffset,
    int  yoffset,
    unsigned char *dst_ptr,
    int dst_pitch
)
{
    const short  *HFilter;
    const short  *VFilter;

    HFilter = vp8_sub_pel_filters[xoffset];   /* 6 tap */
    VFilter = vp8_sub_pel_filters[yoffset];   /* 6 tap */

    filter_block2d(src_ptr, dst_ptr, src_pixels_per_line, dst_pitch, HFilter, VFilter);
}
void vp8_sixtap_predict8x8_c
(
    unsigned char  *src_ptr,
    int  src_pixels_per_line,
    int  xoffset,
    int  yoffset,
    unsigned char *dst_ptr,
    int  dst_pitch
)
{
    const short  *HFilter;
    const short  *VFilter;
    // int FData[(7+INTERP_EXTEND*2)*16];   /* Temp data buffer used in filtering */
    int FData[(7+INTERP_EXTEND*2)*8];   /* Temp data buffer used in filtering */

    HFilter = vp8_sub_pel_filters[xoffset];   /* 6 tap */
    VFilter = vp8_sub_pel_filters[yoffset];   /* 6 tap */

    /* First filter 1-D horizontally... */
    filter_block2d_first_pass(src_ptr - ((INTERP_EXTEND-1) * src_pixels_per_line), FData, src_pixels_per_line, 1,
                              7+INTERP_EXTEND*2, 8, HFilter);


    /* then filter verticaly... */
    filter_block2d_second_pass(FData + 8*(INTERP_EXTEND-1), dst_ptr, dst_pitch, 8, 8, 8, 8, VFilter);

}

void vp8_sixtap_predict_avg8x8_c
(
    unsigned char  *src_ptr,
    int  src_pixels_per_line,
    int  xoffset,
    int  yoffset,
    unsigned char *dst_ptr,
    int  dst_pitch
)
{
    const short  *HFilter;
    const short  *VFilter;
    // int FData[(7+INTERP_EXTEND*2)*16];   /* Temp data buffer used in filtering */
    int FData[(7+INTERP_EXTEND*2)*8];   /* Temp data buffer used in filtering */

    HFilter = vp8_sub_pel_filters[xoffset];   /* 6 tap */
    VFilter = vp8_sub_pel_filters[yoffset];   /* 6 tap */

    /* First filter 1-D horizontally... */
    filter_block2d_first_pass(src_ptr - ((INTERP_EXTEND-1) * src_pixels_per_line), FData, src_pixels_per_line, 1,
                              7+INTERP_EXTEND*2, 8, HFilter);

    /* then filter verticaly... */
    filter_block2d_second_pass_avg(FData + 8*(INTERP_EXTEND-1), dst_ptr, dst_pitch, 8, 8, 8, 8, VFilter);
}

void vp8_sixtap_predict8x4_c
(
    unsigned char  *src_ptr,
    int  src_pixels_per_line,
    int  xoffset,
    int  yoffset,
    unsigned char *dst_ptr,
    int  dst_pitch
)
{
    const short  *HFilter;
    const short  *VFilter;
    // int FData[(7+INTERP_EXTEND*2)*16];   /* Temp data buffer used in filtering */
    int FData[(3+INTERP_EXTEND*2)*8];   /* Temp data buffer used in filtering */

    HFilter = vp8_sub_pel_filters[xoffset];   /* 6 tap */
    VFilter = vp8_sub_pel_filters[yoffset];   /* 6 tap */

    /* First filter 1-D horizontally... */
    filter_block2d_first_pass(src_ptr - ((INTERP_EXTEND-1) * src_pixels_per_line), FData, src_pixels_per_line, 1,
                              3+INTERP_EXTEND*2, 8, HFilter);


    /* then filter verticaly... */
    filter_block2d_second_pass(FData + 8*(INTERP_EXTEND-1), dst_ptr, dst_pitch, 8, 8, 4, 8, VFilter);

}

void vp8_sixtap_predict16x16_c
(
    unsigned char  *src_ptr,
    int  src_pixels_per_line,
    int  xoffset,
    int  yoffset,
    unsigned char *dst_ptr,
    int  dst_pitch
)
{
    const short  *HFilter;
    const short  *VFilter;
    // int FData[(15+INTERP_EXTEND*2)*24];   /* Temp data buffer used in filtering */
    int FData[(15+INTERP_EXTEND*2)*16];  /* Temp data buffer used in filtering */


    HFilter = vp8_sub_pel_filters[xoffset];   /* 6 tap */
    VFilter = vp8_sub_pel_filters[yoffset];   /* 6 tap */

    /* First filter 1-D horizontally... */
    filter_block2d_first_pass(src_ptr - ((INTERP_EXTEND-1) * src_pixels_per_line), FData, src_pixels_per_line, 1,
                              15+INTERP_EXTEND*2, 16, HFilter);

    /* then filter verticaly... */
    filter_block2d_second_pass(FData + 16*(INTERP_EXTEND-1), dst_ptr, dst_pitch, 16, 16, 16, 16, VFilter);

}

void vp8_sixtap_predict_avg16x16_c
(
    unsigned char  *src_ptr,
    int  src_pixels_per_line,
    int  xoffset,
    int  yoffset,
    unsigned char *dst_ptr,
    int  dst_pitch
)
{
    const short  *HFilter;
    const short  *VFilter;
    // int FData[(15+INTERP_EXTEND*2)*24];   /* Temp data buffer used in filtering */
    int FData[(15+INTERP_EXTEND*2)*16];  /* Temp data buffer used in filtering */

    HFilter = vp8_sub_pel_filters[xoffset];   /* 6 tap */
    VFilter = vp8_sub_pel_filters[yoffset];   /* 6 tap */

    /* First filter 1-D horizontally... */
    filter_block2d_first_pass(src_ptr - ((INTERP_EXTEND-1) * src_pixels_per_line), FData,
                              src_pixels_per_line, 1, 15+INTERP_EXTEND*2, 16, HFilter);

    /* then filter verticaly... */
    filter_block2d_second_pass_avg(FData + 16*(INTERP_EXTEND-1), dst_ptr, dst_pitch,
                                   16, 16, 16, 16, VFilter);
}

/****************************************************************************
 *
 *  ROUTINE       : filter_block2d_bil_first_pass
 *
 *  INPUTS        : UINT8  *src_ptr    : Pointer to source block.
 *                  UINT32  src_stride : Stride of source block.
 *                  UINT32  height     : Block height.
 *                  UINT32  width      : Block width.
 *                  INT32  *vp8_filter : Array of 2 bi-linear filter taps.
 *
 *  OUTPUTS       : INT32  *dst_ptr    : Pointer to filtered block.
 *
 *  RETURNS       : void
 *
 *  FUNCTION      : Applies a 1-D 2-tap bi-linear filter to the source block
 *                  in the horizontal direction to produce the filtered output
 *                  block. Used to implement first-pass of 2-D separable filter.
 *
 *  SPECIAL NOTES : Produces INT32 output to retain precision for next pass.
 *                  Two filter taps should sum to VP8_FILTER_WEIGHT.
 *
 ****************************************************************************/
static void filter_block2d_bil_first_pass
(
    unsigned char  *src_ptr,
    unsigned short *dst_ptr,
    unsigned int    src_stride,
    unsigned int    height,
    unsigned int    width,
    const short    *vp8_filter
)
{
    unsigned int i, j;

    for (i = 0; i < height; i++)
    {
        for (j = 0; j < width; j++)
        {
            /* Apply bilinear filter */
            dst_ptr[j] = (((int)src_ptr[0] * vp8_filter[0]) +
                          ((int)src_ptr[1] * vp8_filter[1]) +
                          (VP8_FILTER_WEIGHT / 2)) >> VP8_FILTER_SHIFT;
            src_ptr++;
        }

        /* Next row... */
        src_ptr += src_stride - width;
        dst_ptr += width;
    }
}

/****************************************************************************
 *
 *  ROUTINE       : filter_block2d_bil_second_pass
 *
 *  INPUTS        : INT32  *src_ptr    : Pointer to source block.
 *                  UINT32  dst_pitch  : Destination block pitch.
 *                  UINT32  height     : Block height.
 *                  UINT32  width      : Block width.
 *                  INT32  *vp8_filter : Array of 2 bi-linear filter taps.
 *
 *  OUTPUTS       : UINT16 *dst_ptr    : Pointer to filtered block.
 *
 *  RETURNS       : void
 *
 *  FUNCTION      : Applies a 1-D 2-tap bi-linear filter to the source block
 *                  in the vertical direction to produce the filtered output
 *                  block. Used to implement second-pass of 2-D separable filter.
 *
 *  SPECIAL NOTES : Requires 32-bit input as produced by filter_block2d_bil_first_pass.
 *                  Two filter taps should sum to VP8_FILTER_WEIGHT.
 *
 ****************************************************************************/
static void filter_block2d_bil_second_pass
(
    unsigned short *src_ptr,
    unsigned char  *dst_ptr,
    int             dst_pitch,
    unsigned int    height,
    unsigned int    width,
    const short    *vp8_filter
)
{
    unsigned int  i, j;
    int  Temp;

    for (i = 0; i < height; i++)
    {
        for (j = 0; j < width; j++)
        {
            /* Apply filter */
            Temp = ((int)src_ptr[0]     * vp8_filter[0]) +
                   ((int)src_ptr[width] * vp8_filter[1]) +
                   (VP8_FILTER_WEIGHT / 2);
            dst_ptr[j] = (unsigned int)(Temp >> VP8_FILTER_SHIFT);
            src_ptr++;
        }

        /* Next row... */
        dst_ptr += dst_pitch;
    }
}

/*
 * As before for filter_block2d_second_pass_avg(), the functional difference
 * between filter_block2d_bil_second_pass() and filter_block2d_bil_second_pass_avg()
 * is that filter_block2d_bil_second_pass() does a bilinear filter on input
 * and stores the result in output; filter_block2d_bil_second_pass_avg(),
 * instead, does a bilinear filter on input, averages the resulting value
 * with the values already present in the output and stores the result of
 * that back into the output ((filter_result + dest + 1) >> 1).
 */
static void filter_block2d_bil_second_pass_avg
(
    unsigned short *src_ptr,
    unsigned char  *dst_ptr,
    int             dst_pitch,
    unsigned int    height,
    unsigned int    width,
    const short    *vp8_filter
)
{
    unsigned int  i, j;
    int  Temp;

    for (i = 0; i < height; i++)
    {
        for (j = 0; j < width; j++)
        {
            /* Apply filter */
            Temp = ((int)src_ptr[0]     * vp8_filter[0]) +
                   ((int)src_ptr[width] * vp8_filter[1]) +
                   (VP8_FILTER_WEIGHT / 2);
            dst_ptr[j] = (unsigned int)(((Temp >> VP8_FILTER_SHIFT) + dst_ptr[j] + 1) >> 1);
            src_ptr++;
        }

        /* Next row... */
        dst_ptr += dst_pitch;
    }
}

/****************************************************************************
 *
 *  ROUTINE       : filter_block2d_bil
 *
 *  INPUTS        : UINT8  *src_ptr          : Pointer to source block.
 *                  UINT32  src_pitch        : Stride of source block.
 *                  UINT32  dst_pitch        : Stride of destination block.
 *                  INT32  *HFilter          : Array of 2 horizontal filter taps.
 *                  INT32  *VFilter          : Array of 2 vertical filter taps.
 *                  INT32  Width             : Block width
 *                  INT32  Height            : Block height
 *
 *  OUTPUTS       : UINT16 *dst_ptr       : Pointer to filtered block.
 *
 *  RETURNS       : void
 *
 *  FUNCTION      : 2-D filters an input block by applying a 2-tap
 *                  bi-linear filter horizontally followed by a 2-tap
 *                  bi-linear filter vertically on the result.
 *
 *  SPECIAL NOTES : The largest block size can be handled here is 16x16
 *
 ****************************************************************************/
static void filter_block2d_bil
(
    unsigned char *src_ptr,
    unsigned char *dst_ptr,
    unsigned int   src_pitch,
    unsigned int   dst_pitch,
    const short   *HFilter,
    const short   *VFilter,
    int            Width,
    int            Height
)
{

    unsigned short FData[17*16];    /* Temp data buffer used in filtering */

    /* First filter 1-D horizontally... */
    filter_block2d_bil_first_pass(src_ptr, FData, src_pitch, Height + 1, Width, HFilter);

    /* then 1-D vertically... */
    filter_block2d_bil_second_pass(FData, dst_ptr, dst_pitch, Height, Width, VFilter);
}

static void filter_block2d_bil_avg
(
    unsigned char *src_ptr,
    unsigned char *dst_ptr,
    unsigned int   src_pitch,
    unsigned int   dst_pitch,
    const short   *HFilter,
    const short   *VFilter,
    int            Width,
    int            Height
)
{
    unsigned short FData[17*16];    /* Temp data buffer used in filtering */

    /* First filter 1-D horizontally... */
    filter_block2d_bil_first_pass(src_ptr, FData, src_pitch, Height + 1, Width, HFilter);

    /* then 1-D vertically... */
    filter_block2d_bil_second_pass_avg(FData, dst_ptr, dst_pitch, Height, Width, VFilter);
}

void vp8_bilinear_predict4x4_c
(
    unsigned char  *src_ptr,
    int   src_pixels_per_line,
    int  xoffset,
    int  yoffset,
    unsigned char *dst_ptr,
    int dst_pitch
)
{
    const short *HFilter;
    const short *VFilter;

    HFilter = vp8_bilinear_filters[xoffset];
    VFilter = vp8_bilinear_filters[yoffset];
#if 0
    {
        int i;
        unsigned char temp1[16];
        unsigned char temp2[16];

        bilinear_predict4x4_mmx(src_ptr, src_pixels_per_line, xoffset, yoffset, temp1, 4);
        filter_block2d_bil(src_ptr, temp2, src_pixels_per_line, 4, HFilter, VFilter, 4, 4);

        for (i = 0; i < 16; i++)
        {
            if (temp1[i] != temp2[i])
            {
                bilinear_predict4x4_mmx(src_ptr, src_pixels_per_line, xoffset, yoffset, temp1, 4);
                filter_block2d_bil(src_ptr, temp2, src_pixels_per_line, 4, HFilter, VFilter, 4, 4);
            }
        }
    }
#endif
    filter_block2d_bil(src_ptr, dst_ptr, src_pixels_per_line, dst_pitch, HFilter, VFilter, 4, 4);

}

void vp8_bilinear_predict8x8_c
(
    unsigned char  *src_ptr,
    int  src_pixels_per_line,
    int  xoffset,
    int  yoffset,
    unsigned char *dst_ptr,
    int  dst_pitch
)
{
    const short *HFilter;
    const short *VFilter;

    HFilter = vp8_bilinear_filters[xoffset];
    VFilter = vp8_bilinear_filters[yoffset];

    filter_block2d_bil(src_ptr, dst_ptr, src_pixels_per_line, dst_pitch, HFilter, VFilter, 8, 8);

}

void vp8_bilinear_predict_avg8x8_c
(
    unsigned char  *src_ptr,
    int  src_pixels_per_line,
    int  xoffset,
    int  yoffset,
    unsigned char *dst_ptr,
    int  dst_pitch
)
{
    const short *HFilter;
    const short *VFilter;

    HFilter = vp8_bilinear_filters[xoffset];
    VFilter = vp8_bilinear_filters[yoffset];

    filter_block2d_bil_avg(src_ptr, dst_ptr, src_pixels_per_line,
                           dst_pitch, HFilter, VFilter, 8, 8);
}

void vp8_bilinear_predict8x4_c
(
    unsigned char  *src_ptr,
    int  src_pixels_per_line,
    int  xoffset,
    int  yoffset,
    unsigned char *dst_ptr,
    int  dst_pitch
)
{
    const short *HFilter;
    const short *VFilter;

    HFilter = vp8_bilinear_filters[xoffset];
    VFilter = vp8_bilinear_filters[yoffset];

    filter_block2d_bil(src_ptr, dst_ptr, src_pixels_per_line, dst_pitch, HFilter, VFilter, 8, 4);

}

void vp8_bilinear_predict16x16_c
(
    unsigned char  *src_ptr,
    int  src_pixels_per_line,
    int  xoffset,
    int  yoffset,
    unsigned char *dst_ptr,
    int  dst_pitch
)
{
    const short *HFilter;
    const short *VFilter;

    HFilter = vp8_bilinear_filters[xoffset];
    VFilter = vp8_bilinear_filters[yoffset];

    filter_block2d_bil(src_ptr, dst_ptr, src_pixels_per_line, dst_pitch, HFilter, VFilter, 16, 16);
}

void vp8_bilinear_predict_avg16x16_c
(
    unsigned char  *src_ptr,
    int  src_pixels_per_line,
    int  xoffset,
    int  yoffset,
    unsigned char *dst_ptr,
    int  dst_pitch
)
{
    const short *HFilter;
    const short *VFilter;

    HFilter = vp8_bilinear_filters[xoffset];
    VFilter = vp8_bilinear_filters[yoffset];

    filter_block2d_bil_avg(src_ptr, dst_ptr, src_pixels_per_line,
                           dst_pitch, HFilter, VFilter, 16, 16);
}
