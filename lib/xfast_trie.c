#include "xfast_trie.h"
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

static xf_leaf_t *xf_leaf(const ds_xfast_t *t, unsigned long x) {
  return (xf_leaf_t *)ds_map_get(&t->levels[t->bits], &x);
}

static xf_node_t *xf_node(const ds_xfast_t *t, unsigned l, unsigned long p) {
  return (xf_node_t *)ds_map_get(&t->levels[l], &p);
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
  t->context = context;
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
  xf_node_t *n;
  *hp = *hs = false;
  if (t->count == 0) {
    return;
  }
  l = xf_longest(t, x);
  n = xf_node(t, l, xf_prefix(t, x, l));
  if (((x >> (t->bits - l - 1)) & 1UL) != 0) {
    /* the 1-child is missing: everything below is smaller */
    xf_leaf_t *lf = xf_leaf(t, n->max);
    *hp = true;
    *pred = n->max;
    *hs = lf->has_next != 0;
    *succ = lf->next;
  } else {
    xf_leaf_t *lf = xf_leaf(t, n->min);
    *hs = true;
    *succ = n->min;
    *hp = lf->has_prev != 0;
    *pred = lf->prev;
  }
}

void *ds_xfast_find(const ds_xfast_t *t, unsigned long key) {
  xf_leaf_t *lf;
  if (t == NULL || !xf_in_range(t, key)) {
    return NULL;
  }
  lf = xf_leaf(t, key);
  return lf == NULL ? NULL : (unsigned char *)lf + xf_round(sizeof(xf_leaf_t));
}

ds_status_t ds_xfast_insert(ds_xfast_t *t, unsigned long key,
                            const void *value) {
  unsigned char *buf;
  xf_leaf_t *lf;
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
  lf = xf_leaf(t, key);
  if (lf != NULL) {
    if (t->value_size > 0) {
      memmove((unsigned char *)lf + xf_round(sizeof(xf_leaf_t)), value,
              t->value_size);
    }
    return DS_EXISTS;
  }
  xf_neighbours(t, key, &hp, &pred, &hs, &succ);
  /* prefixes first, remembering what to undo */
  for (l = 0; l < t->bits && st == DS_OK; l++) {
    unsigned long p = xf_prefix(t, key, l);
    xf_node_t *n = xf_node(t, l, p);
    if (n == NULL) {
      xf_node_t nn;
      nn.min = nn.max = key;
      created[l] = 1;
      st = ds_map_put(&t->levels[l], &p, &nn);
    } else {
      created[l] = 0;
      old[l] = *n;
      if (key < n->min) {
        n->min = key;
      }
      if (key > n->max) {
        n->max = key;
      }
    }
  }
  if (st == DS_OK) {
    buf = (unsigned char *)ds_context_alloc(t->context, t->leaf_size);
    if (buf == NULL) {
      st = DS_ERR_ALLOC;
    } else {
      xf_leaf_t *nl = (xf_leaf_t *)(void *)buf;
      nl->has_prev = hp ? 1 : 0;
      nl->has_next = hs ? 1 : 0;
      nl->prev = pred;
      nl->next = succ;
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
        xf_node_t *n = xf_node(t, done, p);
        if (n != NULL) {
          *n = old[done];
        }
      }
    }
    return st;
  }
  if (hp) {
    xf_leaf_t *pl = xf_leaf(t, pred);
    pl->has_next = 1;
    pl->next = key;
  }
  if (hs) {
    xf_leaf_t *sl = xf_leaf(t, succ);
    sl->has_prev = 1;
    sl->prev = key;
  }
  t->count++;
  return DS_OK;
}

ds_status_t ds_xfast_remove(ds_xfast_t *t, unsigned long key, void *out_value) {
  xf_leaf_t *lf;
  xf_leaf_t links;
  unsigned l;
  if (t == NULL) {
    return DS_ERR_NULL;
  }
  if (!xf_in_range(t, key)) {
    return DS_NOT_FOUND;
  }
  lf = xf_leaf(t, key);
  if (lf == NULL) {
    return DS_NOT_FOUND;
  }
  links = *lf;
  if (out_value != NULL && t->value_size > 0) {
    memcpy(out_value, (unsigned char *)lf + xf_round(sizeof(xf_leaf_t)),
           t->value_size);
  }
  if (links.has_prev) {
    xf_leaf_t *pl = xf_leaf(t, links.prev);
    pl->has_next = links.has_next;
    pl->next = links.next;
  }
  if (links.has_next) {
    xf_leaf_t *sl = xf_leaf(t, links.next);
    sl->has_prev = links.has_prev;
    sl->prev = links.prev;
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
    xf_node_t *n = xf_node(t, l, p);
    if (l + 1 == t->bits) {
      h0 = xf_leaf(t, c0) != NULL;
      h1 = xf_leaf(t, c1) != NULL;
      mn0 = mx0 = c0;
      mn1 = mx1 = c1;
    } else {
      xf_node_t *a = xf_node(t, l + 1, c0);
      xf_node_t *b = xf_node(t, l + 1, c1);
      h0 = a != NULL;
      h1 = b != NULL;
      if (h0) {
        mn0 = a->min;
        mx0 = a->max;
      }
      if (h1) {
        mn1 = b->min;
        mx1 = b->max;
      }
    }
    if (!h0 && !h1) {
      (void)ds_map_remove(&t->levels[l], &p, NULL);
    } else {
      n->min = h0 ? mn0 : mn1;
      n->max = h1 ? mx1 : mx0;
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
  if (xf_leaf(t, x) != NULL) {
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
  if (xf_leaf(t, x) != NULL) {
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
  xf_leaf_t *lf;
  if (t == NULL || out == NULL) {
    return DS_ERR_NULL;
  }
  if (xf_in_range(t, x) && (lf = xf_leaf(t, x)) != NULL) {
    if (!lf->has_prev) {
      return DS_NOT_FOUND;
    }
    *out = lf->prev;
    return DS_OK;
  }
  return ds_xfast_floor(t, x, out);
}

ds_status_t ds_xfast_successor(const ds_xfast_t *t, unsigned long x,
                               unsigned long *out) {
  xf_leaf_t *lf;
  if (t == NULL || out == NULL) {
    return DS_ERR_NULL;
  }
  if (xf_in_range(t, x) && (lf = xf_leaf(t, x)) != NULL) {
    if (!lf->has_next) {
      return DS_NOT_FOUND;
    }
    *out = lf->next;
    return DS_OK;
  }
  return ds_xfast_ceiling(t, x, out);
}

ds_status_t ds_xfast_min(const ds_xfast_t *t, unsigned long *out) {
  xf_node_t *n;
  unsigned long root = 0;
  if (t == NULL || out == NULL) {
    return DS_ERR_NULL;
  }
  if (t->count == 0) {
    return DS_NOT_FOUND;
  }
  n = xf_node(t, 0, root);
  *out = n->min;
  return DS_OK;
}

ds_status_t ds_xfast_max(const ds_xfast_t *t, unsigned long *out) {
  xf_node_t *n;
  unsigned long root = 0;
  if (t == NULL || out == NULL) {
    return DS_ERR_NULL;
  }
  if (t->count == 0) {
    return DS_NOT_FOUND;
  }
  n = xf_node(t, 0, root);
  *out = n->max;
  return DS_OK;
}
