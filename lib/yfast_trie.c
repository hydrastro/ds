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

static ds_status_t yf_new_bucket(ds_yfast_t *t, size_t *out) {
  size_t id;
  void *b;
  ds_status_t st;
  b = ds_context_alloc(t->context, t->bucket_ops->instance_size);
  if (b == NULL) {
    return DS_ERR_ALLOC;
  }
  st = t->bucket_ops->init(b, t->value_size, t->context);
  if (st != DS_OK) {
    ds_context_free(t->context, b);
    return st;
  }
  if (t->nfree > 0) {
    id = t->free_ids[--t->nfree];
  } else {
    /* each array gets its own copy of the capacity: a failure between
     * the two reallocations must not make either look larger */
    size_t cap = t->bucket_capacity, fcap = t->bucket_capacity;
    st = yf_reserve(t->context, (void **)&t->buckets, &cap, t->nbuckets + 1,
                    sizeof(void *));
    if (st == DS_OK) {
      st = yf_reserve(t->context, (void **)&t->free_ids, &fcap, t->nbuckets + 1,
                      sizeof(size_t));
    }
    if (st != DS_OK) {
      t->bucket_ops->destroy(b);
      ds_context_free(t->context, b);
      return st;
    }
    t->bucket_capacity = cap < fcap ? cap : fcap;
    id = t->nbuckets++;
  }
  t->buckets[id] = b;
  *out = id;
  return DS_OK;
}

static void yf_free_bucket(ds_yfast_t *t, size_t id) {
  t->bucket_ops->destroy(t->buckets[id]);
  ds_context_free(t->context, t->buckets[id]);
  t->buckets[id] = NULL;
  t->free_ids[t->nfree++] = id;
}

ds_status_t ds_yfast_init_buckets(ds_yfast_t *t, unsigned bits,
                                  size_t value_size, const ds_map_ops_t *ops,
                                  const ds_ordered_ops_t *bucket_ops,
                                  ds_context_t *context) {
  ds_status_t st;
  if (t == NULL) {
    return DS_ERR_NULL;
  }
  memset(t, 0, sizeof(*t));
  if (bits == 0 || bits > YF_MAXBITS ||
      (bucket_ops != NULL &&
       (bucket_ops->split == NULL || bucket_ops->join == NULL))) {
    return DS_ERR_INVALID; /* rebalancing needs split and join */
  }
  st = ds_xfast_init(&t->reps, bits, sizeof(size_t), ops, context);
  if (st != DS_OK) {
    return st;
  }
  t->bits = bits;
  t->value_size = value_size;
  t->target = bits < 4 ? 4 : bits;
  t->context = context;
  t->bucket_ops = bucket_ops != NULL ? bucket_ops : ds_ordered_array_ops();
  return DS_OK;
}

ds_status_t ds_yfast_init(ds_yfast_t *t, unsigned bits, size_t value_size,
                          const ds_map_ops_t *ops, ds_context_t *context) {
  return ds_yfast_init_buckets(t, bits, value_size, ops, NULL, context);
}

