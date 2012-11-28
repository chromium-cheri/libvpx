/*
 *  Copyright (c) 2011 The WebM project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */


#include "vpx_ports/asm_offsets.h"
#include "vpx_config.h"
#include "vp9/encoder/vp9_block.h"
#include "vp9/common/vp9_blockd.h"
#include "vp9/encoder/vp9_onyx_int.h"
#include "vp9/encoder/vp9_treewriter.h"
#include "vp9/encoder/vp9_tokenize.h"

BEGIN

/* regular quantize */
DEFINE(vp9_block_coeff,                         offsetof(BLOCK, coeff));
DEFINE(vp9_block_zbin,                          offsetof(BLOCK, zbin));
DEFINE(vp9_block_round,                         offsetof(BLOCK, round));
DEFINE(vp9_block_quant,                         offsetof(BLOCK, quant));
DEFINE(vp9_block_quant_fast,                    offsetof(BLOCK, quant_fast));
DEFINE(vp9_block_zbin_extra,                    offsetof(BLOCK, zbin_extra));
DEFINE(vp9_block_zrun_zbin_boost,               offsetof(BLOCK, zrun_zbin_boost));
DEFINE(vp9_block_quant_shift,                   offsetof(BLOCK, quant_shift));

DEFINE(vp9_blockd_qcoeff,                       offsetof(BLOCKD, qcoeff));
DEFINE(vp9_blockd_dequant,                      offsetof(BLOCKD, dequant));
DEFINE(vp9_blockd_dqcoeff,                      offsetof(BLOCKD, dqcoeff));
DEFINE(vp9_blockd_eob,                          offsetof(BLOCKD, eob));

/* subtract */
DEFINE(vp9_block_base_src,                      offsetof(BLOCK, base_src));
DEFINE(vp9_block_src,                           offsetof(BLOCK, src));
DEFINE(vp9_block_src_diff,                      offsetof(BLOCK, src_diff));
DEFINE(vp9_block_src_stride,                    offsetof(BLOCK, src_stride));

DEFINE(vp9_blockd_predictor,                    offsetof(BLOCKD, predictor));

/* pack tokens */
DEFINE(vp9_writer_lowvalue,                     offsetof(vp9_writer, lowvalue));
DEFINE(vp9_writer_range,                        offsetof(vp9_writer, range));
DEFINE(vp9_writer_value,                        offsetof(vp9_writer, value));
DEFINE(vp9_writer_count,                        offsetof(vp9_writer, count));
DEFINE(vp9_writer_pos,                          offsetof(vp9_writer, pos));
DEFINE(vp9_writer_buffer,                       offsetof(vp9_writer, buffer));

DEFINE(tokenextra_token,                        offsetof(TOKENEXTRA, Token));
DEFINE(tokenextra_extra,                        offsetof(TOKENEXTRA, Extra));
DEFINE(tokenextra_context_tree,                 offsetof(TOKENEXTRA, context_tree));
DEFINE(tokenextra_skip_eob_node,                offsetof(TOKENEXTRA, skip_eob_node));
DEFINE(TOKENEXTRA_SZ,                           sizeof(TOKENEXTRA));

DEFINE(vp9_extra_bit_struct_sz,                 sizeof(vp9_extra_bit_struct));

DEFINE(vp9_token_value,                         offsetof(vp9_token, value));
DEFINE(vp9_token_len,                           offsetof(vp9_token, Len));

DEFINE(vp9_extra_bit_struct_tree,               offsetof(vp9_extra_bit_struct, tree));
DEFINE(vp9_extra_bit_struct_prob,               offsetof(vp9_extra_bit_struct, prob));
DEFINE(vp9_extra_bit_struct_len,                offsetof(vp9_extra_bit_struct, Len));
DEFINE(vp9_extra_bit_struct_base_val,           offsetof(vp9_extra_bit_struct, base_val));

DEFINE(vp9_comp_tplist,                         offsetof(VP9_COMP, tplist));
DEFINE(vp9_comp_common,                         offsetof(VP9_COMP, common));

DEFINE(tokenlist_start,                         offsetof(TOKENLIST, start));
DEFINE(tokenlist_stop,                          offsetof(TOKENLIST, stop));
DEFINE(TOKENLIST_SZ,                            sizeof(TOKENLIST));

DEFINE(vp9_common_mb_rows,                      offsetof(VP9_COMMON, mb_rows));

END

/* add asserts for any offset that is not supported by assembly code
 * add asserts for any size that is not supported by assembly code

 * These are used in vp8cx_pack_tokens.  They are hard coded so if their sizes
 * change they will have to be adjusted.
 */

#if HAVE_ARMV5TE
ct_assert(TOKENEXTRA_SZ, sizeof(TOKENEXTRA) == 8)
ct_assert(vp9_extra_bit_struct_sz, sizeof(vp9_extra_bit_struct) == 16)
#endif
