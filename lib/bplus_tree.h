#ifndef DS_BPLUS_TREE_H
#define DS_BPLUS_TREE_H

#include "common.h"
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct bplus_tree_node {
  struct bplus_tree_internal_node *internal;
} ds_bplus_tree_node_t;

typedef struct bplus_tree_internal_node {
  int num_keys;
  bool is_leaf;
  /* routing keys (separators); for leaves these are the stored records */
  ds_bplus_tree_node_t **keys;
  /* children pointers (internal nodes only) */
  struct bplus_tree_internal_node **children;
  struct bplus_tree_internal_node *parent;
  /* leaf chain links (leaves only) */
  struct bplus_tree_internal_node *next;
  struct bplus_tree_internal_node *prev;
} ds_bplus_tree_internal_node_t;

typedef struct bplus_tree {
  int order;
  bool bstar;
  ds_bplus_tree_internal_node_t *root;
  ds_bplus_tree_internal_node_t *head; /* first (leftmost) leaf */
  ds_bplus_tree_node_t *nil;
  size_t size;
  int (*compare)(ds_bplus_tree_node_t *, ds_bplus_tree_node_t *);
  void *(*allocator)(size_t);
  void (*deallocator)(void *);
} ds_bplus_tree_t;

ds_bplus_tree_t *ds_bplus_tree_create(int order,
                                      int (*compare)(ds_bplus_tree_node_t *,
                                                     ds_bplus_tree_node_t *));
ds_bplus_tree_t *ds_bplus_tree_create_alloc(
    int order, bool bstar,
    int (*compare)(ds_bplus_tree_node_t *, ds_bplus_tree_node_t *),
    void *(*allocator)(size_t), void (*deallocator)(void *));
bool ds_bplus_tree_insert(ds_bplus_tree_t *tree, ds_bplus_tree_node_t *data);
ds_bplus_tree_node_t *ds_bplus_tree_search(ds_bplus_tree_t *tree,
                                           ds_bplus_tree_node_t *key);
bool ds_bplus_tree_remove(ds_bplus_tree_t *tree, ds_bplus_tree_node_t *key,
                          ds_bplus_tree_node_t **out_data);
ds_bplus_tree_node_t *ds_bplus_tree_minimum(ds_bplus_tree_t *tree);
ds_bplus_tree_node_t *ds_bplus_tree_maximum(ds_bplus_tree_t *tree);
bool ds_bplus_tree_is_empty(ds_bplus_tree_t *tree);
size_t ds_bplus_tree_size(ds_bplus_tree_t *tree);
void ds_bplus_tree_walk_forward(ds_bplus_tree_t *tree,
                                void (*callback)(ds_bplus_tree_node_t *));
void ds_bplus_tree_range_walk(ds_bplus_tree_t *tree, ds_bplus_tree_node_t *low,
                              ds_bplus_tree_node_t *high,
                              void (*callback)(ds_bplus_tree_node_t *));
void ds_bplus_tree_destroy(ds_bplus_tree_t *tree,
                           void (*destroy)(ds_bplus_tree_node_t *));
ds_bplus_tree_t *ds_bplus_tree_clone(
    ds_bplus_tree_t *tree,
    ds_bplus_tree_node_t *(*clone_node)(ds_bplus_tree_node_t *));

#ifdef __cplusplus
}
#endif

#endif /* DS_BPLUS_TREE_H */
