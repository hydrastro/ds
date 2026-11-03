#include "veb_tree.h"
#include "rh_map.h"
#include <limits.h>

#define VEB_LEAF_BITS 5U   /* levels of <= 32 keys are one word */
#define VEB_DENSE_BITS 10U /* <= 2^10 clusters: dense pointer array */
#define VEB_WIDTH ((unsigned int)(CHAR_BIT * sizeof(unsigned long)))

/* ---- word helpers -------------------------------------------------------- */

static unsigned long veb_lowest(unsigned long m) {
  unsigned long i = 0;
  while ((m & 1UL) == 0) {
    m >>= 1;
    i++;
  }
  return i;
}

static unsigned long veb_highest(unsigned long m) {
  unsigned long i = 0;
  while (m > 1UL) {
    m >>= 1;
    i++;
  }
  return i;
}

/* Bits strictly above / below position x of a leaf mask. */
static unsigned long veb_above(unsigned long m, unsigned long x) {
  return x + 1 >= VEB_WIDTH ? 0UL : (m >> (x + 1)) << (x + 1);
}

static unsigned long veb_below(unsigned long m, unsigned long x) {
  return x == 0 ? 0UL : m & ((1UL << x) - 1UL);
}

/* ---- node helpers -------------------------------------------------------- */

static bool veb_is_leaf(const ds_veb_node_t *n) {
  return n->bits <= VEB_LEAF_BITS;
}

static unsigned int veb_lo_bits(const ds_veb_node_t *n) {
  return n->bits / 2;
}

static unsigned int veb_hi_bits(const ds_veb_node_t *n) {
  return n->bits - n->bits / 2;
}

static bool veb_dense(const ds_veb_node_t *n) {
  return veb_hi_bits(n) <= VEB_DENSE_BITS;
}

static unsigned long veb_high(const ds_veb_node_t *n, unsigned long x) {
  return x >> veb_lo_bits(n);
}

static unsigned long veb_low(const ds_veb_node_t *n, unsigned long x) {
  return x & ((1UL << veb_lo_bits(n)) - 1UL);
}

static unsigned long veb_index(const ds_veb_node_t *n, unsigned long h,
                               unsigned long l) {
  return (h << veb_lo_bits(n)) | l;
}

static bool veb_empty(const ds_veb_node_t *n) {
  if (n == NULL) {
    return true;
  }
  return veb_is_leaf(n) ? n->mask == 0 : !n->has;
}

static unsigned long veb_min_of(const ds_veb_node_t *n) {
  return veb_is_leaf(n) ? veb_lowest(n->mask) : n->min;
}

static unsigned long veb_max_of(const ds_veb_node_t *n) {
  return veb_is_leaf(n) ? veb_highest(n->mask) : n->max;
}

static void veb_node_init(ds_veb_node_t *n, unsigned int bits) {
  n->min = 0;
  n->max = 0;
  n->mask = 0;
  n->bits = bits;
  n->has = false;
  n->summary = NULL;
  n->clusters = NULL;
}

static ds_veb_node_t *veb_node_new(ds_context_t *ctx, unsigned int bits) {
  ds_veb_node_t *n =
      (ds_veb_node_t *)ds_context_alloc(ctx, sizeof(ds_veb_node_t));
  if (n != NULL) {
    veb_node_init(n, bits);
  }
  return n;
}

/* ---- cluster index: dense array or rh_map -------------------------------- */

static ds_veb_node_t *veb_cluster_get(const ds_veb_node_t *n, unsigned long h) {
  void *slot;
  if (n->clusters == NULL) {
    return NULL;
  }
  if (veb_dense(n)) {
    return ((ds_veb_node_t **)n->clusters)[h];
  }
  slot = ds_rh_map_get((const ds_rh_map_t *)n->clusters, &h);
  return slot == NULL ? NULL : *(ds_veb_node_t **)slot;
}

static ds_status_t veb_clusters_create(ds_context_t *ctx, ds_veb_node_t *n) {
  ds_rh_map_config_t config;
  ds_rh_map_t *map;
  ds_status_t status;
  if (veb_dense(n)) {
    n->clusters = ds_context_calloc(ctx, (size_t)1 << veb_hi_bits(n),
                                    sizeof(ds_veb_node_t *));
    return n->clusters == NULL ? DS_ERR_ALLOC : DS_OK;
  }
  map = (ds_rh_map_t *)ds_context_alloc(ctx, sizeof(ds_rh_map_t));
  if (map == NULL) {
    return DS_ERR_ALLOC;
  }
  ds_rh_map_config_init(&config);
  config.key_size = sizeof(unsigned long);
  config.value_size = sizeof(ds_veb_node_t *);
  config.hash = ds_rh_map_hash_ulong;
  config.equal = ds_rh_map_equal_ulong;
  config.context = ctx;
  status = ds_rh_map_init(map, &config, 0);
  if (status != DS_OK) {
    ds_context_free(ctx, map);
    return status;
  }
  n->clusters = map;
  return DS_OK;
}

