#include "bsp_tree.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

#define BSP_NONE (~(size_t)0)
#define BSP_MAXD DS_BSP_MAX_DIM

static bool bsp_finite(const double *x, size_t n) {
  size_t i;
  for (i = 0; i < n; i++) {
    if (!(x[i] > -HUGE_VAL && x[i] < HUGE_VAL)) {
      return false;
    }
  }
  return true;
}

static double bsp_dot(const double *a, const double *b, size_t d) {
  size_t i;
  double s = 0.0;
  for (i = 0; i < d; i++) {
    s += a[i] * b[i];
  }
  return s;
}

static int bsp_cmp_double(const void *a, const void *b) {
  double x = *(const double *)a;
  double y = *(const double *)b;
  return (x > y) - (x < y);
}

static ds_status_t bsp_grow(ds_context_t *ctx, void **p, size_t elem,
                            size_t n) {
  void *q = ds_context_realloc(ctx, *p, n * elem);
  if (q == NULL) {
    return DS_ERR_ALLOC;
  }
  *p = q;
  return DS_OK;
}

static ds_status_t bsp_append(ds_context_t *ctx, double **items, size_t **vals,
                              size_t *count, size_t *cap, size_t width,
                              const double *item, size_t value) {
  if (*count == *cap) {
    size_t nc = *cap < 4 ? 4 : *cap * 2;
    if (bsp_grow(ctx, (void **)items, width * sizeof(double), nc) ||
        bsp_grow(ctx, (void **)vals, sizeof(size_t), nc)) {
      return DS_ERR_ALLOC;
    }
    *cap = nc;
  }
  memcpy(*items + *count * width, item, width * sizeof(double));
  (*vals)[(*count)++] = value;
  return DS_OK;
}

