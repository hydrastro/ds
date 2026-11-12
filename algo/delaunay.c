#include "delaunay.h"
#include "../lib/checked.h"
#include "robust_predicates.h"
#include "space_order.h"
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#define DT_NONE DS_DELAUNAY_NONE
#define DT_GHOST DS_DELAUNAY_NONE /* the vertex at infinity */

#define DT_ALIVE 0
#define DT_CAVITY 1
#define DT_FREE 2

typedef struct dt {
  const double *xy;
  size_t n;
  size_t *v;           /* 3 vertices per slot */
  size_t *nb;          /* 3 neighbours per slot: across the edge opposite k */
  unsigned char *mark; /* per slot */
  size_t slots;
  size_t cap;
  size_t *free_slots;
  size_t nfree;
  size_t free_cap;
  size_t *stack;
  size_t stack_cap;
  size_t *bnd; /* boundary edges: a, b, outside triangle, its edge index */
  size_t nbnd;
  size_t bnd_cap;
  size_t *fan; /* vertex (ghost at n) -> new triangle starting there */
  size_t last;
  unsigned long rng;
  ds_context_t *ctx;
} dt_t;

static const double *dt_p(const dt_t *d, size_t i) { return d->xy + 2 * i; }

static bool dt_same(const double *a, const double *b) {
  return !(a[0] < b[0]) && !(a[0] > b[0]) && !(a[1] < b[1]) && !(a[1] > b[1]);
}

static unsigned long dt_rand(dt_t *d) {
  unsigned long x = d->rng;
  x ^= (x << 13) & 0xFFFFFFFFUL;
  x ^= x >> 17;
  x ^= (x << 5) & 0xFFFFFFFFUL;
  d->rng = x & 0xFFFFFFFFUL;
  return d->rng;
}

/* grow a size_t array to hold need * per elements */
static ds_status_t dt_grow(ds_context_t *ctx, size_t **arr, size_t *cap,
                           size_t need, size_t per) {
  size_t nc;
  size_t bytes;
  void *q;
  if (need <= *cap) {
    return DS_OK;
  }
  nc = *cap < 16 ? 16 : *cap;
  while (nc < need) {
    if (nc > ((size_t)-1) / 2) {
      nc = need;
      break;
    }
    nc *= 2;
  }
  if (!ds_size_mul(nc, per, &bytes) ||
      !ds_size_mul(bytes, sizeof(size_t), &bytes)) {
    return DS_ERR_OVERFLOW;
  }
  q = ds_context_realloc(ctx, *arr, bytes);
  if (q == NULL) {
    return DS_ERR_ALLOC;
  }
  *arr = (size_t *)q;
  *cap = nc;
  return DS_OK;
}

static ds_status_t dt_new(dt_t *d, size_t a, size_t b, size_t c, size_t *out) {
  size_t t;
  if (d->nfree > 0) {
    t = d->free_slots[--d->nfree];
  } else {
    if (d->slots == d->cap) {
      size_t cap_v = d->cap;
      size_t cap_n = d->cap;
      size_t bytes;
      ds_status_t st = dt_grow(d->ctx, &d->v, &cap_v, d->slots + 1, 3);
      void *m;
      if (st == DS_OK) {
        st = dt_grow(d->ctx, &d->nb, &cap_n, d->slots + 1, 3);
      }
      if (st != DS_OK) {
        return st;
      }
      if (cap_v != cap_n || !ds_size_mul(cap_v, 1, &bytes)) {
        return DS_ERR_INTERNAL;
      }
      m = ds_context_realloc(d->ctx, d->mark, bytes);
      if (m == NULL) {
        return DS_ERR_ALLOC;
      }
      d->mark = (unsigned char *)m;
      d->cap = cap_v;
    }
    t = d->slots++;
  }
  d->v[3 * t] = a;
  d->v[3 * t + 1] = b;
  d->v[3 * t + 2] = c;
  d->nb[3 * t] = d->nb[3 * t + 1] = d->nb[3 * t + 2] = DT_NONE;
  d->mark[t] = DT_ALIVE;
  *out = t;
  return DS_OK;
}

static int dt_ghost_at(const dt_t *d, size_t t) {
  int k;
  for (k = 0; k < 3; k++) {
    if (d->v[3 * t + (size_t)k] == DT_GHOST) {
      return k;
    }
  }
  return -1;
}

static bool dt_between(const double *a, const double *b, const double *p) {
  if (a[0] < b[0] || a[0] > b[0]) {
    return (a[0] < p[0] && p[0] < b[0]) || (b[0] < p[0] && p[0] < a[0]);
  }
  return (a[1] < p[1] && p[1] < b[1]) || (b[1] < p[1] && p[1] < a[1]);
}

