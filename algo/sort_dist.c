#include "sort_dist.h"
#include "select.h"
#include <string.h>

#define AT(base, i, es) ((unsigned char *)(base) + (size_t)(i) * (es))
#define CAT(base, i, es) ((const unsigned char *)(base) + (size_t)(i) * (es))
#define SD_NONE (~(size_t)0)
#define SD_SMALL 16
#define SD_RADIX 257 /* end-of-key bucket + 256 byte values */
#define SD_STACK_INLINE 192
#define SD_SHUFFLE_SMALL 64
#define SD_J_SMALL 40
#define SD_J_STACK_BYTES 2048

typedef size_t (*sd_bucket_func_t)(const void *elem, void *ctx);

static void *sd_alloc(ds_context_t *ctx, size_t count, size_t size) {
  if (count == 0) {
    count = 1;
  }
  if (size != 0 && count > ((size_t)-1) / size) {
    return NULL;
  }
  return ds_context_alloc(ctx, count * size);
}

static void sd_key_range(const void *base, size_t count, size_t es,
                         ds_key_func_t key, void *user, unsigned long *out_min,
                         unsigned long *out_max) {
  size_t i;
  unsigned long k;
  unsigned long mn = key(base, user);
  unsigned long mx = mn;
  for (i = 1; i < count; i++) {
    k = key(CAT(base, i, es), user);
    if (k < mn) {
      mn = k;
    }
    if (k > mx) {
      mx = k;
    }
  }
  *out_min = mn;
  *out_max = mx;
}

/* Monotone map of [mn, mx] onto [0, m). */
static size_t sd_interp(unsigned long k, unsigned long mn, unsigned long mx,
                        size_t m) {
  double f;
  size_t b;
  if (mx <= mn || m <= 1) {
    return 0;
  }
  f = (double)(k - mn) * (double)m / ((double)(mx - mn) + 1.0);
  b = f > 0.0 ? (size_t)f : 0;
  return b >= m ? m - 1 : b;
}

/* Stable distribution of base into buckets through scratch. counts has
 * buckets + 1 entries; afterwards bucket b is [start(b), counts[b]) with
 * start(0) = 0 and start(b) = counts[b - 1]. */
static ds_status_t sd_distribute(void *base, size_t count, size_t es,
                                 sd_bucket_func_t bf, void *ctx, size_t buckets,
                                 void *scratch, size_t *counts) {
  size_t i;
  size_t b;
  size_t sum = 0;
  size_t c;
  memset(counts, 0, (buckets + 1) * sizeof(size_t));
  for (i = 0; i < count; i++) {
    b = bf(CAT(base, i, es), ctx);
    if (b >= buckets) {
      return DS_ERR_RANGE;
    }
    counts[b + 1]++;
  }
  for (b = 0; b <= buckets; b++) {
    c = counts[b];
    counts[b] = sum;
    sum += c;
  }
  for (i = 0; i < count; i++) {
    b = bf(CAT(base, i, es), ctx);
    memcpy(AT(scratch, counts[b + 1], es), CAT(base, i, es), es);
    counts[b + 1]++;
  }
  /* counts[b + 1] now holds the end of bucket b; shift to counts[b] */
  for (b = 0; b < buckets; b++) {
    counts[b] = counts[b + 1];
  }
  counts[buckets] = count;
  memcpy(base, scratch, count * es);
  return DS_OK;
}

static void sd_sort_buckets(void *base, size_t es, size_t buckets,
                            const size_t *counts, ds_compare_func_t compare,
                            void *user, ds_sort_func_t per_bucket) {
  size_t b;
  size_t s;
  size_t e;
  for (b = 0; b < buckets; b++) {
    s = b == 0 ? 0 : counts[b - 1];
    e = counts[b];
    if (e - s > 1) {
      per_bucket(AT(base, s, es), e - s, es, compare, user);
    }
  }
}

typedef struct sd_index_adapter {
  ds_index_key_func_t key;
  void *user;
} sd_index_adapter_t;

static size_t sd_index_bucket(const void *elem, void *ctx) {
  const sd_index_adapter_t *a = (const sd_index_adapter_t *)ctx;
  return a->key(elem, a->user);
}

ds_status_t ds_bucket_sort(void *base, size_t count, size_t elem_size,
                           ds_index_key_func_t bucket, size_t buckets,
                           ds_compare_func_t compare, void *user,
                           ds_sort_func_t per_bucket, void *scratch,
                           size_t *counts) {
  sd_index_adapter_t ad;
  ds_status_t st;
  if (base == NULL || bucket == NULL || compare == NULL) {
    return DS_ERR_NULL;
  }
  if (elem_size == 0 || buckets == 0) {
    return DS_ERR_INVALID;
  }
  if (count < 2) {
    return DS_OK;
  }
  if (scratch == NULL || counts == NULL) {
    return DS_ERR_NULL;
  }
  ad.key = bucket;
  ad.user = user;
  st = sd_distribute(base, count, elem_size, sd_index_bucket, &ad, buckets,
                     scratch, counts);
  if (st != DS_OK) {
    return st;
  }
  sd_sort_buckets(base, elem_size, buckets, counts, compare, user,
                  per_bucket != NULL ? per_bucket : ds_insertion_sort);
  return DS_OK;
}

typedef struct sd_range_ctx {
  ds_key_func_t key;
  void *user;
  unsigned long min;
  unsigned long width;
  size_t m;
} sd_range_ctx_t;

static size_t sd_range_bucket(const void *elem, void *ctx) {
  const sd_range_ctx_t *r = (const sd_range_ctx_t *)ctx;
  return (size_t)((r->key(elem, r->user) - r->min) / r->width);
}

static size_t sd_interp_bucket(const void *elem, void *ctx) {
  const sd_range_ctx_t *r = (const sd_range_ctx_t *)ctx;
  return sd_interp(r->key(elem, r->user), r->min, r->width, r->m);
}

ds_status_t ds_range_sort(void *base, size_t count, size_t elem_size,
                          ds_key_func_t key, ds_compare_func_t compare,
                          void *user, size_t buckets, void *scratch,
                          size_t *counts) {
  sd_range_ctx_t r;
  unsigned long mx;
  ds_status_t st;
  if (base == NULL || key == NULL || compare == NULL) {
    return DS_ERR_NULL;
  }
  if (elem_size == 0 || buckets == 0) {
    return DS_ERR_INVALID;
  }
  if (count < 2) {
    return DS_OK;
  }
  if (scratch == NULL || counts == NULL) {
    return DS_ERR_NULL;
  }
  r.key = key;
  r.user = user;
  sd_key_range(base, count, elem_size, key, user, &r.min, &mx);
  r.width = (mx - r.min) / (unsigned long)buckets + 1UL;
  r.m = buckets;
  st = sd_distribute(base, count, elem_size, sd_range_bucket, &r, buckets,
                     scratch, counts);
  if (st != DS_OK) {
    return st;
  }
  sd_sort_buckets(base, elem_size, buckets, counts, compare, user,
                  ds_insertion_sort);
  return DS_OK;
}