static void veb_clusters_free(ds_context_t *ctx, ds_veb_node_t *n) {
  if (n->clusters == NULL) {
    return;
  }
  if (!veb_dense(n)) {
    ds_rh_map_destroy((ds_rh_map_t *)n->clusters);
  }
  ds_context_free(ctx, n->clusters);
  n->clusters = NULL;
}

static ds_status_t veb_cluster_put(ds_veb_node_t *n, unsigned long h,
                                   ds_veb_node_t *c) {
  if (veb_dense(n)) {
    ((ds_veb_node_t **)n->clusters)[h] = c;
    return DS_OK;
  }
  return ds_rh_map_put((ds_rh_map_t *)n->clusters, &h, &c);
}

static void veb_cluster_drop(ds_veb_node_t *n, unsigned long h) {
  if (veb_dense(n)) {
    ((ds_veb_node_t **)n->clusters)[h] = NULL;
  } else {
    (void)ds_rh_map_remove((ds_rh_map_t *)n->clusters, &h, NULL);
  }
}

/* ---- recursive operations (depth O(log w)) ------------------------------- */

static void veb_free_subtree(ds_context_t *ctx, ds_veb_node_t *n);

static void veb_free_children(ds_context_t *ctx, ds_veb_node_t *n) {
  size_t i;
  size_t count;
  size_t cursor = 0;
  const void *key;
  void *value;
  if (n->summary != NULL) {
    veb_free_subtree(ctx, n->summary);
    n->summary = NULL;
  }
  if (n->clusters != NULL) {
    if (veb_dense(n)) {
      count = (size_t)1 << veb_hi_bits(n);
      for (i = 0; i < count; i++) {
        if (((ds_veb_node_t **)n->clusters)[i] != NULL) {
          veb_free_subtree(ctx, ((ds_veb_node_t **)n->clusters)[i]);
        }
      }
    } else {
      while (ds_rh_map_next((const ds_rh_map_t *)n->clusters, &cursor, &key,
                            &value)) {
        veb_free_subtree(ctx, *(ds_veb_node_t **)value);
      }
    }
    veb_clusters_free(ctx, n);
  }
}

static void veb_free_subtree(ds_context_t *ctx, ds_veb_node_t *n) {
  veb_free_children(ctx, n);
  ds_context_free(ctx, n);
}

static bool veb_member(const ds_veb_node_t *n, unsigned long x) {
  const ds_veb_node_t *c;
  for (;;) {
    if (veb_is_leaf(n)) {
      return ((n->mask >> x) & 1UL) != 0;
    }
    if (!n->has) {
      return false;
    }
    if (x == n->min || x == n->max) {
      return true;
    }
    c = veb_cluster_get(n, veb_high(n, x));
    if (c == NULL) {
      return false;
    }
    x = veb_low(n, x);
    n = c;
  }
}

/* x is not in n. Either succeeds or leaves n unchanged. */
static ds_status_t veb_insert(ds_context_t *ctx, ds_veb_node_t *n,
                              unsigned long x) {
  unsigned long y;
  unsigned long newmin;
  unsigned long h;
  unsigned long l;
  ds_veb_node_t *c;
  bool made_clusters = false;
  bool made_cluster = false;
  bool made_summary = false;
  ds_status_t status;

  if (veb_is_leaf(n)) {
    n->mask |= 1UL << x;
    return DS_OK;
  }
  if (!n->has) {
    n->min = x;
    n->max = x;
    n->has = true;
    return DS_OK;
  }
  y = x;
  newmin = n->min;
  if (x < n->min) {
    y = n->min; /* the old minimum moves down into the clusters */
    newmin = x;
  }
  h = veb_high(n, y);
  l = veb_low(n, y);

  if (n->clusters == NULL) {
    status = veb_clusters_create(ctx, n);
    if (status != DS_OK) {
      return status;
    }
    made_clusters = true;
  }
  c = veb_cluster_get(n, h);
  if (c == NULL) {
    c = veb_node_new(ctx, veb_lo_bits(n));
    if (c == NULL) {
      status = DS_ERR_ALLOC;
      goto fail;
    }
    status = veb_cluster_put(n, h, c);
    if (status != DS_OK) {
      ds_context_free(ctx, c);
      goto fail;
    }
    made_cluster = true;
  }
  if (veb_empty(c)) {
    if (n->summary == NULL) {
      n->summary = veb_node_new(ctx, veb_hi_bits(n));
      if (n->summary == NULL) {
        status = DS_ERR_ALLOC;
        goto fail;
      }
      made_summary = true;
    }
    status = veb_insert(ctx, n->summary, h);
    if (status != DS_OK) {
      goto fail;
    }
    (void)veb_insert(ctx, c, l); /* empty: never allocates */
  } else {
    status = veb_insert(ctx, c, l);
    if (status != DS_OK) {
      goto fail;
    }
  }
  n->min = newmin;
  if (y > n->max) {
    n->max = y;
  }
  return DS_OK;

fail:
  if (made_summary) {
    ds_context_free(ctx, n->summary);
    n->summary = NULL;
  }
  if (made_cluster) {
    veb_cluster_drop(n, h);
    ds_context_free(ctx, c);
  }
  if (made_clusters) {
    veb_clusters_free(ctx, n);
  }
  return status;
}

