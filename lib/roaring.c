#include "roaring.h"
#include <limits.h>
#include <string.h>

#define RB_W (sizeof(unsigned long) * CHAR_BIT)
#define RB_WORDS (65536UL / RB_W)
#define RB_ARRAY_MAX 4096U
#define RB_BITMAP_BYTES (RB_WORDS * sizeof(unsigned long))
#define RB_MAX32 0xFFFFFFFFUL

typedef ds_roaring_container_t rc_t;

static unsigned rb_popcount(unsigned long x) {
#if defined(__GNUC__)
  return (unsigned)__builtin_popcountl(x);
#else
  unsigned c = 0;
  while (x != 0) {
    x &= x - 1;
    c++;
  }
  return c;
#endif
}

static unsigned rb_ctz(unsigned long x) {
#if defined(__GNUC__)
  return (unsigned)__builtin_ctzl(x);
#else
  unsigned c = 0;
  while ((x & 1UL) == 0) {
    x >>= 1;
    c++;
  }
  return c;
#endif
}

/* ---- bitmap words ---- */

static int bm_get(const unsigned long *w, unsigned v) {
  return (int)((w[v / RB_W] >> (v % RB_W)) & 1UL);
}

/* what bm_range does to the bits lo .. hi */
#define BM_CLEAR 0
#define BM_SET 1
#define BM_FLIP 2
#define BM_COPY 3 /* or in the bits of src */

static void bm_range(unsigned long *w, const unsigned long *src, unsigned lo,
                     unsigned hi, int how) {
  size_t a = lo / RB_W, b = hi / RB_W, i;
  unsigned long ma = ~0UL << (lo % RB_W);
  unsigned long mb =
      (hi % RB_W == RB_W - 1) ? ~0UL : ((1UL << (hi % RB_W + 1)) - 1UL);
  for (i = a; i <= b; i++) {
    unsigned long m = (i == a ? ma : ~0UL) & (i == b ? mb : ~0UL);
    switch (how) {
    case BM_CLEAR:
      w[i] &= ~m;
      break;
    case BM_SET:
      w[i] |= m;
      break;
    case BM_FLIP:
      w[i] ^= m;
      break;
    default:
      w[i] |= src[i] & m;
      break;
    }
  }
}

static size_t bm_count(const unsigned long *w) {
  size_t i, n = 0;
  for (i = 0; i < RB_WORDS; i++) {
    n += rb_popcount(w[i]);
  }
  return n;
}

/* first set (bit = 1) or clear (bit = 0) position >= from, 65536 if none */
static unsigned long bm_next(const unsigned long *w, unsigned long from,
                             int bit) {
  size_t i;
  unsigned long x;
  if (from >= 65536UL) {
    return 65536UL;
  }
  i = from / RB_W;
  x = bit ? w[i] : ~w[i];
  x &= ~0UL << (from % RB_W);
  for (;;) {
    if (x != 0) {
      return (unsigned long)(i * RB_W + rb_ctz(x));
    }
    if (++i == RB_WORDS) {
      return 65536UL;
    }
    x = bit ? w[i] : ~w[i];
  }
}

static size_t bm_runs(const unsigned long *w) {
  size_t i, n = 0;
  unsigned long carry = 0;
  for (i = 0; i < RB_WORDS; i++) {
    n += rb_popcount(w[i] & ~((w[i] << 1) | carry));
    carry = w[i] >> (RB_W - 1);
  }
  return n;
}

/* ---- sorted 16-bit arrays ---- */

static size_t rb_lower(const unsigned short *a, size_t n, unsigned v) {
  size_t lo = 0, hi = n;
  while (lo < hi) {
    size_t mid = lo + (hi - lo) / 2;
    if ((unsigned)a[mid] < v) {
      lo = mid + 1;
    } else {
      hi = mid;
    }
  }
  return lo;
}

/* runs with start <= v */
static size_t run_upper(const rc_t *c, unsigned v) {
  size_t lo = 0, hi = c->count;
  while (lo < hi) {
    size_t mid = lo + (hi - lo) / 2;
    if ((unsigned)c->values[2 * mid] <= v) {
      lo = mid + 1;
    } else {
      hi = mid;
    }
  }
  return lo;
}

static unsigned run_start(const rc_t *c, size_t k) {
  return (unsigned)c->values[2 * k];
}

static unsigned run_end(const rc_t *c, size_t k) {
  return (unsigned)c->values[2 * k] + (unsigned)c->values[2 * k + 1];
}

/* ---- containers ---- */

static void rc_free(ds_context_t *ctx, rc_t *c) {
  ds_context_free(ctx, c->values);
  ds_context_free(ctx, c->bits);
  c->values = NULL;
  c->bits = NULL;
  c->count = 0;
  c->capacity = 0;
  c->cardinality = 0;
}

static void rc_expand(const rc_t *c, unsigned long *w) {
  size_t i;
  switch (c->kind) {
  case DS_ROARING_ARRAY:
    for (i = 0; i < c->count; i++) {
      w[c->values[i] / RB_W] |= 1UL << (c->values[i] % RB_W);
    }
    break;
  case DS_ROARING_BITMAP:
    memcpy(w, c->bits, RB_BITMAP_BYTES);
    break;
  case DS_ROARING_RUN:
    for (i = 0; i < c->count; i++) {
      bm_range(w, NULL, run_start(c, i), run_end(c, i), BM_SET);
    }
    break;
  }
}

