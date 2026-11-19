#include <dsa/structures/xfast_trie.h>
#include <limits.h>
#include <string.h>

#define XF_MAXBITS (sizeof(unsigned long) * CHAR_BIT)

typedef struct xf_node {
  unsigned long min;
  unsigned long max;
} xf_node_t;

typedef struct xf_leaf {
  unsigned long prev;
  unsigned long next;
  unsigned char has_prev;
  unsigned char has_next;
} xf_leaf_t;

static size_t xf_round(size_t n) {
  size_t a = sizeof(unsigned long);
  return (n + a - 1) / a * a;
}

static unsigned long xf_prefix(const ds_xfast_t *t, unsigned long x,
                               unsigned l) {
  if (l == 0) {
    return 0;
  }
  if (l >= t->bits) {
    return x;
  }
  return x >> (t->bits - l);
}

/*
 * Nodes and leaves are copied in and out of the tables, never used in
 * place: a map keeps its values only as aligned as its layout makes them
 * (dsa/structures/map_iface.h: a stride of the value size for most backends),
 * and a leaf's size includes the caller's value, so a leaf can sit at any
 * address. Absent entries read as zero.
 */
static unsigned char *xf_leaf_slot(const ds_xfast_t *t, unsigned long x) {
  return (unsigned char *)ds_map_get(&t->levels[t->bits], &x);
}

static bool xf_leaf_get(const ds_xfast_t *t, unsigned long x, xf_leaf_t *out) {
  const unsigned char *s = xf_leaf_slot(t, x);
  if (s == NULL) {
    memset(out, 0, sizeof(*out));
    return false;
  }
  memcpy(out, s, sizeof(*out));
  return true;
}

static void xf_leaf_set(const ds_xfast_t *t, unsigned long x,
                        const xf_leaf_t *in) {
  unsigned char *s = xf_leaf_slot(t, x);
  if (s != NULL) {
    memcpy(s, in, sizeof(*in));
  }
}

static bool xf_node_get(const ds_xfast_t *t, unsigned l, unsigned long p,
                        xf_node_t *out) {
  const void *s = ds_map_get(&t->levels[l], &p);
  if (s == NULL) {
    out->min = out->max = 0;
    return false;
  }
  memcpy(out, s, sizeof(*out));
  return true;
}

static void xf_node_set(const ds_xfast_t *t, unsigned l, unsigned long p,
                        const xf_node_t *in) {
  void *s = ds_map_get(&t->levels[l], &p);
  if (s != NULL) {
    memcpy(s, in, sizeof(*in));
  }
}

ds_status_t ds_xfast_init(ds_xfast_t *t, unsigned bits, size_t value_size,
                          const ds_map_ops_t *ops, ds_context_t *context) {
  unsigned l;
  ds_status_t st = DS_OK;
  if (t == NULL) {
    return DS_ERR_NULL;
  }
  memset(t, 0, sizeof(*t));
  if (bits == 0 || bits > XF_MAXBITS) {
    return DS_ERR_INVALID;
  }
  if (value_size > (size_t)-1 / 2) {
    return DS_ERR_OVERFLOW;
  }
  t->levels =
      (ds_map_t *)ds_context_calloc(context, bits + 1, sizeof(ds_map_t));
  if (t->levels == NULL) {
    return DS_ERR_ALLOC;
  }
  t->bits = bits;
  t->value_size = value_size;
  t->leaf_size = xf_round(sizeof(xf_leaf_t)) + value_size;
  t->context = context != NULL ? context : ds_default_context();
  for (l = 0; l <= bits && st == DS_OK; l++) {
    st = ds_map_init(&t->levels[l], ops, sizeof(unsigned long),
                     l == bits ? t->leaf_size : sizeof(xf_node_t),
                     ds_map_hash_ulong, ds_map_equal_ulong, NULL, context);
  }
  if (st != DS_OK) {
    ds_xfast_destroy(t);
  }
  return st;
}

