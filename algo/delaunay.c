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
  ds_context_free(dt->context, dt->fixed);
  memset(dt, 0, sizeof(*dt));
}

/* ---- constrained Delaunay triangulation ---- */

#define CD_NONE DS_DELAUNAY_NONE

typedef struct cd {
  ds_delaunay_t *dt;
  const double *xy;
  size_t *vtri;  /* per point: an incident triangle */
  size_t *rep;   /* per point: the inserted point standing for it */
  size_t *queue; /* crossed edges still to flip, as vertex pairs */
  size_t *fresh; /* edges the flips made, as vertex pairs */
  size_t cap;    /* pairs the two arrays hold */
} cd_t;

static const double *cd_p(const cd_t *c, size_t v) { return c->xy + 2 * v; }

static unsigned cd_at(const ds_delaunay_t *dt, size_t t, size_t v) {
  unsigned k;
  for (k = 0; k < 3; k++) {
    if (dt->tri[3 * t + k] == v) {
      return k;
    }
  }
  return 3;
}

static ds_status_t cd_reserve(cd_t *c, size_t pairs) {
  ds_context_t *ctx = c->dt->context;
  size_t cap = c->cap < 16 ? 16 : c->cap;
  size_t *q;
  if (pairs <= c->cap) {
    return DS_OK;
  }
  while (cap < pairs) {
    if (cap > (size_t)-1 / 8 / sizeof(size_t)) {
      return DS_ERR_OVERFLOW;
    }
    cap *= 2;
  }
  q = (size_t *)ds_context_realloc(ctx, c->queue, 2 * cap * sizeof(size_t));
  if (q == NULL) {
    return DS_ERR_ALLOC;
  }
  c->queue = q;
  q = (size_t *)ds_context_realloc(ctx, c->fresh, 2 * cap * sizeof(size_t));
  if (q == NULL) {
    return DS_ERR_ALLOC; /* the queue grew; the next call retries */
  }
  c->fresh = q;
  c->cap = cap;
  return DS_OK;
}

/* the triangle of the fan around a whose angle at a holds the direction
 * of b, or a fan vertex on the segment; *kind 0: b is a fan vertex (the
 * edge exists, *t and *k name it), 1: a fan vertex *through lies on the
 * segment, 2: the segment leaves a across the edge opposite a in *t */
static int cd_start(const cd_t *c, size_t a, size_t b, size_t *t, unsigned *k,
                    size_t *through) {
  const ds_delaunay_t *dt = c->dt;
  size_t first = c->vtri[a];
  size_t cur;
  int dir;
  for (dir = 0; dir < 2; dir++) {
    cur = first;
    do {
      unsigned ka = cd_at(dt, cur, a);
      size_t p = dt->tri[3 * cur + (ka + 1) % 3];
      size_t q = dt->tri[3 * cur + (ka + 2) % 3];
      double op = ds_orient2d(cd_p(c, a), cd_p(c, b), cd_p(c, p));
      double oq = ds_orient2d(cd_p(c, a), cd_p(c, b), cd_p(c, q));
      const double *pa = cd_p(c, a);
      const double *pb = cd_p(c, b);
      if (p == b || q == b) {
        *t = cur;
        *k = p == b ? (ka + 2) % 3 : (ka + 1) % 3;
        return 0;
      }
      if (!(op < 0.0) && !(op > 0.0) &&
          (cd_p(c, p)[0] - pa[0]) * (pb[0] - pa[0]) +
                  (cd_p(c, p)[1] - pa[1]) * (pb[1] - pa[1]) >
              0.0) {
        *through = p;
        return 1;
      }
      if (!(oq < 0.0) && !(oq > 0.0) &&
          (cd_p(c, q)[0] - pa[0]) * (pb[0] - pa[0]) +
                  (cd_p(c, q)[1] - pa[1]) * (pb[1] - pa[1]) >
              0.0) {
        *through = q;
        return 1;
      }
      if (op < 0.0 && oq > 0.0) {
        *t = cur;
        *k = ka;
        return 2;
      }
      /* around a: counter-clockwise first, then clockwise from the start */
      cur = dt->adj[3 * cur + (dir == 0 ? (ka + 1) % 3 : (ka + 2) % 3)];
    } while (cur != CD_NONE && cur != first);
    if (cur == first) {
      break; /* a closed fan has been seen whole */
    }
  }
  return -1;
}