/* replace c's payload with an array (card <= 4096) or bitmap built from w */
static ds_status_t rc_from_words(ds_context_t *ctx, rc_t *c,
                                 const unsigned long *w, size_t card) {
  rc_t n;
  memset(&n, 0, sizeof(n));
  n.cardinality = card;
  if (card <= RB_ARRAY_MAX) {
    unsigned long pos = bm_next(w, 0, 1);
    n.kind = DS_ROARING_ARRAY;
    n.values = (unsigned short *)ds_context_alloc(
        ctx, (card == 0 ? 1 : card) * sizeof(unsigned short));
    if (n.values == NULL) {
      return DS_ERR_ALLOC;
    }
    n.capacity = card;
    while (pos < 65536UL) {
      n.values[n.count++] = (unsigned short)pos;
      pos = bm_next(w, pos + 1, 1);
    }
  } else {
    n.kind = DS_ROARING_BITMAP;
    n.bits = (unsigned long *)ds_context_alloc(ctx, RB_BITMAP_BYTES);
    if (n.bits == NULL) {
      return DS_ERR_ALLOC;
    }
    memcpy(n.bits, w, RB_BITMAP_BYTES);
  }
  rc_free(ctx, c);
  *c = n;
  return DS_OK;
}

static ds_status_t rc_array_to_bitmap(ds_context_t *ctx, rc_t *c) {
  unsigned long *w =
      (unsigned long *)ds_context_calloc(ctx, RB_WORDS, sizeof(unsigned long));
  size_t card = c->cardinality;
  if (w == NULL) {
    return DS_ERR_ALLOC;
  }
  rc_expand(c, w);
  rc_free(ctx, c);
  c->kind = DS_ROARING_BITMAP;
  c->bits = w;
  c->cardinality = card;
  return DS_OK;
}

/* turn a run container into an array or bitmap by cardinality */
static ds_status_t rc_unpack(ds_context_t *ctx, rc_t *c) {
  size_t card = c->cardinality, i, k = 0;
  unsigned v;
  unsigned short *a;
  if (card > RB_ARRAY_MAX) {
    return rc_array_to_bitmap(ctx, c); /* rc_expand handles runs */
  }
  a = (unsigned short *)ds_context_alloc(ctx, (card == 0 ? 1 : card) *
                                                  sizeof(unsigned short));
  if (a == NULL) {
    return DS_ERR_ALLOC;
  }
  for (i = 0; i < c->count; i++) {
    for (v = run_start(c, i);; v++) {
      a[k++] = (unsigned short)v;
      if (v == run_end(c, i)) {
        break;
      }
    }
  }
  rc_free(ctx, c);
  c->kind = DS_ROARING_ARRAY;
  c->values = a;
  c->count = k;
  c->capacity = card;
  c->cardinality = k;
  return DS_OK;
}

static bool rc_contains(const rc_t *c, unsigned v) {
  size_t i;
  switch (c->kind) {
  case DS_ROARING_ARRAY:
    i = rb_lower(c->values, c->count, v);
    return i < c->count && (unsigned)c->values[i] == v;
  case DS_ROARING_BITMAP:
    return bm_get(c->bits, v) != 0;
  case DS_ROARING_RUN:
    i = run_upper(c, v);
    return i > 0 && v <= run_end(c, i - 1);
  }
  return false;
}

/* ---- run containers in place ---- */

/* bytes of the array or bitmap form of a container of card values */
static size_t rc_plain_bytes(size_t card) {
  return card <= RB_ARRAY_MAX ? 2 * card : RB_BITMAP_BYTES;
}

/* room for need runs */
static ds_status_t rc_run_reserve(ds_context_t *ctx, rc_t *c, size_t need) {
  size_t cap;
  unsigned short *v;
  if (need <= c->capacity) {
    return DS_OK;
  }
  cap = c->capacity < 4 ? 4 : c->capacity * 2;
  if (cap < need) {
    cap = need;
  }
  if (cap > 32768) {
    cap = 32768; /* no 16-bit set has more runs */
  }
  v = (unsigned short *)ds_context_realloc(ctx, c->values,
                                           2 * cap * sizeof(unsigned short));
  if (v == NULL) {
    return DS_ERR_ALLOC;
  }
  c->values = v;
  c->capacity = cap;
  return DS_OK;
}

/* after a change, keep a run container no larger than its array or
 * bitmap form (best effort: a larger run container is still valid) */
static void rc_run_tidy(ds_context_t *ctx, rc_t *c) {
  if (4 * c->count > rc_plain_bytes(c->cardinality)) {
    (void)rc_unpack(ctx, c);
  }
}

static ds_status_t rc_run_add(ds_context_t *ctx, rc_t *c, unsigned v) {
  size_t i = run_upper(c, v); /* runs starting at or before v */
  bool join_prev, join_next;
  ds_status_t st;
  if (i > 0 && v <= run_end(c, i - 1)) {
    return DS_EXISTS;
  }
  join_prev = i > 0 && run_end(c, i - 1) + 1 == v;
  join_next = i < c->count && run_start(c, i) == v + 1;
  if (join_prev && join_next) {
    /* v closes the gap between runs i - 1 and i */
    c->values[2 * (i - 1) + 1] =
        (unsigned short)(run_end(c, i) - run_start(c, i - 1));
    memmove(c->values + 2 * i, c->values + 2 * (i + 1),
            2 * (c->count - i - 1) * sizeof(unsigned short));
    c->count--;
  } else if (join_prev) {
    c->values[2 * (i - 1) + 1]++;
  } else if (join_next) {
    c->values[2 * i]--;
    c->values[2 * i + 1]++;
  } else {
    st = rc_run_reserve(ctx, c, c->count + 1);
    if (st != DS_OK) {
      return st;
    }
    memmove(c->values + 2 * (i + 1), c->values + 2 * i,
            2 * (c->count - i) * sizeof(unsigned short));
    c->values[2 * i] = (unsigned short)v;
    c->values[2 * i + 1] = 0;
    c->count++;
  }
  c->cardinality++;
  rc_run_tidy(ctx, c);
  return DS_OK;
}

