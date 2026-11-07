#include "kd_tree.h"
#include <string.h>

#define KD_SIZE_MAX (~(size_t)0)

static double *kd_pt(const ds_kd_tree_t *t, size_t i) {
  return t->points + i * t->dims;
}

static void kd_swap(ds_kd_tree_t *t, size_t a, size_t b) {
  size_t d;
  size_t id;
  double tmp;
  double *pa;
  double *pb;
  if (a == b) {
    return;
  }
  pa = kd_pt(t, a);
  pb = kd_pt(t, b);
  for (d = 0; d < t->dims; d++) {
    tmp = pa[d];
    pa[d] = pb[d];
    pb[d] = tmp;
  }
  id = t->ids[a];
  t->ids[a] = t->ids[b];
  t->ids[b] = id;
}

/* Quickselect on axis: put the element of rank `k` at position k. */
static void kd_select(ds_kd_tree_t *t, size_t lo, size_t hi, size_t k,
                      size_t axis) {
  size_t i;
  size_t store;
  double pivot;
  while (hi - lo > 1) {
    kd_swap(t, lo + (hi - lo) / 2, hi - 1);
    pivot = kd_pt(t, hi - 1)[axis];
    store = lo;
    for (i = lo; i + 1 < hi; i++) {
      if (kd_pt(t, i)[axis] < pivot) {
        kd_swap(t, i, store++);
      }
    }
    kd_swap(t, store, hi - 1);
    if (store == k) {
      return;
    }
    if (k < store) {
      hi = store;
    } else {
      lo = store + 1;
    }
  }
}

static void kd_build(ds_kd_tree_t *t, size_t lo, size_t hi, size_t depth) {
  size_t mid;
  while (hi - lo > 1) {
    mid = lo + (hi - lo) / 2;
    kd_select(t, lo, hi, mid, depth % t->dims);
    kd_build(t, lo, mid, depth + 1);
    lo = mid + 1;
    depth++;
  }
}

static ds_status_t kd_setup(ds_kd_tree_t *t, const double *points, size_t count,
                            size_t dims) {
  size_t i;
  if (t == NULL || (points == NULL && count > 0)) {
    return DS_ERR_NULL;
  }
  if (dims == 0) {
    return DS_ERR_INVALID;
  }
  if (count > KD_SIZE_MAX / dims ||
      count * dims > KD_SIZE_MAX / sizeof(double)) {
    return DS_ERR_OVERFLOW;
  }
  t->count = count;
  t->dims = dims;
  if (count > 0) {
    memcpy(t->points, points, count * dims * sizeof(double));
  }
  for (i = 0; i < count; i++) {
    t->ids[i] = i;
  }
  kd_build(t, 0, count, 0);
  return DS_OK;
}

ds_status_t ds_kd_tree_init(ds_kd_tree_t *tree, const double *points,
                            size_t count, size_t dims, ds_context_t *context) {
  if (tree == NULL) {
    return DS_ERR_NULL;
  }
  if (dims == 0) {
    return DS_ERR_INVALID;
  }
  if (count > KD_SIZE_MAX / dims ||
      count * dims > KD_SIZE_MAX / sizeof(double)) {
    return DS_ERR_OVERFLOW;
  }
  tree->context = context != NULL ? context : ds_default_context();
  tree->fixed = false;
  tree->points = (double *)ds_context_alloc(
      tree->context, (count > 0 ? count * dims : 1) * sizeof(double));
  tree->ids = (size_t *)ds_context_alloc(
      tree->context, (count > 0 ? count : 1) * sizeof(size_t));
  if (tree->points == NULL || tree->ids == NULL) {
    ds_context_free(tree->context, tree->points);
    ds_context_free(tree->context, tree->ids);
    tree->points = NULL;
    tree->ids = NULL;
    return DS_ERR_ALLOC;
  }
  return kd_setup(tree, points, count, dims);
}

ds_status_t ds_kd_tree_init_buffer(ds_kd_tree_t *tree, const double *points,
                                   size_t count, size_t dims,
                                   double *point_buffer, size_t *id_buffer) {
  if (tree == NULL ||
      (count > 0 && (point_buffer == NULL || id_buffer == NULL))) {
    return DS_ERR_NULL;
  }
  tree->points = point_buffer;
  tree->ids = id_buffer;
  tree->context = NULL;
  tree->fixed = true;
  return kd_setup(tree, points, count, dims);
}

void ds_kd_tree_destroy(ds_kd_tree_t *tree) {
  if (tree == NULL) {
    return;
  }
  if (!tree->fixed) {
    ds_context_free(tree->context, tree->points);
    ds_context_free(tree->context, tree->ids);
  }
  tree->points = NULL;
  tree->ids = NULL;
  tree->count = 0;
}

static double kd_dist2(const ds_kd_tree_t *t, const double *a,
                       const double *b) {
  double s = 0.0;
  double d;
  size_t i;
  for (i = 0; i < t->dims; i++) {
    d = a[i] - b[i];
    s += d * d;
  }
  return s;
}

/* k-nearest search with a sorted result list of up to k entries. */
typedef struct kd_knn {
  size_t k;
  size_t found;
  size_t *ids;
  double *dist2;
} kd_knn_t;

static void kd_knn_offer(kd_knn_t *r, size_t id, double d2) {
  size_t i;
  if (r->found == r->k && d2 >= r->dist2[r->k - 1]) {
    return;
  }
  i = r->found < r->k ? r->found++ : r->k - 1;
  while (i > 0 && r->dist2[i - 1] > d2) {
    r->dist2[i] = r->dist2[i - 1];
    r->ids[i] = r->ids[i - 1];
    i--;
  }
  r->dist2[i] = d2;
  r->ids[i] = id;
}

