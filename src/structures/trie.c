#include <dsa/structures/hash_table.h>
#include <dsa/structures/trie.h>
#include <stdbool.h>
#include <stdlib.h>

static ds_trie_t *trie_create_internal(
    size_t num_splits, ds_trie_node_t *(*store_search)(void *, size_t),
    void *(*store_create)(struct trie *, size_t),
    void (*store_insert)(void *, ds_trie_node_t *),
    void (*store_remove)(void *, ds_trie_node_t *),
    void (*store_destroy_entry)(void *, ds_trie_node_t *),
    void (*store_destroy)(void *), size_t (*store_get_size)(void *),
    void (*store_apply)(struct trie *, void *,
                        void (*)(struct trie *, ds_trie_node_t *, va_list *),
                        va_list *),
    void *(*store_clone)(struct trie *, void *, ds_trie_node_t *,
                         void *(*clone_data)(void *)),
    ds_trie_next_fn store_next, void *(*allocator)(size_t),
    void (*deallocator)(void *), ds_context_t *context);

static int trie_hash_table_compare_slice(void *a, void *b) {
  size_t slice_a;
  size_t slice_b;
  slice_a = (size_t)a;
  slice_b = (size_t)b;
  if (slice_a < slice_b) {
    return -1;
  }
  if (slice_a > slice_b) {
    return 1;
  }
  return 0;
}

static size_t trie_hash_table_hash_slice(void *key) {
  size_t slice;
  slice = (size_t)key;
  return ds_hash_func_size_t(&slice);
}

/* ---- walks over stores with next: no recursion ---- */

/* The first node of node's subtree in post-order: down through first
 * children. */
static ds_trie_node_t *trie_post_first(ds_trie_next_fn next,
                                       ds_trie_node_t *node) {
  ds_trie_node_t *child = next(node->children, NULL);
  while (child != NULL) {
    node = child;
    child = next(node->children, NULL);
  }
  return node;
}

/* The node after node in the post-order of top's subtree (top excluded),
 * or NULL. Asked before node is handed to a callback that may free it. */
static ds_trie_node_t *trie_post_next(ds_trie_next_fn next,
                                      const ds_trie_node_t *top,
                                      ds_trie_node_t *node) {
  ds_trie_node_t *parent = node->parent;
  ds_trie_node_t *sibling = next(parent->children, node);
  if (sibling != NULL) {
    return trie_post_first(next, sibling);
  }
  return parent == top ? NULL : parent;
}

/* The node after node in the pre-order of top's subtree (top excluded),
 * or NULL; node top gives the first. */
static ds_trie_node_t *trie_pre_next(ds_trie_next_fn next,
                                     const ds_trie_node_t *top,
                                     ds_trie_node_t *node) {
  ds_trie_node_t *child = next(node->children, NULL);
  if (child != NULL) {
    return child;
  }
  while (node != top) {
    ds_trie_node_t *sibling = next(node->parent->children, node);
    if (sibling != NULL) {
      return sibling;
    }
    node = node->parent;
  }
  return NULL;
}

void ds_trie_walk(ds_trie_t *trie, ds_trie_node_t *node, ds_trie_next_fn next,
                  void (*f)(ds_trie_t *, ds_trie_node_t *, va_list *),
                  va_list *args) {
  ds_trie_node_t *current;
  ds_trie_node_t *following;
  if (node == NULL || next == NULL || f == NULL) {
    return; /* trie is only passed on to f, and may be NULL */
  }
  current = trie_post_first(next, node);
  if (current == node) {
    return;
  }
  for (;;) {
    following = trie_post_next(next, node, current);
    if (args != NULL) {
      va_list copy;
      DS_VA_COPY(copy, *args);
      f(trie, current, &copy);
      va_end(copy);
    } else {
      f(trie, current, NULL);
    }
    if (following == NULL) {
      return;
    }
    current = following;
  }
}

ds_trie_node_t *ds_trie_preorder_next(ds_trie_t *trie, ds_trie_node_t *top,
                                      ds_trie_node_t *node) {
  if (trie == NULL || top == NULL || trie->store_next == NULL) {
    return NULL;
  }
  return trie_pre_next(trie->store_next, top, node != NULL ? node : top);
}

/* Free the nodes below node with their stores, in post-order, without
 * memory: the stores are not detached from, each node's successor is
 * found while the node is still alive. node's own store keeps the freed
 * entries; its owner destroys or resets it. */
static void trie_free_below(ds_trie_t *trie, ds_trie_next_fn next,
                            ds_trie_node_t *node) {
  ds_trie_node_t *current = trie_post_first(next, node);
  ds_trie_node_t *following;
  if (current == node) {
    return;
  }
  for (;;) {
    following = trie_post_next(next, node, current);
    trie->store_destroy(current->children);
    ds_trie_free(trie, current);
    if (following == NULL) {
      return;
    }
    current = following;
  }
}