static ds_status_t rc_run_remove(ds_context_t *ctx, rc_t *c, unsigned v) {
  size_t i = run_upper(c, v), k;
  unsigned s, e;
  ds_status_t st;
  if (i == 0 || v > run_end(c, i - 1)) {
    return DS_NOT_FOUND;
  }
  k = i - 1;
  s = run_start(c, k);
  e = run_end(c, k);
  if (s == e) {
    memmove(c->values + 2 * k, c->values + 2 * (k + 1),
            2 * (c->count - k - 1) * sizeof(unsigned short));
    c->count--;
  } else if (v == s) {
    c->values[2 * k]++;
    c->values[2 * k + 1]--;
  } else if (v == e) {
    c->values[2 * k + 1]--;
  } else {
    /* split [s, e] into [s, v - 1] and [v + 1, e] */
    st = rc_run_reserve(ctx, c, c->count + 1);
    if (st != DS_OK) {
      return st;
    }
    memmove(c->values + 2 * (k + 2), c->values + 2 * (k + 1),
            2 * (c->count - k - 1) * sizeof(unsigned short));
    c->values[2 * k + 1] = (unsigned short)(v - 1 - s);
    c->values[2 * (k + 1)] = (unsigned short)(v + 1);
    c->values[2 * (k + 1) + 1] = (unsigned short)(e - v - 1);
    c->count++;
  }
  c->cardinality--;
  rc_run_tidy(ctx, c);
  return DS_OK;
}

static ds_status_t rc_add(ds_context_t *ctx, rc_t *c, unsigned v) {
  size_t i;
  ds_status_t st;
  if (c->kind == DS_ROARING_RUN) {
    return rc_run_add(ctx, c, v);
  }
  if (c->kind == DS_ROARING_ARRAY) {
    i = rb_lower(c->values, c->count, v);
    if (i < c->count && (unsigned)c->values[i] == v) {
      return DS_EXISTS;
    }
    if (c->count >= RB_ARRAY_MAX) {
      st = rc_array_to_bitmap(ctx, c);
      if (st != DS_OK) {
        return st;
      }
    } else {
      if (c->count == c->capacity) {
        size_t cap = c->capacity < 4 ? 4 : c->capacity * 2;
        unsigned short *a;
        if (cap > RB_ARRAY_MAX) {
          cap = RB_ARRAY_MAX;
        }
        a = (unsigned short *)ds_context_realloc(ctx, c->values,
                                                 cap * sizeof(unsigned short));
        if (a == NULL) {
          return DS_ERR_ALLOC;
        }
        c->values = a;
        c->capacity = cap;
      }
      memmove(c->values + i + 1, c->values + i,
              (c->count - i) * sizeof(unsigned short));
      c->values[i] = (unsigned short)v;
      c->count++;
      c->cardinality++;
      return DS_OK;
    }
  }
  if (bm_get(c->bits, v)) {
    return DS_EXISTS;
  }
  c->bits[v / RB_W] |= 1UL << (v % RB_W);
  c->cardinality++;
  return DS_OK;
}

static ds_status_t rc_remove(ds_context_t *ctx, rc_t *c, unsigned v) {
  size_t i;
  if (c->kind == DS_ROARING_RUN) {
    return rc_run_remove(ctx, c, v);
  }
  if (!rc_contains(c, v)) {
    return DS_NOT_FOUND;
  }
  if (c->kind == DS_ROARING_ARRAY) {
    i = rb_lower(c->values, c->count, v);
    memmove(c->values + i, c->values + i + 1,
            (c->count - i - 1) * sizeof(unsigned short));
    c->count--;
    c->cardinality--;
    return DS_OK;
  }
  c->bits[v / RB_W] &= ~(1UL << (v % RB_W));
  c->cardinality--;
  if (c->cardinality <= RB_ARRAY_MAX) {
    /* best effort: a bitmap that cannot shrink is still valid */
    unsigned long *w = c->bits;
    c->bits = NULL;
    if (rc_from_words(ctx, c, w, c->cardinality) != DS_OK) {
      c->bits = w;
    } else {
      ds_context_free(ctx, w);
    }
  }
  return DS_OK;
}

/* elements <= v */
static size_t rc_rank(const rc_t *c, unsigned v) {
  size_t i, r = 0, k;
  switch (c->kind) {
  case DS_ROARING_ARRAY:
    return rb_lower(c->values, c->count, v + 1);
  case DS_ROARING_BITMAP:
    for (i = 0; i < v / RB_W; i++) {
      r += rb_popcount(c->bits[i]);
    }
    k = v % RB_W;
    r += rb_popcount(c->bits[i] &
                     (k == RB_W - 1 ? ~0UL : ((1UL << (k + 1)) - 1UL)));
    return r;
  case DS_ROARING_RUN:
    k = run_upper(c, v);
    for (i = 0; i < k; i++) {
      unsigned e = run_end(c, i);
      r += (size_t)((e < v ? e : v) - run_start(c, i) + 1);
    }
    return r;
  }
  return 0;
}

static unsigned rc_select(const rc_t *c, size_t k) {
  size_t i;
  switch (c->kind) {
  case DS_ROARING_ARRAY:
    return (unsigned)c->values[k];
  case DS_ROARING_BITMAP:
    for (i = 0; i < RB_WORDS; i++) {
      size_t p = rb_popcount(c->bits[i]);
      if (k < p) {
        unsigned long x = c->bits[i];
        while (k-- > 0) {
          x &= x - 1;
        }
        return (unsigned)(i * RB_W + rb_ctz(x));
      }
      k -= p;
    }
    break;
  case DS_ROARING_RUN:
    for (i = 0; i < c->count; i++) {
      size_t len = (size_t)c->values[2 * i + 1] + 1;
      if (k < len) {
        return run_start(c, i) + (unsigned)k;
      }
      k -= len;
    }
    break;
  }
  return 0;
}

static bool rc_next(const rc_t *c, unsigned v, unsigned *out) {
  size_t i;
  unsigned long p;
  switch (c->kind) {
  case DS_ROARING_ARRAY:
    i = rb_lower(c->values, c->count, v);
    if (i < c->count) {
      *out = (unsigned)c->values[i];
      return true;
    }
    return false;
  case DS_ROARING_BITMAP:
    p = bm_next(c->bits, v, 1);
    if (p < 65536UL) {
      *out = (unsigned)p;
      return true;
    }
    return false;
  case DS_ROARING_RUN:
    i = run_upper(c, v);
    if (i > 0 && v <= run_end(c, i - 1)) {
      *out = v;
      return true;
    }
    if (i < c->count) {
      *out = run_start(c, i);
      return true;
    }
    return false;
  }
  return false;
}

