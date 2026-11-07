#include "kdb_tree.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

#define KD_NONE (~(size_t)0)
#define KD_MAXD DS_KDB_MAX_DIM

/* ---- shared helpers
 * ---------------------------------------------------------- */

static bool kd_finite(const double *x, size_t n) {
  size_t i;
  for (i = 0; i < n; i++) {
    if (!(x[i] > -HUGE_VAL && x[i] < HUGE_VAL)) {
      return false;
    }
  }
  return true;
}

static bool kd_box_ok(const double *b, size_t d) {
  size_t a;
  if (!kd_finite(b, 2 * d)) {
    return false;
  }
  for (a = 0; a < d; a++) {
    if (b[a] > b[d + a]) {
      return false;
    }
  }
  return true;
}

/* point in the half-open region [lo, hi) */
static bool kd_in_region(const double *p, const double *r, size_t d) {
  size_t a;
  for (a = 0; a < d; a++) {
    if (p[a] < r[a] || !(p[a] < r[d + a])) {
      return false;
    }
  }
  return true;
}

/* closed box meets half-open region */
static bool kd_box_meets_region(const double *b, const double *r, size_t d) {
  size_t a;
  for (a = 0; a < d; a++) {
    if (!(b[a] < r[d + a]) || b[d + a] < r[a]) {
      return false;
    }
  }
  return true;
}

/* closed boxes meet */
static bool kd_box_hit(const double *x, const double *y, size_t d) {
  size_t a;
  for (a = 0; a < d; a++) {
    if (x[d + a] < y[a] || y[d + a] < x[a]) {
      return false;
    }
  }
  return true;
}

static bool kd_point_in_box(const double *p, const double *b, size_t d) {
  size_t a;
  for (a = 0; a < d; a++) {
    if (p[a] < b[a] || p[a] > b[d + a]) {
      return false;
    }
  }
  return true;
}

static double kd_mindist2(const double *b, const double *p, size_t d) {
  size_t a;
  double s = 0.0;
  double t;
  for (a = 0; a < d; a++) {
    if (p[a] < b[a]) {
      t = b[a] - p[a];
    } else if (p[a] > b[d + a]) {
      t = p[a] - b[d + a];
    } else {
      t = 0.0;
    }
    s += t * t;
  }
  return s;
}

static double kd_dist2(const double *x, const double *y, size_t d) {
  size_t a;
  double s = 0.0;
  for (a = 0; a < d; a++) {
    s += (x[a] - y[a]) * (x[a] - y[a]);
  }
  return s;
}

static void kd_all_space(double *r, size_t d) {
  size_t a;
  for (a = 0; a < d; a++) {
    r[a] = -HUGE_VAL;
    r[d + a] = HUGE_VAL;
  }
}

static void kd_empty_box(double *b, size_t d) {
  size_t a;
  for (a = 0; a < d; a++) {
    b[a] = HUGE_VAL;
    b[d + a] = -HUGE_VAL;
  }
}

static void kd_extend(double *b, const double *x, size_t d) {
  size_t a;
  for (a = 0; a < d; a++) {
    if (x[a] < b[a]) {
      b[a] = x[a];
    }
    if (x[d + a] > b[d + a]) {
      b[d + a] = x[d + a];
    }
  }
}

static int kd_cmp_double(const void *a, const void *b) {
  double x = *(const double *)a;
  double y = *(const double *)b;
  return (x > y) - (x < y);
}

/* k-best list: keeps the k smallest (dist, value) pairs sorted */
static void kd_best_push(double *dk, size_t *vk, size_t k, size_t *found,
                         double s, size_t value) {
  size_t pos;
  if (*found == k && !(s < dk[k - 1])) {
    return;
  }
  pos = *found < k ? (*found)++ : k - 1;
  while (pos > 0 && dk[pos - 1] > s) {
    dk[pos] = dk[pos - 1];
    vk[pos] = vk[pos - 1];
    pos--;
  }
  dk[pos] = s;
  vk[pos] = value;
}

/* Insert (node, key) into a stack kept so that the smallest key is on top
 * within [base, top). */
static void kd_push_sorted(size_t *stack, double *key, size_t base, size_t *top,
                           size_t node, double s) {
  size_t j;
  for (j = *top; j > base && key[j - 1] < s; j--) {
    key[j] = key[j - 1];
    stack[j] = stack[j - 1];
  }
  key[j] = s;
  stack[j] = node;
  (*top)++;
}

static ds_status_t kd_grow(ds_context_t *ctx, void **p, size_t *cap,
                           size_t need, size_t elem) {
  size_t nc;
  void *q;
  if (need <= *cap) {
    return DS_OK;
  }
  nc = *cap < 8 ? 8 : *cap;
  while (nc < need) {
    if (nc > ((size_t)-1) / 2 / elem) {
      return DS_ERR_OVERFLOW;
    }
    nc *= 2;
  }
  q = ds_context_realloc(ctx, *p, nc * elem);
  if (q == NULL) {
    return DS_ERR_ALLOC;
  }
  *p = q;
  *cap = nc;
  return DS_OK;
}

/* ---- k-d-B-tree
 * ----------------------------------------------------------------- */

#define KP(t, v) ((t)->pages[v])

static size_t kdb_stride(const ds_kdb_tree_t *t, size_t v) {
  return KP(t, v).leaf ? t->dim : 2 * t->dim;
}

static ds_status_t kdb_reserve(ds_kdb_tree_t *t, size_t v, size_t need) {
  size_t cap = KP(t, v).capacity;
  size_t nc;
  void *p;
  if (need <= cap) {
    return DS_OK;
  }
  nc = cap < 4 ? 4 : cap;
  while (nc < need) {
    nc *= 2;
  }
  p = ds_context_realloc(t->context, KP(t, v).data,
                         nc * kdb_stride(t, v) * sizeof(double));
  if (p == NULL) {
    return DS_ERR_ALLOC;
  }
  KP(t, v).data = (double *)p;
  p = ds_context_realloc(t->context, KP(t, v).ref, nc * sizeof(size_t));
  if (p == NULL) {
    return DS_ERR_ALLOC;
  }
  KP(t, v).ref = (size_t *)p;
  KP(t, v).capacity = nc;
  return DS_OK;
}

static ds_status_t kdb_new_page(ds_kdb_tree_t *t, bool leaf, size_t *out) {
  size_t v;
  ds_status_t st;
  if (t->free_list != KD_NONE) {
    v = t->free_list;
    t->free_list = KP(t, v).count;
  } else {
    void *p = t->pages;
    st = kd_grow(t->context, &p, &t->page_capacity, t->page_count + 1,
                 sizeof(ds_kdb_page_t));
    if (st != DS_OK) {
      return st;
    }
    t->pages = (ds_kdb_page_t *)p;
    v = t->page_count++;
  }
  memset(&KP(t, v), 0, sizeof(ds_kdb_page_t));
  KP(t, v).leaf = (unsigned char)leaf;
  st = kdb_reserve(t, v, leaf ? t->bucket + 1 : t->fanout + 1);
  if (st != DS_OK) {
    KP(t, v).count = t->free_list;
    t->free_list = v;
    return st;
  }
  *out = v;
  return DS_OK;
}

ds_status_t ds_kdb_tree_init(ds_kdb_tree_t *t, size_t dim, size_t bucket,
                             size_t fanout, ds_kdb_axis_rule_t axis_rule,
                             ds_context_t *context) {
  ds_status_t st;
  if (t == NULL) {
    return DS_ERR_NULL;
  }
  if (bucket == 0) {
    bucket = 16;
  }
  if (fanout == 0) {
    fanout = 16;
  }
  if (dim == 0 || dim > KD_MAXD || bucket < 2 || fanout < 3 ||
      ((int)axis_rule != DS_KDB_AXIS_SPREAD &&
       axis_rule != DS_KDB_AXIS_VARIANCE && axis_rule != DS_KDB_AXIS_CYCLIC)) {
    return DS_ERR_INVALID;
  }
  memset(t, 0, sizeof(*t));
  t->dim = dim;
  t->bucket = bucket;
  t->fanout = fanout;
  t->axis_rule = axis_rule;
  t->free_list = KD_NONE;
  t->context = context;
  st = kdb_new_page(t, true, &t->root);
  if (st != DS_OK) {
    ds_kdb_tree_destroy(t);
    return st;
  }
  t->height = 1;
  return DS_OK;
}

void ds_kdb_tree_destroy(ds_kdb_tree_t *t) {
  size_t v;
  if (t == NULL) {
    return;
  }
  for (v = 0; v < t->page_count; v++) {
    ds_context_free(t->context, t->pages[v].data);
    ds_context_free(t->context, t->pages[v].ref);
  }
  ds_context_free(t->context, t->pages);
  ds_context_free(t->context, t->path);
  memset(t, 0, sizeof(*t));
  t->free_list = KD_NONE;
}

size_t ds_kdb_tree_size(const ds_kdb_tree_t *t) {
  return t == NULL ? 0 : t->size;
}

size_t ds_kdb_tree_height(const ds_kdb_tree_t *t) {
  return t == NULL ? 0 : t->height;
}

size_t ds_kdb_tree_pages(const ds_kdb_tree_t *t) {
  size_t n = 0;
  size_t v;
  if (t == NULL) {
    return 0;
  }
  n = t->page_count;
  for (v = t->free_list; v != KD_NONE; v = t->pages[v].count) {
    n--;
  }
  return n;
}

