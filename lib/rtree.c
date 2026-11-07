#include "rtree.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

#define RT_NONE (~(size_t)0)

/* ---- box helpers ------------------------------------------------------------
 */

static double bx_area(const double *b, size_t d) {
  size_t a;
  double s = 1.0;
  for (a = 0; a < d; a++) {
    s *= b[d + a] - b[a];
  }
  return s;
}

static double bx_margin(const double *b, size_t d) {
  size_t a;
  double s = 0.0;
  for (a = 0; a < d; a++) {
    s += b[d + a] - b[a];
  }
  return s;
}

static void bx_union(const double *x, const double *y, double *out, size_t d) {
  size_t a;
  for (a = 0; a < d; a++) {
    out[a] = x[a] < y[a] ? x[a] : y[a];
    out[d + a] = x[d + a] > y[d + a] ? x[d + a] : y[d + a];
  }
}

static double bx_enlarge(const double *x, const double *y, size_t d) {
  double u[2 * DS_RTREE_MAX_DIM];
  bx_union(x, y, u, d);
  return bx_area(u, d) - bx_area(x, d);
}

static double bx_overlap(const double *x, const double *y, size_t d) {
  size_t a;
  double s = 1.0;
  for (a = 0; a < d; a++) {
    double lo = x[a] > y[a] ? x[a] : y[a];
    double hi = x[d + a] < y[d + a] ? x[d + a] : y[d + a];
    if (hi <= lo) {
      return 0.0;
    }
    s *= hi - lo;
  }
  return s;
}

static bool bx_hit(const double *x, const double *y, size_t d) {
  size_t a;
  for (a = 0; a < d; a++) {
    if (x[d + a] < y[a] || y[d + a] < x[a]) {
      return false;
    }
  }
  return true;
}

static bool bx_contains(const double *outer, const double *inner, size_t d) {
  size_t a;
  for (a = 0; a < d; a++) {
    if (inner[a] < outer[a] || inner[d + a] > outer[d + a]) {
      return false;
    }
  }
  return true;
}

static double bx_mindist2(const double *b, const double *p, size_t d) {
  size_t a;
  double s = 0.0;
  double t;
  for (a = 0; a < d; a++) {
    t = p[a] < b[a] ? b[a] - p[a] : (p[a] > b[d + a] ? p[a] - b[d + a] : 0.0);
    s += t * t;
  }
  return s;
}

/* ---- node storage
 * -------------------------------------------------------------- */

#define RT_E(t) ((t)->max_entries + 1)

static double *rt_box(const ds_rtree_t *t, size_t v, size_t i) {
  return t->box + (v * RT_E(t) + i) * 2 * t->dim;
}

static size_t *rt_ref(const ds_rtree_t *t, size_t v, size_t i) {
  return t->ref + v * RT_E(t) + i;
}

static ds_status_t rt_node(ds_rtree_t *t, bool leaf, size_t level,
                           size_t *out) {
  size_t v;
  size_t cap;
  void *p;
  if (t->free_list != RT_NONE) {
    v = t->free_list;
    t->free_list = t->count[v];
  } else {
    if (t->nodes == t->capacity) {
      cap = t->capacity < 8 ? 8 : t->capacity * 2;
      if (cap > ((size_t)-1) / RT_E(t) / (2 * t->dim * sizeof(double))) {
        return DS_ERR_OVERFLOW;
      }
      p = ds_context_realloc(t->context, t->count, cap * sizeof(size_t));
      if (p == NULL) {
        return DS_ERR_ALLOC;
      }
      t->count = (size_t *)p;
      p = ds_context_realloc(t->context, t->level, cap * sizeof(size_t));
      if (p == NULL) {
        return DS_ERR_ALLOC;
      }
      t->level = (size_t *)p;
      p = ds_context_realloc(t->context, t->parent, cap * sizeof(size_t));
      if (p == NULL) {
        return DS_ERR_ALLOC;
      }
      t->parent = (size_t *)p;
      p = ds_context_realloc(t->context, t->leaf, cap);
      if (p == NULL) {
        return DS_ERR_ALLOC;
      }
      t->leaf = (unsigned char *)p;
      p = ds_context_realloc(t->context, t->box,
                             cap * RT_E(t) * 2 * t->dim * sizeof(double));
      if (p == NULL) {
        return DS_ERR_ALLOC;
      }
      t->box = (double *)p;
      p = ds_context_realloc(t->context, t->ref,
                             cap * RT_E(t) * sizeof(size_t));
      if (p == NULL) {
        return DS_ERR_ALLOC;
      }
      t->ref = (size_t *)p;
      t->capacity = cap;
    }
    v = t->nodes++;
  }
  t->count[v] = 0;
  t->leaf[v] = (unsigned char)leaf;
  t->level[v] = level;
  t->parent[v] = RT_NONE;
  *out = v;
  return DS_OK;
}

static void rt_free_node(ds_rtree_t *t, size_t v) {
  t->count[v] = t->free_list;
  t->free_list = v;
}

/* Bounding box of all entries of v. */
static void rt_cover(const ds_rtree_t *t, size_t v, double *out) {
  size_t i;
  memcpy(out, rt_box(t, v, 0), 2 * t->dim * sizeof(double));
  for (i = 1; i < t->count[v]; i++) {
    bx_union(out, rt_box(t, v, i), out, t->dim);
  }
}

static size_t rt_slot_in_parent(const ds_rtree_t *t, size_t v) {
  size_t p = t->parent[v];
  size_t i;
  for (i = 0; i < t->count[p]; i++) {
    if (*rt_ref(t, p, i) == v) {
      return i;
    }
  }
  return RT_NONE;
}

/* Recompute the boxes on the path from v to the root. */
static void rt_adjust(ds_rtree_t *t, size_t v) {
  size_t i;
  while (t->parent[v] != RT_NONE) {
    i = rt_slot_in_parent(t, v);
    rt_cover(t, v, rt_box(t, t->parent[v], i));
    v = t->parent[v];
  }
}