ds_status_t ds_pigeonhole_sort(void *base, size_t count, size_t elem_size,
                               ds_index_key_func_t key, void *user,
                               size_t range, size_t *counts, void *scratch) {
  sd_index_adapter_t ad;
  if (base == NULL || key == NULL) {
    return DS_ERR_NULL;
  }
  if (elem_size == 0 || range == 0) {
    return DS_ERR_INVALID;
  }
  if (count < 2) {
    return count == 1 && key(base, user) >= range ? DS_ERR_RANGE : DS_OK;
  }
  if (scratch == NULL || counts == NULL) {
    return DS_ERR_NULL;
  }
  ad.key = key;
  ad.user = user;
  return sd_distribute(base, count, elem_size, sd_index_bucket, &ad, range,
                       scratch, counts);
}

ds_status_t ds_rapid_sort_ulong(unsigned long *values, size_t count,
                                unsigned long min_value, size_t range,
                                size_t *counts) {
  size_t i;
  size_t v;
  size_t o = 0;
  size_t c;
  if (count > 0 && (values == NULL || counts == NULL)) {
    return DS_ERR_NULL;
  }
  if (range == 0) {
    return count == 0 ? DS_OK : DS_ERR_RANGE;
  }
  for (i = 0; i < count; i++) {
    if (values[i] < min_value || values[i] - min_value >= range) {
      return DS_ERR_RANGE;
    }
  }
  memset(counts, 0, range * sizeof(size_t));
  for (i = 0; i < count; i++) {
    counts[values[i] - min_value]++;
  }
  for (v = 0; v < range; v++) {
    for (c = counts[v]; c > 0; c--) {
      values[o++] = min_value + (unsigned long)v;
    }
  }
  return DS_OK;
}

static unsigned long sd_index_as_key(const void *elem, void *ctx) {
  const sd_index_adapter_t *a = (const sd_index_adapter_t *)ctx;
  return (unsigned long)a->key(elem, a->user);
}

ds_status_t ds_restricted_universe_sort(void *base, size_t count,
                                        size_t elem_size,
                                        ds_index_key_func_t key, void *user,
                                        size_t universe,
                                        ds_context_t *context) {
  sd_index_adapter_t ad;
  void *scratch;
  size_t *counts;
  size_t i;
  ds_status_t st;
  if (base == NULL || key == NULL) {
    return DS_ERR_NULL;
  }
  if (elem_size == 0 || universe == 0) {
    return DS_ERR_INVALID;
  }
  for (i = 0; i < count; i++) {
    if (key(CAT(base, i, elem_size), user) >= universe) {
      return DS_ERR_RANGE;
    }
  }
  if (count < 2) {
    return DS_OK;
  }
  scratch = sd_alloc(context, count, elem_size);
  if (scratch == NULL) {
    return DS_ERR_ALLOC;
  }
  ad.key = key;
  ad.user = user;
  if (universe <= 4 * count + 256) {
    counts = (size_t *)sd_alloc(context, universe + 1, sizeof(size_t));
    if (counts == NULL) {
      ds_context_free(context, scratch);
      return DS_ERR_ALLOC;
    }
    st = sd_distribute(base, count, elem_size, sd_index_bucket, &ad, universe,
                       scratch, counts);
    ds_context_free(context, counts);
  } else {
    ds_radix_sort_keyed(base, count, elem_size, sd_index_as_key, &ad, scratch);
    st = DS_OK;
  }
  ds_context_free(context, scratch);
  return st;
}

ds_status_t ds_histogram_sort(void *base, size_t count, size_t elem_size,
                              ds_key_func_t key, ds_compare_func_t compare,
                              void *user, void *scratch, size_t *counts) {
  sd_range_ctx_t r;
  unsigned long mx;
  ds_status_t st;
  if (base == NULL || key == NULL || compare == NULL) {
    return DS_ERR_NULL;
  }
  if (elem_size == 0) {
    return DS_ERR_INVALID;
  }
  if (count < 2) {
    return DS_OK;
  }
  if (scratch == NULL || counts == NULL) {
    return DS_ERR_NULL;
  }
  r.key = key;
  r.user = user;
  sd_key_range(base, count, elem_size, key, user, &r.min, &mx);
  r.width = mx; /* sd_interp_bucket reads the maximum from width */
  r.m = count;
  st = sd_distribute(base, count, elem_size, sd_interp_bucket, &r, count,
                     scratch, counts);
  if (st != DS_OK) {
    return st;
  }
  sd_sort_buckets(base, elem_size, count, counts, compare, user,
                  ds_insertion_sort);
  return DS_OK;
}

ds_status_t ds_interpolation_sort(void *base, size_t count, size_t elem_size,
                                  ds_key_func_t key, ds_compare_func_t compare,
                                  void *user, void *scratch, size_t *counts) {
  return ds_histogram_sort(base, count, elem_size, key, compare, user, scratch,
                           counts);
}

ds_status_t ds_address_calculation_sort(void *base, size_t count,
                                        size_t elem_size, ds_key_func_t key,
                                        ds_compare_func_t compare, void *user,
                                        size_t buckets, ds_context_t *context) {
  size_t *head;
  size_t *next;
  unsigned char *scratch;
  unsigned long mn;
  unsigned long mx;
  size_t i;
  size_t b;
  size_t prev;
  size_t cur;
  size_t o = 0;
  if (base == NULL || key == NULL || compare == NULL) {
    return DS_ERR_NULL;
  }
  if (elem_size == 0) {
    return DS_ERR_INVALID;
  }
  if (count < 2) {
    return DS_OK;
  }
  if (buckets == 0) {
    buckets = count;
  }
  head = (size_t *)sd_alloc(context, buckets, sizeof(size_t));
  next = (size_t *)sd_alloc(context, count, sizeof(size_t));
  scratch = (unsigned char *)sd_alloc(context, count, elem_size);
  if (head == NULL || next == NULL || scratch == NULL) {
    ds_context_free(context, head);
    ds_context_free(context, next);
    ds_context_free(context, scratch);
    return DS_ERR_ALLOC;
  }
  for (b = 0; b < buckets; b++) {
    head[b] = SD_NONE;
  }
  sd_key_range(base, count, elem_size, key, user, &mn, &mx);
  for (i = 0; i < count; i++) {
    b = sd_interp(key(CAT(base, i, elem_size), user), mn, mx, buckets);
    /* insert after the last element not greater than item i */
    prev = SD_NONE;
    cur = head[b];
    while (cur != SD_NONE && compare(CAT(base, cur, elem_size),
                                     CAT(base, i, elem_size), user) <= 0) {
      prev = cur;
      cur = next[cur];
    }
    next[i] = cur;
    if (prev == SD_NONE) {
      head[b] = i;
    } else {
      next[prev] = i;
    }
  }
  for (b = 0; b < buckets; b++) {
    for (cur = head[b]; cur != SD_NONE; cur = next[cur]) {
      memcpy(AT(scratch, o, elem_size), CAT(base, cur, elem_size), elem_size);
      o++;
    }
  }
  memcpy(base, scratch, count * elem_size);
  ds_context_free(context, head);
  ds_context_free(context, next);
  ds_context_free(context, scratch);
  return DS_OK;
}

