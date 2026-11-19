#include <dsa/core/checked.h>
#include <dsa/core/stage.h>
#include <dsa/structures/ab_tree.h>
#include <dsa/structures/fusion_tree.h>
#include <dsa/structures/ordered_iface.h>
#include <string.h>

/* ---- sorted arrays ---- */

typedef struct oa {
  unsigned long *keys;
  unsigned char *values;
  size_t count;
  size_t key_capacity;
  size_t value_capacity;
  size_t value_size;
  ds_context_t *context;
} oa_t;

static ds_status_t oa_grow(ds_context_t *ctx, void **p, size_t *cap,
                           size_t need, size_t elem) {
  size_t nc, bytes;
  void *q;
  if (need <= *cap || elem == 0) {
    return DS_OK;
  }
  nc = *cap < 8 ? 8 : *cap;
  while (nc < need) {
    if (nc > (size_t)-1 / 2) {
      return DS_ERR_OVERFLOW;
    }
    nc *= 2;
  }
  if (!ds_size_mul(nc, elem, &bytes)) {
    return DS_ERR_OVERFLOW;
  }
  q = ds_context_realloc(ctx, *p, bytes);
  if (q == NULL) {
    return DS_ERR_ALLOC;
  }
  *p = q;
  *cap = nc;
  return DS_OK;
}

static ds_status_t oa_reserve(oa_t *a, size_t need) {
  ds_status_t st = oa_grow(a->context, (void **)&a->keys, &a->key_capacity,
                           need, sizeof(unsigned long));
  if (st == DS_OK) {
    st = oa_grow(a->context, (void **)&a->values, &a->value_capacity, need,
                 a->value_size);
  }
  return st;
}

static size_t oa_lower(const oa_t *a, unsigned long x) {
  size_t lo = 0, hi = a->count;
  while (lo < hi) {
    size_t mid = lo + (hi - lo) / 2;
    if (a->keys[mid] < x) {
      lo = mid + 1;
    } else {
      hi = mid;
    }
  }
  return lo;
}

static ds_status_t oa_init(void *s, size_t value_size, ds_context_t *ctx) {
  oa_t *a = (oa_t *)s;
  memset(a, 0, sizeof(*a));
  a->value_size = value_size;
  a->context = ctx;
  return DS_OK;
}

static void oa_destroy(void *s) {
  oa_t *a = (oa_t *)s;
  ds_context_free(a->context, a->keys);
  ds_context_free(a->context, a->values);
  a->keys = NULL;
  a->values = NULL;
  a->count = 0;
  a->key_capacity = 0;
  a->value_capacity = 0;
}

static ds_status_t oa_insert(void *s, unsigned long key, const void *value) {
  oa_t *a = (oa_t *)s;
  size_t pos = oa_lower(a, key), vs = a->value_size;
  ds_stage_t staged;
  ds_status_t st;
  if (pos < a->count && a->keys[pos] == key) {
    if (vs != 0) {
      if (value != NULL)
        memmove(a->values + pos * vs, value, vs);
      else
        memset(a->values + pos * vs, 0, vs);
    }
    return DS_EXISTS;
  }
  if (a->count == (size_t)-1)
    return DS_ERR_OVERFLOW;
  st = ds_stage_copy(&staged, a->context, true, NULL, 0, value, vs);
  if (st != DS_OK)
    return st;
  st = oa_reserve(a, a->count + 1);
  if (st == DS_OK) {
    memmove(a->keys + pos + 1, a->keys + pos,
            (a->count - pos) * sizeof(unsigned long));
    a->keys[pos] = key;
    if (vs != 0) {
      memmove(a->values + (pos + 1) * vs, a->values + pos * vs,
              (a->count - pos) * vs);
      if (staged.value != NULL)
        memcpy(a->values + pos * vs, staged.value, vs);
      else
        memset(a->values + pos * vs, 0, vs);
    }
    a->count++;
  }
  ds_stage_release(&staged);
  return st;
}

static ds_status_t oa_remove(void *s, unsigned long key, void *out_value) {
  oa_t *a = (oa_t *)s;
  size_t pos = oa_lower(a, key), vs = a->value_size;
  if (pos == a->count || a->keys[pos] != key) {
    return DS_NOT_FOUND;
  }
  if (out_value != NULL && vs > 0) {
    memcpy(out_value, a->values + pos * vs, vs);
  }
  memmove(a->keys + pos, a->keys + pos + 1,
          (a->count - pos - 1) * sizeof(unsigned long));
  if (vs > 0) {
    memmove(a->values + pos * vs, a->values + (pos + 1) * vs,
            (a->count - pos - 1) * vs);
  }
  a->count--;
  return DS_OK;
}

static void *oa_find(const void *s, unsigned long key) {
  const oa_t *a = (const oa_t *)s;
  size_t pos = oa_lower(a, key);
  if (pos == a->count || a->keys[pos] != key) {
    return NULL;
  }
  return a->value_size > 0 ? (void *)(a->values + pos * a->value_size)
                           : (void *)(a->keys + pos);
}

