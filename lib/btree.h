#ifndef DS_BTREE_H
#define DS_BTREE_H

#include "common.h"
#include "status.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct btree_node {
  struct btree_internal_node *internal;
} ds_btree_node_t;

typedef struct btree_internal_node {
  int num_keys;
  bool is_leaf;
  struct btree_internal_node **children;
  struct btree_internal_node *parent;
  ds_btree_node_t **data;
} ds_btree_internal_node_t;

typedef struct btree {
  int degree;
  ds_btree_internal_node_t *root;
  ds_btree_node_t *nil;
  size_t size;
  void *(*allocator)(size_t);
  void (*deallocator)(void *);
} ds_btree_t;

/* NULL when memory runs out */
ds_btree_internal_node_t *ds_btree_create_node(ds_btree_t *tree, bool is_leaf);
/* NULL for a degree below 2 (the minimum degree t: nodes hold t - 1 to
 * 2t - 1 keys), one too large to size the nodes, when memory runs out, or
 * without an allocator and a deallocator */
ds_btree_t *ds_btree_create(int degree);
ds_btree_t *ds_btree_create_alloc(int degree, void *(*allocator)(size_t),
                                  void (*deallocator)(void *));
void ds_btree_split_child(ds_btree_t *tree, ds_btree_internal_node_t *parent,
                          int index);
void ds_btree_insert_non_full(ds_btree_t *tree, ds_btree_internal_node_t *node,
                              ds_btree_node_t *data,
                              int (*compare)(ds_btree_node_t *,
                                             ds_btree_node_t *));
void ds_btree_insert(ds_btree_t *tree, ds_btree_node_t *data,
                     int (*compare)(ds_btree_node_t *, ds_btree_node_t *));
ds_btree_node_t *
ds_btree_search_node(ds_btree_t *tree, ds_btree_internal_node_t *node,
                     ds_btree_node_t *key,
                     int (*compare)(ds_btree_node_t *, ds_btree_node_t *));
ds_btree_node_t *ds_btree_search(ds_btree_t *tree, ds_btree_node_t *key,
                                 int (*compare)(ds_btree_node_t *,
                                                ds_btree_node_t *));
void *ds_btree_local_minimum(ds_btree_t *tree, ds_btree_internal_node_t *node);
void *ds_btree_local_maximum(ds_btree_t *tree, ds_btree_internal_node_t *node);
void *ds_btree_minimum(ds_btree_t *tree);
void *ds_btree_maximum(ds_btree_t *tree);
void ds_btree_rebalance(ds_btree_t *tree, ds_btree_internal_node_t *node);
void ds_btree_delete_node(ds_btree_t *tree, ds_btree_node_t *key);
void ds_btree_destroy_node(ds_btree_t *tree, ds_btree_node_t *node,
                           void (*destroy)(ds_btree_node_t *));
void ds_btree_destroy_recursive(ds_btree_t *tree,
                                ds_btree_internal_node_t *node,
                                void (*destroy)(ds_btree_node_t *));
void ds_btree_destroy_tree(ds_btree_t *tree,
                           void (*destroy)(ds_btree_node_t *));
void ds_btree_inorder_walk_helper(ds_btree_t *tree,
                                  ds_btree_internal_node_t *node,
                                  void (*callback)(ds_btree_node_t *));
void ds_btree_inorder_walk(ds_btree_t *tree, ds_btree_internal_node_t *node,
                           void (*callback)(ds_btree_node_t *));
void ds_btree_inorder_walk_tree(ds_btree_t *tree,
                                void (*callback)(ds_btree_node_t *));
void ds_btree_preorder_walk_helper(ds_btree_t *tree,
                                   ds_btree_internal_node_t *node,
                                   void (*callback)(ds_btree_node_t *));
void ds_btree_preorder_walk(ds_btree_t *tree, ds_btree_internal_node_t *node,
                            void (*callback)(ds_btree_node_t *));
void ds_btree_preorder_walk_tree(ds_btree_t *tree,
                                 void (*callback)(ds_btree_node_t *));
void ds_btree_postorder_walk_helper(ds_btree_t *tree,
                                    ds_btree_internal_node_t *node,
                                    void (*callback)(ds_btree_node_t *));
void ds_btree_postorder_walk(ds_btree_t *tree, ds_btree_internal_node_t *node,
                             void (*callback)(ds_btree_node_t *));
void ds_btree_postorder_walk_tree(ds_btree_t *tree,
                                  void (*callback)(ds_btree_node_t *));
ds_btree_internal_node_t *
ds_btree_clone_recursive(ds_btree_t *tree, ds_btree_t *new_tree,
                         ds_btree_internal_node_t *node,
                         ds_btree_node_t *(*clone_node)(ds_btree_node_t *));
/* Legacy clone: NULL when the copy cannot be allocated, or when clone_node
 * returns NULL; the clones it made before are then not released (clone_ex
 * releases them). */
ds_btree_t *ds_btree_clone(ds_btree_t *tree,
                           ds_btree_node_t *(*clone_node)(ds_btree_node_t *));
/* Strong failure guarantee: NULL from clone_node rolls back the clones
 * made so far through destroy_clone and returns DS_ERR_CALLBACK;
 * DS_ERR_ALLOC when memory runs out (the clones are rolled back too). All
 * callbacks are required. On any error *out is NULL and the source is
 * unchanged. */
ds_status_t ds_btree_clone_ex(const ds_btree_t *tree,
                              ds_btree_node_t *(*clone_node)(ds_btree_node_t *),
                              void (*destroy_clone)(ds_btree_node_t *),
                              ds_btree_t **out);

#ifdef __cplusplus
}
#endif

#endif /* DS_BTREE_H */