/* mark the edge held by t opposite its vertex k, on both sides */
static void cd_fix(ds_delaunay_t *dt, size_t t, unsigned k) {
  size_t n = dt->adj[3 * t + k];
  dt->fixed[3 * t + k] = 1;
  if (n != CD_NONE) {
    size_t u = dt->tri[3 * t + (k + 1) % 3];
    size_t v = dt->tri[3 * t + (k + 2) % 3];
    unsigned kn;
    for (kn = 0; kn < 3; kn++) {
      if (dt->tri[3 * n + kn] != u && dt->tri[3 * n + kn] != v) {
        dt->fixed[3 * n + kn] = 1;
      }
    }
  }
}

/* the triangle holding the edge (u, v) and the index of its vertex
 * opposite the edge; false when (u, v) is not an edge */
static bool cd_find_edge(const cd_t *c, size_t u, size_t v, size_t *t,
                         unsigned *k) {
  size_t through;
  return cd_start(c, u, v, t, k, &through) == 0;
}

/* whether segments (p, q) and (r, s) cross at a point inside both */
static bool cd_cross(const cd_t *c, size_t p, size_t q, size_t r, size_t s) {
  double o1 = ds_orient2d(cd_p(c, p), cd_p(c, q), cd_p(c, r));
  double o2 = ds_orient2d(cd_p(c, p), cd_p(c, q), cd_p(c, s));
  double o3 = ds_orient2d(cd_p(c, r), cd_p(c, s), cd_p(c, p));
  double o4 = ds_orient2d(cd_p(c, r), cd_p(c, s), cd_p(c, q));
  return ((o1 > 0.0 && o2 < 0.0) || (o1 < 0.0 && o2 > 0.0)) &&
         ((o3 > 0.0 && o4 < 0.0) || (o3 < 0.0 && o4 > 0.0));
}

/* the neighbour of t across the edge opposite its vertex k, and the index
 * there of the vertex opposite the same edge */
static unsigned cd_mirror(const ds_delaunay_t *dt, size_t t, unsigned k,
                          size_t n) {
  size_t u = dt->tri[3 * t + (k + 1) % 3];
  size_t v = dt->tri[3 * t + (k + 2) % 3];
  unsigned kn;
  for (kn = 0; kn < 3; kn++) {
    if (dt->tri[3 * n + kn] != u && dt->tri[3 * n + kn] != v) {
      return kn;
    }
  }
  return 3;
}

/* point the neighbour n's edge that faced old to now */
static void cd_repoint(ds_delaunay_t *dt, size_t n, size_t old, size_t now) {
  unsigned q;
  if (n == CD_NONE) {
    return;
  }
  for (q = 0; q < 3; q++) {
    if (dt->adj[3 * n + q] == old) {
      dt->adj[3 * n + q] = now;
      return;
    }
  }
}

/* flip the edge of t1 opposite its vertex k1: the quadrilateral x, p, y, q
 * (counter-clockwise, x in t1, y in t2) gets the diagonal x-y */
