#include "trie.h"
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

ds_trie_node_t *FUNC(trie_create_node)(ds_trie_t *trie) {
  ds_trie_node_t *node = (ds_trie_node_t *)trie->allocator(sizeof(ds_trie_node_t));
  node->children = trie->store_create(trie->num_splits);

  node->data_slice = 0;
  node->is_terminal = false;
  node->terminal_data = NULL;
  node->parent = NULL;
  return node;
}

ds_trie_t *FUNC(trie_create)(
    size_t num_splits, ds_trie_node_t *(*store_search)(void *, size_t),
    void *(*store_create)(size_t), void (*store_insert)(void *, ds_trie_node_t *),
    void (*store_remove)(void *, ds_trie_node_t *),
    void (*store_destroy_entry)(void *, ds_trie_node_t *),
    void (*store_destroy)(void *), size_t (*store_get_size)(void *),
    void (*store_apply)(struct trie *, void *,
                        void (*)(struct trie *, ds_trie_node_t *, va_list *),
                        va_list *),
    void *(*store_clone)(struct trie *, void *, ds_trie_node_t *)) {
  return FUNC(trie_create_alloc)(
      num_splits, store_search, store_create, store_insert, store_remove,
      store_destroy_entry, store_destroy, store_get_size, store_apply,
      store_clone, malloc, free);
}

ds_trie_t *FUNC(trie_create_alloc)(
    size_t num_splits, ds_trie_node_t *(*store_search)(void *, size_t),
    void *(*store_create)(size_t), void (*store_insert)(void *, ds_trie_node_t *),
    void (*store_remove)(void *, ds_trie_node_t *),
    void (*store_destroy_entry)(void *, ds_trie_node_t *),
    void (*store_destroy)(void *), size_t (*store_get_size)(void *),
    void (*store_apply)(struct trie *, void *,
                        void (*)(struct trie *, ds_trie_node_t *, va_list *),
                        va_list *),
    void *(*store_clone)(struct trie *, void *, ds_trie_node_t *),
    void *(*allocator)(size_t), void (*deallocator)(void *)) {
  ds_trie_t *trie = (ds_trie_t *)allocator(sizeof(ds_trie_t));
  trie->allocator = allocator;
  trie->deallocator = deallocator;
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
  trie->root = FUNC(trie_create_node)(trie);
#ifdef DS_THREAD_SAFE
  LOCK_INIT_RECURSIVE(trie)
#endif
  return trie;
}

void FUNC(trie_insert)(ds_trie_t *trie, void *data,
                       size_t (*get_slice)(void *, size_t),
                       bool (*has_slice)(void *, size_t)) {
  ds_trie_node_t *current_node;
  ds_trie_node_t *result;
  size_t current_slice;
  size_t i;
#ifdef DS_THREAD_SAFE
  LOCK(trie)
#endif
  current_node = trie->root;

  for (i = 0; has_slice(data, i); i++) {
    current_slice = get_slice(data, i);
    result = trie->store_search(current_node->children, current_slice);

    if (result != NULL) {
      current_node = result;
    } else {
      ds_trie_node_t *new_node = FUNC(trie_create_node)(trie);
      new_node->data_slice = current_slice;
      new_node->parent = current_node;
      trie->store_insert(current_node->children, new_node);
      current_node = new_node;
    }
  }

  current_node->is_terminal = true;
  current_node->terminal_data = data;
#ifdef DS_THREAD_SAFE
  UNLOCK(trie)
#endif
}

ds_trie_node_t *FUNC(trie_search)(ds_trie_t *trie, void *data,
                               size_t (*get_slice)(void *, size_t),
                               bool (*has_slice)(void *, size_t)) {
  ds_trie_node_t *current_node;
  ds_trie_node_t *result;
  size_t current_slice;
  size_t i;
#ifdef DS_THREAD_SAFE
  LOCK(trie)
#endif
  if (trie->root == NULL) {
    return NULL;
  };
  current_node = trie->root;

  for (i = 0; has_slice(data, i); i++) {
    current_slice = get_slice(data, i);
    result = trie->store_search(current_node->children, current_slice);

    if (result == NULL) {
#ifdef DS_THREAD_SAFE
      UNLOCK(trie)
#endif
      return NULL;
    }
    current_node = result;
  }
  result = current_node->is_terminal ? current_node : NULL;

#ifdef DS_THREAD_SAFE
  UNLOCK(trie)
#endif
  return result;
}