ds_status_t ds_rtree_init(ds_rtree_t *t, size_t dim, size_t max_entries,
                          ds_rtree_variant_t variant, ds_context_t *context) {
  ds_status_t st;
  if (t == NULL) {
    return DS_ERR_NULL;
  }
  if (max_entries == 0) {
    max_entries = 8;
  }
  if (dim == 0 || dim > DS_RTREE_MAX_DIM || max_entries < 4 ||
      max_entries > 1024 ||
      ((int)variant != DS_RTREE_GUTTMAN && variant != DS_RTREE_RSTAR)) {
    return DS_ERR_INVALID;
  }
  memset(t, 0, sizeof(*t));
  t->dim = dim;
  t->max_entries = max_entries;
  t->min_entries = max_entries * 2 / 5 > 2 ? max_entries * 2 / 5 : 2;
  t->variant = variant;
  t->free_list = RT_NONE;
  t->context = context;
  st = rt_node(t, true, 0, &t->root);
  if (st != DS_OK) {
    ds_rtree_destroy(t);
    return st;
  }
  t->height = 1;
  return DS_OK;
}

void ds_rtree_destroy(ds_rtree_t *t) {
  if (t == NULL) {
    return;
  }
  ds_context_free(t->context, t->count);
  ds_context_free(t->context, t->level);
  ds_context_free(t->context, t->parent);
  ds_context_free(t->context, t->leaf);
  ds_context_free(t->context, t->box);
  ds_context_free(t->context, t->ref);
  ds_context_free(t->context, t->reinserted);
  ds_context_free(t->context, t->stack);
  memset(t, 0, sizeof(*t));
  t->free_list = RT_NONE;
}

size_t ds_rtree_size(const ds_rtree_t *t) {
  return t == NULL ? 0 : t->size;
}

size_t ds_rtree_height(const ds_rtree_t *t) {
  return t == NULL ? 0 : t->height;
}

/* ---- choosing and splitting
 * ------------------------------------------------------- */

static size_t rt_choose(const ds_rtree_t *t, size_t v, const double *b) {
  size_t i;
  size_t j;
  size_t best = 0;
  double be = HUGE_VAL;
  double ba = HUGE_VAL;
  double bo = HUGE_VAL;
  size_t d = t->dim;
  bool overlap_mode = t->variant == DS_RTREE_RSTAR && t->level[v] == 1;
  for (i = 0; i < t->count[v]; i++) {
    const double *e = rt_box(t, v, i);
    double enl = bx_enlarge(e, b, d);
    double area = bx_area(e, d);
    if (overlap_mode) {
      /* overlap enlargement against the siblings */
      double u[2 * DS_RTREE_MAX_DIM];
      double ov = 0.0;
      bx_union(e, b, u, d);
      for (j = 0; j < t->count[v]; j++) {
        if (j != i) {
          ov += bx_overlap(u, rt_box(t, v, j), d) -
                bx_overlap(e, rt_box(t, v, j), d);
        }
      }
      if (ov < bo || (ov <= bo && (enl < be || (enl <= be && area < ba)))) {
        bo = ov;
        be = enl;
        ba = area;
        best = i;
      }
    } else if (enl < be || (enl <= be && area < ba)) {
      be = enl;
      ba = area;
      best = i;
    }
  }
  return best;
}

static void rt_split_quadratic(ds_rtree_t *t, const double *box, size_t n,
                               unsigned char *group) {
  size_t d = t->dim;
  size_t i;
  size_t j;
  size_t s1 = 0;
  size_t s2 = 1;
  size_t n1 = 1;
  size_t n2 = 1;
  size_t left = n - 2;
  double worst = -HUGE_VAL;
  double c1[2 * DS_RTREE_MAX_DIM];
  double c2[2 * DS_RTREE_MAX_DIM];
  for (i = 0; i < n; i++) {
    for (j = i + 1; j < n; j++) {
      double u[2 * DS_RTREE_MAX_DIM];
      double waste;
      bx_union(box + i * 2 * d, box + j * 2 * d, u, d);
      waste = bx_area(u, d) - bx_area(box + i * 2 * d, d) -
              bx_area(box + j * 2 * d, d);
      if (waste > worst) {
        worst = waste;
        s1 = i;
        s2 = j;
      }
    }
  }
  for (i = 0; i < n; i++) {
    group[i] = 0;
  }
  group[s1] = 1;
  group[s2] = 2;
  memcpy(c1, box + s1 * 2 * d, 2 * d * sizeof(double));
  memcpy(c2, box + s2 * 2 * d, 2 * d * sizeof(double));
  while (left > 0) {
    size_t pick = RT_NONE;
    double pd = -1.0;
    double e1 = 0.0;
    double e2 = 0.0;
    if (n1 + left == t->min_entries || n2 + left == t->min_entries) {
      unsigned char g = n1 + left == t->min_entries ? 1 : 2;
      for (i = 0; i < n; i++) {
        if (group[i] == 0) {
          group[i] = g;
        }
      }
      break;
    }
    for (i = 0; i < n; i++) {
      double a1;
      double a2;
      if (group[i] != 0) {
        continue;
      }
      a1 = bx_enlarge(c1, box + i * 2 * d, d);
      a2 = bx_enlarge(c2, box + i * 2 * d, d);
      if (fabs(a1 - a2) > pd) {
        pd = fabs(a1 - a2);
        pick = i;
        e1 = a1;
        e2 = a2;
      }
    }
    if (e1 < e2 ||
        (e1 <= e2 && (bx_area(c1, d) < bx_area(c2, d) ||
                      (bx_area(c1, d) <= bx_area(c2, d) && n1 <= n2)))) {
      group[pick] = 1;
      bx_union(c1, box + pick * 2 * d, c1, d);
      n1++;
    } else {
      group[pick] = 2;
      bx_union(c2, box + pick * 2 * d, c2, d);
      n2++;
    }
    left--;
  }
}

/* Shell sort of entry indices by one bound (axis, or dim + axis). */
static void rt_sort_order(size_t *order, size_t n, const double *box, size_t d,
                          size_t key) {
  size_t gap;
  size_t i;
  size_t j;
  for (i = 0; i < n; i++) {
    order[i] = i;
  }
  for (gap = n / 2; gap > 0; gap = gap == 2 ? 1 : gap * 5 / 11) {
    for (i = gap; i < n; i++) {
      size_t x = order[i];
      double kx = box[x * 2 * d + key];
      for (j = i; j >= gap && box[order[j - gap] * 2 * d + key] > kx;
           j -= gap) {
        order[j] = order[j - gap];
      }
      order[j] = x;
    }
  }
}

