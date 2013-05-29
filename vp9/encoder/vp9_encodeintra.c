/*
 *  Copyright (c) 2010 The WebM project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "./vpx_config.h"
#include "vp9_rtcd.h"
#include "vp9/encoder/vp9_quantize.h"
#include "vp9/common/vp9_reconintra.h"
#include "vp9/encoder/vp9_encodemb.h"
#include "vp9/common/vp9_invtrans.h"
#include "vp9/encoder/vp9_encodeintra.h"

static void encode_intra4x4block(MACROBLOCK *x, int ib, BLOCK_SIZE_TYPE bs);

int vp9_encode_intra(VP9_COMP *cpi, MACROBLOCK *x, int use_16x16_pred) {
  MB_MODE_INFO * mbmi = &x->e_mbd.mode_info_context->mbmi;
  (void) cpi;

  mbmi->mode = DC_PRED;
  mbmi->uv_mode = DC_PRED;
  mbmi->ref_frame = INTRA_FRAME;

  if (use_16x16_pred)
    mbmi->txfm_size = TX_16X16;
  else
    mbmi->txfm_size = TX_4X4;
  vp9_encode_intra_block_y(&cpi->common, x, BLOCK_SIZE_MB16X16);
  return vp9_get_mb_ss(x->plane[0].src_diff);
}
