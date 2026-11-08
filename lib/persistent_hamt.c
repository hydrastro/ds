#include "persistent_hamt.h"
#include <string.h>

#define PH_BITS 5U
#define PH_MASK 31U
#define PH_HBITS (sizeof(size_t) * CHAR_BIT)

typedef ds_phamt_node_t ph_node_t;
typedef ds_phamt_leaf_t ph_leaf_t;
typedef const ds_phamt_config_t ph_cfg_t;

static unsigned ph_popcount(unsigned long x) {
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

static ph_leaf_t **ph_leaves(ph_node_t *n) {
  return (ph_leaf_t **)(void *)(n + 1);
}

static ph_node_t **ph_children(ph_node_t *n) {
  return (ph_node_t **)(void *)(ph_leaves(n) + n->leaves);
}

static ph_leaf_t *const *ph_cleaves(const ph_node_t *n) {
  return (ph_leaf_t *const *)(const void *)(n + 1);
}

static ph_node_t *const *ph_cchildren(const ph_node_t *n) {
  return (ph_node_t *const *)(const void *)(ph_cleaves(n) + n->leaves);
}

static size_t ph_index(unsigned long map, unsigned long bit) {
  return ph_popcount(map & (bit - 1UL));
}

static unsigned long ph_bit(size_t hash, size_t shift) {
  return 1UL << ((hash >> shift) & PH_MASK);
}

static bool ph_equal(ph_cfg_t *cfg, const ph_leaf_t *l, size_t hash,
                     const void *key) {
  if (l->hash != hash) {
    return false;
  }
  return cfg->equal == NULL ? l->key == key
                            : cfg->equal(l->key, key, cfg->user);
}

static ph_node_t *ph_alloc(ph_cfg_t *cfg, size_t leaves, size_t children) {
  ph_node_t *n;
  if (leaves > 64 || children > 64) {
    /* only collision nodes grow past 32 entries */
    size_t max = ((size_t)-1 - sizeof(ph_node_t)) / sizeof(void *);
    if (leaves > max || children > max - leaves) {
      return NULL;
    }
  }
  n = (ph_node_t *)ds_context_alloc(
      cfg->context, sizeof(ph_node_t) + (leaves + children) * sizeof(void *));
  if (n == NULL) {
    return NULL;
  }
  n->refs = 1;
  n->datamap = 0;
  n->nodemap = 0;
  n->leaves = leaves;
  n->children = children;
  n->collision = 0;
  return n;
}

static void ph_leaf_release(ph_cfg_t *cfg, ph_leaf_t *l) {
  if (--l->refs > 0) {
    return;
  }
  if (cfg->destroy_key != NULL) {
    cfg->destroy_key(l->key, cfg->user);
  }
  if (cfg->destroy_value != NULL) {
    cfg->destroy_value(l->value, cfg->user);
  }
  ds_context_free(cfg->context, l);
}

/* depth is bounded by the hash width, so recursion stays shallow */
static void ph_release(ph_cfg_t *cfg, ph_node_t *n) {
  size_t i;
  if (n == NULL || --n->refs > 0) {
    return;
  }
  for (i = 0; i < n->leaves; i++) {
    ph_leaf_release(cfg, ph_leaves(n)[i]);
  }
  for (i = 0; i < n->children; i++) {
    ph_release(cfg, ph_children(n)[i]);
  }
  ds_context_free(cfg->context, n);
}

/* one more reference to every leaf and child of n */
static void ph_retain_all(ph_node_t *n) {
  size_t i;
  for (i = 0; i < n->leaves; i++) {
    ph_leaves(n)[i]->refs++;
  }
  for (i = 0; i < n->children; i++) {
    ph_children(n)[i]->refs++;
  }
}

/* new node of the same shape as n, everything retained */
static ph_node_t *ph_clone(ph_cfg_t *cfg, const ph_node_t *n) {
  ph_node_t *m = ph_alloc(cfg, n->leaves, n->children);
  if (m == NULL) {
    return NULL;
  }
  m->datamap = n->datamap;
  m->nodemap = n->nodemap;
  m->collision = n->collision;
  memcpy(ph_leaves(m), ph_cleaves(n),
         (n->leaves + n->children) * sizeof(void *));
  ph_retain_all(m);
  return m;
}

/* node for two leaves (references transferred) diverging at or below
 * shift */
static ph_node_t *ph_merge(ph_cfg_t *cfg, ph_leaf_t *a, ph_leaf_t *b,
                           size_t shift) {
  ph_node_t *n;
  unsigned long ba, bb;
  if (shift >= PH_HBITS) {
    n = ph_alloc(cfg, 2, 0);
    if (n == NULL) {
      return NULL;
    }
    n->collision = 1;
    ph_leaves(n)[0] = a;
    ph_leaves(n)[1] = b;
    return n;
  }
  ba = ph_bit(a->hash, shift);
  bb = ph_bit(b->hash, shift);
  if (ba == bb) {
    ph_node_t *c = ph_merge(cfg, a, b, shift + PH_BITS);
    if (c == NULL) {
      return NULL;
    }
    n = ph_alloc(cfg, 0, 1);
    if (n == NULL) {
      /* free c's shell without releasing the leaves we were handed */
      ph_node_t *k = c;
      while (k != NULL) {
        ph_node_t *next = k->children > 0 ? ph_children(k)[0] : NULL;
        ds_context_free(cfg->context, k);
        k = next;
      }
      return NULL;
    }
    n->nodemap = ba;
    ph_children(n)[0] = c;
    return n;
  }
  n = ph_alloc(cfg, 2, 0);
  if (n == NULL) {
    return NULL;
  }
  n->datamap = ba | bb;
  ph_leaves(n)[0] = ba < bb ? a : b;
  ph_leaves(n)[1] = ba < bb ? b : a;
  return n;
}

/*
 * Insert leaf (one reference transferred on success) into n.
 * owned: the caller hands over its reference to n and n is not shared, so
 * it may be edited; otherwise n is left untouched. On success *out holds
 * the caller's (new) reference. On failure nothing changed.
 */
static ds_status_t ph_put(ph_cfg_t *cfg, ph_node_t *n, bool owned,
                          ph_leaf_t *leaf, size_t shift, ph_node_t **out) {
  size_t i, idx, cidx;
  unsigned long bit;
  ph_node_t *m;
  if (n->collision) {
    for (i = 0; i < n->leaves; i++) {
      if (ph_equal(cfg, ph_leaves(n)[i], leaf->hash, leaf->key)) {
        break;
      }
    }
    if (i < n->leaves) {
      if (owned) {
        ph_leaf_release(cfg, ph_leaves(n)[i]);
        ph_leaves(n)[i] = leaf;
        *out = n;
        return DS_EXISTS;
      }
      m = ph_clone(cfg, n);
      if (m == NULL) {
        return DS_ERR_ALLOC;
      }
      ph_leaf_release(cfg, ph_leaves(m)[i]);
      ph_leaves(m)[i] = leaf;
      *out = m;
      return DS_EXISTS;
    }
    m = ph_alloc(cfg, n->leaves + 1, 0);
    if (m == NULL) {
      return DS_ERR_ALLOC;
    }
    m->collision = 1;
    memcpy(ph_leaves(m), ph_leaves(n), n->leaves * sizeof(void *));
    ph_leaves(m)[n->leaves] = leaf;
    for (i = 0; i < n->leaves; i++) {
      ph_leaves(m)[i]->refs++;
    }
    if (owned) {
      ph_release(cfg, n);
    }
    *out = m;
    return DS_OK;
  }
  bit = ph_bit(leaf->hash, shift);
  if (n->datamap & bit) {
    ph_leaf_t *e;
    ph_node_t *sub;
    idx = ph_index(n->datamap, bit);
    e = ph_leaves(n)[idx];
    if (ph_equal(cfg, e, leaf->hash, leaf->key)) {
      if (owned) {
        ph_leaves(n)[idx] = leaf;
        ph_leaf_release(cfg, e);
        *out = n;
        return DS_EXISTS;
      }
      m = ph_clone(cfg, n);
      if (m == NULL) {
        return DS_ERR_ALLOC;
      }
      ph_leaf_release(cfg, e);
      ph_leaves(m)[idx] = leaf;
      *out = m;
      return DS_EXISTS;
    }
    /* push the resident entry and the new one down a level */
    sub = ph_merge(cfg, e, leaf, shift + PH_BITS);
    if (sub == NULL) {
      return DS_ERR_ALLOC;
    }
    e->refs++; /* sub's reference */
    m = ph_alloc(cfg, n->leaves - 1, n->children + 1);
    if (m == NULL) {
      e->refs--;
      /* sub's leaves are e and leaf; free shells only */
      while (sub != NULL) {
        ph_node_t *next = sub->children > 0 ? ph_children(sub)[0] : NULL;
        ds_context_free(cfg->context, sub);
        sub = next;
      }
      return DS_ERR_ALLOC;
    }
    m->datamap = n->datamap & ~bit;
    m->nodemap = n->nodemap | bit;
    cidx = ph_index(m->nodemap, bit);
    memcpy(ph_leaves(m), ph_leaves(n), idx * sizeof(void *));
    memcpy(ph_leaves(m) + idx, ph_leaves(n) + idx + 1,
           (n->leaves - idx - 1) * sizeof(void *));
    memcpy(ph_children(m), ph_children(n), cidx * sizeof(void *));
    ph_children(m)[cidx] = sub;
    memcpy(ph_children(m) + cidx + 1, ph_children(n) + cidx,
           (n->children - cidx) * sizeof(void *));
    for (i = 0; i < m->leaves; i++) {
      ph_leaves(m)[i]->refs++;
    }
    for (i = 0; i < m->children; i++) {
      if (i != cidx) {
        ph_children(m)[i]->refs++;
      }
    }
    if (owned) {
      ph_release(cfg, n);
    }
    *out = m;
    return DS_OK;
  }
  if (n->nodemap & bit) {
    ph_node_t *child, *nc;
    bool child_owned;
    ds_status_t st;
    cidx = ph_index(n->nodemap, bit);
    child = ph_children(n)[cidx];
    child_owned = owned && child->refs == 1;
    st = ph_put(cfg, child, child_owned, leaf, shift + PH_BITS, &nc);
    if (DS_FAILED(st)) {
      return st;
    }
    if (owned) {
      if (!child_owned) {
        ph_release(cfg, child);
      }
      ph_children(n)[cidx] = nc;
      *out = n;
      return st;
    }
    m = ph_clone(cfg, n);
    if (m == NULL) {
      /* nc is a fresh path; the caller's guard reference keeps the leaf
       * (and the caller's key and value) alive while it goes away */
      ph_release(cfg, nc);
      return DS_ERR_ALLOC;
    }
    ph_release(cfg, ph_children(m)[cidx]);
    ph_children(m)[cidx] = nc;
    *out = m;
    return st;
  }
  idx = ph_index(n->datamap, bit);
  m = ph_alloc(cfg, n->leaves + 1, n->children);
  if (m == NULL) {
    return DS_ERR_ALLOC;
  }
  m->datamap = n->datamap | bit;
  m->nodemap = n->nodemap;
  memcpy(ph_leaves(m), ph_leaves(n), idx * sizeof(void *));
  ph_leaves(m)[idx] = leaf;
  memcpy(ph_leaves(m) + idx + 1, ph_leaves(n) + idx,
         (n->leaves - idx) * sizeof(void *));
  memcpy(ph_children(m), ph_children(n), n->children * sizeof(void *));
  for (i = 0; i < m->leaves; i++) {
    if (i != idx) {
      ph_leaves(m)[i]->refs++;
    }
  }
  for (i = 0; i < m->children; i++) {
    ph_children(m)[i]->refs++;
  }
  if (owned) {
    ph_release(cfg, n);
  }
  *out = m;
  return DS_OK;
}

/* n without leaf slot idx (bitmap: bit cleared) */
static ph_node_t *ph_drop_leaf(ph_cfg_t *cfg, const ph_node_t *n, size_t idx,
                               unsigned long bit) {
  ph_node_t *m = ph_alloc(cfg, n->leaves - 1, n->children);
  if (m == NULL) {
    return NULL;
  }
  m->collision = n->collision;
  m->datamap = n->datamap & ~bit;
  m->nodemap = n->nodemap;
  memcpy(ph_leaves(m), ph_cleaves(n), idx * sizeof(void *));
  memcpy(ph_leaves(m) + idx, ph_cleaves(n) + idx + 1,
         (n->leaves - idx - 1) * sizeof(void *));
  memcpy(ph_children(m), ph_cchildren(n), n->children * sizeof(void *));
  ph_retain_all(m);
  return m;
}

static bool ph_singleton(const ph_node_t *n) {
  return n->leaves == 1 && n->children == 0;
}

/*
 * Remove key from n. Same ownership contract as ph_put. *out may become
 * NULL when n ends up empty. DS_NOT_FOUND changes nothing (and *out is
 * not set).
 */
/* owned n: drop leaf slot idx in place (the slot's reference released) */
static void ph_drop_inplace(ph_cfg_t *cfg, ph_node_t *n, size_t idx,
                            unsigned long bit) {
  ph_leaf_t *leaf = ph_leaves(n)[idx];
  void **slots = (void **)(void *)ph_leaves(n);
  memmove(slots + idx, slots + idx + 1,
          (n->leaves + n->children - idx - 1) * sizeof(void *));
  n->leaves--;
  n->datamap &= ~bit;
  ph_leaf_release(cfg, leaf);
}

/*
 * Bitmap node without child slot cidx (bit), gaining leaf (when not NULL)
 * at that bit. Leaf references are retained; the dropped child is not
 * touched. In place when m == n (slots never grow: datamap and nodemap are
 * disjoint, so a bitmap node holds at most 32 slots).
 */
static void ph_fill_inline(const ph_node_t *n, ph_node_t *m, unsigned long bit,
                           size_t cidx, ph_leaf_t *leaf) {
  void *tmp[32];
  size_t idx = ph_index(n->datamap, bit), k = 0, i, L = n->leaves,
         C = n->children;
  for (i = 0; i < idx; i++) {
    tmp[k++] = ph_cleaves(n)[i];
  }
  if (leaf != NULL) {
    tmp[k++] = leaf;
    leaf->refs++;
  }
  for (i = idx; i < L; i++) {
    tmp[k++] = ph_cleaves(n)[i];
  }
  for (i = 0; i < C; i++) {
    if (i != cidx) {
      tmp[k++] = ph_cchildren(n)[i];
    }
  }
  m->datamap = leaf != NULL ? (n->datamap | bit) : n->datamap;
  m->nodemap = n->nodemap & ~bit;
  m->leaves = L + (leaf != NULL ? 1U : 0U);
  m->children = C - 1;
  memcpy(ph_leaves(m), tmp, k * sizeof(void *));
}

/*
 * Remove key from n. Same ownership contract as ph_put. *out may become
 * NULL when n ends up empty. DS_NOT_FOUND changes nothing (and *out is
 * not set). In owned mode only the deepest level may allocate (collision
 * nodes); every level above edits in place, so a failure never leaves a
 * half-applied edit.
 */
static ds_status_t ph_remove(ph_cfg_t *cfg, ph_node_t *n, bool owned,
                             size_t hash, const void *key, size_t shift,
                             ph_node_t **out) {
  size_t i, idx, cidx;
  unsigned long bit;
  ph_node_t *m;
  if (n->collision) {
    for (i = 0; i < n->leaves; i++) {
      if (ph_equal(cfg, ph_leaves(n)[i], hash, key)) {
        break;
      }
    }
    if (i == n->leaves) {
      return DS_NOT_FOUND;
    }
    if (owned) {
      ph_drop_inplace(cfg, n, i, 0);
      *out = n;
      return DS_OK;
    }
    m = ph_drop_leaf(cfg, n, i, 0);
    if (m == NULL) {
      return DS_ERR_ALLOC;
    }
    *out = m;
    return DS_OK;
  }
  bit = ph_bit(hash, shift);
  if (n->datamap & bit) {
    idx = ph_index(n->datamap, bit);
    if (!ph_equal(cfg, ph_leaves(n)[idx], hash, key)) {
      return DS_NOT_FOUND;
    }
    if (n->leaves == 1 && n->children == 0) {
      if (owned) {
        ph_release(cfg, n);
      }
      *out = NULL;
      return DS_OK;
    }
    if (owned) {
      ph_drop_inplace(cfg, n, idx, bit);
      *out = n;
      return DS_OK;
    }
    m = ph_drop_leaf(cfg, n, idx, bit);
    if (m == NULL) {
      return DS_ERR_ALLOC;
    }
    *out = m;
    return DS_OK;
  }
  if (n->nodemap & bit) {
    ph_node_t *child, *nc;
    bool child_owned;
    ds_status_t st;
    cidx = ph_index(n->nodemap, bit);
    child = ph_children(n)[cidx];
    child_owned = owned && child->refs == 1;
    st = ph_remove(cfg, child, child_owned, hash, key, shift + PH_BITS, &nc);
    if (st != DS_OK) {
      return st;
    }
    if (nc != NULL && !ph_singleton(nc)) {
      if (owned) {
        if (!child_owned) {
          ph_release(cfg, child);
        }
        ph_children(n)[cidx] = nc;
        *out = n;
        return DS_OK;
      }
      m = ph_clone(cfg, n);
      if (m == NULL) {
        ph_release(cfg, nc);
        return DS_ERR_ALLOC;
      }
      ph_release(cfg, ph_children(m)[cidx]);
      ph_children(m)[cidx] = nc;
      *out = m;
      return DS_OK;
    }
    /* inline the child's last entry (or drop an emptied child) */
    if (nc == NULL && n->leaves == 0 && n->children == 1) {
      if (owned) {
        if (child_owned) {
          ph_children(n)[cidx] = NULL; /* consumed by the recursion */
        }
        ph_release(cfg, n);
      }
      *out = NULL;
      return DS_OK;
    }
    if (owned) {
      if (!child_owned) {
        ph_release(cfg, child); /* this node's reference to it */
      }
      ph_fill_inline(n, n, bit, cidx, nc != NULL ? ph_leaves(nc)[0] : NULL);
      ph_release(cfg, nc);
      *out = n;
      return DS_OK;
    }
    m = ph_alloc(cfg, n->leaves + (nc != NULL ? 1U : 0U), n->children - 1);
    if (m == NULL) {
      ph_release(cfg, nc);
      return DS_ERR_ALLOC;
    }
    ph_fill_inline(n, m, bit, cidx, nc != NULL ? ph_leaves(nc)[0] : NULL);
    for (i = 0; i < m->leaves; i++) {
      if (nc == NULL || ph_leaves(m)[i] != ph_leaves(nc)[0]) {
        ph_leaves(m)[i]->refs++;
      }
    }
    for (i = 0; i < m->children; i++) {
      ph_children(m)[i]->refs++;
    }
    ph_release(cfg, nc);
    *out = m;
    return DS_OK;
  }
  return DS_NOT_FOUND;
}

/* ---- public ---- */

void ds_phamt_config_init(ds_phamt_config_t *config) {
  if (config == NULL) {
    return;
  }
  memset(config, 0, sizeof(*config));
}

void ds_phamt_init(ds_phamt_t *map, const ds_phamt_config_t *config) {
  if (map == NULL) {
    return;
  }
  map->root = NULL;
  map->size = 0;
  map->config = config;
}

void ds_phamt_copy(const ds_phamt_t *map, ds_phamt_t *out) {
  if (map == NULL || out == NULL) {
    return;
  }
  if (map->root != NULL) {
    map->root->refs++;
  }
  if (out != map) {
    *out = *map;
  } else if (map->root != NULL) {
    map->root->refs--;
  }
}

void ds_phamt_release(ds_phamt_t *map) {
  if (map == NULL) {
    return;
  }
  if (map->config != NULL) {
    ph_release(map->config, map->root);
  }
  map->root = NULL;
  map->size = 0;
}

ds_status_t ds_phamt_put(const ds_phamt_t *map, void *key, void *value,
                         ds_phamt_t *out) {
  ph_cfg_t *cfg;
  ph_leaf_t *leaf;
  ph_node_t *root, *old;
  ds_status_t st;
  bool owned;
  size_t size;
  if (map == NULL || out == NULL || map->config == NULL ||
      map->config->hash == NULL) {
    return DS_ERR_NULL;
  }
  cfg = map->config;
  old = map->root;
  size = map->size;
  owned = out == map && old != NULL && old->refs == 1;
  leaf = (ph_leaf_t *)ds_context_alloc(cfg->context, sizeof(ph_leaf_t));
  if (leaf == NULL) {
    return DS_ERR_ALLOC;
  }
  leaf->refs = 2; /* the structure's reference plus a guard against
                     failure paths releasing the caller's key/value */
  leaf->hash = cfg->hash(key, cfg->user);
  leaf->key = key;
  leaf->value = value;
  if (old == NULL) {
    root = ph_alloc(cfg, 1, 0);
    if (root == NULL) {
      ds_context_free(cfg->context, leaf);
      return DS_ERR_ALLOC;
    }
    root->datamap = ph_bit(leaf->hash, 0);
    ph_leaves(root)[0] = leaf;
    st = DS_OK;
  } else {
    st = ph_put(cfg, old, owned, leaf, 0, &root);
    if (DS_FAILED(st)) {
      ds_context_free(cfg->context, leaf);
      return st;
    }
  }
  leaf->refs--;
  if (out == map && !owned) {
    ph_release(cfg, old); /* the handle's reference to a shared root */
  }
  out->root = root;
  out->size = size + (st == DS_OK ? 1U : 0U);
  out->config = cfg;
  return st;
}

ds_status_t ds_phamt_remove(const ds_phamt_t *map, const void *key,
                            ds_phamt_t *out) {
  ph_cfg_t *cfg;
  ph_node_t *root, *old;
  ds_status_t st;
  bool owned;
  size_t size;
  if (map == NULL || out == NULL || map->config == NULL ||
      map->config->hash == NULL) {
    return DS_ERR_NULL;
  }
  cfg = map->config;
  old = map->root;
  size = map->size;
  if (old == NULL) {
    st = DS_NOT_FOUND;
  } else {
    owned = out == map && old->refs == 1;
    st = ph_remove(cfg, old, owned, cfg->hash(key, cfg->user), key, 0, &root);
    if (st == DS_OK) {
      if (out == map && !owned) {
        ph_release(cfg, old);
      }
      out->root = root;
      out->size = size - 1;
      out->config = cfg;
      return DS_OK;
    }
    if (DS_FAILED(st)) {
      return st;
    }
  }
  ds_phamt_copy(map, out);
  return st;
}

static const ph_leaf_t *ph_find(const ds_phamt_t *map, const void *key) {
  ph_cfg_t *cfg;
  const ph_node_t *n;
  size_t hash, shift = 0, i;
  if (map == NULL || map->root == NULL || map->config == NULL ||
      map->config->hash == NULL) {
    return NULL;
  }
  cfg = map->config;
  hash = cfg->hash(key, cfg->user);
  n = map->root;
  for (;;) {
    unsigned long bit;
    if (n->collision) {
      for (i = 0; i < n->leaves; i++) {
        if (ph_equal(cfg, ph_cleaves(n)[i], hash, key)) {
          return ph_cleaves(n)[i];
        }
      }
      return NULL;
    }
    bit = ph_bit(hash, shift);
    if (n->datamap & bit) {
      const ph_leaf_t *l = ph_cleaves(n)[ph_index(n->datamap, bit)];
      return ph_equal(cfg, l, hash, key) ? l : NULL;
    }
    if (!(n->nodemap & bit)) {
      return NULL;
    }
    n = ph_cchildren(n)[ph_index(n->nodemap, bit)];
    shift += PH_BITS;
  }
}

ds_status_t ds_phamt_get(const ds_phamt_t *map, const void *key,
                         void **out_value) {
  const ph_leaf_t *l;
  if (map == NULL) {
    return DS_ERR_NULL;
  }
  l = ph_find(map, key);
  if (l == NULL) {
    return DS_NOT_FOUND;
  }
  if (out_value != NULL) {
    *out_value = l->value;
  }
  return DS_OK;
}

bool ds_phamt_contains(const ds_phamt_t *map, const void *key) {
  return ph_find(map, key) != NULL;
}

size_t ds_phamt_size(const ds_phamt_t *map) {
  return map == NULL ? 0 : map->size;
}

static bool ph_leaf_same(ph_cfg_t *cfg, const ph_leaf_t *a, const ph_leaf_t *b,
                         ds_phamt_equal_func_t value_equal, void *user) {
  if (a == b) {
    return true;
  }
  if (!ph_equal(cfg, a, b->hash, b->key)) {
    return false;
  }
  return value_equal == NULL ? a->value == b->value
                             : value_equal(a->value, b->value, user);
}

static bool ph_nodes_equal(ph_cfg_t *cfg, const ph_node_t *a,
                           const ph_node_t *b,
                           ds_phamt_equal_func_t value_equal, void *user) {
  size_t i, j;
  if (a == b) {
    return true;
  }
  if (a->collision != b->collision || a->leaves != b->leaves ||
      a->children != b->children) {
    return false;
  }
  if (a->collision) {
    for (i = 0; i < a->leaves; i++) {
      for (j = 0; j < b->leaves; j++) {
        if (ph_leaf_same(cfg, ph_cleaves(a)[i], ph_cleaves(b)[j], value_equal,
                         user)) {
          break;
        }
      }
      if (j == b->leaves) {
        return false;
      }
    }
    return true;
  }
  if (a->datamap != b->datamap || a->nodemap != b->nodemap) {
    return false;
  }
  for (i = 0; i < a->leaves; i++) {
    if (!ph_leaf_same(cfg, ph_cleaves(a)[i], ph_cleaves(b)[i], value_equal,
                      user)) {
      return false;
    }
  }
  for (i = 0; i < a->children; i++) {
    if (!ph_nodes_equal(cfg, ph_cchildren(a)[i], ph_cchildren(b)[i],
                        value_equal, user)) {
      return false;
    }
  }
  return true;
}

bool ds_phamt_equals(const ds_phamt_t *a, const ds_phamt_t *b,
                     ds_phamt_equal_func_t value_equal, void *user) {
  if (a == NULL || b == NULL) {
    return a == b;
  }
  if (a->size != b->size) {
    return false;
  }
  if (a->root == NULL || b->root == NULL) {
    return a->root == b->root;
  }
  return ph_nodes_equal(a->config, a->root, b->root, value_equal, user);
}

void ds_phamt_iter_init(ds_phamt_iter_t *it, const ds_phamt_t *map) {
  if (it == NULL) {
    return;
  }
  it->depth = 0;
  if (map != NULL && map->root != NULL) {
    it->node[0] = map->root;
    it->pos[0] = 0;
    it->depth = 1;
  }
}

bool ds_phamt_iter_next(ds_phamt_iter_t *it, const void **key, void **value) {
  if (it == NULL) {
    return false;
  }
  while (it->depth > 0) {
    const ph_node_t *n = it->node[it->depth - 1];
    size_t p = it->pos[it->depth - 1]++;
    if (p < n->leaves) {
      const ph_leaf_t *l = ph_cleaves(n)[p];
      if (key != NULL) {
        *key = l->key;
      }
      if (value != NULL) {
        *value = l->value;
      }
      return true;
    }
    if (p < n->leaves + n->children) {
      it->node[it->depth] = ph_cchildren(n)[p - n->leaves];
      it->pos[it->depth] = 0;
      it->depth++;
    } else {
      it->depth--;
    }
  }
  return false;
}

ds_status_t ds_phamt_visit(const ds_phamt_t *map, ds_phamt_visit_func_t visit,
                           void *user) {
  ds_phamt_iter_t it;
  const void *k;
  void *v;
  if (map == NULL || visit == NULL) {
    return DS_ERR_NULL;
  }
  ds_phamt_iter_init(&it, map);
  while (ds_phamt_iter_next(&it, &k, &v)) {
    if (!visit(k, v, user)) {
      return DS_STOP;
    }
  }
  return DS_OK;
}

size_t ds_phamt_node_count(const ds_phamt_t *map) {
  const ph_node_t *stack[DS_PHAMT_MAX_DEPTH];
  size_t pos[DS_PHAMT_MAX_DEPTH], depth = 0, count = 0;
  if (map == NULL || map->root == NULL) {
    return 0;
  }
  stack[0] = map->root;
  pos[0] = 0;
  depth = 1;
  count = 1;
  while (depth > 0) {
    const ph_node_t *n = stack[depth - 1];
    if (pos[depth - 1] < n->children) {
      stack[depth] = ph_cchildren(n)[pos[depth - 1]++];
      pos[depth] = 0;
      depth++;
      count++;
    } else {
      depth--;
    }
  }
  return count;
}
