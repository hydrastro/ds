#include "plop_hash.h"
#include "checked.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

static ds_status_t pl_reserve(ds_context_t *ctx, void **p, size_t *cap,
                              size_t need, size_t elem) {
  size_t nc;
  void *q;
  if (need <= *cap) {
    return DS_OK;
  }
  nc = *cap < 4 ? 8 : *cap;
  while (nc < need) {
    if (nc > (size_t)-1 / 2 / elem) {
      return DS_ERR_OVERFLOW;
    }
    nc *= 2;
  }
  if (nc > (size_t)-1 / elem) {
    return DS_ERR_OVERFLOW; /* the first capacity alone may not fit */
  }
  q = ds_context_realloc(ctx, *p, nc * elem);
  if (q == NULL) {
    return DS_ERR_ALLOC;
  }
  *p = q;
  *cap = nc;
  return DS_OK;
}

static const double *pl_point(const ds_plop_hash_t *h, const unsigned char *r) {
  (void)h;
  return (const double *)(const void *)r;
}

ds_status_t ds_plop_init(ds_plop_hash_t *h, size_t dims, const double *lo,
                         const double *hi, size_t payload_size,
                         size_t page_size, double max_load,
                         ds_plop_split_t split, ds_context_t *context) {
  size_t d;
  if (h == NULL || lo == NULL || hi == NULL) {
    return DS_ERR_NULL;
  }
  memset(h, 0, sizeof(*h));
  if (dims == 0 || dims > DS_PLOP_MAX_DIMS || page_size == 0 ||
      !(max_load > 0.0) || max_load > 1.0 ||
      (split != DS_PLOP_MIDPOINT && split != DS_PLOP_MEDIAN)) {
    return DS_ERR_INVALID;
  }
  for (d = 0; d < dims; d++) {
    if (!(lo[d] < hi[d]) || !(fabs(lo[d]) < HUGE_VAL) ||
        !(fabs(hi[d]) < HUGE_VAL)) {
      return DS_ERR_INVALID;
    }
  }
  h->dims = dims;
  h->payload_size = payload_size;
  /* records keep the doubles aligned: round the payload up */
  {
    size_t padded;
    if (!ds_size_align(payload_size, sizeof(double), &padded) ||
        !ds_size_mul_add(dims, sizeof(double), padded, &h->record_size)) {
      return DS_ERR_OVERFLOW;
    }
  }
  h->page_size = page_size;
  h->max_load = max_load;
  h->split = split;
  h->context = context;
  for (d = 0; d < dims; d++) {
    ds_plop_axis_t *a = &h->axis[d];
    h->lo[d] = lo[d];
    h->hi[d] = hi[d];
    if (pl_reserve(context, (void **)&a->bound, &a->capacity, 1,
                   sizeof(double)) != DS_OK) {
      ds_plop_destroy(h);
      return DS_ERR_ALLOC;
    }
    a->slice =
        (size_t *)ds_context_alloc(context, a->capacity * sizeof(size_t));
    a->born = (size_t *)ds_context_alloc(context, a->capacity * sizeof(size_t));
    if (a->slice == NULL || a->born == NULL) {
      ds_plop_destroy(h);
      return DS_ERR_ALLOC;
    }
    a->bound[0] = -HUGE_VAL;
    a->slice[0] = 0;
    a->born[0] = 0;
    a->slices = 1;
    a->round = 1;
  }
  h->buckets = (ds_plop_bucket_t *)ds_context_calloc(context, 1,
                                                     sizeof(ds_plop_bucket_t));
  if (h->buckets == NULL) {
    ds_plop_destroy(h);
    return DS_ERR_ALLOC;
  }
  h->bucket_count = 1;
  h->bucket_capacity = 1;
  return DS_OK;
}

void ds_plop_destroy(ds_plop_hash_t *h) {
  size_t i;
  if (h == NULL) {
    return;
  }
  for (i = 0; i < h->dims; i++) {
    ds_context_free(h->context, h->axis[i].bound);
    ds_context_free(h->context, h->axis[i].slice);
    ds_context_free(h->context, h->axis[i].born);
  }
  for (i = 0; i < h->bucket_count; i++) {
    ds_context_free(h->context, h->buckets[i].records);
  }
  ds_context_free(h->context, h->buckets);
  ds_context_free(h->context, h->exp_axis);
  ds_context_free(h->context, h->exp_start);
  ds_context_free(h->context, h->exp_counts);
  memset(h, 0, sizeof(*h));
}

