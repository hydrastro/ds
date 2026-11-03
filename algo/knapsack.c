#include "knapsack.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

static void *ks_alloc(ds_context_t *ctx, size_t count, size_t size) {
  if (count == 0) {
    count = 1;
  }
  if (count > ((size_t)-1) / size) {
    return NULL;
  }
  return ds_context_alloc(ctx, count * size);
}

ds_status_t ds_knapsack_01(const size_t *weights, const double *values,
                           size_t n, size_t capacity, unsigned char *out_take,
                           double *out_value, ds_context_t *context) {
  double *best;
  unsigned char *take; /* n x (capacity + 1) bits */
  size_t row;
  size_t i;
  size_t c;
  size_t w;
  double cand;
  if (out_value == NULL || (n > 0 && (weights == NULL || values == NULL))) {
    return DS_ERR_NULL;
  }
  if (capacity == (size_t)-1) {
    return DS_ERR_OVERFLOW;
  }
  row = (capacity + 1 + 7) / 8;
  if (n > 0 && row > ((size_t)-1) / n) {
    return DS_ERR_OVERFLOW;
  }
  best = (double *)ds_context_calloc(context, capacity + 1, sizeof(double));
  take = out_take != NULL
             ? (unsigned char *)ds_context_calloc(context, n * row + 1, 1)
             : NULL;
  if (best == NULL || (out_take != NULL && take == NULL)) {
    ds_context_free(context, best);
    ds_context_free(context, take);
    return DS_ERR_ALLOC;
  }
  for (i = 0; i < n; i++) {
    w = weights[i];
    if (w > capacity || !(values[i] > 0)) {
      continue;
    }
    for (c = capacity + 1; c-- > w;) {
      cand = best[c - w] + values[i];
      if (cand > best[c]) {
        best[c] = cand;
        if (take != NULL) {
          take[i * row + c / 8] =
              (unsigned char)(take[i * row + c / 8] | (1U << (c % 8)));
        }
      }
    }
  }
  *out_value = best[capacity];
  if (out_take != NULL) {
    c = capacity;
    for (i = n; i-- > 0;) {
      out_take[i] = (unsigned char)((take[i * row + c / 8] >> (c % 8)) & 1U);
      if (out_take[i]) {
        c -= weights[i];
      }
    }
  }
  ds_context_free(context, best);
  ds_context_free(context, take);
  return DS_OK;
}

ds_status_t ds_knapsack_unbounded(const size_t *weights, const double *values,
                                  size_t n, size_t capacity, size_t *out_counts,
                                  double *out_value, ds_context_t *context) {
  double *best;
  size_t *pick;
  size_t i;
  size_t c;
  double cand;
  if (out_value == NULL || (n > 0 && (weights == NULL || values == NULL))) {
    return DS_ERR_NULL;
  }
  if (capacity == (size_t)-1) {
    return DS_ERR_OVERFLOW;
  }
  best = (double *)ds_context_calloc(context, capacity + 1, sizeof(double));
  pick = (size_t *)ks_alloc(context, capacity + 1, sizeof(size_t));
  if (best == NULL || pick == NULL) {
    ds_context_free(context, best);
    ds_context_free(context, pick);
    return DS_ERR_ALLOC;
  }
  for (c = 0; c <= capacity; c++) {
    pick[c] = (size_t)-1;
    for (i = 0; i < n; i++) {
      if (weights[i] == 0 || weights[i] > c || !(values[i] > 0)) {
        continue;
      }
      cand = best[c - weights[i]] + values[i];
      if (cand > best[c]) {
        best[c] = cand;
        pick[c] = i;
      }
    }
  }
  *out_value = best[capacity];
  if (out_counts != NULL) {
    for (i = 0; i < n; i++) {
      out_counts[i] = 0;
    }
    c = capacity;
    while (c > 0) {
      if (pick[c] == (size_t)-1) {
        c--; /* unused capacity */
        continue;
      }
      out_counts[pick[c]]++;
      c -= weights[pick[c]];
    }
  }
  ds_context_free(context, best);
  ds_context_free(context, pick);
  return DS_OK;
}

typedef struct ks_item {
  double key;
  size_t index;
} ks_item_t;

static int ks_cmp_desc(const void *a, const void *b) {
  const ks_item_t *x = (const ks_item_t *)a;
  const ks_item_t *y = (const ks_item_t *)b;
  if (x->key > y->key) {
    return -1;
  }
  if (x->key < y->key) {
    return 1;
  }
  return x->index < y->index ? -1 : (x->index > y->index);
}