/* Cut for an overflowing point page: false when all points coincide. */
static bool kdb_point_cut(ds_kdb_tree_t *t, size_t v, double *scratch,
                          size_t *out_axis, double *out_x) {
  size_t d = t->dim;
  size_t n = KP(t, v).count;
  const double *pts = KP(t, v).data;
  size_t a;
  size_t i;
  size_t j;
  size_t axis = d;
  double best = 0.0;
  double x;
  for (j = 0; j < d; j++) {
    double lo = HUGE_VAL;
    double hi = -HUGE_VAL;
    a = t->axis_rule == DS_KDB_AXIS_CYCLIC ? (KP(t, v).axis + j) % d : j;
    for (i = 0; i < n; i++) {
      double c = pts[i * d + a];
      lo = c < lo ? c : lo;
      hi = c > hi ? c : hi;
    }
    if (hi - lo > best) {
      best = hi - lo;
      axis = a;
      if (t->axis_rule == DS_KDB_AXIS_CYCLIC) {
        break;
      }
    }
  }
  if (axis == d) {
    return false;
  }
  for (i = 0; i < n; i++) {
    scratch[i] = pts[i * d + axis];
  }
  qsort(scratch, n, sizeof(double), kd_cmp_double);
  x = scratch[n / 2];
  if (!(x > scratch[0])) {
    for (i = 1; i < n && !(scratch[i] > scratch[0]); i++) {
    }
    x = scratch[i];
  }
  *out_axis = axis;
  *out_x = x;
  return true;
}

/* Cut for an overflowing region page: the hyperplane through a region
 * boundary minimising the larger side, then the regions it cuts. */
static bool kdb_region_cut(const ds_kdb_tree_t *t, size_t v, size_t *out_axis,
                           double *out_x) {
  size_t d = t->dim;
  size_t n = KP(t, v).count;
  const double *r = KP(t, v).data;
  size_t a;
  size_t i;
  size_t j;
  size_t side;
  bool any = false;
  size_t best_max = 0;
  size_t best_cut = 0;
  for (a = 0; a < d; a++) {
    for (i = 0; i < n; i++) {
      for (side = 0; side < 2; side++) {
        double x = r[i * 2 * d + side * d + a];
        size_t l = 0;
        size_t rr = 0;
        size_t cut = 0;
        size_t mx;
        if (!(x > -HUGE_VAL && x < HUGE_VAL)) {
          continue;
        }
        for (j = 0; j < n; j++) {
          const double *e = r + j * 2 * d;
          if (e[d + a] <= x) {
            l++;
          } else if (e[a] >= x) {
            rr++;
          } else {
            cut++;
          }
        }
        if (l + cut >= n || rr + cut >= n) {
          continue;
        }
        mx = l > rr ? l + cut : rr + cut;
        if (!any || mx < best_max || (mx == best_max && cut < best_cut)) {
          any = true;
          best_max = mx;
          best_cut = cut;
          *out_axis = a;
          *out_x = x;
        }
      }
    }
  }
  return any;
}

/* Split page v by the hyperplane x_axis = x: v keeps the lower side, the
 * upper side goes to a new page. Cut child regions are split recursively
 * (depth bounded by the height). */
static ds_status_t kdb_split(ds_kdb_tree_t *t, size_t v, size_t axis, double x,
                             size_t *out_w) {
  size_t d = t->dim;
  size_t w;
  size_t i;
  size_t keep = 0;
  size_t n;
  ds_status_t st = kdb_new_page(t, KP(t, v).leaf != 0, &w);
  if (st != DS_OK) {
    return st;
  }
  KP(t, w).axis = KP(t, v).axis;
  n = KP(t, v).count;
  if (KP(t, v).leaf) {
    st = kdb_reserve(t, w, n);
    if (st != DS_OK) {
      return st;
    }
    for (i = 0; i < n; i++) {
      double *p = KP(t, v).data + i * d;
      size_t val = KP(t, v).ref[i];
      if (p[axis] < x) {
        if (keep != i) {
          memmove(KP(t, v).data + keep * d, p, d * sizeof(double));
          KP(t, v).ref[keep] = val;
        }
        keep++;
      } else {
        memcpy(KP(t, w).data + KP(t, w).count * d, p, d * sizeof(double));
        KP(t, w).ref[KP(t, w).count++] = val;
      }
    }
    KP(t, v).count = keep;
    *out_w = w;
    return DS_OK;
  }
  st = kdb_reserve(t, w, n);
  if (st != DS_OK) {
    return st;
  }
  for (i = 0; i < n; i++) {
    double reg[2 * KD_MAXD];
    size_t child = KP(t, v).ref[i];
    memcpy(reg, KP(t, v).data + i * 2 * d, 2 * d * sizeof(double));
    if (reg[d + axis] <= x) {
      memcpy(KP(t, v).data + keep * 2 * d, reg, 2 * d * sizeof(double));
      KP(t, v).ref[keep++] = child;
    } else if (reg[axis] >= x) {
      memcpy(KP(t, w).data + KP(t, w).count * 2 * d, reg,
             2 * d * sizeof(double));
      KP(t, w).ref[KP(t, w).count++] = child;
    } else {
      size_t c2;
      double hi = reg[d + axis];
      st = kdb_split(t, child, axis, x, &c2);
      if (st != DS_OK) {
        return st;
      }
      reg[d + axis] = x;
      memcpy(KP(t, v).data + keep * 2 * d, reg, 2 * d * sizeof(double));
      KP(t, v).ref[keep++] = child;
      reg[d + axis] = hi;
      reg[axis] = x;
      memcpy(KP(t, w).data + KP(t, w).count * 2 * d, reg,
             2 * d * sizeof(double));
      KP(t, w).ref[KP(t, w).count++] = c2;
    }
  }
  KP(t, v).count = keep;
  *out_w = w;
  return DS_OK;
}

static ds_status_t kdb_path_need(ds_kdb_tree_t *t, size_t need) {
  void *p = t->path;
  ds_status_t st =
      kd_grow(t->context, &p, &t->path_capacity, need, sizeof(size_t));
  t->path = (size_t *)p;
  return st;
}

ds_status_t ds_kdb_tree_insert(ds_kdb_tree_t *t, const double *point,
                               size_t value) {
  size_t d;
  size_t v;
  size_t i;
  size_t level = 0;
  double *scratch = NULL;
  ds_status_t st;
  if (t == NULL || point == NULL) {
    return DS_ERR_NULL;
  }
  d = t->dim;
  if (!kd_finite(point, d)) {
    return DS_ERR_INVALID;
  }
  st = kdb_path_need(t, 2 * t->height + 2);
  if (st != DS_OK) {
    return st;
  }
  v = t->root;
  while (!KP(t, v).leaf) {
    for (i = 0; i < KP(t, v).count; i++) {
      if (kd_in_region(point, KP(t, v).data + i * 2 * d, d)) {
        break;
      }
    }
    if (i == KP(t, v).count) {
      return DS_ERR_INTERNAL;
    }
    t->path[2 * level] = v;
    t->path[2 * level + 1] = i;
    level++;
    v = KP(t, v).ref[i];
  }
  st = kdb_reserve(t, v, KP(t, v).count + 1);
  if (st != DS_OK) {
    return st;
  }
  memcpy(KP(t, v).data + KP(t, v).count * d, point, d * sizeof(double));
  KP(t, v).ref[KP(t, v).count++] = value;
  t->size++;
  while (KP(t, v).count > (KP(t, v).leaf ? t->bucket : t->fanout)) {
    size_t axis = 0;
    double x = 0.0;
    size_t w;
    if (KP(t, v).leaf) {
      void *p = ds_context_realloc(t->context, scratch,
                                   KP(t, v).count * sizeof(double));
      if (p == NULL) {
        st = DS_ERR_ALLOC;
        break;
      }
      scratch = (double *)p;
      if (!kdb_point_cut(t, v, scratch, &axis, &x)) {
        break; /* coinciding points: an oversized page */
      }
    } else if (!kdb_region_cut(t, v, &axis, &x)) {
      st = DS_ERR_INTERNAL;
      break;
    }
    st = kdb_split(t, v, axis, x, &w);
    if (st != DS_OK) {
      break;
    }
    if (KP(t, v).leaf && t->axis_rule == DS_KDB_AXIS_CYCLIC) {
      KP(t, v).axis = KP(t, w).axis = (unsigned char)((axis + 1) % d);
    }
    if (level == 0) {
      size_t r;
      double *reg;
      st = kdb_new_page(t, false, &r);
      if (st != DS_OK) {
        break;
      }
      reg = KP(t, r).data;
      kd_all_space(reg, d);
      reg[d + axis] = x;
      kd_all_space(reg + 2 * d, d);
      reg[2 * d + axis] = x;
      KP(t, r).ref[0] = v;
      KP(t, r).ref[1] = w;
      KP(t, r).count = 2;
      t->root = r;
      t->height++;
      break;
    }
    level--;
    {
      size_t pv = t->path[2 * level];
      size_t ps = t->path[2 * level + 1];
      double reg[2 * KD_MAXD];
      st = kdb_reserve(t, pv, KP(t, pv).count + 1);
      if (st != DS_OK) {
        break;
      }
      memcpy(reg, KP(t, pv).data + ps * 2 * d, 2 * d * sizeof(double));
      KP(t, pv).data[ps * 2 * d + d + axis] = x;
      reg[axis] = x;
      memcpy(KP(t, pv).data + KP(t, pv).count * 2 * d, reg,
             2 * d * sizeof(double));
      KP(t, pv).ref[KP(t, pv).count++] = w;
      v = pv;
    }
  }
  ds_context_free(t->context, scratch);
  return st;
}