static ds_status_t rc_clone(ds_context_t *ctx, const rc_t *src, rc_t *dst) {
  *dst = *src;
  dst->values = NULL;
  dst->bits = NULL;
  if (src->kind == DS_ROARING_BITMAP) {
    dst->bits = (unsigned long *)ds_context_alloc(ctx, RB_BITMAP_BYTES);
    if (dst->bits == NULL) {
      return DS_ERR_ALLOC;
    }
    memcpy(dst->bits, src->bits, RB_BITMAP_BYTES);
  } else {
    size_t n = src->kind == DS_ROARING_RUN ? 2 * src->count : src->count;
    dst->values = (unsigned short *)ds_context_alloc(
        ctx, (n == 0 ? 1 : n) * sizeof(unsigned short));
    if (dst->values == NULL) {
      return DS_ERR_ALLOC;
    }
    if (n > 0) {
      memcpy(dst->values, src->values, n * sizeof(unsigned short));
    }
    dst->capacity = src->kind == DS_ROARING_RUN ? src->count : n;
  }
  return DS_OK;
}

/* ---- directory ---- */

static size_t dir_lower(const ds_roaring_t *r, unsigned key) {
  return rb_lower(r->keys, r->size, key);
}

static ds_status_t dir_reserve(ds_roaring_t *r, size_t need) {
  size_t cap;
  unsigned short *k;
  rc_t *c;
  if (need <= r->capacity) {
    return DS_OK;
  }
  cap = r->capacity < 4 ? 4 : r->capacity * 2;
  if (cap < need) {
    cap = need;
  }
  k = (unsigned short *)ds_context_realloc(r->context, r->keys,
                                           cap * sizeof(unsigned short));
  if (k == NULL) {
    return DS_ERR_ALLOC;
  }
  r->keys = k; /* a larger key array is harmless if the next step fails */
  c = (rc_t *)ds_context_realloc(r->context, r->containers, cap * sizeof(rc_t));
  if (c == NULL) {
    return DS_ERR_ALLOC;
  }
  r->containers = c;
  r->capacity = cap;
  return DS_OK;
}

static ds_status_t dir_insert(ds_roaring_t *r, size_t pos, unsigned key) {
  ds_status_t st = dir_reserve(r, r->size + 1);
  if (st != DS_OK) {
    return st;
  }
  memmove(r->keys + pos + 1, r->keys + pos,
          (r->size - pos) * sizeof(unsigned short));
  memmove(r->containers + pos + 1, r->containers + pos,
          (r->size - pos) * sizeof(rc_t));
  r->keys[pos] = (unsigned short)key;
  memset(&r->containers[pos], 0, sizeof(rc_t));
  r->containers[pos].kind = DS_ROARING_ARRAY;
  r->size++;
  return DS_OK;
}

static void dir_erase(ds_roaring_t *r, size_t pos) {
  rc_free(r->context, &r->containers[pos]);
  memmove(r->keys + pos, r->keys + pos + 1,
          (r->size - pos - 1) * sizeof(unsigned short));
  memmove(r->containers + pos, r->containers + pos + 1,
          (r->size - pos - 1) * sizeof(rc_t));
  r->size--;
}

/* ---- public ---- */

void ds_roaring_init(ds_roaring_t *r, ds_context_t *context) {
  if (r == NULL) {
    return;
  }
  r->keys = NULL;
  r->containers = NULL;
  r->size = 0;
  r->capacity = 0;
  r->context = context;
}

void ds_roaring_destroy(ds_roaring_t *r) {
  size_t i;
  if (r == NULL) {
    return;
  }
  for (i = 0; i < r->size; i++) {
    rc_free(r->context, &r->containers[i]);
  }
  ds_context_free(r->context, r->keys);
  ds_context_free(r->context, r->containers);
  ds_roaring_init(r, r->context);
}

ds_status_t ds_roaring_add(ds_roaring_t *r, unsigned long x) {
  unsigned high, low;
  size_t pos;
  ds_status_t st;
  if (r == NULL) {
    return DS_ERR_NULL;
  }
  if (x > RB_MAX32) {
    return DS_ERR_RANGE;
  }
  high = (unsigned)(x >> 16);
  low = (unsigned)(x & 0xFFFFUL);
  pos = dir_lower(r, high);
  if (pos == r->size || (unsigned)r->keys[pos] != high) {
    st = dir_insert(r, pos, high);
    if (st != DS_OK) {
      return st;
    }
  }
  st = rc_add(r->context, &r->containers[pos], low);
  if (DS_FAILED(st) && r->containers[pos].cardinality == 0) {
    dir_erase(r, pos);
  }
  return st;
}

ds_status_t ds_roaring_remove(ds_roaring_t *r, unsigned long x) {
  unsigned high;
  size_t pos;
  ds_status_t st;
  if (r == NULL) {
    return DS_ERR_NULL;
  }
  if (x > RB_MAX32) {
    return DS_NOT_FOUND;
  }
  high = (unsigned)(x >> 16);
  pos = dir_lower(r, high);
  if (pos == r->size || (unsigned)r->keys[pos] != high) {
    return DS_NOT_FOUND;
  }
  st = rc_remove(r->context, &r->containers[pos], (unsigned)(x & 0xFFFFUL));
  if (st == DS_OK && r->containers[pos].cardinality == 0) {
    dir_erase(r, pos);
  }
  return st;
}

bool ds_roaring_contains(const ds_roaring_t *r, unsigned long x) {
  unsigned high;
  size_t pos;
  if (r == NULL || x > RB_MAX32) {
    return false;
  }
  high = (unsigned)(x >> 16);
  pos = dir_lower(r, high);
  return pos < r->size && (unsigned)r->keys[pos] == high &&
         rc_contains(&r->containers[pos], (unsigned)(x & 0xFFFFUL));
}

