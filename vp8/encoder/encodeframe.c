/*
 *  Copyright (c) 2010 The WebM project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */


#include "vpx_ports/config.h"
#include "encodemb.h"
#include "encodemv.h"
#include "vp8/common/common.h"
#include "onyx_int.h"
#include "vp8/common/extend.h"
#include "vp8/common/entropymode.h"
#include "vp8/common/quant_common.h"
#include "segmentation.h"
#include "vp8/common/setupintrarecon.h"
#include "encodeintra.h"
#include "vp8/common/reconinter.h"
#include "rdopt.h"
#include "vp8/common/findnearmv.h"
#include "vp8/common/reconintra.h"
#include "vp8/common/seg_common.h"
#include <stdio.h>
#include <math.h>
#include <limits.h>
#include "vp8/common/subpixel.h"
#include "vpx_ports/vpx_timer.h"
#include "vp8/common/pred_common.h"

//#define DBG_PRNT_SEGMAP 1

#if CONFIG_RUNTIME_CPU_DETECT
#define RTCD(x)     &cpi->common.rtcd.x
#define IF_RTCD(x)  (x)
#else
#define RTCD(x)     NULL
#define IF_RTCD(x)  NULL
#endif

#ifdef ENC_DEBUG
int enc_debug=0;
int mb_row_debug, mb_col_debug;
#endif

extern void vp8_stuff_mb(VP8_COMP *cpi, MACROBLOCKD *x, TOKENEXTRA **t) ;

extern void vp8cx_initialize_me_consts(VP8_COMP *cpi, int QIndex);
extern void vp8_auto_select_speed(VP8_COMP *cpi);
extern void vp8cx_init_mbrthread_data(VP8_COMP *cpi,
                                      MACROBLOCK *x,
                                      MB_ROW_COMP *mbr_ei,
                                      int mb_row,
                                      int count);
void vp8_build_block_offsets(MACROBLOCK *x);
void vp8_setup_block_ptrs(MACROBLOCK *x);
int vp8cx_encode_inter_macroblock(VP8_COMP *cpi, MACROBLOCK *x, TOKENEXTRA **t, int recon_yoffset, int recon_uvoffset);
int vp8cx_encode_intra_macro_block(VP8_COMP *cpi, MACROBLOCK *x, TOKENEXTRA **t);
static void adjust_act_zbin( VP8_COMP *cpi, MACROBLOCK *x );



#ifdef MODE_STATS
unsigned int inter_y_modes[MB_MODE_COUNT] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
unsigned int inter_uv_modes[VP8_UV_MODES] = {0, 0, 0, 0};
unsigned int inter_b_modes[B_MODE_COUNT] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
unsigned int y_modes[VP8_YMODES] = {0, 0, 0, 0, 0, 0};
unsigned int i8x8_modes[VP8_I8X8_MODES]={0  };
unsigned int uv_modes[VP8_UV_MODES] = {0, 0, 0, 0};
unsigned int uv_modes_y[VP8_YMODES][VP8_UV_MODES]=
{
{0, 0, 0, 0},
{0, 0, 0, 0},
{0, 0, 0, 0},
{0, 0, 0, 0},
{0, 0, 0, 0},
{0, 0, 0, 0}
};
unsigned int b_modes[B_MODE_COUNT] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
#endif


/* activity_avg must be positive, or flat regions could get a zero weight
 *  (infinite lambda), which confounds analysis.
 * This also avoids the need for divide by zero checks in
 *  vp8_activity_masking().
 */
#define VP8_ACTIVITY_AVG_MIN (64)

/* This is used as a reference when computing the source variance for the
 *  purposes of activity masking.
 * Eventually this should be replaced by custom no-reference routines,
 *  which will be faster.
 */
static const unsigned char VP8_VAR_OFFS[16]=
{
    128,128,128,128,128,128,128,128,128,128,128,128,128,128,128,128
};


// Original activity measure from Tim T's code.
static unsigned int tt_activity_measure( VP8_COMP *cpi, MACROBLOCK *x )
{
    unsigned int act;
    unsigned int sse;
    /* TODO: This could also be done over smaller areas (8x8), but that would
     *  require extensive changes elsewhere, as lambda is assumed to be fixed
     *  over an entire MB in most of the code.
     * Another option is to compute four 8x8 variances, and pick a single
     *  lambda using a non-linear combination (e.g., the smallest, or second
     *  smallest, etc.).
     */
    act =     VARIANCE_INVOKE(&cpi->rtcd.variance, var16x16)(x->src.y_buffer,
                    x->src.y_stride, VP8_VAR_OFFS, 0, &sse);
    act = act<<4;

    /* If the region is flat, lower the activity some more. */
    if (act < 8<<12)
        act = act < 5<<12 ? act : 5<<12;

    return act;
}

// Stub for alternative experimental activity measures.
static unsigned int alt_activity_measure( VP8_COMP *cpi,
                                          MACROBLOCK *x, int use_dc_pred )
{
    return vp8_encode_intra(cpi,x, use_dc_pred);
}


// Measure the activity of the current macroblock
// What we measure here is TBD so abstracted to this function
#define ALT_ACT_MEASURE 1
static unsigned int mb_activity_measure( VP8_COMP *cpi, MACROBLOCK *x,
                                  int mb_row, int mb_col)
{
    unsigned int mb_activity;

    if  ( ALT_ACT_MEASURE )
    {
        int use_dc_pred = (mb_col || mb_row) && (!mb_col || !mb_row);

        // Or use and alternative.
        mb_activity = alt_activity_measure( cpi, x, use_dc_pred );
    }
    else
    {
        // Original activity measure from Tim T's code.
        mb_activity = tt_activity_measure( cpi, x );
    }

    if ( mb_activity < VP8_ACTIVITY_AVG_MIN )
        mb_activity = VP8_ACTIVITY_AVG_MIN;

    return mb_activity;
}