static void bsp_best_push(double *dk, size_t *vk, size_t k, size_t *found,
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

/* ---- BSP-tree
 * ------------------------------------------------------------------ */

bool ds_bsp_principal_plane(const double *points, size_t n, size_t dim,
                            double *scratch, double *normal, double *offset,
                            void *user) {
  double mean[BSP_MAXD];
  double cov[BSP_MAXD * BSP_MAXD];
  double v[BSP_MAXD];
  double *proj = scratch;
  size_t i;
  size_t a;
  size_t b;
  size_t it;
  double norm;
  double x;
  (void)user;
  if (n < 2 || dim == 0 || dim > BSP_MAXD || scratch == NULL) {
    return false;
  }
  memset(cov, 0, sizeof(cov));
  for (a = 0; a < dim; a++) {
    mean[a] = 0.0;
    for (i = 0; i < n; i++) {
      mean[a] += points[i * dim + a];
    }
    mean[a] /= (double)n;
  }
  for (a = 0; a < dim; a++) {
    for (b = 0; b < dim; b++) {
      double s = 0.0;
      for (i = 0; i < n; i++) {
        s += (points[i * dim + a] - mean[a]) * (points[i * dim + b] - mean[b]);
      }
      cov[a * dim + b] = s;
    }
  }
  /* start from the axis of largest variance, then power iteration */
  b = 0;
  for (a = 1; a < dim; a++) {
    if (cov[a * dim + a] > cov[b * dim + b]) {
      b = a;
    }
  }
  if (!(cov[b * dim + b] > 0.0)) {
    return false;
  }
  for (a = 0; a < dim; a++) {
    v[a] = a == b ? 1.0 : 0.0;
  }
  for (it = 0; it < 32; it++) {
    double w[BSP_MAXD];
    for (a = 0; a < dim; a++) {
      w[a] = 0.0;
      for (b = 0; b < dim; b++) {
        w[a] += cov[a * dim + b] * v[b];
      }
    }
    norm = sqrt(bsp_dot(w, w, dim));
    if (!(norm > 0.0)) {
      break;
    }
    for (a = 0; a < dim; a++) {
      v[a] = w[a] / norm;
    }
  }
  for (i = 0; i < n; i++) {
    proj[i] = bsp_dot(points + i * dim, v, dim);
  }
  qsort(proj, n, sizeof(double), bsp_cmp_double);
  x = proj[n / 2];
  if (!(x > proj[0])) {
    for (i = 1; i < n && !(proj[i] > proj[0]); i++) {
    }
    x = i < n ? proj[i] : proj[0];
  }
  memcpy(normal, v, dim * sizeof(double));
  *offset = x;
  return true;
}

static ds_status_t bsp_node(ds_bsp_tree_t *t, size_t *out) {
  size_t v;
  if (t->free_list != BSP_NONE) {
    v = t->free_list;
    t->free_list = t->pos[v];
  } else {
    if (t->nodes == t->node_capacity) {
      size_t nc = t->node_capacity < 8 ? 8 : t->node_capacity * 2;
      if (bsp_grow(t->context, (void **)&t->normal, t->dim * sizeof(double),
                   nc) ||
          bsp_grow(t->context, (void **)&t->offset, sizeof(double), nc) ||
          bsp_grow(t->context, (void **)&t->neg, sizeof(size_t), nc) ||
          bsp_grow(t->context, (void **)&t->pos, sizeof(size_t), nc) ||
          bsp_grow(t->context, (void **)&t->parent, sizeof(size_t), nc) ||
          bsp_grow(t->context, (void **)&t->points, sizeof(double *), nc) ||
          bsp_grow(t->context, (void **)&t->values, sizeof(size_t *), nc) ||
          bsp_grow(t->context, (void **)&t->count, sizeof(size_t), nc) ||
          bsp_grow(t->context, (void **)&t->capacity, sizeof(size_t), nc)) {
        return DS_ERR_ALLOC;
      }
      t->node_capacity = nc;
    }
    v = t->nodes++;
  }
  memset(t->normal + v * t->dim, 0, t->dim * sizeof(double));
  t->offset[v] = 0.0;
  t->neg[v] = t->pos[v] = BSP_NONE;
  t->parent[v] = BSP_NONE;
  t->points[v] = NULL;
  t->values[v] = NULL;
  t->count[v] = t->capacity[v] = 0;
  *out = v;
  return DS_OK;
}

static void bsp_free(ds_bsp_tree_t *t, size_t v) {
  ds_context_free(t->context, t->points[v]);
  ds_context_free(t->context, t->values[v]);
  t->points[v] = NULL;
  t->values[v] = NULL;
  t->count[v] = t->capacity[v] = 0;
  t->neg[v] = BSP_NONE;
  t->pos[v] = t->free_list;
  t->free_list = v;
}

ds_status_t ds_bsp_tree_init(ds_bsp_tree_t *t, size_t dim, size_t bucket,
                             ds_bsp_plane_func_t plane, void *plane_user,
                             ds_context_t *context) {
  ds_status_t st;
  if (t == NULL) {
    return DS_ERR_NULL;
  }
  if (bucket == 0) {
    bucket = 16;
  }
  if (dim == 0 || dim > BSP_MAXD || bucket < 2) {
    return DS_ERR_INVALID;
  }
  memset(t, 0, sizeof(*t));
  t->dim = dim;
  t->bucket = bucket;
  t->plane = plane != NULL ? plane : ds_bsp_principal_plane;
  t->plane_user = plane_user;
  t->free_list = BSP_NONE;
  t->context = context;
  st = bsp_node(t, &t->root);
  if (st != DS_OK) {
    ds_bsp_tree_destroy(t);
  }
  return st;
}

void ds_bsp_tree_destroy(ds_bsp_tree_t *t) {
  size_t v;
  if (t == NULL) {
    return;
  }
  for (v = 0; v < t->nodes; v++) {
    ds_context_free(t->context, t->points[v]);
    ds_context_free(t->context, t->values[v]);
  }
  ds_context_free(t->context, t->normal);
  ds_context_free(t->context, t->offset);
  ds_context_free(t->context, t->neg);
  ds_context_free(t->context, t->pos);
  ds_context_free(t->context, t->parent);
  ds_context_free(t->context, t->points);
  ds_context_free(t->context, t->values);
  ds_context_free(t->context, t->count);
  ds_context_free(t->context, t->capacity);
  memset(t, 0, sizeof(*t));
}

size_t ds_bsp_tree_size(const ds_bsp_tree_t *t) {
  return t == NULL ? 0 : t->size;
}

size_t ds_bsp_tree_depth(const ds_bsp_tree_t *t) {
  size_t best = 0;
  size_t v;
  if (t == NULL) {
    return 0;
  }
  for (v = 0; v < t->nodes; v++) {
    size_t k = 0;
    size_t u;
    if (t->neg[v] != BSP_NONE || t->points[v] == NULL) {
      continue;
    }
    for (u = v; t->parent[u] != BSP_NONE; u = t->parent[u]) {
      k++;
    }
    best = k > best ? k : best;
  }
  return best;
}

static bool bsp_side(const ds_bsp_tree_t *t, size_t v, const double *p) {
  return !(bsp_dot(t->normal + v * t->dim, p, t->dim) < t->offset[v]);
}

static size_t bsp_leaf(const ds_bsp_tree_t *t, const double *p) {
  size_t v = t->root;
  while (t->neg[v] != BSP_NONE) {
    v = bsp_side(t, v, p) ? t->pos[v] : t->neg[v];
  }
  return v;
}

ds_status_t ds_bsp_tree_insert(ds_bsp_tree_t *t, const double *point,
                               size_t value) {
  size_t d;
  size_t v;
  ds_status_t st;
  if (t == NULL || point == NULL) {
    return DS_ERR_NULL;
  }
  d = t->dim;
  if (!bsp_finite(point, d)) {
    return DS_ERR_INVALID;
  }
  v = bsp_leaf(t, point);
  st = bsp_append(t->context, &t->points[v], &t->values[v], &t->count[v],
                  &t->capacity[v], d, point, value);
  if (st != DS_OK) {
    return st;
  }
  t->size++;
  while (t->count[v] > t->bucket) {
    double nrm[BSP_MAXD];
    double off;
    size_t a = 0;
    size_t b = 0;
    size_t i;
    size_t nneg = 0;
    double *scratch =
        (double *)ds_context_alloc(t->context, t->count[v] * sizeof(double));
    bool ok;
    if (scratch == NULL) {
      return DS_ERR_ALLOC;
    }
    ok = t->plane(t->points[v], t->count[v], d, scratch, nrm, &off,
                  t->plane_user);
    ds_context_free(t->context, scratch);
    if (!ok || !bsp_finite(nrm, d) || !bsp_finite(&off, 1)) {
      break;
    }
    for (i = 0; i < t->count[v]; i++) {
      nneg += bsp_dot(nrm, t->points[v] + i * d, d) < off;
    }
    if (nneg == 0 || nneg == t->count[v]) {
      break; /* the plane separates nothing */
    }
    st = bsp_node(t, &a);
    if (st == DS_OK) {
      st = bsp_node(t, &b);
      if (st != DS_OK) {
        bsp_free(t, a);
      }
    }
    if (st != DS_OK) {
      return st;
    }
    for (i = 0; i < t->count[v] && st == DS_OK; i++) {
      const double *p = t->points[v] + i * d;
      size_t to = bsp_dot(nrm, p, d) < off ? a : b;
      st = bsp_append(t->context, &t->points[to], &t->values[to], &t->count[to],
                      &t->capacity[to], d, p, t->values[v][i]);
    }
    if (st != DS_OK) {
      bsp_free(t, a);
      bsp_free(t, b);
      return st;
    }
    ds_context_free(t->context, t->points[v]);
    ds_context_free(t->context, t->values[v]);
    t->points[v] = NULL;
    t->values[v] = NULL;
    t->count[v] = t->capacity[v] = 0;
    memcpy(t->normal + v * d, nrm, d * sizeof(double));
    t->offset[v] = off;
    t->neg[v] = a;
    t->pos[v] = b;
    t->parent[a] = t->parent[b] = v;
    v = t->count[a] > t->count[b] ? a : b;
  }
  return DS_OK;
}

ds_status_t ds_bsp_tree_remove(ds_bsp_tree_t *t, const double *point,
                               size_t value) {
  size_t d;
  size_t v;
  size_t u;
  size_t i;
  if (t == NULL || point == NULL) {
    return DS_ERR_NULL;
  }
  d = t->dim;
  if (!bsp_finite(point, d)) {
    return DS_NOT_FOUND;
  }
  v = bsp_leaf(t, point);
  for (i = 0; i < t->count[v]; i++) {
    if (t->values[v][i] == value &&
        memcmp(t->points[v] + i * d, point, d * sizeof(double)) == 0) {
      break;
    }
  }
  if (i == t->count[v]) {
    return DS_NOT_FOUND;
  }
  t->count[v]--;
  if (i != t->count[v]) {
    memcpy(t->points[v] + i * d, t->points[v] + t->count[v] * d,
           d * sizeof(double));
    t->values[v][i] = t->values[v][t->count[v]];
  }
  t->size--;
  for (u = t->parent[v]; u != BSP_NONE; u = t->parent[u]) {
    size_t a = t->neg[u];
    size_t b = t->pos[u];
    size_t need = t->count[a] + t->count[b];
    if (t->neg[a] != BSP_NONE || t->neg[b] != BSP_NONE || need > t->bucket) {
      break;
    }
    if (need > t->capacity[a] &&
        (bsp_grow(t->context, (void **)&t->points[a], d * sizeof(double),
                  need) ||
         bsp_grow(t->context, (void **)&t->values[a], sizeof(size_t), need))) {
      break;
    }
    if (need > t->capacity[a]) {
      t->capacity[a] = need;
    }
    if (t->count[b] > 0) {
      memcpy(t->points[a] + t->count[a] * d, t->points[b],
             t->count[b] * d * sizeof(double));
      memcpy(t->values[a] + t->count[a], t->values[b],
             t->count[b] * sizeof(size_t));
    }
    t->points[u] = t->points[a];
    t->values[u] = t->values[a];
    t->count[u] = need;
    t->capacity[u] = t->capacity[a];
    t->points[a] = NULL;
    t->values[a] = NULL;
    t->neg[u] = t->pos[u] = BSP_NONE;
    bsp_free(t, a);
    bsp_free(t, b);
  }
  return DS_OK;
}

/* min and max of n.x over the box */
static void bsp_box_range(const double *n, const double *box, size_t d,
                          double *lo, double *hi) {
  size_t a;
  *lo = *hi = 0.0;
  for (a = 0; a < d; a++) {
    if (n[a] >= 0.0) {
      *lo += n[a] * box[a];
      *hi += n[a] * box[d + a];
    } else {
      *lo += n[a] * box[d + a];
      *hi += n[a] * box[a];
    }
  }
}

ds_status_t ds_bsp_tree_query(const ds_bsp_tree_t *t, const double *box,
                              ds_bsp_visit_func_t visit, void *user) {
  size_t d;
  size_t *stack;
  size_t top = 0;
  ds_status_t st = DS_OK;
  if (t == NULL || box == NULL || visit == NULL) {
    return DS_ERR_NULL;
  }
  d = t->dim;
  stack =
      (size_t *)ds_context_alloc(t->context, (t->nodes + 1) * sizeof(size_t));
  if (stack == NULL) {
    return DS_ERR_ALLOC;
  }
  stack[top++] = t->root;
  while (top > 0 && st == DS_OK) {
    size_t v = stack[--top];
    size_t i;
    if (t->neg[v] != BSP_NONE) {
      double lo;
      double hi;
      bsp_box_range(t->normal + v * d, box, d, &lo, &hi);
      if (lo < t->offset[v]) {
        stack[top++] = t->neg[v];
      }
      if (!(hi < t->offset[v])) {
        stack[top++] = t->pos[v];
      }
      continue;
    }
    for (i = 0; i < t->count[v]; i++) {
      const double *p = t->points[v] + i * d;
      size_t a;
      for (a = 0; a < d && p[a] >= box[a] && p[a] <= box[d + a]; a++) {
      }
      if (a == d && !visit(p, t->values[v][i], user)) {
        st = DS_STOP;
        break;
      }
    }
  }
  ds_context_free(t->context, stack);
  return st;
}

ds_status_t ds_bsp_tree_nearest(const ds_bsp_tree_t *t, const double *point,
                                size_t k, size_t *out_values, double *out_dist,
                                size_t *out_found) {
  size_t d;
  size_t *stack;
  double *key;
  double *dk;
  size_t *vk;
  size_t top;
  size_t found = 0;
  size_t i;
  if (t == NULL || point == NULL || out_found == NULL ||
      (k > 0 && (out_values == NULL || out_dist == NULL))) {
    return DS_ERR_NULL;
  }
  *out_found = 0;
  if (k == 0 || t->size == 0) {
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
  stack[0] = t->root;
  key[0] = 0.0;
  top = 1;
  while (top > 0) {
    size_t v;
    double lb;
    top--;
    v = stack[top];
    lb = key[top];
    if (found == k && !(lb < dk[k - 1])) {
      continue;
    }
    if (t->neg[v] != BSP_NONE) {
      const double *n = t->normal + v * d;
      double nn = bsp_dot(n, n, d);
      double s = bsp_dot(n, point, d) - t->offset[v];
      double far = nn > 0.0 ? s * s / nn : 0.0;
      size_t near_child = s < 0.0 ? t->neg[v] : t->pos[v];
      size_t far_child = s < 0.0 ? t->pos[v] : t->neg[v];
      /* the far side first, so the near side is popped next */
      key[top] = far > lb ? far : lb;
      stack[top++] = far_child;
      key[top] = lb;
      stack[top++] = near_child;
      continue;
    }
    for (i = 0; i < t->count[v]; i++) {
      const double *p = t->points[v] + i * d;
      double s = 0.0;
      size_t a;
      for (a = 0; a < d; a++) {
        s += (p[a] - point[a]) * (p[a] - point[a]);
      }
      bsp_best_push(dk, vk, k, &found, s, t->values[v][i]);
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

bool ds_bsp_tree_valid(const ds_bsp_tree_t *t) {
  size_t d;
  size_t *stack;
  size_t top = 0;
  size_t total = 0;
  bool ok = true;
  if (t == NULL || t->neg == NULL) {
    return false;
  }
  d = t->dim;
  stack =
      (size_t *)ds_context_alloc(t->context, (t->nodes + 1) * sizeof(size_t));
  if (stack == NULL) {
    return false;
  }
  stack[top++] = t->root;
  while (top > 0 && ok) {
    size_t v = stack[--top];
    size_t i;
    if (t->neg[v] != BSP_NONE) {
      ok = t->parent[t->neg[v]] == v && t->parent[t->pos[v]] == v;
      stack[top++] = t->neg[v];
      stack[top++] = t->pos[v];
      continue;
    }
    for (i = 0; i < t->count[v] && ok; i++) {
      ok = bsp_leaf(t, t->points[v] + i * d) == v;
    }
    total += t->count[v];
  }
  ds_context_free(t->context, stack);
  return ok && total == t->size;
}

/* ---- triangulation
 * ----------------------------------------------------------------- */

static double ct_cross(const double *a, const double *b, const double *c) {
  return (b[0] - a[0]) * (c[1] - a[1]) - (b[1] - a[1]) * (c[0] - a[0]);
}

static bool ct_same(const double *p, const double *q) {
  return memcmp(p, q, 2 * sizeof(double)) == 0;
}

/* p inside or on the counter-clockwise triangle abc */
static bool ct_in_tri(const double *p, const double *a, const double *b,
                      const double *c) {
  return ct_cross(a, b, p) >= 0.0 && ct_cross(b, c, p) >= 0.0 &&
         ct_cross(c, a, p) >= 0.0;
}

ds_status_t ds_cell_tree_triangulate(const double *xy, size_t n, size_t *out,
                                     ds_context_t *context) {
  size_t *v;
  size_t m = n;
  size_t k = 0;
  size_t i;
  double area = 0.0;
  if (xy == NULL || out == NULL) {
    return DS_ERR_NULL;
  }
  if (n < 3 || !bsp_finite(xy, 2 * n)) {
    return DS_ERR_INVALID;
  }
  for (i = 0; i < n; i++) {
    const double *a = xy + 2 * i;
    const double *b = xy + 2 * ((i + 1) % n);
    area += a[0] * b[1] - b[0] * a[1];
  }
  if (!(area > 0.0) && !(area < 0.0)) {
    return DS_ERR_INVALID;
  }
  v = (size_t *)ds_context_alloc(context, n * sizeof(size_t));
  if (v == NULL) {
    return DS_ERR_ALLOC;
  }
  for (i = 0; i < n; i++) {
    v[i] = area > 0.0 ? i : n - 1 - i; /* counter-clockwise */
  }
  while (m > 3) {
    bool cut = false;
    for (i = 0; i < m && !cut; i++) {
      const double *a = xy + 2 * v[(i + m - 1) % m];
      const double *b = xy + 2 * v[i];
      const double *c = xy + 2 * v[(i + 1) % m];
      size_t j;
      bool ear = ct_cross(a, b, c) > 0.0;
      for (j = 0; j < m && ear; j++) {
        const double *p = xy + 2 * v[j];
        if (j == i || j == (i + 1) % m || j == (i + m - 1) % m) {
          continue;
        }
        if (ct_same(p, a) || ct_same(p, b) || ct_same(p, c)) {
          continue;
        }
        ear = !ct_in_tri(p, a, b, c);
      }
      if (ear) {
        out[3 * k] = v[(i + m - 1) % m];
        out[3 * k + 1] = v[i];
        out[3 * k + 2] = v[(i + 1) % m];
        k++;
        memmove(v + i, v + i + 1, (m - i - 1) * sizeof(size_t));
        m--;
        cut = true;
      }
    }
    if (!cut) {
      /* drop a collinear vertex as a flat triangle, else give up */
      for (i = 0; i < m && !cut; i++) {
        const double *a = xy + 2 * v[(i + m - 1) % m];
        const double *b = xy + 2 * v[i];
        const double *c = xy + 2 * v[(i + 1) % m];
        double cr = ct_cross(a, b, c);
        if (!(cr > 0.0) && !(cr < 0.0)) {
          out[3 * k] = v[(i + m - 1) % m];
          out[3 * k + 1] = v[i];
          out[3 * k + 2] = v[(i + 1) % m];
          k++;
          memmove(v + i, v + i + 1, (m - i - 1) * sizeof(size_t));
          m--;
          cut = true;
        }
      }
      if (!cut) {
        ds_context_free(context, v);
        return DS_ERR_INVALID; /* not a simple polygon */
      }
    }
  }
  out[3 * k] = v[0];
  out[3 * k + 1] = v[1];
  out[3 * k + 2] = v[2];
  ds_context_free(context, v);
  return DS_OK;
}

/* ---- cell tree
 * ------------------------------------------------------------------------ */

static ds_status_t ct_node(ds_cell_tree_t *t, size_t depth, size_t *out) {
  size_t v;
  if (t->nodes == t->node_capacity) {
    size_t nc = t->node_capacity < 8 ? 8 : t->node_capacity * 2;
    if (bsp_grow(t->context, (void **)&t->line, 3 * sizeof(double), nc) ||
        bsp_grow(t->context, (void **)&t->neg, sizeof(size_t), nc) ||
        bsp_grow(t->context, (void **)&t->pos, sizeof(size_t), nc) ||
        bsp_grow(t->context, (void **)&t->depth, sizeof(size_t), nc) ||
        bsp_grow(t->context, (void **)&t->items, sizeof(size_t *), nc) ||
        bsp_grow(t->context, (void **)&t->item_count, sizeof(size_t), nc) ||
        bsp_grow(t->context, (void **)&t->item_capacity, sizeof(size_t), nc)) {
      return DS_ERR_ALLOC;
    }
    t->node_capacity = nc;
  }
  v = t->nodes++;
  t->line[3 * v] = t->line[3 * v + 1] = t->line[3 * v + 2] = 0.0;
  t->neg[v] = t->pos[v] = BSP_NONE;
  t->depth[v] = depth;
  t->items[v] = NULL;
  t->item_count[v] = t->item_capacity[v] = 0;
  *out = v;
  return DS_OK;
}

ds_status_t ds_cell_tree_init(ds_cell_tree_t *t, size_t bucket,
                              size_t max_depth, ds_context_t *context) {
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
  if (bucket < 2) {
    return DS_ERR_INVALID;
  }
  memset(t, 0, sizeof(*t));
  t->bucket = bucket;
  t->max_depth = max_depth;
  t->poly_free = BSP_NONE;
  t->context = context;
  st = ct_node(t, 0, &t->root);
  if (st != DS_OK) {
    ds_cell_tree_destroy(t);
  }
  return st;
}

void ds_cell_tree_destroy(ds_cell_tree_t *t) {
  size_t i;
  if (t == NULL) {
    return;
  }
  for (i = 0; i < t->polys; i++) {
    ds_context_free(t->context, t->poly[i]);
  }
  for (i = 0; i < t->nodes; i++) {
    ds_context_free(t->context, t->items[i]);
  }
  ds_context_free(t->context, t->poly);
  ds_context_free(t->context, t->poly_n);
  ds_context_free(t->context, t->poly_first);
  ds_context_free(t->context, t->poly_value);
  ds_context_free(t->context, t->poly_alive);
  ds_context_free(t->context, t->poly_stamp);
  ds_context_free(t->context, t->piece);
  ds_context_free(t->context, t->piece_poly);
  ds_context_free(t->context, t->line);
  ds_context_free(t->context, t->neg);
  ds_context_free(t->context, t->pos);
  ds_context_free(t->context, t->depth);
  ds_context_free(t->context, t->items);
  ds_context_free(t->context, t->item_count);
  ds_context_free(t->context, t->item_capacity);
  memset(t, 0, sizeof(*t));
}

size_t ds_cell_tree_size(const ds_cell_tree_t *t) {
  return t == NULL ? 0 : t->size;
}

size_t ds_cell_tree_pieces(const ds_cell_tree_t *t) {
  return t == NULL ? 0 : t->live_pieces;
}

/* side of a triangle: 1 negative only, 2 positive only, 3 both */
static unsigned ct_classify(const double *line, const double *tri) {
  double lo = HUGE_VAL;
  double hi = -HUGE_VAL;
  size_t i;
  for (i = 0; i < 3; i++) {
    double s = line[0] * tri[2 * i] + line[1] * tri[2 * i + 1];
    lo = s < lo ? s : lo;
    hi = s > hi ? s : hi;
  }
  return (lo < line[2] ? 1U : 0U) | (hi < line[2] ? 0U : 2U);
}

static ds_status_t ct_add_item(ds_cell_tree_t *t, size_t v, size_t piece) {
  if (t->item_count[v] == t->item_capacity[v]) {
    size_t nc = t->item_capacity[v] < 4 ? 8 : t->item_capacity[v] * 2;
    if (bsp_grow(t->context, (void **)&t->items[v], sizeof(size_t), nc)) {
      return DS_ERR_ALLOC;
    }
    t->item_capacity[v] = nc;
  }
  t->items[v][t->item_count[v]++] = piece;
  return DS_OK;
}

/* Split leaf v along the principal direction of its pieces' centroids. */
static ds_status_t ct_split(ds_cell_tree_t *t, size_t v) {
  size_t n = t->item_count[v];
  double *cen;
  double *scratch;
  double nrm[2];
  double off;
  size_t i;
  size_t cn = 0;
  size_t cp = 0;
  size_t a = 0;
  size_t b = 0;
  double line[3];
  ds_status_t st = DS_OK;
  cen = (double *)ds_context_alloc(t->context, 2 * n * sizeof(double));
  scratch = (double *)ds_context_alloc(t->context, n * sizeof(double));
  if (cen == NULL || scratch == NULL) {
    ds_context_free(t->context, cen);
    ds_context_free(t->context, scratch);
    return DS_ERR_ALLOC;
  }
  for (i = 0; i < n; i++) {
    const double *tri = t->piece + 6 * t->items[v][i];
    cen[2 * i] = (tri[0] + tri[2] + tri[4]) / 3.0;
    cen[2 * i + 1] = (tri[1] + tri[3] + tri[5]) / 3.0;
  }
  if (!ds_bsp_principal_plane(cen, n, 2, scratch, nrm, &off, NULL)) {
    ds_context_free(t->context, cen);
    ds_context_free(t->context, scratch);
    return DS_EXISTS;
  }
  ds_context_free(t->context, cen);
  ds_context_free(t->context, scratch);
  line[0] = nrm[0];
  line[1] = nrm[1];
  line[2] = off;
  for (i = 0; i < n; i++) {
    unsigned s = ct_classify(line, t->piece + 6 * t->items[v][i]);
    cn += (s & 1U) != 0;
    cp += (s & 2U) != 0;
  }
  if (cn >= n || cp >= n) {
    return DS_EXISTS; /* no shrinking split */
  }
  st = ct_node(t, t->depth[v] + 1, &a);
  if (st == DS_OK) {
    st = ct_node(t, t->depth[v] + 1, &b);
  }
  for (i = 0; i < n && st == DS_OK; i++) {
    size_t pc = t->items[v][i];
    unsigned s = ct_classify(line, t->piece + 6 * pc);
    if (s & 1U) {
      st = ct_add_item(t, a, pc);
    }
    if (st == DS_OK && (s & 2U)) {
      st = ct_add_item(t, b, pc);
    }
  }
  if (st != DS_OK) {
    return st;
  }
  memcpy(t->line + 3 * v, line, sizeof(line));
  ds_context_free(t->context, t->items[v]);
  t->items[v] = NULL;
  t->item_count[v] = t->item_capacity[v] = 0;
  t->neg[v] = a;
  t->pos[v] = b;
  return DS_OK;
}

static ds_status_t ct_place(ds_cell_tree_t *t, size_t piece) {
  size_t stack[2 * 64 + 4];
  size_t top = 0;
  ds_status_t st = DS_OK;
  const double *tri = t->piece + 6 * piece;
  stack[top++] = t->root;
  while (top > 0 && st == DS_OK) {
    size_t v = stack[--top];
    if (t->neg[v] != BSP_NONE) {
      unsigned s = ct_classify(t->line + 3 * v, tri);
      if (s & 1U) {
        stack[top++] = t->neg[v];
      }
      if (s & 2U) {
        stack[top++] = t->pos[v];
      }
      continue;
    }
    st = ct_add_item(t, v, piece);
    if (st == DS_OK && t->item_count[v] > t->bucket &&
        t->depth[v] < t->max_depth) {
      st = ct_split(t, v);
      if (st == DS_EXISTS) {
        st = DS_OK;
      }
    }
  }
  return st;
}

ds_status_t ds_cell_tree_insert(ds_cell_tree_t *t, const double *xy, size_t n,
                                size_t value, size_t *out_handle) {
  size_t *tri;
  size_t h;
  size_t i;
  size_t first;
  ds_status_t st;
  if (t == NULL || xy == NULL) {
    return DS_ERR_NULL;
  }
  if (t->max_depth > 64) {
    t->max_depth = 64; /* bounds the placement stack */
  }
  if (n < 3) {
    return DS_ERR_INVALID;
  }
  tri = (size_t *)ds_context_alloc(t->context, 3 * (n - 2) * sizeof(size_t));
  if (tri == NULL) {
    return DS_ERR_ALLOC;
  }
  st = ds_cell_tree_triangulate(xy, n, tri, t->context);
  if (st != DS_OK) {
    ds_context_free(t->context, tri);
    return st;
  }
  if (t->poly_free != BSP_NONE) {
    h = t->poly_free;
    t->poly_free = t->poly_first[h];
  } else {
    if (t->polys == t->poly_capacity) {
      size_t nc = t->poly_capacity < 8 ? 8 : t->poly_capacity * 2;
      if (bsp_grow(t->context, (void **)&t->poly, sizeof(double *), nc) ||
          bsp_grow(t->context, (void **)&t->poly_n, sizeof(size_t), nc) ||
          bsp_grow(t->context, (void **)&t->poly_first, sizeof(size_t), nc) ||
          bsp_grow(t->context, (void **)&t->poly_value, sizeof(size_t), nc) ||
          bsp_grow(t->context, (void **)&t->poly_alive, 1, nc) ||
          bsp_grow(t->context, (void **)&t->poly_stamp, sizeof(unsigned), nc)) {
        ds_context_free(t->context, tri);
        return DS_ERR_ALLOC;
      }
      t->poly_capacity = nc;
    }
    h = t->polys++;
    t->poly[h] = NULL;
  }
  t->poly[h] = (double *)ds_context_alloc(t->context, 2 * n * sizeof(double));
  if (t->poly[h] == NULL ||
      (t->pieces + n - 2 > t->piece_capacity &&
       (bsp_grow(t->context, (void **)&t->piece, 6 * sizeof(double),
                 (t->pieces + n - 2) * 2) ||
        bsp_grow(t->context, (void **)&t->piece_poly, sizeof(size_t),
                 (t->pieces + n - 2) * 2)))) {
    ds_context_free(t->context, t->poly[h]);
    t->poly[h] = NULL;
    t->poly_first[h] = t->poly_free;
    t->poly_alive[h] = 0;
    t->poly_free = h;
    ds_context_free(t->context, tri);
    return DS_ERR_ALLOC;
  }
  if (t->pieces + n - 2 > t->piece_capacity) {
    t->piece_capacity = (t->pieces + n - 2) * 2;
  }
  memcpy(t->poly[h], xy, 2 * n * sizeof(double));
  t->poly_n[h] = n;
  t->poly_value[h] = value;
  t->poly_alive[h] = 1;
  t->poly_stamp[h] = 0;
  first = t->pieces;
  t->poly_first[h] = first;
  for (i = 0; i < n - 2; i++) {
    size_t j;
    for (j = 0; j < 3; j++) {
      t->piece[6 * (first + i) + 2 * j] = xy[2 * tri[3 * i + j]];
      t->piece[6 * (first + i) + 2 * j + 1] = xy[2 * tri[3 * i + j] + 1];
    }
    t->piece_poly[first + i] = h;
  }
  t->pieces += n - 2;
  ds_context_free(t->context, tri);
  for (i = 0; i < n - 2 && st == DS_OK; i++) {
    st = ct_place(t, first + i);
  }
  if (st != DS_OK) {
    return st;
  }
  t->live_pieces += n - 2;
  t->size++;
  if (out_handle != NULL) {
    *out_handle = h;
  }
  return DS_OK;
}

ds_status_t ds_cell_tree_remove(ds_cell_tree_t *t, size_t handle) {
  size_t i;
  size_t np;
  if (t == NULL) {
    return DS_ERR_NULL;
  }
  if (handle >= t->polys || !t->poly_alive[handle]) {
    return DS_NOT_FOUND;
  }
  np = t->poly_n[handle] - 2;
  for (i = 0; i < np; i++) {
    size_t pc = t->poly_first[handle] + i;
    size_t stack[2 * 64 + 4];
    size_t top = 0;
    stack[top++] = t->root;
    while (top > 0) {
      size_t v = stack[--top];
      size_t j;
      if (t->neg[v] != BSP_NONE) {
        unsigned s = ct_classify(t->line + 3 * v, t->piece + 6 * pc);
        if (s & 1U) {
          stack[top++] = t->neg[v];
        }
        if (s & 2U) {
          stack[top++] = t->pos[v];
        }
        continue;
      }
      for (j = 0; j < t->item_count[v]; j++) {
        if (t->items[v][j] == pc) {
          t->items[v][j] = t->items[v][--t->item_count[v]];
          break;
        }
      }
    }
    t->piece_poly[pc] = BSP_NONE;
  }
  ds_context_free(t->context, t->poly[handle]);
  t->poly[handle] = NULL;
  t->poly_alive[handle] = 0;
  t->poly_first[handle] = t->poly_free;
  t->poly_free = handle;
  t->live_pieces -= np;
  t->size--;
  return DS_OK;
}

static void ct_epoch(ds_cell_tree_t *t) {
  if (++t->epoch == 0) {
    size_t i;
    for (i = 0; i < t->polys; i++) {
      t->poly_stamp[i] = 0;
    }
    t->epoch = 1;
  }
}

ds_status_t ds_cell_tree_query_point(ds_cell_tree_t *t, double x, double y,
                                     ds_cell_visit_func_t visit, void *user) {
  size_t v;
  size_t i;
  double p[2];
  if (t == NULL || visit == NULL) {
    return DS_ERR_NULL;
  }
  p[0] = x;
  p[1] = y;
  ct_epoch(t);
  v = t->root;
  while (t->neg[v] != BSP_NONE) {
    const double *l = t->line + 3 * v;
    v = l[0] * x + l[1] * y < l[2] ? t->neg[v] : t->pos[v];
  }
  for (i = 0; i < t->item_count[v]; i++) {
    size_t pc = t->items[v][i];
    size_t h = t->piece_poly[pc];
    const double *tri = t->piece + 6 * pc;
    bool in;
    if (t->poly_stamp[h] == t->epoch) {
      continue;
    }
    in = ct_cross(tri, tri + 2, tri + 4) >= 0.0
             ? ct_in_tri(p, tri, tri + 2, tri + 4)
             : ct_in_tri(p, tri, tri + 4, tri + 2);
    if (in) {
      t->poly_stamp[h] = t->epoch;
      if (!visit(t->poly_value[h], user)) {
        return DS_STOP;
      }
    }
  }
  return DS_OK;
}

/* closed triangle meets closed box (separating axes) */
static bool ct_tri_box(const double *tri, const double *box) {
  size_t i;
  double lo;
  double hi;
  for (i = 0; i < 2; i++) {
    lo = tri[i];
    hi = tri[i];
    lo = tri[2 + i] < lo ? tri[2 + i] : lo;
    hi = tri[2 + i] > hi ? tri[2 + i] : hi;
    lo = tri[4 + i] < lo ? tri[4 + i] : lo;
    hi = tri[4 + i] > hi ? tri[4 + i] : hi;
    if (hi < box[i] || lo > box[2 + i]) {
      return false;
    }
  }
  for (i = 0; i < 3; i++) {
    const double *a = tri + 2 * i;
    const double *b = tri + 2 * ((i + 1) % 3);
    double nx = b[1] - a[1];
    double ny = a[0] - b[0];
    double tlo = HUGE_VAL;
    double thi = -HUGE_VAL;
    double blo;
    double bhi;
    size_t j;
    for (j = 0; j < 3; j++) {
      double s = nx * tri[2 * j] + ny * tri[2 * j + 1];
      tlo = s < tlo ? s : tlo;
      thi = s > thi ? s : thi;
    }
    blo = (nx >= 0.0 ? nx * box[0] : nx * box[2]) +
          (ny >= 0.0 ? ny * box[1] : ny * box[3]);
    bhi = (nx >= 0.0 ? nx * box[2] : nx * box[0]) +
          (ny >= 0.0 ? ny * box[3] : ny * box[1]);
    if (thi < blo || tlo > bhi) {
      return false;
    }
  }
  return true;
}

ds_status_t ds_cell_tree_query_box(ds_cell_tree_t *t, const double *box,
                                   ds_cell_visit_func_t visit, void *user) {
  size_t *stack;
  size_t top = 0;
  ds_status_t st = DS_OK;
  if (t == NULL || box == NULL || visit == NULL) {
    return DS_ERR_NULL;
  }
  stack =
      (size_t *)ds_context_alloc(t->context, (t->nodes + 1) * sizeof(size_t));
  if (stack == NULL) {
    return DS_ERR_ALLOC;
  }
  ct_epoch(t);
  stack[top++] = t->root;
  while (top > 0 && st == DS_OK) {
    size_t v = stack[--top];
    size_t i;
    if (t->neg[v] != BSP_NONE) {
      double lo;
      double hi;
      bsp_box_range(t->line + 3 * v, box, 2, &lo, &hi);
      if (lo < t->line[3 * v + 2]) {
        stack[top++] = t->neg[v];
      }
      if (!(hi < t->line[3 * v + 2])) {
        stack[top++] = t->pos[v];
      }
      continue;
    }
    for (i = 0; i < t->item_count[v]; i++) {
      size_t pc = t->items[v][i];
      size_t h = t->piece_poly[pc];
      if (t->poly_stamp[h] == t->epoch || !ct_tri_box(t->piece + 6 * pc, box)) {
        continue;
      }
      t->poly_stamp[h] = t->epoch;
      if (!visit(t->poly_value[h], user)) {
        st = DS_STOP;
        break;
      }
    }
  }
  ds_context_free(t->context, stack);
  return st;
}