unsigned long ds_roaring_cardinality(const ds_roaring_t *r) {
  size_t i;
  unsigned long n = 0;
  if (r == NULL) {
    return 0;
  }
  for (i = 0; i < r->size; i++) {
    n += (unsigned long)r->containers[i].cardinality;
  }
  return n;
}

unsigned long ds_roaring_rank(const ds_roaring_t *r, unsigned long x) {
  unsigned high;
  size_t i;
  unsigned long n = 0;
  if (r == NULL) {
    return 0;
  }
  if (x > RB_MAX32) {
    x = RB_MAX32;
  }
  high = (unsigned)(x >> 16);
  for (i = 0; i < r->size && (unsigned)r->keys[i] < high; i++) {
    n += (unsigned long)r->containers[i].cardinality;
  }
  if (i < r->size && (unsigned)r->keys[i] == high) {
    n += (unsigned long)rc_rank(&r->containers[i], (unsigned)(x & 0xFFFFUL));
  }
  return n;
}

ds_status_t ds_roaring_select(const ds_roaring_t *r, unsigned long k,
                              unsigned long *out) {
  size_t i;
  if (r == NULL || out == NULL) {
    return DS_ERR_NULL;
  }
  for (i = 0; i < r->size; i++) {
    unsigned long c = (unsigned long)r->containers[i].cardinality;
    if (k < c) {
      *out = ((unsigned long)r->keys[i] << 16) |
             (unsigned long)rc_select(&r->containers[i], (size_t)k);
      return DS_OK;
    }
    k -= c;
  }
  return DS_NOT_FOUND;
}

ds_status_t ds_roaring_next(const ds_roaring_t *r, unsigned long x,
                            unsigned long *out) {
  unsigned high, v;
  size_t i;
  if (r == NULL || out == NULL) {
    return DS_ERR_NULL;
  }
  if (x > RB_MAX32) {
    return DS_NOT_FOUND;
  }
  high = (unsigned)(x >> 16);
  i = dir_lower(r, high);
  if (i < r->size && (unsigned)r->keys[i] == high) {
    if (rc_next(&r->containers[i], (unsigned)(x & 0xFFFFUL), &v)) {
      *out = ((unsigned long)high << 16) | (unsigned long)v;
      return DS_OK;
    }
    i++;
  }
  if (i < r->size && rc_next(&r->containers[i], 0, &v)) {
    *out = ((unsigned long)r->keys[i] << 16) | (unsigned long)v;
    return DS_OK;
  }
  return DS_NOT_FOUND;
}

static ds_status_t rb_push(ds_roaring_t *r, unsigned key, rc_t *c) {
  ds_status_t st = dir_reserve(r, r->size + 1);
  if (st != DS_OK) {
    rc_free(r->context, c);
    return st;
  }
  r->keys[r->size] = (unsigned short)key;
  r->containers[r->size] = *c;
  r->size++;
  return DS_OK;
}

static size_t rb_merge(const rc_t *a, const rc_t *b, ds_roaring_op_t op,
                       unsigned short *out) {
  size_t i = 0, j = 0, n = 0;
  while (i < a->count || j < b->count) {
    if (j == b->count || (i < a->count && a->values[i] < b->values[j])) {
      if (op != DS_ROARING_AND) {
        out[n++] = a->values[i];
      }
      i++;
    } else if (i == a->count || b->values[j] < a->values[i]) {
      if (op == DS_ROARING_OR || op == DS_ROARING_XOR) {
        out[n++] = b->values[j];
      }
      j++;
    } else {
      if (op == DS_ROARING_AND || op == DS_ROARING_OR) {
        out[n++] = a->values[i];
      }
      i++;
      j++;
    }
  }
  return n;
}

/* ---- operations with run containers, without converting them ---- */

static bool rb_keep(ds_roaring_op_t op, bool x, bool y) {
  switch (op) {
  case DS_ROARING_AND:
    return x && y;
  case DS_ROARING_OR:
    return x || y;
  case DS_ROARING_XOR:
    return x != y;
  case DS_ROARING_ANDNOT:
    return x && !y;
  }
  return false;
}

/* a set of runs: n (start, length - 1) pairs, in order and apart */
typedef struct rb_runs {
  const unsigned short *v;
  size_t n;
} rb_runs_t;

/* where membership in runs r changes next at or after pos (*k is the first
 * run not ending before pos); *in says whether pos is inside a run */
static unsigned long rb_runs_edge(const rb_runs_t *r, size_t *k,
                                  unsigned long pos, bool *in) {
  unsigned long s, e;
  while (*k < r->n && (unsigned long)r->v[2 * *k] + r->v[2 * *k + 1] < pos) {
    (*k)++;
  }
  if (*k == r->n) {
    *in = false;
    return 65536UL;
  }
  s = r->v[2 * *k];
  e = s + r->v[2 * *k + 1];
  *in = pos >= s;
  return *in ? e + 1 : s;
}

/* a op b over runs, in one sweep over the boundaries of both: the result
 * as runs in out (room for 32768) and its cardinality */
static size_t rb_runs_op(const rb_runs_t *a, const rb_runs_t *b,
                         ds_roaring_op_t op, unsigned short *out,
                         size_t *card) {
  size_t i = 0, j = 0, n = 0;
  unsigned long pos = 0;
  *card = 0;
  while (pos < 65536UL && (i < a->n || j < b->n)) {
    bool x, y;
    unsigned long na = rb_runs_edge(a, &i, pos, &x);
    unsigned long nb = rb_runs_edge(b, &j, pos, &y);
    unsigned long next = na < nb ? na : nb;
    if (rb_keep(op, x, y)) {
      if (n > 0 &&
          (unsigned long)out[2 * (n - 1)] + out[2 * (n - 1) + 1] + 1 == pos) {
        out[2 * (n - 1) + 1] = (unsigned short)(next - 1 - out[2 * (n - 1)]);
      } else {
        out[2 * n] = (unsigned short)pos;
        out[2 * n + 1] = (unsigned short)(next - 1 - pos);
        n++;
      }
      *card += (size_t)(next - pos);
    }
    pos = next;
  }
  return n;
}

