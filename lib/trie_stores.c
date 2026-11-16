#include "trie_stores.h"
#include "allocators.h"
#include "checked.h"
#include "rh_map.h"
#include "vec.h"
#include <stdarg.h>
#include <stdlib.h>

typedef void (*trie_visit_t)(ds_trie_t *, ds_trie_node_t *, va_list *);

static void *ts_alloc(ds_trie_t *trie, size_t size) {
  if (trie != NULL && (trie->use_context || trie->allocator != NULL)) {
    return ds_trie_alloc(trie, size);
  }
  return malloc(size);
}

/* apply and clone come from the trie's generic walk and copy, which need
 * no recursion: a store only provides next. */
static void ts_apply(ds_trie_t *trie, void *store, trie_visit_t f,
                     va_list *args, ds_trie_next_fn next) {
  ds_trie_node_t *first = store != NULL ? next(store, NULL) : NULL;
  if (first != NULL) {
    ds_trie_walk(trie, first->parent, next, f, args);
  }
}

/* ---- Robin Hood map store: slice -> child ---- */

/* Each store remembers how it was allocated (a context, else a
 * deallocator): a cloned store may outlive the trie it was created
 * through. The map or vector inside a store takes its arrays from the
 * same place as the trie's nodes: the trie's context, or, for a trie
 * given its own allocator functions, a context over them (allocators.h)
 * that the store carries after itself in the same block. A trie on
 * malloc and free needs none: its arrays come from the default context. */
typedef struct ts_owner {
  void (*deallocator)(void *);
  ds_context_t *context; /* when the trie allocates from its context */
  ds_context_t *arrays;  /* where the store's map or vector allocates */
} ts_owner_t;

typedef struct ts_functions {
  ds_allocator_functions_t functions;
  ds_context_t context;
} ts_functions_t;

static bool ts_own_functions(const ds_trie_t *trie) {
  return trie != NULL && !trie->use_context && trie->allocator != NULL &&
         trie->deallocator != NULL &&
         !(trie->allocator == malloc && trie->deallocator == free);
}

/* A store of size bytes, followed (aligned) by the context over the
 * trie's allocator functions when it needs one; owner is set up. */
static void *ts_create_store(ds_trie_t *trie, size_t size, ts_owner_t *owner) {
  size_t at = (size + DS_MAX_ALIGN - 1) / DS_MAX_ALIGN * DS_MAX_ALIGN;
  bool own = ts_own_functions(trie);
  unsigned char *block =
      (unsigned char *)ts_alloc(trie, own ? at + sizeof(ts_functions_t) : size);
  if (block == NULL) {
    return NULL;
  }
  owner->deallocator = trie != NULL ? trie->deallocator : NULL;
  owner->context = trie != NULL && trie->use_context ? trie->context : NULL;
  owner->arrays = owner->context; /* NULL: the default context */
  if (own) {
    ts_functions_t *f = (ts_functions_t *)(void *)(block + at);
    f->functions.allocate = trie->allocator;
    f->functions.release = trie->deallocator;
    ds_context_init(&f->context);
    ds_context_use_allocator_functions(&f->context, &f->functions);
    f->context.diagnostic_sink = trie->context->diagnostic_sink;
    owner->arrays = &f->context;
  }
  return block;
}

typedef struct ts_rh_store {
  ds_rh_map_t map;
  ts_owner_t owner;
} ts_rh_store_t;

static void ts_free(ts_owner_t owner, void *block) {
  if (owner.context != NULL) {
    ds_context_free(owner.context, block);
  } else if (owner.deallocator != NULL) {
    owner.deallocator(block);
  } else {
    free(block);
  }
}

static ds_trie_node_t *ts_rh_search(void *store, size_t slice) {
  ds_trie_node_t **child =
      (ds_trie_node_t **)ds_rh_map_get(&((ts_rh_store_t *)store)->map, &slice);
  return child == NULL ? NULL : *child;
}

