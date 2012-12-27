/*
 *  Copyright (c) 2010 The WebM project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */


#ifndef VP9_DECODER_VP9_DEQUANTIZE_H_
#define VP9_DECODER_VP9_DEQUANTIZE_H_

#include "vp9/common/vp9_blockd.h"

#if CONFIG_LOSSLESS
extern void vp9_dequant_idct_add_lossless_c(int16_t *input,
                                            const int16_t *dq,
                                            uint8_t *pred,
                                            uint8_t *output,
                                            int pitch, int stride);
extern void vp9_dequant_dc_idct_add_lossless_c(int16_t *input,
                                               const int16_t *dq,
                                               uint8_t *pred,
                                               uint8_t *output,
                                               int pitch, int stride, int dc);
extern void vp9_dequant_dc_idct_add_y_block_lossless_c(int16_t *q,
                                                       const int16_t *dq,
                                                       uint8_t *pre,
                                                       uint8_t *dst,
                                                       int stride,
                                                       uint16_t *eobs,
                                                       const int16_t *dc);
extern void vp9_dequant_idct_add_y_block_lossless_c(int16_t *q,
                                                    const int16_t *dq,
                                                    uint8_t *pre,
                                                    uint8_t *dst,
                                                    int stride,
                                                    uint16_t *eobs);
extern void vp9_dequant_idct_add_uv_block_lossless_c(int16_t *q,
                                                     const int16_t *dq,
                                                     uint8_t *pre,
                                                     uint8_t *dst_u,
                                                     uint8_t *dst_v,
                                                     int stride,
                                                     uint16_t *eobs);
#endif  // CONFIG_LOSSLESS

typedef void (*vp9_dequant_idct_add_fn_t)(int16_t *input, const int16_t *dq,
                                          uint8_t *pred, uint8_t *output,
                                          int pitch, int stride);
typedef void(*vp9_dequant_dc_idct_add_fn_t)(int16_t *input, const int16_t *dq,
                                            uint8_t *pred, uint8_t *output,
                                            int pitch, int stride, int dc);

typedef void(*vp9_dequant_dc_idct_add_y_block_fn_t)(int16_t *q,
                                                    const int16_t *dq,
                                                    uint8_t *pre, uint8_t *dst,
                                                    int stride, uint16_t *eobs,
                                                    const int16_t *dc);
typedef void(*vp9_dequant_idct_add_y_block_fn_t)(int16_t *q, const int16_t *dq,
                                                 uint8_t *pre, uint8_t *dst,
                                                 int stride, uint16_t *eobs);
typedef void(*vp9_dequant_idct_add_uv_block_fn_t)(int16_t *q, const int16_t *dq,
                                                  uint8_t *pre, uint8_t *dst_u,
                                                  uint8_t *dst_v, int stride,
                                                  uint16_t *eobs);

<<<<<<< HEAD   (89ac94 Removed mmx versions of vp9_bilinear_predict filters)
void vp9_ht_dequant_idct_add_c(TX_TYPE tx_type, int16_t *input,
                               const int16_t *dq,
                               uint8_t *pred, uint8_t *dest,
                               int pitch, int stride);
=======
void vp9_ht_dequant_idct_add_c(TX_TYPE tx_type, short *input, const short *dq,
                                    unsigned char *pred, unsigned char *dest,
                                    int pitch, int stride, uint16_t eobs);
>>>>>>> BRANCH (16810c Merge branch 'vp9-preview' of review:webm/libvpx)

<<<<<<< HEAD   (89ac94 Removed mmx versions of vp9_bilinear_predict filters)
void vp9_ht_dequant_idct_add_8x8_c(TX_TYPE tx_type, int16_t *input,
                                   const int16_t *dq, uint8_t *pred,
                                   uint8_t *dest, int pitch, int stride);
=======
void vp9_ht_dequant_idct_add_8x8_c(TX_TYPE tx_type, short *input,
                                   const short *dq, unsigned char *pred,
                                   unsigned char *dest, int pitch, int stride,
                                   uint16_t eobs);
>>>>>>> BRANCH (16810c Merge branch 'vp9-preview' of review:webm/libvpx)

<<<<<<< HEAD   (89ac94 Removed mmx versions of vp9_bilinear_predict filters)
void vp9_ht_dequant_idct_add_16x16_c(TX_TYPE tx_type, int16_t *input,
                                     const int16_t *dq, uint8_t *pred,
                                     uint8_t *dest,
                                     int pitch, int stride);
=======
void vp9_ht_dequant_idct_add_16x16_c(TX_TYPE tx_type, short *input,
                                     const short *dq, unsigned char *pred,
                                     unsigned char *dest,
                                     int pitch, int stride, uint16_t eobs);
>>>>>>> BRANCH (16810c Merge branch 'vp9-preview' of review:webm/libvpx)

#if CONFIG_SUPERBLOCKS
void vp9_dequant_dc_idct_add_y_block_8x8_inplace_c(int16_t *q,
                                                   const int16_t *dq,
                                                   uint8_t *dst,
                                                   int stride,
                                                   uint16_t *eobs,
                                                   const int16_t *dc,
                                                   MACROBLOCKD *xd);

void vp9_dequant_dc_idct_add_y_block_4x4_inplace_c(int16_t *q,
                                                   const int16_t *dq,
                                                   uint8_t *dst,
                                                   int stride,
                                                   uint16_t *eobs,
                                                   const int16_t *dc,
                                                   MACROBLOCKD *xd);

void vp9_dequant_idct_add_uv_block_8x8_inplace_c(int16_t *q,
                                                 const int16_t *dq,
                                                 uint8_t *dstu,
                                                 uint8_t *dstv,
                                                 int stride,
                                                 uint16_t *eobs,
                                                 MACROBLOCKD *xd);

void vp9_dequant_idct_add_uv_block_4x4_inplace_c(int16_t *q,
                                                 const int16_t *dq,
                                                 uint8_t *dstu,
                                                 uint8_t *dstv,
                                                 int stride,
                                                 uint16_t *eobs,
                                                 MACROBLOCKD *xd);
#endif  // CONFIG_SUPERBLOCKS

#endif  // VP9_DECODER_VP9_DEQUANTIZE_H_