// Calculate an "average" mb activity value for the frame
#define ACT_MEDIAN 0
static void calc_av_activity( VP8_COMP *cpi, int64_t activity_sum )
{
#if ACT_MEDIAN
    // Find median: Simple n^2 algorithm for experimentation
    {
        unsigned int median;
        unsigned int i,j;
        unsigned int * sortlist;
        unsigned int tmp;

        // Create a list to sort to
        CHECK_MEM_ERROR(sortlist,
                        vpx_calloc(sizeof(unsigned int),
                        cpi->common.MBs));

        // Copy map to sort list
        vpx_memcpy( sortlist, cpi->mb_activity_map,
                    sizeof(unsigned int) * cpi->common.MBs );


        // Ripple each value down to its correct position
        for ( i = 1; i < cpi->common.MBs; i ++ )
        {
            for ( j = i; j > 0; j -- )
            {
                if ( sortlist[j] < sortlist[j-1] )
                {
                    // Swap values
                    tmp = sortlist[j-1];
                    sortlist[j-1] = sortlist[j];
                    sortlist[j] = tmp;
                }
                else
                    break;
            }
        }

        // Even number MBs so estimate median as mean of two either side.
        median = ( 1 + sortlist[cpi->common.MBs >> 1] +
                   sortlist[(cpi->common.MBs >> 1) + 1] ) >> 1;

        cpi->activity_avg = median;

        vpx_free(sortlist);
    }
#else
    // Simple mean for now
    cpi->activity_avg = (unsigned int)(activity_sum/cpi->common.MBs);
#endif

    if (cpi->activity_avg < VP8_ACTIVITY_AVG_MIN)
        cpi->activity_avg = VP8_ACTIVITY_AVG_MIN;

    // Experimental code: return fixed value normalized for several clips
    if  ( ALT_ACT_MEASURE )
        cpi->activity_avg = 100000;
}

#define USE_ACT_INDEX   0
#define OUTPUT_NORM_ACT_STATS   0

#if USE_ACT_INDEX
// Calculate and activity index for each mb
static void calc_activity_index( VP8_COMP *cpi, MACROBLOCK *x )
{
    VP8_COMMON *const cm = & cpi->common;
    int mb_row, mb_col;

    int64_t act;
    int64_t a;
    int64_t b;

#if OUTPUT_NORM_ACT_STATS
    FILE *f = fopen("norm_act.stt", "a");
    fprintf(f, "\n%12d\n", cpi->activity_avg );
#endif

    // Reset pointers to start of activity map
    x->mb_activity_ptr = cpi->mb_activity_map;

    // Calculate normalized mb activity number.
    for (mb_row = 0; mb_row < cm->mb_rows; mb_row++)
    {
        // for each macroblock col in image
        for (mb_col = 0; mb_col < cm->mb_cols; mb_col++)
        {
            // Read activity from the map
            act = *(x->mb_activity_ptr);

            // Calculate a normalized activity number
            a = act + 4*cpi->activity_avg;
            b = 4*act + cpi->activity_avg;

            if ( b >= a )
                *(x->activity_ptr) = (int)((b + (a>>1))/a) - 1;
            else
                *(x->activity_ptr) = 1 - (int)((a + (b>>1))/b);

#if OUTPUT_NORM_ACT_STATS
            fprintf(f, " %6d", *(x->mb_activity_ptr));
#endif
            // Increment activity map pointers
            x->mb_activity_ptr++;
        }

#if OUTPUT_NORM_ACT_STATS
        fprintf(f, "\n");
#endif

    }

#if OUTPUT_NORM_ACT_STATS
    fclose(f);
#endif

}
#endif

// Loop through all MBs. Note activity of each, average activity and
// calculate a normalized activity for each
static void build_activity_map( VP8_COMP *cpi )
{
    MACROBLOCK *const x = & cpi->mb;
    MACROBLOCKD *xd = &x->e_mbd;
    VP8_COMMON *const cm = & cpi->common;

#if ALT_ACT_MEASURE
    YV12_BUFFER_CONFIG *new_yv12 = &cm->yv12_fb[cm->new_fb_idx];
    int recon_yoffset;
    int recon_y_stride = new_yv12->y_stride;
#endif

    int mb_row, mb_col;
    unsigned int mb_activity;
    int64_t activity_sum = 0;

    // for each macroblock row in image
    for (mb_row = 0; mb_row < cm->mb_rows; mb_row++)
    {
#if ALT_ACT_MEASURE
        // reset above block coeffs
        xd->up_available = (mb_row != 0);
        recon_yoffset = (mb_row * recon_y_stride * 16);
#endif
        // for each macroblock col in image
        for (mb_col = 0; mb_col < cm->mb_cols; mb_col++)
        {
#if ALT_ACT_MEASURE
            xd->dst.y_buffer = new_yv12->y_buffer + recon_yoffset;
            xd->left_available = (mb_col != 0);
            recon_yoffset += 16;
#endif
            //Copy current mb to a buffer
            RECON_INVOKE(&xd->rtcd->recon, copy16x16)(x->src.y_buffer, x->src.y_stride, x->thismb, 16);

            // measure activity
            mb_activity = mb_activity_measure( cpi, x, mb_row, mb_col );

            // Keep frame sum
            activity_sum += mb_activity;

            // Store MB level activity details.
            *x->mb_activity_ptr = mb_activity;

            // Increment activity map pointer
            x->mb_activity_ptr++;

            // adjust to the next column of source macroblocks
            x->src.y_buffer += 16;
        }


        // adjust to the next row of mbs
        x->src.y_buffer += 16 * x->src.y_stride - 16 * cm->mb_cols;

#if ALT_ACT_MEASURE
        //extend the recon for intra prediction
        vp8_extend_mb_row(new_yv12, xd->dst.y_buffer + 16,
                          xd->dst.u_buffer + 8, xd->dst.v_buffer + 8);
#endif

    }

    // Calculate an "average" MB activity
    calc_av_activity(cpi, activity_sum);

#if USE_ACT_INDEX
    // Calculate an activity index number of each mb
    calc_activity_index( cpi, x );
#endif

}

// Macroblock activity masking
void vp8_activity_masking(VP8_COMP *cpi, MACROBLOCK *x)
{
#if USE_ACT_INDEX
    x->rdmult += *(x->mb_activity_ptr) * (x->rdmult >> 2);
    x->errorperbit = x->rdmult * 100 /(110 * x->rddiv);
    x->errorperbit += (x->errorperbit==0);
#else
    int64_t a;
    int64_t b;
    int64_t act = *(x->mb_activity_ptr);

    // Apply the masking to the RD multiplier.
    a = act + (2*cpi->activity_avg);
    b = (2*act) + cpi->activity_avg;

    x->rdmult = (unsigned int)(((int64_t)x->rdmult*b + (a>>1))/a);
    x->errorperbit = x->rdmult * 100 /(110 * x->rddiv);
    x->errorperbit += (x->errorperbit==0);
#endif

    // Activity based Zbin adjustment
    adjust_act_zbin(cpi, x);
}