ds_status_t ds_flash_sort(void *base, size_t count, size_t elem_size,
                          ds_key_func_t key, ds_compare_func_t compare,
                          void *user, size_t classes, ds_context_t *context) {
  size_t *l;
  unsigned char *flash;
  unsigned char *hold;
  unsigned char *a = (unsigned char *)base;
  unsigned long mn;
  unsigned long mx;
  size_t m;
  size_t i;
  size_t j;
  size_t k;
  size_t moves;
  if (base == NULL || key == NULL || compare == NULL) {
    return DS_ERR_NULL;
  }
  if (elem_size == 0) {
    return DS_ERR_INVALID;
  }
  if (count < 2) {
    return DS_OK;
  }
  sd_key_range(base, count, elem_size, key, user, &mn, &mx);
  if (mn != mx) {
    m = classes != 0 ? classes : count / 100 * 43 + (count % 100) * 43 / 100;
    if (m < 2) {
      m = 2;
    }
    l = (size_t *)sd_alloc(context, m, sizeof(size_t));
    flash = (unsigned char *)sd_alloc(context, 2, elem_size);
    if (l == NULL || flash == NULL) {
      ds_context_free(context, l);
      ds_context_free(context, flash);
      return DS_ERR_ALLOC;
    }
    hold = flash + elem_size;
    memset(l, 0, m * sizeof(size_t));
    for (i = 0; i < count; i++) {
      l[sd_interp(key(CAT(a, i, elem_size), user), mn, mx, m)]++;
    }
    for (k = 1; k < m; k++) {
      l[k] += l[k - 1]; /* l[k]: end of class k */
    }
    /* cycle leader permutation: every element goes to the top free slot
     * of its class */
    moves = 0;
    j = 0;
    k = m - 1;
    while (moves < count - 1) {
      while (j + 1 > l[k]) {
        j++;
        if (j >= count) {
          break;
        }
        k = sd_interp(key(CAT(a, j, elem_size), user), mn, mx, m);
      }
      if (j >= count) {
        break;
      }
      memcpy(flash, CAT(a, j, elem_size), elem_size);
      while (j != l[k]) {
        k = sd_interp(key(flash, user), mn, mx, m);
        memcpy(hold, CAT(a, l[k] - 1, elem_size), elem_size);
        memcpy(AT(a, l[k] - 1, elem_size), flash, elem_size);
        memcpy(flash, hold, elem_size);
        l[k]--;
        moves++;
      }
    }
    ds_context_free(context, l);
    ds_context_free(context, flash);
  }
  ds_insertion_sort(base, count, elem_size, compare, user);
  return DS_OK;
}

ds_status_t ds_linear_probing_sort(void *base, size_t count, size_t elem_size,
                                   ds_key_func_t key, ds_compare_func_t compare,
                                   void *user, ds_context_t *context) {
  unsigned char *table;
  unsigned char *used;
  unsigned long mn;
  unsigned long mx;
  size_t slots;
  size_t homes;
  size_t i;
  size_t p;
  size_t e;
  size_t o;
  if (base == NULL || key == NULL || compare == NULL) {
    return DS_ERR_NULL;
  }
  if (elem_size == 0) {
    return DS_ERR_INVALID;
  }
  if (count < 2) {
    return DS_OK;
  }
  if (count > (((size_t)-1) - 16) / 2) {
    return DS_ERR_OVERFLOW;
  }
  homes = count + count / 4 + 1;
  slots = 2 * count + 16; /* the slots past `homes` are the overflow area */
  table = (unsigned char *)sd_alloc(context, slots, elem_size);
  used = (unsigned char *)ds_context_calloc(context, slots, 1);
  if (table == NULL || used == NULL) {
    ds_context_free(context, table);
    ds_context_free(context, used);
    return DS_ERR_ALLOC;
  }
  sd_key_range(base, count, elem_size, key, user, &mn, &mx);
  for (i = 0; i < count; i++) {
    p = sd_interp(key(CAT(base, i, elem_size), user), mn, mx, homes);
    /* skip the run's elements not greater than this one (stability) */
    while (p < slots && used[p] &&
           compare(CAT(table, p, elem_size), CAT(base, i, elem_size), user) <=
               0) {
      p++;
    }
    e = p;
    while (e < slots && used[e]) {
      e++;
    }
    if (e >= slots) {
      ds_context_free(context, table);
      ds_context_free(context, used);
      return DS_ERR_OVERFLOW;
    }
    if (e > p) {
      memmove(AT(table, p + 1, elem_size), CAT(table, p, elem_size),
              (e - p) * elem_size);
    }
    memcpy(AT(table, p, elem_size), CAT(base, i, elem_size), elem_size);
    used[e] = 1;
    used[p] = 1;
  }
  o = 0;
  for (p = 0; p < slots; p++) {
    if (used[p]) {
      memcpy(AT(base, o, elem_size), CAT(table, p, elem_size), elem_size);
      o++;
    }
  }
  ds_context_free(context, table);
  ds_context_free(context, used);
  return DS_OK;
}

typedef struct sd_dps_ctx {
  ds_key_func_t key;
  void *user;
  unsigned long mn;
  unsigned long med;
  unsigned long mx;
  size_t half;
  size_t m;
} sd_dps_ctx_t;

static size_t sd_dps_bucket(const void *elem, void *ctx) {
  const sd_dps_ctx_t *d = (const sd_dps_ctx_t *)ctx;
  unsigned long k = d->key(elem, d->user);
  if (k <= d->med) {
    return sd_interp(k, d->mn, d->med, d->half);
  }
  return d->half + sd_interp(k, d->med + 1, d->mx, d->m - d->half);
}

