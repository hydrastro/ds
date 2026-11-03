#include "segment_tree.h"
#include <limits.h>
#include <stdlib.h>

static long seg_combine(ds_segment_tree_t *st, long a, long b) {
  switch (st->op) {
  case DS_SEGMENT_TREE_SUM:
    return a + b;
  case DS_SEGMENT_TREE_MIN:
    return a < b ? a : b;
  case DS_SEGMENT_TREE_MAX:
    return a > b ? a : b;
  default:
    return a + b;
  }
}

static long seg_identity(ds_segment_tree_t *st) {
  switch (st->op) {
  case DS_SEGMENT_TREE_SUM:
    return 0L;
  case DS_SEGMENT_TREE_MIN:
    return LONG_MAX;
  case DS_SEGMENT_TREE_MAX:
    return LONG_MIN;
  default:
    return 0L;
  }
}

/* apply an assign of `value` to node covering `count` elements */
static void seg_apply_assign(ds_segment_tree_t *st, size_t node, size_t count,
                             long value) {
  switch (st->op) {
  case DS_SEGMENT_TREE_SUM:
    st->tree[node] = value * (long)count;
    break;
  default:
    st->tree[node] = value;
    break;
  }
  st->lazy_set[node] = value;
  st->has_set[node] = true;
  st->lazy_add[node] = 0L;
}

/* apply an add of `delta` to node covering `count` elements */
static void seg_apply_add(ds_segment_tree_t *st, size_t node, size_t count,
                          long delta) {
  switch (st->op) {
  case DS_SEGMENT_TREE_SUM:
    st->tree[node] += delta * (long)count;
    break;
  default:
    st->tree[node] += delta;
    break;
  }
  if (st->has_set[node]) {
    st->lazy_set[node] += delta;
  } else {
    st->lazy_add[node] += delta;
  }
}

static void seg_push_down(ds_segment_tree_t *st, size_t node, size_t left,
                          size_t mid, size_t right) {
  size_t left_count = mid - left + 1;
  size_t right_count = right - mid;
  if (st->has_set[node]) {
    seg_apply_assign(st, 2 * node, left_count, st->lazy_set[node]);
    seg_apply_assign(st, 2 * node + 1, right_count, st->lazy_set[node]);
    st->has_set[node] = false;
    st->lazy_set[node] = 0L;
  }
  if (st->lazy_add[node] != 0L) {
    seg_apply_add(st, 2 * node, left_count, st->lazy_add[node]);
    seg_apply_add(st, 2 * node + 1, right_count, st->lazy_add[node]);
    st->lazy_add[node] = 0L;
  }
}

static void seg_build(ds_segment_tree_t *st, const long *values, size_t node,
                      size_t left, size_t right) {
  if (left == right) {
    st->tree[node] = values[left];
    return;
  }
  {
    size_t mid = left + (right - left) / 2;
    seg_build(st, values, 2 * node, left, mid);
    seg_build(st, values, 2 * node + 1, mid + 1, right);
    st->tree[node] =
        seg_combine(st, st->tree[2 * node], st->tree[2 * node + 1]);
  }
}

ds_segment_tree_t *ds_segment_tree_create(const long *values, size_t n,
                                          ds_segment_tree_op_t op) {
  return ds_segment_tree_create_alloc(values, n, op, malloc, free);
}