static void cd_flip(cd_t *c, size_t t1, unsigned k1) {
  ds_delaunay_t *dt = c->dt;
  size_t t2 = dt->adj[3 * t1 + k1];
  unsigned k2 = cd_mirror(dt, t1, k1, t2);
  size_t x = dt->tri[3 * t1 + k1];
  size_t p = dt->tri[3 * t1 + (k1 + 1) % 3];
  size_t q = dt->tri[3 * t1 + (k1 + 2) % 3];
  size_t y = dt->tri[3 * t2 + k2];
  size_t a1 = dt->adj[3 * t1 + (k1 + 1) % 3]; /* across (q, x) */
  size_t a2 = dt->adj[3 * t1 + (k1 + 2) % 3]; /* across (x, p) */
  size_t b1 = dt->adj[3 * t2 + (k2 + 1) % 3]; /* across (p, y) */
  size_t b2 = dt->adj[3 * t2 + (k2 + 2) % 3]; /* across (y, q) */
  unsigned char fa1 = dt->fixed[3 * t1 + (k1 + 1) % 3];
  unsigned char fa2 = dt->fixed[3 * t1 + (k1 + 2) % 3];
  unsigned char fb1 = dt->fixed[3 * t2 + (k2 + 1) % 3];
  unsigned char fb2 = dt->fixed[3 * t2 + (k2 + 2) % 3];
  /* t1 = (x, p, y), t2 = (y, q, x) */
  dt->tri[3 * t1] = x;
  dt->tri[3 * t1 + 1] = p;
  dt->tri[3 * t1 + 2] = y;
  dt->adj[3 * t1] = b1;
  dt->adj[3 * t1 + 1] = t2;
  dt->adj[3 * t1 + 2] = a2;
  dt->fixed[3 * t1] = fb1;
  dt->fixed[3 * t1 + 1] = 0;
  dt->fixed[3 * t1 + 2] = fa2;
  dt->tri[3 * t2] = y;
  dt->tri[3 * t2 + 1] = q;
  dt->tri[3 * t2 + 2] = x;
  dt->adj[3 * t2] = a1;
  dt->adj[3 * t2 + 1] = t1;
  dt->adj[3 * t2 + 2] = b2;
  dt->fixed[3 * t2] = fa1;
  dt->fixed[3 * t2 + 1] = 0;
  dt->fixed[3 * t2 + 2] = fb2;
  cd_repoint(dt, b1, t2, t1);
  cd_repoint(dt, a1, t1, t2);
  c->vtri[x] = t1;
  c->vtri[p] = t1;
  c->vtri[y] = t1;
  c->vtri[q] = t2;
}

/*
 * Insert the segment (a, b), or its first piece up to a point lying on it
 * (*stop: where it ended), by Sloan's method ("A fast algorithm for
 * generating constrained Delaunay triangulations", 1993): collect the
 * edges the segment crosses; flip them one at a time, each only when its
 * quadrilateral is strictly convex (one always is), keeping the new edges
 * that still cross in the queue; then make the new edges that do not
 * cross locally Delaunay again by flips. Interior vertices and any
 * configuration of crossed triangles need no special case, and edges
 * already constrained are never flipped.
 */