ds_status_t
ds_distributive_partitioning_sort(void *base, size_t count, size_t elem_size,
                                  ds_key_func_t key, ds_compare_func_t compare,
                                  void *user, ds_context_t *context) {
  sd_dps_ctx_t d;
  unsigned char *a = (unsigned char *)base;
  unsigned char *scratch;
  size_t *counts;
  size_t *stack; /* pairs (lo, len) */
  size_t top = 0;
  size_t lo;
  size_t len;
  size_t b;
  size_t s;
  if (base == NULL || key == NULL || compare == NULL) {
    return DS_ERR_NULL;
  }
  if (elem_size == 0) {
    return DS_ERR_INVALID;
  }
  if (count < 2) {
    return DS_OK;
  }
  scratch = (unsigned char *)sd_alloc(context, count, elem_size);
  counts = (size_t *)sd_alloc(context, count + 1, sizeof(size_t));
  stack = (size_t *)sd_alloc(context, count + 2, 2 * sizeof(size_t));
  if (scratch == NULL || counts == NULL || stack == NULL) {
    ds_context_free(context, scratch);
    ds_context_free(context, counts);
    ds_context_free(context, stack);
    return DS_ERR_ALLOC;
  }
  d.key = key;
  d.user = user;
  stack[0] = 0;
  stack[1] = count;
  top = 1;
  while (top > 0) {
    top--;
    lo = stack[2 * top];
    len = stack[2 * top + 1];
    if (len <= SD_SMALL) {
      ds_insertion_sort(AT(a, lo, elem_size), len, elem_size, compare, user);
      continue;
    }
    sd_key_range(AT(a, lo, elem_size), len, elem_size, key, user, &d.mn, &d.mx);
    if (d.mn == d.mx) {
      /* equal keys: only the comparator can still order them */
      ds_intro_sort(AT(a, lo, elem_size), len, elem_size, compare, user);
      continue;
    }
    memcpy(scratch, CAT(a, lo, elem_size), len * elem_size);
    ds_select_floyd_rivest(scratch, len, elem_size, (len - 1) / 2, compare,
                           user);
    d.med = key(CAT(scratch, (len - 1) / 2, elem_size), user);
    d.m = len;
    d.half = len / 2;
    sd_distribute(AT(a, lo, elem_size), len, elem_size, sd_dps_bucket, &d, len,
                  scratch, counts);
    for (b = 0; b < len; b++) {
      s = b == 0 ? 0 : counts[b - 1];
      if (counts[b] - s > 1) {
        stack[2 * top] = lo + s;
        stack[2 * top + 1] = counts[b] - s;
        top++;
      }
    }
  }
  ds_context_free(context, scratch);
  ds_context_free(context, counts);
  ds_context_free(context, stack);
  return DS_OK;
}

/* ---- byte-string sorts ---------------------------------------------- */

typedef struct sd_stack {
  size_t inline_data[SD_STACK_INLINE * 3];
  size_t *data;
  size_t len; /* entries (triples) */
  size_t cap;
  ds_context_t *ctx;
} sd_stack_t;

static void sd_stack_init(sd_stack_t *s, ds_context_t *ctx) {
  s->data = s->inline_data;
  s->len = 0;
  s->cap = SD_STACK_INLINE;
  s->ctx = ctx;
}

static void sd_stack_free(sd_stack_t *s) {
  if (s->data != s->inline_data) {
    ds_context_free(s->ctx, s->data);
  }
}

static ds_status_t sd_stack_push(sd_stack_t *s, size_t a, size_t b, size_t c) {
  size_t *grown;
  size_t ncap;
  if (s->len == s->cap) {
    if (s->cap > ((size_t)-1) / 6 / sizeof(size_t)) {
      return DS_ERR_OVERFLOW;
    }
    ncap = s->cap * 2;
    grown = (size_t *)ds_context_alloc(s->ctx, ncap * 3 * sizeof(size_t));
    if (grown == NULL) {
      return DS_ERR_ALLOC;
    }
    memcpy(grown, s->data, s->len * 3 * sizeof(size_t));
    if (s->data != s->inline_data) {
      ds_context_free(s->ctx, s->data);
    }
    s->data = grown;
    s->cap = ncap;
  }
  s->data[3 * s->len] = a;
  s->data[3 * s->len + 1] = b;
  s->data[3 * s->len + 2] = c;
  s->len++;
  return DS_OK;
}

static void sd_stack_pop(sd_stack_t *s, size_t *a, size_t *b, size_t *c) {
  s->len--;
  *a = s->data[3 * s->len];
  *b = s->data[3 * s->len + 1];
  *c = s->data[3 * s->len + 2];
}

static size_t sd_bucket_of(ds_byte_at_func_t byte_at, const void *elem,
                           size_t depth, void *user) {
  int v = byte_at(elem, depth, user);
  return v < 0 ? 0 : (size_t)v + 1;
}

/* Compare two keys from `depth` on. */
static int sd_bytes_cmp(ds_byte_at_func_t byte_at, const void *x, const void *y,
                        size_t depth, void *user) {
  int bx;
  int by;
  for (;; depth++) {
    bx = byte_at(x, depth, user);
    by = byte_at(y, depth, user);
    if (bx < 0) {
      bx = -1;
    }
    if (by < 0) {
      by = -1;
    }
    if (bx != by) {
      return bx < by ? -1 : 1;
    }
    if (bx < 0) {
      return 0;
    }
  }
}

/* Stable insertion sort by bytes from depth. */
static void sd_bytes_insertion(unsigned char *a, size_t lo, size_t len,
                               size_t es, ds_byte_at_func_t byte_at,
                               size_t depth, void *user) {
  size_t i;
  size_t j;
  for (i = lo + 1; i < lo + len; i++) {
    for (j = i; j > lo && sd_bytes_cmp(byte_at, CAT(a, j - 1, es),
                                       CAT(a, j, es), depth, user) > 0;
         j--) {
      ds_elem_swap(AT(a, j - 1, es), AT(a, j, es), es);
    }
  }
}

