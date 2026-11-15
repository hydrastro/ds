#ifndef DS_SPLAY_TREE_H
#define DS_SPLAY_TREE_H

#include "common.h"
#include "status.h"
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
} ds_splay_tree_t;

/* NULL when memory runs out, or without an allocator and a deallocator */
ds_splay_tree_t *ds_splay_tree_create(int (*compare)(ds_splay_tree_node_t *,
                                                     ds_splay_tree_node_t *));
ds_splay_tree_t *ds_splay_tree_create_alloc(
    int (*compare)(ds_splay_tree_node_t *, ds_splay_tree_node_t *),
    void *(*allocator)(size_t), void (*deallocator)(void *));
void ds_splay_tree_insert(ds_splay_tree_t *tree, ds_splay_tree_node_t *node);
ds_splay_tree_node_t *ds_splay_tree_search(ds_splay_tree_t *tree,
                                           ds_splay_tree_node_t *node);
ds_splay_tree_node_t *ds_splay_tree_remove(ds_splay_tree_t *tree,
                                           ds_splay_tree_node_t *node);
ds_splay_tree_node_t *ds_splay_tree_minimum(ds_splay_tree_t *tree,
                                            ds_splay_tree_node_t *node);
ds_splay_tree_node_t *ds_splay_tree_maximum(ds_splay_tree_t *tree,
                                            ds_splay_tree_node_t *node);
bool ds_splay_tree_is_empty(ds_splay_tree_t *tree);
void ds_splay_tree_destroy_node(ds_splay_tree_t *tree,
                                ds_splay_tree_node_t *node,
                                void (*destroy)(ds_splay_tree_node_t *));
void ds_splay_tree_inorder_walk(ds_splay_tree_t *tree,
                                ds_splay_tree_node_t *node,
                                void (*callback)(ds_splay_tree_node_t *));
void ds_splay_tree_inorder_walk_tree(ds_splay_tree_t *tree,
                                     void (*callback)(ds_splay_tree_node_t *));
void ds_splay_tree_destroy(ds_splay_tree_t *tree,
                           void (*destroy)(ds_splay_tree_node_t *));
/* Legacy clone: NULL when the copy cannot be allocated, or when clone_node
 * returns NULL; the clones it made before are then not released (clone_ex
 * releases them). */
ds_splay_tree_t *ds_splay_tree_clone(
    ds_splay_tree_t *tree,
    ds_splay_tree_node_t *(*clone_node)(ds_splay_tree_node_t *));
/* Strong failure guarantee: NULL from clone_node rolls back the clones
 * made so far through destroy_clone and returns DS_ERR_CALLBACK;
 * DS_ERR_ALLOC when the copy itself cannot be allocated. All callbacks
 * are required. On any error *out is NULL and the source is unchanged. */
ds_status_t ds_splay_tree_clone_ex(
    const ds_splay_tree_t *tree,
    ds_splay_tree_node_t *(*clone_node)(ds_splay_tree_node_t *),
    void (*destroy_clone)(ds_splay_tree_node_t *), ds_splay_tree_t **out);

#ifdef __cplusplus
}
#endif

#endif /* DS_SPLAY_TREE_H */