static ds_status_t cd_segment(cd_t *c, size_t a, size_t b, size_t *stop) {
  ds_delaunay_t *dt = c->dt;
  size_t t;
  unsigned k;
  size_t through = CD_NONE;
  size_t u;
  size_t w;
  size_t end = b;
  size_t nq = 0;
  size_t head = 0;
  size_t nf = 0;
  size_t budget;
  ds_status_t st;
  int how = cd_start(c, a, b, &t, &k, &through);
  *stop = b;
  if (how < 0) {
    return DS_ERR_STATE;
  }
  if (how == 0) {
    cd_fix(dt, t, k);
    return DS_OK;
  }
  if (how == 1) {
    /* the edge to the point on the segment; the rest comes next */
    *stop = through;
    if (!cd_find_edge(c, a, through, &t, &k)) {
      return DS_ERR_STATE;
    }
    cd_fix(dt, t, k);
    return DS_OK;
  }
  /* the crossed edges, by a walk; nothing changes until they are known */
  u = dt->tri[3 * t + (k + 2) % 3]; /* left of a -> b */
  w = dt->tri[3 * t + (k + 1) % 3]; /* right */
  for (;;) {
    size_t n;
    size_t r;
    unsigned kn;
    double o;
    if (dt->fixed[3 * t + k]) {
      return DS_ERR_INVALID; /* crosses a constrained edge */
    }
    st = cd_reserve(c, nq + 1);
    if (st != DS_OK) {
      return st;
    }
    c->queue[2 * nq] = u;
    c->queue[2 * nq + 1] = w;
    nq++;
    n = dt->adj[3 * t + k];
    if (n == CD_NONE) {
      return DS_ERR_STATE; /* out of the hull: cannot happen */
    }
    kn = cd_mirror(dt, t, k, n);
    if (kn == 3) {
      return DS_ERR_STATE;
    }
    r = dt->tri[3 * n + kn];
    if (r == b) {
      break;
    }
    o = ds_orient2d(cd_p(c, a), cd_p(c, b), cd_p(c, r));
    if (!(o < 0.0) && !(o > 0.0)) {
      end = r; /* r lies on the segment: stop there */
      *stop = r;
      break;
    }
    /* the next crossed edge: (r, w) when r is left, (u, r) when right */
    if (o > 0.0) {
      u = r;
    } else {
      w = r;
    }
    t = n;
    {
      unsigned q;
      k = 3;
      for (q = 0; q < 3; q++) {
        size_t z = dt->tri[3 * t + q];
        if (z != u && z != w) {
          k = q;
        }
      }
    }
    if (k == 3) {
      return DS_ERR_STATE;
    }
  }
  st = cd_reserve(c, nq + 1);
  if (st != DS_OK) {
    return st;
  }
  /* flip the crossed edges away: a queue, each edge flipped once its
   * quadrilateral is convex; the work is bounded as a guard */
  budget = 64 + 4 * nq * nq;
  while (head != nq) {
    size_t eu = c->queue[2 * (head % c->cap)];
    size_t ev = c->queue[2 * (head % c->cap) + 1];
    size_t t1;
    unsigned k1;
    size_t t2;
    unsigned k2;
    size_t x;
    size_t y;
    head++;
    if (budget-- == 0 || !cd_find_edge(c, eu, ev, &t1, &k1)) {
      return DS_ERR_STATE;
    }
    t2 = dt->adj[3 * t1 + k1];
    if (t2 == CD_NONE) {
      return DS_ERR_STATE;
    }
    k2 = cd_mirror(dt, t1, k1, t2);
    x = dt->tri[3 * t1 + k1];
    y = dt->tri[3 * t2 + k2];
    if (!cd_cross(c, eu, ev, x, y)) {
      /* not convex yet: try again later */
      c->queue[2 * (nq % c->cap)] = eu;
      c->queue[2 * (nq % c->cap) + 1] = ev;
      nq++;
      continue;
    }
    cd_flip(c, t1, k1);
    if (cd_cross(c, a, end, x, y)) {
      c->queue[2 * (nq % c->cap)] = x;
      c->queue[2 * (nq % c->cap) + 1] = y;
      nq++;
    } else if (!((x == a && y == end) || (x == end && y == a))) {
      c->fresh[2 * nf] = x;
      c->fresh[2 * nf + 1] = y;
      nf++;
    }
  }
  if (!cd_find_edge(c, a, end, &t, &k)) {
    return DS_ERR_STATE;
  }
  cd_fix(dt, t, k);
  /* the new edges back to locally Delaunay, flipping until none moves */
  {
    bool moved = true;
    budget = 64 + 4 * nf * nf;
    while (moved) {
      size_t i;
      moved = false;
      for (i = 0; i < nf; i++) {
        size_t t1;
        unsigned k1;
        size_t t2;
        unsigned k2;
        if (!cd_find_edge(c, c->fresh[2 * i], c->fresh[2 * i + 1], &t1, &k1)) {
          return DS_ERR_STATE;
        }
        t2 = dt->adj[3 * t1 + k1];
        if (t2 == CD_NONE || dt->fixed[3 * t1 + k1]) {
          continue;
        }
        k2 = cd_mirror(dt, t1, k1, t2);
        if (ds_incircle(cd_p(c, dt->tri[3 * t1]), cd_p(c, dt->tri[3 * t1 + 1]),
                        cd_p(c, dt->tri[3 * t1 + 2]),
                        cd_p(c, dt->tri[3 * t2 + k2])) > 0.0) {
          size_t x = dt->tri[3 * t1 + k1];
          size_t y = dt->tri[3 * t2 + k2];
          if (budget-- == 0) {
            return DS_ERR_STATE;
          }
          cd_flip(c, t1, k1);
          c->fresh[2 * i] = x;
          c->fresh[2 * i + 1] = y;
          moved = true;
        }
      }
    }
  }
  return DS_OK;
}

static int cd_point_cmp(const void *x, const void *y, const double *xy) {
  size_t i = *(const size_t *)x;
  size_t j = *(const size_t *)y;
  if (xy[2 * i] < xy[2 * j]) {
    return -1;
  }
  if (xy[2 * i] > xy[2 * j]) {
    return 1;
  }
  if (xy[2 * i + 1] < xy[2 * j + 1]) {
    return -1;
  }
  return xy[2 * i + 1] > xy[2 * j + 1] ? 1 : 0;
}

/* sort indices by coordinates (heap sort: no comparator context in C89
 * qsort) */