static ds_status_t oa_floor(const void *s, unsigned long x,
                            unsigned long *out) {
  const oa_t *a = (const oa_t *)s;
  size_t pos = oa_lower(a, x);
  if (pos < a->count && a->keys[pos] == x) {
    *out = x;
    return DS_OK;
  }
  if (pos == 0) {
    return DS_NOT_FOUND;
  }
  *out = a->keys[pos - 1];
  return DS_OK;
}

static ds_status_t oa_ceiling(const void *s, unsigned long x,
                              unsigned long *out) {
  const oa_t *a = (const oa_t *)s;
  size_t pos = oa_lower(a, x);
  if (pos == a->count) {
    return DS_NOT_FOUND;
  }
  *out = a->keys[pos];
  return DS_OK;
}

static size_t oa_size(const void *s) { return ((const oa_t *)s)->count; }

static ds_status_t oa_select(const void *s, size_t k, unsigned long *out) {
  const oa_t *a = (const oa_t *)s;
  if (k >= a->count) {
    return DS_NOT_FOUND;
  }
  *out = a->keys[k];
  return DS_OK;
}

/* arrays copy between instances, so only the value layout must agree */
static bool oa_compatible(const void *s, const void *other) {
  return ((const oa_t *)s)->value_size == ((const oa_t *)other)->value_size;
}

static ds_status_t oa_split(void *s, unsigned long key, void *upper) {
  oa_t *a = (oa_t *)s, *u = (oa_t *)upper;
  size_t pos = oa_lower(a, key), moved = a->count - pos;
  ds_status_t st;
  if (u->count != 0 || !oa_compatible(a, u)) {
    return DS_ERR_INVALID;
  }
  st = oa_reserve(u, moved);
  if (st != DS_OK) {
    return st;
  }
  if (moved > 0) {
    memcpy(u->keys, a->keys + pos, moved * sizeof(unsigned long));
    if (a->value_size > 0) {
      memcpy(u->values, a->values + pos * a->value_size, moved * a->value_size);
    }
  }
  u->count = moved;
  a->count = pos;
  return DS_OK;
}

static ds_status_t oa_join(void *s, void *upper) {
  oa_t *a = (oa_t *)s, *u = (oa_t *)upper;
  ds_status_t st;
  if (!oa_compatible(a, u)) {
    return DS_ERR_INVALID;
  }
  if (u->count > (size_t)-1 - a->count)
    return DS_ERR_OVERFLOW;
  st = oa_reserve(a, a->count + u->count);
  if (st != DS_OK) {
    return st;
  }
  if (u->count > 0) {
    memcpy(a->keys + a->count, u->keys, u->count * sizeof(unsigned long));
    if (a->value_size > 0) {
      memcpy(a->values + a->count * a->value_size, u->values,
             u->count * a->value_size);
    }
  }
  a->count += u->count;
  u->count = 0;
  return DS_OK;
}

static bool oa_next(const void *s, size_t *cursor, unsigned long *key,
                    void **value) {
  const oa_t *a = (const oa_t *)s;
  if (*cursor >= a->count) {
    return false;
  }
  if (key != NULL) {
    *key = a->keys[*cursor];
  }
  if (value != NULL) {
    *value = a->value_size > 0 ? (void *)(a->values + *cursor * a->value_size)
                               : (void *)(a->keys + *cursor);
  }
  (*cursor)++;
  return true;
}

static ds_status_t oa_for_each(const void *s, ds_ordered_visit_fn visit,
                               void *user) {
  size_t cursor = 0;
  unsigned long key;
  void *value;
  while (oa_next(s, &cursor, &key, &value)) {
    ds_status_t st = visit(key, value, user);
    if (st != DS_OK) {
      return st;
    }
  }
  return DS_OK;
}

/* ---- treap ---- */

typedef struct ot_node {
  struct ot_node *left;
  struct ot_node *right;
  struct ot_node *parent;
  unsigned long key;
  unsigned long priority;
  size_t size;
} ot_node_t;

typedef struct ot {
  ot_node_t *root;
  size_t value_size;
  size_t offset;
  size_t node_size;
  unsigned long rng;
  ds_context_t *context;
} ot_t;

static size_t ot_sz(const ot_node_t *n) { return n == NULL ? 0 : n->size; }

static void ot_update(ot_node_t *n) {
  n->size = 1 + ot_sz(n->left) + ot_sz(n->right);
}

static void *ot_value(const ot_t *t, ot_node_t *n) {
  return t->value_size > 0 ? (void *)((unsigned char *)n + t->offset)
                           : (void *)&n->key;
}

/* Build the changed spine top down, then repair metadata bottom up through
 * parent links. These operations never allocate or consume the C call stack. */
