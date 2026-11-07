#include "bang_file.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

#define BF_NONE (~(size_t)0)
#define BF_MAXD DS_BANG_MAX_DIM
#define BF_B DS_BANG_BITS
#define BF_SCALE 1073741824.0 /* 2^30 */

/* ---- helpers
 * ------------------------------------------------------------------- */

static bool bf_finite(const double *x, size_t n) {
  size_t i;
  for (i = 0; i < n; i++) {
    if (!(x[i] > -HUGE_VAL && x[i] < HUGE_VAL)) {
      return false;
    }
  }
  return true;
}

static ds_status_t bf_check_domain(size_t dim, const double *domain) {
  size_t a;
  if (dim == 0 || dim > BF_MAXD || !bf_finite(domain, 2 * dim)) {
    return DS_ERR_INVALID;
  }
  for (a = 0; a < dim; a++) {
    if (!(domain[a] < domain[dim + a])) {
      return DS_ERR_INVALID;
    }
  }
  return DS_OK;
}

static void bf_quantise(const double *domain, size_t d, const double *p,
                        unsigned long *q) {
  size_t a;
  for (a = 0; a < d; a++) {
    double f = (p[a] - domain[a]) / (domain[d + a] - domain[a]) * BF_SCALE;
    if (!(f > 0.0)) {
      q[a] = 0;
    } else if (!(f < BF_SCALE)) {
      q[a] = (1UL << BF_B) - 1;
    } else {
      q[a] = (unsigned long)f;
      if (q[a] > (1UL << BF_B) - 1) {
        q[a] = (1UL << BF_B) - 1;
      }
    }
  }
}

/* bit i of the interleaved key (axis i mod d, most significant first) */
static unsigned bf_bit(const unsigned long *q, size_t d, size_t i) {
  return (unsigned)((q[i % d] >> (BF_B - 1 - i / d)) & 1UL);
}

/* The real box of the cell (qlo, depth), widened by one quantum and
 * unbounded at the domain border. */
static void bf_cell_box(const double *domain, size_t d,
                        const unsigned long *qlo, size_t depth, double *box) {
  size_t a;
  for (a = 0; a < d; a++) {
    size_t n = depth / d + (a < depth % d ? 1 : 0);
    double span = domain[d + a] - domain[a];
    double quantum = span / BF_SCALE;
    double lo = (double)qlo[a];
    double hi = lo + ldexp(1.0, (int)(BF_B - n));
    box[a] =
        qlo[a] == 0 ? -HUGE_VAL : domain[a] + span * lo / BF_SCALE - quantum;
    box[d + a] = !(hi < BF_SCALE) ? HUGE_VAL
                                  : domain[a] + span * hi / BF_SCALE + quantum;
  }
}

static bool bf_point_in_box(const double *p, const double *b, size_t d) {
  size_t a;
  for (a = 0; a < d; a++) {
    if (p[a] < b[a] || p[a] > b[d + a]) {
      return false;
    }
  }
  return true;
}

static bool bf_box_hit(const double *x, const double *y, size_t d) {
  size_t a;
  for (a = 0; a < d; a++) {
    if (x[d + a] < y[a] || y[d + a] < x[a]) {
      return false;
    }
  }
  return true;
}

static double bf_dist2(const double *x, const double *y, size_t d) {
  size_t a;
  double s = 0.0;
  for (a = 0; a < d; a++) {
    s += (x[a] - y[a]) * (x[a] - y[a]);
  }
  return s;
}

