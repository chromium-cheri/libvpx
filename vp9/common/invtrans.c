/*
 *  Copyright (c) 2010 The WebM project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "invtrans.h"

static void recon_dcblock(MACROBLOCKD *xd) {
  BLOCKD *b = &xd->block[24];
  int i;

  for (i = 0; i < 16; i++) {
    xd->block[i].dqcoeff[0] = b->diff[i];
  }
}

static void recon_dcblock_8x8(MACROBLOCKD *xd) {
  BLOCKD *b = &xd->block[24]; // for coeff 0, 2, 8, 10

  xd->block[0].dqcoeff[0] = b->diff[0];
  xd->block[4].dqcoeff[0] = b->diff[1];
  xd->block[8].dqcoeff[0] = b->diff[4];
  xd->block[12].dqcoeff[0] = b->diff[8];
}

void vp9_inverse_transform_b_4x4(const vp9_idct_rtcd_vtable_t *rtcd,
                                 BLOCKD *b, int pitch) {
  if (b->eob <= 1)
    IDCT_INVOKE(rtcd, idct1)(b->dqcoeff, b->diff, pitch);
  else
    IDCT_INVOKE(rtcd, idct16)(b->dqcoeff, b->diff, pitch);
}

void vp9_inverse_transform_mby_4x4(const vp9_idct_rtcd_vtable_t *rtcd,
                                   MACROBLOCKD *xd) {
  int i;
  BLOCKD *blockd = xd->block;
  int has_2nd_order = get_2nd_order_usage(xd);

  if (has_2nd_order) {
    /* do 2nd order transform on the dc block */
    IDCT_INVOKE(rtcd, iwalsh16)(blockd[24].dqcoeff, blockd[24].diff);
    recon_dcblock(xd);
  }

  for (i = 0; i < 16; i++) {
    TX_TYPE tx_type = get_tx_type_4x4(xd, &xd->block[i]);
    if (tx_type != DCT_DCT) {
      vp9_ihtllm_c(xd->block[i].dqcoeff, xd->block[i].diff, 32,
                   tx_type, 4);
    } else {
      vp9_inverse_transform_b_4x4(rtcd, &blockd[i], 32);
    }
  }
}

void vp9_inverse_transform_mbuv_4x4(const vp9_idct_rtcd_vtable_t *rtcd,
                                    MACROBLOCKD *xd) {
  int i;
  BLOCKD *blockd = xd->block;

  for (i = 16; i < 24; i++) {
    vp9_inverse_transform_b_4x4(rtcd, &blockd[i], 16);
  }
}

void vp9_inverse_transform_mb_4x4(const vp9_idct_rtcd_vtable_t *rtcd,
                                  MACROBLOCKD *xd) {
  vp9_inverse_transform_mby_4x4(rtcd, xd);
  vp9_inverse_transform_mbuv_4x4(rtcd, xd);
}

void vp9_inverse_transform_b_8x8(const vp9_idct_rtcd_vtable_t *rtcd,
                                 short *input_dqcoeff, short *output_coeff,
                                 int pitch) {
  // int b,i;
  // if (b->eob > 1)
  IDCT_INVOKE(rtcd, idct8)(input_dqcoeff, output_coeff, pitch);
  // else
  // IDCT_INVOKE(rtcd, idct8_1)(b->dqcoeff, b->diff, pitch);//pitch
}

void vp9_inverse_transform_mby_8x8(const vp9_idct_rtcd_vtable_t *rtcd,
                                   MACROBLOCKD *xd) {
  int i;
  BLOCKD *blockd = xd->block;
  int has_2nd_order = get_2nd_order_usage(xd);

  if (has_2nd_order) {
    // do 2nd order transform on the dc block
    IDCT_INVOKE(rtcd, ihaar2)(blockd[24].dqcoeff, blockd[24].diff, 8);
    recon_dcblock_8x8(xd); // need to change for 8x8
  }

  for (i = 0; i < 9; i += 8) {
    TX_TYPE tx_type = get_tx_type_8x8(xd, &xd->block[i]);
    if (tx_type != DCT_DCT) {
      vp9_ihtllm_c(xd->block[i].dqcoeff, xd->block[i].diff, 32, tx_type, 8);
    } else {
      vp9_inverse_transform_b_8x8(rtcd, &blockd[i].dqcoeff[0],
                                  &blockd[i].diff[0], 32);
    }
  }
  for (i = 2; i < 11; i += 8) {
    TX_TYPE tx_type = get_tx_type_8x8(xd, &xd->block[i]);
    if (tx_type != DCT_DCT) {
      vp9_ihtllm_c(xd->block[i + 2].dqcoeff, xd->block[i].diff, 32, tx_type, 8);
    } else {
      vp9_inverse_transform_b_8x8(rtcd, &blockd[i + 2].dqcoeff[0],
                                  &blockd[i].diff[0], 32);
    }
  }
}

void vp9_inverse_transform_mbuv_8x8(const vp9_idct_rtcd_vtable_t *rtcd,
                                    MACROBLOCKD *xd) {
  int i;
  BLOCKD *blockd = xd->block;

  for (i = 16; i < 24; i += 4) {
    vp9_inverse_transform_b_8x8(rtcd, &blockd[i].dqcoeff[0],
                                &blockd[i].diff[0], 16);
  }
}

void vp9_inverse_transform_mb_8x8(const vp9_idct_rtcd_vtable_t *rtcd,
                                  MACROBLOCKD *xd) {
  vp9_inverse_transform_mby_8x8(rtcd, xd);
  vp9_inverse_transform_mbuv_8x8(rtcd, xd);
}

void vp9_inverse_transform_b_16x16(const vp9_idct_rtcd_vtable_t *rtcd,
                                   short *input_dqcoeff,
                                   short *output_coeff, int pitch) {
  IDCT_INVOKE(rtcd, idct16x16)(input_dqcoeff, output_coeff, pitch);
}

void vp9_inverse_transform_mby_16x16(const vp9_idct_rtcd_vtable_t *rtcd,
                                     MACROBLOCKD *xd) {
  BLOCKD *bd = &xd->block[0];
  TX_TYPE tx_type = get_tx_type_16x16(xd, bd);
  if (tx_type != DCT_DCT) {
    vp9_ihtllm_c(bd->dqcoeff, bd->diff, 32, tx_type, 16);
  } else {
    vp9_inverse_transform_b_16x16(rtcd, &xd->block[0].dqcoeff[0],
                                  &xd->block[0].diff[0], 32);
  }
}

void vp9_inverse_transform_mb_16x16(const vp9_idct_rtcd_vtable_t *rtcd,
                                    MACROBLOCKD *xd) {
  vp9_inverse_transform_mby_16x16(rtcd, xd);
  vp9_inverse_transform_mbuv_8x8(rtcd, xd);
}