static ot_node_t *ot_merge(ot_node_t *a, ot_node_t *b) {
  ot_node_t *root = NULL, *tail = NULL, **slot = &root;
  while (a != NULL && b != NULL) {
    ot_node_t *chosen;
    if (a->priority > b->priority) {
      chosen = a;
      a = a->right;
      *slot = chosen;
      slot = &chosen->right;
    } else {
      chosen = b;
      b = b->left;
      *slot = chosen;
      slot = &chosen->left;
    }
    chosen->parent = tail;
    tail = chosen;
  }
  *slot = a == NULL ? b : a;
  if (*slot != NULL)
    (*slot)->parent = tail;
  for (; tail != NULL; tail = tail->parent)
    ot_update(tail);
  return root;
}

/* l: keys < key, r: keys >= key */
static void ot_split_lt(ot_node_t *n, unsigned long key, ot_node_t **l,
                        ot_node_t **r) {
  ot_node_t *left = NULL, *right = NULL, **ls = l, **rs = r;
  while (n != NULL) {
    ot_node_t *next;
    if (n->key < key) {
      next = n->right;
      *ls = n;
      n->parent = left;
      left = n;
      ls = &n->right;
    } else {
      next = n->left;
      *rs = n;
      n->parent = right;
      right = n;
      rs = &n->left;
    }
    n = next;
  }
  *ls = NULL;
  *rs = NULL;
  for (; left != NULL; left = left->parent)
    ot_update(left);
  for (; right != NULL; right = right->parent)
    ot_update(right);
}

static ot_node_t *ot_find_node(const ot_t *t, unsigned long key) {
  ot_node_t *n = t->root;
  while (n != NULL && n->key != key) {
    n = key < n->key ? n->left : n->right;
  }
  return n;
}

static ds_status_t ot_init(void *s, size_t value_size, ds_context_t *ctx) {
  ot_t *t = (ot_t *)s;
  memset(t, 0, sizeof(*t));
  t->value_size = value_size;
  t->context = ctx;
  t->rng = 0x9E3779B9UL;
  if (!ds_size_align(sizeof(ot_node_t), DS_MAX_ALIGN, &t->offset) ||
      !ds_size_add(t->offset, value_size, &t->node_size)) {
    return DS_ERR_OVERFLOW;
  }
  return DS_OK;
}

static void ot_destroy(void *s) {
  ot_t *t = (ot_t *)s;
  ot_node_t *n = t->root;
  /* rotate left children up until the root has none, then drop it */
  while (n != NULL) {
    if (n->left != NULL) {
      ot_node_t *l = n->left;
      n->left = l->right;
      l->right = n;
      n = l;
    } else {
      ot_node_t *r = n->right;
      ds_context_free(t->context, n);
      n = r;
    }
  }
  t->root = NULL;
}

static ds_status_t ot_insert(void *s, unsigned long key, const void *value) {
  ot_t *t = (ot_t *)s;
  ot_node_t *n = ot_find_node(t, key), *l, *r;
  unsigned long x;
  if (n != NULL) {
    if (t->value_size > 0) {
      if (value != NULL)
        memmove(ot_value(t, n), value, t->value_size);
      else
        memset(ot_value(t, n), 0, t->value_size);
    }
    return DS_EXISTS;
  }
  if (ot_sz(t->root) == (size_t)-1)
    return DS_ERR_OVERFLOW;
  n = (ot_node_t *)ds_context_alloc(t->context, t->node_size);
  if (n == NULL) {
    return DS_ERR_ALLOC;
  }
  x = t->rng;
  x ^= (x << 13) & 0xFFFFFFFFUL;
  x ^= x >> 17;
  x ^= (x << 5) & 0xFFFFFFFFUL;
  t->rng = x & 0xFFFFFFFFUL;
  n->priority = t->rng;
  n->key = key;
  n->left = NULL;
  n->right = NULL;
  n->parent = NULL;
  n->size = 1;
  if (t->value_size > 0) {
    if (value != NULL)
      memcpy(ot_value(t, n), value, t->value_size);
    else
      memset(ot_value(t, n), 0, t->value_size);
  }
  ot_split_lt(t->root, key, &l, &r);
  t->root = ot_merge(ot_merge(l, n), r);
  return DS_OK;
}

static ds_status_t ot_remove(void *s, unsigned long key, void *out_value) {
  ot_t *t = (ot_t *)s;
  ot_node_t *n = ot_find_node(t, key), *l, *r, *m;
  if (n == NULL) {
    return DS_NOT_FOUND;
  }
  if (out_value != NULL && t->value_size > 0) {
    memcpy(out_value, ot_value(t, n), t->value_size);
  }
  ot_split_lt(t->root, key, &l, &r);
  if (key == (unsigned long)-1) {
    m = r; /* the only key >= ULONG_MAX */
    r = NULL;
  } else {
    ot_split_lt(r, key + 1, &m, &r);
  }
  ds_context_free(t->context, m);
  t->root = ot_merge(l, r);
  return DS_OK;
}

static void *ot_find(const void *s, unsigned long key) {
  const ot_t *t = (const ot_t *)s;
  ot_node_t *n = ot_find_node(t, key);
  return n == NULL ? NULL : ot_value(t, n);
}