ds_status_t ds_msd_radix_sort(void *base, size_t count, size_t elem_size,
                              ds_byte_at_func_t byte_at, void *user,
                              void *scratch, ds_context_t *context) {
  sd_stack_t stk;
  size_t counts[SD_RADIX + 1];
  unsigned char *a = (unsigned char *)base;
  size_t lo;
  size_t len;
  size_t depth;
  size_t i;
  size_t b;
  size_t sum;
  size_t c;
  ds_status_t st = DS_OK;
  if (base == NULL || byte_at == NULL) {
    return DS_ERR_NULL;
  }
  if (elem_size == 0) {
    return DS_ERR_INVALID;
  }
  if (count < 2) {
    return DS_OK;
  }
  if (scratch == NULL) {
    return DS_ERR_NULL;
  }
  sd_stack_init(&stk, context);
  sd_stack_push(&stk, 0, count, 0);
  while (stk.len > 0) {
    sd_stack_pop(&stk, &lo, &len, &depth);
    if (len <= SD_SMALL) {
      sd_bytes_insertion(a, lo, len, elem_size, byte_at, depth, user);
      continue;
    }
    memset(counts, 0, sizeof(counts));
    for (i = lo; i < lo + len; i++) {
      counts[sd_bucket_of(byte_at, CAT(a, i, elem_size), depth, user)]++;
    }
    sum = 0;
    for (b = 0; b <= SD_RADIX; b++) {
      c = counts[b];
      counts[b] = sum;
      sum += c;
    }
    for (i = lo; i < lo + len; i++) {
      b = sd_bucket_of(byte_at, CAT(a, i, elem_size), depth, user);
      memcpy(AT(scratch, counts[b], elem_size), CAT(a, i, elem_size),
             elem_size);
      counts[b]++;
    }
    memcpy(AT(a, lo, elem_size), scratch, len * elem_size);
    /* counts[b] is now the end of bucket b; bucket 0 (ended keys) is
     * done */
    for (b = SD_RADIX; b-- > 1;) {
      size_t s = counts[b - 1];
      if (counts[b] - s > 1) {
        st = sd_stack_push(&stk, lo + s, counts[b] - s, depth + 1);
        if (st != DS_OK) {
          sd_stack_free(&stk);
          return st;
        }
      }
    }
  }
  sd_stack_free(&stk);
  return st;
}

ds_status_t ds_american_flag_sort(void *base, size_t count, size_t elem_size,
                                  ds_byte_at_func_t byte_at, void *user,
                                  ds_context_t *context) {
  sd_stack_t stk;
  size_t start[SD_RADIX + 1];
  size_t next[SD_RADIX];
  unsigned char *a = (unsigned char *)base;
  size_t lo;
  size_t len;
  size_t depth;
  size_t i;
  size_t b;
  size_t v;
  ds_status_t st;
  if (base == NULL || byte_at == NULL) {
    return DS_ERR_NULL;
  }
  if (elem_size == 0) {
    return DS_ERR_INVALID;
  }
  if (count < 2) {
    return DS_OK;
  }
  sd_stack_init(&stk, context);
  sd_stack_push(&stk, 0, count, 0);
  while (stk.len > 0) {
    sd_stack_pop(&stk, &lo, &len, &depth);
    if (len <= SD_SMALL) {
      sd_bytes_insertion(a, lo, len, elem_size, byte_at, depth, user);
      continue;
    }
    memset(start, 0, sizeof(start));
    for (i = lo; i < lo + len; i++) {
      start[sd_bucket_of(byte_at, CAT(a, i, elem_size), depth, user) + 1]++;
    }
    start[0] = lo;
    for (b = 1; b <= SD_RADIX; b++) {
      start[b] += start[b - 1];
    }
    for (b = 0; b < SD_RADIX; b++) {
      next[b] = start[b];
    }
    /* permute in place: each element is swapped into its bucket */
    for (b = 0; b < SD_RADIX; b++) {
      while (next[b] < start[b + 1]) {
        v = sd_bucket_of(byte_at, CAT(a, next[b], elem_size), depth, user);
        if (v == b) {
          next[b]++;
        } else {
          ds_elem_swap(AT(a, next[b], elem_size), AT(a, next[v], elem_size),
                       elem_size);
          next[v]++;
        }
      }
    }
    for (b = SD_RADIX; b-- > 1;) {
      if (start[b + 1] - start[b] > 1) {
        st = sd_stack_push(&stk, start[b], start[b + 1] - start[b], depth + 1);
        if (st != DS_OK) {
          sd_stack_free(&stk);
          return st;
        }
      }
    }
  }
  sd_stack_free(&stk);
  return DS_OK;
}

ds_status_t ds_multikey_quicksort(void *base, size_t count, size_t elem_size,
                                  ds_byte_at_func_t byte_at, void *user,
                                  ds_context_t *context) {
  sd_stack_t stk;
  unsigned char *a = (unsigned char *)base;
  size_t lo;
  size_t len;
  size_t depth;
  size_t lt;
  size_t gt;
  size_t i;
  size_t pv;
  size_t v;
  size_t b0;
  size_t b1;
  size_t b2;
  ds_status_t st = DS_OK;
  if (base == NULL || byte_at == NULL) {
    return DS_ERR_NULL;
  }
  if (elem_size == 0) {
    return DS_ERR_INVALID;
  }
  if (count < 2) {
    return DS_OK;
  }
  sd_stack_init(&stk, context);
  sd_stack_push(&stk, 0, count, 0);
  while (stk.len > 0 && st == DS_OK) {
    sd_stack_pop(&stk, &lo, &len, &depth);
    if (len <= SD_SMALL / 2) {
      sd_bytes_insertion(a, lo, len, elem_size, byte_at, depth, user);
      continue;
    }
    /* pivot byte: median of first, middle and last */
    b0 = sd_bucket_of(byte_at, CAT(a, lo, elem_size), depth, user);
    b1 = sd_bucket_of(byte_at, CAT(a, lo + len / 2, elem_size), depth, user);
    b2 = sd_bucket_of(byte_at, CAT(a, lo + len - 1, elem_size), depth, user);
    if ((b0 <= b1 && b1 <= b2) || (b2 <= b1 && b1 <= b0)) {
      pv = b1;
    } else if ((b1 <= b0 && b0 <= b2) || (b2 <= b0 && b0 <= b1)) {
      pv = b0;
    } else {
      pv = b2;
    }
    lt = lo;
    gt = lo + len; /* [gt, lo + len) holds bytes > pivot */
    i = lo;
    while (i < gt) {
      v = sd_bucket_of(byte_at, CAT(a, i, elem_size), depth, user);
      if (v < pv) {
        ds_elem_swap(AT(a, lt, elem_size), AT(a, i, elem_size), elem_size);
        lt++;
        i++;
      } else if (v > pv) {
        gt--;
        ds_elem_swap(AT(a, i, elem_size), AT(a, gt, elem_size), elem_size);
      } else {
        i++;
      }
    }
    if (lo + len - gt > 1) {
      st = sd_stack_push(&stk, gt, lo + len - gt, depth);
    }
    if (st == DS_OK && pv != 0 && gt - lt > 1) {
      st = sd_stack_push(&stk, lt, gt - lt, depth + 1);
    }
    if (st == DS_OK && lt - lo > 1) {
      st = sd_stack_push(&stk, lo, lt - lo, depth);
    }
  }
  sd_stack_free(&stk);
  return st;
}