static size_t kdb_find_leaf(const ds_kdb_tree_t *t, const double *point) {
  size_t d = t->dim;
  size_t v = t->root;
  size_t i;
  while (!KP(t, v).leaf) {
    for (i = 0; i < KP(t, v).count; i++) {
      if (kd_in_region(point, KP(t, v).data + i * 2 * d, d)) {
        break;
      }
    }
    if (i == KP(t, v).count) {
      return KD_NONE;
    }
    v = KP(t, v).ref[i];
  }
  return v;
}

ds_status_t ds_kdb_tree_remove(ds_kdb_tree_t *t, const double *point,
                               size_t value) {
  size_t v;
  size_t i;
  size_t d;
  if (t == NULL || point == NULL) {
    return DS_ERR_NULL;
  }
  d = t->dim;
  if (!kd_finite(point, d)) {
    return DS_NOT_FOUND;
  }
  v = kdb_find_leaf(t, point);
  if (v == KD_NONE) {
    return DS_NOT_FOUND;
  }
  for (i = 0; i < KP(t, v).count; i++) {
    if (KP(t, v).ref[i] == value &&
        memcmp(KP(t, v).data + i * d, point, d * sizeof(double)) == 0) {
      size_t last = --KP(t, v).count;
      if (i != last) {
        memcpy(KP(t, v).data + i * d, KP(t, v).data + last * d,
               d * sizeof(double));
        KP(t, v).ref[i] = KP(t, v).ref[last];
      }
      t->size--;
      return DS_OK;
    }
  }
  return DS_NOT_FOUND;
}

bool ds_kdb_tree_contains(const ds_kdb_tree_t *t, const double *point) {
  size_t v;
  size_t i;
  size_t a;
  if (t == NULL || point == NULL || !kd_finite(point, t->dim)) {
    return false;
  }
  v = kdb_find_leaf(t, point);
  if (v == KD_NONE) {
    return false;
  }
  for (i = 0; i < KP(t, v).count; i++) {
    const double *p = KP(t, v).data + i * t->dim;
    for (a = 0; a < t->dim && !(p[a] < point[a]) && !(p[a] > point[a]); a++) {
    }
    if (a == t->dim) {
      return true;
    }
  }
  return false;
}

ds_status_t ds_kdb_tree_query(const ds_kdb_tree_t *t, const double *box,
                              ds_kdb_visit_func_t visit, void *user) {
  size_t *stack;
  size_t top = 0;
  size_t d;
  size_t i;
  ds_status_t st = DS_OK;
  if (t == NULL || box == NULL || visit == NULL) {
    return DS_ERR_NULL;
  }
  d = t->dim;
  stack = (size_t *)ds_context_alloc(t->context,
                                     (t->page_count + 1) * sizeof(size_t));
  if (stack == NULL) {
    return DS_ERR_ALLOC;
  }
  stack[top++] = t->root;
  while (top > 0 && st == DS_OK) {
    size_t v = stack[--top];
    const ds_kdb_page_t *pg = &t->pages[v];
    for (i = 0; i < pg->count; i++) {
      if (pg->leaf) {
        if (kd_point_in_box(pg->data + i * d, box, d) &&
            !visit(pg->data + i * d, pg->ref[i], user)) {
          st = DS_STOP;
          break;
        }
      } else if (kd_box_meets_region(box, pg->data + i * 2 * d, d)) {
        stack[top++] = pg->ref[i];
      }
    }
  }
  ds_context_free(t->context, stack);
  return st;
}

ds_status_t ds_kdb_tree_nearest(const ds_kdb_tree_t *t, const double *point,
                                size_t k, size_t *out_values, double *out_dist,
                                size_t *out_found) {
  size_t *stack;
  double *key;
  double *dk;
  size_t *vk;
  size_t top = 0;
  size_t found = 0;
  size_t i;
  size_t d;
  if (t == NULL || point == NULL || out_found == NULL ||
      (k > 0 && (out_values == NULL || out_dist == NULL))) {
    return DS_ERR_NULL;
  }
  *out_found = 0;
  if (k == 0 || t->size == 0) {
    return DS_OK;
  }
  d = t->dim;
  stack = (size_t *)ds_context_alloc(t->context,
                                     (t->page_count + 1) * sizeof(size_t));
  key = (double *)ds_context_alloc(t->context,
                                   (t->page_count + 1) * sizeof(double));
  dk = (double *)ds_context_alloc(t->context, k * sizeof(double));
  vk = (size_t *)ds_context_alloc(t->context, k * sizeof(size_t));
  if (stack == NULL || key == NULL || dk == NULL || vk == NULL) {
    ds_context_free(t->context, stack);
    ds_context_free(t->context, key);
    ds_context_free(t->context, dk);
    ds_context_free(t->context, vk);
    return DS_ERR_ALLOC;
  }
  stack[0] = t->root;
  key[0] = 0.0;
  top = 1;
  while (top > 0) {
    size_t v = stack[--top];
    const ds_kdb_page_t *pg = &t->pages[v];
    size_t base = top;
    if (found == k && !(key[top] < dk[k - 1])) {
      continue;
    }
    for (i = 0; i < pg->count; i++) {
      if (pg->leaf) {
        kd_best_push(dk, vk, k, &found, kd_dist2(pg->data + i * d, point, d),
                     pg->ref[i]);
      } else {
        double s = kd_mindist2(pg->data + i * 2 * d, point, d);
        if (found < k || s < dk[k - 1]) {
          kd_push_sorted(stack, key, base, &top, pg->ref[i], s);
        }
      }
    }
  }
  for (i = 0; i < found; i++) {
    out_values[i] = vk[i];
    out_dist[i] = sqrt(dk[i]);
  }
  *out_found = found;
  ds_context_free(t->context, stack);
  ds_context_free(t->context, key);
  ds_context_free(t->context, dk);
  ds_context_free(t->context, vk);
  return DS_OK;
}

static bool kdb_check(const ds_kdb_tree_t *t, size_t v, const double *region,
                      size_t depth, size_t *count) {
  size_t d = t->dim;
  const ds_kdb_page_t *pg = &t->pages[v];
  size_t i;
  size_t j;
  size_t a;
  if (pg->leaf) {
    if (depth != t->height - 1) {
      return false;
    }
    for (i = 0; i < pg->count; i++) {
      if (!kd_in_region(pg->data + i * d, region, d)) {
        return false;
      }
    }
    *count += pg->count;
    return true;
  }
  if (pg->count > t->fanout || pg->count == 0 || depth + 1 >= t->height) {
    return false;
  }
  for (i = 0; i < pg->count; i++) {
    const double *r = pg->data + i * 2 * d;
    for (a = 0; a < d; a++) {
      if (r[a] < region[a] || r[d + a] > region[d + a] || !(r[a] < r[d + a])) {
        return false;
      }
    }
    for (j = i + 1; j < pg->count; j++) {
      const double *q = pg->data + j * 2 * d;
      for (a = 0; a < d; a++) {
        if (!(r[a] < q[d + a] && q[a] < r[d + a])) {
          break;
        }
      }
      if (a == d) {
        return false; /* interiors overlap */
      }
    }
    if (!kdb_check(t, pg->ref[i], r, depth + 1, count)) {
      return false;
    }
  }
  return true;
}

bool ds_kdb_tree_valid(const ds_kdb_tree_t *t) {
  double all[2 * KD_MAXD];
  size_t count = 0;
  if (t == NULL || t->pages == NULL) {
    return false;
  }
  kd_all_space(all, t->dim);
  return kdb_check(t, t->root, all, 0, &count) && count == t->size;
}

/* ---- adaptive k-d tree
 * ------------------------------------------------------------- */

static void akd_swap(ds_akd_tree_t *t, size_t i, size_t j) {
  size_t d = t->dim;
  size_t a;
  size_t v;
  if (i == j) {
    return;
  }
  for (a = 0; a < d; a++) {
    double x = t->points[i * d + a];
    t->points[i * d + a] = t->points[j * d + a];
    t->points[j * d + a] = x;
  }
  v = t->values[i];
  t->values[i] = t->values[j];
  t->values[j] = v;
}

/* Move points of [lo, hi) with coordinate < x first; returns the count. */
static size_t akd_partition(ds_akd_tree_t *t, size_t lo, size_t hi, size_t axis,
                            double x) {
  size_t i;
  size_t m = lo;
  for (i = lo; i < hi; i++) {
    if (t->points[i * t->dim + axis] < x) {
      akd_swap(t, i, m++);
    }
  }
  return m - lo;
}

/* The value of rank k within [lo, hi) on axis (iterative quickselect). */
static double akd_select(ds_akd_tree_t *t, size_t lo, size_t hi, size_t axis,
                         size_t k) {
  size_t d = t->dim;
  while (hi - lo > 1) {
    double pv = t->points[(lo + (hi - lo) / 2) * d + axis];
    size_t i;
    size_t lt = lo;
    size_t gt = hi;
    /* three-way partition: < pv | == pv | > pv */
    i = lo;
    while (i < gt) {
      double c = t->points[i * d + axis];
      if (c < pv) {
        akd_swap(t, i++, lt++);
      } else if (c > pv) {
        akd_swap(t, i, --gt);
      } else {
        i++;
      }
    }
    if (k < lt) {
      hi = lt;
    } else if (k >= gt) {
      lo = gt;
    } else {
      return pv;
    }
  }
  return t->points[lo * d + axis];
}