static
void encode_mb_row(VP8_COMP *cpi,
                   VP8_COMMON *cm,
                   int mb_row,
                   MACROBLOCK  *x,
                   MACROBLOCKD *xd,
                   TOKENEXTRA **tp,
                   int *totalrate)
{
    int recon_yoffset, recon_uvoffset;
    int mb_col;
    int ref_fb_idx = cm->lst_fb_idx;
    int dst_fb_idx = cm->new_fb_idx;
    int recon_y_stride = cm->yv12_fb[ref_fb_idx].y_stride;
    int recon_uv_stride = cm->yv12_fb[ref_fb_idx].uv_stride;
    int map_index = (mb_row * cpi->common.mb_cols);

    // Reset the left context
    vp8_zero(cm->left_context)

    // reset above block coeffs
    xd->above_context = cm->above_context;

    xd->up_available = (mb_row != 0);
    recon_yoffset = (mb_row * recon_y_stride * 16);
    recon_uvoffset = (mb_row * recon_uv_stride * 8);

    cpi->tplist[mb_row].start = *tp;
    //printf("Main mb_row = %d\n", mb_row);

    // Distance of Mb to the top & bottom edges, specified in 1/8th pel
    // units as they are always compared to values that are in 1/8th pel units
    xd->mb_to_top_edge = -((mb_row * 16) << 3);
    xd->mb_to_bottom_edge = ((cm->mb_rows - 1 - mb_row) * 16) << 3;

    // Set up limit values for vertical motion vector components
    // to prevent them extending beyond the UMV borders
    x->mv_row_min = -((mb_row * 16) + (VP8BORDERINPIXELS - 16));
    x->mv_row_max = ((cm->mb_rows - 1 - mb_row) * 16)
                        + (VP8BORDERINPIXELS - 16);

    // Set the mb activity pointer to the start of the row.
    x->mb_activity_ptr = &cpi->mb_activity_map[map_index];

    // for each macroblock col in image
    for (mb_col = 0; mb_col < cm->mb_cols; mb_col++)
    {
#ifdef ENC_DEBUG
        enc_debug = (cpi->common.current_video_frame ==1 && mb_row==4 && mb_col==0);
        mb_col_debug=mb_col;
        mb_row_debug=mb_row;
#endif
        // Distance of Mb to the left & right edges, specified in
        // 1/8th pel units as they are always compared to values
        // that are in 1/8th pel units
        xd->mb_to_left_edge = -((mb_col * 16) << 3);
        xd->mb_to_right_edge = ((cm->mb_cols - 1 - mb_col) * 16) << 3;

        // Set up limit values for horizontal motion vector components
        // to prevent them extending beyond the UMV borders
        x->mv_col_min = -((mb_col * 16) + (VP8BORDERINPIXELS - 16));
        x->mv_col_max = ((cm->mb_cols - 1 - mb_col) * 16)
                            + (VP8BORDERINPIXELS - 16);

        xd->dst.y_buffer = cm->yv12_fb[dst_fb_idx].y_buffer + recon_yoffset;
        xd->dst.u_buffer = cm->yv12_fb[dst_fb_idx].u_buffer + recon_uvoffset;
        xd->dst.v_buffer = cm->yv12_fb[dst_fb_idx].v_buffer + recon_uvoffset;
        xd->left_available = (mb_col != 0);

        x->rddiv = cpi->RDDIV;
        x->rdmult = cpi->RDMULT;

        //Copy current mb to a buffer
        RECON_INVOKE(&xd->rtcd->recon, copy16x16)(x->src.y_buffer, x->src.y_stride, x->thismb, 16);

        if(cpi->oxcf.tuning == VP8_TUNE_SSIM)
            vp8_activity_masking(cpi, x);

        // Is segmentation enabled
        if (xd->segmentation_enabled)
        {
            // Code to set segment id in xd->mbmi.segment_id
            if (cpi->segmentation_map[map_index+mb_col] <= 3)
                xd->mode_info_context->mbmi.segment_id = cpi->segmentation_map[map_index+mb_col];
            else
                xd->mode_info_context->mbmi.segment_id = 0;

            vp8cx_mb_init_quantizer(cpi, x);
        }
        else
            // Set to Segment 0 by default
            xd->mode_info_context->mbmi.segment_id = 0;

        x->active_ptr = cpi->active_map + map_index + mb_col;

        /* force 4x4 transform for mode selection */
        xd->mode_info_context->mbmi.txfm_size = TX_4X4;

        if (cm->frame_type == KEY_FRAME)
        {
            *totalrate += vp8cx_encode_intra_macro_block(cpi, x, tp);
            //Note the encoder may have changed the segment_id

#ifdef MODE_STATS
            y_modes[xd->mode_info_context->mbmi.mode] ++;
#endif
        }
        else
        {
            *totalrate += vp8cx_encode_inter_macroblock(cpi, x, tp, recon_yoffset, recon_uvoffset);
            //Note the encoder may have changed the segment_id

#ifdef MODE_STATS
            inter_y_modes[xd->mode_info_context->mbmi.mode] ++;

            if (xd->mode_info_context->mbmi.mode == SPLITMV)
            {
                int b;

                for (b = 0; b < x->partition_info->count; b++)
                {
                    inter_b_modes[x->partition_info->bmi[b].mode] ++;
                }
            }

#endif

            // Count of last ref frame 0,0 usage
            if ((xd->mode_info_context->mbmi.mode == ZEROMV) && (xd->mode_info_context->mbmi.ref_frame == LAST_FRAME))
                cpi->inter_zz_count ++;
        }

        cpi->tplist[mb_row].stop = *tp;

        // Increment pointer into gf usage flags structure.
        x->gf_active_ptr++;

        // Increment the activity mask pointers.
        x->mb_activity_ptr++;

        // adjust to the next column of macroblocks
        x->src.y_buffer += 16;
        x->src.u_buffer += 8;
        x->src.v_buffer += 8;

        recon_yoffset += 16;
        recon_uvoffset += 8;

        // skip to next mb
        xd->mode_info_context++;

        xd->prev_mode_info_context++;
        assert((xd->prev_mode_info_context - cpi->common.prev_mip)
            ==(xd->mode_info_context - cpi->common.mip));
        x->partition_info++;

        xd->above_context++;
    }

    //extend the recon for intra prediction
    vp8_extend_mb_row(
        &cm->yv12_fb[dst_fb_idx],
        xd->dst.y_buffer + 16,
        xd->dst.u_buffer + 8,
        xd->dst.v_buffer + 8);

    // this is to account for the border
    xd->prev_mode_info_context++;
    xd->mode_info_context++;
    x->partition_info++;

// debug output
#if DBG_PRNT_SEGMAP
    {
        FILE *statsfile;
        statsfile = fopen("segmap2.stt", "a");
        fprintf(statsfile, "\n" );
        fclose(statsfile);
    }
#endif
}