/* the runs of a sorted array, into out (room for 4096) */
static void rb_array_runs(const rc_t *c, unsigned short *out, rb_runs_t *r) {
  size_t i, n = 0;
  for (i = 0; i < c->count; i++) {
    if (n > 0 && (unsigned)out[2 * (n - 1)] + out[2 * (n - 1) + 1] + 1 ==
                     (unsigned)c->values[i]) {
      out[2 * (n - 1) + 1]++;
    } else {
      out[2 * n] = c->values[i];
      out[2 * n + 1] = 0;
      n++;
    }
  }
  r->v = out;
  r->n = n;
}

/* the container of the smallest form for runs (cardinality card) */
static ds_status_t rc_from_runs(ds_context_t *ctx, rc_t *out,
                                const unsigned short *runs, size_t n,
                                size_t card) {
  size_t i, k = 0;
  memset(out, 0, sizeof(*out));
  out->kind = DS_ROARING_ARRAY;
  if (card == 0) {
    return DS_OK;
  }
  out->cardinality = card;
  if (4 * n < rc_plain_bytes(card)) {
    out->kind = DS_ROARING_RUN;
    out->values =
        (unsigned short *)ds_context_alloc(ctx, 2 * n * sizeof(unsigned short));
    if (out->values == NULL) {
      return DS_ERR_ALLOC;
    }
    memcpy(out->values, runs, 2 * n * sizeof(unsigned short));
    out->count = n;
    out->capacity = n;
  } else if (card <= RB_ARRAY_MAX) {
    out->values =
        (unsigned short *)ds_context_alloc(ctx, card * sizeof(unsigned short));
    if (out->values == NULL) {
      return DS_ERR_ALLOC;
    }
    for (i = 0; i < n; i++) {
      unsigned v = runs[2 * i], e = v + runs[2 * i + 1];
      for (;; v++) {
        out->values[k++] = (unsigned short)v;
        if (v == e) {
          break;
        }
      }
    }
    out->count = card;
    out->capacity = card;
  } else {
    out->kind = DS_ROARING_BITMAP;
    out->bits = (unsigned long *)ds_context_calloc(ctx, RB_WORDS,
                                                   sizeof(unsigned long));
    if (out->bits == NULL) {
      return DS_ERR_ALLOC;
    }
    for (i = 0; i < n; i++) {
      bm_range(out->bits, NULL, runs[2 * i],
               (unsigned)runs[2 * i] + runs[2 * i + 1], BM_SET);
    }
  }
  return DS_OK;
}

/* the container of the smallest form for the bits of w */
static ds_status_t rc_best_from_words(ds_context_t *ctx, rc_t *out,
                                      const unsigned long *w, size_t card) {
  size_t nr;
  memset(out, 0, sizeof(*out));
  out->kind = DS_ROARING_ARRAY;
  if (card == 0) {
    return DS_OK;
  }
  nr = bm_runs(w);
  if (4 * nr < rc_plain_bytes(card)) {
    unsigned long p = bm_next(w, 0, 1);
    out->values = (unsigned short *)ds_context_alloc(
        ctx, 2 * nr * sizeof(unsigned short));
    if (out->values == NULL) {
      return DS_ERR_ALLOC;
    }
    out->kind = DS_ROARING_RUN;
    while (p < 65536UL) {
      unsigned long e = bm_next(w, p, 0);
      out->values[2 * out->count] = (unsigned short)p;
      out->values[2 * out->count + 1] = (unsigned short)(e - p - 1);
      out->count++;
      p = bm_next(w, e, 1);
    }
    out->capacity = nr;
    out->cardinality = card;
    return DS_OK;
  }
  return rc_from_words(ctx, out, w, card);
}

/* the values of an array inside (or outside) runs, two pointers */
static size_t rb_array_in_runs(const rc_t *arr, const rc_t *run, bool inside,
                               unsigned short *out) {
  size_t i, k = 0, n = 0;
  for (i = 0; i < arr->count; i++) {
    unsigned v = arr->values[i];
    bool in;
    while (k < run->count && run_end(run, k) < v) {
      k++;
    }
    in = k < run->count && run_start(run, k) <= v;
    if (in == inside) {
      out[n++] = (unsigned short)v;
    }
  }
  return n;
}

/* a op b where a or b is a run container; wa is a bitmap of scratch, tmp
 * room for 4096 runs, runbuf room for 32768 */
