#ifndef DS_P_TREE_H
#define DS_P_TREE_H

/*
 * ds_p_tree_t, Jagadish's P-tree (polyhedral tree): a dynamic R-tree-like
 * spatial access method for boxes in d <= 8 dimensions whose nodes are
 * bounded not by boxes but by polytopes with a fixed set of k orientations
 * (k-DOPs): for each direction u_j the node keeps the interval of u_j.x
 * over everything below it. Directions beyond the axes (by default the
 * axes plus the pairwise diagonals) give tighter bounds for skewed data;
 * with only the axes it is an R-tree.
 *
 * Insertion follows the least enlargement of the summed interval widths
 * with a quadratic split; removal condenses underfull nodes by reinserting
 * their objects. Queries prune by the slabs (a box misses a node when some
 * direction separates them). Every object carries a size_t value. Memory
 * comes from the ds_context_t.
 */

#include "context.h"
#include "status.h"
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define DS_P_TREE_MAX_DIM 8
#define DS_P_TREE_MAX_DIRS 64

typedef bool (*ds_p_tree_visit_func_t)(const double *box, size_t value,
                                       void *user);

typedef struct ds_p_tree {
  size_t dim;
  size_t k;     /* directions */
  double *dirs; /* k x d */
  size_t max_entries;
  size_t min_entries;
  /* nodes */
  size_t *count;
  unsigned char *leaf;
  size_t *parent;
  size_t nodes;
  size_t capacity;
  size_t free_list;
  /* entries: max_entries + 1 per node */
  double *slab; /* 2k per entry: low[k] then high[k] */
  double *box;  /* 2d per entry (objects) */
  size_t *ref;  /* child or value */
  size_t root;
  size_t height;
  size_t size;
  ds_context_t *context;
} ds_p_tree_t;

/* dirs: k x d directions (NULL: the axes and the pairwise diagonals,
 * k = d^2); max_entries >= 4 (0: 8). */
ds_status_t ds_p_tree_init(ds_p_tree_t *t, size_t dim, const double *dirs,
                           size_t k, size_t max_entries, ds_context_t *context);
void ds_p_tree_destroy(ds_p_tree_t *t);
size_t ds_p_tree_size(const ds_p_tree_t *t);
size_t ds_p_tree_height(const ds_p_tree_t *t);
size_t ds_p_tree_directions(const ds_p_tree_t *t);
ds_status_t ds_p_tree_insert(ds_p_tree_t *t, const double *box, size_t value);
ds_status_t ds_p_tree_remove(ds_p_tree_t *t, const double *box, size_t value);
/* Boxes meeting the closed query box. */
ds_status_t ds_p_tree_query(const ds_p_tree_t *t, const double *box,
                            ds_p_tree_visit_func_t visit, void *user);
/* Slabs exact, levels even, fill legal, count right. */
bool ds_p_tree_valid(const ds_p_tree_t *t);

#ifdef __cplusplus
}
#endif

#endif /* DS_P_TREE_H */
