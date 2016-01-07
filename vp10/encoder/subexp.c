/*
 *  Copyright (c) 2013 The WebM project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */
#include "vpx_dsp/bitwriter.h"

#include "vp10/common/common.h"
#include "vp10/common/entropy.h"
#include "vp10/encoder/cost.h"
#include "vp10/encoder/subexp.h"

#define vp10_cost_upd256  ((int)(vp10_cost_one(upd) - vp10_cost_zero(upd)))

static const uint8_t update_bits[255] = {
   5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,
   6,  6,  6,  6,  6,  6,  6,  6,  6,  6,  6,  6,  6,  6,  6,  6,
   8,  8,  8,  8,  8,  8,  8,  8,  8,  8,  8,  8,  8,  8,  8,  8,
   8,  8,  8,  8,  8,  8,  8,  8,  8,  8,  8,  8,  8,  8,  8,  8,
  10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10,
  10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10,
  10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10,
  10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10,
  10, 10, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11,
  11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11,
  11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11,
  11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11,
  11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11,
  11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11,
  11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11,
  11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11,  0,
};

static int recenter_nonneg(int v, int m) {
  if (v > (m << 1))
    return v;
  else if (v >= m)
    return ((v - m) << 1);
  else
    return ((m - v) << 1) - 1;
}

static int remap_prob(int v, int m) {
  int i;
  static const uint8_t map_table[MAX_PROB - 1] = {
    // generated by:
    //   map_table[j] = split_index(j, MAX_PROB - 1, MODULUS_PARAM);
     20,  21,  22,  23,  24,  25,   0,  26,  27,  28,  29,  30,  31,  32,  33,
     34,  35,  36,  37,   1,  38,  39,  40,  41,  42,  43,  44,  45,  46,  47,
     48,  49,   2,  50,  51,  52,  53,  54,  55,  56,  57,  58,  59,  60,  61,
      3,  62,  63,  64,  65,  66,  67,  68,  69,  70,  71,  72,  73,   4,  74,
     75,  76,  77,  78,  79,  80,  81,  82,  83,  84,  85,   5,  86,  87,  88,
     89,  90,  91,  92,  93,  94,  95,  96,  97,   6,  98,  99, 100, 101, 102,
    103, 104, 105, 106, 107, 108, 109,   7, 110, 111, 112, 113, 114, 115, 116,
    117, 118, 119, 120, 121,   8, 122, 123, 124, 125, 126, 127, 128, 129, 130,
    131, 132, 133,   9, 134, 135, 136, 137, 138, 139, 140, 141, 142, 143, 144,
    145,  10, 146, 147, 148, 149, 150, 151, 152, 153, 154, 155, 156, 157,  11,
    158, 159, 160, 161, 162, 163, 164, 165, 166, 167, 168, 169,  12, 170, 171,
    172, 173, 174, 175, 176, 177, 178, 179, 180, 181,  13, 182, 183, 184, 185,
    186, 187, 188, 189, 190, 191, 192, 193,  14, 194, 195, 196, 197, 198, 199,
    200, 201, 202, 203, 204, 205,  15, 206, 207, 208, 209, 210, 211, 212, 213,
    214, 215, 216, 217,  16, 218, 219, 220, 221, 222, 223, 224, 225, 226, 227,
    228, 229,  17, 230, 231, 232, 233, 234, 235, 236, 237, 238, 239, 240, 241,
     18, 242, 243, 244, 245, 246, 247, 248, 249, 250, 251, 252, 253,  19,
  };
  v--;
  m--;
  if ((m << 1) <= MAX_PROB)
    i = recenter_nonneg(v, m) - 1;
  else
    i = recenter_nonneg(MAX_PROB - 1 - v, MAX_PROB - 1 - m) - 1;

  i = map_table[i];
  return i;
}

static int prob_diff_update_cost(vpx_prob newp, vpx_prob oldp) {
  int delp = remap_prob(newp, oldp);
  return update_bits[delp] << VP9_PROB_COST_SHIFT;
}

static void encode_uniform(vpx_writer *w, int v) {
  const int l = 8;
  const int m = (1 << l) - 190;
  if (v < m) {
    vpx_write_literal(w, v, l - 1);
  } else {
    vpx_write_literal(w, m + ((v - m) >> 1), l - 1);
    vpx_write_literal(w, (v - m) & 1, 1);
  }
}