void init_encode_frame_mb_context(VP8_COMP *cpi)
{
    MACROBLOCK *const x = & cpi->mb;
    VP8_COMMON *const cm = & cpi->common;
    MACROBLOCKD *const xd = & x->e_mbd;

    // GF active flags data structure
    x->gf_active_ptr = (signed char *)cpi->gf_active_flags;

    // Activity map pointer
    x->mb_activity_ptr = cpi->mb_activity_map;

    x->vector_range = 32;

    x->act_zbin_adj = 0;

    x->partition_info = x->pi;

    xd->mode_info_context = cm->mi;
    xd->mode_info_stride = cm->mode_info_stride;
    xd->prev_mode_info_context = cm->prev_mi;

    xd->frame_type = cm->frame_type;

    xd->frames_since_golden = cm->frames_since_golden;
    xd->frames_till_alt_ref_frame = cm->frames_till_alt_ref_frame;

    // reset intra mode contexts
    if (cm->frame_type == KEY_FRAME)
        vp8_init_mbmode_probs(cm);

    // Copy data over into macro block data sturctures.
    x->src = * cpi->Source;
    xd->pre = cm->yv12_fb[cm->lst_fb_idx];
    xd->dst = cm->yv12_fb[cm->new_fb_idx];

    // set up frame for intra coded blocks
    vp8_setup_intra_recon(&cm->yv12_fb[cm->new_fb_idx]);

    vp8_build_block_offsets(x);

    vp8_setup_block_dptrs(&x->e_mbd);

    vp8_setup_block_ptrs(x);

    xd->mode_info_context->mbmi.mode = DC_PRED;
    xd->mode_info_context->mbmi.uv_mode = DC_PRED;

    xd->left_context = &cm->left_context;

    vp8_zero(cpi->count_mb_ref_frame_usage)
    vp8_zero(cpi->ymode_count)
    vp8_zero(cpi->uv_mode_count)

    x->mvc = cm->fc.mvc;
#if CONFIG_HIGH_PRECISION_MV
    x->mvc_hp = cm->fc.mvc_hp;
#endif

    vpx_memset(cm->above_context, 0,
               sizeof(ENTROPY_CONTEXT_PLANES) * cm->mb_cols);

    xd->fullpixel_mask = 0xffffffff;
    if(cm->full_pixel)
        xd->fullpixel_mask = 0xfffffff8;
}