static void rt_split_rstar(ds_rtree_t *t, const double *box, size_t n,
                           unsigned char *group, size_t *order) {
  size_t d = t->dim;
  size_t m = t->min_entries;
  size_t axis;
  size_t key;
  size_t k;
  size_t i;
  size_t best_axis = 0;
  double best_margin = HUGE_VAL;
  double best_ov = HUGE_VAL;
  double best_area = HUGE_VAL;
  size_t best_key = 0;
  size_t best_k = m;
  double l[2 * DS_RTREE_MAX_DIM];
  double r[2 * DS_RTREE_MAX_DIM];
  /* the axis with the smallest total margin over all distributions */
  for (axis = 0; axis < d; axis++) {
    double msum = 0.0;
    for (key = 0; key < 2; key++) {
      rt_sort_order(order, n, box, d, axis + key * d);
      for (k = m; k <= n - m; k++) {
        memcpy(l, box + order[0] * 2 * d, 2 * d * sizeof(double));
        for (i = 1; i < k; i++) {
          bx_union(l, box + order[i] * 2 * d, l, d);
        }
        memcpy(r, box + order[k] * 2 * d, 2 * d * sizeof(double));
        for (i = k + 1; i < n; i++) {
          bx_union(r, box + order[i] * 2 * d, r, d);
        }
        msum += bx_margin(l, d) + bx_margin(r, d);
      }
    }
    if (msum < best_margin) {
      best_margin = msum;
      best_axis = axis;
    }
  }
  /* on that axis: the distribution with least overlap, then area */
  for (key = 0; key < 2; key++) {
    rt_sort_order(order, n, box, d, best_axis + key * d);
    for (k = m; k <= n - m; k++) {
      double ov;
      double ar;
      memcpy(l, box + order[0] * 2 * d, 2 * d * sizeof(double));
      for (i = 1; i < k; i++) {
        bx_union(l, box + order[i] * 2 * d, l, d);
      }
      memcpy(r, box + order[k] * 2 * d, 2 * d * sizeof(double));
      for (i = k + 1; i < n; i++) {
        bx_union(r, box + order[i] * 2 * d, r, d);
      }
      ov = bx_overlap(l, r, d);
      ar = bx_area(l, d) + bx_area(r, d);
      if (ov < best_ov || (ov <= best_ov && ar < best_area)) {
        best_ov = ov;
        best_area = ar;
        best_key = key;
        best_k = k;
      }
    }
  }
  rt_sort_order(order, n, box, d, best_axis + best_key * d);
  for (i = 0; i < n; i++) {
    group[order[i]] = (unsigned char)(i < best_k ? 1 : 2);
  }
}

/* Split overflowing node v; the new sibling in *out_w. */
static ds_status_t rt_split(ds_rtree_t *t, size_t v, size_t *out_w) {
  size_t d = t->dim;
  size_t n = t->count[v];
  size_t i;
  size_t w;
  size_t a = 0;
  size_t b = 0;
  double *box;
  size_t *ref;
  size_t *order;
  unsigned char *group;
  ds_status_t st = rt_node(t, t->leaf[v] != 0, t->level[v], &w);
  if (st != DS_OK) {
    return st;
  }
  box = (double *)ds_context_alloc(t->context, n * 2 * d * sizeof(double));
  ref = (size_t *)ds_context_alloc(t->context, 2 * n * sizeof(size_t));
  group = (unsigned char *)ds_context_alloc(t->context, n);
  if (box == NULL || ref == NULL || group == NULL) {
    ds_context_free(t->context, box);
    ds_context_free(t->context, ref);
    ds_context_free(t->context, group);
    rt_free_node(t, w);
    return DS_ERR_ALLOC;
  }
  order = ref + n;
  memcpy(box, rt_box(t, v, 0), n * 2 * d * sizeof(double));
  memcpy(ref, rt_ref(t, v, 0), n * sizeof(size_t));
  if (t->variant == DS_RTREE_RSTAR) {
    rt_split_rstar(t, box, n, group, order);
  } else {
    rt_split_quadratic(t, box, n, group);
  }
  for (i = 0; i < n; i++) {
    size_t node = group[i] == 1 ? v : w;
    size_t pos = group[i] == 1 ? a++ : b++;
    memcpy(rt_box(t, node, pos), box + i * 2 * d, 2 * d * sizeof(double));
    *rt_ref(t, node, pos) = ref[i];
    if (!t->leaf[v]) {
      t->parent[ref[i]] = node;
    }
  }
  t->count[v] = a;
  t->count[w] = b;
  ds_context_free(t->context, box);
  ds_context_free(t->context, ref);
  ds_context_free(t->context, group);
  *out_w = w;
  return DS_OK;
}

static ds_status_t rt_insert_at(ds_rtree_t *t, const double *b, size_t ref,
                                size_t level);