ds_segment_tree_t *ds_segment_tree_create_alloc(const long *values, size_t n,
                                                ds_segment_tree_op_t op,
                                                void *(*allocator)(size_t),
                                                void (*deallocator)(void *)) {
  ds_segment_tree_t *st =
      (ds_segment_tree_t *)allocator(sizeof(ds_segment_tree_t));
  size_t cap = (n == 0 ? 1 : n) * 4;
  size_t i;
  st->allocator = allocator;
  st->deallocator = deallocator;
  st->n = n;
  st->capacity = cap;
  st->op = op;
  st->tree = (long *)allocator(cap * sizeof(long));
  st->lazy_add = (long *)allocator(cap * sizeof(long));
  st->lazy_set = (long *)allocator(cap * sizeof(long));
  st->has_set = (bool *)allocator(cap * sizeof(bool));
  for (i = 0; i < cap; i++) {
    st->tree[i] = 0L;
    st->lazy_add[i] = 0L;
    st->lazy_set[i] = 0L;
    st->has_set[i] = false;
  }
  if (n > 0) {
    seg_build(st, values, 1, 0, n - 1);
  }
  return st;
}

static void seg_update_add(ds_segment_tree_t *st, size_t node, size_t left,
                           size_t right, size_t ql, size_t qr, long delta) {
  size_t mid;
  if (qr < left || right < ql) {
    return;
  }
  if (ql <= left && right <= qr) {
    seg_apply_add(st, node, right - left + 1, delta);
    return;
  }
  mid = left + (right - left) / 2;
  seg_push_down(st, node, left, mid, right);
  seg_update_add(st, 2 * node, left, mid, ql, qr, delta);
  seg_update_add(st, 2 * node + 1, mid + 1, right, ql, qr, delta);
  st->tree[node] = seg_combine(st, st->tree[2 * node], st->tree[2 * node + 1]);
}

void ds_segment_tree_update_add(ds_segment_tree_t *st, size_t left,
                                size_t right, long delta) {
  if (st->n > 0 && left <= right && right < st->n) {
    seg_update_add(st, 1, 0, st->n - 1, left, right, delta);
  }
}

static void seg_update_assign(ds_segment_tree_t *st, size_t node, size_t left,
                              size_t right, size_t ql, size_t qr, long value) {
  size_t mid;
  if (qr < left || right < ql) {
    return;
  }
  if (ql <= left && right <= qr) {
    seg_apply_assign(st, node, right - left + 1, value);
    return;
  }
  mid = left + (right - left) / 2;
  seg_push_down(st, node, left, mid, right);
  seg_update_assign(st, 2 * node, left, mid, ql, qr, value);
  seg_update_assign(st, 2 * node + 1, mid + 1, right, ql, qr, value);
  st->tree[node] = seg_combine(st, st->tree[2 * node], st->tree[2 * node + 1]);
}

void ds_segment_tree_update_assign(ds_segment_tree_t *st, size_t left,
                                   size_t right, long value) {
  if (st->n > 0 && left <= right && right < st->n) {
    seg_update_assign(st, 1, 0, st->n - 1, left, right, value);
  }
}

static long seg_query(ds_segment_tree_t *st, size_t node, size_t left,
                      size_t right, size_t ql, size_t qr) {
  size_t mid;
  long left_result;
  long right_result;
  if (qr < left || right < ql) {
    return seg_identity(st);
  }
  if (ql <= left && right <= qr) {
    return st->tree[node];
  }
  mid = left + (right - left) / 2;
  seg_push_down(st, node, left, mid, right);
  left_result = seg_query(st, 2 * node, left, mid, ql, qr);
  right_result = seg_query(st, 2 * node + 1, mid + 1, right, ql, qr);
  return seg_combine(st, left_result, right_result);
}

long ds_segment_tree_query(ds_segment_tree_t *st, size_t left, size_t right) {
  long result;
  if (st->n == 0 || left > right || right >= st->n) {
    result = seg_identity(st);
  } else {
    result = seg_query(st, 1, 0, st->n - 1, left, right);
  }
  return result;
}

size_t ds_segment_tree_size(ds_segment_tree_t *st) {
  size_t result;
  result = st->n;
  return result;
}

void ds_segment_tree_destroy(ds_segment_tree_t *st) {
  st->deallocator(st->tree);
  st->deallocator(st->lazy_add);
  st->deallocator(st->lazy_set);
  st->deallocator(st->has_set);
  st->deallocator(st);
}
