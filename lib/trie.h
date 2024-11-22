#ifndef DS_TRIE_H
#define DS_TRIE_H

#include "common.h"
#include <stdbool.h>
#include <stdlib.h>

typedef struct trie_node {
  size_t data_slice;
  struct trie_node *parent;
  void *children;
  bool is_terminal;
  void *terminal_data;
} trie_node_t;

typedef struct trie {
  trie_node_t *root;
  size_t num_splits;

  trie_node_t *(*store_search)(void *store, size_t slice);
  void *(*store_create)(size_t size);

  void (*store_insert)(void *store, trie_node_t *node);
  void (*store_remove)(void *store, trie_node_t *node);
  void (*store_destroy_entry)(void *store, trie_node_t *node);
  void (*store_destroy)(void *store);
  size_t (*store_get_size)(void *store);
  void (*store_apply)(struct trie *, void *store,

                      void (*f)(struct trie *, trie_node_t *));

#ifdef TRIE_THREAD_SAFE
  mutex_t lock;
  bool is_thread_safe;
#endif
} trie_t;

trie_node_t *trie_create_node(trie_t *trie);
trie_t *trie_create(
    size_t num_splits, trie_node_t *(*store_search)(void *store, size_t slice),
    void *(*store_create)(size_t size),
    void (*store_insert)(void *store, trie_node_t *node),
    void (*store_remove)(void *store, trie_node_t *node),
    void (*store_destroy_entry)(void *store, trie_node_t *node),
    void (*store_destroy)(void *store), size_t (*store_get_size)(void *store),
    void (*store_apply)(struct trie *, void *store,
                        void (*f)(struct trie *, trie_node_t *)));

void trie_insert(trie_t *trie, void *data,
                 size_t (*get_slice)(void *data, size_t slice),
                 bool (*has_slice)(void *data, size_t slice));
trie_node_t *trie_search(trie_t *trie, void *data,
                         size_t (*get_slice)(void *data, size_t slice),
                         bool (*has_slice)(void *data, size_t slice));
void trie_remove_node(trie_t *trie, trie_node_t *node);
void trie_remove(trie_t *trie, trie_node_t *node);
void trie_destroy_node(trie_t *trie, trie_node_t *node);
void trie_destroy_node_nonrec(trie_t *trie, trie_node_t *node);
void trie_destroy(trie_t *trie, trie_node_t *node);
void trie_destroy_tree(trie_t *trie);

#endif // DS_TRIE_H