/* A childless copy of from under parent, inserted and checked (store
 * insertions cannot report failure). */
static ds_trie_node_t *trie_copy_node(ds_trie_t *trie,
                                      const ds_trie_node_t *from,
                                      ds_trie_node_t *parent) {
  ds_trie_node_t *node = ds_trie_create_node(trie);
  if (node == NULL) {
    return NULL;
  }
  node->data_slice = from->data_slice;
  node->is_terminal = from->is_terminal;
  node->terminal_data = from->terminal_data;
  node->parent = parent;
  trie->store_insert(parent->children, node);
  if (trie->store_search(parent->children, node->data_slice) != node) {
    trie->store_destroy(node->children);
    ds_trie_free(trie, node);
    return NULL;
  }
  return node;
}

/* Copy the nodes below src under dst (whose store is empty), in pre-order
 * and without recursion, sharing their data. On failure the caller frees
 * what was added (trie_free_below on dst). */
static ds_status_t trie_copy_below(ds_trie_t *trie, ds_trie_next_fn next,
                                   const ds_trie_node_t *src,
                                   ds_trie_node_t *dst) {
  const ds_trie_node_t *from = src;
  ds_trie_node_t *to = dst;
  ds_trie_node_t *child = next(from->children, NULL);
  for (;;) {
    if (child != NULL) {
      ds_trie_node_t *copy = trie_copy_node(trie, child, to);
      if (copy == NULL) {
        return DS_ERR_ALLOC;
      }
      from = child;
      to = copy;
      child = next(from->children, NULL);
    } else {
      if (from == src) {
        return DS_OK;
      }
      child = next(from->parent->children, from);
      from = from->parent;
      to = to->parent;
    }
  }
}

/* Replace the data below top by clone_data's copies (once the structure
 * is complete, so a failed copy never holds cloned data). */
static void trie_clone_data_below(ds_trie_next_fn next, ds_trie_node_t *top,
                                  void *(*clone_data)(void *)) {
  ds_trie_node_t *node = trie_pre_next(next, top, top);
  while (node != NULL) {
    if (node->terminal_data != NULL) {
      node->terminal_data = clone_data(node->terminal_data);
    }
    node = trie_pre_next(next, top, node);
  }
}

void *ds_trie_clone_store(ds_trie_t *trie, void *store,
                          ds_trie_node_t *parent_node,
                          void *(*clone_data)(void *), ds_trie_next_fn next) {
  ds_trie_node_t *first;
  void *copy;
  void *saved;
  if (trie == NULL || store == NULL || parent_node == NULL || next == NULL) {
    return NULL;
  }
  copy = trie->store_create(trie, trie->num_splits);
  if (copy == NULL) {
    return NULL;
  }
  first = next(store, NULL);
  if (first == NULL) {
    return copy;
  }
  if (first->parent == parent_node) { /* the copy needs its own owner */
    trie->store_destroy(copy);
    return NULL;
  }
  saved = parent_node->children;
  parent_node->children = copy;
  if (trie_copy_below(trie, next, first->parent, parent_node) != DS_OK) {
    trie_free_below(trie, next, parent_node);
    parent_node->children = saved;
    trie->store_destroy(copy);
    return NULL;
  }
  if (clone_data != NULL) {
    trie_clone_data_below(next, parent_node, clone_data);
  }
  parent_node->children = saved;
  return copy;
}

void *ds_trie_alloc(ds_trie_t *trie, size_t size) {
  if (trie == NULL) {
    return NULL;
  }
  return trie->use_context ? ds_context_alloc(trie->context, size)
                           : trie->allocator(size);
}

void ds_trie_free(ds_trie_t *trie, void *ptr) {
  if (trie == NULL || ptr == NULL) {
    return;
  }
  if (trie->use_context) {
    ds_context_free(trie->context, ptr);
  } else {
    trie->deallocator(ptr);
  }
}

ds_trie_node_t *ds_trie_create_node(ds_trie_t *trie) {
  ds_trie_node_t *node;
  node = (ds_trie_node_t *)ds_trie_alloc(trie, sizeof(ds_trie_node_t));
  if (node == NULL) {
    return NULL;
  }
  node->children = trie->store_create(trie, trie->num_splits);
  if (node->children == NULL) {
    ds_trie_free(trie, node);
    return NULL;
  }
  node->data_slice = 0;
  node->is_terminal = false;
  node->terminal_data = NULL;
  node->parent = NULL;
  return node;
}