static ds_status_t ot_floor(const void *s, unsigned long x,
                            unsigned long *out) {
  const ot_node_t *n = ((const ot_t *)s)->root;
  bool found = false;
  while (n != NULL) {
    if (n->key <= x) {
      *out = n->key;
      found = true;
      n = n->right;
    } else {
      n = n->left;
    }
  }
  return found ? DS_OK : DS_NOT_FOUND;
}

static ds_status_t ot_ceiling(const void *s, unsigned long x,
                              unsigned long *out) {
  const ot_node_t *n = ((const ot_t *)s)->root;
  bool found = false;
  while (n != NULL) {
    if (n->key >= x) {
      *out = n->key;
      found = true;
      n = n->left;
    } else {
      n = n->right;
    }
  }
  return found ? DS_OK : DS_NOT_FOUND;
}

static size_t ot_size(const void *s) { return ot_sz(((const ot_t *)s)->root); }

static ot_node_t *ot_select_node(const ot_t *t, size_t k) {
  ot_node_t *n = t->root;
  if (k >= ot_sz(n)) {
    return NULL;
  }
  for (;;) {
    size_t ls = ot_sz(n->left);
    if (k < ls) {
      n = n->left;
    } else if (k == ls) {
      return n;
    } else {
      k -= ls + 1;
      n = n->right;
    }
  }
}

static ds_status_t ot_select(const void *s, size_t k, unsigned long *out) {
  ot_node_t *n = ot_select_node((const ot_t *)s, k);
  if (n == NULL) {
    return DS_NOT_FOUND;
  }
  *out = n->key;
  return DS_OK;
}

/* treaps move nodes between instances: the node layout and the
 * context that will free them must be the same */
static bool ot_compatible(const void *s, const void *other) {
  const ot_t *a = (const ot_t *)s, *b = (const ot_t *)other;
  return a->value_size == b->value_size && a->node_size == b->node_size &&
         ds_context_compatible(a->context, b->context);
}

static ds_status_t ot_split(void *s, unsigned long key, void *upper) {
  ot_t *t = (ot_t *)s, *u = (ot_t *)upper;
  ot_node_t *l, *r;
  if (u->root != NULL || !ot_compatible(t, u)) {
    return DS_ERR_INVALID;
  }
  ot_split_lt(t->root, key, &l, &r);
  t->root = l;
  u->root = r;
  return DS_OK;
}

static ds_status_t ot_join(void *s, void *upper) {
  ot_t *t = (ot_t *)s, *u = (ot_t *)upper;
  if (!ot_compatible(t, u)) {
    return DS_ERR_INVALID;
  }
  t->root = ot_merge(t->root, u->root);
  u->root = NULL;
  return DS_OK;
}

static bool ot_next(const void *s, size_t *cursor, unsigned long *key,
                    void **value) {
  const ot_t *t = (const ot_t *)s;
  ot_node_t *n = ot_select_node(t, *cursor);
  if (n == NULL) {
    return false;
  }
  if (key != NULL) {
    *key = n->key;
  }
  if (value != NULL) {
    *value = ot_value(t, n);
  }
  (*cursor)++;
  return true;
}

/* In-order walk through parent links, O(n), without allocation. */
static ds_status_t ot_for_each(const void *s, ds_ordered_visit_fn visit,
                               void *user) {
  const ot_t *t = (const ot_t *)s;
  ot_node_t *n = t->root;
  if (n != NULL)
    while (n->left != NULL)
      n = n->left;
  while (n != NULL) {
    ds_status_t st = visit(n->key, ot_value(t, n), user);
    if (st != DS_OK)
      return st;
    if (n->right != NULL) {
      n = n->right;
      while (n->left != NULL)
        n = n->left;
    } else {
      while (n->parent != NULL && n == n->parent->right)
        n = n->parent;
      n = n->parent;
    }
  }
  return DS_OK;
}

/* ---- fusion tree: dsa/structures/fusion_tree.h behind the ops table; split
 * and join rebuild in O(n) ---- */

static ds_status_t of_init(void *s, size_t value_size, ds_context_t *ctx) {
  return ds_fusion_init((ds_fusion_tree_t *)s, value_size, ctx);
}

static void of_destroy(void *s) { ds_fusion_destroy((ds_fusion_tree_t *)s); }

static ds_status_t of_insert(void *s, unsigned long key, const void *value) {
  ds_fusion_tree_t *t = (ds_fusion_tree_t *)s;
  bool present = ds_fusion_contains(t, key);
  ds_status_t st = ds_fusion_put(t, key, value);
  return st == DS_OK && present ? DS_EXISTS : st;
}

static ds_status_t of_remove(void *s, unsigned long key, void *out_value) {
  return ds_fusion_remove((ds_fusion_tree_t *)s, key, out_value);
}

static void *of_find(const void *s, unsigned long key) {
  return ds_fusion_get((const ds_fusion_tree_t *)s, key);
}