void FUNC(trie_delete_node)(ds_trie_t *trie, ds_trie_node_t *node) {
  ds_trie_node_t *cur = node;
  ds_trie_node_t *temp;
  if (!node->is_terminal) {
    return;
  }
  node->is_terminal = false;
  while (cur != NULL && !cur->is_terminal) {
    if (trie->store_get_size(cur->children) == 0 && cur->parent != NULL) {
      trie->store_destroy_entry(cur->parent->children, cur);
      if (cur != trie->root) {
        temp = cur->parent;
        trie->store_destroy(cur->children);
        trie->deallocator(cur);
        cur = temp;
        continue;
      }
    }
    cur = cur->parent;
  }
}

void FUNC(trie_destroy_node)(ds_trie_t *trie, ds_trie_node_t *node,
                             void (*destroy)(ds_trie_t *, ds_trie_node_t *,
                                             va_list *)) {
  if (!node->is_terminal) {
    return;
  }
  destroy(trie, node, NULL);
  FUNC(trie_delete_node)(trie, node);
}

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
void FUNC(trie_destroy_callback)(ds_trie_t *trie, ds_trie_node_t *node,
                                 va_list *args) {
  trie->store_destroy(node->children);
  trie->deallocator(node);
}
#pragma GCC diagnostic pop

void FUNC(trie_delete_trie)(ds_trie_t *trie) {
  trie->store_apply(trie, trie->root->children, FUNC(trie_destroy_callback),
                    NULL);
  trie->store_destroy(trie->root->children);
}

void FUNC(trie_destroy_trie)(ds_trie_t *trie,
                             void (*destroy)(ds_trie_t *, ds_trie_node_t *,
                                             va_list *)) {
  if (destroy != NULL) {
    trie->store_apply(trie, trie->root->children, destroy, NULL);
  }
  trie->store_apply(trie, trie->root->children, FUNC(trie_destroy_callback),
                    NULL);
  trie->store_destroy(trie->root->children);
  if (destroy != NULL) {
    destroy(trie, trie->root, NULL);
  }
  trie->deallocator(trie->root);
  trie->deallocator(trie);
}

void FUNC(trie_apply)(ds_trie_t *trie, ds_trie_node_t *node,
                      void (*f)(ds_trie_t *, ds_trie_node_t *, va_list *), ...) {
  va_list args;
  va_start(args, f);

  trie->store_apply(trie, node->children, f, &args);
  va_end(args);
}

ds_trie_node_t *FUNC(trie_clone_node)(ds_trie_t *trie, ds_trie_node_t *node,
                                   ds_trie_node_t *parent_node,
                                   void *(*clone_data)(void *)) {

  ds_trie_node_t *new_node = (ds_trie_node_t *)trie->allocator(sizeof(ds_trie_node_t));
  new_node->data_slice = node->data_slice;
  new_node->is_terminal = node->is_terminal;
  new_node->terminal_data = clone_data(node->terminal_data);
  new_node->parent = parent_node;

  new_node->children = trie->store_clone(trie, node->children, new_node);

  return new_node;
}

ds_trie_t *FUNC(trie_clone)(ds_trie_t *trie, void *(*clone_data)(void *)) {
  ds_trie_t *new_trie = (ds_trie_t *)trie->allocator(sizeof(ds_trie_t));
  new_trie->num_splits = trie->num_splits;
  new_trie->store_search = trie->store_search;
  new_trie->store_create = trie->store_create;
  new_trie->store_insert = trie->store_insert;
  new_trie->store_remove = trie->store_remove;
  new_trie->store_destroy_entry = trie->store_destroy_entry;
  new_trie->store_destroy = trie->store_destroy;
  new_trie->store_get_size = trie->store_get_size;
  new_trie->store_apply = trie->store_apply;
  new_trie->store_clone = trie->store_clone;
  new_trie->allocator = trie->allocator;
  new_trie->deallocator = trie->deallocator;

  new_trie->root = FUNC(trie_clone_node)(trie, trie->root, NULL, clone_data);

  return new_trie;
}