void ds_xfast_destroy(ds_xfast_t *t) {
  unsigned l;
  if (t == NULL || t->levels == NULL) {
    return;
  }
  for (l = 0; l <= t->bits; l++) {
    ds_map_destroy(&t->levels[l]);
  }
  ds_context_free(t->context, t->levels);
  memset(t, 0, sizeof(*t));
}

size_t ds_xfast_size(const ds_xfast_t *t) { return t == NULL ? 0 : t->count; }

static bool xf_in_range(const ds_xfast_t *t, unsigned long x) {
  return t->bits >= XF_MAXBITS || (x >> t->bits) == 0;
}

/* longest stored prefix level of x (count > 0) */
static unsigned xf_longest(const ds_xfast_t *t, unsigned long x) {
  unsigned lo = 0;
  unsigned hi = t->bits;
  while (lo < hi) {
    unsigned mid = lo + (hi - lo + 1) / 2;
    unsigned long p = xf_prefix(t, x, mid);
    if (ds_map_get(&t->levels[mid], &p) != NULL) {
      lo = mid;
    } else {
      hi = mid - 1;
    }
  }
  return lo;
}

/* neighbours of an absent x: *pred < x < *succ */
static void xf_neighbours(const ds_xfast_t *t, unsigned long x, bool *hp,
                          unsigned long *pred, bool *hs, unsigned long *succ) {
  unsigned l;
  xf_node_t n;
  xf_leaf_t lf;
  *hp = *hs = false;
  if (t->count == 0) {
    return;
  }
  l = xf_longest(t, x);
  (void)xf_node_get(t, l, xf_prefix(t, x, l), &n);
  if (((x >> (t->bits - l - 1)) & 1UL) != 0) {
    /* the 1-child is missing: everything below is smaller */
    (void)xf_leaf_get(t, n.max, &lf);
    *hp = true;
    *pred = n.max;
    *hs = lf.has_next != 0;
    *succ = lf.next;
  } else {
    (void)xf_leaf_get(t, n.min, &lf);
    *hs = true;
    *succ = n.min;
    *hp = lf.has_prev != 0;
    *pred = lf.prev;
  }
}

void *ds_xfast_find(const ds_xfast_t *t, unsigned long key) {
  unsigned char *s;
  if (t == NULL || !xf_in_range(t, key)) {
    return NULL;
  }
  s = xf_leaf_slot(t, key);
  return s == NULL ? NULL : s + xf_round(sizeof(xf_leaf_t));
}