static ds_status_t of_floor(const void *s, unsigned long x,
                            unsigned long *out) {
  return ds_fusion_floor((const ds_fusion_tree_t *)s, x, out, NULL);
}

static ds_status_t of_ceiling(const void *s, unsigned long x,
                              unsigned long *out) {
  return ds_fusion_ceiling((const ds_fusion_tree_t *)s, x, out, NULL);
}

static size_t of_size(const void *s) {
  return ds_fusion_size((const ds_fusion_tree_t *)s);
}

static ds_status_t of_select(const void *s, size_t k, unsigned long *out) {
  return ds_fusion_select((const ds_fusion_tree_t *)s, k, out, NULL) == DS_OK
             ? DS_OK
             : DS_NOT_FOUND;
}

/* fusion trees are rebuilt, not moved: only the value layout must agree */
static bool of_compatible(const void *s, const void *other) {
  return ((const ds_fusion_tree_t *)s)->value_size ==
         ((const ds_fusion_tree_t *)other)->value_size;
}

typedef struct of_dump {
  unsigned long *keys;
  unsigned char *values;
  size_t value_size;
  size_t n;
} of_dump_t;

static int of_dump_visit(unsigned long key, void *value, void *user) {
  of_dump_t *d = (of_dump_t *)user;
  d->keys[d->n] = key;
  if (d->value_size > 0) {
    memcpy(d->values + d->n * d->value_size, value, d->value_size);
  }
  d->n++;
  return 0;
}

/* every entry of a and then b, in order, into arrays */
static ds_status_t of_dump2(const ds_fusion_tree_t *a,
                            const ds_fusion_tree_t *b, of_dump_t *d) {
  size_t n = ds_fusion_size(a) + (b != NULL ? ds_fusion_size(b) : 0);
  d->value_size = a->value_size;
  d->n = 0;
  d->keys = (unsigned long *)ds_context_alloc(a->context,
                                              (n + 1) * sizeof(unsigned long));
  d->values =
      (unsigned char *)ds_context_alloc(a->context, n * a->value_size + 1);
  if (d->keys == NULL || d->values == NULL) {
    ds_context_free(a->context, d->keys);
    ds_context_free(a->context, d->values);
    return DS_ERR_ALLOC;
  }
  (void)ds_fusion_foreach(a, of_dump_visit, d);
  if (b != NULL) {
    (void)ds_fusion_foreach(b, of_dump_visit, d);
  }
  return DS_OK;
}

static ds_status_t of_split(void *s, unsigned long key, void *upper) {
  ds_fusion_tree_t *a = (ds_fusion_tree_t *)s;
  ds_fusion_tree_t *u = (ds_fusion_tree_t *)upper;
  ds_fusion_tree_t lo;
  ds_fusion_tree_t hi;
  of_dump_t d;
  size_t cut;
  ds_status_t st;
  if (ds_fusion_size(u) != 0 || !of_compatible(a, u)) {
    return DS_ERR_INVALID;
  }
  st = of_dump2(a, NULL, &d);
  if (st != DS_OK) {
    return st;
  }
  cut = ds_fusion_rank(a, key);
  (void)ds_fusion_init(&lo, a->value_size, a->context);
  (void)ds_fusion_init(&hi, u->value_size, u->context);
  st = ds_fusion_build(&lo, d.keys, d.values, cut);
  if (st == DS_OK) {
    st = ds_fusion_build(&hi, d.keys + cut, d.values + cut * d.value_size,
                         d.n - cut);
  }
  if (st == DS_OK) { /* both built: nothing can fail from here */
    ds_fusion_destroy(a);
    ds_fusion_destroy(u);
    *a = lo;
    *u = hi;
  } else {
    ds_fusion_destroy(&lo);
    ds_fusion_destroy(&hi);
  }
  ds_context_free(a->context, d.keys);
  ds_context_free(a->context, d.values);
  return st;
}

static ds_status_t of_join(void *s, void *upper) {
  ds_fusion_tree_t *a = (ds_fusion_tree_t *)s;
  ds_fusion_tree_t *u = (ds_fusion_tree_t *)upper;
  ds_fusion_tree_t all;
  unsigned long amax = 0;
  unsigned long umin = 0;
  of_dump_t d;
  ds_status_t st;
  if (!of_compatible(a, u)) {
    return DS_ERR_INVALID;
  }
  if (ds_fusion_size(u) == 0) {
    return DS_OK;
  }
  if (ds_fusion_max(a, &amax, NULL) == DS_OK &&
      (ds_fusion_min(u, &umin, NULL) != DS_OK || umin <= amax)) {
    return DS_ERR_INVALID; /* upper's keys must all be greater */
  }
  st = of_dump2(a, u, &d);
  if (st != DS_OK) {
    return st;
  }
  (void)ds_fusion_init(&all, a->value_size, a->context);
  st = ds_fusion_build(&all, d.keys, d.values, d.n);
  if (st == DS_OK) {
    ds_fusion_destroy(a);
    ds_fusion_clear(u);
    *a = all;
  }
  ds_context_free(a->context, d.keys);
  ds_context_free(a->context, d.values);
  return st;
}