ds_status_t ds_knapsack_fractional(const double *weights, const double *values,
                                   size_t n, double capacity,
                                   double *out_fraction, double *out_value,
                                   ds_context_t *context) {
  ks_item_t *items;
  size_t i;
  size_t k = 0;
  double total = 0;
  double room = capacity;
  double f;
  if (out_value == NULL || (n > 0 && (weights == NULL || values == NULL))) {
    return DS_ERR_NULL;
  }
  items = (ks_item_t *)ks_alloc(context, n, sizeof(ks_item_t));
  if (items == NULL) {
    return DS_ERR_ALLOC;
  }
  for (i = 0; i < n; i++) {
    if (out_fraction != NULL) {
      out_fraction[i] = 0;
    }
    if (!(values[i] > 0)) {
      continue;
    }
    if (!(weights[i] > 0)) { /* free value */
      total += values[i];
      if (out_fraction != NULL) {
        out_fraction[i] = 1;
      }
      continue;
    }
    items[k].key = values[i] / weights[i];
    items[k].index = i;
    k++;
  }
  qsort(items, k, sizeof(ks_item_t), ks_cmp_desc);
  for (i = 0; i < k && room > 0; i++) {
    size_t j = items[i].index;
    f = weights[j] <= room ? 1.0 : room / weights[j];
    total += f * values[j];
    room -= f * weights[j];
    if (out_fraction != NULL) {
      out_fraction[j] = f;
    }
  }
  *out_value = total;
  ds_context_free(context, items);
  return DS_OK;
}

ds_status_t ds_bin_packing(const double *sizes, size_t n, double capacity,
                           ds_bin_policy_t policy, size_t *out_bin,
                           size_t *out_bins, ds_context_t *context) {
  ks_item_t *order;
  double *room;
  size_t bins = 0;
  size_t i;
  size_t j;
  size_t k;
  size_t pick;
  double s;
  int pol = (int)policy;
  if (out_bin == NULL || out_bins == NULL || (n > 0 && sizes == NULL)) {
    return DS_ERR_NULL;
  }
  if (pol < 0 || pol > (int)DS_BIN_BEST_FIT_DECREASING) {
    return DS_ERR_INVALID;
  }
  order = (ks_item_t *)ks_alloc(context, n, sizeof(ks_item_t));
  room = (double *)ks_alloc(context, n, sizeof(double));
  if (order == NULL || room == NULL) {
    ds_context_free(context, order);
    ds_context_free(context, room);
    return DS_ERR_ALLOC;
  }
  for (i = 0; i < n; i++) {
    if (!(sizes[i] >= 0) || sizes[i] > capacity) {
      ds_context_free(context, order);
      ds_context_free(context, room);
      return DS_ERR_RANGE;
    }
    order[i].key = sizes[i];
    order[i].index = i;
  }
  if (policy == DS_BIN_FIRST_FIT_DECREASING ||
      policy == DS_BIN_BEST_FIT_DECREASING) {
    qsort(order, n, sizeof(ks_item_t), ks_cmp_desc);
  }
  for (k = 0; k < n; k++) {
    i = order[k].index;
    s = sizes[i];
    pick = (size_t)-1;
    switch (policy) {
    case DS_BIN_NEXT_FIT:
      if (bins > 0 && room[bins - 1] >= s) {
        pick = bins - 1;
      }
      break;
    case DS_BIN_FIRST_FIT:
    case DS_BIN_FIRST_FIT_DECREASING:
      for (j = 0; j < bins && pick == (size_t)-1; j++) {
        if (room[j] >= s) {
          pick = j;
        }
      }
      break;
    case DS_BIN_BEST_FIT:
    case DS_BIN_BEST_FIT_DECREASING:
      for (j = 0; j < bins; j++) {
        if (room[j] >= s && (pick == (size_t)-1 || room[j] < room[pick])) {
          pick = j;
        }
      }
      break;
    case DS_BIN_WORST_FIT:
    default:
      for (j = 0; j < bins; j++) {
        if (room[j] >= s && (pick == (size_t)-1 || room[j] > room[pick])) {
          pick = j;
        }
      }
      break;
    }
    if (pick == (size_t)-1) {
      pick = bins++;
      room[pick] = capacity;
    }
    room[pick] -= s;
    out_bin[i] = pick;
  }
  *out_bins = bins;
  ds_context_free(context, order);
  ds_context_free(context, room);
  return DS_OK;
}

