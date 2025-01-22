#ifndef DS_TRIE_H
#define DS_TRIE_H

#include "common.h"
#include <stdarg.h>
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
  trie_node_t *(*store_search)(void *, size_t);
  void *(*store_create)(size_t);
  void (*store_insert)(void *, trie_node_t *);
  void (*store_remove)(void *, trie_node_t *);
  void (*store_destroy_entry)(void *, trie_node_t *);
  void (*store_destroy)(void *);
  size_t (*store_get_size)(void *);
  void (*store_apply)(struct trie *, void *,
                      void (*)(struct trie *, trie_node_t *, va_list *),
                      va_list *);
  void *(*store_clone)(struct trie *, void *, trie_node_t *);

#ifdef DS_THREAD_SAFE
  mutex_t lock;
  bool is_thread_safe;
#endif
} trie_t;

trie_node_t *FUNC(trie_create_node)(trie_t *trie);
trie_t *FUNC(trie_create)(
    size_t num_splits, trie_node_t *(*store_search)(void *, size_t),
    void *(*store_create)(size_t), void (*store_insert)(void *, trie_node_t *),
    void (*store_remove)(void *, trie_node_t *),
    void (*store_destroy_entry)(void *, trie_node_t *),
    void (*store_destroy)(void *), size_t (*store_get_size)(void *),
    void (*store_apply)(struct trie *, void *,
                        void (*)(struct trie *, trie_node_t *, va_list *),
                        va_list *),
    void *(*store_clone)(struct trie *, void *, trie_node_t *));

void FUNC(trie_insert)(trie_t *trie, void *data, size_t (*get_slice)(void *, size_t),
                 bool (*has_slice)(void *, size_t));
trie_node_t *FUNC(trie_search)(trie_t *trie, void *data,
                         size_t (*get_slice)(void *, size_t),
                         bool (*has_slice)(void *, size_t));
void FUNC(trie_delete_node)(trie_t *trie, trie_node_t *node);
void FUNC(trie_destroy_node)(trie_t *trie, trie_node_t *node,
                       void (*destroy)(trie_t *, trie_node_t *, va_list *));
void FUNC(trie_destroy_callback)(trie_t *trie, trie_node_t *node, va_list *args);
void FUNC(trie_delete_trie)(trie_t *trie);
void FUNC(trie_destroy_trie)(trie_t *trie,
                       void (*destroy)(trie_t *, trie_node_t *, va_list *));
void FUNC(trie_apply)(trie_t *trie, trie_node_t *node,
                void (*f)(trie_t *, trie_node_t *, va_list *), ...);
trie_node_t *FUNC(trie_clone_node)(trie_t *trie, trie_node_t *node,
                             trie_node_t *parent_node,
                             void *(*clone_data)(void *));
trie_t *FUNC(trie_clone)(trie_t *trie, void *(*clone_data)(void *));

#endif /* DS_TRIE_H */
