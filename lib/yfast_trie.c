#include "yfast_trie.h"
#include <limits.h>
#include <string.h>

#define YF_MAXBITS (sizeof(unsigned long) * CHAR_BIT)

static ds_status_t yf_reserve(ds_context_t *ctx, void **p, size_t *cap,
                              size_t need, size_t elem) {
  size_t nc;
  void *q;
  if (need <= *cap) {
    return DS_OK;
  }
  nc = *cap < 4 ? 8 : *cap;
  while (nc < need) {
    if (nc > (size_t)-1 / 2 / (elem == 0 ? 1 : elem)) {
      return DS_ERR_OVERFLOW;
    }
    nc *= 2;
  }
  q = ds_context_realloc(ctx, *p, nc * (elem == 0 ? 1 : elem));
  if (q == NULL) {
    return DS_ERR_ALLOC;
  }
  *p = q;
  *cap = nc;
  return DS_OK;
}

static ds_status_t yf_bucket_reserve(ds_yfast_t *t, ds_yfast_bucket_t *b,
                                     size_t need) {
  size_t cap = b->capacity;
  ds_status_t st = yf_reserve(t->context, (void **)&b->keys, &cap, need,
                              sizeof(unsigned long));
  if (st == DS_OK && t->value_size > 0) {
    cap = b->capacity;
    st = yf_reserve(t->context, (void **)&b->values, &cap, need, t->value_size);
  }
  if (st == DS_OK) {
    b->capacity = cap;
  }
  return st;
}

static ds_status_t yf_new_bucket(ds_yfast_t *t, size_t *out) {
  size_t id;
  if (t->nfree > 0) {
    id = t->free_ids[--t->nfree];
  } else {
    size_t cap = t->bucket_capacity;
    ds_status_t st = yf_reserve(t->context, (void **)&t->buckets, &cap,
                                t->nbuckets + 1, sizeof(ds_yfast_bucket_t));
    if (st == DS_OK) {
      cap = t->bucket_capacity;
      st = yf_reserve(t->context, (void **)&t->free_ids, &cap, t->nbuckets + 1,
                      sizeof(size_t));
    }
    if (st != DS_OK) {
      return st;
    }
    t->bucket_capacity = cap;
    id = t->nbuckets++;
  }
  memset(&t->buckets[id], 0, sizeof(ds_yfast_bucket_t));
  *out = id;
  return DS_OK;
}

static void yf_free_bucket(ds_yfast_t *t, size_t id) {
  ds_context_free(t->context, t->buckets[id].keys);
  ds_context_free(t->context, t->buckets[id].values);
  memset(&t->buckets[id], 0, sizeof(ds_yfast_bucket_t));
  t->free_ids[t->nfree++] = id;
}

ds_status_t ds_yfast_init(ds_yfast_t *t, unsigned bits, size_t value_size,
                          const ds_map_ops_t *ops, ds_context_t *context) {
  ds_status_t st;
  if (t == NULL) {
    return DS_ERR_NULL;
  }
  memset(t, 0, sizeof(*t));
  if (bits == 0 || bits > YF_MAXBITS) {
    return DS_ERR_INVALID;
  }
  st = ds_xfast_init(&t->reps, bits, sizeof(size_t), ops, context);
  if (st != DS_OK) {
    return st;
  }
  t->bits = bits;
  t->value_size = value_size;
  t->target = bits < 4 ? 4 : bits;
  t->context = context;
  return DS_OK;
}

void ds_yfast_destroy(ds_yfast_t *t) {
  size_t i;
  if (t == NULL) {
    return;
  }
  for (i = 0; i < t->nbuckets; i++) {
    ds_context_free(t->context, t->buckets[i].keys);
    ds_context_free(t->context, t->buckets[i].values);
  }
  ds_context_free(t->context, t->buckets);
  ds_context_free(t->context, t->free_ids);
  ds_xfast_destroy(&t->reps);
  memset(t, 0, sizeof(*t));
}

size_t ds_yfast_size(const ds_yfast_t *t) { return t == NULL ? 0 : t->count; }

size_t ds_yfast_buckets(const ds_yfast_t *t) {
  return t == NULL ? 0 : ds_xfast_size(&t->reps);
}

static bool yf_in_range(const ds_yfast_t *t, unsigned long x) {
  return t->bits >= YF_MAXBITS || (x >> t->bits) == 0;
}

/* bucket covering x and its representative (count of reps > 0) */
static size_t yf_bucket_of(const ds_yfast_t *t, unsigned long x,
                           unsigned long *rep) {
  unsigned long r = 0;
  (void)ds_xfast_floor(&t->reps, x, &r); /* rep 0 always exists */
  *rep = r;
  return *(size_t *)ds_xfast_find(&t->reps, r);
}