/* R*: remove the 30% farthest entries of v and insert them again. */
static ds_status_t rt_reinsert(ds_rtree_t *t, size_t v) {
  size_t d = t->dim;
  size_t n = t->count[v];
  size_t p = (n * 3) / 10 > 0 ? (n * 3) / 10 : 1;
  size_t i;
  size_t j;
  double c[2 * DS_RTREE_MAX_DIM];
  double *dist;
  size_t *idx;
  double *box;
  size_t *ref;
  size_t level = t->level[v];
  ds_status_t st = DS_OK;
  dist = (double *)ds_context_alloc(t->context, n * sizeof(double));
  idx = (size_t *)ds_context_alloc(t->context, 2 * n * sizeof(size_t));
  box = (double *)ds_context_alloc(t->context, p * 2 * d * sizeof(double));
  if (dist == NULL || idx == NULL || box == NULL) {
    ds_context_free(t->context, dist);
    ds_context_free(t->context, idx);
    ds_context_free(t->context, box);
    return DS_ERR_ALLOC;
  }
  ref = idx + n;
  rt_cover(t, v, c);
  for (i = 0; i < n; i++) {
    const double *e = rt_box(t, v, i);
    double s = 0.0;
    size_t a;
    for (a = 0; a < d; a++) {
      double dd = (e[a] + e[d + a]) / 2.0 - (c[a] + c[d + a]) / 2.0;
      s += dd * dd;
    }
    dist[i] = s;
    idx[i] = i;
  }
  /* farthest first (insertion sort; n is small) */
  for (i = 1; i < n; i++) {
    size_t x = idx[i];
    for (j = i; j > 0 && dist[idx[j - 1]] < dist[x]; j--) {
      idx[j] = idx[j - 1];
    }
    idx[j] = x;
  }
  for (i = 0; i < p; i++) {
    memcpy(box + i * 2 * d, rt_box(t, v, idx[i]), 2 * d * sizeof(double));
    ref[i] = *rt_ref(t, v, idx[i]);
  }
  /* compact the remaining entries (remove the chosen ones) */
  {
    unsigned char *gone = (unsigned char *)ds_context_calloc(t->context, n, 1);
    size_t w = 0;
    if (gone == NULL) {
      ds_context_free(t->context, dist);
      ds_context_free(t->context, idx);
      ds_context_free(t->context, box);
      return DS_ERR_ALLOC;
    }
    for (i = 0; i < p; i++) {
      gone[idx[i]] = 1;
    }
    for (i = 0; i < n; i++) {
      if (!gone[i]) {
        if (w != i) {
          memcpy(rt_box(t, v, w), rt_box(t, v, i), 2 * d * sizeof(double));
          *rt_ref(t, v, w) = *rt_ref(t, v, i);
        }
        w++;
      }
    }
    t->count[v] = w;
    ds_context_free(t->context, gone);
  }
  rt_adjust(t, v);
  /* close ones first */
  for (i = p; i-- > 0 && st == DS_OK;) {
    st = rt_insert_at(t, box + i * 2 * d, ref[i], level);
  }
  ds_context_free(t->context, dist);
  ds_context_free(t->context, idx);
  ds_context_free(t->context, box);
  return st;
}

/* Insert an entry (a value at level 0, a subtree at its level). */
static ds_status_t rt_insert_at(ds_rtree_t *t, const double *b, size_t ref,
                                size_t level) {
  size_t v = t->root;
  size_t w;
  size_t r;
  size_t i;
  ds_status_t st;
  while (t->level[v] > level) {
    i = rt_choose(t, v, b);
    v = *rt_ref(t, v, i);
  }
  memcpy(rt_box(t, v, t->count[v]), b, 2 * t->dim * sizeof(double));
  *rt_ref(t, v, t->count[v]) = ref;
  t->count[v]++;
  if (!t->leaf[v]) {
    t->parent[ref] = v;
  }
  rt_adjust(t, v);
  while (t->count[v] > t->max_entries) {
    if (t->variant == DS_RTREE_RSTAR && v != t->root &&
        !t->reinserted[t->level[v]]) {
      t->reinserted[t->level[v]] = 1;
      return rt_reinsert(t, v);
    }
    st = rt_split(t, v, &w);
    if (st != DS_OK) {
      return st;
    }
    if (v == t->root) {
      st = rt_node(t, false, t->level[v] + 1, &r);
      if (st != DS_OK) {
        return st;
      }
      rt_cover(t, v, rt_box(t, r, 0));
      *rt_ref(t, r, 0) = v;
      rt_cover(t, w, rt_box(t, r, 1));
      *rt_ref(t, r, 1) = w;
      t->count[r] = 2;
      t->parent[v] = t->parent[w] = r;
      t->root = r;
      t->height++;
      {
        void *p = ds_context_realloc(t->context, t->reinserted, t->height + 1);
        if (p == NULL) {
          return DS_ERR_ALLOC;
        }
        t->reinserted = (unsigned char *)p;
        t->reinserted[t->height] = 0;
        t->reinserted[t->height - 1] = 1; /* never reinsert at the root */
      }
      return DS_OK;
    }
    {
      size_t p = t->parent[v];
      i = rt_slot_in_parent(t, v);
      rt_cover(t, v, rt_box(t, p, i));
      rt_cover(t, w, rt_box(t, p, t->count[p]));
      *rt_ref(t, p, t->count[p]) = w;
      t->count[p]++;
      t->parent[w] = p;
      rt_adjust(t, p);
      v = p;
    }
  }
  return DS_OK;
}

ds_status_t ds_rtree_insert(ds_rtree_t *t, const double *box, size_t value) {
  size_t a;
  if (t == NULL || box == NULL) {
    return DS_ERR_NULL;
  }
  for (a = 0; a < t->dim; a++) {
    if (!(box[a] <= box[t->dim + a])) {
      return DS_ERR_INVALID;
    }
  }
  if (t->reinserted == NULL) {
    t->reinserted =
        (unsigned char *)ds_context_calloc(t->context, t->height + 1, 1);
    if (t->reinserted == NULL) {
      return DS_ERR_ALLOC;
    }
  }
  memset(t->reinserted, 0, t->height + 1);
  if (t->height > 0) {
    t->reinserted[t->height - 1] = 1;
  }
  t->size++;
  return rt_insert_at(t, box, value, 0);
}

static ds_status_t rt_stack_need(ds_rtree_t *t, size_t need) {
  void *p;
  size_t cap;
  if (need <= t->stack_capacity) {
    return DS_OK;
  }
  cap = t->stack_capacity < 64 ? 64 : t->stack_capacity;
  while (cap < need) {
    cap *= 2;
  }
  p = ds_context_realloc(t->context, t->stack, cap * sizeof(size_t));
  if (p == NULL) {
    return DS_ERR_ALLOC;
  }
  t->stack = (size_t *)p;
  t->stack_capacity = cap;
  return DS_OK;
}