ds_status_t ds_xfast_insert(ds_xfast_t *t, unsigned long key,
                            const void *value) {
  unsigned char *buf;
  unsigned char *s;
  bool hp;
  bool hs;
  unsigned long pred = 0;
  unsigned long succ = 0;
  xf_node_t old[XF_MAXBITS];
  unsigned char created[XF_MAXBITS];
  unsigned l;
  ds_status_t st = DS_OK;
  if (t == NULL || (t->value_size > 0 && value == NULL)) {
    return DS_ERR_NULL;
  }
  if (!xf_in_range(t, key)) {
    return DS_ERR_RANGE;
  }
  s = xf_leaf_slot(t, key);
  if (s != NULL) {
    if (t->value_size > 0) {
      memmove(s + xf_round(sizeof(xf_leaf_t)), value, t->value_size);
    }
    return DS_EXISTS;
  }
  xf_neighbours(t, key, &hp, &pred, &hs, &succ);
  /* prefixes first, remembering what to undo */
  for (l = 0; l < t->bits && st == DS_OK; l++) {
    unsigned long p = xf_prefix(t, key, l);
    xf_node_t n;
    if (!xf_node_get(t, l, p, &n)) {
      n.min = n.max = key;
      created[l] = 1;
      st = ds_map_put(&t->levels[l], &p, &n);
    } else {
      created[l] = 0;
      old[l] = n;
      if (key < n.min) {
        n.min = key;
      }
      if (key > n.max) {
        n.max = key;
      }
      xf_node_set(t, l, p, &n);
    }
  }
  if (st == DS_OK) {
    buf = (unsigned char *)ds_context_alloc(t->context, t->leaf_size);
    if (buf == NULL) {
      st = DS_ERR_ALLOC;
    } else {
      xf_leaf_t nl;
      memset(&nl, 0, sizeof(nl)); /* the padding is stored too */
      nl.has_prev = hp ? 1 : 0;
      nl.has_next = hs ? 1 : 0;
      nl.prev = pred;
      nl.next = succ;
      memcpy(buf, &nl, sizeof(nl));
      if (t->value_size > 0) {
        memcpy(buf + xf_round(sizeof(xf_leaf_t)), value, t->value_size);
      }
      st = ds_map_put(&t->levels[t->bits], &key, buf);
      ds_context_free(t->context, buf);
    }
  }
  if (st != DS_OK) {
    unsigned done = l;
    if (done > t->bits) {
      done = t->bits;
    }
    /* undo levels 0 .. done-1 (a failed put changed nothing) */
    while (done-- > 0) {
      unsigned long p = xf_prefix(t, key, done);
      if (created[done]) {
        (void)ds_map_remove(&t->levels[done], &p, NULL);
      } else {
        xf_node_set(t, done, p, &old[done]);
      }
    }
    return st;
  }
  if (hp) {
    xf_leaf_t pl;
    (void)xf_leaf_get(t, pred, &pl);
    pl.has_next = 1;
    pl.next = key;
    xf_leaf_set(t, pred, &pl);
  }
  if (hs) {
    xf_leaf_t sl;
    (void)xf_leaf_get(t, succ, &sl);
    sl.has_prev = 1;
    sl.prev = key;
    xf_leaf_set(t, succ, &sl);
  }
  t->count++;
  return DS_OK;
}

ds_status_t ds_xfast_remove(ds_xfast_t *t, unsigned long key, void *out_value) {
  const unsigned char *s;
  xf_leaf_t links;
  unsigned l;
  if (t == NULL) {
    return DS_ERR_NULL;
  }
  if (!xf_in_range(t, key)) {
    return DS_NOT_FOUND;
  }
  s = xf_leaf_slot(t, key);
  if (s == NULL) {
    return DS_NOT_FOUND;
  }
  memcpy(&links, s, sizeof(links));
  if (out_value != NULL && t->value_size > 0) {
    memcpy(out_value, s + xf_round(sizeof(xf_leaf_t)), t->value_size);
  }
  if (links.has_prev) {
    xf_leaf_t pl;
    (void)xf_leaf_get(t, links.prev, &pl);
    pl.has_next = links.has_next;
    pl.next = links.next;
    xf_leaf_set(t, links.prev, &pl);
  }
  if (links.has_next) {
    xf_leaf_t sl;
    (void)xf_leaf_get(t, links.next, &sl);
    sl.has_prev = links.has_prev;
    sl.prev = links.prev;
    xf_leaf_set(t, links.next, &sl);
  }
  (void)ds_map_remove(&t->levels[t->bits], &key, NULL);
  /* recompute the prefixes bottom-up from their children */
  for (l = t->bits; l-- > 0;) {
    unsigned long p = xf_prefix(t, key, l);
    unsigned long c0 = p << 1;
    unsigned long c1 = (p << 1) | 1UL;
    bool h0;
    bool h1;
    unsigned long mn0 = 0;
    unsigned long mx0 = 0;
    unsigned long mn1 = 0;
    unsigned long mx1 = 0;
    if (l + 1 == t->bits) {
      h0 = xf_leaf_slot(t, c0) != NULL;
      h1 = xf_leaf_slot(t, c1) != NULL;
      mn0 = mx0 = c0;
      mn1 = mx1 = c1;
    } else {
      xf_node_t a;
      xf_node_t b;
      h0 = xf_node_get(t, l + 1, c0, &a);
      h1 = xf_node_get(t, l + 1, c1, &b);
      mn0 = a.min;
      mx0 = a.max;
      mn1 = b.min;
      mx1 = b.max;
    }
    if (!h0 && !h1) {
      (void)ds_map_remove(&t->levels[l], &p, NULL);
    } else {
      xf_node_t n;
      n.min = h0 ? mn0 : mn1;
      n.max = h1 ? mx1 : mx0;
      xf_node_set(t, l, p, &n);
    }
  }
  t->count--;
  return DS_OK;
}