/* x is in n. */
static void veb_delete(ds_context_t *ctx, ds_veb_node_t *n, unsigned long x) {
  unsigned long h;
  unsigned long first;
  ds_veb_node_t *c;

  if (veb_is_leaf(n)) {
    n->mask &= ~(1UL << x);
    return;
  }
  if (n->min == n->max) {
    n->has = false;
    return;
  }
  if (x == n->min) {
    /* The next smallest key leaves its cluster to become the minimum. */
    first = veb_min_of(n->summary);
    c = veb_cluster_get(n, first);
    x = veb_index(n, first, veb_min_of(c));
    n->min = x;
  }
  h = veb_high(n, x);
  c = veb_cluster_get(n, h);
  veb_delete(ctx, c, veb_low(n, x));
  if (veb_empty(c)) {
    veb_cluster_drop(n, h);
    ds_context_free(ctx, c);
    veb_delete(ctx, n->summary, h);
    if (veb_empty(n->summary)) {
      ds_context_free(ctx, n->summary);
      n->summary = NULL;
      veb_clusters_free(ctx, n);
      n->max = n->min; /* only the minimum is left */
    } else if (x == n->max) {
      h = veb_max_of(n->summary);
      n->max = veb_index(n, h, veb_max_of(veb_cluster_get(n, h)));
    }
  } else if (x == n->max) {
    n->max = veb_index(n, h, veb_max_of(c));
  }
}

static bool veb_succ(const ds_veb_node_t *n, unsigned long x,
                     unsigned long *out) {
  unsigned long h;
  unsigned long l;
  unsigned long r;
  const ds_veb_node_t *c;
  unsigned long m;
  if (veb_is_leaf(n)) {
    m = veb_above(n->mask, x);
    if (m == 0) {
      return false;
    }
    *out = veb_lowest(m);
    return true;
  }
  if (!n->has) {
    return false;
  }
  if (x < n->min) {
    *out = n->min;
    return true;
  }
  h = veb_high(n, x);
  l = veb_low(n, x);
  c = veb_cluster_get(n, h);
  if (!veb_empty(c) && l < veb_max_of(c)) {
    (void)veb_succ(c, l, &r);
    *out = veb_index(n, h, r);
    return true;
  }
  if (n->summary == NULL || !veb_succ(n->summary, h, &h)) {
    return false;
  }
  *out = veb_index(n, h, veb_min_of(veb_cluster_get(n, h)));
  return true;
}

static bool veb_pred(const ds_veb_node_t *n, unsigned long x,
                     unsigned long *out) {
  unsigned long h;
  unsigned long l;
  unsigned long r;
  const ds_veb_node_t *c;
  unsigned long m;
  if (veb_is_leaf(n)) {
    m = veb_below(n->mask, x);
    if (m == 0) {
      return false;
    }
    *out = veb_highest(m);
    return true;
  }
  if (!n->has) {
    return false;
  }
  if (x > n->max) {
    *out = n->max;
    return true;
  }
  h = veb_high(n, x);
  l = veb_low(n, x);
  c = veb_cluster_get(n, h);
  if (!veb_empty(c) && l > veb_min_of(c)) {
    (void)veb_pred(c, l, &r);
    *out = veb_index(n, h, r);
    return true;
  }
  if (n->summary != NULL && veb_pred(n->summary, h, &h)) {
    *out = veb_index(n, h, veb_max_of(veb_cluster_get(n, h)));
    return true;
  }
  if (x > n->min) {
    *out = n->min;
    return true;
  }
  return false;
}

/* ---- public API ---------------------------------------------------------- */

