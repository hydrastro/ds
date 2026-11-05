#ifndef DS_SCAPEGOAT_TREE_H
#define DS_SCAPEGOAT_TREE_H

/*
 * Scapegoat trees (Galperin and Rivest; Andersson's general balanced
 * trees): binary search trees with no per-node balance data. An insertion
 * deeper than log_{1/alpha}(n) walks back up to the first ancestor that
 * is not alpha-weight-balanced (the scapegoat) and rebuilds its subtree
 * perfectly balanced; deletions rebuild the whole tree once the size has
 * fallen below alpha times its maximum. Amortized O(log n) updates,
 * worst-case O(log n) searches.
 *
 * alpha = alpha_num / alpha_den in (1/2, 1) is pluggable (default 2/3):
 * smaller alpha means shallower trees and more rebuilding. Elements are
 * stored by value in node arrays.
 *
 * Concurrency: a tree may be used by one thread at a time.
 */

#include "context.h"
#include "status.h"
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define DS_SCAPEGOAT_NONE (~(size_t)0)

typedef int (*ds_scapegoat_compare_func_t)(const void *a, const void *b,
                                           void *user);

typedef struct ds_scapegoat_tree {
  unsigned char *data;
  size_t *left;
  size_t *right;
  size_t *path;  /* scratch: insertion path, traversal stack */
  size_t *order; /* scratch: flattened subtree */
  size_t root;
  size_t size;
  size_t max_size;
  size_t slots;
  size_t capacity;
  size_t free_list;
  size_t elem_size;
  size_t alpha_num;
  size_t alpha_den;
  size_t rebuilds;
  ds_scapegoat_compare_func_t compare;
  void *user;
  ds_context_t *context;
} ds_scapegoat_tree_t;

/* alpha_num = alpha_den = 0 for 2/3; otherwise 1/2 < alpha < 1. */
ds_status_t ds_scapegoat_tree_init(ds_scapegoat_tree_t *t, size_t elem_size,
                                   ds_scapegoat_compare_func_t compare,
                                   void *user, size_t alpha_num,
                                   size_t alpha_den, ds_context_t *context);
void ds_scapegoat_tree_destroy(ds_scapegoat_tree_t *t);
size_t ds_scapegoat_tree_size(const ds_scapegoat_tree_t *t);
/* DS_EXISTS for a duplicate key. */
ds_status_t ds_scapegoat_tree_insert(ds_scapegoat_tree_t *t, const void *elem);
ds_status_t ds_scapegoat_tree_remove(ds_scapegoat_tree_t *t, const void *key,
                                     void *out);
ds_status_t ds_scapegoat_tree_find(const ds_scapegoat_tree_t *t,
                                   const void *key, void *out);
size_t ds_scapegoat_tree_height(const ds_scapegoat_tree_t *t);
/* Subtree rebuilds so far. */
size_t ds_scapegoat_tree_rebuilds(const ds_scapegoat_tree_t *t);
void ds_scapegoat_tree_foreach(const ds_scapegoat_tree_t *t,
                               bool (*visit)(const void *elem, void *user),
                               void *user);
/* Order and the height bound log_{1/alpha}(max_size) + 1. */
bool ds_scapegoat_tree_valid(const ds_scapegoat_tree_t *t);

#ifdef __cplusplus
}
#endif

#endif /* DS_SCAPEGOAT_TREE_H */