ds_trie_t *ds_trie_create_hash_alloc(size_t num_splits,
                                     void *(*allocator)(size_t),
                                     void (*deallocator)(void *)) {
  return ds_trie_create_alloc(
      num_splits, ds_trie_hash_table_store_search,
      ds_trie_hash_table_store_create, ds_trie_hash_table_store_insert,
      ds_trie_hash_table_store_remove, ds_trie_hash_table_store_destroy_entry,
      ds_trie_hash_table_store_destroy, ds_trie_hash_table_store_get_size,
      ds_trie_hash_table_store_apply, ds_trie_hash_table_store_clone, allocator,
      deallocator);
}

ds_trie_node_t *ds_trie_hash_table_store_search(void *store, size_t slice) {
  ds_hash_table_t *table;
  ds_trie_node_t *result;
  table = (ds_hash_table_t *)store;
  result = (ds_trie_node_t *)ds_hash_table_lookup(
      table, (void *)slice, trie_hash_table_hash_slice,
      trie_hash_table_compare_slice);
  if (result == table->nil) {
    return NULL;
  }
  return result;
}

void *ds_trie_hash_table_store_create(ds_trie_t *trie, size_t size) {
  if (trie->use_context) {
    ds_hash_table_config_t config;
    ds_hash_table_config_init(&config);
    config.capacity = size;
    config.context = trie->context;
    return (void *)ds_hash_table_create_config(&config);
  }
  return (void *)ds_hash_table_create_alloc(size, HASH_CHAINING, NULL,
                                            trie->allocator, trie->deallocator);
}

void ds_trie_hash_table_store_insert(void *store, ds_trie_node_t *node) {
  ds_hash_table_t *table;
  table = (ds_hash_table_t *)store;
  ds_hash_table_insert_with(table, (void *)node->data_slice, (void *)node,
                            trie_hash_table_hash_slice,
                            trie_hash_table_compare_slice);
}

void ds_trie_hash_table_store_remove(void *store, ds_trie_node_t *node) {
  ds_trie_hash_table_store_destroy_entry(store, node);
}

void ds_trie_hash_table_store_destroy_entry(void *store, ds_trie_node_t *node) {
  ds_hash_table_t *table;
  table = (ds_hash_table_t *)store;
  ds_hash_table_remove_with(table, (void *)node->data_slice,
                            trie_hash_table_hash_slice,
                            trie_hash_table_compare_slice, NULL);
}

void ds_trie_hash_table_store_destroy(void *store) {
  ds_hash_table_t *table;
  table = (ds_hash_table_t *)store;
  ds_hash_table_destroy_with(table, NULL);
}

size_t ds_trie_hash_table_store_get_size(void *store) {
  ds_hash_table_t *table;
  table = (ds_hash_table_t *)store;
  return table->size;
}

/* Newest child first: the table's list, which removals and resizing keep
 * in order (and insertions extend at the end already passed). The lookup
 * compares slices, never other children. */
ds_trie_node_t *ds_trie_hash_table_store_next(void *store,
                                              const ds_trie_node_t *after) {
  ds_hash_table_t *table;
  ds_hash_node_t *node;
  table = (ds_hash_table_t *)store;
  if (table == NULL) {
    return NULL;
  }
  if (after == NULL) {
    node = table->last_node;
  } else {
    node = ds_hash_table_lookup_node(table, (void *)after->data_slice,
                                     trie_hash_table_hash_slice,
                                     trie_hash_table_compare_slice);
    if (node == NULL) {
      return NULL;
    }
    node = node->list_prev;
  }
  return node != NULL ? (ds_trie_node_t *)node->value : NULL;
}

void ds_trie_hash_table_store_apply(ds_trie_t *trie, void *store,
                                    void (*f)(ds_trie_t *, ds_trie_node_t *,
                                              va_list *),
                                    va_list *args) {
  ds_trie_node_t *first;
  first = ds_trie_hash_table_store_next(store, NULL);
  if (first != NULL) {
    ds_trie_walk(trie, first->parent, ds_trie_hash_table_store_next, f, args);
  }
}

void *ds_trie_hash_table_store_clone(ds_trie_t *trie, void *store,
                                     ds_trie_node_t *parent_node,
                                     void *(*clone_data)(void *)) {
  return ds_trie_clone_store(trie, store, parent_node, clone_data,
                             ds_trie_hash_table_store_next);
}

ds_trie_t *ds_trie_create(
    size_t num_splits, ds_trie_node_t *(*store_search)(void *, size_t),
    void *(*store_create)(struct trie *, size_t),
    void (*store_insert)(void *, ds_trie_node_t *),
    void (*store_remove)(void *, ds_trie_node_t *),
    void (*store_destroy_entry)(void *, ds_trie_node_t *),
    void (*store_destroy)(void *), size_t (*store_get_size)(void *),
    void (*store_apply)(struct trie *, void *,
                        void (*)(struct trie *, ds_trie_node_t *, va_list *),
                        va_list *),
    void *(*store_clone)(struct trie *, void *, ds_trie_node_t *,
                         void *(*clone_data)(void *))) {
  return ds_trie_create_alloc(num_splits, store_search, store_create,
                              store_insert, store_remove, store_destroy_entry,
                              store_destroy, store_get_size, store_apply,
                              store_clone, malloc, free);
}

