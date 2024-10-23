#include "trie.h"
#include <stdbool.h>
#include <stdlib.h>

trie_node_t *trie_create_node(trie_t *trie) {
  trie_node_t *node = (trie_node_t *)malloc(sizeof(trie_node_t));
  node->children = trie->store_create(trie->num_splits);

  node->data_slice = 0;
  node->is_terminal = false;
  node->terminal_data = NULL;
  node->parent = NULL;
  return node;
}

trie_t *trie_create(
    size_t num_splits, trie_node_t *(*store_search)(void *store, size_t slice),
    void *(*store_create)(size_t size),
    void (*store_insert)(void *store, trie_node_t *node),
    void (*store_remove)(void *store, trie_node_t *node),
    void (*store_destroy_entry)(void *store, trie_node_t *node),
    void (*store_destroy)(void *store), size_t (*store_get_size)(void *store),
    void (*store_apply)(struct trie *, void *store,
                        void (*f)(struct trie *, trie_node_t *))) {
  trie_t *trie = (trie_t *)malloc(sizeof(trie_t));
  trie->num_splits = num_splits;
  trie->store_search = store_search;
  trie->store_create = store_create;
  trie->store_insert = store_insert;
  trie->store_remove = store_remove;
  trie->store_destroy_entry = store_destroy_entry;
  trie->store_destroy = store_destroy;
  trie->store_get_size = store_get_size;
  trie->store_apply = store_apply;
  trie->root = trie_create_node(trie);
#ifdef TRIE_THREAD_SAFE
  LOCK_INIT_RECURSIVE(trie)
#endif
  return trie;
}

void trie_insert(trie_t *trie, void *data,
                 size_t (*get_slice)(void *data, size_t slice),
                 bool (*has_slice)(void *data, size_t slice)) {
#ifdef TRIE_THREAD_SAFE
  LOCK(trie)
#endif
  trie_node_t *current_node = trie->root;
  trie_node_t *result;
  size_t current_slice;
  int i;

  for (i = 0; has_slice(data, i); i++) {
    current_slice = get_slice(data, i);
    result = trie->store_search(current_node->children, current_slice);

    if (result != NULL) {
      current_node = result;
    } else {
      trie_node_t *new_node = trie_create_node(trie);
      new_node->data_slice = current_slice;
      new_node->parent = current_node;
      trie->store_insert(current_node->children, new_node);
      current_node = new_node;
    }
  }

  current_node->is_terminal = true;
  current_node->terminal_data = data;
#ifdef TRIE_THREAD_SAFE
  UNLOCK(trie)
#endif
}

trie_node_t *trie_search(trie_t *trie, void *data,
                         size_t (*get_slice)(void *data, size_t slice),
                         bool (*has_slice)(void *data, size_t slice)) {
#ifdef TRIE_THREAD_SAFE
  LOCK(trie)
#endif
  trie_node_t *current_node = trie->root;
  trie_node_t *result;
  size_t current_slice;
  int i;

  for (i = 0; has_slice(data, i); i++) {
    current_slice = get_slice(data, i);
    result = trie->store_search(current_node->children, current_slice);

    if (result == NULL) {
#ifdef TRIE_THREAD_SAFE
      UNLOCK(trie)
#endif
      return NULL;
    }
    current_node = result;
  }
  result = current_node->is_terminal ? current_node : NULL;

#ifdef TRIE_THREAD_SAFE
  UNLOCK(trie)
#endif
  return result;
}

void trie_remove_node(trie_t *trie, trie_node_t *node) {
#ifdef TRIE_THREAD_SAFE
  LOCK(trie)
#endif
  trie_node_t *parent;
  if (trie->store_get_size(node->children) == 0) {

    if (node != trie->root) {
      parent = node->parent;
      trie->store_remove(parent->children, node);
      if (trie->store_get_size(parent->children) == 0) {
        trie_remove_node(trie, parent);
      }
    }

  } else {
    node->is_terminal = false;
    node->terminal_data = NULL;
  }
#ifdef TRIE_THREAD_SAFE
  UNLOCK(trie)
#endif
}

void trie_remove(trie_t *trie, trie_node_t *node) {
#ifdef TRIE_THREAD_SAFE
  LOCK(trie)
#endif
  trie_node_t *parent;
  if (node == trie->root) {
#ifdef TRIE_THREAD_SAFE
    UNLOCK(trie)
#endif
    return;
  }

  parent = node->parent;
  trie->store_remove(parent->children, node);
  if (trie->store_get_size(parent->children) == 0) {
    trie_remove_node(trie, parent);
  }
#ifdef TRIE_THREAD_SAFE
  UNLOCK(trie)
#endif
}

void trie_destroy_node(trie_t *trie, trie_node_t *node) {
#ifdef TRIE_THREAD_SAFE
  LOCK(trie)
#endif
  trie_node_t *parent;
  if (trie->store_get_size(node->children) == 0) {
    if (node != trie->root) {
      parent = node->parent;
      trie->store_destroy_entry(parent->children, node);
      if (trie->store_get_size(parent->children) == 0 && !parent->is_terminal) {
        trie_destroy_node(trie, parent);
      }
    }
    trie->store_destroy(node->children);
    free(node);

  } else {
    node->is_terminal = false;
    node->terminal_data = NULL;
  }
#ifdef TRIE_THREAD_SAFE
  UNLOCK(trie)
#endif
}
void trie_destroy_node_nonrec(trie_t *trie, trie_node_t *node) {
#ifdef TRIE_THREAD_SAFE
  LOCK(trie)
#endif
  trie_node_t *parent;
  if (trie->store_get_size(node->children) == 0) {
    trie->store_destroy(node->children);
    free(node);
  } else {
    node->is_terminal = false;
    node->terminal_data = NULL;
  }
#ifdef TRIE_THREAD_SAFE
  UNLOCK(trie)
#endif
}
void trie_destroy(trie_t *trie, trie_node_t *node) {
#ifdef TRIE_THREAD_SAFE
  LOCK(trie)
#endif
  trie_node_t *parent;
  parent = node->parent;
  trie->store_apply(trie, node->children, trie_destroy_node_nonrec);
  if (node != trie->root) {
    trie->store_destroy_entry(parent->children, node);
    if (trie->store_get_size(parent->children) == 0) {
      trie_destroy_node(trie, parent);
    }
  }
#ifdef TRIE_THREAD_SAFE
  UNLOCK(trie)
#endif
}
void trie_destroy_tree(trie_t *trie) {
#ifdef TRIE_THREAD_SAFE
  LOCK(trie)
#endif
  trie_destroy(trie, trie->root);
#ifdef TRIE_THREAD_SAFE
  UNLOCK(trie)
#endif
}