static INLINE int write_bit_gte(vpx_writer *w, int word, int test) {
  vpx_write_literal(w, word >= test, 1);
  return word >= test;
}

static void encode_term_subexp(vpx_writer *w, int word) {
  if (!write_bit_gte(w, word, 16)) {
    vpx_write_literal(w, word, 4);
  } else if (!write_bit_gte(w, word, 32)) {
    vpx_write_literal(w, word - 16, 4);
  } else if (!write_bit_gte(w, word, 64)) {
    vpx_write_literal(w, word - 32, 5);
  } else {
    encode_uniform(w, word - 64);
  }
}

void vp10_write_prob_diff_update(vpx_writer *w, vpx_prob newp, vpx_prob oldp) {
  const int delp = remap_prob(newp, oldp);
  encode_term_subexp(w, delp);
}

int vp10_prob_diff_update_savings_search(const unsigned int *ct,
                                        vpx_prob oldp, vpx_prob *bestp,
                                        vpx_prob upd) {
  const int old_b = cost_branch256(ct, oldp);
  int bestsavings = 0;
  vpx_prob newp, bestnewp = oldp;
  const int step = *bestp > oldp ? -1 : 1;

  for (newp = *bestp; newp != oldp; newp += step) {
    const int new_b = cost_branch256(ct, newp);
    const int update_b = prob_diff_update_cost(newp, oldp) + vp10_cost_upd256;
    const int savings = old_b - new_b - update_b;
    if (savings > bestsavings) {
      bestsavings = savings;
      bestnewp = newp;
    }
  }
  *bestp = bestnewp;
  return bestsavings;
}

int vp10_prob_diff_update_savings_search_model(const unsigned int *ct,
                                              const vpx_prob *oldp,
                                              vpx_prob *bestp,
                                              vpx_prob upd,
                                              int stepsize) {
  int i, old_b, new_b, update_b, savings, bestsavings;
  int newp;
  const int step_sign = *bestp > oldp[PIVOT_NODE] ? -1 : 1;
  const int step = stepsize * step_sign;
  vpx_prob bestnewp, newplist[ENTROPY_NODES], oldplist[ENTROPY_NODES];
  vp10_model_to_full_probs(oldp, oldplist);
  memcpy(newplist, oldp, sizeof(vpx_prob) * UNCONSTRAINED_NODES);
  for (i = UNCONSTRAINED_NODES, old_b = 0; i < ENTROPY_NODES; ++i)
    old_b += cost_branch256(ct + 2 * i, oldplist[i]);
  old_b += cost_branch256(ct + 2 * PIVOT_NODE, oldplist[PIVOT_NODE]);

  bestsavings = 0;
  bestnewp = oldp[PIVOT_NODE];

  assert(stepsize > 0);

  for (newp = *bestp; (newp - oldp[PIVOT_NODE]) * step_sign < 0;
      newp += step) {
    if (newp < 1 || newp > 255)
      continue;
    newplist[PIVOT_NODE] = newp;
    vp10_model_to_full_probs(newplist, newplist);
    for (i = UNCONSTRAINED_NODES, new_b = 0; i < ENTROPY_NODES; ++i)
      new_b += cost_branch256(ct + 2 * i, newplist[i]);
    new_b += cost_branch256(ct + 2 * PIVOT_NODE, newplist[PIVOT_NODE]);
    update_b = prob_diff_update_cost(newp, oldp[PIVOT_NODE]) +
        vp10_cost_upd256;
    savings = old_b - new_b - update_b;
    if (savings > bestsavings) {
      bestsavings = savings;
      bestnewp = newp;
    }
  }

  *bestp = bestnewp;
  return bestsavings;
}

#if CONFIG_SUBFRAME_STATS || 1
static int cal_cost(unsigned int ct[][2], vpx_prob p, int n) {
  int i, p0 = p;
  unsigned int total_ct[2] = {0 , 0};
  int cost = 0;

  for (i = 0; i <= n; ++i) {
    cost += cost_branch256(ct[i], p);
    total_ct[0] += ct[i][0];
    total_ct[1] += ct[i][1];
    if (i < n)
      p = merge_probs(p0, total_ct, 24, 112);
  }

  return cost;
}

