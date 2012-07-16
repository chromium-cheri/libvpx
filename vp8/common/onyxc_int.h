/*
 *  Copyright (c) 2010 The WebM project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */


#ifndef __INC_VP8C_INT_H
#define __INC_VP8C_INT_H

#include "vpx_config.h"
#include "vpx/internal/vpx_codec_internal.h"
#include "loopfilter.h"
#include "entropymv.h"
#include "entropy.h"
#include "idct.h"
#include "recon.h"
#if CONFIG_POSTPROC
#include "postproc.h"
#endif

/*#ifdef PACKET_TESTING*/
#include "header.h"
/*#endif*/

/* Create/destroy static data structures. */

void vp8_initialize_common(void);

#define MINQ 0

#define MAXQ 255
#define QINDEX_BITS 8

#define QINDEX_RANGE (MAXQ + 1)

#define NUM_YV12_BUFFERS 4

#define COMP_PRED_CONTEXTS   2

typedef struct frame_contexts {
  vp8_prob bmode_prob [VP8_BINTRAMODES - 1];
  vp8_prob ymode_prob [VP8_YMODES - 1]; /* interframe intra mode probs */
  vp8_prob uv_mode_prob [VP8_YMODES][VP8_UV_MODES - 1];
  vp8_prob i8x8_mode_prob [VP8_I8X8_MODES - 1];
  vp8_prob sub_mv_ref_prob [SUBMVREF_COUNT][VP8_SUBMVREFS - 1];
  vp8_prob mbsplit_prob [VP8_NUMMBSPLITS - 1];
  vp8_prob coef_probs [BLOCK_TYPES] [COEF_BANDS] [PREV_COEF_CONTEXTS] [ENTROPY_NODES];
  vp8_prob coef_probs_8x8 [BLOCK_TYPES_8X8] [COEF_BANDS] [PREV_COEF_CONTEXTS] [ENTROPY_NODES];
  MV_CONTEXT mvc[2];
#if CONFIG_HIGH_PRECISION_MV
  MV_CONTEXT_HP mvc_hp[2];
#endif
#if CONFIG_ADAPTIVE_ENTROPY
  MV_CONTEXT pre_mvc[2];
#if CONFIG_HIGH_PRECISION_MV
  MV_CONTEXT_HP pre_mvc_hp[2];
#endif
  vp8_prob pre_bmode_prob [VP8_BINTRAMODES - 1];
  vp8_prob pre_ymode_prob [VP8_YMODES - 1]; /* interframe intra mode probs */
  vp8_prob pre_uv_mode_prob [VP8_YMODES][VP8_UV_MODES - 1];
  vp8_prob pre_i8x8_mode_prob [VP8_I8X8_MODES - 1];
  vp8_prob pre_sub_mv_ref_prob [SUBMVREF_COUNT][VP8_SUBMVREFS - 1];
  vp8_prob pre_mbsplit_prob [VP8_NUMMBSPLITS - 1];
  unsigned int bmode_counts [VP8_BINTRAMODES];
  unsigned int ymode_counts [VP8_YMODES];   /* interframe intra mode probs */
  unsigned int uv_mode_counts [VP8_YMODES][VP8_UV_MODES];
  unsigned int i8x8_mode_counts [VP8_I8X8_MODES];   /* interframe intra mode probs */
  unsigned int sub_mv_ref_counts [SUBMVREF_COUNT][VP8_SUBMVREFS];
  unsigned int mbsplit_counts [VP8_NUMMBSPLITS];

  vp8_prob pre_coef_probs [BLOCK_TYPES] [COEF_BANDS] [PREV_COEF_CONTEXTS] [ENTROPY_NODES];
  vp8_prob pre_coef_probs_8x8 [BLOCK_TYPES_8X8] [COEF_BANDS] [PREV_COEF_CONTEXTS] [ENTROPY_NODES];
  unsigned int coef_counts [BLOCK_TYPES] [COEF_BANDS]
  [PREV_COEF_CONTEXTS] [MAX_ENTROPY_TOKENS];
  unsigned int coef_counts_8x8 [BLOCK_TYPES_8X8] [COEF_BANDS]
  [PREV_COEF_CONTEXTS] [MAX_ENTROPY_TOKENS];
  unsigned int MVcount [2] [MVvals];
#if CONFIG_HIGH_PRECISION_MV
  unsigned int MVcount_hp [2] [MVvals_hp];
#endif
#endif  /* CONFIG_ADAPTIVE_ENTROPY */
  int mode_context[6][4];
  int mode_context_a[6][4];
  int vp8_mode_contexts[6][4];
  int mv_ref_ct[6][4][2];
  int mv_ref_ct_a[6][4][2];
} FRAME_CONTEXT;