/* is point p in conflict with triangle t: inside its circumcircle, or for
 * a ghost triangle beyond (or inside) its hull edge */
static bool dt_conflict(const dt_t *d, size_t t, const double *p) {
  const size_t *v = d->v + 3 * t;
  int g = dt_ghost_at(d, t);
  if (g < 0) {
    return ds_incircle(dt_p(d, v[0]), dt_p(d, v[1]), dt_p(d, v[2]), p) > 0.0;
  }
  {
    const double *a = dt_p(d, v[(g + 1) % 3]);
    const double *b = dt_p(d, v[(g + 2) % 3]);
    double o = ds_orient2d(a, b, p);
    if (o > 0.0) {
      return true;
    }
    if (o < 0.0) {
      return false;
    }
    return dt_between(a, b, p);
  }
}

/* a triangle in conflict with p, found by a scan (the walk's fallback) */
static size_t dt_scan(const dt_t *d, const double *p) {
  size_t t;
  for (t = 0; t < d->slots; t++) {
    if (d->mark[t] == DT_ALIVE && dt_conflict(d, t, p)) {
      return t;
    }
  }
  return DT_NONE;
}

/* walk towards p from the last triangle: a finite triangle containing p
 * or the ghost triangle beyond the hull edge it crossed */
static size_t dt_locate(dt_t *d, const double *p) {
  size_t t = d->last;
  size_t steps = 0;
  size_t limit = d->slots + 16;
  int g = dt_ghost_at(d, t);
  if (g >= 0) {
    t = d->nb[3 * t + (size_t)g]; /* the finite triangle across its edge */
  }
  for (;;) {
    size_t start;
    size_t k;
    bool moved = false;
    if (dt_ghost_at(d, t) >= 0) {
      return t;
    }
    if (++steps > limit) {
      return dt_scan(d, p);
    }
    start = (size_t)(dt_rand(d) % 3UL);
    for (k = 0; k < 3; k++) {
      size_t e = (start + k) % 3;
      size_t a = d->v[3 * t + (e + 1) % 3];
      size_t b = d->v[3 * t + (e + 2) % 3];
      if (ds_orient2d(dt_p(d, a), dt_p(d, b), p) < 0.0) {
        t = d->nb[3 * t + e];
        moved = true;
        break;
      }
    }
    if (!moved) {
      return t;
    }
  }
}

static size_t dt_slot_of(size_t vertex, size_t n) {
  return vertex == DT_GHOST ? n : vertex;
}