/* first index with keys[i] >= x */
static size_t yf_lower(const ds_yfast_bucket_t *b, unsigned long x) {
  size_t lo = 0;
  size_t hi = b->count;
  while (lo < hi) {
    size_t mid = lo + (hi - lo) / 2;
    if (b->keys[mid] < x) {
      lo = mid + 1;
    } else {
      hi = mid;
    }
  }
  return lo;
}

/* move keys [from, count) of bucket id into a new bucket with rep
 * keys[from] */
static ds_status_t yf_split(ds_yfast_t *t, size_t id) {
  size_t nid;
  size_t from;
  size_t moved;
  ds_yfast_bucket_t *b;
  ds_yfast_bucket_t *nb;
  ds_status_t st = yf_new_bucket(t, &nid);
  if (st != DS_OK) {
    return st;
  }
  b = &t->buckets[id];
  nb = &t->buckets[nid];
  from = b->count / 2;
  moved = b->count - from;
  st = yf_bucket_reserve(t, nb, moved);
  if (st == DS_OK) {
    st = ds_xfast_insert(&t->reps, b->keys[from], &nid);
  }
  if (st != DS_OK) {
    yf_free_bucket(t, nid);
    return st;
  }
  memcpy(nb->keys, b->keys + from, moved * sizeof(unsigned long));
  if (t->value_size > 0) {
    memcpy(nb->values, b->values + from * t->value_size, moved * t->value_size);
  }
  nb->count = moved;
  b->count = from;
  return DS_OK;
}

ds_status_t ds_yfast_insert(ds_yfast_t *t, unsigned long key,
                            const void *value) {
  unsigned long rep;
  size_t id;
  size_t pos;
  ds_yfast_bucket_t *b;
  ds_status_t st;
  if (t == NULL || (t->value_size > 0 && value == NULL)) {
    return DS_ERR_NULL;
  }
  if (!yf_in_range(t, key)) {
    return DS_ERR_RANGE;
  }
  if (ds_xfast_size(&t->reps) == 0) {
    unsigned long zero = 0;
    st = yf_new_bucket(t, &id);
    if (st == DS_OK) {
      st = ds_xfast_insert(&t->reps, zero, &id);
      if (st != DS_OK) {
        yf_free_bucket(t, id);
      }
    }
    if (st != DS_OK) {
      return st;
    }
  }
  id = yf_bucket_of(t, key, &rep);
  b = &t->buckets[id];
  pos = yf_lower(b, key);
  if (pos < b->count && b->keys[pos] == key) {
    if (t->value_size > 0) {
      memmove(b->values + pos * t->value_size, value, t->value_size);
    }
    return DS_EXISTS;
  }
  st = yf_bucket_reserve(t, b, b->count + 1);
  if (st != DS_OK) {
    return st;
  }
  memmove(b->keys + pos + 1, b->keys + pos,
          (b->count - pos) * sizeof(unsigned long));
  b->keys[pos] = key;
  if (t->value_size > 0) {
    memmove(b->values + (pos + 1) * t->value_size,
            b->values + pos * t->value_size, (b->count - pos) * t->value_size);
    memcpy(b->values + pos * t->value_size, value, t->value_size);
  }
  b->count++;
  t->count++;
  if (b->count > 2 * t->target) {
    (void)yf_split(t, id); /* on failure the bucket just stays larger */
  }
  return DS_OK;
}

ds_status_t ds_yfast_remove(ds_yfast_t *t, unsigned long key, void *out_value) {
  unsigned long rep;
  size_t id;
  size_t pos;
  ds_yfast_bucket_t *b;
  if (t == NULL) {
    return DS_ERR_NULL;
  }
  if (!yf_in_range(t, key) || t->count == 0) {
    return DS_NOT_FOUND;
  }
  id = yf_bucket_of(t, key, &rep);
  b = &t->buckets[id];
  pos = yf_lower(b, key);
  if (pos == b->count || b->keys[pos] != key) {
    return DS_NOT_FOUND;
  }
  if (out_value != NULL && t->value_size > 0) {
    memcpy(out_value, b->values + pos * t->value_size, t->value_size);
  }
  memmove(b->keys + pos, b->keys + pos + 1,
          (b->count - pos - 1) * sizeof(unsigned long));
  if (t->value_size > 0) {
    memmove(b->values + pos * t->value_size,
            b->values + (pos + 1) * t->value_size,
            (b->count - pos - 1) * t->value_size);
  }
  b->count--;
  t->count--;
  if (b->count < t->target / 2 && ds_xfast_size(&t->reps) > 1) {
    /* merge the higher of two neighbouring buckets into the lower one */
    unsigned long other;
    unsigned long lo_rep;
    unsigned long hi_rep;
    size_t lo_id;
    size_t hi_id;
    ds_yfast_bucket_t *lo;
    ds_yfast_bucket_t *hi;
    if (ds_xfast_successor(&t->reps, rep, &other) == DS_OK) {
      lo_rep = rep;
      hi_rep = other;
    } else {
      (void)ds_xfast_predecessor(&t->reps, rep, &other);
      lo_rep = other;
      hi_rep = rep;
    }
    lo_id = *(size_t *)ds_xfast_find(&t->reps, lo_rep);
    hi_id = *(size_t *)ds_xfast_find(&t->reps, hi_rep);
    lo = &t->buckets[lo_id];
    hi = &t->buckets[hi_id];
    if (yf_bucket_reserve(t, lo, lo->count + hi->count) == DS_OK) {
      memcpy(lo->keys + lo->count, hi->keys, hi->count * sizeof(unsigned long));
      if (t->value_size > 0) {
        memcpy(lo->values + lo->count * t->value_size, hi->values,
               hi->count * t->value_size);
      }
      lo->count += hi->count;
      (void)ds_xfast_remove(&t->reps, hi_rep, NULL);
      yf_free_bucket(t, hi_id);
      if (lo->count > 2 * t->target) {
        (void)yf_split(t, lo_id);
      }
    }
  }
  return DS_OK;
}