ds_status_t ds_cutting_stock(const double *lengths, const size_t *demand,
                             size_t piece_count, double stock,
                             ds_bin_policy_t policy, size_t *out_stock,
                             size_t *out_bars, ds_context_t *context) {
  double *sizes;
  size_t total = 0;
  size_t i;
  size_t j;
  size_t k = 0;
  ds_status_t status;
  if (out_stock == NULL || out_bars == NULL ||
      (piece_count > 0 && (lengths == NULL || demand == NULL))) {
    return DS_ERR_NULL;
  }
  for (i = 0; i < piece_count; i++) {
    if (demand[i] > ((size_t)-1) - total) {
      return DS_ERR_OVERFLOW;
    }
    total += demand[i];
  }
  sizes = (double *)ks_alloc(context, total, sizeof(double));
  if (sizes == NULL) {
    return DS_ERR_ALLOC;
  }
  for (i = 0; i < piece_count; i++) {
    for (j = 0; j < demand[i]; j++) {
      sizes[k++] = lengths[i];
    }
  }
  status =
      ds_bin_packing(sizes, total, stock, policy, out_stock, out_bars, context);
  ds_context_free(context, sizes);
  return status;
}

ds_status_t ds_strip_packing(const double *widths, const double *heights,
                             size_t n, double strip_width,
                             ds_strip_policy_t policy, double *out_x,
                             double *out_y, double *out_height,
                             ds_context_t *context) {
  ks_item_t *order;
  double *level_y;
  double *level_used;
  double *level_h;
  size_t levels = 0;
  size_t i;
  size_t j;
  size_t k;
  size_t pick;
  double top = 0;
  if (out_x == NULL || out_y == NULL || out_height == NULL ||
      (n > 0 && (widths == NULL || heights == NULL))) {
    return DS_ERR_NULL;
  }
  if (policy != DS_STRIP_NFDH && policy != DS_STRIP_FFDH) {
    return DS_ERR_INVALID;
  }
  order = (ks_item_t *)ks_alloc(context, n, sizeof(ks_item_t));
  level_y = (double *)ks_alloc(context, n, sizeof(double));
  level_used = (double *)ks_alloc(context, n, sizeof(double));
  level_h = (double *)ks_alloc(context, n, sizeof(double));
  if (order == NULL || level_y == NULL || level_used == NULL ||
      level_h == NULL) {
    ds_context_free(context, order);
    ds_context_free(context, level_y);
    ds_context_free(context, level_used);
    ds_context_free(context, level_h);
    return DS_ERR_ALLOC;
  }
  for (i = 0; i < n; i++) {
    if (!(widths[i] >= 0) || widths[i] > strip_width || !(heights[i] >= 0)) {
      ds_context_free(context, order);
      ds_context_free(context, level_y);
      ds_context_free(context, level_used);
      ds_context_free(context, level_h);
      return DS_ERR_RANGE;
    }
    order[i].key = heights[i];
    order[i].index = i;
  }
  qsort(order, n, sizeof(ks_item_t), ks_cmp_desc);
  for (k = 0; k < n; k++) {
    i = order[k].index;
    pick = (size_t)-1;
    if (policy == DS_STRIP_NFDH) {
      if (levels > 0 && level_used[levels - 1] + widths[i] <= strip_width) {
        pick = levels - 1;
      }
    } else {
      for (j = 0; j < levels && pick == (size_t)-1; j++) {
        if (level_used[j] + widths[i] <= strip_width) {
          pick = j;
        }
      }
    }
    if (pick == (size_t)-1) {
      pick = levels++;
      level_y[pick] = top;
      level_used[pick] = 0;
      level_h[pick] = heights[i]; /* the tallest of its level */
      top += heights[i];
    }
    out_x[i] = level_used[pick];
    out_y[i] = level_y[pick];
    level_used[pick] += widths[i];
  }
  *out_height = top;
  ds_context_free(context, order);
  ds_context_free(context, level_y);
  ds_context_free(context, level_used);
  ds_context_free(context, level_h);
  return DS_OK;
}

ds_status_t ds_matrix_chain(const size_t *dims, size_t n, double *out_cost,
                            size_t *out_split, ds_context_t *context) {
  double *m;
  size_t len;
  size_t i;
  size_t j;
  size_t k;
  double c;
  if (dims == NULL || out_cost == NULL) {
    return DS_ERR_NULL;
  }
  if (n == 0) {
    *out_cost = 0;
    return DS_OK;
  }
  if (n > ((size_t)-1) / n / sizeof(double)) {
    return DS_ERR_OVERFLOW;
  }
  m = (double *)ds_context_calloc(context, n * n, sizeof(double));
  if (m == NULL) {
    return DS_ERR_ALLOC;
  }
  for (len = 1; len < n; len++) {
    for (i = 0; i + len < n; i++) {
      j = i + len;
      m[i * n + j] = HUGE_VAL;
      for (k = i; k < j; k++) {
        c = m[i * n + k] + m[(k + 1) * n + j] +
            (double)dims[i] * (double)dims[k + 1] * (double)dims[j + 1];
        if (c < m[i * n + j]) {
          m[i * n + j] = c;
          if (out_split != NULL) {
            out_split[i * n + j] = k;
          }
        }
      }
    }
  }
  *out_cost = m[n - 1];
  ds_context_free(context, m);
  return DS_OK;
}