typedef enum {
  RECON_CLAMP_REQUIRED        = 0,
  RECON_CLAMP_NOTREQUIRED     = 1
} CLAMP_TYPE;

typedef enum {
  SIXTAP   = 0,
  BILINEAR = 1,
#if CONFIG_ENHANCED_INTERP
  EIGHTTAP = 2,
  EIGHTTAP_SHARP = 3,
#endif
} INTERPOLATIONFILTERTYPE;

typedef enum {
  SINGLE_PREDICTION_ONLY = 0,
  COMP_PREDICTION_ONLY   = 1,
  HYBRID_PREDICTION      = 2,
  NB_PREDICTION_TYPES    = 3,
} COMPPREDMODE_TYPE;

/* TODO: allows larger transform */
typedef enum {
  ONLY_4X4            = 0,
  ALLOW_8X8           = 1
} TXFM_MODE;

typedef struct VP8_COMMON_RTCD {
#if CONFIG_RUNTIME_CPU_DETECT
  vp8_idct_rtcd_vtable_t        idct;
  vp8_recon_rtcd_vtable_t       recon;
  vp8_subpix_rtcd_vtable_t      subpix;
  vp8_loopfilter_rtcd_vtable_t  loopfilter;
#if CONFIG_POSTPROC
  vp8_postproc_rtcd_vtable_t    postproc;
#endif
  int                           flags;
#else
  int unused;
#endif
} VP8_COMMON_RTCD;