static void encode_frame_internal(VP8_COMP *cpi)
{
    int mb_row;
    MACROBLOCK *const x = & cpi->mb;
    VP8_COMMON *const cm = & cpi->common;
    MACROBLOCKD *const xd = & x->e_mbd;

    TOKENEXTRA *tp = cpi->tok;
    int totalrate;

    // Compute a modified set of reference frame probabilities to use when
    // prediction fails. These are based on the current genreal estimates for
    // this frame which may be updated with each itteration of the recode loop.
    compute_mod_refprobs( cm );

// debug output
#if DBG_PRNT_SEGMAP
    {
        FILE *statsfile;
        statsfile = fopen("segmap2.stt", "a");
        fprintf(statsfile, "\n" );
        fclose(statsfile);
    }
#endif

    totalrate = 0;

    // Functions setup for all frame types so we can use MC in AltRef
    if (cm->mcomp_filter_type == SIXTAP)
    {
        xd->subpixel_predict        = SUBPIX_INVOKE(
                                        &cpi->common.rtcd.subpix, sixtap4x4);
        xd->subpixel_predict8x4     = SUBPIX_INVOKE(
                                        &cpi->common.rtcd.subpix, sixtap8x4);
        xd->subpixel_predict8x8     = SUBPIX_INVOKE(
                                        &cpi->common.rtcd.subpix, sixtap8x8);
        xd->subpixel_predict16x16   = SUBPIX_INVOKE(
                                        &cpi->common.rtcd.subpix, sixtap16x16);
        xd->subpixel_predict_avg8x8 = SUBPIX_INVOKE(
                                        &cpi->common.rtcd.subpix, sixtap_avg8x8);
        xd->subpixel_predict_avg16x16 = SUBPIX_INVOKE(
                                        &cpi->common.rtcd.subpix, sixtap_avg16x16);
    }
    else
    {
        xd->subpixel_predict        = SUBPIX_INVOKE(
                                        &cpi->common.rtcd.subpix, bilinear4x4);
        xd->subpixel_predict8x4     = SUBPIX_INVOKE(
                                        &cpi->common.rtcd.subpix, bilinear8x4);
        xd->subpixel_predict8x8     = SUBPIX_INVOKE(
                                        &cpi->common.rtcd.subpix, bilinear8x8);
        xd->subpixel_predict16x16   = SUBPIX_INVOKE(
                                      &cpi->common.rtcd.subpix, bilinear16x16);
        xd->subpixel_predict_avg8x8 = SUBPIX_INVOKE(
                                      &cpi->common.rtcd.subpix, bilinear_avg8x8);
        xd->subpixel_predict_avg16x16 = SUBPIX_INVOKE(
                                      &cpi->common.rtcd.subpix, bilinear_avg16x16);
    }

    // Reset frame count of inter 0,0 motion vector usage.
    cpi->inter_zz_count = 0;

    cpi->prediction_error = 0;
    cpi->intra_error = 0;
    cpi->skip_true_count = 0;
    cpi->skip_false_count = 0;

#if 0
    // Experimental code
    cpi->frame_distortion = 0;
    cpi->last_mb_distortion = 0;
#endif

    xd->mode_info_context = cm->mi;

    xd->prev_mode_info_context = cm->prev_mi;

    vp8_zero(cpi->MVcount);
#if CONFIG_HIGH_PRECISION_MV
    vp8_zero(cpi->MVcount_hp);
#endif
    vp8_zero(cpi->coef_counts);

    vp8cx_frame_init_quantizer(cpi);

    vp8_initialize_rd_consts(cpi, cm->base_qindex + cm->y1dc_delta_q);
    vp8cx_initialize_me_consts(cpi, cm->base_qindex);

    if(cpi->oxcf.tuning == VP8_TUNE_SSIM)
    {
        // Initialize encode frame context.
        init_encode_frame_mb_context(cpi);

        // Build a frame level activity map
        build_activity_map(cpi);
    }

    // re-initencode frame context.
    init_encode_frame_mb_context(cpi);

    cpi->rd_single_diff = cpi->rd_comp_diff = cpi->rd_hybrid_diff = 0;
    vpx_memset(cpi->single_pred_count, 0, sizeof(cpi->single_pred_count));
    vpx_memset(cpi->comp_pred_count, 0, sizeof(cpi->comp_pred_count));

    {
        struct vpx_usec_timer  emr_timer;
        vpx_usec_timer_start(&emr_timer);

        {
            // for each macroblock row in the image
            for (mb_row = 0; mb_row < cm->mb_rows; mb_row++)
            {
                encode_mb_row(cpi, cm, mb_row, x, xd, &tp, &totalrate);

                // adjust to the next row of MBs
                x->src.y_buffer += 16 * x->src.y_stride - 16 * cm->mb_cols;
                x->src.u_buffer += 8 * x->src.uv_stride - 8 * cm->mb_cols;
                x->src.v_buffer += 8 * x->src.uv_stride - 8 * cm->mb_cols;
            }

            cpi->tok_count = tp - cpi->tok;

        }

        vpx_usec_timer_mark(&emr_timer);
        cpi->time_encode_mb_row += vpx_usec_timer_elapsed(&emr_timer);

    }

    // 256 rate units to the bit
    cpi->projected_frame_size = totalrate >> 8;   // projected_frame_size in units of BYTES

    // Make a note of the percentage MBs coded Intra.
    if (cm->frame_type == KEY_FRAME)
    {
        cpi->this_frame_percent_intra = 100;
    }
    else
    {
        int tot_modes;

        tot_modes = cpi->count_mb_ref_frame_usage[INTRA_FRAME]
                    + cpi->count_mb_ref_frame_usage[LAST_FRAME]
                    + cpi->count_mb_ref_frame_usage[GOLDEN_FRAME]
                    + cpi->count_mb_ref_frame_usage[ALTREF_FRAME];

        if (tot_modes)
            cpi->this_frame_percent_intra = cpi->count_mb_ref_frame_usage[INTRA_FRAME] * 100 / tot_modes;

    }

#if 0
    {
        int cnt = 0;
        int flag[2] = {0, 0};

        for (cnt = 0; cnt < MVPcount; cnt++)
        {
            if (cm->fc.pre_mvc[0][cnt] != cm->fc.mvc[0][cnt])
            {
                flag[0] = 1;
                vpx_memcpy(cm->fc.pre_mvc[0], cm->fc.mvc[0], MVPcount);
                break;
            }
        }

        for (cnt = 0; cnt < MVPcount; cnt++)
        {
            if (cm->fc.pre_mvc[1][cnt] != cm->fc.mvc[1][cnt])
            {
                flag[1] = 1;
                vpx_memcpy(cm->fc.pre_mvc[1], cm->fc.mvc[1], MVPcount);
                break;
            }
        }

        if (flag[0] || flag[1])
            vp8_build_component_cost_table(cpi->mb.mvcost, (const MV_CONTEXT *) cm->fc.mvc, flag);
    }
#endif

#if 0
    // Keep record of the total distortion this time around for future use
    cpi->last_frame_distortion = cpi->frame_distortion;
#endif

}

void vp8_encode_frame(VP8_COMP *cpi)
{
    if (cpi->sf.RD)
    {
        int frame_type, pred_type;
        int redo = 0;
        int single_diff, comp_diff, hybrid_diff;

        /*
         * This code does a single RD pass over the whole frame assuming
         * either compound, single or hybrid prediction as per whatever has
         * worked best for that type of frame in the past.
         * It also predicts whether another coding mode would have worked
         * better that this coding mode. If that is the case, it remembers
         * that for subsequent frames. If the difference is above a certain
         * threshold, it will actually re-encode the current frame using
         * that different coding mode.
         */
        if (cpi->common.frame_type == KEY_FRAME)
            frame_type = 0;
        else if (cpi->is_src_frame_alt_ref && cpi->common.refresh_golden_frame)
            frame_type = 3;
        else if (cpi->common.refresh_golden_frame || cpi->common.refresh_alt_ref_frame)
            frame_type = 1;
        else
            frame_type = 2;

        if (cpi->rd_prediction_type_threshes[frame_type][1] >
                cpi->rd_prediction_type_threshes[frame_type][0] &&
            cpi->rd_prediction_type_threshes[frame_type][1] >
                cpi->rd_prediction_type_threshes[frame_type][2])
            pred_type = COMP_PREDICTION_ONLY;
        else if (cpi->rd_prediction_type_threshes[frame_type][0] >
                    cpi->rd_prediction_type_threshes[frame_type][1] &&
                 cpi->rd_prediction_type_threshes[frame_type][0] >
                    cpi->rd_prediction_type_threshes[frame_type][2])
            pred_type = SINGLE_PREDICTION_ONLY;
        else
            pred_type = HYBRID_PREDICTION;

        cpi->common.comp_pred_mode = pred_type;
        encode_frame_internal(cpi);

        single_diff = cpi->rd_single_diff / cpi->common.MBs;
        cpi->rd_prediction_type_threshes[frame_type][0] += single_diff;
        cpi->rd_prediction_type_threshes[frame_type][0] >>= 1;
        comp_diff   = cpi->rd_comp_diff   / cpi->common.MBs;
        cpi->rd_prediction_type_threshes[frame_type][1] += comp_diff;
        cpi->rd_prediction_type_threshes[frame_type][1] >>= 1;
        hybrid_diff = cpi->rd_hybrid_diff / cpi->common.MBs;
        cpi->rd_prediction_type_threshes[frame_type][2] += hybrid_diff;
        cpi->rd_prediction_type_threshes[frame_type][2] >>= 1;

        if (cpi->common.comp_pred_mode == HYBRID_PREDICTION)
        {
            int single_count_zero = 0;
            int comp_count_zero = 0;
            int i;

            for ( i = 0; i < COMP_PRED_CONTEXTS; i++ )
            {
                single_count_zero += cpi->single_pred_count[i];
                comp_count_zero += cpi->comp_pred_count[i];
            }

            if (comp_count_zero == 0)
            {
                cpi->common.comp_pred_mode = SINGLE_PREDICTION_ONLY;
            }
            else if (single_count_zero == 0)
            {
                cpi->common.comp_pred_mode = COMP_PREDICTION_ONLY;
            }
        }
    }
    else
    {
        encode_frame_internal(cpi);
    }

}