ds_status_t ds_rtree_remove(ds_rtree_t *t, const double *box, size_t value) {
  size_t top = 0;
  size_t v;
  size_t i;
  size_t leaf = RT_NONE;
  size_t slot = 0;
  size_t d;
  /* entries to reinsert: box, ref, level */
  double *rbox = NULL;
  size_t *rref = NULL;
  size_t rn = 0;
  size_t rcap = 0;
  ds_status_t st;
  if (t == NULL || box == NULL) {
    return DS_ERR_NULL;
  }
  d = t->dim;
  st = rt_stack_need(t, t->nodes + 1);
  if (st != DS_OK) {
    return st;
  }
  t->stack[top++] = t->root;
  while (top > 0 && leaf == RT_NONE) {
    v = t->stack[--top];
    for (i = 0; i < t->count[v]; i++) {
      if (t->leaf[v]) {
        if (*rt_ref(t, v, i) == value &&
            memcmp(rt_box(t, v, i), box, 2 * d * sizeof(double)) == 0) {
          leaf = v;
          slot = i;
          break;
        }
      } else if (bx_contains(rt_box(t, v, i), box, d)) {
        t->stack[top++] = *rt_ref(t, v, i);
      }
    }
  }
  if (leaf == RT_NONE) {
    return DS_NOT_FOUND;
  }
  /* remove the entry */
  t->count[leaf]--;
  if (slot != t->count[leaf]) {
    memcpy(rt_box(t, leaf, slot), rt_box(t, leaf, t->count[leaf]),
           2 * d * sizeof(double));
    *rt_ref(t, leaf, slot) = *rt_ref(t, leaf, t->count[leaf]);
  }
  t->size--;
  /* condense: underfull nodes leave the tree, their entries come back */
  v = leaf;
  while (v != t->root) {
    size_t p = t->parent[v];
    if (t->count[v] < t->min_entries) {
      size_t ps = rt_slot_in_parent(t, v);
      /* save v's entries (level = v's level) */
      if (rn + t->count[v] > rcap) {
        size_t nc = (rn + t->count[v]) * 2 + 8;
        void *q1 =
            ds_context_realloc(t->context, rbox, nc * 2 * d * sizeof(double));
        void *q2;
        if (q1 == NULL) {
          ds_context_free(t->context, rref);
          return DS_ERR_ALLOC;
        }
        rbox = (double *)q1;
        q2 = ds_context_realloc(t->context, rref, 2 * nc * sizeof(size_t));
        if (q2 == NULL) {
          ds_context_free(t->context, rbox);
          return DS_ERR_ALLOC;
        }
        rref = (size_t *)q2;
        rcap = nc;
      }
      for (i = 0; i < t->count[v]; i++) {
        memcpy(rbox + rn * 2 * d, rt_box(t, v, i), 2 * d * sizeof(double));
        rref[2 * rn] = *rt_ref(t, v, i);
        rref[2 * rn + 1] = t->level[v];
        rn++;
      }
      t->count[p]--;
      if (ps != t->count[p]) {
        memcpy(rt_box(t, p, ps), rt_box(t, p, t->count[p]),
               2 * d * sizeof(double));
        *rt_ref(t, p, ps) = *rt_ref(t, p, t->count[p]);
      }
      rt_free_node(t, v);
    } else {
      rt_cover(t, v, rt_box(t, p, rt_slot_in_parent(t, v)));
    }
    v = p;
  }
  /* shorten the tree */
  while (!t->leaf[t->root] && t->count[t->root] == 1) {
    size_t old = t->root;
    t->root = *rt_ref(t, old, 0);
    t->parent[t->root] = RT_NONE;
    rt_free_node(t, old);
    t->height--;
  }
  if (!t->leaf[t->root] && t->count[t->root] == 0) {
    /* everything went to the reinsertion list */
    t->leaf[t->root] = 1;
    t->level[t->root] = 0;
    t->height = 1;
  }
  st = DS_OK;
  for (i = 0; i < rn && st == DS_OK; i++) {
    size_t lvl = rref[2 * i + 1];
    if (lvl >= t->height) {
      /* the tree shrank below this level: reinsert the subtree's leaf
       * entries one by one */
      size_t top2 = 0;
      st = rt_stack_need(t, t->nodes + 1);
      if (st != DS_OK) {
        break;
      }
      if (lvl == 0) {
        st = rt_insert_at(t, rbox + i * 2 * d, rref[2 * i], 0);
        continue;
      }
      t->stack[top2++] = rref[2 * i];
      while (top2 > 0 && st == DS_OK) {
        size_t w = t->stack[--top2];
        size_t j;
        for (j = 0; j < t->count[w] && st == DS_OK; j++) {
          if (t->leaf[w]) {
            double eb[2 * DS_RTREE_MAX_DIM];
            memcpy(eb, rt_box(t, w, j), 2 * d * sizeof(double));
            memset(t->reinserted, 1, t->height + 1);
            st = rt_insert_at(t, eb, *rt_ref(t, w, j), 0);
          } else {
            t->stack[top2++] = *rt_ref(t, w, j);
          }
        }
        rt_free_node(t, w);
      }
      continue;
    }
    if (t->reinserted != NULL) {
      memset(t->reinserted, 1, t->height + 1); /* no forced reinsertion */
    }
    st = rt_insert_at(t, rbox + i * 2 * d, rref[2 * i], lvl);
  }
  ds_context_free(t->context, rbox);
  ds_context_free(t->context, rref);
  return st;
}

static ds_status_t rt_walk(ds_rtree_t *t, const double *q, bool point,
                           ds_rtree_visit_func_t visit, void *user) {
  size_t top = 0;
  size_t v;
  size_t i;
  size_t d = t->dim;
  double pb[2 * DS_RTREE_MAX_DIM];
  ds_status_t st = rt_stack_need(t, t->nodes * RT_E(t) + 1);
  if (st != DS_OK) {
    return st;
  }
  if (point) {
    memcpy(pb, q, d * sizeof(double));
    memcpy(pb + d, q, d * sizeof(double));
    q = pb;
  }
  t->stack[top++] = t->root;
  while (top > 0) {
    v = t->stack[--top];
    for (i = 0; i < t->count[v]; i++) {
      if (!bx_hit(rt_box(t, v, i), q, d)) {
        continue;
      }
      if (t->leaf[v]) {
        if (!visit(rt_box(t, v, i), *rt_ref(t, v, i), user)) {
          return DS_STOP;
        }
      } else {
        t->stack[top++] = *rt_ref(t, v, i);
      }
    }
  }
  return DS_OK;
}

