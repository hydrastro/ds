#include "d_tree.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

#define DT_NONE (~(size_t)0)

typedef struct dt_edge {
  double x0;
  double y0;
  double x1;
  double y1;
  size_t side; /* 0: left half, 1: right half */
} dt_edge_t;

static int dt_cmp_edge(const void *a, const void *b) {
  const dt_edge_t *e = (const dt_edge_t *)a;
  const dt_edge_t *f = (const dt_edge_t *)b;
  if (e->x0 < f->x0) {
    return -1;
  }
  if (e->x0 > f->x0) {
    return 1;
  }
  if (e->y0 < f->y0) {
    return -1;
  }
  if (e->y0 > f->y0) {
    return 1;
  }
  if (e->x1 < f->x1) {
    return -1;
  }
  if (e->x1 > f->x1) {
    return 1;
  }
  if (e->y1 < f->y1) {
    return -1;
  }
  if (e->y1 > f->y1) {
    return 1;
  }
  return 0;
}

static bool dt_same(const dt_edge_t *e, const dt_edge_t *f) {
  return dt_cmp_edge(e, f) == 0;
}

/* even-odd ray cast to +x against a set of edges (half-open in y) */
static bool dt_inside(const double *edges, size_t m, double x, double y) {
  size_t i;
  bool in = false;
  for (i = 0; i < m; i++) {
    double x0 = edges[4 * i];
    double y0 = edges[4 * i + 1];
    double x1 = edges[4 * i + 2];
    double y1 = edges[4 * i + 3];
    if ((y0 > y) != (y1 > y)) {
      double xc = x0 + (y - y0) * (x1 - x0) / (y1 - y0);
      if (x < xc) {
        in = !in;
      }
    }
  }
  return in;
}

typedef struct dt_keyed {
  double key;
  size_t id;
} dt_keyed_t;

static int dt_cmp_keyed(const void *a, const void *b) {
  const dt_keyed_t *x = (const dt_keyed_t *)a;
  const dt_keyed_t *y = (const dt_keyed_t *)b;
  if (x->key < y->key) {
    return -1;
  }
  if (x->key > y->key) {
    return 1;
  }
  return (x->id > y->id) - (x->id < y->id);
}

typedef struct dt_work {
  size_t node;
  size_t first;
  size_t count;
  size_t depth;
} dt_work_t;