static ds_status_t rb_pair_runs(ds_context_t *ctx, const rc_t *a, const rc_t *b,
                                ds_roaring_op_t op, unsigned long *wa,
                                unsigned short *tmp, unsigned short *runbuf,
                                rc_t *out) {
  rb_runs_t ra, rb;
  size_t n, card, i;
  const rc_t *run = a->kind == DS_ROARING_RUN ? a : b;
  const rc_t *other = run == a ? b : a;
  memset(out, 0, sizeof(*out));
  out->kind = DS_ROARING_ARRAY;
  if (other->kind == DS_ROARING_BITMAP) {
    /* on a copy of the bitmap, a word range at a time per run */
    bool run_first = run == a;
    if (op == DS_ROARING_AND) {
      memset(wa, 0, RB_BITMAP_BYTES);
    } else if (op == DS_ROARING_ANDNOT && run_first) {
      memset(wa, 0, RB_BITMAP_BYTES); /* run \ bitmap: the runs, then mask */
    } else {
      memcpy(wa, other->bits, RB_BITMAP_BYTES);
    }
    for (i = 0; i < run->count; i++) {
      unsigned lo = run_start(run, i), hi = run_end(run, i);
      switch (op) {
      case DS_ROARING_AND:
        bm_range(wa, other->bits, lo, hi, BM_COPY);
        break;
      case DS_ROARING_OR:
        bm_range(wa, NULL, lo, hi, BM_SET);
        break;
      case DS_ROARING_XOR:
        bm_range(wa, NULL, lo, hi, BM_FLIP);
        break;
      case DS_ROARING_ANDNOT:
        bm_range(wa, NULL, lo, hi, run_first ? BM_SET : BM_CLEAR);
        break;
      }
    }
    if (op == DS_ROARING_ANDNOT && run_first) {
      for (i = 0; i < RB_WORDS; i++) {
        wa[i] &= ~other->bits[i];
      }
    }
    return rc_best_from_words(ctx, out, wa, bm_count(wa));
  }
  if (other->kind == DS_ROARING_ARRAY &&
      (op == DS_ROARING_AND || (op == DS_ROARING_ANDNOT && other == a))) {
    /* the result is a subset of the array */
    n = rb_array_in_runs(other, run, op == DS_ROARING_AND, tmp);
    if (n == 0) {
      return DS_OK;
    }
    out->values =
        (unsigned short *)ds_context_alloc(ctx, n * sizeof(unsigned short));
    if (out->values == NULL) {
      return DS_ERR_ALLOC;
    }
    memcpy(out->values, tmp, n * sizeof(unsigned short));
    out->count = n;
    out->capacity = n;
    out->cardinality = n;
    return DS_OK;
  }
  /* run op run, or an array taken as its runs */
  if (a->kind == DS_ROARING_RUN) {
    ra.v = a->values;
    ra.n = a->count;
  } else {
    rb_array_runs(a, tmp, &ra);
  }
  if (b->kind == DS_ROARING_RUN) {
    rb.v = b->values;
    rb.n = b->count;
  } else {
    rb_array_runs(b, tmp, &rb);
  }
  n = rb_runs_op(&ra, &rb, op, runbuf, &card);
  return rc_from_runs(ctx, out, runbuf, n, card);
}

static ds_status_t rb_pair(ds_context_t *ctx, const rc_t *a, const rc_t *b,
                           ds_roaring_op_t op, unsigned long *wa,
                           unsigned long *wb, unsigned short *tmp,
                           unsigned short *runbuf, rc_t *out) {
  size_t i, card = 0;
  if (a->kind == DS_ROARING_RUN || b->kind == DS_ROARING_RUN) {
    return rb_pair_runs(ctx, a, b, op, wa, tmp, runbuf, out);
  }
  memset(out, 0, sizeof(*out));
  out->kind = DS_ROARING_ARRAY;
  if (a->kind == DS_ROARING_ARRAY && b->kind == DS_ROARING_ARRAY) {
    size_t n = rb_merge(a, b, op, tmp);
    if (n <= RB_ARRAY_MAX) {
      if (n == 0) {
        return DS_OK;
      }
      out->values =
          (unsigned short *)ds_context_alloc(ctx, n * sizeof(unsigned short));
      if (out->values == NULL) {
        return DS_ERR_ALLOC;
      }
      memcpy(out->values, tmp, n * sizeof(unsigned short));
      out->count = n;
      out->capacity = n;
      out->cardinality = n;
      return DS_OK;
    }
  }
  memset(wa, 0, RB_BITMAP_BYTES);
  memset(wb, 0, RB_BITMAP_BYTES);
  rc_expand(a, wa);
  rc_expand(b, wb);
  for (i = 0; i < RB_WORDS; i++) {
    switch (op) {
    case DS_ROARING_AND:
      wa[i] &= wb[i];
      break;
    case DS_ROARING_OR:
      wa[i] |= wb[i];
      break;
    case DS_ROARING_XOR:
      wa[i] ^= wb[i];
      break;
    case DS_ROARING_ANDNOT:
      wa[i] &= ~wb[i];
      break;
    }
    card += rb_popcount(wa[i]);
  }
  if (card == 0) {
    return DS_OK;
  }
  return rc_from_words(ctx, out, wa, card);
}

static bool rb_has_runs(const ds_roaring_t *r) {
  size_t i;
  for (i = 0; i < r->size; i++) {
    if (r->containers[i].kind == DS_ROARING_RUN) {
      return true;
    }
  }
  return false;
}

ds_status_t ds_roaring_combine(const ds_roaring_t *a, const ds_roaring_t *b,
                               ds_roaring_op_t op, ds_roaring_t *out,
                               ds_context_t *context) {
  size_t i = 0, j = 0;
  unsigned long *wa;
  unsigned short *tmp;
  unsigned short *runbuf = NULL;
  size_t work = 2 * RB_BITMAP_BYTES + 2 * RB_ARRAY_MAX * sizeof(unsigned short);
  ds_status_t st = DS_OK;
  if (a == NULL || b == NULL || out == NULL) {
    return DS_ERR_NULL;
  }
  if (out == a || out == b) {
    return DS_ERR_INVALID;
  }
  ds_roaring_init(out, context);
  if (rb_has_runs(a) || rb_has_runs(b)) {
    work += 2 * 32768 * sizeof(unsigned short); /* the most runs there are */
  }
  wa = (unsigned long *)ds_context_alloc(context, work);
  if (wa == NULL) {
    return DS_ERR_ALLOC;
  }
  tmp = (unsigned short *)(void *)(wa + 2 * RB_WORDS);
  if (work > 2 * RB_BITMAP_BYTES + 2 * RB_ARRAY_MAX * sizeof(unsigned short)) {
    runbuf = tmp + 2 * RB_ARRAY_MAX;
  }
  while (st == DS_OK && (i < a->size || j < b->size)) {
    rc_t c;
    unsigned key;
    bool keep;
    memset(&c, 0, sizeof(c));
    if (j == b->size || (i < a->size && a->keys[i] < b->keys[j])) {
      key = (unsigned)a->keys[i];
      keep = op != DS_ROARING_AND;
      st = keep ? rc_clone(context, &a->containers[i], &c) : DS_OK;
      i++;
    } else if (i == a->size || b->keys[j] < a->keys[i]) {
      key = (unsigned)b->keys[j];
      keep = op == DS_ROARING_OR || op == DS_ROARING_XOR;
      st = keep ? rc_clone(context, &b->containers[j], &c) : DS_OK;
      j++;
    } else {
      key = (unsigned)a->keys[i];
      st = rb_pair(context, &a->containers[i], &b->containers[j], op, wa,
                   wa + RB_WORDS, tmp, runbuf, &c);
      keep = c.cardinality > 0;
      i++;
      j++;
    }
    if (st != DS_OK) {
      rc_free(context, &c);
    } else if (keep) {
      st = rb_push(out, key, &c);
    }
  }
  ds_context_free(context, wa);
  if (st != DS_OK) {
    ds_roaring_destroy(out);
  }
  return st;
}