void ds_yfast_destroy(ds_yfast_t *t) {
  size_t i;
  if (t == NULL || t->bucket_ops == NULL) {
    return;
  }
  for (i = 0; i < t->nbuckets; i++) {
    if (t->buckets[i] != NULL) {
      t->bucket_ops->destroy(t->buckets[i]);
      ds_context_free(t->context, t->buckets[i]);
    }
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

static unsigned long yf_top(const ds_yfast_t *t) {
  return t->bits >= YF_MAXBITS ? (unsigned long)-1 : (1UL << t->bits) - 1;
}

/* bucket covering x, its representative and id (count of reps > 0) */
static void *yf_bucket_of(const ds_yfast_t *t, unsigned long x,
                          unsigned long *rep, size_t *id) {
  unsigned long r = 0;
  (void)ds_xfast_floor(&t->reps, x, &r); /* rep 0 always exists */
  *rep = r;
  *id = *(size_t *)ds_xfast_find(&t->reps, r);
  return t->buckets[*id];
}

/* move the upper half of bucket id into a new bucket. The new
 * representative goes into the X-fast trie first, so a failing split is
 * undone by removing it again. */
static ds_status_t yf_split(ds_yfast_t *t, size_t id) {
  const ds_ordered_ops_t *ops = t->bucket_ops;
  size_t nid;
  unsigned long mid;
  ds_status_t st;
  if (ops->select(t->buckets[id], ops->size(t->buckets[id]) / 2, &mid) !=
      DS_OK) {
    return DS_ERR_INTERNAL;
  }
  st = yf_new_bucket(t, &nid);
  if (st != DS_OK) {
    return st;
  }
  st = ds_xfast_insert(&t->reps, mid, &nid);
  if (st != DS_OK) {
    yf_free_bucket(t, nid);
    return st;
  }
  st = ops->split(t->buckets[id], mid, t->buckets[nid]);
  if (st != DS_OK) {
    (void)ds_xfast_remove(&t->reps, mid, NULL);
    yf_free_bucket(t, nid);
  }
  return st;
}

ds_status_t ds_yfast_insert(ds_yfast_t *t, unsigned long key,
                            const void *value) {
  const ds_ordered_ops_t *ops;
  unsigned long rep;
  size_t id;
  void *b;
  ds_status_t st;
  if (t == NULL || (t->value_size > 0 && value == NULL)) {
    return DS_ERR_NULL;
  }
  if (!yf_in_range(t, key)) {
    return DS_ERR_RANGE;
  }
  ops = t->bucket_ops;
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
  b = yf_bucket_of(t, key, &rep, &id);
  st = ops->insert(b, key, value);
  if (st != DS_OK) {
    return st;
  }
  t->count++;
  if (ops->size(b) > 2 * t->target) {
    (void)yf_split(t, id); /* on failure the bucket just stays larger */
  }
  return DS_OK;
}

ds_status_t ds_yfast_remove(ds_yfast_t *t, unsigned long key, void *out_value) {
  const ds_ordered_ops_t *ops;
  unsigned long rep;
  size_t id;
  void *b;
  ds_status_t st;
  if (t == NULL) {
    return DS_ERR_NULL;
  }
  if (!yf_in_range(t, key) || t->count == 0) {
    return DS_NOT_FOUND;
  }
  ops = t->bucket_ops;
  b = yf_bucket_of(t, key, &rep, &id);
  st = ops->remove(b, key, t->value_size > 0 ? out_value : NULL);
  if (st != DS_OK) {
    return st;
  }
  t->count--;
  if (ops->size(b) < t->target / 2 && ds_xfast_size(&t->reps) > 1) {
    /* merge the higher of two neighbouring buckets into the lower one */
    unsigned long other, lo_rep, hi_rep;
    size_t lo_id, hi_id;
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
    if (ops->join(t->buckets[lo_id], t->buckets[hi_id]) == DS_OK) {
      (void)ds_xfast_remove(&t->reps, hi_rep, NULL);
      yf_free_bucket(t, hi_id);
      if (ops->size(t->buckets[lo_id]) > 2 * t->target) {
        (void)yf_split(t, lo_id);
      }
    }
  }
  return DS_OK;
}

void *ds_yfast_find(const ds_yfast_t *t, unsigned long key) {
  unsigned long rep;
  size_t id;
  if (t == NULL || t->count == 0 || !yf_in_range(t, key)) {
    return NULL;
  }
  return t->bucket_ops->find(yf_bucket_of(t, key, &rep, &id), key);
}

ds_status_t ds_yfast_floor(const ds_yfast_t *t, unsigned long x,
                           unsigned long *out) {
  unsigned long rep;
  size_t id;
  void *b;
  if (t == NULL || out == NULL) {
    return DS_ERR_NULL;
  }
  if (t->count == 0) {
    return DS_NOT_FOUND;
  }
  if (!yf_in_range(t, x)) {
    x = yf_top(t);
  }
  b = yf_bucket_of(t, x, &rep, &id);
  for (;;) {
    if (t->bucket_ops->floor(b, x, out) == DS_OK) {
      return DS_OK;
    }
    /* nothing <= x here: the previous bucket's maximum */
    if (ds_xfast_predecessor(&t->reps, rep, &rep) != DS_OK) {
      return DS_NOT_FOUND;
    }
    b = t->buckets[*(size_t *)ds_xfast_find(&t->reps, rep)];
    x = yf_top(t);
  }
}

ds_status_t ds_yfast_ceiling(const ds_yfast_t *t, unsigned long x,
                             unsigned long *out) {
  unsigned long rep;
  size_t id;
  void *b;
  if (t == NULL || out == NULL) {
    return DS_ERR_NULL;
  }
  if (t->count == 0 || !yf_in_range(t, x)) {
    return DS_NOT_FOUND;
  }
  b = yf_bucket_of(t, x, &rep, &id);
  for (;;) {
    if (t->bucket_ops->ceiling(b, x, out) == DS_OK) {
      return DS_OK;
    }
    if (ds_xfast_successor(&t->reps, rep, &rep) != DS_OK) {
      return DS_NOT_FOUND;
    }
    b = t->buckets[*(size_t *)ds_xfast_find(&t->reps, rep)];
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
  if (t == NULL || out == NULL) {
    return DS_ERR_NULL;
  }
  return ds_yfast_floor(t, yf_top(t), out);
}