void vp8_setup_block_ptrs(MACROBLOCK *x)
{
    int r, c;
    int i;

    for (r = 0; r < 4; r++)
    {
        for (c = 0; c < 4; c++)
        {
            x->block[r*4+c].src_diff = x->src_diff + r * 4 * 16 + c * 4;
        }
    }

    for (r = 0; r < 2; r++)
    {
        for (c = 0; c < 2; c++)
        {
            x->block[16 + r*2+c].src_diff = x->src_diff + 256 + r * 4 * 8 + c * 4;
        }
    }


    for (r = 0; r < 2; r++)
    {
        for (c = 0; c < 2; c++)
        {
            x->block[20 + r*2+c].src_diff = x->src_diff + 320 + r * 4 * 8 + c * 4;
        }
    }

    x->block[24].src_diff = x->src_diff + 384;


    for (i = 0; i < 25; i++)
    {
        x->block[i].coeff = x->coeff + i * 16;
    }
}

void vp8_build_block_offsets(MACROBLOCK *x)
{
    int block = 0;
    int br, bc;

    vp8_build_block_doffsets(&x->e_mbd);

    // y blocks
    x->thismb_ptr = &x->thismb[0];
    for (br = 0; br < 4; br++)
    {
        for (bc = 0; bc < 4; bc++)
        {
            BLOCK *this_block = &x->block[block];
            //this_block->base_src = &x->src.y_buffer;
            //this_block->src_stride = x->src.y_stride;
            //this_block->src = 4 * br * this_block->src_stride + 4 * bc;
            this_block->base_src = &x->thismb_ptr;
            this_block->src_stride = 16;
            this_block->src = 4 * br * 16 + 4 * bc;
            ++block;
        }
    }

    // u blocks
    for (br = 0; br < 2; br++)
    {
        for (bc = 0; bc < 2; bc++)
        {
            BLOCK *this_block = &x->block[block];
            this_block->base_src = &x->src.u_buffer;
            this_block->src_stride = x->src.uv_stride;
            this_block->src = 4 * br * this_block->src_stride + 4 * bc;
            ++block;
        }
    }

    // v blocks
    for (br = 0; br < 2; br++)
    {
        for (bc = 0; bc < 2; bc++)
        {
            BLOCK *this_block = &x->block[block];
            this_block->base_src = &x->src.v_buffer;
            this_block->src_stride = x->src.uv_stride;
            this_block->src = 4 * br * this_block->src_stride + 4 * bc;
            ++block;
        }
    }
}

static void sum_intra_stats(VP8_COMP *cpi, MACROBLOCK *x)
{
    const MACROBLOCKD *xd = & x->e_mbd;
    const MB_PREDICTION_MODE m = xd->mode_info_context->mbmi.mode;
    const MB_PREDICTION_MODE uvm = xd->mode_info_context->mbmi.uv_mode;

#ifdef MODE_STATS
    const int is_key = cpi->common.frame_type == KEY_FRAME;

    ++ (is_key ? uv_modes : inter_uv_modes)[uvm];
    ++ uv_modes_y[m][uvm];

    if (m == B_PRED)
    {
        unsigned int *const bct = is_key ? b_modes : inter_b_modes;

        int b = 0;

        do
        {
            ++ bct[xd->block[b].bmi.as_mode.first];
        }
        while (++b < 16);
    }

    if(m==I8X8_PRED)
    {
        i8x8_modes[xd->block[0].bmi.as_mode.first]++;
        i8x8_modes[xd->block[2].bmi.as_mode.first]++;
        i8x8_modes[xd->block[8].bmi.as_mode.first]++;
        i8x8_modes[xd->block[10].bmi.as_mode.first]++;
    }
#endif

    ++cpi->ymode_count[m];
    ++cpi->uv_mode_count[uvm];

}

// Experimental stub function to create a per MB zbin adjustment based on
// some previously calculated measure of MB activity.
static void adjust_act_zbin( VP8_COMP *cpi, MACROBLOCK *x )
{
#if USE_ACT_INDEX
    x->act_zbin_adj = *(x->mb_activity_ptr);
#else
    int64_t a;
    int64_t b;
    int64_t act = *(x->mb_activity_ptr);

    // Apply the masking to the RD multiplier.
    a = act + 4*cpi->activity_avg;
    b = 4*act + cpi->activity_avg;

    if ( act > cpi->activity_avg )
        x->act_zbin_adj = (int)(((int64_t)b + (a>>1))/a) - 1;
    else
        x->act_zbin_adj = 1 - (int)(((int64_t)a + (b>>1))/b);
#endif
}