ds_status_t ds_rtree_query(ds_rtree_t *t, const double *box,
                           ds_rtree_visit_func_t visit, void *user) {
  if (t == NULL || box == NULL || visit == NULL) {
    return DS_ERR_NULL;
  }
  return rt_walk(t, box, false, visit, user);
}

ds_status_t ds_rtree_query_point(ds_rtree_t *t, const double *point,
                                 ds_rtree_visit_func_t visit, void *user) {
  if (t == NULL || point == NULL || visit == NULL) {
    return DS_ERR_NULL;
  }
  return rt_walk(t, point, true, visit, user);
}

ds_status_t ds_rtree_nearest(ds_rtree_t *t, const double *point, size_t k,
                             size_t *out_values, double *out_dist,
                             size_t *out_found) {
  size_t top = 0;
  size_t found = 0;
  size_t v;
  size_t i;
  size_t j;
  size_t d;
  double *dk;
  size_t *vk;
  double *nd; /* per stack entry: its min distance */
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
  st = rt_stack_need(t, t->nodes * RT_E(t) + 1);
  if (st != DS_OK) {
    return st;
  }
  dk = (double *)ds_context_alloc(t->context, k * sizeof(double));
  vk = (size_t *)ds_context_alloc(t->context, k * sizeof(size_t));
  nd = (double *)ds_context_alloc(t->context,
                                  (t->nodes * RT_E(t) + 1) * sizeof(double));
  if (dk == NULL || vk == NULL || nd == NULL) {
    ds_context_free(t->context, dk);
    ds_context_free(t->context, vk);
    ds_context_free(t->context, nd);
    return DS_ERR_ALLOC;
  }
  t->stack[top] = t->root;
  nd[top++] = 0.0;
  while (top > 0) {
    size_t base;
    top--;
    v = t->stack[top];
    if (found == k && nd[top] >= dk[k - 1]) {
      continue;
    }
    if (t->leaf[v]) {
      for (i = 0; i < t->count[v]; i++) {
        double s = bx_mindist2(rt_box(t, v, i), point, d);
        if (found < k || s < dk[found - 1]) {
          size_t pos = found < k ? found++ : k - 1;
          while (pos > 0 && dk[pos - 1] > s) {
            dk[pos] = dk[pos - 1];
            vk[pos] = vk[pos - 1];
            pos--;
          }
          dk[pos] = s;
          vk[pos] = *rt_ref(t, v, i);
        }
      }
      continue;
    }
    /* children pushed farthest first */
    base = top;
    for (i = 0; i < t->count[v]; i++) {
      double s = bx_mindist2(rt_box(t, v, i), point, d);
      size_t c = *rt_ref(t, v, i);
      for (j = top; j > base && nd[j - 1] < s; j--) {
        nd[j] = nd[j - 1];
        t->stack[j] = t->stack[j - 1];
      }
      nd[j] = s;
      t->stack[j] = c;
      top++;
    }
  }
  for (i = 0; i < found; i++) {
    out_values[i] = vk[i];
    out_dist[i] = sqrt(dk[i]);
  }
  *out_found = found;
  ds_context_free(t->context, dk);
  ds_context_free(t->context, vk);
  ds_context_free(t->context, nd);
  return DS_OK;
}

static bool rt_check(const ds_rtree_t *t, size_t v, size_t level,
                     size_t *count) {
  size_t i;
  double c[2 * DS_RTREE_MAX_DIM];
  if (t->level[v] != level || t->count[v] > t->max_entries ||
      (v != t->root && t->count[v] < t->min_entries)) {
    return false;
  }
  if (t->leaf[v] != (level == 0)) {
    return false;
  }
  if (t->leaf[v]) {
    *count += t->count[v];
    return true;
  }
  for (i = 0; i < t->count[v]; i++) {
    size_t ch = *rt_ref(t, v, i);
    if (t->parent[ch] != v || t->count[ch] == 0) {
      return false;
    }
    rt_cover(t, ch, c);
    if (memcmp(c, rt_box(t, v, i), 2 * t->dim * sizeof(double)) != 0) {
      return false;
    }
    if (!rt_check(t, ch, level - 1, count)) {
      return false;
    }
  }
  return true;
}

bool ds_rtree_valid(const ds_rtree_t *t) {
  size_t count = 0;
  if (t == NULL) {
    return false;
  }
  return rt_check(t, t->root, t->height - 1, &count) && count == t->size;
}

/* ---- R+-trees
 * ------------------------------------------------------------------------ */

/* Regions are half-open boxes [lo, hi); the root region is all of space,
 * so sibling regions partition their parent exactly. */

static bool rp_box_in_region(const double *b, const double *r, size_t d) {
  size_t a;
  for (a = 0; a < d; a++) {
    if (!(b[a] < r[d + a]) || b[d + a] < r[a]) {
      return false;
    }
  }
  return true;
}

static bool rp_point_in_region(const double *p, const double *r, size_t d) {
  size_t a;
  for (a = 0; a < d; a++) {
    if (p[a] < r[a] || !(p[a] < r[d + a])) {
      return false;
    }
  }
  return true;
}

static int rp_cmp_double(const void *a, const void *b) {
  double x = *(const double *)a;
  double y = *(const double *)b;
  return (x > y) - (x < y);
}

typedef struct rp_piece {
  double region[2 * DS_RTREE_MAX_DIM];
  size_t *idx;
  size_t n;
  bool stuck;
} rp_piece_t;

/* Find the best cut of the objects idx[0..n) inside region; false if no
 * cut makes both sides smaller. */