static void *ts_rh_create(ds_trie_t *trie, size_t size) {
  ts_owner_t owner;
  ts_rh_store_t *s =
      (ts_rh_store_t *)ts_create_store(trie, sizeof(ts_rh_store_t), &owner);
  ds_rh_map_config_t config;
  (void)size; /* grow on demand: most trie nodes have few children */
  if (s == NULL) {
    return NULL;
  }
  s->owner = owner;
  ds_rh_map_config_init(&config);
  config.key_size = sizeof(size_t);
  config.value_size = sizeof(ds_trie_node_t *);
  config.hash = ds_rh_map_hash_size;
  config.equal = ds_rh_map_equal_size;
  config.context = owner.arrays;
  if (ds_rh_map_init(&s->map, &config, 0) != DS_OK) {
    ts_free(s->owner, s);
    return NULL;
  }
  return s;
}

static void ts_rh_insert(void *store, ds_trie_node_t *node) {
  ds_rh_map_put(&((ts_rh_store_t *)store)->map, &node->data_slice, &node);
}

static void ts_rh_remove(void *store, ds_trie_node_t *node) {
  ds_rh_map_remove(&((ts_rh_store_t *)store)->map, &node->data_slice, NULL);
}

static void ts_rh_destroy(void *store) {
  ts_rh_store_t *s = (ts_rh_store_t *)store;
  if (s == NULL) {
    return;
  }
  ds_rh_map_destroy(&s->map);
  ts_free(s->owner, s);
}

static size_t ts_rh_size(void *store) {
  return ds_rh_map_size(&((ts_rh_store_t *)store)->map);
}

/* The map's stable order (ds_rh_map_after): by hash, unchanged by
 * insertions, removals and growth. Keys are slices, so finding after's
 * place looks at no other child. */
static ds_trie_node_t *ts_rh_next(void *store, const ds_trie_node_t *after) {
  void *value;
  if (store == NULL ||
      !ds_rh_map_after(&((ts_rh_store_t *)store)->map,
                       after != NULL ? (const void *)&after->data_slice : NULL,
                       NULL, &value)) {
    return NULL;
  }
  return *(ds_trie_node_t **)value;
}

static void ts_rh_apply(ds_trie_t *trie, void *store, trie_visit_t f,
                        va_list *args) {
  ts_apply(trie, store, f, args, ts_rh_next);
}

static void *ts_rh_clone(ds_trie_t *trie, void *store,
                         ds_trie_node_t *parent_node,
                         void *(*clone_data)(void *)) {
  return ds_trie_clone_store(trie, store, parent_node, clone_data, ts_rh_next);
}

static const ds_trie_store_ops_t ts_rh_ops = {
    ts_rh_search,  ts_rh_create, ts_rh_insert, ts_rh_remove, ts_rh_remove,
    ts_rh_destroy, ts_rh_size,   ts_rh_apply,  ts_rh_clone,  ts_rh_next};

const ds_trie_store_ops_t *ds_trie_rh_map_store_ops(void) { return &ts_rh_ops; }

/* ---- sorted vector store: children ordered by slice ---- */

typedef struct ts_vec_store {
  ds_vec_t children;
  ts_owner_t owner;
  size_t hint; /* where next() last answered; kept by insert and remove */
} ts_vec_store_t;

static int ts_by_slice(const void *a, const void *b, void *user) {
  size_t x = (*(ds_trie_node_t *const *)a)->data_slice;
  size_t y = (*(ds_trie_node_t *const *)b)->data_slice;
  (void)user;
  return (x > y) - (x < y);
}

static ds_status_t ts_vec_find(ts_vec_store_t *s, size_t slice,
                               size_t *out_index) {
  ds_trie_node_t probe;
  ds_trie_node_t *key = &probe;
  probe.data_slice = slice;
  return ds_vec_binary_search(&s->children, &key, ts_by_slice, NULL, out_index);
}