int vp8cx_encode_intra_macro_block(VP8_COMP *cpi, MACROBLOCK *x, TOKENEXTRA **t)
{
    int rate;

    // Non rd path deprecated in test code base
    //if (cpi->sf.RD && cpi->compressor_speed != 2)
    vp8_rd_pick_intra_mode(cpi, x, &rate);
    //else
    //   vp8_pick_intra_mode(cpi, x, &rate);

    if(cpi->oxcf.tuning == VP8_TUNE_SSIM)
    {
        adjust_act_zbin( cpi, x );
        vp8_update_zbin_extra(cpi, x);
    }

    /* test code: set transform size based on mode selection */
    if(cpi->common.txfm_mode == ALLOW_8X8
        && x->e_mbd.mode_info_context->mbmi.mode != I8X8_PRED
        && x->e_mbd.mode_info_context->mbmi.mode != B_PRED)
    {
        x->e_mbd.mode_info_context->mbmi.txfm_size = TX_8X8;
        cpi->t8x8_count++;
    }
    else
    {
        x->e_mbd.mode_info_context->mbmi.txfm_size = TX_4X4;
        cpi->t4x4_count ++;
    }

    if(x->e_mbd.mode_info_context->mbmi.mode == I8X8_PRED)
    {
        vp8_encode_intra8x8mby(IF_RTCD(&cpi->rtcd), x);
        vp8_encode_intra8x8mbuv(IF_RTCD(&cpi->rtcd), x);
    }
    else if (x->e_mbd.mode_info_context->mbmi.mode == B_PRED)
        vp8_encode_intra4x4mby(IF_RTCD(&cpi->rtcd), x);
    else
        vp8_encode_intra16x16mby(IF_RTCD(&cpi->rtcd), x);

    if(x->e_mbd.mode_info_context->mbmi.mode != I8X8_PRED)
        vp8_encode_intra16x16mbuv(IF_RTCD(&cpi->rtcd), x);
    sum_intra_stats(cpi, x);
    vp8_tokenize_mb(cpi, &x->e_mbd, t);

    return rate;
}
#ifdef SPEEDSTATS
extern int cnt_pm;
#endif

extern void vp8_fix_contexts(MACROBLOCKD *x);