/* insert point pi: DS_OK, DS_EXISTS for a duplicate */
static ds_status_t dt_insert(dt_t *d, size_t pi) {
  const double *p = dt_p(d, pi);
  size_t t0 = dt_locate(d, p);
  size_t sp = 0;
  size_t i;
  size_t k;
  ds_status_t st;
  if (t0 != DT_NONE) {
    for (k = 0; k < 3; k++) {
      size_t w = d->v[3 * t0 + k];
      if (w != DT_GHOST && dt_same(dt_p(d, w), p)) {
        return DS_EXISTS;
      }
    }
    if (!dt_conflict(d, t0, p)) {
      t0 = dt_scan(d, p);
    }
  }
  if (t0 == DT_NONE) {
    return DS_ERR_INTERNAL;
  }
  /* the cavity: every triangle in conflict, reached through conflicts */
  d->nbnd = 0;
  st = dt_grow(d->ctx, &d->stack, &d->stack_cap, 1, 1);
  if (st != DS_OK) {
    return st;
  }
  d->stack[sp++] = t0;
  d->mark[t0] = DT_CAVITY;
  st = dt_grow(d->ctx, &d->free_slots, &d->free_cap, d->nfree + 1, 1);
  if (st != DS_OK) {
    d->mark[t0] = DT_ALIVE;
    return st;
  }
  d->free_slots[d->nfree++] = t0; /* cavity list: the tail of free_slots */
  {
    size_t cavity_start = d->nfree - 1;
    while (sp > 0) {
      size_t t = d->stack[--sp];
      for (k = 0; k < 3; k++) {
        size_t u = d->nb[3 * t + k];
        if (d->mark[u] == DT_CAVITY) {
          continue;
        }
        if (dt_conflict(d, u, p)) {
          st = dt_grow(d->ctx, &d->stack, &d->stack_cap, sp + 1, 1);
          if (st == DS_OK) {
            st = dt_grow(d->ctx, &d->free_slots, &d->free_cap, d->nfree + 1, 1);
          }
          if (st != DS_OK) {
            break;
          }
          d->mark[u] = DT_CAVITY;
          d->stack[sp++] = u;
          d->free_slots[d->nfree++] = u;
        } else {
          size_t j;
          st = dt_grow(d->ctx, &d->bnd, &d->bnd_cap, d->nbnd + 1, 4);
          if (st != DS_OK) {
            break;
          }
          for (j = 0; j < 3 && d->nb[3 * u + j] != t; j++) {
          }
          d->bnd[4 * d->nbnd] = d->v[3 * t + (k + 1) % 3];
          d->bnd[4 * d->nbnd + 1] = d->v[3 * t + (k + 2) % 3];
          d->bnd[4 * d->nbnd + 2] = u;
          d->bnd[4 * d->nbnd + 3] = j;
          d->nbnd++;
        }
      }
      if (st != DS_OK) {
        break;
      }
    }
    if (st != DS_OK) {
      /* nothing has changed yet: unmark the cavity and give up */
      for (i = cavity_start; i < d->nfree; i++) {
        d->mark[d->free_slots[i]] = DT_ALIVE;
      }
      d->nfree = cavity_start;
      return st;
    }
    /* one new triangle per boundary edge; reserve them all first so that
     * the replacement cannot fail halfway (the cavity's slots are reused,
     * and there are two more boundary edges than cavity triangles) */
    {
      size_t cavity = d->nfree - cavity_start;
      size_t extra = d->nbnd > cavity ? d->nbnd - cavity : 0;
      size_t cap_v = d->cap;
      size_t cap_n = d->cap;
      if (d->slots + extra > d->cap) {
        void *m;
        st = dt_grow(d->ctx, &d->v, &cap_v, d->slots + extra, 3);
        if (st == DS_OK) {
          st = dt_grow(d->ctx, &d->nb, &cap_n, d->slots + extra, 3);
        }
        if (st == DS_OK && cap_v == cap_n) {
          m = ds_context_realloc(d->ctx, d->mark, cap_v);
          if (m == NULL) {
            st = DS_ERR_ALLOC;
          } else {
            d->mark = (unsigned char *)m;
            d->cap = cap_v;
          }
        }
        if (st != DS_OK) {
          for (i = cavity_start; i < d->nfree; i++) {
            d->mark[d->free_slots[i]] = DT_ALIVE;
          }
          d->nfree = cavity_start;
          return st;
        }
      }
    }
    for (i = cavity_start; i < d->nfree; i++) {
      d->mark[d->free_slots[i]] = DT_FREE;
    }
  }
  /* the fan around p */
  for (i = 0; i < d->nbnd; i++) {
    size_t a = d->bnd[4 * i];
    size_t b = d->bnd[4 * i + 1];
    size_t u = d->bnd[4 * i + 2];
    size_t j = d->bnd[4 * i + 3];
    size_t t;
    (void)dt_new(d, a, b, pi, &t); /* capacity was reserved above */
    d->nb[3 * t + 2] = u;
    d->nb[3 * u + j] = t;
    d->fan[dt_slot_of(a, d->n)] = t;
    if (a != DT_GHOST && b != DT_GHOST) {
      d->last = t;
    }
  }
  for (i = 0; i < d->nbnd; i++) {
    size_t t = d->nb[3 * d->bnd[4 * i + 2] + d->bnd[4 * i + 3]];
    size_t next = d->fan[dt_slot_of(d->v[3 * t + 1], d->n)];
    d->nb[3 * t] = next;     /* across (b, p) */
    d->nb[3 * next + 1] = t; /* across (p, b) of the next triangle */
  }
  for (i = 0; i < d->nbnd; i++) {
    d->fan[dt_slot_of(d->bnd[4 * i], d->n)] = DT_NONE;
  }
  return DS_OK;
}

typedef struct dt_key {
  unsigned long key;
  size_t index;
} dt_key_t;

static int dt_key_cmp(const void *x, const void *y) {
  const dt_key_t *a = (const dt_key_t *)x;
  const dt_key_t *b = (const dt_key_t *)y;
  if (a->key != b->key) {
    return a->key < b->key ? -1 : 1;
  }
  return (a->index > b->index) - (a->index < b->index);
}

typedef struct dt_point {
  double x;
  double y;
} dt_point_t;

static int dt_point_cmp(const void *p, const void *q) {
  const dt_point_t *a = (const dt_point_t *)p;
  const dt_point_t *b = (const dt_point_t *)q;
  if (a->x < b->x) {
    return -1;
  }
  if (a->x > b->x) {
    return 1;
  }
  return (a->y > b->y) - (a->y < b->y);
}