static bool of_next(const void *s, size_t *cursor, unsigned long *key,
                    void **value) {
  const ds_fusion_tree_t *t = (const ds_fusion_tree_t *)s;
  unsigned long k = 0;
  if (ds_fusion_select(t, *cursor, &k, NULL) != DS_OK) {
    return false;
  }
  if (key != NULL) {
    *key = k;
  }
  if (value != NULL) {
    *value = ds_fusion_get(t, k);
  }
  (*cursor)++;
  return true;
}

typedef struct of_walk {
  ds_ordered_visit_fn visit;
  void *user;
  ds_status_t status;
} of_walk_t;

static int of_walk_visit(unsigned long key, void *value, void *user) {
  of_walk_t *w = (of_walk_t *)user;
  w->status = w->visit(key, value, w->user);
  return w->status != DS_OK;
}

static ds_status_t of_for_each(const void *s, ds_ordered_visit_fn visit,
                               void *user) {
  const ds_fusion_tree_t *t = (const ds_fusion_tree_t *)s;
  of_walk_t w;
  w.visit = visit;
  w.user = user;
  w.status = DS_OK;
  (void)ds_fusion_foreach(t, of_walk_visit, &w);
  return w.status;
}

static const ds_ordered_ops_t ordered_array_ops = {
    "sorted array", DS_ORDERED_CONTIGUOUS,
    sizeof(oa_t),   oa_init,
    oa_destroy,     oa_insert,
    oa_remove,      oa_find,
    oa_floor,       oa_ceiling,
    oa_size,        oa_select,
    oa_split,       oa_join,
    oa_next,        oa_compatible,
    oa_for_each};
static const ds_ordered_ops_t ordered_treap_ops = {
    "treap",      DS_ORDERED_LOG_UPDATE | DS_ORDERED_SPLIT_FAST,
    sizeof(ot_t), ot_init,
    ot_destroy,   ot_insert,
    ot_remove,    ot_find,
    ot_floor,     ot_ceiling,
    ot_size,      ot_select,
    ot_split,     ot_join,
    ot_next,      ot_compatible,
    ot_for_each};

const ds_ordered_ops_t *ds_ordered_array_ops(void) {
  return &ordered_array_ops;
}
const ds_ordered_ops_t *ds_ordered_treap_ops(void) {
  return &ordered_treap_ops;
}
static const ds_ordered_ops_t ordered_fusion_ops = {"fusion tree",
                                                    DS_ORDERED_LOG_UPDATE,
                                                    sizeof(ds_fusion_tree_t),
                                                    of_init,
                                                    of_destroy,
                                                    of_insert,
                                                    of_remove,
                                                    of_find,
                                                    of_floor,
                                                    of_ceiling,
                                                    of_size,
                                                    of_select,
                                                    of_split,
                                                    of_join,
                                                    of_next,
                                                    of_compatible,
                                                    of_for_each};
const ds_ordered_ops_t *ds_ordered_fusion_ops(void) {
  return &ordered_fusion_ops;
}

/* ---- (a,b)-tree: dsa/structures/ab_tree.h with (a, b) = (4, 8), items holding
 * the key and then the value; split and join are its O(log n) ones, which move
 * nodes ---- */

#define OAB_A 4
#define OAB_B 8

typedef struct oab {
  ds_ab_tree_t t;
  size_t value_size;
  unsigned char *item; /* scratch: one item */
} oab_t;

static int oab_compare(const void *a, const void *b, void *user) {
  unsigned long x;
  unsigned long y;
  (void)user;
  memcpy(&x, a, sizeof(x));
  memcpy(&y, b, sizeof(y));
  return (x > y) - (x < y);
}

static ds_status_t oab_init(void *s, size_t value_size, ds_context_t *ctx) {
  oab_t *o = (oab_t *)s;
  ds_status_t st;
  if (value_size > (size_t)-1 - sizeof(unsigned long)) {
    return DS_ERR_OVERFLOW;
  }
  o->value_size = value_size;
  o->item = (unsigned char *)ds_context_alloc(ctx, sizeof(unsigned long) +
                                                       value_size);
  if (o->item == NULL) {
    return DS_ERR_ALLOC;
  }
  st = ds_ab_init(&o->t, OAB_A, OAB_B, sizeof(unsigned long) + value_size,
                  oab_compare, NULL, ctx);
  if (st != DS_OK) {
    ds_context_free(ctx, o->item);
    o->item = NULL;
  }
  return st;
}

static void oab_destroy(void *s) {
  oab_t *o = (oab_t *)s;
  ds_context_t *ctx = o->t.context;
  ds_ab_destroy(&o->t);
  ds_context_free(ctx, o->item);
  o->item = NULL;
}