/* Blind trie: internal nodes keep only their branching depth; children
 * hang off a sorted first-child/next-sibling list labelled by byte + 1
 * (0 for an ended key). Leaves hold a chain of identical keys. */
typedef struct sd_bnode {
  size_t depth; /* internal: branching depth; leaf: SD_NONE */
  size_t child; /* first child (internal) or first element (leaf) */
  size_t sibling;
  size_t label;
  size_t tail; /* leaf: last element of the chain */
} sd_bnode_t;

static size_t sd_blind_find_child(const sd_bnode_t *nodes, size_t node,
                                  size_t label) {
  size_t c = nodes[node].child;
  while (c != SD_NONE && nodes[c].label < label) {
    c = nodes[c].sibling;
  }
  return c != SD_NONE && nodes[c].label == label ? c : SD_NONE;
}

/* Link `node` into parent's sorted child list. */
static void sd_blind_link(sd_bnode_t *nodes, size_t parent, size_t node) {
  size_t *link = &nodes[parent].child;
  while (*link != SD_NONE && nodes[*link].label < nodes[node].label) {
    link = &nodes[*link].sibling;
  }
  nodes[node].sibling = *link;
  *link = node;
}

/* Replace child `old` of parent (or the root) by `repl` in place. */
static void sd_blind_replace(sd_bnode_t *nodes, size_t parent, size_t *root,
                             size_t old, size_t repl) {
  size_t *link;
  if (parent == SD_NONE) {
    *root = repl;
    nodes[repl].sibling = SD_NONE;
    return;
  }
  link = &nodes[parent].child;
  while (*link != old) {
    link = &nodes[*link].sibling;
  }
  nodes[repl].sibling = nodes[old].sibling;
  *link = repl;
}

ds_status_t ds_blind_sort(void *base, size_t count, size_t elem_size,
                          ds_byte_at_func_t byte_at, void *user,
                          ds_context_t *context) {
  sd_bnode_t *nodes;
  size_t *chain; /* next element with an identical key */
  size_t *stack;
  unsigned char *scratch;
  unsigned char *a = (unsigned char *)base;
  size_t used = 0;
  size_t root = SD_NONE;
  size_t i;
  size_t node;
  size_t parent;
  size_t c;
  size_t d;
  size_t leaf;
  size_t inner;
  size_t top;
  size_t o;
  size_t e;
  int bx;
  int by;
  if (base == NULL || byte_at == NULL) {
    return DS_ERR_NULL;
  }
  if (elem_size == 0) {
    return DS_ERR_INVALID;
  }
  if (count < 2) {
    return DS_OK;
  }
  nodes = (sd_bnode_t *)sd_alloc(context, 2 * count, sizeof(sd_bnode_t));
  chain = (size_t *)sd_alloc(context, count, sizeof(size_t));
  stack = (size_t *)sd_alloc(context, 2 * count, sizeof(size_t));
  scratch = (unsigned char *)sd_alloc(context, count, elem_size);
  if (nodes == NULL || chain == NULL || stack == NULL || scratch == NULL) {
    ds_context_free(context, nodes);
    ds_context_free(context, chain);
    ds_context_free(context, stack);
    ds_context_free(context, scratch);
    return DS_ERR_ALLOC;
  }
  for (i = 0; i < count; i++) {
    chain[i] = SD_NONE;
    leaf = used++;
    nodes[leaf].depth = SD_NONE;
    nodes[leaf].child = i;
    nodes[leaf].tail = i;
    nodes[leaf].sibling = SD_NONE;
    nodes[leaf].label = 0;
    if (root == SD_NONE) {
      root = leaf;
      continue;
    }
    /* blind descent to some leaf */
    node = root;
    while (nodes[node].depth != SD_NONE) {
      c = sd_blind_find_child(
          nodes, node,
          sd_bucket_of(byte_at, CAT(a, i, elem_size), nodes[node].depth, user));
      node = c != SD_NONE ? c : nodes[node].child;
    }
    /* first differing depth against that leaf's key */
    e = nodes[node].child;
    for (d = 0;; d++) {
      bx = byte_at(CAT(a, i, elem_size), d, user);
      by = byte_at(CAT(a, e, elem_size), d, user);
      if (bx < 0) {
        bx = -1;
      }
      if (by < 0) {
        by = -1;
      }
      if (bx != by || bx < 0) {
        break;
      }
    }
    if (bx == by) {
      /* identical key: append to the chain (stable) */
      chain[nodes[node].tail] = i;
      nodes[node].tail = i;
      used--;
      continue;
    }
    /* walk down again to where depth d belongs */
    parent = SD_NONE;
    node = root;
    while (nodes[node].depth != SD_NONE && nodes[node].depth < d) {
      parent = node;
      node = sd_blind_find_child(
          nodes, node,
          sd_bucket_of(byte_at, CAT(a, i, elem_size), nodes[node].depth, user));
    }
    nodes[leaf].label = (size_t)(bx + 1);
    if (nodes[node].depth == d) {
      sd_blind_link(nodes, node, leaf);
      continue;
    }
    inner = used++;
    nodes[inner].depth = d;
    nodes[inner].child = SD_NONE;
    nodes[inner].label = nodes[node].label;
    nodes[inner].tail = SD_NONE;
    sd_blind_replace(nodes, parent, &root, node, inner);
    nodes[node].label = (size_t)(by + 1);
    nodes[node].sibling = SD_NONE;
    sd_blind_link(nodes, inner, node);
    sd_blind_link(nodes, inner, leaf);
  }
  /* left-to-right walk */
  o = 0;
  top = 0;
  stack[top++] = root;
  while (top > 0) {
    node = stack[--top];
    if (nodes[node].depth == SD_NONE) {
      for (e = nodes[node].child; e != SD_NONE; e = chain[e]) {
        memcpy(AT(scratch, o, elem_size), CAT(a, e, elem_size), elem_size);
        o++;
      }
      continue;
    }
    /* push children in reverse order */
    d = top;
    for (c = nodes[node].child; c != SD_NONE; c = nodes[c].sibling) {
      stack[top++] = c;
    }
    for (i = 0; i < (top - d) / 2; i++) {
      c = stack[d + i];
      stack[d + i] = stack[top - 1 - i];
      stack[top - 1 - i] = c;
    }
  }
  memcpy(base, scratch, count * elem_size);
  ds_context_free(context, nodes);
  ds_context_free(context, chain);
  ds_context_free(context, stack);
  ds_context_free(context, scratch);
  return DS_OK;
}

/* ---- postman's sort ------------------------------------------------- */

typedef struct sd_field_ctx {
  ds_index_key_func_t key;
  void *user;
} sd_field_ctx_t;

