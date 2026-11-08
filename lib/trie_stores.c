#include "trie_stores.h"
#include "rh_map.h"
#include "vec.h"
#include <stdarg.h>
#include <stdlib.h>

typedef void (*trie_visit_t)(ds_trie_t *, ds_trie_node_t *, va_list *);
typedef void (*trie_apply_t)(ds_trie_t *, void *, trie_visit_t, va_list *);

static void *ts_alloc(ds_trie_t *trie, size_t size) {
  if (trie != NULL && (trie->use_context || trie->allocator != NULL)) {
    return ds_trie_alloc(trie, size);
  }
  return malloc(size);
}

/* Post-order visit of one child: its subtree first, then the child. */
static void ts_visit(ds_trie_t *trie, ds_trie_node_t *child, trie_visit_t f,
                     va_list *args, trie_apply_t apply) {
  va_list subtree;
  va_list self;
  if (args == NULL) {
    apply(trie, child->children, f, NULL);
    f(trie, child, NULL);
    return;
  }
  DS_VA_COPY(subtree, *args);
  DS_VA_COPY(self, *args);
  apply(trie, child->children, f, &subtree);
  f(trie, child, &self);
  va_end(subtree);
  va_end(self);
}

/* ---- Robin Hood map store: slice -> child ---- */

/* Each store remembers how it was allocated (a context, else a
 * deallocator): a cloned store may outlive the trie it was created
 * through. */
typedef struct ts_owner {
  void (*deallocator)(void *);
  ds_context_t *context; /* when the trie allocates from its context */
} ts_owner_t;

static ts_owner_t ts_owner_of(const ds_trie_t *trie) {
  ts_owner_t o;
  o.deallocator = trie != NULL ? trie->deallocator : NULL;
  o.context = trie != NULL && trie->use_context ? trie->context : NULL;
  return o;
}

