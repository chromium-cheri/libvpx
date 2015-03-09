/*
 *  Copyright (c) 2015 The WebM project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */


#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#include "vp9/common/vp9_palette.h"

#if CONFIG_PALETTE
void insertion_sort(double *data, int n) {
  int i, j, k;
  double val;

  if (n <= 1)
    return;

  for (i = 1; i < n; i++) {
    val = data[i];
    j = 0;
    while (val > data[j] && j < i)
      j++;

    if (j == i)
      continue;

    for (k = i; k > j; k--)
      data[k] = data[k - 1];
    data[j] = val;
  }
}

int count_colors(const uint8_t *src, int stride, int rows, int cols) {
  int n = 0, r, c, i, val_count[256];
  uint8_t val;
  memset(val_count, 0, sizeof(val_count));

  for (r = 0; r < rows; r++) {
      for (c = 0; c < cols; c++) {
        val = src[r * stride + c];
        val_count[val]++;
      }
    }

    for (i = 0; i < 256; i++) {
      if (val_count[i]) {
        n++;
      }
    }

    return n;
}

int run_lengh_encoding(uint8_t *seq, int n, uint16_t *runs, int max_run) {
  int this_run, i, l = 0;
  uint8_t symbol;

  for (i = 0; i < n; ) {
    if ((l + 2) > (2 * max_run - 1))
      return 0;

    symbol = seq[i];
    runs[l++] = symbol;
    this_run = 1;
    i++;
    while (seq[i] == symbol && i < n) {
      i++;
      this_run++;
    }
    runs[l++] = this_run;
  }

  return l;
}

int run_lengh_decoding(uint16_t *runs, int l, uint8_t *seq) {
  int i, j = 0;

  for (i = 0; i < l; i += 2) {
    memset(seq + j, runs[i], runs[i + 1]);
    j += runs[i + 1];
  }

  return j;
}

void transpose_block(uint8_t *seq_in, uint8_t *seq_out, int rows, int cols) {
  int r, c;
  uint8_t seq_dup[4096];
  memcpy(seq_dup, seq_in, rows * cols);

  for (r = 0; r < cols; r++) {
    for (c = 0; c < rows; c++) {
      seq_out[r * rows + c] = seq_dup[c * cols + r];
    }
  }
}

void palette_color_insertion(uint8_t *old_colors, int *m, int *count,
                             MB_MODE_INFO *mbmi) {
  int k = *m, n = mbmi->palette_literal_size;
  int i, j, l, idx, min_idx = -1;
  uint8_t *new_colors = mbmi->palette_literal_colors;
  uint8_t val;

  if (mbmi->palette_indexed_size > 0) {
    for (i = 0; i < mbmi->palette_indexed_size; i++)
      count[mbmi->palette_indexed_colors[i]] +=
          (8 - abs(mbmi->palette_color_delta[i]));
  }

  i = 0;
  while (i < k) {
    count[i] -= 1;
    i++;
  }

  if (n <= 0)
    return;

  for (i = 0; i < n; i++) {
    val = new_colors[i];
    j = 0;
    while (val > old_colors[j] && j < k)
      j++;
    if (j < k && val == old_colors[j]) {
      count[j] += 8;
      continue;
    }

    idx = j;
    k++;
    if (k > PALETTE_BUF_SIZE) {
      k--;
      min_idx = 0;
      for (l = 1; l < k; l++) {
        if (count[l] < count[min_idx])
          min_idx = l;
      }

      l = min_idx;
      while (l < k - 1) {
        old_colors[l] = old_colors[l + 1];
        count[l] = count[l + 1];
        l++;
      }
    }

    if (min_idx < 0 || idx <= min_idx)
      j = idx;
    else
      j = idx - 1;

    if (j == k - 1) {
      old_colors[k - 1] = val;
      count[k - 1] = 8;
    } else {
      for (l = k - 1; l > j; l--) {
        old_colors[l] = old_colors[l - 1];
        count[l] = count[l - 1];
      }
      old_colors[j] = val;
      count[j] = 8;
    }
  }
  *m = k;
}

int palette_color_lookup(uint8_t *dic, int n, uint8_t val, int bits) {
  int j, min, arg_min = 0, i = 1;

  if (n < 1)
    return -1;

  min = abs(val - dic[0]);
  arg_min = 0;
  while (i < n) {
    j = abs(val - dic[i]);
    if (j < min) {
      min = j;
      arg_min = i;
    }
    i++;
  }

  if (min < (1 << bits))
    return arg_min;
  else
    return -1;
}

int get_bit_depth(int n) {
  int i = 1, p = 2;
  while (p < n) {
    i++;
    p = p << 1;
  }

  return i;
}

int palette_max_run(BLOCK_SIZE bsize) {
  int table[BLOCK_SIZES] = {
      16, 16, 16, 32,  // BLOCK_4X4,   BLOCK_4X8,   BLOCK_8X4,   BLOCK_8X8
      64, 64, 64, 64,  // BLOCK_8X16,  BLOCK_16X8,  BLOCK_16X16, BLOCK_16X32
      64, 64, 64, 64,  // BLOCK_32X16, BLOCK_32X32, BLOCK_32X64, BLOCK_64X32
      64               // BLOCK_64X64
  };

  return table[bsize];
}

double calc_dist(double *p1, double *p2, int dim) {
  double dist = 0;
  int i = 0;

  for (i = 0; i < dim; i++) {
    dist = dist + (p1[i] - p2[i]) * (p1[i] - p2[i]);
  }
  return dist;
}