static ds_status_t oab_insert(void *s, unsigned long key, const void *value) {
  oab_t *o = (oab_t *)s;
  memcpy(o->item, &key, sizeof(key));
  if (o->value_size > 0) {
    if (value != NULL) {
      memcpy(o->item + sizeof(key), value, o->value_size);
    } else {
      memset(o->item + sizeof(key), 0, o->value_size);
    }
  }
  return ds_ab_put(&o->t, o->item);
}

static ds_status_t oab_remove(void *s, unsigned long key, void *out_value) {
  oab_t *o = (oab_t *)s;
  ds_status_t st = ds_ab_remove(&o->t, &key, o->item);
  if (st == DS_OK && out_value != NULL && o->value_size > 0) {
    memcpy(out_value, o->item + sizeof(key), o->value_size);
  }
  return st;
}

/* the value of an item, or its key when there are no values */
static void *oab_value(const oab_t *o, unsigned char *item) {
  if (item == NULL) {
    return NULL;
  }
  return o->value_size > 0 ? item + sizeof(unsigned long) : item;
}

static void *oab_find(const void *s, unsigned long key) {
  const oab_t *o = (const oab_t *)s;
  return oab_value(o, (unsigned char *)ds_ab_find(&o->t, &key));
}

static ds_status_t oab_key(const void *item, unsigned long *out) {
  if (item == NULL) {
    return DS_NOT_FOUND;
  }
  if (out != NULL) {
    memcpy(out, item, sizeof(*out));
  }
  return DS_OK;
}

static ds_status_t oab_floor(const void *s, unsigned long x,
                             unsigned long *out) {
  return oab_key(ds_ab_floor(&((const oab_t *)s)->t, &x), out);
}

static ds_status_t oab_ceiling(const void *s, unsigned long x,
                               unsigned long *out) {
  return oab_key(ds_ab_ceiling(&((const oab_t *)s)->t, &x), out);
}

static size_t oab_size(const void *s) {
  return ds_ab_size(&((const oab_t *)s)->t);
}

static ds_status_t oab_select(const void *s, size_t k, unsigned long *out) {
  return oab_key(ds_ab_select(&((const oab_t *)s)->t, k), out);
}

/* split and join move nodes: the value layout and the memory must agree */
static bool oab_compatible(const void *s, const void *other) {
  const oab_t *a = (const oab_t *)s;
  const oab_t *b = (const oab_t *)other;
  return a->value_size == b->value_size &&
         ds_context_compatible(a->t.context, b->t.context);
}

static ds_status_t oab_split(void *s, unsigned long key, void *upper) {
  oab_t *a = (oab_t *)s;
  oab_t *u = (oab_t *)upper;
  if (ds_ab_size(&u->t) != 0 || !oab_compatible(a, u)) {
    return DS_ERR_INVALID;
  }
  return ds_ab_split(&a->t, &key, &u->t);
}

static ds_status_t oab_join(void *s, void *upper) {
  oab_t *a = (oab_t *)s;
  oab_t *u = (oab_t *)upper;
  if (!oab_compatible(a, u)) {
    return DS_ERR_INVALID;
  }
  return ds_ab_join(&a->t, &u->t);
}

/* by rank: O(log n) per step */
static bool oab_next(const void *s, size_t *cursor, unsigned long *key,
                     void **value) {
  const oab_t *o = (const oab_t *)s;
  unsigned char *item = (unsigned char *)ds_ab_select(&o->t, *cursor);
  if (item == NULL) {
    return false;
  }
  (void)oab_key(item, key);
  if (value != NULL) {
    *value = oab_value(o, item);
  }
  (*cursor)++;
  return true;
}

typedef struct oab_walk {
  const oab_t *o;
  ds_ordered_visit_fn visit;
  void *user;
  ds_status_t status;
} oab_walk_t;

static int oab_walk_visit(void *item, void *user) {
  oab_walk_t *w = (oab_walk_t *)user;
  unsigned long key;
  memcpy(&key, item, sizeof(key));
  w->status = w->visit(key, oab_value(w->o, (unsigned char *)item), w->user);
  return w->status != DS_OK;
}

static ds_status_t oab_for_each(const void *s, ds_ordered_visit_fn visit,
                                void *user) {
  oab_walk_t w;
  w.o = (const oab_t *)s;
  w.visit = visit;
  w.user = user;
  w.status = DS_OK;
  (void)ds_ab_foreach(&w.o->t, oab_walk_visit, &w);
  return w.status;
}

static const ds_ordered_ops_t ordered_ab_ops = {
    "(a,b)-tree",  DS_ORDERED_LOG_UPDATE | DS_ORDERED_SPLIT_LOG,
    sizeof(oab_t), oab_init,
    oab_destroy,   oab_insert,
    oab_remove,    oab_find,
    oab_floor,     oab_ceiling,
    oab_size,      oab_select,
    oab_split,     oab_join,
    oab_next,      oab_compatible,
    oab_for_each};