ds_status_t ds_d_tree_build(ds_d_tree_t *t, const double *xy,
                            const size_t *offsets, const size_t *values,
                            size_t n, ds_context_t *context) {
  size_t i;
  size_t cap;
  size_t *ids = NULL;
  double *cen = NULL;
  dt_work_t *work = NULL;
  dt_edge_t *ebuf = NULL;
  dt_keyed_t *keyed = NULL;
  size_t wn = 0;
  size_t total_edges;
  ds_status_t st = DS_OK;
  if (t == NULL || (n > 0 && (xy == NULL || offsets == NULL))) {
    return DS_ERR_NULL;
  }
  if (n == 0) {
    return DS_ERR_INVALID;
  }
  for (i = 0; i < n; i++) {
    size_t k;
    if (offsets[i + 1] < offsets[i] + 3) {
      return DS_ERR_INVALID;
    }
    for (k = 2 * offsets[i]; k < 2 * offsets[i + 1]; k++) {
      if (!(xy[k] > -HUGE_VAL && xy[k] < HUGE_VAL)) {
        return DS_ERR_INVALID;
      }
    }
  }
  memset(t, 0, sizeof(*t));
  t->regions = n;
  t->context = context;
  cap = 2 * n;
  total_edges = offsets[n] - offsets[0];
#define DT_ALLOC(field, type, count)                                           \
  t->field = (type *)ds_context_calloc(context, (count), sizeof(type));        \
  if (t->field == NULL) {                                                      \
    st = DS_ERR_ALLOC;                                                         \
    goto fail;                                                                 \
  }
  DT_ALLOC(value, size_t, n)
  DT_ALLOC(poly, double *, n)
  DT_ALLOC(poly_n, size_t, n)
  DT_ALLOC(left, size_t, cap)
  DT_ALLOC(right, size_t, cap)
  DT_ALLOC(region, size_t, cap)
  DT_ALLOC(edges, double *, cap)
  DT_ALLOC(edge_count, size_t, cap)
  DT_ALLOC(division, size_t, cap)
#undef DT_ALLOC
  ids = (size_t *)ds_context_alloc(context, n * sizeof(size_t));
  cen = (double *)ds_context_alloc(context, 2 * n * sizeof(double));
  work = (dt_work_t *)ds_context_alloc(context, (cap + 1) * sizeof(dt_work_t));
  ebuf = (dt_edge_t *)ds_context_alloc(context,
                                       (total_edges + 1) * sizeof(dt_edge_t));
  keyed = (dt_keyed_t *)ds_context_alloc(context, n * sizeof(dt_keyed_t));
  if (ids == NULL || cen == NULL || work == NULL || ebuf == NULL ||
      keyed == NULL) {
    st = DS_ERR_ALLOC;
    goto fail;
  }
  for (i = 0; i < n; i++) {
    size_t m = offsets[i + 1] - offsets[i];
    size_t k;
    t->value[i] = values != NULL ? values[i] : i;
    t->poly_n[i] = m;
    t->poly[i] = (double *)ds_context_alloc(context, 2 * m * sizeof(double));
    if (t->poly[i] == NULL) {
      st = DS_ERR_ALLOC;
      goto fail;
    }
    memcpy(t->poly[i], xy + 2 * offsets[i], 2 * m * sizeof(double));
    cen[2 * i] = cen[2 * i + 1] = 0.0;
    for (k = 0; k < m; k++) {
      cen[2 * i] += t->poly[i][2 * k];
      cen[2 * i + 1] += t->poly[i][2 * k + 1];
    }
    cen[2 * i] /= (double)m;
    cen[2 * i + 1] /= (double)m;
    ids[i] = i;
  }
  t->nodes = 1;
  t->root = 0;
  work[0].node = 0;
  work[0].first = 0;
  work[0].count = n;
  work[0].depth = 0;
  wn = 1;
  while (wn > 0) {
    dt_work_t w = work[--wn];
    size_t half;
    size_t axis = 0;
    double lo[2] = {HUGE_VAL, HUGE_VAL};
    double hi[2] = {-HUGE_VAL, -HUGE_VAL};
    size_t ne = 0;
    size_t j;
    size_t k;
    size_t kept = 0;
    size_t shared = 0;
    if (w.depth + 1 > t->height) {
      t->height = w.depth + 1;
    }
    if (w.count == 1) {
      t->left[w.node] = t->right[w.node] = DT_NONE;
      t->region[w.node] = ids[w.first];
      continue;
    }
    for (i = w.first; i < w.first + w.count; i++) {
      for (k = 0; k < 2; k++) {
        double c = cen[2 * ids[i] + k];
        lo[k] = c < lo[k] ? c : lo[k];
        hi[k] = c > hi[k] ? c : hi[k];
      }
    }
    axis = hi[1] - lo[1] > hi[0] - lo[0] ? 1 : 0;
    /* sort this range by centroid along the wider axis */
    for (i = 0; i < w.count; i++) {
      keyed[i].key = cen[2 * ids[w.first + i] + axis];
      keyed[i].id = ids[w.first + i];
    }
    qsort(keyed, w.count, sizeof(dt_keyed_t), dt_cmp_keyed);
    for (i = 0; i < w.count; i++) {
      ids[w.first + i] = keyed[i].id;
    }
    half = w.count / 2;
    /* edges of both halves, normalised, sorted */
    for (i = w.first; i < w.first + w.count; i++) {
      size_t r = ids[i];
      size_t m = t->poly_n[r];
      for (k = 0; k < m; k++) {
        const double *a = t->poly[r] + 2 * k;
        const double *b = t->poly[r] + 2 * ((k + 1) % m);
        dt_edge_t *e = &ebuf[ne++];
        bool swap = a[0] > b[0] || (!(a[0] < b[0]) && a[1] > b[1]);
        e->x0 = swap ? b[0] : a[0];
        e->y0 = swap ? b[1] : a[1];
        e->x1 = swap ? a[0] : b[0];
        e->y1 = swap ? a[1] : b[1];
        e->side = i < w.first + half ? 0 : 1;
      }
    }
    qsort(ebuf, ne, sizeof(dt_edge_t), dt_cmp_edge);
    /* the left half's boundary: its edges not shared inside it */
    t->edges[w.node] =
        (double *)ds_context_alloc(context, (ne + 1) * 4 * sizeof(double));
    if (t->edges[w.node] == NULL) {
      st = DS_ERR_ALLOC;
      goto fail;
    }
    for (i = 0; i < ne;) {
      size_t run = 1;
      size_t lefts = 0;
      size_t rights = 0;
      while (i + run < ne && dt_same(&ebuf[i], &ebuf[i + run])) {
        run++;
      }
      for (j = i; j < i + run; j++) {
        lefts += ebuf[j].side == 0;
        rights += ebuf[j].side == 1;
      }
      if (lefts % 2 == 1) {
        double *o = t->edges[w.node] + 4 * kept++;
        o[0] = ebuf[i].x0;
        o[1] = ebuf[i].y0;
        o[2] = ebuf[i].x1;
        o[3] = ebuf[i].y1;
      }
      if (lefts > 0 && rights > 0) {
        shared++;
      }
      i += run;
    }
    t->edge_count[w.node] = kept;
    t->division[w.node] = shared;
    t->left[w.node] = t->nodes;
    t->right[w.node] = t->nodes + 1;
    work[wn].node = t->nodes;
    work[wn].first = w.first;
    work[wn].count = half;
    work[wn].depth = w.depth + 1;
    work[wn + 1].node = t->nodes + 1;
    work[wn + 1].first = w.first + half;
    work[wn + 1].count = w.count - half;
    work[wn + 1].depth = w.depth + 1;
    wn += 2;
    t->nodes += 2;
  }
  ds_context_free(context, ids);
  ds_context_free(context, cen);
  ds_context_free(context, work);
  ds_context_free(context, ebuf);
  ds_context_free(context, keyed);
  return DS_OK;
fail:
  ds_context_free(context, ids);
  ds_context_free(context, cen);
  ds_context_free(context, work);
  ds_context_free(context, ebuf);
  ds_context_free(context, keyed);
  ds_d_tree_destroy(t);
  return st;
}