void calc_indices(double *data, double *centroids, int *indices,
                  int n, int k, int dim) {
  int i, j;
  double min_dist, this_dist;

  for (i = 0; i < n; i++) {
    min_dist = calc_dist(data + i * dim, centroids, dim);
    indices[i] = 0;
    for (j = 0; j < k; j++) {
      this_dist = calc_dist(data + i * dim, centroids + j * dim, dim);
      if (this_dist < min_dist) {
        min_dist = this_dist;
        indices[i] = j;
      }
    }
  }
}

void calc_centroids(double *data, double *centroids, int *indices,
                   int n, int k, int dim) {
  int i, j, index;
  int count[256];
  unsigned int seed = time(NULL);
  memset(count, 0, sizeof(count[0]) * k);
  memset(centroids, 0, sizeof(centroids[0]) * k * dim);

  for (i = 0; i < n; i++) {
    index = indices[i];
    count[index]++;
    for (j = 0; j < dim; j++) {
      centroids[index * dim + j] += data[i * dim + j];
    }
  }

  for (i = 0; i < k; i++) {
    if (!count[i])
      memcpy(centroids + i * dim, data + (rand_r(&seed) % n) * dim,
             sizeof(centroids[0]) * dim);
    else
      for (j = 0; j < dim; j++)
        centroids[i * dim + j] /= count[i];
  }
}

double calc_total_dist(double *data, double *centroids, int *indices,
                       int n, int k, int dim) {
  double dist = 0;
  int i;
  (void) k;

  for (i = 0; i < n; i++) {
    dist += calc_dist(data + i * dim, centroids + indices[i] * dim, dim);
  }

  return dist;
}

int k_means(double *data, double *centroids, int *indices,
             int n, int k, int dim, int max_itr) {
  int i = 0;
  int pre_indices[4096];
  double pre_total_dist, cur_total_dist;
  double pre_centroids[256];

  calc_indices(data, centroids, indices, n, k, dim);
  pre_total_dist = calc_total_dist(data, centroids, indices, n, k, dim);
  memcpy(pre_centroids, centroids, sizeof(pre_centroids[0]) * k * dim);
  memcpy(pre_indices, indices, sizeof(pre_indices[0]) * n);
  while (i < max_itr) {
    calc_centroids(data, centroids, indices, n, k, dim);
    calc_indices(data, centroids, indices, n, k, dim);
    cur_total_dist = calc_total_dist(data, centroids, indices, n, k, dim);

    if (cur_total_dist > pre_total_dist && 0) {
      memcpy(centroids, pre_centroids, sizeof(pre_centroids[0]) * k * dim);
      memcpy(indices, pre_indices, sizeof(pre_indices[0]) * n);
      break;
    }
    if (!memcmp(centroids, pre_centroids, sizeof(pre_centroids[0]) * k * dim))
      break;

    memcpy(pre_centroids, centroids, sizeof(pre_centroids[0]) * k * dim);
    memcpy(pre_indices, indices, sizeof(pre_indices[0]) * n);
    pre_total_dist = cur_total_dist;
    i++;
  }
  return i;
}

int is_in_boundary(int rows, int cols, int r, int c) {
  if (r < 0 || r >= rows || c < 0 || c >= cols)
    return 0;
  return 1;
}

void zz_scan_order(int *order, int rows, int cols) {
  int r, c, dir, idx;

  memset(order, 0, sizeof(order[0]) * rows * cols);
  r = 0;
  c = 0;
  dir = 1;
  idx = 0;
  while (r != (rows - 1) || c != (cols - 1)) {
    order[idx++] = r * cols + c;
    if (dir == -1) {
      if (is_in_boundary(rows, cols, r + 1, c - 1)) {
        r = r + 1;
        c = c - 1;
      } else if (is_in_boundary(rows, cols, r + 1, c)) {
        r = r + 1;
        dir *= -1;
      } else if (is_in_boundary(rows, cols, r, c + 1)) {
        c = c + 1;
        dir *= -1;
      }
    } else {
      if (is_in_boundary(rows, cols, r - 1, c + 1)) {
        r = r - 1;
        c = c + 1;
      } else if (is_in_boundary(rows, cols, r, c + 1)) {
        c = c + 1;
        dir *= -1;
      } else if (is_in_boundary(rows, cols, r + 1, c)) {
        r = r + 1;
        dir *= -1;
      }
    }
  }
  order[idx] = (rows - 1) * cols + cols - 1;
}

void spin_order(int *order, int cols, int r_start, int c_start,
                int h, int w, int idx) {
  int r, c;

  if (h <= 0 && w <= 0) {
    return;
  } else if (h <= 0) {
    for (c = 0; c < w; c++)
      order[idx++] = r_start * cols + c + c_start;
    return;
  } else if  (w <= 0) {
    for (r = 0; r < h; r++)
        order[idx++] = (r + r_start) * cols;
    return;
  }

  for (r = 0; r < h; r++)
    order[idx++] = (r + r_start) * cols + c_start;

  for (c = 0; c < w; c++)
    order[idx++] = (h + r_start) * cols + c + c_start;

  for (r = 0; r < h; r++)
    order[idx++] = (h - r + r_start) * cols + w + c_start;

  for (c = 0; c < w; c++)
    order[idx++] = r_start * cols + w - c + c_start;

  spin_order(order, cols, r_start + 1, c_start + 1, h - 2, w - 2, idx);
}

void spin_scan_order(int *order, int rows, int cols) {
  spin_order(order, cols, 0, 0, rows - 1, cols - 1, 0);
}
#endif