static bool rp_cut(const double *boxes, size_t d, const double *region,
                   const size_t *idx, size_t n, double *scratch,
                   size_t *out_axis, double *out_x) {
  size_t a;
  size_t c;
  size_t i;
  size_t key;
  bool any = false;
  size_t best_sum = 0;
  size_t best_imb = 0;
  for (a = 0; a < d; a++) {
    for (key = 0; key < 2; key++) {
      for (i = 0; i < n; i++) {
        scratch[i] = boxes[idx[i] * 2 * d + key * d + a];
      }
      qsort(scratch, n, sizeof(double), rp_cmp_double);
      for (c = 1; c <= 3; c++) {
        double x = scratch[(n * c) / 4 < n ? (n * c) / 4 : n - 1];
        size_t l = 0;
        size_t r = 0;
        size_t imb;
        if (!(x > region[a]) || !(x < region[d + a])) {
          continue;
        }
        for (i = 0; i < n; i++) {
          const double *b = boxes + idx[i] * 2 * d;
          l += b[a] < x;
          r += b[d + a] >= x;
        }
        /* both sides must shrink, by an eighth once there is room to */
        if (l == 0 || r == 0 || l >= n || r >= n ||
            (n >= 16 && (l > n - n / 8 || r > n - n / 8))) {
          continue;
        }
        imb = l > r ? l - r : r - l;
        if (!any || l + r < best_sum || (l + r == best_sum && imb < best_imb)) {
          any = true;
          best_sum = l + r;
          best_imb = imb;
          *out_axis = a;
          *out_x = x;
        }
      }
    }
  }
  return any;
}

typedef struct rp_work {
  size_t node;
  size_t *idx;
  size_t n;
} rp_work_t;

static ds_status_t rp_grow(ds_rplus_t *t, size_t *cap, size_t need) {
  size_t nc;
  void *p;
  if (need <= *cap) {
    return DS_OK;
  }
  nc = *cap < 16 ? 16 : *cap;
  while (nc < need) {
    nc *= 2;
  }
  p = ds_context_realloc(t->context, t->region,
                         nc * 2 * t->dim * sizeof(double));
  if (p == NULL) {
    return DS_ERR_ALLOC;
  }
  t->region = (double *)p;
  p = ds_context_realloc(t->context, t->first, nc * sizeof(size_t));
  if (p == NULL) {
    return DS_ERR_ALLOC;
  }
  t->first = (size_t *)p;
  p = ds_context_realloc(t->context, t->count, nc * sizeof(size_t));
  if (p == NULL) {
    return DS_ERR_ALLOC;
  }
  t->count = (size_t *)p;
  p = ds_context_realloc(t->context, t->leaf, nc);
  if (p == NULL) {
    return DS_ERR_ALLOC;
  }
  t->leaf = (unsigned char *)p;
  *cap = nc;
  return DS_OK;
}

static ds_status_t rp_make_leaf(ds_rplus_t *t, const double *boxes,
                                const size_t *values, size_t node,
                                const size_t *idx, size_t n, size_t *ecap) {
  size_t i;
  size_t d = t->dim;
  if (t->entries + n > *ecap) {
    size_t nc = *ecap < 16 ? 16 : *ecap;
    void *p;
    while (nc < t->entries + n) {
      nc *= 2;
    }
    p = ds_context_realloc(t->context, t->ebox, nc * 2 * d * sizeof(double));
    if (p == NULL) {
      return DS_ERR_ALLOC;
    }
    t->ebox = (double *)p;
    p = ds_context_realloc(t->context, t->evalue, nc * sizeof(size_t));
    if (p == NULL) {
      return DS_ERR_ALLOC;
    }
    t->evalue = (size_t *)p;
    *ecap = nc;
  }
  t->leaf[node] = 1;
  t->first[node] = t->entries;
  t->count[node] = n;
  for (i = 0; i < n; i++) {
    memcpy(t->ebox + (t->entries + i) * 2 * d, boxes + idx[i] * 2 * d,
           2 * d * sizeof(double));
    t->evalue[t->entries + i] = values != NULL ? values[idx[i]] : idx[i];
  }
  t->entries += n;
  return DS_OK;
}

