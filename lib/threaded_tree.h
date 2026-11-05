#ifndef DS_THREADED_TREE_H
#define DS_THREADED_TREE_H

/*
 * Threaded binary search trees: the null links of a binary tree are
 * replaced by threads to the in-order predecessor (left) and successor
 * (right), so in-order traversal needs neither a stack nor parent links.
 *
 * ds_threaded_tree_t stores elements by value in node arrays with a tag
 * bit per link. Two modes:
 *   DS_THREAD_BOTH   fully threaded (threaded binary tree): O(1) amortized
 *                    successor and predecessor
 *   DS_THREAD_RIGHT  right-threaded: only successor threads; predecessors
 *                    are found by descending from the root
 * Insertion, deletion (Knuth's cases, threads repaired), search, first and
 * last, successor and predecessor stepping.
 *
 * ds_cupif_giannini_traverse visits any binary tree given as left/right
 * index arrays in pre-, in- or post-order with O(1) extra space and no
 * stack, by threading the null right links temporarily (the
 * Cupif-Giannini / Morris family of traversals); the arrays are restored
 * before returning, also when the visitor stops early.
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

#define DS_THREADED_NONE (~(size_t)0)

typedef int (*ds_threaded_compare_func_t)(const void *a, const void *b,
                                          void *user);

typedef enum ds_thread_mode {
  DS_THREAD_BOTH = 0,
  DS_THREAD_RIGHT
} ds_thread_mode_t;

typedef struct ds_threaded_tree {
  unsigned char *data;
  size_t *left;
  size_t *right;
  unsigned char *lthread; /* left link is a thread */
  unsigned char *rthread;
  size_t root;
  size_t size;
  size_t slots;
  size_t capacity;
  size_t free_list;
  size_t elem_size;
  ds_thread_mode_t mode;
  ds_threaded_compare_func_t compare;
  void *user;
  ds_context_t *context;
} ds_threaded_tree_t;

ds_status_t ds_threaded_tree_init(ds_threaded_tree_t *t, size_t elem_size,
                                  ds_threaded_compare_func_t compare,
                                  void *user, ds_thread_mode_t mode,
                                  ds_context_t *context);
void ds_threaded_tree_destroy(ds_threaded_tree_t *t);
size_t ds_threaded_tree_size(const ds_threaded_tree_t *t);
/* DS_EXISTS for a duplicate key. */
ds_status_t ds_threaded_tree_insert(ds_threaded_tree_t *t, const void *elem);
/* Node handle of key (handles change when a two-child node is deleted). */
ds_status_t ds_threaded_tree_find(const ds_threaded_tree_t *t, const void *key,
                                  size_t *out_node);
ds_status_t ds_threaded_tree_remove(ds_threaded_tree_t *t, const void *key,
                                    void *out);
size_t ds_threaded_tree_first(const ds_threaded_tree_t *t);
size_t ds_threaded_tree_last(const ds_threaded_tree_t *t);
size_t ds_threaded_tree_next(const ds_threaded_tree_t *t, size_t node);
size_t ds_threaded_tree_prev(const ds_threaded_tree_t *t, size_t node);
const void *ds_threaded_tree_get(const ds_threaded_tree_t *t, size_t node);

typedef enum ds_traversal_order {
  DS_TRAVERSE_PREORDER = 0,
  DS_TRAVERSE_INORDER,
  DS_TRAVERSE_POSTORDER
} ds_traversal_order_t;

/* left/right: child indices or DS_THREADED_NONE. Returns DS_STOP when the
 * visitor returned false. */
ds_status_t ds_cupif_giannini_traverse(size_t *left, size_t *right, size_t root,
                                       ds_traversal_order_t order,
                                       bool (*visit)(size_t node, void *user),
                                       void *user);

#ifdef __cplusplus
}
#endif

#endif /* DS_THREADED_TREE_H */