ds_trie_t *ds_trie_create_alloc(
    size_t num_splits, ds_trie_node_t *(*store_search)(void *, size_t),
    void *(*store_create)(struct trie *, size_t),
    void (*store_insert)(void *, ds_trie_node_t *),
    void (*store_remove)(void *, ds_trie_node_t *),
    void (*store_destroy_entry)(void *, ds_trie_node_t *),
    void (*store_destroy)(void *), size_t (*store_get_size)(void *),
    void (*store_apply)(struct trie *, void *,
                        void (*)(struct trie *, ds_trie_node_t *, va_list *),
                        va_list *),
    void *(*store_clone)(struct trie *, void *, ds_trie_node_t *,
                         void *(*clone_data)(void *)),
    void *(*allocator)(size_t), void (*deallocator)(void *)) {
  /* the built-in hash store has a next operation */
  ds_trie_next_fn next =
      store_search == ds_trie_hash_table_store_search &&
              store_insert == ds_trie_hash_table_store_insert &&
              store_remove == ds_trie_hash_table_store_remove &&
              store_apply == ds_trie_hash_table_store_apply
          ? ds_trie_hash_table_store_next
          : NULL;
  return trie_create_internal(num_splits, store_search, store_create,
                              store_insert, store_remove, store_destroy_entry,
                              store_destroy, store_get_size, store_apply,
                              store_clone, next, allocator, deallocator, NULL);
}

static ds_trie_t *trie_create_internal(
    size_t num_splits, ds_trie_node_t *(*store_search)(void *, size_t),
    void *(*store_create)(struct trie *, size_t),
    void (*store_insert)(void *, ds_trie_node_t *),
    void (*store_remove)(void *, ds_trie_node_t *),
    void (*store_destroy_entry)(void *, ds_trie_node_t *),
    void (*store_destroy)(void *), size_t (*store_get_size)(void *),
    void (*store_apply)(struct trie *, void *,
                        void (*)(struct trie *, ds_trie_node_t *, va_list *),
                        va_list *),
    void *(*store_clone)(struct trie *, void *, ds_trie_node_t *,
                         void *(*clone_data)(void *)),
    ds_trie_next_fn store_next, void *(*allocator)(size_t),
    void (*deallocator)(void *), ds_context_t *context) {
  ds_trie_t *trie;
  /* the context (if any) is in place before the root and its store */
  trie = context != NULL
             ? (ds_trie_t *)ds_context_alloc(context, sizeof(ds_trie_t))
             : (ds_trie_t *)allocator(sizeof(ds_trie_t));
  if (trie == NULL) {
    return NULL;
  }
  trie->allocator = allocator;
  trie->deallocator = deallocator;
  trie->use_context = context != NULL;
  trie->context = context != NULL ? context : ds_default_context();
  trie->num_splits = num_splits;
  trie->store_search = store_search;
  trie->store_create = store_create;
  trie->store_insert = store_insert;
  trie->store_remove = store_remove;
  trie->store_destroy_entry = store_destroy_entry;
  trie->store_destroy = store_destroy;
  trie->store_get_size = store_get_size;
  trie->store_apply = store_apply;
  trie->store_clone = store_clone;
  trie->store_next = store_next;
  trie->root = ds_trie_create_node(trie);
  if (trie->root == NULL) {
    ds_trie_free(trie, trie);
    return NULL;
  }
  return trie;
}

/* Release the nodes created by a failed insertion: first is the topmost
 * one (attached under its parent), last the deepest; each created node has
 * only the next one as child. */
static void trie_unwind(ds_trie_t *trie, ds_trie_node_t *first,
                        ds_trie_node_t *last) {
  ds_trie_node_t *cur = last;
  if (first == NULL) {
    return;
  }
  for (;;) {
    ds_trie_node_t *parent = cur->parent;
    bool is_first = cur == first;
    if (parent != NULL) {
      trie->store_remove(parent->children, cur);
    }
    trie->store_destroy(cur->children);
    ds_trie_free(trie, cur);
    if (is_first) {
      return;
    }
    cur = parent;
  }
}

