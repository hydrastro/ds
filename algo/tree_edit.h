#ifndef DS_TREE_EDIT_H
#define DS_TREE_EDIT_H

/*
 * Tree and forest edit distance (Zhang & Shasha 1989) for ordered
 * labelled trees: the cheapest sequence of node deletions (children move
 * up to the parent), insertions and relabellings turning one forest into
 * another. O(n1 n2 min(depth1, leaves1) min(depth2, leaves2)) time,
 * O(n1 n2) space.
 *
 * A forest is given in preorder: degree[i] children follow node i (one
 * tree, or several consecutive trees: the tree editing problem is the
 * one-tree case of the forest editing problem). Labels are stored by
 * value, elem_size bytes per node.
 *
 * Costs come from callbacks with a user pointer; NULL callbacks mean unit
 * costs, with relabelling free when the labels are byte-equal. Costs must
 * be finite (NaN or infinite costs give unspecified results). The
 * recurrence minimizes over all edit mappings, so any finite costs give
 * the cheapest mapping, but the result is a metric only for nonnegative
 * costs with free identity relabels and symmetric insert/delete. The
 * optimal mapping (which nodes of the first forest are kept, and as
 * which node of the second) is recovered by recomputing the subforest
 * tables along the optimal decomposition, without recursion; branches are
 * matched with a relative tolerance of 1e-9, so when two decompositions
 * differ by less, either may be returned.
 *
 * Work tables: two (n1 + 2) x (n2 + 2) arrays of doubles and O(n1 + n2)
 * words; every size is overflow-checked (DS_ERR_OVERFLOW).
 */

#include "context.h"
#include "status.h"
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define DS_TREE_EDIT_NONE (~(size_t)0)

typedef struct ds_tree_edit_costs {
  double (*remove)(const void *label, void *user);
  double (*insert)(const void *label, void *user);
  double (*relabel)(const void *from, const void *to, void *user);
  void *user;
} ds_tree_edit_costs_t;

typedef struct ds_tree_edit_forest {
  size_t nodes;
  const size_t *degree; /* preorder */
  const void *labels;   /* nodes * elem_size bytes, preorder */
} ds_tree_edit_forest_t;

/* costs may be NULL. map (optional, a.nodes entries, preorder indices):
 * map[i] = the node of b that node i of a becomes, or DS_TREE_EDIT_NONE
 * if it is deleted (nodes of b not in the image are inserted).
 * DS_ERR_INVALID if a degree sequence is not a forest. */
ds_status_t ds_tree_edit_distance(const ds_tree_edit_forest_t *a,
                                  const ds_tree_edit_forest_t *b,
                                  size_t elem_size,
                                  const ds_tree_edit_costs_t *costs,
                                  double *out_distance, size_t *map,
                                  ds_context_t *context);

#ifdef __cplusplus
}
#endif

#endif /* DS_TREE_EDIT_H */