static size_t sd_field_bucket(const void *elem, void *ctx) {
  const sd_field_ctx_t *f = (const sd_field_ctx_t *)ctx;
  return f->key(elem, f->user);
}

ds_status_t ds_postman_sort(void *base, size_t count, size_t elem_size,
                            const ds_postman_field_t *fields,
                            size_t field_count, void *user,
                            ds_context_t *context) {
  sd_stack_t stk;
  sd_field_ctx_t fc;
  unsigned char *a = (unsigned char *)base;
  unsigned char *scratch;
  size_t *counts;
  size_t max_range = 0;
  size_t lo;
  size_t len;
  size_t level;
  size_t f;
  size_t i;
  size_t b;
  size_t s;
  ds_status_t st = DS_OK;
  if (base == NULL || (field_count > 0 && fields == NULL)) {
    return DS_ERR_NULL;
  }
  if (elem_size == 0) {
    return DS_ERR_INVALID;
  }
  for (f = 0; f < field_count; f++) {
    if (fields[f].key == NULL) {
      return DS_ERR_NULL;
    }
    if (fields[f].range == 0) {
      return DS_ERR_INVALID;
    }
    for (i = 0; i < count; i++) {
      if (fields[f].key(CAT(a, i, elem_size), user) >= fields[f].range) {
        return DS_ERR_RANGE;
      }
    }
    if (fields[f].range > max_range) {
      max_range = fields[f].range;
    }
  }
  if (count < 2 || field_count == 0) {
    return DS_OK;
  }
  scratch = (unsigned char *)sd_alloc(context, count, elem_size);
  counts = (size_t *)sd_alloc(context, max_range + 1, sizeof(size_t));
  if (scratch == NULL || counts == NULL) {
    ds_context_free(context, scratch);
    ds_context_free(context, counts);
    return DS_ERR_ALLOC;
  }
  sd_stack_init(&stk, context);
  sd_stack_push(&stk, 0, count, 0);
  while (stk.len > 0 && st == DS_OK) {
    sd_stack_pop(&stk, &lo, &len, &level);
    fc.key = fields[level].key;
    fc.user = user;
    sd_distribute(AT(a, lo, elem_size), len, elem_size, sd_field_bucket, &fc,
                  fields[level].range, scratch, counts);
    if (level + 1 == field_count) {
      continue;
    }
    for (b = fields[level].range; b-- > 0 && st == DS_OK;) {
      s = b == 0 ? 0 : counts[b - 1];
      if (counts[b] - s > 1) {
        st = sd_stack_push(&stk, lo + s, counts[b] - s, level + 1);
      }
    }
  }
  sd_stack_free(&stk);
  ds_context_free(context, scratch);
  ds_context_free(context, counts);
  return st;
}

/* ---- shuffle sort and J sort ---------------------------------------- */

static size_t sd_upper_bound(const unsigned char *a, size_t n, size_t es,
                             const void *key, ds_compare_func_t compare,
                             void *user) {
  size_t lo = 0;
  size_t hi = n;
  size_t mid;
  while (lo < hi) {
    mid = lo + (hi - lo) / 2;
    if (compare(CAT(a, mid, es), key, user) <= 0) {
      lo = mid + 1;
    } else {
      hi = mid;
    }
  }
  return lo;
}

static ds_status_t sd_shuffle(unsigned char *a, size_t n, size_t es,
                              ds_compare_func_t compare, void *user,
                              unsigned char *scratch, ds_context_t *context) {
  size_t s;
  size_t *counts;
  size_t i;
  size_t b;
  size_t sum;
  size_t c;
  size_t start;
  ds_status_t st;
  if (n < SD_SHUFFLE_SMALL) {
    ds_insertion_sort(a, n, es, compare, user);
    return DS_OK;
  }
  s = n / 8;
  /* the first n/8 items, sorted, become the bucket splitters */
  st = sd_shuffle(a, s, es, compare, user, scratch, context);
  if (st != DS_OK) {
    return st;
  }
  counts = (size_t *)sd_alloc(context, s + 2, sizeof(size_t));
  if (counts == NULL) {
    return DS_ERR_ALLOC;
  }
  memset(counts, 0, (s + 2) * sizeof(size_t));
  for (i = s; i < n; i++) {
    counts[sd_upper_bound(a, s, es, CAT(a, i, es), compare, user)]++;
  }
  /* layout: bucket 0, splitter 0, bucket 1, splitter 1, ..., bucket s */
  sum = 0;
  for (b = 0; b <= s; b++) {
    c = counts[b];
    counts[b] = sum;
    sum += c + 1;
    if (b < s) {
      memcpy(AT(scratch, sum - 1, es), CAT(a, b, es), es);
    }
  }
  for (i = s; i < n; i++) {
    b = sd_upper_bound(a, s, es, CAT(a, i, es), compare, user);
    memcpy(AT(scratch, counts[b], es), CAT(a, i, es), es);
    counts[b]++;
  }
  memcpy(a, scratch, n * es);
  /* counts[b] is the end of bucket b; bucket b starts after splitter
   * b - 1 */
  start = 0;
  st = DS_OK;
  for (b = 0; b <= s && st == DS_OK; b++) {
    if (counts[b] - start > 1) {
      st = sd_shuffle(AT(a, start, es), counts[b] - start, es, compare, user,
                      scratch, context);
    }
    start = counts[b] + 1;
  }
  ds_context_free(context, counts);
  return st;
}

ds_status_t ds_shuffle_sort(void *base, size_t count, size_t elem_size,
                            ds_compare_func_t compare, void *user,
                            ds_context_t *context) {
  unsigned char *scratch;
  ds_status_t st;
  if (base == NULL || compare == NULL) {
    return DS_ERR_NULL;
  }
  if (elem_size == 0) {
    return DS_ERR_INVALID;
  }
  if (count < 2) {
    return DS_OK;
  }
  if (count < SD_SHUFFLE_SMALL) {
    ds_insertion_sort(base, count, elem_size, compare, user);
    return DS_OK;
  }
  scratch = (unsigned char *)sd_alloc(context, count, elem_size);
  if (scratch == NULL) {
    return DS_ERR_ALLOC;
  }
  st = sd_shuffle((unsigned char *)base, count, elem_size, compare, user,
                  scratch, context);
  ds_context_free(context, scratch);
  return st;
}