typedef struct akd_work {
  size_t node;
  size_t first;
  size_t count;
  size_t depth;
  double cell[2 * KD_MAXD];
} akd_work_t;

ds_status_t ds_akd_tree_build(ds_akd_tree_t *t, size_t dim,
                              const double *points, const size_t *values,
                              size_t n, size_t bucket,
                              ds_kdb_axis_rule_t axis_rule,
                              ds_kdb_cut_rule_t cut_rule,
                              ds_context_t *context) {
  size_t cap;
  size_t i;
  size_t a;
  akd_work_t *work = NULL;
  size_t wn = 0;
  size_t wcap = 0;
  ds_status_t st = DS_OK;
  if (t == NULL || (n > 0 && points == NULL)) {
    return DS_ERR_NULL;
  }
  if (bucket == 0) {
    bucket = 8;
  }
  if (dim == 0 || dim > KD_MAXD ||
      ((int)axis_rule != DS_KDB_AXIS_SPREAD &&
       axis_rule != DS_KDB_AXIS_VARIANCE && axis_rule != DS_KDB_AXIS_CYCLIC) ||
      ((int)cut_rule != DS_KDB_CUT_MEDIAN && cut_rule != DS_KDB_CUT_MIDPOINT &&
       cut_rule != DS_KDB_CUT_SLIDING_MIDPOINT)) {
    return DS_ERR_INVALID;
  }
  if (!kd_finite(points, n * dim)) {
    return DS_ERR_INVALID;
  }
  memset(t, 0, sizeof(*t));
  t->dim = dim;
  t->count = n;
  t->context = context;
  cap = 2 * (n > 0 ? n : 1);
  t->points = (double *)ds_context_alloc(context, (n > 0 ? n : 1) * dim *
                                                      sizeof(double));
  t->values =
      (size_t *)ds_context_alloc(context, (n > 0 ? n : 1) * sizeof(size_t));
  t->axis = (size_t *)ds_context_alloc(context, cap * sizeof(size_t));
  t->cut = (double *)ds_context_alloc(context, cap * sizeof(double));
  t->left = (size_t *)ds_context_alloc(context, cap * sizeof(size_t));
  t->right = (size_t *)ds_context_alloc(context, cap * sizeof(size_t));
  t->bounds =
      (double *)ds_context_alloc(context, cap * 2 * dim * sizeof(double));
  if (t->points == NULL || t->values == NULL || t->axis == NULL ||
      t->cut == NULL || t->left == NULL || t->right == NULL ||
      t->bounds == NULL) {
    ds_akd_tree_destroy(t);
    return DS_ERR_ALLOC;
  }
  if (n > 0) {
    memcpy(t->points, points, n * dim * sizeof(double));
  }
  for (i = 0; i < n; i++) {
    t->values[i] = values != NULL ? values[i] : i;
  }
  {
    void *p = NULL;
    st = kd_grow(context, &p, &wcap, 16, sizeof(akd_work_t));
    work = (akd_work_t *)p;
    if (st != DS_OK) {
      ds_akd_tree_destroy(t);
      return st;
    }
  }
  t->nodes = 1;
  work[0].node = 0;
  work[0].first = 0;
  work[0].count = n;
  work[0].depth = 0;
  kd_empty_box(work[0].cell, dim);
  for (i = 0; i < n; i++) {
    for (a = 0; a < dim; a++) {
      double c = points[i * dim + a];
      if (c < work[0].cell[a]) {
        work[0].cell[a] = c;
      }
      if (c > work[0].cell[dim + a]) {
        work[0].cell[dim + a] = c;
      }
    }
  }
  wn = 1;
  while (wn > 0) {
    akd_work_t w = work[--wn];
    double *bb = t->bounds + w.node * 2 * dim;
    size_t axis = dim;
    double best = 0.0;
    double x = 0.0;
    size_t nl;
    size_t j;
    if (w.depth > t->depth) {
      t->depth = w.depth;
    }
    kd_empty_box(bb, dim);
    for (i = w.first; i < w.first + w.count; i++) {
      for (a = 0; a < dim; a++) {
        double c = t->points[i * dim + a];
        if (c < bb[a]) {
          bb[a] = c;
        }
        if (c > bb[dim + a]) {
          bb[dim + a] = c;
        }
      }
    }
    /* the axis */
    for (j = 0; j < dim; j++) {
      double score;
      a = axis_rule == DS_KDB_AXIS_CYCLIC ? (w.depth + j) % dim : j;
      if (!(bb[dim + a] > bb[a])) {
        continue;
      }
      if (axis_rule == DS_KDB_AXIS_VARIANCE) {
        double mean = 0.0;
        double var = 0.0;
        for (i = w.first; i < w.first + w.count; i++) {
          mean += t->points[i * dim + a];
        }
        mean /= (double)w.count;
        for (i = w.first; i < w.first + w.count; i++) {
          double dd = t->points[i * dim + a] - mean;
          var += dd * dd;
        }
        score = var;
      } else {
        score = bb[dim + a] - bb[a];
      }
      if (axis == dim || score > best) {
        best = score;
        axis = a;
        if (axis_rule == DS_KDB_AXIS_CYCLIC) {
          break;
        }
      }
    }
    if (w.count <= bucket || axis == dim) {
      t->axis[w.node] = KD_NONE;
      t->cut[w.node] = 0.0;
      t->left[w.node] = w.first;
      t->right[w.node] = w.count;
      continue;
    }
    /* the cut */
    if (cut_rule == DS_KDB_CUT_MEDIAN) {
      x = akd_select(t, w.first, w.first + w.count, axis,
                     w.first + w.count / 2);
    } else if (cut_rule == DS_KDB_CUT_MIDPOINT) {
      x = bb[axis] + (bb[dim + axis] - bb[axis]) / 2.0;
    } else {
      x = w.cell[axis] + (w.cell[dim + axis] - w.cell[axis]) / 2.0;
      if (x > bb[dim + axis]) {
        x = bb[dim + axis]; /* slide down: the largest points alone */
      }
    }
    nl = akd_partition(t, w.first, w.first + w.count, axis, x);
    if (nl == 0) {
      /* slide up past the smallest coordinate */
      double nx = HUGE_VAL;
      for (i = w.first; i < w.first + w.count; i++) {
        double c = t->points[i * dim + axis];
        if (c > bb[axis] && c < nx) {
          nx = c;
        }
      }
      x = nx;
      nl = akd_partition(t, w.first, w.first + w.count, axis, x);
    }
    if (wn + 2 > wcap) {
      void *p = work;
      st = kd_grow(context, &p, &wcap, wn + 2, sizeof(akd_work_t));
      work = (akd_work_t *)p;
      if (st != DS_OK) {
        break;
      }
    }
    t->axis[w.node] = axis;
    t->cut[w.node] = x;
    t->left[w.node] = t->nodes;
    t->right[w.node] = t->nodes + 1;
    work[wn].node = t->nodes;
    work[wn].first = w.first;
    work[wn].count = nl;
    work[wn].depth = w.depth + 1;
    memcpy(work[wn].cell, w.cell, 2 * dim * sizeof(double));
    work[wn].cell[dim + axis] = x;
    work[wn + 1].node = t->nodes + 1;
    work[wn + 1].first = w.first + nl;
    work[wn + 1].count = w.count - nl;
    work[wn + 1].depth = w.depth + 1;
    memcpy(work[wn + 1].cell, w.cell, 2 * dim * sizeof(double));
    work[wn + 1].cell[axis] = x;
    wn += 2;
    t->nodes += 2;
  }
  ds_context_free(context, work);
  if (st != DS_OK) {
    ds_akd_tree_destroy(t);
  }
  return st;
}

void ds_akd_tree_destroy(ds_akd_tree_t *t) {
  if (t == NULL) {
    return;
  }
  ds_context_free(t->context, t->points);
  ds_context_free(t->context, t->values);
  ds_context_free(t->context, t->axis);
  ds_context_free(t->context, t->cut);
  ds_context_free(t->context, t->left);
  ds_context_free(t->context, t->right);
  ds_context_free(t->context, t->bounds);
  memset(t, 0, sizeof(*t));
}

size_t ds_akd_tree_depth(const ds_akd_tree_t *t) {
  return t == NULL ? 0 : t->depth;
}

ds_status_t ds_akd_tree_query(const ds_akd_tree_t *t, const double *box,
                              ds_kdb_visit_func_t visit, void *user) {
  size_t *stack;
  size_t top = 0;
  size_t d;
  size_t i;
  ds_status_t st = DS_OK;
  if (t == NULL || box == NULL || visit == NULL) {
    return DS_ERR_NULL;
  }
  if (t->count == 0) {
    return DS_OK;
  }
  d = t->dim;
  stack = (size_t *)ds_context_alloc(t->context, t->nodes * sizeof(size_t));
  if (stack == NULL) {
    return DS_ERR_ALLOC;
  }
  stack[top++] = 0;
  while (top > 0 && st == DS_OK) {
    size_t v = stack[--top];
    if (!kd_box_hit(t->bounds + v * 2 * d, box, d)) {
      continue;
    }
    if (t->axis[v] != KD_NONE) {
      stack[top++] = t->left[v];
      stack[top++] = t->right[v];
      continue;
    }
    for (i = t->left[v]; i < t->left[v] + t->right[v]; i++) {
      if (kd_point_in_box(t->points + i * d, box, d) &&
          !visit(t->points + i * d, t->values[i], user)) {
        st = DS_STOP;
        break;
      }
    }
  }
  ds_context_free(t->context, stack);
  return st;
}

