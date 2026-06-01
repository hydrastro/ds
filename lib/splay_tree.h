#ifndef DS_SPLAY_TREE_H
#define DS_SPLAY_TREE_H

#include "common.h"
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct splay_tree_node {
  struct splay_tree_node *left;
  struct splay_tree_node *right;
  struct splay_tree_node *parent;
} ds_splay_tree_node_t;

typedef struct splay_tree {
  ds_splay_tree_node_t *root;
  ds_splay_tree_node_t *nil;
  size_t size;
  int (*compare)(ds_splay_tree_node_t *, ds_splay_tree_node_t *);
  void *(*allocator)(size_t);
  void (*deallocator)(void *);
#ifdef DS_THREAD_SAFE
  mutex_t lock;
  bool is_thread_safe;
#endif
} ds_splay_tree_t;

ds_splay_tree_t *FUNC(ds_splay_tree_create)(
    int (*compare)(ds_splay_tree_node_t *, ds_splay_tree_node_t *));
ds_splay_tree_t *FUNC(ds_splay_tree_create_alloc)(
    int (*compare)(ds_splay_tree_node_t *, ds_splay_tree_node_t *),
    void *(*allocator)(size_t), void (*deallocator)(void *));
void FUNC(ds_splay_tree_insert)(ds_splay_tree_t *tree,
                                ds_splay_tree_node_t *node);
ds_splay_tree_node_t *FUNC(ds_splay_tree_search)(ds_splay_tree_t *tree,
                                                 ds_splay_tree_node_t *node);
ds_splay_tree_node_t *FUNC(ds_splay_tree_remove)(ds_splay_tree_t *tree,
                                                 ds_splay_tree_node_t *node);
ds_splay_tree_node_t *FUNC(ds_splay_tree_minimum)(ds_splay_tree_t *tree,
                                                  ds_splay_tree_node_t *node);
ds_splay_tree_node_t *FUNC(ds_splay_tree_maximum)(ds_splay_tree_t *tree,
                                                  ds_splay_tree_node_t *node);
bool FUNC(ds_splay_tree_is_empty)(ds_splay_tree_t *tree);
void FUNC(ds_splay_tree_destroy_node)(ds_splay_tree_t *tree,
                                      ds_splay_tree_node_t *node,
                                      void (*destroy)(ds_splay_tree_node_t *));
void FUNC(ds_splay_tree_inorder_walk)(ds_splay_tree_t *tree,
                                      ds_splay_tree_node_t *node,
                                      void (*callback)(ds_splay_tree_node_t *));
void FUNC(ds_splay_tree_inorder_walk_tree)(
    ds_splay_tree_t *tree, void (*callback)(ds_splay_tree_node_t *));
void FUNC(ds_splay_tree_destroy)(ds_splay_tree_t *tree,
                                 void (*destroy)(ds_splay_tree_node_t *));
ds_splay_tree_t *FUNC(ds_splay_tree_clone)(
    ds_splay_tree_t *tree,
    ds_splay_tree_node_t *(*clone_node)(ds_splay_tree_node_t *));

#ifdef __cplusplus
}
#endif

#endif /* DS_SPLAY_TREE_H */