static double bf_mindist2(const double *b, const double *p, size_t d) {
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

static ds_status_t bf_append(ds_context_t *ctx, double **items, size_t **vals,
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

static size_t bf_find(const double *items, const size_t *vals, size_t count,
                      size_t width, const double *item, size_t value) {
  size_t i;
  for (i = 0; i < count; i++) {
    if (vals[i] == value &&
        memcmp(items + i * width, item, width * sizeof(double)) == 0) {
      return i;
    }
  }
  return BF_NONE;
}

static void bf_drop(double *items, size_t *vals, size_t *count, size_t width,
                    size_t i) {
  size_t last = --*count;
  if (i != last) {
    memcpy(items + i * width, items + last * width, width * sizeof(double));
    vals[i] = vals[last];
  }
}

static void bf_best_push(double *dk, size_t *vk, size_t k, size_t *found,
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

typedef struct bf_cand {
  double dist;
  size_t id;
} bf_cand_t;

static int bf_cmp_cand(const void *a, const void *b) {
  double x = ((const bf_cand_t *)a)->dist;
  double y = ((const bf_cand_t *)b)->dist;
  return (x > y) - (x < y);
}

/* k nearest points over a set of buckets with known cell boxes */
static ds_status_t bf_knn(ds_context_t *ctx, size_t d, size_t nb,
                          const double *boxes, double *const *items,
                          size_t *const *values, const size_t *count,
                          const double *point, size_t k, size_t *out_values,
                          double *out_dist, size_t *out_found) {
  bf_cand_t *cand;
  double *dk;
  size_t *vk;
  size_t found = 0;
  size_t i;
  size_t j;
  cand = (bf_cand_t *)ds_context_alloc(ctx, (nb + 1) * sizeof(bf_cand_t));
  dk = (double *)ds_context_alloc(ctx, k * sizeof(double));
  vk = (size_t *)ds_context_alloc(ctx, k * sizeof(size_t));
  if (cand == NULL || dk == NULL || vk == NULL) {
    ds_context_free(ctx, cand);
    ds_context_free(ctx, dk);
    ds_context_free(ctx, vk);
    return DS_ERR_ALLOC;
  }
  for (i = 0; i < nb; i++) {
    cand[i].dist = bf_mindist2(boxes + i * 2 * d, point, d);
    cand[i].id = i;
  }
  qsort(cand, nb, sizeof(bf_cand_t), bf_cmp_cand);
  for (i = 0; i < nb; i++) {
    size_t b = cand[i].id;
    if (found == k && !(cand[i].dist < dk[k - 1])) {
      break;
    }
    for (j = 0; j < count[b]; j++) {
      bf_best_push(dk, vk, k, &found, bf_dist2(items[b] + j * d, point, d),
                   values[b][j]);
    }
  }
  for (i = 0; i < found; i++) {
    out_values[i] = vk[i];
    out_dist[i] = sqrt(dk[i]);
  }
  *out_found = found;
  ds_context_free(ctx, cand);
  ds_context_free(ctx, dk);
  ds_context_free(ctx, vk);
  return DS_OK;
}

/* ---- EXCELL
 * ------------------------------------------------------------------------ */

static size_t ex_prefix(const unsigned long *q, size_t d, size_t n) {
  size_t i;
  size_t p = 0;
  for (i = 0; i < n; i++) {
    p = (p << 1) | bf_bit(q, d, i);
  }
  return p;
}

static void ex_cell_box(const ds_excell_t *e, size_t b, double *box) {
  unsigned long qlo[BF_MAXD];
  size_t d = e->dim;
  size_t l = e->local[b];
  size_t i;
  for (i = 0; i < d; i++) {
    qlo[i] = 0;
  }
  for (i = 0; i < l; i++) {
    if ((e->prefix[b] >> (l - 1 - i)) & 1U) {
      qlo[i % d] |= 1UL << (BF_B - 1 - i / d);
    }
  }
  bf_cell_box(e->domain, d, qlo, l, box);
}

static ds_status_t ex_new_bucket(ds_excell_t *e, size_t *out) {
  size_t b;
  if (e->free_list != BF_NONE) {
    b = e->free_list;
    e->free_list = e->local[b];
  } else {
    if (e->buckets == e->bucket_capacity) {
      size_t nc = e->bucket_capacity < 8 ? 8 : e->bucket_capacity * 2;
      void *p;
#define EX_RE(field, type)                                                     \
  p = ds_context_realloc(e->context, e->field, nc * sizeof(type));             \
  if (p == NULL) {                                                             \
    return DS_ERR_ALLOC;                                                       \
  }                                                                            \
  e->field = (type *)p;
      EX_RE(local, size_t)
      EX_RE(prefix, size_t)
      EX_RE(count, size_t)
      EX_RE(capacity, size_t)
      EX_RE(points, double *)
      EX_RE(values, size_t *)
      EX_RE(alive, unsigned char)
#undef EX_RE
      e->bucket_capacity = nc;
    }
    b = e->buckets++;
  }
  e->local[b] = 0;
  e->prefix[b] = 0;
  e->count[b] = 0;
  e->capacity[b] = 0;
  e->points[b] = NULL;
  e->values[b] = NULL;
  e->alive[b] = 1;
  e->live_buckets++;
  *out = b;
  return DS_OK;
}

static void ex_free_bucket(ds_excell_t *e, size_t b) {
  ds_context_free(e->context, e->points[b]);
  ds_context_free(e->context, e->values[b]);
  e->points[b] = NULL;
  e->values[b] = NULL;
  e->count[b] = 0;
  e->capacity[b] = 0;
  e->alive[b] = 0;
  e->local[b] = e->free_list;
  e->free_list = b;
  e->live_buckets--;
}

ds_status_t ds_excell_init(ds_excell_t *e, size_t dim, const double *domain,
                           size_t bucket, size_t max_depth,
                           ds_context_t *context) {
  size_t b;
  ds_status_t st;
  if (e == NULL || domain == NULL) {
    return DS_ERR_NULL;
  }
  if (bucket == 0) {
    bucket = 16;
  }
  if (max_depth == 0) {
    max_depth = 20;
  }
  st = bf_check_domain(dim, domain);
  if (st != DS_OK) {
    return st;
  }
  if (bucket < 2 || max_depth > 28 || max_depth > BF_B * dim) {
    return DS_ERR_INVALID;
  }
  memset(e, 0, sizeof(*e));
  e->dim = dim;
  e->bucket = bucket;
  e->max_depth = max_depth;
  memcpy(e->domain, domain, 2 * dim * sizeof(double));
  e->free_list = BF_NONE;
  e->context = context;
  e->dir = (size_t *)ds_context_alloc(context, sizeof(size_t));
  if (e->dir == NULL) {
    return DS_ERR_ALLOC;
  }
  st = ex_new_bucket(e, &b);
  if (st != DS_OK) {
    ds_excell_destroy(e);
    return st;
  }
  e->dir[0] = b;
  return DS_OK;
}

void ds_excell_destroy(ds_excell_t *e) {
  size_t b;
  if (e == NULL) {
    return;
  }
  for (b = 0; b < e->buckets; b++) {
    if (e->alive[b]) {
      ds_context_free(e->context, e->points[b]);
      ds_context_free(e->context, e->values[b]);
    }
  }
  ds_context_free(e->context, e->dir);
  ds_context_free(e->context, e->local);
  ds_context_free(e->context, e->prefix);
  ds_context_free(e->context, e->count);
  ds_context_free(e->context, e->capacity);
  ds_context_free(e->context, e->points);
  ds_context_free(e->context, e->values);
  ds_context_free(e->context, e->alive);
  memset(e, 0, sizeof(*e));
}

size_t ds_excell_size(const ds_excell_t *e) { return e == NULL ? 0 : e->size; }

size_t ds_excell_depth(const ds_excell_t *e) {
  return e == NULL ? 0 : e->depth;
}

size_t ds_excell_buckets(const ds_excell_t *e) {
  return e == NULL ? 0 : e->live_buckets;
}

static size_t ex_locate(const ds_excell_t *e, const double *p) {
  unsigned long q[BF_MAXD];
  bf_quantise(e->domain, e->dim, p, q);
  return e->dir[ex_prefix(q, e->dim, e->depth)];
}

/* Halve bucket b's cell: the upper half goes to a new bucket. */
static ds_status_t ex_split(ds_excell_t *e, size_t b, size_t *out) {
  size_t d = e->dim;
  size_t l = e->local[b];
  size_t nb;
  size_t i;
  size_t keep = 0;
  size_t start;
  size_t n;
  ds_status_t st = ex_new_bucket(e, &nb);
  if (st != DS_OK) {
    return st;
  }
  e->local[nb] = l + 1;
  e->prefix[nb] = (e->prefix[b] << 1) | 1U;
  for (i = 0; i < e->count[b]; i++) {
    unsigned long q[BF_MAXD];
    double *p = e->points[b] + i * d;
    size_t v = e->values[b][i];
    bf_quantise(e->domain, d, p, q);
    if (bf_bit(q, d, l)) {
      st = bf_append(e->context, &e->points[nb], &e->values[nb], &e->count[nb],
                     &e->capacity[nb], d, p, v);
      if (st != DS_OK) {
        /* give the moved points back */
        size_t j;
        for (j = 0; j < e->count[nb]; j++) {
          memcpy(e->points[b] + keep * d, e->points[nb] + j * d,
                 d * sizeof(double));
          e->values[b][keep++] = e->values[nb][j];
        }
        for (j = i; j < e->count[b]; j++) {
          memmove(e->points[b] + keep * d, e->points[b] + j * d,
                  d * sizeof(double));
          e->values[b][keep++] = e->values[b][j];
        }
        e->count[b] = keep;
        ex_free_bucket(e, nb);
        return st;
      }
    } else {
      if (keep != i) {
        memmove(e->points[b] + keep * d, p, d * sizeof(double));
        e->values[b][keep] = v;
      }
      keep++;
    }
  }
  e->count[b] = keep;
  e->local[b] = l + 1;
  e->prefix[b] <<= 1;
  start = e->prefix[nb] << (e->depth - l - 1);
  n = (size_t)1 << (e->depth - l - 1);
  for (i = 0; i < n; i++) {
    e->dir[start + i] = nb;
  }
  *out = nb;
  return DS_OK;
}

ds_status_t ds_excell_insert(ds_excell_t *e, const double *point,
                             size_t value) {
  size_t b;
  ds_status_t st;
  if (e == NULL || point == NULL) {
    return DS_ERR_NULL;
  }
  if (!bf_finite(point, e->dim)) {
    return DS_ERR_INVALID;
  }
  b = ex_locate(e, point);
  st = bf_append(e->context, &e->points[b], &e->values[b], &e->count[b],
                 &e->capacity[b], e->dim, point, value);
  if (st != DS_OK) {
    return st;
  }
  e->size++;
  while (e->count[b] > e->bucket && e->local[b] < e->max_depth) {
    size_t nb;
    if (e->local[b] == e->depth) {
      size_t n = (size_t)1 << e->depth;
      size_t i;
      size_t *nd =
          (size_t *)ds_context_alloc(e->context, 2 * n * sizeof(size_t));
      if (nd == NULL) {
        return DS_ERR_ALLOC;
      }
      for (i = 0; i < 2 * n; i++) {
        nd[i] = e->dir[i >> 1];
      }
      ds_context_free(e->context, e->dir);
      e->dir = nd;
      e->depth++;
    }
    st = ex_split(e, b, &nb);
    if (st != DS_OK) {
      return st;
    }
    if (e->count[nb] > e->count[b]) {
      b = nb;
    }
  }
  return DS_OK;
}

ds_status_t ds_excell_remove(ds_excell_t *e, const double *point,
                             size_t value) {
  size_t b;
  size_t i;
  size_t l;
  size_t d;
  if (e == NULL || point == NULL) {
    return DS_ERR_NULL;
  }
  d = e->dim;
  if (!bf_finite(point, d)) {
    return DS_NOT_FOUND;
  }
  b = ex_locate(e, point);
  i = bf_find(e->points[b], e->values[b], e->count[b], d, point, value);
  if (i == BF_NONE) {
    return DS_NOT_FOUND;
  }
  bf_drop(e->points[b], e->values[b], &e->count[b], d, i);
  e->size--;
  /* merge with the buddy, as long as merging goes on */
  for (l = e->local[b]; l > 0; l = e->local[b]) {
    size_t bp = e->prefix[b] ^ 1U;
    size_t buddy = e->dir[bp << (e->depth - l)];
    if (buddy != b && e->local[buddy] == l &&
        e->count[b] + e->count[buddy] <= e->bucket * 2 / 3) {
      size_t need = e->count[b] + e->count[buddy];
      size_t start;
      size_t n;
      bool room = true;
      if (need > e->capacity[b]) {
        void *p = ds_context_realloc(e->context, e->points[b],
                                     need * d * sizeof(double));
        if (p == NULL) {
          room = false;
        } else {
          e->points[b] = (double *)p;
          p = ds_context_realloc(e->context, e->values[b],
                                 need * sizeof(size_t));
          if (p == NULL) {
            room = false;
          } else {
            e->values[b] = (size_t *)p;
            e->capacity[b] = need;
          }
        }
      }
      if (room) {
        if (e->count[buddy] > 0) {
          memcpy(e->points[b] + e->count[b] * d, e->points[buddy],
                 e->count[buddy] * d * sizeof(double));
          memcpy(e->values[b] + e->count[b], e->values[buddy],
                 e->count[buddy] * sizeof(size_t));
        }
        e->count[b] = need;
        e->local[b] = l - 1;
        e->prefix[b] >>= 1;
        start = e->prefix[b] << (e->depth - l + 1);
        n = (size_t)1 << (e->depth - l + 1);
        for (i = 0; i < n; i++) {
          e->dir[start + i] = b;
        }
        ex_free_bucket(e, buddy);
        continue;
      }
    }
    break;
  }
  /* halve the directory while no bucket needs its full depth */
  while (e->depth > 0) {
    size_t n = (size_t)1 << e->depth;
    size_t k;
    for (k = 0; k < n; k += 2) {
      if (e->dir[k] != e->dir[k + 1]) {
        break;
      }
    }
    if (k < n) {
      break;
    }
    for (k = 0; k < n / 2; k++) {
      e->dir[k] = e->dir[2 * k];
    }
    e->depth--;
  }
  return DS_OK;
}

ds_status_t ds_excell_query(const ds_excell_t *e, const double *box,
                            ds_bang_visit_func_t visit, void *user) {
  size_t b;
  size_t i;
  size_t d;
  if (e == NULL || box == NULL || visit == NULL) {
    return DS_ERR_NULL;
  }
  d = e->dim;
  for (b = 0; b < e->buckets; b++) {
    double cell[2 * BF_MAXD];
    if (!e->alive[b] || e->count[b] == 0) {
      continue;
    }
    ex_cell_box(e, b, cell);
    if (!bf_box_hit(cell, box, d)) {
      continue;
    }
    for (i = 0; i < e->count[b]; i++) {
      const double *p = e->points[b] + i * d;
      if (bf_point_in_box(p, box, d) && !visit(p, e->values[b][i], user)) {
        return DS_STOP;
      }
    }
  }
  return DS_OK;
}

ds_status_t ds_excell_nearest(const ds_excell_t *e, const double *point,
                              size_t k, size_t *out_values, double *out_dist,
                              size_t *out_found) {
  double *boxes;
  size_t b;
  ds_status_t st;
  if (e == NULL || point == NULL || out_found == NULL ||
      (k > 0 && (out_values == NULL || out_dist == NULL))) {
    return DS_ERR_NULL;
  }
  *out_found = 0;
  if (k == 0 || e->size == 0) {
    return DS_OK;
  }
  boxes = (double *)ds_context_alloc(e->context,
                                     e->buckets * 2 * e->dim * sizeof(double));
  if (boxes == NULL) {
    return DS_ERR_ALLOC;
  }
  for (b = 0; b < e->buckets; b++) {
    if (e->alive[b]) {
      ex_cell_box(e, b, boxes + b * 2 * e->dim);
    } else {
      memset(boxes + b * 2 * e->dim, 0, 2 * e->dim * sizeof(double));
    }
  }
  st = bf_knn(e->context, e->dim, e->buckets, boxes, e->points, e->values,
              e->count, point, k, out_values, out_dist, out_found);
  ds_context_free(e->context, boxes);
  return st;
}

bool ds_excell_valid(const ds_excell_t *e) {
  size_t n;
  size_t i;
  size_t b;
  size_t total = 0;
  size_t d;
  size_t *cover;
  bool ok = true;
  if (e == NULL || e->dir == NULL) {
    return false;
  }
  d = e->dim;
  n = (size_t)1 << e->depth;
  cover =
      (size_t *)ds_context_calloc(e->context, e->buckets + 1, sizeof(size_t));
  if (cover == NULL) {
    return false;
  }
  for (i = 0; i < n && ok; i++) {
    b = e->dir[i];
    ok = b < e->buckets && e->alive[b] && e->local[b] <= e->depth &&
         (i >> (e->depth - e->local[b])) == e->prefix[b];
    if (ok) {
      cover[b]++;
    }
  }
  for (b = 0; b < e->buckets && ok; b++) {
    if (!e->alive[b]) {
      continue;
    }
    ok = cover[b] == ((size_t)1 << (e->depth - e->local[b]));
    for (i = 0; i < e->count[b] && ok; i++) {
      unsigned long q[BF_MAXD];
      bf_quantise(e->domain, d, e->points[b] + i * d, q);
      ok = ex_prefix(q, d, e->local[b]) == e->prefix[b];
    }
    total += e->count[b];
  }
  ds_context_free(e->context, cover);
  return ok && total == e->size;
}

/* ---- nested-cell trie
 * --------------------------------------------------------------- */

static ds_status_t bb_new_node(ds_bang_base_t *t, size_t parent, unsigned bit,
                               size_t *out) {
  size_t d = t->dim;
  size_t v;
  size_t a;
  if (t->node_free != BF_NONE) {
    v = t->node_free;
    t->node_free = t->parent[v];
  } else {
    if (t->nodes == t->node_capacity) {
      size_t nc = t->node_capacity < 8 ? 8 : t->node_capacity * 2;
      void *p;
#define BB_RE(field, type, per)                                                \
  p = ds_context_realloc(t->context, t->field, nc * (per) * sizeof(type));     \
  if (p == NULL) {                                                             \
    return DS_ERR_ALLOC;                                                       \
  }                                                                            \
  t->field = (type *)p;
      BB_RE(child, size_t, 2)
      BB_RE(parent, size_t, 1)
      BB_RE(depth, size_t, 1)
      BB_RE(qlo, unsigned long, d)
      BB_RE(node_bucket, size_t, 1)
#undef BB_RE
      t->node_capacity = nc;
    }
    v = t->nodes++;
  }
  t->child[2 * v] = t->child[2 * v + 1] = BF_NONE;
  t->parent[v] = parent;
  t->node_bucket[v] = BF_NONE;
  if (parent == BF_NONE) {
    t->depth[v] = 0;
    for (a = 0; a < d; a++) {
      t->qlo[v * d + a] = 0;
    }
  } else {
    size_t k = t->depth[parent];
    t->depth[v] = k + 1;
    memcpy(t->qlo + v * d, t->qlo + parent * d, d * sizeof(unsigned long));
    if (bit) {
      t->qlo[v * d + k % d] |= 1UL << (BF_B - 1 - k / d);
    }
    t->child[2 * parent + bit] = v;
  }
  *out = v;
  return DS_OK;
}

static ds_status_t bb_new_bucket(ds_bang_base_t *t, size_t node, size_t *out) {
  size_t b;
  if (t->free_list != BF_NONE) {
    b = t->free_list;
    t->free_list = t->node[b];
  } else {
    if (t->buckets == t->bucket_capacity) {
      size_t nc = t->bucket_capacity < 8 ? 8 : t->bucket_capacity * 2;
      void *p;
#define BB_RE(field, type)                                                     \
  p = ds_context_realloc(t->context, t->field, nc * sizeof(type));             \
  if (p == NULL) {                                                             \
    return DS_ERR_ALLOC;                                                       \
  }                                                                            \
  t->field = (type *)p;
      BB_RE(node, size_t)
      BB_RE(count, size_t)
      BB_RE(capacity, size_t)
      BB_RE(items, double *)
      BB_RE(values, size_t *)
      BB_RE(alive, unsigned char)
#undef BB_RE
      t->bucket_capacity = nc;
    }
    b = t->buckets++;
  }
  t->node[b] = node;
  t->count[b] = 0;
  t->capacity[b] = 0;
  t->items[b] = NULL;
  t->values[b] = NULL;
  t->alive[b] = 1;
  t->node_bucket[node] = b;
  t->live_buckets++;
  *out = b;
  return DS_OK;
}

/* Drop bucket b; its cell's now useless trie nodes are pruned. */
static void bb_free_bucket(ds_bang_base_t *t, size_t b) {
  size_t v = t->node[b];
  ds_context_free(t->context, t->items[b]);
  ds_context_free(t->context, t->values[b]);
  t->items[b] = NULL;
  t->values[b] = NULL;
  t->count[b] = 0;
  t->capacity[b] = 0;
  t->alive[b] = 0;
  t->node[b] = t->free_list;
  t->free_list = b;
  t->live_buckets--;
  t->node_bucket[v] = BF_NONE;
  while (t->parent[v] != BF_NONE && t->node_bucket[v] == BF_NONE &&
         t->child[2 * v] == BF_NONE && t->child[2 * v + 1] == BF_NONE) {
    size_t p = t->parent[v];
    if (t->child[2 * p] == v) {
      t->child[2 * p] = BF_NONE;
    } else {
      t->child[2 * p + 1] = BF_NONE;
    }
    t->parent[v] = t->node_free;
    t->node_free = v;
    v = p;
  }
}

static ds_status_t bb_init(ds_bang_base_t *t, size_t dim, size_t width,
                           const double *domain, size_t bucket,
                           ds_context_t *context) {
  size_t root;
  size_t b;
  ds_status_t st;
  if (t == NULL || domain == NULL) {
    return DS_ERR_NULL;
  }
  if (bucket == 0) {
    bucket = 16;
  }
  st = bf_check_domain(dim, domain);
  if (st != DS_OK) {
    return st;
  }
  if (bucket < 2) {
    return DS_ERR_INVALID;
  }
  memset(t, 0, sizeof(*t));
  t->dim = dim;
  t->width = width;
  t->bucket = bucket;
  memcpy(t->domain, domain, 2 * dim * sizeof(double));
  t->node_free = BF_NONE;
  t->free_list = BF_NONE;
  t->context = context;
  st = bb_new_node(t, BF_NONE, 0, &root);
  if (st == DS_OK) {
    st = bb_new_bucket(t, root, &b);
  }
  return st;
}

static void bb_destroy(ds_bang_base_t *t) {
  size_t b;
  for (b = 0; b < t->buckets; b++) {
    if (t->alive[b]) {
      ds_context_free(t->context, t->items[b]);
      ds_context_free(t->context, t->values[b]);
    }
  }
  ds_context_free(t->context, t->child);
  ds_context_free(t->context, t->parent);
  ds_context_free(t->context, t->depth);
  ds_context_free(t->context, t->qlo);
  ds_context_free(t->context, t->node_bucket);
  ds_context_free(t->context, t->node);
  ds_context_free(t->context, t->count);
  ds_context_free(t->context, t->capacity);
  ds_context_free(t->context, t->items);
  ds_context_free(t->context, t->values);
  ds_context_free(t->context, t->alive);
  memset(t, 0, sizeof(*t));
}

/* The deepest bucket on the path of key q, looking no deeper than limit. */
static size_t bb_lookup(const ds_bang_base_t *t, const unsigned long *q,
                        size_t limit) {
  size_t v = 0;
  size_t best = t->node_bucket[0];
  size_t k;
  for (k = 0; k < limit; k++) {
    size_t c = t->child[2 * v + bf_bit(q, t->dim, k)];
    if (c == BF_NONE) {
      break;
    }
    v = c;
    if (t->node_bucket[v] != BF_NONE) {
      best = t->node_bucket[v];
    }
  }
  return best;
}

/* Make the trie path from node v down along key q to the given depth. */
static ds_status_t bb_path(ds_bang_base_t *t, size_t v, const unsigned long *q,
                           size_t depth, size_t *out) {
  while (t->depth[v] < depth) {
    unsigned bit = bf_bit(q, t->dim, t->depth[v]);
    size_t c = t->child[2 * v + bit];
    if (c == BF_NONE) {
      ds_status_t st = bb_new_node(t, v, bit, &c);
      if (st != DS_OK) {
        return st;
      }
    }
    v = c;
  }
  *out = v;
  return DS_OK;
}

/* Move the items of bucket b selected by sel into bucket nb. */
static ds_status_t bb_move(ds_bang_base_t *t, size_t b, size_t nb,
                           const unsigned char *sel) {
  size_t w = t->width;
  size_t i;
  size_t keep = 0;
  size_t n = t->count[b];
  ds_status_t st = DS_OK;
  for (i = 0; i < n; i++) {
    if (sel[i]) {
      st = bf_append(t->context, &t->items[nb], &t->values[nb], &t->count[nb],
                     &t->capacity[nb], w, t->items[b] + i * w, t->values[b][i]);
      if (st != DS_OK) {
        break;
      }
    }
  }
  if (st != DS_OK) {
    t->count[nb] = 0;
    return st;
  }
  for (i = 0; i < n; i++) {
    if (!sel[i]) {
      if (keep != i) {
        memmove(t->items[b] + keep * w, t->items[b] + i * w,
                w * sizeof(double));
        t->values[b][keep] = t->values[b][i];
      }
      keep++;
    }
  }
  t->count[b] = keep;
  return DS_OK;
}

/* Merge bucket b into the nearest enclosing bucket when both fit two
 * thirds of a bucket. */
static size_t bb_merge_up(ds_bang_base_t *t, size_t b) {
  size_t v = t->node[b];
  size_t enc;
  size_t need;
  if (t->parent[v] == BF_NONE) {
    return BF_NONE;
  }
  for (v = t->parent[v]; t->node_bucket[v] == BF_NONE; v = t->parent[v]) {
  }
  enc = t->node_bucket[v];
  need = t->count[b] + t->count[enc];
  if (need > t->bucket * 2 / 3) {
    return BF_NONE;
  }
  if (need > t->capacity[enc]) {
    void *p = ds_context_realloc(t->context, t->items[enc],
                                 need * t->width * sizeof(double));
    if (p == NULL) {
      return BF_NONE;
    }
    t->items[enc] = (double *)p;
    p = ds_context_realloc(t->context, t->values[enc], need * sizeof(size_t));
    if (p == NULL) {
      return BF_NONE;
    }
    t->values[enc] = (size_t *)p;
    t->capacity[enc] = need;
  }
  if (t->count[b] > 0) {
    memcpy(t->items[enc] + t->count[enc] * t->width, t->items[b],
           t->count[b] * t->width * sizeof(double));
    memcpy(t->values[enc] + t->count[enc], t->values[b],
           t->count[b] * sizeof(size_t));
  }
  t->count[enc] = need;
  bb_free_bucket(t, b);
  return enc;
}

static void bb_merge_chain(ds_bang_base_t *t, size_t b) {
  while (b != BF_NONE) {
    b = bb_merge_up(t, b);
  }
}

static ds_status_t bb_query(const ds_bang_base_t *t, const double *box,
                            ds_bang_visit_func_t visit, void *user) {
  size_t b;
  size_t i;
  size_t d = t->dim;
  size_t w = t->width;
  for (b = 0; b < t->buckets; b++) {
    double cell[2 * BF_MAXD];
    size_t v;
    if (!t->alive[b] || t->count[b] == 0) {
      continue;
    }
    v = t->node[b];
    bf_cell_box(t->domain, d, t->qlo + v * d, t->depth[v], cell);
    if (!bf_box_hit(cell, box, d)) {
      continue;
    }
    for (i = 0; i < t->count[b]; i++) {
      const double *it = t->items[b] + i * w;
      bool in = w == d ? bf_point_in_box(it, box, d) : bf_box_hit(it, box, d);
      if (in && !visit(it, t->values[b][i], user)) {
        return DS_STOP;
      }
    }
  }
  return DS_OK;
}

/* ---- BANG file
 * ------------------------------------------------------------------------ */

ds_status_t ds_bang_file_init(ds_bang_file_t *f, size_t dim,
                              const double *domain, size_t bucket,
                              ds_context_t *context) {
  ds_status_t st;
  if (f == NULL) {
    return DS_ERR_NULL;
  }
  st = bb_init(&f->base, dim, dim, domain, bucket, context);
  if (st != DS_OK && st != DS_ERR_NULL && st != DS_ERR_INVALID) {
    bb_destroy(&f->base);
  }
  return st;
}

void ds_bang_file_destroy(ds_bang_file_t *f) {
  if (f != NULL) {
    bb_destroy(&f->base);
  }
}

size_t ds_bang_file_size(const ds_bang_file_t *f) {
  return f == NULL ? 0 : f->base.size;
}

size_t ds_bang_file_regions(const ds_bang_file_t *f) {
  return f == NULL ? 0 : f->base.live_buckets;
}

/* Carve a nested region out of overflowing bucket b: the sub-cell of its
 * cell holding closest to half of b's points. false when none helps. */
static ds_status_t bang_split(ds_bang_base_t *t, size_t b, size_t *out_nb) {
  size_t d = t->dim;
  size_t n = t->count[b];
  size_t v0 = t->node[b];
  size_t k = t->depth[v0];
  size_t maxk = BF_B * d;
  unsigned long *q;
  unsigned char *in;
  unsigned char *best_in;
  size_t i;
  size_t j;
  size_t cur = n;
  size_t best = 0;
  size_t best_depth = 0;
  size_t nb = 0;
  size_t node;
  ds_status_t st = DS_OK;
  q = (unsigned long *)ds_context_alloc(t->context,
                                        (n + 1) * d * sizeof(unsigned long));
  in = (unsigned char *)ds_context_alloc(t->context, n + 1);
  best_in = (unsigned char *)ds_context_alloc(t->context, n + 1);
  if (q == NULL || in == NULL || best_in == NULL) {
    ds_context_free(t->context, q);
    ds_context_free(t->context, in);
    ds_context_free(t->context, best_in);
    return DS_ERR_ALLOC;
  }
  for (i = 0; i < n; i++) {
    bf_quantise(t->domain, d, t->items[b] + i * d, q + i * d);
    in[i] = 1;
  }
  for (j = k; j < maxk; j++) {
    size_t c1 = 0;
    unsigned bit;
    size_t big;
    for (i = 0; i < n; i++) {
      if (in[i]) {
        c1 += bf_bit(q + i * d, d, j);
      }
    }
    bit = c1 * 2 > cur ? 1U : 0U;
    big = bit ? c1 : cur - c1;
    for (i = 0; i < n; i++) {
      if (in[i] && bf_bit(q + i * d, d, j) != bit) {
        in[i] = 0;
      }
    }
    /* candidate: the child at depth j + 1 with `big` points */
    {
      size_t dist_big = big * 2 > n ? big * 2 - n : n - big * 2;
      size_t dist_best = best * 2 > n ? best * 2 - n : n - best * 2;
      if (big < n && big > 0 && (best == 0 || dist_big < dist_best)) {
        best = big;
        best_depth = j + 1;
        memcpy(best_in, in, n);
      }
    }
    if (big * 2 <= n) {
      break;
    }
    cur = big;
  }
  if (best == 0) {
    ds_context_free(t->context, q);
    ds_context_free(t->context, in);
    ds_context_free(t->context, best_in);
    return DS_EXISTS; /* coinciding points */
  }
  /* the path follows the bits of any chosen point */
  for (i = 0; i < n && !best_in[i]; i++) {
  }
  st = bb_path(t, v0, q + i * d, best_depth, &node);
  if (st == DS_OK) {
    st = t->node_bucket[node] == BF_NONE ? bb_new_bucket(t, node, &nb)
                                         : DS_ERR_INTERNAL;
  }
  if (st == DS_OK) {
    st = bb_move(t, b, nb, best_in);
    if (st != DS_OK) {
      bb_free_bucket(t, nb);
    }
  }
  ds_context_free(t->context, q);
  ds_context_free(t->context, in);
  ds_context_free(t->context, best_in);
  if (st == DS_OK) {
    *out_nb = nb;
  }
  return st;
}

ds_status_t ds_bang_file_insert(ds_bang_file_t *f, const double *point,
                                size_t value) {
  ds_bang_base_t *t;
  unsigned long q[BF_MAXD];
  size_t b;
  ds_status_t st;
  if (f == NULL || point == NULL) {
    return DS_ERR_NULL;
  }
  t = &f->base;
  if (!bf_finite(point, t->dim)) {
    return DS_ERR_INVALID;
  }
  bf_quantise(t->domain, t->dim, point, q);
  b = bb_lookup(t, q, BF_B * t->dim);
  st = bf_append(t->context, &t->items[b], &t->values[b], &t->count[b],
                 &t->capacity[b], t->dim, point, value);
  if (st != DS_OK) {
    return st;
  }
  t->size++;
  while (t->count[b] > t->bucket) {
    size_t nb;
    st = bang_split(t, b, &nb);
    if (st == DS_EXISTS) {
      return DS_OK; /* an oversized region */
    }
    if (st != DS_OK) {
      return st;
    }
    if (t->count[nb] > t->count[b]) {
      b = nb;
    }
  }
  return DS_OK;
}

ds_status_t ds_bang_file_remove(ds_bang_file_t *f, const double *point,
                                size_t value) {
  ds_bang_base_t *t;
  unsigned long q[BF_MAXD];
  size_t b;
  size_t i;
  if (f == NULL || point == NULL) {
    return DS_ERR_NULL;
  }
  t = &f->base;
  if (!bf_finite(point, t->dim)) {
    return DS_NOT_FOUND;
  }
  bf_quantise(t->domain, t->dim, point, q);
  b = bb_lookup(t, q, BF_B * t->dim);
  i = bf_find(t->items[b], t->values[b], t->count[b], t->dim, point, value);
  if (i == BF_NONE) {
    return DS_NOT_FOUND;
  }
  bf_drop(t->items[b], t->values[b], &t->count[b], t->dim, i);
  t->size--;
  bb_merge_chain(t, b);
  return DS_OK;
}

ds_status_t ds_bang_file_query(const ds_bang_file_t *f, const double *box,
                               ds_bang_visit_func_t visit, void *user) {
  if (f == NULL || box == NULL || visit == NULL) {
    return DS_ERR_NULL;
  }
  return bb_query(&f->base, box, visit, user);
}

ds_status_t ds_bang_file_nearest(const ds_bang_file_t *f, const double *point,
                                 size_t k, size_t *out_values, double *out_dist,
                                 size_t *out_found) {
  const ds_bang_base_t *t;
  double *boxes;
  size_t b;
  size_t d;
  ds_status_t st;
  if (f == NULL || point == NULL || out_found == NULL ||
      (k > 0 && (out_values == NULL || out_dist == NULL))) {
    return DS_ERR_NULL;
  }
  t = &f->base;
  *out_found = 0;
  if (k == 0 || t->size == 0) {
    return DS_OK;
  }
  d = t->dim;
  boxes = (double *)ds_context_alloc(t->context,
                                     t->buckets * 2 * d * sizeof(double));
  if (boxes == NULL) {
    return DS_ERR_ALLOC;
  }
  for (b = 0; b < t->buckets; b++) {
    if (t->alive[b]) {
      size_t v = t->node[b];
      bf_cell_box(t->domain, d, t->qlo + v * d, t->depth[v], boxes + b * 2 * d);
    } else {
      memset(boxes + b * 2 * d, 0, 2 * d * sizeof(double));
    }
  }
  st = bf_knn(t->context, d, t->buckets, boxes, t->items, t->values, t->count,
              point, k, out_values, out_dist, out_found);
  ds_context_free(t->context, boxes);
  return st;
}

static bool bb_valid(const ds_bang_base_t *t, bool boxes) {
  size_t b;
  size_t i;
  size_t d = t->dim;
  size_t total = 0;
  for (b = 0; b < t->buckets; b++) {
    if (!t->alive[b]) {
      continue;
    }
    if (t->node_bucket[t->node[b]] != b) {
      return false;
    }
    for (i = 0; i < t->count[b]; i++) {
      const double *it = t->items[b] + i * t->width;
      unsigned long q[BF_MAXD];
      size_t limit = BF_B * d;
      bf_quantise(t->domain, d, it, q);
      if (boxes) {
        unsigned long qh[BF_MAXD];
        size_t j;
        bf_quantise(t->domain, d, it + d, qh);
        for (j = 0; j < limit && bf_bit(q, d, j) == bf_bit(qh, d, j); j++) {
        }
        limit = j;
      }
      if (bb_lookup(t, q, limit) != b) {
        return false;
      }
    }
    total += t->count[b];
  }
  return total == t->size;
}

bool ds_bang_file_valid(const ds_bang_file_t *f) {
  return f != NULL && f->base.node_bucket != NULL && bb_valid(&f->base, false);
}

/* ---- R-file
 * -------------------------------------------------------------------------- */

ds_status_t ds_rfile_init(ds_rfile_t *f, size_t dim, const double *domain,
                          size_t bucket, ds_context_t *context) {
  ds_status_t st;
  if (f == NULL) {
    return DS_ERR_NULL;
  }
  st = bb_init(&f->base, dim, 2 * dim, domain, bucket, context);
  if (st != DS_OK && st != DS_ERR_NULL && st != DS_ERR_INVALID) {
    bb_destroy(&f->base);
  }
  return st;
}

void ds_rfile_destroy(ds_rfile_t *f) {
  if (f != NULL) {
    bb_destroy(&f->base);
  }
}

size_t ds_rfile_size(const ds_rfile_t *f) {
  return f == NULL ? 0 : f->base.size;
}

size_t ds_rfile_buckets(const ds_rfile_t *f) {
  return f == NULL ? 0 : f->base.live_buckets;
}

/* the box's low-corner key and the length of the cell prefix it fits */
static size_t rf_key(const ds_bang_base_t *t, const double *box,
                     unsigned long *q) {
  unsigned long qh[BF_MAXD];
  size_t d = t->dim;
  size_t j;
  size_t limit = BF_B * d;
  bf_quantise(t->domain, d, box, q);
  bf_quantise(t->domain, d, box + d, qh);
  for (j = 0; j < limit && bf_bit(q, d, j) == bf_bit(qh, d, j); j++) {
  }
  return j;
}

/* Hand the boxes fitting the fullest proper sub-cell of b's cell to a new
 * bucket there. DS_EXISTS when every box straddles. */
static ds_status_t rf_split(ds_bang_base_t *t, size_t b, size_t *out_nb) {
  size_t d = t->dim;
  size_t n = t->count[b];
  size_t v0 = t->node[b];
  size_t k = t->depth[v0];
  size_t maxk = BF_B * d;
  unsigned long *q;
  size_t *len;
  unsigned char *in;
  size_t i;
  size_t j;
  size_t cur = n;
  size_t carve_depth = 0;
  size_t nb = 0;
  size_t node;
  ds_status_t st = DS_OK;
  q = (unsigned long *)ds_context_alloc(t->context,
                                        (n + 1) * d * sizeof(unsigned long));
  len = (size_t *)ds_context_alloc(t->context, (n + 1) * sizeof(size_t));
  in = (unsigned char *)ds_context_alloc(t->context, n + 1);
  if (q == NULL || len == NULL || in == NULL) {
    ds_context_free(t->context, q);
    ds_context_free(t->context, len);
    ds_context_free(t->context, in);
    return DS_ERR_ALLOC;
  }
  for (i = 0; i < n; i++) {
    len[i] = rf_key(t, t->items[b] + i * 2 * d, q + i * d);
    in[i] = 1;
  }
  for (j = k; j < maxk; j++) {
    size_t c[2] = {0, 0};
    unsigned bit;
    for (i = 0; i < n; i++) {
      if (in[i] && len[i] > j) {
        c[bf_bit(q + i * d, d, j)]++;
      }
    }
    bit = c[1] > c[0] ? 1U : 0U;
    if (c[bit] == 0) {
      break; /* everything left straddles this halving */
    }
    for (i = 0; i < n; i++) {
      if (in[i] && (len[i] <= j || bf_bit(q + i * d, d, j) != bit)) {
        in[i] = 0;
      }
    }
    if (c[bit] < cur) {
      carve_depth = j + 1;
      cur = c[bit];
      break;
    }
    /* all of them fit the same half: look one level deeper */
  }
  if (carve_depth == 0) {
    ds_context_free(t->context, q);
    ds_context_free(t->context, len);
    ds_context_free(t->context, in);
    return DS_EXISTS;
  }
  for (i = 0; i < n && !in[i]; i++) {
  }
  st = bb_path(t, v0, q + i * d, carve_depth, &node);
  if (st == DS_OK) {
    st = t->node_bucket[node] == BF_NONE ? bb_new_bucket(t, node, &nb)
                                         : DS_ERR_INTERNAL;
  }
  if (st == DS_OK) {
    st = bb_move(t, b, nb, in);
    if (st != DS_OK) {
      bb_free_bucket(t, nb);
    }
  }
  ds_context_free(t->context, q);
  ds_context_free(t->context, len);
  ds_context_free(t->context, in);
  if (st == DS_OK) {
    *out_nb = nb;
  }
  return st;
}

ds_status_t ds_rfile_insert(ds_rfile_t *f, const double *box, size_t value) {
  ds_bang_base_t *t;
  unsigned long q[BF_MAXD];
  size_t b;
  size_t a;
  size_t limit;
  ds_status_t st;
  if (f == NULL || box == NULL) {
    return DS_ERR_NULL;
  }
  t = &f->base;
  if (!bf_finite(box, 2 * t->dim)) {
    return DS_ERR_INVALID;
  }
  for (a = 0; a < t->dim; a++) {
    if (box[a] > box[t->dim + a]) {
      return DS_ERR_INVALID;
    }
  }
  limit = rf_key(t, box, q);
  b = bb_lookup(t, q, limit);
  st = bf_append(t->context, &t->items[b], &t->values[b], &t->count[b],
                 &t->capacity[b], 2 * t->dim, box, value);
  if (st != DS_OK) {
    return st;
  }
  t->size++;
  while (t->count[b] > t->bucket) {
    size_t nb;
    st = rf_split(t, b, &nb);
    if (st == DS_EXISTS) {
      return DS_OK;
    }
    if (st != DS_OK) {
      return st;
    }
    if (t->count[nb] > t->count[b]) {
      b = nb;
    }
  }
  return DS_OK;
}

ds_status_t ds_rfile_remove(ds_rfile_t *f, const double *box, size_t value) {
  ds_bang_base_t *t;
  unsigned long q[BF_MAXD];
  size_t b;
  size_t i;
  if (f == NULL || box == NULL) {
    return DS_ERR_NULL;
  }
  t = &f->base;
  if (!bf_finite(box, 2 * t->dim)) {
    return DS_NOT_FOUND;
  }
  b = bb_lookup(t, q, rf_key(t, box, q));
  i = bf_find(t->items[b], t->values[b], t->count[b], 2 * t->dim, box, value);
  if (i == BF_NONE) {
    return DS_NOT_FOUND;
  }
  bf_drop(t->items[b], t->values[b], &t->count[b], 2 * t->dim, i);
  t->size--;
  bb_merge_chain(t, b);
  return DS_OK;
}

ds_status_t ds_rfile_query(const ds_rfile_t *f, const double *box,
                           ds_bang_visit_func_t visit, void *user) {
  if (f == NULL || box == NULL || visit == NULL) {
    return DS_ERR_NULL;
  }
  return bb_query(&f->base, box, visit, user);
}

bool ds_rfile_valid(const ds_rfile_t *f) {
  return f != NULL && f->base.node_bucket != NULL && bb_valid(&f->base, true);
}
