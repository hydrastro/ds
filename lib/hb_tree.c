#include "hb_tree.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

#define HB_NONE (~(size_t)0)
#define HB_MAXD DS_HB_MAX_DIM
#define HB_B 30
#define HB_SCALE 1073741824.0

/* ---- helpers
 * ----------------------------------------------------------------- */

static bool hb_finite(const double *x, size_t n) {
  size_t i;
  for (i = 0; i < n; i++) {
    if (!(x[i] > -HUGE_VAL && x[i] < HUGE_VAL)) {
      return false;
    }
  }
  return true;
}

static bool hb_box_ok(const double *b, size_t d) {
  size_t a;
  if (!hb_finite(b, 2 * d)) {
    return false;
  }
  for (a = 0; a < d; a++) {
    if (b[a] > b[d + a]) {
      return false;
    }
  }
  return true;
}

static ds_status_t hb_domain_ok(size_t dim, const double *domain) {
  size_t a;
  if (domain == NULL) {
    return DS_ERR_NULL;
  }
  if (!hb_finite(domain, 2 * dim)) {
    return DS_ERR_INVALID;
  }
  for (a = 0; a < dim; a++) {
    if (!(domain[a] < domain[dim + a])) {
      return DS_ERR_INVALID;
    }
  }
  return DS_OK;
}

static bool hb_point_in_box(const double *p, const double *b, size_t d) {
  size_t a;
  for (a = 0; a < d; a++) {
    if (p[a] < b[a] || p[a] > b[d + a]) {
      return false;
    }
  }
  return true;
}

static bool hb_box_hit(const double *x, const double *y, size_t d) {
  size_t a;
  for (a = 0; a < d; a++) {
    if (x[d + a] < y[a] || y[d + a] < x[a]) {
      return false;
    }
  }
  return true;
}

/* closed box meets half-open cell */
static bool hb_box_meets_cell(const double *b, const double *c, size_t d) {
  size_t a;
  for (a = 0; a < d; a++) {
    if (!(b[a] < c[d + a]) || b[d + a] < c[a]) {
      return false;
    }
  }
  return true;
}