typedef struct VP8Common {
  struct vpx_internal_error_info  error;

  DECLARE_ALIGNED(16, short, Y1dequant[QINDEX_RANGE][16]);
  DECLARE_ALIGNED(16, short, Y2dequant[QINDEX_RANGE][16]);
  DECLARE_ALIGNED(16, short, UVdequant[QINDEX_RANGE][16]);

  int Width;
  int Height;
  int horiz_scale;
  int vert_scale;

  YUV_TYPE clr_type;
  CLAMP_TYPE  clamp_type;

  YV12_BUFFER_CONFIG *frame_to_show;

  YV12_BUFFER_CONFIG yv12_fb[NUM_YV12_BUFFERS];
  int fb_idx_ref_cnt[NUM_YV12_BUFFERS];
  int new_fb_idx, lst_fb_idx, gld_fb_idx, alt_fb_idx;

  YV12_BUFFER_CONFIG post_proc_buffer;
  YV12_BUFFER_CONFIG temp_scale_frame;


  FRAME_TYPE last_frame_type;  /* Save last frame's frame type for motion search. */
  FRAME_TYPE frame_type;

  int show_frame;

  int frame_flags;
  int MBs;
  int mb_rows;
  int mb_cols;
  int mode_info_stride;

  /* profile settings */
  int experimental;
  int mb_no_coeff_skip;
  TXFM_MODE txfm_mode;
  COMPPREDMODE_TYPE comp_pred_mode;
  int no_lpf;
  int use_bilinear_mc_filter;
  int full_pixel;

  int base_qindex;
  int last_kf_gf_q;  /* Q used on the last GF or KF */

  int y1dc_delta_q;
  int y2dc_delta_q;
  int y2ac_delta_q;
  int uvdc_delta_q;
  int uvac_delta_q;

  unsigned int frames_since_golden;
  unsigned int frames_till_alt_ref_frame;

  /* We allocate a MODE_INFO struct for each macroblock, together with
     an extra row on top and column on the left to simplify prediction. */

  MODE_INFO *mip; /* Base of allocated array */
  MODE_INFO *mi;  /* Corresponds to upper left visible macroblock */
  MODE_INFO *prev_mip; /* MODE_INFO array 'mip' from last decoded frame */
  MODE_INFO *prev_mi;  /* 'mi' from last frame (points into prev_mip) */


  // Persistent mb segment id map used in prediction.
  unsigned char *last_frame_seg_map;

  INTERPOLATIONFILTERTYPE mcomp_filter_type;
  LOOPFILTERTYPE filter_type;

  loop_filter_info_n lf_info;

  int filter_level;
  int last_sharpness_level;
  int sharpness_level;

  int refresh_last_frame;       /* Two state 0 = NO, 1 = YES */
  int refresh_golden_frame;     /* Two state 0 = NO, 1 = YES */
  int refresh_alt_ref_frame;     /* Two state 0 = NO, 1 = YES */

  int copy_buffer_to_gf;         /* 0 none, 1 Last to GF, 2 ARF to GF */
  int copy_buffer_to_arf;        /* 0 none, 1 Last to ARF, 2 GF to ARF */

  int refresh_entropy_probs;    /* Two state 0 = NO, 1 = YES */

  int ref_frame_sign_bias[MAX_REF_FRAMES];    /* Two state 0, 1 */

  /* Y,U,V,Y2 */
  ENTROPY_CONTEXT_PLANES *above_context;   /* row of context for each plane */
  ENTROPY_CONTEXT_PLANES left_context;  /* (up to) 4 contexts "" */

  /* keyframe block modes are predicted by their above, left neighbors */

  vp8_prob kf_bmode_prob [VP8_BINTRAMODES] [VP8_BINTRAMODES] [VP8_BINTRAMODES - 1];
  vp8_prob kf_ymode_prob[8][VP8_YMODES - 1]; /* keyframe "" */
  int kf_ymode_probs_index;
  int kf_ymode_probs_update;
  vp8_prob kf_uv_mode_prob[VP8_YMODES] [VP8_UV_MODES - 1];

  vp8_prob prob_intra_coded;
  vp8_prob prob_last_coded;
  vp8_prob prob_gf_coded;

  // Context probabilities when using predictive coding of segment id
  vp8_prob segment_pred_probs[PREDICTION_PROBS];
  unsigned char temporal_update;

  // Context probabilities for reference frame prediction
  unsigned char ref_scores[MAX_REF_FRAMES];
  vp8_prob ref_pred_probs[PREDICTION_PROBS];
  vp8_prob mod_refprobs[MAX_REF_FRAMES][PREDICTION_PROBS];

  vp8_prob prob_comppred[COMP_PRED_CONTEXTS];

#if CONFIG_NEWENTROPY
  vp8_prob mbskip_pred_probs[MBSKIP_CONTEXTS];
#endif

  FRAME_CONTEXT lfc_a; /* last alt ref entropy */
  FRAME_CONTEXT lfc; /* last frame entropy */
  FRAME_CONTEXT fc;  /* this frame entropy */

  // int mv_ref_ct[6][4][2];
  // int mv_ref_ct_a[6][4][2];
  // int mode_context[6][4];
  // int mode_context_a[6][4];
  // int vp8_mode_contexts[6][4];

  unsigned int current_video_frame;
  int near_boffset[3];
  int version;

#ifdef PACKET_TESTING
  VP8_HEADER oh;
#endif
  double bitrate;
  double framerate;

#if CONFIG_RUNTIME_CPU_DETECT
  VP8_COMMON_RTCD rtcd;
#endif

#if CONFIG_POSTPROC
  struct postproc_state  postproc_state;
#endif

#if CONFIG_PRED_FILTER
  /* Prediction filter variables */
  int pred_filter_mode;   // 0=disabled at the frame level (no MB filtered)
  // 1=enabled at the frame level (all MB filtered)
  // 2=specified per MB (1=filtered, 0=non-filtered)
  vp8_prob prob_pred_filter_off;
#endif

} VP8_COMMON;

#endif