ds_status_t ds_trie_insert_with(ds_trie_t *trie, void *data,
                                ds_trie_slice_fn slice, void *user,
                                void **out_previous) {
  ds_trie_node_t *current_node;
  ds_trie_node_t *result;
  ds_trie_node_t *new_node;
  ds_trie_node_t *first = NULL;
  size_t current_slice;
  size_t i;
  ds_status_t status;
  bool existed;
  if (trie == NULL || slice == NULL)
    return DS_ERR_NULL;
  current_node = trie->root;

  for (i = 0;; ++i) {
    status = slice(data, i, &current_slice, user);
    if (status == DS_STOP)
      break;
    if (status != DS_OK || i == (size_t)-1) {
      trie_unwind(trie, first, current_node);
      return status < 0 ? status
                        : (status == DS_OK ? DS_ERR_OVERFLOW : DS_ERR_CALLBACK);
    }
    result = trie->store_search(current_node->children, current_slice);

    if (result != NULL) {
      current_node = result;
    } else {
      new_node = ds_trie_create_node(trie);
      if (new_node == NULL) {
        trie_unwind(trie, first, current_node);
        return DS_ERR_ALLOC;
      }
      new_node->data_slice = current_slice;
      new_node->parent = current_node;
      trie->store_insert(current_node->children, new_node);
      /* store insertions cannot report failure: confirm the commit */
      if (trie->store_search(current_node->children, current_slice) !=
          new_node) {
        trie->store_destroy(new_node->children);
        ds_trie_free(trie, new_node);
        trie_unwind(trie, first, current_node);
        return DS_ERR_ALLOC;
      }
      if (first == NULL) {
        first = new_node;
      }
      current_node = new_node;
    }
  }

  existed = current_node->is_terminal;
  if (out_previous != NULL)
    *out_previous = existed ? current_node->terminal_data : NULL;
  current_node->is_terminal = true;
  current_node->terminal_data = data;
  return existed ? DS_EXISTS : DS_OK;
}

typedef struct trie_direct_slices {
  size_t (*get)(void *, size_t);
  bool (*has)(void *, size_t);
} trie_direct_slices_t;
static ds_status_t trie_slice_direct(void *data, size_t index, size_t *slice,
                                     void *user) {
  trie_direct_slices_t *policy = (trie_direct_slices_t *)user;
  if (!policy->has(data, index))
    return DS_STOP;
  *slice = policy->get(data, index);
  return DS_OK;
}
ds_trie_node_t *ds_trie_search(ds_trie_t *trie, void *data,
                               size_t (*get_slice)(void *, size_t),
                               bool (*has_slice)(void *, size_t)) {
  trie_direct_slices_t policy;
  ds_trie_node_t *out = NULL;
  if (get_slice == NULL || has_slice == NULL)
    return NULL;
  policy.get = get_slice;
  policy.has = has_slice;
  (void)ds_trie_get_with(trie, data, trie_slice_direct, &policy, &out);
  return out;
}

void ds_trie_delete_node(ds_trie_t *trie, ds_trie_node_t *node) {
  ds_trie_node_t *cur;
  ds_trie_node_t *temp;
  if (trie == NULL || node == NULL)
    return;
  if (!node->is_terminal) {
    return;
  }
  node->is_terminal = false;
  cur = node;
  while (cur != NULL && !cur->is_terminal) {
    if (trie->store_get_size(cur->children) == 0 && cur->parent != NULL) {
      trie->store_destroy_entry(cur->parent->children, cur);
      if (cur != trie->root) {
        temp = cur->parent;
        trie->store_destroy(cur->children);
        ds_trie_free(trie, cur);
        cur = temp;
        continue;
      }
    }
    cur = cur->parent;
  }
}

void ds_trie_destroy_node(ds_trie_t *trie, ds_trie_node_t *node,
                          void (*destroy)(ds_trie_t *, ds_trie_node_t *,
                                          va_list *)) {
  if (!node->is_terminal) {
    return;
  }
  destroy(trie, node, NULL);
  ds_trie_delete_node(trie, node);
}

void ds_trie_destroy_callback(ds_trie_t *trie, ds_trie_node_t *node,
                              va_list *args) {
  (void)args;
  trie->store_destroy(node->children);
  ds_trie_free(trie, node);
}

void ds_trie_delete_trie(ds_trie_t *trie) {
  if (trie->store_next != NULL) {
    trie_free_below(trie, trie->store_next, trie->root);
  } else {
    trie->store_apply(trie, trie->root->children, ds_trie_destroy_callback,
                      NULL);
  }
  trie->store_destroy(trie->root->children);
}

void ds_trie_destroy_trie(ds_trie_t *trie,
                          void (*destroy)(ds_trie_t *, ds_trie_node_t *,
                                          va_list *)) {
  if (trie->store_next != NULL) {
    /* every callback sees the whole trie; then the nodes go */
    if (destroy != NULL) {
      ds_trie_walk(trie, trie->root, trie->store_next, destroy, NULL);
    }
    trie_free_below(trie, trie->store_next, trie->root);
  } else {
    if (destroy != NULL) {
      trie->store_apply(trie, trie->root->children, destroy, NULL);
    }
    trie->store_apply(trie, trie->root->children, ds_trie_destroy_callback,
                      NULL);
  }
  trie->store_destroy(trie->root->children);
  if (destroy != NULL) {
    destroy(trie, trie->root, NULL);
  }
  ds_trie_free(trie, trie->root);
  ds_trie_free(trie, trie);
}