static double hb_mindist2(const double *b, const double *p, size_t d) {
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

static void hb_empty_box(double *b, size_t d) {
  size_t a;
  for (a = 0; a < d; a++) {
    b[a] = HUGE_VAL;
    b[d + a] = -HUGE_VAL;
  }
}

static void hb_extend(double *b, const double *x, size_t d, bool point) {
  size_t a;
  for (a = 0; a < d; a++) {
    double lo = x[a];
    double hi = point ? x[a] : x[d + a];
    if (lo < b[a]) {
      b[a] = lo;
    }
    if (hi > b[d + a]) {
      b[d + a] = hi;
    }
  }
}

static int hb_cmp_double(const void *a, const void *b) {
  double x = *(const double *)a;
  double y = *(const double *)b;
  return (x > y) - (x < y);
}

static ds_status_t hb_append(ds_context_t *ctx, double **items, size_t **vals,
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

static size_t hb_find(const double *items, const size_t *vals, size_t count,
                      size_t width, const double *item, size_t value) {
  size_t i;
  for (i = 0; i < count; i++) {
    if (vals[i] == value &&
        memcmp(items + i * width, item, width * sizeof(double)) == 0) {
      return i;
    }
  }
  return HB_NONE;
}

static void hb_drop(double *items, size_t *vals, size_t *count, size_t width,
                    size_t i) {
  size_t last = --*count;
  if (i != last) {
    memcpy(items + i * width, items + last * width, width * sizeof(double));
    vals[i] = vals[last];
  }
}

static void hb_best_push(double *dk, size_t *vk, size_t k, size_t *found,
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

static ds_status_t hb_grow(ds_context_t *ctx, void **p, size_t elem,
                           size_t ncap) {
  void *q = ds_context_realloc(ctx, *p, ncap * elem);
  if (q == NULL) {
    return DS_ERR_ALLOC;
  }
  *p = q;
  return DS_OK;
}

/* DZ-expressions: 30-bit quantisation, interleaved most significant first */
static void hb_quantise(const double *domain, size_t d, const double *p,
                        unsigned long *q) {
  size_t a;
  for (a = 0; a < d; a++) {
    double f = (p[a] - domain[a]) / (domain[d + a] - domain[a]) * HB_SCALE;
    if (!(f > 0.0)) {
      q[a] = 0;
    } else if (!(f < HB_SCALE)) {
      q[a] = (1UL << HB_B) - 1;
    } else {
      q[a] = (unsigned long)f;
      if (q[a] > (1UL << HB_B) - 1) {
        q[a] = (1UL << HB_B) - 1;
      }
    }
  }
}

static unsigned hb_bit(const unsigned long *q, size_t d, size_t i) {
  return (unsigned)((q[i % d] >> (HB_B - 1 - i / d)) & 1UL);
}

/* does key q lie in the DZ cell (z, depth)? */
static bool hb_dz_match(const unsigned long *q, const unsigned long *z,
                        size_t depth, size_t d) {
  size_t a;
  for (a = 0; a < d; a++) {
    size_t n = depth / d + (a < depth % d ? 1 : 0);
    unsigned long mask;
    if (n == 0) {
      continue;
    }
    mask = ((1UL << n) - 1) << (HB_B - n);
    if ((q[a] & mask) != (z[a] & mask)) {
      return false;
    }
  }
  return true;
}

/* the DZ cell of key q at the given depth (low corner) */
static void hb_dz_of(const unsigned long *q, size_t depth, size_t d,
                     unsigned long *z) {
  size_t a;
  for (a = 0; a < d; a++) {
    size_t n = depth / d + (a < depth % d ? 1 : 0);
    z[a] = n == 0 ? 0 : q[a] & (((1UL << n) - 1) << (HB_B - n));
  }
}

/* real box of a DZ cell, one quantum wider, unbounded at the border */
static void hb_dz_box(const double *domain, size_t d, const unsigned long *z,
                      size_t depth, double *box) {
  size_t a;
  for (a = 0; a < d; a++) {
    size_t n = depth / d + (a < depth % d ? 1 : 0);
    double span = domain[d + a] - domain[a];
    double quantum = span / HB_SCALE;
    double lo = (double)z[a];
    double hi = lo + ldexp(1.0, (int)(HB_B - n));
    box[a] = z[a] == 0 ? -HUGE_VAL : domain[a] + span * lo / HB_SCALE - quantum;
    box[d + a] = !(hi < HB_SCALE) ? HUGE_VAL
                                  : domain[a] + span * hi / HB_SCALE + quantum;
  }
}

/* Choose a DZ sub-cell below `base` (depth k) for the keys keys[0..n) that
 * extend it (len[i] >= its depth when len is given): descend into the
 * heavier half while it holds more than half, keep the candidate closest
 * to half. Returns its depth (0: none) and marks the chosen keys. */
static size_t hb_carve(const unsigned long *keys, const size_t *len, size_t n,
                       size_t d, size_t k, unsigned char *in,
                       unsigned char *best_in) {
  size_t maxk = HB_B * d;
  size_t j;
  size_t i;
  size_t cur = 0;
  size_t best = 0;
  size_t best_depth = 0;
  for (i = 0; i < n; i++) {
    in[i] = (unsigned char)(len == NULL || len[i] >= k);
    cur += in[i];
  }
  if (cur == 0) {
    return 0;
  }
  for (j = k; j < maxk; j++) {
    size_t c[2] = {0, 0};
    unsigned bit;
    size_t big;
    for (i = 0; i < n; i++) {
      if (in[i] && (len == NULL || len[i] > j)) {
        c[hb_bit(keys + i * d, d, j)]++;
      }
    }
    bit = c[1] > c[0] ? 1U : 0U;
    big = c[bit];
    if (big == 0) {
      break;
    }
    for (i = 0; i < n; i++) {
      if (in[i] &&
          ((len != NULL && len[i] <= j) || hb_bit(keys + i * d, d, j) != bit)) {
        in[i] = 0;
      }
    }
    if (big < n) {
      size_t db = big * 2 > n ? big * 2 - n : n - big * 2;
      size_t dbest = best * 2 > n ? best * 2 - n : n - best * 2;
      if (best == 0 || db < dbest) {
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
  (void)cur;
  return best_depth;
}

/* ---- hB-tree / buddy tree engine
 * ---------------------------------------------------- */

#define KLEAF HB_NONE

static ds_status_t hb_knode(ds_hb_tree_t *t, size_t owner, size_t *out) {
  size_t v;
  if (t->kfree != HB_NONE) {
    v = t->kfree;
    t->kfree = t->klo[v];
  } else {
    if (t->knodes == t->kcapacity) {
      size_t nc = t->kcapacity < 16 ? 16 : t->kcapacity * 2;
      size_t d = t->dim;
      if (hb_grow(t->context, (void **)&t->kaxis, sizeof(size_t), nc) ||
          hb_grow(t->context, (void **)&t->kvalue, sizeof(double), nc) ||
          hb_grow(t->context, (void **)&t->klo, sizeof(size_t), nc) ||
          hb_grow(t->context, (void **)&t->khi, sizeof(size_t), nc) ||
          hb_grow(t->context, (void **)&t->ktarget, sizeof(size_t), nc) ||
          hb_grow(t->context, (void **)&t->kpage, 1, nc) ||
          hb_grow(t->context, (void **)&t->kowner, sizeof(size_t), nc) ||
          hb_grow(t->context, (void **)&t->kmbb, 2 * d * sizeof(double), nc)) {
        return DS_ERR_ALLOC;
      }
      t->kcapacity = nc;
    }
    v = t->knodes++;
  }
  t->kaxis[v] = KLEAF;
  t->kvalue[v] = 0.0;
  t->klo[v] = t->khi[v] = HB_NONE;
  t->ktarget[v] = HB_NONE;
  t->kpage[v] = 1;
  t->kowner[v] = owner;
  hb_empty_box(t->kmbb + v * 2 * t->dim, t->dim);
  *out = v;
  return DS_OK;
}

static void hb_kfree(ds_hb_tree_t *t, size_t v) {
  t->klo[v] = t->kfree;
  t->kfree = v;
}

static ds_status_t hb_inode(ds_hb_tree_t *t, size_t level, size_t *out) {
  size_t v;
  if (t->ifree != HB_NONE) {
    v = t->ifree;
    t->ifree = t->iroot[v];
  } else {
    if (t->inodes == t->icapacity) {
      size_t nc = t->icapacity < 8 ? 8 : t->icapacity * 2;
      if (hb_grow(t->context, (void **)&t->iroot, sizeof(size_t), nc) ||
          hb_grow(t->context, (void **)&t->ileaves, sizeof(size_t), nc) ||
          hb_grow(t->context, (void **)&t->iparent, sizeof(size_t), nc) ||
          hb_grow(t->context, (void **)&t->ilevel, sizeof(size_t), nc)) {
        return DS_ERR_ALLOC;
      }
      t->icapacity = nc;
    }
    v = t->inodes++;
  }
  t->iroot[v] = HB_NONE;
  t->ileaves[v] = 0;
  t->iparent[v] = HB_NONE;
  t->ilevel[v] = level;
  *out = v;
  return DS_OK;
}

static ds_status_t hb_page(ds_hb_tree_t *t, size_t owner, size_t *out) {
  size_t p;
  if (t->pfree != HB_NONE) {
    p = t->pfree;
    t->pfree = t->owner[p];
  } else {
    if (t->pages == t->pcapacity) {
      size_t nc = t->pcapacity < 8 ? 8 : t->pcapacity * 2;
      if (hb_grow(t->context, (void **)&t->points, sizeof(double *), nc) ||
          hb_grow(t->context, (void **)&t->values, sizeof(size_t *), nc) ||
          hb_grow(t->context, (void **)&t->count, sizeof(size_t), nc) ||
          hb_grow(t->context, (void **)&t->capacity, sizeof(size_t), nc) ||
          hb_grow(t->context, (void **)&t->owner, sizeof(size_t), nc) ||
          hb_grow(t->context, (void **)&t->stamp, sizeof(unsigned), nc) ||
          hb_grow(t->context, (void **)&t->alive, 1, nc)) {
        return DS_ERR_ALLOC;
      }
      t->pcapacity = nc;
    }
    p = t->pages++;
  }
  t->points[p] = NULL;
  t->values[p] = NULL;
  t->count[p] = 0;
  t->capacity[p] = 0;
  t->owner[p] = owner;
  t->stamp[p] = 0;
  t->alive[p] = 1;
  t->live_pages++;
  *out = p;
  return DS_OK;
}

ds_status_t ds_hb_tree_init(ds_hb_tree_t *t, size_t dim, ds_hb_mode_t mode,
                            const double *domain, size_t bucket, size_t fanout,
                            ds_context_t *context) {
  size_t a;
  size_t leaf = 0;
  size_t page = 0;
  ds_status_t st;
  if (t == NULL) {
    return DS_ERR_NULL;
  }
  if (bucket == 0) {
    bucket = 16;
  }
  if (fanout == 0) {
    fanout = 32;
  }
  if (dim == 0 || dim > HB_MAXD || bucket < 2 || fanout < 4 ||
      ((int)mode != DS_HB_HOLEY && mode != DS_HB_BUDDY)) {
    return DS_ERR_INVALID;
  }
  if (mode == DS_HB_BUDDY) {
    st = hb_domain_ok(dim, domain);
    if (st != DS_OK) {
      return st;
    }
  }
  memset(t, 0, sizeof(*t));
  t->dim = dim;
  t->bucket = bucket;
  t->fanout = fanout;
  t->mode = mode;
  for (a = 0; a < dim; a++) {
    t->domain[a] = mode == DS_HB_BUDDY ? domain[a] : -HUGE_VAL;
    t->domain[dim + a] = mode == DS_HB_BUDDY ? domain[dim + a] : HUGE_VAL;
  }
  t->kfree = t->ifree = t->pfree = HB_NONE;
  t->context = context;
  st = hb_inode(t, 1, &t->root);
  if (st == DS_OK) {
    st = hb_knode(t, t->root, &leaf);
  }
  if (st == DS_OK) {
    st = hb_page(t, t->root, &page);
  }
  if (st != DS_OK) {
    ds_hb_tree_destroy(t);
    return st;
  }
  t->iroot[t->root] = leaf;
  t->ileaves[t->root] = 1;
  t->ktarget[leaf] = page;
  t->height = 1;
  return DS_OK;
}

void ds_hb_tree_destroy(ds_hb_tree_t *t) {
  size_t p;
  if (t == NULL) {
    return;
  }
  for (p = 0; p < t->pages; p++) {
    if (t->alive[p]) {
      ds_context_free(t->context, t->points[p]);
      ds_context_free(t->context, t->values[p]);
    }
  }
  ds_context_free(t->context, t->kaxis);
  ds_context_free(t->context, t->kvalue);
  ds_context_free(t->context, t->klo);
  ds_context_free(t->context, t->khi);
  ds_context_free(t->context, t->ktarget);
  ds_context_free(t->context, t->kpage);
  ds_context_free(t->context, t->kowner);
  ds_context_free(t->context, t->kmbb);
  ds_context_free(t->context, t->iroot);
  ds_context_free(t->context, t->ileaves);
  ds_context_free(t->context, t->iparent);
  ds_context_free(t->context, t->ilevel);
  ds_context_free(t->context, t->points);
  ds_context_free(t->context, t->values);
  ds_context_free(t->context, t->count);
  ds_context_free(t->context, t->capacity);
  ds_context_free(t->context, t->owner);
  ds_context_free(t->context, t->stamp);
  ds_context_free(t->context, t->alive);
  memset(t, 0, sizeof(*t));
}

size_t ds_hb_tree_size(const ds_hb_tree_t *t) {
  return t == NULL ? 0 : t->size;
}

size_t ds_hb_tree_height(const ds_hb_tree_t *t) {
  return t == NULL ? 0 : t->height;
}

size_t ds_hb_tree_pages(const ds_hb_tree_t *t) {
  return t == NULL ? 0 : t->live_pages;
}

/* Descend to the k-d leaf holding p; the leaf's cell and the cell of its
 * index node come back too. path (optional) receives the leaves passed. */
static size_t hb_descend(const ds_hb_tree_t *t, const double *p, double *cell,
                         double *icell, size_t *path, size_t *plen) {
  size_t d = t->dim;
  size_t i = t->root;
  size_t n = 0;
  size_t a;
  for (a = 0; a < d; a++) {
    cell[a] = -HUGE_VAL;
    cell[d + a] = HUGE_VAL;
  }
  for (;;) {
    size_t v = t->iroot[i];
    if (icell != NULL) {
      memcpy(icell, cell, 2 * d * sizeof(double));
    }
    while (t->kaxis[v] != KLEAF) {
      a = t->kaxis[v];
      if (p[a] < t->kvalue[v]) {
        cell[d + a] = t->kvalue[v];
        v = t->klo[v];
      } else {
        cell[a] = t->kvalue[v];
        v = t->khi[v];
      }
    }
    if (path != NULL) {
      path[n++] = v;
    }
    if (t->kpage[v]) {
      if (plen != NULL) {
        *plen = n;
      }
      return v;
    }
    i = t->ktarget[v];
  }
}

/* Leaves of index node i's k-d tree with their cells (stack-based). */
typedef struct hb_leafset {
  size_t *leaf;
  double *cell;
  size_t n;
  size_t cap;
} hb_leafset_t;

static ds_status_t hb_leaves(ds_hb_tree_t *t, size_t i, const double *icell,
                             hb_leafset_t *out) {
  size_t d = t->dim;
  size_t *st = NULL;
  double *sc = NULL;
  size_t top = 0;
  size_t cap = 0;
  ds_status_t r = DS_OK;
  out->n = 0;
  cap = 2 * t->ileaves[i] + 2;
  st = (size_t *)ds_context_alloc(t->context, cap * sizeof(size_t));
  sc = (double *)ds_context_alloc(t->context, cap * 2 * d * sizeof(double));
  if (st == NULL || sc == NULL ||
      (out->cap < t->ileaves[i] &&
       (hb_grow(t->context, (void **)&out->leaf, sizeof(size_t),
                t->ileaves[i]) ||
        hb_grow(t->context, (void **)&out->cell, 2 * d * sizeof(double),
                t->ileaves[i])))) {
    ds_context_free(t->context, st);
    ds_context_free(t->context, sc);
    return DS_ERR_ALLOC;
  }
  if (out->cap < t->ileaves[i]) {
    out->cap = t->ileaves[i];
  }
  st[top] = t->iroot[i];
  memcpy(sc, icell, 2 * d * sizeof(double));
  top++;
  while (top > 0) {
    size_t v;
    double c[2 * HB_MAXD];
    top--;
    v = st[top];
    memcpy(c, sc + top * 2 * d, 2 * d * sizeof(double));
    if (t->kaxis[v] == KLEAF) {
      if (out->n == out->cap) {
        r = DS_ERR_INTERNAL;
        break;
      }
      out->leaf[out->n] = v;
      memcpy(out->cell + out->n * 2 * d, c, 2 * d * sizeof(double));
      out->n++;
      continue;
    }
    st[top] = t->klo[v];
    memcpy(sc + top * 2 * d, c, 2 * d * sizeof(double));
    sc[top * 2 * d + d + t->kaxis[v]] = t->kvalue[v];
    top++;
    st[top] = t->khi[v];
    memcpy(sc + top * 2 * d, c, 2 * d * sizeof(double));
    sc[top * 2 * d + t->kaxis[v]] = t->kvalue[v];
    top++;
  }
  ds_context_free(t->context, st);
  ds_context_free(t->context, sc);
  return r;
}

static void hb_page_mbb(const ds_hb_tree_t *t, size_t p, double *b) {
  size_t i;
  hb_empty_box(b, t->dim);
  if (p == HB_NONE) {
    return;
  }
  for (i = 0; i < t->count[p]; i++) {
    hb_extend(b, t->points[p] + i * t->dim, t->dim, true);
  }
}

/* union of the leaf boxes of index node i (walks its k-d tree) */
static void hb_index_mbb(const ds_hb_tree_t *t, size_t i, double *b,
                         size_t *stack) {
  size_t top = 0;
  hb_empty_box(b, t->dim);
  stack[top++] = t->iroot[i];
  while (top > 0) {
    size_t v = stack[--top];
    if (t->kaxis[v] == KLEAF) {
      hb_extend(b, t->kmbb + v * 2 * t->dim, t->dim, false);
    } else {
      stack[top++] = t->klo[v];
      stack[top++] = t->khi[v];
    }
  }
}

/* Turn leaf v into an inner node (axis, x) with two fresh leaves. */
static ds_status_t hb_cut_leaf(ds_hb_tree_t *t, size_t v, size_t axis, double x,
                               size_t *lo, size_t *hi) {
  size_t a;
  size_t b;
  ds_status_t st = hb_knode(t, t->kowner[v], &a);
  if (st != DS_OK) {
    return st;
  }
  st = hb_knode(t, t->kowner[v], &b);
  if (st != DS_OK) {
    hb_kfree(t, a);
    return st;
  }
  t->ktarget[a] = t->ktarget[b] = t->ktarget[v];
  t->kpage[a] = t->kpage[b] = t->kpage[v];
  t->kaxis[v] = axis;
  t->kvalue[v] = x;
  t->klo[v] = a;
  t->khi[v] = b;
  t->ileaves[t->kowner[v]]++;
  *lo = a;
  *hi = b;
  return DS_OK;
}

/* Move points of page p with coordinate >= x on axis into page q. */
static ds_status_t hb_page_split_at(ds_hb_tree_t *t, size_t p, size_t q,
                                    size_t axis, double x) {
  size_t d = t->dim;
  size_t i;
  size_t keep = 0;
  for (i = 0; i < t->count[p]; i++) {
    double *pt = t->points[p] + i * d;
    if (pt[axis] >= x) {
      ds_status_t st =
          hb_append(t->context, &t->points[q], &t->values[q], &t->count[q],
                    &t->capacity[q], d, pt, t->values[p][i]);
      if (st != DS_OK) {
        return st;
      }
    } else {
      if (keep != i) {
        memmove(t->points[p] + keep * d, pt, d * sizeof(double));
        t->values[p][keep] = t->values[p][i];
      }
      keep++;
    }
  }
  t->count[p] = keep;
  return DS_OK;
}

/* hB: extract a brick of between one and two thirds of page p's points. */
static ds_status_t hb_split_holey(ds_hb_tree_t *t, size_t p,
                                  const double *icell, hb_leafset_t *ls) {
  size_t d = t->dim;
  size_t n = t->count[p];
  size_t i;
  size_t j;
  size_t ncut = 0;
  size_t cut_axis[64];
  double cut_x[64];
  unsigned char cut_hi[64];
  unsigned char *in;
  double *vals;
  size_t cur = n;
  size_t owner = t->owner[p];
  size_t q;
  ds_status_t st;
  in = (unsigned char *)ds_context_alloc(t->context, n);
  vals = (double *)ds_context_alloc(t->context, n * sizeof(double));
  if (in == NULL || vals == NULL) {
    ds_context_free(t->context, in);
    ds_context_free(t->context, vals);
    return DS_ERR_ALLOC;
  }
  memset(in, 1, n);
  while (cur * 3 > n * 2 && ncut < 64) {
    size_t axis = d;
    size_t m = 0;
    size_t lo_n = 0;
    double best = 0.0;
    double x;
    size_t a;
    for (a = 0; a < d; a++) {
      double lo = HUGE_VAL;
      double hi = -HUGE_VAL;
      for (i = 0; i < n; i++) {
        if (in[i]) {
          double c = t->points[p][i * d + a];
          lo = c < lo ? c : lo;
          hi = c > hi ? c : hi;
        }
      }
      if (hi - lo > best) {
        best = hi - lo;
        axis = a;
      }
    }
    if (axis == d) {
      break;
    }
    for (i = 0; i < n; i++) {
      if (in[i]) {
        vals[m++] = t->points[p][i * d + axis];
      }
    }
    qsort(vals, m, sizeof(double), hb_cmp_double);
    x = vals[m / 2];
    if (!(x > vals[0])) {
      for (j = 1; j < m && !(vals[j] > vals[0]); j++) {
      }
      x = vals[j];
    }
    for (i = 0; i < m; i++) {
      lo_n += vals[i] < x;
    }
    cut_axis[ncut] = axis;
    cut_x[ncut] = x;
    cut_hi[ncut] = (unsigned char)(lo_n * 2 < m);
    for (i = 0; i < n; i++) {
      if (in[i] && (t->points[p][i * d + axis] < x) == (cut_hi[ncut] != 0)) {
        in[i] = 0;
      }
    }
    cur = cut_hi[ncut] ? m - lo_n : lo_n;
    ncut++;
  }
  ds_context_free(t->context, vals);
  if (ncut == 0 || cur == 0) {
    ds_context_free(t->context, in);
    return DS_EXISTS; /* coinciding points */
  }
  st = hb_page(t, owner, &q);
  if (st == DS_OK) {
    /* move the brick's points */
    size_t keep = 0;
    for (i = 0; i < n && st == DS_OK; i++) {
      if (in[i]) {
        st = hb_append(t->context, &t->points[q], &t->values[q], &t->count[q],
                       &t->capacity[q], d, t->points[p] + i * d,
                       t->values[p][i]);
      }
    }
    for (i = 0; i < n && st == DS_OK; i++) {
      if (!in[i]) {
        if (keep != i) {
          memmove(t->points[p] + keep * d, t->points[p] + i * d,
                  d * sizeof(double));
          t->values[p][keep] = t->values[p][i];
        }
        keep++;
      }
    }
    if (st == DS_OK) {
      t->count[p] = keep;
    }
  }
  ds_context_free(t->context, in);
  if (st != DS_OK) {
    return st;
  }
  /* carve the brick out of every leaf of p */
  st = hb_leaves(t, owner, icell, ls);
  for (i = 0; i < ls->n && st == DS_OK; i++) {
    size_t v = ls->leaf[i];
    double c[2 * HB_MAXD];
    bool inside = true;
    if (!t->kpage[v] || t->ktarget[v] != p) {
      continue;
    }
    memcpy(c, ls->cell + i * 2 * d, 2 * d * sizeof(double));
    for (j = 0; j < ncut && st == DS_OK; j++) {
      size_t a = cut_axis[j];
      double x = cut_x[j];
      size_t lo;
      size_t hi;
      if (!(x > c[a])) { /* the cell is wholly on the high side */
        if (!cut_hi[j]) {
          inside = false;
          break;
        }
        continue;
      }
      if (!(x < c[d + a])) { /* wholly on the low side */
        if (cut_hi[j]) {
          inside = false;
          break;
        }
        continue;
      }
      st = hb_cut_leaf(t, v, a, x, &lo, &hi);
      if (cut_hi[j]) {
        v = hi;
        c[a] = x;
      } else {
        v = lo;
        c[d + a] = x;
      }
    }
    if (st == DS_OK && inside) {
      t->ktarget[v] = q;
    }
  }
  return st;
}

/* buddy: halve page p's cell until both halves hold points */
static ds_status_t hb_split_buddy(ds_hb_tree_t *t, size_t v,
                                  const double *cell) {
  size_t d = t->dim;
  size_t p = t->ktarget[v];
  double c[2 * HB_MAXD];
  size_t rounds;
  ds_status_t st = DS_OK;
  size_t a;
  /* the domain bounds the halving where the cell is unbounded */
  for (a = 0; a < d; a++) {
    c[a] = cell[a] > t->domain[a] ? cell[a] : t->domain[a];
    c[d + a] = cell[d + a] < t->domain[d + a] ? cell[d + a] : t->domain[d + a];
  }
  for (rounds = 0; rounds < HB_B * d; rounds++) {
    size_t axis = 0;
    double rel = -1.0;
    double x;
    size_t lo_n = 0;
    size_t i;
    size_t lo;
    size_t hi;
    size_t q;
    for (a = 0; a < d; a++) {
      double r = (c[d + a] - c[a]) / (t->domain[d + a] - t->domain[a]);
      if (r > rel * (1.0 + 1e-9)) {
        rel = r;
        axis = a;
      }
    }
    x = c[axis] + (c[d + axis] - c[axis]) / 2.0;
    if (!(x > c[axis]) || !(x < c[d + axis])) {
      return DS_EXISTS;
    }
    for (i = 0; i < t->count[p]; i++) {
      lo_n += t->points[p][i * d + axis] < x;
    }
    if (lo_n == 0 || lo_n == t->count[p]) {
      /* one half is empty: halve the other one, without a node; the
       * bounding boxes stand for the empty space */
      if (lo_n == 0) {
        c[axis] = x;
      } else {
        c[d + axis] = x;
      }
      continue;
    }
    st = hb_cut_leaf(t, v, axis, x, &lo, &hi);
    if (st != DS_OK) {
      return st;
    }
    st = hb_page(t, t->owner[p], &q);
    if (st == DS_OK) {
      st = hb_page_split_at(t, p, q, axis, x);
    }
    if (st != DS_OK) {
      return st;
    }
    t->ktarget[lo] = p;
    t->ktarget[hi] = q;
    hb_page_mbb(t, p, t->kmbb + lo * 2 * d);
    hb_page_mbb(t, q, t->kmbb + hi * 2 * d);
    return DS_OK;
  }
  return DS_EXISTS;
}

/* Split index node i at its k-d root; returns the new sibling. */
static ds_status_t hb_split_index(ds_hb_tree_t *t, size_t i, size_t *out_j) {
  size_t d = t->dim;
  size_t r = t->iroot[i];
  size_t axis = t->kaxis[r];
  double x = t->kvalue[r];
  size_t j;
  size_t *stack;
  size_t top = 0;
  size_t *pairs = NULL; /* (old page, new page) for pages cut in two */
  size_t npairs = 0;
  size_t cap = 2 * t->ileaves[i] + 4;
  size_t lo;
  size_t hi;
  ds_status_t st = hb_inode(t, t->ilevel[i], &j);
  if (st != DS_OK) {
    return st;
  }
  stack = (size_t *)ds_context_alloc(t->context, cap * sizeof(size_t));
  pairs = (size_t *)ds_context_alloc(t->context, cap * sizeof(size_t));
  if (stack == NULL || pairs == NULL) {
    ds_context_free(t->context, stack);
    ds_context_free(t->context, pairs);
    return DS_ERR_ALLOC;
  }
  t->iroot[i] = t->klo[r];
  t->iroot[j] = t->khi[r];
  hb_kfree(t, r);
  /* mark the pages of the low side */
  if (++t->epoch == 0) {
    size_t p;
    for (p = 0; p < t->pages; p++) {
      t->stamp[p] = 0;
    }
    t->epoch = 1;
  }
  t->ileaves[i] = 0;
  stack[top++] = t->iroot[i];
  while (top > 0) {
    size_t v = stack[--top];
    if (t->kaxis[v] != KLEAF) {
      stack[top++] = t->klo[v];
      stack[top++] = t->khi[v];
      continue;
    }
    t->ileaves[i]++;
    if (t->kpage[v] && t->ktarget[v] != HB_NONE) {
      t->stamp[t->ktarget[v]] = t->epoch;
    }
  }
  /* the high side moves to j; pages on both sides are cut */
  t->ileaves[j] = 0;
  stack[top++] = t->iroot[j];
  while (top > 0 && st == DS_OK) {
    size_t v = stack[--top];
    size_t p;
    size_t k;
    t->kowner[v] = j;
    if (t->kaxis[v] != KLEAF) {
      stack[top++] = t->klo[v];
      stack[top++] = t->khi[v];
      continue;
    }
    t->ileaves[j]++;
    p = t->ktarget[v];
    if (!t->kpage[v]) {
      continue;
    }
    if (p == HB_NONE) {
      continue;
    }
    if (t->stamp[p] != t->epoch) {
      t->owner[p] = j;
      continue;
    }
    for (k = 0; k < npairs && pairs[2 * k] != p; k++) {
    }
    if (k == npairs) {
      size_t q;
      st = hb_page(t, j, &q);
      if (st == DS_OK) {
        st = hb_page_split_at(t, p, q, axis, x);
      }
      if (st != DS_OK) {
        break;
      }
      pairs[2 * npairs] = p;
      pairs[2 * npairs + 1] = q;
      npairs++;
    }
    t->ktarget[v] = pairs[2 * k + 1];
  }
  ds_context_free(t->context, pairs);
  if (st != DS_OK) {
    ds_context_free(t->context, stack);
    return st;
  }
  /* link into the parent */
  if (i == t->root) {
    size_t root;
    size_t leaf = 0;
    st = hb_inode(t, t->ilevel[i] + 1, &root);
    if (st == DS_OK) {
      st = hb_knode(t, root, &leaf);
    }
    if (st != DS_OK) {
      ds_context_free(t->context, stack);
      return st;
    }
    t->iroot[root] = leaf;
    t->ileaves[root] = 1;
    t->ktarget[leaf] = i;
    t->kpage[leaf] = 0;
    t->iparent[i] = leaf;
    t->root = root;
    t->height++;
  }
  st = hb_cut_leaf(t, t->iparent[i], axis, x, &lo, &hi);
  if (st == DS_OK) {
    t->ktarget[lo] = i;
    t->ktarget[hi] = j;
    t->iparent[i] = lo;
    t->iparent[j] = hi;
    if (t->mode == DS_HB_BUDDY) {
      size_t need =
          2 * (t->ileaves[i] > t->ileaves[j] ? t->ileaves[i] : t->ileaves[j]) +
          4;
      if (need > cap) {
        void *p = ds_context_realloc(t->context, stack, need * sizeof(size_t));
        if (p == NULL) {
          ds_context_free(t->context, stack);
          return DS_ERR_ALLOC;
        }
        stack = (size_t *)p;
      }
      hb_index_mbb(t, i, t->kmbb + lo * 2 * d, stack);
      hb_index_mbb(t, j, t->kmbb + hi * 2 * d, stack);
    }
    *out_j = j;
  }
  ds_context_free(t->context, stack);
  return st;
}

/* Split overfull index nodes from i upwards. */
static ds_status_t hb_fix_index(ds_hb_tree_t *t, size_t i) {
  size_t todo[3 * 64];
  size_t n = 0;
  ds_status_t st = DS_OK;
  todo[n++] = i;
  while (n > 0 && st == DS_OK) {
    size_t x = todo[--n];
    size_t j;
    if (t->ileaves[x] <= t->fanout) {
      continue;
    }
    st = hb_split_index(t, x, &j);
    if (st != DS_OK) {
      break;
    }
    if (n + 3 > sizeof(todo) / sizeof(todo[0])) {
      n = 0; /* pathological: give up balancing here, stay correct */
    }
    todo[n++] = t->kowner[t->iparent[x]];
    todo[n++] = j;
    todo[n++] = x;
  }
  return st;
}

ds_status_t ds_hb_tree_insert(ds_hb_tree_t *t, const double *point,
                              size_t value) {
  size_t d;
  size_t v;
  size_t p;
  size_t path[256];
  size_t plen = 0;
  size_t k;
  double cell[2 * HB_MAXD];
  double icell[2 * HB_MAXD];
  ds_status_t st;
  if (t == NULL || point == NULL) {
    return DS_ERR_NULL;
  }
  d = t->dim;
  if (!hb_finite(point, d)) {
    return DS_ERR_INVALID;
  }
  if (t->height > 256) {
    return DS_ERR_OVERFLOW;
  }
  v = hb_descend(t, point, cell, icell, path, &plen);
  p = t->ktarget[v];
  if (p == HB_NONE) {
    st = hb_page(t, t->kowner[v], &p);
    if (st != DS_OK) {
      return st;
    }
    t->ktarget[v] = p;
  }
  st = hb_append(t->context, &t->points[p], &t->values[p], &t->count[p],
                 &t->capacity[p], d, point, value);
  if (st != DS_OK) {
    return st;
  }
  t->size++;
  if (t->mode == DS_HB_BUDDY) {
    for (k = 0; k < plen; k++) {
      hb_extend(t->kmbb + path[k] * 2 * d, point, d, true);
    }
  }
  if (t->count[p] <= t->bucket) {
    return DS_OK;
  }
  if (t->mode == DS_HB_HOLEY) {
    hb_leafset_t ls;
    memset(&ls, 0, sizeof(ls));
    st = hb_split_holey(t, p, icell, &ls);
    ds_context_free(t->context, ls.leaf);
    ds_context_free(t->context, ls.cell);
  } else {
    st = hb_split_buddy(t, v, cell);
  }
  if (st == DS_EXISTS) {
    return DS_OK; /* coinciding points: an oversized page */
  }
  if (st != DS_OK) {
    return st;
  }
  return hb_fix_index(t, t->owner[p]);
}

ds_status_t ds_hb_tree_remove(ds_hb_tree_t *t, const double *point,
                              size_t value) {
  size_t d;
  size_t v;
  size_t p;
  size_t i;
  size_t path[256];
  size_t plen = 0;
  double cell[2 * HB_MAXD];
  if (t == NULL || point == NULL) {
    return DS_ERR_NULL;
  }
  d = t->dim;
  if (!hb_finite(point, d)) {
    return DS_NOT_FOUND;
  }
  v = hb_descend(t, point, cell, NULL, path, &plen);
  p = t->ktarget[v];
  if (p == HB_NONE) {
    return DS_NOT_FOUND;
  }
  i = hb_find(t->points[p], t->values[p], t->count[p], d, point, value);
  if (i == HB_NONE) {
    return DS_NOT_FOUND;
  }
  hb_drop(t->points[p], t->values[p], &t->count[p], d, i);
  t->size--;
  if (t->mode == DS_HB_BUDDY) {
    size_t *stack;
    size_t k;
    hb_page_mbb(t, p, t->kmbb + v * 2 * d);
    stack = (size_t *)ds_context_alloc(t->context,
                                       (2 * t->fanout + 8) * sizeof(size_t));
    if (stack == NULL) {
      return DS_ERR_ALLOC; /* the point is gone; boxes stay conservative */
    }
    for (k = plen - 1; k-- > 0;) {
      size_t c = t->ktarget[path[k]];
      if (2 * t->ileaves[c] + 4 > 2 * t->fanout + 8) {
        void *q = ds_context_realloc(t->context, stack,
                                     (2 * t->ileaves[c] + 4) * sizeof(size_t));
        if (q == NULL) {
          break;
        }
        stack = (size_t *)q;
      }
      hb_index_mbb(t, c, t->kmbb + path[k] * 2 * d, stack);
    }
    ds_context_free(t->context, stack);
  }
  return DS_OK;
}

bool ds_hb_tree_contains(const ds_hb_tree_t *t, const double *point) {
  size_t v;
  size_t p;
  size_t i;
  double cell[2 * HB_MAXD];
  if (t == NULL || point == NULL || !hb_finite(point, t->dim)) {
    return false;
  }
  v = hb_descend(t, point, cell, NULL, NULL, NULL);
  p = t->ktarget[v];
  if (p == HB_NONE) {
    return false;
  }
  for (i = 0; i < t->count[p]; i++) {
    if (memcmp(t->points[p] + i * t->dim, point, t->dim * sizeof(double)) ==
        0) {
      return true;
    }
  }
  return false;
}

static void hb_new_epoch(ds_hb_tree_t *t) {
  if (++t->epoch == 0) {
    size_t p;
    for (p = 0; p < t->pages; p++) {
      t->stamp[p] = 0;
    }
    t->epoch = 1;
  }
}

ds_status_t ds_hb_tree_query(ds_hb_tree_t *t, const double *box,
                             ds_hb_visit_func_t visit, void *user) {
  size_t d;
  size_t *stack;
  double *cells;
  size_t top = 0;
  size_t a;
  ds_status_t st = DS_OK;
  if (t == NULL || box == NULL || visit == NULL) {
    return DS_ERR_NULL;
  }
  d = t->dim;
  stack =
      (size_t *)ds_context_alloc(t->context, (t->knodes + 2) * sizeof(size_t));
  cells = (double *)ds_context_alloc(t->context,
                                     (t->knodes + 2) * 2 * d * sizeof(double));
  if (stack == NULL || cells == NULL) {
    ds_context_free(t->context, stack);
    ds_context_free(t->context, cells);
    return DS_ERR_ALLOC;
  }
  hb_new_epoch(t);
  stack[0] = t->iroot[t->root];
  for (a = 0; a < d; a++) {
    cells[a] = -HUGE_VAL;
    cells[d + a] = HUGE_VAL;
  }
  top = 1;
  while (top > 0 && st == DS_OK) {
    size_t v;
    double c[2 * HB_MAXD];
    top--;
    v = stack[top];
    memcpy(c, cells + top * 2 * d, 2 * d * sizeof(double));
    if (!hb_box_meets_cell(box, c, d)) {
      continue;
    }
    if (t->kaxis[v] != KLEAF) {
      stack[top] = t->klo[v];
      memcpy(cells + top * 2 * d, c, 2 * d * sizeof(double));
      cells[top * 2 * d + d + t->kaxis[v]] = t->kvalue[v];
      top++;
      stack[top] = t->khi[v];
      memcpy(cells + top * 2 * d, c, 2 * d * sizeof(double));
      cells[top * 2 * d + t->kaxis[v]] = t->kvalue[v];
      top++;
      continue;
    }
    if (t->ktarget[v] == HB_NONE ||
        (t->mode == DS_HB_BUDDY && !hb_box_hit(t->kmbb + v * 2 * d, box, d))) {
      continue;
    }
    if (!t->kpage[v]) {
      stack[top] = t->iroot[t->ktarget[v]];
      memcpy(cells + top * 2 * d, c, 2 * d * sizeof(double));
      top++;
    } else {
      size_t p = t->ktarget[v];
      size_t i;
      if (t->stamp[p] == t->epoch) {
        continue;
      }
      t->stamp[p] = t->epoch;
      for (i = 0; i < t->count[p]; i++) {
        const double *pt = t->points[p] + i * d;
        if (hb_point_in_box(pt, box, d) && !visit(pt, t->values[p][i], user)) {
          st = DS_STOP;
          break;
        }
      }
    }
  }
  ds_context_free(t->context, stack);
  ds_context_free(t->context, cells);
  return st;
}

ds_status_t ds_hb_tree_nearest(ds_hb_tree_t *t, const double *point, size_t k,
                               size_t *out_values, double *out_dist,
                               size_t *out_found) {
  size_t d;
  size_t *stack;
  double *cells;
  double *key;
  double *dk;
  size_t *vk;
  size_t top;
  size_t found = 0;
  size_t i;
  size_t a;
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
      (size_t *)ds_context_alloc(t->context, (t->knodes + 2) * sizeof(size_t));
  cells = (double *)ds_context_alloc(t->context,
                                     (t->knodes + 2) * 2 * d * sizeof(double));
  key =
      (double *)ds_context_alloc(t->context, (t->knodes + 2) * sizeof(double));
  dk = (double *)ds_context_alloc(t->context, k * sizeof(double));
  vk = (size_t *)ds_context_alloc(t->context, k * sizeof(size_t));
  if (stack == NULL || cells == NULL || key == NULL || dk == NULL ||
      vk == NULL) {
    ds_context_free(t->context, stack);
    ds_context_free(t->context, cells);
    ds_context_free(t->context, key);
    ds_context_free(t->context, dk);
    ds_context_free(t->context, vk);
    return DS_ERR_ALLOC;
  }
  hb_new_epoch(t);
  stack[0] = t->iroot[t->root];
  key[0] = 0.0;
  for (a = 0; a < d; a++) {
    cells[a] = -HUGE_VAL;
    cells[d + a] = HUGE_VAL;
  }
  top = 1;
  while (top > 0) {
    size_t v;
    double c[2 * HB_MAXD];
    size_t base;
    top--;
    v = stack[top];
    if (found == k && !(key[top] < dk[k - 1])) {
      continue;
    }
    memcpy(c, cells + top * 2 * d, 2 * d * sizeof(double));
    base = top;
    if (t->kaxis[v] != KLEAF) {
      size_t side;
      for (side = 0; side < 2; side++) {
        double cc[2 * HB_MAXD];
        size_t child = side ? t->khi[v] : t->klo[v];
        double s;
        size_t j;
        memcpy(cc, c, 2 * d * sizeof(double));
        if (side) {
          cc[t->kaxis[v]] = t->kvalue[v];
        } else {
          cc[d + t->kaxis[v]] = t->kvalue[v];
        }
        s = hb_mindist2(cc, point, d);
        if (t->kaxis[child] == KLEAF && t->mode == DS_HB_BUDDY) {
          double m = hb_mindist2(t->kmbb + child * 2 * d, point, d);
          s = m > s ? m : s;
        }
        for (j = top; j > base && key[j - 1] < s; j--) {
          key[j] = key[j - 1];
          stack[j] = stack[j - 1];
          memcpy(cells + j * 2 * d, cells + (j - 1) * 2 * d,
                 2 * d * sizeof(double));
        }
        key[j] = s;
        stack[j] = child;
        memcpy(cells + j * 2 * d, cc, 2 * d * sizeof(double));
        top++;
      }
      continue;
    }
    if (t->ktarget[v] == HB_NONE) {
      continue;
    }
    if (!t->kpage[v]) {
      stack[top] = t->iroot[t->ktarget[v]];
      memcpy(cells + top * 2 * d, c, 2 * d * sizeof(double));
      top++;
      continue;
    }
    {
      size_t p = t->ktarget[v];
      if (t->stamp[p] == t->epoch) {
        continue;
      }
      t->stamp[p] = t->epoch;
      for (i = 0; i < t->count[p]; i++) {
        const double *pt = t->points[p] + i * d;
        double s = 0.0;
        for (a = 0; a < d; a++) {
          s += (pt[a] - point[a]) * (pt[a] - point[a]);
        }
        hb_best_push(dk, vk, k, &found, s, t->values[p][i]);
      }
    }
  }
  for (i = 0; i < found; i++) {
    out_values[i] = vk[i];
    out_dist[i] = sqrt(dk[i]);
  }
  *out_found = found;
  ds_context_free(t->context, stack);
  ds_context_free(t->context, cells);
  ds_context_free(t->context, key);
  ds_context_free(t->context, dk);
  ds_context_free(t->context, vk);
  return DS_OK;
}

bool ds_hb_tree_valid(const ds_hb_tree_t *t) {
  size_t d;
  size_t p;
  size_t total = 0;
  size_t v;
  size_t *leaves;
  size_t *stack;
  bool ok = true;
  if (t == NULL || t->iroot == NULL) {
    return false;
  }
  d = t->dim;
  for (p = 0; p < t->pages && ok; p++) {
    size_t i;
    if (!t->alive[p]) {
      continue;
    }
    for (i = 0; i < t->count[p] && ok; i++) {
      double cell[2 * HB_MAXD];
      size_t leaf = hb_descend(t, t->points[p] + i * d, cell, NULL, NULL, NULL);
      ok = t->ktarget[leaf] == p;
    }
    total += t->count[p];
  }
  if (!ok || total != t->size) {
    return false;
  }
  /* owners, leaf counts and bounding boxes */
  leaves =
      (size_t *)ds_context_calloc(t->context, t->inodes + 1, sizeof(size_t));
  stack =
      (size_t *)ds_context_alloc(t->context, (t->knodes + 2) * sizeof(size_t));
  if (leaves == NULL || stack == NULL) {
    ds_context_free(t->context, leaves);
    ds_context_free(t->context, stack);
    return false;
  }
  {
    size_t top = 0;
    stack[top++] = t->iroot[t->root];
    while (top > 0 && ok) {
      v = stack[--top];
      if (t->kaxis[v] != KLEAF) {
        ok = t->kowner[t->klo[v]] == t->kowner[v] &&
             t->kowner[t->khi[v]] == t->kowner[v];
        stack[top++] = t->klo[v];
        stack[top++] = t->khi[v];
        continue;
      }
      leaves[t->kowner[v]]++;
      if (t->ktarget[v] == HB_NONE) {
        continue;
      }
      if (t->kpage[v]) {
        ok = t->owner[t->ktarget[v]] == t->kowner[v] &&
             t->ilevel[t->kowner[v]] == 1;
        if (ok && t->mode == DS_HB_BUDDY) {
          double b[2 * HB_MAXD];
          hb_page_mbb(t, t->ktarget[v], b);
          ok = memcmp(b, t->kmbb + v * 2 * d, 2 * d * sizeof(double)) == 0;
        }
      } else {
        size_t c = t->ktarget[v];
        ok = t->iparent[c] == v && t->ilevel[c] + 1 == t->ilevel[t->kowner[v]];
        stack[top++] = t->iroot[c];
      }
    }
  }
  if (ok && t->mode == DS_HB_BUDDY) {
    /* index leaves: union of the child's leaves */
    size_t *s2 = (size_t *)ds_context_alloc(t->context,
                                            (t->knodes + 2) * sizeof(size_t));
    if (s2 == NULL) {
      ok = false;
    }
    for (v = 0; v < t->knodes && ok; v++) {
      double b[2 * HB_MAXD];
      size_t i;
      bool live = false;
      for (i = 0; i < t->inodes && !live; i++) {
        live = t->iparent[i] == v && t->ktarget[v] == i && !t->kpage[v];
      }
      if (!live) {
        continue;
      }
      hb_index_mbb(t, t->ktarget[v], b, s2);
      ok = memcmp(b, t->kmbb + v * 2 * d, 2 * d * sizeof(double)) == 0;
    }
    ds_context_free(t->context, s2);
  }
  for (v = 0; v < t->inodes && ok; v++) {
    if (leaves[v] > 0) {
      ok = leaves[v] == t->ileaves[v];
    }
  }
  ds_context_free(t->context, leaves);
  ds_context_free(t->context, stack);
  return ok;
}

/* ---- BD-tree
 * -------------------------------------------------------------------------------
 */

static ds_status_t bd_node(ds_bd_tree_t *t, size_t *out) {
  size_t v;
  if (t->free_list != HB_NONE) {
    v = t->free_list;
    t->free_list = t->out[v];
  } else {
    if (t->nodes == t->node_capacity) {
      size_t nc = t->node_capacity < 8 ? 8 : t->node_capacity * 2;
      if (hb_grow(t->context, (void **)&t->dz_depth, sizeof(size_t), nc) ||
          hb_grow(t->context, (void **)&t->dz, t->dim * sizeof(unsigned long),
                  nc) ||
          hb_grow(t->context, (void **)&t->in, sizeof(size_t), nc) ||
          hb_grow(t->context, (void **)&t->out, sizeof(size_t), nc) ||
          hb_grow(t->context, (void **)&t->parent, sizeof(size_t), nc) ||
          hb_grow(t->context, (void **)&t->points, sizeof(double *), nc) ||
          hb_grow(t->context, (void **)&t->values, sizeof(size_t *), nc) ||
          hb_grow(t->context, (void **)&t->count, sizeof(size_t), nc) ||
          hb_grow(t->context, (void **)&t->capacity, sizeof(size_t), nc)) {
        return DS_ERR_ALLOC;
      }
      t->node_capacity = nc;
    }
    v = t->nodes++;
  }
  t->dz_depth[v] = 0;
  memset(t->dz + v * t->dim, 0, t->dim * sizeof(unsigned long));
  t->in[v] = t->out[v] = HB_NONE;
  t->parent[v] = HB_NONE;
  t->points[v] = NULL;
  t->values[v] = NULL;
  t->count[v] = t->capacity[v] = 0;
  *out = v;
  return DS_OK;
}

static void bd_free(ds_bd_tree_t *t, size_t v) {
  ds_context_free(t->context, t->points[v]);
  ds_context_free(t->context, t->values[v]);
  t->points[v] = NULL;
  t->values[v] = NULL;
  t->count[v] = t->capacity[v] = 0;
  t->in[v] = HB_NONE;
  t->out[v] = t->free_list;
  t->free_list = v;
}

ds_status_t ds_bd_tree_init(ds_bd_tree_t *t, size_t dim, const double *domain,
                            size_t bucket, ds_context_t *context) {
  ds_status_t st;
  if (t == NULL) {
    return DS_ERR_NULL;
  }
  if (bucket == 0) {
    bucket = 16;
  }
  if (dim == 0 || dim > HB_MAXD || bucket < 2) {
    return DS_ERR_INVALID;
  }
  st = hb_domain_ok(dim, domain);
  if (st != DS_OK) {
    return st;
  }
  memset(t, 0, sizeof(*t));
  t->dim = dim;
  t->bucket = bucket;
  memcpy(t->domain, domain, 2 * dim * sizeof(double));
  t->free_list = HB_NONE;
  t->context = context;
  st = bd_node(t, &t->root);
  if (st != DS_OK) {
    ds_bd_tree_destroy(t);
  }
  return st;
}

void ds_bd_tree_destroy(ds_bd_tree_t *t) {
  size_t v;
  if (t == NULL) {
    return;
  }
  for (v = 0; v < t->nodes; v++) {
    ds_context_free(t->context, t->points[v]);
    ds_context_free(t->context, t->values[v]);
  }
  ds_context_free(t->context, t->dz_depth);
  ds_context_free(t->context, t->dz);
  ds_context_free(t->context, t->in);
  ds_context_free(t->context, t->out);
  ds_context_free(t->context, t->parent);
  ds_context_free(t->context, t->points);
  ds_context_free(t->context, t->values);
  ds_context_free(t->context, t->count);
  ds_context_free(t->context, t->capacity);
  memset(t, 0, sizeof(*t));
}

size_t ds_bd_tree_size(const ds_bd_tree_t *t) {
  return t == NULL ? 0 : t->size;
}

size_t ds_bd_tree_leaves(const ds_bd_tree_t *t) {
  size_t v;
  size_t n = 0;
  if (t == NULL) {
    return 0;
  }
  /* walk the tree: free nodes are not reachable */
  {
    size_t *stack =
        (size_t *)ds_context_alloc(t->context, (t->nodes + 1) * sizeof(size_t));
    size_t top = 0;
    if (stack == NULL) {
      return 0;
    }
    stack[top++] = t->root;
    while (top > 0) {
      v = stack[--top];
      if (t->in[v] == HB_NONE) {
        n++;
      } else {
        stack[top++] = t->in[v];
        stack[top++] = t->out[v];
      }
    }
    ds_context_free(t->context, stack);
  }
  return n;
}

static size_t bd_leaf(const ds_bd_tree_t *t, const unsigned long *q,
                      size_t *base) {
  size_t v = t->root;
  *base = 0;
  while (t->in[v] != HB_NONE) {
    if (hb_dz_match(q, t->dz + v * t->dim, t->dz_depth[v], t->dim)) {
      *base = t->dz_depth[v];
      v = t->in[v];
    } else {
      v = t->out[v];
    }
  }
  return v;
}

ds_status_t ds_bd_tree_insert(ds_bd_tree_t *t, const double *point,
                              size_t value) {
  size_t d;
  size_t v;
  size_t base;
  unsigned long q[HB_MAXD];
  ds_status_t st;
  if (t == NULL || point == NULL) {
    return DS_ERR_NULL;
  }
  d = t->dim;
  if (!hb_finite(point, d)) {
    return DS_ERR_INVALID;
  }
  hb_quantise(t->domain, d, point, q);
  v = bd_leaf(t, q, &base);
  st = hb_append(t->context, &t->points[v], &t->values[v], &t->count[v],
                 &t->capacity[v], d, point, value);
  if (st != DS_OK) {
    return st;
  }
  t->size++;
  while (t->count[v] > t->bucket) {
    size_t n = t->count[v];
    unsigned long *keys;
    unsigned char *in;
    unsigned char *best_in;
    size_t depth;
    size_t i;
    size_t a = 0;
    size_t b = 0;
    keys = (unsigned long *)ds_context_alloc(t->context,
                                             n * d * sizeof(unsigned long));
    in = (unsigned char *)ds_context_alloc(t->context, n);
    best_in = (unsigned char *)ds_context_alloc(t->context, n);
    if (keys == NULL || in == NULL || best_in == NULL) {
      ds_context_free(t->context, keys);
      ds_context_free(t->context, in);
      ds_context_free(t->context, best_in);
      return DS_ERR_ALLOC;
    }
    for (i = 0; i < n; i++) {
      hb_quantise(t->domain, d, t->points[v] + i * d, keys + i * d);
    }
    depth = hb_carve(keys, NULL, n, d, base, in, best_in);
    st = DS_OK;
    if (depth > 0) {
      st = bd_node(t, &a);
      if (st == DS_OK) {
        st = bd_node(t, &b);
        if (st != DS_OK) {
          bd_free(t, a);
        }
      }
    }
    if (depth > 0 && st == DS_OK) {
      /* a: the split-off sub-cell; b: the rest (takes v's bucket) */
      for (i = 0; i < n && !best_in[i]; i++) {
      }
      hb_dz_of(keys + i * d, depth, d, t->dz + v * d);
      t->dz_depth[v] = depth;
      t->points[b] = t->points[v];
      t->values[b] = t->values[v];
      t->count[b] = t->count[v];
      t->capacity[b] = t->capacity[v];
      t->points[v] = NULL;
      t->values[v] = NULL;
      t->count[v] = t->capacity[v] = 0;
      t->in[v] = a;
      t->out[v] = b;
      t->parent[a] = t->parent[b] = v;
      {
        size_t keep = 0;
        for (i = 0; i < n && st == DS_OK; i++) {
          if (best_in[i]) {
            st = hb_append(t->context, &t->points[a], &t->values[a],
                           &t->count[a], &t->capacity[a], d,
                           t->points[b] + i * d, t->values[b][i]);
          }
        }
        for (i = 0; i < n && st == DS_OK; i++) {
          if (!best_in[i]) {
            if (keep != i) {
              memmove(t->points[b] + keep * d, t->points[b] + i * d,
                      d * sizeof(double));
              t->values[b][keep] = t->values[b][i];
            }
            keep++;
          }
        }
        if (st == DS_OK) {
          t->count[b] = keep;
        }
      }
    }
    ds_context_free(t->context, keys);
    ds_context_free(t->context, in);
    ds_context_free(t->context, best_in);
    if (st != DS_OK) {
      return st;
    }
    if (depth == 0) {
      break; /* coinciding keys: an oversized bucket */
    }
    if (t->count[a] > t->count[b]) {
      v = a;
      base = depth;
    } else {
      v = b;
    }
  }
  return DS_OK;
}

ds_status_t ds_bd_tree_remove(ds_bd_tree_t *t, const double *point,
                              size_t value) {
  size_t d;
  size_t v;
  size_t u;
  size_t base;
  size_t i;
  unsigned long q[HB_MAXD];
  if (t == NULL || point == NULL) {
    return DS_ERR_NULL;
  }
  d = t->dim;
  if (!hb_finite(point, d)) {
    return DS_NOT_FOUND;
  }
  hb_quantise(t->domain, d, point, q);
  v = bd_leaf(t, q, &base);
  i = hb_find(t->points[v], t->values[v], t->count[v], d, point, value);
  if (i == HB_NONE) {
    return DS_NOT_FOUND;
  }
  hb_drop(t->points[v], t->values[v], &t->count[v], d, i);
  t->size--;
  /* collapse a node whose two leaves fit one bucket */
  u = t->parent[v];
  while (u != HB_NONE) {
    size_t a = t->in[u];
    size_t b = t->out[u];
    size_t need;
    void *p;
    if (t->in[a] != HB_NONE || t->in[b] != HB_NONE ||
        t->count[a] + t->count[b] > t->bucket) {
      break;
    }
    need = t->count[a] + t->count[b];
    if (need > t->capacity[b]) {
      p = ds_context_realloc(t->context, t->points[b],
                             need * d * sizeof(double));
      if (p == NULL) {
        break;
      }
      t->points[b] = (double *)p;
      p = ds_context_realloc(t->context, t->values[b], need * sizeof(size_t));
      if (p == NULL) {
        break;
      }
      t->values[b] = (size_t *)p;
      t->capacity[b] = need;
    }
    if (t->count[a] > 0) {
      memcpy(t->points[b] + t->count[b] * d, t->points[a],
             t->count[a] * d * sizeof(double));
      memcpy(t->values[b] + t->count[b], t->values[a],
             t->count[a] * sizeof(size_t));
    }
    t->points[u] = t->points[b];
    t->values[u] = t->values[b];
    t->count[u] = need;
    t->capacity[u] = t->capacity[b];
    t->points[b] = NULL;
    t->values[b] = NULL;
    t->in[u] = t->out[u] = HB_NONE;
    t->dz_depth[u] = 0;
    bd_free(t, a);
    bd_free(t, b);
    u = t->parent[u];
  }
  return DS_OK;
}

ds_status_t ds_bd_tree_query(const ds_bd_tree_t *t, const double *box,
                             ds_hb_visit_func_t visit, void *user) {
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
    if (t->in[v] != HB_NONE) {
      double c[2 * HB_MAXD];
      hb_dz_box(t->domain, d, t->dz + v * d, t->dz_depth[v], c);
      if (hb_box_hit(c, box, d)) {
        stack[top++] = t->in[v];
      }
      stack[top++] = t->out[v];
    } else {
      size_t i;
      for (i = 0; i < t->count[v]; i++) {
        const double *p = t->points[v] + i * d;
        if (hb_point_in_box(p, box, d) && !visit(p, t->values[v][i], user)) {
          st = DS_STOP;
          break;
        }
      }
    }
  }
  ds_context_free(t->context, stack);
  return st;
}

bool ds_bd_tree_valid(const ds_bd_tree_t *t) {
  size_t d;
  size_t *stack;
  size_t top = 0;
  size_t total = 0;
  bool ok = true;
  if (t == NULL || t->in == NULL) {
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
    if (t->in[v] != HB_NONE) {
      ok = t->parent[t->in[v]] == v && t->parent[t->out[v]] == v &&
           t->dz_depth[v] > 0;
      stack[top++] = t->in[v];
      stack[top++] = t->out[v];
      continue;
    }
    for (i = 0; i < t->count[v] && ok; i++) {
      unsigned long q[HB_MAXD];
      size_t base;
      hb_quantise(t->domain, d, t->points[v] + i * d, q);
      ok = bd_leaf(t, q, &base) == v;
    }
    total += t->count[v];
  }
  ds_context_free(t->context, stack);
  return ok && total == t->size;
}

/* ---- GBD-tree
 * ------------------------------------------------------------------------------
 */

#define GE(t, v, i) ((v) * ((t)->fanout + 1) + (i))

static ds_status_t gbd_node(ds_gbd_tree_t *t, bool leaf, size_t *out) {
  size_t v;
  if (t->nfree != HB_NONE) {
    v = t->nfree;
    t->nfree = t->ncount[v];
  } else {
    if (t->nodes == t->ncapacity) {
      size_t nc = t->ncapacity < 8 ? 8 : t->ncapacity * 2;
      size_t e = t->fanout + 1;
      size_t d = t->dim;
      if (hb_grow(t->context, (void **)&t->ncount, sizeof(size_t), nc) ||
          hb_grow(t->context, (void **)&t->nleaf, 1, nc) ||
          hb_grow(t->context, (void **)&t->nparent, sizeof(size_t), nc) ||
          hb_grow(t->context, (void **)&t->edepth, e * sizeof(size_t), nc) ||
          hb_grow(t->context, (void **)&t->edz, e * d * sizeof(unsigned long),
                  nc) ||
          hb_grow(t->context, (void **)&t->ebox, e * 2 * d * sizeof(double),
                  nc) ||
          hb_grow(t->context, (void **)&t->eref, e * sizeof(size_t), nc)) {
        return DS_ERR_ALLOC;
      }
      t->ncapacity = nc;
    }
    v = t->nodes++;
  }
  t->ncount[v] = 0;
  t->nleaf[v] = (unsigned char)leaf;
  t->nparent[v] = HB_NONE;
  *out = v;
  return DS_OK;
}

static void gbd_free(ds_gbd_tree_t *t, size_t v) {
  t->ncount[v] = t->nfree;
  t->nfree = v;
}

ds_status_t ds_gbd_tree_init(ds_gbd_tree_t *t, size_t dim, const double *domain,
                             size_t fanout, ds_context_t *context) {
  ds_status_t st;
  if (t == NULL) {
    return DS_ERR_NULL;
  }
  if (fanout == 0) {
    fanout = 16;
  }
  if (dim == 0 || dim > HB_MAXD || fanout < 4 || fanout > 4096) {
    return DS_ERR_INVALID;
  }
  st = hb_domain_ok(dim, domain);
  if (st != DS_OK) {
    return st;
  }
  memset(t, 0, sizeof(*t));
  t->dim = dim;
  t->fanout = fanout;
  memcpy(t->domain, domain, 2 * dim * sizeof(double));
  t->nfree = HB_NONE;
  t->context = context;
  st = gbd_node(t, true, &t->root);
  if (st != DS_OK) {
    ds_gbd_tree_destroy(t);
    return st;
  }
  t->height = 1;
  return DS_OK;
}

void ds_gbd_tree_destroy(ds_gbd_tree_t *t) {
  if (t == NULL) {
    return;
  }
  ds_context_free(t->context, t->ncount);
  ds_context_free(t->context, t->nleaf);
  ds_context_free(t->context, t->nparent);
  ds_context_free(t->context, t->edepth);
  ds_context_free(t->context, t->edz);
  ds_context_free(t->context, t->ebox);
  ds_context_free(t->context, t->eref);
  memset(t, 0, sizeof(*t));
}

size_t ds_gbd_tree_size(const ds_gbd_tree_t *t) {
  return t == NULL ? 0 : t->size;
}

size_t ds_gbd_tree_height(const ds_gbd_tree_t *t) {
  return t == NULL ? 0 : t->height;
}

static void gbd_cover(const ds_gbd_tree_t *t, size_t v, double *b) {
  size_t i;
  hb_empty_box(b, t->dim);
  for (i = 0; i < t->ncount[v]; i++) {
    hb_extend(b, t->ebox + GE(t, v, i) * 2 * t->dim, t->dim, false);
  }
}

static size_t gbd_slot(const ds_gbd_tree_t *t, size_t v) {
  size_t p = t->nparent[v];
  size_t i;
  for (i = 0; i < t->ncount[p]; i++) {
    if (t->eref[GE(t, p, i)] == v) {
      return i;
    }
  }
  return HB_NONE;
}

static void gbd_fix_up(ds_gbd_tree_t *t, size_t v) {
  while (t->nparent[v] != HB_NONE) {
    size_t i = gbd_slot(t, v);
    gbd_cover(t, v, t->ebox + GE(t, t->nparent[v], i) * 2 * t->dim);
    v = t->nparent[v];
  }
}

/* common prefix length of a key and an entry's DZ-expression */
static size_t gbd_common(const unsigned long *q, const unsigned long *z,
                         size_t depth, size_t d) {
  size_t j;
  for (j = 0; j < depth && hb_bit(q, d, j) == hb_bit(z, d, j); j++) {
  }
  return j;
}

static size_t gbd_choose(const ds_gbd_tree_t *t, size_t v,
                         const unsigned long *q, const double *box) {
  size_t d = t->dim;
  size_t i;
  size_t best = 0;
  size_t best_len = 0;
  bool best_full = false;
  double best_enl = HUGE_VAL;
  for (i = 0; i < t->ncount[v]; i++) {
    size_t e = GE(t, v, i);
    size_t len = gbd_common(q, t->edz + e * d, t->edepth[e], d);
    bool full = len == t->edepth[e];
    double u[2 * HB_MAXD];
    double enl = 0.0;
    size_t a;
    memcpy(u, t->ebox + e * 2 * d, 2 * d * sizeof(double));
    hb_extend(u, box, d, false);
    for (a = 0; a < d; a++) {
      enl += (u[d + a] - u[a]) -
             (t->ebox[e * 2 * d + d + a] - t->ebox[e * 2 * d + a]);
    }
    if ((full && !best_full) ||
        (full == best_full &&
         (len > best_len || (len == best_len && enl < best_enl)))) {
      best = i;
      best_len = len;
      best_full = full;
      best_enl = enl;
    }
  }
  return best;
}

/* Split node v: the entries extending the best halving DZ-expression move
 * to a new node; returns the expression in z/depth. */
static ds_status_t gbd_split(ds_gbd_tree_t *t, size_t v, size_t *out_w,
                             unsigned long *z, size_t *zdepth) {
  size_t d = t->dim;
  size_t n = t->ncount[v];
  unsigned long *keys;
  size_t *len;
  unsigned char *in;
  unsigned char *best_in;
  size_t depth;
  size_t i;
  size_t w;
  size_t a = 0;
  size_t b = 0;
  size_t moved = 0;
  ds_status_t st;
  keys = (unsigned long *)ds_context_alloc(t->context,
                                           n * d * sizeof(unsigned long));
  len = (size_t *)ds_context_alloc(t->context, n * sizeof(size_t));
  in = (unsigned char *)ds_context_alloc(t->context, n);
  best_in = (unsigned char *)ds_context_alloc(t->context, n);
  if (keys == NULL || len == NULL || in == NULL || best_in == NULL) {
    st = DS_ERR_ALLOC;
    goto done;
  }
  for (i = 0; i < n; i++) {
    size_t e = GE(t, v, i);
    memcpy(keys + i * d, t->edz + e * d, d * sizeof(unsigned long));
    len[i] = t->edepth[e];
  }
  depth = hb_carve(keys, len, n, d, 0, in, best_in);
  for (i = 0; i < n; i++) {
    moved += depth > 0 && best_in[i];
  }
  if (depth == 0 || moved == 0 || moved == n) {
    /* nothing separates them: move the later half, named by its common
     * prefix */
    for (i = 0; i < n; i++) {
      best_in[i] = (unsigned char)(i >= n / 2);
    }
    depth = HB_B * d;
    for (i = n / 2 + 1; i < n; i++) {
      size_t c = gbd_common(keys + i * d, keys + (n / 2) * d,
                            len[i] < len[n / 2] ? len[i] : len[n / 2], d);
      depth = c < depth ? c : depth;
    }
    if (len[n / 2] < depth) {
      depth = len[n / 2];
    }
  }
  for (i = 0; i < n && !best_in[i]; i++) {
  }
  hb_dz_of(keys + i * d, depth, d, z);
  *zdepth = depth;
  st = gbd_node(t, t->nleaf[v] != 0, &w);
  if (st != DS_OK) {
    goto done;
  }
  for (i = 0; i < n; i++) {
    size_t src = GE(t, v, i);
    size_t dst = best_in[i] ? GE(t, w, b++) : GE(t, v, a++);
    size_t tmp_depth = t->edepth[src];
    size_t tmp_ref = t->eref[src];
    unsigned long tz[HB_MAXD];
    double tb[2 * HB_MAXD];
    memcpy(tz, t->edz + src * d, d * sizeof(unsigned long));
    memcpy(tb, t->ebox + src * 2 * d, 2 * d * sizeof(double));
    t->edepth[dst] = tmp_depth;
    t->eref[dst] = tmp_ref;
    memcpy(t->edz + dst * d, tz, d * sizeof(unsigned long));
    memcpy(t->ebox + dst * 2 * d, tb, 2 * d * sizeof(double));
    if (!t->nleaf[v]) {
      t->nparent[tmp_ref] = best_in[i] ? w : v;
    }
  }
  t->ncount[v] = a;
  t->ncount[w] = b;
  *out_w = w;
done:
  ds_context_free(t->context, keys);
  ds_context_free(t->context, len);
  ds_context_free(t->context, in);
  ds_context_free(t->context, best_in);
  return st;
}

ds_status_t ds_gbd_tree_insert(ds_gbd_tree_t *t, const double *box,
                               size_t value) {
  size_t d;
  size_t v;
  size_t a;
  size_t e;
  double c[HB_MAXD];
  unsigned long q[HB_MAXD];
  ds_status_t st;
  if (t == NULL || box == NULL) {
    return DS_ERR_NULL;
  }
  d = t->dim;
  if (!hb_box_ok(box, d)) {
    return DS_ERR_INVALID;
  }
  for (a = 0; a < d; a++) {
    c[a] = (box[a] + box[d + a]) / 2.0;
  }
  hb_quantise(t->domain, d, c, q);
  v = t->root;
  while (!t->nleaf[v]) {
    size_t i = gbd_choose(t, v, q, box);
    hb_extend(t->ebox + GE(t, v, i) * 2 * d, box, d, false);
    v = t->eref[GE(t, v, i)];
  }
  e = GE(t, v, t->ncount[v]);
  t->edepth[e] = HB_B * d;
  memcpy(t->edz + e * d, q, d * sizeof(unsigned long));
  memcpy(t->ebox + e * 2 * d, box, 2 * d * sizeof(double));
  t->eref[e] = value;
  t->ncount[v]++;
  t->size++;
  while (t->ncount[v] > t->fanout) {
    size_t w;
    unsigned long z[HB_MAXD];
    size_t zd;
    size_t p;
    st = gbd_split(t, v, &w, z, &zd);
    if (st != DS_OK) {
      return st;
    }
    if (v == t->root) {
      size_t r;
      st = gbd_node(t, false, &r);
      if (st != DS_OK) {
        return st;
      }
      e = GE(t, r, 0);
      t->edepth[e] = 0;
      memset(t->edz + e * d, 0, d * sizeof(unsigned long));
      gbd_cover(t, v, t->ebox + e * 2 * d);
      t->eref[e] = v;
      t->ncount[r] = 1;
      t->nparent[v] = r;
      t->root = r;
      t->height++;
    }
    p = t->nparent[v];
    gbd_cover(t, v, t->ebox + GE(t, p, gbd_slot(t, v)) * 2 * d);
    e = GE(t, p, t->ncount[p]);
    t->edepth[e] = zd;
    memcpy(t->edz + e * d, z, d * sizeof(unsigned long));
    gbd_cover(t, w, t->ebox + e * 2 * d);
    t->eref[e] = w;
    t->ncount[p]++;
    t->nparent[w] = p;
    v = p;
  }
  return DS_OK;
}

ds_status_t ds_gbd_tree_remove(ds_gbd_tree_t *t, const double *box,
                               size_t value) {
  size_t d;
  size_t *stack;
  size_t top = 0;
  size_t leaf = HB_NONE;
  size_t slot = 0;
  size_t v;
  if (t == NULL || box == NULL) {
    return DS_ERR_NULL;
  }
  d = t->dim;
  if (!hb_box_ok(box, d)) {
    return DS_NOT_FOUND;
  }
  stack =
      (size_t *)ds_context_alloc(t->context, (t->nodes + 1) * sizeof(size_t));
  if (stack == NULL) {
    return DS_ERR_ALLOC;
  }
  stack[top++] = t->root;
  while (top > 0 && leaf == HB_NONE) {
    size_t i;
    v = stack[--top];
    for (i = 0; i < t->ncount[v]; i++) {
      size_t e = GE(t, v, i);
      if (t->nleaf[v]) {
        if (t->eref[e] == value &&
            memcmp(t->ebox + e * 2 * d, box, 2 * d * sizeof(double)) == 0) {
          leaf = v;
          slot = i;
          break;
        }
      } else {
        const double *b = t->ebox + e * 2 * d;
        size_t a;
        for (a = 0; a < d && b[a] <= box[a] && box[d + a] <= b[d + a]; a++) {
        }
        if (a == d) {
          stack[top++] = t->eref[e];
        }
      }
    }
  }
  ds_context_free(t->context, stack);
  if (leaf == HB_NONE) {
    return DS_NOT_FOUND;
  }
  v = leaf;
  t->ncount[v]--;
  if (slot != t->ncount[v]) {
    size_t s = GE(t, v, slot);
    size_t l = GE(t, v, t->ncount[v]);
    t->edepth[s] = t->edepth[l];
    t->eref[s] = t->eref[l];
    memcpy(t->edz + s * d, t->edz + l * d, d * sizeof(unsigned long));
    memcpy(t->ebox + s * 2 * d, t->ebox + l * 2 * d, 2 * d * sizeof(double));
  }
  t->size--;
  /* empty nodes leave the tree */
  while (t->ncount[v] == 0 && v != t->root) {
    size_t p = t->nparent[v];
    size_t i = gbd_slot(t, v);
    t->ncount[p]--;
    if (i != t->ncount[p]) {
      size_t s = GE(t, p, i);
      size_t l = GE(t, p, t->ncount[p]);
      t->edepth[s] = t->edepth[l];
      t->eref[s] = t->eref[l];
      memcpy(t->edz + s * d, t->edz + l * d, d * sizeof(unsigned long));
      memcpy(t->ebox + s * 2 * d, t->ebox + l * 2 * d, 2 * d * sizeof(double));
    }
    gbd_free(t, v);
    v = p;
  }
  gbd_fix_up(t, v);
  while (!t->nleaf[t->root] && t->ncount[t->root] == 1) {
    size_t old = t->root;
    t->root = t->eref[GE(t, old, 0)];
    t->nparent[t->root] = HB_NONE;
    gbd_free(t, old);
    t->height--;
  }
  if (!t->nleaf[t->root] && t->ncount[t->root] == 0) {
    t->nleaf[t->root] = 1;
    t->height = 1;
  }
  return DS_OK;
}

ds_status_t ds_gbd_tree_query(const ds_gbd_tree_t *t, const double *box,
                              ds_hb_visit_func_t visit, void *user) {
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
    for (i = 0; i < t->ncount[v]; i++) {
      size_t e = GE(t, v, i);
      if (!hb_box_hit(t->ebox + e * 2 * d, box, d)) {
        continue;
      }
      if (!t->nleaf[v]) {
        stack[top++] = t->eref[e];
      } else if (!visit(t->ebox + e * 2 * d, t->eref[e], user)) {
        st = DS_STOP;
        break;
      }
    }
  }
  ds_context_free(t->context, stack);
  return st;
}

bool ds_gbd_tree_valid(const ds_gbd_tree_t *t) {
  size_t d;
  size_t *stack;
  size_t *lvl;
  size_t top = 0;
  size_t total = 0;
  bool ok = true;
  if (t == NULL || t->ncount == NULL) {
    return false;
  }
  d = t->dim;
  stack =
      (size_t *)ds_context_alloc(t->context, (t->nodes + 1) * sizeof(size_t));
  lvl = (size_t *)ds_context_alloc(t->context, (t->nodes + 1) * sizeof(size_t));
  if (stack == NULL || lvl == NULL) {
    ds_context_free(t->context, stack);
    ds_context_free(t->context, lvl);
    return false;
  }
  stack[top] = t->root;
  lvl[top++] = 1;
  while (top > 0 && ok) {
    size_t v;
    size_t l;
    size_t i;
    top--;
    v = stack[top];
    l = lvl[top];
    if (t->ncount[v] > t->fanout || (v != t->root && t->ncount[v] == 0)) {
      ok = false;
      break;
    }
    if (t->nleaf[v]) {
      ok = l == t->height;
      total += t->ncount[v];
      continue;
    }
    for (i = 0; i < t->ncount[v] && ok; i++) {
      size_t e = GE(t, v, i);
      size_t c = t->eref[e];
      double b[2 * HB_MAXD];
      gbd_cover(t, c, b);
      ok = t->nparent[c] == v &&
           memcmp(b, t->ebox + e * 2 * d, 2 * d * sizeof(double)) == 0;
      stack[top] = c;
      lvl[top++] = l + 1;
    }
  }
  ds_context_free(t->context, stack);
  ds_context_free(t->context, lvl);
  return ok && total == t->size;
}

/* ---- BV-tree
 * ---------------------------------------------------------------------------------
 */

static ds_status_t bv_inode(ds_bv_tree_t *t, size_t level, size_t *out) {
  size_t v;
  if (t->inodes == t->inode_capacity) {
    size_t nc = t->inode_capacity < 8 ? 8 : t->inode_capacity * 2;
    if (hb_grow(t->context, (void **)&t->ilevel, sizeof(size_t), nc) ||
        hb_grow(t->context, (void **)&t->iparent, sizeof(size_t), nc) ||
        hb_grow(t->context, (void **)&t->icount, sizeof(size_t), nc) ||
        hb_grow(t->context, (void **)&t->icapacity, sizeof(size_t), nc) ||
        hb_grow(t->context, (void **)&t->edepth, sizeof(size_t *), nc) ||
        hb_grow(t->context, (void **)&t->edz, sizeof(unsigned long *), nc) ||
        hb_grow(t->context, (void **)&t->echild, sizeof(size_t *), nc) ||
        hb_grow(t->context, (void **)&t->elevel, sizeof(size_t *), nc)) {
      return DS_ERR_ALLOC;
    }
    t->inode_capacity = nc;
  }
  v = t->inodes++;
  t->ilevel[v] = level;
  t->iparent[v] = HB_NONE;
  t->icount[v] = t->icapacity[v] = 0;
  t->edepth[v] = NULL;
  t->edz[v] = NULL;
  t->echild[v] = NULL;
  t->elevel[v] = NULL;
  *out = v;
  return DS_OK;
}

static ds_status_t bv_add(ds_bv_tree_t *t, size_t v, size_t depth,
                          const unsigned long *z, size_t child, size_t level) {
  size_t d = t->dim;
  size_t i;
  if (t->icount[v] == t->icapacity[v]) {
    size_t nc = t->icapacity[v] < 4 ? 8 : t->icapacity[v] * 2;
    if (hb_grow(t->context, (void **)&t->edepth[v], sizeof(size_t), nc) ||
        hb_grow(t->context, (void **)&t->edz[v], d * sizeof(unsigned long),
                nc) ||
        hb_grow(t->context, (void **)&t->echild[v], sizeof(size_t), nc) ||
        hb_grow(t->context, (void **)&t->elevel[v], sizeof(size_t), nc)) {
      return DS_ERR_ALLOC;
    }
    t->icapacity[v] = nc;
  }
  i = t->icount[v]++;
  t->edepth[v][i] = depth;
  memcpy(t->edz[v] + i * d, z, d * sizeof(unsigned long));
  t->echild[v][i] = child;
  t->elevel[v][i] = level;
  if (level > 0) {
    t->iparent[child] = v;
  }
  return DS_OK;
}

static void bv_del(ds_bv_tree_t *t, size_t v, size_t i) {
  size_t d = t->dim;
  size_t last = --t->icount[v];
  if (i != last) {
    t->edepth[v][i] = t->edepth[v][last];
    memcpy(t->edz[v] + i * d, t->edz[v] + last * d, d * sizeof(unsigned long));
    t->echild[v][i] = t->echild[v][last];
    t->elevel[v][i] = t->elevel[v][last];
  }
}

static ds_status_t bv_page(ds_bv_tree_t *t, size_t *out) {
  size_t p;
  if (t->pages == t->page_capacity) {
    size_t nc = t->page_capacity < 8 ? 8 : t->page_capacity * 2;
    if (hb_grow(t->context, (void **)&t->points, sizeof(double *), nc) ||
        hb_grow(t->context, (void **)&t->values, sizeof(size_t *), nc) ||
        hb_grow(t->context, (void **)&t->count, sizeof(size_t), nc) ||
        hb_grow(t->context, (void **)&t->capacity, sizeof(size_t), nc)) {
      return DS_ERR_ALLOC;
    }
    t->page_capacity = nc;
  }
  p = t->pages++;
  t->points[p] = NULL;
  t->values[p] = NULL;
  t->count[p] = t->capacity[p] = 0;
  *out = p;
  return DS_OK;
}

ds_status_t ds_bv_tree_init(ds_bv_tree_t *t, size_t dim, const double *domain,
                            size_t bucket, size_t fanout,
                            ds_context_t *context) {
  unsigned long z[HB_MAXD];
  size_t page = 0;
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
  if (dim == 0 || dim > HB_MAXD || bucket < 2 || fanout < 4) {
    return DS_ERR_INVALID;
  }
  st = hb_domain_ok(dim, domain);
  if (st != DS_OK) {
    return st;
  }
  memset(t, 0, sizeof(*t));
  t->dim = dim;
  t->bucket = bucket;
  t->fanout = fanout;
  memcpy(t->domain, domain, 2 * dim * sizeof(double));
  t->context = context;
  memset(z, 0, sizeof(z));
  st = bv_inode(t, 1, &t->root);
  if (st == DS_OK) {
    st = bv_page(t, &page);
  }
  if (st == DS_OK) {
    st = bv_add(t, t->root, 0, z, page, 0);
  }
  if (st != DS_OK) {
    ds_bv_tree_destroy(t);
  }
  return st;
}

void ds_bv_tree_destroy(ds_bv_tree_t *t) {
  size_t i;
  if (t == NULL) {
    return;
  }
  for (i = 0; i < t->inodes; i++) {
    ds_context_free(t->context, t->edepth[i]);
    ds_context_free(t->context, t->edz[i]);
    ds_context_free(t->context, t->echild[i]);
    ds_context_free(t->context, t->elevel[i]);
  }
  for (i = 0; i < t->pages; i++) {
    ds_context_free(t->context, t->points[i]);
    ds_context_free(t->context, t->values[i]);
  }
  ds_context_free(t->context, t->ilevel);
  ds_context_free(t->context, t->iparent);
  ds_context_free(t->context, t->icount);
  ds_context_free(t->context, t->icapacity);
  ds_context_free(t->context, t->edepth);
  ds_context_free(t->context, t->edz);
  ds_context_free(t->context, t->echild);
  ds_context_free(t->context, t->elevel);
  ds_context_free(t->context, t->points);
  ds_context_free(t->context, t->values);
  ds_context_free(t->context, t->count);
  ds_context_free(t->context, t->capacity);
  ds_context_free(t->context, t->scratch);
  memset(t, 0, sizeof(*t));
}

size_t ds_bv_tree_size(const ds_bv_tree_t *t) {
  return t == NULL ? 0 : t->size;
}

size_t ds_bv_tree_height(const ds_bv_tree_t *t) {
  return t == NULL ? 0 : t->ilevel[t->root];
}

size_t ds_bv_tree_guards(const ds_bv_tree_t *t) {
  return t == NULL ? 0 : t->guards;
}

/* Search for key q: the page, the level-1 node where it was chosen, and
 * the depth of the entry that chose it. Guards met on the way are carried
 * down and compete at their own level. */
static ds_status_t bv_search(ds_bv_tree_t *t, const unsigned long *q,
                             size_t *out_page, size_t *out_x,
                             size_t *out_depth) {
  size_t d = t->dim;
  size_t v = t->root;
  size_t level = t->ilevel[v];
  size_t ng = 0;
  for (;;) {
    size_t i;
    size_t best_child = HB_NONE;
    size_t best_depth = 0;
    bool any = false;
    /* carry this node's guards for lower levels */
    for (i = 0; i < t->icount[v]; i++) {
      if (t->elevel[v][i] + 1 < level) {
        if (2 * ng + 2 > t->scratch_capacity) {
          size_t nc = t->scratch_capacity < 32 ? 64 : t->scratch_capacity * 2;
          if (hb_grow(t->context, (void **)&t->scratch, sizeof(size_t), nc)) {
            return DS_ERR_ALLOC;
          }
          t->scratch_capacity = nc;
        }
        t->scratch[2 * ng] = v;
        t->scratch[2 * ng + 1] = i;
        ng++;
      } else if (t->elevel[v][i] + 1 == level &&
                 hb_dz_match(q, t->edz[v] + i * d, t->edepth[v][i], d) &&
                 (!any || t->edepth[v][i] > best_depth)) {
        any = true;
        best_depth = t->edepth[v][i];
        best_child = t->echild[v][i];
      }
    }
    for (i = 0; i < ng; i++) {
      size_t gv = t->scratch[2 * i];
      size_t gi = t->scratch[2 * i + 1];
      if (t->elevel[gv][gi] + 1 == level &&
          hb_dz_match(q, t->edz[gv] + gi * d, t->edepth[gv][gi], d) &&
          (!any || t->edepth[gv][gi] > best_depth)) {
        any = true;
        best_depth = t->edepth[gv][gi];
        best_child = t->echild[gv][gi];
      }
    }
    if (!any) {
      return DS_ERR_INTERNAL;
    }
    if (level == 1) {
      *out_page = best_child;
      *out_x = v;
      *out_depth = best_depth;
      return DS_OK;
    }
    v = best_child;
    level--;
  }
}

/* entries of v pointing one level down (guards are not counted) */
static size_t bv_native(const ds_bv_tree_t *t, size_t v) {
  size_t i;
  size_t n = 0;
  for (i = 0; i < t->icount[v]; i++) {
    n += t->elevel[v][i] + 1 == t->ilevel[v];
  }
  return n;
}

/* Split index node m along the DZ-expression that best halves its
 * entries; enclosing entries are promoted to the parent as guards. */
static ds_status_t bv_split(ds_bv_tree_t *t, size_t m) {
  size_t d = t->dim;
  size_t n = t->icount[m];
  size_t level = t->ilevel[m];
  unsigned long *keys;
  size_t *len;
  unsigned char *in;
  unsigned char *best_in;
  unsigned long z[HB_MAXD];
  size_t depth;
  size_t moved = 0;
  size_t i;
  size_t parent;
  size_t nn;
  ds_status_t st = DS_OK;
  keys = (unsigned long *)ds_context_alloc(t->context,
                                           n * d * sizeof(unsigned long));
  len = (size_t *)ds_context_alloc(t->context, n * sizeof(size_t));
  in = (unsigned char *)ds_context_alloc(t->context, n);
  best_in = (unsigned char *)ds_context_alloc(t->context, n);
  if (keys == NULL || len == NULL || in == NULL || best_in == NULL) {
    st = DS_ERR_ALLOC;
    goto done;
  }
  /* the split is chosen over the node's own entries; guards follow */
  {
    size_t k = 0;
    for (i = 0; i < n; i++) {
      if (t->elevel[m][i] + 1 == level) {
        memcpy(keys + k * d, t->edz[m] + i * d, d * sizeof(unsigned long));
        len[k++] = t->edepth[m][i];
      }
    }
    depth = hb_carve(keys, len, k, d, 0, in, best_in);
    for (i = 0; i < k; i++) {
      moved += depth > 0 && best_in[i];
    }
    if (depth == 0 || moved == 0 || moved == k) {
      st = DS_EXISTS;
      goto done;
    }
    for (i = 0; i < k && !best_in[i]; i++) {
    }
    hb_dz_of(keys + i * d, depth, d, z);
    for (i = 0; i < n; i++) {
      best_in[i] = (unsigned char)(t->edepth[m][i] >= depth &&
                                   hb_dz_match(t->edz[m] + i * d, z, depth, d));
    }
  }
  if (m == t->root) {
    unsigned long zero[HB_MAXD];
    size_t r;
    memset(zero, 0, sizeof(zero));
    st = bv_inode(t, level + 1, &r);
    if (st == DS_OK) {
      st = bv_add(t, r, 0, zero, m, level);
    }
    if (st != DS_OK) {
      goto done;
    }
    t->root = r;
  }
  parent = t->iparent[m];
  for (i = 0; i < t->icount[parent]; i++) {
    if (t->elevel[parent][i] == level && t->edepth[parent][i] == depth &&
        memcmp(t->edz[parent] + i * d, z, d * sizeof(unsigned long)) == 0) {
      st = DS_EXISTS;
      goto done;
    }
  }
  st = bv_inode(t, level, &nn);
  if (st != DS_OK) {
    goto done;
  }
  /* entries move (extending z), get promoted (enclosing z) or stay; walk
   * the original order backwards so deletions keep indices valid */
  for (i = n; i-- > 0 && st == DS_OK;) {
    size_t ed = t->edepth[m][i];
    unsigned long ez[HB_MAXD];
    size_t ec = t->echild[m][i];
    size_t el = t->elevel[m][i];
    memcpy(ez, t->edz[m] + i * d, d * sizeof(unsigned long));
    if (best_in[i]) {
      st = bv_add(t, nn, ed, ez, ec, el);
      if (st == DS_OK) {
        bv_del(t, m, i);
      }
    } else if (ed < depth && hb_dz_match(z, ez, ed, d)) {
      st = bv_add(t, parent, ed, ez, ec, el);
      if (st == DS_OK) {
        bv_del(t, m, i);
        t->guards++;
      }
    }
  }
  if (st == DS_OK) {
    st = bv_add(t, parent, depth, z, nn, level);
  }
done:
  ds_context_free(t->context, keys);
  ds_context_free(t->context, len);
  ds_context_free(t->context, in);
  ds_context_free(t->context, best_in);
  return st;
}

ds_status_t ds_bv_tree_insert(ds_bv_tree_t *t, const double *point,
                              size_t value) {
  size_t d;
  size_t page;
  size_t x;
  size_t depth;
  unsigned long q[HB_MAXD];
  ds_status_t st;
  if (t == NULL || point == NULL) {
    return DS_ERR_NULL;
  }
  d = t->dim;
  if (!hb_finite(point, d)) {
    return DS_ERR_INVALID;
  }
  hb_quantise(t->domain, d, point, q);
  st = bv_search(t, q, &page, &x, &depth);
  if (st != DS_OK) {
    return st;
  }
  st = hb_append(t->context, &t->points[page], &t->values[page],
                 &t->count[page], &t->capacity[page], d, point, value);
  if (st != DS_OK) {
    return st;
  }
  t->size++;
  if (t->count[page] > t->bucket) {
    size_t n = t->count[page];
    unsigned long *keys;
    unsigned char *in;
    unsigned char *best_in;
    size_t cut;
    size_t i;
    keys = (unsigned long *)ds_context_alloc(t->context,
                                             n * d * sizeof(unsigned long));
    in = (unsigned char *)ds_context_alloc(t->context, n);
    best_in = (unsigned char *)ds_context_alloc(t->context, n);
    if (keys == NULL || in == NULL || best_in == NULL) {
      ds_context_free(t->context, keys);
      ds_context_free(t->context, in);
      ds_context_free(t->context, best_in);
      return DS_ERR_ALLOC;
    }
    for (i = 0; i < n; i++) {
      hb_quantise(t->domain, d, t->points[page] + i * d, keys + i * d);
    }
    cut = hb_carve(keys, NULL, n, d, depth, in, best_in);
    if (cut > 0) {
      unsigned long z[HB_MAXD];
      bool dup = false;
      for (i = 0; i < n && !best_in[i]; i++) {
      }
      hb_dz_of(keys + i * d, cut, d, z);
      for (i = 0; i < t->icount[x] && !dup; i++) {
        dup = t->elevel[x][i] == 0 && t->edepth[x][i] == cut &&
              memcmp(t->edz[x] + i * d, z, d * sizeof(unsigned long)) == 0;
      }
      if (!dup) {
        size_t qpage;
        size_t moved = 0;
        st = bv_page(t, &qpage);
        if (st == DS_OK) {
          st = bv_add(t, x, cut, z, qpage, 0);
        }
        /* the points that a search now sends to the new page move */
        for (i = 0; i < t->count[page] && st == DS_OK;) {
          size_t pg;
          size_t xx;
          size_t dd;
          unsigned long k2[HB_MAXD];
          hb_quantise(t->domain, d, t->points[page] + i * d, k2);
          st = bv_search(t, k2, &pg, &xx, &dd);
          if (st == DS_OK && pg == qpage) {
            st = hb_append(t->context, &t->points[qpage], &t->values[qpage],
                           &t->count[qpage], &t->capacity[qpage], d,
                           t->points[page] + i * d, t->values[page][i]);
            if (st == DS_OK) {
              hb_drop(t->points[page], t->values[page], &t->count[page], d, i);
              moved++;
            }
          } else {
            i++;
          }
        }
        if (st == DS_OK && moved == 0) {
          bv_del(t, x, t->icount[x] - 1); /* nothing moved: undo */
        }
      }
    }
    ds_context_free(t->context, keys);
    ds_context_free(t->context, in);
    ds_context_free(t->context, best_in);
    if (st != DS_OK) {
      return st;
    }
  }
  while (bv_native(t, x) > t->fanout) {
    st = bv_split(t, x);
    if (st == DS_EXISTS) {
      return DS_OK; /* an oversized node */
    }
    if (st != DS_OK) {
      return st;
    }
    x = t->iparent[x];
  }
  return DS_OK;
}

ds_status_t ds_bv_tree_remove(ds_bv_tree_t *t, const double *point,
                              size_t value) {
  size_t page;
  size_t x;
  size_t depth;
  size_t i;
  unsigned long q[HB_MAXD];
  ds_status_t st;
  if (t == NULL || point == NULL) {
    return DS_ERR_NULL;
  }
  if (!hb_finite(point, t->dim)) {
    return DS_NOT_FOUND;
  }
  hb_quantise(t->domain, t->dim, point, q);
  st = bv_search(t, q, &page, &x, &depth);
  if (st != DS_OK) {
    return st;
  }
  i = hb_find(t->points[page], t->values[page], t->count[page], t->dim, point,
              value);
  if (i == HB_NONE) {
    return DS_NOT_FOUND;
  }
  hb_drop(t->points[page], t->values[page], &t->count[page], t->dim, i);
  t->size--;
  return DS_OK;
}

bool ds_bv_tree_contains(ds_bv_tree_t *t, const double *point) {
  size_t page;
  size_t x;
  size_t depth;
  size_t i;
  unsigned long q[HB_MAXD];
  if (t == NULL || point == NULL || !hb_finite(point, t->dim)) {
    return false;
  }
  hb_quantise(t->domain, t->dim, point, q);
  if (bv_search(t, q, &page, &x, &depth) != DS_OK) {
    return false;
  }
  for (i = 0; i < t->count[page]; i++) {
    if (memcmp(t->points[page] + i * t->dim, point, t->dim * sizeof(double)) ==
        0) {
      return true;
    }
  }
  return false;
}

ds_status_t ds_bv_tree_query(const ds_bv_tree_t *t, const double *box,
                             ds_hb_visit_func_t visit, void *user) {
  size_t d;
  size_t *stack;
  size_t top = 0;
  ds_status_t st = DS_OK;
  if (t == NULL || box == NULL || visit == NULL) {
    return DS_ERR_NULL;
  }
  d = t->dim;
  stack =
      (size_t *)ds_context_alloc(t->context, (t->inodes + 1) * sizeof(size_t));
  if (stack == NULL) {
    return DS_ERR_ALLOC;
  }
  stack[top++] = t->root;
  while (top > 0 && st == DS_OK) {
    size_t v = stack[--top];
    size_t i;
    for (i = 0; i < t->icount[v] && st == DS_OK; i++) {
      double c[2 * HB_MAXD];
      size_t ch = t->echild[v][i];
      hb_dz_box(t->domain, d, t->edz[v] + i * d, t->edepth[v][i], c);
      if (!hb_box_hit(c, box, d)) {
        continue;
      }
      if (t->elevel[v][i] > 0) {
        stack[top++] = ch;
      } else {
        size_t j;
        for (j = 0; j < t->count[ch]; j++) {
          const double *p = t->points[ch] + j * d;
          if (hb_point_in_box(p, box, d) && !visit(p, t->values[ch][j], user)) {
            st = DS_STOP;
            break;
          }
        }
      }
    }
  }
  ds_context_free(t->context, stack);
  return st;
}

bool ds_bv_tree_valid(ds_bv_tree_t *t) {
  size_t p;
  size_t total = 0;
  size_t d;
  if (t == NULL || t->icount == NULL) {
    return false;
  }
  d = t->dim;
  for (p = 0; p < t->pages; p++) {
    size_t i;
    for (i = 0; i < t->count[p]; i++) {
      unsigned long q[HB_MAXD];
      size_t pg;
      size_t x;
      size_t depth;
      hb_quantise(t->domain, d, t->points[p] + i * d, q);
      if (bv_search(t, q, &pg, &x, &depth) != DS_OK || pg != p) {
        return false;
      }
    }
    total += t->count[p];
  }
  return total == t->size;
}