ds_status_t ds_akd_tree_nearest(const ds_akd_tree_t *t, const double *point,
                                size_t k, size_t *out_values, double *out_dist,
                                size_t *out_found) {
  size_t *stack;
  double *key;
  double *dk;
  size_t *vk;
  size_t top;
  size_t found = 0;
  size_t i;
  size_t d;
  if (t == NULL || point == NULL || out_found == NULL ||
      (k > 0 && (out_values == NULL || out_dist == NULL))) {
    return DS_ERR_NULL;
  }
  *out_found = 0;
  if (k == 0 || t->count == 0) {
    return DS_OK;
  }
  d = t->dim;
  stack =
      (size_t *)ds_context_alloc(t->context, (t->nodes + 1) * sizeof(size_t));
  key = (double *)ds_context_alloc(t->context, (t->nodes + 1) * sizeof(double));
  dk = (double *)ds_context_alloc(t->context, k * sizeof(double));
  vk = (size_t *)ds_context_alloc(t->context, k * sizeof(size_t));
  if (stack == NULL || key == NULL || dk == NULL || vk == NULL) {
    ds_context_free(t->context, stack);
    ds_context_free(t->context, key);
    ds_context_free(t->context, dk);
    ds_context_free(t->context, vk);
    return DS_ERR_ALLOC;
  }
  stack[0] = 0;
  key[0] = 0.0;
  top = 1;
  while (top > 0) {
    size_t v = stack[--top];
    size_t base = top;
    if (found == k && !(key[top] < dk[k - 1])) {
      continue;
    }
    if (t->axis[v] == KD_NONE) {
      for (i = t->left[v]; i < t->left[v] + t->right[v]; i++) {
        kd_best_push(dk, vk, k, &found, kd_dist2(t->points + i * d, point, d),
                     t->values[i]);
      }
      continue;
    }
    kd_push_sorted(stack, key, base, &top, t->left[v],
                   kd_mindist2(t->bounds + t->left[v] * 2 * d, point, d));
    kd_push_sorted(stack, key, base, &top, t->right[v],
                   kd_mindist2(t->bounds + t->right[v] * 2 * d, point, d));
  }
  for (i = 0; i < found; i++) {
    out_values[i] = vk[i];
    out_dist[i] = sqrt(dk[i]);
  }
  *out_found = found;
  ds_context_free(t->context, stack);
  ds_context_free(t->context, key);
  ds_context_free(t->context, dk);
  ds_context_free(t->context, vk);
  return DS_OK;
}

/* ---- object table (boxes)
 * ------------------------------------------------------------ */

static ds_status_t obj_add(ds_kdb_objects_t *o, ds_context_t *ctx, size_t d,
                           const double *box, size_t value, size_t *out) {
  size_t id;
  if (o->free_list != KD_NONE) {
    id = o->free_list;
    o->free_list = o->value[id];
  } else {
    if (o->count == o->capacity) {
      size_t nc = o->capacity < 8 ? 8 : o->capacity * 2;
      void *p = ds_context_realloc(ctx, o->box, nc * 2 * d * sizeof(double));
      if (p == NULL) {
        return DS_ERR_ALLOC;
      }
      o->box = (double *)p;
      p = ds_context_realloc(ctx, o->value, nc * sizeof(size_t));
      if (p == NULL) {
        return DS_ERR_ALLOC;
      }
      o->value = (size_t *)p;
      p = ds_context_realloc(ctx, o->alive, nc);
      if (p == NULL) {
        return DS_ERR_ALLOC;
      }
      o->alive = (unsigned char *)p;
      o->capacity = nc;
    }
    id = o->count++;
  }
  memcpy(o->box + id * 2 * d, box, 2 * d * sizeof(double));
  o->value[id] = value;
  o->alive[id] = 1;
  *out = id;
  return DS_OK;
}

static void obj_del(ds_kdb_objects_t *o, size_t id) {
  o->alive[id] = 0;
  o->value[id] = o->free_list;
  o->free_list = id;
}

static void obj_destroy(ds_kdb_objects_t *o, ds_context_t *ctx) {
  ds_context_free(ctx, o->box);
  ds_context_free(ctx, o->value);
  ds_context_free(ctx, o->alive);
  memset(o, 0, sizeof(*o));
  o->free_list = KD_NONE;
}

static bool obj_is(const ds_kdb_objects_t *o, size_t id, size_t d,
                   const double *box, size_t value) {
  return o->value[id] == value &&
         memcmp(o->box + id * 2 * d, box, 2 * d * sizeof(double)) == 0;
}

/* Node arrays shared by the two box trees. */
typedef struct kd_nodes {
  size_t **axis;
  double **cut;
  size_t **left;
  size_t **right;
  size_t ***items;
  size_t **item_count;
  size_t **item_capacity;
} kd_nodes_t;

static ds_status_t kd_nodes_grow(ds_context_t *ctx, kd_nodes_t *n, size_t *cap,
                                 size_t need) {
  size_t nc;
  void *p;
  if (need <= *cap) {
    return DS_OK;
  }
  nc = *cap < 8 ? 8 : *cap * 2;
  while (nc < need) {
    nc *= 2;
  }
#define KD_RE(field, type)                                                     \
  p = ds_context_realloc(ctx, *n->field, nc * sizeof(type));                   \
  if (p == NULL) {                                                             \
    return DS_ERR_ALLOC;                                                       \
  }                                                                            \
  *n->field = (type *)p;
  KD_RE(axis, size_t)
  KD_RE(cut, double)
  KD_RE(left, size_t)
  KD_RE(right, size_t)
  KD_RE(items, size_t *)
  KD_RE(item_count, size_t)
  KD_RE(item_capacity, size_t)
#undef KD_RE
  *cap = nc;
  return DS_OK;
}

static ds_status_t kd_item_add(ds_context_t *ctx, size_t **items, size_t *count,
                               size_t *cap, size_t id) {
  if (*count == *cap) {
    size_t nc = *cap < 4 ? 4 : *cap * 2;
    void *p = ds_context_realloc(ctx, *items, nc * sizeof(size_t));
    if (p == NULL) {
      return DS_ERR_ALLOC;
    }
    *items = (size_t *)p;
    *cap = nc;
  }
  (*items)[(*count)++] = id;
  return DS_OK;
}

/* ---- extended k-d tree
 * ------------------------------------------------------------------ */

static kd_nodes_t ekd_nodes(ds_ekd_tree_t *t) {
  kd_nodes_t n;
  n.axis = &t->axis;
  n.cut = &t->cut;
  n.left = &t->left;
  n.right = &t->right;
  n.items = &t->items;
  n.item_count = &t->item_count;
  n.item_capacity = &t->item_capacity;
  return n;
}

static ds_status_t ekd_new_leaf(ds_ekd_tree_t *t, size_t *out) {
  kd_nodes_t n = ekd_nodes(t);
  ds_status_t st =
      kd_nodes_grow(t->context, &n, &t->node_capacity, t->nodes + 1);
  size_t v;
  if (st != DS_OK) {
    return st;
  }
  v = t->nodes++;
  t->axis[v] = KD_NONE;
  t->cut[v] = 0.0;
  t->left[v] = t->right[v] = KD_NONE;
  t->items[v] = NULL;
  t->item_count[v] = 0;
  t->item_capacity[v] = 0;
  *out = v;
  return DS_OK;
}

ds_status_t ds_ekd_tree_init(ds_ekd_tree_t *t, size_t dim, size_t bucket,
                             size_t max_depth, ds_context_t *context) {
  size_t root;
  ds_status_t st;
  if (t == NULL) {
    return DS_ERR_NULL;
  }
  if (bucket == 0) {
    bucket = 8;
  }
  if (max_depth == 0) {
    max_depth = 32;
  }
  if (dim == 0 || dim > KD_MAXD) {
    return DS_ERR_INVALID;
  }
  memset(t, 0, sizeof(*t));
  t->dim = dim;
  t->bucket = bucket;
  t->max_depth = max_depth;
  t->objects.free_list = KD_NONE;
  t->context = context;
  st = ekd_new_leaf(t, &root);
  if (st != DS_OK) {
    ds_ekd_tree_destroy(t);
  }
  return st;
}

void ds_ekd_tree_destroy(ds_ekd_tree_t *t) {
  size_t v;
  if (t == NULL) {
    return;
  }
  for (v = 0; v < t->nodes; v++) {
    ds_context_free(t->context, t->items[v]);
  }
  obj_destroy(&t->objects, t->context);
  ds_context_free(t->context, t->axis);
  ds_context_free(t->context, t->cut);
  ds_context_free(t->context, t->left);
  ds_context_free(t->context, t->right);
  ds_context_free(t->context, t->items);
  ds_context_free(t->context, t->item_count);
  ds_context_free(t->context, t->item_capacity);
  ds_context_free(t->context, t->stack);
  ds_context_free(t->context, t->cells);
  memset(t, 0, sizeof(*t));
}