/* sorted position of the slice holding x */
static size_t pl_pos(const ds_plop_axis_t *a, double x) {
  size_t lo = 0;
  size_t hi = a->slices;
  while (lo + 1 < hi) {
    size_t mid = lo + (hi - lo) / 2;
    if (a->bound[mid] > x) {
      hi = mid;
    } else {
      lo = mid;
    }
  }
  return lo;
}

static size_t pl_address(const ds_plop_hash_t *h, const size_t *c) {
  size_t best = 0;
  size_t bd = 0;
  size_t d;
  size_t e;
  size_t off = 0;
  for (d = 0; d < h->dims; d++) {
    size_t b = h->axis[d].born[c[d]];
    if (b > best) {
      best = b;
      bd = d;
    }
  }
  if (best == 0) {
    return 0;
  }
  e = best - 1;
  for (d = 0; d < h->dims; d++) {
    if (d != bd) {
      off = off * h->exp_counts[e * h->dims + d] + c[d];
    }
  }
  return h->exp_start[e] + off;
}

static size_t pl_locate(const ds_plop_hash_t *h, const double *x) {
  size_t c[DS_PLOP_MAX_DIMS];
  size_t d;
  for (d = 0; d < h->dims; d++) {
    c[d] = h->axis[d].slice[pl_pos(&h->axis[d], x[d])];
  }
  return pl_address(h, c);
}

/* mixed-radix offset of c over every axis but skip (the layout of the
 * cells one expansion creates) */
static size_t pl_offset(const ds_plop_hash_t *h, const size_t *c,
                        const size_t *counts, size_t skip) {
  size_t off = 0;
  size_t d;
  for (d = 0; d < h->dims; d++) {
    if (d != skip) {
      off = off * counts[d] + c[d];
    }
  }
  return off;
}

static int pl_cmp_double(const void *x, const void *y) {
  double a = *(const double *)x;
  double b = *(const double *)y;
  return a < b ? -1 : a > b ? 1 : 0;
}

/* odometer over every axis but `skip` (counts per axis); returns false
 * after the last combination */
static bool pl_next(size_t *c, const size_t *counts, size_t dims, size_t skip) {
  size_t d;
  for (d = 0; d < dims; d++) {
    if (d == skip) {
      continue;
    }
    if (++c[d] < counts[d]) {
      return true;
    }
    c[d] = 0;
  }
  return false;
}

static double pl_split_value(const ds_plop_hash_t *h, size_t d, double L,
                             double H, size_t s) {
  double a;
  double b;
  double width = h->hi[d] - h->lo[d];
  if (h->split == DS_PLOP_MEDIAN) {
    /* gather the coordinates of the slice's records */
    size_t counts[DS_PLOP_MAX_DIMS];
    size_t c[DS_PLOP_MAX_DIMS];
    size_t n = 0;
    size_t i;
    double *xs = NULL;
    size_t cap = 0;
    bool ok = true;
    for (i = 0; i < h->dims; i++) {
      counts[i] = h->axis[i].slices;
      c[i] = 0;
    }
    c[d] = s;
    do {
      const ds_plop_bucket_t *bk = &h->buckets[pl_address(h, c)];
      size_t r;
      if (pl_reserve(h->context, (void **)&xs, &cap, n + bk->count + 1,
                     sizeof(double)) != DS_OK) {
        ok = false;
        break;
      }
      for (r = 0; r < bk->count; r++) {
        xs[n++] = pl_point(h, bk->records + r * h->record_size)[d];
      }
    } while (pl_next(c, counts, h->dims, d));
    if (ok && n > 1) {
      double v;
      qsort(xs, n, sizeof(double), pl_cmp_double);
      v = xs[n / 2];
      if (!(v > xs[0])) {
        for (i = n / 2; i < n && !(xs[i] > xs[0]); i++) {
        }
        v = i < n ? xs[i] : v;
      }
      ds_context_free(h->context, xs);
      if (v > L && v < H) {
        return v;
      }
    } else {
      ds_context_free(h->context, xs);
    }
  }
  a = L > h->lo[d] ? L : h->lo[d];
  b = H < h->hi[d] ? H : h->hi[d];
  if (!(a < b)) {
    if (fabs(L) < HUGE_VAL && fabs(H) < HUGE_VAL) {
      a = L;
      b = H;
    } else if (fabs(L) < HUGE_VAL) {
      a = L;
      b = L + (width > 1.0 ? width : 1.0);
    } else {
      a = H - (width > 1.0 ? width : 1.0);
      b = H;
    }
  }
  return a + (b - a) / 2.0;
}

