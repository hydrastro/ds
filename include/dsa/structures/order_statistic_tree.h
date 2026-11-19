#ifndef DS_ORDER_STATISTIC_TREE_H
#define DS_ORDER_STATISTIC_TREE_H

#include <dsa/core/common.h>
#include <dsa/core/context.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Order-statistic tree: an intrusive, AVL-balanced binary search tree
 * augmented with subtree sizes, giving O(log n) select(k) (k-th smallest) and
 * rank(x) (number of keys strictly less than x) in addition to the usual
 * ordered-set operations. Users embed ds_ost_node_t as the first member of
 * their struct and recover the container with CAST().
 */

typedef struct ost_node {
  struct ost_node *left;
  struct ost_node *right;
  struct ost_node *parent;
  size_t size; /* number of nodes in this subtree (including self) */
  int height;  /* AVL height */
} ds_ost_node_t;

typedef int (*ds_ost_compare_fn)(ds_ost_node_t *, ds_ost_node_t *, void *);

typedef struct ost {
  ds_ost_node_t *root;
  ds_ost_node_t *nil;
  size_t size;
  int (*compare)(ds_ost_node_t *, ds_ost_node_t *);
  ds_context_t *context; /* borrowed allocation owner */
  ds_ost_compare_fn compare_with;
  void *compare_user;
} ds_ost_t;

/* NULL when memory runs out */

ds_ost_t *ds_ost_create(int (*compare)(ds_ost_node_t *, ds_ost_node_t *),
                        ds_context_t *context);
bool ds_ost_insert(ds_ost_t *tree, ds_ost_node_t *node);
ds_ost_node_t *ds_ost_search(ds_ost_t *tree, ds_ost_node_t *key);
bool ds_ost_remove(ds_ost_t *tree, ds_ost_node_t *key,
                   ds_ost_node_t **out_node);
/* select(k): k-th smallest (0-based). Returns nil if k >= size. */
ds_ost_node_t *ds_ost_select(ds_ost_t *tree, size_t k);
/* rank(x): number of keys strictly less than x. */
size_t ds_ost_rank(ds_ost_t *tree, ds_ost_node_t *key);
ds_ost_node_t *ds_ost_minimum(ds_ost_t *tree);
ds_ost_node_t *ds_ost_maximum(ds_ost_t *tree);
size_t ds_ost_size(ds_ost_t *tree);
bool ds_ost_is_empty(ds_ost_t *tree);
void ds_ost_inorder_walk(ds_ost_t *tree, void (*callback)(ds_ost_node_t *));
void ds_ost_destroy(ds_ost_t *tree, void (*destroy)(ds_ost_node_t *));

/* Context owns internal allocations and must outlive the container; NULL
 * selects the default. Comparison/user state must remain stable and alive.
 * Output is NULL on failure. Allocator-pair constructors use the supplied
 * allocation functions. */
ds_status_t ds_ost_create_context(ds_ost_compare_fn compare, void *user,
                                  ds_context_t *context, ds_ost_t **out);

typedef ds_status_t (*ds_ost_visit_fn)(ds_ost_node_t *, void *);
typedef ds_status_t (*ds_ost_clone_fn)(ds_ost_node_t *, ds_ost_node_t **,
                                       void *);
typedef void (*ds_ost_destroy_fn)(ds_ost_node_t *, void *);
/* Checked access preserves out on absence/error. Splay lookup/insertion can
 * change shape even on absence/DS_EXISTS. Insertions require detached records.
 * Walk propagates the first non-DS_OK visitor status. Callbacks may not mutate
 * or destroy the container or retain borrowed views. */
ds_status_t ds_ost_insert_ex(ds_ost_t *container, ds_ost_node_t *node);
ds_status_t ds_ost_find_ex(ds_ost_t *container, ds_ost_node_t *key,
                           ds_ost_node_t **out);
ds_status_t ds_ost_remove_ex(ds_ost_t *container, ds_ost_node_t *key,
                             ds_ost_node_t **out);

ds_status_t ds_ost_walk_with(const ds_ost_t *container, ds_ost_visit_fn visit,
                             void *user);
/* Clone copies records and the comparison policy, not necessarily the shape.
 * NULL context inherits storage ownership. Each successful callback returns a
 * fresh, equivalent record. Failure leaves out NULL, rolls back completed
 * clones, and leaves the unfinished allocation with its callback. Negative
 * errors propagate; positive clone results map to DS_ERR_CALLBACK. Destroy
 * may free payloads but must not inspect or mutate the container. */
ds_status_t ds_ost_clone_with(const ds_ost_t *container, ds_ost_clone_fn clone,
                              ds_ost_destroy_fn destroy, void *user,
                              ds_context_t *context, ds_ost_t **out);
void ds_ost_destroy_with(ds_ost_t *container, ds_ost_destroy_fn destroy,
                         void *user);

/* Select reports an out-of-range ordinal; rank accepts absent keys too. */
ds_status_t ds_ost_select_ex(ds_ost_t *tree, size_t rank, ds_ost_node_t **out);
ds_status_t ds_ost_rank_ex(ds_ost_t *tree, ds_ost_node_t *key, size_t *out);

#ifdef __cplusplus
}
#endif

#endif /* DS_ORDER_STATISTIC_TREE_H */