const ds_ordered_ops_t *ds_ordered_ab_ops(void) { return &ordered_ab_ops; }

/* ---- wrapper ---- */

ds_status_t ds_ordered_init(ds_ordered_t *s, const ds_ordered_ops_t *ops,
                            size_t value_size, ds_context_t *context) {
  ds_status_t st;
  if (s == NULL) {
    return DS_ERR_NULL;
  }
  if (ops == NULL) {
    ops = ds_ordered_treap_ops();
  }
  s->ops = NULL;
  s->impl = ds_context_alloc(context, ops->instance_size);
  if (s->impl == NULL) {
    return DS_ERR_ALLOC;
  }
  st = ops->init(s->impl, value_size, context);
  if (st != DS_OK) {
    ds_context_free(context, s->impl);
    s->impl = NULL;
    return st;
  }
  s->ops = ops;
  s->context = context != NULL ? context : ds_default_context();
  return DS_OK;
}

void ds_ordered_destroy(ds_ordered_t *s) {
  if (s == NULL || s->ops == NULL) {
    return;
  }
  s->ops->destroy(s->impl);
  ds_context_free(s->context, s->impl);
  s->ops = NULL;
  s->impl = NULL;
}

ds_status_t ds_ordered_insert(ds_ordered_t *s, unsigned long key,
                              const void *value) {
  if (s == NULL || s->ops == NULL) {
    return DS_ERR_NULL;
  }
  return s->ops->insert(s->impl, key, value);
}

ds_status_t ds_ordered_remove(ds_ordered_t *s, unsigned long key,
                              void *out_value) {
  if (s == NULL || s->ops == NULL) {
    return DS_ERR_NULL;
  }
  return s->ops->remove(s->impl, key, out_value);
}

void *ds_ordered_find(const ds_ordered_t *s, unsigned long key) {
  if (s == NULL || s->ops == NULL) {
    return NULL;
  }
  return s->ops->find(s->impl, key);
}

ds_status_t ds_ordered_floor(const ds_ordered_t *s, unsigned long x,
                             unsigned long *out) {
  if (s == NULL || s->ops == NULL || out == NULL) {
    return DS_ERR_NULL;
  }
  return s->ops->floor(s->impl, x, out);
}

ds_status_t ds_ordered_ceiling(const ds_ordered_t *s, unsigned long x,
                               unsigned long *out) {
  if (s == NULL || s->ops == NULL || out == NULL) {
    return DS_ERR_NULL;
  }
  return s->ops->ceiling(s->impl, x, out);
}

size_t ds_ordered_size(const ds_ordered_t *s) {
  if (s == NULL || s->ops == NULL) {
    return 0;
  }
  return s->ops->size(s->impl);
}

ds_status_t ds_ordered_select(const ds_ordered_t *s, size_t k,
                              unsigned long *out) {
  if (s == NULL || s->ops == NULL || out == NULL) {
    return DS_ERR_NULL;
  }
  return s->ops->select(s->impl, k, out);
}

bool ds_ordered_compatible(const ds_ordered_t *a, const ds_ordered_t *b) {
  if (a == NULL || b == NULL || a->ops == NULL || a->ops != b->ops) {
    return false;
  }
  return a->ops->compatible(a->impl, b->impl);
}

ds_status_t ds_ordered_split(ds_ordered_t *s, unsigned long key,
                             ds_ordered_t *upper) {
  if (s == NULL || upper == NULL || s->ops == NULL || upper->ops == NULL) {
    return DS_ERR_NULL;
  }
  if (!ds_ordered_compatible(s, upper) || s == upper) {
    return DS_ERR_INVALID;
  }
  return s->ops->split(s->impl, key, upper->impl);
}

ds_status_t ds_ordered_join(ds_ordered_t *s, ds_ordered_t *upper) {
  if (s == NULL || upper == NULL || s->ops == NULL || upper->ops == NULL) {
    return DS_ERR_NULL;
  }
  if (!ds_ordered_compatible(s, upper) || s == upper) {
    return DS_ERR_INVALID;
  }
  return s->ops->join(s->impl, upper->impl);
}

bool ds_ordered_next(const ds_ordered_t *s, size_t *cursor, unsigned long *key,
                     void **value) {
  if (s == NULL || s->ops == NULL || cursor == NULL) {
    return false;
  }
  return s->ops->next(s->impl, cursor, key, value);
}

ds_status_t ds_ordered_for_each(const ds_ordered_t *s,
                                ds_ordered_visit_fn visit, void *user) {
  size_t cursor = 0;
  unsigned long key;
  void *value;
  if (s == NULL || s->ops == NULL || visit == NULL) {
    return DS_ERR_NULL;
  }
  if (s->ops->for_each != NULL) {
    return s->ops->for_each(s->impl, visit, user);
  }
  while (s->ops->next(s->impl, &cursor, &key, &value)) {
    ds_status_t st = visit(key, value, user);
    if (st != DS_OK) {
      return st;
    }
  }
  return DS_OK;
}