static ds_status_t pl_expand(ds_plop_hash_t *h) {
  size_t d = h->turn;
  ds_plop_axis_t *ax = &h->axis[d];
  size_t s = ax->pointer;
  size_t k;
  size_t e = h->expansions;
  size_t counts[DS_PLOP_MAX_DIMS];
  size_t c[DS_PLOP_MAX_DIMS];
  size_t cells = 1;
  size_t i;
  size_t cap;
  double L;
  double H;
  double v;
  ds_status_t st;
  for (i = 0; i < h->dims; i++) {
    counts[i] = h->axis[i].slices;
    if (i != d) {
      if (cells > (size_t)-1 / counts[i]) {
        return DS_ERR_OVERFLOW;
      }
      cells *= counts[i];
    }
  }
  if (h->bucket_count > (size_t)-1 - cells) {
    return DS_ERR_OVERFLOW;
  }
  for (k = 0; ax->slice[k] != s; k++) {
  }
  L = ax->bound[k];
  H = k + 1 < ax->slices ? ax->bound[k + 1] : HUGE_VAL;
  v = pl_split_value(h, d, L, H, s);
  if (!(v > L)) {
    v = L; /* degenerate: the old slice becomes empty */
  }
  /* reserve everything first, so a failure changes nothing */
  cap = h->exp_capacity;
  st = pl_reserve(h->context, (void **)&h->exp_axis, &cap, e + 1,
                  sizeof(size_t));
  if (st == DS_OK) {
    cap = h->exp_capacity;
    st = pl_reserve(h->context, (void **)&h->exp_start, &cap, e + 1,
                    sizeof(size_t));
  }
  if (st == DS_OK) {
    size_t need;
    st = ds_size_mul(e + 1, h->dims, &need)
             ? pl_reserve(h->context, (void **)&h->exp_counts,
                          &h->exp_counts_capacity, need, sizeof(size_t))
             : DS_ERR_OVERFLOW;
  }
  if (st != DS_OK) {
    return st;
  }
  h->exp_capacity = cap;
  cap = ax->capacity;
  st = pl_reserve(h->context, (void **)&ax->bound, &cap, ax->slices + 1,
                  sizeof(double));
  if (st == DS_OK) {
    cap = ax->capacity;
    st = pl_reserve(h->context, (void **)&ax->slice, &cap, ax->slices + 1,
                    sizeof(size_t));
  }
  if (st == DS_OK) {
    cap = ax->capacity;
    st = pl_reserve(h->context, (void **)&ax->born, &cap, ax->slices + 1,
                    sizeof(size_t));
  }
  if (st != DS_OK) {
    return st;
  }
  ax->capacity = cap;
  st = pl_reserve(h->context, (void **)&h->buckets, &h->bucket_capacity,
                  h->bucket_count + cells, sizeof(ds_plop_bucket_t));
  if (st != DS_OK) {
    return st;
  }
  /* pre-size the new buckets: count the movers per new cell */
  memset(&h->buckets[h->bucket_count], 0, cells * sizeof(ds_plop_bucket_t));
  for (i = 0; i < h->dims; i++) {
    c[i] = 0;
  }
  c[d] = s;
  do {
    const ds_plop_bucket_t *bk = &h->buckets[pl_address(h, c)];
    ds_plop_bucket_t *nb =
        &h->buckets[h->bucket_count + pl_offset(h, c, counts, d)];
    size_t r;
    size_t movers = 0;
    for (r = 0; r < bk->count; r++) {
      if (!(pl_point(h, bk->records + r * h->record_size)[d] < v)) {
        movers++;
      }
    }
    if (movers > 0) {
      nb->records = (unsigned char *)ds_context_alloc(h->context,
                                                      movers * h->record_size);
      if (nb->records == NULL) {
        size_t j;
        for (j = 0; j < cells; j++) {
          ds_context_free(h->context, h->buckets[h->bucket_count + j].records);
        }
        return DS_ERR_ALLOC;
      }
      nb->capacity = movers;
    }
  } while (pl_next(c, counts, h->dims, d));
  /* commit: log, slice, then move the records */
  h->exp_axis[e] = d;
  h->exp_start[e] = h->bucket_count;
  for (i = 0; i < h->dims; i++) {
    h->exp_counts[e * h->dims + i] = counts[i];
  }
  h->expansions++;
  memmove(ax->bound + k + 2, ax->bound + k + 1,
          (ax->slices - k - 1) * sizeof(double));
  memmove(ax->slice + k + 2, ax->slice + k + 1,
          (ax->slices - k - 1) * sizeof(size_t));
  ax->bound[k + 1] = v;
  ax->slice[k + 1] = ax->slices;
  ax->born[ax->slices] = e + 1;
  ax->slices++;
  for (i = 0; i < h->dims; i++) {
    c[i] = 0;
  }
  do {
    ds_plop_bucket_t *bk;
    ds_plop_bucket_t *nb =
        &h->buckets[h->bucket_count + pl_offset(h, c, counts, d)];
    size_t r;
    size_t w = 0;
    c[d] = s;
    bk = &h->buckets[pl_address(h, c)];
    for (r = 0; r < bk->count; r++) {
      unsigned char *rec = bk->records + r * h->record_size;
      if (!(pl_point(h, rec)[d] < v)) {
        memcpy(nb->records + nb->count * h->record_size, rec, h->record_size);
        nb->count++;
      } else {
        if (w != r) {
          memcpy(bk->records + w * h->record_size, rec, h->record_size);
        }
        w++;
      }
    }
    bk->count = w;
    c[d] = 0;
  } while (pl_next(c, counts, h->dims, d));
  h->bucket_count += cells;
  if (++ax->pointer == ax->round) {
    ax->pointer = 0;
    ax->round = ax->slices;
    h->turn = (h->turn + 1) % h->dims;
  }
  return DS_OK;
}