static ds_context_t *ts_scratch_context(const ds_trie_t *trie) {
  return trie != NULL ? trie->context : NULL;
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

static void ts_rh_apply(ds_trie_t *trie, void *store, trie_visit_t f,
                        va_list *args);

static ds_trie_node_t *ts_rh_search(void *store, size_t slice) {
  ds_trie_node_t **child =
      (ds_trie_node_t **)ds_rh_map_get(&((ts_rh_store_t *)store)->map, &slice);
  return child == NULL ? NULL : *child;
}

static void *ts_rh_create(ds_trie_t *trie, size_t size) {
  ts_rh_store_t *s = (ts_rh_store_t *)ts_alloc(trie, sizeof(ts_rh_store_t));
  ds_rh_map_config_t config;
  (void)size; /* grow on demand: most trie nodes have few children */
  if (s == NULL) {
    return NULL;
  }
  s->owner = ts_owner_of(trie);
  ds_rh_map_config_init(&config);
  config.key_size = sizeof(size_t);
  config.value_size = sizeof(ds_trie_node_t *);
  config.hash = ds_rh_map_hash_size;
  config.equal = ds_rh_map_equal_size;
  config.context = trie != NULL ? trie->context : NULL;
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

static void ts_rh_apply(ds_trie_t *trie, void *store, trie_visit_t f,
                        va_list *args) {
  ds_rh_map_t *map = store == NULL ? NULL : &((ts_rh_store_t *)store)->map;
  ds_vec_t snapshot;
  size_t cursor = 0;
  size_t i;
  void *value;
  if (map == NULL || ds_vec_init(&snapshot, sizeof(ds_trie_node_t *),
                                 ts_scratch_context(trie)) != DS_OK) {
    return;
  }
  while (ds_rh_map_next(map, &cursor, NULL, &value)) {
    ds_vec_push(&snapshot, value);
  }
  for (i = 0; i < ds_vec_size(&snapshot); i++) {
    ts_visit(trie, *(ds_trie_node_t **)ds_vec_at(&snapshot, i), f, args,
             ts_rh_apply);
  }
  ds_vec_destroy(&snapshot);
}

static void *ts_rh_clone(ds_trie_t *trie, void *store,
                         ds_trie_node_t *parent_node,
                         void *(*clone_data)(void *)) {
  ds_rh_map_t *map = &((ts_rh_store_t *)store)->map;
  void *copy = ts_rh_create(trie, ds_rh_map_size(map));
  size_t cursor = 0;
  void *value;
  if (copy == NULL) {
    return NULL;
  }
  while (ds_rh_map_next(map, &cursor, NULL, &value)) {
    ts_rh_insert(copy, ds_trie_clone_node(trie, *(ds_trie_node_t **)value,
                                          parent_node, clone_data));
  }
  return copy;
}

static const ds_trie_store_ops_t ts_rh_ops = {
    ts_rh_search,  ts_rh_create, ts_rh_insert, ts_rh_remove, ts_rh_remove,
    ts_rh_destroy, ts_rh_size,   ts_rh_apply,  ts_rh_clone};

const ds_trie_store_ops_t *ds_trie_rh_map_store_ops(void) { return &ts_rh_ops; }

/* ---- sorted vector store: children ordered by slice ---- */

typedef struct ts_vec_store {
  ds_vec_t children;
  ts_owner_t owner;
} ts_vec_store_t;

static void ts_vec_apply(ds_trie_t *trie, void *store, trie_visit_t f,
                         va_list *args);

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
  ts_vec_store_t *s = (ts_vec_store_t *)ts_alloc(trie, sizeof(ts_vec_store_t));
  (void)size;
  if (s == NULL) {
    return NULL;
  }
  s->owner = ts_owner_of(trie);
  ds_vec_init(&s->children, sizeof(ds_trie_node_t *),
              trie != NULL ? trie->context : NULL);
  return s;
}

static void ts_vec_insert(void *store, ds_trie_node_t *node) {
  ts_vec_store_t *s = (ts_vec_store_t *)store;
  size_t index;
  if (ts_vec_find(s, node->data_slice, &index) == DS_OK) {
    ds_vec_set(&s->children, index, &node);
  } else {
    ds_vec_insert(&s->children, index, &node);
  }
}

static void ts_vec_remove(void *store, ds_trie_node_t *node) {
  ts_vec_store_t *s = (ts_vec_store_t *)store;
  size_t index;
  if (ts_vec_find(s, node->data_slice, &index) == DS_OK) {
    ds_vec_erase(&s->children, index, NULL);
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

static void ts_vec_apply(ds_trie_t *trie, void *store, trie_visit_t f,
                         va_list *args) {
  ts_vec_store_t *s = (ts_vec_store_t *)store;
  ds_vec_t snapshot;
  size_t i;
  if (s == NULL ||
      ds_vec_copy(&snapshot, &s->children, ts_scratch_context(trie)) != DS_OK) {
    return;
  }
  for (i = 0; i < ds_vec_size(&snapshot); i++) {
    ts_visit(trie, *(ds_trie_node_t **)ds_vec_at(&snapshot, i), f, args,
             ts_vec_apply);
  }
  ds_vec_destroy(&snapshot);
}

static void *ts_vec_clone(ds_trie_t *trie, void *store,
                          ds_trie_node_t *parent_node,
                          void *(*clone_data)(void *)) {
  ts_vec_store_t *s = (ts_vec_store_t *)store;
  ts_vec_store_t *copy = (ts_vec_store_t *)ts_vec_create(trie, 0);
  ds_trie_node_t *child;
  size_t i;
  if (copy == NULL) {
    return NULL;
  }
  ds_vec_reserve(&copy->children, ds_vec_size(&s->children));
  for (i = 0; i < ds_vec_size(&s->children); i++) {
    child =
        ds_trie_clone_node(trie, *(ds_trie_node_t **)ds_vec_at(&s->children, i),
                           parent_node, clone_data);
    ds_vec_push(&copy->children, &child);
  }
  return copy;
}

static const ds_trie_store_ops_t ts_vec_ops = {
    ts_vec_search,  ts_vec_create, ts_vec_insert, ts_vec_remove, ts_vec_remove,
    ts_vec_destroy, ts_vec_size,   ts_vec_apply,  ts_vec_clone};

const ds_trie_store_ops_t *ds_trie_sorted_vec_store_ops(void) {
  return &ts_vec_ops;
}