ds_status_t ds_j_sort(void *base, size_t count, size_t elem_size,
                      ds_compare_func_t compare, void *user,
                      ds_context_t *context) {
  union {
    long l;
    double d;
    void *p;
    unsigned char bytes[SD_J_STACK_BYTES];
  } local;
  void *scratch;
  ds_status_t st;
  if (base == NULL || compare == NULL) {
    return DS_ERR_NULL;
  }
  if (elem_size == 0) {
    return DS_ERR_INVALID;
  }
  if (count >= SD_J_SMALL) {
    return ds_shuffle_sort(base, count, elem_size, compare, user, context);
  }
  if (count < 2) {
    return DS_OK;
  }
  if (count * elem_size <= sizeof(local)) {
    return ds_strand_sort(base, count, elem_size, compare, user, local.bytes);
  }
  scratch = sd_alloc(context, count, elem_size);
  if (scratch == NULL) {
    return DS_ERR_ALLOC;
  }
  st = ds_strand_sort(base, count, elem_size, compare, user, scratch);
  ds_context_free(context, scratch);
  return st;
}

/* ---- UnShuffle sort
 * ---------------------------------------------------------- */

/* A pile is a deque of element indices: the front part is kept reversed in
 * its own array, so the pile reads front[nf-1..0] then back[0..nb-1]. */
typedef struct sd_pile {
  size_t *front;
  size_t nf;
  size_t cf;
  size_t *back;
  size_t nb;
  size_t cb;
  size_t pos; /* merge cursor */
} sd_pile_t;

static size_t sd_pile_at(const sd_pile_t *p, size_t i) {
  return i < p->nf ? p->front[p->nf - 1 - i] : p->back[i - p->nf];
}

static size_t sd_pile_len(const sd_pile_t *p) { return p->nf + p->nb; }

static ds_status_t sd_push(ds_context_t *ctx, size_t **a, size_t *n,
                           size_t *cap, size_t v) {
  if (*n == *cap) {
    size_t nc = *cap < 4 ? 8 : *cap * 2;
    void *q = ds_context_realloc(ctx, *a, nc * sizeof(size_t));
    if (q == NULL) {
      return DS_ERR_ALLOC;
    }
    *a = (size_t *)q;
    *cap = nc;
  }
  (*a)[(*n)++] = v;
  return DS_OK;
}

ds_status_t ds_unshuffle_sort(void *base, size_t count, size_t elem_size,
                              ds_compare_func_t compare, void *user,
                              size_t *out_piles, ds_context_t *context) {
  unsigned char *copy = NULL;
  sd_pile_t *piles = NULL;
  size_t np = 0;
  size_t pcap = 0;
  size_t *heap = NULL;
  size_t hn = 0;
  size_t i;
  size_t k;
  ds_status_t st = DS_OK;
  if (out_piles != NULL) {
    *out_piles = 0;
  }
  if (base == NULL || compare == NULL) {
    return count < 2 ? DS_OK : DS_ERR_NULL;
  }
  if (count < 2 || elem_size == 0) {
    if (out_piles != NULL) {
      *out_piles = count;
    }
    return DS_OK;
  }
  copy = (unsigned char *)ds_context_alloc(context, count * elem_size);
  if (copy == NULL) {
    return DS_ERR_ALLOC;
  }
  memcpy(copy, base, count * elem_size);
  /* deal: the most recently used pile is tried first */
  for (i = 0; i < count && st == DS_OK; i++) {
    const void *x = CAT(copy, i, elem_size);
    size_t p;
    bool placed = false;
    for (k = 0; k < np && !placed; k++) {
      sd_pile_t *q;
      size_t last;
      size_t first;
      p = np - 1 - k;
      q = &piles[p];
      last = sd_pile_at(q, sd_pile_len(q) - 1);
      first = sd_pile_at(q, 0);
      if (compare(x, CAT(copy, last, elem_size), user) >= 0) {
        st = sd_push(context, &q->back, &q->nb, &q->cb, i);
        placed = true;
      } else if (compare(x, CAT(copy, first, elem_size), user) < 0) {
        st = sd_push(context, &q->front, &q->nf, &q->cf, i);
        placed = true;
      }
    }
    if (!placed && st == DS_OK) {
      if (np == pcap) {
        size_t nc = pcap < 4 ? 8 : pcap * 2;
        void *q = ds_context_realloc(context, piles, nc * sizeof(sd_pile_t));
        if (q == NULL) {
          st = DS_ERR_ALLOC;
          break;
        }
        piles = (sd_pile_t *)q;
        pcap = nc;
      }
      memset(&piles[np], 0, sizeof(sd_pile_t));
      st = sd_push(context, &piles[np].back, &piles[np].nb, &piles[np].cb, i);
      np++;
    }
  }
  /* merge: a binary heap of piles keyed by their next element (ties by
   * pile number) */
  if (st == DS_OK) {
    heap = (size_t *)ds_context_alloc(context, np * sizeof(size_t));
    if (heap == NULL) {
      st = DS_ERR_ALLOC;
    }
  }
  if (st == DS_OK) {
    size_t out = 0;
#define SD_KEY(p) CAT(copy, sd_pile_at(&piles[p], piles[p].pos), elem_size)
#define SD_LESS(a, b)                                                          \
  (compare(SD_KEY(a), SD_KEY(b), user) < 0 ||                                  \
   (compare(SD_KEY(a), SD_KEY(b), user) == 0 && (a) < (b)))
    for (k = 0; k < np; k++) {
      size_t j = hn++;
      heap[j] = k;
      while (j > 0 && SD_LESS(heap[j], heap[(j - 1) / 2])) {
        size_t t = heap[j];
        heap[j] = heap[(j - 1) / 2];
        heap[(j - 1) / 2] = t;
        j = (j - 1) / 2;
      }
    }
    while (hn > 0) {
      size_t p = heap[0];
      size_t j = 0;
      memcpy(AT(base, out++, elem_size), SD_KEY(p), elem_size);
      piles[p].pos++;
      if (piles[p].pos == sd_pile_len(&piles[p])) {
        heap[0] = heap[--hn];
      }
      for (;;) {
        size_t l = 2 * j + 1;
        size_t r = l + 1;
        size_t m = j;
        if (l < hn && SD_LESS(heap[l], heap[m])) {
          m = l;
        }
        if (r < hn && SD_LESS(heap[r], heap[m])) {
          m = r;
        }
        if (m == j) {
          break;
        }
        {
          size_t t = heap[j];
          heap[j] = heap[m];
          heap[m] = t;
        }
        j = m;
      }
    }
#undef SD_KEY
#undef SD_LESS
  }
  if (out_piles != NULL && st == DS_OK) {
    *out_piles = np;
  }
  for (k = 0; k < np; k++) {
    ds_context_free(context, piles[k].front);
    ds_context_free(context, piles[k].back);
  }
  ds_context_free(context, piles);
  ds_context_free(context, heap);
  ds_context_free(context, copy);
  return st;
}