double ds_plop_load(const ds_plop_hash_t *h) {
  if (h == NULL || h->bucket_count == 0) {
    return 0.0;
  }
  return (double)h->records / ((double)h->bucket_count * (double)h->page_size);
}

ds_status_t ds_plop_insert(ds_plop_hash_t *h, const double *point,
                           const void *payload) {
  ds_plop_bucket_t *bk;
  size_t d;
  ds_status_t st;
  if (h == NULL || point == NULL || (h->payload_size > 0 && payload == NULL)) {
    return DS_ERR_NULL;
  }
  for (d = 0; d < h->dims; d++) {
    if (!(fabs(point[d]) < HUGE_VAL)) {
      return DS_ERR_INVALID;
    }
  }
  bk = &h->buckets[pl_locate(h, point)];
  st = pl_reserve(h->context, (void **)&bk->records, &bk->capacity,
                  bk->count + 1, h->record_size);
  if (st != DS_OK) {
    return st;
  }
  memset(bk->records + bk->count * h->record_size, 0, h->record_size);
  memcpy(bk->records + bk->count * h->record_size, point,
         h->dims * sizeof(double));
  if (h->payload_size > 0) {
    memcpy(bk->records + bk->count * h->record_size + h->dims * sizeof(double),
           payload, h->payload_size);
  }
  bk->count++;
  h->records++;
  /* expansion failures leave a valid, fuller file; the next insertion
   * tries again */
  while (ds_plop_load(h) > h->max_load && pl_expand(h) == DS_OK) {
  }
  return DS_OK;
}