int vp8cx_encode_inter_macroblock
(
    VP8_COMP *cpi, MACROBLOCK *x, TOKENEXTRA **t,
    int recon_yoffset, int recon_uvoffset
)
{
    VP8_COMMON *cm = &cpi->common;
    MACROBLOCKD *const xd = &x->e_mbd;
    int intra_error = 0;
    int rate;
    int distortion;
    unsigned char *segment_id = &xd->mode_info_context->mbmi.segment_id;
    int seg_ref_active;
     unsigned char ref_pred_flag;

    x->skip = 0;

    if (xd->segmentation_enabled)
        x->encode_breakout = cpi->segment_encode_breakout[*segment_id];
    else
        x->encode_breakout = cpi->oxcf.encode_breakout;

    //if (cpi->sf.RD)
    // For now this codebase is limited to a single rd encode path
    {
        int zbin_mode_boost_enabled = cpi->zbin_mode_boost_enabled;
        int single, compound, hybrid;

        /* Are we using the fast quantizer for the mode selection? */
        if(cpi->sf.use_fastquant_for_pick)
        {
            cpi->mb.quantize_b      = QUANTIZE_INVOKE(&cpi->rtcd.quantize,
                                                      fastquantb);
            cpi->mb.quantize_b_pair = QUANTIZE_INVOKE(&cpi->rtcd.quantize,
                                                      fastquantb_pair);

            /* the fast quantizer does not use zbin_extra, so
             * do not recalculate */
            cpi->zbin_mode_boost_enabled = 0;
        }
        vp8_rd_pick_inter_mode(cpi, x, recon_yoffset, recon_uvoffset, &rate,
                               &distortion, &intra_error, &single, &compound, &hybrid);

        cpi->rd_single_diff += single;
        cpi->rd_comp_diff   += compound;
        cpi->rd_hybrid_diff += hybrid;
        if (x->e_mbd.mode_info_context->mbmi.ref_frame &&
            x->e_mbd.mode_info_context->mbmi.mode != SPLITMV)
        {
            unsigned char pred_context;

            pred_context = get_pred_context( cm, xd, PRED_COMP );

            if (xd->mode_info_context->mbmi.second_ref_frame == INTRA_FRAME)
                cpi->single_pred_count[pred_context]++;
            else
                cpi->comp_pred_count[pred_context]++;
        }


        /* test code: set transform size based on mode selection */
        if( cpi->common.txfm_mode == ALLOW_8X8
            && x->e_mbd.mode_info_context->mbmi.mode != I8X8_PRED
            && x->e_mbd.mode_info_context->mbmi.mode != B_PRED
            && x->e_mbd.mode_info_context->mbmi.mode != SPLITMV)
        {
            x->e_mbd.mode_info_context->mbmi.txfm_size = TX_8X8;
            cpi->t8x8_count ++;
        }
        else
        {
            x->e_mbd.mode_info_context->mbmi.txfm_size = TX_4X4;
            cpi->t4x4_count++;
        }

        /* switch back to the regular quantizer for the encode */
        if (cpi->sf.improved_quant)
        {
            cpi->mb.quantize_b      = QUANTIZE_INVOKE(&cpi->rtcd.quantize,
                                                      quantb);
            cpi->mb.quantize_b_pair = QUANTIZE_INVOKE(&cpi->rtcd.quantize,
                                                      quantb_pair);
        }
        /* restore cpi->zbin_mode_boost_enabled */
        cpi->zbin_mode_boost_enabled = zbin_mode_boost_enabled;

    }
    //else
    // The non rd encode path has been deleted from this code base
    // to simplify development
    //    vp8_pick_inter_mode

    cpi->prediction_error += distortion;
    cpi->intra_error += intra_error;

    if(cpi->oxcf.tuning == VP8_TUNE_SSIM)
    {
        // Adjust the zbin based on this MB rate.
        adjust_act_zbin( cpi, x );
    }

    {
        // Experimental code. Special case for gf and arf zeromv modes.
        // Increase zbin size to supress noise
        cpi->zbin_mode_boost = 0;
        if (cpi->zbin_mode_boost_enabled)
        {
            if ( xd->mode_info_context->mbmi.ref_frame != INTRA_FRAME )
            {
                if (xd->mode_info_context->mbmi.mode == ZEROMV)
                {
                    if (xd->mode_info_context->mbmi.ref_frame != LAST_FRAME)
                        cpi->zbin_mode_boost = GF_ZEROMV_ZBIN_BOOST;
                    else
                        cpi->zbin_mode_boost = LF_ZEROMV_ZBIN_BOOST;
                }
                else if (xd->mode_info_context->mbmi.mode == SPLITMV)
                    cpi->zbin_mode_boost = 0;
                else
                    cpi->zbin_mode_boost = MV_ZBIN_BOOST;
            }
        }

        /* The fast quantizer doesn't use zbin_extra, only do so with
         * the regular quantizer. */
        if (cpi->sf.improved_quant)
            vp8_update_zbin_extra(cpi, x);
    }

    seg_ref_active = segfeature_active( xd, *segment_id, SEG_LVL_REF_FRAME );

    // SET VARIOUS PREDICTION FLAGS

    // Did the chosen reference frame match its predicted value.
    ref_pred_flag = ( (xd->mode_info_context->mbmi.ref_frame ==
                           get_pred_ref( cm, xd )) );
    set_pred_flag( xd, PRED_REF, ref_pred_flag );

    // If we have just a single reference frame coded for a segment then
    // exclude from the reference frame counts used to work out
    // probabilities. NOTE: At the moment we dont support custom trees
    // for the reference frame coding for each segment but this is a
    // possible future action.
    if ( !seg_ref_active ||
         ( ( check_segref( xd, *segment_id, INTRA_FRAME ) +
             check_segref( xd, *segment_id, LAST_FRAME ) +
             check_segref( xd, *segment_id, GOLDEN_FRAME ) +
             check_segref( xd, *segment_id, ALTREF_FRAME ) ) > 1 ) )
    {
// TODO this may not be a good idea as it makes sample size small and means
// the predictor functions cannot use data about most likely value only most
// likely unpredicted value.
//#if CONFIG_COMPRED
//        // Only update count for incorrectly predicted cases
//        if ( !ref_pred_flag )
//#endif
        {
            cpi->count_mb_ref_frame_usage
                [xd->mode_info_context->mbmi.ref_frame]++;
        }
    }

    if (xd->mode_info_context->mbmi.ref_frame == INTRA_FRAME)
    {
        if (xd->mode_info_context->mbmi.mode == B_PRED)
        {
            vp8_encode_intra16x16mbuv(IF_RTCD(&cpi->rtcd), x);
            vp8_encode_intra4x4mby(IF_RTCD(&cpi->rtcd), x);
        }
        else if(xd->mode_info_context->mbmi.mode == I8X8_PRED)
        {
            vp8_encode_intra8x8mby(IF_RTCD(&cpi->rtcd), x);
            vp8_encode_intra8x8mbuv(IF_RTCD(&cpi->rtcd), x);
        }
        else
        {
            vp8_encode_intra16x16mbuv(IF_RTCD(&cpi->rtcd), x);
            vp8_encode_intra16x16mby(IF_RTCD(&cpi->rtcd), x);
        }
        sum_intra_stats(cpi, x);
    }
    else
    {
        int ref_fb_idx;

        if (xd->mode_info_context->mbmi.ref_frame == LAST_FRAME)
            ref_fb_idx = cpi->common.lst_fb_idx;
        else if (xd->mode_info_context->mbmi.ref_frame == GOLDEN_FRAME)
            ref_fb_idx = cpi->common.gld_fb_idx;
        else
            ref_fb_idx = cpi->common.alt_fb_idx;

        xd->pre.y_buffer = cpi->common.yv12_fb[ref_fb_idx].y_buffer + recon_yoffset;
        xd->pre.u_buffer = cpi->common.yv12_fb[ref_fb_idx].u_buffer + recon_uvoffset;
        xd->pre.v_buffer = cpi->common.yv12_fb[ref_fb_idx].v_buffer + recon_uvoffset;

        if (xd->mode_info_context->mbmi.second_ref_frame) {
            int second_ref_fb_idx;

            if (xd->mode_info_context->mbmi.second_ref_frame == LAST_FRAME)
                second_ref_fb_idx = cpi->common.lst_fb_idx;
            else if (xd->mode_info_context->mbmi.second_ref_frame == GOLDEN_FRAME)
                second_ref_fb_idx = cpi->common.gld_fb_idx;
            else
                second_ref_fb_idx = cpi->common.alt_fb_idx;

            xd->second_pre.y_buffer = cpi->common.yv12_fb[second_ref_fb_idx].y_buffer +
                                            recon_yoffset;
            xd->second_pre.u_buffer = cpi->common.yv12_fb[second_ref_fb_idx].u_buffer +
                                            recon_uvoffset;
            xd->second_pre.v_buffer = cpi->common.yv12_fb[second_ref_fb_idx].v_buffer +
                                            recon_uvoffset;
        }

        if (!x->skip)
        {
            vp8_encode_inter16x16(IF_RTCD(&cpi->rtcd), x);

            // Clear mb_skip_coeff if mb_no_coeff_skip is not set
            if (!cpi->common.mb_no_coeff_skip)
                xd->mode_info_context->mbmi.mb_skip_coeff = 0;

        }
        else
        {
            vp8_build_inter16x16_predictors_mb(xd, xd->dst.y_buffer,
                                           xd->dst.u_buffer, xd->dst.v_buffer,
                                           xd->dst.y_stride, xd->dst.uv_stride);
        }
    }

    if (!x->skip)
    {
#ifdef ENC_DEBUG
        if (enc_debug)
        {
          int i;
            printf("Segment=%d [%d, %d]: %d %d:\n", x->e_mbd.mode_info_context->mbmi.segment_id, mb_col_debug, mb_row_debug, xd->mb_to_left_edge, xd->mb_to_top_edge);
            for (i =0; i<400; i++) {
              printf("%3d ", xd->qcoeff[i]);
              if (i%16 == 15) printf("\n");
            }
            printf("\n");
            printf("eobs = ");
            for (i=0;i<25;i++)
              printf("%d:%d ", i, xd->block[i].eob);
            printf("\n");
            fflush(stdout);
        }
#endif
        vp8_tokenize_mb(cpi, xd, t);
#ifdef ENC_DEBUG
        if (enc_debug) {
          printf("Tokenized\n");
          fflush(stdout);
        }
#endif
    }
    else
    {
        if (cpi->common.mb_no_coeff_skip)
        {
            xd->mode_info_context->mbmi.mb_skip_coeff = 1;
            cpi->skip_true_count ++;
            vp8_fix_contexts(xd);
        }
        else
        {
            vp8_stuff_mb(cpi, xd, t);
            xd->mode_info_context->mbmi.mb_skip_coeff = 0;
            cpi->skip_false_count ++;
        }
    }
    return rate;
}