static void cd_sort(size_t *a, size_t n, const double *xy) {
  size_t start = n / 2;
  size_t end = n;
  while (end > 1) {
    size_t root;
    if (start > 0) {
      start--;
    } else {
      size_t t = a[0];
      end--;
      a[0] = a[end];
      a[end] = t;
    }
    root = start;
    for (;;) {
      size_t child = 2 * root + 1;
      size_t t;
      if (child >= end) {
        break;
      }
      if (child + 1 < end && cd_point_cmp(&a[child], &a[child + 1], xy) < 0) {
        child++;
      }
      if (cd_point_cmp(&a[root], &a[child], xy) >= 0) {
        break;
      }
      t = a[root];
      a[root] = a[child];
      a[child] = t;
      root = child;
    }
  }
}

ds_status_t ds_delaunay_constrain(ds_delaunay_t *dt, const double *xy,
                                  const size_t *segments, size_t count,
                                  size_t *inserted) {
  cd_t c;
  size_t *order = NULL;
  size_t i;
  size_t n;
  size_t done = 0;
  ds_status_t st = DS_OK;
  if (inserted != NULL) {
    *inserted = 0;
  }
  if (dt == NULL || xy == NULL || (count > 0 && segments == NULL)) {
    return DS_ERR_NULL;
  }
  n = dt->points;
  for (i = 0; i < 2 * count; i++) {
    if (segments[i] >= n) {
      return DS_ERR_RANGE;
    }
  }
  if (count == 0) {
    return DS_OK;
  }
  if (dt->triangles == 0) {
    return DS_ERR_STATE;
  }
  memset(&c, 0, sizeof(c));
  c.dt = dt;
  c.xy = xy;
  if (dt->fixed == NULL) {
    dt->fixed =
        (unsigned char *)ds_context_calloc(dt->context, 3 * dt->triangles, 1);
    if (dt->fixed == NULL) {
      return DS_ERR_ALLOC;
    }
  }
  c.vtri = (size_t *)ds_context_alloc(dt->context, n * sizeof(size_t));
  c.rep = (size_t *)ds_context_alloc(dt->context, n * sizeof(size_t));
  order = (size_t *)ds_context_alloc(dt->context, n * sizeof(size_t));
  if (c.vtri == NULL || c.rep == NULL || order == NULL ||
      cd_reserve(&c, 16) != DS_OK) {
    st = DS_ERR_ALLOC;
    goto done;
  }
  for (i = 0; i < n; i++) {
    c.vtri[i] = CD_NONE;
    order[i] = i;
  }
  for (i = 0; i < dt->triangles; i++) {
    unsigned k;
    for (k = 0; k < 3; k++) {
      c.vtri[dt->tri[3 * i + k]] = i;
    }
  }
  /* each point stands for the inserted point with its coordinates */
  cd_sort(order, n, xy);
  for (i = 0; i < n;) {
    size_t j = i;
    size_t r = CD_NONE;
    while (j < n && cd_point_cmp(&order[i], &order[j], xy) == 0) {
      if (c.vtri[order[j]] != CD_NONE) {
        r = order[j];
      }
      j++;
    }
    for (; i < j; i++) {
      c.rep[order[i]] = r;
    }
  }
  for (done = 0; done < count && st == DS_OK; done++) {
    size_t a = c.rep[segments[2 * done]];
    size_t b = c.rep[segments[2 * done + 1]];
    if (a == CD_NONE || b == CD_NONE) {
      st = DS_ERR_STATE; /* a point in no triangle: degenerate input */
      break;
    }
    /* a segment through points on it goes in piece by piece */
    while (a != b && st == DS_OK) {
      size_t stop;
      st = cd_segment(&c, a, b, &stop);
      a = stop;
    }
    if (st != DS_OK) {
      break;
    }
  }
done:
  ds_context_free(dt->context, c.vtri);
  ds_context_free(dt->context, c.rep);
  ds_context_free(dt->context, c.queue);
  ds_context_free(dt->context, c.fresh);
  ds_context_free(dt->context, order);
  if (inserted != NULL) {
    *inserted = done;
  }
  return st;
}

bool ds_delaunay_is_constrained(const ds_delaunay_t *dt, size_t t, unsigned k) {
  return dt != NULL && dt->fixed != NULL && t < dt->triangles && k < 3 &&
         dt->fixed[3 * t + k] != 0;
}
