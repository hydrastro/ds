#include "grid_file.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

#define GF_NONE (~(size_t)0)
#define GF_MAXD DS_GRID_MAX_DIM

/* ---- helpers
 * ------------------------------------------------------------------- */

static bool gf_finite(const double *x, size_t n) {
  size_t i;
  for (i = 0; i < n; i++) {
    if (!(x[i] > -HUGE_VAL && x[i] < HUGE_VAL)) {
      return false;
    }
  }
  return true;
}

static bool gf_point_in_box(const double *p, const double *b, size_t d) {
  size_t a;
  for (a = 0; a < d; a++) {
    if (p[a] < b[a] || p[a] > b[d + a]) {
      return false;
    }
  }
  return true;
}

static bool gf_box_hit(const double *x, const double *y, size_t d) {
  size_t a;
  for (a = 0; a < d; a++) {
    if (x[d + a] < y[a] || y[d + a] < x[a]) {
      return false;
    }
  }
  return true;
}

static double gf_dist2(const double *x, const double *y, size_t d) {
  size_t a;
  double s = 0.0;
  for (a = 0; a < d; a++) {
    s += (x[a] - y[a]) * (x[a] - y[a]);
  }
  return s;
}

static double gf_mindist2(const double *b, const double *p, size_t d) {
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

static int gf_cmp_double(const void *a, const void *b) {
  double x = *(const double *)a;
  double y = *(const double *)b;
  return (x > y) - (x < y);
}

static void gf_best_push(double *dk, size_t *vk, size_t k, size_t *found,
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

static ds_status_t gf_append(ds_context_t *ctx, double **items, size_t **vals,
                             size_t *count, size_t *cap, size_t width,
                             const double *item, size_t value) {
  if (*count == *cap) {
    size_t nc = *cap < 4 ? 4 : *cap * 2;
    void *p = ds_context_realloc(ctx, *items, nc * width * sizeof(double));
    if (p == NULL) {
      return DS_ERR_ALLOC;
    }
    *items = (double *)p;
    p = ds_context_realloc(ctx, *vals, nc * sizeof(size_t));
    if (p == NULL) {
      return DS_ERR_ALLOC;
    }
    *vals = (size_t *)p;
    *cap = nc;
  }
  memcpy(*items + *count * width, item, width * sizeof(double));
  (*vals)[(*count)++] = value;
  return DS_OK;
}

/* index of (item, value) in a list, or GF_NONE */
static size_t gf_find(const double *items, const size_t *vals, size_t count,
                      size_t width, const double *item, size_t value) {
  size_t i;
  for (i = 0; i < count; i++) {
    if (vals[i] == value &&
        memcmp(items + i * width, item, width * sizeof(double)) == 0) {
      return i;
    }
  }
  return GF_NONE;
}

static void gf_drop(double *items, size_t *vals, size_t *count, size_t width,
                    size_t i) {
  size_t last = --*count;
  if (i != last) {
    memcpy(items + i * width, items + last * width, width * sizeof(double));
    vals[i] = vals[last];
  }
}

/* ---- fixed grid
 * -------------------------------------------------------------- */

ds_status_t ds_fixed_grid_init(ds_fixed_grid_t *g, size_t dim,
                               const double *domain, const size_t *cells,
                               ds_context_t *context) {
  size_t a;
  size_t n = 1;
  if (g == NULL || domain == NULL || cells == NULL) {
    return DS_ERR_NULL;
  }
  if (dim == 0 || dim > GF_MAXD || !gf_finite(domain, 2 * dim)) {
    return DS_ERR_INVALID;
  }
  for (a = 0; a < dim; a++) {
    if (cells[a] == 0 || !(domain[a] < domain[dim + a]) ||
        cells[a] > ((size_t)1 << 24) / n) {
      return DS_ERR_INVALID;
    }
    n *= cells[a];
  }
  memset(g, 0, sizeof(*g));
  g->dim = dim;
  memcpy(g->domain, domain, 2 * dim * sizeof(double));
  memcpy(g->cells, cells, dim * sizeof(size_t));
  g->cell_count = n;
  g->context = context;
  g->points = (double **)ds_context_calloc(context, n, sizeof(double *));
  g->values = (size_t **)ds_context_calloc(context, n, sizeof(size_t *));
  g->count = (size_t *)ds_context_calloc(context, n, sizeof(size_t));
  g->capacity = (size_t *)ds_context_calloc(context, n, sizeof(size_t));
  if (g->points == NULL || g->values == NULL || g->count == NULL ||
      g->capacity == NULL) {
    ds_fixed_grid_destroy(g);
    return DS_ERR_ALLOC;
  }
  return DS_OK;
}

void ds_fixed_grid_destroy(ds_fixed_grid_t *g) {
  size_t i;
  if (g == NULL) {
    return;
  }
  for (i = 0; i < g->cell_count; i++) {
    if (g->points != NULL) {
      ds_context_free(g->context, g->points[i]);
    }
    if (g->values != NULL) {
      ds_context_free(g->context, g->values[i]);
    }
  }
  ds_context_free(g->context, g->points);
  ds_context_free(g->context, g->values);
  ds_context_free(g->context, g->count);
  ds_context_free(g->context, g->capacity);
  memset(g, 0, sizeof(*g));
}

size_t ds_fixed_grid_size(const ds_fixed_grid_t *g) {
  return g == NULL ? 0 : g->size;
}

static size_t fg_axis_cell(const ds_fixed_grid_t *g, size_t a, double x) {
  double lo = g->domain[a];
  double hi = g->domain[g->dim + a];
  double f;
  size_t c;
  if (!(x > lo)) {
    return 0;
  }
  if (!(x < hi)) {
    return g->cells[a] - 1;
  }
  f = (x - lo) / (hi - lo) * (double)g->cells[a];
  c = (size_t)f;
  return c < g->cells[a] ? c : g->cells[a] - 1;
}

size_t ds_fixed_grid_cell(const ds_fixed_grid_t *g, const double *point) {
  size_t a;
  size_t idx = 0;
  if (g == NULL || point == NULL) {
    return 0;
  }
  for (a = 0; a < g->dim; a++) {
    idx = idx * g->cells[a] + fg_axis_cell(g, a, point[a]);
  }
  return idx;
}

ds_status_t ds_fixed_grid_insert(ds_fixed_grid_t *g, const double *point,
                                 size_t value) {
  size_t c;
  ds_status_t st;
  if (g == NULL || point == NULL) {
    return DS_ERR_NULL;
  }
  if (!gf_finite(point, g->dim)) {
    return DS_ERR_INVALID;
  }
  c = ds_fixed_grid_cell(g, point);
  st = gf_append(g->context, &g->points[c], &g->values[c], &g->count[c],
                 &g->capacity[c], g->dim, point, value);
  if (st == DS_OK) {
    g->size++;
  }
  return st;
}

ds_status_t ds_fixed_grid_remove(ds_fixed_grid_t *g, const double *point,
                                 size_t value) {
  size_t c;
  size_t i;
  if (g == NULL || point == NULL) {
    return DS_ERR_NULL;
  }
  if (!gf_finite(point, g->dim)) {
    return DS_NOT_FOUND;
  }
  c = ds_fixed_grid_cell(g, point);
  i = gf_find(g->points[c], g->values[c], g->count[c], g->dim, point, value);
  if (i == GF_NONE) {
    return DS_NOT_FOUND;
  }
  gf_drop(g->points[c], g->values[c], &g->count[c], g->dim, i);
  g->size--;
  return DS_OK;
}

ds_status_t ds_fixed_grid_query(const ds_fixed_grid_t *g, const double *box,
                                ds_grid_visit_func_t visit, void *user) {
  size_t d;
  size_t lo[GF_MAXD];
  size_t hi[GF_MAXD];
  size_t cur[GF_MAXD];
  size_t a;
  if (g == NULL || box == NULL || visit == NULL) {
    return DS_ERR_NULL;
  }
  d = g->dim;
  for (a = 0; a < d; a++) {
    if (box[a] > box[d + a]) {
      return DS_OK;
    }
    lo[a] = fg_axis_cell(g, a, box[a]);
    hi[a] = fg_axis_cell(g, a, box[d + a]);
    cur[a] = lo[a];
  }
  for (;;) {
    size_t idx = 0;
    size_t i;
    for (a = 0; a < d; a++) {
      idx = idx * g->cells[a] + cur[a];
    }
    for (i = 0; i < g->count[idx]; i++) {
      const double *p = g->points[idx] + i * d;
      if (gf_point_in_box(p, box, d) && !visit(p, g->values[idx][i], user)) {
        return DS_STOP;
      }
    }
    for (a = d; a-- > 0;) {
      if (cur[a] < hi[a]) {
        cur[a]++;
        break;
      }
      cur[a] = lo[a];
    }
    if (a == (size_t)-1) {
      break;
    }
  }
  return DS_OK;
}

/* The cell box of a row-major index. */
static void fg_cell_box(const ds_fixed_grid_t *g, size_t idx, double *b) {
  size_t a;
  size_t d = g->dim;
  for (a = d; a-- > 0;) {
    size_t c = idx % g->cells[a];
    double w = (g->domain[d + a] - g->domain[a]) / (double)g->cells[a];
    idx /= g->cells[a];
    b[a] = c == 0 ? -HUGE_VAL : g->domain[a] + w * (double)c;
    b[d + a] =
        c + 1 == g->cells[a] ? HUGE_VAL : g->domain[a] + w * (double)(c + 1);
  }
}

typedef struct gf_cand {
  double dist;
  size_t id;
} gf_cand_t;

static int gf_cmp_cand(const void *a, const void *b) {
  double x = ((const gf_cand_t *)a)->dist;
  double y = ((const gf_cand_t *)b)->dist;
  return (x > y) - (x < y);
}

ds_status_t ds_fixed_grid_nearest(const ds_fixed_grid_t *g, const double *point,
                                  size_t k, size_t *out_values,
                                  double *out_dist, size_t *out_found) {
  gf_cand_t *cand;
  double *dk;
  size_t *vk;
  size_t nc = 0;
  size_t found = 0;
  size_t i;
  size_t j;
  size_t d;
  if (g == NULL || point == NULL || out_found == NULL ||
      (k > 0 && (out_values == NULL || out_dist == NULL))) {
    return DS_ERR_NULL;
  }
  *out_found = 0;
  if (k == 0 || g->size == 0) {
    return DS_OK;
  }
  d = g->dim;
  cand = (gf_cand_t *)ds_context_alloc(g->context,
                                       g->cell_count * sizeof(gf_cand_t));
  dk = (double *)ds_context_alloc(g->context, k * sizeof(double));
  vk = (size_t *)ds_context_alloc(g->context, k * sizeof(size_t));
  if (cand == NULL || dk == NULL || vk == NULL) {
    ds_context_free(g->context, cand);
    ds_context_free(g->context, dk);
    ds_context_free(g->context, vk);
    return DS_ERR_ALLOC;
  }
  for (i = 0; i < g->cell_count; i++) {
    if (g->count[i] > 0) {
      double b[2 * GF_MAXD];
      fg_cell_box(g, i, b);
      cand[nc].dist = gf_mindist2(b, point, d);
      cand[nc++].id = i;
    }
  }
  qsort(cand, nc, sizeof(gf_cand_t), gf_cmp_cand);
  for (i = 0; i < nc; i++) {
    size_t c = cand[i].id;
    if (found == k && !(cand[i].dist < dk[k - 1])) {
      break;
    }
    for (j = 0; j < g->count[c]; j++) {
      gf_best_push(dk, vk, k, &found, gf_dist2(g->points[c] + j * d, point, d),
                   g->values[c][j]);
    }
  }
  for (i = 0; i < found; i++) {
    out_values[i] = vk[i];
    out_dist[i] = sqrt(dk[i]);
  }
  *out_found = found;
  ds_context_free(g->context, cand);
  ds_context_free(g->context, dk);
  ds_context_free(g->context, vk);
  return DS_OK;
}

/* ---- grid file
 * -------------------------------------------------------------------- */

/* pending items pushed out of a layer (multilayer files) */
typedef struct gf_evict {
  double *items;
  size_t *values;
  size_t count;
  size_t capacity;
} gf_evict_t;

static size_t gf_cell(const ds_grid_file_t *g, size_t a, double x) {
  size_t lo = 0;
  size_t hi = g->scale_count[a];
  while (lo < hi) {
    size_t mid = lo + (hi - lo) / 2;
    if (g->scale[a][mid] <= x) {
      lo = mid + 1;
    } else {
      hi = mid;
    }
  }
  return lo;
}

static size_t gf_index(const ds_grid_file_t *g, const size_t *c) {
  size_t a;
  size_t idx = 0;
  for (a = 0; a < g->dim; a++) {
    idx = idx * (g->scale_count[a] + 1) + c[a];
  }
  return idx;
}

static size_t gf_locate(const ds_grid_file_t *g, const double *p) {
  size_t c[GF_MAXD];
  size_t a;
  for (a = 0; a < g->dim; a++) {
    c[a] = gf_cell(g, a, p[a]);
  }
  return g->dir[gf_index(g, c)];
}

static void gf_region(const ds_grid_file_t *g, size_t b, double *r) {
  size_t a;
  size_t d = g->dim;
  for (a = 0; a < d; a++) {
    size_t lo = g->lo[b * d + a];
    size_t hi = g->hi[b * d + a];
    r[a] = lo == 0 ? -HUGE_VAL : g->scale[a][lo - 1];
    r[d + a] = hi == g->scale_count[a] ? HUGE_VAL : g->scale[a][hi];
  }
}

static ds_status_t gf_new_bucket(ds_grid_file_t *g, size_t *out) {
  size_t b;
  if (g->free_list != GF_NONE) {
    b = g->free_list;
    g->free_list = g->count[b];
  } else {
    if (g->buckets == g->bucket_capacity) {
      size_t nc = g->bucket_capacity < 8 ? 8 : g->bucket_capacity * 2;
      void *p;
#define GF_RE(field, type, per)                                                \
  p = ds_context_realloc(g->context, g->field, nc * (per) * sizeof(type));     \
  if (p == NULL) {                                                             \
    return DS_ERR_ALLOC;                                                       \
  }                                                                            \
  g->field = (type *)p;
      GF_RE(lo, size_t, g->dim)
      GF_RE(hi, size_t, g->dim)
      GF_RE(count, size_t, 1)
      GF_RE(capacity, size_t, 1)
      GF_RE(items, double *, 1)
      GF_RE(values, size_t *, 1)
      GF_RE(alive, unsigned char, 1)
      GF_RE(stamp, unsigned, 1)
#undef GF_RE
      g->bucket_capacity = nc;
    }
    b = g->buckets++;
  }
  g->count[b] = 0;
  g->capacity[b] = 0;
  g->items[b] = NULL;
  g->values[b] = NULL;
  g->alive[b] = 1;
  g->stamp[b] = 0;
  g->live_buckets++;
  *out = b;
  return DS_OK;
}

static void gf_free_bucket(ds_grid_file_t *g, size_t b) {
  ds_context_free(g->context, g->items[b]);
  ds_context_free(g->context, g->values[b]);
  g->items[b] = NULL;
  g->values[b] = NULL;
  g->alive[b] = 0;
  g->count[b] = g->free_list;
  g->free_list = b;
  g->live_buckets--;
}

static ds_status_t gf_init_any(ds_grid_file_t *g, size_t dim, size_t width,
                               size_t bucket, size_t max_cells,
                               ds_context_t *context) {
  size_t b;
  size_t a;
  ds_status_t st;
  if (g == NULL) {
    return DS_ERR_NULL;
  }
  if (bucket == 0) {
    bucket = 16;
  }
  if (max_cells == 0) {
    max_cells = (size_t)1 << 22;
  }
  if (dim == 0 || dim > GF_MAXD || bucket < 2) {
    return DS_ERR_INVALID;
  }
  memset(g, 0, sizeof(*g));
  g->dim = dim;
  g->width = width;
  g->bucket = bucket;
  g->max_cells = max_cells;
  g->free_list = GF_NONE;
  g->context = context;
  g->dir = (size_t *)ds_context_alloc(context, sizeof(size_t));
  if (g->dir == NULL) {
    return DS_ERR_ALLOC;
  }
  g->dir_size = 1;
  st = gf_new_bucket(g, &b);
  if (st != DS_OK) {
    ds_grid_file_destroy(g);
    return st;
  }
  for (a = 0; a < dim; a++) {
    g->lo[a] = g->hi[a] = 0;
  }
  g->dir[0] = b;
  return DS_OK;
}

ds_status_t ds_grid_file_init(ds_grid_file_t *g, size_t dim, size_t bucket,
                              size_t max_cells, ds_context_t *context) {
  return gf_init_any(g, dim, dim, bucket, max_cells, context);
}

void ds_grid_file_destroy(ds_grid_file_t *g) {
  size_t i;
  if (g == NULL) {
    return;
  }
  for (i = 0; i < g->buckets; i++) {
    if (g->alive[i]) {
      ds_context_free(g->context, g->items[i]);
      ds_context_free(g->context, g->values[i]);
    }
  }
  for (i = 0; i < GF_MAXD; i++) {
    ds_context_free(g->context, g->scale[i]);
  }
  ds_context_free(g->context, g->dir);
  ds_context_free(g->context, g->lo);
  ds_context_free(g->context, g->hi);
  ds_context_free(g->context, g->count);
  ds_context_free(g->context, g->capacity);
  ds_context_free(g->context, g->items);
  ds_context_free(g->context, g->values);
  ds_context_free(g->context, g->alive);
  ds_context_free(g->context, g->stamp);
  memset(g, 0, sizeof(*g));
  g->free_list = GF_NONE;
}

size_t ds_grid_file_size(const ds_grid_file_t *g) {
  return g == NULL ? 0 : g->size;
}

size_t ds_grid_file_buckets(const ds_grid_file_t *g) {
  return g == NULL ? 0 : g->live_buckets;
}

size_t ds_grid_file_cells(const ds_grid_file_t *g) {
  return g == NULL ? 0 : g->dir_size;
}

/* Insert split value x on axis a, adding a directory slice. DS_EXISTS when
 * x already is a split; DS_ERR_OVERFLOW past max_cells. */
static ds_status_t gf_refine(ds_grid_file_t *g, size_t a, double x) {
  size_t d = g->dim;
  size_t j = gf_cell(g, a, x);
  size_t n = g->scale_count[a];
  size_t ns;
  size_t *nd;
  size_t c[GF_MAXD];
  size_t oc[GF_MAXD];
  size_t i;
  size_t b;
  size_t k;
  if (j > 0 && !(g->scale[a][j - 1] < x)) {
    return DS_EXISTS;
  }
  if (g->dir_size / (n + 1) > g->max_cells / (n + 2)) {
    return DS_ERR_OVERFLOW;
  }
  ns = g->dir_size / (n + 1) * (n + 2);
  if (n == g->scale_capacity[a]) {
    size_t nc = n < 4 ? 8 : n * 2;
    void *p = ds_context_realloc(g->context, g->scale[a], nc * sizeof(double));
    if (p == NULL) {
      return DS_ERR_ALLOC;
    }
    g->scale[a] = (double *)p;
    g->scale_capacity[a] = nc;
  }
  nd = (size_t *)ds_context_alloc(g->context, ns * sizeof(size_t));
  if (nd == NULL) {
    return DS_ERR_ALLOC;
  }
  /* fill the new directory from the old one */
  for (k = 0; k < d; k++) {
    c[k] = 0;
  }
  for (i = 0; i < ns; i++) {
    size_t idx = 0;
    for (k = 0; k < d; k++) {
      oc[k] = c[k];
    }
    if (oc[a] > j) {
      oc[a]--;
    }
    for (k = 0; k < d; k++) {
      idx = idx * (g->scale_count[k] + 1) + oc[k];
    }
    nd[i] = g->dir[idx];
    for (k = d; k-- > 0;) {
      size_t lim = k == a ? n + 2 : g->scale_count[k] + 1;
      if (++c[k] < lim) {
        break;
      }
      c[k] = 0;
    }
  }
  memmove(g->scale[a] + j + 1, g->scale[a] + j, (n - j) * sizeof(double));
  g->scale[a][j] = x;
  g->scale_count[a] = n + 1;
  ds_context_free(g->context, g->dir);
  g->dir = nd;
  g->dir_size = ns;
  for (b = 0; b < g->buckets; b++) {
    if (!g->alive[b]) {
      continue;
    }
    if (g->lo[b * d + a] > j) {
      g->lo[b * d + a]++;
    }
    if (g->hi[b * d + a] >= j) {
      g->hi[b * d + a]++;
    }
  }
  return DS_OK;
}

/* Point every cell of [lo, hi] at bucket b. */
static void gf_assign(ds_grid_file_t *g, const size_t *lo, const size_t *hi,
                      size_t b) {
  size_t c[GF_MAXD];
  size_t a;
  size_t d = g->dim;
  memcpy(c, lo, d * sizeof(size_t));
  for (;;) {
    g->dir[gf_index(g, c)] = b;
    for (a = d; a-- > 0;) {
      if (c[a] < hi[a]) {
        c[a]++;
        break;
      }
      c[a] = lo[a];
    }
    if (a == (size_t)-1) {
      return;
    }
  }
}

/* Does item (a point, or a box inside bucket b's region) lie within the
 * cell range [lo, hi]? */
static bool gf_item_in(const ds_grid_file_t *g, const double *item,
                       const size_t *lo, const size_t *hi) {
  size_t a;
  size_t d = g->dim;
  for (a = 0; a < d; a++) {
    size_t c = gf_cell(g, a, item[a]);
    if (c < lo[a] || c > hi[a]) {
      return false;
    }
    if (g->width > d) {
      size_t ch = gf_cell(g, a, item[d + a]);
      if (ch < lo[a] || ch > hi[a]) {
        return false;
      }
    }
  }
  return true;
}

/* Split bucket b's cell range on axis a before cell mid; the upper part
 * goes to a new bucket. Boxes now cut by the boundary go to ev. */
static ds_status_t gf_split_range(ds_grid_file_t *g, size_t b, size_t a,
                                  size_t mid, gf_evict_t *ev, size_t *out) {
  size_t d = g->dim;
  size_t w = g->width;
  size_t nb;
  size_t i;
  size_t keep = 0;
  ds_status_t st = gf_new_bucket(g, &nb);
  if (st != DS_OK) {
    return st;
  }
  memcpy(g->lo + nb * d, g->lo + b * d, d * sizeof(size_t));
  memcpy(g->hi + nb * d, g->hi + b * d, d * sizeof(size_t));
  g->lo[nb * d + a] = mid;
  g->hi[b * d + a] = mid - 1;
  for (i = 0; i < g->count[b] && st == DS_OK; i++) {
    double *it = g->items[b] + i * w;
    size_t val = g->values[b][i];
    if (gf_item_in(g, it, g->lo + b * d, g->hi + b * d)) {
      if (keep != i) {
        memmove(g->items[b] + keep * w, it, w * sizeof(double));
        g->values[b][keep] = val;
      }
      keep++;
    } else if (gf_item_in(g, it, g->lo + nb * d, g->hi + nb * d)) {
      st = gf_append(g->context, &g->items[nb], &g->values[nb], &g->count[nb],
                     &g->capacity[nb], w, it, val);
    } else {
      st = ev == NULL ? DS_ERR_INTERNAL
                      : gf_append(g->context, &ev->items, &ev->values,
                                  &ev->count, &ev->capacity, w, it, val);
      if (st == DS_OK) {
        g->size--;
      }
    }
  }
  if (st != DS_OK) {
    /* keep everything consistent: undo the range change */
    size_t j;
    for (j = i; j < g->count[b]; j++) {
      if (keep != j) {
        memmove(g->items[b] + keep * w, g->items[b] + j * w,
                w * sizeof(double));
        g->values[b][keep] = g->values[b][j];
      }
      keep++;
    }
    g->count[b] = keep;
    for (j = 0; j < g->count[nb]; j++) {
      (void)gf_append(g->context, &g->items[b], &g->values[b], &g->count[b],
                      &g->capacity[b], w, g->items[nb] + j * w,
                      g->values[nb][j]);
    }
    g->hi[b * d + a] = g->hi[nb * d + a];
    gf_free_bucket(g, nb);
    return st;
  }
  g->count[b] = keep;
  gf_assign(g, g->lo + nb * d, g->hi + nb * d, nb);
  *out = nb;
  return DS_OK;
}

/* Restore bucket b to capacity by range splits and scale refinements. */
static ds_status_t gf_overflow(ds_grid_file_t *g, size_t b, gf_evict_t *ev) {
  size_t d = g->dim;
  double *scratch = NULL;
  ds_status_t st = DS_OK;
  while (g->count[b] > g->bucket) {
    size_t a;
    size_t best = d;
    size_t span = 0;
    size_t nb;
    for (a = 0; a < d; a++) {
      size_t s = g->hi[b * d + a] - g->lo[b * d + a];
      if (s > span) {
        span = s;
        best = a;
      }
    }
    if (best < d) {
      size_t mid = g->lo[b * d + best] + (span + 1) / 2;
      st = gf_split_range(g, b, best, mid, ev, &nb);
      if (st != DS_OK) {
        break;
      }
      if (g->count[nb] > g->count[b]) {
        b = nb;
      }
      continue;
    }
    /* a single cell: refine the scale with the widest spread of keys */
    {
      size_t n = g->count[b];
      size_t i;
      double bspread = 0.0;
      double x;
      void *p;
      best = d;
      for (a = 0; a < d; a++) {
        double lo = HUGE_VAL;
        double hi = -HUGE_VAL;
        for (i = 0; i < n; i++) {
          double c = g->items[b][i * g->width + a];
          lo = c < lo ? c : lo;
          hi = c > hi ? c : hi;
        }
        if (hi - lo > bspread) {
          bspread = hi - lo;
          best = a;
        }
      }
      if (best == d) {
        break; /* coinciding keys: an oversized bucket */
      }
      p = ds_context_realloc(g->context, scratch, n * sizeof(double));
      if (p == NULL) {
        st = DS_ERR_ALLOC;
        break;
      }
      scratch = (double *)p;
      for (i = 0; i < n; i++) {
        scratch[i] = g->items[b][i * g->width + best];
      }
      qsort(scratch, n, sizeof(double), gf_cmp_double);
      x = scratch[n / 2];
      if (!(x > scratch[0])) {
        for (i = 1; i < n && !(scratch[i] > scratch[0]); i++) {
        }
        x = scratch[i];
      }
      st = gf_refine(g, best, x);
      if (st == DS_ERR_OVERFLOW) {
        st = DS_OK; /* the directory is at its limit */
        break;
      }
      if (st != DS_OK) {
        break;
      }
    }
  }
  ds_context_free(g->context, scratch);
  return st;
}

static ds_status_t gf_insert_item(ds_grid_file_t *g, const double *item,
                                  size_t value, gf_evict_t *ev) {
  size_t b = gf_locate(g, item);
  ds_status_t st =
      gf_append(g->context, &g->items[b], &g->values[b], &g->count[b],
                &g->capacity[b], g->width, item, value);
  if (st != DS_OK) {
    return st;
  }
  g->size++;
  return gf_overflow(g, b, ev);
}

ds_status_t ds_grid_file_insert(ds_grid_file_t *g, const double *point,
                                size_t value) {
  if (g == NULL || point == NULL) {
    return DS_ERR_NULL;
  }
  if (g->width != g->dim) {
    return DS_ERR_STATE;
  }
  if (!gf_finite(point, g->dim)) {
    return DS_ERR_INVALID;
  }
  return gf_insert_item(g, point, value, NULL);
}

/* Merge bucket b with an adjacent buddy when both fit two thirds of a
 * bucket and their union is a box of cells. */
static void gf_try_merge(ds_grid_file_t *g, size_t b) {
  size_t d = g->dim;
  size_t a;
  size_t side;
  size_t limit = g->bucket * 2 / 3;
  for (a = 0; a < d; a++) {
    for (side = 0; side < 2; side++) {
      size_t c[GF_MAXD];
      size_t nb;
      size_t k;
      size_t i;
      bool same = true;
      memcpy(c, g->lo + b * d, d * sizeof(size_t));
      if (side == 0) {
        if (g->lo[b * d + a] == 0) {
          continue;
        }
        c[a] = g->lo[b * d + a] - 1;
      } else {
        if (g->hi[b * d + a] == g->scale_count[a]) {
          continue;
        }
        c[a] = g->hi[b * d + a] + 1;
      }
      nb = g->dir[gf_index(g, c)];
      if (nb == b || g->count[b] + g->count[nb] > limit) {
        continue;
      }
      for (k = 0; k < d && same; k++) {
        if (k != a && (g->lo[nb * d + k] != g->lo[b * d + k] ||
                       g->hi[nb * d + k] != g->hi[b * d + k])) {
          same = false;
        }
      }
      if (!same) {
        continue;
      }
      /* move nb's items into b first (it may fail: then nothing changes) */
      {
        size_t need = g->count[b] + g->count[nb];
        if (need > g->capacity[b]) {
          void *p = ds_context_realloc(g->context, g->items[b],
                                       need * g->width * sizeof(double));
          if (p == NULL) {
            return;
          }
          g->items[b] = (double *)p;
          p = ds_context_realloc(g->context, g->values[b],
                                 need * sizeof(size_t));
          if (p == NULL) {
            return;
          }
          g->values[b] = (size_t *)p;
          g->capacity[b] = need;
        }
      }
      for (i = 0; i < g->count[nb]; i++) {
        memcpy(g->items[b] + (g->count[b] + i) * g->width,
               g->items[nb] + i * g->width, g->width * sizeof(double));
        g->values[b][g->count[b] + i] = g->values[nb][i];
      }
      g->count[b] += g->count[nb];
      if (side == 0) {
        g->lo[b * d + a] = g->lo[nb * d + a];
      } else {
        g->hi[b * d + a] = g->hi[nb * d + a];
      }
      gf_assign(g, g->lo + nb * d, g->hi + nb * d, b);
      gf_free_bucket(g, nb);
      return;
    }
  }
}

static ds_status_t gf_remove_item(ds_grid_file_t *g, const double *item,
                                  size_t value) {
  size_t b = gf_locate(g, item);
  size_t i =
      gf_find(g->items[b], g->values[b], g->count[b], g->width, item, value);
  if (i == GF_NONE) {
    return DS_NOT_FOUND;
  }
  gf_drop(g->items[b], g->values[b], &g->count[b], g->width, i);
  g->size--;
  gf_try_merge(g, b);
  return DS_OK;
}

ds_status_t ds_grid_file_remove(ds_grid_file_t *g, const double *point,
                                size_t value) {
  if (g == NULL || point == NULL) {
    return DS_ERR_NULL;
  }
  if (!gf_finite(point, g->dim)) {
    return DS_NOT_FOUND;
  }
  return gf_remove_item(g, point, value);
}

ds_status_t ds_grid_file_find(const ds_grid_file_t *g, const double *point,
                              size_t *out_value) {
  size_t b;
  size_t i;
  if (g == NULL || point == NULL) {
    return DS_ERR_NULL;
  }
  if (!gf_finite(point, g->dim)) {
    return DS_NOT_FOUND;
  }
  b = gf_locate(g, point);
  for (i = 0; i < g->count[b]; i++) {
    if (memcmp(g->items[b] + i * g->width, point, g->dim * sizeof(double)) ==
        0) {
      if (out_value != NULL) {
        *out_value = g->values[b][i];
      }
      return DS_OK;
    }
  }
  return DS_NOT_FOUND;
}

ds_status_t ds_grid_file_region(const ds_grid_file_t *g, const double *point,
                                double *out_region) {
  if (g == NULL || point == NULL || out_region == NULL) {
    return DS_ERR_NULL;
  }
  if (!gf_finite(point, g->dim)) {
    return DS_ERR_INVALID;
  }
  gf_region(g, gf_locate(g, point), out_region);
  return DS_OK;
}

/* Visit every distinct bucket whose cells meet the closed box. */
static ds_status_t gf_query_items(ds_grid_file_t *g, const double *box,
                                  ds_grid_visit_func_t visit, void *user) {
  size_t d = g->dim;
  size_t w = g->width;
  size_t lo[GF_MAXD];
  size_t hi[GF_MAXD];
  size_t c[GF_MAXD];
  size_t a;
  for (a = 0; a < d; a++) {
    if (box[a] > box[d + a]) {
      return DS_OK;
    }
    lo[a] = gf_cell(g, a, box[a]);
    hi[a] = gf_cell(g, a, box[d + a]);
    c[a] = lo[a];
  }
  if (++g->epoch == 0) {
    size_t b;
    for (b = 0; b < g->buckets; b++) {
      g->stamp[b] = 0;
    }
    g->epoch = 1;
  }
  for (;;) {
    size_t b = g->dir[gf_index(g, c)];
    if (g->stamp[b] != g->epoch) {
      size_t i;
      g->stamp[b] = g->epoch;
      for (i = 0; i < g->count[b]; i++) {
        const double *it = g->items[b] + i * w;
        bool in = w == d ? gf_point_in_box(it, box, d) : gf_box_hit(it, box, d);
        if (in && !visit(it, g->values[b][i], user)) {
          return DS_STOP;
        }
      }
    }
    for (a = d; a-- > 0;) {
      if (c[a] < hi[a]) {
        c[a]++;
        break;
      }
      c[a] = lo[a];
    }
    if (a == (size_t)-1) {
      return DS_OK;
    }
  }
}

ds_status_t ds_grid_file_query(ds_grid_file_t *g, const double *box,
                               ds_grid_visit_func_t visit, void *user) {
  if (g == NULL || box == NULL || visit == NULL) {
    return DS_ERR_NULL;
  }
  return gf_query_items(g, box, visit, user);
}

ds_status_t ds_grid_file_nearest(const ds_grid_file_t *g, const double *point,
                                 size_t k, size_t *out_values, double *out_dist,
                                 size_t *out_found) {
  gf_cand_t *cand;
  double *dk;
  size_t *vk;
  size_t nc = 0;
  size_t found = 0;
  size_t i;
  size_t j;
  size_t d;
  if (g == NULL || point == NULL || out_found == NULL ||
      (k > 0 && (out_values == NULL || out_dist == NULL))) {
    return DS_ERR_NULL;
  }
  *out_found = 0;
  if (k == 0 || g->size == 0) {
    return DS_OK;
  }
  d = g->dim;
  cand =
      (gf_cand_t *)ds_context_alloc(g->context, g->buckets * sizeof(gf_cand_t));
  dk = (double *)ds_context_alloc(g->context, k * sizeof(double));
  vk = (size_t *)ds_context_alloc(g->context, k * sizeof(size_t));
  if (cand == NULL || dk == NULL || vk == NULL) {
    ds_context_free(g->context, cand);
    ds_context_free(g->context, dk);
    ds_context_free(g->context, vk);
    return DS_ERR_ALLOC;
  }
  for (i = 0; i < g->buckets; i++) {
    if (g->alive[i] && g->count[i] > 0) {
      double r[2 * GF_MAXD];
      gf_region(g, i, r);
      cand[nc].dist = gf_mindist2(r, point, d);
      cand[nc++].id = i;
    }
  }
  qsort(cand, nc, sizeof(gf_cand_t), gf_cmp_cand);
  for (i = 0; i < nc; i++) {
    size_t b = cand[i].id;
    if (found == k && !(cand[i].dist < dk[k - 1])) {
      break;
    }
    for (j = 0; j < g->count[b]; j++) {
      gf_best_push(dk, vk, k, &found,
                   gf_dist2(g->items[b] + j * g->width, point, d),
                   g->values[b][j]);
    }
  }
  for (i = 0; i < found; i++) {
    out_values[i] = vk[i];
    out_dist[i] = sqrt(dk[i]);
  }
  *out_found = found;
  ds_context_free(g->context, cand);
  ds_context_free(g->context, dk);
  ds_context_free(g->context, vk);
  return DS_OK;
}

bool ds_grid_file_valid(const ds_grid_file_t *g) {
  size_t d;
  size_t a;
  size_t i;
  size_t b;
  size_t total = 0;
  size_t cells = 1;
  size_t live = 0;
  size_t *cellcount;
  if (g == NULL || g->dir == NULL) {
    return false;
  }
  d = g->dim;
  for (a = 0; a < d; a++) {
    for (i = 1; i < g->scale_count[a]; i++) {
      if (!(g->scale[a][i - 1] < g->scale[a][i])) {
        return false;
      }
    }
    cells *= g->scale_count[a] + 1;
  }
  if (cells != g->dir_size) {
    return false;
  }
  /* every cell points at a live bucket whose range holds the cell */
  {
    size_t c[GF_MAXD];
    for (a = 0; a < d; a++) {
      c[a] = 0;
    }
    for (i = 0; i < g->dir_size; i++) {
      b = g->dir[i];
      if (b >= g->buckets || !g->alive[b]) {
        return false;
      }
      for (a = 0; a < d; a++) {
        if (c[a] < g->lo[b * d + a] || c[a] > g->hi[b * d + a]) {
          return false;
        }
      }
      for (a = d; a-- > 0;) {
        if (++c[a] <= g->scale_count[a]) {
          break;
        }
        c[a] = 0;
      }
    }
  }
  cellcount =
      (size_t *)ds_context_calloc(g->context, g->buckets, sizeof(size_t));
  if (cellcount == NULL) {
    return false;
  }
  for (i = 0; i < g->dir_size; i++) {
    cellcount[g->dir[i]]++;
  }
  for (b = 0; b < g->buckets; b++) {
    size_t vol = 1;
    if (!g->alive[b]) {
      continue;
    }
    live++;
    for (a = 0; a < d; a++) {
      if (g->lo[b * d + a] > g->hi[b * d + a] ||
          g->hi[b * d + a] > g->scale_count[a]) {
        ds_context_free(g->context, cellcount);
        return false;
      }
      vol *= g->hi[b * d + a] - g->lo[b * d + a] + 1;
    }
    /* the range's cells all map here */
    if (cellcount[b] != vol) {
      ds_context_free(g->context, cellcount);
      return false;
    }
    for (i = 0; i < g->count[b]; i++) {
      if (!gf_item_in(g, g->items[b] + i * g->width, g->lo + b * d,
                      g->hi + b * d)) {
        ds_context_free(g->context, cellcount);
        return false;
      }
    }
    total += g->count[b];
  }
  ds_context_free(g->context, cellcount);
  return total == g->size && live == g->live_buckets;
}

/* ---- twin grid file
 * ------------------------------------------------------------------ */

ds_status_t ds_twin_grid_file_init(ds_twin_grid_file_t *g, size_t dim,
                                   size_t bucket, ds_context_t *context) {
  ds_status_t st;
  if (g == NULL) {
    return DS_ERR_NULL;
  }
  st = ds_grid_file_init(&g->twin[0], dim, bucket, 0, context);
  if (st != DS_OK) {
    return st;
  }
  st = ds_grid_file_init(&g->twin[1], dim, bucket, 0, context);
  if (st != DS_OK) {
    ds_grid_file_destroy(&g->twin[0]);
  }
  return st;
}

void ds_twin_grid_file_destroy(ds_twin_grid_file_t *g) {
  if (g == NULL) {
    return;
  }
  ds_grid_file_destroy(&g->twin[0]);
  ds_grid_file_destroy(&g->twin[1]);
}

size_t ds_twin_grid_file_size(const ds_twin_grid_file_t *g) {
  return g == NULL ? 0 : g->twin[0].size + g->twin[1].size;
}

size_t ds_twin_grid_file_buckets(const ds_twin_grid_file_t *g) {
  return g == NULL ? 0 : g->twin[0].live_buckets + g->twin[1].live_buckets;
}

ds_status_t ds_twin_grid_file_insert(ds_twin_grid_file_t *g,
                                     const double *point, size_t value) {
  size_t b0;
  size_t b1;
  size_t pick;
  if (g == NULL || point == NULL) {
    return DS_ERR_NULL;
  }
  if (!gf_finite(point, g->twin[0].dim)) {
    return DS_ERR_INVALID;
  }
  b0 = gf_locate(&g->twin[0], point);
  b1 = gf_locate(&g->twin[1], point);
  if (g->twin[0].count[b0] < g->twin[0].bucket) {
    pick = 0;
  } else if (g->twin[1].count[b1] < g->twin[1].bucket) {
    pick = 1;
  } else {
    /* both full: the twin with fewer buckets grows */
    pick = g->twin[1].live_buckets < g->twin[0].live_buckets ? 1 : 0;
  }
  return gf_insert_item(&g->twin[pick], point, value, NULL);
}

ds_status_t ds_twin_grid_file_remove(ds_twin_grid_file_t *g,
                                     const double *point, size_t value) {
  ds_status_t st;
  if (g == NULL || point == NULL) {
    return DS_ERR_NULL;
  }
  st = ds_grid_file_remove(&g->twin[0], point, value);
  if (st == DS_NOT_FOUND) {
    st = ds_grid_file_remove(&g->twin[1], point, value);
  }
  return st;
}

ds_status_t ds_twin_grid_file_query(ds_twin_grid_file_t *g, const double *box,
                                    ds_grid_visit_func_t visit, void *user) {
  ds_status_t st;
  if (g == NULL || box == NULL || visit == NULL) {
    return DS_ERR_NULL;
  }
  st = gf_query_items(&g->twin[0], box, visit, user);
  if (st != DS_OK) {
    return st;
  }
  return gf_query_items(&g->twin[1], box, visit, user);
}

ds_status_t ds_twin_grid_file_nearest(const ds_twin_grid_file_t *g,
                                      const double *point, size_t k,
                                      size_t *out_values, double *out_dist,
                                      size_t *out_found) {
  size_t *v;
  double *dd;
  size_t f0 = 0;
  size_t f1 = 0;
  size_t i = 0;
  size_t j = 0;
  size_t n = 0;
  ds_status_t st;
  if (g == NULL || point == NULL || out_found == NULL ||
      (k > 0 && (out_values == NULL || out_dist == NULL))) {
    return DS_ERR_NULL;
  }
  *out_found = 0;
  if (k == 0) {
    return DS_OK;
  }
  v = (size_t *)ds_context_alloc(g->twin[0].context, 2 * k * sizeof(size_t));
  dd = (double *)ds_context_alloc(g->twin[0].context, 2 * k * sizeof(double));
  if (v == NULL || dd == NULL) {
    ds_context_free(g->twin[0].context, v);
    ds_context_free(g->twin[0].context, dd);
    return DS_ERR_ALLOC;
  }
  st = ds_grid_file_nearest(&g->twin[0], point, k, v, dd, &f0);
  if (st == DS_OK) {
    st = ds_grid_file_nearest(&g->twin[1], point, k, v + k, dd + k, &f1);
  }
  if (st == DS_OK) {
    while (n < k && (i < f0 || j < f1)) {
      if (j >= f1 || (i < f0 && dd[i] <= dd[k + j])) {
        out_values[n] = v[i];
        out_dist[n++] = dd[i++];
      } else {
        out_values[n] = v[k + j];
        out_dist[n++] = dd[k + j++];
      }
    }
    *out_found = n;
  }
  ds_context_free(g->twin[0].context, v);
  ds_context_free(g->twin[0].context, dd);
  return st;
}

/* ---- two-level grid file
 * ------------------------------------------------------------------ */

ds_status_t ds_grid_file2_init(ds_grid_file2_t *g, size_t dim, size_t bucket,
                               size_t page_cells, ds_context_t *context) {
  ds_status_t st;
  if (g == NULL) {
    return DS_ERR_NULL;
  }
  if (page_cells == 0) {
    page_cells = 256;
  }
  if (page_cells < 4) {
    return DS_ERR_INVALID;
  }
  memset(g, 0, sizeof(*g));
  g->page_cells = page_cells;
  g->bucket = bucket;
  g->context = context;
  st = gf_init_any(&g->top, dim, dim, 2, 0, context);
  if (st != DS_OK) {
    return st;
  }
  g->pages =
      (ds_grid_file_t *)ds_context_alloc(context, 8 * sizeof(ds_grid_file_t));
  if (g->pages == NULL) {
    ds_grid_file_destroy(&g->top);
    return DS_ERR_ALLOC;
  }
  g->page_capacity = 8;
  st = ds_grid_file_init(&g->pages[0], dim, bucket, 0, context);
  if (st != DS_OK) {
    ds_context_free(context, g->pages);
    ds_grid_file_destroy(&g->top);
  }
  return st;
}

void ds_grid_file2_destroy(ds_grid_file2_t *g) {
  size_t b;
  if (g == NULL) {
    return;
  }
  for (b = 0; b < g->top.buckets; b++) {
    if (g->top.alive[b]) {
      ds_grid_file_destroy(&g->pages[b]);
    }
  }
  ds_context_free(g->context, g->pages);
  ds_grid_file_destroy(&g->top);
  memset(g, 0, sizeof(*g));
}

size_t ds_grid_file2_size(const ds_grid_file2_t *g) {
  return g == NULL ? 0 : g->size;
}

size_t ds_grid_file2_pages(const ds_grid_file2_t *g) {
  return g == NULL ? 0 : g->top.live_buckets;
}

typedef struct gf2_ctx {
  ds_grid_file2_t *g;
  ds_grid_file_t *lo_page;
  ds_grid_file_t *hi_page;
  size_t axis;
  double x;
  ds_status_t st;
} gf2_ctx_t;

static bool gf2_move(const double *item, size_t value, void *user) {
  gf2_ctx_t *c = (gf2_ctx_t *)user;
  c->st = ds_grid_file_insert(item[c->axis] < c->x ? c->lo_page : c->hi_page,
                              item, value);
  return c->st == DS_OK;
}

/* Split the top-level region of page b; the page's points are shared out
 * between two fresh pages. */
static ds_status_t gf2_split_page(ds_grid_file2_t *g, size_t b,
                                  size_t *out_nb) {
  ds_grid_file_t *top = &g->top;
  size_t d = top->dim;
  size_t a;
  size_t best = d;
  size_t span = 0;
  size_t nb = 0;
  double all[2 * GF_MAXD];
  ds_grid_file_t lo_page;
  ds_grid_file_t hi_page;
  gf2_ctx_t c;
  ds_status_t st;
  for (a = 0; a < d; a++) {
    size_t s = top->hi[b * d + a] - top->lo[b * d + a];
    if (s > span) {
      span = s;
      best = a;
    }
  }
  if (best == d) {
    /* refine a top scale at the median of the page's points */
    ds_grid_file_t *pg = &g->pages[b];
    double *vals =
        (double *)ds_context_alloc(g->context, (pg->size + 1) * sizeof(double));
    double bspread = 0.0;
    size_t i;
    size_t j;
    size_t n;
    double x;
    if (vals == NULL) {
      return DS_ERR_ALLOC;
    }
    for (a = 0; a < d; a++) {
      double lo = HUGE_VAL;
      double hi = -HUGE_VAL;
      for (i = 0; i < pg->buckets; i++) {
        if (!pg->alive[i]) {
          continue;
        }
        for (j = 0; j < pg->count[i]; j++) {
          double v = pg->items[i][j * d + a];
          lo = v < lo ? v : lo;
          hi = v > hi ? v : hi;
        }
      }
      if (hi - lo > bspread) {
        bspread = hi - lo;
        best = a;
      }
    }
    if (best == d) {
      ds_context_free(g->context, vals);
      return DS_EXISTS; /* nothing separates the points */
    }
    n = 0;
    for (i = 0; i < pg->buckets; i++) {
      if (pg->alive[i]) {
        for (j = 0; j < pg->count[i]; j++) {
          vals[n++] = pg->items[i][j * d + best];
        }
      }
    }
    qsort(vals, n, sizeof(double), gf_cmp_double);
    x = vals[n / 2];
    if (!(x > vals[0])) {
      for (i = 1; i < n && !(vals[i] > vals[0]); i++) {
      }
      x = vals[i];
    }
    ds_context_free(g->context, vals);
    st = gf_refine(top, best, x);
    if (st != DS_OK) {
      return st;
    }
    span = 1;
  }
  /* the split value between the two halves of the range */
  {
    size_t mid = top->lo[b * d + best] + (span + 1) / 2;
    c.x = top->scale[best][mid - 1];
    c.axis = best;
    st = ds_grid_file_init(&lo_page, d, g->bucket, 0, g->context);
    if (st != DS_OK) {
      return st;
    }
    st = ds_grid_file_init(&hi_page, d, g->bucket, 0, g->context);
    if (st != DS_OK) {
      ds_grid_file_destroy(&lo_page);
      return st;
    }
    c.g = g;
    c.lo_page = &lo_page;
    c.hi_page = &hi_page;
    c.st = DS_OK;
    for (a = 0; a < d; a++) {
      all[a] = -HUGE_VAL;
      all[d + a] = HUGE_VAL;
    }
    st = gf_query_items(&g->pages[b], all, gf2_move, &c);
    if (st == DS_OK || st == DS_STOP) {
      st = c.st;
    }
    if (st == DS_OK) {
      st = gf_split_range(top, b, best, mid, NULL, &nb);
    }
    if (st == DS_OK && nb >= g->page_capacity) {
      size_t ncap = g->page_capacity * 2;
      void *p;
      while (ncap <= nb) {
        ncap *= 2;
      }
      p = ds_context_realloc(g->context, g->pages,
                             ncap * sizeof(ds_grid_file_t));
      if (p == NULL) {
        st = DS_ERR_ALLOC;
      } else {
        g->pages = (ds_grid_file_t *)p;
        g->page_capacity = ncap;
      }
    }
    if (st != DS_OK) {
      ds_grid_file_destroy(&lo_page);
      ds_grid_file_destroy(&hi_page);
      return st;
    }
  }
  ds_grid_file_destroy(&g->pages[b]);
  g->pages[b] = lo_page;
  g->pages[nb] = hi_page;
  *out_nb = nb;
  return DS_OK;
}

ds_status_t ds_grid_file2_insert(ds_grid_file2_t *g, const double *point,
                                 size_t value) {
  size_t b;
  ds_status_t st;
  if (g == NULL || point == NULL) {
    return DS_ERR_NULL;
  }
  if (!gf_finite(point, g->top.dim)) {
    return DS_ERR_INVALID;
  }
  b = gf_locate(&g->top, point);
  st = ds_grid_file_insert(&g->pages[b], point, value);
  if (st != DS_OK) {
    return st;
  }
  g->size++;
  while (g->pages[b].dir_size > g->page_cells) {
    size_t nb;
    st = gf2_split_page(g, b, &nb);
    if (st == DS_EXISTS || st == DS_ERR_OVERFLOW) {
      return DS_OK; /* an oversized page */
    }
    if (st != DS_OK) {
      return st;
    }
    if (g->pages[nb].dir_size > g->pages[b].dir_size) {
      b = nb;
    }
  }
  return DS_OK;
}

ds_status_t ds_grid_file2_remove(ds_grid_file2_t *g, const double *point,
                                 size_t value) {
  ds_status_t st;
  if (g == NULL || point == NULL) {
    return DS_ERR_NULL;
  }
  if (!gf_finite(point, g->top.dim)) {
    return DS_NOT_FOUND;
  }
  st = ds_grid_file_remove(&g->pages[gf_locate(&g->top, point)], point, value);
  if (st == DS_OK) {
    g->size--;
  }
  return st;
}

ds_status_t ds_grid_file2_query(ds_grid_file2_t *g, const double *box,
                                ds_grid_visit_func_t visit, void *user) {
  ds_grid_file_t *top;
  size_t d;
  size_t lo[GF_MAXD];
  size_t hi[GF_MAXD];
  size_t c[GF_MAXD];
  size_t a;
  if (g == NULL || box == NULL || visit == NULL) {
    return DS_ERR_NULL;
  }
  top = &g->top;
  d = top->dim;
  for (a = 0; a < d; a++) {
    if (box[a] > box[d + a]) {
      return DS_OK;
    }
    lo[a] = gf_cell(top, a, box[a]);
    hi[a] = gf_cell(top, a, box[d + a]);
    c[a] = lo[a];
  }
  if (++top->epoch == 0) {
    size_t b;
    for (b = 0; b < top->buckets; b++) {
      top->stamp[b] = 0;
    }
    top->epoch = 1;
  }
  for (;;) {
    size_t b = top->dir[gf_index(top, c)];
    if (top->stamp[b] != top->epoch) {
      ds_status_t st;
      top->stamp[b] = top->epoch;
      st = gf_query_items(&g->pages[b], box, visit, user);
      if (st != DS_OK) {
        return st;
      }
    }
    for (a = d; a-- > 0;) {
      if (c[a] < hi[a]) {
        c[a]++;
        break;
      }
      c[a] = lo[a];
    }
    if (a == (size_t)-1) {
      return DS_OK;
    }
  }
}

bool ds_grid_file2_valid(const ds_grid_file2_t *g) {
  size_t b;
  size_t total = 0;
  size_t d;
  if (g == NULL || !ds_grid_file_valid(&g->top)) {
    return false;
  }
  d = g->top.dim;
  for (b = 0; b < g->top.buckets; b++) {
    const ds_grid_file_t *pg;
    size_t i;
    size_t j;
    double r[2 * GF_MAXD];
    if (!g->top.alive[b]) {
      continue;
    }
    pg = &g->pages[b];
    if (!ds_grid_file_valid(pg)) {
      return false;
    }
    gf_region(&g->top, b, r);
    for (i = 0; i < pg->buckets; i++) {
      if (!pg->alive[i]) {
        continue;
      }
      for (j = 0; j < pg->count[i]; j++) {
        const double *p = pg->items[i] + j * d;
        size_t a;
        for (a = 0; a < d; a++) {
          if (p[a] < r[a] || !(p[a] < r[d + a])) {
            return false;
          }
        }
      }
    }
    total += pg->size;
  }
  return total == g->size;
}

/* ---- multilayer grid file
 * ---------------------------------------------------------------- */

ds_status_t ds_mlgrid_file_init(ds_mlgrid_file_t *g, size_t dim, size_t bucket,
                                size_t max_layers, ds_context_t *context) {
  if (g == NULL) {
    return DS_ERR_NULL;
  }
  if (bucket == 0) {
    bucket = 16;
  }
  if (max_layers == 0) {
    max_layers = 8;
  }
  if (dim == 0 || dim > GF_MAXD || bucket < 2 || max_layers > 64) {
    return DS_ERR_INVALID;
  }
  memset(g, 0, sizeof(*g));
  g->dim = dim;
  g->bucket = bucket;
  g->max_layers = max_layers;
  g->context = context;
  g->layer = (ds_grid_file_t *)ds_context_alloc(
      context, max_layers * sizeof(ds_grid_file_t));
  return g->layer == NULL ? DS_ERR_ALLOC : DS_OK;
}

void ds_mlgrid_file_destroy(ds_mlgrid_file_t *g) {
  size_t i;
  if (g == NULL) {
    return;
  }
  for (i = 0; i < g->layers; i++) {
    ds_grid_file_destroy(&g->layer[i]);
  }
  ds_context_free(g->context, g->layer);
  ds_context_free(g->context, g->overflow);
  ds_context_free(g->context, g->overflow_values);
  memset(g, 0, sizeof(*g));
}

size_t ds_mlgrid_file_size(const ds_mlgrid_file_t *g) {
  return g == NULL ? 0 : g->size;
}

size_t ds_mlgrid_file_layers(const ds_mlgrid_file_t *g) {
  return g == NULL ? 0 : g->layers;
}

/* Does the box lie within the region of the bucket holding its low
 * corner? */
static bool ml_fits(const ds_grid_file_t *l, const double *box) {
  size_t b = gf_locate(l, box);
  return gf_item_in(l, box, l->lo + b * l->dim, l->hi + b * l->dim);
}

/* Place one box at layer `from` or below. Boxes cut by splits are queued
 * in ev with the layer below in mind. */
static ds_status_t ml_place(ds_mlgrid_file_t *g, const double *box,
                            size_t value, size_t from, gf_evict_t *ev) {
  size_t L;
  size_t w = 2 * g->dim;
  for (L = from; L < g->max_layers; L++) {
    if (L == g->layers) {
      ds_status_t st =
          gf_init_any(&g->layer[L], g->dim, w, g->bucket, 0, g->context);
      if (st != DS_OK) {
        return st;
      }
      g->layers++;
    }
    if (ml_fits(&g->layer[L], box)) {
      return gf_insert_item(&g->layer[L], box, value, ev);
    }
  }
  return gf_append(g->context, &g->overflow, &g->overflow_values,
                   &g->overflow_count, &g->overflow_capacity, w, box, value);
}

ds_status_t ds_mlgrid_file_insert(ds_mlgrid_file_t *g, const double *box,
                                  size_t value) {
  size_t d;
  size_t a;
  size_t w;
  ds_status_t st;
  gf_evict_t ev;
  if (g == NULL || box == NULL) {
    return DS_ERR_NULL;
  }
  d = g->dim;
  w = 2 * d;
  if (!gf_finite(box, w)) {
    return DS_ERR_INVALID;
  }
  for (a = 0; a < d; a++) {
    if (box[a] > box[d + a]) {
      return DS_ERR_INVALID;
    }
  }
  memset(&ev, 0, sizeof(ev));
  st = ml_place(g, box, value, 0, &ev);
  /* cascade: boxes cut by a split find the first layer they fit in */
  while (st == DS_OK && ev.count > 0) {
    gf_evict_t next;
    size_t i;
    memset(&next, 0, sizeof(next));
    for (i = 0; i < ev.count && st == DS_OK; i++) {
      st = ml_place(g, ev.items + i * w, ev.values[i], 0, &next);
    }
    ds_context_free(g->context, ev.items);
    ds_context_free(g->context, ev.values);
    ev = next;
  }
  ds_context_free(g->context, ev.items);
  ds_context_free(g->context, ev.values);
  if (st == DS_OK) {
    g->size++;
  }
  return st;
}

ds_status_t ds_mlgrid_file_remove(ds_mlgrid_file_t *g, const double *box,
                                  size_t value) {
  size_t L;
  size_t i;
  size_t w;
  if (g == NULL || box == NULL) {
    return DS_ERR_NULL;
  }
  w = 2 * g->dim;
  if (!gf_finite(box, w)) {
    return DS_NOT_FOUND;
  }
  for (L = 0; L < g->layers; L++) {
    if (gf_remove_item(&g->layer[L], box, value) == DS_OK) {
      g->size--;
      return DS_OK;
    }
  }
  i = gf_find(g->overflow, g->overflow_values, g->overflow_count, w, box,
              value);
  if (i == GF_NONE) {
    return DS_NOT_FOUND;
  }
  gf_drop(g->overflow, g->overflow_values, &g->overflow_count, w, i);
  g->size--;
  return DS_OK;
}

ds_status_t ds_mlgrid_file_query(ds_mlgrid_file_t *g, const double *box,
                                 ds_grid_visit_func_t visit, void *user) {
  size_t L;
  size_t i;
  size_t w;
  if (g == NULL || box == NULL || visit == NULL) {
    return DS_ERR_NULL;
  }
  w = 2 * g->dim;
  for (L = 0; L < g->layers; L++) {
    ds_status_t st = gf_query_items(&g->layer[L], box, visit, user);
    if (st != DS_OK) {
      return st;
    }
  }
  for (i = 0; i < g->overflow_count; i++) {
    const double *b = g->overflow + i * w;
    if (gf_box_hit(b, box, g->dim) && !visit(b, g->overflow_values[i], user)) {
      return DS_STOP;
    }
  }
  return DS_OK;
}

bool ds_mlgrid_file_valid(const ds_mlgrid_file_t *g) {
  size_t L;
  size_t total;
  if (g == NULL) {
    return false;
  }
  total = g->overflow_count;
  for (L = 0; L < g->layers; L++) {
    if (!ds_grid_file_valid(&g->layer[L])) {
      return false;
    }
    total += g->layer[L].size;
  }
  return total == g->size;
}