void ds_trie_apply(ds_trie_t *trie, ds_trie_node_t *node,
                   void (*f)(ds_trie_t *, ds_trie_node_t *, va_list *), ...) {
  va_list args;
  va_start(args, f);
  if (trie->store_next != NULL) {
    ds_trie_walk(trie, node, trie->store_next, f, &args);
  } else {
    trie->store_apply(trie, node->children, f, &args);
  }
  va_end(args);
}

ds_trie_node_t *ds_trie_clone_node(ds_trie_t *trie, ds_trie_node_t *node,
                                   ds_trie_node_t *parent_node,
                                   void *(*clone_data)(void *)) {
  ds_trie_node_t *new_node;
  if (trie->store_next != NULL) {
    /* the whole subtree or nothing, without recursion */
    new_node = ds_trie_create_node(trie);
    if (new_node == NULL) {
      return NULL;
    }
    new_node->data_slice = node->data_slice;
    new_node->is_terminal = node->is_terminal;
    new_node->terminal_data = node->terminal_data;
    new_node->parent = parent_node;
    if (trie_copy_below(trie, trie->store_next, node, new_node) != DS_OK) {
      trie_free_below(trie, trie->store_next, new_node);
      trie->store_destroy(new_node->children);
      ds_trie_free(trie, new_node);
      return NULL;
    }
    if (clone_data != NULL) {
      if (new_node->terminal_data != NULL) {
        new_node->terminal_data = clone_data(new_node->terminal_data);
      }
      trie_clone_data_below(trie->store_next, new_node, clone_data);
    }
    return new_node;
  }
  /* a store without next copies itself, calling back here per child */
  new_node = (ds_trie_node_t *)ds_trie_alloc(trie, sizeof(ds_trie_node_t));
  if (new_node == NULL) {
    return NULL;
  }
  new_node->data_slice = node->data_slice;
  new_node->is_terminal = node->is_terminal;
  new_node->terminal_data = node->terminal_data;
  new_node->parent = parent_node;
  new_node->children = NULL; /* store_clone may save and restore it */
  new_node->children =
      trie->store_clone(trie, node->children, new_node, clone_data);
  if (new_node->children == NULL) {
    ds_trie_free(trie, new_node);
    return NULL;
  }
  if (clone_data != NULL && new_node->terminal_data != NULL) {
    new_node->terminal_data = clone_data(new_node->terminal_data);
  }
  return new_node;
}

ds_trie_t *ds_trie_clone(ds_trie_t *trie, void *(*clone_data)(void *)) {
  ds_trie_t *new_trie;
  if (trie == NULL) {
    return NULL;
  }
  new_trie = (ds_trie_t *)ds_trie_alloc(trie, sizeof(ds_trie_t));
  if (new_trie == NULL) {
    return NULL;
  }
  *new_trie = *trie;
  new_trie->root = ds_trie_clone_node(trie, trie->root, NULL, clone_data);
  if (new_trie->root == NULL) {
    ds_trie_free(trie, new_trie);
    return NULL;
  }
  return new_trie;
}

static const ds_trie_store_ops_t ds_trie_hash_ops = {
    ds_trie_hash_table_store_search,        ds_trie_hash_table_store_create,
    ds_trie_hash_table_store_insert,        ds_trie_hash_table_store_remove,
    ds_trie_hash_table_store_destroy_entry, ds_trie_hash_table_store_destroy,
    ds_trie_hash_table_store_get_size,      ds_trie_hash_table_store_apply,
    ds_trie_hash_table_store_clone,         ds_trie_hash_table_store_next};

const ds_trie_store_ops_t *ds_trie_hash_store_ops(void) {
  return &ds_trie_hash_ops;
}

void ds_trie_config_init(ds_trie_config_t *config) {
  if (config == NULL) {
    return;
  }
  config->num_splits = 256U;
  config->store_ops = NULL;
  config->allocator = NULL;
  config->deallocator = NULL;
  config->context = NULL;
}

ds_trie_t *ds_trie_create_config(const ds_trie_config_t *config) {
  const ds_trie_store_ops_t *ops;
  void *(*allocator)(size_t);
  void (*deallocator)(void *);
  ds_trie_t *trie;

  if (config == NULL) {
    return NULL;
  }
  ops =
      config->store_ops != NULL ? config->store_ops : ds_trie_hash_store_ops();
  allocator = config->allocator != NULL ? config->allocator : malloc;
  deallocator = config->deallocator != NULL ? config->deallocator : free;

  trie = trie_create_internal(
      config->num_splits, ops->search, ops->create, ops->insert, ops->remove,
      ops->destroy_entry, ops->destroy, ops->get_size, ops->apply, ops->clone,
      ops->next, allocator, deallocator,
      config->allocator == NULL ? config->context : NULL);
  if (trie != NULL && !trie->use_context && config->context != NULL) {
    trie->context = config->context; /* diagnostics only */
  }
  return trie;
}

