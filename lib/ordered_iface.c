#include "ordered_iface.h"
#include "checked.h"
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
  ds_status_t st;
  if (pos < a->count && a->keys[pos] == key) {
    if (vs > 0) {
      memmove(a->values + pos * vs, value, vs);
    }
    return DS_EXISTS;
  }
  st = oa_reserve(a, a->count + 1);
  if (st != DS_OK) {
    return st;
  }
  memmove(a->keys + pos + 1, a->keys + pos,
          (a->count - pos) * sizeof(unsigned long));
  a->keys[pos] = key;
  if (vs > 0) {
    memmove(a->values + (pos + 1) * vs, a->values + pos * vs,
            (a->count - pos) * vs);
    memcpy(a->values + pos * vs, value, vs);
  }
  a->count++;
  return DS_OK;
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

/* recursion follows one root-to-leaf path: expected O(log n) deep */
static ot_node_t *ot_merge(ot_node_t *a, ot_node_t *b) {
  if (a == NULL) {
    return b;
  }
  if (b == NULL) {
    return a;
  }
  if (a->priority > b->priority) {
    a->right = ot_merge(a->right, b);
    ot_update(a);
    return a;
  }
  b->left = ot_merge(a, b->left);
  ot_update(b);
  return b;
}

/* l: keys < key, r: keys >= key */
static void ot_split_lt(ot_node_t *n, unsigned long key, ot_node_t **l,
                        ot_node_t **r) {
  if (n == NULL) {
    *l = NULL;
    *r = NULL;
    return;
  }
  if (n->key < key) {
    ot_split_lt(n->right, key, &n->right, r);
    ot_update(n);
    *l = n;
  } else {
    ot_split_lt(n->left, key, l, &n->left);
    ot_update(n);
    *r = n;
  }
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
      memmove(ot_value(t, n), value, t->value_size);
    }
    return DS_EXISTS;
  }
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
  n->size = 1;
  if (t->value_size > 0) {
    memcpy(ot_value(t, n), value, t->value_size);
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
         a->context == b->context;
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

/* In-order walk with an explicit stack of ancestors, O(n). The stack
 * grows with the tree height; if it cannot grow, the walk goes on from the
 * current rank by selection, O(log n) expected per entry, so it never
 * fails for lack of memory. */
static ds_status_t ot_for_each(const void *s, ds_ordered_visit_fn visit,
                               void *user) {
  const ot_t *t = (const ot_t *)s;
  ot_node_t **stack;
  ot_node_t *n = t->root;
  size_t cap = 64;
  size_t top = 0;
  size_t rank = 0;
  ds_status_t st;
  stack = (ot_node_t **)ds_context_alloc(t->context, cap * sizeof(*stack));
  while (stack != NULL && (n != NULL || top > 0)) {
    while (n != NULL) {
      if (top == cap) {
        ot_node_t **grown = NULL;
        size_t bytes;
        if (cap <= ((size_t)-1) / 2 / sizeof(*stack) &&
            ds_size_mul(cap * 2, sizeof(*stack), &bytes)) {
          grown = (ot_node_t **)ds_context_realloc(t->context, stack, bytes);
        }
        if (grown == NULL) {
          ds_context_free(t->context, stack);
          stack = NULL;
          break;
        }
        stack = grown;
        cap *= 2;
      }
      stack[top++] = n;
      n = n->left;
    }
    if (stack == NULL) {
      break;
    }
    n = stack[--top];
    st = visit(n->key, ot_value(t, n), user);
    if (st != DS_OK) {
      ds_context_free(t->context, stack);
      return st;
    }
    rank++;
    n = n->right;
  }
  if (stack != NULL) {
    ds_context_free(t->context, stack);
    return DS_OK;
  }
  /* fallback: continue by rank */
  for (n = ot_select_node(t, rank); n != NULL; n = ot_select_node(t, ++rank)) {
    st = visit(n->key, ot_value(t, n), user);
    if (st != DS_OK) {
      return st;
    }
  }
  return DS_OK;
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
  s->context = context;
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