size_t ds_ekd_tree_size(const ds_ekd_tree_t *t) {
  return t == NULL ? 0 : t->size;
}

size_t ds_ekd_tree_entries(const ds_ekd_tree_t *t) {
  return t == NULL ? 0 : t->entries;
}

/* traversal stack: (node, depth) pairs plus a cell per entry */
static ds_status_t ekd_stack_need(ds_ekd_tree_t *t, size_t need) {
  size_t cap = t->stack_capacity;
  void *p;
  if (need <= cap) {
    return DS_OK;
  }
  cap = cap < 32 ? 32 : cap;
  while (cap < need) {
    cap *= 2;
  }
  p = ds_context_realloc(t->context, t->stack, 2 * cap * sizeof(size_t));
  if (p == NULL) {
    return DS_ERR_ALLOC;
  }
  t->stack = (size_t *)p;
  p = ds_context_realloc(t->context, t->cells,
                         cap * 2 * t->dim * sizeof(double));
  if (p == NULL) {
    return DS_ERR_ALLOC;
  }
  t->cells = (double *)p;
  t->stack_capacity = cap;
  return DS_OK;
}

/* Split an overfull leaf on the depth's axis if both halves shrink. */
static ds_status_t ekd_try_split(ds_ekd_tree_t *t, size_t v, size_t depth,
                                 const double *cell) {
  size_t d = t->dim;
  size_t axis = depth % d;
  size_t n = t->item_count[v];
  size_t i;
  size_t k;
  size_t l;
  size_t r;
  size_t lc = 0;
  size_t rc = 0;
  double *c;
  double cands[2];
  double x = 0.0;
  bool ok = false;
  ds_status_t st;
  c = (double *)ds_context_alloc(t->context, n * sizeof(double));
  if (c == NULL) {
    return DS_ERR_ALLOC;
  }
  for (i = 0; i < n; i++) {
    const double *b = t->objects.box + t->items[v][i] * 2 * d;
    c[i] = (b[axis] + b[d + axis]) / 2.0;
  }
  qsort(c, n, sizeof(double), kd_cmp_double);
  cands[0] = c[n / 2];
  cands[1] = cell[axis] > -HUGE_VAL && cell[d + axis] < HUGE_VAL
                 ? cell[axis] + (cell[d + axis] - cell[axis]) / 2.0
                 : c[n / 2];
  ds_context_free(t->context, c);
  for (k = 0; k < 2 && !ok; k++) {
    x = cands[k];
    if (!(x > cell[axis] && x < cell[d + axis])) {
      continue;
    }
    l = r = 0;
    for (i = 0; i < n; i++) {
      const double *b = t->objects.box + t->items[v][i] * 2 * d;
      l += b[axis] < x;
      r += b[d + axis] >= x;
    }
    ok = l < n && r < n;
  }
  if (!ok) {
    return DS_OK;
  }
  st = ekd_new_leaf(t, &lc);
  if (st == DS_OK) {
    st = ekd_new_leaf(t, &rc);
  }
  if (st != DS_OK) {
    return st;
  }
  for (i = 0; i < n && st == DS_OK; i++) {
    size_t id = t->items[v][i];
    const double *b = t->objects.box + id * 2 * d;
    if (b[axis] < x) {
      st = kd_item_add(t->context, &t->items[lc], &t->item_count[lc],
                       &t->item_capacity[lc], id);
    }
    if (st == DS_OK && b[d + axis] >= x) {
      st = kd_item_add(t->context, &t->items[rc], &t->item_count[rc],
                       &t->item_capacity[rc], id);
    }
  }
  if (st != DS_OK) {
    /* undo: the leaf keeps its items */
    ds_context_free(t->context, t->items[rc]);
    ds_context_free(t->context, t->items[lc]);
    t->nodes -= 2;
    return st;
  }
  t->entries += t->item_count[lc] + t->item_count[rc] - n;
  ds_context_free(t->context, t->items[v]);
  t->items[v] = NULL;
  t->item_count[v] = t->item_capacity[v] = 0;
  t->axis[v] = axis;
  t->cut[v] = x;
  t->left[v] = lc;
  t->right[v] = rc;
  return DS_OK;
}

ds_status_t ds_ekd_tree_insert(ds_ekd_tree_t *t, const double *box,
                               size_t value) {
  size_t d;
  size_t id;
  size_t top = 0;
  ds_status_t st;
  if (t == NULL || box == NULL) {
    return DS_ERR_NULL;
  }
  d = t->dim;
  if (!kd_box_ok(box, d)) {
    return DS_ERR_INVALID;
  }
  st = ekd_stack_need(t, t->nodes + 1);
  if (st != DS_OK) {
    return st;
  }
  st = obj_add(&t->objects, t->context, d, box, value, &id);
  if (st != DS_OK) {
    return st;
  }
  t->stack[0] = 0;
  t->stack[1] = 0;
  kd_all_space(t->cells, d);
  top = 1;
  while (top > 0 && st == DS_OK) {
    size_t v;
    size_t depth;
    double cell[2 * KD_MAXD];
    top--;
    v = t->stack[2 * top];
    depth = t->stack[2 * top + 1];
    memcpy(cell, t->cells + top * 2 * d, 2 * d * sizeof(double));
    if (t->axis[v] == KD_NONE) {
      st = kd_item_add(t->context, &t->items[v], &t->item_count[v],
                       &t->item_capacity[v], id);
      if (st != DS_OK) {
        break;
      }
      t->entries++;
      if (t->item_count[v] > t->bucket && depth < t->max_depth) {
        st = ekd_try_split(t, v, depth, cell);
      }
      continue;
    }
    {
      size_t a = t->axis[v];
      double x = t->cut[v];
      if (box[a] < x) {
        t->stack[2 * top] = t->left[v];
        t->stack[2 * top + 1] = depth + 1;
        memcpy(t->cells + top * 2 * d, cell, 2 * d * sizeof(double));
        t->cells[top * 2 * d + d + a] = x;
        top++;
      }
      if (box[d + a] >= x) {
        t->stack[2 * top] = t->right[v];
        t->stack[2 * top + 1] = depth + 1;
        memcpy(t->cells + top * 2 * d, cell, 2 * d * sizeof(double));
        t->cells[top * 2 * d + a] = x;
        top++;
      }
    }
  }
  if (st == DS_OK) {
    t->size++;
  }
  return st;
}

ds_status_t ds_ekd_tree_remove(ds_ekd_tree_t *t, const double *box,
                               size_t value) {
  size_t d;
  size_t id = KD_NONE;
  size_t top;
  ds_status_t st;
  if (t == NULL || box == NULL) {
    return DS_ERR_NULL;
  }
  d = t->dim;
  if (!kd_box_ok(box, d)) {
    return DS_NOT_FOUND;
  }
  st = ekd_stack_need(t, t->nodes + 1);
  if (st != DS_OK) {
    return st;
  }
  t->stack[0] = 0;
  top = 1;
  while (top > 0) {
    size_t v = t->stack[2 * --top];
    if (t->axis[v] == KD_NONE) {
      size_t i;
      for (i = 0; i < t->item_count[v]; i++) {
        size_t it = t->items[v][i];
        if (id == KD_NONE ? obj_is(&t->objects, it, d, box, value) : it == id) {
          break;
        }
      }
      if (i == t->item_count[v]) {
        if (id == KD_NONE) {
          return DS_NOT_FOUND; /* the first leaf it meets lacks it */
        }
        return DS_ERR_INTERNAL;
      }
      id = t->items[v][i];
      t->items[v][i] = t->items[v][--t->item_count[v]];
      t->entries--;
      continue;
    }
    if (box[t->axis[v]] < t->cut[v]) {
      t->stack[2 * top++] = t->left[v];
    }
    if (box[d + t->axis[v]] >= t->cut[v]) {
      t->stack[2 * top++] = t->right[v];
    }
  }
  if (id == KD_NONE) {
    return DS_NOT_FOUND;
  }
  obj_del(&t->objects, id);
  t->size--;
  return DS_OK;
}

ds_status_t ds_ekd_tree_query(ds_ekd_tree_t *t, const double *box,
                              ds_kdb_visit_func_t visit, void *user) {
  size_t d;
  size_t top;
  size_t a;
  ds_status_t st;
  if (t == NULL || box == NULL || visit == NULL) {
    return DS_ERR_NULL;
  }
  d = t->dim;
  st = ekd_stack_need(t, t->nodes + 1);
  if (st != DS_OK) {
    return st;
  }
  t->stack[0] = 0;
  kd_all_space(t->cells, d);
  top = 1;
  while (top > 0) {
    size_t v;
    double cell[2 * KD_MAXD];
    top--;
    v = t->stack[2 * top];
    memcpy(cell, t->cells + top * 2 * d, 2 * d * sizeof(double));
    if (t->axis[v] == KD_NONE) {
      size_t i;
      for (i = 0; i < t->item_count[v]; i++) {
        size_t id = t->items[v][i];
        const double *b = t->objects.box + id * 2 * d;
        double p[KD_MAXD];
        if (!kd_box_hit(b, box, d)) {
          continue;
        }
        for (a = 0; a < d; a++) {
          p[a] = b[a] > box[a] ? b[a] : box[a];
        }
        if (kd_in_region(p, cell, d) && !visit(b, t->objects.value[id], user)) {
          return DS_STOP;
        }
      }
      continue;
    }
    a = t->axis[v];
    if (box[a] < t->cut[v]) {
      t->stack[2 * top] = t->left[v];
      memcpy(t->cells + top * 2 * d, cell, 2 * d * sizeof(double));
      t->cells[top * 2 * d + d + a] = t->cut[v];
      top++;
    }
    if (box[d + a] >= t->cut[v]) {
      t->stack[2 * top] = t->right[v];
      memcpy(t->cells + top * 2 * d, cell, 2 * d * sizeof(double));
      t->cells[top * 2 * d + a] = t->cut[v];
      top++;
    }
  }
  return DS_OK;
}