ds_trie_t *ds_trie_create_hash(size_t num_splits) {
  ds_trie_config_t config;
  ds_trie_config_init(&config);
  config.num_splits = num_splits;
  config.store_ops = ds_trie_hash_store_ops();
  return ds_trie_create_config(&config);
}

ds_status_t ds_trie_insert(ds_trie_t *trie, void *data,
                           size_t (*get_slice)(void *, size_t),
                           bool (*has_slice)(void *, size_t)) {
  trie_direct_slices_t policy;
  ds_status_t status;
  if (trie == NULL || get_slice == NULL || has_slice == NULL) {
    return DS_CONTEXT_ERROR(trie != NULL ? trie->context : NULL, DS_ERR_NULL,
                            "ds.trie/null_argument", "trie",
                            "trie insert received NULL argument");
  }
  policy.get = get_slice;
  policy.has = has_slice;
  status = ds_trie_insert_with(trie, data, trie_slice_direct, &policy, NULL);
  return status == DS_EXISTS ? DS_OK : status;
}

ds_status_t ds_trie_get(ds_trie_t *trie, void *data,
                        size_t (*get_slice)(void *, size_t),
                        bool (*has_slice)(void *, size_t),
                        ds_trie_node_t **out_node) {
  ds_trie_node_t *node;
  if (out_node != NULL) {
    *out_node = NULL;
  }
  if (trie == NULL || get_slice == NULL || has_slice == NULL ||
      out_node == NULL) {
    return DS_CONTEXT_ERROR(trie != NULL ? trie->context : NULL, DS_ERR_NULL,
                            "ds.trie/null_argument", "trie",
                            "trie get received NULL argument");
  }
  node = ds_trie_search(trie, data, get_slice, has_slice);
  if (node == NULL) {
    return DS_NOT_FOUND;
  }
  *out_node = node;
  return DS_OK;
}

ds_status_t ds_trie_remove(ds_trie_t *trie, void *data,
                           size_t (*get_slice)(void *, size_t),
                           bool (*has_slice)(void *, size_t)) {
  ds_trie_node_t *node;
  ds_status_t status;
  status = ds_trie_get(trie, data, get_slice, has_slice, &node);
  if (status != DS_OK) {
    return status;
  }
  ds_trie_delete_node(trie, node);
  return DS_OK;
}

void ds_trie_destroy(ds_trie_t *trie) {
  if (trie == NULL) {
    return;
  }
  ds_trie_destroy_trie(trie, NULL);
}