ds_status_t ds_xfast_floor(const ds_xfast_t *t, unsigned long x,
                           unsigned long *out) {
  bool hp;
  bool hs;
  unsigned long p = 0;
  unsigned long s = 0;
  if (t == NULL || out == NULL) {
    return DS_ERR_NULL;
  }
  if (!xf_in_range(t, x)) {
    return ds_xfast_max(t, out);
  }
  if (xf_leaf_slot(t, x) != NULL) {
    *out = x;
    return DS_OK;
  }
  xf_neighbours(t, x, &hp, &p, &hs, &s);
  if (!hp) {
    return DS_NOT_FOUND;
  }
  *out = p;
  return DS_OK;
}

ds_status_t ds_xfast_ceiling(const ds_xfast_t *t, unsigned long x,
                             unsigned long *out) {
  bool hp;
  bool hs;
  unsigned long p = 0;
  unsigned long s = 0;
  if (t == NULL || out == NULL) {
    return DS_ERR_NULL;
  }
  if (!xf_in_range(t, x)) {
    return DS_NOT_FOUND;
  }
  if (xf_leaf_slot(t, x) != NULL) {
    *out = x;
    return DS_OK;
  }
  xf_neighbours(t, x, &hp, &p, &hs, &s);
  if (!hs) {
    return DS_NOT_FOUND;
  }
  *out = s;
  return DS_OK;
}

ds_status_t ds_xfast_predecessor(const ds_xfast_t *t, unsigned long x,
                                 unsigned long *out) {
  xf_leaf_t lf;
  if (t == NULL || out == NULL) {
    return DS_ERR_NULL;
  }
  if (xf_in_range(t, x) && xf_leaf_get(t, x, &lf)) {
    if (!lf.has_prev) {
      return DS_NOT_FOUND;
    }
    *out = lf.prev;
    return DS_OK;
  }
  return ds_xfast_floor(t, x, out);
}

ds_status_t ds_xfast_successor(const ds_xfast_t *t, unsigned long x,
                               unsigned long *out) {
  xf_leaf_t lf;
  if (t == NULL || out == NULL) {
    return DS_ERR_NULL;
  }
  if (xf_in_range(t, x) && xf_leaf_get(t, x, &lf)) {
    if (!lf.has_next) {
      return DS_NOT_FOUND;
    }
    *out = lf.next;
    return DS_OK;
  }
  return ds_xfast_ceiling(t, x, out);
}

ds_status_t ds_xfast_min(const ds_xfast_t *t, unsigned long *out) {
  xf_node_t n;
  if (t == NULL || out == NULL) {
    return DS_ERR_NULL;
  }
  if (t->count == 0) {
    return DS_NOT_FOUND;
  }
  (void)xf_node_get(t, 0, 0, &n);
  *out = n.min;
  return DS_OK;
}

ds_status_t ds_xfast_max(const ds_xfast_t *t, unsigned long *out) {
  xf_node_t n;
  if (t == NULL || out == NULL) {
    return DS_ERR_NULL;
  }
  if (t->count == 0) {
    return DS_NOT_FOUND;
  }
  (void)xf_node_get(t, 0, 0, &n);
  *out = n.max;
  return DS_OK;
}