ds_status_t ds_rplus_build(ds_rplus_t *t, size_t dim, const double *boxes,
                           const size_t *values, size_t n, size_t leaf_capacity,
                           ds_context_t *context) {
  size_t ncap = 0;
  size_t ecap = 0;
  size_t i;
  size_t a;
  size_t wn = 0;
  size_t wcap = 0;
  rp_work_t *work = NULL;
  rp_piece_t *pieces = NULL;
  double *scratch = NULL;
  size_t *root_idx = NULL;
  ds_status_t st = DS_OK;
  if (t == NULL || (n > 0 && boxes == NULL)) {
    return DS_ERR_NULL;
  }
  if (leaf_capacity == 0) {
    leaf_capacity = 8;
  }
  if (dim == 0 || dim > DS_RTREE_MAX_DIM || leaf_capacity < 2) {
    return DS_ERR_INVALID;
  }
  for (i = 0; i < n; i++) {
    for (a = 0; a < dim; a++) {
      if (!(boxes[i * 2 * dim + a] <= boxes[i * 2 * dim + dim + a])) {
        return DS_ERR_INVALID;
      }
    }
  }
  memset(t, 0, sizeof(*t));
  t->dim = dim;
  t->objects = n;
  t->context = context;
  st = rp_grow(t, &ncap, 1);
  pieces = (rp_piece_t *)ds_context_alloc(t->context,
                                          leaf_capacity * sizeof(rp_piece_t));
  scratch =
      (double *)ds_context_alloc(t->context, (n > 0 ? n : 1) * sizeof(double));
  root_idx =
      (size_t *)ds_context_alloc(t->context, (n > 0 ? n : 1) * sizeof(size_t));
  if (st != DS_OK || pieces == NULL || scratch == NULL || root_idx == NULL) {
    st = DS_ERR_ALLOC;
    goto done;
  }
  t->nodes = 1;
  for (a = 0; a < dim; a++) {
    t->region[a] = -HUGE_VAL;
    t->region[dim + a] = HUGE_VAL;
  }
  for (i = 0; i < n; i++) {
    root_idx[i] = i;
  }
  work = (rp_work_t *)ds_context_alloc(t->context, 16 * sizeof(rp_work_t));
  if (work == NULL) {
    st = DS_ERR_ALLOC;
    goto done;
  }
  wcap = 16;
  work[0].node = 0;
  work[0].idx = root_idx;
  work[0].n = n;
  wn = 1;
  root_idx = NULL;
  while (wn > 0 && st == DS_OK) {
    rp_work_t w = work[--wn];
    size_t np = 1;
    size_t k;
    if (w.n <= leaf_capacity) {
      st = rp_make_leaf(t, boxes, values, w.node, w.idx, w.n, &ecap);
      ds_context_free(t->context, w.idx);
      continue;
    }
    /* split into up to leaf_capacity disjoint pieces */
    memcpy(pieces[0].region, t->region + w.node * 2 * dim,
           2 * dim * sizeof(double));
    pieces[0].idx = w.idx;
    pieces[0].n = w.n;
    pieces[0].stuck = false;
    while (np < leaf_capacity && st == DS_OK) {
      size_t pick = np;
      size_t axis = 0;
      double x = 0.0;
      size_t *li;
      size_t *ri;
      size_t ln = 0;
      size_t rn = 0;
      rp_piece_t *p;
      for (k = 0; k < np; k++) {
        if (!pieces[k].stuck && pieces[k].n > leaf_capacity &&
            (pick == np || pieces[k].n > pieces[pick].n)) {
          pick = k;
        }
      }
      if (pick == np) {
        break;
      }
      p = &pieces[pick];
      if (!rp_cut(boxes, dim, p->region, p->idx, p->n, scratch, &axis, &x)) {
        p->stuck = true;
        continue;
      }
      li = (size_t *)ds_context_alloc(t->context, p->n * sizeof(size_t));
      ri = (size_t *)ds_context_alloc(t->context, p->n * sizeof(size_t));
      if (li == NULL || ri == NULL) {
        ds_context_free(t->context, li);
        ds_context_free(t->context, ri);
        st = DS_ERR_ALLOC;
        break;
      }
      for (k = 0; k < p->n; k++) {
        const double *b = boxes + p->idx[k] * 2 * dim;
        if (b[axis] < x) {
          li[ln++] = p->idx[k];
        }
        if (b[dim + axis] >= x) {
          ri[rn++] = p->idx[k];
        }
      }
      ds_context_free(t->context, p->idx);
      memcpy(pieces[np].region, p->region, 2 * dim * sizeof(double));
      p->region[dim + axis] = x;
      pieces[np].region[axis] = x;
      p->idx = li;
      p->n = ln;
      pieces[np].idx = ri;
      pieces[np].n = rn;
      pieces[np].stuck = false;
      np++;
    }
    if (st != DS_OK) {
      for (k = 0; k < np; k++) {
        ds_context_free(t->context, pieces[k].idx);
      }
      break;
    }
    if (np == 1) {
      /* nothing separates these objects: an oversized leaf */
      st = rp_make_leaf(t, boxes, values, w.node, pieces[0].idx, pieces[0].n,
                        &ecap);
      ds_context_free(t->context, pieces[0].idx);
      continue;
    }
    st = rp_grow(t, &ncap, t->nodes + np);
    if (st == DS_OK && wn + np > wcap) {
      void *q = ds_context_realloc(t->context, work,
                                   (wn + np) * 2 * sizeof(rp_work_t));
      if (q == NULL) {
        st = DS_ERR_ALLOC;
      } else {
        work = (rp_work_t *)q;
        wcap = (wn + np) * 2;
      }
    }
    if (st != DS_OK) {
      for (k = 0; k < np; k++) {
        ds_context_free(t->context, pieces[k].idx);
      }
      break;
    }
    t->leaf[w.node] = 0;
    t->first[w.node] = t->nodes;
    t->count[w.node] = np;
    for (k = 0; k < np; k++) {
      size_t c = t->nodes++;
      memcpy(t->region + c * 2 * dim, pieces[k].region,
             2 * dim * sizeof(double));
      t->leaf[c] = 1;
      t->first[c] = 0;
      t->count[c] = 0;
      work[wn].node = c;
      work[wn].idx = pieces[k].idx;
      work[wn].n = pieces[k].n;
      wn++;
    }
  }
done:
  while (wn > 0) {
    ds_context_free(t->context, work[--wn].idx);
  }
  ds_context_free(t->context, work);
  ds_context_free(t->context, pieces);
  ds_context_free(t->context, scratch);
  ds_context_free(t->context, root_idx);
  if (st != DS_OK) {
    ds_rplus_destroy(t);
  }
  return st;
}

void ds_rplus_destroy(ds_rplus_t *t) {
  if (t == NULL) {
    return;
  }
  ds_context_free(t->context, t->region);
  ds_context_free(t->context, t->first);
  ds_context_free(t->context, t->count);
  ds_context_free(t->context, t->leaf);
  ds_context_free(t->context, t->ebox);
  ds_context_free(t->context, t->evalue);
  memset(t, 0, sizeof(*t));
}

ds_status_t ds_rplus_query(const ds_rplus_t *t, const double *box,
                           ds_rtree_visit_func_t visit, void *user) {
  size_t *stack;
  size_t top = 0;
  size_t d;
  size_t i;
  size_t a;
  double p[DS_RTREE_MAX_DIM];
  ds_status_t st = DS_OK;
  if (t == NULL || box == NULL || visit == NULL) {
    return DS_ERR_NULL;
  }
  if (t->nodes == 0) {
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
    if (!rp_box_in_region(box, t->region + v * 2 * d, d)) {
      continue;
    }
    if (!t->leaf[v]) {
      for (i = 0; i < t->count[v]; i++) {
        stack[top++] = t->first[v] + i;
      }
      continue;
    }
    for (i = t->first[v]; i < t->first[v] + t->count[v]; i++) {
      const double *b = t->ebox + i * 2 * d;
      if (!bx_hit(b, box, d)) {
        continue;
      }
      /* report each object once: in the leaf holding the low corner of
       * its intersection with the query */
      for (a = 0; a < d; a++) {
        p[a] = b[a] > box[a] ? b[a] : box[a];
      }
      if (rp_point_in_region(p, t->region + v * 2 * d, d) &&
          !visit(b, t->evalue[i], user)) {
        st = DS_STOP;
        break;
      }
    }
  }
  ds_context_free(t->context, stack);
  return st;
}

size_t ds_rplus_entries(const ds_rplus_t *t) {
  return t == NULL ? 0 : t->entries;
}