static bool pl_same(const ds_plop_hash_t *h, const unsigned char *rec,
                    const double *p) {
  const double *x = pl_point(h, rec);
  size_t d;
  for (d = 0; d < h->dims; d++) {
    if (x[d] < p[d] || x[d] > p[d]) {
      return false;
    }
  }
  return true;
}

ds_status_t ds_plop_find(const ds_plop_hash_t *h, const double *point,
                         void *payload_out) {
  const ds_plop_bucket_t *bk;
  size_t r;
  if (h == NULL || point == NULL) {
    return DS_ERR_NULL;
  }
  bk = &h->buckets[pl_locate(h, point)];
  for (r = 0; r < bk->count; r++) {
    const unsigned char *rec = bk->records + r * h->record_size;
    if (pl_same(h, rec, point)) {
      if (payload_out != NULL && h->payload_size > 0) {
        memcpy(payload_out, rec + h->dims * sizeof(double), h->payload_size);
      }
      return DS_OK;
    }
  }
  return DS_ERR_RANGE;
}

ds_status_t ds_plop_remove(ds_plop_hash_t *h, const double *point) {
  ds_plop_bucket_t *bk;
  size_t r;
  if (h == NULL || point == NULL) {
    return DS_ERR_NULL;
  }
  bk = &h->buckets[pl_locate(h, point)];
  for (r = 0; r < bk->count; r++) {
    unsigned char *rec = bk->records + r * h->record_size;
    if (pl_same(h, rec, point)) {
      bk->count--;
      if (r != bk->count) {
        memcpy(rec, bk->records + bk->count * h->record_size, h->record_size);
      }
      h->records--;
      return DS_OK;
    }
  }
  return DS_ERR_RANGE;
}

ds_status_t ds_plop_range(const ds_plop_hash_t *h, const double *lo,
                          const double *hi, ds_plop_visit_fn visit,
                          void *user) {
  size_t first[DS_PLOP_MAX_DIMS];
  size_t span[DS_PLOP_MAX_DIMS];
  size_t at[DS_PLOP_MAX_DIMS];
  size_t c[DS_PLOP_MAX_DIMS];
  size_t d;
  if (h == NULL || lo == NULL || hi == NULL || visit == NULL) {
    return DS_ERR_NULL;
  }
  memset(c, 0, sizeof(c));
  for (d = 0; d < h->dims; d++) {
    size_t a;
    size_t b;
    if (lo[d] > hi[d]) {
      return DS_OK;
    }
    a = pl_pos(&h->axis[d], lo[d]);
    b = pl_pos(&h->axis[d], hi[d]);
    first[d] = a;
    span[d] = b - a + 1;
    at[d] = 0;
  }
  for (;;) {
    const ds_plop_bucket_t *bk;
    size_t r;
    for (d = 0; d < h->dims; d++) {
      c[d] = h->axis[d].slice[first[d] + at[d]];
    }
    bk = &h->buckets[pl_address(h, c)];
    for (r = 0; r < bk->count; r++) {
      const unsigned char *rec = bk->records + r * h->record_size;
      const double *x = pl_point(h, rec);
      bool in = true;
      for (d = 0; in && d < h->dims; d++) {
        in = !(x[d] < lo[d]) && !(x[d] > hi[d]);
      }
      if (in && !visit(x, rec + h->dims * sizeof(double), user)) {
        return DS_OK;
      }
    }
    for (d = 0; d < h->dims; d++) {
      if (++at[d] < span[d]) {
        break;
      }
      at[d] = 0;
    }
    if (d == h->dims) {
      return DS_OK;
    }
  }
}

size_t ds_plop_size(const ds_plop_hash_t *h) {
  return h == NULL ? 0 : h->records;
}

size_t ds_plop_buckets(const ds_plop_hash_t *h) {
  return h == NULL ? 0 : h->bucket_count;
}

size_t ds_plop_overflow_pages(const ds_plop_hash_t *h) {
  size_t i;
  size_t n = 0;
  if (h == NULL) {
    return 0;
  }
  for (i = 0; i < h->bucket_count; i++) {
    size_t pages = (h->buckets[i].count + h->page_size - 1) / h->page_size;
    if (pages > 1) {
      n += pages - 1;
    }
  }
  return n;
}