ds_status_t ds_trie_create_config_ex(const ds_trie_config_t *config,
                                     ds_trie_t **out) {
  ds_trie_config_t normalized;
  const ds_trie_store_ops_t *ops;
  if (out == NULL)
    return DS_ERR_NULL;
  *out = NULL;
  if (config == NULL)
    return DS_ERR_NULL;
  normalized = *config;
  if ((config->allocator == NULL) != (config->deallocator == NULL))
    return DS_ERR_INVALID;
  ops =
      config->store_ops != NULL ? config->store_ops : ds_trie_hash_store_ops();
  if (ops->search == NULL || ops->create == NULL || ops->insert == NULL ||
      ops->remove == NULL || ops->destroy_entry == NULL ||
      ops->destroy == NULL || ops->get_size == NULL ||
      (ops->next == NULL && (ops->apply == NULL || ops->clone == NULL)))
    return DS_ERR_INVALID;
  if (normalized.allocator == NULL && normalized.context == NULL)
    normalized.context = ds_default_context();
  *out = ds_trie_create_config(&normalized);
  return *out != NULL ? DS_OK : DS_ERR_ALLOC;
}
ds_status_t ds_trie_get_with(ds_trie_t *trie, void *data,
                             ds_trie_slice_fn slice, void *user,
                             ds_trie_node_t **out) {
  ds_trie_node_t *node;
  size_t index, part;
  ds_status_t status;
  if (trie == NULL || slice == NULL || out == NULL)
    return DS_ERR_NULL;
  node = trie->root;
  for (index = 0;; ++index) {
    status = slice(data, index, &part, user);
    if (status == DS_STOP)
      break;
    if (status != DS_OK)
      return status < 0 ? status : DS_ERR_CALLBACK;
    if (index == (size_t)-1)
      return DS_ERR_OVERFLOW;
    node = trie->store_search(node->children, part);
    if (node == NULL)
      return DS_NOT_FOUND;
  }
  if (!node->is_terminal)
    return DS_NOT_FOUND;
  *out = node;
  return DS_OK;
}
ds_status_t ds_trie_remove_with(ds_trie_t *trie, void *data,
                                ds_trie_slice_fn slice, void *user,
                                void **out_data) {
  ds_trie_node_t *node;
  ds_status_t status = ds_trie_get_with(trie, data, slice, user, &node);
  if (status != DS_OK)
    return status;
  if (out_data != NULL)
    *out_data = node->terminal_data;
  ds_trie_delete_node(trie, node);
  return DS_OK;
}
ds_status_t ds_trie_walk_with(ds_trie_t *trie, ds_trie_node_t *top,
                              bool postorder, ds_trie_visit_fn visit,
                              void *user) {
  ds_trie_node_t *node;
  ds_status_t status;
  if (trie == NULL || top == NULL || visit == NULL)
    return DS_ERR_NULL;
  if (trie->store_next == NULL)
    return DS_ERR_UNSUPPORTED;
  node = postorder ? trie_post_first(trie->store_next, top) : top;
  while (node != NULL) {
    status = visit(trie, node, user);
    if (status != DS_OK)
      return status;
    if (postorder) {
      if (node == top)
        break;
      node = trie_post_next(trie->store_next, top, node);
      if (node == NULL)
        node = top;
    } else
      node = trie_pre_next(trie->store_next, top, node);
  }
  return DS_OK;
}
ds_status_t ds_trie_destroy_with(ds_trie_t *trie,
                                 ds_trie_data_destroy_fn destroy, void *user) {
  ds_trie_node_t *node;
  if (trie == NULL)
    return DS_OK;
  if (trie->store_next == NULL)
    return DS_ERR_UNSUPPORTED;
  if (destroy != NULL) {
    for (node = trie->root; node != NULL;
         node = trie_pre_next(trie->store_next, trie->root, node))
      if (node->is_terminal)
        destroy(node->terminal_data, user);
  }
  ds_trie_destroy(trie);
  return DS_OK;
}
ds_status_t ds_trie_clone_with(ds_trie_t *trie, ds_trie_clone_fn clone,
                               ds_trie_data_destroy_fn destroy, void *user,
                               const ds_trie_config_t *config,
                               ds_trie_t **out) {
  ds_trie_config_t inherited;
  ds_trie_store_ops_t ops;
  ds_trie_t *copy;
  ds_trie_node_t *node, *done;
  void *data;
  ds_status_t status;
  if (out == NULL)
    return DS_ERR_NULL;
  *out = NULL;
  if (trie == NULL)
    return DS_ERR_NULL;
  if ((clone == NULL) != (destroy == NULL))
    return DS_ERR_INVALID;
  if (trie->store_next == NULL)
    return DS_ERR_UNSUPPORTED;
  if (config == NULL) {
    ds_trie_config_init(&inherited);
    inherited.num_splits = trie->num_splits;
    inherited.allocator = trie->use_context ? NULL : trie->allocator;
    inherited.deallocator = trie->use_context ? NULL : trie->deallocator;
    inherited.context = trie->context;
    ops.search = trie->store_search;
    ops.create = trie->store_create;
    ops.insert = trie->store_insert;
    ops.remove = trie->store_remove;
    ops.destroy_entry = trie->store_destroy_entry;
    ops.destroy = trie->store_destroy;
    ops.get_size = trie->store_get_size;
    ops.apply = trie->store_apply;
    ops.clone = trie->store_clone;
    ops.next = trie->store_next;
    inherited.store_ops = &ops;
    config = &inherited;
  }
  if (config->store_ops != NULL && config->store_ops->next == NULL)
    return DS_ERR_UNSUPPORTED;
  status = ds_trie_create_config_ex(config, &copy);
  if (status != DS_OK)
    return status;
  copy->root->data_slice = trie->root->data_slice;
  copy->root->is_terminal = trie->root->is_terminal;
  copy->root->terminal_data = trie->root->terminal_data;
  status = trie_copy_below(copy, trie->store_next, trie->root, copy->root);
  if (status != DS_OK) {
    ds_trie_destroy(copy);
    return status;
  }
  if (clone != NULL) {
    for (node = copy->root; node != NULL;
         node = trie_pre_next(copy->store_next, copy->root, node)) {
      if (!node->is_terminal)
        continue;
      data = NULL;
      status = clone(node->terminal_data, &data, user);
      if (status != DS_OK) {
        for (done = copy->root; done != node;
             done = trie_pre_next(copy->store_next, copy->root, done))
          if (done->is_terminal)
            destroy(done->terminal_data, user);
        ds_trie_destroy(copy);
        return status < 0 ? status : DS_ERR_CALLBACK;
      }
      node->terminal_data = data;
    }
  }
  *out = copy;
  return DS_OK;
}