static bool veb_in_universe(const ds_veb_tree_t *tree, unsigned long key) {
  return tree->root.bits >= VEB_WIDTH || (key >> tree->root.bits) == 0;
}

ds_status_t ds_veb_tree_init(ds_veb_tree_t *tree, unsigned int universe_bits,
                             ds_context_t *context) {
  if (tree == NULL) {
    return DS_ERR_NULL;
  }
  if (universe_bits == 0 || universe_bits > VEB_WIDTH) {
    return DS_ERR_RANGE;
  }
  veb_node_init(&tree->root, universe_bits);
  tree->size = 0;
  tree->context = context;
  return DS_OK;
}

void ds_veb_tree_destroy(ds_veb_tree_t *tree) {
  if (tree != NULL) {
    veb_free_children(tree->context, &tree->root);
    veb_node_init(&tree->root, tree->root.bits);
    tree->size = 0;
  }
}

void ds_veb_tree_clear(ds_veb_tree_t *tree) {
  ds_veb_tree_destroy(tree);
}

ds_status_t ds_veb_tree_insert(ds_veb_tree_t *tree, unsigned long key) {
  ds_status_t status;
  if (!veb_in_universe(tree, key)) {
    return DS_ERR_RANGE;
  }
  if (veb_member(&tree->root, key)) {
    return DS_EXISTS;
  }
  status = veb_insert(tree->context, &tree->root, key);
  if (status == DS_OK) {
    tree->size++;
  }
  return status;
}

ds_status_t ds_veb_tree_remove(ds_veb_tree_t *tree, unsigned long key) {
  if (!veb_in_universe(tree, key) || !veb_member(&tree->root, key)) {
    return DS_NOT_FOUND;
  }
  veb_delete(tree->context, &tree->root, key);
  tree->size--;
  return DS_OK;
}

bool ds_veb_tree_contains(const ds_veb_tree_t *tree, unsigned long key) {
  return veb_in_universe(tree, key) && veb_member(&tree->root, key);
}

ds_status_t ds_veb_tree_min(const ds_veb_tree_t *tree, unsigned long *out) {
  if (tree->size == 0) {
    return DS_EMPTY;
  }
  if (out != NULL) {
    *out = veb_min_of(&tree->root);
  }
  return DS_OK;
}

ds_status_t ds_veb_tree_max(const ds_veb_tree_t *tree, unsigned long *out) {
  if (tree->size == 0) {
    return DS_EMPTY;
  }
  if (out != NULL) {
    *out = veb_max_of(&tree->root);
  }
  return DS_OK;
}

ds_status_t ds_veb_tree_pop_min(ds_veb_tree_t *tree, unsigned long *out) {
  unsigned long key;
  if (tree->size == 0) {
    return DS_EMPTY;
  }
  key = veb_min_of(&tree->root);
  veb_delete(tree->context, &tree->root, key);
  tree->size--;
  if (out != NULL) {
    *out = key;
  }
  return DS_OK;
}

ds_status_t ds_veb_tree_pop_max(ds_veb_tree_t *tree, unsigned long *out) {
  unsigned long key;
  if (tree->size == 0) {
    return DS_EMPTY;
  }
  key = veb_max_of(&tree->root);
  veb_delete(tree->context, &tree->root, key);
  tree->size--;
  if (out != NULL) {
    *out = key;
  }
  return DS_OK;
}

ds_status_t ds_veb_tree_successor(const ds_veb_tree_t *tree, unsigned long key,
                                  unsigned long *out) {
  unsigned long r;
  if (tree->size == 0 || !veb_in_universe(tree, key)) {
    return DS_NOT_FOUND;
  }
  if (!veb_succ(&tree->root, key, &r)) {
    return DS_NOT_FOUND;
  }
  if (out != NULL) {
    *out = r;
  }
  return DS_OK;
}

ds_status_t ds_veb_tree_predecessor(const ds_veb_tree_t *tree,
                                    unsigned long key, unsigned long *out) {
  unsigned long r;
  if (tree->size == 0) {
    return DS_NOT_FOUND;
  }
  if (!veb_in_universe(tree, key)) {
    r = veb_max_of(&tree->root);
  } else if (!veb_pred(&tree->root, key, &r)) {
    return DS_NOT_FOUND;
  }
  if (out != NULL) {
    *out = r;
  }
  return DS_OK;
}

size_t ds_veb_tree_size(const ds_veb_tree_t *tree) {
  return tree->size;
}

bool ds_veb_tree_is_empty(const ds_veb_tree_t *tree) {
  return tree->size == 0;
}

unsigned int ds_veb_tree_universe_bits(const ds_veb_tree_t *tree) {
  return tree->root.bits;
}
