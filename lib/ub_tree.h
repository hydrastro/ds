#ifndef DS_UB_TREE_H
#define DS_UB_TREE_H

/*
 * The two structures DADS calls "UB-tree".
 *
 * ds_ub_tree_t, Bayer's universal B-tree: a B+-tree over the Z-addresses
 * (bit-interleaved Morton codes) of points with d <= 8 unsigned integer
 * coordinates of up to 32 bits each. Leaves hold points in Z order, so
 * each leaf covers a Z-region of space. Box queries walk the Z-curve
 * between the box corners and jump over the parts outside the box with
 * the Tropf-Herzog BIGMIN computation, seeking the next leaf by key.
 * Exact match, insertion and removal take O(log n); a removal that empties
 * a leaf drops it (leaves may run underfull otherwise).
 *
 * ds_ubranch_tree_t, the unlimited branching tree: a forest of ordered
 * trees holding sorted lists (sets) of size_t; each root is a distinct
 * first element, a list is a path from a root, and lists with a common
 * prefix share nodes (children are ordered, so every path increases).
 * Besides exact lookup it answers subset queries (stored lists contained
 * in a set) and superset queries (stored lists containing a set) by
 * pruned descents.
 *
 * Memory comes from the ds_context_t.
 */

#include "context.h"
#include "status.h"
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define DS_UB_MAX_DIM 8

/* ---- universal B-tree --------------------------------------------------- */

typedef bool (*ds_ub_visit_func_t)(const unsigned long *point, size_t value,
                                   void *user);

typedef struct ds_ub_tree {
  size_t dim;
  size_t bits;  /* per coordinate, 1..32 */
  size_t words; /* 32-bit words per Z-address */
  size_t leaf_cap;
  size_t fanout;
  /* nodes */
  unsigned char *leaf;
  size_t *count;
  size_t *next; /* leaf chain */
  size_t *prev;
  size_t *parent;
  unsigned long *keys; /* per node slot: words (leaf: entry key; inner:
                          separator = first key of child i) */
  unsigned long *pts;  /* leaf slots: d coordinates */
  size_t *ref;         /* leaf: value; inner: child */
  size_t slots;        /* per node: max(leaf_cap, fanout) + 1 */
  size_t nodes;
  size_t capacity;
  size_t free_list;
  size_t root;
  size_t height;
  size_t size;
  ds_context_t *context;
} ds_ub_tree_t;

/* bits per coordinate 1..32 (0: 32); leaf_cap >= 2 (0: 32); fanout >= 3
 * (0: 32). */
ds_status_t ds_ub_tree_init(ds_ub_tree_t *t, size_t dim, size_t bits,
                            size_t leaf_cap, size_t fanout,
                            ds_context_t *context);
void ds_ub_tree_destroy(ds_ub_tree_t *t);
size_t ds_ub_tree_size(const ds_ub_tree_t *t);
size_t ds_ub_tree_height(const ds_ub_tree_t *t);
ds_status_t ds_ub_tree_insert(ds_ub_tree_t *t, const unsigned long *point,
                              size_t value);
ds_status_t ds_ub_tree_remove(ds_ub_tree_t *t, const unsigned long *point,
                              size_t value);
/* first value stored at the point */
ds_status_t ds_ub_tree_find(const ds_ub_tree_t *t, const unsigned long *point,
                            size_t *out_value);
/* points in the closed box lo..hi (2d coordinates) in Z order */
ds_status_t ds_ub_tree_query(const ds_ub_tree_t *t, const unsigned long *box,
                             ds_ub_visit_func_t visit, void *user);
/* the Z-address (words of 32 bits, most significant first) of a point */
void ds_ub_tree_zaddress(const ds_ub_tree_t *t, const unsigned long *point,
                         unsigned long *out);
bool ds_ub_tree_valid(const ds_ub_tree_t *t);

/* ---- unlimited branching tree ----------------------------------------- */

typedef bool (*ds_ubranch_visit_func_t)(const size_t *list, size_t n,
                                        size_t value, void *user);

typedef struct ds_ubranch_tree {
  /* nodes: element, ordered children */
  size_t *elem;
  size_t *parent;
  size_t **child;
  size_t *child_count;
  size_t *child_capacity;
  unsigned char *terminal; /* a list ends here */
  size_t *value;
  size_t nodes;
  size_t capacity;
  size_t free_list;
  size_t *roots; /* ordered by element */
  size_t root_count;
  size_t root_capacity;
  size_t size;
  size_t *path; /* scratch for reporting lists */
  size_t path_capacity;
  ds_context_t *context;
} ds_ubranch_tree_t;

ds_status_t ds_ubranch_tree_init(ds_ubranch_tree_t *t, ds_context_t *context);
void ds_ubranch_tree_destroy(ds_ubranch_tree_t *t);
size_t ds_ubranch_tree_size(const ds_ubranch_tree_t *t);
size_t ds_ubranch_tree_nodes(const ds_ubranch_tree_t *t);
/* list: n >= 1 strictly increasing elements. DS_EXISTS if present (the
 * value is replaced). */
ds_status_t ds_ubranch_tree_insert(ds_ubranch_tree_t *t, const size_t *list,
                                   size_t n, size_t value);
ds_status_t ds_ubranch_tree_remove(ds_ubranch_tree_t *t, const size_t *list,
                                   size_t n);
ds_status_t ds_ubranch_tree_find(const ds_ubranch_tree_t *t, const size_t *list,
                                 size_t n, size_t *out_value);
/* stored lists that are subsets of the strictly increasing set */
ds_status_t ds_ubranch_tree_subsets(ds_ubranch_tree_t *t, const size_t *set,
                                    size_t n, ds_ubranch_visit_func_t visit,
                                    void *user);
/* stored lists that contain every element of the set */
ds_status_t ds_ubranch_tree_supersets(ds_ubranch_tree_t *t, const size_t *set,
                                      size_t n, ds_ubranch_visit_func_t visit,
                                      void *user);

#ifdef __cplusplus
}
#endif

#endif /* DS_UB_TREE_H */