static void kd_knn_search(const ds_kd_tree_t *t, const double *q, size_t lo,
                          size_t hi, size_t depth, kd_knn_t *r) {
  size_t mid;
  size_t axis;
  double diff;
  if (lo >= hi) {
    return;
  }
  mid = lo + (hi - lo) / 2;
  axis = depth % t->dims;
  kd_knn_offer(r, t->ids[mid], kd_dist2(t, kd_pt(t, mid), q));
  diff = q[axis] - kd_pt(t, mid)[axis];
  if (diff < 0) {
    kd_knn_search(t, q, lo, mid, depth + 1, r);
    if (r->found < r->k || diff * diff < r->dist2[r->found - 1]) {
      kd_knn_search(t, q, mid + 1, hi, depth + 1, r);
    }
  } else {
    kd_knn_search(t, q, mid + 1, hi, depth + 1, r);
    if (r->found < r->k || diff * diff < r->dist2[r->found - 1]) {
      kd_knn_search(t, q, lo, mid, depth + 1, r);
    }
  }
}

size_t ds_kd_tree_knn(const ds_kd_tree_t *tree, const double *query, size_t k,
                      size_t *out_indices, double *out_dist2) {
  kd_knn_t r;
  if (tree == NULL || query == NULL || out_indices == NULL ||
      out_dist2 == NULL || k == 0) {
    return 0;
  }
  r.k = k;
  r.found = 0;
  r.ids = out_indices;
  r.dist2 = out_dist2;
  kd_knn_search(tree, query, 0, tree->count, 0, &r);
  return r.found;
}

ds_status_t ds_kd_tree_nearest(const ds_kd_tree_t *tree, const double *query,
                               size_t *out_index, double *out_dist2) {
  size_t id;
  double d2;
  if (tree == NULL || query == NULL) {
    return DS_ERR_NULL;
  }
  if (ds_kd_tree_knn(tree, query, 1, &id, &d2) == 0) {
    return DS_EMPTY;
  }
  if (out_index != NULL) {
    *out_index = id;
  }
  if (out_dist2 != NULL) {
    *out_dist2 = d2;
  }
  return DS_OK;
}

typedef struct kd_range {
  const double *low;
  const double *high;
  const double *centre;
  double radius2;
  ds_kd_visit_func_t visit;
  void *user;
} kd_range_t;

static bool kd_box_search(const ds_kd_tree_t *t, const kd_range_t *r, size_t lo,
                          size_t hi, size_t depth) {
  size_t mid;
  size_t axis;
  size_t d;
  double v;
  const double *p;
  bool inside = true;
  if (lo >= hi) {
    return true;
  }
  mid = lo + (hi - lo) / 2;
  axis = depth % t->dims;
  p = kd_pt(t, mid);
  for (d = 0; d < t->dims; d++) {
    if (p[d] < r->low[d] || p[d] > r->high[d]) {
      inside = false;
      break;
    }
  }
  if (inside && !r->visit(t->ids[mid], p, r->user)) {
    return false;
  }
  v = p[axis];
  if (r->low[axis] <= v && !kd_box_search(t, r, lo, mid, depth + 1)) {
    return false;
  }
  if (r->high[axis] >= v && !kd_box_search(t, r, mid + 1, hi, depth + 1)) {
    return false;
  }
  return true;
}

static bool kd_radius_search(const ds_kd_tree_t *t, const kd_range_t *r,
                             size_t lo, size_t hi, size_t depth) {
  size_t mid;
  size_t axis;
  double diff;
  const double *p;
  if (lo >= hi) {
    return true;
  }
  mid = lo + (hi - lo) / 2;
  axis = depth % t->dims;
  p = kd_pt(t, mid);
  if (kd_dist2(t, p, r->centre) <= r->radius2 &&
      !r->visit(t->ids[mid], p, r->user)) {
    return false;
  }
  diff = r->centre[axis] - p[axis];
  if ((diff <= 0 || diff * diff <= r->radius2) &&
      !kd_radius_search(t, r, lo, mid, depth + 1)) {
    return false;
  }
  if ((diff >= 0 || diff * diff <= r->radius2) &&
      !kd_radius_search(t, r, mid + 1, hi, depth + 1)) {
    return false;
  }
  return true;
}

ds_status_t ds_kd_tree_box(const ds_kd_tree_t *tree, const double *low,
                           const double *high, ds_kd_visit_func_t visit,
                           void *user) {
  kd_range_t r;
  if (tree == NULL || low == NULL || high == NULL || visit == NULL) {
    return DS_ERR_NULL;
  }
  r.low = low;
  r.high = high;
  r.centre = NULL;
  r.radius2 = 0.0;
  r.visit = visit;
  r.user = user;
  return kd_box_search(tree, &r, 0, tree->count, 0) ? DS_OK : DS_STOP;
}

ds_status_t ds_kd_tree_radius(const ds_kd_tree_t *tree, const double *centre,
                              double radius, ds_kd_visit_func_t visit,
                              void *user) {
  kd_range_t r;
  if (tree == NULL || centre == NULL || visit == NULL) {
    return DS_ERR_NULL;
  }
  r.low = NULL;
  r.high = NULL;
  r.centre = centre;
  r.radius2 = radius * radius;
  r.visit = visit;
  r.user = user;
  return kd_radius_search(tree, &r, 0, tree->count, 0) ? DS_OK : DS_STOP;
}

size_t ds_kd_tree_size(const ds_kd_tree_t *tree) { return tree->count; }