static unsigned long dt_quantize(double x, double lo, double hi) {
  double f;
  if (!(hi > lo)) {
    return 0;
  }
  f = (x - lo) / (hi - lo) * 65535.0;
  if (!(f > 0.0)) {
    return 0;
  }
  if (!(f < 65535.0)) {
    return 65535UL;
  }
  return (unsigned long)f;
}

static void dt_free_work(dt_t *d) {
  ds_context_free(d->ctx, d->v);
  ds_context_free(d->ctx, d->nb);
  ds_context_free(d->ctx, d->mark);
  ds_context_free(d->ctx, d->free_slots);
  ds_context_free(d->ctx, d->stack);
  ds_context_free(d->ctx, d->bnd);
  ds_context_free(d->ctx, d->fan);
}

/* the first triangle and its three ghosts; out_used marks the three
 * vertices used */
static ds_status_t dt_start(dt_t *d, const dt_key_t *order, size_t n,
                            size_t *used, bool *out_found) {
  size_t i;
  size_t a = order[0].index;
  size_t b = DT_NONE;
  size_t c = DT_NONE;
  size_t t0;
  size_t g[3];
  size_t x[3];
  ds_status_t st;
  int k;
  *out_found = false;
  for (i = 1; i < n && b == DT_NONE; i++) {
    if (!dt_same(dt_p(d, order[i].index), dt_p(d, a))) {
      b = order[i].index;
      used[1] = i;
    }
  }
  for (i = 1; i < n && b != DT_NONE && c == DT_NONE; i++) {
    double o = ds_orient2d(dt_p(d, a), dt_p(d, b), dt_p(d, order[i].index));
    if (o > 0.0 || o < 0.0) {
      c = order[i].index;
      used[2] = i;
      if (o < 0.0) {
        size_t tmp = b;
        b = c;
        c = tmp;
      }
    }
  }
  if (c == DT_NONE) {
    return DS_OK; /* fewer than three distinct points, or all collinear */
  }
  used[0] = 0;
  x[0] = a;
  x[1] = b;
  x[2] = c;
  st = dt_new(d, a, b, c, &t0);
  for (k = 0; k < 3 && st == DS_OK; k++) {
    st = dt_new(d, x[(k + 2) % 3], x[(k + 1) % 3], DT_GHOST, &g[k]);
  }
  if (st != DS_OK) {
    return st;
  }
  for (k = 0; k < 3; k++) {
    d->nb[3 * t0 + (size_t)k] = g[k];
    d->nb[3 * g[k] + 2] = t0;
    d->nb[3 * g[k]] = g[(k + 2) % 3];
    d->nb[3 * g[(k + 2) % 3] + 1] = g[k];
  }
  d->last = t0;
  *out_found = true;
  return DS_OK;
}