/* ---- skd-tree
 * -----------------------------------------------------------------------------
 */

static kd_nodes_t skd_nodes(ds_skd_tree_t *t) {
  kd_nodes_t n;
  n.axis = &t->axis;
  n.cut = &t->cut;
  n.left = &t->left;
  n.right = &t->right;
  n.items = &t->items;
  n.item_count = &t->item_count;
  n.item_capacity = &t->item_capacity;
  return n;
}

static ds_status_t skd_new_leaf(ds_skd_tree_t *t, size_t *out) {
  size_t v;
  if (t->free_list != KD_NONE) {
    v = t->free_list;
    t->free_list = t->left[v];
  } else {
    size_t cap = t->node_capacity;
    kd_nodes_t n = skd_nodes(t);
    ds_status_t st = kd_nodes_grow(t->context, &n, &cap, t->nodes + 1);
    void *p;
    if (st != DS_OK) {
      return st;
    }
    if (cap != t->node_capacity) {
      p = ds_context_realloc(t->context, t->parent, cap * sizeof(size_t));
      if (p == NULL) {
        return DS_ERR_ALLOC;
      }
      t->parent = (size_t *)p;
      p = ds_context_realloc(t->context, t->bounds,
                             cap * 2 * t->dim * sizeof(double));
      if (p == NULL) {
        return DS_ERR_ALLOC;
      }
      t->bounds = (double *)p;
      t->node_capacity = cap;
    }
    v = t->nodes++;
  }
  t->axis[v] = KD_NONE;
  t->cut[v] = 0.0;
  t->left[v] = t->right[v] = KD_NONE;
  t->parent[v] = KD_NONE;
  t->items[v] = NULL;
  t->item_count[v] = 0;
  t->item_capacity[v] = 0;
  kd_empty_box(t->bounds + v * 2 * t->dim, t->dim);
  *out = v;
  return DS_OK;
}

static void skd_free_node(ds_skd_tree_t *t, size_t v) {
  ds_context_free(t->context, t->items[v]);
  t->items[v] = NULL;
  t->item_count[v] = t->item_capacity[v] = 0;
  t->axis[v] = KD_NONE;
  t->left[v] = t->free_list;
  t->free_list = v;
}

ds_status_t ds_skd_tree_init(ds_skd_tree_t *t, size_t dim, size_t bucket,
                             ds_context_t *context) {
  ds_status_t st;
  if (t == NULL) {
    return DS_ERR_NULL;
  }
  if (bucket == 0) {
    bucket = 8;
  }
  if (dim == 0 || dim > KD_MAXD || bucket < 2) {
    return DS_ERR_INVALID;
  }
  memset(t, 0, sizeof(*t));
  t->dim = dim;
  t->bucket = bucket;
  t->objects.free_list = KD_NONE;
  t->free_list = KD_NONE;
  t->context = context;
  st = skd_new_leaf(t, &t->root);
  if (st != DS_OK) {
    ds_skd_tree_destroy(t);
  }
  return st;
}

void ds_skd_tree_destroy(ds_skd_tree_t *t) {
  size_t v;
  if (t == NULL) {
    return;
  }
  for (v = 0; v < t->nodes; v++) {
    ds_context_free(t->context, t->items[v]);
  }
  obj_destroy(&t->objects, t->context);
  ds_context_free(t->context, t->axis);
  ds_context_free(t->context, t->cut);
  ds_context_free(t->context, t->left);
  ds_context_free(t->context, t->right);
  ds_context_free(t->context, t->parent);
  ds_context_free(t->context, t->bounds);
  ds_context_free(t->context, t->items);
  ds_context_free(t->context, t->item_count);
  ds_context_free(t->context, t->item_capacity);
  ds_context_free(t->context, t->stack);
  memset(t, 0, sizeof(*t));
}

size_t ds_skd_tree_size(const ds_skd_tree_t *t) {
  return t == NULL ? 0 : t->size;
}

static double skd_centre(const ds_skd_tree_t *t, size_t id, size_t a) {
  const double *b = t->objects.box + id * 2 * t->dim;
  return (b[a] + b[t->dim + a]) / 2.0;
}

static void skd_leaf_bounds(ds_skd_tree_t *t, size_t v) {
  size_t i;
  double *bb = t->bounds + v * 2 * t->dim;
  kd_empty_box(bb, t->dim);
  for (i = 0; i < t->item_count[v]; i++) {
    kd_extend(bb, t->objects.box + t->items[v][i] * 2 * t->dim, t->dim);
  }
}

static void skd_node_bounds(ds_skd_tree_t *t, size_t v) {
  double *bb = t->bounds + v * 2 * t->dim;
  if (t->axis[v] == KD_NONE) {
    skd_leaf_bounds(t, v);
    return;
  }
  memcpy(bb, t->bounds + t->left[v] * 2 * t->dim, 2 * t->dim * sizeof(double));
  kd_extend(bb, t->bounds + t->right[v] * 2 * t->dim, t->dim);
}

/* Split an overfull leaf at the median centroid on the widest axis. */
static ds_status_t skd_split(ds_skd_tree_t *t, size_t v) {
  size_t d = t->dim;
  size_t n = t->item_count[v];
  size_t a;
  size_t i;
  size_t axis = d;
  size_t lc;
  size_t rc;
  double best = 0.0;
  double x;
  double *c;
  ds_status_t st;
  for (a = 0; a < d; a++) {
    double lo = HUGE_VAL;
    double hi = -HUGE_VAL;
    for (i = 0; i < n; i++) {
      double m = skd_centre(t, t->items[v][i], a);
      lo = m < lo ? m : lo;
      hi = m > hi ? m : hi;
    }
    if (hi - lo > best) {
      best = hi - lo;
      axis = a;
    }
  }
  if (axis == d) {
    return DS_OK; /* coinciding centroids */
  }
  c = (double *)ds_context_alloc(t->context, n * sizeof(double));
  if (c == NULL) {
    return DS_ERR_ALLOC;
  }
  for (i = 0; i < n; i++) {
    c[i] = skd_centre(t, t->items[v][i], axis);
  }
  qsort(c, n, sizeof(double), kd_cmp_double);
  x = c[n / 2];
  if (!(x > c[0])) {
    for (i = 1; i < n && !(c[i] > c[0]); i++) {
    }
    x = c[i];
  }
  ds_context_free(t->context, c);
  st = skd_new_leaf(t, &lc);
  if (st != DS_OK) {
    return st;
  }
  st = skd_new_leaf(t, &rc);
  if (st != DS_OK) {
    skd_free_node(t, lc);
    return st;
  }
  for (i = 0; i < n && st == DS_OK; i++) {
    size_t id = t->items[v][i];
    size_t to = skd_centre(t, id, axis) < x ? lc : rc;
    st = kd_item_add(t->context, &t->items[to], &t->item_count[to],
                     &t->item_capacity[to], id);
  }
  if (st != DS_OK) {
    skd_free_node(t, lc);
    skd_free_node(t, rc);
    return st;
  }
  ds_context_free(t->context, t->items[v]);
  t->items[v] = NULL;
  t->item_count[v] = t->item_capacity[v] = 0;
  t->axis[v] = axis;
  t->cut[v] = x;
  t->left[v] = lc;
  t->right[v] = rc;
  t->parent[lc] = t->parent[rc] = v;
  skd_leaf_bounds(t, lc);
  skd_leaf_bounds(t, rc);
  return DS_OK;
}

ds_status_t ds_skd_tree_insert(ds_skd_tree_t *t, const double *box,
                               size_t value) {
  size_t d;
  size_t id;
  size_t v;
  ds_status_t st;
  if (t == NULL || box == NULL) {
    return DS_ERR_NULL;
  }
  d = t->dim;
  if (!kd_box_ok(box, d)) {
    return DS_ERR_INVALID;
  }
  st = obj_add(&t->objects, t->context, d, box, value, &id);
  if (st != DS_OK) {
    return st;
  }
  v = t->root;
  while (t->axis[v] != KD_NONE) {
    v = skd_centre(t, id, t->axis[v]) < t->cut[v] ? t->left[v] : t->right[v];
  }
  st = kd_item_add(t->context, &t->items[v], &t->item_count[v],
                   &t->item_capacity[v], id);
  if (st != DS_OK) {
    obj_del(&t->objects, id);
    return st;
  }
  t->size++;
  /* bounds up the path (they only grow) */
  {
    size_t u;
    for (u = v; u != KD_NONE; u = t->parent[u]) {
      kd_extend(t->bounds + u * 2 * d, box, d);
    }
  }
  if (t->item_count[v] > t->bucket) {
    return skd_split(t, v);
  }
  return DS_OK;
}