static ds_trie_node_t *ts_vec_search(void *store, size_t slice) {
  ts_vec_store_t *s = (ts_vec_store_t *)store;
  size_t index;
  if (ts_vec_find(s, slice, &index) != DS_OK) {
    return NULL;
  }
  return *(ds_trie_node_t **)ds_vec_at(&s->children, index);
}

static void *ts_vec_create(ds_trie_t *trie, size_t size) {
  ts_owner_t owner;
  ts_vec_store_t *s =
      (ts_vec_store_t *)ts_create_store(trie, sizeof(ts_vec_store_t), &owner);
  (void)size;
  if (s == NULL) {
    return NULL;
  }
  s->owner = owner;
  s->hint = 0;
  ds_vec_init(&s->children, sizeof(ds_trie_node_t *), owner.arrays);
  return s;
}

static void ts_vec_insert(void *store, ds_trie_node_t *node) {
  ts_vec_store_t *s = (ts_vec_store_t *)store;
  size_t index;
  if (ts_vec_find(s, node->data_slice, &index) == DS_OK) {
    ds_vec_set(&s->children, index, &node);
  } else {
    if (ds_vec_insert(&s->children, index, &node) == DS_OK &&
        index <= s->hint) {
      s->hint++;
    }
  }
}

static void ts_vec_remove(void *store, ds_trie_node_t *node) {
  ts_vec_store_t *s = (ts_vec_store_t *)store;
  size_t index;
  if (ts_vec_find(s, node->data_slice, &index) == DS_OK) {
    ds_vec_erase(&s->children, index, NULL);
    if (index < s->hint) {
      s->hint--;
    }
  }
}

static void ts_vec_destroy(void *store) {
  ts_vec_store_t *s = (ts_vec_store_t *)store;
  if (s == NULL) {
    return;
  }
  ds_vec_destroy(&s->children);
  ts_free(s->owner, s);
}

static size_t ts_vec_size(void *store) {
  return ds_vec_size(&((ts_vec_store_t *)store)->children);
}

static ds_trie_node_t *ts_vec_child(ts_vec_store_t *s, size_t i) {
  return *(ds_trie_node_t **)ds_vec_at(&s->children, i);
}

/* Slice order. after is found by address, at the hint (where the last
 * answer was, and where walks ask next) or by a scan: never by comparing
 * slices, since destruction leaves freed children in the vector. */
static ds_trie_node_t *ts_vec_next(void *store, const ds_trie_node_t *after) {
  ts_vec_store_t *s = (ts_vec_store_t *)store;
  size_t n;
  size_t i = 0;
  if (s == NULL) {
    return NULL;
  }
  n = ds_vec_size(&s->children);
  if (after != NULL) {
    if (s->hint < n && ts_vec_child(s, s->hint) == after) {
      i = s->hint + 1;
    } else {
      while (i < n && ts_vec_child(s, i) != after) {
        i++;
      }
      if (i == n) {
        return NULL;
      }
      i++;
    }
  }
  if (i >= n) {
    return NULL;
  }
  s->hint = i;
  return ts_vec_child(s, i);
}

static void ts_vec_apply(ds_trie_t *trie, void *store, trie_visit_t f,
                         va_list *args) {
  ts_apply(trie, store, f, args, ts_vec_next);
}

static void *ts_vec_clone(ds_trie_t *trie, void *store,
                          ds_trie_node_t *parent_node,
                          void *(*clone_data)(void *)) {
  return ds_trie_clone_store(trie, store, parent_node, clone_data, ts_vec_next);
}

static const ds_trie_store_ops_t ts_vec_ops = {
    ts_vec_search,  ts_vec_create, ts_vec_insert, ts_vec_remove, ts_vec_remove,
    ts_vec_destroy, ts_vec_size,   ts_vec_apply,  ts_vec_clone,  ts_vec_next};

const ds_trie_store_ops_t *ds_trie_sorted_vec_store_ops(void) {
  return &ts_vec_ops;
}