ds_status_t ds_delaunay_build(ds_delaunay_t *dt, const double *xy, size_t n,
                              ds_context_t *context) {
  dt_t d;
  dt_key_t *order = NULL;
  size_t used[3];
  size_t i;
  size_t bytes;
  double lo[2];
  double hi[2];
  bool found = false;
  ds_status_t st = DS_OK;
  if (dt == NULL || (xy == NULL && n > 0)) {
    return DS_ERR_NULL;
  }
  memset(dt, 0, sizeof(*dt));
  dt->points = n;
  dt->context = context;
  for (i = 0; i < 2 * n; i++) {
    if (!(xy[i] - xy[i] < 1.0)) { /* NaN or infinite */
      return DS_ERR_INVALID;
    }
  }
  if (n < 3) {
    return DS_OK;
  }
  memset(&d, 0, sizeof(d));
  d.xy = xy;
  d.n = n;
  d.ctx = context;
  d.rng = 0x2545F491UL;
  /* Hilbert order over the bounding box */
  if (!ds_size_mul(n, sizeof(dt_key_t), &bytes)) {
    return DS_ERR_OVERFLOW;
  }
  order = (dt_key_t *)ds_context_alloc(context, bytes);
  if (order == NULL) {
    return DS_ERR_ALLOC;
  }
  lo[0] = hi[0] = xy[0];
  lo[1] = hi[1] = xy[1];
  for (i = 1; i < n; i++) {
    int c;
    for (c = 0; c < 2; c++) {
      double v = xy[2 * i + (size_t)c];
      lo[c] = v < lo[c] ? v : lo[c];
      hi[c] = v > hi[c] ? v : hi[c];
    }
  }
  for (i = 0; i < n; i++) {
    unsigned long key = 0;
    (void)ds_space_order_encode(
        DS_ORDER_HILBERT, 16, (long)dt_quantize(xy[2 * i], lo[0], hi[0]),
        (long)dt_quantize(xy[2 * i + 1], lo[1], hi[1]), &key);
    order[i].key = key;
    order[i].index = i;
  }
  qsort(order, n, sizeof(dt_key_t), dt_key_cmp);
  /* the fan map: one entry per vertex plus the ghost */
  if (!ds_size_mul(n + 1, sizeof(size_t), &bytes)) {
    st = DS_ERR_OVERFLOW;
  } else {
    d.fan = (size_t *)ds_context_alloc(context, bytes);
    if (d.fan == NULL) {
      st = DS_ERR_ALLOC;
    } else {
      for (i = 0; i <= n; i++) {
        d.fan[i] = DT_NONE;
      }
    }
  }
  if (st == DS_OK) {
    st = dt_start(&d, order, n, used, &found);
  }
  if (st == DS_OK && found) {
    for (i = 0; i < n && st == DS_OK; i++) {
      if (i == used[0] || i == used[1] || i == used[2]) {
        continue;
      }
      st = dt_insert(&d, order[i].index);
      if (st == DS_EXISTS) {
        dt->duplicates++;
        st = DS_OK;
      }
    }
  } else if (st == DS_OK) {
    /* no triangle: count the duplicates of the degenerate input, sorting
     * the points by coordinates (the Hilbert keys may interleave) */
    dt_point_t *pts = NULL;
    if (ds_size_mul(n, sizeof(dt_point_t), &bytes)) {
      pts = (dt_point_t *)ds_context_alloc(context, bytes);
    }
    if (pts == NULL) {
      st = DS_ERR_ALLOC;
    } else {
      for (i = 0; i < n; i++) {
        pts[i].x = xy[2 * i];
        pts[i].y = xy[2 * i + 1];
      }
      qsort(pts, n, sizeof(dt_point_t), dt_point_cmp);
      for (i = 1; i < n; i++) {
        if (!dt_point_cmp(&pts[i], &pts[i - 1])) {
          dt->duplicates++;
        }
      }
      ds_context_free(context, pts);
    }
  }
  /* compact the finite triangles */
  if (st == DS_OK && found) {
    size_t count = 0;
    size_t *map = NULL;
    for (i = 0; i < d.slots; i++) {
      if (d.mark[i] == DT_ALIVE && dt_ghost_at(&d, i) < 0) {
        count++;
      }
    }
    if (!ds_size_mul(d.slots, sizeof(size_t), &bytes)) {
      st = DS_ERR_OVERFLOW;
    } else {
      map = (size_t *)ds_context_alloc(context, bytes);
    }
    if (st == DS_OK && map == NULL) {
      st = DS_ERR_ALLOC;
    }
    if (st == DS_OK && !ds_size_mul(count, 3 * sizeof(size_t), &bytes)) {
      st = DS_ERR_OVERFLOW;
    }
    if (st == DS_OK) {
      dt->tri = (size_t *)ds_context_alloc(context, bytes == 0 ? 1 : bytes);
      dt->adj = (size_t *)ds_context_alloc(context, bytes == 0 ? 1 : bytes);
      if (dt->tri == NULL || dt->adj == NULL) {
        st = DS_ERR_ALLOC;
      }
    }
    if (st == DS_OK) {
      size_t o = 0;
      for (i = 0; i < d.slots; i++) {
        map[i] = DT_NONE;
        if (d.mark[i] == DT_ALIVE && dt_ghost_at(&d, i) < 0) {
          map[i] = o++;
        }
      }
      for (i = 0; i < d.slots; i++) {
        size_t k;
        if (map[i] == DT_NONE) {
          continue;
        }
        for (k = 0; k < 3; k++) {
          size_t u = d.nb[3 * i + k];
          dt->tri[3 * map[i] + k] = d.v[3 * i + k];
          dt->adj[3 * map[i] + k] = map[u];
        }
      }
      dt->triangles = count;
    } else {
      ds_context_free(context, dt->tri);
      ds_context_free(context, dt->adj);
      dt->tri = dt->adj = NULL;
    }
    ds_context_free(context, map);
  }
  dt_free_work(&d);
  ds_context_free(context, order);
  if (st != DS_OK) {
    memset(dt, 0, sizeof(*dt));
  }
  return st;
}

void ds_delaunay_destroy(ds_delaunay_t *dt) {
  if (dt == NULL) {
    return;
  }
  ds_context_free(dt->context, dt->tri);
  ds_context_free(dt->context, dt->adj);
  memset(dt, 0, sizeof(*dt));
}