ds_status_t ds_skd_tree_remove(ds_skd_tree_t *t, const double *box,
                               size_t value) {
  size_t d;
  size_t v;
  size_t i;
  size_t u;
  if (t == NULL || box == NULL) {
    return DS_ERR_NULL;
  }
  d = t->dim;
  if (!kd_box_ok(box, d)) {
    return DS_NOT_FOUND;
  }
  v = t->root;
  while (t->axis[v] != KD_NONE) {
    double m = (box[t->axis[v]] + box[d + t->axis[v]]) / 2.0;
    v = m < t->cut[v] ? t->left[v] : t->right[v];
  }
  for (i = 0; i < t->item_count[v]; i++) {
    if (obj_is(&t->objects, t->items[v][i], d, box, value)) {
      break;
    }
  }
  if (i == t->item_count[v]) {
    return DS_NOT_FOUND;
  }
  obj_del(&t->objects, t->items[v][i]);
  t->items[v][i] = t->items[v][--t->item_count[v]];
  t->size--;
  /* merge sibling leaves that fit one bucket, then fix bounds upward */
  u = t->parent[v];
  if (u != KD_NONE && t->axis[t->left[u]] == KD_NONE &&
      t->axis[t->right[u]] == KD_NONE &&
      t->item_count[t->left[u]] + t->item_count[t->right[u]] <= t->bucket) {
    size_t lc = t->left[u];
    size_t rc = t->right[u];
    size_t mc = t->item_count[lc] + t->item_count[rc];
    void *p =
        mc <= t->item_capacity[lc]
            ? (void *)t->items[lc]
            : ds_context_realloc(t->context, t->items[lc], mc * sizeof(size_t));
    if (p != NULL || mc == 0) {
      size_t *merged = (size_t *)p;
      if (t->item_count[rc] > 0) {
        memcpy(merged + t->item_count[lc], t->items[rc],
               t->item_count[rc] * sizeof(size_t));
      }
      t->items[u] = merged;
      t->item_count[u] = mc;
      t->item_capacity[u] =
          mc <= t->item_capacity[lc] ? t->item_capacity[lc] : mc;
      t->items[lc] = NULL;
      t->item_count[lc] = t->item_capacity[lc] = 0;
      t->axis[u] = KD_NONE;
      t->left[u] = t->right[u] = KD_NONE;
      skd_free_node(t, lc);
      skd_free_node(t, rc);
      v = u;
    }
    /* on allocation failure the tree simply stays unmerged */
  }
  for (u = v; u != KD_NONE; u = t->parent[u]) {
    skd_node_bounds(t, u);
  }
  return DS_OK;
}

static ds_status_t skd_stack_need(ds_skd_tree_t *t, size_t need) {
  void *p = t->stack;
  ds_status_t st =
      kd_grow(t->context, &p, &t->stack_capacity, need, sizeof(size_t));
  t->stack = (size_t *)p;
  return st;
}

ds_status_t ds_skd_tree_query(ds_skd_tree_t *t, const double *box,
                              ds_kdb_visit_func_t visit, void *user) {
  size_t d;
  size_t top = 0;
  size_t i;
  ds_status_t st;
  if (t == NULL || box == NULL || visit == NULL) {
    return DS_ERR_NULL;
  }
  d = t->dim;
  st = skd_stack_need(t, t->nodes + 1);
  if (st != DS_OK) {
    return st;
  }
  t->stack[top++] = t->root;
  while (top > 0) {
    size_t v = t->stack[--top];
    if (!kd_box_hit(t->bounds + v * 2 * d, box, d)) {
      continue;
    }
    if (t->axis[v] != KD_NONE) {
      t->stack[top++] = t->left[v];
      t->stack[top++] = t->right[v];
      continue;
    }
    for (i = 0; i < t->item_count[v]; i++) {
      size_t id = t->items[v][i];
      const double *b = t->objects.box + id * 2 * d;
      if (kd_box_hit(b, box, d) && !visit(b, t->objects.value[id], user)) {
        return DS_STOP;
      }
    }
  }
  return DS_OK;
}

ds_status_t ds_skd_tree_query_point(ds_skd_tree_t *t, const double *point,
                                    ds_kdb_visit_func_t visit, void *user) {
  double b[2 * KD_MAXD];
  if (t == NULL || point == NULL || visit == NULL) {
    return DS_ERR_NULL;
  }
  memcpy(b, point, t->dim * sizeof(double));
  memcpy(b + t->dim, point, t->dim * sizeof(double));
  return ds_skd_tree_query(t, b, visit, user);
}

ds_status_t ds_skd_tree_nearest(ds_skd_tree_t *t, const double *point, size_t k,
                                size_t *out_values, double *out_dist,
                                size_t *out_found) {
  double *key;
  double *dk;
  size_t *vk;
  size_t top;
  size_t found = 0;
  size_t i;
  size_t d;
  ds_status_t st;
  if (t == NULL || point == NULL || out_found == NULL ||
      (k > 0 && (out_values == NULL || out_dist == NULL))) {
    return DS_ERR_NULL;
  }
  *out_found = 0;
  if (k == 0 || t->size == 0) {
    return DS_OK;
  }
  d = t->dim;
  st = skd_stack_need(t, t->nodes + 1);
  if (st != DS_OK) {
    return st;
  }
  key = (double *)ds_context_alloc(t->context, (t->nodes + 1) * sizeof(double));
  dk = (double *)ds_context_alloc(t->context, k * sizeof(double));
  vk = (size_t *)ds_context_alloc(t->context, k * sizeof(size_t));
  if (key == NULL || dk == NULL || vk == NULL) {
    ds_context_free(t->context, key);
    ds_context_free(t->context, dk);
    ds_context_free(t->context, vk);
    return DS_ERR_ALLOC;
  }
  t->stack[0] = t->root;
  key[0] = 0.0;
  top = 1;
  while (top > 0) {
    size_t v = t->stack[--top];
    size_t base = top;
    if (found == k && !(key[top] < dk[k - 1])) {
      continue;
    }
    if (t->axis[v] == KD_NONE) {
      for (i = 0; i < t->item_count[v]; i++) {
        size_t id = t->items[v][i];
        kd_best_push(dk, vk, k, &found,
                     kd_mindist2(t->objects.box + id * 2 * d, point, d),
                     t->objects.value[id]);
      }
      continue;
    }
    kd_push_sorted(t->stack, key, base, &top, t->left[v],
                   kd_mindist2(t->bounds + t->left[v] * 2 * d, point, d));
    kd_push_sorted(t->stack, key, base, &top, t->right[v],
                   kd_mindist2(t->bounds + t->right[v] * 2 * d, point, d));
  }
  for (i = 0; i < found; i++) {
    out_values[i] = vk[i];
    out_dist[i] = sqrt(dk[i]);
  }
  *out_found = found;
  ds_context_free(t->context, key);
  ds_context_free(t->context, dk);
  ds_context_free(t->context, vk);
  return DS_OK;
}

bool ds_skd_tree_valid(const ds_skd_tree_t *t) {
  size_t d;
  size_t *stack;
  double *lim; /* per stack entry: centroid limits [lo, hi) */
  size_t top = 0;
  size_t count = 0;
  bool ok = true;
  if (t == NULL || t->axis == NULL) {
    return false;
  }
  d = t->dim;
  stack =
      (size_t *)ds_context_alloc(t->context, (t->nodes + 1) * sizeof(size_t));
  lim = (double *)ds_context_alloc(t->context,
                                   (t->nodes + 1) * 2 * d * sizeof(double));
  if (stack == NULL || lim == NULL) {
    ds_context_free(t->context, stack);
    ds_context_free(t->context, lim);
    return false;
  }
  stack[top] = t->root;
  kd_all_space(lim, d);
  top++;
  while (top > 0 && ok) {
    size_t v = stack[--top];
    double l[2 * KD_MAXD];
    double bb[2 * KD_MAXD];
    size_t i;
    size_t a;
    memcpy(l, lim + top * 2 * d, 2 * d * sizeof(double));
    kd_empty_box(bb, d);
    if (t->axis[v] == KD_NONE) {
      for (i = 0; i < t->item_count[v] && ok; i++) {
        size_t id = t->items[v][i];
        double c[KD_MAXD];
        if (!t->objects.alive[id]) {
          ok = false;
          break;
        }
        for (a = 0; a < d; a++) {
          c[a] = skd_centre(t, id, a);
        }
        ok = kd_in_region(c, l, d);
        kd_extend(bb, t->objects.box + id * 2 * d, d);
      }
      count += t->item_count[v];
    } else {
      size_t lc = t->left[v];
      size_t rc = t->right[v];
      ok = t->parent[lc] == v && t->parent[rc] == v;
      memcpy(bb, t->bounds + lc * 2 * d, 2 * d * sizeof(double));
      kd_extend(bb, t->bounds + rc * 2 * d, d);
      stack[top] = lc;
      memcpy(lim + top * 2 * d, l, 2 * d * sizeof(double));
      lim[top * 2 * d + d + t->axis[v]] = t->cut[v];
      top++;
      stack[top] = rc;
      memcpy(lim + top * 2 * d, l, 2 * d * sizeof(double));
      lim[top * 2 * d + t->axis[v]] = t->cut[v];
      top++;
    }
    if (ok && memcmp(bb, t->bounds + v * 2 * d, 2 * d * sizeof(double)) != 0) {
      ok = false;
    }
  }
  ds_context_free(t->context, stack);
  ds_context_free(t->context, lim);
  return ok && count == t->size;
}
