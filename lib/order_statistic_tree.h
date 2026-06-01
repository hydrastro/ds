#ifndef DS_ORDER_STATISTIC_TREE_H
#define DS_ORDER_STATISTIC_TREE_H

#include "common.h"
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Order-statistic tree: an intrusive, AVL-balanced binary search tree augmented
 * with subtree sizes, giving O(log n) select(k) (k-th smallest) and rank(x)
 * (number of keys strictly less than x) in addition to the usual ordered-set
 * operations. Users embed ds_ost_node_t as the first member of their struct and
 * recover the container with CAST().
 */

typedef struct ost_node {
  struct ost_node *left;
  struct ost_node *right;
  struct ost_node *parent;
  size_t size; /* number of nodes in this subtree (including self) */
  int height;  /* AVL height */
} ds_ost_node_t;

typedef struct ost {
  ds_ost_node_t *root;
  ds_ost_node_t *nil;
  size_t size;
  int (*compare)(ds_ost_node_t *, ds_ost_node_t *);
  void *(*allocator)(size_t);
  void (*deallocator)(void *);
#ifdef DS_THREAD_SAFE
  mutex_t lock;
  bool is_thread_safe;
#endif
} ds_ost_t;

ds_ost_t *FUNC(ds_ost_create)(int (*compare)(ds_ost_node_t *, ds_ost_node_t *));
ds_ost_t *FUNC(ds_ost_create_alloc)(int (*compare)(ds_ost_node_t *,
                                                   ds_ost_node_t *),
                                    void *(*allocator)(size_t),
                                    void (*deallocator)(void *));
bool FUNC(ds_ost_insert)(ds_ost_t *tree, ds_ost_node_t *node);
ds_ost_node_t *FUNC(ds_ost_search)(ds_ost_t *tree, ds_ost_node_t *key);
bool FUNC(ds_ost_remove)(ds_ost_t *tree, ds_ost_node_t *key,
                         ds_ost_node_t **out_node);
/* select(k): k-th smallest (0-based). Returns nil if k >= size. */
ds_ost_node_t *FUNC(ds_ost_select)(ds_ost_t *tree, size_t k);
/* rank(x): number of keys strictly less than x. */
size_t FUNC(ds_ost_rank)(ds_ost_t *tree, ds_ost_node_t *key);
ds_ost_node_t *FUNC(ds_ost_minimum)(ds_ost_t *tree);
ds_ost_node_t *FUNC(ds_ost_maximum)(ds_ost_t *tree);
size_t FUNC(ds_ost_size)(ds_ost_t *tree);
bool FUNC(ds_ost_is_empty)(ds_ost_t *tree);
void FUNC(ds_ost_inorder_walk)(ds_ost_t *tree,
                               void (*callback)(ds_ost_node_t *));
void FUNC(ds_ost_destroy)(ds_ost_t *tree, void (*destroy)(ds_ost_node_t *));

#ifdef __cplusplus
}
#endif

#endif /* DS_ORDER_STATISTIC_TREE_H */