void ds_d_tree_destroy(ds_d_tree_t *t) {
  size_t i;
  if (t == NULL) {
    return;
  }
  if (t->poly != NULL) {
    for (i = 0; i < t->regions; i++) {
      ds_context_free(t->context, t->poly[i]);
    }
  }
  if (t->edges != NULL) {
    for (i = 0; i < 2 * t->regions; i++) {
      ds_context_free(t->context, t->edges[i]);
    }
  }
  ds_context_free(t->context, t->value);
  ds_context_free(t->context, t->poly);
  ds_context_free(t->context, t->poly_n);
  ds_context_free(t->context, t->left);
  ds_context_free(t->context, t->right);
  ds_context_free(t->context, t->region);
  ds_context_free(t->context, t->edges);
  ds_context_free(t->context, t->edge_count);
  ds_context_free(t->context, t->division);
  memset(t, 0, sizeof(*t));
}

size_t ds_d_tree_height(const ds_d_tree_t *t) {
  return t == NULL ? 0 : t->height;
}

size_t ds_d_tree_root_division(const ds_d_tree_t *t) {
  return t == NULL || t->left == NULL || t->left[t->root] == DT_NONE
             ? 0
             : t->division[t->root];
}

ds_status_t ds_d_tree_locate(const ds_d_tree_t *t, double x, double y,
                             size_t *out_value) {
  size_t v;
  size_t r;
  size_t m;
  double *e;
  size_t k;
  bool in;
  if (t == NULL || out_value == NULL) {
    return DS_ERR_NULL;
  }
  if (t->left == NULL) {
    return DS_NOT_FOUND;
  }
  v = t->root;
  while (t->left[v] != DT_NONE) {
    v = dt_inside(t->edges[v], t->edge_count[v], x, y) ? t->left[v]
                                                       : t->right[v];
  }
  /* the leaf's region must hold the point (outside the subdivision it
   * does not) */
  r = t->region[v];
  m = t->poly_n[r];
  e = (double *)ds_context_alloc(t->context, 4 * m * sizeof(double));
  if (e == NULL) {
    return DS_ERR_ALLOC;
  }
  for (k = 0; k < m; k++) {
    e[4 * k] = t->poly[r][2 * k];
    e[4 * k + 1] = t->poly[r][2 * k + 1];
    e[4 * k + 2] = t->poly[r][2 * ((k + 1) % m)];
    e[4 * k + 3] = t->poly[r][2 * ((k + 1) % m) + 1];
  }
  in = dt_inside(e, m, x, y);
  ds_context_free(t->context, e);
  if (!in) {
    return DS_NOT_FOUND;
  }
  *out_value = t->value[r];
  return DS_OK;
}