bool ds_roaring_equals(const ds_roaring_t *a, const ds_roaring_t *b) {
  size_t i;
  if (a == NULL || b == NULL) {
    return a == b;
  }
  if (a->size != b->size) {
    return false;
  }
  for (i = 0; i < a->size; i++) {
    const rc_t *x = &a->containers[i], *y = &b->containers[i];
    unsigned u = 0, v = 0;
    bool more;
    if (a->keys[i] != b->keys[i] || x->cardinality != y->cardinality) {
      return false;
    }
    if (x->kind == y->kind) {
      if (x->kind == DS_ROARING_BITMAP) {
        if (memcmp(x->bits, y->bits, RB_BITMAP_BYTES) != 0) {
          return false;
        }
        continue;
      }
      if (x->count != y->count) {
        return false;
      }
      if (x->count > 0 && memcmp(x->values, y->values,
                                 (x->kind == DS_ROARING_RUN ? 2 : 1) *
                                     x->count * sizeof(unsigned short)) != 0) {
        return false;
      }
      continue;
    }
    /* same cardinality: compare element by element */
    more = rc_next(x, 0, &u);
    while (more) {
      if (!rc_next(y, u, &v) || u != v) {
        return false;
      }
      more = u < 65535U && rc_next(x, u + 1, &u);
    }
  }
  return true;
}

static size_t rc_runs(const rc_t *c) {
  size_t i, n = 0;
  switch (c->kind) {
  case DS_ROARING_ARRAY:
    for (i = 0; i < c->count; i++) {
      if (i == 0 || (unsigned)c->values[i] != (unsigned)c->values[i - 1] + 1) {
        n++;
      }
    }
    return n;
  case DS_ROARING_BITMAP:
    return bm_runs(c->bits);
  case DS_ROARING_RUN:
    return c->count;
  }
  return 0;
}

static ds_status_t rc_to_runs(ds_context_t *ctx, rc_t *c, size_t nr) {
  unsigned short *v = (unsigned short *)ds_context_alloc(
      ctx, 2 * (nr == 0 ? 1 : nr) * sizeof(unsigned short));
  size_t k = 0, i, card = c->cardinality;
  if (v == NULL) {
    return DS_ERR_ALLOC;
  }
  if (c->kind == DS_ROARING_ARRAY) {
    for (i = 0; i < c->count; i++) {
      if (i == 0 || (unsigned)c->values[i] != (unsigned)c->values[i - 1] + 1) {
        v[2 * k] = c->values[i];
        v[2 * k + 1] = 0;
        k++;
      } else {
        v[2 * k - 1]++;
      }
    }
  } else {
    unsigned long s = bm_next(c->bits, 0, 1);
    while (s < 65536UL) {
      unsigned long e = bm_next(c->bits, s, 0);
      v[2 * k] = (unsigned short)s;
      v[2 * k + 1] = (unsigned short)(e - s - 1);
      k++;
      s = bm_next(c->bits, e, 1);
    }
  }
  rc_free(ctx, c);
  c->kind = DS_ROARING_RUN;
  c->values = v;
  c->count = k;
  c->capacity = nr;
  c->cardinality = card;
  return DS_OK;
}

ds_status_t ds_roaring_optimize(ds_roaring_t *r) {
  size_t i;
  if (r == NULL) {
    return DS_ERR_NULL;
  }
  for (i = 0; i < r->size; i++) {
    rc_t *c = &r->containers[i];
    size_t nr = rc_runs(c);
    size_t run_bytes = 4 * nr;
    size_t plain =
        c->cardinality <= RB_ARRAY_MAX ? 2 * c->cardinality : RB_BITMAP_BYTES;
    ds_status_t st = DS_OK;
    if (c->kind != DS_ROARING_RUN && run_bytes < plain) {
      st = rc_to_runs(r->context, c, nr);
    } else if (c->kind == DS_ROARING_RUN && plain < run_bytes) {
      st = rc_unpack(r->context, c);
    } else if (c->kind == DS_ROARING_ARRAY && c->capacity > c->count) {
      unsigned short *a = (unsigned short *)ds_context_realloc(
          r->context, c->values, c->count * sizeof(unsigned short));
      if (a != NULL) {
        c->values = a;
        c->capacity = c->count;
      }
    }
    if (st != DS_OK) {
      return st;
    }
  }
  return DS_OK;
}

size_t ds_roaring_bytes(const ds_roaring_t *r) {
  size_t i, n;
  if (r == NULL) {
    return 0;
  }
  n = r->capacity * (sizeof(unsigned short) + sizeof(rc_t));
  for (i = 0; i < r->size; i++) {
    const rc_t *c = &r->containers[i];
    if (c->kind == DS_ROARING_BITMAP) {
      n += RB_BITMAP_BYTES;
    } else if (c->kind == DS_ROARING_RUN) {
      n += 2 * c->capacity * sizeof(unsigned short);
    } else {
      n += c->capacity * sizeof(unsigned short);
    }
  }
  return n;
}

void ds_roaring_stats(const ds_roaring_t *r, size_t *arrays, size_t *bitmaps,
                      size_t *runs) {
  size_t i, n[3] = {0, 0, 0};
  if (r != NULL) {
    for (i = 0; i < r->size; i++) {
      n[r->containers[i].kind]++;
    }
  }
  if (arrays != NULL) {
    *arrays = n[0];
  }
  if (bitmaps != NULL) {
    *bitmaps = n[1];
  }
  if (runs != NULL) {
    *runs = n[2];
  }
}
