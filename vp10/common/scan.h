/*
 *  Copyright (c) 2013 The WebM project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#ifndef VP10_COMMON_SCAN_H_
#define VP10_COMMON_SCAN_H_

#include "vpx/vpx_integer.h"
#include "vpx_ports/mem.h"

#include "vp10/common/enums.h"
#include "vp10/common/blockd.h"

#ifdef __cplusplus
extern "C" {
#endif

#define MAX_NEIGHBORS 2

typedef struct {
  const int16_t *scan;
  const int16_t *iscan;
  const int16_t *neighbors;
} scan_order;

extern const scan_order vp10_default_scan_orders[TX_SIZES][TX_TYPES];
extern const scan_order vp10_intra_scan_orders[TX_SIZES][TX_TYPES];

static INLINE int get_coef_context(const int16_t *neighbors,
                                   const uint8_t *token_cache, int c) {
  return (1 + token_cache[neighbors[MAX_NEIGHBORS * c + 0]] +
          token_cache[neighbors[MAX_NEIGHBORS * c + 1]]) >> 1;
}

static INLINE const scan_order *get_intra_scan(TX_SIZE tx_size,
                                               TX_TYPE tx_type) {
  return &vp10_intra_scan_orders[tx_size][tx_type];
}

#if CONFIG_EXT_TX
extern const scan_order vp10_inter_scan_orders[TX_SIZES][TX_TYPES];

static INLINE const scan_order *get_inter_scan(TX_SIZE tx_size,
                                               TX_TYPE tx_type) {
  return &vp10_inter_scan_orders[tx_size][tx_type];
}
#endif  // CONFIG_EXT_TX

static INLINE const scan_order *get_scan(TX_SIZE tx_size,
                                         TX_TYPE tx_type,
                                         int is_inter) {
#if CONFIG_EXT_TX
  return
      is_inter ? &vp10_inter_scan_orders[tx_size][tx_type] :
                 &vp10_intra_scan_orders[tx_size][tx_type];
#else
  (void) is_inter;
  return &vp10_intra_scan_orders[tx_size][tx_type];
#endif  // CONFIG_EXT_TX
}

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // VP10_COMMON_SCAN_H_