void *ds_yfast_find(const ds_yfast_t *t, unsigned long key) {
  unsigned long rep;
  size_t id;
  size_t pos;
  const ds_yfast_bucket_t *b;
  if (t == NULL || t->count == 0 || !yf_in_range(t, key)) {
    return NULL;
  }
  id = yf_bucket_of(t, key, &rep);
  b = &t->buckets[id];
  pos = yf_lower(b, key);
  if (pos == b->count || b->keys[pos] != key) {
    return NULL;
  }
  return t->value_size > 0 ? b->values + pos * t->value_size
                           : (void *)(b->keys + pos);
}

ds_status_t ds_yfast_floor(const ds_yfast_t *t, unsigned long x,
                           unsigned long *out) {
  unsigned long rep;
  size_t id;
  if (t == NULL || out == NULL) {
    return DS_ERR_NULL;
  }
  if (t->count == 0) {
    return DS_NOT_FOUND;
  }
  if (!yf_in_range(t, x)) {
    return ds_yfast_max(t, out);
  }
  id = yf_bucket_of(t, x, &rep);
  for (;;) {
    const ds_yfast_bucket_t *b = &t->buckets[id];
    size_t pos = yf_lower(b, x);
    if (pos < b->count && b->keys[pos] == x) {
      *out = x;
      return DS_OK;
    }
    if (pos > 0) {
      *out = b->keys[pos - 1];
      return DS_OK;
    }
    /* nothing <= x here: the previous (non-empty) bucket's maximum */
    if (ds_xfast_predecessor(&t->reps, rep, &rep) != DS_OK) {
      return DS_NOT_FOUND;
    }
    id = *(size_t *)ds_xfast_find(&t->reps, rep);
    x = (unsigned long)-1;
    if (!yf_in_range(t, x)) {
      x = t->bits >= YF_MAXBITS ? x : (1UL << t->bits) - 1;
    }
  }
}

ds_status_t ds_yfast_ceiling(const ds_yfast_t *t, unsigned long x,
                             unsigned long *out) {
  unsigned long rep;
  size_t id;
  if (t == NULL || out == NULL) {
    return DS_ERR_NULL;
  }
  if (t->count == 0 || !yf_in_range(t, x)) {
    return DS_NOT_FOUND;
  }
  id = yf_bucket_of(t, x, &rep);
  for (;;) {
    const ds_yfast_bucket_t *b = &t->buckets[id];
    size_t pos = yf_lower(b, x);
    if (pos < b->count) {
      *out = b->keys[pos];
      return DS_OK;
    }
    if (ds_xfast_successor(&t->reps, rep, &rep) != DS_OK) {
      return DS_NOT_FOUND;
    }
    id = *(size_t *)ds_xfast_find(&t->reps, rep);
    x = 0;
  }
}

ds_status_t ds_yfast_predecessor(const ds_yfast_t *t, unsigned long x,
                                 unsigned long *out) {
  if (t == NULL || out == NULL) {
    return DS_ERR_NULL;
  }
  if (x == 0) {
    return DS_NOT_FOUND;
  }
  return ds_yfast_floor(t, x - 1, out);
}

ds_status_t ds_yfast_successor(const ds_yfast_t *t, unsigned long x,
                               unsigned long *out) {
  if (t == NULL || out == NULL) {
    return DS_ERR_NULL;
  }
  if (x == (unsigned long)-1 || !yf_in_range(t, x + 1)) {
    return DS_NOT_FOUND;
  }
  return ds_yfast_ceiling(t, x + 1, out);
}

ds_status_t ds_yfast_min(const ds_yfast_t *t, unsigned long *out) {
  return ds_yfast_ceiling(t, 0, out);
}

ds_status_t ds_yfast_max(const ds_yfast_t *t, unsigned long *out) {
  unsigned long top;
  if (t == NULL || out == NULL) {
    return DS_ERR_NULL;
  }
  top = t->bits >= YF_MAXBITS ? (unsigned long)-1 : (1UL << t->bits) - 1;
  return ds_yfast_floor(t, top, out);
}