int vp10_prob_update_search_subframe(unsigned int ct[][2],
                                     vpx_prob oldp, vpx_prob *bestp,
                                     vpx_prob upd, int n) {
  const int old_b = cal_cost(ct, oldp, n);
  int bestsavings = 0;
  vpx_prob newp, bestnewp = oldp;
  const int step = *bestp > oldp ? -1 : 1;

  for (newp = *bestp; newp != oldp; newp += step) {
    const int new_b = cal_cost(ct, newp, n);
    const int update_b = prob_diff_update_cost(newp, oldp) + vp10_cost_upd256;
    const int savings = old_b - new_b - update_b;
    if (savings > bestsavings) {
      bestsavings = savings;
      bestnewp = newp;
    }
  }
  *bestp = bestnewp;
  return bestsavings;
}

int vp10_prob_update_search_model_subframe(unsigned int ct[ENTROPY_NODES]
                                                          [COEF_PROBS_BUFS][2],
                                           const vpx_prob *oldp,
                                           vpx_prob *bestp, vpx_prob upd,
                                           int stepsize, int n) {
  int i, old_b, new_b, update_b, savings, bestsavings;
  int newp;
  const int step_sign = *bestp > oldp[PIVOT_NODE] ? -1 : 1;
  const int step = stepsize * step_sign;
  vpx_prob bestnewp, newplist[ENTROPY_NODES], oldplist[ENTROPY_NODES];
  vp10_model_to_full_probs(oldp, oldplist);
  memcpy(newplist, oldp, sizeof(vpx_prob) * UNCONSTRAINED_NODES);
  for (i = UNCONSTRAINED_NODES, old_b = 0; i < ENTROPY_NODES; ++i)
    old_b += cal_cost(ct[i], oldplist[i], n);
  old_b += cal_cost(ct[PIVOT_NODE], oldplist[PIVOT_NODE], n);

  bestsavings = 0;
  bestnewp = oldp[PIVOT_NODE];

  assert(stepsize > 0);

  for (newp = *bestp; (newp - oldp[PIVOT_NODE]) * step_sign < 0;
      newp += step) {
    if (newp < 1 || newp > 255)
      continue;
    newplist[PIVOT_NODE] = newp;
    vp10_model_to_full_probs(newplist, newplist);
    for (i = UNCONSTRAINED_NODES, new_b = 0; i < ENTROPY_NODES; ++i)
      new_b += cal_cost(ct[i], newplist[i], n);
    new_b += cal_cost(ct[PIVOT_NODE], newplist[PIVOT_NODE], n);
    update_b = prob_diff_update_cost(newp, oldp[PIVOT_NODE]) +
        vp10_cost_upd256;
    savings = old_b - new_b - update_b;
    if (savings > bestsavings) {
      bestsavings = savings;
      bestnewp = newp;
    }
  }

  *bestp = bestnewp;
  return bestsavings;
}
#endif  // CONFIG_SUBFRAME_STATS || 1

void vp10_cond_prob_diff_update(vpx_writer *w, vpx_prob *oldp,
                               const unsigned int ct[2]) {
  const vpx_prob upd = DIFF_UPDATE_PROB;
  vpx_prob newp = get_binary_prob(ct[0], ct[1]);
  const int savings = vp10_prob_diff_update_savings_search(ct, *oldp, &newp,
                                                          upd);
  assert(newp >= 1);
  if (savings > 0) {
    vpx_write(w, 1, upd);
    vp10_write_prob_diff_update(w, newp, *oldp);
    *oldp = newp;
  } else {
    vpx_write(w, 0, upd);
  }
}

int vp10_cond_prob_diff_update_savings(vpx_prob *oldp,
                                       const unsigned int ct[2]) {
  const vpx_prob upd = DIFF_UPDATE_PROB;
  vpx_prob newp = get_binary_prob(ct[0], ct[1]);
  const int savings = vp10_prob_diff_update_savings_search(ct, *oldp, &newp,
                                                           upd);
  return savings;
}
